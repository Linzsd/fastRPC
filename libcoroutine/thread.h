#ifndef THREAD_H
#define THREAD_H

#include <functional>
#include <string>
#include <mutex>
#include <condition_variable>

namespace fastRPC {

    // 用于线程方法间的同步
    class Semaphore {
    private:
        std::mutex mtx;
        std::condition_variable cv;
        int count;

    public:
        // 信号量初始化为0
        explicit Semaphore(int count_ = 0) : count(count_) {}

        // P操作
        void wait() {
            std::unique_lock<std::mutex> lock(mtx);
            while (count == 0) {
                cv.wait(lock); // wait for signals
            }
            count--;
        }

        // V操作
        void signal() {
            std::unique_lock<std::mutex> lock(mtx);
            count++;
            cv.notify_one();  // signal
        }
    };

    class Thread {
    public:
        Thread(std::function<void()> callback, const std::string& name);
        ~Thread();

        pid_t getPid() const {
            return m_pid;
        }

        const std::string& getName() const {
            return m_name;
        }

        void join();

    public:
        static pid_t GetThreadId();

        static Thread* GetThis();

        static const std::string& GetName();

        static void SetName(const std::string& name);

    private:
        static void* run(void* arg);

    private:
        pid_t m_pid = -1;
        pthread_t m_thread = 0;

        std::function<void()> m_callback;
        std::string m_name;

        Semaphore m_semaphore;
    };

}

#endif //THREAD_H
