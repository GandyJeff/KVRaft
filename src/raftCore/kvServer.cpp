#include "kvServer.h"
#include "mprpcconfig.h"
#include <rpcprovider.h>

// ====================== 构造函数与初始化 ======================
/**
 * @brief KvServer构造函数
 * @param me 当前服务器ID
 * @param maxraftstate 触发快照的日志大小阈值
 * @param nodeInforFileName 节点信息文件名
 * @param port 服务器端口
 * @details 初始化KV服务器，创建Raft节点，启动RPC服务，连接其他节点
 */
KvServer::KvServer(int me, int maxraftstate, std::string nodeInforFileName, short port)
    : m_skipList(6) // 初始化跳表数据结构，设置层数为6
{
    // 1.初始化数据结构
    // 创建持久化对象
    std::shared_ptr<Persister> persister = std::make_shared<Persister>(me);
    // 初始化成员变量，存储当前节点ID和Raft状态大小限制
    m_me = me;
    m_maxRaftState = maxraftstate;
    // 创建应用消息队列，用于在KV服务器和Raft节点之间传递 ApplyMsg 消息
    applyChan = std::make_shared<LockQueue<ApplyMsg>>(); // 这是KV层与Raft层之间的主要通信通道
    // 实例化Raft节点，用于分布式一致性协调
    m_raftNode = std::make_shared<Raft>();

    // 2.启动RPC服务线程
    // clerk层面 kvserver开启rpc接受功能
    // 同时raft与raft节点之间也要开启rpc功能，因此有两个注册
    std::thread t([this, port]() -> void
                  {
    // 初始化 RpcProvider 并注册 KvServer 和 Raft 节点服务
    RpcProvider provider;
    provider.NotifyService(this);
    provider.NotifyService(this->m_raftNode.get());  

    // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
    provider.Run(m_me, port); });
    t.detach();

    // 等待其他节点启动
    ////开启rpc远程调用能力，需要注意必须要保证所有节点都开启rpc接受功能之后才能开启rpc远程调用能力
    ////这里使用睡眠来保证
    std::cout << "raftServer node:" << m_me << " start to sleep to wait all ohter raftnode start!!!!" << std::endl;
    sleep(6);
    std::cout << "raftServer node:" << m_me << " wake up!!!! start to connect other raftnode" << std::endl;

    // 3.节点发现与连接
    // 加载节点信息并连接其他节点，获取所有raft节点ip、port，并进行连接，且要排除自己
    MprpcConfig config;
    config.LoadConfigFile(nodeInforFileName.c_str());

    // 解析所有节点的IP和端口信息，存储到 ipPortVt 向量中
    std::vector<std::pair<std::string, short>> ipPortVt;
    for (int i = 0; i < INT_MAX - 1; ++i)
    {
        std::string node = "node" + std::to_string(i);

        std::string nodeIp = config.Load(node + "ip");
        std::string nodePortStr = config.Load(node + "port");
        if (nodeIp.empty())
        {
            break;
        }
        ipPortVt.emplace_back(nodeIp, atoi(nodePortStr.c_str()));
    }

    // 4.Raft初始化
    // 创建 RaftRpcUtil 对象连接其他节点，存储到 servers 向量中
    std::vector<std::shared_ptr<RaftRpcUtil>> servers;
    // 进行连接
    for (int i = 0; i < ipPortVt.size(); ++i)
    {
        // 当前节点在 servers 向量中对应的位置设置为 nullptr
        if (i == m_me)
        {
            servers.push_back(nullptr);
            continue;
        }

        std::string otherNodeIp = ipPortVt[i].first;
        short otherNodePort = ipPortVt[i].second;
        auto *rpc = new RaftRpcUtil(otherNodeIp, otherNodePort);
        servers.push_back(std::shared_ptr<RaftRpcUtil>(rpc));

        std::cout << "node" << m_me << " 连接node" << i << "success!" << std::endl;
    }
    // 根据节点数量和当前节点ID进行睡眠，确保所有节点之间的连接都已建立。等待所有节点相互连接成功，再启动raft
    sleep(ipPortVt.size() - me);
    // 调用 Raft 节点的 init 方法进行初始化
    m_raftNode->init(servers, m_me, persister, applyChan);
    // kv的server直接与raft通信，但kv不直接与raft通信，所以需要把ApplyMsg的chan传递下去用于通信，两者的persist也是共用的

    // 5.快照恢复
    // 初始化成员变量和加载快照
    m_skipList;      // 初始化跳表
    waitApplyCh;     // 初始化等待通道
    m_lastRequestId; // 初始化最后请求ID映射
    m_lastSnapShotRaftLogIndex = 0;
    auto snapshot = persister->ReadSnapshot();
    // 如果快照不为空，则调用 ReadSnapShotToInstall 方法安装快照
    if (!snapshot.empty())
    {
        ReadSnapShotToInstall(snapshot);
    }

    // 6.启动命令处理循环
    // 创建新线程运行 ReadRaftApplyCommandLoop 方法，该方法负责处理来自Raft的应用命令
    std::thread t2(&KvServer::ReadRaftApplyCommandLoop, this); // 马上向其他节点宣告自己就是leader
    t2.join();                                                 // 使用 join() 等待线程结束，但由于 ReadRaftApplyCommandLoop 会无限循环，因此构造函数会一直阻塞在这里
}

