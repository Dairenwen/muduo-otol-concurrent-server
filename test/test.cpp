#include "eventloop.hpp"
#include "timewheel.hpp"
#include "server.hpp"
#include "buffer.hpp"
#include "any.hpp"
#include "log.hpp"
#include "socket.hpp"
#include "HttpServer.hpp"
#include "channel.hpp"
#include "poller.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "HttpContext.hpp"
#include "connection.hpp"
#include "acceptor.hpp"
#include "loopthread.hpp"
#include "LoopThreadPool.hpp"
#include "util.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <assert.h>
#include <memory>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <future>
#include <regex>
#include <stdexcept>
#include <unordered_map>
#include <cerrno>
#include <poll.h>
#include <sys/resource.h>
#include <fstream>
#include <functional>
#include <limits.h>
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
    EventLoop loop;
    TimeWheel tw(&loop);
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
void testserver()
{
    std::cout << "=============== Buffer Test Begin ===============\n";

    // 1. 测试初始化
    {
        Buffer buf;

        assert(buf.ReadAbleSize() == 0);
        assert(buf.HeadIdleSize() == 0);
        assert(buf.TailIdleSize() == BUFFER_SIZE);

        std::cout << "[PASS] 初始化测试\n";
    }

    // 2. 测试 WriteStringAndPush
    {
        Buffer buf;
        std::string str = "hello world";
        buf.WriteStringAndPush(str);
        assert(buf.ReadAbleSize() == str.size());
        std::string result = buf.ReadAsString(str.size());
        assert(result == str);
        // ReadAsString 不应该移动 reader
        assert(buf.ReadAbleSize() == str.size());
        std::cout << "[PASS] 写入测试\n";
    }

    // 3. 测试 ReadAsStringAndPop
    {
        Buffer buf;
        buf.WriteStringAndPush("abcdef");
        std::string result = buf.ReadAsStringAndPop(3);
        assert(result == "abc");
        assert(buf.ReadAbleSize() == 3);
        assert(buf.ReadAsString(3) == "def");
        std::cout << "[PASS] 读取并移动 reader 测试\n";
    }

    // 4. 测试连续写入
    {
        Buffer buf;

        buf.WriteStringAndPush("hello");
        buf.WriteStringAndPush(" ");
        buf.WriteStringAndPush("world");
        assert(buf.ReadAbleSize() == 11);
        assert(buf.ReadAsString(11) == "hello world");
        std::cout << "[PASS] 连续写入测试\n";
    }

    // 5. 测试部分读取
    {
        Buffer buf;
        buf.WriteStringAndPush("123456789");
        assert(buf.ReadAsStringAndPop(3) == "123");
        assert(buf.ReadAsStringAndPop(3) == "456");
        assert(buf.ReadAsStringAndPop(3) == "789");
        assert(buf.ReadAbleSize() == 0);
        std::cout << "[PASS] 连续读取测试\n";
    }

    // 6. 测试头部空间搬移
    {
        Buffer buf;
        // 几乎填满整个 Buffer
        std::string first(BUFFER_SIZE - 4, 'A');

        buf.WriteStringAndPush(first);

        // 消费前面一半
        uint64_t pop_len = BUFFER_SIZE / 2;

        std::string popped = buf.ReadAsStringAndPop(pop_len);

        assert(popped == first.substr(0, pop_len));

        // 尾部空间很小，但是头部存在大量空间
        // 这里应该触发 memmove
        std::string second(8, 'B');

        buf.WriteStringAndPush(second);

        std::string expected = first.substr(pop_len) + second;

        assert(buf.ReadAsString(buf.ReadAbleSize()) == expected);

        std::cout << "[PASS] 空间整理测试\n";
    }

    // 7. 测试自动扩容
    {
        Buffer buf;

        std::string big(BUFFER_SIZE * 2 + 100, 'X');

        buf.WriteStringAndPush(big);

        assert(buf.ReadAbleSize() == big.size());
        assert(buf.ReadAsString(big.size()) == big);

        std::cout << "[PASS] 自动扩容测试\n";
    }

    // 8. 测试 FindCRLF
    {
        Buffer buf;

        buf.WriteStringAndPush("GET / HTTP/1.1\r\n");
        char *pos = buf.FindCRLF();
        assert(pos != nullptr);
        assert(*pos == '\n');
        std::cout << "[PASS] FindCRLF 测试\n";
    }

    // 9. 测试 HTTP GetLine
    {
        Buffer buf;

        std::string request =
            "GET /index.html HTTP/1.1\r\n"
            "Host: www.baidu.com\r\n"
            "Content-Length: 10\r\n"
            "\r\n";

        buf.WriteStringAndPush(request);

        std::string line1 = buf.GetLine();
        std::string line2 = buf.GetLine();
        std::string line3 = buf.GetLine();
        std::string line4 = buf.GetLine();

        assert(line1 == "GET /index.html HTTP/1.1\r\n");
        assert(line2 == "Host: www.baidu.com\r\n");
        assert(line3 == "Content-Length: 10\r\n");
        assert(line4 == "\r\n");
        assert(buf.ReadAbleSize() == 0);

        std::cout << "[PASS] HTTP逐行读取测试\n";
    }

    // 10. 测试不完整 HTTP 行
    {
        Buffer buf;

        buf.WriteStringAndPush("GET /index.html HTTP/1.1");

        assert(buf.FindCRLF() == nullptr);
        assert(buf.GetLine() == "");

        // 不能因为没找到一整行就消费数据
        assert(buf.ReadAbleSize() == std::string("GET /index.html HTTP/1.1").size());
        std::cout << "[PASS] HTTP半包测试\n";
    }

    // 11. 测试网络半包追加
    {
        Buffer buf;

        buf.WriteStringAndPush("GET /index");

        // 当前没有完整行
        assert(buf.GetLine() == "");

        // 第二次 recv
        buf.WriteStringAndPush(".html HTTP/1.1\r\n");

        assert(buf.GetLine() == "GET /index.html HTTP/1.1\r\n");

        assert(buf.ReadAbleSize() == 0);

        std::cout << "[PASS] HTTP半包拼接测试\n";
    }

    // 12. 测试 clear
    {
        Buffer buf;

        buf.WriteStringAndPush("hello world");

        assert(buf.ReadAbleSize() != 0);

        buf.clear();

        assert(buf.ReadAbleSize() == 0);
        assert(buf.HeadIdleSize() == 0);

        std::cout << "[PASS] clear测试\n";
    }

    std::cout
        << "========== ALL BUFFER TEST PASSED ==========\n";
}
void testlog()
{
    TRACE_LOG("服务器初始化");

    DBG_LOG(
        "客户端 fd = %d",
        5);

    INF_LOG(
        "服务器启动成功，端口 = %d",
        8080);

    WARN_LOG(
        "Buffer 剩余空间不足: %d",
        128);

    ERR_LOG(
        "连接失败 errno = %d",
        errno);

    FATAL_LOG(
        "监听套接字创建失败");
}
void testsocket()
{
    std::cout << "=============== Socket Test Begin ===============\n";

    // 空对象没有 fd；未创建套接字时的操作应该失败或安全返回。
    {
        Socket socket;
        assert(socket.GetSocketFd() == -1);
        assert(socket.Bind(0, "127.0.0.1") == false);
        assert(socket.Listen() == false);
        assert(socket.Accept() == -1);
        assert(socket.Recv(nullptr, 1) == -1);
        assert(socket.Send(nullptr, 1) == -1);
        socket.SetNonBlocking(true);
        socket.SetReuseAddr(true);
        socket.Close();

        std::cout << "[PASS] 空套接字和错误状态测试\n";
    }

    // 分别测试 Create、Bind、Listen 的基础服务端流程。
    {
        Socket server;
        assert(server.Create());
        assert(server.GetSocketFd() >= 0);
        server.SetReuseAddr(true);
        assert(server.Bind(0, "127.0.0.1")); // 端口 0 让内核自动分配空闲端口
        assert(server.Listen(8));
        server.Close();
        assert(server.GetSocketFd() == -1);

        std::cout << "[PASS] 创建、绑定、监听、关闭测试\n";
    }

    // 使用 CreateServer 创建真正的回环服务器，并读取内核分配的临时端口。
    Socket server;
    assert(server.CreateServer("127.0.0.1", 0)); // 端口 0 让内核自动分配空闲端口

    sockaddr_in server_addr;
    socklen_t server_addr_len = sizeof(server_addr);
    assert(getsockname(server.GetSocketFd(), (sockaddr *)(&server_addr), &server_addr_len) == 0);
    uint16_t port = ntohs(server_addr.sin_port); // 获取内核分配的临时端口
    assert(port != 0);

    // 客户端在线程中连接，主线程执行 accept，模拟真实的服务端/客户端协作。
    std::thread client_thread([port]()
                              {
                                    Socket client;
                                    assert(client.CreateClient("127.0.0.1", port));
                                    assert(client.GetSocketFd() >= 0);

                                    const char message[] = "socket client connected";
                                    assert(client.Send(message, sizeof(message)) == (ssize_t)(sizeof(message))); });

    int client_fd = server.Accept();
    // accept 成功应该返回 >= 0 的连接 fd
    assert(client_fd >= 0);
    client_thread.join(); // 等待客户端线程结束，确保客户端发送数据完成。
    server.Close();

    std::cout << "[PASS] 服务端创建、客户端连接、accept 测试\n";

    // socketpair 创建一对互相连接的本地 socket，适合测试 Send/Recv。不经过 IP 层，但能直接验证 Socket 对 fd 的收发封装。
    {
        int pair_fds[2] = {-1, -1};
        assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair_fds) == 0);

        Socket left(pair_fds[0]);
        Socket right(pair_fds[1]);

        const char request[] = "hello from left";
        char receive_buffer[64] = {};
        assert(left.Send(request, sizeof(request)) == (ssize_t)(sizeof(request)));
        assert(right.Recv(receive_buffer, sizeof(receive_buffer)) == (ssize_t)(sizeof(request)));
        assert(std::strcmp(receive_buffer, request) == 0);

        const char response[] = "hello from right";
        std::memset(receive_buffer, 0, sizeof(receive_buffer));
        assert(right.Send(response, sizeof(response)) == (ssize_t)(sizeof(response)));
        assert(left.Recv(receive_buffer, sizeof(receive_buffer)) == (ssize_t)(sizeof(response)));
        assert(std::strcmp(receive_buffer, response) == 0);

        std::cout << "[PASS] Send/Recv 双向通信测试\n";

        // 非阻塞 socket 没有数据可读时，当前实现将 EAGAIN 转换为 0。
        left.SetNonBlocking(true);
        int flags = fcntl(left.GetSocketFd(), F_GETFL, 0);
        assert(flags >= 0 && (flags & O_NONBLOCK) != 0);
        assert(left.Recv(receive_buffer, sizeof(receive_buffer)) == 0); // 没有数据可读，返回 0

        left.SetNonBlocking(false);
        flags = fcntl(left.GetSocketFd(), F_GETFL, 0);
        assert(flags >= 0 && (flags & O_NONBLOCK) == 0);

        std::cout << "[PASS] 非阻塞模式切换测试\n";
    }

    // 非法 IP 应该在连接前失败，并释放已经创建的 fd。
    {
        Socket client;
        assert(!client.CreateClient("invalid-ip", 1));
        assert(client.GetSocketFd() == -1);

        Socket server_with_invalid_ip;
        assert(!server_with_invalid_ip.CreateServer("invalid-ip", 0));
        assert(server_with_invalid_ip.GetSocketFd() == -1);

        std::cout << "[PASS] 非法地址错误处理测试\n";
    }

    std::cout << "=============== All Socket Tests Passed ===============\n";
}

void testchannel_poller()
{
    std::cout << "=============== Channel/Poller Test Begin ===============\n";

    // socketpair 不依赖端口和网络时序；pair_fds[1] 写入的数据会使 pair_fds[0] 产生 EPOLLIN，
    // 因此可稳定覆盖 Channel -> Poller -> active channel -> HandleEvent 的完整路径。
    int pair_fds[2] = {-1, -1};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair_fds) == 0);

    Poller poller;
    // Channel::Update/Remove 内部调用 shared_from_this()，因此必须由 std::shared_ptr 管理，不能继续创建栈上的 Channel 对象。
    auto channel = std::make_shared<Channel>(&poller, pair_fds[0]);
    int read_callback_count = 0;
    int write_callback_count = 0;
    int event_callback_count = 0;
    std::string received;

    channel->SetReadCallbck([&]()
                            {
                                char buffer[64] = {};
                                const ssize_t bytes = read(pair_fds[0], buffer, sizeof(buffer));
                                assert(bytes > 0);
                                received.assign(buffer, static_cast<size_t>(bytes));
                                ++read_callback_count; });
    channel->SetWriteCallbck([&]()
                             { ++write_callback_count; });
    channel->SetEventCallbck([&]()
                             { ++event_callback_count; });

    // 注册读事件后，另一端写入，Poller 应仅把这个 Channel 作为活跃对象返回。
    channel->EnableRead();
    assert(channel->ReadAble());
    assert(!channel->WriteAble());
    channel->Update();

    const std::string request = "channel-poller-read";
    assert(write(pair_fds[1], request.data(), request.size()) == static_cast<ssize_t>(request.size()));

    std::vector<std::shared_ptr<Channel>> active_channels;
    poller.Poll(active_channels);
    assert(active_channels.size() == 1);
    assert(active_channels.front() == channel);
    active_channels.front()->HandleEvent();
    assert(read_callback_count == 1);
    assert(event_callback_count == 1);
    assert(received == request);
    std::cout << "[PASS] EPOLLIN 注册、轮询和读回调测试\n";

    // EPOLLOUT 在本地 stream socket 可写时应立即就绪；开启后重新 MOD 注册，验证写回调和统一事件回调都能沿相同的分发链被调用。
    channel->EnableWrite();
    assert(channel->WriteAble());
    channel->Update();
    poller.Poll(active_channels);
    assert(active_channels.size() == 1);
    active_channels.front()->HandleEvent();
    assert(write_callback_count == 1);
    assert(event_callback_count == 2);
    std::cout << "[PASS] EPOLLOUT 修改注册和写回调测试\n";

    // 先从 epoll 删除，再关闭描述符，避免 Poller 的 fd -> Channel 映射留下悬空项。
    channel->DisableAll();
    assert(!channel->ReadAble());
    assert(!channel->WriteAble());
    channel->Remove();

    // Remove 后 Poller 不再持有 Channel；清空本轮活跃列表和外部引用后，才允许对象析构，从而验证事件处理期间的保活语义。
    active_channels.clear();
    channel.reset(); // 释放 shared_ptr，触发 Channel 析构
    close(pair_fds[0]);
    close(pair_fds[1]);

    std::cout << "[PASS] 事件取消与移除测试\n";
    std::cout << "=============== All Channel/Poller Tests Passed ===============\n";
}

