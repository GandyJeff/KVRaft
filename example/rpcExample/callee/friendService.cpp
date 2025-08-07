#include <mprpcchannel.h>
#include <iostream>
#include <string>
#include <vector>

#include "rpcExample/friend.pb.h"
#include "rpcprovider.h"

// 通过继承Protobuf生成的基类 FiendServiceRpc ，定义RPC服务接口实现
class FriendService : public fixbug::FriendServiceRpc
{
public:
  // 本地方法
  std::vector<std::string> GetFriendsList(uint32_t userid)
  {
    std::cout << "local do GetFriendsList service! userid:" << userid << std::endl;
    std::vector<std::string> vec;
    vec.push_back("gao yang");
    vec.push_back("liu hong");
    vec.push_back("wang shuo");
    return vec;
  }

  // 重写基类方法,本地方法模拟数据库查询，返回固定好友列表。
  void GetFriendsList(::google::protobuf::RpcController *controller, const ::fixbug::GetFriendsListRequest *request,
                      ::fixbug::GetFriendsListResponse *response, ::google::protobuf::Closure *done)
  {
    uint32_t userid = request->userid();
    std::vector<std::string> friendsList = GetFriendsList(userid); // 调用本地业务方法
    response->mutable_result()->set_errcode(0);                    // 设置错误码
    response->mutable_result()->set_errmsg("");                    // 设置错误消息
    for (std::string &name : friendsList)
    {
      std::string *p = response->add_friends(); // 添加好友
      *p = name;
    }
    done->Run(); // 触发回调，发送响应
  }
};

int main(int argc, char **argv)
{
  // std::string ip = "127.0.0.1";
  // short port = 7788;
  // provider是一个rpc网络服务对象。把UserService对象发布到rpc节点上
  RpcProvider provider;
  provider.NotifyService(new FriendService());

  // 启动一个rpc服务发布节点   Run以后，进程进入阻塞状态，等待远程的rpc调用请求
  provider.Run(1, 7788);

  return 0;
}
