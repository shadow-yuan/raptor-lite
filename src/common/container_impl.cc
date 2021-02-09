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

#include "src/common/container_impl.h"
#include <string.h>

#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/event.h"
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/mpscq.h"
#include "raptor-lite/utils/time.h"

#include "src/common/cid.h"
#include "src/common/endpoint_impl.h"
#include "src/common/resolve_address.h"
#include "src/common/socket_util.h"

#ifdef _WIN32
#include "src/windows/connection.h"
#include "src/windows/iocp_thread.h"
#else
#include "src/linux/connection.h"
#include "src/linux/epoll_thread.h"
#endif

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

ContainerImpl::ContainerImpl(ContainerImpl::Option *option)
    : _shutdown(true) {
    memcpy(&_option, option, sizeof(_option));
}

ContainerImpl::~ContainerImpl() {
    if (!_shutdown) {
        Shutdown();
    }
}

raptor_error ContainerImpl::Init() {
    if (!_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("tcp server already running");

    _poll_thread = std::make_shared<PollingThread>(this);
    raptor_error e = _poll_thread->Init(_option.recv_send_threads);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("ContainerImpl: Failed to init poll thread, %s", e->ToString().c_str());
        return e;
    }

    _poll_thread->EnableTimeoutCheck(!_option.not_check_connection_timeout);
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
                   std::bind(&ContainerImpl::MessageQueueThread, this, std::placeholders::_1),
                   nullptr, &success);

        if (!success) {
            break;
        }
        _running_threads++;
    }

    if (_running_threads == 0) {
        log_error("ContainerImpl: Failed to create message queue thread");
        return RAPTOR_ERROR_FROM_STATIC_STRING("ContainerImpl: Failed to create mq threads");
    }

    _conn_mtx.Lock();
    _mgr.resize(_option.default_container_size);
    for (size_t i = 0; i < _option.default_container_size; i++) {
        _free_index_list.emplace_back(static_cast<uint32_t>(i));
    }
    _conn_mtx.Unlock();

    int64_t n = GetCurrentMilliseconds();
    _magic_number = static_cast<uint16_t>(((n / 1000) >> 16) & 0xffff);
    _last_check_time.Store(n);
    log_debug("ContainerImpl: initialization completed");
    return RAPTOR_ERROR_NONE;
}

raptor_error ContainerImpl::Start() {
    raptor_error err = _poll_thread->Start();
    if (err != RAPTOR_ERROR_NONE) {
        log_error("ContainerImpl: Failed to start poll thread");
        return RAPTOR_ERROR_FROM_STATIC_STRING("ContainerImpl: Failed to start poll thread");
    }

    for (int i = 0; i < _running_threads; i++) {
        _mq_threads[i].Start();
    }

    if (_timer_thread) {
        _timer_thread->Start();
    }

    return RAPTOR_ERROR_NONE;
}

void ContainerImpl::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        _poll_thread->Shutdown();
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

