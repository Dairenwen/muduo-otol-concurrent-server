#include <sys/timerfd.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
#include "timewheel.hpp"
#include <regex>
#include <assert.h>
#include "any.hpp"
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
    std::string str = "GET /search?name=tom&age=20 HTTP/1.1\r\nContent-Type: application/json\r\nAuthorization: Bearer\r\nUser-Agent: PostmanRuntime/7.56.1\r\nAccept: */*\r\nPostman-Token: 0023f37c-c345-4087-a91b-9f5f9a96984e\r\nHost: 124.222.53.34:8080\r\nAccept-Encoding: gzip, deflate, br\r\nConnection: keep-alive\r\n";
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

struct Student
{
    std::string name;
    int age;
};

void testAny()
{
    std::cout << "=============== Any Test Start ===============\n";

    // 1. 测试 int 构造和 get
    {
        Any a(10);

        int *p = a.get<int>();

        assert(p != nullptr);
        assert(*p == 10);
        assert(a.get<double>() == nullptr);

        std::cout << "[PASS] int constructor / get / wrong type\n";
    }

    // 2. 测试 string
    {
        Any a(std::string("hello"));

        std::string *p = a.get<std::string>();

        assert(p != nullptr);
        assert(*p == "hello");
        assert(a.get<int>() == nullptr);

        std::cout << "[PASS] string constructor / get\n";
    }

    // 3. 测试自定义类型
    {
        Student stu{"Tom", 20};

        Any a(stu);

        Student *p = a.get<Student>();

        assert(p != nullptr);
        assert(p->name == "Tom");
        assert(p->age == 20);

        std::cout << "[PASS] custom struct\n";
    }

    // 4. 测试拷贝构造
    {
        Any a(100);
        Any b(a);

        assert(a.get<int>() != nullptr);
        assert(b.get<int>() != nullptr);

        assert(*a.get<int>() == 100);
        assert(*b.get<int>() == 100);

        // 修改 b，验证是深拷贝
        *b.get<int>() = 200;

        assert(*a.get<int>() == 100);
        assert(*b.get<int>() == 200);

        std::cout << "[PASS] copy constructor / deep copy\n";
    }

    // 5. 测试 Any 对 Any 的赋值
    {
        Any a(10);
        Any b(std::string("old"));

        b = a;

        assert(b.get<int>() != nullptr);
        assert(*b.get<int>() == 10);
        assert(b.get<std::string>() == nullptr);

        // 验证赋值也是深拷贝
        *b.get<int>() = 20;

        assert(*a.get<int>() == 10);
        assert(*b.get<int>() == 20);

        std::cout << "[PASS] Any assignment / deep copy\n";
    }

    // 6. 测试任意类型赋值
    {
        Any a(10);

        assert(*a.get<int>() == 10);

        a = std::string("hello");

        assert(a.get<int>() == nullptr);
        assert(a.get<std::string>() != nullptr);
        assert(*a.get<std::string>() == "hello");

        a = 3.14;

        assert(a.get<std::string>() == nullptr);
        assert(a.get<double>() != nullptr);
        assert(*a.get<double>() == 3.14);

        std::cout << "[PASS] template operator=\n";
    }

    // 7. 测试自赋值
    {
        Any a(999);

        a = a;

        assert(a.get<int>() != nullptr);
        assert(*a.get<int>() == 999);

        std::cout << "[PASS] self assignment\n";
    }

    // 9. 测试不同 Any 互不影响
    {
        Any a(std::string("AAA"));
        Any b = a;

        *b.get<std::string>() = "BBB";

        assert(*a.get<std::string>() == "AAA");
        assert(*b.get<std::string>() == "BBB");

        std::cout << "[PASS] independent copied objects\n";
    }

    std::cout << "=============== All Any Tests Passed ===============\n";
}
int main()
{
    // testtimerfd();
    // testtimewheel();
    // testregex();
    testAny();
    return 0;
}
