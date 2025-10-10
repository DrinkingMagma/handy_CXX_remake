#include "event_base.h"
#include "conn.h"
#include "logger.h"
#include "poller.h"
#include "thread_pool.h"
#include "conn.h"

namespace handy
{
    // 内部结构体定义（匿名命名空间，隐藏实现）
    namespace 
    {
        // 可重复定时器结构体（存储重读定时器的核心信息）
        struct TimerRepeatable
        {
            int64_t at;             // 下一次超时时间戳（毫秒）
            int64_t interval_ms;    // 定时器重复间隔（毫秒）
            TimerId timerId;        // 当前周期的定时器ID（用于取消）
            Task task;              // 定时器触发时执行的任务（回调函数）
        };
    } // namespace 
    
}   // namespace handy