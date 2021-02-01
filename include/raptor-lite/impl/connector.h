#ifndef __RAPTOR_LITE_CONNECTOR__
#define __RAPTOR_LITE_CONNECTOR__

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace raptor {
class Endpoint;
class Property;
class ConnectorHandler {
public:
    virtual ~ConnectorHandler() {}

    /*
     * socket property:
     *   1. SocketNoSIGPIPE     (bool)
     *   2. SocketReuseAddress  (bool)
     *   3. SocketRecvTimeout   (int)
     *   4. SocketSendTimeout   (int)
     */
    virtual void OnConnect(Endpoint *ep, Property *settings);
};

class Connector {
public:
    virtual ~Connector() {}
    virtual bool Start() = 0;
    virtual void Shutdown() = 0;
    virtual bool Connect(const std::string &addr) = 0;
};

/*
 * Property:
 *   1. ConnectorHandler (required)
 */
Connector *CreateConnector(const Property &p);
void DestoryConnector(Connector *);
}  // namespace raptor

#endif  // __RAPTOR_LITE_CONNECTOR__
