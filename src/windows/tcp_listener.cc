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
#include <memory>
#include "raptor-lite/impl/acceptor.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/property.h"
#include "raptor-lite/utils/list_entry.h"
#include "raptor-lite/utils/log.h"

#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/windows/socket_setting.h"

namespace raptor {

struct ListenerObject {
    list_entry entry;
    SOCKET listen_fd;
    SOCKET new_socket;
    int port;
    raptor_dualstack_mode mode;
    uint8_t addr_buffer[(sizeof(raptor_sockaddr_in6) + 16) * 2];
    raptor_resolved_address addr;
    OVERLAPPED overlapped;
    ListenerObject() {
        RAPTOR_LIST_ENTRY_INIT(&entry);
        listen_fd = INVALID_SOCKET;
        new_socket = INVALID_SOCKET;
        port = 0;
        memset(addr_buffer, 0, sizeof(addr_buffer));
        memset(&addr, 0, sizeof(addr));
        memset(&overlapped, 0, sizeof(overlapped));
    }
    ~ListenerObject() {
        if (listen_fd != INVALID_SOCKET) {
            closesocket(listen_fd);
        }
        if (new_socket != INVALID_SOCKET) {
            closesocket(new_socket);
        }
    }
};

TcpListener::TcpListener(AcceptorHandler *service)
    : _service(service)
    , _shutdown(true)
    , _number_of_thread(0)
    , _running_threads(0)
    , _AcceptEx(nullptr)
    , _GetAcceptExSockAddrs(nullptr)
    , _threads(nullptr) {
    RAPTOR_LIST_INIT(&_head);
    memset(&_exit, 0, sizeof(_exit));
}

TcpListener::~TcpListener() {
    if (!_shutdown) {
        Shutdown();
    }
}

raptor_error TcpListener::Init(int threads) {
    if (!_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener is already running");

    auto e = _iocp.create(threads);
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }

    _shutdown = false;
    _number_of_thread = threads;
    _threads = new Thread[threads];

    for (int i = 0; i < threads; i++) {
        bool success = false;

        _threads[i] =
            Thread("Win32:listen", std::bind(&TcpListener::WorkThread, this, std::placeholders::_1),
                   nullptr, &success);

        if (!success) {
            break;
        }
        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error TcpListener::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener is not initialized");
    }

    for (int i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void TcpListener::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;
        _iocp.post(NULL, &_exit);
        for (int i = 0; i < _running_threads; i++) {
            _threads[i].Join();
        }

        AutoMutex g(&_mutex);
        list_entry *entry = _head.next;
        while (entry != &_head) {
            auto obj = reinterpret_cast<ListenerObject *>(entry);
            entry = entry->next;
            delete obj;
        }
        RAPTOR_LIST_INIT(&_head);

        delete[] _threads;
        _threads = nullptr;
    }
}

raptor_error TcpListener::AddListeningPort(const raptor_resolved_address *addr) {
    if (_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("TcpListener has been shutdown");
    raptor_resolved_address mapped_addr;
    raptor_dualstack_mode mode;
    SOCKET listen_fd;

    raptor_error e = raptor_create_socket(addr, &mapped_addr, &listen_fd, &mode);

    if (e != RAPTOR_ERROR_NONE) {
        log_error("Failed to create socket: %s", e->ToString().c_str());
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
        log_error("Failed to configure socket: %s", e->ToString().c_str());
        return e;
    }

    _mutex.Lock();
    std::unique_ptr<ListenerObject> node(new ListenerObject);
    node->listen_fd = listen_fd;
    node->port = port;
    node->mode = mode;
    node->addr = mapped_addr;
    if (!_iocp.add(node->listen_fd, node.get())) {
        _mutex.Unlock();
        return RAPTOR_ERROR_FROM_STATIC_STRING("Failed to bind iocp");
    }
    e = StartAcceptEx(node.get());
    if (e != RAPTOR_ERROR_NONE) {
        _mutex.Unlock();
        return e;
    }

    raptor_list_push_back(&_head, &node->entry);
    node.release();
    _mutex.Unlock();

    char *addr_string = nullptr;
    raptor_sockaddr_to_string(&addr_string, &mapped_addr, 0);
    log_debug("start listening on %s",
              addr_string ? addr_string : std::to_string(node->port).c_str());
    free(addr_string);
    return e;
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
            log_error("Failed to get AcceptEx: %s", e->ToString().c_str());
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
            log_error("Failed to get GetAcceptexSockAddrs: %s", e->ToString().c_str());
            return e;
        }
    }
    return RAPTOR_ERROR_NONE;
}

