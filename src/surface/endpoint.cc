#include "raptor-lite/impl/endpoint.h"
#include "src/common/endpoint_impl.h"

namespace raptor {

Endpoint::Endpoint(std::shared_ptr<EndpointImpl> impl)
    : _impl(impl) {}

Endpoint::~Endpoint() {}

uint64_t Endpoint::ConnectionId() const {
    return _impl->ConnectionId();
}

uint64_t Endpoint::SocketFd() const {
    return _impl->SocketFd();
}

std::string Endpoint::PeerString() const {
    return _impl->PeerString();
}

bool Endpoint::SendMsg(const Slice &slice) const {
    return _impl->SendMsg(slice);
}

bool Endpoint::SendMsg(void *data, size_t len) const {
    return _impl->SendMsg(data, len);
}

bool Endpoint::Close() const {
    return _impl->Close();
}

const std::string &Endpoint::LocalIp() const {
    return _impl->LocalIp();
}

uint16_t Endpoint::LocalPort() const {
    return _impl->LocalPort();
}

const std::string &Endpoint::RemoteIp() const {
    return _impl->RemoteIp();
}

uint16_t Endpoint::RemotePort() const {
    return _impl->RemotePort();
}
}  // namespace raptor
