/*
 *
 * Copyright (c) 2020 The Raptor Authors. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "src/linux/tcp_container.h"
#include <string.h>

#include "raptor-lite/utils/mpscq.h"
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/time.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/event.h"
#include "src/common/cid.h"
#include "src/common/endpoint_impl.h"
#include "src/common/resolve_address.h"
#include "src/common/socket_util.h"
#include "src/linux/tcp_listener.h"
#include "src/linux/socket_setting.h"

namespace raptor {
enum TcpMessageType {
    kRecvAMessage,
    kCloseEvent,
    kHeartbeatEvent,
};

struct TcpMessageNode {
    MultiProducerSingleConsumerQueue::Node node;
    TcpMessageType type;
    Endpoint ep;
    Slice slice;
    Event event;
    TcpMessageNode(const Endpoint &o)
        : ep(o) {}

    TcpMessageNode(const Endpoint &o, const Event &ev)
        : ep(o)
        , event(ev) {}
};

constexpr uint32_t InvalidIndex = static_cast<uint32_t>(-1);

TcpContainer::TcpContainer(TcpContainer::Option *option)
    : _shutdown(true) {
    memcpy(&_option, option, sizeof(_option));
}

TcpContainer::~TcpContainer() {
    if (!_shutdown) {
        Shutdown();
    }
}

raptor_error TcpContainer::Init() {
    if (!_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("tcp server already running");

    _epoll_thread = std::make_shared<EpollThread>(this);
    raptor_error e = _epoll_thread->Init(_option.recv_send_threads);
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }

    _epoll_thread->EnableTimeoutCheck(!_option.not_check_connection_timeout);
    if (_option.heartbeat_handler && _option.heartbeat_handler->GetHeartbeatInterval() > 0) {
        _timer_thread = std::make_shared<Timer>(this);
        _timer_thread->Init();
    }

    _shutdown = false;
    _count.Store(0);

    _running_threads = 0;
    _mq_threads = new Thread[_option.mq_consumer_threads];
    for (size_t i = 0; i < _option.mq_consumer_threads; i++) {
        bool success = false;
        _mq_threads[i] =
            Thread("message_queue",
                   std::bind(&TcpContainer::MessageQueueThread, this, std::placeholders::_1),
                   nullptr, &success);

        if (!success) {
            break;
        }
        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpContainer: Failed to create mq threads");
    }

    _conn_mtx.Lock();
    _mgr.resize(_option.default_container_size);
    for (size_t i = 0; i < _option.default_container_size; i++) {
        _free_index_list.emplace_back(i);
    }
    _conn_mtx.Unlock();

    int64_t n = GetCurrentMilliseconds();
    _magic_number = static_cast<uint16_t>(((n / 1000) >> 16) & 0xffff);
    _last_check_time.Store(n);
    return RAPTOR_ERROR_NONE;
}

raptor_error TcpContainer::Start() {
    raptor_error err = _epoll_thread->Start();
    if (err == RAPTOR_ERROR_NONE) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpContainer: failed to start epoll thread");
    }

    for (int i = 0; i < _running_threads; i++) {
        _mq_threads[i].Start();
    }

    if (_timer_thread) {
        _timer_thread->Start();
    }

    return RAPTOR_ERROR_NONE;
}

void TcpContainer::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        _epoll_thread->Shutdown();
        if (_timer_thread) {
            _timer_thread->Shutdown();
        }
        _cv.Signal();

        for (int i = 0; i < _running_threads; i++) {
            _mq_threads[i].Join();
        }

        _conn_mtx.Lock();
        _timeout_records.clear();
        _free_index_list.clear();
        for (auto &obj : _mgr) {
            if (obj.first) {
                obj.first->Shutdown(false);
                obj.first.reset();
            }
        }
        _mgr.clear();
        _conn_mtx.Unlock();

        // clear message queue
        bool empty = true;
        do {
            auto n = _mpscq.PopAndCheckEnd(&empty);
            auto msg = reinterpret_cast<TcpMessageNode *>(n);
            if (msg != nullptr) {
                _count.FetchSub(1, MemoryOrder::RELAXED);
                delete msg;
            }
        } while (!empty);
    }
}

bool TcpContainer::SendMsg(const Endpoint &ep, const void *data, size_t len) {
    uint32_t index = 0;
    if (!CheckConnectionId(ep.ConnectionId(), &index)) {
        return false;
    }

    auto con = GetConnection(index);
    if (con) {
        return con->SendMsg(data, len);
    }
    return false;
}

void TcpContainer::CloseEndpoint(const Endpoint &ep, bool event_notify) {
    uint32_t index = 0;
    if (!CheckConnectionId(ep.ConnectionId(), &index)) {
        return;
    }

    auto con = GetConnection(index);
    if (con) {
        con->Shutdown(event_notify);
        DeleteConnection(index);
    }
    return;
}

raptor_error TcpContainer::AttachEndpoint(const Endpoint &ep) {
    AutoMutex g(&_conn_mtx);

    if (_free_index_list.empty() && _mgr.size() >= _option.max_container_size) {
        raptor_error err = RAPTOR_ERROR_FROM_FORMAT(
            "The number of connections has exceeded the limit : %u", _option.max_container_size);
        return err;
    }

    std::shared_ptr<EndpointImpl> obj = ep._impl;

    if (_free_index_list.empty()) {
        size_t count = _mgr.size();
        size_t expand =
            ((count * 2) < _option.max_container_size) ? (count * 2) : _option.max_container_size;

        _mgr.resize(expand);
        for (size_t i = count; i < expand; i++) {
            _mgr[i].first = nullptr;
            _mgr[i].second = _timeout_records.end();
            _free_index_list.emplace_back(i);
        }
    }

    uint32_t index = _free_index_list.front();
    _free_index_list.pop_front();

    uint16_t listen_port = obj->GetListenPort();
    ConnectionId cid = core::BuildConnectionId(_magic_number, listen_port, index);
    int64_t deadline_line = GetCurrentMilliseconds() + _option.connection_timeoutms;

    obj->SetConnection(cid);
    obj->SetContainer(this);

    _mgr[index].first = std::make_shared<Connection>(obj);
    _mgr[index].first->SetProtocol(_option.proto_handler);
    _mgr[index].first->Init(this, _epoll_thread.get());
    _mgr[index].second = _timeout_records.insert({deadline_line, index});

    if (_timer_thread && _option.heartbeat_handler) {
        uint32_t handle_id = _mgr[index].first->HandleId();
        uint32_t delay = static_cast<uint32_t>(_option.heartbeat_handler->GetHeartbeatInterval());
        _timer_thread->SetTimer(index, handle_id, delay);
    }
    return RAPTOR_ERROR_NONE;
}

// Receiver implement (epoll event)
void TcpContainer::OnErrorEvent(void *ptr) {
    uint32_t index = 0;
    if (!CheckConnectionId((ConnectionId)ptr, &index)) {
        log_error("TcpContainer: OnErrorEvent found invalid index, cid = %x", (uint64_t)ptr);
        return;
    }

    raptor_error err = RAPTOR_ERROR_FROM_STATIC_STRING("EpollThread:OnErrorEvent");

    auto con = GetConnection(index);
    if (con) {
        con->Shutdown(true, Event(kSocketError, err));
        DeleteConnection(index);
    }
}

void TcpContainer::OnRecvEvent(void *ptr) {
    uint32_t index = 0;
    if (!CheckConnectionId((ConnectionId)ptr, &index)) {
        log_error("TcpContainer: OnRecvEvent found invalid index, cid = %x", (uint64_t)ptr);
        return;
    }

    auto con = GetConnection(index);
    if (!con) return;
    if (con->DoRecvEvent()) {
        RefreshTime(index);
        return;
    }
    raptor_error err = RAPTOR_POSIX_ERROR("Connection:DoRecv");
    con->Shutdown(true, Event(kSocketError, err));
    DeleteConnection(index);
    log_error("TcpContainer: Failed to sync recv");
}

void TcpContainer::OnSendEvent(void *ptr) {
    uint32_t index = 0;
    if (!CheckConnectionId((ConnectionId)ptr, &index)) {
        log_error("TcpContainer: OnSendEvent found invalid index, cid = %x", (uint64_t)ptr);
        return;
    }

    auto con = GetConnection(index);
    if (!con) return;
    if (con->DoSendEvent()) {
        RefreshTime(index);
        return;
    }

    raptor_error err = RAPTOR_POSIX_ERROR("Connection:DoSend");
    con->Shutdown(true, Event(kSocketError, err));
    DeleteConnection(index);
    log_error("TcpContainer: Failed to post sync send");
}

void TcpContainer::OnTimeoutCheck(int64_t current_millseconds) {

    // At least 3s to check once
    if (current_millseconds - _last_check_time.Load() < 3000) {
        return;
    }
    _last_check_time.Store(current_millseconds);

    AutoMutex g(&_conn_mtx);
    raptor_error err = RAPTOR_ERROR_FROM_STATIC_STRING("Connection:Heart-beat Timeout");

    auto it = _timeout_records.begin();
    while (it != _timeout_records.end()) {
        if (it->first > current_millseconds) {
            break;
        }

        uint32_t index = it->second;

        ++it;

        _mgr[index].first->Shutdown(true, Event(kConnectionTimeout, err));
        _mgr[index].first.reset();
        _timeout_records.erase(_mgr[index].second);
        _mgr[index].second = _timeout_records.end();
        _free_index_list.emplace_back(index);
    }
}

// ServiceInterface implement

void TcpContainer::OnDataReceived(const Endpoint &ep, const Slice &s) {
    TcpMessageNode *msg = new TcpMessageNode(ep);
    msg->slice = s;
    msg->type = TcpMessageType::kRecvAMessage;
    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void TcpContainer::OnClosed(const Endpoint &ep, const Event &event) {
    if (!_option.closed_handler) {
        return;
    }

    TcpMessageNode *msg = new TcpMessageNode(ep, event);
    msg->type = TcpMessageType::kCloseEvent;

    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void TcpContainer::OnTimerEvent(const Endpoint &ep) {
    if (!_option.heartbeat_handler) {
        return;
    }

    TcpMessageNode *msg = new TcpMessageNode(ep);
    msg->type = TcpMessageType::kHeartbeatEvent;

    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void TcpContainer::MessageQueueThread(void *ptr) {
    while (!_shutdown) {
        RaptorMutexLock(_mutex);

        while (_count.Load() == 0) {
            _cv.Wait(&_mutex);
            if (_shutdown) {
                RaptorMutexUnlock(_mutex);
                return;
            }
        }
        auto n = _mpscq.pop();
        auto msg = reinterpret_cast<struct TcpMessageNode *>(n);

        if (msg != nullptr) {
            _count.FetchSub(1, MemoryOrder::RELAXED);
            this->Dispatch(msg);
            delete msg;
        }
        RaptorMutexUnlock(_mutex);
    }
}

void TcpContainer::Dispatch(struct TcpMessageNode *msg) {
    switch (msg->type) {
    case TcpMessageType::kRecvAMessage:
        _option.msg_handler->OnMessage(msg->ep, msg->slice);
        break;
    case TcpMessageType::kCloseEvent:
        _option.closed_handler->OnClosed(msg->ep, msg->event);
        break;
    case TcpMessageType::kHeartbeatEvent:
        _option.heartbeat_handler->OnHeartbeat(msg->ep);
        break;
    default:
        log_error("unknow message type %d", static_cast<int>(msg->type));
        break;
    }
}

void TcpContainer::DeleteConnection(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    if (!_mgr[index].first) {
        return;
    }
    _mgr[index].first.reset();
    _timeout_records.erase(_mgr[index].second);
    _mgr[index].second = _timeout_records.end();
    _free_index_list.emplace_back(index);
}

void TcpContainer::RefreshTime(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    if (!_mgr[index].first) {
        return;
    }
    int64_t deadline_line = GetCurrentMilliseconds() + _option.connection_timeoutms;
    _timeout_records.erase(_mgr[index].second);
    _mgr[index].second = _timeout_records.insert({deadline_line, index});
}

bool TcpContainer::CheckConnectionId(uint64_t cid, uint32_t *index) const {
    if (cid == core::InvalidConnectionId) {
        return false;
    }
    uint16_t magic = core::GetMagicNumber(cid);
    if (magic != _magic_number) {
        return false;
    }

    uint32_t idx = core::GetUserId(cid);
    if (idx == InvalidIndex || idx >= _option.max_container_size) {
        return false;
    }
    *index = idx;
    return true;
}

std::shared_ptr<Connection> TcpContainer::GetConnection(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    auto obj = _mgr[index].first;
    return obj;
}

void TcpContainer::OnTimer(uint32_t tid1, uint32_t tid2) {
    // tid1 = index, tid2 = handle_id
    std::shared_ptr<Connection> conn = GetConnection(tid1);
    if (!conn) {
        return;
    }
    if (conn->HandleId() == tid2 && conn->IsOnline()) {
        OnTimerEvent(conn->_endpoint);

        // prepare next timer
        uint32_t delay = static_cast<uint32_t>(_option.heartbeat_handler->GetHeartbeatInterval());
        _timer_thread->SetTimer(tid1, tid2, delay);
    }
}

}  // namespace raptor
