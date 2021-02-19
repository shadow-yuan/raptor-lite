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

#include <string.h>

#include "raptor-lite/impl/connector.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/utils/log.h"
#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/windows/socket_setting.h"

namespace raptor {
struct async_connect_record_entry {
    OverLappedEx ole;
    raptor_resolved_address addr;
    SOCKET fd;
    intptr_t user_value;
};

TcpConnector::TcpConnector(ConnectorHandler *handler)
    : _handler(handler)
    , _shutdown(true)
    , _connectex(nullptr)
    , _tcp_user_timeout_ms(0)
    , _counter(0) {}

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

        _mtex.Lock();
        for (auto record : _records) {
            auto entry = reinterpret_cast<struct async_connect_record_entry *>(record);
            closesocket(entry->fd);
            _poll_thread->Delete(entry->fd);
        }
        _records.clear();
        _mtex.Unlock();

        _poll_thread->Shutdown();

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

    e = InternalConnect(&addrs->addrs[0], user, _tcp_user_timeout_ms);
    if (e != RAPTOR_ERROR_NONE) {
        log_warn("TcpConnector: Failed to connect %s, %s", addr.c_str(), e->ToString().c_str());
    }
    raptor_resolved_addresses_destroy(addrs);
    return e;
}

void TcpConnector::OnTimeoutCheck(int64_t) {}

async_connect_record_entry *TcpConnector::FindRecordEntryFromOverlapped(OVERLAPPED *overlaped) {
    size_t off = offsetof(OverLappedEx, overlapped);
    intptr_t olex = reinterpret_cast<intptr_t>(overlaped) - static_cast<intptr_t>(off);
    off = offsetof(async_connect_record_entry, ole);
    intptr_t entry = olex - static_cast<intptr_t>(off);
    return reinterpret_cast<async_connect_record_entry *>(entry);
}

void TcpConnector::OnEventProcess(EventDetail *detail) {

    RAPTOR_ASSERT(detail->event_type & internal::kConnectEvent);

    async_connect_record_entry *entry = FindRecordEntryFromOverlapped(detail->overlaped);
    // SOCKET sock_fd = reinterpret_cast<SOCKET>(detail->ptr);

    // get local address
    raptor_resolved_address local;
    local.len = sizeof(local.addr);
    memset(local.addr, 0, local.len);
    getsockname(entry->fd, (struct sockaddr *)local.addr, (int *)&local.len);

    std::shared_ptr<EndpointImpl> endpoint =
        std::make_shared<EndpointImpl>(entry->fd, &local, &entry->addr);

    if (detail->event_type & internal::kErrorEvent) {
        // Maybe an error occurred or the connection was closed
        raptor_error err = RAPTOR_WINDOWS_ERROR(detail->error_code, "IOCP_WAIT");
        _handler->OnErrorOccurred(endpoint, err);
    } else {
        // update connect context
        setsockopt(entry->fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
        Property property({"UserCustomValue", entry->user_value});
        _handler->OnConnect(endpoint, property);
        if (endpoint->IsOnline()) {
            ProcessProperty(entry->fd, property);
        }
    }

    AutoMutex g(&_mtex);
    size_t n = _records.erase(reinterpret_cast<intptr_t>(entry));
    if (n > 0) {
        delete entry;
    }
}

void TcpConnector::ProcessProperty(SOCKET fd, const Property &p) {
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

raptor_error TcpConnector::InternalConnect(const raptor_resolved_address *addr, intptr_t user,
                                           int timeout_millseconds) {
    raptor_dualstack_mode mode = RAPTOR_DSMODE_NONE;
    raptor_resolved_address local_address;
    raptor_resolved_address mapped_addr;
    int status;
    BOOL ret;
    char *str_addr = nullptr;

    struct async_connect_record_entry *entry = new struct async_connect_record_entry;

    // SHADOW: If OVERLAPPED not initialized, ConnectEx will fail
    // and WSAGetLastError will return 6.
    memset(entry, 0, sizeof(*entry));

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

    entry->ole.event_type = internal::kConnectEvent;
    entry->ole.HandleId = _counter.FetchAdd(1, MemoryOrder::RELAXED);
    entry->user_value = user;

    _mtex.Lock();
    memcpy(&entry->addr, &mapped_addr, sizeof(mapped_addr));
    _records.insert(reinterpret_cast<intptr_t>(entry));
    _poll_thread->Add(entry->fd, static_cast<uint64_t>(entry->fd), internal::kConnectEvent);
    _mtex.Unlock();

    ret = _connectex(entry->fd, (raptor_sockaddr *)&mapped_addr.addr, (int)mapped_addr.len, NULL, 0,
                     NULL, &entry->ole.overlapped);

    /* It wouldn't be unusual to get a success immediately. But we'll still get
        an IOCP notification, so let's ignore it. */
    if (!ret) {
        int last_error = WSAGetLastError();
        if (last_error != ERROR_IO_PENDING) {
            error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "ConnectEx");
            goto failure;
        }
    }

    raptor_sockaddr_to_string(&str_addr, &mapped_addr, 0);
    if (str_addr) {
        log_info("TcpConnector: start connecting %s", str_addr);
        free(str_addr);
    }
    return RAPTOR_ERROR_NONE;

failure:
    if (entry->fd != INVALID_SOCKET) {
        closesocket(entry->fd);
    }
    delete entry;
    return error;
}

}  // namespace raptor