void TcpListener::WorkThread(void *) {
    while (!_shutdown) {
        DWORD NumberOfBytesTransferred = 0;
        ListenerObject *CompletionKey = NULL;
        LPOVERLAPPED lpOverlapped = NULL;
        bool ret = _iocp.polling(&NumberOfBytesTransferred, (PULONG_PTR)&CompletionKey,
                                 &lpOverlapped, INFINITE);

        if (!ret) {
            continue;
        }

        if (lpOverlapped == &_exit) {  // shutdown
            break;
        }

        raptor_resolved_address client;
        memset(&client, 0, sizeof(client));
        ParsingNewConnectionAddress(CompletionKey, &client);

        auto ep = std::make_shared<EndpointImpl>(CompletionKey->new_socket, &client);
        ep->SetListenPort(static_cast<uint16_t>(CompletionKey->port));
        Property property;
        _service->OnAccept(ep, property);
        ProcessProperty(CompletionKey->new_socket, property);

        CompletionKey->new_socket = INVALID_SOCKET;
        raptor_error e = StartAcceptEx(CompletionKey);
        if (e != RAPTOR_ERROR_NONE) {
            log_error("TcpListener: AcceptEx next fd error: %s", e->ToString().c_str());
            break;
        }
    }
}

raptor_error TcpListener::StartAcceptEx(struct ListenerObject *sp) {
    BOOL success = false;
    DWORD addrlen = sizeof(raptor_sockaddr_in6) + 16;
    DWORD bytes_received = 0;
    raptor_error error = RAPTOR_ERROR_NONE;
    SOCKET sock;

    sock = WSASocket(((raptor_sockaddr *)sp->addr.addr)->sa_family, SOCK_STREAM, IPPROTO_TCP, NULL,
                     0, RAPTOR_WSA_SOCKET_FLAGS);

    if (sock == INVALID_SOCKET) {
        error = RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "WSASocket");
        goto failure;
    }

    error = raptor_tcp_prepare_socket(sock, 0);
    if (error != RAPTOR_ERROR_NONE) goto failure;

    /* Start the "accept" asynchronously. */
    success = _AcceptEx(sp->listen_fd, sock, sp->addr_buffer, 0, addrlen, addrlen, &bytes_received,
                        &sp->overlapped);

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
    sp->new_socket = sock;
    return RAPTOR_ERROR_NONE;

failure:
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    return error;
}

void TcpListener::ParsingNewConnectionAddress(const ListenerObject *sp,
                                              raptor_resolved_address *client) {

    raptor_sockaddr *local = NULL;
    raptor_sockaddr *remote = NULL;

    int local_addr_len = sizeof(raptor_sockaddr_in6) + 16;
    int remote_addr_len = sizeof(raptor_sockaddr_in6) + 16;

    _GetAcceptExSockAddrs((void *)sp->addr_buffer, 0, sizeof(raptor_sockaddr_in6) + 16,
                          sizeof(raptor_sockaddr_in6) + 16, &local, &local_addr_len, &remote,
                          &remote_addr_len);

    if (remote != nullptr) {
        client->len = remote_addr_len;
        memcpy(client->addr, remote, remote_addr_len);
    }
}

void TcpListener::ProcessProperty(SOCKET fd, const Property &p) {
    /*
        raptor_set_socket_no_sigpipe_if_possible(sock_fd);
        raptor_set_socket_reuse_addr(sock_fd, 1);
        raptor_set_socket_rcv_timeout(sock_fd, 5000);
        raptor_set_socket_snd_timeout(sock_fd, 5000);
    */
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

    int SocketSendTimeout = 0;
    if (p.CheckValue<int>("SocketSendTimeout", SocketSendTimeout) && SocketSendTimeout > 0) {
        raptor_set_socket_snd_timeout(fd, SocketSendTimeout);
    }

    int SocketRecvTimeout = 0;
    if (p.CheckValue<int>("SocketRecvTimeout", SocketRecvTimeout) && SocketRecvTimeout > 0) {
        raptor_set_socket_rcv_timeout(fd, SocketRecvTimeout);
    }
}

}  // namespace raptor
