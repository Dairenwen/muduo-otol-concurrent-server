#include "HttpResponse.hpp"
#include <strings.h>
#include <sstream>
#include <strings.h>

void HttpResponse::ReSet()
{
    _statu = 200;           // 默认响应成功
    _redirect_flag = false; // 默认不重定向

    _body.clear();         // 清空响应正文
    _redirect_url.clear(); // 清空重定向地址
    _headers.clear();      // 清空所有响应头
}

void HttpResponse::SetHeader(std::string &key, std::string &val)
{
    _headers[key] = val;
}

bool HttpResponse::HasHeader(std::string &key)
{
    return _headers.find(key) != _headers.end();
}

std::string HttpResponse::GetHeader(std::string &key)
{
    auto it = _headers.find(key);

    if (it == _headers.end())
    {
        return "";
    }

    return it->second;
}

void HttpResponse::SetContent(std::string &body, std::string &type)
{
    // 保存正文
    _body = body;

    // 设置正文类型
    _headers["Content-Type"] = type;

    // Content-Length 表示正文的字节数
    _headers["Content-Length"] = std::to_string(_body.size());
}

/**
 * @brief 设置 HTTP 重定向

 * 默认使用 302 临时重定向。
 * SetRedirect("/login");
 * 最终对应：
 * HTTP/1.1 302 Found
 * Location: /login
 */
void HttpResponse::SetRedirect(std::string &url, int statu)
{
    // 设置响应状态码
    _statu = statu;

    // 标记当前响应为重定向
    _redirect_flag = true;

    // 保存重定向目标地址
    _redirect_url = url;

    // HTTP 重定向真正通过 Location 响应头告诉客户端目标地址
    _headers["Location"] = url;
}

bool HttpResponse::Close()
{
    // 响应端只在显式 close 时关闭。
    for (const auto &header : _headers)
    {
        if (strcasecmp(header.first.c_str(), "Connection") != 0)
            continue;

        std::istringstream values(header.second);
        std::string token;

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

            if (token == "close")
                return true;
        }
    }

    return false;
}
