#include "HttpContext.hpp"
#include <regex>

const size_t MAX_LINE_SIZE = 8 * 1024;         // 单行上限，包含结尾 CRLF。
const size_t MAX_HEAD_SIZE = 64 * 1024;        // 全部请求头及结束空行的上限。
const size_t MAX_BODY_SIZE = 16 * 1024 * 1024; // 正文上限。

HttpContext::HttpContext()
{
    Clear();
}

int HttpContext::RespStatu() const { return _resp_statu; }
HttpRecvStatu HttpContext::RecvStatu() const { return _recv_statu; }
HttpRequest &HttpContext::Request() { return _request; }

void HttpContext::Clear()
{
    // 仅重置解析上下文，缓冲区中下一条请求的数据由调用者保留。
    _resp_statu = 200;
    _recv_statu = RECV_HTTP_LINE;
    _head_size = 0;
    _body_length = 0;
    _request.Clear();
}

bool HttpContext::SetError(int status)
{
    _resp_statu = status;
    _recv_statu = RECV_HTTP_ERROR;
    return false;
}

bool HttpContext::ReadLine(Buffer &buffer, std::string &line)
{
    char *end = buffer.FindCRLF();
    const size_t length = end == nullptr ? buffer.ReadAbleSize() : (size_t)(end - buffer.GetReaderPtr() + 1);
    // 即使尚未收到换行也检查长度，请求行过长返回 414，头字段过长返回 431。
    if (length > MAX_LINE_SIZE)
        return SetError(_recv_statu == RECV_HTTP_LINE ? 414 : 431);
    // 将当前未消费的行也计入限制，避免多个合法短行组成超大的请求头。
    if (_recv_statu == RECV_HTTP_HEAD && length > MAX_HEAD_SIZE - _head_size)
        return SetError(431);

    if (end == nullptr)
        return false; // 暂时保存

    // Buffer 查找的是 LF；先验证 CRLF，避免单个 LF 导致长度下溢。
    if (length < 2 || end[-1] != '\r')
        return SetError(400);
    
    line = buffer.GetLine();
    if (_recv_statu == RECV_HTTP_HEAD)
    {
        line.resize(line.size() - 2); // 头字段去掉 CRLF
        _head_size += length;
    }
    return true;
}

bool HttpContext::ParseHttpLine(const std::string &line)
{
    // 捕获组依次为原串、方法、路径、查询串、版本及 CRLF 后的内容。
    static const std::regex pattern("(GET|HEAD|POST|PUT|DELETE) ([^?\\s]*)(?:\\?([^\\s]*))? (HTTP/1\\.[01])\\r\\n([\\s\\S]*)");
    std::smatch matches;

    if (!std::regex_match(line, matches, pattern))
        return SetError(400);

    const std::string target = matches[2].str();
    const std::string query = matches[3].str();
    // 当前解析器处理 /path?query 形式的请求目标。
    if (target.empty() || target.front() != '/' ||
        target.find('#') != std::string::npos ||
        query.find('#') != std::string::npos ||
        std::any_of(target.begin(), target.end(), // 不能出现控制或空白字符
                    [](unsigned char ch)
                    { return ch <= 32 || ch == 127; }))
        return SetError(400);

    std::string path;
    // 路径和查询参数都需要 URL 解码；但只有查询参数中的 `+` 通常转换为空格，路径中的 `+` 应保留为普通加号。
    if (!Util::UrlDecode(target, path) || !Util::ValidPath(path))
        return SetError(400);

    _request._method = matches[1].str();
    _request._path = std::move(path);
    _request._version = matches[4].str();

    if (matches[3].matched)
    {
        // 先按原始分隔符拆分，再解码
        std::vector<std::string> params;
        Util::Split(query, "&", params);
        for (const std::string &param : params)
        {
            // 只按第一个 '=' 拆分；没有 '=' 的参数使用""，'+' 解码为空格。
            const size_t equal = param.find('=');
            std::string key, value;
            if (!Util::UrlDecode(param.substr(0, equal), key, true) ||
                !Util::UrlDecode(equal == std::string::npos ? "" : param.substr(equal + 1), value, true))
                return SetError(400);

            _request.SetParam(key, value);
        }
    }
    return true;
}

