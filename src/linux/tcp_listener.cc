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

#include "src/linux/tcp_listener.h"
#include <string.h>
#include "src/common/sockaddr.h"
#include "src/common/socket_util.h"
#include "src/utils/log.h"
#include "src/utils/list_entry.h"
#include "src/linux/socket_setting.h"
#include "src/common/endpoint_impl.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/property.h"

namespace raptor {

struct ListenerObject {
    list_entry entry;
    raptor_resolved_address addr;
    int listen_fd;
    int port;
    raptor_dualstack_mode mode;
};

TcpListener::TcpListener(AcceptorHandler *handler)
    : _handler(handler)
    , _shutdown(true)
    , _number_of_thread(0)
    , _running_threads(0)
    , _threads(nullptr) {
    RAPTOR_LIST_INIT(&_head);
}

TcpListener::~TcpListener() {
    RAPTOR_ASSERT(_shutdown);
}

RefCountedPtr<Status> TcpListener::Init(int threads) {
    if (!_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpAcceptor is already running");
    }
    auto e = _epoll.create();
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }
    _shutdown = false;
    _number_of_thread = threads;
    _threads = new Thread[threads];

    for (int i = 0; i < threads; i++) {
        bool success = false;

        _threads[i] =
            Thread("Linux:listen", std::bind(&TcpListener::DoPolling, this, std::placeholders::_1),
                   &success);

        if (!success) {
            break;
        }
        _running_threads++;
    }

    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpAcceptor failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

RefCountedPtr<Status> TcpListener::StartListening() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpAcceptor is not initialized");
    }

    for (int i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void TcpListener::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;
        _thd.Join();

        _mtex.Lock();
        list_entry *entry = _head.next;
        while (entry != &_head) {
            auto obj = reinterpret_cast<ListenerObject *>(entry);
            entry = entry->next;

            raptor_set_socket_shutdown(obj->listen_fd);
            delete obj;
        }
        RAPTOR_LIST_INIT(&_head);
        _mtex.Unlock();
    }
}

void TcpListener::DoPolling(void *ptr) {
    while (!_shutdown) {
        int number_of_fd = _epoll.polling();
        if (number_of_fd <= 0) {
            continue;
        }

        for (int i = 0; i < number_of_fd; i++) {
            struct epoll_event *ev = _epoll.get_event(i);
            ProcessEpollEvents(ev->data.ptr, ev->events);
        }
    }
}

RefCountedPtr<Status> TcpListener::AddListeningPort(const raptor_resolved_address *addr) {
    if (_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("tcp listener uninitialized");

    int port = 0, listen_fd = 0;
    raptor_dualstack_mode mode;
    raptor_error e = raptor_create_dualstack_socket(addr, SOCK_STREAM, 0, &mode, &listen_fd);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("Failed to create socket: %s", e->ToString().c_str());
        return e;
    }
    e = raptor_tcp_server_prepare_socket(listen_fd, addr, &port, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("Failed to configure socket: %s", e->ToString().c_str());
        return e;
    }

    // Add to epoll
    _mtex.Lock();
    ListenerObject *node = new ListenerObject;
    raptor_list_push_back(&_head, &node->entry);
    _mtex.Unlock();

    node->addr = *addr;
    node->listen_fd = listen_fd;
    node->port = port;
    node->mode = mode;

    _epoll.add(node->listen_fd, node, EPOLLIN);

    char *strAddr = nullptr;
    raptor_sockaddr_to_string(&strAddr, addr, 0);
    log_debug("start listening on %s", strAddr ? strAddr : std::to_string(node->port).c_str());
    Free(strAddr);
    return e;
}

void TcpListener::ProcessEpollEvents(void *ptr, uint32_t events) {
    ListenerObject *sp = (ListenerObject *)ptr;
    RAPTOR_ASSERT(sp != nullptr);
    for (;;) {
        raptor_resolved_address client;
        int sock_fd = AcceptEx(sp->listen_fd, &client, 1, 1);
        if (sock_fd > 0) {
            // TODO(SHADOW): sp->port not use
            Endpoint ep(std::make_shared<EndpointImpl>(sock_fd, &client));
            Property property;
            _handler->OnAccept(&ep, &property);
            ProcessProperty(sock_fd, property);
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN) {
            return;
        }
        log_error("Failed accept: %s on port: %d", strerror(errno), sp->port);
    }
}

int TcpListener::AcceptEx(int sockfd, raptor_resolved_address *resolved_addr, int nonblock,
                          int cloexec) {
    int flags = 0;
    flags |= nonblock ? SOCK_NONBLOCK : 0;
    flags |= cloexec ? SOCK_CLOEXEC : 0;
    resolved_addr->len = sizeof(resolved_addr->addr);
    return accept4(sockfd, reinterpret_cast<raptor_sockaddr *>(resolved_addr->addr),
                   &resolved_addr->len, flags);
}

void TcpListener::ProcessProperty(int fd, const Property &p) {
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
