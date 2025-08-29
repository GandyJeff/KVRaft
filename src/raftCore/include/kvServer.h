#ifndef KVSERVER_H
#define KVSERVER_H

#include <boost/any.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/serialization.hpp>
#include <boost/serialization/unordered_map.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include "kvServerRPC.pb.h"
#include "raft.h"
#include "skipList.h"

class KvServer : raftKVRpcProtoc::kvServerRpc
{
private:
    // 核心成员变量
    std::mutex m_mtx;                               // 互斥锁，保证线程安全
    int m_me;                                       // 当前服务器 ID
    std::shared_ptr<Raft> m_raftNode;               // Raft 节点指针
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan; // 与 Raft 节点通信的管道
    int m_maxRaftState;                             // 触发快照的日志大小阈值

    // 存储相关成员变量
    std::string m_serializedKVData;                      // 序列化后的 KV 数据
    SkipList<std::string, std::string> m_skipList;       // 跳表数据结构
    std::unordered_map<std::string, std::string> m_kvDB; // KV 数据库

    // 请求处理相关成员变量
    std::unordered_map<int, LockQueue<Op> *> waitApplyCh; // 等待 Raft 应用的操作队列
    std::unordered_map<std::string, int> m_lastRequestId; // 客户端最后请求 ID 记录

    // 快照相关成员变量
    int m_lastSnapShotRaftLogIndex; // 最后一次快照的 Raft 日志索引

public:
    // 构造函数与初始化，启动 KV 服务器
    KvServer() = delete; // 删除默认构造函数
    KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port);

    // KV 操作方法
    // 执行追加插入操作
    void ExecuteAppendOpOnKVDB(Op op);
    // 执行插入操作
    void ExecutePutOpOnKVDB(Op op);
    // 执行查询操作
    void ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist);
    // 打印 KV 数据库内容（调试用）
    void DprintfKVDB();

    // 客户端 API 方法
    void Get(const raftKVRpcProtoc::GetArgs *args, raftKVRpcProtoc::GetReply *reply);
    void PutAppend(const raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply);

    // Raft 交互方法
    // 从 Raft 获取命令
    void GetCommandFromRaft(ApplyMsg message);
    // 持续读取 Raft 应用命令
    void ReadRaftApplyCommandLoop();
    // 向等待队列发送消息
    bool SendMessageToWaitChan(const Op &op, int raftIndex);
    // 检查请求是否重复
    bool ifRequestDuplicate(std::string ClientId, int RequestId);

    // 快照管理方法
    // 读取并安装快照
    void ReadSnapShotToInstall(std::string snapshot);
    // 检查是否需要发送快照命令
    void IfNeedToSendSnapShotCommand(int raftIndex, int proportion);
    // 从 Raft 获取快照
    void GetSnapShotFromRaft(ApplyMsg message);
    // 创建快照
    std::string MakeSnapShot();

    // RPC 接口实现
    void PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::PutAppendArgs *request,
                   ::raftKVRpcProtoc::PutAppendReply *response, ::google::protobuf::Closure *done) override;

    void Get(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::GetArgs *request,
             ::raftKVRpcProtoc::GetReply *response, ::google::protobuf::Closure *done) override;

    // 序列化与反序列化
private:
    // 将 boost::serialization::access 类声明为友元，允许它访问 KvServer 的私有成员变量，这是Boost序列化库的标准用法
    friend class boost::serialization::access;

    // Boost库通过重载 & 操作符，根据归档类型自动选择序列化或反序列化操作
    template <typename Archive>
    void serialize(Archive &ar, const unsigned int version) // 这里面写需要序列话和反序列化的字段
    {
        ar & m_serializedKVData; // 序列化/反序列化跳表数据的字符串表示
        ar & m_lastRequestId;    // 序列化/反序列化客户端请求ID映射表
    }

    // 创建并返回当前KV服务器状态的快照数据
    std::string getSnapshotData()
    {
        m_serializedKVData = m_skipList.dump_file(); // 将跳表数据序列化为字符串，存储到 m_serializedKVData
        std::stringstream ss;                        // 创建字符串流 ss
        boost::archive::text_oarchive oa(ss);        // 文本输出归档 oa
        oa << *this;                                 // 将当前 KvServer 对象序列化到归档中
        m_serializedKVData.clear();                  // 清空
        return ss.str();                             // 返回序列化后的完整快照字符串
    }

    // 从字符串解析快照方法
    void parseFromString(const std::string &str)
    {
        std::stringstream ss(str);                // 创建字符串流 ss 并初始化为传入的快照字符串
        boost::archive::text_iarchive ia(ss);     // 创建文本输入归档 ia
        ia >> *this;                              // 从归档中反序列化数据到当前对象
        m_skipList.load_file(m_serializedKVData); // 恢复跳表数据
        m_serializedKVData.clear();               // 清空
    }
};

#endif // KVSERVER_H