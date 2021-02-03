#include "src/windows/tcp_container.h"
#include "src/common/endpoint_impl.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/utils/log.h"
#include "src/windows/socket_setting.h"
#include "src/utils/time.h"

namespace raptor {
TcpContainer::TcpContainer(MessageHandler *handler)
    : _shutdown(true)
    , _iocp_thread(this) {
    int64_t n = Now();
    _magic_number = (n >> 16) & 0xffff;
}

TcpContainer::~TcpContainer() {}

raptor_error TcpContainer::Init(TcpContainer::Option *option) {
    memcpy(&_option, option, sizeof(_option));
    _connections.resize(_option.default_container_size);
    for (int i = 0; i < _option.default_container_size; i++) {
        _free_index_list.emplace_back(i);
    }
    return _iocp_thread.Init(_option.rs_thread, _option.rs_thread);
}

bool TcpContainer::Start() {
    return _iocp_thread.Start();
}

void TcpContainer::Shutdown() {
    _iocp_thread.Shutdown();
}

void TcpContainer::AttachEndpoint(Endpoint *ep) {
    if (_free_index_list.empty() && _connections.size() >= _option.max_container_size) {
        log_error("The number of connections has exceeded the limit : %u",
                  _option.max_container_size);
        ep->Close();
        return;
    }

    AutoMutex g(&_mutex);
    if (_free_index_list.empty()) {
        size_t count = _connections.size();
        size_t expand =
            ((count * 2) < _option.max_container_size) ? (count * 2) : _option.max_container_size;
        _connections.resize(expand);

        for (size_t i = count; i < expand; i++) {
            _connections[i].first = nullptr;
            _connections[i].second = _timeout_records.end();
            _free_index_list.emplace_back(i);
        }
    }

    uint32_t index = _free_index_list.front();
    _free_index_list.pop_front();

    ConnectionId cid = core::BuildConnectionId(_magic_number, ep->GetListenPort(), index);

    int64_t deadline_second = GetCurrentMilliseconds() + _option.connection_timeoutms;

    std::unique_ptr<Connection> conn(new Connection(this));
    std::shared_ptr<EndpointImpl> obj = ep->_impl;
    obj->SetConnection(cid);
    // obj->SetContainer(this);
    conn->Init(obj);
    conn->SetProtocol(_option.proto_handler);

    // associate with iocp
    _iocp_thread.Add(obj->SocketFd(), (void *)cid);

    // submit the first asynchronous read
    if (conn->AsyncRecv()) {
        conn->Shutdown(true);
        _free_index_list.push_back(index);
    } else {
        _connections[index].first = conn.release();
        _connections[index].second = _timeout_records.insert({deadline_second, index});
    }
}

bool TcpContainer::SendMsg(Endpoint *ep, const void *data, size_t len) {}

void TcpContainer::OnErrorEvent(void *ptr, size_t err_code) {}
void TcpContainer::OnRecvEvent(void *ptr, size_t transferred_bytes) {}
void TcpContainer::OnSendEvent(void *ptr, size_t transferred_bytes) {}
void TcpContainer::OnCheckingEvent(time_t current) {}
}  // namespace raptor
