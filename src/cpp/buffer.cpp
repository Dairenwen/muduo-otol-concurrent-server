#include "buffer.hpp"

Buffer::Buffer()
    : _reader_ptr(0), _writer_ptr(0), _buffer(BUFFER_SIZE)
{
}
char *Buffer::Begin()
{
    return (char *)&*_buffer.begin();
}
char *Buffer::GetWriterPtr()
{
    return Begin() + _writer_ptr;
}
char *Buffer::GetReaderPtr()
{
    return Begin() + _reader_ptr;
}
uint64_t Buffer::TailIdleSize()
{
    return _buffer.size() - _writer_ptr;
}
uint64_t Buffer::HeadIdleSize()
{
    return _reader_ptr;
}
uint64_t Buffer::ReadAbleSize()
{
    return _writer_ptr - _reader_ptr;
}
void Buffer::MoveReaderPtr(uint64_t len)
{
    assert(len <= ReadAbleSize());
    _reader_ptr += len;
}
void Buffer::MoveWriterPtr(uint64_t len)
{
    assert(len <= TailIdleSize());
    _writer_ptr += len;
}
void Buffer::EnsureSpace(uint64_t len)
{
    if (TailIdleSize() >= len)
        return;
    else if (TailIdleSize() + HeadIdleSize() >= len)
    {
        uint64_t rsz = ReadAbleSize();
        std::copy(GetReaderPtr(), GetReaderPtr() + rsz, Begin()); // 若头部还有空间，则拷贝过去
        _writer_ptr = rsz;
        _reader_ptr = 0;
    }
    else
        _buffer.resize(_writer_ptr + len);
}
void Buffer::Write(const char *data, uint64_t len)
{
    // 先检查空间是否足够
    EnsureSpace(len);
    std::copy(data, data + len, GetWriterPtr());
}
void Buffer::WriteAndPush(const char *data, uint64_t len)
{
    Write(data, len);
    MoveWriterPtr(len);
}
void Buffer::WriteString(const std::string &data)
{
    return Write(data.c_str(), data.size());
}
void Buffer::WriteStringAndPush(const std::string &data)
{
    WriteString(data);
    MoveWriterPtr(data.size());
}

void Buffer::WriteBuffer(Buffer &data)
{
    return Write(data.GetReaderPtr(), data.ReadAbleSize());
}
void Buffer::WriteBufferAndPush(Buffer &data)
{
    WriteBuffer(data);
    MoveWriterPtr(data.ReadAbleSize());
}
void Buffer::Read(char *data, uint64_t len)
{
    assert(len <= ReadAbleSize());
    std::copy(GetReaderPtr(), GetReaderPtr() + len, data);
}
std::string Buffer::ReadAsString(uint64_t len)
{
    assert(len <= ReadAbleSize());
    std::string ret;
    ret.resize(len);
    Read(&ret[0], len);
    return ret;
}
std::string Buffer::ReadAsStringAndPop(uint64_t len)
{
    std::string ret = ReadAsString(len);
    MoveReaderPtr(len);
    return ret;
}
char *Buffer::FindCRLF()
{
    return (char *)memchr(GetReaderPtr(), '\n', ReadAbleSize()); // 找到换行符的位置
}
std::string Buffer::GetLine()
{
    char *pos = FindCRLF();

    if (pos == nullptr)
        return "";

    return ReadAsStringAndPop(pos - GetReaderPtr() + 1);
}
void Buffer::clear()
{
    _reader_ptr = 0;
    _writer_ptr = 0;
}
