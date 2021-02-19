# Property
关于 raptor-lite 中出现的各种 Property 的说明文档

## 对象属性
### Acceptor
```c++
raptor_error CreateAcceptor(const Property &p, Acceptor **out);
```
|      名称       |   约束   | 说明 |
| --------------- | -------- |----------------------------------|
| AcceptorHandler | required | 接收客户端连接成功事件, 无默认值|
| ListenThreadNum | optional | 监听线程数量, 默认为1 |

### Connector
```c++
raptor_error CreateConnector(const Property &p, Connector **out);
```
|      名称       |   约束   | 说明 |
| --------------- | -------- |-----------------------------------|
| ConnectorHandler| required | 接收与服务器连接的结果事件, 无默认值|
| ConnecThreadNum | optional | 连接线程数量, 默认为1 |
| TcpUserTimeoutMs| optional | SOCKET 选项 TCP_USER_TIMEOUT, 默认为0|

### Container
```c++
raptor_error CreateContainer(const Property &p, Container **out);
```
|      名称       |   约束   | 说明 |
| ------------------------ | -------- | ----------------------------------- |
| MessageHandler           | required | 接收远端节点发来的数据, 无默认值|
| ProtocolHandler          | optional | 协议解析接口, 默认为 nullptr |
| HeartbeatHandler         | optional | 接收心跳相关的事件, 默认为 nullptr |
| EndpointClosedHandler    | optional | 接收远端节点断开的事件, 默认为 nullptr|
| EndpointNotifyHandler    | optional | 设置附加到 container 到的 endpoint 是否经过消息队列再通知外部, 默认为 nullptr|
| RecvSendThreads          | optional | 数据收发线程数量设置, 默认为1, 主要影响 IOCP/EPOLL 工作线程 |
| DefaultContainerSize     | optional | 容器初始连接数量, 默认为256|
| MaxContainerSize         | optional | 可以容纳同时在线的连接数量的上限, 默认为1048576|
| NotCheckConnectionTimeout| optional | 是否不允许检测连接超时, 默认为 false, 设置 true 时会忽略ConnectionTimeoutMs 选项|
| ConnectionTimeoutMs      | optional | 连接多久未收到数据则认为超时, 默认 60000 毫秒|
| MQConsumerThreads        | optional | 设置消息队列消费者线程数, 默认为1|

示例: 创建 Container 时, 先设置好各种属性.
```c++
raptor::Container *container = nullptr;
raptor::Property property{
                    {"AcceptorHandler", static_cast<AcceptorHandler *>(&obj)},
                    {"MessageHandler", static_cast<MessageHandler *>(&obj)},
                    {"HeartbeatHandler", static_cast<HeartbeatHandler *>(&obj)},
                    {"EndpointClosedHandler", static_cast<EndpointClosedHandler *>(&obj)}};

raptor_error err = raptor::CreateContainer(property, &container);
```

## 连接属性
使用 AcceptorHandler 和 ConnectorHandler 时会用到
|      名称       |   约束   | 说明 |
| --------------- | -------- |-----------------------------------|
| UserCustomValue    | optional | 获取调用 Connector->Connect() 时传入的自定义参数, 默认为 0|
| SocketNoSIGPIPE    | optional | 设置是否禁用 SIGPIPE, 默认为 true|
| SocketReuseAddress | optional | 设置 SOCKET 选项 SO_REUSEADDR, 默认为true|
| SocketRecvTimeoutMs| optional | 设置 SOCKET 选项 SO_RCVTIMEO, int 类型 |
| SocketSendTimeoutMs| optional | 设置 SOCKET 选项 SO_SNDTIMEO, int 类型 |
| SocketLowLatency   | optional | 设置 SOCKET 选项 TCP_NODELAY, 默认为true|
| SocketNonBlocking  | optional | 设置是否非阻塞模式, 默认为true|

```c++
class AcceptorHandler {
public:
    virtual ~AcceptorHandler() {}
    virtual void OnAccept(const Endpoint &ep, Property &settings) = 0;
};

class ConnectorHandler {
public:
    virtual ~ConnectorHandler() {}
    virtual void OnConnect(const Endpoint &ep, Property &settings) = 0;
};
```

示例: 当 `OnAccept` 或 `OnConnect` 被调用即表示成功建立了连接, 此时 `Endpoint` 中包含连接的信息, 通过修改`settings` 从而设置该连接的各种 SOCKET 属性.
```c++
// set property
settings({{"SocketRecvTimeoutMs", 5000},
          {"SocketSendTimeoutMs", 5000} });
```

示例：当 `OnConnect` 被调用时, 通过 `settings` 获取自定义参数.
```c++
// get user custom value (OnConnect)
uintptr_t current_index = settings.GetValue<uintptr_t>("UserCustomValue");
```
