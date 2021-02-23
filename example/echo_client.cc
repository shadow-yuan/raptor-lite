#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <iostream>
#include <functional>
#include <map>
#include <sstream>

#include "raptor-lite/raptor-lite.h"

int64_t g_begin_time(0);

int64_t g_end_time(0);

raptor::AtomicInt64 g_max_request_time(INT64_MIN);

raptor::AtomicInt64 g_min_request_time(INT64_MAX);

raptor::AtomicInt64 g_send_request_count(0);

raptor::AtomicInt64 g_recv_request_count(0);

int32_t g_connect_connection(0);
raptor::AtomicInt32 g_success_connection(0);
raptor::AtomicInt32 g_fail_connection(0);
raptor::AtomicInt32 g_finish_connection(0);

raptor::AtomicBool g_can_finish(false);

raptor::AtomicInt64 g_all_requests_taken_time(0);

// raptor::Mutex g_mtx;
// raptor::ConditionVariable g_cv;

#define ENDPOINT_LEAVE 0
#define ENDPOINT_ENTER 1
#define ENDPOINT_MSG 2

struct client_message {
    raptor::MultiProducerSingleConsumerQueue::Node node;
    raptor::Endpoint ep;
    raptor::Slice msg;
    int type;
};

struct connection_ext_info : public raptor::RefCounted<connection_ext_info> {
    connection_ext_info()
        : request_counter(0)
        , current_index(0)
        , gen_id(0) {}
    raptor::AtomicInt32 request_counter;
    int current_index;

    int gen_id;

    // key:req id, value: send ms
    std::map<int, int64_t> tbls;
};

// raptor::MakeRefCounted<>();

struct echo_header {
    uint32_t payload_size;
    uint32_t request_id;
};

class EchoClient : public raptor::ConnectorHandler,
                   public raptor::MessageHandler,
                   public raptor::ProtocolHandler,
                   public raptor::EndpointNotifyHandler,
                   public raptor::EndpointClosedHandler {
public:
    EchoClient(/* args */);
    ~EchoClient();

    void OnErrorOccurred(const raptor::Endpoint &ep, raptor_error desc) {
        log_warn("OnErrorOccurred: %lld", ep.SocketFd());

        g_fail_connection.FetchAdd(1);
        if (g_fail_connection.Load() + g_success_connection.Load() == g_connect_connection) {
            g_can_finish.Store(true);
        }
    }

    void OnNotify(const raptor::Endpoint &ep) {

        g_success_connection.FetchAdd(1);

        if (g_fail_connection.Load() + g_success_connection.Load() == g_connect_connection) {
            g_can_finish.Store(true);
        }

        auto msg = new client_message;
        msg->ep = ep;
        msg->type = ENDPOINT_ENTER;
        _mpscq.push(&msg->node);
    }

    void OnConnect(const raptor::Endpoint &ep, raptor::Property &settings) {
        settings({{"SocketRecvTimeoutMs", 5000}, {"SocketSendTimeoutMs", 5000}});
        uintptr_t current_index = settings.GetValue<uintptr_t>("UserCustomValue");
        ep.SetExtInfo(current_index);

        raptor_error e = container->AttachEndpoint(ep, true);
        if (e != RAPTOR_ERROR_NONE) {
            log_error("AttachEndpoint:%s", e->ToString().c_str());
            abort();
        }
    }

    int OnCheckPackageLength(const raptor::Endpoint &ep, const void *data, size_t len) {
        if (len < sizeof(echo_header)) return 0;
        auto p = reinterpret_cast<const echo_header *>(data);
        return sizeof(echo_header) + p->payload_size;
    }

    int OnMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {
        g_recv_request_count.FetchAdd(1);
        auto cmsg = new client_message;
        cmsg->ep = ep;
        cmsg->type = ENDPOINT_MSG;
        cmsg->msg = msg;
        _mpscq.push(&cmsg->node);
        return 0;
    }

    void OnClosed(const raptor::Endpoint &ep, const raptor::Event &event) {
        /* log_debug("OnClosed: %s, event:%d, err:%d, desc:%s", ep.PeerString().c_str(),
           event.Type(), event.ErrorCode(), event.What().c_str()); */
        g_finish_connection.FetchAdd(1);
        auto cmsg = new client_message;
        cmsg->ep = ep;
        cmsg->type = ENDPOINT_LEAVE;
        _mpscq.push(&cmsg->node);
    }

    void init() {

        uint32_t cpus = raptor_get_number_of_cpu_cores();

        raptor::Property p{
            {"ConnectorHandler", static_cast<raptor::ConnectorHandler *>(this)},
            {"MessageHandler", static_cast<raptor::MessageHandler *>(this)},
            {"ProtocolHandler", static_cast<raptor::ProtocolHandler *>(this)},
            {"EndpointClosedHandler", static_cast<raptor::EndpointClosedHandler *>(this)},
            {"EndpointNotifyHandler", static_cast<raptor::EndpointNotifyHandler *>(this)},
            {"NotCheckConnectionTimeout", true},
            {"TcpUserTimeoutMs", 5000},
            {"RecvSendThreads", 1},
            {"MQConsumerThreads", 1}};

        raptor_error err = raptor::CreateContainer(p, &container);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to create container: %s", err->ToString().c_str());
            abort();
        }
        err = raptor::CreateConnector(p, &connector);
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to create connector: %s", err->ToString().c_str());
            abort();
        }
    }

    void start() {
        _shutdown = false;
        _thd.Start();

        raptor_error err = connector->Start();
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to start connector: %s", err->ToString().c_str());
            return;
        }
        err = container->Start();
        if (err != RAPTOR_ERROR_NONE) {
            log_error("Failed to start container: %s", err->ToString().c_str());
            return;
        }
    }

    void begin_test(const char *addr, int connections, int requests) {
        log_info("Target:%s Connections:%d Requests:%d", addr, connections, requests);
        _max_requests = requests;
        _request_per_connection = requests / connections;
        log_info("Request per connection:%d", _request_per_connection);
        g_connect_connection = connections;
        g_begin_time = GetCurrentMilliseconds();
        for (int i = 1; i <= connections; i++) {
            raptor_error err = connector->Connect(addr, static_cast<intptr_t>(i));
            if (err != RAPTOR_ERROR_NONE) {
                log_error("Failed to connect %s, error: %s, index: %d", addr,
                          err->ToString().c_str(), i);
                return;
            }
        }
    }

    void stop() {
        _shutdown = true;
        _thd.Join();

        connector->Shutdown();
        container->Shutdown();
    }

    void WorkThread(void *) {
        while (!_shutdown) {
            auto n = _mpscq.pop();
            if (n != nullptr) {
                auto msg = reinterpret_cast<client_message *>(n);
                Dispatch(msg);
                delete msg;
            }
        }
    }
    void Dispatch(client_message *msg);
    void EndpointEnter(const raptor::Endpoint &ep);
    void EndpointLeave(const raptor::Endpoint &ep);
    void EndpointMessage(const raptor::Endpoint &ep, const raptor::Slice &s);

