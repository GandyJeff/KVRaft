#include "Persister.h"
#include "util.h"

// 同时保存Raft状态和快照数据
void Persister::Save(const std::string raftstate, const std::string snapshot)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    // 清除之前的Raft状态和快照数据
    clearRaftStateAndSnapshot();
    // 将raftstate和snapshot写入本地文件
    // 将新的Raft状态数据写入输出流
    m_raftStateOutStream << raftstate;
    // 将新的快照数据写入输出流
    m_snapshotOutStream << snapshot;
}

// 读取快照数据
std::string Persister::ReadSnapshot()
{
    std::lock_guard<std::mutex> lg(m_mtx);
    // 检查并关闭输出流
    if (m_snapshotOutStream.is_open())
    {
        m_snapshotOutStream.close();
    }

    // 延迟执行代码块，确保在函数返回前重新打开输出流
    DEFER { m_snapshotOutStream.open(m_snapshotFileName); }; // 默认是追加
    // 创建输入流读取快照文件
    std::fstream ifs(m_snapshotFileName, std::ios_base::in);
    // 检查文件是否正常打开，若失败返回空字符串
    if (!ifs.good())
    {
        return "";
    }
    std::string snapshot;
    ifs >> snapshot;
    ifs.close();
    return snapshot;
}

// 仅保存Raft状态数据
void Persister::SaveRaftState(const std::string &data)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    // 清除之前的Raft状态数据
    clearRaftState();
    // 将新的Raft状态数据写入输出流
    m_raftStateOutStream << data;
    // 更新Raft状态数据大小
    m_raftStateSize += data.size();
}

// 获取Raft状态数据的大小
long long Persister::RaftStateSize()
{
    std::lock_guard<std::mutex> lg(m_mtx);

    return m_raftStateSize;
}

// 读取Raft状态数据
std::string Persister::ReadRaftState()
{
    std::lock_guard<std::mutex> lg(m_mtx);

    // 创建输入流读取快照文件
    std::fstream ifs(m_raftStateFileName, std::ios_base::in);
    // 检查文件是否正常打开，若失败返回空字符串
    if (!ifs.good())
    {
        return "";
    }
    std::string snapshot;
    ifs >> snapshot;
    ifs.close();
    return snapshot;
}

// 构造函数，初始化Persister对象，参数 me 是节点ID，用于区分不同节点的持久化文件
Persister::Persister(const int me)
    : m_raftStateFileName("raftstatePersist" + std::to_string(me) + ".txt"),
      m_snapshotFileName("snapshotPersist" + std::to_string(me) + ".txt"),
      m_raftStateSize(0)
{
    // 检查文件状态并清空文件
    bool fileOpenFlag = true;
    // 创建文件流，以输出和截断模式打开文件
    std::fstream file(m_raftStateFileName, std::ios::out | std::ios::trunc);
    // 检查文件是否成功打开
    if (file.is_open())
    {
        file.close();
    }
    else
    {
        fileOpenFlag = false;
    }

    file = std::fstream(m_snapshotFileName, std::ios::out | std::ios::trunc);
    if (file.is_open())
    {
        file.close();
    }
    else
    {
        fileOpenFlag = false;
    }

    // 如果文件打开失败，输出错误信息
    if (!fileOpenFlag)
    {
        DPrintf("[func-Persister::Persister] file open error");
    }

    // 打开输出流，绑定到相应的文件
    m_raftStateOutStream.open(m_raftStateFileName);
    m_snapshotOutStream.open(m_snapshotFileName);
}

// 析构函数，负责清理资源
Persister::~Persister()
{
    if (m_raftStateOutStream.is_open())
    {
        m_raftStateOutStream.close();
    }
    if (m_snapshotOutStream.is_open())
    {
        m_snapshotOutStream.close();
    }
}

// 清除Raft状态数据
void Persister::clearRaftState()
{
    // 重置Raft状态数据大小
    m_raftStateSize = 0;
    // 检查并关闭Raft状态输出流
    if (m_raftStateOutStream.is_open())
    {
        m_raftStateOutStream.close();
    }
    // 以输出和截断模式重新打开文件流，清空文件内容
    m_raftStateOutStream.open(m_raftStateFileName, std::ios::out | std::ios::trunc);
}

// 清除快照数据
void Persister::clearSnapshot()
{
    // 检查并关闭快照输出流
    if (m_snapshotOutStream.is_open())
    {
        m_snapshotOutStream.close();
    }
    // 以输出和截断模式重新打开文件流，清空文件内容
    m_snapshotOutStream.open(m_snapshotFileName, std::ios::out | std::ios::trunc);
}

// 同时清除Raft状态和快照数据
void Persister::clearRaftStateAndSnapshot()
{
    clearRaftState();
    clearSnapshot();
}