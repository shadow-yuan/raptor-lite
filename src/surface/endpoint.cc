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

uint16_t Endpoint::GetListenPort() const {
    return _impl->GetListenPort();
}

std::string Endpoint::PeerString() const {
    return _impl->PeerString();
}

void Endpoint::BindWithContainer(Container *container) {
    _impl->BindWithContainer(container);
}

bool Endpoint::SendMsg(const Slice &slice) {
    if (slice.Empty()) return false;
    return _impl->SendMsg(slice);
}

bool Endpoint::SendMsg(const void *data, size_t len) {
    if (!data || len == 0) return false;
    return _impl->SendMsg(data, len);
}

int Endpoint::SyncSend(const void *data, size_t len) {
    if (!data || len == 0) return 0;
    return _impl->SyncSend(data, len);
}

int Endpoint::SyncRecv(void *data, size_t len) {
    if (!data || len == 0) return 0;
    return _impl->SyncRecv(data, len);
}

void Endpoint::Close(bool notify) {
    _impl->Close(notify);
}

std::string Endpoint::LocalIp() const {
    return _impl->LocalIp();
}

uint16_t Endpoint::LocalPort() const {
    return _impl->LocalPort();
}

std::string Endpoint::RemoteIp() const {
    return _impl->RemoteIp();
}

uint16_t Endpoint::RemotePort() const {
    return _impl->RemotePort();
}
bool Endpoint::IsOnline() const {
    return _impl->IsOnline();
}
void Endpoint::SetExtInfo(uintptr_t info) {
    _impl->SetExtInfo(info);
}
uintptr_t Endpoint::GetExtInfo() const {
    return _impl->GetExtInfo();
}
}  // namespace raptor
