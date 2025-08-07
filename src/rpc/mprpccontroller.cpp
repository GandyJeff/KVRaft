#include "mprpccontroller.h"

MprpcController::MprpcController()
{
    m_failed = false;
    m_errText = "";
}

// 重置控制器状态
void MprpcController::Reset()
{
    m_failed = false;
    m_errText = "";
}

// 检查 RPC 调用是否失败
bool MprpcController::Failed() const
{
    return m_failed;
}

// 获取错误信息
std::string MprpcController::ErrorText() const
{
    return m_errText;
}

// 设置失败状态和原因
void MprpcController::SetFailed(const std::string &reason)
{
    m_failed = true;
    m_errText = reason;
}

// 目前未实现具体的功能，可以不实现，但是必须写出来
void MprpcController::StartCancel() {}
bool MprpcController::IsCanceled() const { return false; }
void MprpcController::NotifyOnCancel(google::protobuf::Closure *callback) {}