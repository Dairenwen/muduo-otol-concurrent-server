#pragma once
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cctype>

class HttpResponse
{
private:
    int _statu;          // HTTP 响应状态码
    bool _redirect_flag; // 是否进行重定向

    std::string _version;      // HTTP 协议版本
    std::string _body;         // HTTP 响应正文
    std::string _redirect_url; // 重定向目标地址

    std::unordered_map<std::string, std::string> _headers; // 响应头字段

public:
    HttpResponse() : _statu(200), _redirect_flag(false), _version("HTTP/1.1") {}
    ~HttpResponse() = default;

    // 重置响应对象
    void ReSet();

    // 设置响应头
    void SetHeader(std::string &key, std::string &val);

    // 判断指定响应头是否存在
    bool HasHeader(std::string &key);

    // 获取指定响应头的值
    std::string GetHeader(std::string &key);

    // 设置响应正文和正文类型
    void SetContent(std::string &body, std::string &type);

    // 设置重定向地址，默认使用 302 状态码
    void SetRedirect(std::string &url, int statu = 302);

    // 判断响应发送完成后是否需要关闭连接
    bool Close();
};
