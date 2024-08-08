#include "thread.h"

#include <sys/syscall.h>
#include <iostream>
#include <unistd.h>

namespace fastRPC {

    // 线程信息
    static thread_local Thread* t_thread          = nullptr;
    static thread_local std::string t_thread_name = "UNKNOWN";

    Thread::Thread(std::function<void()> callback, const std::string& name):m_callback(callback), m_name(name) {
        int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
        if (rt) {
            std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
            throw std::logic_error("pthread_create error");
        }
        // 等待线程函数完成初始化
        m_semaphore.wait();
    }

    Thread::~Thread() {
        if (m_thread) {
            pthread_detach(m_thread);
            m_thread = 0;
        }
    }

    void Thread::join() {
        if (m_thread) {
            int rt = pthread_join(m_thread, nullptr);
            if (rt) {
                std::cerr << "pthread_join failed, rt = " << rt << ", name = " << m_name << std::endl;
                throw std::logic_error("pthread_join error");
            }
            m_thread = 0;
        }
    }

    void* Thread::run(void* arg) {
        Thread* thread = (Thread*)arg;

        t_thread       = thread;
        t_thread_name  = thread->m_name;
        thread->m_pid  = GetThreadId();
        pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

        std::function<void()> callBack;
        callBack.swap(thread->m_callback); // swap -> 可以减少m_callBack中智能指针的引用计数

        // 初始化完成
        thread->m_semaphore.signal();

        callBack();
        return 0;
    }


    pid_t Thread::GetThreadId() {
        return syscall(SYS_gettid);
    }

    Thread* Thread::GetThis() {
        return t_thread;
    }

    const std::string& Thread::GetName() {
        return t_thread_name;
    }

    void Thread::SetName(const std::string &name) {
        if (t_thread) {
            t_thread->m_name = name;
        }
        t_thread_name = name;
    }

}