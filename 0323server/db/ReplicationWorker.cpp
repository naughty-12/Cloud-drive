#include "ReplicationWorker.h"
#include "../cluster/NodeManager.h"
#include <cstdio>
#include <chrono>

void ReplicationWorker::start(NodeManager* nodeMgr) {
    if (m_running.load()) return;
    m_nodeMgr = nodeMgr;
    m_running.store(true);
    m_thread = std::thread(&ReplicationWorker::run, this);
}

void ReplicationWorker::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // 剩余任务有意丢弃——集群级关停，
    // 与 DbWorker 行为一致。单节点崩溃后通过对账机制恢复。
}

bool ReplicationWorker::enqueue(ReplicationTask task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.size() >= MAX_QUEUE_SIZE) {
            // 队列已满 → 对端很可能不可达。丢弃并告警。
            // 对端恢复后由对账机制补齐。
            fprintf(stderr, "[ReplWorker] WARNING: queue full (%zu tasks), "
                    "dropping block %d for file %lld\n",
                    m_queue.size(), task.blockSeq, (long long)task.fileId);
            return false;
        }
        m_queue.push(std::move(task));
    }
    m_cv.notify_one();
    return true;
}

void ReplicationWorker::run() {
    while (m_running.load()) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_queue.empty() || !m_running.load();
        });

        while (!m_queue.empty()) {
            ReplicationTask task = std::move(m_queue.front());
            m_queue.pop();
            lock.unlock();

            try {
                // 重试循环：3 次尝试，间隔 1s
                while (task.retryCount < task.maxRetries) {
                    if (executeTask(task)) {
                        break; // 成功
                    }
                    task.retryCount++;
                    if (task.retryCount < task.maxRetries) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(RETRY_DELAY_MS));
                    }
                }

                if (task.retryCount >= task.maxRetries) {
                    fprintf(stderr, "[ReplWorker] FAILED: file=%lld block=%d "
                            "after %d retries — data not replicated to peers\n",
                            (long long)task.fileId, task.blockSeq, task.maxRetries);
                }
            } catch (...) {
                fprintf(stderr, "[ReplWorker] exception in task, skipping\n");
                // 不要重新抛出异常——保持工作线程存活（与 DbWorker 相同）
            }

            lock.lock();
        }
    }
}

bool ReplicationWorker::executeTask(const ReplicationTask& task) {
    if (!m_nodeMgr) return false;
    return m_nodeMgr->replicateBlock(task.fileId, task.blockSeq, task.offset,
                                     task.data.data(), (int)task.data.size());
}
