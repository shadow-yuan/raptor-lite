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

#ifndef __RAPTOR_CORE_WINDOWS_IOCP_THREAD__
#define __RAPTOR_CORE_WINDOWS_IOCP_THREAD__

#include <stddef.h>

#include "src/common/service.h"
#include "src/windows/iocp.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/thread.h"

namespace raptor {
class IocpThread {
public:
    explicit IocpThread(internal::IIocpReceiver *service);
    ~IocpThread();
    raptor_error Init(size_t rs_threads, size_t kernel_threads);
    raptor_error Start();
    void Shutdown();
    bool Add(SOCKET sock, void *CompletionKey);
    void EnableTimeoutCheck(bool b);

private:
    void WorkThread(void *);

    internal::IIocpReceiver *_service;
    bool _shutdown;
    bool _enable_timeout_check;
    size_t _number_of_threads;
    size_t _running_threads;

    Thread *_threads;

    OVERLAPPED _exit;
    Iocp _iocp;
};
}  // namespace raptor

#endif  // __RAPTOR_CORE_WINDOWS_IOCP_THREAD__
