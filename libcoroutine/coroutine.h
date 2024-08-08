#ifndef COROUTINE_H
#define COROUTINE_H

#include <memory>
#include <functional>
#include <ucontext.h>
#include <mutex>
#include <atomic>
#include <iostream>
#include <assert.h>

namespace fastRPC {

    /**
     ** @brief 协程类
     */
    class Coroutine : public std::enable_shared_from_this<Coroutine> {

    public:
        // 协程状态
        enum State {
            READY,   // 就绪态，刚创建或者yield之后的状态
            RUNNING, // 运行中
            TERMINAL // 终止
        };

    private:
        // 仅由GetThis()调用 -> 私有 -> 创建主协程
        Coroutine();

    public:
        /**
         * @brief 构造函数，⽤于创建⽤户协程
         ** @param[in] callback 协程要执行的函数
         ** @param[in] stackSize 栈⼤⼩
         ** @param[in] run_in_scheduler 本协程是否参与调度器调度，默认为true
         */
        Coroutine(std::function<void()> callback, size_t stackSize = 0, bool run_in_scheduler = true);
        ~Coroutine();

        /**
         * @brief 重置协程状态和运行函数，复⽤栈空间，不重新创建栈
         * @param callback
         */
        void reset(std::function<void()> callback);

        // 任务线程恢复执行
        void resume();
        // 任务线程让出执行权
        void yield();

        uint64_t getId() const {return m_id;}
        State getState() const {return m_state;}

    public:
        // 设置当前运行的协程
        static void SetThis(Coroutine *co);

        // 得到当前运行的协程
        static std::shared_ptr<Coroutine> GetThis();

        // 设置调度协程（默认为主协程）
        static void SetSchedulerCoroutine(Coroutine* co);

        // 得到当前运行的协程id
        static uint64_t GetCoroutineId();

        // 协程函数
        static void MainFunc();

    private:
        // id
        uint64_t m_id = 0;
        // 栈大小
        uint32_t m_stackSize = 0;
        // 协程状态
        State m_state = READY;
        // 协程上下文
        ucontext_t m_ctx;
        // 协程栈指针
        void* m_stack = nullptr;
        // 协程函数
        std::function<void()> m_callback;
        // 是否让出执行权交给调度协程
        bool m_runInScheduler;

    public:
        std::mutex m_mutex;
    };
}

#endif //COROUTINE_H
