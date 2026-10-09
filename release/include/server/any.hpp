#pragma once
#include <typeinfo>
#include <algorithm>

class Any
{
private:
    class holder
    {
    public:
        virtual ~holder() {}                            // 保证通过父类指针释放子类对象时正确析构
        virtual const std::type_info &type() const = 0; // 获取实际保存的数据类型
        virtual holder *clone() const = 0;              // 克隆当前实际子类对象
    };

    template <class T>
    class placeholder : public holder
    {
    public:
        placeholder(const T &val) : _val(val) {}
        ~placeholder() override // 销毁具体的 placeholder<T> 对象
        {
        }

        const std::type_info &type() const override // 返回当前保存的 T 类型
        {
            return typeid(T);
        }

        holder *clone() const override // 复制当前 placeholder<T> 对象
        {
            return new placeholder<T>(_val);
        }

    public:
        T _val; // 真正保存的数据
    };

    holder *_content; // 这里保存的是holder的指针，实际上统一指向不同的 placeholder<T> 对象

public:
    Any() : _content(nullptr) // 构造一个空 Any
    {
    }

    ~Any() // 释放内部保存的对象
    {
        delete _content;
    }
    Any(const Any &other) : _content(other._content ? other._content->clone() : nullptr) // 深拷贝另一个 Any
    {
    }

    Any &swap(Any &swp)
    {
        std::swap(_content, swp._content);
        return *this;
    }

    Any &operator=(const Any &other) // 拷贝另一个 Any
    {
        Any(other).swap(*this);
        return *this;
    }

    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) // 将任意类型数据包装成 placeholder<T>
    {
    }

    template <class T>
    T *get() // 获取内部保存的 T 类型数据
    {
        // 判断获取于保存的类型是否一致，如果不一致返回为空
        if (_content == nullptr)
            return nullptr;
        else
            return typeid(T) == _content->type() ? &(((placeholder<T> *)_content)->_val) : nullptr;
    }

    template <class T>
    Any &operator=(const T &val) // 将任意类型数据赋值给 Any
    {
        // 将旧数据放到临时对象中，结束自动释放
        Any(val).swap(*this);
        return *this;
    }
};