private:
    raptor::Container *container = nullptr;
    raptor::Connector *connector = nullptr;
    int _request_per_connection = 0;
    int _max_requests = 0;
    bool _shutdown = true;
    raptor::Thread _thd;
    raptor::MultiProducerSingleConsumerQueue _mpscq;
    std::map<uint64_t, connection_ext_info *> _conns;
};

EchoClient::EchoClient(/* args */) {
    // don't call raptor function, because EchoClient(depend: each handler) not construction
    // completed.
    _thd = raptor::Thread("client", std::bind(&EchoClient::WorkThread, this, std::placeholders::_1),
                          nullptr);
}

EchoClient::~EchoClient() {
    raptor::DestroyConnector(connector);
    raptor::DestroyContainer(container);
}

void EchoClient::Dispatch(client_message *msg) {
    switch (msg->type) {
    case 1:
        EndpointEnter(msg->ep);
        break;
    case 2:
        EndpointMessage(msg->ep, msg->msg);
        break;
    default:
        EndpointLeave(msg->ep);
        break;
    }
}

void EchoClient::EndpointLeave(const raptor::Endpoint &ep) {
    log_debug("EndpointLeave: %s", ep.PeerString().c_str());
    auto cid = ep.ConnectionId();
    auto info = _conns[cid];
    raptor::RefCountedPtr<connection_ext_info> cei(info);
    _conns.erase(cid);
}

void EchoClient::EndpointEnter(const raptor::Endpoint &ep) {

    raptor::RefCountedPtr<connection_ext_info> cei = raptor::MakeRefCounted<connection_ext_info>();

    cei->RefIfNonZero();
    cei->current_index = static_cast<int>(ep.GetExtInfo());

    auto cid = ep.ConnectionId();
    RAPTOR_ASSERT(cid != 0);

    _conns[cid] = cei.get();

    echo_header hdr;
    hdr.payload_size = rand() % 256;
    hdr.request_id = cei->gen_id++;

    raptor::Slice req = raptor::MakeSliceByLength(hdr.payload_size + sizeof(echo_header));
    memcpy(req.Buffer(), &hdr, sizeof(echo_header));
    uint8_t *payload = req.Buffer() + sizeof(echo_header);
    memset(payload, '0', hdr.payload_size);
    if (hdr.payload_size > 1) {
        payload[hdr.payload_size - 1] = 'E';
    }
    payload[0] = 'B';

    cei->tbls[hdr.request_id] = GetCurrentMilliseconds();
    container->SendMsg(cid, req);

    g_send_request_count.FetchAdd(1, raptor::MemoryOrder::RELAXED);
}

