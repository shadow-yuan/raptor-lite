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

#ifndef __RAPTOR_CORE_LINUX_TCP_LISTENER__
#define __RAPTOR_CORE_LINUX_TCP_LISTENER__

#include "src/linux/epoll.h"
#include "src/common/resolve_address.h"
#include "raptor-lite/utils/list_entry.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/utils/thread.h"
#include "raptor-lite/impl/acceptor.h"

namespace raptor {
class AcceptorHandler;
struct ListenerObject;

class TcpListener final {
public:
    explicit TcpListener(AcceptorHandler *handler);
    ~TcpListener();

    RefCountedPtr<Status> Init(int threads = 1);
    RefCountedPtr<Status> AddListeningPort(const raptor_resolved_address *addr);
    RefCountedPtr<Status> Start();
    void Shutdown();

private:
    void DoPolling(void *ptr);
    void ProcessEpollEvents(void *ptr, uint32_t events);
    int AcceptEx(int fd, raptor_resolved_address *addr, int nonblock, int cloexec);
    void ProcessProperty(int fd, const Property &p);

private:
    AcceptorHandler *_handler;
    bool _shutdown;
    int _number_of_thread;
    int _running_threads;

    Thread *_threads;
    Epoll _epoll;
    list_entry _head;
    Mutex _mtex;
};

}  // namespace raptor
#endif  // __RAPTOR_CORE_LINUX_TCP_LISTENER__
