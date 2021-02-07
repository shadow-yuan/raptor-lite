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

#ifndef __RAPTOR_SURFACE_ACCEPTOR__
#define __RAPTOR_SURFACE_ACCEPTOR__

#include <memory>

#include "raptor-lite/impl/acceptor.h"
#include "raptor-lite/impl/property.h"
#ifdef __GNUC__
#include "src/linux/tcp_listener.h"
#else
#include "src/windows/tcp_listener.h"
#endif
#include "src/common/resolve_address.h"

namespace raptor {
class AcceptorAdaptor : public Acceptor {

public:
    explicit AcceptorAdaptor(AcceptorHandler *handler);
    ~AcceptorAdaptor();
    raptor_error Init(int threads = 1);
    raptor_error Start() override;
    void Shutdown() override;
    raptor_error AddListening(const std::string &addr) override;

private:
    std::unique_ptr<TcpListener> _impl;
};

AcceptorAdaptor::AcceptorAdaptor(AcceptorHandler *handler)
    : _impl(new TcpListener(handler)) {}

AcceptorAdaptor::~AcceptorAdaptor() {}

raptor_error AcceptorAdaptor::Init(int threads) {
    return _impl->Init(threads);
}

raptor_error AcceptorAdaptor::Start() {
    return _impl->Start();
}

void AcceptorAdaptor::Shutdown() {
    _impl->Shutdown();
}

raptor_error AcceptorAdaptor::AddListening(const std::string &addr) {
    raptor_resolved_addresses *addrs = nullptr;
    raptor_error e = raptor_blocking_resolve_address(addr.c_str(), nullptr, &addrs);
    if (e == RAPTOR_ERROR_NONE) {
        for (size_t i = 0; i < addrs->naddrs; i++) {
            e = _impl->AddListeningPort(&addrs->addrs[i]);
            if (e != RAPTOR_ERROR_NONE) {
                break;
            }
        }
    }
    raptor_resolved_addresses_destroy(addrs);
    return e;
}

/*
 * Property:
 *   1. AcceptorHandler (required)
 *   2. ListenThreadNum (optional)
 */
raptor_error CreateAcceptor(const Property &p, Acceptor **out) {
    AcceptorHandler *handler =
        reinterpret_cast<AcceptorHandler *>(p.GetValue<intptr_t>("AcceptorHandler"));

    int ListenThreadNum = p.GetValue("ListenThreadNum", 1);

    *out = nullptr;

    if (!handler) {
        return RAPTOR_ERROR_FROM_STATIC_STRING("Missing AcceptorHandler");
    }

    AcceptorAdaptor *adaptor = new AcceptorAdaptor(handler);
    raptor_error e           = adaptor->Init(ListenThreadNum);
    if (e == RAPTOR_ERROR_NONE) {
        *out = adaptor;
    }
    return e;
}

void DestoryAcceptor(Acceptor *a) {
    if (a) {
        a->Shutdown();
        delete a;
    }
}
}  // namespace raptor

#endif  // __RAPTOR_SURFACE_ACCEPTOR__
