#include "event_base.h"
#include "logger.h"
#include "utils.h"
#include <csignal>
#include <unistd.h>

using namespace handy;
static EventBase* g_base = nullptr;

void sigIntHandler(int sig)
{
    (void)sig;
    INFO("exit timer.cpp");
    if (g_base) g_base->exit();
}

void testAbsoluteTime(EventBase* base, int64_t startTime);
void testCancelTimer(EventBase* base, int64_t startTime);
void testMultipleTimers(EventBase* base, int64_t startTime);

int main(int argc, char *argv[]) 
{
    signal(SIGINT, sigIntHandler);

    Logger::getInstance().setLogLevel(Logger::LINFO);
    Logger::getInstance().setLogFileName("./logs/timer.log");
    Logger::getInstance().clear();

    std::unique_ptr<EventBase> base(new EventBase());
    g_base = base.get();

    INFO("========== Timer Examples Start ==========");
    int64_t startTime = Utils::timeMilli();

    base->runAfter(100, [startTime]() {
        int64_t now = Utils::timeMilli();
        INFO("1. runAfter(100ms): Executed after 100ms delay (elapsed: %ldms)", now - startTime);
    });

    base->runAfter(200, [startTime]() {
        int64_t now = Utils::timeMilli();
        INFO("2. runAfter(200ms): Executed after 200ms delay (elapsed: %ldms)", now - startTime);
    });

    base->runAfter(300, [startTime]() {
        int64_t now = Utils::timeMilli();
        INFO("3. runAfter(300ms): Executed after 300ms delay (elapsed: %ldms)", now - startTime);
    });

    int repeatCount = 0;
    static TimerId periodicTimerStatic;
    periodicTimerStatic = base->runAfter(500, [&repeatCount, startTime]() {
        repeatCount++;
        int64_t now = Utils::timeMilli();
        INFO("4. runAfter with interval(500ms): Executed %d times (elapsed: %ldms)", repeatCount, now - startTime);
        
        if (repeatCount >= 3) {
            INFO("   Cancelling periodic timer after 3 executions");
            g_base->cancel(periodicTimerStatic);
            
            g_base->runAfter(100, [startTime]() {
                int64_t now = Utils::timeMilli();
                INFO("5. After cancel: This runs 100ms after cancellation (elapsed: %ldms)", now - startTime);
                
                testAbsoluteTime(g_base, startTime);
            });
        }
    }, 500);

    base->loop();

    g_base = nullptr;
    INFO("========== Timer Examples End ==========");
    return 0;
}

void testAbsoluteTime(EventBase* base, int64_t startTime)
{
    INFO("========== Absolute Time Test Start ==========");
    
    int64_t now = Utils::timeMilli();
    int64_t futureTime = now + 300;
    
    base->runAt(futureTime, [startTime, futureTime]() {
        int64_t elapsed = Utils::timeMilli() - startTime;
        INFO("6. runAt(futureTime): Scheduled at %ld, executed at %ld (elapsed: %ldms)", 
             futureTime, Utils::timeMilli(), elapsed);
    });

    base->runAfter(200, [base, startTime]() {
        testCancelTimer(base, startTime);
    });
}

void testCancelTimer(EventBase* base, int64_t startTime)
{
    INFO("========== Cancel Timer Test ==========");
    
    static TimerId toCancelStatic;
    toCancelStatic = base->runAfter(100, []() {
        INFO("This should NOT be printed - timer was cancelled");
    });
    
    INFO("Created timer to cancel, will cancel it in 50ms...");
    
    base->runAfter(50, [base, startTime]() {
        bool cancelled = base->cancel(toCancelStatic);
        INFO("7. Timer cancelled: %s", cancelled ? "success" : "failed");
        
        if (cancelled) {
            INFO("   Cancelled timer will not execute");
        }
        
        testMultipleTimers(base, startTime);
    });
}

void testMultipleTimers(EventBase* base, int64_t startTime)
{
    INFO("========== Multiple Timers Test ==========");
    
    const int NUM_TIMERS = 5;
    TimerId timers[NUM_TIMERS];
    
    for (int i = 0; i < NUM_TIMERS; i++) {
        int delay = 100 + i * 50;
        timers[i] = base->runAfter(delay, [delay, startTime, i]() {
            int64_t now = Utils::timeMilli();
            INFO("8.%d Multiple timer %d: Executed at delay %dms (elapsed: %ldms)", 
                 i+1, i, delay, now - startTime);
        });
    }
    
    base->runAfter(400, [base, startTime]() {
        INFO("========== Summary ==========");
        INFO("All timer examples completed!");
        INFO("Total elapsed time: %ldms", Utils::timeMilli() - startTime);
        base->exit();
    });
}