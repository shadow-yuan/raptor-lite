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
#include "raptor-lite/utils/log.h"
#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/linux/socket_setting.h"

namespace raptor {
struct async_connect_record_entry {
    int fd;
    intptr_t user_value;
    raptor_resolved_address addr;
};

TcpConnector::TcpConnector(ConnectorHandler *handler)
    : _handler(handler)
    , _shutdown(true)
    , _tcp_user_timeout_ms(0) {}

TcpConnector::~TcpConnector() {
    Shutdown();
}

raptor_error TcpConnector::Init(int threads, int tcp_user_timeout) {
    if (!_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpConnector is already running");
    }

    _poll_thread = std::make_shared<PollingThread>(this);
    raptor_error e = _poll_thread->Init(threads, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpConnector: Failed to init poll thread, %s", e->ToString().c_str());
        return e;
    }
    _poll_thread->EnableTimeoutCheck(false);

    _tcp_user_timeout_ms = tcp_user_timeout;

    _shutdown = false;

    return RAPTOR_ERROR_NONE;
}

raptor_error TcpConnector::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpConnector is not initialized");
    }

    return _poll_thread->Start();
}

void TcpConnector::Shutdown() {
    if (!_shutdown) {
        log_warn("TcpConnector: prepare to shutdown");
        _shutdown = true;
        _poll_thread->Shutdown();

        AutoMutex g(&_mtex);
        for (auto record : _records) {
            auto entry = reinterpret_cast<struct async_connect_record_entry *>(record);
            close(entry->fd);
        }
        _records.clear();
        log_warn("TcpConnector: shutdown");
    }
}

raptor_error TcpConnector::Connect(const std::string &addr, intptr_t user) {
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

    e = AsyncConnect(&addrs->addrs[0], user, _tcp_user_timeout_ms);
    if (e != RAPTOR_ERROR_NONE) {
        log_warn("TcpConnector: Failed to connect %s, %s", addr.c_str(), e->ToString().c_str());
    }
    raptor_resolved_addresses_destroy(addrs);
    return e;
}

raptor_error TcpConnector::AsyncConnect(const raptor_resolved_address *addr, intptr_t user,
                                        int timeout_ms) {
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

    _mtex.Lock();
    auto entry = new struct async_connect_record_entry;
    entry->fd = sock_fd;
    entry->user_value = user;
    memcpy(&entry->addr, &mapped_addr, sizeof(mapped_addr));
    _records.insert(reinterpret_cast<intptr_t>(entry));
    _poll_thread->Add(sock_fd, (void *)entry, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
    _mtex.Unlock();

    char *str_addr = nullptr;
    raptor_sockaddr_to_string(&str_addr, &mapped_addr, 0);
    if (str_addr) {
        log_info("TcpConnector: start connecting %s", str_addr);
        free(str_addr);
    }
    return RAPTOR_ERROR_NONE;
}

void TcpConnector::OnTimeoutCheck(int64_t) {}
void TcpConnector::OnEventProcess(EventDetail *detail) {
    struct async_connect_record_entry *entry =
        reinterpret_cast<struct async_connect_record_entry *>(detail->ptr);

    // get local address
    raptor_resolved_address local;
    local.len = sizeof(local.addr);
    memset(local.addr, 0, local.len);
    getsockname(entry->fd, (struct sockaddr *)local.addr, (socklen_t *)&local.len);

    std::shared_ptr<EndpointImpl> endpoint =
        std::make_shared<EndpointImpl>(entry->fd, &local, &entry->addr);

    _poll_thread->Delete(entry->fd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);

    if (detail->event_type & internal::kSendEvent) {
        Property property({"UserCustomValue", entry->user_value});
        _handler->OnConnect(endpoint, property);
        if (endpoint->IsOnline()) {
            ProcessProperty(entry->fd, property);
        }
    } else {
        int error_code = raptor_get_socket_error(entry->fd);
        raptor_error err = MakeRefCounted<Status>(error_code, "getsockopt(SO_ERROR)");
        _handler->OnErrorOccurred(endpoint, err);
        close(entry->fd);
    }

    AutoMutex g(&_mtex);
    _records.erase(reinterpret_cast<intptr_t>(entry));
    delete entry;
}

void TcpConnector::ProcessProperty(int fd, const Property &p) {
    bool SocketNoSIGPIPE = true;
    if (p.CheckValue<bool>("SocketNoSIGPIPE", SocketNoSIGPIPE) && SocketNoSIGPIPE) {
        raptor_set_socket_no_sigpipe_if_possible(fd);
    }

    bool SocketReuseAddress = true;
    if (p.CheckValue<bool>("SocketReuseAddress", SocketReuseAddress) && !SocketReuseAddress) {
        raptor_set_socket_reuse_addr(fd, 0);
    }

    bool SocketLowLatency = true;
    if (p.CheckValue<bool>("SocketLowLatency", SocketLowLatency) && !SocketLowLatency) {
        raptor_set_socket_low_latency(fd, 0);
    }

    int SocketSendTimeoutMs = 0;
    if (p.CheckValue<int>("SocketSendTimeoutMs", SocketSendTimeoutMs) && SocketSendTimeoutMs > 0) {
        raptor_set_socket_snd_timeout(fd, SocketSendTimeoutMs);
    }

    int SocketRecvTimeoutMs = 0;
    if (p.CheckValue<int>("SocketRecvTimeoutMs", SocketRecvTimeoutMs) && SocketRecvTimeoutMs > 0) {
        raptor_set_socket_rcv_timeout(fd, SocketRecvTimeoutMs);
    }

    bool SocketNonBlocking = true;
    if (p.CheckValue<bool>("SocketNonBlocking", SocketNonBlocking) && !SocketNonBlocking) {
        raptor_set_socket_nonblocking(fd, 0);
    }
}
}  // namespace raptor