// ====================== KV操作方法 ======================
/**
 * @brief 追加值到现有键或创建新键到KV数据库
 * @param op 操作对象，包含键、值、客户端ID和请求ID
 * @details 将值追加到指定键对应的值上，如果键不存在则创建
 */
void KvServer::ExecuteAppendOpOnKVDB(Op op)
{
    m_mtx.lock();

    std::string oldValue;
    if (m_skipList.search_element(op.Key, oldValue))
    {
        // 键存在，追加值
        std::string newValue = oldValue + op.Value;
        m_skipList.insert_set_element(op.Key, newValue);
    }
    else
    {
        // 键不存在，直接插入
        m_skipList.insert_element(op.Key, op.Value);
    }
    // 更新最后请求ID
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mtx.unlock();

    // 调试信息
    DprintfKVDB();
}

/**
 * @brief 执行写入操作到KV数据库
 * @param op 操作对象，包含键、值、客户端ID和请求ID
 * @details 将指定的值写入到指定的键，覆盖已存在的值
 */
void KvServer::ExecutePutOpOnKVDB(Op op)
{
    m_mtx.lock();
    m_skipList.insert_set_element(op.Key, op.Value);
    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mtx.unlock();

    //    DPrintf("[KVServerExePUT----]ClientId :%d ,RequestID :%d ,Key : %v, value : %v", op.ClientId, op.RequestId,
    //    op.Key, op.Value)
    DprintfKVDB();
}

/**
 * @brief 执行从KV数据库获取操作
 * @param op 操作对象，包含键、客户端ID和请求ID
 * @param value 用于存储获取到的值的指针
 * @param exist 用于指示键是否存在的指针
 * @details 从KV数据库中获取指定键的值，如果键存在则设置exist为true并返回值
 */
void KvServer::ExecuteGetOpOnKVDB(Op op, std::string *value, bool *exist)
{
    m_mtx.lock();
    *value = "";
    *exist = false;

    // 查找键 op.Key ：如果找到，方法返回 true ，将对应值存入 *value ，并设置 *exist = true
    if (m_skipList.search_element(op.Key, *value))
    {
        *exist = true;
    }

    m_lastRequestId[op.ClientId] = op.RequestId;
    m_mtx.unlock();

    DprintfKVDB();
}

/**
 * @brief 调试用：打印KV数据库内容
 * @details 当Debug标志为true时，打印KV数据库中的所有键值对
 */
void KvServer::DprintfKVDB()
{
    if (!Debug)
    {
        return;
    }

    std::lock_guard<std::mutex> lg(m_mtx);
    DEFER { m_skipList.display_list(); };
}

// ====================== 客户端API方法 ======================
/**
 * @brief 客户端Get请求处理
 * @param args Get请求参数，包含键、客户端ID和请求ID
 * @param reply Get请求响应，包含错误码和值
 * @details 处理客户端的Get请求，通过Raft共识后执行查询操作
 */
