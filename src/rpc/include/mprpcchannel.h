#pragma once

#include <google/protobuf/descriptor.h> // Protobuf描述符，用于获取服务和方法元数据
#include <google/protobuf/message.h>    // Protobuf消息基类
#include <google/protobuf/service.h>    // Protobuf服务基类，定义RpcChannel接口

#include <algorithm>     // 标准库算法
#include <functional>    // 函数对象和绑定器
#include <iostream>      // 标准输入输出
#include <map>           // 有序映射容器
#include <random>        // 随机数生成
#include <string>        // 字符串处理
#include <unordered_map> // 无序映射容器
#include <vector>        // 动态数组容器

// 核心功能：处理RPC调用的发送和接收逻辑
//  继承google::protobuf::RpcChannel基类允许 MprpcChannel 被Protobuf生成的stub代码使用
class MprpcChannel : public google::protobuf::RpcChannel
{
public:
    // - CallMethod 方法是所有RPC调用的入口点，该方法负责数据序列化和网络发送
    void CallMethod(const google::protobuf::MethodDescriptor *method,
                    google::protobuf::RpcController *controller,
                    const google::protobuf::Message *request,
                    google::protobuf::Message *response,
                    google::protobuf::Closure *done) override;

    MprpcChannel(std::string ip, short port, bool connectNow); // connectNow : 是否立即建立连接
private:
    int m_clientFd;
    const std::string m_ip; // 保存ip和端口，如果断了可以尝试重连
    const uint16_t m_port;

    // 使用Doxygen格式注释，便于生成文档
    /// @brief 连接ip和端口,并设置m_clientFd
    /// @param ip ip地址，本机字节序
    /// @param port 端口，本机字节序
    /// @return 成功返回空字符串，否则返回失败信息
    bool newConnect(const char *ip, uint16_t port, std::string *errMsg);
};