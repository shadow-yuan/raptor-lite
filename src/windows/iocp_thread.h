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
#include <stdint.h>
#include <memory>

#include "src/common/service.h"
#include "src/windows/iocp.h"
#include "raptor-lite/utils/ref_counted.h"
#include "raptor-lite/utils/status.h"
#include "raptor-lite/utils/thread.h"

namespace raptor {
typedef struct {
    OVERLAPPED overlapped;
    int event_type;
    uint32_t HandleId;
} OverLappedEx;

struct EventDetail {
    uint64_t ptr;
    int event_type;
    int error_code;
    uint32_t transferred_bytes;
    uint32_t handle_id;
    OVERLAPPED *overlaped;
};

class IocpThread {
public:
    explicit IocpThread(internal::EventReceivingService *service);
    ~IocpThread();
    raptor_error Init(size_t rs_threads, size_t kernel_threads = 0);
    raptor_error Start();
    void Shutdown();
    bool Add(SOCKET sock, void *CompletionKey);
    void EnableTimeoutCheck(bool b);

private:
    void WorkThread(void *);

    internal::EventReceivingService *_service;
    bool _shutdown;
    bool _enable_timeout_check;
    size_t _number_of_threads;
    size_t _running_threads;

    Thread *_threads;

    OVERLAPPED _exit;
    Iocp _iocp;
};

class IocpThreadAdaptor;
class PollingThread final {
public:
    explicit PollingThread(internal::EventReceivingService *service);
    ~PollingThread();

    PollingThread(const PollingThread &) = delete;
    PollingThread &operator=(const PollingThread &) = delete;

    raptor_error Init(size_t rs_threads, size_t kernel_threads = 0);
    raptor_error Start();
    void Shutdown();
    bool Add(SOCKET sock, uint64_t CompletionKey, int type);
    int Modify(SOCKET sock, uint64_t CompletionKey, int type);
    int Delete(SOCKET sock, int type = 0);
    void EnableTimeoutCheck(bool b);

private:
    internal::EventReceivingService *_service;
    RefCountedPtr<IocpThreadAdaptor> _impl;
    bool _enable_timeout_check;
    int32_t _instance_id;
};

}  // namespace raptor

#endif  // __RAPTOR_CORE_WINDOWS_IOCP_THREAD__
