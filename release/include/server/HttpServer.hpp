#pragma once
#include "server.hpp"
#include <regex>
#include "HttpRequest.hpp"
#include <cstdlib>
#include <limits.h>
#include <sstream>
#include <strings.h>
#include "HttpResponse.hpp"

#define DEFAULT_TIMEOUT 30 // 设置默认超时 30sec 防止恶意连接

class HttpServer
{
private:
    using Handler = std::function<void(const HttpRequest &, HttpResponse &)>;
    // 每项保存已编译的正则及其处理函数，匹配时不再重复编译。
    using Handlers = std::unordered_map<std::string, std::pair<std::regex, Handler>>;

    Handlers _get_route;     // get路由表
    Handlers _post_route;    // post路由表
    Handlers _put_route;     // put路由表
    Handlers _delete_route;  // delete路由表
    std::string _static_dir; // 静态资源目录
    TcpServer _server;       // TCP服务器
private:
    bool IsValidResponseHeader(const std::string &name, const std::string &value);                // 检查响应头是否非法含有\r\n
    void Route(HttpRequest &request, HttpResponse &response);                                     // 路由入口，选择静态资源或动态处理。
    bool FileHandler(const HttpRequest &request, HttpResponse &response);                         // 处理静态资源，未找到返回 false。
    void Dispatcher(HttpRequest &request, HttpResponse &response);                                // 按方法和路径查找动态路由并调用处理函数。
    bool SendResponse(const ConnPtr &conn, const HttpResponse &response, bool head_only = false); // HEAD 只发送头部
    void OnConnected(const ConnPtr &conn);                                                        // 连接建立时的回调
    void OnMessage(const ConnPtr &conn, Buffer &buffer);                                          // 收到数据时的回调
    void OnClose(const ConnPtr &conn);                                                            // 连接关闭时的回调
    void OnAny(const ConnPtr &conn);                                                              // 连接任意事件时的回调
public:
    HttpServer(int port, int timeout = DEFAULT_TIMEOUT);
    void Start();
    void Stop();
    void SetThreadCount(int count);
    void EnableInactiveRelease(uint64_t timeout);
    void SetStaticDir(const std::string &dir);
    void AddGetRoute(const std::string &path, const Handler &handler);
    void AddPostRoute(const std::string &path, const Handler &handler);
    void AddPutRoute(const std::string &path, const Handler &handler);
    void AddDeleteRoute(const std::string &path, const Handler &handler);
};
