#include <google/protobuf/service.h>
#include <string>

class MprpcController : public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset();                              // 重置控制器状态
    bool Failed() const;                       // 检查 RPC 调用是否失败
    std::string ErrorText() const;             // 获取错误信息
    void SetFailed(const std::string &reason); // 设置失败状态和原因

    // // 目前未实现具体的功能
    // void StartCancel();
    // bool IsCanceled() const;
    // void NotifyOnCancel(google::protobuf::Closure *callback);

private:
    bool m_failed;         // RPC方法执行过程中的状态
    std::string m_errText; // RPC方法执行过程中的错误信息
};