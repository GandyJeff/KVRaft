#include "rpcprovider.h"
#include "rpcheader.pb.h" // 包含RPC消息头定义

#include <arpa/inet.h> // 提供网络地址转换函数
#include <netdb.h>     // 提供主机名解析函数
#include <unistd.h>    // 提供POSIX操作系统API
#include <cstring>     // 提供字符串操作函数
#include <fstream>     // 提供文件流操作
#include <string>      // 提供C++字符串类

RpcProvider::~RpcProvider()
{
    std::cout << "[func - RpcProvider::~RpcProvider()]: ip和port信息：" << m_muduo_server->ipPort() << std::endl;
    m_eventLoop.quit(); // 退出事件循环
}

// 该函数是框架提供给外部的RPC服务注册接口,将服务对象及其方法描述符存储在本地映射表中
void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    ServiceInfo service_info;

    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    // 获取服务名称
    std::string service_name = pserviceDesc->name();
    // 获取服务方法数量
    int methodCnt = pserviceDesc->method_count();

    std::cout << "service_name:" << service_name << std::endl;

    // 遍历所有服务方法
    for (int i = 0; i < methodCnt; ++i)
    {
        // 获取方法描述符
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        // 将方法描述符存入映射表
        service_info.m_methodMap.insert({method_name, pmethodDesc});
    }
    // 保存服务对象
    service_info.m_service = service;
    // 将服务信息存入服务映射表
    m_serviceMap.insert({service_name, service_info});
}

// 启动rpc服务节点，开始提供rpc远程网络调用服务
// 传入节点索引和端口号，生成节点配置信息，并指定RPC服务监听的网络端口
void RpcProvider::Run(int nodeIndex, short port)
{
    // 获取本机IP地址
    char *ipC;
    // 存储主机名的字符数组
    char hname[128];
    // 主机信息结构体指针
    struct hostent *hent;
    // 通过 gethostname 系统调用获取本机主机名
    gethostname(hname, sizeof(hname)); // 获取主机名
    hent = gethostbyname(hname);       // 通过主机名获取主机信息
    /*
    h_addr_list是主机的IP地址列表（网络字节序）
    - 1.hent->h_addr_list[i] 获取第i个网络地址（二进制格式）
    - 2.(struct in_addr *) 将通用地址指针转换为IPv4地址结构体指针
    - 3.inet_ntoa() 将网络字节序的IP地址转换为点分十进制字符串
    */
    for (int i = 0; hent->h_addr_list[i]; i++)
    {
        ipC = inet_ntoa(*(struct in_addr *)(hent->h_addr_list[i])); // 转换为点分十进制IP
    }
    std::string ip = std::string(ipC);

    // 将节点信息写入配置文件
    std::string node = "node" + std::to_string(nodeIndex);
    // 文件输出流，用于写入节点信息
    std::ofstream outfile;
    outfile.open("test.conf", std::ios::app); // 打开文件追加写入
    if (!outfile.is_open())
    {
        std::cout << "打开文件失败！" << std::endl;
        exit(EXIT_FAILURE);
    }
    outfile << node + "ip=" + ip << std::endl;
    outfile << node + "port=" + std::to_string(port) << std::endl;
    outfile.close();

    // 创建服务器地址
    muduo::net::InetAddress address(ip, port);

    // 创建TCP服务器
    m_muduo_server = std::make_shared<muduo::net::TcpServer>(&m_eventLoop, address, "RpcProvider");

    // 绑定回调函数
    m_muduo_server->setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    m_muduo_server->setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // 设置线程数量
    m_muduo_server->setThreadNum(4);

    // 打印启动信息
    std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;

    // 启动网络服务和事件循环
    m_muduo_server->start();
    m_eventLoop.loop();
}

// 新的socket连接回调,处理新连接
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
{
    if (!conn->connected())
    {
        // 连接断开时关闭连接
        conn->shutdown();
    }
}

