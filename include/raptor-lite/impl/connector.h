#ifndef __RAPTOR_IMPL_CONNECTOR__
#define __RAPTOR_IMPL_CONNECTOR__

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace raptor {
class Connector {
public:
    ~Connector();
    virtual ~Connector() {}

    virtual bool Start() = 0;
    virtual void Shutdown() = 0;

    virtual bool Connect(const std::string &addr) = 0;
    virtual bool SendRawMsg(uint64_t cid, const void *buff, size_t len) = 0;
    virtual bool Close(uint64_t cid) = 0;
};
}  // namespace raptor

#endif  // __RAPTOR_IMPL_CONNECTOR__
