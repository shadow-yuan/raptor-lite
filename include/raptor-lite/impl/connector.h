#ifndef __RAPTOR_LITE_CONNECTOR__
#define __RAPTOR_LITE_CONNECTOR__

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "raptor-lite/utils/status.h"

namespace raptor {
class Endpoint;
class Property;
class ConnectorHandler {
public:
    virtual ~ConnectorHandler() {}

    /*
     * settings property:
     *   1. SocketNoSIGPIPE     (bool, default:true)
     *   2. SocketReuseAddress  (bool, default:true)
     *   3. SocketRecvTimeout   (int)
     *   4. SocketSendTimeout   (int)
     *   5. SocketLowLatency    (bool, default:true)
     */
    virtual void OnConnect(Endpoint *ep, Property *settings);
    virtual void OnErrorOccurred(Endpoint *ep, raptor_error desc);
};

class Connector {
public:
    virtual ~Connector() {}
    virtual raptor_error Start() = 0;
    virtual void Shutdown() = 0;
    virtual raptor_error Connect(const std::string &addr) = 0;
};

/*
 * Property:
 *   1. ConnectorHandler (required)
 *   2. ConnecThreadNum  (optional, default:1)
 *   3. TcpUserTimeoutMs (optional, default:0)
 */
raptor_error CreateConnector(const Property &p, Connector **out);
void DestoryConnector(Connector *);
}  // namespace raptor

#endif  // __RAPTOR_LITE_CONNECTOR__
