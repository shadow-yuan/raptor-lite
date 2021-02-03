#pragma once

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <list>
#include <vector>
#include <utility>

#include "raptor-lite/impl/handler.h"
#include "raptor-lite/utils/status.h"
#include "src/windows/iocp_thread.h"
#include "src/common/service.h"
#include "src/windows/connection.h"
#include "raptor-lite/utils/sync.h"

namespace raptor {
class Endpoint;
class Connection;
class TcpContainer : public internal::IIocpReceiver {
public:
    typedef struct {
        int rs_thread = 1;
        int default_container_size = 256;
        int max_container_size = 1048576;
        int connection_timeoutms = 60000;  // 1 min
        bool enable_connection_timeout = false;
        MessageHandler *msg_handler = nullptr;
        ProtocolHandler *proto_handler = nullptr;
        HeartbeatHandler *heartbeat_handler = nullptr;
        EventHandler *event_handler = nullptr;
    } Option;

    explicit TcpContainer(MessageHandler *handler);
    ~TcpContainer();

    raptor_error Init(TcpContainer::Option *option);

    bool Start();
    void Shutdown();
    void AttachEndpoint(Endpoint *);
    bool SendMsg(Endpoint *ep, const void *data, size_t len);

private:
    void OnErrorEvent(void *ptr, size_t err_code) override;
    void OnRecvEvent(void *ptr, size_t transferred_bytes) override;
    void OnSendEvent(void *ptr, size_t transferred_bytes) override;
    void OnCheckingEvent(time_t current) override;

private:
    bool _shutdown;
    SendRecvThread _iocp_thread;
    TcpContainer::Option _option;
    uint16_t _magic_number;
    
    // key: deadline, value: index
    std::multimap<int64_t, uint32_t> _timeout_records;
    using ConnectionInfo = std::pair<Connection *, std::multimap<int64_t, uint32_t>::iterator>;
    std::vector<ConnectionInfo> _connections;
    std::list<uint32_t> _free_index_list;
    Mutex _mutex;
};

}  // namespace raptor