void EchoClient::EndpointMessage(const raptor::Endpoint &ep, const raptor::Slice &msg) {

    auto rsp_hdr = reinterpret_cast<const echo_header *>(msg.begin());
    auto cid = ep.ConnectionId();

    auto info = _conns[cid];
    raptor::RefCountedPtr<connection_ext_info> cei(info);

    cei->RefIfNonZero();
    cei->request_counter.FetchAdd(1);

    auto bt = cei->tbls[rsp_hdr->request_id];

    auto interval = GetCurrentMilliseconds() - bt;
    g_all_requests_taken_time.FetchAdd(interval);

    if (interval > g_max_request_time.Load()) {
        g_max_request_time.Store(interval);
    }
    if (interval < g_min_request_time.Load()) {
        g_min_request_time.Store(interval);
    }

    if (cei->request_counter.Load() == _request_per_connection) {
        g_finish_connection.FetchAdd(1);
        cei->Unref();

        _conns.erase(cid);
        container->CloseEndpoint(ep, true);

        if (g_can_finish.Load()) {
            if (g_finish_connection.Load() + g_fail_connection.Load() == g_connect_connection) {
                g_end_time = GetCurrentMilliseconds();
                log_debug("send cv signal");
                // g_cv.Signal();
            }
        }

    } else {

        echo_header hdr;
        hdr.payload_size = rand() % 256;
        hdr.request_id = cei->gen_id++;

        raptor::Slice req = raptor::MakeSliceByLength(hdr.payload_size + sizeof(echo_header));
        memcpy(req.Buffer(), &hdr, sizeof(echo_header));
        uint8_t *payload = req.Buffer() + sizeof(echo_header);
        memset(payload, '0', hdr.payload_size);
        if (hdr.payload_size > 1) {
            payload[hdr.payload_size - 1] = 'E';
        }
        payload[0] = 'B';

        cei->tbls[hdr.request_id] = GetCurrentMilliseconds();

        container->SendMsg(cid, req);

        g_send_request_count.FetchAdd(1, raptor::MemoryOrder::RELAXED);
    }
}

void usage() {
    printf("Usage:\n"
           "\t-a     Target server address \n"
           "\t-c     Number of concurrent connections \n"
           "\t-r     Total number of requests to be sent \n");
}

int main(int argc, char *argv[]) {

    if (argc != 4) {
        usage();
        return -1;
    }

    const char *target_server = nullptr;
    const char *connections = nullptr;
    const char *requests = nullptr;

    for (int i = 1; i < argc; i++) {
        const char *p = argv[i];
        if (p[0] != '-') {
            usage();
            return -1;
        }

        switch (p[1]) {
        case 'a':
            target_server = p + 2;
            break;
        case 'c':
            connections = p + 2;
            break;
        case 'r':
            requests = p + 2;
            break;
        default:
            break;
        }
    }

    int number_of_connections = atoi(connections);
    int number_of_requests = atoi(requests);

    if (!target_server || number_of_connections <= 0 || number_of_requests <= 0) {
        return -1;
    }

    srand(time(0));

    printf(" ---- prepare start echo client ---- \n");
    RaptorGlobalStartup();
    EchoClient client;
    client.init();
    client.start();
    client.begin_test(target_server, number_of_connections, number_of_requests);
    // g_cv.Wait(&g_mtx);

    while (true) {
        int cb = getchar();
        if (cb == 'Q' || cb == 'q') {
            break;
        }
        if (g_end_time == 0) {
            g_end_time = GetCurrentMilliseconds();
        }

        double token_times_seconds = (g_end_time - g_begin_time) * 1.0f / 1000;
        int64_t complete_requests = g_recv_request_count.Load();
        int64_t failed_requests = g_send_request_count.Load() - complete_requests;
        std::cout << "Concurrency Level:      " << number_of_connections << std::endl;
        std::cout << "Total requests:         " << number_of_requests << std::endl;
        std::cout << "Time taken for tests:   " << token_times_seconds << " seconds" << std::endl;
        std::cout << "Complete requests:      " << complete_requests << std::endl;
        std::cout << "Failed requests:        " << failed_requests << std::endl;

        double all_request_taken_seconds = g_all_requests_taken_time.Load() * 1.0f / 1000;
        std::cout << "All requests take time: " << all_request_taken_seconds << " seconds"
                  << std::endl;

        std::cout << "Request max taken time: " << g_max_request_time.Load() << std::endl;
        std::cout << "Request min taken time: " << g_min_request_time.Load() << std::endl;

        std::cout << "Success connections:    " << g_success_connection.Load() << std::endl;
        std::cout << "Fail connections:       " << g_fail_connection.Load() << std::endl;
        std::cout << "Finish connections:     " << g_finish_connection.Load() << std::endl;

        double requests_per_second = complete_requests / all_request_taken_seconds;
        double ms_per_requests = g_all_requests_taken_time.Load() * 1.0f / complete_requests;

        std::cout << "Requests per second:    " << requests_per_second << " [#/sec]" << std::endl;
        std::cout << "Time per request:       " << ms_per_requests << " [ms]" << std::endl;
    }

    client.stop();
    RaptorGlobalCleanup();
    return 0;
}
