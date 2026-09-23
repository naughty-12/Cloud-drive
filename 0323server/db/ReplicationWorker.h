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
    // 与 DbWorker 相同模式：独立线程 + 任务队列 + 互斥锁 + 条件变量
    ReplicationWorker() = default;
    ~ReplicationWorker() { stop(); }

    void start(NodeManager* nodeMgr);
    void stop();

    // 入队一个复制任务。队列满时返回 false（对端不可达）。
    // 任务数据会被拷贝——入队返回后即可安全销毁源数据。
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

    // 队列大小上限——防止对端离线时内存无限增长。
    // 100 个任务 × 平均 1MB = 最多 100MB。超出后新任务被丢弃
    // 并告警（对端本就不可达；后续通过未来对账机制补齐）。
    enum { MAX_QUEUE_SIZE = 100 };

    // 重试：3 次尝试，间隔 1s。总延迟最多 3s。
    // 1s 足以应对瞬时网络抖动；若 3s 内对端仍未恢复，
    // 则大概率要等心跳重连（5s 周期）后才能恢复。
    enum { RETRY_DELAY_MS = 1000, MAX_RETRIES = 3 };
};

#endif
