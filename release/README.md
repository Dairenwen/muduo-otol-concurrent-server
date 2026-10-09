# muduo-otol-concurrent-server

## 平台要求

- Linux；项目使用 epoll、eventfd 和 timerfd。
- C++17 编译器、CMake 3.16 或更新版本，以及线程库。
- 当前目录中的二进制在 Ubuntu 24.04 / aarch64（ARM64）环境构建。
  其他 CPU 架构应在对应 Linux 环境重新编译；这些二进制不能直接在 macOS 上运行。

## 从源码构建

在项目根目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
cmake --install build --prefix "$PWD/release"
```

构建生成 `release/server` 与 `release/lib` 下的动静态库。
安装命令同步头文件、网页、README 和 LICENSE，不自动打包或清空目录。
修改源码后，重复以上命令即可覆盖同名产物。

## 启动服务器

**先进入 release 目录，再启动：**

```sh
cd release
./server
```

如果拿到的是独立的发布目录：

```sh
cd /absolute/path/to/release
./server
```

浏览器打开 `http://127.0.0.1:8080/`。服务器在前台运行，按 Ctrl+C 停止。

默认设置在 `src/cpp/main.cpp` 中：端口 **8080**、工作线程 **3**、空闲超时 **30 秒**，
静态资源目录为当前工作目录下的 `./wwwroot`。
需要调整端口、线程数或资源路径时，修改入口中的配置，再重新构建。
当前入口没有 `--port`、`--threads`、`--wwwroot` 或 `--help` 命令行参数。

容器内启动时，浏览器访问需要容器端口映射或转发；也可以在容器内部验证：

```sh
curl -i http://127.0.0.1:8080/api/status
curl -I http://127.0.0.1:8080/
```

如果提示静态目录无效，检查是否在 release 目录启动，以及 wwwroot 是否存在。
如果监听失败，检查 8080 是否已被其他进程占用。

## 目录内容

```text
release/
├── server
├── lib/
│   ├── libserver.a
│   ├── libserver.so -> libserver.so.0
│   ├── libserver.so.0 -> libserver.so.0.1.0
│   └── libserver.so.0.1.0
├── include/server/    # 公共头文件及其依赖
├── wwwroot/           # 网页、CSS、JavaScript
├── README.md
└── LICENSE
```

`server` 静态链接本项目库，运行它不需要额外加载 libserver.so，但仍依赖 Linux 系统运行库。
`.so` 的两个版本软链接指向同一份动态库，不能当作重复文件随意删掉。

## 在其他 C++ 程序中使用库

同时提供头文件与库，不依赖 CMake 包导入配置。
在你自己的程序目录中，新建一个最小链接示例 `example.cpp`：

```cpp
#include "util.hpp"
#include <iostream>

int main()
{
    std::cout << Util::StatusDesc(200) << '\n';
    return 0;
}
```

下面的 `/absolute/path/to/release` 请替换为发布目录的真实路径。

静态链接：

```sh
g++ -std=c++17 -I/absolute/path/to/release/include/server \
    example.cpp /absolute/path/to/release/lib/libserver.a \
    -pthread -o example_static
./example_static
```

动态链接：

```sh
g++ -std=c++17 -I/absolute/path/to/release/include/server \
    example.cpp -L/absolute/path/to/release/lib -lserver \
    -pthread -o example_shared
LD_LIBRARY_PATH=/absolute/path/to/release/lib ./example_shared
```

两种示例均输出 `OK`。动态链接程序运行时必须能找到对应的共享库；
上面的 LD_LIBRARY_PATH 为这次启动指定查找目录。
库使用者也应采用兼容的 Linux 架构和 C++ 运行环境。

## 使用已有网页测试

入口通过 `SetStaticDir("./wwwroot")` 提供已有静态文件，同时为状态页和表单页注册四种方法的动态路由。

| 路径 | 用途 |
| --- | --- |
| `/`、`/index.html` | 首页 |
| `/docs/` | 子目录首页 |
| `/examples/table.html` | 资源列表与浏览器筛选 |
| `/performance.html` | 超过 240 KiB 的静态文件传输 |
| `/404.html` | 错误页面外观示例，实际状态为 200 |
| `/missing-page` | 不存在的资源，实际状态为 404 |

wwwroot 包含 12 个 HTML 文件和 2 个公共资源文件。
`assets/style.css` 为页面提供样式；`assets/app.js` 提供表格筛选、复制等浏览器交互。
HTML 页面是不同的测试样例，并非每一个都是服务器启动所必需。

状态页与表单页使用以下接口；这些是动态路径，不需要对应的磁盘文件。

| 方法 | 路径 | 对应前端 |
| --- | --- | --- |
| GET / HEAD | `/api/status` | status.html 的检查按钮、请求实验 |
| GET | `/api/query` | examples/form.html 的查询表单 |
| POST | `/api/items` | examples/form.html 的 POST 表单、请求实验 |
| PUT | `/api/items/42` | examples/form.html 的请求实验 |
| DELETE | `/api/items/42` | examples/form.html 的请求实验 |

POST / PUT 返回声明的正文字节数，DELETE 仅演示方法分发，不写入或删除文件。
没有注册 `/redirect`、`/api/close` 或其他未被现有前端实际使用的接口。
自动 HTTP 测试在自己的服务端注册测试路由，与正式入口独立。

## 运行测试

回到项目根目录执行：

```sh
ctest --test-dir build --output-on-failure
```

目前 HTTP 测试有两个已知失败：HTTP/1.0 显式 keep-alive 被关闭、Connection 多值头未按 token 判断。
这些库实现未在本次简化中修改。测试入口未捕获异常时会显示 Aborted。