// 联合测试 EventLoop 和 TimeWheel:
// timerfd 每秒就绪 -> Poller 返回事件 -> EventLoop 处理事件 -> TimeWheel 前进一格 -> 到期任务的析构函数执行回调。
void testeventloop_timewheel()
{
    // 管道用于把子进程的测试结果传回父进程：'1' 表示任务顺序正确，'0' 表示失败。
    int result_pipe[2];
    assert(pipe(result_pipe) == 0);

    pid_t child_pid = fork();
    assert(child_pid >= 0);

    if (child_pid == 0)
    {
        // 子进程只保留写端
        close(result_pipe[0]);

        // TimeWheel 在构造时会把自己的 timerfd 注册到 loop 的 Poller 中。
        EventLoop loop;
        std::vector<int> fired_tasks;

        // 记录每个任务实际触发的顺序，后面用它验证时间轮没有漏任务或乱序。
        auto record_task = [&fired_tasks](int id)
        {
            fired_tasks.push_back(id);
            std::cout << "[EventLoop/TimeWheel] task " << id << " fired" << std::endl;
        };

        // timeout 的单位是秒：任务 1、2、3 应分别在时间轮前进 1、2、3 格时执行。
        // AddTask 发生在当前线程，任务会被 EventLoop 的任务队列接收并在事件循环中落盘。
        loop.AddTask(1, 1, [&]()
                     { record_task(1); });
        loop.AddTask(2, 2, [&]()
                     { record_task(2); });
        loop.AddTask(3, 3, [&]()
                     {
                         record_task(3);

                         // 第 3 个任务执行说明至少已经推进到第 3 秒；此时检查全部顺序。
                         const bool passed = (fired_tasks == std::vector<int>{1, 2, 3});
                         const char result = passed ? '1' : '0';

                         // 先把结果写给父进程，再退出子进程，避免 EventLoop 永久运行。
                         ssize_t written;
                         do
                         {
                             written = write(result_pipe[1], &result, sizeof(result));
                         } while (written == -1 && errno == EINTR); // 被信号中断时重试。
                         close(result_pipe[1]);

                         // 单字节结果必须完整写入；写入失败也作为测试失败。
                         const bool write_ok = written == static_cast<ssize_t>(sizeof(result));
                         _exit(passed && write_ok ? 0 : 1); });

        // 这里会一直 Poll；timerfd 事件到来后，TimeWheel 才会自动推进并触发任务。
        loop.StartEventLoop();
        _exit(2);
    }

    // 父进程只保留读端
    close(result_pipe[1]);
    constexpr int timeout_seconds = 6;
    int status = 0;
    bool finished = false;

    // 每 100ms 非阻塞检查一次，给 3 秒任务留出余量，同时避免测试永久阻塞。
    for (int elapsed = 0; elapsed < timeout_seconds * 10; ++elapsed)
    {
        pid_t wait_result = waitpid(child_pid, &status, WNOHANG);
        if (wait_result == child_pid)
        {
            finished = true;
            break;
        }
        assert(wait_result == 0);
        usleep(100000);
    }

    if (!finished)
    {
        // 超时通常意味着 timerfd 没有接入 EventLoop，或时间轮没有正常推进。
        kill(child_pid, SIGKILL);
        waitpid(child_pid, &status, 0);
        close(result_pipe[0]);
        assert(false && "EventLoop/TimeWheel test timed out");
    }

    // 子进程退出后读取它写入的测试结果，并检查退出码和管道结果都成功。
    char result = '0';
    assert(read(result_pipe[0], &result, sizeof(result)) == sizeof(result));
    close(result_pipe[0]);
    assert(result == '1');
    std::cout << "=============== EventLoop/TimeWheel Test Passed ===============\n";
}

