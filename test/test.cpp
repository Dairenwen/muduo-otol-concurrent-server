#include <sys/timerfd.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
#include "timewheel.hpp"
#include <regex>
using namespace std;

// 测试时间轮的timerfd功能
void testtimerfd()
{
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd < 0)
    {
        cerr << "timerfd create failed" << endl;
        return;
    }

    struct itimerspec itime;
    itime.it_interval.tv_sec = 1;
    itime.it_interval.tv_nsec = 0; // 设置第一次超时之后间隔
    itime.it_value.tv_nsec = 0;
    itime.it_value.tv_sec = 1;
    timerfd_settime(timerfd, 0, &itime, nullptr);

    while (1)
    {
        unsigned long long times;
        int ret = read(timerfd, &times, 8); // 读取累计未处理的到期次数，内核计数器每到期一次就+1，读取后清零。
        if (ret < 0)
        {
            cerr << "read failed" << endl;
            break;
        }
        sleep(1);
    }
    close(timerfd);
    return;
}
// 测试时间轮的功能
void testtimewheel()
{
    TimeWheel tw;
    tw.AddTask(1, 5, []()
               { cout << "Task 1 executed" << endl; });
    tw.AddTask(2, 10, []()
               { cout << "Task 2 executed" << endl; });
    tw.AddTask(3, 15, []()
               { cout << "Task 3 executed" << endl; });

    for (int i = 0; i < 20; ++i)
    {
        tw.RunTimerTask();
        sleep(1);
    }
}
// 测试正则表达式的功能
void testregex()
{
    // std::string str = "Hello, world!";
    // std::regex pattern("Hello, (\\w+)!");
    // std::smatch match;

    // if (std::regex_match(str, match, pattern))
    // {
    //     for (const auto &e : match)
    //     {
    //         std::cout << e << std::endl;
    //     }
    // }
    // else
    // {
    //     std::cout << "No match found." << std::endl;
    // }

    // 输入以下str：
    //  GET / HTTP/1.1
    //  Content-Type: application/json
    //  Authorization: Bearer
    //  User-Agent: PostmanRuntime/7.56.1
    //  Accept: */*
    //  Postman-Token: 0023f37c-c345-4087-a91b-9f5f9a96984e
    //  Host: 124.222.53.34:8080
    //  Accept-Encoding: gzip, deflate, br
    //  Connection: keep-alive
    std::string str = "GET / HTTP/1.1\r\nContent-Type: application/json\r\nAuthorization: Bearer\r\nUser-Agent: PostmanRuntime/7.56.1\r\nAccept: */*\r\nPostman-Token: 0023f37c-c345-4087-a91b-9f5f9a96984e\r\nHost: 124.222.53.34:8080\r\nAccept-Encoding: gzip, deflate, br\r\nConnection: keep-alive\r\n";
    std::regex pattern("(GET|HEAD|POST|PUT|DELETE) ([^?\\s]*)(?:\\?([^\\s]*))? (HTTP/1\\.[01])\\r\\n([\\s\\S]*)");
    std::smatch match;

    if (std::regex_match(str, match, pattern))
    {
        for (const auto &e : match)
        {
            std::cout << e << std::endl;
        }
    }
    else
    {
        std::cout << "No match found." << std::endl;
    }
}
int main()
{
    // testtimerfd();
    // testtimewheel();
    testregex();
    return 0;
}
