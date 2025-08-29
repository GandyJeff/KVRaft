#include "raftServerRpcUtil.h"

// kvserver不同于raft节点之间，kvserver的rpc是用于clerk向kvserver调用，不会被调用，因此只用写caller功能，不用写callee功能
// 先开启服务器，再尝试连接其他的节点，中间给一个间隔时间，等待其他的rpc服务器节点启动
// 构造函数接收目标节点的IP地址和端口
raftServerRpcUtil::raftServerRpcUtil(std::string ip, short port)
{
    stub = new raftKVRpcProtoc::kvServerRpc_Stub(new MprpcChannel(ip, port, false));
}

raftServerRpcUtil::~raftServerRpcUtil() { delete stub; }

// 调用远程节点的Get方法，获取键值对
bool raftServerRpcUtil::Get(raftKVRpcProtoc::GetArgs *GetArgs, raftKVRpcProtoc::GetReply *reply)
{
    MprpcController controller;
    stub->Get(&controller, GetArgs, reply, nullptr);
    return !controller.Failed();
}

// 调用远程节点的PutAppend方法，设置或追加键值对
bool raftServerRpcUtil::PutAppend(raftKVRpcProtoc::PutAppendArgs *args, raftKVRpcProtoc::PutAppendReply *reply)
{
    MprpcController controller;
    stub->PutAppend(&controller, args, reply, nullptr);
    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    return !controller.Failed();
}
