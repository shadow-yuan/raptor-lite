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

#include "src/windows/tcp_connector.h"
#include "raptor-lite/impl/connector.h"
#include "raptor-lite/impl/endpoint.h"
#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/windows/socket_setting.h"

namespace raptor {
struct async_connect_record_entry {
    SOCKET fd;
    OVERLAPPED overlapped;
    raptor_resolved_address addr;
};

TcpConnector::TcpConnector(ConnectorHandler *handler)
    : _handler(handler)
    , _shutdown(true)
    , _connectex(nullptr)
    , _tcp_user_timeout_ms(0) {
    memset(&_exit, 0, sizeof(_exit));
}

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
        _shutdown = true;
        _poll_thread->Shutdown();

        AutoMutex g(&_mtex);
        for (auto record : _records) {
            auto entry = reinterpret_cast<struct async_connect_record_entry *>(record);
            closesocket(entry->fd);
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

    e = InternalConnect(&addrs->addrs[0], _tcp_user_timeout_ms);
    raptor_resolved_addresses_destroy(addrs);
    return e;
}

void TcpConnector::OnTimeoutCheck(int64_t) {}

void TcpConnector::OnEventProcess(EventDetail *detail) {

    auto CompletionKey = reinterpret_cast<struct async_connect_record_entry *>(detail->ptr);

    std::shared_ptr<EndpointImpl> endpoint =
        std::make_shared<EndpointImpl>(CompletionKey->fd, &CompletionKey->addr);

    if (detail->event_type & internal::kErrorEvent) {
        // Maybe an error occurred or the connection was closed
        raptor_error err = RAPTOR_WINDOWS_ERROR(detail->error_code, "IOCP_WAIT");
        _handler->OnErrorOccurred(endpoint, err);
    } else {
        // update connect context
        setsockopt(CompletionKey->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        Property property;
        _handler->OnConnect(endpoint, property);
        if (endpoint->IsOnline()) {
            ProcessProperty(CompletionKey->fd, property);
        }
    }

    AutoMutex g(&_mtex);
    _records.erase(reinterpret_cast<intptr_t>(CompletionKey));
    delete CompletionKey;
}

void TcpConnector::ProcessProperty(SOCKET fd, const Property &p) {
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

raptor_error TcpConnector::GetConnectExIfNecessary(SOCKET s) {
    if (!_connectex) {
        GUID guid = WSAID_CONNECTEX;
        DWORD ioctl_num_bytes;
        int status;

        /* Grab the function pointer for ConnectEx for that specific socket.
            It may change depending on the interface. */
        status = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &_connectex,
                          sizeof(_connectex), &ioctl_num_bytes, NULL, NULL);

        if (status != 0) {
            return RAPTOR_WINDOWS_ERROR(WSAGetLastError(),
                                        "WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)");
        }
    }

    return RAPTOR_ERROR_NONE;
}

raptor_error TcpConnector::InternalConnect(const raptor_resolved_address *addr,
                                           int timeout_millseconds) {
    raptor_dualstack_mode mode;
    raptor_resolved_address local_address;
    raptor_resolved_address mapped_addr;
    int status;
    BOOL ret;

    struct async_connect_record_entry *entry = new struct async_connect_record_entry;
    raptor_error error = raptor_create_socket(addr, &mapped_addr, &entry->fd, &mode);

    if (error != RAPTOR_ERROR_NONE) {
        goto failure;
    }
    error = raptor_tcp_prepare_socket(entry->fd, timeout_millseconds);
    if (error != RAPTOR_ERROR_NONE) {
        goto failure;
    }
    error = GetConnectExIfNecessary(entry->fd);
    if (error != RAPTOR_ERROR_NONE) {
        goto failure;
    }

    raptor_sockaddr_make_wildcard6(0, &local_address);

    status = bind(entry->fd, (raptor_sockaddr *)&local_address.addr, (int)local_address.len);

    if (status != 0) {
        error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "bind");
        goto failure;
    }

    ret = _connectex(entry->fd, (raptor_sockaddr *)&mapped_addr.addr, (int)mapped_addr.len, NULL, 0,
                     NULL, &entry->overlapped);

    /* It wouldn't be unusual to get a success immediately. But we'll still get
        an IOCP notification, so let's ignore it. */
    if (!ret) {
        int last_error = WSAGetLastError();
        if (last_error != ERROR_IO_PENDING) {
            error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "ConnectEx");
            goto failure;
        }
    }

    _mtex.Lock();
    memcpy(&entry->addr, &mapped_addr, sizeof(mapped_addr));
    _records.insert(reinterpret_cast<intptr_t>(entry));
    _poll_thread->Add(entry->fd, (void *)entry);
    _mtex.Unlock();
    return RAPTOR_ERROR_NONE;

failure:
    if (entry->fd != INVALID_SOCKET) {
        closesocket(entry->fd);
    }
    delete entry;
    return error;
}

}  // namespace raptor
