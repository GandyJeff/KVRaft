#pragma once

#include "raftRpcPro/include/raftRpc.pb.h"
#include "raftRpcUtil.h"
#include "ApplyMsg.h"
#include "config.h"
#include "util.h"

#include <chrono>

/*
extern 的核心作用是：将符号的链接属性从内部改为外部，可被其他文件访问
使用 extern constexpr 声明一个可在编译期求值的外部常量，且必须在声明时初始化（因为需编译期求值）
*/

/// @brief 网络状态表示
extern constexpr int Disconnected = 0; // 网络异常
extern constexpr int AppNormal = 1;    // 网络正常

/// @brief 投票状态
extern constexpr int Killed = 0; // 已终止
extern constexpr int Voted = 1;  // 已投票
extern constexpr int Expire = 2; // 过期
extern constexpr int Normal = 3; // 正常

class Raft : public raftRpcProctoc::raftRpc
{
private:
    // 通用成员变量
    int m_me;                                          // 当前节点ID
    std::mutex m_mtx;                                  // 互斥锁
    std::vector<std::shared_ptr<RaftRpcUtil>> m_peers; // 其他节点的RPC客户端
    std::shared_ptr<LockQueue<ApplyMsg>> applyChan;    // 向状态机发送消息的通道
    // std::shared_ptr<Persister> m_persister;            // 持久化对象
    // std::unique_ptr<monsoon::IOManager> m_ioManager;   // IO管理器

    // 领导者选举相关成员变量
    int m_currentTerm; // 当前任期
    int m_votedFor;    // 当前任期投票给哪个节点(-1表示未投票)
    enum Status
    {
        Follower,
        Candidate,
        Leader
    }; // 节点状态枚举
    Status m_status;                                                    // 当前节点状态
    std::chrono::_V2::system_clock::time_point m_lastResetElectionTime; // 最后重置选举定时器的时间

    // 日志复制相关成员变量
    std::vector<raftRpcProctoc::LogEntry> m_logs; // 日志条目数组
    int m_commitIndex;                            // 已提交的最大日志索引
    int m_lastApplied;                            // 已应用到状态机的最大日志索引
    std::vector<int> m_nextIndex;                 // 每个节点的下一个日志索引(仅Leader维护)
    std::vector<int> m_matchIndex;                // 每个节点已匹配的日志索引(仅Leader维护)
    int m_lastSnapshotIncludeIndex;               // 快照包含的最后日志索引
    int m_lastSnapshotIncludeTerm;                // 快照包含的最后日志任期

    // 心跳相关成员变量
    std::chrono::_V2::system_clock::time_point m_lastResetHearBeatTime; // 最后重置心跳定时器的时间

public:
    /* 领导者选举相关方法*/
    // 选举超时定时器
    void electionTimeOutTicker();
    // 启动新选举
    void doElection();
    // 发送投票请求
    bool sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args, std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum);
    // 处理投票请求
    void RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply);
    // 检查日志是否最新
    bool UpToDate(int index, int term);
    // 获取当前状态
    void GetState(int *term, bool *isLeader);

    // 日志复制相关方法
    // 处理追加日志请求
    void AppendEntries(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply);
    // 发送追加日志请求
    bool sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args, std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums);
    // 匹配日志
    bool matchLog(int logIndex, int logTerm);
    // 更新提交索引
    void leaderUpdateCommitIndex();
    // 获取新命令索引
    int getNewCommandIndex();
    // 获取前一个日志信息
    void getPrevLogInfo(int server, int *preIndex, int *preTerm);
    // 获取最后日志索引
    int getLastLogIndex();
    // 获取最后日志任期
    int getLastLogTerm();
    // 获取最后日志的索引和任期
    void getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm);
    // 根据索引获取日志任期
    int getLogTermFromLogIndex(int logIndex);
    // 日志逻辑索引转换为物理索引
    int getSlicesIndexFromLogIndex(int logIndex);

    // 心跳相关方法
    // 发起心跳
    void doHeartBeat();
    // 心跳定时器
    void leaderHearBeatTicker();

    // 定时器维护相关方法
    void applierTicker(); // 向状态机定时写入日志

    // 持久化相关方法
    // 持久化当前状态
    void persist();
    // 读取持久化数据
    void readPersist(std::string data);
    // 获取要持久化的数据
    std::string persistData();
    // 创建快照
    void Snapshot(int index, std::string snapshot);
    // 有条件安装快照
    bool CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot);
    // 安装快照
    void InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *reply);
    // 发送快照
    void leaderSendSnapShot(int server);
    // 获取Raft状态大小
    int GetRaftStateSize();

    // 其他方法
    // 获取要应用的日志
    std::vector<ApplyMsg> getApplyLogs();
    // // 向KV服务器推送消息
    // void pushMsgToKvServer(ApplyMsg msg);
    // // 开始处理命令
    // void Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader);
    // // 初始化
    // void init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyCh);

    // RPC接口方法
    // RPC追加日志
    void AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request, ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done) override;
    // RPC安装快照
    void InstallSnapshot(google::protobuf::RpcController *controller, const ::raftRpcProctoc::InstallSnapshotRequest *request, ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done) override;
    // RPC投票请求
    void RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request, ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done) override;
};
