#include "raft.h"

/* 领导者选举相关方法*/

/// @brief 选举超时定时器
// 定期检查是否需要触发新选举，确保节点在一段时间没有收到心跳后发起选举
void Raft::electionTimeOutTicker()
{
  // 无限循环，确保定时器持续运行，不会退出。
  while (true)
  {
    // 如果当前节点是领导者（ m_status == Leader ），则不需要参与选举，每隔 HeartBeatTimeout 时间（心跳超时时间）休眠一次。
    while (m_status == Leader)
    {
      usleep(HeartBeatTimeout); // usleep 函数用于微秒级休眠，HeartBeatTimeout 是一个常量，定义了心跳间隔。HeartBeatTimeout比选举超时一般小一个数量级。
    }
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{}; // 表示适合的睡眠时间，类型为纳秒级持续时间。
    std::chrono::system_clock::time_point wakeTime{};                                      // 表示唤醒时间点，类型为系统时钟时间点。
    {
      m_mtx.lock();
      wakeTime = now();                                                                        // 记录当前时间点。
      suitableSleepTime = getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime; // 计算适合的睡眠时间：睡眠时间 = 随机选举超时时间 + 上一次重置时间 - 当前时间。
      m_mtx.unlock();
    }

    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1) // 检查计算出的睡眠时间是否大于1毫秒。
    {

      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count()); // 如果大于1毫秒，则调用 usleep 函数休眠指定的微秒数（将纳秒转换为微秒），确保节点在指定时间内不会发起选举。

      // // 获取当前时间点
      // auto start = std::chrono::steady_clock::now();

      //   // 获取函数运行结束后的时间点
      //   auto end = std::chrono::steady_clock::now();

      //   // 计算时间差并输出结果（单位为毫秒）
      //   std::chrono::duration<double, std::milli> duration = end - start;

      //   // 使用ANSI控制序列将输出颜色修改为紫色
      //   std::cout << "\033[1;35m electionTimeOutTicker();函数设置睡眠时间为: "
      //             << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
      //             << std::endl;
      //   std::cout << "\033[1;35m electionTimeOutTicker();函数实际睡眠时间为: " << duration.count() << " 毫秒\033[0m"
      //             << std::endl;
    }

    if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime - wakeTime).count() > 0) // 检查在睡眠期间选举定时器是否被重置。
    {
      // 上一次重置时间与当前时间的差值如果大于0，说明在睡眠期间定时器被重置（通常是因为收到了领导者的心跳消息），则使用 continue 语句跳过当前循环，重新开始。
      // 这一步是选举定时器的核心逻辑，确保在收到心跳时不会发起不必要的选举。
      continue;
    }
    // 如果睡眠期间定时器没有被重置，则调用 doElection 函数发起新选举。
    doElection();
  }
}

/// @brief 启动新选举
// 当选举定时器超时时被调用，将节点状态转变为候选人并发起选举。构造需要发送的rpc，并多线程调用sendRequestVote处理rpc及响应。
void Raft::doElection()
{
  // 获取互斥锁 m_mtx ，确保多线程环境下的线程安全，防止并发访问导致数据竞争。
  std::lock_guard<std::mutex> g(m_mtx);
  // 领导者不参与选举
  if (m_status == Leader)
  {
    return;
  }

  // 打印调试信息，记录选举定时器到期且节点不是领导者，开始选举流程。 m_me 是当前节点ID。
  DPrintf("[       ticker-func-rf(%d)              ]  选举定时器到期且不是leader，开始选举 \n", m_me);

  // 转变为候选人并增加任期，Follower ==> Candidate
  m_status = Candidate;
  m_currentTerm += 1; // 增加当前任期号 m_currentTerm ，这是发起新选举的关键步骤，每个新选举都会增加任期号。
  m_votedFor = m_me;  // 候选人首先给自己投一票， m_votedFor 记录当前任期内投票给哪个节点。
  persist();          // 调用 persist() 函数持久化当前状态（包括 m_status 、 m_currentTerm 和 m_votedFor ），确保即使节点崩溃重启，状态也能恢复。

  // 创建共享指针 votedNum 用于跟踪已获得的票数，初始值为1（自己的一票）。使用共享指针是为了在异步线程中安全更新计数值。
  std::shared_ptr<int> votedNum = std::make_shared<int>(1); // 已获得的票数(初始为1票自己)

  // 重置选举定时器，避免在选举过程中因定时器超时而重复发起选举。 now() 函数返回当前时间点。
  m_lastResetElectionTime = now();

  // 向所有其他节点发送投票请求，遍历所有其他节点（ m_peers 是节点列表），跳过自己（ i == m_me ）。
  for (int i = 0; i < m_peers.size(); i++)
  {
    if (i == m_me)
    {
      continue;
    }

    // 声明变量存储最后一个日志条目的索引和任期。
    int lastLogIndex = -1, lastLogTerm = -1;
    // 调用 getLastLogIndexAndTerm 函数获取当前节点最后一个日志条目的索引和任期，这用于向其他节点证明自己的日志是最新的（Raft算法要求投票给日志更完整的节点）。
    getLastLogIndexAndTerm(&lastLogIndex, &lastLogTerm); // 获取最后一个日志的索引和任期

    // 创建投票请求对象 RequestVoteArgs 。
    std::shared_ptr<raftRpcProctoc::RequestVoteArgs> requestVoteArgs = std::make_shared<raftRpcProctoc::RequestVoteArgs>();
    requestVoteArgs->set_term(m_currentTerm);        // 当前任期号
    requestVoteArgs->set_candidateid(m_me);          // 候选人ID（当前节点ID）
    requestVoteArgs->set_lastlogindex(lastLogIndex); // 最后一个日志条目的索引
    requestVoteArgs->set_lastlogterm(lastLogTerm);   // 最后一个日志条目的任期

    // 创建投票响应对象 RequestVoteReply ，用于接收其他节点的投票结果。
    auto requestVoteReply = std::make_shared<raftRpcProctoc::RequestVoteReply>();

    // 创建异步线程，调用 sendRequestVote 函数向目标节点发送投票请求。
    std::thread t(&Raft::sendRequestVote, this, i, requestVoteArgs, requestVoteReply, votedNum); // 传递 votedNum 共享指针，以便在收到投票响应时更新票数。
    t.detach();
  }
}

