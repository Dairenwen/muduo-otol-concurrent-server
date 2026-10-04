#include "eventloop.hpp"
#include "timewheel.hpp"
#include "server.hpp"
#include "buffer.hpp"
#include "any.hpp"
#include "log.hpp"
#include "socket.hpp"
#include "channel.hpp"
#include "poller.hpp"
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
                         write(result_pipe[1], &result, sizeof(result));
                         _exit(passed ? 0 : 1); });

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
    testutil();
    return 0;
}
