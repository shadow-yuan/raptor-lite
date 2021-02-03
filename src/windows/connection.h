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

#ifndef __RAPTOR_CORE_WINDOWS_CONNECTION__
#define __RAPTOR_CORE_WINDOWS_CONNECTION__

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "src/common/cid.h"
#include "src/common/resolve_address.h"
#include "src/common/service.h"
#include "raptor-lite/utils/slice.h"
#include "raptor-lite/utils/slice_buffer.h"
#include "src/windows/iocp.h"
#include "raptor-lite/utils/sync.h"
#include "src/common/endpoint_impl.h"
#include "raptor-lite/impl/handler.h"

namespace raptor {
class Connection final {
    friend class TcpServer;
    friend class TcpContainer;
    
public:
    explicit Connection(internal::INotificationTransfer *service);
    ~Connection();

    // Before Init, sock must be associated with iocp
    void Init(std::shared_ptr<EndpointImpl> obj);
    void SetProtocol(ProtocolHandler *p);
    void Shutdown(bool notify);

    bool SendWithHeader(const void *hdr, size_t hdr_len, const void *data, size_t data_len);
    bool IsOnline();

    void SetUserData(void *ptr);
    void GetUserData(void **ptr) const;
    void SetExtendInfo(uint64_t data);
    void GetExtendInfo(uint64_t &data) const;
    int GetPeerString(char *buf, int buf_size);

private:
    // IOCP Event
    bool OnSendEvent(size_t size);
    bool OnRecvEvent(size_t size);

    // if success return the number of parsed packets
    // otherwise return -1 (protocol error)
    int ParsingProtocol();

    // return true if reach recv buffer tail.
    bool ReadSliceFromRecvBuffer(size_t read_size, Slice &s);

    bool AsyncSend();
    bool AsyncRecv();

private:
    enum { DEFAULT_TEMP_SLICE_COUNT = 2 };

    internal::INotificationTransfer *_service;
    ProtocolHandler *_proto;
    std::shared_ptr<EndpointImpl> _endpoint;

    bool _send_pending;

    SOCKET _fd;

    OverLappedEx _send_overlapped;
    OverLappedEx _recv_overlapped;

    raptor_resolved_address _addr;
    Slice _addr_str;

    SliceBuffer _rcv_buffer;
    SliceBuffer _snd_buffer;

    Slice _tmp_buffer[DEFAULT_TEMP_SLICE_COUNT];

    Mutex _rcv_mtx;
    Mutex _snd_mtx;

    uint64_t _user_data;
    void *_extend_ptr;
};
}  // namespace raptor
#endif  // __RAPTOR_CORE_WINDOWS_CONNECTION__
