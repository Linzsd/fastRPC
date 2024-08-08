#include <sys/epoll.h>
#include <fcntl.h>
#include <string.h>
#include "ioscheduler.h"


static bool debug = true;

namespace fastRPC {

    IOScheduler *IOScheduler::GetThis() {
        return dynamic_cast<IOScheduler*>(Scheduler::GetThis());
    }

    IOScheduler::FdContext::EventContext &IOScheduler::FdContext::getEventContext(Event event) {
        assert(event==READ || event==WRITE);
        switch (event)
        {
            case READ:
                return read;
            case WRITE:
                return write;
        }
        throw std::invalid_argument("Unsupported event type");
    }

    void IOScheduler::FdContext::resetEventContext(EventContext &ctx)  {
        ctx.scheduler = nullptr;
        ctx.coroutine.reset();
        ctx.callback = nullptr;
    }

    // no lock
    void IOScheduler::FdContext::triggerEvent(IOScheduler::Event event) {
        assert(events & event);

        // 删除事件
        events = (Event)(events & ~event);

        // trigger
        EventContext& ctx = getEventContext(event);
        if (ctx.callback) {
            // call ScheduleTask(std::function<void()>* func, int thread_id)
            ctx.scheduler->scheduleLock(&ctx.callback);
        } else {
            // call ScheduleTask(std::shared_ptr<Coroutine>* co, int thread_id)
            ctx.scheduler->scheduleLock(&ctx.coroutine);
        }

        // reset event context
        resetEventContext(ctx);
        return;
    }

    IOScheduler::IOScheduler(size_t threads, bool use_caller, const std::string &name):
    Scheduler(threads, use_caller, name) {
        // create epoll fd
        m_epfd = epoll_create(5000);
        assert(m_epfd > 0);

        // create pipe
        int rt = pipe(m_tickleFds);
        assert(!rt);

        // 添加read事件
        epoll_event event;
        event.events  = EPOLLIN | EPOLLET; // Edge Triggered
        event.data.fd = m_tickleFds[0];

        // 设置non-blocked
        rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
        assert(!rt);

        rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
        assert(!rt);

        contextResize(32);

        start();
    }

    IOScheduler::~IOScheduler() {
        stop();
        close(m_epfd);
        close(m_tickleFds[0]);
        close(m_tickleFds[1]);

        for (size_t i = 0; i < m_fdContexts.size(); ++i) {
            if (m_fdContexts[i]) {
                delete m_fdContexts[i];
            }
        }
    }

    // no lock
    void IOScheduler::contextResize(size_t size) {
        m_fdContexts.resize(size);

        for (size_t i = 0; i < m_fdContexts.size(); ++i) {
            if (m_fdContexts[i] == nullptr) {
                m_fdContexts[i] = new FdContext();
                m_fdContexts[i]->fd = i;
            }
        }
    }

