#pragma once

#include <google/protobuf/descriptor.h> // Protobuf描述符
#include <muduo/net/EventLoop.h>        // muduo事件循环
#include <muduo/net/InetAddress.h>      // 网络地址
#include <muduo/net/TcpConnection.h>    // TCP连接
#include <muduo/net/TcpServer.h>        // TCP服务器
#include <functional>                   // 函数对象
#include <string>                       // 字符串
#include <unordered_map>                // 哈希表
#include "google/protobuf/service.h"    // Protobuf服务基类

// 框架提供的专门发布rpc服务的网络对象类
class RpcProvider
{
public:
    ~RpcProvider();

    // 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
    void NotifyService(google::protobuf::Service *service);

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    void Run(int nodeIndex, short port); // 传入节点索引和端口号，生成节点配置信息，并指定RPC服务监听的网络端口

private:
    // 组合EventLoop
    muduo::net::EventLoop m_eventLoop;

    // TCP服务器智能指针
    std::shared_ptr<muduo::net::TcpServer> m_muduo_server;

    // service服务类型信息,包含服务对象和方法映射
    struct ServiceInfo
    {
        google::protobuf::Service *m_service;                                                    // 保存服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor *> m_methodMap; // 保存服务方法
    };

    // 存储注册成功的服务对象和其服务方法的所有信息,存储已注册服务及其方法的哈希表
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;

    // 新的socket连接回调,处理新连接
    void OnConnection(const muduo::net::TcpConnectionPtr &);
    // 已建立连接用户的读写事件回调
    void OnMessage(const muduo::net::TcpConnectionPtr &, muduo::net::Buffer *, muduo::Timestamp);
    // Closure的回调操作，用于序列化rpc的响应和网络发送
    void SendRpcResponse(const muduo::net::TcpConnectionPtr &, google::protobuf::Message *);
};