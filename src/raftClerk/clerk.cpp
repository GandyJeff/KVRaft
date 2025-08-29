#include "clerk.h"
#include "raftServerRpcUtil.h"
#include "util.h"
#include <string>
#include <vector>

// 内部Put/Append通用实现
void Clerk::PutAppend(std::string key, std::string value, std::string op)
{
    m_requestId++; // 递增请求ID
    auto requestId = m_requestId;
    auto server = m_recentLeaderId; // 从最近的领导者开始尝试
    // 循环直到成功
    while (true)
    {
        raftKVRpcProtoc::PutAppendArgs args;                   // 创建请求参数
        args.set_key(key);                                     // 设置key
        args.set_value(value);                                 // 设置value
        args.set_op(op);                                       // 设置操作类型（Put或Append）
        args.set_clientid(m_clientId);                         // 设置客户端ID
        args.set_requestid(requestId);                         // 设置请求ID
        raftKVRpcProtoc::PutAppendReply reply;                 // 创建响应对象
        bool ok = m_servers[server]->PutAppend(&args, &reply); // 发送RPC请求
        if (!ok || reply.err() == ErrWrongLeader)
        {
            // 请求失败或目标不是领导者
            DPrintf("【Clerk::PutAppend】原以为的leader：{%d}请求失败，向新leader{%d}重试 ，操作：{%s}", server, server + 1, op.c_str());
            if (!ok)
            {
                DPrintf("重试原因：rpc失败");
            }
            if (reply.err() == ErrWrongLeader)
            {
                DPrintf("重试原因：非leader");
            }
            server = (server + 1) % m_servers.size(); // 尝试下一个节点
            continue;
        }

        if (reply.err() == OK)
        {                              // 成功
            m_recentLeaderId = server; // 更新最近的领导者
            return;
        }
    }
}

// 初始化方法
void Clerk::Init(std::string configFileName)
{
    // 获取所有raft节点ip、port ，并进行连接
    MprpcConfig config;                                  // 创建配置对象
    config.LoadConfigFile(configFileName.c_str());       // 加载配置文件
    std::vector<std::pair<std::string, short>> ipPortVt; // 存储节点IP和端口
    for (int i = 0; i < INT_MAX - 1; ++i)
    {                                                  // 循环加载所有节点
        std::string node = "node" + std::to_string(i); // 节点配置名

        std::string nodeIp = config.Load(node + "ip");        // 加载节点IP
        std::string nodePortStr = config.Load(node + "port"); // 加载节点端口
        if (nodeIp.empty())
        {
            // 没有更多节点
            break;
        }
        ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str())); // 添加到列表
    }
    // 进行连接，遍历所有节点
    for (const auto &item : ipPortVt)
    {
        std::string ip = item.first;                                  // IP
        short port = item.second;                                     // 端口
        auto *rpc = new raftServerRpcUtil(ip, port);                  // 创建RPC客户端
        m_servers.push_back(std::shared_ptr<raftServerRpcUtil>(rpc)); // 添加到服务器列表
    }
}

// 获取键值对
std::string Clerk::Get(std::string key)
{
    m_requestId++; // 递增请求ID
    auto requestId = m_requestId;
    int server = m_recentLeaderId; // 从最近的领导者开始尝试
    raftKVRpcProtoc::GetArgs args; // 创建请求参数
    args.set_key(key);             // 设置key
    args.set_clientid(m_clientId); // 设置客户端ID
    args.set_requestid(requestId); // 设置请求ID

    while (true)
    {                                                    // 循环直到成功
        raftKVRpcProtoc::GetReply reply;                 // 创建响应对象
        bool ok = m_servers[server]->Get(&args, &reply); // 发送RPC请求
        if (!ok || reply.err() == ErrWrongLeader)
        {                                             // 请求失败或目标不是领导者
            server = (server + 1) % m_servers.size(); // 尝试下一个节点
            continue;
        }
        if (reply.err() == ErrNoKey)
        {
            // 键不存在
            std::cout << "键不存在!" << std::endl;
            return "";
        }
        if (reply.err() == OK)
        {                              // 成功
            m_recentLeaderId = server; // 更新最近的领导者
            return reply.value();      // 返回值
        }
    }
    return "";
}

// 设置键值对
void Clerk::Put(std::string key, std::string value) { PutAppend(key, value, "Put"); }

// 追加值
void Clerk::Append(std::string key, std::string value) { PutAppend(key, value, "Append"); }

Clerk::Clerk() : m_clientId(Uuid()), m_requestId(0), m_recentLeaderId(0) {}