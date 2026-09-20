#pragma once
#include <vector>
#include <assert.h>
#include <stdint.h>
#include <cstring>
#include <string>
const int BUFFER_SIZE = 100;

class Buffer
{
private:
    std::vector<char> _buffer; // 缓冲区数组
    uint64_t _reader_ptr;      // 读取指针
    uint64_t _writer_ptr;      // 写入指针
public:
    Buffer();
    char *Begin();
    char *GetWriterPtr();                       // 获取写入指针
    char *GetReaderPtr();                       // 获取读取指针
    uint64_t TailIdleSize();                    // 获取尾部空间
    uint64_t HeadIdleSize();                    // 获取头部空间
    uint64_t ReadAbleSize();                    // 总空闲空间
    void MoveReaderPtr(uint64_t len);           // 移动读取指针
    void MoveWriterPtr(uint64_t len);           // 移动写入指针
    void EnsureSpace(uint64_t len);             // 确保有足够空间
    void Write(const char *data, uint64_t len); // 写入数据
    void WriteString(const std::string &data);
    void WriteBuffer(Buffer &data);
    void WriteAndPush(const char *data, uint64_t len);
    std::string ReadAsString(uint64_t len);
    std::string GetLine();
    void WriteStringAndPush(const std::string &data);
    void WriteBufferAndPush(Buffer &data);
    std::string ReadAsStringAndPop(uint64_t len);
    char *FindCRLF();
    void Read(char *data, uint64_t len); // 读取数据
    void clear();                        // 清空缓存
};
