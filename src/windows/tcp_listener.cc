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

#include "src/windows/tcp_listener.h"
#include <string.h>
#include "raptor-lite/impl/acceptor.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/property.h"
#include "raptor-lite/utils/list_entry.h"
#include "raptor-lite/utils/log.h"

#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/windows/socket_setting.h"

namespace raptor {

struct ListenInformation {
    list_entry head;  // list head
    int listen_port;
    int count;
    SOCKET listen_fd;
    raptor_resolved_address addr;  // for acceptex
    Mutex mutex;                   // for list
    ListenInformation() {
        RAPTOR_LIST_INIT(&head);
        listen_port = 0;
        count = 0;
        listen_fd = INVALID_SOCKET;
        memset(&addr, 0, sizeof(addr));
    }
    ~ListenInformation() {
        if (listen_fd != INVALID_SOCKET) {
            closesocket(listen_fd);
        }
    }
};

struct AcceptObject {
    list_entry entry;
    SOCKET new_socket;
    raptor_dualstack_mode mode;
    uint8_t addr_buffer[(sizeof(raptor_sockaddr_in6) + 16) * 2];
    OverLappedEx olex;
    AcceptObject() {
        RAPTOR_LIST_ENTRY_INIT(&entry);
        new_socket = INVALID_SOCKET;
        mode = RAPTOR_DSMODE_NONE;
        memset(addr_buffer, 0, sizeof(addr_buffer));
        memset(&olex, 0, sizeof(olex));
    }
    ~AcceptObject() {
        if (new_socket != INVALID_SOCKET) {
            closesocket(new_socket);
        }
    }
};
constexpr uint32_t AcceptBaseHandleId = 9527;
TcpListener::TcpListener(AcceptorHandler *service)
    : _service(service)
    , _shutdown(true)
    , _AcceptEx(nullptr)
    , _GetAcceptExSockAddrs(nullptr)
    , _threads(1) {}

TcpListener::~TcpListener() {
    Shutdown();
}

raptor_error TcpListener::Init(int threads) {
    if (!_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener is already running");

    _poll_thread = std::make_shared<PollingThread>(this);
    raptor_error e = _poll_thread->Init(threads, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to init poll thread, %s", e->ToString().c_str());
        return e;
    }
    _poll_thread->EnableTimeoutCheck(false);

    _shutdown = false;
    _threads = threads;

    return RAPTOR_ERROR_NONE;
}

raptor_error TcpListener::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener is not initialized");
    }

    return _poll_thread->Start();
}

void TcpListener::Shutdown() {
    if (!_shutdown) {
        log_warn("TcpListener: prepare to shutdown");
        _shutdown = true;
        _poll_thread->Shutdown();

        AutoMutex g(&_mutex);
        for (size_t i = 0; i < _heads.size(); i++) {
            ListenInformation *info = _heads[i];
            list_entry *entry = nullptr;
            do {
                entry = raptor_list_pop_back(&info->head);
                if (entry) {
                    delete reinterpret_cast<AcceptObject *>(entry);
                }
            } while (entry);

            delete info;
        }
        _heads.clear();
    }
}

raptor_error TcpListener::AddListeningPort(const raptor_resolved_address *addr) {
    if (_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener has been shutdown");
    raptor_resolved_address mapped_addr;
    raptor_dualstack_mode mode;
    SOCKET listen_fd;

    raptor_error e = raptor_create_socket(addr, &mapped_addr, &listen_fd, &mode);

    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to create socket: %s", e->ToString().c_str());
        return e;
    }

    if (!_AcceptEx || !_GetAcceptExSockAddrs) {
        e = GetExtensionFunction(listen_fd);
        if (e != RAPTOR_ERROR_NONE) {
            closesocket(listen_fd);
            return e;
        }
    }

    int port = 0;
    e = raptor_tcp_server_prepare_socket(listen_fd, &mapped_addr, &port, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to configure socket: %s", e->ToString().c_str());
        return e;
    }

    _mutex.Lock();
    size_t index = _heads.size();
    ListenInformation *info = new ListenInformation();
    info->listen_fd = listen_fd;
    info->listen_port = port;
    memcpy(&info->addr, &mapped_addr, sizeof(mapped_addr));
    _heads.emplace_back(info);
    _poll_thread->Add(listen_fd, info);
    _mutex.Unlock();

    info->mutex.Lock();
    for (size_t i = 0; i < _threads + 1; i++) {
        struct AcceptObject *node = new AcceptObject;
        node->mode = mode;
        node->olex.event_type = internal::kAcceptEvent;
        node->olex.HandleId = AcceptBaseHandleId + static_cast<uint32_t>(index);
        raptor_list_push_back(&info->head, &node->entry);

        e = StartAcceptEx(listen_fd, &info->addr, node);
        if (e != RAPTOR_ERROR_NONE) {
            log_error("TcpListener: Failed to StartAcceptEx, %s", e->ToString().c_str());
            raptor_list_remove_entry(&node->entry);
            delete node;
            info->mutex.Unlock();
            return e;
        }

        info->count++;

        if (i == 0) {
            char *addr_string = nullptr;
            raptor_sockaddr_to_string(&addr_string, &mapped_addr, 0);
            log_info("TcpListener: start listening on %s",
                     addr_string ? addr_string : std::to_string(port).c_str());
            if (addr_string) free(addr_string);
        }
    }
    info->mutex.Unlock();
    return RAPTOR_ERROR_NONE;
}

