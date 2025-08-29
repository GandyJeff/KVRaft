#pragma once

#include <iostream>
#include "kvServerRPC.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "rpcprovider.h"

/// @brief 维护当前节点对其他某一个结点的所有rpc通信，包括接收其他节点的rpc和发送
// 对于一个节点来说，对于任意其他的节点都要维护一个rpc连接，
class raftServerRpcUtil
{
private:
    // 指向Protocol Buffers生成的RPC存根对象，用于调用远程RPC方法
    raftKVRpcProtoc::kvServerRpc_Stub *stub;

public:
    // 响应其他节点的方法
    // 调用远程节点的Get方法，获取键值对
    bool Get(raftKVRpcProtoc::GetArgs *GetArgs, raftKVRpcProtoc::GetReply *reply);
    // 调用远程节点的PutAppend方法，设置或追加键值对
    bool PutAppend(raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply);

    raftServerRpcUtil(std::string ip, short port);
    ~raftServerRpcUtil();
};