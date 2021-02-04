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

#ifndef __RAPTOR_CORE_WINDOWS_TCP_SERVER__
#define __RAPTOR_CORE_WINDOWS_TCP_SERVER__

#include <stddef.h>
#include <stdint.h>
#include <map>
#include <list>
#include <vector>
#include <utility>

#include "raptor-lite/impl/container.h"
#include "raptor-lite/impl/handler.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/mpscq.h"
#include "raptor-lite/utils/sync.h"

#include "src/common/service.h"
#include "src/utils/timer.h"
#include "src/windows/iocp_thread.h"
#include "src/windows/connection.h"
#include "src/windows/connection.h"

namespace raptor {
class Endpoint;
class Timer;
struct TcpMessageNode;
class TcpContainer : public Container,
                     public TimerHandler,
                     public internal::IIocpReceiver,
                     public internal::INotificationTransfer {
public:
    typedef struct _Option {
        size_t mq_consumer_threads = 1;
        size_t recv_send_threads = 1;
        size_t default_container_size = 256;
        size_t max_container_size = 1048576;
        size_t connection_timeoutms = 60000;  // 1 min
        bool not_check_connection_timeout = true;
        MessageHandler *msg_handler = nullptr;
        ProtocolHandler *proto_handler = nullptr;
        HeartbeatHandler *heartbeat_handler = nullptr;
        EndpointClosedHandler *closed_handler = nullptr;
    } Option;

    explicit TcpContainer(TcpContainer::Option *option);
    ~TcpContainer();

    raptor_error Init();
    raptor_error Start() override;
    void Shutdown() override;
    raptor_error AttachEndpoint(const Endpoint &ep) override;
    bool SendMsg(const Endpoint &ep, const void *data, size_t len) override;
    void CloseEndpoint(const Endpoint &ep, bool event_notify = false) override;

    // internal::IIocpReceiver impl
    void OnErrorEvent(void *ptr, size_t err_code) override;
    void OnRecvEvent(void *ptr, size_t transferred_bytes, uint32_t handle_id) override;
    void OnSendEvent(void *ptr, size_t transferred_bytes, uint32_t handle_id) override;
    void OnTimeoutCheck(int64_t current_millseconds) override;

    // internal::INotificationTransfer impl
    void OnDataReceived(const Endpoint &ep, const Slice &s) override;
    void OnClosed(const Endpoint &ep, const Event &event) override;

private:
    void OnTimer(uint32_t tid1, uint32_t tid2) override;
    void MessageQueueThread(void *);
    bool CheckConnectionId(uint64_t cid, uint32_t *index) const;
    void Dispatch(struct TcpMessageNode *msg);
    void DeleteConnection(uint32_t index);
    void RefreshTime(uint32_t index);
    std::shared_ptr<Connection> GetConnection(uint32_t index);
    void OnTimerEvent(const Endpoint &ep);

private:
    using TimeoutRecordMap = std::multimap<int64_t, uint32_t>;
    using ConnectionInfo = std::pair<std::shared_ptr<Connection>, TimeoutRecordMap::iterator>;

    bool _shutdown;
    TcpContainer::Option _option;

    MultiProducerSingleConsumerQueue _mpscq;
    Thread *_mq_threads;
    Mutex _mutex;
    ConditionVariable _cv;
    AtomicUInt32 _count;
    int _running_threads;

    std::shared_ptr<IocpThread> _iocp_thread;
    std::shared_ptr<Timer> _timer_thread;

    Mutex _conn_mtx;
    std::vector<ConnectionInfo> _mgr;
    // key: timeout deadline, value: index for _mgr
    TimeoutRecordMap _timeout_records;
    std::list<uint32_t> _free_index_list;
    uint16_t _magic_number;
    Atomic<int64_t> _last_check_time;
};
}  // namespace raptor
#endif  // __RAPTOR_CORE_WINDOWS_TCP_SERVER__
