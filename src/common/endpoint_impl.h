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

#ifndef __RAPTOR_COMMON_ENDPOINT_IMPL__
#define __RAPTOR_COMMON_ENDPOINT_IMPL__
#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "src/common/resolve_address.h"

namespace raptor {
class Container;

class EndpointImpl final : public std::enable_shared_from_this<EndpointImpl> {
    friend class Connection;

public:
    EndpointImpl(uint64_t fd, raptor_resolved_address *local, raptor_resolved_address *remote);
    ~EndpointImpl();

    void SetConnection(uint64_t connection_id);
    void SetContainer(Container *container);
    void SetListenPort(uint16_t port);

    inline std::shared_ptr<EndpointImpl> GetEndpointImpl() {
        return this->shared_from_this();
    }

    // If attached to the container, it returns a valid value.
    uint64_t ConnectionId() const;

    // don't close it
    uint64_t SocketFd() const;
    uint16_t GetListenPort() const;

    std::string PeerString() const;

    // Give the endpoint to the container management.
    // It means that the data receiving and sending work
    // is handed over to the container to complete.
    void BindWithContainer(Container *container);

    bool SendMsg(const Slice &slice);
    bool SendMsg(const void *data, size_t len);

    // Used directly, not bind with the container
    int SyncRecv(void *data, size_t len);
    int SyncSend(const void *data, size_t len);

    bool Close(bool notify);

    std::string LocalIp() const;
    uint16_t LocalPort() const;
    std::string RemoteIp() const;
    uint16_t RemotePort() const;

    bool IsOnline() const;
    void SetExtInfo(uintptr_t info);
    uintptr_t GetExtInfo() const;

private:
    uint64_t _fd;
    uint64_t _connection_id;
    Container *_container;
    uint16_t _listen_port;  // for server
    uintptr_t _ext_info;
    raptor_resolved_address _local_addr;
    raptor_resolved_address _remote_addr;
};

}  // namespace raptor

#endif  // __RAPTOR_COMMON_ENDPOINT_IMPL__
