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

#include "raptor-lite/impl/container.h"
#include <memory>

#include "raptor-lite/utils/status.h"
#include "raptor-lite/impl/handler.h"
#include "raptor-lite/impl/property.h"
#include "src/common/container_impl.h"

namespace raptor {
class ContainerAdaptor : public Container {

public:
    explicit ContainerAdaptor(ContainerImpl::Option *option);
    ~ContainerAdaptor();

    raptor_error Init();
    raptor_error Start() override;
    void Shutdown() override;
    raptor_error AttachEndpoint(const Endpoint &ep, bool notify = false) override;
    bool SendMsg(uint64_t connection_id, const Slice &s) override;
    void CloseEndpoint(const Endpoint &ep, bool event_notify = false) override;

private:
    std::shared_ptr<ContainerImpl> _impl;
};

ContainerAdaptor::ContainerAdaptor(ContainerImpl::Option *option)
    : _impl(std::make_shared<ContainerImpl>(option)) {}

ContainerAdaptor::~ContainerAdaptor() {}

raptor_error ContainerAdaptor::Init() {
    return _impl->Init();
}

raptor_error ContainerAdaptor::Start() {
    return _impl->Start();
}
void ContainerAdaptor::Shutdown() {
    _impl->Shutdown();
}

raptor_error ContainerAdaptor::AttachEndpoint(const Endpoint &ep, bool notify) {
    return _impl->AttachEndpoint(ep, notify);
}

bool ContainerAdaptor::SendMsg(uint64_t connection_id, const Slice &s) {
    if (s.Empty()) return true;
    return _impl->SendMsg(connection_id, s);
}

void ContainerAdaptor::CloseEndpoint(const Endpoint &ep, bool event_notify) {
    _impl->CloseEndpoint(ep, event_notify);
}

/*
 * Property:
 *   0. EndpointNotifyHandler      (optional, default: nullptr)
 *   1. ProtocolHandler            (optional, default: nullptr)
 *   2. MessageHandler             (required)
 *   3. HeartbeatHandler           (optional, default: nullptr)
 *   4. EndpointClosedHandler      (optional, default: nullptr)
 *   5. RecvSendThreads            (optional, default: 1)
 *   6. DefaultContainerSize       (optional, default: 256)
 *   7. MaxContainerSize           (optional, default: 1048576)
 *   8. NotCheckConnectionTimeout  (optional, default: false)
 *   9. ConnectionTimeoutMs        (optional, default: 60000)
 *  10. MQConsumerThreads          (optional, default: 1)
 */
raptor_error CreateContainer(const Property &p, Container **out) {
    MessageHandler *message_handler =
        reinterpret_cast<MessageHandler *>(p.GetValue<intptr_t>("MessageHandler", 0));

    if (!message_handler) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("Missing MessageHandler");
    }

    *out = nullptr;

    ContainerImpl::Option option;
    option.msg_handler = message_handler;

    option.proto_handler =
        reinterpret_cast<ProtocolHandler *>(p.GetValue<intptr_t>("ProtocolHandler", 0));

    option.heartbeat_handler =
        reinterpret_cast<HeartbeatHandler *>(p.GetValue<intptr_t>("HeartbeatHandler", 0));

    option.closed_handler =
        reinterpret_cast<EndpointClosedHandler *>(p.GetValue<intptr_t>("EndpointClosedHandler", 0));

    option.notify_handler =
        reinterpret_cast<EndpointNotifyHandler *>(p.GetValue<intptr_t>("EndpointNotifyHandler", 0));

    option.recv_send_threads = p.GetValue("RecvSendThreads", 1);
    option.default_container_size = p.GetValue("DefaultContainerSize", 256);
    option.max_container_size = p.GetValue("MaxContainerSize", 1048576);
    option.not_check_connection_timeout = p.GetValue<bool>("NotCheckConnectionTimeout", false);
    option.connection_timeoutms = p.GetValue("ConnectionTimeoutMs", 60000);
    option.mq_consumer_threads = p.GetValue("MQConsumerThreads", 1);

    ContainerAdaptor *adaptor = new ContainerAdaptor(&option);
    raptor_error err = adaptor->Init();
    if (err == RAPTOR_ERROR_NONE) {
        *out = adaptor;
    }
    return err;
}

void DestroyContainer(Container *cc) {
    if (cc) {
        cc->Shutdown();
        delete cc;
    }
}
}  // namespace raptor
