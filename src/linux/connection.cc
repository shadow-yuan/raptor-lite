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

#include "src/linux/connection.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include "raptor-lite/impl/handler.h"
#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/sync.h"
#include "raptor-lite/utils/time.h"

#include "src/linux/epoll_thread.h"
#include "src/linux/socket_setting.h"
#include "src/common/endpoint_impl.h"
#include "src/common/service.h"
#include "src/common/socket_util.h"

namespace raptor {
AtomicUInt32 Connection::global_counter(0);
Connection::Connection(std::shared_ptr<EndpointImpl> obj)
    : _service(nullptr)
    , _proto(nullptr)
    , _epoll_thread(nullptr) {

    _handle_id = global_counter.FetchAdd(1, MemoryOrder::RELAXED);
    _endpoint  = obj;
}

Connection::~Connection() {}

void Connection::Init(internal::NotificationTransferService *service, PollingThread *t) {
    _service      = service;
    _epoll_thread = t;

    _epoll_thread->Add((int)_endpoint->_fd, (void *)_endpoint->_connection_id,
                       EPOLLIN | EPOLLET | EPOLLONESHOT);
}

void Connection::SetProtocol(ProtocolHandler *p) {
    _proto = p;
}

bool Connection::SendMsg(const void *data, size_t data_len) {
    if (!_endpoint->IsOnline()) return false;
    AutoMutex g(&_snd_mutex);
    _snd_buffer.AddSlice(Slice(data, data_len));
    _epoll_thread->Modify((int)_endpoint->_fd, (void *)_endpoint->_connection_id,
                          EPOLLOUT | EPOLLET);
    return true;
}

void Connection::Shutdown(bool notify, const Event &ev) {
    if (!_endpoint->IsOnline()) {
        return;
    }

    _epoll_thread->Delete((int)_endpoint->_fd, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLONESHOT);
    raptor_set_socket_shutdown((int)_endpoint->_fd);
    _endpoint->_fd = uint64_t(~0);

    if (notify) {
        _service->OnClosed(_endpoint, ev);
    }

    _rcv_mutex.Lock();
    _rcv_buffer.ClearBuffer();
    _rcv_mutex.Unlock();

    _snd_mutex.Lock();
    _snd_buffer.ClearBuffer();
    _snd_mutex.Unlock();
}

bool Connection::IsOnline() {
    return _endpoint->IsOnline();
}

raptor_error Connection::DoRecvEvent(EventDetail *detail) {
    int result = OnRecv();
    if (result == 0) {
        _epoll_thread->Modify((int)_endpoint->_fd, (void *)_endpoint->_connection_id,
                              EPOLLIN | EPOLLET | EPOLLONESHOT);
        return RAPTOR_ERROR_NONE;
    }
    return RAPTOR_POSIX_ERROR("Connection:OnRecv, connection may be closed");
}

raptor_error Connection::DoSendEvent(EventDetail *detail) {
    int result = OnSend();
    if (result == 0 && !_snd_buffer.Empty()) {
        _epoll_thread->Modify((int)_endpoint->_fd, (void *)_endpoint->_connection_id,
                              EPOLLOUT | EPOLLET);
        return RAPTOR_ERROR_NONE;
    }
    return RAPTOR_POSIX_ERROR("Connection:OnSend, connection may be closed");
}

int Connection::OnRecv() {
    AutoMutex g(&_rcv_mutex);

    int recv_bytes   = 0;
    int unused_space = 0;
    do {
        char buffer[8192];

        unused_space = sizeof(buffer);
        recv_bytes   = ::recv((int)_endpoint->_fd, buffer, unused_space, 0);

        if (recv_bytes == 0) {
            return -1;
        }

        if (recv_bytes < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
                return 0;
            }
            return -1;
        }

        if (!_proto) {
            _service->OnDataReceived(_endpoint, Slice(buffer, recv_bytes));
        } else {
            // Add to recv buffer
            _rcv_buffer.AddSlice(Slice(buffer, recv_bytes));
            if (ParsingProtocol() == -1) {
                return -1;
            }
        }

    } while (recv_bytes == unused_space);
    return 0;
}

int Connection::OnSend() {
    AutoMutex g(&_snd_mutex);
    if (_snd_buffer.Empty()) {
        return 0;
    }

    size_t count = 0;
    do {

        Slice slice = _snd_buffer.Front();
        int slen    = ::send((int)_endpoint->_fd, slice.begin(), slice.size(), 0);

        if (slen == 0) {
            return -1;
        }

        if (slen < 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
                return 0;
            }
            return -1;
        }

        _snd_buffer.MoveHeader((size_t)slen);
        count = _snd_buffer.SliceCount();

    } while (count > 0);
    return 0;
}

bool Connection::ReadSliceFromRecvBuffer(size_t read_size, Slice &s) {
    size_t cache_size = _rcv_buffer.GetBufferLength();
    if (read_size >= cache_size) {
        s = _rcv_buffer.Merge();
        return true;
    }
    s = _rcv_buffer.GetHeader(read_size);
    return false;
}

int Connection::ParsingProtocol() {
    size_t cache_size            = _rcv_buffer.GetBufferLength();
    constexpr size_t header_size = 1024;
    int package_counter          = 0;

    while (cache_size > 0) {
        size_t read_size = header_size;
        int pack_len     = 0;
        Slice package;
        do {
            bool reach_tail = ReadSliceFromRecvBuffer(read_size, package);
            pack_len = _proto->OnCheckPackageLength(_endpoint, package.begin(), package.size());
            if (pack_len < 0) {
                log_warn("Connection: Internal protocol parsing error");
                return -1;
            }

            // equal 0 means we need more data
            if (pack_len == 0) {
                if (reach_tail) {
                    goto done;
                }
                read_size *= 2;
                continue;
            }

            // We got the length of a whole packet
            if (cache_size >= (size_t)pack_len) {
                break;
            }
            goto done;
        } while (false);

        if (package.size() < static_cast<size_t>(pack_len)) {
            package = _rcv_buffer.GetHeader(pack_len);
        } else {
            size_t n = package.size() - pack_len;
            package.PopBack(n);
        }
        _service->OnDataReceived(_endpoint, package);
        _rcv_buffer.MoveHeader(pack_len);

        cache_size = _rcv_buffer.GetBufferLength();
        package_counter++;
    }
done:
    return package_counter;
}

void Connection::SetExtendInfo(uintptr_t data) {
    _endpoint->SetExtInfo(data);
}

uintptr_t Connection::GetExtendInfo() const {
    return _endpoint->GetExtInfo();
}

}  // namespace raptor
