#include "src/common/sockaddr.h"
#include <assert.h>

int RaptorGlobalStartup() {
#ifdef _WIN32
    WSADATA wsaData;
    int status = WSAStartup(MAKEWORD(2, 0), &wsaData);
    assert(status == 0);
#endif
    return 0;
}

int RaptorGlobalCleanup() {
#ifdef _WIN32
    return WSACleanup();
#else
    return 0;
#endif
}
