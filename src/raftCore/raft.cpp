#include "raft.h"

/* 领导者选举相关方法*/

/// @brief 选举超时定时器
// 定期检查是否需要触发新选举，确保节点在一段时间没有收到心跳后发起选举
void Raft::electionTimeOutTicker()
{
  // 1.持续运行
  // 无限循环，确保定时器持续运行，不会退出。
  while (true)
  {
    // 2.领导者检查
    // 如果当前节点是领导者（ m_status == Leader ），则不需要参与选举，每隔 HeartBeatTimeout 时间（心跳超时时间）休眠一次。
    while (m_status == Leader)
    {
      usleep(HeartBeatTimeout); // usleep 函数用于微秒级休眠，HeartBeatTimeout 是一个常量，定义了心跳间隔。HeartBeatTimeout比选举超时一般小一个数量级。
    }

    // 3.睡眠时间计算
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{}; // 表示适合的睡眠时间，类型为纳秒级持续时间。
    std::chrono::system_clock::time_point wakeTime{};                                      // 表示唤醒时间点，类型为系统时钟时间点。
    {
      m_mtx.lock();
      wakeTime = now();                                                                        // 记录当前时间点。
      suitableSleepTime = getRandomizedElectionTimeout() + m_lastResetElectionTime - wakeTime; // 计算适合的睡眠时间：睡眠时间 = 随机选举超时时间 + 上一次重置时间 - 当前时间。
      m_mtx.unlock();
    }

    // 4.休眠
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

    // 5.定时器重置检查
    if (std::chrono::duration<double, std::milli>(m_lastResetElectionTime - wakeTime).count() > 0) // 检查在睡眠期间选举定时器是否被重置。
    {
      // 上一次重置时间与当前时间的差值如果大于0，说明在睡眠期间定时器被重置（通常是因为收到了领导者的心跳消息），则使用 continue 语句跳过当前循环，重新开始。
      // 这一步是选举定时器的核心逻辑，确保在收到心跳时不会发起不必要的选举。
      continue;
    }

    // 6.选举触发
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

/// @brief 发送投票请求并处理响应
/// 向其他节点发送投票请求，并根据响应更新投票计数，判断是否当选领导者
bool Raft::sendRequestVote(int server, std::shared_ptr<raftRpcProctoc::RequestVoteArgs> args,
                           std::shared_ptr<raftRpcProctoc::RequestVoteReply> reply, std::shared_ptr<int> votedNum) // votedNum : 用于跟踪已获得的票数（在异步环境中安全更新）。
{
  // 1.发送投票请求
  // 通过 m_peers[server] 获取目标节点的RPC客户端存根。
  bool ok = m_peers[server]->RequestVote(args.get(), reply.get()); // ok 表示RPC调用是否成功。

  // 2.处理RPC失败
  if (!ok)
  {
    // 如果RPC调用失败（网络问题或目标节点不可用），直接返回失败结果。
    return ok;
  }

  // 3.线程安全保护
  std::lock_guard<std::mutex> lg(m_mtx);

  // 4.任期比较
  // 检查响应中的任期是否大于当前节点的任期
  if (reply->term() > m_currentTerm)
  {
    // 大于当前节点的任期，说明当前节点的任期已过期，其他节点有更高的任期。
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    persist();
    return true;
  }
  // 检查响应中的任期是否小于当前节点的任期
  else if (reply->term() < m_currentTerm)
  {
    // 说明该响应已过期
    return true;
  }

  // 5.断言任期相等
  myAssert(reply->term() == m_currentTerm, format("assert {reply.Term==rf.currentTerm} fail"));

  // 6.投票处理
  // 检查是否获得投票
  if (!reply->votegranted())
  {
    return true;
  }
  // 如果获得投票，增加票数计数
  *votedNum = *votedNum + 1;

  // 7.多数票检查
  // 检查是否获得多数票
  if (*votedNum > m_peers.size() / 2) // 检查是否获得多数票（票数大于节点总数的一半）。m_peers.size() 是节点总数。
  {
    if (m_status == Candidate) // 如果获得多数票且当前节点是候选人
    {
      m_status = Leader;               // Candidate ==> Leader
      m_lastResetHearBeatTime = now(); // 重置心跳定时器
      // 初始化nextIndex和matchIndex
      for (int i = 0; i < m_peers.size(); i++)
      {
        m_nextIndex[i] = getLastLogIndex() + 1; // 领导者发送给每个跟随者的下一个日志条目的索引，初始化为领导者最后一个日志条目的索引+1。
        m_matchIndex[i] = 0;                    // 领导者已知的每个跟随者已复制的最高日志条目的索引，初始化为0。
      }
      // 立即发送心跳，通知其他节点自己已成为领导者
      doHeartBeat();
    }
  }
  return true;
}

/// @brief 处理投票请求
/// 收到候选人的投票请求时被调用，根据任期和日志新旧程度决定是否投票
// - 参数 args 包含投票请求的参数（任期、候选人ID、最后日志索引和任期）。
// - 参数 reply 用于设置投票响应。
void Raft::RequestVote(const raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *reply)
{
  // 1.线程安全保护
  std::lock_guard<std::mutex> lg(m_mtx);

  // 2.持久化
  // 使用 DEFER 宏（自定义宏，通常用于延迟执行）确保在函数退出前调用 persist() 函数持久化状态变更。
  DEFER { persist(); };

  // 3.任期比较
  // 处理不同任期情况
  if (args->term() < m_currentTerm) // 检查请求的任期是否小于当前节点的任期
  {
    // 小于当前节点的任期说明候选人的任期已过期，拒绝投票。
    reply->set_term(m_currentTerm); // 设置响应的任期为当前节点的任期，提示候选人更新任期。
    reply->set_votestate(Expire);   // 设置投票状态为 Expire （过期）。
    reply->set_votegranted(false);  // 设置 votegranted 为 false （拒绝投票）。
    return;
  }
  if (args->term() > m_currentTerm) // 检查请求的任期是否大于当前节点的任期
  {
    // 大于当前节点的任期说明当前节点的任期已过期，更新状态。
    m_status = Follower;          // 将节点状态设置为 Follower （跟随者）。
    m_currentTerm = args->term(); // 更新当前任期为请求的任期。
    m_votedFor = -1;              // 重置 m_votedFor 为 -1 （表示在新任期内尚未投票给任何候选人）。
  }

  // 4.断言任期相等
  // 断言请求的任期等于当前节点的任期，条件不满足时输出的错误信息。进行防御性编程措施，确保经过前面的处理后，任期一定相等。
  myAssert(args->term() == m_currentTerm, format("[func--rf{%d}] 前面校验过args.Term==rf.currentTerm，此时却不等", m_me));

  // 5.日志完整性检查
  // 检查候选人日志是否足够新
  if (!UpToDate(args->lastlogindex(), args->lastlogterm()))
  {
    // 候选人的日志不是最新的，拒绝投票。
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  }

  // 6.投票记录检查
  // 检查是否已经投票给其他候选人
  if (m_votedFor != -1 && m_votedFor != args->candidateid())
  {
    // m_votedFor 不等于 -1 且不等于当前候选人ID，说明已经投票给其他候选人，拒绝投票。
    reply->set_term(m_currentTerm);
    reply->set_votestate(Voted);
    reply->set_votegranted(false);
    return;
  }
  else
  {
    m_votedFor = args->candidateid();
    m_lastResetElectionTime = now(); // 投票后重置选举定时器
    reply->set_term(m_currentTerm);
    reply->set_votestate(Normal);
    reply->set_votegranted(true); // 设置 votegranted 为 true （授予投票）。
    return;
  }
}

/// @brief 检查日志是否最新
/// 判断候选人的日志是否比当前节点的日志更新，index 是候选人最后一条日志的索引， term 是候选人最后一条日志的任期
bool Raft::UpToDate(int index, int term)
{
  // 1.初始化
  // 声明并初始化两个整数变量 lastIndex 和 lastTerm ，用于存储当前节点最后一条日志的索引和任期，初始值设为-1（表示尚未获取）。
  int lastIndex = -1;
  int lastTerm = -1;

  // 2.获取当前状态
  // 获取当前节点日志信息
  getLastLogIndexAndTerm(&lastIndex, &lastTerm);

  // 3.比较日志新旧
  // 判断候选人日志是否比当前节点日志更新。判断规则遵循Raft算法
  return term > lastTerm || (term == lastTerm && index >= lastIndex);
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 日志复制相关方法
/// @brief 处理追加日志请求
/// 处理来自领导者的日志追加请求，进行日志一致性检查和更新
void Raft::AppendEntries(const raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *reply)
{
  // 1.线程安全保护
  std::lock_guard<std::mutex> locker(m_mtx);
  reply->set_appstate(AppNormal); // 能接收到代表网络是正常的

  // 2.任期检查
  // 如果请求的任期小于当前节点的任期，拒绝请求并返回当前节点的任期
  if (args->term() < m_currentTerm)
  {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(-100);
    DPrintf("[func-AppendEntries-rf{%d}] 拒绝了 因为Leader{%d}的term{%v}< rf{%d}.term{%d}\n", m_me, args->leaderid(),
            args->term(), m_me, m_currentTerm);
    return;
  }
  DEFER { persist(); };
  // 如果请求的任期大于当前节点的任期，更新当前节点的任期，转换为跟随者状态，并重置投票记录。
  if (args->term() > m_currentTerm)
  {
    m_status = Follower;
    m_currentTerm = args->term();
    m_votedFor = -1;
  }

  myAssert(args->term() == m_currentTerm, format("assert {args.Term == rf.currentTerm} fail"));
  // 确保节点为跟随者状态，并重置选举定时器，避免在领导者正常工作时触发新的选举。
  m_status = Follower;             // 确保状态为跟随者
  m_lastResetElectionTime = now(); // 重置选举定时器

  // 3.日志边界检查
  // 如果前一个日志索引大于当前节点的最后日志索引，拒绝请求并返回当前节点的最后日志索引+1。
  if (args->prevlogindex() > getLastLogIndex())
  {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(getLastLogIndex() + 1);
    return;
  }
  // 如果前一个日志索引小于快照包含的最大索引，拒绝请求并返回快照包含的最大索引+1。
  else if (args->prevlogindex() < m_lastSnapshotIncludeIndex)
  {
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    reply->set_updatenextindex(m_lastSnapshotIncludeIndex + 1);
    return;
  }

  // 4.日志匹配检查
  // 调用 matchLog 函数检查前一个日志条目的任期是否匹配。
  if (matchLog(args->prevlogindex(), args->prevlogterm()))
  {
    // 5.日志追加
    // 遍历所有待追加的日志条目。
    for (int i = 0; i < args->entries_size(); i++)
    {
      auto log = args->entries(i);
      // 如果日志索引大于当前节点的最后日志索引，直接添加到日志列表。
      if (log.logindex() > getLastLogIndex())
      {
        m_logs.push_back(log);
      }
      // 如果日志索引已存在，检查任期是否相同
      else
      {
        // 如果任期相同但命令不同，触发断言失败（理论上不应发生）。
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() == log.logterm() &&
            m_logs[getSlicesIndexFromLogIndex(log.logindex())].command() != log.command())
        {
          myAssert(false, format("[func-AppendEntries-rf{%d}] 两节点logIndex{%d}和term{%d}相同，但是其command不同！！\n",
                                 m_me, log.logindex(), log.logterm()));
        }
        // 如果任期不同，替换为新的日志条目。
        if (m_logs[getSlicesIndexFromLogIndex(log.logindex())].logterm() != log.logterm())
        {
          m_logs[getSlicesIndexFromLogIndex(log.logindex())] = log;
        }
      }
    }

    // 断言验证：确保追加日志后的最后日志索引等于前一个日志索引加上追加的日志条目数量。
    myAssert(
        getLastLogIndex() >= args->prevlogindex() + args->entries_size(),
        format("[func-AppendEntries1-rf{%d}]rf.getLastLogIndex(){%d} != args.PrevLogIndex{%d}+len(args.Entries){%d}",
               m_me, getLastLogIndex(), args->prevlogindex(), args->entries_size()));

    //  6.提交索引更新
    //  如果领导者的提交索引大于当前节点的提交索引，更新当前节点的提交索引，但不超过最后日志索引。
    if (args->leadercommit() > m_commitIndex)
    {
      m_commitIndex = std::min(args->leadercommit(), getLastLogIndex());
    }

    // 断言验证：确保最后日志索引大于等于提交索引，这是日志一致性的基本要求。
    myAssert(getLastLogIndex() >= m_commitIndex,
             format("[func-AppendEntries1-rf{%d}]  rf.getLastLogIndex{%d} < rf.commitIndex{%d}", m_me,
                    getLastLogIndex(), m_commitIndex));

    // 7.响应处理
    // 设置响应为成功，并返回当前节点的任期。
    reply->set_success(true);
    reply->set_term(m_currentTerm);

    return;
  }
  // 日志不匹配处理
  else
  {
    // 如果前一个日志条目不匹配，查找最近的匹配点。
    reply->set_updatenextindex(args->prevlogindex());

    for (int index = args->prevlogindex(); index >= m_lastSnapshotIncludeIndex; --index)
    {
      if (getLogTermFromLogIndex(index) != getLogTermFromLogIndex(args->prevlogindex()))
      {
        reply->set_updatenextindex(index + 1);
        break;
      }
    }
    // 设置响应为失败，并返回当前节点的任期。
    reply->set_success(false);
    reply->set_term(m_currentTerm);
    return;
  }
}

/// @brief 发送追加日志请求并处理响应
/// 领导者向跟随者发送追加日志请求，并根据响应更新日志同步状态
bool Raft::sendAppendEntries(int server, std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> args,
                             std::shared_ptr<raftRpcProctoc::AppendEntriesReply> reply,
                             std::shared_ptr<int> appendNums)
{
  // 记录发送追加日志请求的开始，包括当前节点ID、目标节点ID和请求中的日志条目数量。
  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc開始 ， args->entries_size():{%d}", m_me,
          server, args->entries_size());

  // 1.发送RPC请求
  // 发送RPC请求，调用目标节点的AppendEntries方法发送日志追加请求，返回请求是否成功。
  bool ok = m_peers[server]->AppendEntries(args.get(), reply.get());

  // 2.处理请求结果
  // 如果RPC请求失败，记录日志并返回失败结果。
  if (!ok)
  {
    // 如果RPC请求失败，记录日志并返回失败结果
    DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc失敗", m_me, server);
    return ok;
  }
  // 记录RPC请求成功
  DPrintf("[func-Raft::sendAppendEntries-raft{%d}] leader 向节点{%d}发送AE rpc成功", m_me, server);
  // 如果响应状态表示连接断开，直接返回
  if (reply->appstate() == Disconnected)
  {
    return ok;
  }

  // 3.任期检查与更新
  // 线程安全保护
  std::lock_guard<std::mutex> lg1(m_mtx);
  // 如果响应中的任期大于当前节点的任期，更新当前节点的任期，转换为跟随者状态，并重置投票记录。
  if (reply->term() > m_currentTerm)
  {
    m_status = Follower;
    m_currentTerm = reply->term();
    m_votedFor = -1;
    return ok;
  }
  // 如果响应中的任期小于当前节点的任期，记录日志并返回
  else if (reply->term() < m_currentTerm)
  {
    DPrintf("[func -sendAppendEntries  rf{%d}]  节点：{%d}的term{%d}<rf{%d}的term{%d}\n", m_me, server, reply->term(),
            m_me, m_currentTerm);
    return ok;
  }

  // 4.状态检查
  // 确保当前节点仍然是领导者，如果不是，直接返回。
  if (m_status != Leader)
  {
    return ok;
  }

  // 断言验证：确保响应中的任期与当前节点的任期相等。
  myAssert(reply->term() == m_currentTerm,
           format("reply.Term{%d} != rf.currentTerm{%d}   ", reply->term(), m_currentTerm));

  //  5.处理响应
  //  如果响应表示失败且包含更新的nextIndex，则更新对应节点的nextIndex
  if (!reply->success())
  {
    if (reply->updatenextindex() != -100)
    {
      DPrintf("[func -sendAppendEntries  rf{%d}]  返回的日志term相等，但是不匹配，回缩nextIndex[%d]：{%d}\n", m_me,
              server, reply->updatenextindex());
      m_nextIndex[server] = reply->updatenextindex();
    }
  }
  else
  {
    // 增加成功追加计数，并记录日志。
    *appendNums = *appendNums + 1;
    DPrintf("---------------------------tmp------------------------- 節點{%d}返回true,當前*appendNums{%d}", server, *appendNums);

    // 更新匹配索引和下一个索引
    m_matchIndex[server] = std::max(m_matchIndex[server], args->prevlogindex() + args->entries_size()); // 更新目标节点的匹配索引为当前值和请求中日志条目的最大索引的较大值。
    m_nextIndex[server] = m_matchIndex[server] + 1;                                                     // 设置下一个索引为匹配索引加1。
    int lastLogIndex = getLastLogIndex();                                                               // 获取当前节点的最后日志索引。

    // 断言验证：确保下一个索引不超过最后日志索引加1
    myAssert(m_nextIndex[server] <= lastLogIndex + 1,
             format("error msg:rf.nextIndex[%d] > lastLogIndex+1, len(rf.logs) = %d   lastLogIndex{%d} = %d", server,
                    m_logs.size(), server, lastLogIndex));

    // 6.多数派确认检查
    // 多数派确认检查
    if (*appendNums >= 1 + m_peers.size() / 2)
    {
      // 如果成功追加计数达到多数派（1 + 节点数/2），重置计数
      *appendNums = 0;
      // 如果请求中包含当前任期的日志条目，更新提交索引为当前值和请求中最后一个日志条目的索引的较大值。
      if (args->entries_size() > 0 && args->entries(args->entries_size() - 1).logterm() == m_currentTerm)
      {
        DPrintf(
            "---------------------------tmp------------------------- 當前term有log成功提交，更新leader的m_commitIndex "
            "from{%d} to{%d}",
            m_commitIndex, args->prevlogindex() + args->entries_size());

        m_commitIndex = std::max(m_commitIndex, args->prevlogindex() + args->entries_size());
      }

      // 断言验证：确保提交索引不超过最后日志索引，并返回请求是否成功。
      myAssert(m_commitIndex <= lastLogIndex,
               format("[func-sendAppendEntries,rf{%d}] lastLogIndex:%d  rf.commitIndex:%d\n", m_me, lastLogIndex,
                      m_commitIndex));
    }
  }

  // 7.返回请求是否成功
  return ok;
}

/*********************************************************************/
// 提交索引更新
/// @brief 更新提交索引
/// 领导者定期调用，根据跟随者的匹配索引更新提交索引
void Raft::leaderUpdateCommitIndex()
{
  // 初始化提交索引为快照包含的最大索引
  m_commitIndex = m_lastSnapshotIncludeIndex;
  // 从最后日志索引向前遍历，检查每个索引是否被多数节点匹配
  for (int index = getLastLogIndex(); index >= m_lastSnapshotIncludeIndex + 1; index--)
  {
    int sum = 0;
    for (int i = 0; i < m_peers.size(); i++)
    {
      if (i == m_me)
      {
        sum += 1;
        continue;
      }
      if (m_matchIndex[i] >= index)
      {
        sum += 1;
      }
    }

    // 如果匹配节点数达到多数，且该索引的日志任期等于当前任期，则更新提交索引并跳出循环
    if (sum >= m_peers.size() / 2 + 1 && getLogTermFromLogIndex(index) == m_currentTerm)
    {
      m_commitIndex = index;
      break;
    }
  }
}

/*********************************************************************/
// 新命令索引分配
/// @brief 获取新命令索引
/// 计算新命令应该分配的日志索引
int Raft::getNewCommandIndex()
{
  // 获取当前最后日志索引
  auto lastLogIndex = getLastLogIndex();
  return lastLogIndex + 1;
}

/*********************************************************************/
// 日志匹配与复制
/// @brief 匹配日志
/// 检查指定索引的日志任期是否匹配
bool Raft::matchLog(int logIndex, int logTerm)
{
  // 断言验证日志索引大于等于快照包含的最大索引且小于等于最后日志索引
  myAssert(logIndex >= m_lastSnapshotIncludeIndex && logIndex <= getLastLogIndex(),
           format("不满足：logIndex{%d}>=rf.lastSnapshotIncludeIndex{%d}&&logIndex{%d}<=rf.getLastLogIndex{%d}",
                  logIndex, m_lastSnapshotIncludeIndex, logIndex, getLastLogIndex()));

  return logTerm == getLogTermFromLogIndex(logIndex);
}

/// @brief 获取前一个日志信息
/// 领导者调用，获取发送给指定服务器的前一个日志索引和任期
void Raft::getPrevLogInfo(int server, int *preIndex, int *preTerm)
{
  // 获取目标服务器的下一个日志索引 nextIndex
  int nextIndex = m_nextIndex[server];

  // 如果 nextIndex 等于快照包含的最大索引加1，则前一个日志信息为快照的最后一条日志
  if (nextIndex == m_lastSnapshotIncludeIndex + 1)
  {
    *preIndex = m_lastSnapshotIncludeIndex;
    *preTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  // 否则，前一个日志索引为 nextIndex - 1
  *preIndex = nextIndex - 1;
  *preTerm = m_logs[getSlicesIndexFromLogIndex(*preIndex)].logterm();
}

/*********************************************************************/
// 日志索引与任期管理
/// @brief 获取最后日志索引
/// 获取当前节点的最后一个日志索引
int Raft::getLastLogIndex()
{
  int lastLogIndex = -1;
  int _ = -1;
  getLastLogIndexAndTerm(&lastLogIndex, &_);
  return lastLogIndex;
}

/// @brief 获取最后日志任期
/// 获取当前节点的最后一个日志任期
int Raft::getLastLogTerm()
{
  int _ = -1;
  int lastLogTerm = -1;
  getLastLogIndexAndTerm(&_, &lastLogTerm);
  return lastLogTerm;
}

/// @brief 获取最后日志的索引和任期
/// 同时获取最后一个日志的索引和任期
void Raft::getLastLogIndexAndTerm(int *lastLogIndex, int *lastLogTerm)
{
  // 如果日志列表为空，则返回快照包含的最后日志索引和任期
  if (m_logs.empty())
  {
    *lastLogIndex = m_lastSnapshotIncludeIndex;
    *lastLogTerm = m_lastSnapshotIncludeTerm;
    return;
  }
  // 否则，返回日志列表中最后一条日志的索引和任期
  else
  {
    *lastLogIndex = m_logs[m_logs.size() - 1].logindex();
    *lastLogTerm = m_logs[m_logs.size() - 1].logterm();
    return;
  }
}

/// @brief 根据索引获取日志任期
/// 根据日志的逻辑索引获取对应的任期
int Raft::getLogTermFromLogIndex(int logIndex)
{
  // 断言验证日志索引大于等于快照包含的最大索引且小于等于最后日志索引
  myAssert(logIndex >= m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} < rf.lastSnapshotIncludeIndex{%d}", m_me,
                  logIndex, m_lastSnapshotIncludeIndex));

  int lastLogIndex = getLastLogIndex();

  myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                            m_me, logIndex, lastLogIndex));

  // 如果日志索引等于快照包含的最大索引，则返回快照的任期
  if (logIndex == m_lastSnapshotIncludeIndex)
  {
    return m_lastSnapshotIncludeTerm;
  }
  // 否则，转换为物理索引后获取日志任期
  else
  {
    return m_logs[getSlicesIndexFromLogIndex(logIndex)].logterm();
  }
}

