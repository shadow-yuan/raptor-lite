#include "src/common/endpoint_impl.h"
#include "src/common/cid.h"
#include "src/common/socket_util.h"

namespace raptor {

EndpointImpl::EndpointImpl(uint64_t fd, raptor_resolved_address *addr)
    : _socket_fd(fd)
    , _connection_id(core::InvalidConnectionId)
    , _container(nullptr) {
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

std::shared_ptr<EndpointImpl> EndpointImpl::GetEndpoint() {
    return this->shared_from_this();
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

bool EndpointImpl::SendMsg(const Slice &slice) const {
    if (_container) {
        // TODO(SHADOW)
    }
    return false;
}

bool EndpointImpl::SendMsg(void *data, size_t len) const {
    if (_container) {
        // TODO(SHADOW)
    }
    return false;
}

bool EndpointImpl::Close() const {
    if (_container) {
        // TODO(SHADOW)
    }
    return false;
}

const std::string &EndpointImpl::LocalIp() const {
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

const std::string &EndpointImpl::RemoteIp() const {
    char ip[128] = {0};
    int ret = raptor_sockaddr_get_ip(&_address, ip, sizeof(ip));
    return std::string(ip, ret);
}

uint16_t EndpointImpl::RemotePort() const {
    return raptor_sockaddr_get_port(&_address);
}

}  // namespace raptor
