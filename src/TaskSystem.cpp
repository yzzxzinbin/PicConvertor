#include "TaskSystem.h"
#include "Logger.h"
#include <algorithm>

namespace PicConvertor {

    TaskSystem::TaskSystem(int threadCount) {
        if (threadCount <= 0) {
            // 保留一个核心给主线程
            threadCount = std::max(1u, std::thread::hardware_concurrency() - 1);
        }

        PC_LOG_INFO("Initializing TaskSystem with " + std::to_string(threadCount) + " worker threads.");

        for (int i = 0; i < threadCount; ++i) {
            workers.emplace_back(&TaskSystem::workerThread, this);
        }
    }

    TaskSystem::~TaskSystem() {
        stop();
    }

    void TaskSystem::submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            tasks.emplace(std::move(task));
        }
        condition.notify_one();
    }

    void TaskSystem::wait_idle() {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] { return tasks.empty() && activeTasks.load() == 0; });
    }

    void TaskSystem::preheat() {
        // 为每个 worker 提交一个空任务并 wait_idle，以确保线程正在运行/被唤醒
        int n = (int)workers.size();
        if (n <= 0) return;
        for (int i = 0; i < n; ++i) {
            submit([](){});
        }
        wait_idle();
        PC_LOG_INFO("TaskSystem preheated with " + std::to_string(n) + " tasks.");
    }

    void TaskSystem::stop() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopFlag) return; // 已经停止
            stopFlag = true;
        }
        condition.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        PC_LOG_INFO("TaskSystem stopped.");
    }

    void TaskSystem::workerThread() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                condition.wait(lock, [this] { return stopFlag || !tasks.empty(); });

                if (stopFlag && tasks.empty()) {
                    return;
                }

                if (tasks.empty()) continue;

                task = std::move(tasks.front());
                tasks.pop();
                activeTasks.fetch_add(1);
            }

            // 执行任务
            try {
                task();
            } catch (const std::exception& e) {
                PC_LOG_ERROR("Exception in TaskSystem worker thread: " + std::string(e.what()));
            } catch (...) {
                PC_LOG_ERROR("Unknown exception in TaskSystem worker thread.");
            }

            activeTasks.fetch_sub(1);
            condition.notify_all();
        }
    }

} // namespace PicConvertor