raptor_error TcpListener::GetExtensionFunction(SOCKET fd) {

    DWORD NumberofBytes;
    int status = 0;

    if (!_AcceptEx) {
        GUID guid = WSAID_ACCEPTEX;
        status = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &_AcceptEx,
                          sizeof(_AcceptEx), &NumberofBytes, NULL, NULL);

        if (status != 0) {
            _AcceptEx = NULL;
            raptor_error e = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "WSAIoctl");
            log_error("TcpListener: Failed to get AcceptEx: %s", e->ToString().c_str());
            return e;
        }
    }

    if (!_GetAcceptExSockAddrs) {
        GUID guid = WSAID_GETACCEPTEXSOCKADDRS;
        status = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                          &_GetAcceptExSockAddrs, sizeof(_GetAcceptExSockAddrs), &NumberofBytes,
                          NULL, NULL);

        if (status != 0) {
            _GetAcceptExSockAddrs = NULL;
            raptor_error e = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "WSAIoctl");
            log_error("TcpListener: Failed to get GetAcceptexSockAddrs: %s", e->ToString().c_str());
            return e;
        }
    }
    return RAPTOR_ERROR_NONE;
}

void TcpListener::OnTimeoutCheck(int64_t) {}

void TcpListener::OnEventProcess(EventDetail *detail) {
    RAPTOR_ASSERT(detail->event_type == internal::kErrorEvent ||
                  detail->event_type == internal::kAcceptEvent);

    size_t off = offsetof(AcceptObject, olex);
    AcceptObject *object = reinterpret_cast<AcceptObject *>(
        reinterpret_cast<intptr_t>(detail->overlaped) - static_cast<intptr_t>(off));

    ListenInformation *info = reinterpret_cast<ListenInformation *>(detail->ptr);

    raptor_resolved_address client;
    memset(&client, 0, sizeof(client));
    ParsingNewConnectionAddress(object, &client);

    auto ep = std::make_shared<EndpointImpl>(object->new_socket, &info->addr, &client);
    ep->SetListenPort(static_cast<uint16_t>(info->listen_port));

    Property property;
    _service->OnAccept(ep, property);
    ProcessProperty(object->new_socket, property);

    object->new_socket = INVALID_SOCKET;
    raptor_error e = StartAcceptEx(info->listen_fd, &info->addr, object);

    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to StartAcceptEx for next fd, %s", e->ToString().c_str());
        Shutdown();
    }
}

raptor_error TcpListener::StartAcceptEx(SOCKET listen_fd, raptor_resolved_address *addr,
                                        struct AcceptObject *sp) {
    BOOL success = false;
    DWORD addrlen = sizeof(raptor_sockaddr_in6) + 16;
    DWORD bytes_received = 0;
    raptor_error error = RAPTOR_ERROR_NONE;
    SOCKET sock;

    sock = WSASocket(((raptor_sockaddr *)addr->addr)->sa_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                     RAPTOR_WSA_SOCKET_FLAGS);

    if (sock == INVALID_SOCKET) {
        error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "WSASocket");
        goto failure;
    }

    error = raptor_tcp_prepare_socket(sock, 0);
    if (error != RAPTOR_ERROR_NONE) goto failure;

    sp->new_socket = sock;

    /* Start the "accept" asynchronously. */
    success = _AcceptEx(listen_fd, sock, sp->addr_buffer, 0, addrlen, addrlen, &bytes_received,
                        &sp->olex.overlapped);

    /* It is possible to get an accept immediately without delay. However, we
        will still get an IOCP notification for it. So let's just ignore it. */
    if (!success) {
        int last_error = WSAGetLastError();
        if (last_error != ERROR_IO_PENDING) {
            error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "AcceptEx");
            goto failure;
        }
    }

    // We're ready to do the accept.
    return RAPTOR_ERROR_NONE;

failure:
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    sp->new_socket = INVALID_SOCKET;
    return error;
}

void TcpListener::ParsingNewConnectionAddress(const AcceptObject *sp,
                                              raptor_resolved_address *client) {

    raptor_sockaddr *local = NULL;
    raptor_sockaddr *remote = NULL;

    int local_addr_len = sizeof(raptor_sockaddr_in6) + 16;
    int remote_addr_len = sizeof(raptor_sockaddr_in6) + 16;

    _GetAcceptExSockAddrs((void *)sp->addr_buffer, 0, sizeof(raptor_sockaddr_in6) + 16,
                          sizeof(raptor_sockaddr_in6) + 16, &local, &local_addr_len, &remote,
                          &remote_addr_len);

    /*
    if (local != nullptr) {
        server->len = local_addr_len;
        memcpy(server->addr, local, local_addr_len);
    }
    */
    if (remote != nullptr) {
        client->len = remote_addr_len;
        memcpy(client->addr, remote, remote_addr_len);
    }
}

void TcpListener::ProcessProperty(SOCKET fd, const Property &p) {
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