void KvServer::Get(const raftKVRpcProtoc::GetArgs *args, raftKVRpcProtoc::GetReply *reply)
{
    // 1.参数解析与准备
    Op op;
    op.Operation = "Get";
    op.Key = args->key();
    op.Value = "";
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();

    // 2.Raft提交
    int raftIndex = -1;
    int _ = -1;
    bool isLeader = false;
    // 提交操作到Raft集群，获取日志索引和leader状态
    m_raftNode->Start(op, &raftIndex, &_, &isLeader);

    // 3.Leader检查
    if (!isLeader)
    {
        reply->set_err(ErrWrongLeader);
        return;
    }

    // 4.等待机制
    // 创建等待通道，用于接收Raft提交后的操作通知
    m_mtx.lock();
    if (waitApplyCh.find(raftIndex) == waitApplyCh.end())
    {
        waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];
    m_mtx.unlock(); // 直接解锁，等待任务执行完成，不能一直拿锁等待

    // 5.超时处理
    // 等待Raft提交操作
    Op raftCommitOp;
    if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp))
    {
        // 6.操作执行
        // 获取当前节点的最新状态
        int _ = -1;
        bool isLeader = false;
        m_raftNode->GetState(&_, &isLeader);

        // 超时情况
        // 检查请求是否重复且当前节点是否仍是leader，如果是则直接执行Get操作
        if (ifRequestDuplicate(op.ClientId, op.RequestId) && isLeader)
        {
            // 如果超时，代表raft集群不保证已经commitIndex该日志，但是如果是已经提交过的get请求，是可以再执行的。
            //  不会违反线性一致性
            std::string value;
            bool exist = false;
            ExecuteGetOpOnKVDB(op, &value, &exist);

            if (exist)
            {
                reply->set_err(OK);
                reply->set_value(value);
            }
            else
            {
                reply->set_err(ErrNoKey);
                reply->set_value("");
            }
        }
        else
        {
            // 否则返回 ErrWrongLeader
            reply->set_err(ErrWrongLeader); // 返回这个，其实就是让clerk换一个节点重试
        }
    }
    // 非超时情况
    else
    {
        // 验证提交的操作是否匹配请求的操作，如果匹配则执行Get操作并返回结果
        if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId)
        {
            std::string value;
            bool exist = false;
            ExecuteGetOpOnKVDB(op, &value, &exist);
            if (exist)
            {
                reply->set_err(OK);
                reply->set_value(value);
            }
            else
            {
                reply->set_err(ErrNoKey);
                reply->set_value("");
            }
        }
        else
        {
            // 否则返回 ErrWrongLeader
            reply->set_err(ErrWrongLeader);
        }
    }

    // 7.资源清理
    m_mtx.lock();
    auto tmp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete tmp;
    m_mtx.unlock();
}

/**
 * @brief 客户端PutAppend请求处理
 * @param args PutAppend请求参数，包含操作类型、键、值、客户端ID和请求ID
 * @param reply PutAppend请求响应，包含错误码
 * @details 处理客户端的Put或Append请求，通过Raft共识后执行相应操作
 */
void KvServer::PutAppend(const raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply)
{
    Op op;
    op.Operation = args->op();
    op.Key = args->key();
    op.Value = args->value();
    op.ClientId = args->clientid();
    op.RequestId = args->requestid();
    int raftIndex = -1;
    int _ = -1;
    bool isleader = false;

    m_raftNode->Start(op, &raftIndex, &_, &isleader);

    if (!isleader)
    {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request %d) To Server %d, key %s, raftIndex %d , but "
            "not leader",
            m_me, &args->clientid(), args->requestid(), m_me, &op.Key, raftIndex);

        reply->set_err(ErrWrongLeader);
        return;
    }

    DPrintf(
        "[func -KvServer::PutAppend -kvserver{%d}]From Client %s (Request %d) To Server %d, key %s, raftIndex %d , is "
        "leader ",
        m_me, &args->clientid(), args->requestid(), m_me, &op.Key, raftIndex);

    // 创建等待通道用于接收Raft提交后的操作通知
    m_mtx.lock();
    if (waitApplyCh.find(raftIndex) == waitApplyCh.end())
    {
        waitApplyCh.insert(std::make_pair(raftIndex, new LockQueue<Op>()));
    }
    auto chForRaftIndex = waitApplyCh[raftIndex];
    m_mtx.unlock(); // 直接解锁，等待任务执行完成，不能一直拿锁等待

    // 处理超时和非超时情况
    Op raftCommitOp;
    // 超时情况：打印调试信息，检查请求是否重复，如果是则返回 OK
    if (!chForRaftIndex->timeOutPop(CONSENSUS_TIMEOUT, &raftCommitOp))
    {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]TIMEOUT PUTAPPEND !!!! Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %s, Opreation %s Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

        if (ifRequestDuplicate(op.ClientId, op.RequestId))
        {
            reply->set_err(OK); // 超时了，但因为是重复的请求，返回ok，实际上就算没有超时，在真正执行的时候也要判断是否重复
        }
        else
        {
            reply->set_err(ErrWrongLeader); /// 这里返回这个的目的让clerk重新尝试
        }
    }
    // 非超时情况
    else
    {
        DPrintf(
            "[func -KvServer::PutAppend -kvserver{%d}]WaitChanGetRaftApplyMessage<--Server %d , get Command <-- Index:%d , "
            "ClientId %s, RequestId %d, Opreation %s, Key :%s, Value :%s",
            m_me, m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

        // 验证提交的操作是否匹配请求的操作
        if (raftCommitOp.ClientId == op.ClientId && raftCommitOp.RequestId == op.RequestId)
        {
            // 可能发生leader的变更导致日志被覆盖，因此必须检查
            reply->set_err(OK);
        }
        else
        {
            reply->set_err(ErrWrongLeader);
        }
    }

    m_mtx.lock();
    auto tmp = waitApplyCh[raftIndex];
    waitApplyCh.erase(raftIndex);
    delete tmp;
    m_mtx.unlock();
}

