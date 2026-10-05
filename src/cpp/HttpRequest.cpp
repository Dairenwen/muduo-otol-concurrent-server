#include "HttpRequest.hpp"

void HttpRequest::SetHeader(const std::string &key, const std::string &val)
{
    _headers[key] = val;
}

bool HttpRequest::HasHeader(const std::string &key)
{
    return _headers.find(key) != _headers.end();
}

std::string HttpRequest::GetHeader(std::string &key)
{
    auto it = _headers.find(key);

    // 没有找到对应请求头
    if (it == _headers.end())
    {
        return "";
    }

    return it->second;
}

void HttpRequest::SetParam(std::string &key, std::string &val)
{
    _params[key] = val;
}

bool HttpRequest::HasParam(std::string &key)
{
    return _params.find(key) != _params.end();
}

std::string HttpRequest::GetParam(std::string &key)
{
    auto it = _params.find(key);

    // 没有找到对应查询参数
    if (it == _params.end())
    {
        return "";
    }

    return it->second;
}

size_t HttpRequest::ContentLength()
{
    const std::string key = "Content-Length";

    auto it = _headers.find(key);

    // 没有 Content-Length，说明没有正文
    if (it == _headers.end())
    {
        return 0;
    }

    return static_cast<size_t>(std::stoull(it->second));
}

/*
 * HTTP/1.1：默认使用长连接（keep-alive）
 * 只有明确指定Connection: close 才关闭连接。
 * HTTP/1.0：默认使用短连接
 * 只有明确指定：Connection: keep-alive 才保持连接。
 */
bool HttpRequest::Close()
{
    std::string connection;

    auto it = _headers.find("Connection");

    if (it != _headers.end())
    {
        connection = it->second;

        // Connection 的值理论上不区分大小写，
        // 因此统一转换为小写后再判断。
        std::transform(connection.begin(), connection.end(), connection.begin(), [](unsigned char ch)
                       { return static_cast<char>(std::tolower(ch)); });
    }

    if (_version == "HTTP/1.1")
    {
        return connection == "close";
    }

    if (_version == "HTTP/1.0")
    {
        return connection != "keep-alive";
    }

    return true;
}

void HttpRequest::Clear()
{
    _method.clear();
    _path.clear();
    _version.clear();
    _body.clear();
    _params.clear();
    _headers.clear();
    _matches = std::smatch{};
}