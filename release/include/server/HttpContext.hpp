#pragma once
#include "HttpRequest.hpp"
#include "util.hpp"
#include "buffer.hpp"
#include <algorithm>
#include <charconv>

enum HttpRecvStatu
{
    RECV_HTTP_ERROR, // 解析失败，停止接收本请求
    RECV_HTTP_LINE,  // 等待完整请求行
    RECV_HTTP_HEAD,  // 接收请求头
    RECV_HTTP_BODY,  // 按 Content-Length 接收正文
    RECV_HTTP_OVER   // 一条请求已完整解析
};

class HttpContext
{
private:
    int _resp_statu;           // 正常为 200，失败时记录对应的 HTTP 错误状态码。
    HttpRecvStatu _recv_statu; // 当前解析阶段
    HttpRequest _request;      // 已解析的请求数据
    size_t _head_size;         // 已消费的请求头字节数
    size_t _body_length;       // 校验后的 Content-Length

    // 用正则解析包含 CRLF 的请求行，填充方法、路径、版本和查询参数。
    bool ParseHttpLine(const std::string &line);
    // 读取并解析请求行，成功后进入请求头阶段。
    bool RecvHttpLine(Buffer &buffer);
    // 逐行解析请求头，读到空行后进入正文阶段。
    bool RecvHttpHead(Buffer &buffer);
    // 解析单个头字段，校验字段格式及 Content-Length。
    bool ParseHttpHead(const std::string &line);
    // 累积正文
    bool RecvHttpBody(Buffer &buffer);
    // 读取完整 CRLF 行；请求行保留 CRLF，头字段去掉 CRLF。
    bool ReadLine(Buffer &buffer, std::string &line);
    // 保存错误码并进入错误状态
    bool SetError(int status);

public:
    HttpContext();
    // 获取响应状态
    int RespStatu() const;
    // 获取解析阶段
    HttpRecvStatu RecvStatu() const;
    // 获取请求对象
    HttpRequest &Request();
    // 从调用者的缓冲区推进解析
    void RecvHttpRequest(Buffer &buffer);
    void Clear();
};
