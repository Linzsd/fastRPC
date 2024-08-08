#include "../libcoroutine/coroutine.h"
#include <vector>

using namespace fastRPC;

std::shared_ptr<Coroutine> coroutine1;
std::shared_ptr<Coroutine> coroutine2;

class Scheduler
{
public:
    // 添加协程调度任务
    void schedule(std::shared_ptr<Coroutine> task)
    {
        m_tasks.push_back(task);
    }

    // 执行调度任务
    void run()
    {
        std::cout << " number " << m_tasks.size() << std::endl;

        std::shared_ptr<Coroutine> task;
        auto it = m_tasks.begin();
        while(it!=m_tasks.end())
        {
            // 迭代器本身也是指针
            task = *it;
            // 由主协程切换到子协程，子协程函数运行完毕后自动切换到主协程
            task->resume();
            std::cout << "come back " << std::endl;
            task->resume();
            it++;
        }
        m_tasks.clear();
    }

private:
    // 任务队列
    std::vector<std::shared_ptr<Coroutine>> m_tasks;
};

void test_coroutine1()
{
    std::cout << "hello world " << std::endl;
    coroutine1->yield();
    std::cout << "goodbye world " << std::endl;
}

void test_coroutine2()
{
    std::cout << "hello world " << std::endl;
    coroutine2->yield();
    std::cout << "goodbye world " << std::endl;
}

int main()
{
    // 初始化当前线程的主协程
    Coroutine::GetThis();

    // 创建调度器
    Scheduler sc;

    // 添加调度任务（任务和子协程绑定）
    coroutine1 = std::make_shared<Coroutine>(test_coroutine1,
            0, false);
    coroutine2 = std::make_shared<Coroutine>(test_coroutine2,
            0, false);
    sc.schedule(coroutine1);
    sc.schedule(coroutine2);

    // 执行调度任务
    sc.run();

    return 0;
}