void testconnection()
{
    // 1. 创建两个已经互相连通的 socket，fds[0] 交给 Connection，fds[1] 用来模拟客户端。
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        throw std::runtime_error("socketpair failed");

    std::cout.flush();
    pid_t child = fork();
    if (child < 0)
        throw std::runtime_error("fork failed");

    if (child == 0)
    {
        // 子进程模拟服务端，只保留 fds[0]。
        close(fds[1]);
        alarm(5); // 5 秒还没有结束就终止子进程，父进程会把这种退出判断为失败。

        // loop 在子进程主线程创建，也在这个线程运行。
        // Connection 创建后处于 CONNECTING，此时还没有监听连接的可读事件。
        EventLoop loop;
        auto conn = std::make_shared<Connection>(&loop, fds[0], 1);
        std::string request; // 累积收到的数据：一次 recv 不保证拿到完整的 hello。

        conn->SetMessageCallback([&request](const ConnPtr &current, Buffer &buffer)
                                 {
            // 取出目前收到的数据并移动读指针，避免下一次重复处理同一段数据。
            request += buffer.ReadAsStringAndPop(buffer.ReadAbleSize());
            if (request.size() > 5) _exit(2);
            if (request == "hello")
            {
                // 这里已经在 loop 线程：Send 会立即执行 SendInLoop。
                // world 先放进输出缓冲区并开启写事件，不是在这里直接发到客户端。
                current->Send("world");
                // Shutdown 将状态改为 DISCONNECTING。
                // 输出缓冲区还有 world，所以等待 HandleWrite 发完后再 Release。
                current->Shutdown();
            } });
        // Release 已关闭 fd 并设为 DISCONNECTED 后，才调用这个关闭回调。
        // EventLoop 没有停止接口，因此用 _exit 结束测试子进程；0 表示成功。
        conn->SetCloseCallback([](const ConnPtr &current)
                               { _exit(current->GetConnStatu() == DISCONNECTED ? 0 : 3); });

        // worker 是另一个线程，Established 会把 EstablishedInLoop 放进 loop 的队列。
        // worker.join() 只等 worker 完成投递，不代表 EstablishedInLoop 已经执行。
        std::thread worker([conn]
                           { conn->Established(); });
        worker.join();
        // 开始事件循环：处理 eventfd 的唤醒，再执行队列中的 EstablishedInLoop。
        // 它将状态改为 CONNECTED 并监听读事件，之后才能走 HandleRead/HandleWrite。
        loop.StartEventLoop();
        _exit(4); // 当前事件循环不应返回；如果返回，测试判为失败。
    }

    // 父进程模拟客户端，只保留 fds[1]
    close(fds[0]);
    // recv 最多等待 7 秒，防止测试一直阻塞。
    timeval timeout{};
    timeout.tv_sec = 7;
    if (setsockopt(fds[1], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        throw std::runtime_error("receive timeout setup failed");

    // 发送请求。即使子进程还没开启读监听，数据也会先保存在内核缓冲区中。
    if (send(fds[1], "hello", 5, 0) != 5)
        throw std::runtime_error("sending hello failed");

    char response[5];
    // 等服务端回复；MSG_WAITALL 尽量收满 5 字节，遇到关闭/超时也可能提前返回。
    ssize_t received = recv(fds[1], response, sizeof(response), MSG_WAITALL);
    // response 没有字符串结束符，所以用 std::string(response, 5) 指定长度。
    bool reply_ok = received == 5 && std::string(response, 5) == "world";
    // 再读一次应返回 0，验证服务端发完 world 后确实关闭了连接。
    ssize_t after_response = recv(fds[1], response, sizeof(response), 0);
    close(fds[1]);

    int status = 0;
    // 等子进程结束，同时检查回复、EOF 和退出码。
    // 若子进程被 alarm 终止，即使客户端收到 EOF，也不会被误认为测试成功。
    if (waitpid(child, &status, 0) != child ||
        !reply_ok || after_response != 0 ||
        !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error("Connection test failed");

    std::cout << "[PASS] Connection: hello -> world -> close\n";
}

void testacceptor()
{
    // 测试分工：父进程是 TCP 客户端；子进程是一个有主/子 Reactor 的小服务端。
    // 子进程主线程：EventLoop -> Poller -> 监听 Channel -> Acceptor -> accept。
    // 子进程工作线程：LoopThread -> EventLoop -> Connection -> Buffer -> 回显/关闭。
    // 两个 Loop 之间用 QueueInLoop + eventfd 交接任务，不直接跨线程操作 Poller。
    auto require = [](bool ok, const char *message)
    {
        // 不使用 assert 做有副作用的操作：Release 编译下 assert 会被移除。
        if (!ok)
            throw std::runtime_error(message);
    };

    // 只需要一个管道传递端口；最终服务端是否通过，由子进程退出码表示。
    int port_pipe[2];
    require(pipe(port_pipe) == 0, "Acceptor test pipe failed");

    std::cout.flush();
    pid_t child = fork();
    require(child >= 0, "Acceptor test fork failed");

    if (child == 0)
    {
        close(port_pipe[0]);
        alarm(12); // 仅作为失败兜底；正常路径会 StopEventLoop、join，然后退出。
        try
        {
            EventLoop main_loop;   // 在主线程构造，也在主线程启动。
            unsigned accepted = 0; // 仅主线程修改。

            // 以下状态仅工作线程修改；主线程在 worker 析构/join 后才读取。
            // 先声明状态，再声明 worker，保证回调引用的变量比工作线程活得更久。
            std::unordered_map<uint64_t, ConnPtr> connections;
            unsigned connected = 0, closed = 0, frames = 0, events = 0;
            size_t echoed_bytes = 0;
            bool idle_timer_seen = false;
            struct RequestState
            {
                std::string pending; // 保存尚未遇到换行符的半条消息。
                unsigned frames = 0;
            };

            {
                LoopThread worker;
                EventLoop *worker_loop = worker.GetLoop(); // worker 构造已等待就绪，这里获取指针。
                require(!worker_loop->IsInLoopThread(), "Worker Loop belongs to main thread");
                Acceptor acceptor(&main_loop, "127.0.0.1", 0);

                acceptor.SetAcceptCallbak([&](int client_fd)
                                          {
                    require(main_loop.IsInLoopThread(), "accept ran outside main Loop");
                    const uint64_t id = ++accepted;
                    require(id <= 3, "Unexpected extra client");
                    INF_LOG("[联合测试] 主 Loop 接收连接 id=%llu，投递到工作 Loop",
                            static_cast<unsigned long long>(id));

                    // 值捕获 fd/id，避免 accept 回调结束后局部变量失效。
                    // QueueInLoop 负责写 eventfd；工作 Loop 被唤醒后才执行这个 lambda。
                    worker_loop->QueueInLoop([&, client_fd, id, worker_loop]()
                    {
                        require(worker_loop->IsInLoopThread(), "Connection setup ran in wrong thread");
                        auto conn = std::make_shared<Connection>(worker_loop, client_fd, id);
                        // map 持有连接：Channel 的回调使用 weak_ptr，本身不会保活 Connection。
                        connections.emplace(id, conn);
                        conn->GetContext() = RequestState{}; // 同时验证 Any 保存协议状态。

                        conn->SetConnectedCallback([&, worker_loop](const ConnPtr &current)
                        {
                            require(worker_loop->IsInLoopThread(), "connected callback in wrong thread");
                            require(current->GetConnStatu() == CONNECTED, "Connection not established");
                            ++connected;
                            // 第 3 条连接故意不发送业务数据，2 秒无活动后由时间轮关闭。
                            // 前两条也有定时器，正常关闭时应取消；Any 事件会刷新存活时间。
                            current->SetInactiveClose(true, current->GetConnId() == 3 ? 2 : 8);
                            if (current->GetConnId() == 3)
                                idle_timer_seen = worker_loop->HasTimer(3);
                            current->Send("ready\n"); // 客户端收到它，说明连接已建立。
                            INF_LOG("[联合测试] 工作 Loop 建立连接 id=%llu",
                                    static_cast<unsigned long long>(current->GetConnId()));
                        });

                        conn->SetMessageCallback([&, worker_loop](const ConnPtr &current, Buffer &buffer)
                        {
                            require(worker_loop->IsInLoopThread(), "message callback in wrong thread");
                            require(current->GetConnId() == 1, "Unexpected message from idle/closing client");
                            auto state = current->GetContext().get<RequestState>();
                            require(state != nullptr, "Any protocol context lost");
                            // TCP 只有字节流：一次 recv 可能是半条，也可能是多条消息。
                            // 先消费 Buffer 中的字节，再以 '\n' 划分完整消息，不能假设一次 recv == 一条。
                            state->pending += buffer.ReadAsStringAndPop(buffer.ReadAbleSize());
                            size_t end;
                            while ((end = state->pending.find('\n')) != std::string::npos)
                            {
                                const std::string frame = state->pending.substr(0, end + 1);
                                state->pending.erase(0, end + 1);
                                ++state->frames;
                                ++frames;
                                echoed_bytes += frame.size();
                                current->Send(frame); // 写入输出 Buffer，等待 EPOLLOUT 真正发送。
                                INF_LOG("[联合测试] 回显第 %u 条消息，字节数=%zu", state->frames, frame.size());
                                if (state->frames == 2)
                                {
                                    // 此时输出 Buffer 还有数据：Shutdown 应等回显发完后再关闭 fd。
                                    current->Shutdown();
                                }
                                require(state->frames <= 2, "Unexpected extra message");
                            }
                        });

                        conn->SetAnyCallback([&, worker_loop](const ConnPtr &)
                        {
                            require(worker_loop->IsInLoopThread(), "event callback in wrong thread");
                            ++events; // Channel 的任意事件回调走到了 Connection。
                        });

                        conn->SetCloseCallback([&, worker_loop](const ConnPtr &current)
                        {
                            require(worker_loop->IsInLoopThread(), "close callback in wrong thread");
                            require(current->GetConnStatu() == DISCONNECTED, "Connection not released");
                            require(current->GetSocketfd() == -1, "Connection fd was not closed");
                            require(connections.erase(current->GetConnId()) == 1, "Duplicate close callback");
                            ++closed;
                            INF_LOG("[联合测试] 连接 id=%llu 已关闭，累计关闭=%u",
                                    static_cast<unsigned long long>(current->GetConnId()), closed);
                            // 所有连接均已移除；跨线程投递停止请求，唤醒主 Loop 的 epoll_wait。
                            if (closed == 3) main_loop.StopEventLoop();
                        });
                        conn->Established(); // 已在所属线程，立即注册连接 Channel 的读监听。
                    }); });

                // port=0 让内核选择空闲端口；在安装好回调以后再告诉客户端。
                sockaddr_in addr{};
                socklen_t addr_len = sizeof(addr);
                require(getsockname(acceptor.GetListenSocketFd(), reinterpret_cast<sockaddr *>(&addr), &addr_len) == 0,
                        "getsockname failed");
                const uint16_t port = ntohs(addr.sin_port);
                require(port != 0 && write(port_pipe[1], &port, sizeof(port)) == sizeof(port), "Port handoff failed");
                close(port_pipe[1]);
                main_loop.StartEventLoop(); // 主线程在这里等待 accept，直到第 3 个关闭回调请求停止。
            } // 先销毁 Acceptor；再析构 worker，停止工作 Loop 并 join。

            // join 建立了同步关系：现在读取工作线程的计数和 map 无需再加锁。
            require(accepted == 3 && connected == 3 && closed == 3, "Connection lifecycle counts mismatch");
            require(connections.empty(), "Connections were not released");
            require(frames == 2 && echoed_bytes == 6 + 128 * 1024 + 1, "Echo byte/frame counts mismatch");
            require(events > 0 && idle_timer_seen, "Channel callback or TimeWheel was not exercised");
            INF_LOG("[联合测试] 服务端验证通过，两个 Loop 和工作线程已正常退出");
        }
        catch (const std::exception &error)
        {
            ERR_LOG("[联合测试] 服务端失败：%s", error.what());
            _exit(1);
        }
        // 到这里对象已按作用域正常析构；_exit 只结束测试子进程。
        alarm(0);
        _exit(0);
    }

    close(port_pipe[1]);
    std::exception_ptr client_error;
    try
    {
        uint16_t port = 0;
        require(read(port_pipe[0], &port, sizeof(port)) == sizeof(port) && port != 0, "No listening port received");

        auto connect_client = [&](Socket &client)
        {
            require(client.CreateClient("127.0.0.1", port), "Client connect failed");
            timeval timeout{};
            timeout.tv_sec = 6;
            // 收发均限时，服务端出错时不会永久卡在 send/recv。
            require(setsockopt(client.GetSocketFd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
                    "Receive timeout setup failed");
            require(setsockopt(client.GetSocketFd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
                    "Send timeout setup failed");
        };
        auto send_all = [&](Socket &client, const std::string &message)
        {
            // send 可能只发出部分字节，必须循环；MSG_NOSIGNAL 防止断连时 SIGPIPE 终止测试。
            size_t sent = 0;
            while (sent < message.size())
            {
                const ssize_t n = client.Send(message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
                if (n < 0 && errno == EINTR)
                    continue;
                require(n > 0, "Client send failed or timed out");
                sent += static_cast<size_t>(n);
            }
        };
        auto receive_exact = [&](Socket &client, const std::string &expected)
        {
            // recv 也可能只收到一部分；累计到预期长度后，再逐字节比较。
            std::string actual(expected.size(), '\0');
            size_t received = 0;
            while (received < actual.size())
            {
                const ssize_t n = client.Recv(&actual[received], actual.size() - received);
                if (n < 0 && errno == EINTR)
                    continue;
                require(n > 0, "Client recv failed, timed out or reached early EOF");
                received += static_cast<size_t>(n);
            }
            require(actual == expected, "Echo content mismatch");
        };
        auto expect_eof = [&](Socket &client)
        {
            char byte;
            ssize_t n;
            do
            {
                n = client.Recv(&byte, 1);
            } while (n < 0 && errno == EINTR);
            // 只有 0 表示正常关闭；-1 超时/错误不能当成关闭成功。
            require(n == 0, "Expected EOF after server close");
        };

        {
            Socket client;
            connect_client(client);
            receive_exact(client, "ready\n");
            send_all(client, "hel");
            send_all(client, "lo\n"); // 分两次发送；TCP 仍可能合并，因此服务端统一按换行解析。
            receive_exact(client, "hello\n");
            const std::string large_message = std::string(128 * 1024, 'x') + '\n';
            send_all(client, large_message); // 大于 Connection 一次 recv 的 64 KiB 缓冲区。
            receive_exact(client, large_message);
            expect_eof(client); // 验证 Shutdown 确实等 128 KiB 回显全部发完才关闭。
            std::cout << "[PASS] 分段消息、大消息回显、发送完毕后关闭\n";
        }
        {
            Socket client;
            connect_client(client);
            receive_exact(client, "ready\n");
            client.Close(); // 客户端主动关闭；服务端应读到 EOF 并只执行一次关闭回调。
            std::cout << "[PASS] 客户端主动断开（服务端退出前会检查关闭计数）\n";
        }
        {
            Socket client;
            connect_client(client);
            receive_exact(client, "ready\n");
            // 不发业务消息、不主动断开；必须由工作 Loop 的 timerfd/TimeWheel 关闭。
            expect_eof(client);
            std::cout << "[PASS] TimeWheel 关闭空闲连接\n";
        }
    }
    catch (...)
    {
        client_error = std::current_exception();
        kill(child, SIGKILL); // 失败时结束本测试的子进程，随后 waitpid，避免遗留进程。
    }
    close(port_pipe[0]);
    int status = 0;
    const bool child_ok = waitpid(child, &status, 0) == child &&
                          WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (client_error)
        std::rethrow_exception(client_error);
    require(child_ok, "Integrated server checks failed or timed out");
    std::cout << "[PASS] Acceptor + LoopThread + EventLoop + Poller + Channel + Connection + Buffer + Any + TimeWheel\n";
}

void testloopthreadpoll()
{
    std::cout << "=============== LoopThreadPool Test Begin ===============\n";

    // 主 Loop 只用于验证“没有工作线程时返回主 Loop”的配置语义。
    EventLoop main_loop;
    LoopThreadPool pool(&main_loop);

    pool.SetThreadCount(2);
    pool.Create();

    // RR 分配：两次获取应落在两个不同的子 Loop，第三次回到第一个。
    EventLoop *first = pool.NextLoop();
    EventLoop *second = pool.NextLoop();
    EventLoop *third = pool.NextLoop();
    assert(first != nullptr && second != nullptr && third == first);
    assert(first != second);
    assert(!first->IsInLoopThread());
    assert(!second->IsInLoopThread());

    // QueueInLoop 会唤醒对应子 Loop，并在子线程中执行任务。
    std::promise<bool> first_task;
    std::promise<bool> second_task;
    auto first_result = first_task.get_future();
    auto second_result = second_task.get_future();
    first->QueueInLoop([first, &first_task]
                       { first_task.set_value(first->IsInLoopThread()); });
    second->QueueInLoop([second, &second_task]
                        { second_task.set_value(second->IsInLoopThread()); });
    assert(first_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(second_result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    assert(first_result.get() && second_result.get());

    std::cout << "[PASS] LoopThreadPool: 创建、RR 分配、任务投递和线程回收\n";
}

void testtcpserver()
{
    // 父进程运行 EchoServer；子进程作为 TCP 客户端。ready 管道传递监听端口，done 管道传递测试结果，业务数据走 TCP。
    auto require = [](bool ok, const char *message)
    {
        if (!ok)
            throw std::runtime_error(message);
    };
    int ready[2], done[2];
    require(pipe(ready) == 0, "ready pipe failed");
    if (pipe(done) != 0)
    {
        close(ready[0]);
        close(ready[1]);
        throw std::runtime_error("done pipe failed");
    }
    std::cout.flush();          // fork 前刷新，避免父子进程重复输出已有内容。
    const pid_t child = fork(); // 必须在 EchoServer 创建工作线程之前 fork。
    if (child < 0)
    {
        close(ready[0]);
        close(ready[1]);
        close(done[0]);
        close(done[1]);
        throw std::runtime_error("fork failed");
    }
    if (child == 0)
    {
        // 子进程只读端口、写结果；关闭不用的管道端，保证 EOF 能正常传递。
        close(ready[1]);
        close(done[0]);
        alarm(35); // 包括等待就绪和客户端收发，整个子进程有硬超时。
        unsigned char result = 1;
        try
        {
            uint16_t port = 0;
            ssize_t n;
            // 等父进程创建监听 socket 后，再连接其实际端口。
            do
            {
                n = read(ready[0], &port, sizeof(port));
            } while (n < 0 && errno == EINTR);
            require(n == sizeof(port) && port != 0, "server did not publish port");
            close(ready[0]);
            auto connect_client = [&](Socket &client, int seconds = 5)
            {
                require(client.CreateClient("127.0.0.1", port), "connect failed");
                timeval timeout{};
                // 收发限时，服务端异常时客户端不会一直阻塞。
                timeout.tv_sec = seconds;
                require(setsockopt(client.GetSocketFd(), SOL_SOCKET, SO_RCVTIMEO,
                                   &timeout, sizeof(timeout)) == 0,
                        "recv timeout failed");
                require(setsockopt(client.GetSocketFd(), SOL_SOCKET, SO_SNDTIMEO,
                                   &timeout, sizeof(timeout)) == 0,
                        "send timeout failed");
            };
            auto echo = [&](Socket &client, const std::string &expected)
            {
                // TCP 可能部分收发：发送和接收都累计到预期长度。
                size_t sent = 0;
                while (sent < expected.size())
                {
                    const ssize_t count = client.Send(expected.data() + sent,
                                                      expected.size() - sent, MSG_NOSIGNAL);
                    // EINTR 重试；MSG_NOSIGNAL 避免断连时 SIGPIPE 终止客户端。
                    if (count < 0 && errno == EINTR)
                        continue;
                    require(count > 0, "send failed/timed out");
                    sent += static_cast<size_t>(count);
                }
                std::string actual(expected.size(), '\0');
                size_t received = 0;
                while (received < actual.size())
                {
                    const ssize_t count = client.Recv(&actual[received], actual.size() - received);
                    if (count < 0 && errno == EINTR)
                        continue;
                    require(count > 0, "early EOF or recv failed/timed out");
                    received += static_cast<size_t>(count);
                }
                require(actual == expected, "echo bytes mismatch"); // 按完整字节串比较，包含零字节。
            };
            auto expect_eof = [&](Socket &client)
            {
                char byte;
                ssize_t count;
                do
                {
                    count = client.Recv(&byte, 1);
                } while (count < 0 && errno == EINTR);
                require(count == 0, "expected EOF, got extra data/error/timeout"); // 只有 0 表示正常关闭。
            };
            auto finish = [&](Socket &client)
            {
                // 关闭发送方向，保留接收方向以观察服务端关闭。
                require(shutdown(client.GetSocketFd(), SHUT_WR) == 0, "half-close failed");
                expect_eof(client); // 确认服务端已经处理 EOF 后才结束测试。
            };
            {
                Socket client;
                connect_client(client);
                echo(client, "hel");
                echo(client, "lo\n");                       // 在同一条连接上分段收发，不假设 recv 的分包边界。
                echo(client, std::string("a\0b\xff\n", 5)); // 验证二进制处理，不依赖字符串结束符。
                std::string large(256 * 1024, '\0');
                for (size_t i = 0; i < large.size(); ++i)
                    large[i] = static_cast<char>(i % 251);
                echo(client, large); // 超过单次 64 KiB 读取，并逐字节比对。
                finish(client);
                std::cout << "[PASS] 多次收发、分段数据、二进制、256 KiB 回显和半关闭\n";
            }
            {
                // 每个并发客户端发送不同内容，检测连接间串线。
                std::vector<std::future<void>> clients;
                for (int i = 0; i < 8; ++i)
                    clients.push_back(std::async(std::launch::async, [&, i]
                                                 {
                        Socket client;
                        connect_client(client);
                        echo(client, std::string(32 * 1024, static_cast<char>('A' + i)));
                        finish(client); }));
                for (auto &client : clients)
                    client.get(); // 等待全部完成，并将客户端线程异常传回测试线程。
                std::cout << "[PASS] 8 个并发客户端，无串线或数据丢失\n";
            }
            {
                Socket client;
                connect_client(client, 15);
                // 先回显确认连接已建立，然后静默等待 10 秒非活跃释放。
                echo(client, "idle-check");
                const auto begin = std::chrono::steady_clock::now();
                expect_eof(client);
                const auto elapsed = std::chrono::steady_clock::now() - begin;
                // 留出时间轮刻度和调度误差，同时排除立即关闭或超时失效。
                require(elapsed >= std::chrono::seconds(8) && elapsed < std::chrono::seconds(14),
                        "idle close happened outside expected interval");
                std::cout << "[PASS] 10 秒空闲超时关闭\n";
            }
            result = 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "[FAIL] EchoServer client: " << error.what() << '\n';
        }
        // 一个字节小于 PIPE_BUF；处理信号中断，父进程还能用 waitpid 交叉验证。
        ssize_t count;
        do
        {
            count = write(done[1], &result, sizeof(result));
        } while (count < 0 && errno == EINTR);
        close(done[1]);
        std::cout.flush();
        std::cerr.flush();
        _exit(result == 0 && count == sizeof(result) ? 0 : 1); // 子进程直接退出，不继续执行父进程逻辑。
    }

    close(ready[0]);
    close(done[1]);
    int client_result = -1;
    std::exception_ptr server_error;
    try
    {
        EchoServer server(0); // 让系统分配空闲端口，避免固定端口冲突
        const uint16_t port = server.GetListenPort();
        ssize_t count;
        do
        {
            count = write(ready[1], &port, sizeof(port));
        } while (count < 0 && errno == EINTR);
        require(count == sizeof(port), "port handoff failed");
        close(ready[1]);
        ready[1] = -1;
        // 辅助线程只等管道结果并投递停止请求；EventLoop 始终在父进程主线程运行。
        std::thread completion([&]
                               {
                                    unsigned char result = 1;
                                    ssize_t n;
                                    do
                                    {
                                        n = read(done[0], &result, sizeof(result));

                                    } while (n < 0 && errno == EINTR);

                                    client_result = n == sizeof(result) ? result : 1;
                                    // 管道 EOF 或读取失败也算失败，仍请求主循环退出。
                                    server.StopServer(); });

        try
        {
            server.StartServer();
        }
        catch (...)
        {
            server_error = std::current_exception();
            kill(child, SIGKILL); // 子进程退出，关闭 done 管道写端，辅助线程读到 EOF。
        }
        completion.join();
    }
    catch (...)
    {
        server_error = std::current_exception();
        kill(child, SIGKILL);
    }
    if (ready[1] >= 0)
        close(ready[1]);
    close(done[0]);
    int status = 0;
    pid_t waited;
    // 回收子进程，检查是否被信号终止或返回失败，避免只相信管道结果。
    do
    {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (server_error)
        std::rethrow_exception(server_error);
    require(waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && client_result == 0,
            "EchoServer child checks failed or timed out");
    std::cout << "[PASS] 父进程 EchoServer / 子进程 TCP 客户端 / 管道同步 / 线程退出\n";
}

void testwebbench()
{
    // 在wenbench下运行./webbench -c 10 -t 30 http://127.0.0.1:8080/，用8080端口进行测试
    EchoServer server(8080);
    server.StartServer();
}

void testutil()
{
    size_t checks = 0;
    auto require = [&](bool condition, const std::string &message)
    {
        ++checks;
        if (!condition)
            throw std::runtime_error("[FAIL] Util: " + message);
    };

    // 1. 字符串分割：完整分隔符、连续分隔符、空输入和结果覆盖。
    std::vector<std::string> parts = {"旧数据"};
    auto check_split = [&](const std::string &input, const std::string &separator,
                           const std::vector<std::string> &expected)
    {
        const size_t count = Util::Split(input, separator, parts);
        require(count == expected.size() && parts == expected,
                "Split 内容或返回数量错误");
    };
    check_split("a/b/c", "/", {"a", "b", "c"});
    check_split("/a//b/", "/", {"a", "b"});
    check_split("::a::::b::", "::", {"a", "b"});
    check_split("a:b::c", "::", {"a:b", "c"});
    check_split("abc", "/", {"abc"});
    check_split("///", "/", {});
    check_split("", "/", {});
    check_split("abc", "", {"abc"});
    check_split("", "", {});
    check_split(std::string("a\0b/c", 5), "/", {std::string("a\0b", 3), "c"});
    std::cout << "[PASS] Split：分隔符、空项、二进制字符串、结果覆盖\n";

    // 2. URL 编解码：校验明确的编码结果，以及 UTF-8、全部字节的往返。
    std::string encoded, decoded;
    const std::string safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    require(Util::UrlEncode(safe, encoded) && encoded == safe, "安全字符应原样保留");
    require(Util::UrlEncode("a b+c/?#%=&", encoded) &&
                encoded == "a%20b%2Bc%2F%3F%23%25%3D%26",
            "特殊字符编码错误");
    require(Util::UrlEncode("a b+c", encoded, true) && encoded == "a+b%2Bc",
            "表单模式空格或加号编码错误");
    require(Util::UrlDecode(encoded, decoded, true) && decoded == "a b+c",
            "表单模式解码错误");
    require(Util::UrlDecode("a+b%2Bc", decoded) && decoded == "a+b+c",
            "默认模式应保留加号");
    require(Util::UrlDecode("%2f%2F%4a%4A", decoded) && decoded == "//JJ",
            "十六进制大小写解码错误");
    require(Util::UrlDecode("%252e", decoded) && decoded == "%2e", "应只解码一次");
    require(Util::UrlEncode("中文", encoded) && encoded == "%E4%B8%AD%E6%96%87",
            "UTF-8 中文编码错误");
    require(Util::UrlDecode(encoded, decoded) && decoded == "中文", "UTF-8 中文往返失败");
    std::string bytes;
    for (int i = 0; i < 256; ++i)
        bytes.push_back(static_cast<char>(i));
    for (bool form_mode : {false, true})
    {
        require(Util::UrlEncode(bytes, encoded, form_mode), "全部字节编码失败");
        require(Util::UrlDecode(encoded, decoded, form_mode) && decoded == bytes,
                "全部 256 种字节往返不一致");
    }
    encoded = decoded = "旧数据";
    require(Util::UrlEncode("", encoded) && encoded.empty(), "空输入编码应清空结果");
    require(Util::UrlDecode("", decoded) && decoded.empty(), "空输入解码应清空结果");
    for (const std::string &bad : {"%", "%0", "%GG", "%0G", "%G0", "ok%20bad%"})
    {
        decoded = "保留原值";
        require(!Util::UrlDecode(bad, decoded), "非法编码应失败：" + bad);
        require(decoded == "保留原值", "解码失败不应修改结果");
    }
    std::string inplace = "中文 +/%";
    const std::string original = inplace;
    require(Util::UrlEncode(inplace, inplace), "原地编码失败");
    require(Util::UrlDecode(inplace, inplace) && inplace == original, "原地编解码往返失败");
    std::cout << "[PASS] URL：特殊字符、中文、256 种字节、非法编码、原地转换\n";

    // 3. 状态码：覆盖各响应类别，未知值应使用默认描述。
    for (const auto &item : std::vector<std::pair<int, std::string>>{
             {100, "Continue"}, {200, "OK"}, {201, "Created"}, {204, "No Content"}, {301, "Moved Permanently"}, {304, "Not Modified"}, {400, "Bad Request"}, {403, "Forbidden"}, {404, "Not Found"}, {500, "Internal Server Error"}, {503, "Service Unavailable"}})
        require(Util::StatusDesc(item.first) == item.second,
                "状态码描述错误：" + std::to_string(item.first));
    for (int unknown : {-1, 0, 199, 600, 999})
        require(Util::StatusDesc(unknown) == "Unknown", "未知状态码默认值错误");
    std::cout << "[PASS] StatusDesc：各类响应和未知状态码\n";

    // 4. MIME：按当前接口契约，传入文件名，后缀大小写精确匹配。
    for (const auto &item : std::vector<std::pair<std::string, std::string>>{
             {"index.html", "text/html"}, {"a.b.txt", "text/plain"}, {"/images/a.png", "image/png"}, {"style.css", "text/css"}, {"app.js", "application/javascript"}, {"data.json", "application/json"}, {"a.svg", "image/svg+xml"}, {"archive.tar.gz", "application/gzip"}, {"font.woff2", "font/woff2"}, {"sheet.xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"}, {"archive.7z", "application/x-7z-compressed"}})
        require(Util::ExtMime(item.first) == item.second, "MIME 错误：" + item.first);
    for (const std::string &unknown : {"", "README", "file.", "file.unknown", "INDEX.HTML"})
        require(Util::ExtMime(unknown) == "application/octet-stream", "MIME 默认值错误：" + unknown);
    std::cout << "[PASS] ExtMime：常见后缀、多点文件名、大小写和默认值\n";

    // 5. 路径检查：允许根目录内回退，拒绝越界及特殊路径字节。
    for (const std::string &valid : {"/", ".", "./", "a", "/a/b", "/a//./b/",
                                     "a/..", "/a/../b", "/a/b/../../", "...", "a..b"})
        require(Util::ValidPath(valid), "合法路径被拒绝：" + valid);
    for (const std::string &invalid : {"", "..", "../a", "/../a", "a/../../b",
                                       "/a/b/../../../", "a\\b"})
        require(!Util::ValidPath(invalid), "非法路径未被拒绝：" + invalid);
    require(!Util::ValidPath(std::string("/a\0/b", 5)), "应拒绝路径中的 NUL");
    require(Util::UrlDecode("/%2e%2e/secret", decoded) && !Util::ValidPath(decoded),
            "解码后的目录越界未被拒绝");
    require(Util::UrlDecode("/a/%2E%2E/b", decoded) && Util::ValidPath(decoded),
            "解码后的合法回退被拒绝");
    require(Util::UrlDecode("/a%00b", decoded) && !Util::ValidPath(decoded),
            "解码后的 NUL 未被拒绝");
    std::cout << "[PASS] ValidPath：目录深度、越界、NUL、URL 解码后检查\n";

    // 6. 文件操作：使用唯一临时文件，不覆盖项目文件。
    char temp_name[] = "/tmp/muduo-util-test-XXXXXX";
    const int fd = mkstemp(temp_name);
    require(fd >= 0, "无法创建临时文件");
    struct TempFile
    {
        std::string path;
        bool removed = false;
        ~TempFile()
        {
            if (!removed)
                std::remove(path.c_str());
        }
    } temp{temp_name};
    require(close(fd) == 0, "临时文件描述符关闭失败");
    const std::string missing = temp.path + ".missing";
    require(Util::IsRegular(temp.path) && !Util::IsDirectory(temp.path), "普通文件类型判断错误");
    require(Util::IsDirectory("/tmp") && !Util::IsRegular("/tmp"), "目录类型判断错误");
    require(!Util::IsRegular(missing) && !Util::IsDirectory(missing), "不存在的路径类型判断错误");
    const std::string nul_path = temp.path + std::string("\0suffix", 7);
    require(!Util::IsRegular(nul_path) && !Util::IsDirectory(nul_path), "应拒绝含 NUL 的文件路径");

    auto check_file = [&](const std::string &data)
    {
        require(Util::WriteFile(temp.path, data), "写文件失败");
        decoded = "旧内容";
        require(Util::ReadFile(temp.path, decoded) && decoded == data, "文件内容不一致");
        struct stat info{};
        require(stat(temp.path.c_str(), &info) == 0 &&
                    info.st_size == static_cast<off_t>(data.size()),
                "文件长度错误或旧内容未截断");
    };
    check_file("第一行\n第二行\r\n");
    check_file(bytes);                         // 包括 NUL 和高位字节。
    check_file(std::string(1024 * 1024, 'x')); // 1 MiB 文件。
    check_file("短内容");                      // 验证覆盖大文件后没有残留。
    check_file("");
    decoded = "保留原值";
    require(!Util::ReadFile(missing, decoded) && decoded == "保留原值", "读取不存在文件的行为错误");
    require(!Util::ReadFile("/tmp", decoded) && decoded == "保留原值", "不应读取目录");
    require(!Util::ReadFile(nul_path, decoded) && decoded == "保留原值", "不应读取含 NUL 的路径");
    require(!Util::WriteFile(nul_path, "x"), "不应写入含 NUL 的路径");
    require(!Util::WriteFile("/tmp", "x"), "不应写入目录");
    require(!Util::WriteFile(temp.path + "/child", "x"), "父路径为普通文件时应写入失败");
    require(std::remove(temp.path.c_str()) == 0, "删除测试临时文件失败");
    temp.removed = true;
    require(!Util::IsRegular(temp.path), "删除后的文件不应存在");
    std::cout << "[PASS] 文件操作：文本、二进制、空文件、大文件、覆盖、失败分支" << std::endl;
    std::cout << "[PASS] Util 全部测试通过" << std::endl;
}

void testhttp()
{
    size_t checks = 0, cases = 0, failures = 0;
    // 不依赖 assert，Release 构建也执行检查；一个用例失败不阻止其他用例。
    auto require = [&](bool ok, const std::string &message)
    {
        ++checks;
        if (!ok)
            throw std::runtime_error(message);
    };
    auto run = [&](const std::string &name, const auto &test)
    {
        ++cases;
        // 将用例名中的换行显示成转义字符，让每条日志保持在同一行。
        std::string label;
        for (char ch : name)
            label += ch == '\r' ? "\\r" : ch == '\n' ? "\\n"
                                                     : std::string(1, ch);
        INF_LOG("[HTTP RUN] %s", label.c_str());
        try
        {
            test();
            INF_LOG("[HTTP PASS] %s", label.c_str());
        }
        catch (const std::exception &error)
        {
            ++failures;
            ERR_LOG("[HTTP FAIL] %s: %s", label.c_str(), error.what());
        }
    };
    // 只使用内存 Buffer，不启动服务器或建立 TCP 连接。
    auto parse = [&](HttpContext &context, Buffer &buffer, const std::string &data)
    {
        buffer.WriteStringAndPush(data);
        context.RecvHttpRequest(buffer);
    };

    run("HttpRequest：请求头、参数、覆盖与清空", [&]
        {
        HttpRequest request;
        std::string key = "X-Test", param = "name", value = "中文", missing = "missing";
        require(!request.HasHeader(key) && request.GetHeader(key).empty(), "缺失请求头行为错误");
        request.SetHeader(key, "first");
        request.SetHeader(key, "second");
        require(request.HasHeader(key) && request.GetHeader(key) == "second", "请求头覆盖失败");
        request.SetParam(param, value);
        require(request.HasParam(param) && request.GetParam(param) == value, "参数保存失败");
        value = "";
        request.SetParam(param, value);
        require(request.HasParam(param) && request.GetParam(param).empty(), "空参数应存在");
        require(!request.HasParam(missing) && request.GetParam(missing).empty(), "缺失参数行为错误");
        require(request.ContentLength() == 0, "没有正文长度时应为 0");
        request.SetHeader("Content-Length", "42");
        require(request.ContentLength() == 42, "正文长度读取错误");
        request.Clear();
        require(!request.HasHeader(key) && !request.HasParam(param) && request.ContentLength() == 0,
                "Clear 未清空请求头或参数"); });

    // 直接使用请求对象时，头字段也应按 HTTP 的大小写不敏感规则查找。
    run("HttpRequest：请求头名称大小写", [&]
        {
        HttpRequest request;
        request.SetHeader("content-length", "4");
        require(request.ContentLength() == 4, "小写 content-length 未被识别"); });
    for (const std::string invalid : {"-1", "+2", "3xyz", "", "18446744073709551616"})
        run("HttpRequest：非法 Content-Length [" + invalid + "]", [&]
            {
            HttpRequest request;
            request.SetHeader("Content-Length", invalid);
            bool rejected = false;
            try { (void)request.ContentLength(); }
            catch (const std::exception &) { rejected = true; }
            require(rejected, "非法长度被接受，应拒绝而不是返回有效长度"); });

    run("HttpResponse：响应头、正文类型和字节长度", [&]
        {
        HttpResponse response;
        std::string key = "X-Test", value = "one", missing = "missing";
        require(!response.HasHeader(missing) && response.GetHeader(missing).empty(), "缺失响应头行为错误");
        response.SetHeader(key, value);
        value = "two";
        response.SetHeader(key, value);
        require(response.GetHeader(key) == "two", "响应头覆盖失败");
        std::string type = "application/octet-stream", length = "Content-Length", content_type = "Content-Type";
        for (std::string body : {std::string("中文"), std::string("a\0b", 3), std::string(1024 * 1024, 'x'), std::string()})
        {
            response.SetContent(body, type);
            require(response.GetHeader(length) == std::to_string(body.size()), "Content-Length 未按字节计算或覆盖失败");
            require(response.GetHeader(content_type) == type, "Content-Type 设置失败");
        } });
    run("HttpResponse：重定向与 ReSet", [&]
        {
        HttpResponse response;
        std::string url = "/login", location = "Location", type = "text/plain", body = "hello", length = "Content-Length";
        response.SetContent(body, type);
        response.SetRedirect(url);
        require(response.HasHeader(location) && response.GetHeader(location) == url, "重定向地址未保存");
        url = "/new";
        response.SetRedirect(url, 301);
        require(response.GetHeader(location) == url, "重定向地址未覆盖");
        response.ReSet();
        require(!response.HasHeader(location) && !response.HasHeader(length), "ReSet 未清除响应头"); });
    run("HttpResponse：显式 keep-alive 与 close", [&]
        {
        HttpResponse response;
        std::string key = "Connection", value = "ClOsE";
        response.SetHeader(key, value);
        require(response.Close(), "显式 close 应关闭连接");
        value = "keep-alive";
        response.SetHeader(key, value);
        require(!response.Close(), "显式 keep-alive 仍关闭连接，检查响应版本初始化及设置接口"); });

    for (const std::string method : {"GET", "HEAD", "POST", "PUT", "DELETE"})
        run("HttpContext：" + method + " 与查询参数", [&]
            {
            HttpContext context;
            Buffer buffer;
            require(context.RespStatu() == 200 && context.RecvStatu() == RECV_HTTP_LINE, "初始状态错误");
            parse(context, buffer, method + " /a%20b?q=a%26b&eq=a%3Db&x=a+b&flag&name=%E4%B8%AD%E6%96%87 HTTP/1.1\r\nhost: localhost\r\n\r\n");
            require(context.RecvStatu() == RECV_HTTP_OVER && context.RespStatu() == 200, "合法请求未完成");
            std::string q = "q", eq = "eq", x = "x", flag = "flag", name = "name", host = "Host";
            require(context.Request().GetParam(q) == "a&b", "编码的 & 被错误拆分");
            require(context.Request().GetParam(eq) == "a=b", "编码的 = 解码错误");
            require(context.Request().GetParam(x) == "a b", "查询参数 + 解码错误");
            require(context.Request().HasParam(flag) && context.Request().GetParam(flag).empty(), "无等号参数错误");
            require(context.Request().GetParam(name) == "中文", "中文参数解码错误");
            require(context.Request().GetHeader(host) == "localhost", "小写 Host 未归一化");
            require(!context.Request().Close() && buffer.ReadAbleSize() == 0, "HTTP/1.1 默认长连接或缓冲消费错误"); });

    run("HttpContext：跨接收分段、二进制正文与下一条请求", [&]
        {
        HttpContext context;
        Buffer buffer;
        parse(context, buffer, "POST / HTTP/1.1\r");
        require(context.RecvStatu() == RECV_HTTP_LINE && buffer.ReadAbleSize() > 0, "半行数据不应消费");
        parse(context, buffer, "\nHost: local\r\nContent-Len");
        require(context.RecvStatu() == RECV_HTTP_HEAD, "应等待剩余请求头");
        parse(context, buffer, "gth: 4\r\n\r\na");
        require(context.RecvStatu() == RECV_HTTP_BODY, "正文不足应等待");
        const std::string next = "GET / HTTP/1.0\r\n\r\n";
        parse(context, buffer, std::string("\0bc", 3) + next);
        require(context.RecvStatu() == RECV_HTTP_OVER && context.Request().ContentLength() == 4, "二进制正文未完成");
        require(buffer.ReadAbleSize() == next.size() && buffer.ReadAsString(next.size()) == next, "误消费下一条请求");
        context.RecvHttpRequest(buffer);
        require(buffer.ReadAbleSize() == next.size(), "完成状态不应继续消费");
        context.Clear();
        require(context.RecvStatu() == RECV_HTTP_LINE && context.Request().ContentLength() == 0, "上下文重置错误");
        context.RecvHttpRequest(buffer);
        require(context.RecvStatu() == RECV_HTTP_OVER && context.Request().Close(), "HTTP/1.0 请求或默认短连接错误"); });
    run("HttpContext：逐字节接收", [&]
        {
        HttpContext context;
        Buffer buffer;
        const std::string data = "POST /?x=1 HTTP/1.1\r\nHost: a\r\nContent-Length: 3\r\n\r\nabc";
        for (size_t i = 0; i < data.size(); ++i)
        {
            parse(context, buffer, data.substr(i, 1));
            require(context.RecvStatu() != RECV_HTTP_ERROR, "逐字节解析失败");
            require((context.RecvStatu() == RECV_HTTP_OVER) == (i + 1 == data.size()), "完成时间错误");
        } });
    for (const auto &item : std::vector<std::pair<std::string, bool>>{
             {"HTTP/1.0\r\nConnection: keep-alive", false},
             {"HTTP/1.1\r\nHost: a\r\nConnection: ClOsE", true}})
        run("HttpRequest：连接策略 [" + item.first + "]", [&]
            {
            HttpContext context;
            Buffer buffer;
            parse(context, buffer, "GET / " + item.first + "\r\n\r\n");
            require(context.RecvStatu() == RECV_HTTP_OVER, "连接策略请求未解析完成");
            require(context.Request().Close() == item.second, "Connection 选项或大小写处理错误"); });

    // 每个错误输入使用全新上下文；拒绝后再次调用不应继续消费缓冲区。
    auto reject = [&](const std::string &name, const std::string &input, int status)
    {
        run("HttpContext：拒绝 " + name, [&]
            {
            HttpContext context;
            Buffer buffer;
            parse(context, buffer, input);
            require(context.RecvStatu() == RECV_HTTP_ERROR, "非法请求未进入错误状态");
            require(context.RespStatu() == status, "错误状态码不正确");
            const size_t remaining = buffer.ReadAbleSize();
            context.RecvHttpRequest(buffer);
            require(buffer.ReadAbleSize() == remaining, "错误状态仍在消费数据"); });
    };
    reject("不支持的方法", "PATCH / HTTP/1.1\r\n", 400);
    reject("不支持的版本", "GET / HTTP/2.0\r\n", 400);
    reject("空路径", "GET  HTTP/1.1\r\n", 400);
    reject("目录越界", "GET /%2e%2e/x HTTP/1.1\r\n", 400);
    reject("错误百分号编码", "GET /bad%GG HTTP/1.1\r\n", 400);
    reject("缺少 Host", "GET / HTTP/1.1\r\n\r\n", 400);
    reject("重复 Host", "GET / HTTP/1.1\r\nHost: a\r\nhost: b\r\n", 400);
    reject("非法头字段名", "GET / HTTP/1.1\r\nHost: a\r\nBad Key: x\r\n", 400);
    for (const std::string length : {"-1", "+1", "1x", "", "18446744073709551616"})
        reject("非法正文长度 [" + length + "]", "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: " + length + "\r\n", 400);
    reject("重复正文长度", "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\ncontent-length: 1\r\n", 400);
    reject("超大正文", "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 16777217\r\n", 413);
    reject("chunked 未支持", "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", 501);
    reject("冲突的正文边界", "POST / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\nContent-Length: 1\r\n\r\n", 400);
    reject("超长请求行", "GET /" + std::string(8192, 'a'), 414);
    reject("超长头字段", "GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(8192, 'a'), 431);
    std::string headers = "GET / HTTP/1.1\r\nHost: a\r\n";
    for (int i = 0; i < 70; ++i)
        headers += "X: " + std::string(1000, 'a') + "\r\n";
    reject("请求头总量超限", headers, 431);
    reject("头字段缺少 CR", "GET / HTTP/1.1\r\nHost: a\r\nX-Test: xyz\n\r\n", 400);
    reject("结束空行仅有 LF", "GET / HTTP/1.1\r\nHost: a\r\n\n", 400);

    INF_LOG("[HTTP SUMMARY] 用例 %zu，检查 %zu，失败用例 %zu", cases, checks, failures);
    // 当前接口没有方法、路径、正文或响应状态的 getter，无法直接核对这些私有值。
    if (failures != 0)
        throw std::runtime_error("HTTP 测试存在 " + std::to_string(failures) + " 个失败用例，请查看日志");
}

// HTTP 测试辅助代码
namespace http_test
{
    void Check(bool ok, const std::string &message)
    {
        if (!ok)
            throw std::runtime_error(message);
    }

    std::string ParentDirectory(const std::string &path)
    {
        const size_t slash = path.find_last_of('/');
        if (slash == std::string::npos)
            return "";
        return slash == 0 ? "/" : path.substr(0, slash);
    }

    std::string FindWwwroot(const std::string &configured_root)
    {
        char resolved[PATH_MAX];
        const char *override_root = std::getenv("HTTP_TEST_WWWROOT");
        if (override_root != NULL)
        {
            // 用户显式指定的目录必须有效，不悄悄改用其他网页。
            Check(realpath(override_root, resolved) != NULL && Util::IsDirectory(resolved),
                  "invalid HTTP_TEST_WWWROOT: " + std::string(override_root));
            return resolved;
        }
        if (realpath(configured_root.c_str(), resolved) != NULL && Util::IsDirectory(resolved))
            return resolved;

        // CMake 的源码路径、当前目录、源码文件和可执行文件位置都可作为起点。
        // 沿父目录寻找项目已有资源，找到后统一使用绝对路径。
        std::vector<std::string> starts;
        starts.push_back(ParentDirectory(configured_root));
        if (realpath(".", resolved) != NULL)
            starts.push_back(resolved);
        if (realpath(__FILE__, resolved) != NULL)
            starts.push_back(ParentDirectory(resolved));
        const ssize_t length = readlink("/proc/self/exe", resolved, sizeof(resolved) - 1);
        if (length > 0 && length < static_cast<ssize_t>(sizeof(resolved) - 1))
        {
            resolved[length] = '\0';
            starts.push_back(ParentDirectory(resolved));
        }

        const std::vector<std::string> locations = {"wwwroot", "release/wwwroot"};
        for (std::string directory : starts)
        {
            while (!directory.empty())
            {
                for (const std::string &location : locations)
                {
                    const std::string candidate = directory + "/" + location;
                    if (realpath(candidate.c_str(), resolved) != NULL && Util::IsDirectory(resolved))
                        return resolved;
                }
                const std::string parent = ParentDirectory(directory);
                if (parent == directory)
                    break;
                directory = parent;
            }
        }
        throw std::runtime_error("wwwroot not found; set HTTP_TEST_WWWROOT to its absolute path");
    }

    struct Suite
    {
        size_t cases, failures;
        Suite() : cases(0), failures(0) {}

        void Run(const std::string &name, const std::function<void()> &test)
        {
            ++cases;
            try
            {
                test();
                std::cout << "[PASS] " << name << std::endl;
            }
            catch (const std::exception &error)
            {
                ++failures;
                std::cerr << "[FAIL] " << name << ": " << error.what() << std::endl;
            }
        }
    };

    struct Response
    {
        int status;
        std::string version, body;
        std::unordered_map<std::string, std::string> headers;
        Response() : status(0) {}
    };

    // 客户端使用独立 fd；析构时关闭，异常分支也不会遗留连接。
    class Client
    {
        Socket socket;
        std::string pending;

    public:
        explicit Client(uint16_t port, int seconds = 6)
        {
            Check(socket.CreateClient("127.0.0.1", port), "connect failed");
            timeval timeout = {};
            timeout.tv_sec = seconds;
            Check(setsockopt(Fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0 &&
                      setsockopt(Fd(), SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0,
                  "socket timeout setup failed");
        }
        int Fd() const { return socket.GetSocketFd(); }
        void Close() { socket.Close(); }

        void Send(const std::string &data)
        {
            size_t sent = 0;
            while (sent < data.size())
            {
                ssize_t n = socket.Send(data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
                if (n < 0 && errno == EINTR)
                    continue;
                Check(n > 0, "send failed or timed out");
                sent += static_cast<size_t>(n);
            }
        }
        void Receive()
        {
            char bytes[32768];
            ssize_t n;
            do
            {
                n = socket.Recv(bytes, sizeof(bytes));
            } while (n < 0 && errno == EINTR);
            Check(n > 0, "unexpected EOF or receive timeout");
            pending.append(bytes, static_cast<size_t>(n));
        }

        Response Read(bool head = false)
        {
            // TCP 没有消息边界：先读完整响应头，再按长度读正文。pending 留给下一条响应，HEAD 不消费 Content-Length 对应的正文。
            size_t end;
            while ((end = pending.find("\r\n\r\n")) == std::string::npos)
            {
                Check(pending.size() < 65536, "response headers exceed 64 KiB");
                Receive();
            }
            std::istringstream lines(pending.substr(0, end));
            std::string line;
            Response response;
            Check(static_cast<bool>(std::getline(lines, line)), "missing status line");
            std::istringstream status(line);
            Check(static_cast<bool>(status >> response.version >> response.status) &&
                      (response.version == "HTTP/1.0" || response.version == "HTTP/1.1"),
                  "invalid status line");
            while (std::getline(lines, line))
            {
                if (!line.empty() && line[line.size() - 1] == '\r')
                    line.erase(line.size() - 1);
                const size_t colon = line.find(':');
                Check(colon != std::string::npos && colon != 0, "invalid response header");
                std::string key = line.substr(0, colon);
                std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch)
                               { return static_cast<char>(std::tolower(ch)); });
                const size_t first = line.find_first_not_of(" \t", colon + 1);
                const std::string value = first == std::string::npos ? "" : line.substr(first);
                Check(response.headers.emplace(key, value).second, "duplicate response header: " + key);
            }
            pending.erase(0, end + 4);
            size_t length = 0;
            if (!head && response.status >= 200 && response.status != 204 && response.status != 304)
            {
                const std::string value = response.headers.at("content-length");
                Check(!value.empty() && value.find_first_not_of("0123456789") == std::string::npos,
                      "invalid response length");
                length = static_cast<size_t>(std::stoull(value));
                Check(length <= 32 * 1024 * 1024, "response exceeds test memory limit");
            }
            while (pending.size() < length)
                Receive();
            response.body = pending.substr(0, length);
            pending.erase(0, length);
            return response;
        }
        void Eof()
        {
            Check(pending.empty(), "extra response bytes");
            char byte;
            ssize_t n;
            do
            {
                n = socket.Recv(&byte, 1);
            } while (n < 0 && errno == EINTR);
            Check(n == 0, "expected clean EOF, got data/error/timeout");
        }
    };

    std::string Wire(const std::string &method, const std::string &path,
                     bool close_connection = true, const std::string &body = "")
    {
        std::string result = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\n";
        if (close_connection)
            result += "Connection: close\r\n";
        if (method == "POST" || method == "PUT")
            result += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        return result + "\r\n" + body;
    }

    Response Exchange(uint16_t port, const std::string &wire, bool head = false)
    {
        Client client(port);
        client.Send(wire);
        Response response = client.Read(head);
        Check(response.headers.at("connection") == "close", "expected Connection: close");
        client.Eof();
        return response;
    }

    void Text(HttpResponse &response, std::string body)
    {
        std::string type = "text/plain; charset=utf-8";
        response.SetContent(body, type);
    }

    void Configure(HttpServer &server, const std::string &root, int threads)
    {
        server.SetStaticDir(root);
        server.SetThreadCount(threads);
        server.AddGetRoute("/api/status", [](const HttpRequest &, HttpResponse &response)
                           {
        std::string body = "{\"status\":\"ok\",\"server\":\"muduo-otol\"}\n";
        std::string type = "application/json; charset=utf-8";
        response.SetContent(body, type); });
        server.AddGetRoute("/api/query", [](const HttpRequest &request, HttpResponse &response)
                           {
        HttpRequest copy(request); // 现有 getter 非 const，只读取副本。
        std::string name = "name", tag = "tag";
        Text(response, "name=" + copy.GetParam(name) + "\ntag=" + copy.GetParam(tag) + "\n"); });
        server.AddPostRoute("/api/items", [](const HttpRequest &request, HttpResponse &response)
                            {
        HttpRequest copy(request);
        Text(response, "POST bytes=" + std::to_string(copy.ContentLength()) + "\n"); });
        server.AddPutRoute(R"(/api/items/([0-9]+))", [](const HttpRequest &request, HttpResponse &response)
                           {
        HttpRequest copy(request);
        Text(response, "PUT bytes=" + std::to_string(copy.ContentLength()) + "\n"); });
        server.AddDeleteRoute(R"(/api/items/([0-9]+))", [](const HttpRequest &, HttpResponse &response)
                              { Text(response, "DELETE accepted\n"); });
        server.AddGetRoute("/redirect", [](const HttpRequest &, HttpResponse &response)
                           { std::string url = "/index.html"; response.SetRedirect(url); });
        server.AddGetRoute("/api/close", [](const HttpRequest &, HttpResponse &response)
                           {
        Text(response, "closing\n");
        std::string key = "Connection", value = "close";
        response.SetHeader(key, value); });
        // 以下路由只用于测试，不添加到正式 main.cpp。
        server.AddGetRoute("/_test/slow", [](const HttpRequest &, HttpResponse &response)
                           { std::this_thread::sleep_for(std::chrono::milliseconds(3500)); Text(response, "slow\n"); });
        server.AddGetRoute("/_test/throw", [](const HttpRequest &, HttpResponse &response)
                           {
            // 按业务处理函数自行捕获异常的方式测试，不要求服务框架兜底。
            try
            {
                throw std::runtime_error("intentional test handler exception");
            }
            catch (const std::exception &)
            {
                Text(response, "handler caught exception\n");
            } });
        server.AddGetRoute("/_test/large", [](const HttpRequest &, HttpResponse &response)
                           {
        std::string body(2 * 1024 * 1024, '\0');
        for (size_t i = 0; i < body.size(); ++i)
            body[i] = static_cast<char>(i % 251);
        std::string type = "application/octet-stream";
        response.SetContent(body, type); });
        server.AddGetRoute("/_test/header", [](const HttpRequest &, HttpResponse &response)
                           {
        // 只用固定测试值验证响应头是否会接受 CRLF 注入。
        Text(response, "header\n");
        std::string key = "X-Test", value = "safe\r\nX-Injected: yes";
        response.SetHeader(key, value); });
        // 制造大小写不同的同名字段，检查最终响应是否正确覆盖且没有重复字段。
        server.AddGetRoute("/_test/header-case",
            [](const HttpRequest &, HttpResponse &response)
            {
                std::string type_key = "content-type";
                std::string old_type = "application/json";
                response.SetHeader(type_key, old_type);

                std::string connection_key = "connection";
                std::string connection_value = "keep-alive";
                response.SetHeader(connection_key, connection_value);

                Text(response, "header case\n");
            });
    }

    // 每个服务器在独立进程中运行。崩溃测试不会中断其余测试。Stop 走实际停止和析构流程；只有超过等待上限时才终止本次创建的进程。
    class ServerProcess
    {
        pid_t child;
        int control;

    public:
        uint16_t port;
        ServerProcess(const std::string &root, int timeout = 3, int threads = 3)
            : child(-1), control(-1), port(0)
        {
            int ready[2], done[2];
            Check(pipe(ready) == 0, "ready pipe failed");
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, done) != 0)
            {
                close(ready[0]);
                close(ready[1]);
                throw std::runtime_error("control socketpair failed");
            }
            std::cout.flush();
            std::cerr.flush();
            child = fork();
            if (child < 0)
            {
                close(ready[0]);
                close(ready[1]);
                close(done[0]);
                close(done[1]);
                throw std::runtime_error("fork failed");
            }
            if (child == 0)
            {
                close(ready[0]);
                close(done[0]);
                struct rlimit limit = {0, 0};
                setrlimit(RLIMIT_CORE, &limit); // 故意触发崩溃时不生成 core 文件。
                alarm(55);
                int quiet = open("/dev/null", O_WRONLY);
                if (quiet >= 0)
                {
                    dup2(quiet, STDOUT_FILENO);
                    close(quiet);
                }
                try
                {
                    std::unique_ptr<HttpServer> server;
                    for (int attempt = 0; attempt < 8 && !server; ++attempt)
                    {
                        // HttpServer 尚无公开端口 getter：先探测，再绑定，冲突时重试。
                        Socket probe;
                        Check(probe.Create() && probe.Bind(0, "127.0.0.1"), "port probe failed");
                        sockaddr_in address = {};
                        socklen_t size = sizeof(address);
                        Check(getsockname(probe.GetSocketFd(), reinterpret_cast<sockaddr *>(&address), &size) == 0,
                              "getsockname failed");
                        port = ntohs(address.sin_port);
                        probe.Close();
                        try
                        {
                            server.reset(new HttpServer(port, timeout));
                        }
                        catch (const std::runtime_error &)
                        {
                            if (attempt == 7)
                                throw;
                        }
                    }
                    Configure(*server, root, threads);
                    ssize_t n;
                    do
                    {
                        n = write(ready[1], &port, sizeof(port));
                    } while (n < 0 && errno == EINTR);
                    Check(n == static_cast<ssize_t>(sizeof(port)), "port handoff failed");
                    close(ready[1]);
                    std::thread completion([&server, &done]()
                                           {
                    char byte;
                    ssize_t count;
                    do { count = recv(done[1], &byte, 1, 0); } while (count < 0 && errno == EINTR);
                    server->Stop(); });
                    server->Start();
                    completion.join();
                    server.reset(); // 真实析构，能够发现活动连接的生命周期问题。
                    close(done[1]);
                    _exit(0);
                }
                catch (const std::exception &error)
                {
                    std::cerr << "[SERVER PROCESS] " << error.what() << std::endl;
                    _exit(1);
                }
            }
            close(ready[1]);
            close(done[1]);
            control = done[0];
            pollfd event = {ready[0], POLLIN, 0};
            int result;
            do
            {
                result = poll(&event, 1, 5000);
            } while (result < 0 && errno == EINTR);
            ssize_t n = result > 0 ? read(ready[0], &port, sizeof(port)) : -1;
            close(ready[0]);
            if (n != static_cast<ssize_t>(sizeof(port)) || port == 0)
            {
                Stop();
                throw std::runtime_error("server did not become ready within 5 seconds");
            }
        }
        pid_t Pid() const { return child; }
        int Stop()
        {
            if (child < 0)
                return 0;
            const char byte = 1;
            ssize_t n;
            do
            {
                n = send(control, &byte, 1, MSG_NOSIGNAL);
            } while (n < 0 && errno == EINTR);
            close(control);
            control = -1;
            int status = 0;
            for (int i = 0; i < 100; ++i)
            {
                const pid_t result = waitpid(child, &status, WNOHANG);
                if (result == child)
                {
                    child = -1;
                    return status;
                }
                if (result < 0 && errno != EINTR)
                {
                    child = -1;
                    return -1;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
            child = -1;
            return status;
        }
        ~ServerProcess() { Stop(); }

    private:
        ServerProcess(const ServerProcess &);
        ServerProcess &operator=(const ServerProcess &);
    };

    long RssKiB(pid_t pid)
    {
        std::ifstream file(("/proc/" + std::to_string(pid) + "/status").c_str());
        std::string line;
        while (std::getline(file, line))
            if (line.compare(0, 6, "VmRSS:") == 0)
            {
                std::istringstream value(line.substr(6));
                long kib = 0;
                value >> kib;
                return kib;
            }
        throw std::runtime_error("cannot read server RSS");
    }

    void Webbench(uint16_t port, int clients, int seconds, const std::string &path,
                  const std::string &root)
    {
        const char *override_path = std::getenv("HTTP_TEST_WEBBENCH");
        std::string executable = override_path ? override_path : "test/webbench/webbench";
        if (override_path == NULL)
        {
            // root 已经是绝对路径；从资源目录向上查找，避免依赖启动目录。
            for (std::string directory = root; !directory.empty();)
            {
                const std::string candidate = directory + "/test/webbench/webbench";
                if (access(candidate.c_str(), X_OK) == 0)
                {
                    executable = candidate;
                    break;
                }
                const std::string parent = ParentDirectory(directory);
                if (parent == directory)
                    break;
                directory = parent;
            }
        }
        Check(access(executable.c_str(), X_OK) == 0,
              "webbench missing; build it in Docker or set HTTP_TEST_WEBBENCH");
        int output[2];
        Check(pipe(output) == 0, "webbench output pipe failed");
        const pid_t pid = fork();
        if (pid < 0)
        {
            close(output[0]);
            close(output[1]);
            throw std::runtime_error("webbench fork failed");
        }
        if (pid == 0)
        {
            setpgid(0, 0); // 压测超时时，只结束本次 webbench 及其客户端进程。
            close(output[0]);
            dup2(output[1], STDOUT_FILENO);
            dup2(output[1], STDERR_FILENO);
            close(output[1]);
            const std::string c = std::to_string(clients), t = std::to_string(seconds);
            const std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
            execl(executable.c_str(), executable.c_str(), "-2", "-c", c.c_str(), "-t", t.c_str(), url.c_str(),
                  static_cast<char *>(NULL));
            _exit(127);
        }
        close(output[1]);
        int status = 0;
        bool finished = false;
        for (int i = 0; i < (seconds + 5) * 20; ++i)
        {
            if (waitpid(pid, &status, WNOHANG) == pid)
            {
                finished = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!finished)
        {
            kill(-pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            {
            }
            close(output[0]);
            throw std::runtime_error("webbench exceeded its deadline");
        }
        std::string report;
        char bytes[2048];
        ssize_t n;
        while ((n = read(output[0], bytes, sizeof(bytes))) > 0)
            report.append(bytes, static_cast<size_t>(n));
        close(output[0]);
        const size_t summary = report.find("Speed=");
        std::cout << "[WEBBENCH] clients=" << clients << " seconds=" << seconds << " path=" << path
                  << " exit_code=" << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) << '\n'
                  << (summary == std::string::npos ? report : report.substr(summary)) << std::flush;
        // 此仓库的 webbench 在 bench() 最后 return i，i 是最后一个客户端的成功数。
        // 因此正常压测也可能返回非零：以完整统计、正常退出和失败数共同判断。
        Check(WIFEXITED(status) && report.find("Some of our childrens died") == std::string::npos,
              "webbench terminated abnormally or lost workers");
        std::smatch match;
        Check(std::regex_search(report, match, std::regex(R"(Requests: ([0-9]+) susceed, ([0-9]+) failed)")),
              "cannot parse webbench result");
        Check(std::stoull(match[1].str()) > 0 && std::stoull(match[2].str()) == 0,
              "webbench reported failed requests or no successes");
        // webbench 不检查 HTTP 状态和正文，所以调用方还会检查压测前后的真实响应。
    }
} // namespace http_test

void testhttpserver()
{
    using namespace http_test;
#ifdef HTTP_TEST_WWWROOT
    const std::string configured_root = HTTP_TEST_WWWROOT;
#else
    const std::string configured_root = "wwwroot";
#endif
    const std::string root = FindWwwroot(configured_root);
    std::cout << "[HTTP TEST ROOT] " << root << std::endl;
    // 保留原来的手动网页测试入口；没有指定时运行自动测试。
    const char *manual = std::getenv("HTTP_TEST_SERVE");
    if (manual && std::string(manual) == "1")
    {
        const char *value = std::getenv("HTTP_TEST_PORT");
        const std::string text = value ? value : "8080";
        Check(!text.empty() && text.size() <= 5 && text.find_first_not_of("0123456789") == std::string::npos,
              "invalid HTTP_TEST_PORT");
        const int port = std::stoi(text);
        Check(port >= 1 && port <= 65535, "HTTP_TEST_PORT out of range");
        HttpServer server(port);
        Configure(server, root, 3);
        std::cout << "HTTP manual test: http://127.0.0.1:" << port << "/" << std::endl;
        server.Start();
        return;
    }

    Suite suite;
    ServerProcess server(root);
    const uint16_t port = server.port;
    const std::string status_body = "{\"status\":\"ok\",\"server\":\"muduo-otol\"}\n";

    suite.Run("request header names ignore case", []()
    {
        Buffer buffer;
        buffer.WriteStringAndPush(
            "GET / HTTP/1.1\r\n"
            "hOsT: localhost\r\n"
            "aUtHoRiZaTiOn: Bearer AbC123\r\n"
            "eTaG: CaseSensitiveToken\r\n"
            "\r\n");

        HttpContext context;
        context.RecvHttpRequest(buffer);
        Check(context.RecvStatu() == RECV_HTTP_OVER, "request parsing failed");
        HttpRequest &request = context.Request();
        const std::vector<std::string> names = {
            "authorization", "Authorization", "AUTHORIZATION"};
        for (std::string name : names)
        {
            Check(request.HasHeader(name), "request header not found");
            Check(request.GetHeader(name) == "Bearer AbC123",
                  "request header value changed or missing");
        }
        std::string etag = "ETag";
        Check(request.HasHeader(etag) && request.GetHeader(etag) == "CaseSensitiveToken",
              "ETag lookup failed");

        // 新名称仅大小写不同，应更新原字段；字段值仍区分大小写。
        request.SetHeader("AUTHORIZATION", "Bearer NewToken");
        std::string authorization = "Authorization";
        Check(request.GetHeader(authorization) == "Bearer NewToken",
              "request header was not updated");
    });
    suite.Run("response header names ignore case", []()
    {
        HttpResponse response;
        std::string key = "content-type";
        std::string value = "application/json";
        response.SetHeader(key, value);
        const std::vector<std::string> names = {
            "content-type", "Content-Type", "CONTENT-TYPE"};
        for (std::string name : names)
        {
            Check(response.HasHeader(name), "response header not found");
            Check(response.GetHeader(name) == value, "response header lookup failed");
        }
        std::string body = "ABC";
        std::string type = "text/plain; charset=utf-8";
        response.SetContent(body, type);
        Check(response.GetHeader(key) == type, "SetContent did not replace the old content type");

        std::string location = "location";
        std::string old_url = "/old";
        std::string new_url = "/new";
        response.SetHeader(location, old_url);
        response.SetRedirect(new_url);
        Check(response.GetHeader(location) == new_url,
              "SetRedirect did not replace the old location");
    });
    suite.Run("response headers have no case duplicates", [&]()
    {
        // Client::Read 按不区分大小写的名称检查重复字段，不会只保留最后一个值。
        const Response response = Exchange(port, Wire("GET", "/_test/header-case"));
        Check(response.status == 200 && response.body == "header case\n",
              "response status or body mismatch");
        Check(response.headers.at("content-type") == "text/plain; charset=utf-8",
              "wrong final content type");
        Check(response.headers.at("connection") == "close", "wrong final connection policy");
    });

    // 第一部分：重写已有覆盖。比较真实文件字节、MIME、长度和连接关闭状态。
    const std::vector<std::string> files = {
        "/index.html", "/about.html", "/docs/index.html", "/docs/routing.html",
        "/examples/form.html", "/examples/table.html", "/status.html", "/download.html",
        "/contact.html", "/performance.html", "/faq.html", "/404.html",
        "/assets/style.css", "/assets/app.js"};
    for (const std::string &path : files)
        suite.Run("static " + path, [&, path]()
                  {
            std::string expected;
            Check(Util::ReadFile(root + path, expected), "fixture read failed");
            const Response response = Exchange(port, Wire("GET", path));
            Check(response.status == 200 && response.body == expected, "static body/status mismatch");
            Check(response.headers.at("content-type") == Util::ExtMime(path), "MIME mismatch");
            Check(response.headers.at("content-length") == std::to_string(expected.size()), "length mismatch"); });
    for (const std::string &path : std::vector<std::string>{"/", "/docs/", "/docs", "/index%2Ehtml?theme=light"})
        suite.Run("directory/decoded path " + path, [&, path]()
                  {
            std::string expected;
            Check(Util::ReadFile(root + (path.find("/docs") == 0 ? "/docs/index.html" : "/index.html"), expected),
                  "index read failed");
            const Response response = Exchange(port, Wire("GET", path));
            Check(response.status == 200 && response.body == expected, "index mismatch"); });
    suite.Run("filename case follows filesystem", [&]()
              {
        std::string expected;
        const bool exists = Util::ReadFile(root + "/INDEX.html", expected);
        const Response response = Exchange(port, Wire("GET", "/INDEX.html"));
        Check(response.status == (exists ? 200 : 404), "filesystem case mismatch");
        if (exists) Check(response.body == expected, "case alias body mismatch"); });
    for (const std::string &path : std::vector<std::string>{"/performance.html", "/api/status"})
        suite.Run("HEAD " + path, [&, path]()
                  {
            const Response get = Exchange(port, Wire("GET", path));
            const Response head = Exchange(port, Wire("HEAD", path), true);
            Check(head.status == get.status && head.body.empty() && head.headers.at("content-length") ==
                  std::to_string(get.body.size()), "HEAD body or length mismatch"); });
    suite.Run("GET JSON", [&]()
              {
        const Response response = Exchange(port, Wire("GET", "/api/status"));
        Check(response.status == 200 && response.body == status_body, "JSON mismatch");
        Check(response.headers.at("content-type") == "application/json; charset=utf-8", "JSON MIME mismatch"); });
    suite.Run("query UTF-8 and escaped separators", [&]()
              {
        const Response response = Exchange(port, Wire("GET", "/api/query?name=%E4%BD%A0%E5%A5%BD+HTTP&tag=a%26b%3D1"));
        Check(response.body == "name=你好 HTTP\ntag=a&b=1\n", "query decode mismatch"); });
    for (const std::string &method : std::vector<std::string>{"POST", "PUT"})
        suite.Run(method + " binary body", [&, method]()
                  {
            const std::string path = method == "POST" ? "/api/items" : "/api/items/42";
            const Response response = Exchange(port, Wire(method, path, true, std::string("a\0b\xff", 4)));
            Check(response.status == 200 && response.body == method + " bytes=4\n", "method/body mismatch"); });
    suite.Run("DELETE numeric regex", [&]()
              {
        Check(Exchange(port, Wire("DELETE", "/api/items/123")).body == "DELETE accepted\n", "DELETE mismatch");
        Check(Exchange(port, Wire("DELETE", "/api/items/abc")).status == 404, "regex accepted letters");
        Check(Exchange(port, Wire("DELETE", "/api/items/42/extra")).status == 404, "regex matched partial path"); });
    suite.Run("redirect", [&]()
              {
        const Response response = Exchange(port, Wire("GET", "/redirect"));
        Check(response.status == 302 && response.headers.at("location") == "/index.html", "redirect mismatch"); });
    suite.Run("missing path and wrong method", [&]()
              {
        Check(Exchange(port, Wire("GET", "/missing-page")).body == "Not Found\n", "404 mismatch");
        Check(Exchange(port, Wire("POST", "/api/status")).status == 404, "POST reached GET route"); });
    suite.Run("HTTP/1.0 default close", [&]()
              {
        const Response response = Exchange(port, "GET /api/status HTTP/1.0\r\n\r\n");
        Check(response.version == "HTTP/1.0" && response.body == status_body, "HTTP/1.0 mismatch"); });
    suite.Run("HTTP/1.0 keep-alive reuse", [&]()
              {
        Client client(port);
        client.Send("GET /api/status HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
        const Response response = client.Read();
        Check(response.version == "HTTP/1.0" && response.headers.at("connection") == "keep-alive", "keep-alive lost");
        client.Send(Wire("GET", "/api/status"));
        Check(client.Read().body == status_body, "reuse failed"); client.Eof(); });
    for (const std::string &connection : std::vector<std::string>{"keep-alive, ClOsE", "keep-alive\r\nconnection: close"})
        suite.Run("Connection token parsing", [&, connection]()
                  { Check(Exchange(port, "GET /api/status HTTP/1.1\r\nHost: a\r\nConnection: " + connection + "\r\n\r\n").body == status_body,
                          "close token failed"); });
    suite.Run("sequential and mixed HEAD pipeline", [&]()
              {
        Client client(port);
        client.Send(Wire("GET", "/api/status", false));
        Check(client.Read().headers.at("connection") == "keep-alive", "default keep-alive failed");
        client.Send(Wire("HEAD", "/api/status", false) + Wire("GET", "/api/status", false) + Wire("GET", "/api/status"));
        Check(client.Read(true).body.empty(), "HEAD pipeline has body");
        Check(client.Read().body == status_body && client.Read().body == status_body, "pipeline mismatch"); client.Eof(); });
    suite.Run("fragmented line, headers and body", [&]()
              {
        Client client(port);
        const std::vector<std::string> fragments = {"PO", "ST /api/items HTTP/1.1\r", "\nHost: a\r\nContent-Len",
            "gth: 11\r\nConnection: close\r\n\r\nhello", " world"};
        for (const std::string &fragment : fragments)
        { client.Send(fragment); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        Check(client.Read().body == "POST bytes=11\n", "fragmented request failed"); client.Eof(); });

    // 暂不支持 Expect：客户端只发请求头、等待确认时，也必须立即得到 417。
    // 一秒接收超时短于测试服务的三秒空闲超时，避免把空闲关闭误判为主动拒绝。
    const std::vector<std::pair<std::string, std::string> > expectations = {
        {"100-continue", "Expect: 100-continue"},
        {"mixed case", "eXpEcT: 100-CoNtInUe"},
        {"unknown value", "Expect: unsupported"},
        {"empty value", "Expect:"},
        {"duplicate fields", "Expect: 100-continue\r\nexpect: unsupported"}};
    for (const std::pair<std::string, std::string> &expectation : expectations)
    {
        suite.Run("reject Expect " + expectation.first, [&, expectation]()
        {
            Client client(port, 1);
            client.Send("POST /api/items HTTP/1.1\r\nHost: a\r\n" + expectation.second +
                        "\r\nContent-Length: 10\r\n\r\n");
            const Response response = client.Read();
            Check(response.status == 417 && response.body == "Expectation Failed\n",
                  "Expect was not rejected before receiving the body");
            Check(response.headers.at("connection") == "close", "Expect rejection kept connection alive");
            client.Eof();
        });
    }
    suite.Run("fragmented Expect headers", [&]()
    {
        Client client(port, 1);
        client.Send("POST /api/items HTTP/1.1\r\nHost: a\r\nExpect: 100-con");
        pollfd event = {client.Fd(), POLLIN, 0};
        Check(poll(&event, 1, 10) == 0, "Expect rejected before request headers were complete");
        client.Send("tinue\r\nContent-Length: 10\r\n\r\n");
        const Response response = client.Read();
        Check(response.status == 417 && response.headers.at("connection") == "close",
              "fragmented Expect did not produce a closing 417 response");
        client.Eof();
    });
    suite.Run("Expect rejects already received body and following pipeline", [&]()
    {
        Client client(port);
        // 正文和下一条请求已到达时，仍应拒绝本次请求并丢弃后续数据。
        client.Send("POST /api/items HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\n"
                    "Content-Length: 10\r\n\r\n0123456789" + Wire("GET", "/api/status"));
        const Response response = client.Read();
        Check(response.status == 417 && response.headers.at("connection") == "close",
              "Expect reached the handler or kept the pipeline alive");
        client.Eof();
        Check(Exchange(port, Wire("GET", "/api/status")).body == status_body,
              "server unavailable after rejecting Expect");
    });
    suite.Run("HEAD with Expect has no response body", [&]()
    {
        const Response response = Exchange(port,
            "HEAD /api/status HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\n"
            "Content-Length: 10\r\n\r\n", true);
        Check(response.status == 417 && response.body.empty() &&
              response.headers.at("content-length") == std::to_string(std::string("Expectation Failed\n").size()),
              "HEAD Expect error response was incorrect");
    });
    suite.Run("HTTP/1.0 ignores Expect", [&]()
    {
        const Response response = Exchange(port,
            "POST /api/items HTTP/1.0\r\nExpect: 100-continue\r\n"
            "Content-Length: 10\r\n\r\n0123456789");
        Check(response.version == "HTTP/1.0" && response.status == 200 && response.body == "POST bytes=10\n",
              "HTTP/1.0 Expect changed normal body handling");
    });

    suite.Run("large POST then GET boundary", [&]()
              {
        Client client(port);
        client.Send(Wire("POST", "/api/items", false, std::string(256 * 1024, 'X')) + Wire("GET", "/api/status"));
        Check(client.Read().body == "POST bytes=262144\n", "large POST mismatch");
        Check(client.Read().body == status_body, "body consumed next request"); client.Eof(); });
    suite.Run("response close discards remaining pipeline", [&]()
              {
        Client client(port);
        client.Send(Wire("GET", "/api/close", false) + Wire("GET", "/api/status"));
        Check(client.Read().body == "closing\n", "response close failed"); client.Eof(); });

    // 表驱动错误测试：每项都要求明确的错误状态、完整响应和关闭连接。
    struct BadRequest
    {
        std::string name, wire;
        int status;
    };
    const std::vector<BadRequest> bad = {
        {"missing Host", "GET / HTTP/1.1\r\n\r\n", 400},
        {"empty Host", "GET / HTTP/1.1\r\nHost: \r\n\r\n", 400},
        {"duplicate Host", "GET / HTTP/1.1\r\nHost: a\r\nhost: b\r\n\r\n", 400},
        {"bad method", "PATCH / HTTP/1.1\r\nHost: a\r\n\r\n", 400},
        {"bad version", "GET / HTTP/2.0\r\n\r\n", 400},
        {"bare LF", "GET / HTTP/1.1\nHost: a\n\n", 400},
        {"bad header name", "GET / HTTP/1.1\r\nHost: a\r\nBad Key: x\r\n\r\n", 400},
        {"folded header", "GET / HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n", 400},
        {"negative length", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: -1\r\n\r\n", 400},
        {"signed length", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: +1\r\n\r\n", 400},
        {"length suffix", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 1x\r\n\r\n", 400},
        {"empty length", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: \r\n\r\n", 400},
        {"length overflow", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 18446744073709551616\r\n\r\n", 400},
        {"duplicate length", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\ncontent-length: 1\r\n\r\nx", 400},
        {"TE plus CL", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\nx", 400},
        {"unsupported chunked", "POST /api/items HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", 501},
        {"Expect with missing Host", "POST /api/items HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 10\r\n\r\n", 400},
        {"Expect with invalid length", "POST /api/items HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\nContent-Length: -1\r\n\r\n", 400},
        {"Expect with oversized body", "POST /api/items HTTP/1.1\r\nHost: a\r\nExpect: 100-continue\r\nContent-Length: 16777217\r\n\r\n", 413},
        {"bad escape", Wire("GET", "/bad%ZZ"), 400},
        {"traversal", Wire("GET", "/../CMakeLists.txt"), 400},
        {"encoded traversal", Wire("GET", "/%2e%2e/CMakeLists.txt"), 400},
        {"NUL path", Wire("GET", "/index.html%00suffix"), 400},
        {"oversized body", "POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 16777217\r\n\r\n", 413},
        {"oversized request line", "GET /" + std::string(8192, 'a'), 414},
        {"oversized header line", "GET / HTTP/1.1\r\nHost: a\r\nX: " + std::string(8192, 'a'), 431}};
    for (const BadRequest &item : bad)
        suite.Run("reject " + item.name, [&, item]()
                  { const Response response = Exchange(port, item.wire); Check(response.status == item.status, "wrong error status"); });
    suite.Run("reject total headers over 64 KiB", [&]()
              {
        std::string wire = "GET / HTTP/1.1\r\nHost: a\r\n";
        for (int i = 0; i < 80; ++i) wire += "X: " + std::string(900, 'a') + "\r\n";
        Check(Exchange(port, wire).status == 431, "total header limit failed"); });

    const size_t basic_cases = suite.cases, basic_failures = suite.failures;
    std::cout << "[HTTP BASIC SUMMARY] cases=" << basic_cases << ", failures=" << basic_failures << std::endl;

    // 第二部分：finaltest。超时、隔离、传输和压力测试集中在此
    // 超时窗口包含时间轮一秒刻度误差；压力的客户端/时长固定，结果可重复比较。
    const std::function<void()> finaltest = [&]()
    {
        for (int kind = 0; kind < 3; ++kind)
            suite.Run("finaltest idle/partial timeout " + std::to_string(kind), [&, kind]()
                      {
                Client client(port);
                if (kind == 1) client.Send("GET /api/status HTTP/1.1\r\nHost: ");
                if (kind == 2) client.Send("POST /api/items HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\nx");
                const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
                client.Eof();
                const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
                std::cout << "[TIMEOUT] kind=" << kind << " elapsed_ms=" << ms << std::endl;
                Check(ms >= 1500 && ms < 5000, "3-second idle timeout outside expected window"); });
        suite.Run("finaltest partial input refresh then idle close", [&]()
                  {
            ServerProcess isolated(root, 2);
            Client client(isolated.port);
            client.Send("GET /api/status HTTP/1.1\r\nX-Slow: ");
            for (int i = 0; i < 6; ++i)
            {
                // 按已确定的策略：持续有数据就保持活跃，不限制接收总时长。
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                pollfd event = {client.Fd(), POLLIN, 0};
                int ready;
                do { ready = poll(&event, 1, 0); } while (ready < 0 && errno == EINTR);
                Check(ready == 0, "partial request closed while input remained active");
                client.Send("a");
            }
            // 停止发送后应由空闲计时器回收，时间轮允许一秒刻度误差。
            const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            client.Eof();
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
            std::cout << "[TIMEOUT] active_partial_ms=3600 idle_close_ms=" << ms << std::endl;
            Check(ms >= 900 && ms < 3500, "2-second idle timeout outside expected window"); });
        suite.Run("finaltest active keep-alive refresh", [&]()
                  {
            Client client(port);
            for (int i = 0; i < 4; ++i)
            {
                client.Send(Wire("GET", "/api/status", false));
                Check(client.Read().body == status_body, "active connection expired early");
                std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            }
            client.Send(Wire("GET", "/api/status"));
            Check(client.Read().body == status_body, "refreshed connection failed"); client.Eof(); });
        suite.Run("finaltest 100 ordered pipeline requests", [&]()
                  {
            Client client(port);
            std::string wire;
            for (int i = 0; i < 100; ++i)
                wire += Wire("GET", "/api/query?name=" + std::to_string(i), i == 99);
            client.Send(wire);
            for (int i = 0; i < 100; ++i)
                Check(client.Read().body == "name=" + std::to_string(i) + "\ntag=\n", "pipeline order/body mismatch");
            client.Eof(); });
        suite.Run("finaltest 16 MiB upload and following request", [&]()
                  {
            Client client(port);
            client.Send(Wire("POST", "/api/items", false, std::string(16 * 1024 * 1024, 'U')) + Wire("GET", "/api/status"));
            Check(client.Read().body == "POST bytes=16777216\n", "maximum body boundary failed");
            Check(client.Read().body == status_body, "maximum body consumed next request"); client.Eof(); });
        suite.Run("finaltest large binary response", [&]()
                  {
            const Response response = Exchange(port, Wire("GET", "/_test/large"));
            Check(response.status == 200 && response.body.size() == 2 * 1024 * 1024, "large body truncated");
            for (size_t i = 0; i < response.body.size(); ++i)
                Check(static_cast<unsigned char>(response.body[i]) == i % 251, "binary bytes corrupted"); });
        suite.Run("finaltest slow reader of existing large HTML", [&]()
                  {
            std::string expected;
            Check(Util::ReadFile(root + "/performance.html", expected) && expected.size() > 240 * 1024,
                  "large HTML fixture missing");
            Client client(port);
            client.Send(Wire("GET", "/performance.html"));
            // 先延迟读取，触发服务端排队；随后核对完整文件和 EOF。
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            Check(client.Read().body == expected, "slow-reader file truncated"); client.Eof(); });
        suite.Run("finaltest 16 concurrent distinct clients", [&]()
                  {
            std::string expected;
            Check(Util::ReadFile(root + "/performance.html", expected), "large HTML read failed");
            std::vector<std::future<void> > clients;
            for (int i = 0; i < 16; ++i)
                clients.push_back(std::async(std::launch::async, [&, i]()
                {
                    for (int n = 0; n < 3; ++n)
                    {
                        const std::string name = std::to_string(i) + "-" + std::to_string(n);
                        Check(Exchange(port, Wire("GET", "/api/query?name=" + name)).body == "name=" + name + "\ntag=\n",
                              "concurrent responses crossed");
                        Check(Exchange(port, Wire("GET", "/performance.html")).body == expected, "concurrent file truncated");
                    }
                }));
            for (std::future<void> &client : clients) client.get(); });
        suite.Run("finaltest processing timeout and worker isolation", [&]()
                  {
            ServerProcess isolated(root, 2, 1);
            Client slow(isolated.port), fast(isolated.port);
            const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
            slow.Send(Wire("GET", "/_test/slow"));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            fast.Send(Wire("GET", "/api/status"));
            bool fast_ok = false;
            try { fast_ok = fast.Read().body == status_body; fast.Eof(); }
            catch (const std::exception &) {}
            fast.Close();
            const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
            slow.Close();
            std::cout << "[PROCESSING] slow_handler_ms=3500 idle_timeout_ms=2000 fast_completed_ms=" << ms << std::endl;
            Check(fast_ok && ms < 1000, "slow handler blocked fast request or made it expire; idle timeout did not interrupt processing"); });
        suite.Run("finaltest handler catches its own exception", [&]()
                  {
            ServerProcess isolated(root);
            const Response response = Exchange(isolated.port, Wire("GET", "/_test/throw"));
            Check(response.status == 200 && response.body == "handler caught exception\n",
                  "handler did not return its locally handled exception response");
            // 再建立新连接，确认本次异常处理后服务仍然可用。
            Check(Exchange(isolated.port, Wire("GET", "/api/status")).body == status_body,
                  "server unavailable after handler caught its own exception");
            const int status = isolated.Stop();
            Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                  "server teardown failed after locally handled exception; wait_status=" + std::to_string(status)); });
        suite.Run("finaltest response header injection rejection", [&]()
                  {
                    const Response response =
                        Exchange(port, Wire("GET", "/_test/header"));

                    Check(response.status == 500,
                        "invalid response header did not produce HTTP 500");
                    Check(response.body == "Internal Server Error\n",
                        "invalid response header leaked the original body");
                    Check(response.headers.count("x-injected") == 0,
                        "injected header reached the client");

                    // 检查 HEAD 错误响应不发送正文。
                    const Response head =
                        Exchange(port, Wire("HEAD", "/_test/header"), true);
                    Check(head.status == 500 && head.body.empty(),
                        "HEAD error response was incorrect");

                    // 第一条请求出错后，不应继续响应同一连接中的第二条请求。
                    Client client(port);
                    client.Send(Wire("GET", "/_test/header", false) +
                                Wire("GET", "/api/status"));

                    Check(client.Read().status == 500,
                        "pipeline did not receive HTTP 500");
                    client.Eof();

                    // 当前连接关闭后，服务仍应正常处理其他连接。
                    Check(Exchange(port, Wire("GET", "/api/status")).body == status_body,
                        "server unavailable after rejecting invalid response header"); });

        suite.Run("finaltest bounded pipeline output memory", [&]()
                  {
            ServerProcess isolated(root);
            Client client(isolated.port);
            int small = 1024;
            Check(setsockopt(client.Fd(), SOL_SOCKET, SO_RCVBUF, &small, sizeof(small)) == 0, "small receive buffer failed");
            const long before = RssKiB(isolated.Pid());
            std::string wire;
            for (int i = 0; i < 32; ++i) wire += Wire("GET", "/_test/large", false);
            client.Send(wire);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            const long growth = RssKiB(isolated.Pid()) - before;
            client.Close();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::cout << "[MEMORY] 32x2MiB pipeline rss_growth_kib=" << growth << std::endl;
            // 32 MiB 是测试的资源预算，不是 HTTP 协议要求。
            Check(growth < 32 * 1024, "output has no backpressure/high-water limit; RSS exceeded test budget"); });
        suite.Run("finaltest stop with active connection", [&]()
                  {
            ServerProcess isolated(root, 30);
            Client client(isolated.port);
            client.Send(Wire("GET", "/api/status", false));
            Check(client.Read().body == status_body, "active connection setup failed");
            const int status = isolated.Stop();
            Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                  "active-connection shutdown crashed/hung; wait_status=" + std::to_string(status)); });
        for (int clients : std::vector<int>{10, 50, 100})
            suite.Run("finaltest webbench " + std::to_string(clients), [&, clients]()
                      {
                const std::string path = clients == 100 ? "/performance.html" : "/";
                std::string expected;
                Check(Util::ReadFile(root + (path == "/" ? "/index.html" : path), expected), "stress fixture read failed");
                Check(Exchange(port, Wire("GET", path)).body == expected, "pre-stress response mismatch");
                Webbench(port, clients, 3, path, root);
                Check(Exchange(port, Wire("GET", path)).body == expected, "post-stress response mismatch"); });
    };
    finaltest();
    std::cout << "[HTTP FINALTEST SUMMARY] cases=" << suite.cases - basic_cases
              << ", failures=" << suite.failures - basic_failures << std::endl;
    // 正常用例关闭客户端后，给已排队的服务器回收任务一个调度机会。
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    suite.Run("server stop after all clients close", [&]()
              {
        const int status = server.Stop();
        Check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "server teardown failed"); });
    std::cout << "[HTTP SERVER SUMMARY] cases=" << suite.cases << ", failures=" << suite.failures << std::endl;
    Check(suite.failures == 0, "HTTP server test failures: " + std::to_string(suite.failures));
}

int main()
{
    // testtimerfd();
    // testregex();
    // testAny();
    // testlog();
    // testserver();
    // testsocket();
    // testchannel_poller();
    // testeventloop_timewheel();
    // testconnection();
    // testacceptor();
    // testloopthreadpoll();
    // testtcpserver();
    // testwebbench();
    // testutil();
    // testhttp();
    try
    {
        testhttpserver();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "[FAIL] test: " << error.what() << std::endl;
        return 1;
    }
}
