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

#include "src/linux/tcp_connector.h"
#include <string.h>
#include "raptor-lite/impl/connector.h"
#include "raptor-lite/impl/endpoint.h"
#include "src/common/endpoint_impl.h"
#include "src/linux/socket_setting.h"

namespace raptor {
struct async_connect_record_entry {
    int fd;
    raptor_resolved_address addr;
};

TcpConnector::TcpConnector(ConnectorHandler *handler)
    : _handler(handler)
    , _shutdown(true)
    , _threads(nullptr)
    , _number_of_thread(0)
    , _tcp_user_timeout_ms(0)
    , _running_threads(0) {}

TcpConnector::~TcpConnector() {}

raptor_error TcpConnector::Init(int threads, int tcp_user_timeout) {
    if (!_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpConnector is already running");
    }

    raptor_error e = _epoll.create();
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }

    _shutdown = false;
    _number_of_thread = threads;
    _tcp_user_timeout_ms = tcp_user_timeout;

    _threads = new Thread[threads];

    for (int i = 0; i < threads; i++) {
        bool success = false;

        _threads[i] = Thread("Linux:connector",
                             std::bind(&TcpConnector::WorkThread, this, std::placeholders::_1),
                             nullptr, &success);

        if (!success) {
            break;
        }
        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpConnector failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error TcpConnector::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpConnector is not initialized");
    }

    for (int i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void TcpConnector::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;

        for (int i = 0; i < _running_threads; i++) {
            _threads[i].Join();
        }
        _epoll.shutdown();

        AutoMutex g(&_mtex);
        for (auto record : _records) {
            auto entry = reinterpret_cast<struct async_connect_record_entry *>(record);
            close(entry->fd);
        }
        _records.clear();
    }
}

raptor_error TcpConnector::Connect(const std::string &addr) {
    raptor_resolved_addresses *addrs;
    auto e = raptor_blocking_resolve_address(addr.c_str(), nullptr, &addrs);
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }
    if (addrs->naddrs == 0) {
        e = RAPTOR_ERROR_FROM_STATIC_STRING("Invalid address: ");
        e->AppendMessage(addr);
        return e;
    }

    e = AsyncConnect(&addrs->addrs[0], _tcp_user_timeout_ms);
    raptor_resolved_addresses_destroy(addrs);
    return e;
}

raptor_error TcpConnector::AsyncConnect(const raptor_resolved_address *addr, int timeout_ms) {
    raptor_resolved_address mapped_addr;
    int sock_fd = -1;

    raptor_error result =
        raptor_tcp_client_prepare_socket(addr, &mapped_addr, &sock_fd, timeout_ms);
    if (result != RAPTOR_ERROR_NONE) {
        return result;
    }
    int err = 0;
    do {
        err = connect(sock_fd, (const raptor_sockaddr *)mapped_addr.addr, mapped_addr.len);
    } while (err < 0 && errno == EINTR);

    if (errno != EWOULDBLOCK && errno != EINPROGRESS) {
        raptor_set_socket_shutdown(sock_fd);
        return RAPTOR_POSIX_ERROR("connect");
    }

    AutoMutex g(&_mtex);
    auto entry = new struct async_connect_record_entry;
    entry->fd = sock_fd;
    memcpy(&entry->addr, &mapped_addr, sizeof(mapped_addr));
    _records.insert(reinterpret_cast<intptr_t>(entry));
    _epoll.add(sock_fd, (void *)entry, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
    return RAPTOR_ERROR_NONE;
}

void TcpConnector::WorkThread(void *) {
    while (!_shutdown) {

        int number_of_fds = _epoll.polling();
        if (_shutdown) {
            return;
        }

        if (number_of_fds <= 0) {
            continue;
        }

        for (int i = 0; i < number_of_fds; i++) {
            struct epoll_event *ev = _epoll.get_event(i);

            struct async_connect_record_entry *entry =
                reinterpret_cast<struct async_connect_record_entry *>(ev->data.ptr);

            std::shared_ptr<EndpointImpl> endpoint =
                std::make_shared<EndpointImpl>(entry->fd, &entry->addr);

            _epoll.remove(entry->fd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);

            if (ev->events & EPOLLERR || ev->events & EPOLLHUP || ev->events & EPOLLRDHUP ||
                ev->events & EPOLLIN) {
                int error_code = raptor_get_socket_error(entry->fd);
                raptor_error err = MakeRefCounted<Status>(error_code, "getsockopt(SO_ERROR)");
                _handler->OnErrorOccurred(endpoint, err);
                close(entry->fd);
            } else if (ev->events & EPOLLOUT) {
                Property property;
                _handler->OnConnect(endpoint, property);
                if (endpoint->IsOnline()) {
                    ProcessProperty(entry->fd, property);
                }
            }

            AutoMutex g(&_mtex);
            _records.erase(reinterpret_cast<intptr_t>(entry));
            delete entry;
        }
    }
}

void TcpConnector::ProcessProperty(int fd, const Property &p) {
    bool SocketNoSIGPIPE = false;
    if (p.CheckValue<bool>("SocketNoSIGPIPE", SocketNoSIGPIPE) && SocketNoSIGPIPE) {
        raptor_set_socket_no_sigpipe_if_possible(fd);
    }

    bool SocketReuseAddress = false;
    if (p.CheckValue<bool>("SocketReuseAddress", SocketReuseAddress) && SocketReuseAddress) {
        raptor_set_socket_reuse_addr(fd, 1);
    }

    bool SocketLowLatency = false;
    if (p.CheckValue<bool>("SocketLowLatency", SocketLowLatency) && SocketLowLatency) {
        raptor_set_socket_low_latency(fd, 1);
    }

    int SocketSendTimeoutMs = 0;
    if (p.CheckValue<int>("SocketSendTimeoutMs", SocketSendTimeoutMs) && SocketSendTimeoutMs > 0) {
        raptor_set_socket_snd_timeout(fd, SocketSendTimeoutMs);
    }

    int SocketRecvTimeoutMs = 0;
    if (p.CheckValue<int>("SocketRecvTimeoutMs", SocketRecvTimeoutMs) && SocketRecvTimeoutMs > 0) {
        raptor_set_socket_rcv_timeout(fd, SocketRecvTimeoutMs);
    }
}
}  // namespace raptor
