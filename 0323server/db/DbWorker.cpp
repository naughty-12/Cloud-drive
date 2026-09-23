#include "DbWorker.h"

void DbWorker::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_thread = std::thread(&DbWorker::run, this);
}

void DbWorker::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void DbWorker::enqueue(Task task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(std::move(task));
    }
    m_cv.notify_one();
}

void DbWorker::run() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_queue.empty() || !m_running.load();
        });

        while (!m_queue.empty()) {
            Task task = std::move(m_queue.front());
            m_queue.pop();
            lock.unlock();
            try {
                task();
            } catch (...) {
                fprintf(stderr, "DbWorker: task threw exception, skipping\n");
                // 不要重新抛出异常——否则会永久终止工作线程
            }
            lock.lock();
        }
    }
}
