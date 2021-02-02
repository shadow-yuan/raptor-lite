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

#include <string>

#include "src/windows/iocp.h"
#include "src/common/sockaddr.h"
#include "src/utils/list_entry.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/utils/thread.h"

namespace raptor {
class AcceptorHandler;
struct ListenerObject;
class Property;

class TcpListener final {
public:
    explicit TcpListener(AcceptorHandler *service);
    ~TcpListener();

    raptor_error Init(int threads = 1);
    raptor_error AddListeningPort(const raptor_resolved_address *addr);
    raptor_error Start();
    void Shutdown();

private:
    void WorkThread(void *ptr);
    raptor_error StartAcceptEx(struct ListenerObject *);
    void ParsingNewConnectionAddress(const ListenerObject *sp, raptor_resolved_address *remote);

    raptor_error GetExtensionFunction(SOCKET fd);
    void ProcessProperty(SOCKET fd, const Property &p);

    AcceptorHandler *_service;
    bool _shutdown;
    int _number_of_thread;
    int _running_threads;
    LPFN_ACCEPTEX _AcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS _GetAcceptExSockAddrs;

    Thread *_threads;
    list_entry _head;
    Iocp _iocp;
    Mutex _mutex;  // for list_entry
    OVERLAPPED _exit;
};

}  // namespace raptor

#endif  // __RAPTOR_CORE_WINDOWS_TCP_LISTENER__
