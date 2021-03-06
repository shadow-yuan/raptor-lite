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

#ifndef __RAPTOR_LITE_ENDPOINT__
#define __RAPTOR_LITE_ENDPOINT__

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string>

namespace raptor {
class Container;
class Slice;
class EndpointImpl;
class Endpoint final {
    friend class TcpContainer;
    friend class ContainerImpl;

public:
    Endpoint(std::shared_ptr<EndpointImpl> impl = nullptr);
    ~Endpoint();

    // If attached to the container, it returns
    // a valid value, otherwise it returns uint64_t(-1) .
    uint64_t ConnectionId() const;

    // Do not close fd externally
    uint64_t SocketFd() const;
    uint16_t GetListenPort() const;

    std::string PeerString() const;

    // Bind the endpoint to the container. It means that the data
    // receiving and sending work is handed over to the container
    // to complete.
    void BindWithContainer(Container *container, bool notify = false) const;

    // Must call BindWithContainer or AttachEndpoint before calling SendMsg.
    bool SendMsg(const Slice &slice) const;
    bool SendMsg(const void *data, size_t len) const;

    // Used directly, not bind with the container
    int SyncRecv(void *data, size_t len) const;
    int SyncSend(const void *data, size_t len) const;

    void Close(bool notify = false) const;

    std::string LocalIp() const;
    uint16_t LocalPort() const;
    std::string RemoteIp() const;
    uint16_t RemotePort() const;

    bool IsOnline() const;
    void SetExtInfo(uintptr_t info) const;
    uintptr_t GetExtInfo() const;

private:
    std::shared_ptr<EndpointImpl> _impl;
};
}  // namespace raptor

#endif  // __RAPTOR_LITE_ENDPOINT__