// 发送投票请求，负责发送选举中的RPC，在发送完rpc后还需要负责接收并处理对端发送回来的响应。
bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args, std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum)
{
}
// 处理投票请求，接收别人发来的选举请求，主要检验是否要给对方投票。
void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply)
{
}
// 检查日志是否最新
bool Raft::UpToDate(int index, int term)
{
}
// 获取当前状态
void Raft::GetState(int *term, bool *isLeader)
{
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 日志复制相关方法
// 处理追加日志请求
void Raft::AppendEntries1(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply)
{
}
// 发送追加日志请求
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args, std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply, std::shared_ptr<int> appendNums)
{
}
// 匹配日志
bool Raft::matchLog(int logIndex, int logTerm)
{
}
// 更新提交索引
void Raft::leaderUpdateCommitIndex()
{
}
// 获取新命令索引
int Raft::getNewCommandIndex()
{
}
// 获取前一个日志信息
void Raft::getPrevLogInfo(int server, int *preIndex, int *preTerm)
{
}
// 获取最后日志索引
int Raft::getLastLogIndex()
{
}
// 获取最后日志任期
int Raft::getLastLogTerm()
{
}
// 获取最后日志的索引和任期
void Raft::getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm)
{
}
// 根据索引获取日志任期
int Raft::getLogTermFromLogIndex(int logIndex)
{
}
// 日志逻辑索引转物理索引
int Raft::getSlicesIndexFromLogIndex(int logIndex)
{
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 心跳相关方法
// 发起心跳
void Raft::doHeartBeat()
{
}
// 心跳定时器
void Raft::leaderHearBeatTicker()
{
}

// 定时器维护相关方法
// 向状态机定时写入日志
void Raft::applierTicker()
{
}

// 持久化相关方法
// 持久化当前状态
void Raft::persist()
{
}
// 读取持久化数据
void Raft::readPersist(std::string data)
{
}
// 获取要持久化的数据
std::string Raft::persistData()
{
}
// 创建快照
void Raft::Snapshot(int index, std::string snapshot)
{
}
// 有条件安装快照
bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot)
{
}
// 安装快照
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *reply)
{
}
// 发送快照
void Raft::leaderSendSnapShot(int server)
{
}
// 获取Raft状态大小
int Raft::GetRaftStateSize()
{
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// // 其他方法
// // 获取要应用的日志
// std::vector<ApplyMsg> Raft::getApplyLogs(){}
// // 向KV服务器推送消息
// void Raft::pushMsgToKvServer(ApplyMsg msg){}
// // 开始处理命令
// void Raft::Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader){}
// // 初始化
// void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyCh){}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// RPC接口方法
// RPC追加日志
void Raft::AppendEntries(google::protobuf::RpcController *controller, const ::raftRpcProctoc::AppendEntriesArgs *request, ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done)
{
}
// RPC安装快照
void Raft::InstallSnapshot(google::protobuf::RpcController *controller, const ::raftRpcProctoc::InstallSnapshotRequest *request, ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done)
{
}
// RPC投票请求
void Raft::RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request, ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done)
{
}