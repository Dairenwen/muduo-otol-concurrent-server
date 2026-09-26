#include <sys/timerfd.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include "timewheel.hpp"
#include <regex>
#include "server.hpp"
#include "buffer.hpp"
#include <assert.h>
#include "any.hpp"
#include "log.hpp"
#include "socket.hpp"
#include "channel.hpp"
#include "poller.hpp"
#include <vector>
#include <memory>
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

int main()
{
    // testtimerfd();
    // testtimewheel();
    // testregex();
    // testAny();
    // testlog();
    // testserver();
    // testsocket();
    testchannel_poller();
    return 0;
}
