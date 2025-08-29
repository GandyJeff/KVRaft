#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <raftServerRpcUtil.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <string>
#include <vector>
#include "kvServerRPC.pb.h"
#include "mprpcconfig.h"
class Clerk
{
private:
    std::vector<std::shared_ptr<raftServerRpcUtil>> m_servers; // 保存所有raft节点的fd，存储所有Raft节点的RPC客户端对象
    std::string m_clientId;                                    ////客户端唯一标识
    int m_requestId;                                           ////请求ID，用于保证幂等性
    int m_recentLeaderId;                                      // 最近的领导者ID（可能不是最新的）

    // 生成随机客户端ID
    std::string Uuid()
    {
        return std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand()) + std::to_string(rand());
    }

    // 内部Put/Append通用实现
    void PutAppend(std::string key, std::string value, std::string op);

public:
    // 对外暴露的三个功能和初始化
    // 初始化方法
    void Init(std::string configFileName);
    // 获取键值对
    std::string Get(std::string key);
    // 设置键值对
    void Put(std::string key, std::string value);
    // 追加值
    void Append(std::string key, std::string value);

    Clerk();
};