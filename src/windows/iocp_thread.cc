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

#include "src/windows/iocp_thread.h"
#include <string.h>
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/time.h"

namespace raptor {
IocpThread::IocpThread(internal::IIocpReceiver *service)
    : _service(service)
    , _shutdown(true)
    , _enable_timeout_check(false)
    , _number_of_threads(0)
    , _running_threads(0)
    , _threads(nullptr) {
    memset(&_exit, 0, sizeof(_exit));
}

IocpThread::~IocpThread() {
    if (!_shutdown) {
        Shutdown();
    }
}

RefCountedPtr<Status> IocpThread::Init(size_t rs_threads, size_t kernel_threads) {
    if (!_shutdown) return RAPTOR_ERROR_NONE;

    auto e = _iocp.create(static_cast<DWORD>(kernel_threads));
    if (e != RAPTOR_ERROR_NONE) {
        return e;
    }

    _shutdown = false;
    _number_of_threads = rs_threads;
    _threads = new Thread[rs_threads];
    _running_threads = 0;

    for (size_t i = 0; i < _number_of_threads; i++) {
        bool success = false;
        _threads[i] =
            Thread("iocp:thread", std::bind(&IocpThread::WorkThread, this, std::placeholders::_1),
                   nullptr, &success);
        if (!success) {
            break;
        }
        _running_threads++;
    }
    if (_running_threads == 0) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("IocpThread: Failed to create thread");
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error IocpThread::Start() {
    if (_shutdown) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("IocpThread is not initialized");
    }
    for (size_t i = 0; i < _running_threads; i++) {
        _threads[i].Start();
    }
    return RAPTOR_ERROR_NONE;
}

void IocpThread::Shutdown() {
    if (!_shutdown) {
        _shutdown = true;
        _iocp.post(NULL, &_exit);
        for (size_t i = 0; i < _running_threads; i++) {
            _threads[i].Join();
        }
        delete[] _threads;
        _threads = nullptr;
    }
}

bool IocpThread::Add(SOCKET sock, void *CompletionKey) {
    return _iocp.add(sock, CompletionKey);
}

void IocpThread::EnableTimeoutCheck(bool b) {
    _enable_timeout_check = b;
}

void IocpThread::WorkThread(void *) {
    while (!_shutdown) {

        if (_enable_timeout_check) {
            int64_t current_millseconds = GetCurrentMilliseconds();
            _service->OnTimeoutCheck(current_millseconds);
        }

        DWORD NumberOfBytesTransferred = 0;
        void *CompletionKey = NULL;
        LPOVERLAPPED lpOverlapped = NULL;

        // https://docs.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
        bool ret = _iocp.polling(&NumberOfBytesTransferred, (PULONG_PTR)&CompletionKey,
                                 &lpOverlapped, 1000);

        if (!ret) {

            /*
                If theGetQueuedCompletionStatus function succeeds, it dequeued a completion
                packet for a successful I/O operation from the completion port and has
                stored information in the variables pointed to by the following parameters:
                lpNumberOfBytes, lpCompletionKey, and lpOverlapped. Upon failure (the return
                value is FALSE), those same parameters can contain particular value
                combinations as follows:

                    (1) If *lpOverlapped is NULL, the function did not dequeue a completion
                        packet from the completion port. In this case, the function does not
                        store information in the variables pointed to by the lpNumberOfBytes
                        and lpCompletionKey parameters, and their values are indeterminate.

                    (2) If *lpOverlapped is not NULL and the function dequeues a completion
                        packet for a failed I/O operation from the completion port, the
                        function stores information about the failed operation in the
                        variables pointed to by lpNumberOfBytes, lpCompletionKey, and
                        lpOverlapped. To get extended error information, call GetLastError.
            */

            if (lpOverlapped != NULL && CompletionKey != NULL) {
                // Maybe an error occurred or the connection was closed
                DWORD err_code = GetLastError();
                _service->OnErrorEvent(CompletionKey, static_cast<size_t>(err_code));
            }
            continue;
        }

        // shutdown signal
        if (lpOverlapped == &_exit) {
            break;
        }

        OverLappedEx *ptr = (OverLappedEx *)lpOverlapped;
        if (ptr->event == IocpEventType::kRecvEvent) {
            _service->OnRecvEvent(CompletionKey, NumberOfBytesTransferred, ptr->HandleId);
        }
        if (ptr->event == IocpEventType::kSendEvent) {
            _service->OnSendEvent(CompletionKey, NumberOfBytesTransferred, ptr->HandleId);
        }
    }
}

}  // namespace raptor
