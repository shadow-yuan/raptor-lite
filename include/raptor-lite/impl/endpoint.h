#ifndef __RAPTOR_IMPL_ENDPOINT__
#define __RAPTOR_IMPL_ENDPOINT__

#include <stddef.h>
#include <stdint.h>
#include <string>

namespace raptor {
class Slice;
class Endpoint final {
public:
    Endpoint(void);
    ~Endpoint();

    uint64_t ConnectionId();
    std::string PeerString();
    bool SendMsg(const Slice &slice);
    bool Close();

    const std::string &LocalIp();
    uint16_t LocalPort();
    const std::string &RemoteIp();
    uint16_t RemotePort();
};
}  // namespace raptor

#endif  // __RAPTOR_IMPL_ENDPOINT__