    int IOScheduler::addEvent(int fd, Event event, std::function<void()> callback) {
        FdContext* fd_ctx = nullptr;
        // 找出要add的FdContext
        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd) {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            std::unique_lock<std::shared_mutex> write_lock(m_mutex);
            // 扩容1.5倍
            contextResize(fd * 1.5);
            fd_ctx = m_fdContexts[fd];
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);
        // 如果该事件已经添加
        if (fd_ctx->events & event) {
            return -1;
        }
        // 如果没有要添加的事件，如fd_ctx->events是read事件，要添加的是write事件，则MOD
        int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
        epoll_event epEvent;
        epEvent.events = EPOLLET | fd_ctx->events | event;
        epEvent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epEvent);
        if (rt) {
            std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return -1;
        }
        // 待执⾏IO事件数加1
        ++ m_pendingEventCount;

        // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进⾏赋值
        fd_ctx->events = (Event)(fd_ctx->events | event);
        FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
        assert(!event_ctx.scheduler && !event_ctx.coroutine && !event_ctx.callback);

        // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执⾏体
        event_ctx.scheduler = Scheduler::GetThis();
        if (callback) {
            event_ctx.callback.swap(callback);
        } else {
            event_ctx.coroutine = Coroutine::GetThis();
            assert(event_ctx.coroutine->getState() == Coroutine::RUNNING);
        }
        return 0;
    }

    bool IOScheduler::delEvent(int fd, Event event) {
        // find FdContext
        FdContext *fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd) {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        // 事件不存在
        if (!(fd_ctx->events & event)) {
            return false;
        }

        // 删除事件
        Event new_events = (Event)(fd_ctx->events & ~event);

        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epEvent;
        epEvent.events = EPOLLET | new_events;
        epEvent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epEvent);
        if (rt) {
            std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return -1;
        }

        --m_pendingEventCount;

        // update fdcontext
        fd_ctx->events = new_events;

        // update event context
        FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
        fd_ctx->resetEventContext(event_ctx);
        return true;
    }

    bool IOScheduler::cancelEvent(int fd, Event event) {
        // attemp to find FdContext
        FdContext *fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd) {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        // the event doesn't exist
        if (!(fd_ctx->events & event)) {
            return false;
        }

        // delete the event
        Event new_events = (Event)(fd_ctx->events & ~event);

        int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
        epoll_event epEvent;
        epEvent.events = EPOLLET | new_events;
        epEvent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epEvent);
        if (rt) {
            std::cerr << "cancelEvent::epoll_ctl failed: " << strerror(errno) << std::endl;
            return -1;
        }

        --m_pendingEventCount;

        // update fdcontext, event context and trigger
        fd_ctx->triggerEvent(event);
        return true;
    }

    bool IOScheduler::cancelAll(int fd) {
        // attemp to find FdContext
        FdContext *fd_ctx = nullptr;

        std::shared_lock<std::shared_mutex> read_lock(m_mutex);
        if ((int)m_fdContexts.size() > fd) {
            fd_ctx = m_fdContexts[fd];
            read_lock.unlock();
        } else {
            read_lock.unlock();
            return false;
        }

        std::lock_guard<std::mutex> lock(fd_ctx->mutex);

        // none of events exist
        if (!fd_ctx->events) {
            return false;
        }

        // delete all events
        int op = EPOLL_CTL_DEL;
        epoll_event epEvent;
        epEvent.events = 0;
        epEvent.data.ptr = fd_ctx;

        int rt = epoll_ctl(m_epfd, op, fd, &epEvent);
        if (rt) {
            std::cerr << "IOManager::epoll_ctl failed: " << strerror(errno) << std::endl;
            return -1;
        }

        // update fdcontext, event context and trigger
        if (fd_ctx->events & READ) {
            fd_ctx->triggerEvent(READ);
            --m_pendingEventCount;
        }

        if (fd_ctx->events & WRITE) {
            fd_ctx->triggerEvent(WRITE);
            --m_pendingEventCount;
        }

        assert(fd_ctx->events == 0);
        return true;
    }

    void IOScheduler::tickle()
    {
        // 没有空闲的线程
        if (!hasIdleThreads()) {
            return;
        }
        // 向管道写一个byte，通知
        int rt = write(m_tickleFds[1], "T", 1);
        assert(rt == 1);
    }

    bool IOScheduler::stopping()
    {
        // 所有待调度的IO事件都执⾏完了才可以退出
        return m_pendingEventCount == 0 && Scheduler::stopping();
    }

    void IOScheduler::idle() {
        // ⼀次epoll_wait最多检测256个就绪事件，如果就绪事件超过了这个数，那么会在下轮epoll_wait继续处理
        static const uint64_t MAX_EVNETS = 256;
        std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVNETS]);

        while (true) {
            if(debug) std::cout << "IOManager::idle(),run in thread: " << Thread::GetThreadId() << std::endl;

            if (stopping()) {
                if(debug) std::cout << "name = " << getName() << " idle exits in thread: "
                                    << Thread::GetThreadId() << std::endl;
                break;
            }

            // blocked at epoll_wait
            int rt = 0;
            while (true) {
                static const uint64_t MAX_TIMEOUT = 5000;
                rt = epoll_wait(m_epfd, events.get(), MAX_EVNETS, (int)MAX_TIMEOUT);
                // EINTR -> retry
                if (rt < 0 && errno == EINTR) {
                    continue;
                } else {
                    break;
                }
            };

            // 遍历所有就绪事件
            for (int i = 0; i < rt; ++i) {
                epoll_event& event = events[i];

                // tickle event
                if (event.data.fd == m_tickleFds[0]) {
                    uint8_t dummy[256];
                    // edge triggered -> exhaust
                    // ticklefd[0]⽤于通知协程调度，这时只需要把管道⾥的内容读完即可，
                    // 本轮idle结束Scheduler::run会重新执⾏协程调度
                    while (read(m_tickleFds[0], dummy, sizeof(dummy)) > 0);
                    continue;
                }

                // other events
                FdContext *fd_ctx = (FdContext *)event.data.ptr;
                std::lock_guard<std::mutex> lock(fd_ctx->mutex);

                // convert EPOLLERR or EPOLLHUP to -> read or write event
                /* EPOLLERR: 出错，⽐如写读端已经关闭的pipe
                 * EPOLLHUP: 套接字对端关闭
                 * 出现这两种事件，应该同时触发fd的读和写事件，否则有可能出现注册的事件永远执⾏不到的情况
                */
                if (event.events & (EPOLLERR | EPOLLHUP)) {
                    event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
                }
                // events happening during this turn of epoll_wait
                int real_events = NONE;
                if (event.events & EPOLLIN) {
                    real_events |= READ;
                }
                if (event.events & EPOLLOUT) {
                    real_events |= WRITE;
                }

                if ((fd_ctx->events & real_events) == NONE) {
                    continue;
                }

                // 删除已经发⽣的事件，将剩下的事件重新加⼊epoll_wait，
                // 如果剩下的事件为0，表示这个fd已经不需要关注了，直接从epoll中删除
                int left_events = (fd_ctx->events & ~real_events);
                int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
                event.events    = EPOLLET | left_events;

                int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
                if (rt2) {
                    std::cerr << "idle::epoll_ctl failed: " << strerror(errno) << std::endl;
                    continue;
                }

                // schedule callback and update fdcontext and event context
                if (real_events & READ) {
                    fd_ctx->triggerEvent(READ);
                    --m_pendingEventCount;
                }
                if (real_events & WRITE) {
                    fd_ctx->triggerEvent(WRITE);
                    --m_pendingEventCount;
                }
            } // end for

            Coroutine::GetThis()->yield();

        } // end while(true)
    }

}