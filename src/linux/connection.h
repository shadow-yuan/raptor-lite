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

#ifndef __RAPTOR_CORE_LINUX_CONNECTION__
#define __RAPTOR_CORE_LINUX_CONNECTION__

#include <stdint.h>
#include <memory>

#include "raptor-lite/impl/event.h"
#include "raptor-lite/utils/atomic.h"
#include "raptor-lite/utils/slice_buffer.h"
#include "raptor-lite/utils/sync.h"

namespace raptor {

class EpollThread;
class EndpointImpl;
class ProtocolHandler;

namespace internal {
class INotificationTransfer;
}  // namespace internal

class Connection final {
    friend class TcpContainer;

public:
    explicit Connection(std::shared_ptr<EndpointImpl> obj);
    ~Connection();

    void Init(internal::INotificationTransfer *service, EpollThread *t);
    void SetProtocol(ProtocolHandler *p);
    bool SendMsg(const void *data, size_t data_len);
    void Shutdown(bool notify, const Event &ev = Event());
    bool IsOnline();

    void SetExtendInfo(uintptr_t data);
    uintptr_t GetExtendInfo() const;

private:
    int OnRecv();
    int OnSend();

    bool DoRecvEvent();
    bool DoSendEvent();

    // if success return the number of parsed packets
    // otherwise return -1 (protocol error)
    int ParsingProtocol();

    // return true if reach recv buffer tail.
    bool ReadSliceFromRecvBuffer(size_t read_size, Slice &s);

    inline uint32_t HandleId() const {
        return _handle_id;
    }

private:
    internal::INotificationTransfer *_service;
    ProtocolHandler *_proto;
    EpollThread *_epoll_thread;

    SliceBuffer _rcv_buffer;
    SliceBuffer _snd_buffer;

    Mutex _rcv_mutex;
    Mutex _snd_mutex;

    std::shared_ptr<EndpointImpl> _endpoint;

    uint32_t _handle_id;

    static AtomicUInt32 global_counter;
};

}  // namespace raptor

#endif  // __RAPTOR_CORE_LINUX_CONNECTION__
