#pragma once

#include "config.h"

// 系统头文件：网络编程相关
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

// Boost库：序列化功能
#include <boost/archive/text_iarchive.hpp> // 文本反序列化
#include <boost/archive/text_oarchive.hpp> // 文本序列化
#include <boost/serialization/access.hpp>  // 序列化权限控制

// C++标准库：线程同步与基础功能
#include <condition_variable> // 条件变量（线程间通信）
#include <functional>         // 函数对象（std::function）
#include <iostream>           // 输入输出流
#include <mutex>              // 互斥锁
#include <queue>              // 队列容器
#include <random>             // 随机数生成
#include <sstream>            // 字符串流
#include <thread>             // 线程支持
#include "config.h"           // 项目配置参数

// 确保函数在作用域结束时自动执行（无论正常退出还是异常退出）
template <typename F>
class DeferClass
{
public:
    // 构造函数：接受右值引用（移动语义）
    DeferClass(F &&f) : m_func(std::forward<F>(f)) {}
    // 构造函数：接受常量左值引用（拷贝语义）
    DeferClass(const F &f) : m_func(f) {}
    // 析构函数：作用域结束时执行函数
    ~DeferClass() { m_func(); }

    // 禁用拷贝构造和赋值（防止函数被多次执行）
    DeferClass(const DeferClass &e) = delete;
    DeferClass &operator=(const DeferClass &e) = delete;

private:
    F m_func; // 存储待执行的函数对象
};