// 已建立连接用户的读写事件回调 如果远程有一个rpc服务的调用请求，那么OnMessage方法就会响应
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp)
{
    // 读取缓冲区全部数据
    std::string recv_buf = buffer->retrieveAllAsString(); // 清空缓冲区并返回所有数据

    // 使用protobuf的CodedInputStream解析数据流
    google::protobuf::io::ArrayInputStream array_input(recv_buf.data(), recv_buf.size()); // 从内存数组读取数据
    google::protobuf::io::CodedInputStream coded_input(&array_input);                     // 提供 Protobuf 编码数据的解析功能
    // 存储消息头大小
    uint32_t header_size{};

    coded_input.ReadVarint32(&header_size); // 读取可变长度整数，解析消息头大小，Varint32 编码可以高效存储小整数

    // 读取消息头并反序列化
    std::string rpc_header_str;
    RPC::RpcHeader rpcHeader;
    std::string service_name;
    std::string method_name;

    // 设置读取限制，安全读取固定大小的消息头数据
    google::protobuf::io::CodedInputStream::Limit msg_limit = coded_input.PushLimit(header_size); // 设置读取上限为header_size
    coded_input.ReadString(&rpc_header_str, header_size);                                         // 读取指定大小的字符串
    coded_input.PopLimit(msg_limit);                                                              // 恢复之前的读取限制

    // 声明参数数据字符串
    uint32_t args_size{};
    if (rpcHeader.ParseFromString(rpc_header_str)) // 反序列化消息头
    {
        // 消息头反序列化成功
        service_name = rpcHeader.service_name();
        method_name = rpcHeader.method_name();
        args_size = rpcHeader.args_size();
    }
    else
    {
        std::cout << "rpc_header_str:" << rpc_header_str << " parse error!" << std::endl;
        return;
    }

    // 读取 RPC 方法的参数数据
    std::string args_str;
    bool read_args_success = coded_input.ReadString(&args_str, args_size);

    if (!read_args_success)
    {
        return;
    }

    // 查找服务和方法
    auto it = m_serviceMap.find(service_name);
    if (it == m_serviceMap.end())
    {
        std::cout << "服务：" << service_name << " is not exist!" << std::endl;
        // 打印已注册服务列表
        std::cout << "当前已经有的服务列表为:";
        for (auto item : m_serviceMap)
        {
            std::cout << item.first << " ";
        }
        std::cout << std::endl;
        return;
    }

    auto mit = it->second.m_methodMap.find(method_name);
    if (mit == it->second.m_methodMap.end())
    {
        std::cout << service_name << ":" << method_name << " is not exist!" << std::endl;
        return;
    }

    // 在服务对象中查找请求的方法
    google::protobuf::Service *service = it->second.m_service;      // 获取服务对象，获取service对象
    const google::protobuf::MethodDescriptor *method = mit->second; // 获取方法描述符，获取method对象

    // 生成rpc方法调用的请求request和响应response参数，由于是rpc的请求，因此请求需要通过request来序列化
    google::protobuf::Message *request = service->GetRequestPrototype(method).New(); // 获取请求消息原型
    if (!request->ParseFromString(args_str))                                         // 反序列化请求参数
    {
        std::cout << "request parse error, content:" << args_str << std::endl;
        return;
    }
    google::protobuf::Message *response = service->GetResponsePrototype(method).New();

    // 创建回调对象
    /*
    模板参数列表指定了回调函数所属的类（ RpcProvider ）和参数类型（连接指针和消息对象指针）
    函数参数依次为：当前对象指针（ this ）、回调函数指针（ &RpcProvider::SendRpcResponse ）、以及要绑定的参数（ conn 和 response ）
    */
    google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider,
                                                                    const muduo::net::TcpConnectionPtr &,
                                                                    google::protobuf::Message *>(this,
                                                                                                 &RpcProvider::SendRpcResponse,
                                                                                                 conn,
                                                                                                 response);

    // 调用服务方法
    service->CallMethod(method, nullptr, request, response, done);
}

// Closure的回调操作，用于序列化rpc的响应和网络发送
void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
{
    // 声明序列化后的响应字符串
    std::string response_str;
    if (response->SerializeToString(&response_str)) // 序列化响应消息
    {
        // 发送响应数据
        conn->send(response_str);
    }
    else
    {
        std::cout << "serialize response_str error!" << std::endl;
    }
    // 长连接设计，不主动断开连接
}