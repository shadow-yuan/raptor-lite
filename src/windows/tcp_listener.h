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

#ifndef __RAPTOR_CORE_WINDOWS_TCP_LISTENER__
#define __RAPTOR_CORE_WINDOWS_TCP_LISTENER__

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "raptor-lite/utils/list_entry.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/utils/thread.h"
#include "src/common/resolve_address.h"
#include "src/common/sockaddr.h"
#include "src/windows/iocp_thread.h"

namespace raptor {
class AcceptorHandler;
struct AcceptObject;
struct ListenInformation;
class Property;

class TcpListener final : public internal::EventReceivingService {
public:
    explicit TcpListener(AcceptorHandler *service);
    ~TcpListener();

    raptor_error Init(int threads = 1);
    raptor_error AddListeningPort(const raptor_resolved_address *addr);
    raptor_error Start();
    void Shutdown();

private:
    void OnEventProcess(EventDetail *detail) override;
    void OnTimeoutCheck(int64_t current_millseconds) override;

    raptor_error StartAcceptEx(SOCKET listen_fd, struct AcceptObject *);
    void ParsingNewConnectionAddress(const AcceptObject *sp, raptor_resolved_address *remote);

    raptor_error GetExtensionFunction(SOCKET fd);
    void ProcessProperty(SOCKET fd, const Property &p);

    AcceptorHandler *_service;
    bool _shutdown;

    LPFN_ACCEPTEX _AcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockAddrs;
    int _threads;

    Mutex _mutex;  // for _heads
    std::vector<ListenInformation *> _heads;

    std::shared_ptr<PollingThread> _poll_thread;
};

}  // namespace raptor

#endif  // __RAPTOR_CORE_WINDOWS_TCP_LISTENER__
