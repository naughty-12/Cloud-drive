#ifndef REPLICATIONWORKER_H
#define REPLICATIONWORKER_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <atomic>
#include <vector>
#include <cstdint>

class NodeManager;

struct ReplicationTask {
    int64_t fileId;
    int     blockSeq;
    int64_t offset;
    std::vector<char> data;
    int     retryCount;
    int     maxRetries;

    ReplicationTask() : fileId(0), blockSeq(0), offset(0), retryCount(0), maxRetries(3) {}
};

class ReplicationWorker {
public:
    // Same pattern as DbWorker: independent thread + task queue + mutex + cv
    ReplicationWorker() = default;
    ~ReplicationWorker() { stop(); }

    void start(NodeManager* nodeMgr);
    void stop();

    // Enqueue a replication task. Returns false if queue is full (peer unreachable).
    // Task data is copied — safe to destroy the source after enqueue returns.
    bool enqueue(ReplicationTask task);

private:
    void run();
    bool executeTask(const ReplicationTask& task);

    NodeManager* m_nodeMgr;

    std::queue<ReplicationTask> m_queue;
    std::mutex       m_mutex;
    std::condition_variable m_cv;
    std::thread      m_thread;
    std::atomic<bool> m_running{false};

    // Queue size limit — prevents unbounded memory growth when peer is offline.
    // 100 tasks × 1MB avg = 100MB max. Beyond this, new tasks are dropped
    // with a warning (peer is unreachable anyway; catch-up via future reconciliation).
    enum { MAX_QUEUE_SIZE = 100 };

    // Retry: 3 attempts with 1s interval. Total = 3s max delay.
    // 1s is enough for transient network flaps; 3s total means if peer hasn't
    // recovered by then, it likely won't until heartbeat reconnects (5s cycle).
    enum { RETRY_DELAY_MS = 1000, MAX_RETRIES = 3 };
};

#endif
