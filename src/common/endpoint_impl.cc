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

#include "src/common/endpoint_impl.h"
#include <string.h>
#include "src/common/cid.h"
#include "src/common/socket_util.h"
#include "raptor-lite/impl/container.h"

#ifdef _WIN32
#include "src/windows/socket_setting.h"
#else
#include "src/linux/socket_setting.h"
#endif
#include "raptor-lite/impl/endpoint.h"

namespace raptor {

EndpointImpl::EndpointImpl(uint64_t fd, raptor_resolved_address *local,
                           raptor_resolved_address *remote)
    : _fd(fd)
    , _connection_id(core::InvalidConnectionId)
    , _container(nullptr) {
    _listen_port = 0;
    _ext_info = 0;
    memcpy(&_local_addr, local, sizeof(raptor_resolved_address));
    memcpy(&_remote_addr, remote, sizeof(raptor_resolved_address));
}

EndpointImpl::~EndpointImpl() {}

void EndpointImpl::SetConnection(uint64_t connection_id) {
    _connection_id = connection_id;
}

void EndpointImpl::SetContainer(Container *container) {
    _container = container;
}

void EndpointImpl::SetListenPort(uint16_t port) {
    _listen_port = port;
}

uint64_t EndpointImpl::ConnectionId() const {
    return _connection_id;
}
uint64_t EndpointImpl::SocketFd() const {
    return _fd;
}

uint16_t EndpointImpl::GetListenPort() const {
    return _listen_port;
}

std::string EndpointImpl::PeerString() const {
    char *ptr = nullptr;
    int ret = raptor_sockaddr_to_string(&ptr, &_remote_addr, 1);
    if (ptr) {
        std::string peer(ptr, ret);
        free(ptr);
        return peer;
    }
    return std::string();
}

void EndpointImpl::BindWithContainer(Container *container, bool notify) {
    container->AttachEndpoint(GetEndpointImpl(), notify);
}

bool EndpointImpl::SendMsg(const Slice &slice) {
    if (_container) {
        return _container->SendMsg(_connection_id, slice);
    }
    return false;
}

bool EndpointImpl::SendMsg(const void *data, size_t len) {
    if (_container) {
        return _container->SendMsg(_connection_id, Slice(data, len));
    }
    return false;
}

int EndpointImpl::SyncSend(const void *data, size_t len) {
#ifdef _WIN32
    return ::send((SOCKET)_fd, (const char *)data, (int)len, 0);
#else
    return ::send((int)_fd, data, len, 0);
#endif
}

int EndpointImpl::SyncRecv(void *data, size_t len) {
#ifdef _WIN32
    return ::recv((SOCKET)_fd, (char *)data, (int)len, 0);
#else
    return ::recv((int)_fd, data, len, 0);
#endif
}

bool EndpointImpl::Close(bool notify) {
    if (_container) {
        _container->CloseEndpoint(GetEndpointImpl(), notify);
    } else {
#ifdef _WIN32
        raptor_set_socket_shutdown((SOCKET)_fd);
#else
        raptor_set_socket_shutdown((int)_fd);
#endif
        _fd = uint64_t(~0);
    }
    return true;
}

std::string EndpointImpl::LocalIp() const {
    char ip[128] = {0};
    int ret = raptor_sockaddr_get_ip(&_local_addr, ip, sizeof(ip));
    return std::string(ip, ret);
}

uint16_t EndpointImpl::LocalPort() const {
    return static_cast<uint16_t>(raptor_sockaddr_get_port(&_local_addr));
}

std::string EndpointImpl::RemoteIp() const {
    char ip[128] = {0};
    int ret = raptor_sockaddr_get_ip(&_remote_addr, ip, sizeof(ip));
    return std::string(ip, ret);
}

uint16_t EndpointImpl::RemotePort() const {
    return static_cast<uint16_t>(raptor_sockaddr_get_port(&_remote_addr));
}

bool EndpointImpl::IsOnline() const {
    return _fd != uint64_t(~0) && _fd > 0;
}

void EndpointImpl::SetExtInfo(uintptr_t info) {
    _ext_info = info;
}

uintptr_t EndpointImpl::GetExtInfo() const {
    return _ext_info;
}
}  // namespace raptor
