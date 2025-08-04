#include "mprpcconfig.h"

#include <iostream>
#include <string>

// 负责解析加载配置文件
void MprpcConfig::LoadConfigFile(const char *config_file)
{
    // 指定为只读模式，按照路径打开文件
    FILE *pf = fopen(config_file, "r");
    if (nullptr == pf)
    {
        std::cout << config_file << " is note exist!" << std::endl;
        exit(EXIT_FAILURE);
    }

    // 1.注释#   2.正确的配置项 =   3.去掉开头的多余的空格
    while (!feof(pf)) // feof(pf) ：检查文件是否到达末尾，文件未结束一直循环
    {
        char buf[512] = {0};

        /*
        fgets 如何“知道读取下一行”?
        - 1.文件指针的初始化 当通过 fopen 打开文件时，文件流对象 pf 会自动维护一个 文件指针 ，初始指向文件开头。
        - 2.fgets 的读取与指针更新。
            - 执行时， fgets 从 当前文件指针位置 开始读取。读取到换行符 \n 或达到最大长度（512-1）或文件结束时停止。
            - 读取完成后，文件指针 自动移动到读取结束位置的下一个字节。因此，下一次调用 fgets 时，会从新的指针位置继续读取
        */

        // 从文件中读取一行，最多读取 511 个字符（留出一个位置给终止符 \0 ）放入缓冲区
        fgets(buf, 512, pf);

        // 去掉字符串前面多余的空格
        std::string read_buf(buf); // 将C风格字符串转换为C++字符串，避免手动内存管理风险，提升代码质量与开发效率
        Trim(read_buf);

        // 判断#的注释
        if (read_buf[0] == '#' || read_buf.empty())
        {
            continue;
        }

        // 解析配置项
        int idx = read_buf.find('=');
        if (idx == -1)
        {
            // 配置项不合法
            continue;
        }

        std::string key;
        std::string value;
        key = read_buf.substr(0, idx);
        Trim(key);
        // rpcserverip = 127.0.0.1\n
        int endidx = read_buf.find('\n', idx); // 从索引 idx 开始查找换行符的位置
        value = read_buf.substr(idx + 1, endidx - idx - 1);
        Trim(value);
        m_configMap.insert({key, value});
    }

    fclose(pf);
}

// 查询配置项信息
std::string MprpcConfig::Load(const std::string &key)
{
    auto it = m_configMap.find(key);
    // 不存在返回空
    if (it == m_configMap.end())
    {
        return "";
    }
    return it->second;
}

// 去掉字符串前后的空格
void MprpcConfig::Trim(std::string &src_buf)
{
    // 查找第一个不是空格的位置
    int idx = src_buf.find_first_not_of(' ');
    if (idx != -1)
    {
        // 说明字符串前面有空格
        src_buf = src_buf.substr(idx, src_buf.size() - idx);
    }
    // 去掉字符串后面多余的空格
    idx = src_buf.find_last_not_of(' ');
    if (idx != -1)
    {
        // 说明字符串后面有空格
        src_buf = src_buf.substr(0, idx + 1);
    }
}