#include "raftRpcUtil.h"

#include <mprpcchannel.h>
#include <mprpccontroller.h>

// 主动调用其他节点的三个方法,可以按照mit6824来调用，但是别的节点调用自己的好像就不行了，要继承protoc提供的service类才行
// 发送日志追加请求（Raft算法核心RPC）
bool RaftRpcUtil::AppendEntries(raftRpcProctoc::AppendEntriesArgs *args, raftRpcProctoc::AppendEntriesReply *response)
{
    MprpcController controller;
    stub_->AppendEntries(&controller, args, response, nullptr);
    return !controller.Failed();
}

// 发送快照安装请求（用于日志压缩）
bool RaftRpcUtil::InstallSnapshot(raftRpcProctoc::InstallSnapshotRequest *args, raftRpcProctoc::InstallSnapshotResponse *response)
{
    MprpcController controller;
    stub_->InstallSnapshot(&controller, args, response, nullptr);
    return !controller.Failed();
}

// 发送投票请求（领导者选举用）
bool RaftRpcUtil::RequestVote(raftRpcProctoc::RequestVoteArgs *args, raftRpcProctoc::RequestVoteReply *response)
{
    MprpcController controller;
    stub_->RequestVote(&controller, args, response, nullptr);
    return !controller.Failed();
}

// 响应其他节点的方法
// 接受远端节点的IP和端口
RaftRpcUtil::RaftRpcUtil(std::string ip, short port)
{
    // 创建 MprpcChannel 对象，用于建立与远端节点的通信通道，发送rpc设置
    stub_ = new raftRpcProctoc::raftRpc_Stub(new MprpcChannel(ip, port, true));
}

RaftRpcUtil::~RaftRpcUtil() { delete stub_; }