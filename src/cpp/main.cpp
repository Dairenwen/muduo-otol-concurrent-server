#include "HttpServer.hpp"
#include <iostream>
#include <string>

int main()
{
    try
    {
        // 在 release 中运行 ./server；静态页面无需单独注册路由。
        HttpServer server(8080);
        server.SetStaticDir("./wwwroot");
        server.SetThreadCount(3);

        auto text = [](HttpResponse &response, std::string body)
        {
            std::string type = "text/plain; charset=utf-8";
            response.SetContent(body, type);
        };
        // status.html 的检查按钮使用此接口。
        server.AddGetRoute("/api/status", [](const HttpRequest &, HttpResponse &response)
                           {
                                std::string body = "{\"status\":\"ok\",\"server\":\"muduo-otol\"}\n";
                                std::string type = "application/json; charset=utf-8";
                                response.SetContent(body, type); });
        // form.html 的 GET 表单使用此接口；getter 非 const，因此读取副本。
        server.AddGetRoute("/api/query", [text](const HttpRequest &request, HttpResponse &response)
                           {
                                HttpRequest copy(request);
                                std::string name = "name", tag = "tag";
                                text(response, "name=" + copy.GetParam(name) + "\ntag=" + copy.GetParam(tag) + "\n"); });
        server.AddPostRoute("/api/items", [text](const HttpRequest &request, HttpResponse &response)
                            {
                                // 暂时不提供正文 getter，仅展示声明的字节长度。
                                HttpRequest copy(request);
                                text(response, "POST bytes=" + std::to_string(copy.ContentLength()) + "\n"); });
        // form.html 请求实验中的 PUT / DELETE 选项使用这个固定示例地址。
        server.AddPutRoute("/api/items/42", [text](const HttpRequest &request, HttpResponse &response)
                           {
                                HttpRequest copy(request);
                                text(response, "PUT bytes=" + std::to_string(copy.ContentLength()) + "\n"); });
        server.AddDeleteRoute("/api/items/42", [text](const HttpRequest &, HttpResponse &response)
                              { text(response, "DELETE accepted\n"); });

        std::cout << "Server: http://127.0.0.1:8080/\nPress Ctrl+C to stop.\n"
                  << std::flush;
        server.Start();
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Server startup failed: " << error.what() << '\n';
        return 1;
    }
}
