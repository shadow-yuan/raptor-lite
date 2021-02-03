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

#include "src/linux/tcp_listener.h"
#include "src/linux/socket_setting.h"
#include "raptor-lite/utils/mpscq.h"
#include "src/common/resolve_address.h"
#include "src/common/socket_util.h"
#include "raptor-lite/utils/log.h"
#include "src/utils/time.h"
#include "raptor-lite/impl/endpoint.h"
#include "src/common/cid.h"
#include "src/common/endpoint_impl.h"
#include "raptor-lite/impl/event.h"

namespace raptor {
enum MessageType {
    kRecvAMessage,
    kOtherEvent,
};
struct TcpMessageNode {
    MultiProducerSingleConsumerQueue::Node node;
    MessageType type;
    Endpoint ep;
    Slice slice;
    Event event;
    TcpMessageNode(const Endpoint &o)
        : ep(o)
        , event(kNoneError) {}

    TcpMessageNode(const Endpoint &o, EventType et)
        : ep(o)
        , event(et) {}
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

    _shutdown = false;
    _count.Store(0);

    _running_threads = 0;
    _mq_threads = new Thread[_option.mq_consumer_threads];
    for (size_t i = 0; i < _option.mq_consumer_threads; i++) {
        bool success = false;
        _mq_threads[i] = Thread(
            "message_queue",
            std::bind(&TcpContainer::MessageQueueThread, this, std::placeholders::_1), &success);

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

    return RAPTOR_ERROR_NONE;
}

void TcpContainer::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        _epoll_thread->Shutdown();
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
    uint32_t index = CheckConnectionId(ep.ConnectionId());
    if (index == InvalidIndex) {
        return false;
    }

    auto con = GetConnection(index);
    if (con) {
        return con->SendMsg(data, len);
    }
    return false;
}

void TcpContainer::CloseEndpoint(const Endpoint &ep, bool event_notify) {
    uint32_t index = CheckConnectionId(ep.ConnectionId());
    if (index == InvalidIndex) {
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

    uint16_t listen_port = ep.GetListenPort();
    ConnectionId cid = core::BuildConnectionId(_magic_number, listen_port, index);
    int64_t deadline_line = GetCurrentMilliseconds() + _option.connection_timeoutms;

    std::shared_ptr<EndpointImpl> obj = ep._impl;
    obj->SetContainer(this);

    _mgr[index].first = std::make_shared<Connection>(this);
    _mgr[index].first->SetProtocol(_option.proto_handler);
    _mgr[index].first->Init(cid, obj, _epoll_thread.get());
    _mgr[index].second = _timeout_records.insert({deadline_line, index});
    return RAPTOR_ERROR_NONE;
}

// Receiver implement (epoll event)
void TcpContainer::OnErrorEvent(void *ptr) {
    ConnectionId cid = (ConnectionId)ptr;
    uint32_t index = CheckConnectionId(cid);
    if (index == InvalidIndex) {
        log_error("TcpContainer: OnErrorEvent found invalid index, cid = %x", cid);
        return;
    }

    auto con = GetConnection(index);
    if (con) {
        con->Shutdown(true);
        DeleteConnection(index);
    }
}

void TcpContainer::OnRecvEvent(void *ptr) {
    ConnectionId cid = (ConnectionId)ptr;
    uint32_t index = CheckConnectionId(cid);
    if (index == InvalidIndex) {
        log_error("TcpContainer: OnRecvEvent found invalid index, cid = %x", cid);
        return;
    }

    auto con = GetConnection(index);
    if (!con) return;
    if (con->DoRecvEvent()) {
        RefreshTime(index);
        return;
    }
    con->Shutdown(true);
    DeleteConnection(index);
    log_error("TcpContainer: Failed to post async recv");
}

void TcpContainer::OnSendEvent(void *ptr) {
    ConnectionId cid = (ConnectionId)ptr;
    uint32_t index = CheckConnectionId(cid);
    if (index == InvalidIndex) {
        log_error("TcpContainer: OnRecvEvent found invalid index, cid = %x", cid);
        return;
    }

    auto con = GetConnection(index);
    if (!con) return;
    if (con->DoSendEvent()) {
        RefreshTime(index);
        return;
    }
    con->Shutdown(true);
    DeleteConnection(index);
    log_error("TcpContainer: Failed to post async send");
}

void TcpContainer::OnTimeoutCheck(int64_t current_millseconds) {

    // At least 3s to check once
    if (current_millseconds - _last_check_time.Load() < 3000) {
        return;
    }
    _last_check_time.Store(current_millseconds);

    AutoMutex g(&_conn_mtx);

    auto it = _timeout_records.begin();
    while (it != _timeout_records.end()) {
        if (it->first > current_millseconds) {
            break;
        }

        uint32_t index = it->second;

        ++it;

        _mgr[index].first->Shutdown(true);
        _mgr[index].first.reset();
        _timeout_records.erase(_mgr[index].second);
        _mgr[index].second = _timeout_records.end();
        _free_index_list.push_back(index);
    }
}

// ServiceInterface implement

void TcpContainer::OnDataReceived(const Endpoint &ep, const Slice &s) {
    TcpMessageNode *msg = new TcpMessageNode(ep);
    msg->slice = s;
    msg->type = MessageType::kRecvAMessage;
    _mpscq.push(&msg->node);
    _count.FetchAdd(1, MemoryOrder::ACQ_REL);
    _cv.Signal();
}

void TcpContainer::OnClosed(const Endpoint &ep) {
    if (!_option.event_handler) {
        return;
    }

    TcpMessageNode *msg = new TcpMessageNode(ep, EventType::kConnectionClosed);
    msg->type = MessageType::kOtherEvent;

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
    case MessageType::kRecvAMessage:
        _option.msg_handler->OnMessage(msg->ep, msg->slice);
        break;
    case MessageType::kOtherEvent:
        _option.event_handler->OnEvent(msg->ep, msg->event);
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
    _free_index_list.push_back(index);
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

uint32_t TcpContainer::CheckConnectionId(ConnectionId cid) const {
    uint32_t failure = InvalidIndex;
    if (cid == core::InvalidConnectionId) {
        return failure;
    }
    uint16_t magic = core::GetMagicNumber(cid);
    if (magic != _magic_number) {
        return failure;
    }

    uint32_t uid = core::GetUserId(cid);
    if (uid >= _option.max_container_size) {
        return failure;
    }
    return uid;
}

std::shared_ptr<Connection> TcpContainer::GetConnection(uint32_t index) {
    AutoMutex g(&_conn_mtx);
    auto obj = _mgr[index].first;
    return obj;
}

}  // namespace raptor