// ====================== Raft交互方法 ======================
/**
 * @brief 处理从Raft提交的命令
 * @param message Raft应用消息，包含命令和索引
 * @details 从Raft获取已提交的命令并执行，更新KV数据库
 */
void KvServer::GetCommandFromRaft(ApplyMsg message)
{
    Op op;
    op.parseFromString(message.Command);

    DPrintf(
        "[KvServer::GetCommandFromRaft-kvserver{%d}] , Got Command --> Index:{%d} , ClientId {%s}, RequestId {%d}, "
        "Opreation {%s}, Key :{%s}, Value :{%s}",
        m_me, message.CommandIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    if (message.CommandIndex <= m_lastSnapShotRaftLogIndex)
    {
        return;
    }

    // State Machine (KVServer solute the duplicate problem)
    // duplicate command will not be exed
    if (!ifRequestDuplicate(op.ClientId, op.RequestId))
    {
        // execute command
        if (op.Operation == "Put")
        {
            ExecutePutOpOnKVDB(op);
        }
        if (op.Operation == "Append")
        {
            ExecuteAppendOpOnKVDB(op);
        }
        //  kv.lastRequestId[op.ClientId] = op.RequestId  在Executexxx函数里面更新的
    }
    // 到这里kvDB已经制作了快照
    if (m_maxRaftState != -1)
    {
        IfNeedToSendSnapShotCommand(message.CommandIndex, 9);
        // 如果raft的log太大（大于指定的比例）就把制作快照
    }

    // Send message to the chan of op.ClientId
    SendMessageToWaitChan(op, message.CommandIndex);
}

/**
 * @brief 循环读取并处理Raft应用命令
 * @details 持续从applyChan中读取Raft应用消息，并调用相应的处理函数
 */
void KvServer::ReadRaftApplyCommandLoop()
{
    while (true)
    {
        // 如果只操作applyChan不用拿锁，因为applyChan自己带锁
        auto message = applyChan->Pop(); // 阻塞弹出
        DPrintf(
            "---------------tmp-------------[func-KvServer::ReadRaftApplyCommandLoop()-kvserver{%d}] 收到了以下raft的消息",
            m_me);

        if (message.CommandValid)
        {
            GetCommandFromRaft(message);
        }
        if (message.SnapshotValid)
        {
            GetSnapShotFromRaft(message);
        }
    }
}

/**
 * @brief 向等待通道发送消息
 * @param op 操作对象
 * @param raftIndex Raft日志索引
 * @return 如果发送成功返回true，否则返回false
 * @details 将操作发送到对应索引的等待通道，通知客户端请求已处理完成
 */
bool KvServer::SendMessageToWaitChan(const Op &op, int raftIndex)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);

    if (waitApplyCh.find(raftIndex) == waitApplyCh.end())
    {
        return false;
    }
    waitApplyCh[raftIndex]->Push(op);
    DPrintf(
        "[RaftApplyMessageSendToWaitChan--> raftserver{%d}] , Send Command --> Index:{%d} , ClientId {%d}, RequestId "
        "{%d}, Opreation {%v}, Key :{%v}, Value :{%v}",
        m_me, raftIndex, &op.ClientId, op.RequestId, &op.Operation, &op.Key, &op.Value);
    return true;
}

