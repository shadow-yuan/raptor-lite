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

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <set>

#include "raptor-lite/impl/property.h"
#include "raptor-lite/utils/thread.h"
#include "raptor-lite/utils/sync.h"
#include "src/common/resolve_address.h"
#include "src/linux/epoll_thread.h"

namespace raptor {
class ConnectorHandler;
class TcpConnector final : public internal::EventReceivingService {
public:
    explicit TcpConnector(ConnectorHandler *handler);
    ~TcpConnector();

    raptor_error Init(int threads = 1, int tcp_user_timeout_ms = 0);
    raptor_error Start();
    void Shutdown();
    raptor_error Connect(const std::string &addr, intptr_t user);

private:
    void OnEventProcess(EventDetail *detail) override;
    void OnTimeoutCheck(int64_t current_millseconds) override;
    raptor_error AsyncConnect(const raptor_resolved_address *addr, intptr_t user, int timeout_ms);
    void ProcessProperty(int fd, const Property &p);

private:
    ConnectorHandler *_handler;
    bool _shutdown;

    int _tcp_user_timeout_ms;

    Mutex _mtex;
    std::set<intptr_t> _records;
    std::shared_ptr<PollingThread> _poll_thread;
};

}  // namespace raptor
