#ifndef DBWORKER_H
#define DBWORKER_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <atomic>

class DbWorker {
public:
    using Task = std::function<void()>;

    DbWorker() = default;
    ~DbWorker() { stop(); }

    void start();
    void stop();
    void enqueue(Task task);

private:
    void run();

    std::queue<Task> m_queue;
    std::mutex       m_mutex;
    std::condition_variable m_cv;
    std::thread      m_thread;
    std::atomic<bool> m_running{false};
};

#endif
