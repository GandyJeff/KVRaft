#include "util.h"

#include <chrono>  // 时间相关操作（时间点、时间段）
#include <cstdarg> // 可变参数函数支持
#include <cstdio>  // 标准输入输出
#include <ctime>   // 时间函数

// 调试日志输出（带时间戳）
void DPrintf(const char *format, ...)
{
    if (Debug)
    {
        // 获取当前的日期，然后取日志信息，写入相应的日志文件当中 a+
        time_t now = time(nullptr);
        tm *nowtm = localtime(&now);
        va_list args;
        va_start(args, format);
        std::printf("[%d-%d-%d-%d-%d-%d] ", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday, nowtm->tm_hour,
                    nowtm->tm_min, nowtm->tm_sec);
        std::vprintf(format, args);
        std::printf("\n");
        va_end(args);
    }
}

// 断言函数，条件不满足时输出错误信息并终止程序，用于开发阶段错误检测
void myAssert(bool condition, std::string message) // 传入需要检查的条件和条件不满足时输出的错误信息
{
    if (!condition)
    {
        std::cerr << "Error: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// 检测端口是否可用，通过尝试绑定来检测端口是否被占用，是网络编程中常用的端口检测方法
bool isReleasePort(unsigned short usPort)
{
    int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(usPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 对应IP地址 127.0.0.1

    // 显式使用 ::bind 确保调用的是系统API中的套接字绑定函数，而不是其他同名函数
    int ret = ::bind(server, (struct sockaddr *)&addr, sizeof(addr));
    if (ret != 0)
    {
        close(server);
        return false;
    }
    close(server);
    return true;
}

// 获取可用端口（从指定端口开始递增检测）
bool getReleasePort(short &port)
{
    short num = 0;
    while (!isReleasePort(port) && num < 30)
    {
        ++port;
        ++num;
    }
    if (num >= 30)
    {
        port = -1;
        return false;
    }
    return true;
}

// 获取当前时间点
std::chrono::_V2::system_clock::time_point now()
{
    return std::chrono::high_resolution_clock::now();
}

// 生成随机的选举超时时间（Raft协议核心功能）
/*
可以把整个随机数生成过程比作“制作饼干”：
- std::random_device rd ：获取真随机种子（相当于“获取独特的原材料”）
- std::mt19937 rng(rd()) ：用种子初始化随机数引擎（相当于“启动搅拌机准备原材料”）
- std::uniform_int_distribution<int> dist(min, max) ：设置饼干模具的形状（规定随机数范围）
- dist(rng) ：用模具和搅拌好的原材料制作一个饼干（生成指定范围内的随机数）
- std::chrono::milliseconds(...) ：给饼干包装（转换为时间类型）
*/
std::chrono::milliseconds getRandomizedElectionTimeout()
{
    // 用 random_device 提供高质量种子，用 mt19937 高效生成大量随机数，两者必须结合使用
    std::random_device rd;                                                                         // 生成真随机种子，仅使用随机种子生成真随机数的速度通常较慢
    std::mt19937 rng(rd());                                                                        // 随机数引擎，以 2^19937-1 为周期，具有高质量的伪随机数生成特性。若仅使用引擎，没有随机种子，每次运行生成相同序列，无法满足随机性需求
    std::uniform_int_distribution<int> dist(minRandomizedElectionTime, maxRandomizedElectionTime); // 初始化范围，规定了后续生成的随机数必须在这个范围内均匀分布

    // dist将引擎生成的原始随机数“转换”为符合我们要求的范围和分布
    return std::chrono::milliseconds(dist(rng)); // milliseconds将生成的整数转换为毫秒时间段
}

// 线程休眠N毫秒
void sleepNMilliseconds(int N)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(N));
};