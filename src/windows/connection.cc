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

#include "src/windows/connection.h"
#include <string.h>

#include "raptor-lite/impl/endpoint.h"
#include "raptor-lite/impl/handler.h"
#include "raptor-lite/utils/log.h"
#include "raptor-lite/utils/useful.h"

#include "src/common/cid.h"
#include "src/common/endpoint_impl.h"
#include "src/common/socket_util.h"
#include "src/common/service.h"

#include "src/windows/socket_setting.h"

namespace raptor {

AtomicUInt32 Connection::global_counter(0);

Connection::Connection(std::shared_ptr<EndpointImpl> obj)
    : _service(nullptr)
    , _proto(nullptr)
    , _send_pending(false) {

    memset(&_send_overlapped.overlapped, 0, sizeof(_send_overlapped.overlapped));
    memset(&_recv_overlapped.overlapped, 0, sizeof(_recv_overlapped.overlapped));
    _send_overlapped.event_type = internal::kSendEvent;
    _recv_overlapped.event_type = internal::kRecvEvent;

    _endpoint = obj;

    _handle_id = global_counter.FetchAdd(1, MemoryOrder::RELAXED);

    _send_overlapped.HandleId = _handle_id;
    _recv_overlapped.HandleId = _handle_id;
}

Connection::~Connection() {}

bool Connection::Init(internal::NotificationTransferService *service, PollingThread *iocp_thread) {
    _service = service;
    _send_pending = false;
    iocp_thread->Add(static_cast<SOCKET>(_endpoint->_fd),
                     reinterpret_cast<void *>(_endpoint->_connection_id));
    return AsyncRecv();
}

void Connection::SetProtocol(ProtocolHandler *p) {
    _proto = p;
}

void Connection::Shutdown(bool notify, const Event &ev) {
    if (!_endpoint->IsOnline()) {
        return;
    }

    raptor_set_socket_shutdown((SOCKET)_endpoint->_fd);
    _endpoint->_fd = static_cast<uint64_t>(INVALID_SOCKET);
    _send_pending = false;

    if (notify) {
        _service->OnClosed(_endpoint, ev);
    }

    _rcv_mtx.Lock();
    _rcv_buffer.ClearBuffer();
    _rcv_mtx.Unlock();

    _snd_mtx.Lock();
    _snd_buffer.ClearBuffer();
    _snd_mtx.Unlock();
}

bool Connection::SendMsg(const void *data, size_t data_len) {
    if (!_endpoint->IsOnline()) return false;

    AutoMutex g(&_snd_mtx);
    _snd_buffer.AddSlice(Slice(data, data_len));
    return AsyncSend();
}

constexpr size_t MAX_PACKAGE_SIZE = 0xffff;
constexpr size_t MAX_WSABUF_COUNT = 16;

bool Connection::AsyncSend() {
    if (_snd_buffer.Empty() || _send_pending) {
        return true;
    }

    WSABUF buffers[MAX_WSABUF_COUNT];
    size_t prepare_send_size = 0;
    DWORD counter = 0;

    _send_pending = true;

    _snd_buffer.ForEachSlice([&counter, &prepare_send_size, &buffers](const Slice &s) -> bool {
        if (counter >= MAX_WSABUF_COUNT) {
            return false;
        }

        if (prepare_send_size + s.Length() > MAX_PACKAGE_SIZE) {
            return false;
        }

        buffers[counter].buf = reinterpret_cast<char *>(const_cast<uint8_t *>(s.begin()));
        buffers[counter].len = static_cast<ULONG>(s.Length());
        prepare_send_size += s.Length();

        counter++;
        return true;
    });

    // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend
    int ret = WSASend((SOCKET)_endpoint->_fd, buffers, counter, NULL, 0,
                      &_send_overlapped.overlapped, NULL);

    if (ret == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            _send_pending = false;
            return false;
        }
    }
    return true;
}

bool Connection::AsyncRecv() {
    DWORD dwFlags = 0;
    WSABUF buffer;

    buffer.buf = NULL;
    buffer.len = 0;

    // https://docs.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsarecv
    int ret = WSARecv((SOCKET)_endpoint->_fd, &buffer, 1, NULL, &dwFlags,
                      &_recv_overlapped.overlapped, NULL);

    if (ret == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            return false;
        }
    }
    return true;
}

bool Connection::IsOnline() {
    return (_endpoint->IsOnline());
}

raptor_error Connection::DoRecvEvent(EventDetail *detail) {
    uint32_t size = detail->transferred_bytes;
    uint32_t handle_id = detail->handle_id;
    if (OnRecvEvent(size, handle_id)) {
        return RAPTOR_ERROR_NONE;
    }

    return RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "Connection:OnRecvEvent");
}

raptor_error Connection::DoSendEvent(EventDetail *detail) {
    uint32_t size = detail->transferred_bytes;
    uint32_t handle_id = detail->handle_id;
    if (OnSendEvent(size, handle_id)) {
        return RAPTOR_ERROR_NONE;
    }

    return RAPTOR_WINDOWS_ERROR(WSAGetLastError(), "Connection:OnSendEvent");
}

// IOCP Event
bool Connection::OnSendEvent(size_t size, uint32_t handle_id) {
    if (_handle_id != handle_id) return true;
    if (size == 0) return false;

    AutoMutex g(&_snd_mtx);
    _send_pending = false;
    _snd_buffer.MoveHeader(size);
    if (_snd_buffer.Empty()) {
        return true;
    }
    return AsyncSend();
}

bool Connection::OnRecvEvent(size_t size, uint32_t handle_id) {
    (size);

    if (handle_id != _handle_id) {
        log_error("Connection::OnRecvEvent handle_id not match");
        return true;
    }

    AutoMutex g(&_rcv_mtx);
    int recv_bytes = 0;
    int unused_space = 0;
    do {
        char buff[8192] = {0};

        unused_space = sizeof(buff);
        recv_bytes = ::recv((SOCKET)_endpoint->_fd, buff, unused_space, 0);

        if (recv_bytes == 0) return false;
        if (recv_bytes < 0) {
            int err = WSAGetLastError();
            if (WSAEWOULDBLOCK == err) return true;
            return false;
        }

        if (_proto) {
            _rcv_buffer.AddSlice(Slice(buff, recv_bytes));
            if (ParsingProtocol() == -1) {
                return false;
            }
        } else {
            _service->OnDataReceived(_endpoint, Slice(buff, recv_bytes));
        }

    } while (recv_bytes == unused_space);

    return AsyncRecv();
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
    size_t cache_size = _rcv_buffer.GetBufferLength();
    constexpr size_t header_size = 1024;
    int package_counter = 0;

    while (cache_size > 0) {
        size_t read_size = header_size;
        int pack_len = 0;
        Slice package;
        do {
            bool reach_tail = ReadSliceFromRecvBuffer(read_size, package);
            pack_len = _proto->OnCheckPackageLength(_endpoint, package.begin(), package.size());
            if (pack_len < 0) {
                log_error("Connection: Internal protocol parsing error");
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
