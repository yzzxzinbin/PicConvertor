#pragma once
#ifndef TILELANDWORLD_TASKSYSTEM_H
#define TILELANDWORLD_TASKSYSTEM_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>

namespace PicConvertor {

    /**
     * @brief 通用多线程任务系统（微改造以方便项目集成）。
     * 
     * 维护一个固定数量的工作线程池，处理所有类型的异步任务。
     * 任务以 std::function<void()> 的形式提交；也支持模板 submit 返回 future。
     */
    class TaskSystem {
    public:
        /**
         * @brief 构造函数。
         * @param threadCount 工作线程数量。如果为 -1，则自动设置为 (硬件核心数 - 1)。
         */
        explicit TaskSystem(int threadCount = -1);
        ~TaskSystem();

        /**
         * @brief 提交一个任务到队列（无返回值）。
         */
        void submit(std::function<void()> task);

        /**
         * @brief 提交可返回值的任务，返回 std::future<T>
         */
        template<typename F, typename... Args>
        auto submitTask(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
            using R = decltype(f(args...));
            auto taskPtr = std::make_shared<std::packaged_task<R()>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
            std::future<R> res = taskPtr->get_future();
            submit([taskPtr]() { (*taskPtr)(); });
            return res;
        }

        /**
         * @brief 等待当前任务队列处理完毕（不停止线程池）。
         */
        void wait_idle();

        // 提交若干空任务并等待，以确保工作线程已启动
        void preheat();

        /**
         * @brief 停止所有线程并等待任务完成 (通常在析构时自动调用，也可手动调用)。
         */
        void stop();

    private:
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;
        
        std::mutex queueMutex;
        std::condition_variable condition;
        std::atomic<bool> stopFlag{false};
        std::atomic<int> activeTasks{0};

        void workerThread();
    };

} // namespace PicConvertor

#endif // TILELANDWORLD_TASKSYSTEM_H
