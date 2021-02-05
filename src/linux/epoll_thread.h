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

#ifndef __RAPTOR_CORE_EPOLL_THREAD__
#define __RAPTOR_CORE_EPOLL_THREAD__

#include <stdint.h>
#include "src/linux/epoll.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/thread.h"
#include "src/common/service.h"

namespace raptor {
struct EventDetail {
    void *ptr;
    int event_type;
    int error_code;
};

class PollingThread final {
public:
    explicit PollingThread(internal::EventReceivingService *rcv);
    ~PollingThread();

    raptor_error Init(int threads = 1, int useless = 0);
    raptor_error Start();
    void Shutdown();
    void EnableTimeoutCheck(bool b);

    int Add(int fd, void *data, uint32_t events);
    int Modify(int fd, void *data, uint32_t events);
    int Delete(int fd, uint32_t events);

private:
    void DoWork(void *ptr);
    internal::EventReceivingService *_receiver;
    bool _shutdown;
    bool _enable_timeout_check;
    int _number_of_threads;
    int _running_threads;
    Epoll _epoll;
    Thread *_threads;
};

}  // namespace raptor
#endif  // __RAPTOR_CORE_EPOLL_THREAD__
