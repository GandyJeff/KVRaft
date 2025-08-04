#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcheader.pb.h"
#include "util.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <string>

// - CallMethod 方法是所有RPC调用的入口点，该方法负责数据序列化和网络发送
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done)
{
    // 1. 检查 socket 文件描述符是否有效
    if (m_clientFd == -1)
    {
        // socket无效，调用 newConnect 尝试重新连接
        std::string errMsg;
        bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
        if (!rt)
        {
            DPrintf("[func-MprpcChannel::CallMethod]重连接ip：{%s} port{%d}失败", m_ip.c_str(), m_port);
            controller->SetFailed(errMsg);
            return;
        }
        else
        {
            DPrintf("[func-MprpcChannel::CallMethod]连接ip：{%s} port{%d}成功", m_ip.c_str(), m_port);
        }
    }

    // 2. 获取服务名和方法名
    const google::protobuf::ServiceDescriptor *sd = method->service();
    std::string service_name = sd->name();    // 服务名
    std::string method_name = method->name(); // 方法名

    // 3. 获取参数的序列化字符串长度 args_size
    uint32_t args_size{};
    std::string args_str;
    if (request->SerializeToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialize request error!");
        return;
    }

    // 4. 构建 RPC 消息头
    RPC::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    std::string rpc_header_str;
    if (!rpcHeader.SerializeToString(&rpc_header_str))
    {
        controller->SetFailed("serialize rpc header error!");
        return;
    }

    // 5. 构建完整 RPC 请求
    //  使用protobuf的CodedOutputStream来构建发送的数据流
    std::string send_rpc_str; // 用来存储最终发送的数据
    {
        // 创建一个StringOutputStream用于写入send_rpc_str
        google::protobuf::io::StringOutputStream string_output(&send_rpc_str);
        google::protobuf::io::CodedOutputStream coded_output(&string_output);

        // 先写入header的长度（变长编码）,便于服务端解析
        coded_output.WriteVarint32(static_cast<uint32_t>(rpc_header_str.size()));

        // 再写入消息头内容
        coded_output.WriteString(rpc_header_str);
    }

    // 最后，将请求参数附加到send_rpc_str后面
    send_rpc_str += args_str;

    // 6. 发送rpc请求
    //  失败会重试连接再发送，重试连接失败会直接return
    while (-1 == send(m_clientFd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
    {
        char errtxt[512] = {0};
        sprintf(errtxt, "send error! errno:%d", errno);
        std::cout << "尝试重新连接，对方ip：" << m_ip << " 对方端口" << m_port << std::endl;
        close(m_clientFd);
        m_clientFd = -1;
        std::string errMsg;
        bool rt = newConnect(m_ip.c_str(), m_port, &errMsg);
        if (!rt)
        {
            controller->SetFailed(errMsg);
            return;
        }
    }

    // 7. 接收rpc请求的响应值
    char recv_buf[1024] = {0};
    int recv_size = 0;
    if (-1 == (recv_size = recv(m_clientFd, recv_buf, 1024, 0)))
    {
        close(m_clientFd);
        m_clientFd = -1;
        char errtxt[512] = {0};
        sprintf(errtxt, "recv error! errno:%d", errno);
        controller->SetFailed(errtxt);
        return;
    }

    // 8. 反序列化rpc调用的响应数据
    // 使用 ParseFromArray 而非 ParseFromString ，避免 recv_buf 中 \0 字符导致的数据截断问题
    if (!response->ParseFromArray(recv_buf, recv_size))
    {
        char errtxt[1050] = {0};
        sprintf(errtxt, "parse error! response_str:%s", recv_buf);
        controller->SetFailed(errtxt);
        return;
    }
    // 成功则响应数据已填充到 response 对象中
}

MprpcChannel::MprpcChannel(std::string ip, short port, bool connectNow) : m_ip(ip), m_port(port), m_clientFd(-1)
{
    // 使用tcp编程，完成rpc方法的远程调用，使用的是短连接，因此每次都要重新连接上去，待改成长连接。
    // 根据 connectNow 参数决定是否立即连接
    if (!connectNow)
    {
        return; // 允许延迟连接
    }
    std::string errMsg;
    auto rt = newConnect(ip.c_str(), port, &errMsg);
    // 如立即连接，调用 newConnect 并支持最多 3 次重试
    int tryCount = 3;
    while (!rt && tryCount--)
    {
        std::cout << errMsg << std::endl;
        rt = newConnect(ip.c_str(), port, &errMsg);
    }
}

// 负责建立与 RPC 服务端的 TCP 连接。
bool MprpcChannel::newConnect(const char *ip, uint16_t port, std::string *errMsg)
{
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        char errtxt[512] = {0};
        sprintf(errtxt, "create socket error! errno:%d", errno);
        m_clientFd = -1;
        *errMsg = errtxt;
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // 连接rpc服务节点
    if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    {
        close(clientfd);
        char errtxt[512] = {0};
        sprintf(errtxt, "connect fail! errno:%d", errno);
        m_clientFd = -1;
        *errMsg = errtxt;
        return false;
    }
    m_clientFd = clientfd;
    return true;
}