bool HttpContext::RecvHttpLine(Buffer &buffer)
{
    std::string line;
    if (!ReadLine(buffer, line) || !ParseHttpLine(line))
        return false;
    _recv_statu = RECV_HTTP_HEAD;
    return true;
}

bool HttpContext::ParseHttpHead(const std::string &line)
{
    // HTTP 方法和头字段名允许的字符；空格、冒号和控制字符不属于 token。
    auto IsToken = [&](unsigned char ch)
    {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') ||
               std::string("!#$%&'*+-.^_`|~").find(ch) != std::string::npos;
    };

    // 字段格式为 name: value；名称不能为空，也不能包含空白或控制字符。
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0 || !std::all_of(line.begin(), line.begin() + colon, IsToken))
        return SetError(400);

    std::string key = line.substr(0, colon);
    // 统一头字段名称， Content-Length
    bool upper = true;
    for (char &ch : key)
    {
        if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');
        if (upper && ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
        upper = ch == '-';
    }

    // 只去掉值两端的空格和 TAB，保留值内部的空白。
    const size_t begin = line.find_first_not_of(" \t", colon + 1), end = line.find_last_not_of(" \t");
    const std::string value = begin == std::string::npos ? "" : line.substr(begin, end - begin + 1);

    if (std::any_of(value.begin(), value.end(), [](unsigned char ch)
                    { return (ch < 32 && ch != '\t') || ch == 127; }))
        return SetError(400);

    if (key == "Content-Length")
    {
        // 严格解析十进制长度，拒绝符号、尾随字符、溢出和重复长度字段。
        size_t length = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), length);
        if (value.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != value.data() + value.size() ||
            _request.HasHeader(key))
            return SetError(400);

        // 在接收正文之前拒绝超大长度，避免持续分配正文内存。
        if (length > MAX_BODY_SIZE)
            return SetError(413);

        _body_length = length;
    }

    // Host 不允许为空或重复；普通重复字段按逗号连接保存。
    if (key == "Host" && (value.empty() || _request.HasHeader(key)))
        return SetError(400);

    if (_request.HasHeader(key))
        _request.SetHeader(key, _request.GetHeader(key) + ", " + value);
    else
        _request.SetHeader(key, value);
    return true;
}

bool HttpContext::RecvHttpHead(Buffer &buffer)
{
    std::string line;
    while (ReadLine(buffer, line))
    {
        // CRLF 空行标志请求头结束
        if (line.empty())
        {
            // HTTP/1.1 要求提供 Host；HTTP/1.0 不作此要求。
            if (_request._version == "HTTP/1.1" && !_request.HasHeader("Host"))
                return SetError(400);

            // 不支持 Transfer-Encoding，同时出现请求错误，未实现返回501
            if (_request.HasHeader("Transfer-Encoding"))
                return SetError(_request.HasHeader("Content-Length") ? 400 : 501);
            _recv_statu = RECV_HTTP_BODY;
            return true;
        }

        if (!ParseHttpHead(line))
            return false;
    }
    return false;
}

bool HttpContext::RecvHttpBody(Buffer &buffer)
{
    if (_recv_statu != RECV_HTTP_BODY)
        return false;
    if (_request._body.size() > _body_length)
        return SetError(400);

    // 当前正文还缺多少字节。
    const size_t remaining = _body_length - _request._body.size();
    const size_t count = std::min(remaining, static_cast<size_t>(buffer.ReadAbleSize()));

    if (count > 0)
        _request._body.append(buffer.ReadAsStringAndPop(count));

    // Body 还没有收完整，等待下一次 socket 数据到来。
    if (_request._body.size() < _body_length)
        return false;

    _recv_statu = RECV_HTTP_OVER;
    return true;
}

void HttpContext::RecvHttpRequest(Buffer &buffer)
{
    // 依次完成请求行、头部和正文
    if (_recv_statu == RECV_HTTP_LINE && !RecvHttpLine(buffer))
        return;
    if (_recv_statu == RECV_HTTP_HEAD && !RecvHttpHead(buffer))
        return;
    if (_recv_statu == RECV_HTTP_BODY)
        RecvHttpBody(buffer);
}
