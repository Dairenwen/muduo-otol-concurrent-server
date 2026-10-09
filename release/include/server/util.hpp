#pragma once
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unordered_map>
#include <utility>
#include <cstddef>
#include <string>
#include <vector>

class Util
{
public:
    // 按完整分隔字符串分割，忽略空项；覆盖 result，返回分割后的项数。
    static size_t Split(const std::string &str, const std::string &sep, std::vector<std::string> &result);

    // 读取文件内容
    static bool ReadFile(const std::string &filename, std::string &content);

    // 向文件写入数据
    static bool WriteFile(const std::string &filename, const std::string &content);

    // URL编码，convert_space 为 true 时将空格编码为 '+'，否则为 "%20"。
    static bool UrlEncode(const std::string &url, std::string &result, bool convert_space = false);

    // URL解码，convert_plus 为 true 时将 '+' 解码为空格；无效百分号编码返回 false。
    static bool UrlDecode(const std::string &url, std::string &result, bool convert_plus = false);

    // 响应状态码的描述信息获取
    static std::string StatusDesc(int status);

    // 根据文件后缀名获取文件mime
    static std::string ExtMime(const std::string &filename);

    // 判断一个文件是不是一个目录
    static bool IsDirectory(const std::string &path);

    // 判断一个文件是不是一个普通文件
    static bool IsRegular(const std::string &path);

    // http请求的资源路径有效性判断 对已 URL 解码的路径进行词法检查，拒绝越过资源根目录和 NULL
    static bool ValidPath(const std::string &path);
};
