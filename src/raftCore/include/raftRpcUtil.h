#ifndef RAFTRPCUTIL_H
#define RAFTRPCUTIL_H

#include "raftRpc.pb.h"

/// @brief 管理与其他单个节点的所有RPC通信
// 对于一个raft节点来说，对于任意其他的节点都要维护一个rpc连接，即MprpcChannel。每个Raft节点都需要为其他每个节点维护一个RPC连接。
class RaftRpcUtil
{
private:
    raftRpcProctoc::raftRpc_Stub *stub_;

public:
    // 主动调用其他节点的三个方法,可以按照mit6824来调用，但是别的节点调用自己的好像就不行了，要继承protoc提供的service类才行
    // 发送日志追加请求（Raft算法核心RPC）
    bool AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response);
    // 发送快照安装请求（用于日志压缩）
    bool InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response);
    // 发送投票请求（领导者选举用）
    bool RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response);
    // 响应其他节点的方法
    /**
     * @param ip  远端ip
     * @param port  远端端口
     */
    // 接受远端节点的IP和端口
    RaftRpcUtil(std::string ip, short port);
    ~RaftRpcUtil();
};

#endif // RAFTRPCUTIL_H