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

#ifndef __RAPTOR_CORE_LINUX_TCP_SERVER__
#define __RAPTOR_CORE_LINUX_TCP_SERVER__

#include <time.h>
#include <map>
#include <memory>
#include <list>
#include <utility>
#include <vector>

#include "src/linux/epoll_thread.h"
#include "src/linux/connection.h"
#include "raptor-lite/utils/mpscq.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/impl/handler.h"
#include "src/common/service.h"

namespace raptor {
class IProtocol;
class TcpListener;
struct TcpMessageNode;
class TcpContainer : public Container,
                     public internal::IEpollReceiver,
                     public internal::INotificationTransfer {
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

    explicit TcpContainer(TcpContainer::Option *option);
    ~TcpContainer();

    raptor_error Init();
    bool Start();
    void Shutdown();
    void AttachEndpoint(Endpoint *);
    bool SendMsg(Endpoint *ep, const void *data, size_t len);
    bool CloseConnection(ConnectionId cid);

    // internal::IEpollReceiver implement
    void OnErrorEvent(void *ptr) override;
    void OnRecvEvent(void *ptr) override;
    void OnSendEvent(void *ptr) override;
    void OnCheckingEvent(time_t current) override;

    // internal::INotificationTransfer impl
    void OnConnectionArrived(ConnectionId cid, const Slice *addr);
    void OnDataReceived(ConnectionId cid, const Slice *s) override;
    void OnConnectionClosed(ConnectionId cid) override;

    // user data
    bool SetUserData(ConnectionId cid, void *ptr);
    bool GetUserData(ConnectionId cid, void **ptr);
    bool SetExtendInfo(ConnectionId cid, uint64_t data);
    bool GetExtendInfo(ConnectionId cid, uint64_t &data);
    int GetPeerString(ConnectionId cid, char *buf, int buf_len);

private:
    void TimeoutCheckThread(void *);
    void MessageQueueThread(void *);
    uint32_t CheckConnectionId(ConnectionId cid) const;
    void Dispatch(struct TcpMessageNode *msg);
    void DeleteConnection(uint32_t index);
    void RefreshTime(uint32_t index);
    std::shared_ptr<Connection> GetConnection(uint32_t index);

private:
    using TimeoutRecordMap = std::multimap<int64_t, uint32_t>;
    using ConnectionInfo = std::pair<Connection *, TimeoutRecordMap::iterator>;

    bool _shutdown;
    TcpContainer::Option _option;

    MultiProducerSingleConsumerQueue _mpscq;
    Thread _mq_thd;
    Mutex _mutex;
    ConditionVariable _cv;
    AtomicUInt32 _count;

    std::shared_ptr<SendRecvThread> _recv_thread;
    std::shared_ptr<SendRecvThread> _send_thread;

    Mutex _conn_mtx;
    std::vector<ConnectionData> _mgr;
    // key: timeout deadline, value: index for _mgr
    TimeoutRecord _timeout_record_list;
    std::list<uint32_t> _free_index_list;
    uint16_t _magic_number;
    Atomic<time_t> _last_timeout_time;
};

}  // namespace raptor
#endif  // __RAPTOR_CORE_LINUX_TCP_SERVER__
