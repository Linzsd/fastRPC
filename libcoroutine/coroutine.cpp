#include "coroutine.h"

namespace fastRPC {

    bool debug = false;
    // 正在运行的协程
    static thread_local Coroutine* t_coroutine = nullptr;
    // 主协程，每个线程都有一个主协程
    static thread_local std::shared_ptr<Coroutine> t_thread_coroutine = nullptr;
    // 调度协程
    static thread_local Coroutine* t_scheduler_coroutine = nullptr;

    // 协程计数器
    static std::atomic<uint64_t> s_coroutine_id{0};
    // 协程id
    static std::atomic<uint64_t> s_coroutine_count{0};

    Coroutine::Coroutine() {
        SetThis(this);
        m_state = RUNNING;

        if (getcontext(&m_ctx)) {
            std::cerr << "Coroutine() failed\n";
            pthread_exit(NULL);
        }

        m_id = s_coroutine_id ++;
        s_coroutine_count ++;
        if(debug) std::cout << "Coroutine(): main id = " << m_id << std::endl;
    }

    Coroutine::Coroutine(std::function<void()> callback, size_t stackSize, bool run_in_scheduler):
    m_callback(callback), m_runInScheduler(run_in_scheduler) {
        m_state = READY;

        m_stackSize = stackSize ? stackSize : 128000;
        m_stack = malloc(m_stackSize);

        if (getcontext(&m_ctx)) {
            std::cerr << "Coroutine(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
            pthread_exit(NULL);
        }

        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_stack.ss_size = m_stackSize;
        makecontext(&m_ctx, &Coroutine::MainFunc, 0);

        m_id = s_coroutine_id ++;
        s_coroutine_count ++;
        if(debug) std::cout << "Coroutine(): child id = " << m_id << std::endl;
    }

    Coroutine::~Coroutine() {
        s_coroutine_count --;
        if(m_stack) {
            free(m_stack);
        }
        if(debug) std::cout << "~Coroutine(): id = " << m_id << std::endl;
    }

    void Coroutine::reset(std::function<void()> callback) {
        assert(m_stack != nullptr && m_state == TERMINAL);

        m_state = READY;
        m_callback = callback;

        if(getcontext(&m_ctx)) {
            std::cerr << "reset() failed\n";
            pthread_exit(NULL);
        }

        m_ctx.uc_link = nullptr;
        m_ctx.uc_stack.ss_sp = m_stack;
        m_ctx.uc_stack.ss_size = m_stackSize;
        makecontext(&m_ctx, &Coroutine::MainFunc, 0);
    }

    void Coroutine::resume() {
        assert(m_state == READY);
        m_state = RUNNING;

        if (m_runInScheduler) {
            SetThis(this);
            if (swapcontext(&t_scheduler_coroutine->m_ctx, &m_ctx)) {
                std::cerr << "resume() to t_scheduler_coroutine failed\n";
                pthread_exit(NULL);
            }
        }
        else {
            SetThis(this);
            if (swapcontext(&t_thread_coroutine->m_ctx, &m_ctx)) {
                std::cerr << "resume() to t_thread_coroutine failed\n";
                pthread_exit(NULL);
            }
        }
    }

    void Coroutine::yield() {
        assert(m_state == RUNNING || m_state == TERMINAL);

        if (m_state != TERMINAL) {
            m_state = READY;
        }

        if (m_runInScheduler) {
            SetThis(t_scheduler_coroutine);
            if (swapcontext(&m_ctx, &t_scheduler_coroutine->m_ctx)) {
                std::cerr << "yield() to to t_scheduler_coroutine failed\n";
                pthread_exit(NULL);
            }
        }
        else {
            SetThis(t_thread_coroutine.get());
            // 将当前协程的ctx换出到主协程
            if (swapcontext(&m_ctx, &t_thread_coroutine->m_ctx)) {
                std::cerr << "yield() to t_thread_coroutine failed\n";
                pthread_exit(NULL);
            }
        }
    }

    void Coroutine::MainFunc() {
        std::shared_ptr<Coroutine> cur = GetThis();
        assert(cur != nullptr);

        cur->m_callback();
        cur->m_callback = nullptr;
        cur->m_state = TERMINAL;

        // 运行完毕 -> 让出执行权
        auto raw_ptr = cur.get();
        cur.reset();
        raw_ptr->yield();
    }


    void Coroutine::SetThis(Coroutine *co) {
        t_coroutine = co;
    }

    std::shared_ptr<Coroutine> Coroutine::GetThis() {
        if(t_coroutine) {
            return t_coroutine->shared_from_this();
        }

        std::shared_ptr<Coroutine> main_coroutine(new Coroutine());
        t_thread_coroutine = main_coroutine;
        t_scheduler_coroutine = main_coroutine.get(); // 除非主动设置 主协程默认为调度协程

        assert(t_coroutine == main_coroutine.get());
        return t_coroutine->shared_from_this();
    }

    void Coroutine::SetSchedulerCoroutine(Coroutine *co) {
        t_scheduler_coroutine = co;
    }

    uint64_t Coroutine::GetCoroutineId() {
        if (t_coroutine) {
            return t_coroutine->getId();
        }
        return -1;
    }

}