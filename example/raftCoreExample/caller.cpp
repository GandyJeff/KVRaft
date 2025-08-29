#include <iostream>
#include "clerk.h"
#include "util.h"
int main()
{
    Clerk client;
    client.Init("test.conf");
    auto start = now();
    int count = 500;
    int tmp = count;
    while (tmp--)
    {
        client.Put("x", std::to_string(tmp));

        std::string get1 = client.Get("x");
        std::printf("get return :{%s}\r\n", get1.c_str());
        sleep(2);
    }
    // while (true)
    // {
    //     std::string get;
    //     client.Get(get); // （Get的第一个参数没有起到实际用途，真正Get的文件地址在KvServer::ExecuteGetOpOnKVDB()）
    //     std::printf("get return :{%s}\r\n", get.c_str());

    //     sleep(2);
    // }
    return 0;
}