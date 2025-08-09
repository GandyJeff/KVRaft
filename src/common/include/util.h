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

// 安全的字符串格式化函数，自动计算缓冲区大小，避免缓冲区溢出
template <typename... Args>
std::string format(const char *format_str, Args... args)
{
    int size_s = std::snprintf(nullptr, 0, format_str, args...) + 1; // 计算所需缓冲区大小（含\0）
    if (size_s <= 0)
    {
        throw std::runtime_error("Error during formatting.");
    }
    auto size = static_cast<size_t>(size_s);
    std::vector<char> buf(size);                           // 分配缓冲区
    std::snprintf(buf.data(), size, format_str, args...);  // 格式化字符串
    return std::string(buf.data(), buf.data() + size - 1); // 去除\0并返回
}

// 实现生产者-消费者模型的线程安全队列，支持阻塞式和超时式出队
template <typename T>
class LockQueue
{
public:
    // 入队（线程安全）
    void Push(const T &data)
    {
        std::lock_guard<std::mutex> lock(m_mutex); // RAII加锁，作用域结束自动解锁
        m_queue.push(data);
        m_condvariable.notify_one(); // 唤醒一个等待的消费者线程
    }

    // 出队（阻塞式，直到有数据）
    T Pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex); // 可手动解锁的锁
        while (m_queue.empty())
        {
            m_condvariable.wait(lock); // 释放锁并等待通知
        }
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }

    // 超时出队（指定时间内无数据则返回false）
    bool timeOutPop(int timeout, T *ResData) // 添加一个超时时间参数，默认为 50 毫秒
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 获取当前时间点，并计算出超时时刻
        auto now = std::chrono::system_clock::now();
        auto timeout_time = now + std::chrono::milliseconds(timeout);
        while (m_queue.empty())
        {
            // 如果已经超时了，就返回一个空对象
            if (m_condvariable.wait_until(lock, timeout_time) == std::cv_status::timeout)
            {
                return false; // 超时
            }
            else
            {
                continue;
            }
        }
        *ResData = m_queue.front();
        m_queue.pop();
        return true;
    }

private:
    std::queue<T> m_queue;                  // 底层存储队列
    std::mutex m_mutex;                     // 互斥锁（保护队列访问）
    std::condition_variable m_condvariable; // 条件变量（线程间通知）
};

// 封装KV操作命令，支持序列化/反序列化，用于Raft集群间命令同步。这个Op是kv传递给raft的command
class Op
{
public:
    std::string Operation; // 操作类型："Get"/"Put"/"Append"
    std::string Key;       // 键
    std::string Value;     // 值
    std::string ClientId;  // 客户端ID（用于线性一致性）
    int RequestId;         // 请求序列号（去重）保证线性一致性

    // 序列化为字符串（用于Raft日志存储）
    std::string asString() const
    {
        std::stringstream ss;
        boost::archive::text_oarchive oa(ss); // Boost文本输出流
        oa << *this;                          // 序列化当前对象
        return ss.str();
    }

    // 从字符串反序列化
    bool parseFromString(std::string str)
    {
        std::stringstream iss(str);
        boost::archive::text_iarchive ia(iss); // Boost文本输入流
        ia >> *this;                           // 反序列化到当前对象
        return true;
    }

    // 重载输出运算符（调试用）
    friend std::ostream &operator<<(std::ostream &os, const Op &obj)
    {
        os << "[Op:Operation{" << obj.Operation << "}Key{" << obj.Key << "}]";
        return os;
    }

private:
    // Boost序列化权限声明
    friend class boost::serialization::access;
    // 序列化函数（Boost要求的格式）
    template <class Archive>
    void serialize(Archive &ar, const unsigned int version)
    {
        ar & Operation;
        &Key;
        &Value;
        &ClientId;
        &RequestId; // 序列化所有成员
    }
};

// 通过socket绑定操作检测端口是否被占用
bool isReleasePort(unsigned short usPort); // 检测端口是否可用
bool getReleasePort(short &port);          // 获取可用端口（从指定端口开始递增检测）

// 提供时间相关操作，为Raft协议的心跳和选举机制提供基础支持
std::chrono::_V2::system_clock::time_point now();         // 获取当前时间点
std::chrono::milliseconds getRandomizedElectionTimeout(); // 生成随机选举超时时间
void sleepNMilliseconds(int N);                           // 线程休眠N毫秒

void DPrintf(const char *format, ...);                                    // 调试日志输出（带时间戳）
void myAssert(bool condition, std::string message = "Assertion failed!"); // 断言函数，条件不满足时输出错误信息并终止程序，用于开发阶段错误检测

#define _CONCAT(a, b) a##b
#define _MAKE_DEFER_(line) DeferClass _CONCAT(defer_placeholder, line) = [&]()

#undef DEFER
#define DEFER _MAKE_DEFER_(__LINE__)