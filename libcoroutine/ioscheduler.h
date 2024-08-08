#ifndef IOSCHEDULER_H
#define IOSCHEDULER_H

#include "scheduler.h"
#include <shared_mutex>

namespace fastRPC {

    class IOScheduler : public Scheduler {
    public:
        enum Event {
            NONE = 0x0,
            // READ == EPOLLIN
            READ = 0x1,
            // WRITE == EPOLLOUT
            WRITE = 0x4
        };

    private:
        struct FdContext {
            struct EventContext {
                // scheduler
                Scheduler *scheduler = nullptr;
                // callback coroutine
                std::shared_ptr<Coroutine> coroutine;
                // callback function
                std::function<void()> callback;
            };

            // 读事件上下文
            EventContext read;
            // 写事件上下文
            EventContext write;

            int fd = 0;
            // events registered
            Event events = NONE;

            std::mutex mutex;

            EventContext& getEventContext(Event event);

            void resetEventContext(EventContext &ctx);

            void triggerEvent(Event event);
        };

    public:
        IOScheduler(size_t threads = 1, bool use_caller = true, const std::string &name = "IOScheduler");
        ~IOScheduler();

        // add one event at a time
        int addEvent(int fd, Event event, std::function<void()> callback = nullptr);
        // delete event
        bool delEvent(int fd, Event event);
        // delete the event and trigger its callback
        bool cancelEvent(int fd, Event event);
        // delete all events and trigger its callback
        bool cancelAll(int fd);

        static IOScheduler* GetThis();
    // 虚函数重写
    protected:
        void tickle() override;

        bool stopping() override;

        void idle() override;

        void contextResize(size_t size);

    private:
        int m_epfd = 0;
        // 管道的文件描述符，fd[0] read，fd[1] write
        int m_tickleFds[2];

        std::atomic<size_t> m_pendingEventCount = {0};
        // 读写锁
        std::shared_mutex m_mutex;
        // store fdcontexts for each fd
        std::vector<FdContext *> m_fdContexts;
    };
}

#endif //IOSCHEDULER_H
