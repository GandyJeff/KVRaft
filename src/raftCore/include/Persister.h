#pragma once

#include <fstream>
#include <mutex>

// 持久化组件，负责将Raft节点的关键状态数据和快照持久化到磁盘存储，确保节点在崩溃重启后能够恢复到正确状态
class Persister
{
private:
    std::mutex m_mtx;
    std::string m_raftState; // 内存中缓存的Raft节点状态数据
    std::string m_snapshot;  // 内存中缓存的快照数据

    // Raft状态文件的文件名常量，定义了Raft状态持久化到磁盘的文件路径
    const std::string m_raftStateFileName;
    // 快照文件的文件名常量，定义了快照数据持久化到磁盘的文件路径
    const std::string m_snapshotFileName;
    // 用于写入Raft状态数据的输出流，提供对Raft状态文件的写入操作接口
    std::ofstream m_raftStateOutStream;
    // 用于写入快照数据的输出流，提供对快照文件的写入操作接口
    std::ofstream m_snapshotOutStream;
    /**
     * 保存raftStateSize的大小
     * 避免每次都读取文件来获取具体的大小
     */
    long long m_raftStateSize;

    // 清除Raft状态数据
    void clearRaftState();
    // 清除快照数据
    void clearSnapshot();
    // 同时清除Raft状态和快照数据
    void clearRaftStateAndSnapshot();

public:
    // 同时保存Raft状态和快照数据
    void Save(std::string raftstate, std::string snapshot);
    // 读取快照数据
    std::string ReadSnapshot();
    // 仅保存Raft状态数据
    void SaveRaftState(const std::string &data);
    // 获取Raft状态数据的大小
    long long RaftStateSize();
    // 读取Raft状态数据
    std::string ReadRaftState();
    // 构造函数，参数 me 是节点ID，用于区分不同节点的持久化文件
    explicit Persister(int me);
    // 析构函数，负责清理资源
    ~Persister();
};