bool ContainerImpl::SendMsg(const Endpoint &ep, const void *data, size_t len) {
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

void ContainerImpl::CloseEndpoint(const Endpoint &ep, bool event_notify) {
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

raptor_error ContainerImpl::AttachEndpoint(const Endpoint &ep) {
    AutoMutex g(&_conn_mtx);

    if (_free_index_list.empty() && _mgr.size() >= _option.max_container_size) {
        log_error(
            "ContainerImpl: Failed to Attach Endpoint, MaxContainerSize = %u CurrentSize = %u",
            _option.max_container_size, _mgr.size());
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
            _free_index_list.emplace_back(static_cast<uint32_t>(i));
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
    _mgr[index].first->Init(this, _poll_thread.get());
    _mgr[index].second = _timeout_records.insert({deadline_line, index});

    if (_timer_thread && _option.heartbeat_handler) {
        uint32_t handle_id = _mgr[index].first->HandleId();
        uint32_t delay = static_cast<uint32_t>(_option.heartbeat_handler->GetHeartbeatInterval());
        _timer_thread->SetTimer(index, handle_id, delay);
    }
    return RAPTOR_ERROR_NONE;
}

// Receiver implement (iocp / epoll event)
void ContainerImpl::OnErrorEvent(uint32_t index, EventDetail *ptr) {
    raptor_error err = MakeRefCounted<Status>(ptr->error_code, "ContainerImpl:OnErrorEvent");
    auto con = GetConnection(index);
    if (con) {
        // log_warn("ContainerImpl:OnErrorEvent index = %u", index);
        con->Shutdown(true, Event(kSocketError, err));
        DeleteConnection(index);
    }
}

void ContainerImpl::OnRecvEvent(uint32_t index, EventDetail *ptr) {
    auto con = GetConnection(index);
    if (!con) return;

    raptor_error err = con->DoRecvEvent(ptr);
    if (err == RAPTOR_ERROR_NONE) {
        RefreshTime(index);
        return;
    }

    con->Shutdown(true, Event(kSocketError, err));
    DeleteConnection(index);
    // log_error("ContainerImpl: Failed to DoRecvEvent");
}

void ContainerImpl::OnSendEvent(uint32_t index, EventDetail *ptr) {
    auto con = GetConnection(index);
    if (!con) return;

    raptor_error err = con->DoSendEvent(ptr);
    if (err == RAPTOR_ERROR_NONE) {
        RefreshTime(index);
        return;
    }

    con->Shutdown(true, Event(kSocketError, err));
    DeleteConnection(index);
    // log_error("ContainerImpl: Failed to DoSendEvent");
}

void ContainerImpl::OnEventProcess(EventDetail *detail) {
    if (!detail) return;

    uint32_t index = 0;
    if (!CheckConnectionId((ConnectionId)detail->ptr, &index)) {
        log_error("ContainerImpl: Found an invalid index, cid = %x event = %d",
                  (uint64_t)detail->ptr, detail->event_type);
        return;
    }

    if (detail->event_type & internal::kErrorEvent) {
        OnErrorEvent(index, detail);
        return;
    }
    if (detail->event_type & internal::kRecvEvent) {
        OnRecvEvent(index, detail);
        return;
    }
    if (detail->event_type & internal::kSendEvent) {
        OnSendEvent(index, detail);
        return;
    }
}

void ContainerImpl::OnTimeoutCheck(int64_t current_millseconds) {

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

        log_warn("ContainerImpl: timeout checking shutdown connection(index = %u)", index);

        _mgr[index].first->Shutdown(true, Event(kConnectionTimeout, err));
        _mgr[index].first.reset();
        _timeout_records.erase(_mgr[index].second);
        _mgr[index].second = _timeout_records.end();
        _free_index_list.emplace_back(index);
    }
}

// ServiceInterface implement

void ContainerImpl::OnDataReceived(const Endpoint &ep, const Slice &s) {
    TcpMessageNode *msg = new TcpMessageNode(ep);
    msg->slice = s;
    msg->type = TcpMessageType::kRecvAMessage;
    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void ContainerImpl::OnClosed(const Endpoint &ep, const Event &event) {
    if (!_option.closed_handler) {
        return;
    }

    TcpMessageNode *msg = new TcpMessageNode(ep, event);
    msg->type = TcpMessageType::kCloseEvent;

    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void ContainerImpl::OnTimerEvent(const Endpoint &ep) {
    if (!_option.heartbeat_handler) {
        return;
    }

    TcpMessageNode *msg = new TcpMessageNode(ep);
    msg->type = TcpMessageType::kHeartbeatEvent;

    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void ContainerImpl::MessageQueueThread(void *) {
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

void ContainerImpl::Dispatch(struct TcpMessageNode *msg) {
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
        log_error("ContainerImpl: Dispatch found an unknow message type %d",
                  static_cast<int>(msg->type));
        break;
    }
}

void ContainerImpl::DeleteConnection(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    if (!_mgr[index].first) {
        return;
    }
    _mgr[index].first.reset();
    _timeout_records.erase(_mgr[index].second);
    _mgr[index].second = _timeout_records.end();
    _free_index_list.emplace_back(index);
}

void ContainerImpl::RefreshTime(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    if (!_mgr[index].first) {
        return;
    }
    int64_t deadline_line = GetCurrentMilliseconds() + _option.connection_timeoutms;
    _timeout_records.erase(_mgr[index].second);
    _mgr[index].second = _timeout_records.insert({deadline_line, index});
}

bool ContainerImpl::CheckConnectionId(uint64_t cid, uint32_t *index) const {
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

std::shared_ptr<Connection> ContainerImpl::GetConnection(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    auto obj = _mgr[index].first;
    return obj;
}

void ContainerImpl::OnTimer(uint32_t tid1, uint32_t tid2) {
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
