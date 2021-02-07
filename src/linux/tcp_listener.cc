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
#include <stdlib.h>
#include <string.h>

#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/property.h"
#include "raptor-lite/utils/log.h"

#include "src/common/endpoint_impl.h"
#include "src/common/sockaddr.h"
#include "src/common/socket_util.h"
#include "src/linux/socket_setting.h"

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
    , _shutdown(true) {
    RAPTOR_LIST_INIT(&_head);
}

TcpListener::~TcpListener() {
    Shutdown();
}

RefCountedPtr<Status> TcpListener::Init(int threads) {
    if (!_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpAcceptor is already running");
    }
    _poll_thread   = std::make_shared<PollingThread>(this);
    raptor_error e = _poll_thread->Init(threads, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to init poll thread, %s", e->ToString().c_str());
        return e;
    }
    _poll_thread->EnableTimeoutCheck(false);

    _shutdown = false;

    return RAPTOR_ERROR_NONE;
}

RefCountedPtr<Status> TcpListener::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("TcpAcceptor is not initialized");
    }

    return _poll_thread->Start();
}

void TcpListener::Shutdown() {
    if (!_shutdown) {
        log_warn("TcpListener: prepare to shutdown");
        _shutdown = true;
        _poll_thread->Shutdown();

        _mtex.Lock();
        list_entry *entry = _head.next;
        while (entry != &_head) {
            auto obj = reinterpret_cast<ListenerObject *>(entry);
            entry    = entry->next;

            close(obj->listen_fd);
            delete obj;
        }
        RAPTOR_LIST_INIT(&_head);
        _mtex.Unlock();
    }
}

RefCountedPtr<Status> TcpListener::AddListeningPort(const raptor_resolved_address *addr) {
    if (_shutdown) return RAPTOR_ERROR_FROM_STATIC_STRING("tcp listener uninitialized");

    int port = 0, listen_fd = 0;
    raptor_dualstack_mode mode;
    raptor_error e = raptor_create_dualstack_socket(addr, SOCK_STREAM, 0, &mode, &listen_fd);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to create socket: %s", e->ToString().c_str());
        return e;
    }
    e = raptor_tcp_server_prepare_socket(listen_fd, addr, &port, 1);
    if (e != RAPTOR_ERROR_NONE) {
        log_error("TcpListener: Failed to configure socket: %s", e->ToString().c_str());
        return e;
    }

    // Add to epoll
    _mtex.Lock();
    ListenerObject *node = new ListenerObject;
    raptor_list_push_back(&_head, &node->entry);

    node->addr      = *addr;
    node->listen_fd = listen_fd;
    node->port      = port;
    node->mode      = mode;

    _poll_thread->Add(node->listen_fd, node, EPOLLIN);
    _mtex.Unlock();

    char *strAddr = nullptr;
    raptor_sockaddr_to_string(&strAddr, addr, 0);
    log_info("TcpListener: start listening on %s",
             strAddr ? strAddr : std::to_string(node->port).c_str());
    if (strAddr) free(strAddr);
    return e;
}

void TcpListener::OnTimeoutCheck(int64_t) {}
void TcpListener::OnEventProcess(EventDetail *detail) {
    ListenerObject *sp = (ListenerObject *)detail->ptr;

    if (!(detail->event_type & internal::kErrorEvent)) {
        raptor_resolved_address client;
        int sock_fd = AcceptEx(sp->listen_fd, &client, 1, 1);
        if (sock_fd > 0) {
            std::shared_ptr<EndpointImpl> obj = std::make_shared<EndpointImpl>(sock_fd, &client);
            obj->SetListenPort(sp->port);
            Property property;
            _handler->OnAccept(obj, property);
            if (obj->IsOnline()) {
                ProcessProperty(sock_fd, property);
            }
        } else {
            if (errno != EINTR && errno != EAGAIN) {
                log_error("TcpListener: Failed accept on port %d (%s)", sp->port, strerror(errno));
            }
        }
    }
}

int TcpListener::AcceptEx(int sockfd, raptor_resolved_address *resolved_addr, int nonblock,
                          int cloexec) {
    int flags = 0;
    flags |= nonblock ? SOCK_NONBLOCK : 0;
    flags |= cloexec ? SOCK_CLOEXEC : 0;
    resolved_addr->len = sizeof(resolved_addr->addr);
    return accept4(sockfd, reinterpret_cast<raptor_sockaddr *>(resolved_addr->addr),
                   reinterpret_cast<socklen_t *>(&resolved_addr->len), flags);
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