/**
 * @brief 检查请求是否重复
 * @param ClientId 客户端ID
 * @param RequestId 请求ID
 * @return 如果请求重复返回true，否则返回false
 * @details 通过比较客户端最后一次请求ID来判断当前请求是否重复
 */
bool KvServer::ifRequestDuplicate(std::string ClientId, int RequestId)
{
    std::lock_guard<std::mutex> lg(m_mtx);
    if (m_lastRequestId.find(ClientId) == m_lastRequestId.end())
    {
        return false;
        // todo :不存在这个client就创建
    }
    return RequestId <= m_lastRequestId[ClientId];
}

// ====================== 快照管理方法 ======================
/*
// raft会与persist层交互，kvserver层也会，因为kvserver层开始的时候需要恢复kvdb的状态
//  关于快照raft层与persist的交互：保存kvserver传来的snapshot；生成leaderInstallSnapshot RPC的时候也需要读取snapshot；
//  因此snapshot的具体格式是由kvserver层来定的，raft只负责传递这个东西
//  snapShot里面包含kvserver需要维护的persist_lastRequestId 以及kvDB真正保存的数据persist_kvdb
*/
/**
 * @brief 读取并安装快照
 * @param snapshot 快照数据
 * @details 从快照数据中恢复KV数据库和请求ID记录
 */
void KvServer::ReadSnapShotToInstall(std::string snapshot)
{
    if (snapshot.empty())
    {
        return;
    }
    parseFromString(snapshot);
}

/**
 * @brief 检查是否需要发送快照命令
 * @param raftIndex 当前Raft日志索引
 * @param proportion 比例阈值
 * @details 当Raft状态大小超过阈值时，创建并发送快照
 */
void KvServer::IfNeedToSendSnapShotCommand(int raftIndex, int proportion)
{
    if (m_raftNode->GetRaftStateSize() > m_maxRaftState / 10.0)
    {
        // Send SnapShot Command
        auto snapshot = MakeSnapShot();
        m_raftNode->Snapshot(raftIndex, snapshot);
    }
}

/**
 * @brief 从Raft获取快照
 * @param message 包含快照的Raft消息
 * @details 安装Raft发送的快照到KV服务器
 */
void KvServer::GetSnapShotFromRaft(ApplyMsg message)
{
    std::lock_guard<std::mutex> lg(m_mtx);

    if (m_raftNode->CondInstallSnapshot(message.SnapshotTerm, message.SnapshotIndex, message.Snapshot))
    {
        ReadSnapShotToInstall(message.Snapshot);
        m_lastSnapShotRaftLogIndex = message.SnapshotIndex;
    }
}

/**
 * @brief 创建快照
 * @return 序列化后的快照数据
 * @details 将当前KV数据库和请求ID记录序列化生成快照
 */
std::string KvServer::MakeSnapShot()
{
    std::lock_guard<std::mutex> lg(m_mtx);
    std::string snapshotData = getSnapshotData();
    return snapshotData;
}

// ====================== RPC接口实现 ======================
/**
 * @brief Protobuf RPC接口实现：处理PutAppend请求
 * @param controller RPC控制器
 * @param request PutAppend请求参数
 * @param response PutAppend响应
 * @param done 完成回调
 * @details Protobuf RPC框架调用的PutAppend接口实现
 */
void KvServer::PutAppend(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::PutAppendArgs *request, ::raftKVRpcProtoc::PutAppendReply *response, ::google::protobuf::Closure *done)
{
    KvServer::PutAppend(request, response);
    done->Run();
}

/**
 * @brief Protobuf RPC接口实现：处理Get请求
 * @param controller RPC控制器
 * @param request Get请求参数
 * @param response Get响应
 * @param done 完成回调
 * @details Protobuf RPC框架调用的Get接口实现
 */
void KvServer::Get(google::protobuf::RpcController *controller, const ::raftKVRpcProtoc::GetArgs *request, ::raftKVRpcProtoc::GetReply *response, ::google::protobuf::Closure *done)
{
    KvServer::Get(request, response);
    done->Run();
}
