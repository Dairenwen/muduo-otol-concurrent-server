#include "HttpRequest.hpp"
#include "util.hpp"
#include <strings.h>

void HttpRequest::SetHeader(const std::string &key, const std::string &val)
{
    _headers[Util::NormalizeHeaderName(key)] = val;
}

bool HttpRequest::HasHeader(const std::string &key)
{
    return _headers.find(Util::NormalizeHeaderName(key)) != _headers.end();
}

std::string HttpRequest::GetHeader(std::string &key)
{
    auto it = _headers.find(Util::NormalizeHeaderName(key));

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

    // 长度字段名称不区分大小写，保留原有头字段存储方式。
    auto it = std::find_if(_headers.begin(), _headers.end(), [&](const auto &header)
                           { return strcasecmp(header.first.c_str(),
                                               key.c_str()) == 0; });

    // 没有 Content-Length，说明没有正文
    if (it == _headers.end())
    {
        return 0;
    }

    // stoull 接受符号和尾随字符，转换前要求非空且全部为数字。
    if (it->second.empty() ||
        !std::all_of(it->second.begin(), it->second.end(),
                     [](unsigned char ch)
                     {
                         return ch >= '0' && ch <= '9';
                     }))
    {
        throw std::invalid_argument("Invalid Content-Length");
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
    bool has_close = false;
    bool has_keep_alive = false;

    for (const auto &header : _headers)
    {
        // HTTP 头字段名称不区分大小写。
        if (strcasecmp(header.first.c_str(), "Connection") != 0)
            continue;

        std::istringstream values(header.second);
        std::string token;

        // Connection 可以包含多个逗号分隔的值
        while (std::getline(values, token, ','))
        {
            const size_t first = token.find_first_not_of(" \t");
            if (first == std::string::npos)
                continue;

            const size_t last = token.find_last_not_of(" \t");
            token = token.substr(first, last - first + 1);

            std::transform(token.begin(), token.end(), token.begin(),
                           [](unsigned char ch)
                           {
                               return static_cast<char>(std::tolower(ch));
                           });

            has_close = has_close || token == "close";
            has_keep_alive = has_keep_alive || token == "keep-alive";
        }
    }

    if (has_close)
        return true;

    if (_version == "HTTP/1.1")
        return false; // 默认长连接。

    if (_version == "HTTP/1.0")
        return !has_keep_alive; // 显式 keep-alive 才保持连接

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
