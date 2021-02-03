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

EndpointImpl::EndpointImpl(uint64_t fd, raptor_resolved_address *addr)
    : _socket_fd(fd)
    , _connection_id(core::InvalidConnectionId)
    , _container(nullptr) {
    _listen_port = 0;
    _ext_info = 0;
    memcpy(&_address, addr, sizeof(raptor_resolved_address));
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
    return _socket_fd;
}

uint16_t EndpointImpl::GetListenPort() const {
    return _listen_port;
}

std::string EndpointImpl::PeerString() const {
    char *ptr = nullptr;
    int ret = raptor_sockaddr_to_string(&ptr, &_address, 1);
    std::string peer(ptr, ret);
    free(ptr);
    return peer;
}

void EndpointImpl::BindWithContainer(Container *container) {
    container->AttachEndpoint(GetEndpointImpl());
}

bool EndpointImpl::SendMsg(const Slice &slice) {
    if (_container) {
        return _container->SendMsg(GetEndpointImpl(), slice.begin(), slice.size());
    }
    return false;
}

bool EndpointImpl::SendMsg(const void *data, size_t len) {
    if (_container) {
        return _container->SendMsg(GetEndpointImpl(), data, len);
    }
    return false;
}

int EndpointImpl::SyncSend(const void *data, size_t len) {
    return ::send(_socket_fd, data, len, 0);
}

int EndpointImpl::SyncRecv(void *data, size_t len) {
    return ::recv(_socket_fd, data, len, 0);
}

bool EndpointImpl::Close(bool notify) {
    if (_container) {
        _container->CloseEndpoint(GetEndpointImpl(), notify);
    } else {
        raptor_set_socket_shutdown(_socket_fd);
        _socket_fd = uint64_t(-1);
    }
    return true;
}

std::string EndpointImpl::LocalIp() const {
    raptor_resolved_address local;
    int ret = raptor_get_local_sockaddr(_socket_fd, &local);
    if (ret != 0) {
        return std::string();
    }

    char ip[128] = {0};
    ret = raptor_sockaddr_get_ip(&local, ip, sizeof(ip));
    return std::string(ip, ret);
}

uint16_t EndpointImpl::LocalPort() const {
    raptor_resolved_address local;
    int ret = raptor_get_local_sockaddr(_socket_fd, &local);
    if (ret != 0) {
        return 0;
    }

    return raptor_sockaddr_get_port(&local);
}

std::string EndpointImpl::RemoteIp() const {
    char ip[128] = {0};
    int ret = raptor_sockaddr_get_ip(&_address, ip, sizeof(ip));
    return std::string(ip, ret);
}

uint16_t EndpointImpl::RemotePort() const {
    return raptor_sockaddr_get_port(&_address);
}

bool EndpointImpl::IsOnline() const {
    return _socket_fd != uint64_t(-1) && _socket_fd > 0;
}

void EndpointImpl::SetExtInfo(uintptr_t info) {
    _ext_info = info;
}

uintptr_t EndpointImpl::GetExtInfo() const {
    return _ext_info;
}
}  // namespace raptor
