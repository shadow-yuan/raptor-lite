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
#include <set>
#include <unordered_map>

#include "raptor-lite/utils/atomic.h"
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/utils/time.h"

namespace raptor {
IocpThread::IocpThread(internal::EventReceivingService *service)
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

raptor_error IocpThread::Init(size_t rs_threads, size_t kernel_threads) {
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
                OverLappedEx *olex = (OverLappedEx *)lpOverlapped;

                int err = WSAGetLastError();
                EventDetail detail;
                detail.error_code = (err == 0) ? (-1) : (err);
                detail.event_type = olex->event_type;
                detail.ptr = reinterpret_cast<uint64_t>(CompletionKey);
                detail.overlaped = lpOverlapped;
                detail.transferred_bytes = NumberOfBytesTransferred;
                detail.handle_id = 0;
                _service->OnEventProcess(&detail);
            } else {
                // IOCP TIMEOUT
            }
            continue;
        }

        // shutdown signal
        if (lpOverlapped == &_exit) {
            break;
        }

        OverLappedEx *olex = (OverLappedEx *)lpOverlapped;

        EventDetail detail;
        detail.error_code = 0;
        detail.event_type = olex->event_type;
        detail.ptr = reinterpret_cast<uint64_t>(CompletionKey);
        detail.overlaped = lpOverlapped;
        detail.transferred_bytes = NumberOfBytesTransferred;
        detail.handle_id = olex->HandleId;
        _service->OnEventProcess(&detail);
    }
}

/*
 * IocpThread adaptor implement
 */
typedef struct {
    uint64_t CompletionKey;
    internal::EventReceivingService *service;
    int event_types;
} IocpSupplementInfo;

class IocpThreadAdaptor final : public RefCounted<IocpThreadAdaptor, PolymorphicRefCount>,
                                public internal::EventReceivingService {
public:
    IocpThreadAdaptor()
        : _iocp(this) {}

    ~IocpThreadAdaptor() {
        _iocp.Shutdown();
    }

    raptor_error Init(size_t rs_threads, size_t kernel_threads) {
        return _iocp.Init(rs_threads, kernel_threads);
    }

    raptor_error Start() {
        return _iocp.Start();
    }

    void Shutdown() {
        _iocp.Shutdown();
    }

    bool Add(SOCKET sock, uint64_t CompletionKey, int type,
             internal::EventReceivingService *service) {
        bool r = _iocp.Add(sock, reinterpret_cast<void *>(sock));
        AutoMutex g(&_sock_mtx);
        IocpSupplementInfo info;
        info.CompletionKey = CompletionKey;
        info.service = service;
        info.event_types = type;
        _interesting_event_services[sock] = info;
        return r;
    }

    inline bool Modify(SOCKET sock, uint64_t CompletionKey, int type,
                       internal::EventReceivingService *service) {
        return Add(sock, CompletionKey, type, service);
    }

    void Delete(SOCKET sock, int type) {
        AutoMutex g(&_sock_mtx);
        auto it = _interesting_event_services.find(sock);
        if (it == _interesting_event_services.end()) {
            return;
        }
        if (type == 0) {
            _interesting_event_services.erase(it);
        } else {
            it->second.event_types &= ~type;
        }
    }

    void EnableTimeoutCheck(internal::EventReceivingService *service, bool timeout_check) {
        AutoMutex g(&_timeout_mtx);

        if (timeout_check) {
            _timeout_check_services.insert(service);
        } else {
            _timeout_check_services.erase(service);
        }
    }

private:
    void OnEventProcess(EventDetail *detail) override {
        auto service = FindEventService(detail);
        if (service) {
            service->OnEventProcess(detail);
        }
    }

    void OnTimeoutCheck(int64_t current_millseconds) override {
        AutoMutex g(&_timeout_mtx);
        for (auto tcs : _timeout_check_services) {
            tcs->OnTimeoutCheck(current_millseconds);
        }
    }

private:
    internal::EventReceivingService *FindEventService(EventDetail *detail) {
        AutoMutex g(&_sock_mtx);
        SOCKET sock = static_cast<SOCKET>(detail->ptr);
        auto it = _interesting_event_services.find(sock);
        if (it == _interesting_event_services.end()) {
            return nullptr;
        }
        if (detail->event_type < internal::kErrorEvent ||
            detail->event_type > internal::kMaxEventValue) {
            detail->event_type = it->second.event_types | internal::kErrorEvent;
        }
        detail->ptr = it->second.CompletionKey;
        return it->second.service;
    }

    IocpThread _iocp;
    Mutex _sock_mtx;
    Mutex _timeout_mtx;

    // send, recv, connect, accept
    std::set<internal::EventReceivingService *> _timeout_check_services;
    std::unordered_map<SOCKET, IocpSupplementInfo> _interesting_event_services;
};

// ------------------------------------
namespace {
IocpThreadAdaptor *iocp_thread_adaptor = nullptr;
AtomicInt32 iocp_thread_count(0);
Mutex iocp_thread_mutex;
}  // namespace
// ------------------------------------

PollingThread::PollingThread(internal::EventReceivingService *service)
    : _service(service)
    , _impl(nullptr)
    , _enable_timeout_check(false)
    , _instance_id(0) {
    AutoMutex g(&iocp_thread_mutex);
    int32_t count = iocp_thread_count.Load();
    if (count == 0) {
        _impl = MakeRefCounted<IocpThreadAdaptor>();
        iocp_thread_adaptor = _impl.get();
    } else {
        iocp_thread_adaptor->RefIfNonZero();
        _impl.reset(iocp_thread_adaptor);
    }
    _instance_id = count + 1;
    iocp_thread_count.FetchAdd(1, MemoryOrder::RELAXED);
}

PollingThread::~PollingThread() {
    this->Shutdown();
    iocp_thread_count.FetchSub(1, MemoryOrder::RELAXED);
}

raptor_error PollingThread::Init(size_t rs_threads, size_t kernel_threads) {
    if (_instance_id == 1) {
        return _impl->Init(rs_threads, kernel_threads);
    }
    return RAPTOR_ERROR_NONE;
}

raptor_error PollingThread::Start() {
    if (_instance_id == 1) {
        return _impl->Start();
    }
    return RAPTOR_ERROR_NONE;
}

void PollingThread::Shutdown() {
    if (_instance_id == 0) {
        return;
    }
    if (_instance_id == 1) {
        _impl->Shutdown();
    }
    _instance_id = 0;
}

bool PollingThread::Add(SOCKET sock, uint64_t CompletionKey, int type) {
    if (_instance_id == 0) {
        return false;
    }
    return _impl->Add(sock, CompletionKey, type, _service);
}

int PollingThread::Modify(SOCKET sock, uint64_t CompletionKey, int type) {
    if (_instance_id == 0) {
        return false;
    }
    return _impl->Modify(sock, CompletionKey, type, _service);
}

int PollingThread::Delete(SOCKET sock, int type) {
    if (_instance_id == 0) {
        return false;
    }
    _impl->Delete(sock, type);
    return true;
}

void PollingThread::EnableTimeoutCheck(bool b) {
    if (_instance_id == 0) {
        return;
    }
    _impl->EnableTimeoutCheck(_service, b);
}

}  // namespace raptor
