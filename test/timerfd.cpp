#include <sys/timerfd.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <iostream>
using namespace std;

void testtimerfd()
{
    int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timerfd < 0)
    {
        cerr << "timerfd create failed" << endl;
        return;
    }

    struct itimerspec itime;
    itime.it_interval.tv_sec = 1;
    itime.it_interval.tv_nsec = 0; // 设置第一次超时之后间隔
    itime.it_value.tv_nsec = 0;
    itime.it_value.tv_sec = 1;
    timerfd_settime(timerfd, 0, &itime, nullptr);

    while (1)
    {
        unsigned long long times;
        int ret = read(timerfd, &times, 8); // 读取累计未处理的到期次数，内核计数器每到期一次就+1，读取后清零。
        if (ret < 0)
        {
            cerr << "read failed" << endl;
            break;
        }
    }
    close(timerfd);
    return;
}

int main()
{
    testtimerfd();
    return 0;
}
