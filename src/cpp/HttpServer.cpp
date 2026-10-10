#include "HttpServer.hpp"
#include "HttpContext.hpp"
#include "log.hpp"

HttpServer::HttpServer(int port, int timeout) : _server(port)
{
    _server.EnableInactiveRelease(timeout);
    _server.SetConnectedCallback(std::bind(&HttpServer::OnConnected, this, std::placeholders::_1));
    _server.SetMessageCallback(std::bind(&HttpServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    _server.SetCloseCallback(std::bind(&HttpServer::OnClose, this, std::placeholders::_1));
    _server.SetAnyCallback(std::bind(&HttpServer::OnAny, this, std::placeholders::_1));
    INF_LOG("HttpServer 已创建，端口=%d", port);
}

void HttpServer::Start()
{
    INF_LOG("HttpServer 启动");
    _server.StartServer();
}

void HttpServer::Stop()
{
    INF_LOG("HttpServer 停止主循环");
    _server.StopServer();
}

void HttpServer::SetThreadCount(int count)
{
    _server.SetThreadCount(count);
}

void HttpServer::EnableInactiveRelease(uint64_t timeout)
{
    _server.EnableInactiveRelease(timeout);
    INF_LOG("HttpServer 请求启用非活跃释放，超时=%llu 秒", static_cast<unsigned long long>(timeout));
}

void HttpServer::SetStaticDir(const std::string &dir)
{
    if (dir.empty())
    {
        _static_dir.clear();
        INF_LOG("HttpServer 已关闭静态资源服务");
        return;
    }

    char resolved[PATH_MAX];
    if (dir.find('\0') != std::string::npos || !realpath(dir.c_str(), resolved) || !Util::IsDirectory(resolved))
    {
        // 检查路径，存在\0或跳转到静态资源目录之外或目录错误
        ERR_LOG("静态资源目录无效：%s", dir.c_str());
        throw std::invalid_argument("Invalid static resource directory");
    }
    _static_dir = resolved;
    INF_LOG("设置静态资源根目录：%s", _static_dir.c_str());
}

void HttpServer::AddGetRoute(const std::string &path, const Handler &handler)
{
    if (path.empty() || !handler)
        throw std::invalid_argument("Invalid GET handler");

    // 正则表达式只在注册时编译一次
    const std::regex pattern(path);
    _get_route[path] = std::make_pair(pattern, handler);
    INF_LOG("注册 GET 路由：%s", path.c_str());
}

void HttpServer::AddPostRoute(const std::string &path, const Handler &handler)
{
    if (path.empty() || !handler)
        throw std::invalid_argument("Invalid POST route");
    const std::regex pattern(path);
    _post_route[path] = std::make_pair(pattern, handler);
    INF_LOG("注册 POST 路由：%s", path.c_str());
}

void HttpServer::AddPutRoute(const std::string &path, const Handler &handler)
{
    if (path.empty() || !handler)
        throw std::invalid_argument("Invalid PUT route");
    const std::regex pattern(path);
    _put_route[path] = std::make_pair(pattern, handler);
    INF_LOG("注册 PUT 路由：%s", path.c_str());
}

void HttpServer::AddDeleteRoute(const std::string &path, const Handler &handler)
{
    if (path.empty() || !handler)
        throw std::invalid_argument("Invalid DELETE route");
    const std::regex pattern(path);
    _delete_route[path] = std::make_pair(pattern, handler);
    INF_LOG("注册 DELETE 路由：%s", path.c_str());
}

void HttpServer::Dispatcher(HttpRequest &request, HttpResponse &response)
{
    // HEAD 复用 GET 的处理结果，发送时省略正文。
    const Handlers *routes = NULL;
    if (request._method == "GET" || request._method == "HEAD")
        routes = &_get_route;
    else if (request._method == "POST")
        routes = &_post_route;
    else if (request._method == "PUT")
        routes = &_put_route;
    else if (request._method == "DELETE")
        routes = &_delete_route;
    else
    {
        response._statu = 405; // 不支持的请求方法
        return;
    }
    // 使用已编译的正则匹配路径，并保存本次请求的捕获结果。
    for (auto it = routes->begin(); it != routes->end(); ++it)
    {
        if (!std::regex_match(request._path, request._matches, it->second.first))
            continue;
        DBG_LOG("命中动态路由：%s %s，规则=%s", request._method.c_str(), request._path.c_str(), it->first.c_str());
        it->second.second(request, response);
        return;
    }
    response._statu = 404;
    DBG_LOG("未找到路由：%s %s", request._method.c_str(), request._path.c_str());
}

bool HttpServer::FileHandler(const HttpRequest &request, HttpResponse &response)
{
    if (_static_dir.empty() || (request._method != "GET" && request._method != "HEAD"))
        return false;
    if (request._path.empty() || request._path[0] != '/' || !Util::ValidPath(request._path))
    {
        response._statu = 403; // Forbidden，请求被拒绝
        return true;
    }
    std::string filename = _static_dir + request._path;
    if (Util::IsDirectory(filename))
        filename += "/index.html";

    char resolved[PATH_MAX];
    if (!realpath(filename.c_str(), resolved)) // 转为实际路径。包括软连接
        return false;

    filename = resolved;
    const std::string prefix = _static_dir == "/" ? "/" : _static_dir + "/";
    if (filename.compare(0, prefix.size(), prefix) != 0) // 检查是否存在软连接跳转到静态资源目录之外
    {
        response._statu = 403;
        ERR_LOG("拒绝访问静态目录外的资源：%s", filename.c_str());
        return true;
    }

    if (!Util::IsRegular(filename))
        return false;

    std::string content;
    if (!Util::ReadFile(filename, content))
    {
        response._statu = 500;
        ERR_LOG("读取静态文件失败：%s", filename.c_str());
        return true;
    }
    std::string type = Util::ExtMime(filename);
    response.SetContent(content, type);
    DBG_LOG("返回静态文件：%s，字节数=%zu", filename.c_str(), content.size());
    return true;
}

void HttpServer::Route(HttpRequest &request, HttpResponse &response)
{
    // 先尝试静态资源，未找到时再分发到动态处理函数。
    if (!FileHandler(request, response))
        Dispatcher(request, response);

    if (response._statu >= 400 && response._body.empty())
    {
        std::string body = Util::StatusDesc(response._statu) + "\n";
        std::string type = "text/plain; charset=utf-8";
        response.SetContent(body, type);
    }
}

bool HttpServer::IsValidResponseHeader(const std::string &name, const std::string &value)
{
    if (name.empty())
        return false;

    // HTTP 字段名允许字母、数字和这些符号。
    const std::string symbols = "!#$%&'*+-.^_`|~";
    for (unsigned char ch : name)
    {
        const bool letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
        const bool digit = ch >= '0' && ch <= '9';

        if (!letter && !digit && symbols.find(ch) == std::string::npos)
            return false;
    }

    // 字段值禁止 CR、LF、NUL 等控制字符，允许水平制表符。
    for (unsigned char ch : value)
    {
        if ((ch < 0x20 && ch != '\t') || ch == 0x7f)
            return false;
    }

    return true;
}

bool HttpServer::SendResponse(const ConnPtr &conn, const HttpResponse &response, bool head_only)
{
    // 必须先完成检查，再构造响应，避免混入原响应的部分内容。
    for (const auto &header : response._headers)
    {
        if (IsValidResponseHeader(header.first, header.second))
            continue;

        ERR_LOG("拒绝发送非法 HTTP 响应头，连接=%llu", static_cast<unsigned long long>(conn->GetConnId()));

        const std::string body = "Internal Server Error\n";
        std::ostringstream error;
        error << response._version << " 500 Internal Server Error\r\n"
              << "Connection: close\r\n"
              << "Content-Type: text/plain; charset=utf-8\r\n"
              << "Content-Length: " << body.size() << "\r\n"
              << "\r\n";

        if (!head_only)
            error << body;

        conn->Send(error.str());
        return false;
    }

    std::ostringstream message;
    message << response._version << " " << response._statu << " " << Util::StatusDesc(response._statu) << "\r\n";
    const bool no_body = response._statu < 200 || response._statu == 204 || response._statu == 304;
    for (auto it = response._headers.begin(); it != response._headers.end(); ++it)
    {
        if (strcasecmp(it->first.c_str(), "Content-Length") == 0 ||
            strcasecmp(it->first.c_str(), "Transfer-Encoding") == 0) // 长度需要重新计算，Transfer-Encoding不考虑
            continue;
        message << it->first << ": " << it->second << "\r\n";
    }
    if (!no_body)
        message << "Content-Length: " << response._body.size() << "\r\n";
    message << "\r\n"; // 响应头结束
    // HEAD 仍返回 GET 正文的长度，但不发送正文数据。
    if (!head_only && !no_body)
        message << response._body;
    conn->Send(message.str());
    DBG_LOG("发送 HTTP 响应，连接=%llu，状态=%d，正文长度=%zu", static_cast<unsigned long long>(conn->GetConnId()), response._statu, response._body.size());
    return true;
}

void HttpServer::OnConnected(const ConnPtr &conn)
{
    conn->GetContext() = HttpContext();
    INF_LOG("HTTP 连接建立，id=%llu", static_cast<unsigned long long>(conn->GetConnId()));
}

void HttpServer::OnMessage(const ConnPtr &conn, Buffer &buffer)
{
    // Shutdown 可能再次调用消息回调，正在关闭的连接不再分发请求。
    if (conn->GetConnStatu() != CONNECTED)
        return;

    HttpContext *context = conn->GetContext().get<HttpContext>(); // 转换为 HttpContext 类型
    if (context == NULL)
    {
        ERR_LOG("HTTP 连接缺少上下文，id=%llu", static_cast<unsigned long long>(conn->GetConnId()));
        conn->Shutdown();
        return;
    }
    while (buffer.ReadAbleSize() > 0)
    {
        context->RecvHttpRequest(buffer);
        if (context->RecvStatu() != RECV_HTTP_OVER && context->RecvStatu() != RECV_HTTP_ERROR)
            return; // 请求未完整，等待后续数据

        HttpRequest &request = context->Request();
        HttpResponse response;
        response._version = request._version.empty() ? "HTTP/1.1" : request._version; // 为空时默认使用 HTTP/1.1
        bool close = context->RecvStatu() == RECV_HTTP_ERROR;
        if (close)
        {
            response._statu = context->RespStatu();
            std::string body = Util::StatusDesc(response._statu) + "\n";
            std::string type = "text/plain; charset=utf-8";
            response.SetContent(body, type);
            ERR_LOG("HTTP 解析失败，连接=%llu，状态=%d", static_cast<unsigned long long>(conn->GetConnId()), response._statu);
        }
        else
        {
            INF_LOG("HTTP 请求：%s %s", request._method.c_str(), request._path.c_str());
            Route(request, response);
            close = close || request.Close() || response.Close();
        }
        std::string key = "Connection", value = close ? "close" : "keep-alive";
        response.SetHeader(key, value);
        bool valid_response = SendResponse(conn, response, request._method == "HEAD"); // 为head获取响应信息，但不接受正文

        if (close || !valid_response)
        {
            buffer.clear();
            conn->Shutdown();
            return;
        }
        context->Clear(); // 清理上下文继续处理下一个请求
    }
}

void HttpServer::OnClose(const ConnPtr &conn)
{
    INF_LOG("HTTP 连接关闭，id=%llu", static_cast<unsigned long long>(conn->GetConnId()));
}

void HttpServer::OnAny(const ConnPtr &conn)
{
    TRACE_LOG("HTTP 连接事件，id=%llu", static_cast<unsigned long long>(conn->GetConnId()));
}
