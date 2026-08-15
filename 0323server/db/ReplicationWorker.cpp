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
    // Remaining tasks intentionally dropped — cluster-wide shutdown,
    // same behavior as DbWorker. Single-node crash recovery via reconciliation.
}

bool ReplicationWorker::enqueue(ReplicationTask task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.size() >= MAX_QUEUE_SIZE) {
            // Queue full → peer is likely unreachable. Drop with warning.
            // Reconciliation will catch up when peer recovers.
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
                // Retry loop: 3 attempts, 1s interval
                while (task.retryCount < task.maxRetries) {
                    if (executeTask(task)) {
                        break; // success
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
                // Do NOT rethrow — keep worker thread alive (same as DbWorker)
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
