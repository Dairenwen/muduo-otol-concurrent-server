#include "util.hpp"

size_t Util::Split(const std::string &str, const std::string &sep, std::vector<std::string> &result)
{
    result.clear(); // 覆盖结果，避免保留上一次分割的数据。
    // 空分隔符表示不分割
    if (sep.empty())
    {
        if (!str.empty())
            result.push_back(str);
        return result.size();
    }
    size_t begin = 0;
    while (begin < str.size())
    {
        const size_t end = str.find(sep, begin);
        // 没有下一个分隔符时，剩余内容就是最后一项。
        if (end == std::string::npos)
        {
            result.push_back(str.substr(begin));
            break;
        }
        // 忽略开头和连续分隔符产生的空项；sep 按完整字符串匹配。
        if (end != begin)
            result.push_back(str.substr(begin, end - begin));
        begin = end + sep.size();
    }
    return result.size();
}

bool Util::ReadFile(const std::string &filename, std::string &content)
{
    if (!IsRegular(filename))
        return false;
    // 二进制读取，用于文本、图片等文件
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
        return false;
    // 读到 EOF；先保存临时结果，读取失败时不修改调用者的内容。
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad())
        return false;
    content = std::move(data); // 移动赋值给content
    return true;
}

bool Util::WriteFile(const std::string &filename, const std::string &content)
{
    if (filename.find('\0') != std::string::npos)
        return false;
    // 二进制并覆盖已有内容
    std::ofstream file(filename, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return false;
    file.write(content.data(), content.size());
    file.close();
    return !file.fail();
}

bool Util::UrlEncode(const std::string &url, std::string &result, bool convert_space)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;
    // 按无符号字节处理，避免 UTF-8 等非 ASCII 字节变成负数用于数组索引。
    for (unsigned char ch : url)
    {
        // 字母、数字及 - _ . ~ 原样保留，其余字节使用百分号编码。
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            encoded.push_back(static_cast<char>(ch));
        // 表单编码模式用 '+' 表示空格；原始 '+' 仍会被编码为 %2B。
        else if (ch == ' ' && convert_space)
            encoded.push_back('+');
        else
        {
            encoded.push_back('%');
            // 一个字节拆成高、低两个 4 位数，生成 %HH，例如 '/' -> %2F。
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    result = std::move(encoded);
    return true;
}

bool Util::UrlDecode(const std::string &url, std::string &result, bool convert_plus)
{
    // 将十六进制字符转换为 0～15
    auto HexValue = [](unsigned char ch)
    {
        if (ch >= '0' && ch <= '9')
            return ch - '0';
        if (ch >= 'a' && ch <= 'f')
            return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
            return ch - 'A' + 10;
        return -1;
    };

    std::string decoded;
    for (size_t i = 0; i < url.size(); ++i)
    {
        if (url[i] == '%')
        {
            // '%' 后必须紧跟两个合法十六进制字符，拒绝截断或错误的编码。
            if (url.size() - i < 3)
                return false;
            const int high = HexValue(url[i + 1]), low = HexValue(url[i + 2]);
            if (high < 0 || low < 0)
                return false;
            // 高 4 位左移后与低 4 位合并，还原原始字节
            decoded.push_back((char)((high << 4) | low));
            i += 2;
        }
        else
            decoded.push_back(url[i] == '+' && convert_plus ? ' ' : url[i]);
    }
    result = std::move(decoded);
    return true;
}

std::string Util::StatusDesc(int status)
{
    static const std::unordered_map<int, std::string> descriptions = {
        {100, "Continue"}, {101, "Switching Protocols"}, {102, "Processing"}, {103, "Early Hints"}, {200, "OK"}, {201, "Created"}, {202, "Accepted"}, {203, "Non-Authoritative Information"}, {204, "No Content"}, {205, "Reset Content"}, {206, "Partial Content"}, {207, "Multi-Status"}, {208, "Already Reported"}, {226, "IM Used"}, {300, "Multiple Choices"}, {301, "Moved Permanently"}, {302, "Found"}, {303, "See Other"}, {304, "Not Modified"}, {305, "Use Proxy"}, {307, "Temporary Redirect"}, {308, "Permanent Redirect"}, {400, "Bad Request"}, {401, "Unauthorized"}, {402, "Payment Required"}, {403, "Forbidden"}, {404, "Not Found"}, {405, "Method Not Allowed"}, {406, "Not Acceptable"}, {407, "Proxy Authentication Required"}, {408, "Request Timeout"}, {409, "Conflict"}, {410, "Gone"}, {411, "Length Required"}, {412, "Precondition Failed"}, {413, "Payload Too Large"}, {414, "URI Too Long"}, {415, "Unsupported Media Type"}, {416, "Range Not Satisfiable"}, {417, "Expectation Failed"}, {418, "I'm a teapot"}, {421, "Misdirected Request"}, {422, "Unprocessable Entity"}, {423, "Locked"}, {424, "Failed Dependency"}, {425, "Too Early"}, {426, "Upgrade Required"}, {428, "Precondition Required"}, {429, "Too Many Requests"}, {431, "Request Header Fields Too Large"}, {451, "Unavailable For Legal Reasons"}, {500, "Internal Server Error"}, {501, "Not Implemented"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}, {504, "Gateway Timeout"}, {505, "HTTP Version Not Supported"}, {506, "Variant Also Negotiates"}, {507, "Insufficient Storage"}, {508, "Loop Detected"}, {510, "Not Extended"}, {511, "Network Authentication Required"}};
    const auto it = descriptions.find(status);
    return it == descriptions.end() ? "Unknown" : it->second;
}

std::string Util::ExtMime(const std::string &filename)
{
    static const std::unordered_map<std::string, std::string> types = {
        {".html", "text/html"},
        {".htm", "text/html"},
        {".css", "text/css"},
        {".txt", "text/plain"},
        {".csv", "text/csv"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".gz", "application/gzip"},
        {".wasm", "application/wasm"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".webp", "image/webp"},
        {".bmp", "image/bmp"},
        {".avif", "image/avif"},
        {".mp3", "audio/mpeg"},
        {".wav", "audio/wav"},
        {".ogg", "audio/ogg"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".xhtml", "application/xhtml+xml"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".xul", "application/vnd.mozilla.xul+xml"},
        {".3gp", "video/3gpp"},
        {".3g2", "video/3gpp2"},
        {".7z", "application/x-7z-compressed"}};

    // 从最后一个点号开始提取扩展名，没有对应的 MIME 类型时，统一按照通用二进制数据处理
    const size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos)
    {
        return "application/octet-stream";
    }

    const auto it = types.find(filename.substr(pos));
    if (it == types.end())
    {
        return "application/octet-stream";
    }
    return it->second;
}

bool Util::IsDirectory(const std::string &path)
{
    if (path.find('\0') != std::string::npos)
        return false;
    struct stat info{};
    // stat 跟随符号链接；先确认查询成功，再检查目标是否为目录。
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool Util::IsRegular(const std::string &path)
{
    if (path.find('\0') != std::string::npos) // 文件名中包含\0直接判为非法
        return false;
    struct stat info{};
    // 普通文件不包括目录、管道和设备文件；符号链接按其目标类型判断。
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool Util::ValidPath(const std::string &path)
{
    if (path.empty() || path.find('\0') != std::string::npos || path.find('\\') != std::string::npos)
        return false;
    std::vector<std::string> parts;
    Split(path, "/", parts);
    size_t depth = 0; // 目录深度
    for (const std::string &part : parts)
    {
        if (part == "..")
        {
            // '..':在根目录继续向上意味越界
            if (depth == 0)
                return false;
            --depth;
        }
        else if (part != ".")
            ++depth;
    }
    return true;
}
