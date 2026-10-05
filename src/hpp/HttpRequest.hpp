#pragma once
#include <string>
#include <unordered_map>
#include <regex>
#include <algorithm>
#include <cctype>
#include <stdexcept>

class HttpRequest
{
private:
    std::string _method;  // 请求方法
    std::string _path;    // 资源路径
    std::string _version; // HTTP 协议版本
    std::string _body;    // 请求正文

    std::unordered_map<std::string, std::string> _params;  // 查询参数
    std::unordered_map<std::string, std::string> _headers; // 请求头

    std::smatch _matches; // 正则路由匹配结果
public:
    HttpRequest() = default;
    ~HttpRequest() = default;

    // 设置请求头字段，SetHeader("Content-Type", "application/json");
    void SetHeader(const std::string &key, const std::string &val);

    // 判断指定请求头是否存在
    bool HasHeader(const std::string &key);

    // 获取指定请求头的值
    std::string GetHeader(std::string &key);

    // 设置 URL 查询参数
    void SetParam(std::string &key, std::string &val);

    // 判断指定查询参数是否存在
    bool HasParam(std::string &key);

    // 获取指定查询参数的值
    std::string GetParam(std::string &key);

    // 获取请求正文长度
    size_t ContentLength();

    // 判断当前 HTTP 请求处理完成后是否应该关闭连接
    bool Close();

    void Clear();
};