/// @brief 日志逻辑索引转物理索引
/// 将日志的逻辑索引转换为在m_logs数组中的物理索引
int Raft::getSlicesIndexFromLogIndex(int logIndex)
{
  // 断言验证日志索引大于快照包含的最大索引且小于等于最后日志索引
  myAssert(logIndex > m_lastSnapshotIncludeIndex,
           format("[func-getSlicesIndexFromLogIndex-rf{%d}]  index{%d} <= rf.lastSnapshotIncludeIndex{%d}", m_me,
                  logIndex, m_lastSnapshotIncludeIndex));
  int lastLogIndex = getLastLogIndex();
  myAssert(logIndex <= lastLogIndex, format("[func-getSlicesIndexFromLogIndex-rf{%d}]  logIndex{%d} > lastLogIndex{%d}",
                                            m_me, logIndex, lastLogIndex));

  // 计算物理索引
  int SliceIndex = logIndex - m_lastSnapshotIncludeIndex - 1;
  return SliceIndex;
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 心跳相关方法
/// @brief 发起心跳
/// 领导者定期调用，向所有跟随者发送心跳消息
void Raft::doHeartBeat()
{
  // 1.线程安全保护
  std::lock_guard<std::mutex> g(m_mtx);

  // 2.状态检查
  // 只有领导者才会发送心跳
  if (m_status == Leader)
  {
    // 记录心跳触发并获取锁，准备发送AppendEntries请求
    DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了且拿到mutex，开始发送AE\n", m_me);
    // 创建一个共享指针，用于跟踪发送的AppendEntries请求数量
    auto appendNums = std::make_shared<int>(1);

    // 3.遍历跟随者
    // 遍历所有节点
    for (int i = 0; i < m_peers.size(); i++)
    {
      // 只向跟随者发送心跳
      if (i == m_me)
      {
        continue;
      }
      DPrintf("[func-Raft::doHeartBeat()-Leader: {%d}] Leader的心跳定时器触发了 index:{%d}\n", m_me, i);
      // 断言检查，确保下一个要发送的日志索引有效(大于等于1)
      myAssert(m_nextIndex[i] >= 1, format("rf.nextIndex[%d] = {%d}", i, m_nextIndex[i]));

      // 4.快照检查
      // 如果下一个要发送的日志索引小于等于最后一个快照包含的索引，说明需要发送快照而不是心跳
      if (m_nextIndex[i] <= m_lastSnapshotIncludeIndex)
      {
        // 创建一个新线程调用 leaderSendSnapShot 函数发送快照，并继续处理下一个节点
        std::thread t(&Raft::leaderSendSnapShot, this, i);
        t.detach();
        continue;
      }

      // 获取前一个日志条目的索引和任期
      int preLogIndex = -1;
      int PrevLogTerm = -1;
      getPrevLogInfo(i, &preLogIndex, &PrevLogTerm);

      // 5.准备请求
      // 创建AppendEntries请求参数对象的共享指针
      std::shared_ptr<raftRpcProctoc::AppendEntriesArgs> appendEntriesArgs = std::make_shared<raftRpcProctoc::AppendEntriesArgs>();
      appendEntriesArgs->set_term(m_currentTerm);         // 当前任期
      appendEntriesArgs->set_leaderid(m_me);              // 领导者ID
      appendEntriesArgs->set_prevlogindex(preLogIndex);   // 前置日志索引
      appendEntriesArgs->set_prevlogterm(PrevLogTerm);    // 前置日志任期
      appendEntriesArgs->clear_entries();                 // 清空日志条目列表
      appendEntriesArgs->set_leadercommit(m_commitIndex); // 领导者的提交索引

      if (preLogIndex != m_lastSnapshotIncludeIndex)
      {
        for (int j = getSlicesIndexFromLogIndex(preLogIndex) + 1; j < m_logs.size(); ++j)
        {
          raftRpcProctoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = m_logs[j];
        }
      }
      else
      {
        for (const auto &item : m_logs)
        {
          raftRpcProctoc::LogEntry *sendEntryPtr = appendEntriesArgs->add_entries();
          *sendEntryPtr = item;
        }
      }

      // 获取最后一个日志条目的索引
      int lastLogIndex = getLastLogIndex();
      myAssert(appendEntriesArgs->prevlogindex() + appendEntriesArgs->entries_size() == lastLogIndex,
               format("appendEntriesArgs.PrevLogIndex{%d}+len(appendEntriesArgs.Entries){%d} != lastLogIndex{%d}",
                      appendEntriesArgs->prevlogindex(), appendEntriesArgs->entries_size(), lastLogIndex));

      // 创建AppendEntries响应对象的共享指针
      const std::shared_ptr<raftRpcProctoc::AppendEntriesReply> appendEntriesReply = std::make_shared<raftRpcProctoc::AppendEntriesReply>();
      // 初始化为断开连接状态
      appendEntriesReply->set_appstate(Disconnected);

      // 6.异步发送
      // 发送AppendEntries请求
      std::thread t(&Raft::sendAppendEntries, this, i, appendEntriesArgs, appendEntriesReply, appendNums);
      t.detach();
    }

    // 7.更新时间
    // 更新最后一次重置心跳的时间为当前时间
    m_lastResetHearBeatTime = now();
  }
}

/// @brief 心跳定时器
/// 领导者定期调用，触发心跳发送
void Raft::leaderHearBeatTicker()
{
  // 1.无限循环
  while (true)
  {
    // 2.领导者检查
    // 当节点不是领导者时，进入内部循环
    while (m_status != Leader)
    {
      // 每隔 HeartBeatTimeout 毫秒睡眠一次
      usleep(1000 * HeartBeatTimeout);
    }
    // 静态原子计数器，用于记录心跳触发的次数。使用 atomic 确保多线程环境下的计数准确性。
    static std::atomic<int32_t> atomicCount = 0;

    // 3.时间计算
    // 定义睡眠时间和唤醒时间变量
    std::chrono::duration<signed long int, std::ratio<1, 1000000000>> suitableSleepTime{};
    std::chrono::system_clock::time_point wakeTime{};
    {
      std::lock_guard<std::mutex> lock(m_mtx);
      // 获取当前时间
      wakeTime = now();
      // 计算合适的睡眠时间：心跳超时时间加上最后一次重置心跳的时间，减去当前时间
      suitableSleepTime = std::chrono::milliseconds(HeartBeatTimeout) + m_lastResetHearBeatTime - wakeTime;
    }

    // 4.睡眠
    // 如果计算出的睡眠时间大于1毫秒，则进行睡眠
    if (std::chrono::duration<double, std::milli>(suitableSleepTime).count() > 1)
    {
      // 输出调试信息，显示设置的睡眠时间。使用ANSI颜色代码 \033[1;35m 使输出为紫色， \033[0m 重置颜色。
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数设置睡眠时间为: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(suitableSleepTime).count() << " 毫秒\033[0m"
                << std::endl;
      // 记录睡眠开始时间，用于后续计算实际睡眠时间
      auto start = std::chrono::steady_clock::now();

      // 将纳秒转换为微秒,根据计算出的睡眠时间进行睡眠
      usleep(std::chrono::duration_cast<std::chrono::microseconds>(suitableSleepTime).count());

      // 记录睡眠结束时间,并计算实际睡眠时间
      auto end = std::chrono::steady_clock::now();
      std::chrono::duration<double, std::milli> duration = end - start;

      // 输出实际睡眠时间
      std::cout << atomicCount << "\033[1;35m leaderHearBeatTicker();函数实际睡眠时间为: " << duration.count()
                << " 毫秒\033[0m" << std::endl;
      // 递增计数器
      ++atomicCount;
    }

    // 检查最后一次重置心跳的时间是否大于当前唤醒时间。如果是，说明心跳已经被其他地方触发，跳过本次循环
    if (std::chrono::duration<double, std::milli>(m_lastResetHearBeatTime - wakeTime).count() > 0)
    {
      continue;
    }

    // 5.心跳触发
    // 发送心跳
    doHeartBeat();
  }
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 定时器维护相关方法
/// @brief 向状态机定时写入日志
/// 定期将已提交的日志应用到状态机
void Raft::applierTicker()
{
  // 1.无限循环
  while (true)
  {
    // 2.线程安全保护
    m_mtx.lock();

    // 3.状态检查
    // 如果是领导者，输出调试信息，记录最后应用的日志索引和提交索引
    if (m_status == Leader)
    {
      DPrintf("[Raft::applierTicker() - raft{%d}]  m_lastApplied{%d}   m_commitIndex{%d}", m_me, m_lastApplied, m_commitIndex);
    }

    // 4.获取待应用日志
    // 获取需要应用到状态机的日志消息
    auto applyMsgs = getApplyLogs();

    // 5.释放锁
    m_mtx.unlock();

    // 6.应用日志
    // 检查是否有日志需要应用
    if (!applyMsgs.empty())
    {
      // 记录需要应用的日志数量
      DPrintf("[func- Raft::applierTicker()-raft{%d}] 向kvserver報告的applyMsgs長度爲：{%d}", m_me, applyMsgs.size());
    }
    // 遍历所有需要应用的日志消息，并将它们推送到应用通道applyChan
    for (auto &message : applyMsgs)
    {
      applyChan->Push(message);
    }

    // 7.定期执行
    // 睡眠 ApplyInterval 毫秒，控制日志应用的频率
    sleepNMilliseconds(ApplyInterval);
  }
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 持久化相关方法

// 基础持久化函数
/// @brief 持久化当前状态
/// 将当前的Raft状态持久化到存储
void Raft::persist()
{
  auto data = persistData();
  m_persister->SaveRaftState(data);
}

/// @brief 读取持久化数据
/// 从存储中读取之前持久化的Raft状态
void Raft::readPersist(std::string data) // 包含序列化的Raft状态数据data
{
  // 1.输入验证
  // 检查输入数据是否为空
  if (data.empty())
  {
    return;
  }

  // 2.准备反序列化环境
  // 创建字符串流，并用输入数据初始化。用于将字符串数据转换为流，供Boost序列化库读取
  std::stringstream iss(data);
  // 关联到字符串流 iss，这是Boost序列化库的文本输入归档，用于反序列化数据
  boost::archive::text_iarchive ia(iss);

  // 3.反序列化
  // 创建 BoostPersistRaftNode 类型的临时对象，该类是Raft的内部类，专门用于封装需要序列化/反序列化的状态数据
  BoostPersistRaftNode boostPersistRaftNode;
  // 使用Boost输入归档 ia 从流中读取数据并反序列化为 boostPersistRaftNode 对象
  ia >> boostPersistRaftNode;

  // 4.恢复核心状态
  // 从反序列化得到的对象中恢复当前任期号
  m_currentTerm = boostPersistRaftNode.m_currentTerm;
  // 恢复当前任期内投票支持的候选人ID
  m_votedFor = boostPersistRaftNode.m_votedFor;
  // 恢复快照中包含的最后一个日志条目的索引
  m_lastSnapshotIncludeIndex = boostPersistRaftNode.m_lastSnapshotIncludeIndex;
  // 恢复快照中包含的最后一个日志条目的任期号
  m_lastSnapshotIncludeTerm = boostPersistRaftNode.m_lastSnapshotIncludeTerm;

  // 5.恢复日志
  // 清空当前的日志列表，准备从持久化数据中恢复
  m_logs.clear();
  // 遍历反序列化得到的日志字符串列表
  for (auto &item : boostPersistRaftNode.m_logs)
  {
    raftRpcProctoc::LogEntry logEntry;
    logEntry.ParseFromString(item);
    m_logs.emplace_back(logEntry); // 比 push_back 更高效，直接在容器内构造对象
  }
}

/// @brief 获取要持久化的数据
/// 准备要持久化的Raft状态数据
std::string Raft::persistData()
{
  // 1.数据封装
  // 将Raft节点的核心状态数据复制到 boostPersistRaftNode 对象中
  BoostPersistRaftNode boostPersistRaftNode;
  boostPersistRaftNode.m_currentTerm = m_currentTerm;
  boostPersistRaftNode.m_votedFor = m_votedFor;
  boostPersistRaftNode.m_lastSnapshotIncludeIndex = m_lastSnapshotIncludeIndex;
  boostPersistRaftNode.m_lastSnapshotIncludeTerm = m_lastSnapshotIncludeTerm;

  // 2.日志序列化
  // 遍历所有日志条目，将每个条目序列化为字符串并添加到 boostPersistRaftNode 对象的日志列表中
  for (auto &item : m_logs)
  {
    // 调用Protocol Buffers库的方法，将 LogEntry 对象序列化为字符串
    boostPersistRaftNode.m_logs.push_back(item.SerializeAsString());
  }

  // 3.序列化环境准备
  std::stringstream ss;
  // Boost序列化库中的文本输出归档
  boost::archive::text_oarchive oa(ss);

  // 4.对象序列化
  // 将对象序列化到字符串流ss中
  oa << boostPersistRaftNode;

  // 5.返回序列化结果
  return ss.str();
}

/*********************************************************************/
/// @brief 创建快照
/// 创建当前状态的快照，用于日志压缩
void Raft::Snapshot(int index, std::string snapshot)
{
  // 1.线程安全保护
  std::lock_guard<std::mutex> lg(m_mtx);

  // 2.参数有效性检查
  // 如果要创建的快照索引 index 小于等于当前最大快照索引 m_lastSnapshotIncludeIndex ，或者大于已提交日志索引 m_commitIndex ，则拒绝创建快照。
  if (m_lastSnapshotIncludeIndex >= index || index > m_commitIndex)
  {
    DPrintf(
        "[func-Snapshot-rf{%d}] rejects replacing log with snapshotIndex %d as current snapshotIndex %d is larger or "
        "smaller ",
        m_me, index, m_lastSnapshotIncludeIndex);
    return;
  }
  // 获取最后日志索引
  auto lastLogIndex = getLastLogIndex();

  // 3.准备新快照信息
  // 新快照包含的最后日志索引，赋值为参数 index
  int newLastSnapshotIncludeIndex = index;
  // 将日志索引转换为 m_logs 数组中的索引，再根据索引获取指定日志条目的任期号
  int newLastSnapshotIncludeTerm = m_logs[getSlicesIndexFromLogIndex(index)].logterm();

  // 4.日志压缩
  // 创建新的日志向量 trunckedLogs
  std::vector<raftRpcProctoc::LogEntry> trunckedLogs;
  // 遍历从 index + 1 到最后日志索引的所有日志条目，这样可以截掉 index 及之前的日志条目
  for (int i = index + 1; i <= getLastLogIndex(); i++)
  {
    trunckedLogs.push_back(m_logs[getSlicesIndexFromLogIndex(i)]);
  }

  // 5.状态更新
  // 更新最大快照索引和对应任期
  m_lastSnapshotIncludeIndex = newLastSnapshotIncludeIndex;
  m_lastSnapshotIncludeTerm = newLastSnapshotIncludeTerm;
  // 替换日志为截取后的日志
  m_logs = trunckedLogs;
  // 确保它们至少为 index
  m_commitIndex = std::max(m_commitIndex, index);
  m_lastApplied = std::max(m_lastApplied, index);

  // 6.持久化
  // 持久化方法，第一个参数是通过 persistData() 获取的序列化状态数据，第二个参数是快照数据
  m_persister->Save(persistData(), snapshot);

  // 7.调试与验证
  DPrintf("[SnapShot]Server %d snapshot snapshot index {%d}, term {%d}, loglen {%d}", m_me, index,
          m_lastSnapshotIncludeTerm, m_logs.size());
  // 检查剩余日志长度加上快照索引是否等于之前记录的最后日志索引，确保日志完整性
  myAssert(m_logs.size() + m_lastSnapshotIncludeIndex == lastLogIndex,
           format("len(rf.logs){%d} + rf.lastSnapshotIncludeIndex{%d} != lastLogjInde{%d}", m_logs.size(),
                  m_lastSnapshotIncludeIndex, lastLogIndex));
}

/// @brief 有条件安装快照
/// 检查是否应该安装快照
bool Raft::CondInstallSnapshot(int lastIncludedTerm, int lastIncludedIndex, std::string snapshot)
{
  return true;
}

/// @brief 安装快照
/// 处理来自领导者的快照安装请求
void Raft::InstallSnapshot(const raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *reply)
{
  // 1.线程安全保护
  m_mtx.lock();
  // 确保函数退出时自动释放互斥锁
  DEFER { m_mtx.unlock(); };

  // 2.任期检查
  // 如果请求的任期小于当前节点的任期，说明发送请求的节点不是合法领导者，拒绝安装快照
  if (args->term() < m_currentTerm)
  {
    // 设置响应中的任期号为当前节点的任期
    reply->set_term(m_currentTerm);
    return;
  }
  // 如果请求的任期大于当前节点的任期，更新当前节点的任期，重置投票记录，将状态改为跟随者，并持久化新状态
  if (args->term() > m_currentTerm)
  {
    m_currentTerm = args->term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
  }
  // 确保节点状态为跟随者
  m_status = Follower;
  m_lastResetElectionTime = now(); // 最后重置选举定时器的时间

  // 3.参数有效性检查
  // 如果请求中的快照索引小于等于当前节点的最大快照索引，说明当前节点已有更新或相同的快照，无需安装
  if (args->lastsnapshotincludeindex() <= m_lastSnapshotIncludeIndex)
  {
    return;
  }

  // 获取当前日志中的最后一个条目的索引
  auto lastLogIndex = getLastLogIndex();

  // 4.日志截断
  // 如果最后日志索引大于快照索引，删除快照索引及之前的日志条目，保留快照索引之后的条目
  if (lastLogIndex > args->lastsnapshotincludeindex())
  {
    // 删除日志向量中的指定范围元素
    m_logs.erase(m_logs.begin(), m_logs.begin() + getSlicesIndexFromLogIndex(args->lastsnapshotincludeindex()) + 1);
  }
  // 如果最后日志索引小于等于快照索引，清空所有日志条目
  else
  {
    m_logs.clear();
  }

  // 5.状态更新
  // 更新状态变量
  // 确保它们至少为快照索引
  m_commitIndex = std::max(m_commitIndex, args->lastsnapshotincludeindex());
  m_lastApplied = std::max(m_lastApplied, args->lastsnapshotincludeindex());
  // 更新快照相关状态变量：最大快照索引和对应任期
  m_lastSnapshotIncludeIndex = args->lastsnapshotincludeindex();
  m_lastSnapshotIncludeTerm = args->lastsnapshotincludeterm();

  // 6.响应设置
  // 设置响应中的任期号为当前节点的任期
  reply->set_term(m_currentTerm);

  // 7.快照应用
  // 设置快照相关信息
  ApplyMsg msg;
  msg.SnapshotValid = true;    // 标记快照有效
  msg.Snapshot = args->data(); // 获取快照数据
  msg.SnapshotTerm = args->lastsnapshotincludeterm();
  msg.SnapshotIndex = args->lastsnapshotincludeindex();

  // 创建新线程调用 pushMsgToKvServer 方法，将快照消息推送到KV服务器
  std::thread t(&Raft::pushMsgToKvServer, this, msg);
  t.detach();

  // 8.持久化
  // 持久化快照和状态
  m_persister->Save(persistData(), args->data());
}

/// @brief 发送快照
/// 领导者向跟随者发送快照
void Raft::leaderSendSnapShot(int server)
{
  // 1.线程安全保护
  m_mtx.lock();

  // 2.请求准备
  // 创建快照请求对象，用于封装快照请求信息
  raftRpcProctoc::InstallSnapshotRequest args;
  // 设置请求参数
  args.set_leaderid(m_me);                                       // 设置领导者ID为当前节点ID
  args.set_term(m_currentTerm);                                  // 设置请求的任期为当前节点的任期
  args.set_lastsnapshotincludeindex(m_lastSnapshotIncludeIndex); // 设置快照包含的最后日志索引
  args.set_lastsnapshotincludeterm(m_lastSnapshotIncludeTerm);   // 设置快照包含的最后日志任期
  args.set_data(m_persister->ReadSnapshot());                    // 设置快照数据，通过 m_persister->ReadSnapshot() 读取持久化的快照数据

  // 接收响应信息
  raftRpcProctoc::InstallSnapshotResponse reply;

  // 3.释放锁
  // RPC调用可能会阻塞，避免长时间持有锁
  m_mtx.unlock();

  // 4.发送RPC
  // 通过 m_peers[server] 调用目标服务器的 InstallSnapshot 方法发送快照请求
  bool ok = m_peers[server]->InstallSnapshot(&args, &reply);

  // 5.重新获取锁
  m_mtx.lock();
  DEFER { m_mtx.unlock(); };

  // 6.响应处理
  // 检查RPC调用是否成功
  if (!ok)
  {
    return;
  }
  // 如果当前节点不再是领导者，或者任期发生了变化，直接返回
  if (m_status != Leader || m_currentTerm != args.term())
  {
    return;
  }

  // 7.状态更新
  // 如果响应中的任期大于当前节点的任期，更新当前节点的任期，重置投票记录，将状态改为跟随者，持久化新状态，并重置选举计时器
  if (reply.term() > m_currentTerm)
  {
    m_currentTerm = reply.term();
    m_votedFor = -1;
    m_status = Follower;
    persist();
    m_lastResetElectionTime = now();
    return;
  }
  // 更新目标跟随者的匹配索引为快照包含的最后日志索引
  m_matchIndex[server] = args.lastsnapshotincludeindex();
  // 更新目标跟随者的下一个索引为匹配索引加1，表示下一次将从该索引开始发送日志
  m_nextIndex[server] = m_matchIndex[server] + 1;
}

/// @brief 获取Raft状态大小
/// 获取当前持久化的Raft状态大小
int Raft::GetRaftStateSize()
{
  return m_persister->RaftStateSize();
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// 其他方法
// 获取要应用的日志
std::vector<ApplyMsg> Raft::getApplyLogs()
{
}
// 向KV服务器推送消息
void Raft::pushMsgToKvServer(ApplyMsg msg) {}
// 开始处理命令
void Raft::Start(Op command, int *newLogIndex, int *newLogTerm, bool *isLeader) {}
// 初始化
void Raft::init(std::vector<std::shared_ptr<RaftRpcUtil>> peers, int me, std::shared_ptr<Persister> persister, std::shared_ptr<LockQueue<ApplyMsg>> applyCh) {}
// 获取当前状态
void Raft::GetState(int *term, bool *isLeader)
{
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// RPC接口方法
/// @brief RPC追加日志
/// gRPC接口实现，处理追加日志请求
void Raft::AppendEntries(google::protobuf::RpcController *controller,
                         const ::raftRpcProctoc::AppendEntriesArgs *request,
                         ::raftRpcProctoc::AppendEntriesReply *response, ::google::protobuf::Closure *done)
{
  AppendEntries(request, response);
  done->Run();
}

/// @brief RPC安装快照
/// gRPC接口实现，处理安装快照请求
void Raft::InstallSnapshot(google::protobuf::RpcController *controller,
                           const ::raftRpcProctoc::InstallSnapshotRequest *request,
                           ::raftRpcProctoc::InstallSnapshotResponse *response, ::google::protobuf::Closure *done)
{
  InstallSnapshot(request, response);
  done->Run();
}

/// @brief RPC投票请求
/// gRPC接口实现，处理投票请求
void Raft::RequestVote(google::protobuf::RpcController *controller, const ::raftRpcProctoc::RequestVoteArgs *request,
                       ::raftRpcProctoc::RequestVoteReply *response, ::google::protobuf::Closure *done)
{
  RequestVote(request, response);
  done->Run();
}