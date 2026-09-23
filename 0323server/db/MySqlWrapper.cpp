#include "MySqlWrapper.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

MySqlWrapper::MySqlWrapper()
    : m_sock(nullptr)
    , m_connected(false)
    , m_lastErrNo(0)
{
}

MySqlWrapper::~MySqlWrapper() {
    disconnect();
}

bool MySqlWrapper::connect(const char* host, const char* user,
                           const char* pass, const char* db) {
    if (m_connected) return true;

    m_sock = mysql_init(nullptr);
    if (!m_sock) return false;

    // Task 12: 连接/读/写超时（秒）——防止锁等待或 MySQL 半死时 DbWorker 线程无限挂起
    // （DbWorker 为全服务单点，一旦挂住所有数据库任务停摆）。必须在 mysql_real_connect 之前设置。
    unsigned int timeoutSec = 5;
    mysql_options(m_sock, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSec);
    mysql_options(m_sock, MYSQL_OPT_READ_TIMEOUT,    &timeoutSec);
    mysql_options(m_sock, MYSQL_OPT_WRITE_TIMEOUT,   &timeoutSec);

    mysql_set_character_set(m_sock, "utf8");

    if (!mysql_real_connect(m_sock, host, user, pass, db, 0, nullptr, 0)) {
        mysql_close(m_sock);
        m_sock = nullptr;
        return false;
    }

    m_connected = true;

    // Task 12 补丁 A：保存连接参数，供断线自愈时重连使用
    m_host = host ? host : "";
    m_user = user ? user : "";
    m_pass = pass ? pass : "";
    m_db   = db   ? db   : "";
    m_lastErrNo = 0;
    return true;
}

void MySqlWrapper::disconnect() {
    if (m_sock) {
        mysql_close(m_sock);
        m_sock = nullptr;
    }
    m_connected = false;
}

// ─── Task 12 补丁 A：连接自愈 ─────────────────────────────────────────────
// 客户端读超时（errno 2013）/ 服务端消失（errno 2006）会让句柄报废：
// 之后所有语句都在客户端侧直接失败。这里判定报废 → 重连一次 → 重放语句一次。
bool MySqlWrapper::isConnectionLost(int errNo) {
    return errNo == 2006   // CR_SERVER_GONE_ERROR
        || errNo == 2013;  // CR_SERVER_LOST
}

bool MySqlWrapper::tryReconnect() {
    if (!hasSavedParams()) return false;   // 从未成功连接过，无参数可恢复

    disconnect();                          // 关闭报废句柄
    if (!connect(m_host.c_str(), m_user.c_str(), m_pass.c_str(), m_db.c_str())) {
        fprintf(stderr, "[MySqlWrapper] reconnect FAILED (%s@%s/%s)\n",
                m_user.c_str(), m_host.c_str(), m_db.c_str());
        return false;
    }
    return true;
}

// ─── 辅助结构体：在 execute 生命周期内持有绑定缓冲区 ───────
struct BindBuf {
    MYSQL_BIND*      bind;
    std::string*     strBufs;
    int64_t*         intBufs;
    double*          dblBufs;
    unsigned long*   strLens;
    size_t           count;

    BindBuf(size_t n)
        : bind(new MYSQL_BIND[n]())
        , strBufs(new std::string[n])
        , intBufs(new int64_t[n])
        , dblBufs(new double[n])
        , strLens(new unsigned long[n])
        , count(n)
    {}

    ~BindBuf() {
        delete[] bind;
        delete[] strBufs;
        delete[] intBufs;
        delete[] dblBufs;
        delete[] strLens;
    }

    // 不可拷贝，可移动
    BindBuf(const BindBuf&) = delete;
    BindBuf& operator=(const BindBuf&) = delete;
    BindBuf(BindBuf&&) = default;
    BindBuf& operator=(BindBuf&&) = default;
};

// ─── bindParams ─────────────────────────────────────────────────────────
// 返回的 BindBuf 必须存活到 mysql_stmt_execute 之后。
static BindBuf bindParams(MYSQL_STMT* stmt,
                          const std::vector<SqlValue>& params) {
    BindBuf buf(params.size());

    for (size_t i = 0; i < params.size(); i++) {
        if (params[i].type() == SqlValue::String) {
            buf.strBufs[i] = params[i].asString();
            buf.strLens[i] = static_cast<unsigned long>(buf.strBufs[i].size());
            buf.bind[i].buffer_type   = MYSQL_TYPE_STRING;
            buf.bind[i].buffer        = const_cast<char*>(buf.strBufs[i].c_str());
            buf.bind[i].buffer_length = static_cast<unsigned long>(buf.strBufs[i].size());
            buf.bind[i].length        = &buf.strLens[i];
            buf.bind[i].is_null       = nullptr;
        } else if (params[i].type() == SqlValue::Int64) {
            buf.intBufs[i] = params[i].asInt64();
            buf.bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            buf.bind[i].buffer      = &buf.intBufs[i];
            buf.bind[i].is_null     = nullptr;
        } else if (params[i].type() == SqlValue::Double) {
            buf.dblBufs[i] = params[i].asDouble();
            buf.bind[i].buffer_type = MYSQL_TYPE_DOUBLE;
            buf.bind[i].buffer      = &buf.dblBufs[i];
            buf.bind[i].is_null     = nullptr;
        } else {
            // Null 值
            buf.bind[i].buffer_type = MYSQL_TYPE_NULL;
            buf.bind[i].is_null     = nullptr;
        }
    }

    mysql_stmt_bind_param(stmt, buf.bind);
    return buf; // 可移动；BindBuf 持有堆数组的所有权
}
// ─── execute ────────────────────────────────────────────────────────────
bool MySqlWrapper::execute(const char* sql,
                           const std::vector<SqlValue>& params) {
    if (executeOnce(sql, params)) return true;

    // Task 12 补丁 A：连接报废 → 重连 + 重试一次
    const int lostErr = m_lastErrNo;   // 必须在重连前快照：connect() 会重置 m_lastErrNo
    if (isConnectionLost(lostErr) && tryReconnect()) {
        const bool ok = executeOnce(sql, params);
        fprintf(stderr, "[MySqlWrapper] connection lost (errno=%d), reconnected & retried once "
                        "→ retry=%s\n", lostErr, ok ? "ok" : "failed");
        return ok;
    }
    return false;
}

// 单次尝试：原有实现（不含自愈）
bool MySqlWrapper::executeOnce(const char* sql,
                               const std::vector<SqlValue>& params) {
    if (!m_connected || !m_sock) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(m_sock);
    if (!stmt) {
        m_lastErrNo = mysql_errno(m_sock);
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        m_lastErrNo = mysql_errno(m_sock);
        mysql_stmt_close(stmt);
        return false;
    }

    // BindBuf 存活到作用域结束——即 mysql_stmt_execute 之后
    BindBuf buf = bindParams(stmt, params);

    if (mysql_stmt_execute(stmt) != 0) {
        m_lastErrNo = mysql_errno(m_sock);
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}

// ─── query ──────────────────────────────────────────────────────────────
bool MySqlWrapper::query(const char* sql,
                         const std::vector<SqlValue>& params,
                         int nColumn,
                         std::list<std::string>& results) {
    if (queryOnce(sql, params, nColumn, results)) return true;

    // Task 12 补丁 A：连接报废 → 重连 + 重试一次
    // queryOnce 的所有 false 分支都发生在任何结果行被追加之前
    // （prepare/execute/store_result/bind_result 失败、或 nColumn<=0 的提前返回），
    // 因此重放不会把重复行写进 results。
    const int lostErr = m_lastErrNo;   // 必须在重连前快照：connect() 会重置 m_lastErrNo
    if (isConnectionLost(lostErr) && tryReconnect()) {
        const bool ok = queryOnce(sql, params, nColumn, results);
        fprintf(stderr, "[MySqlWrapper] connection lost (errno=%d), reconnected & retried once "
                        "→ retry=%s\n", lostErr, ok ? "ok" : "failed");
        return ok;
    }
    return false;
}

// 单次尝试：原有实现（不含自愈）
bool MySqlWrapper::queryOnce(const char* sql,
                             const std::vector<SqlValue>& params,
                             int nColumn,
                             std::list<std::string>& results) {
    if (!m_connected || !m_sock) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(m_sock);
    if (!stmt) {
        m_lastErrNo = mysql_errno(m_sock);
        return false;
    }

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        m_lastErrNo = mysql_errno(m_sock);
        mysql_stmt_close(stmt);
        return false;
    }

    // BindBuf 存活到作用域结束——即 mysql_stmt_execute 之后
    BindBuf buf = bindParams(stmt, params);

    if (mysql_stmt_execute(stmt) != 0) {
        m_lastErrNo = mysql_errno(m_sock);
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        m_lastErrNo = mysql_errno(m_sock);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if (nColumn <= 0) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return true;
    }

    // 分配结果绑定缓冲区（缓冲区零初始化，出错时可安全清理）
    MYSQL_BIND* resultBind = new MYSQL_BIND[nColumn]();
    char** buffers = new char*[nColumn]();
    unsigned long* lengths = new unsigned long[nColumn];
    bool* isNulls = new bool[nColumn];

    const int BUF_SIZE = 512;
    for (int i = 0; i < nColumn; i++) {
        buffers[i] = new char[BUF_SIZE]();
        resultBind[i].buffer_type   = MYSQL_TYPE_STRING;
        resultBind[i].buffer        = buffers[i];
        resultBind[i].buffer_length = BUF_SIZE;
        resultBind[i].length        = &lengths[i];
        resultBind[i].is_null       = &isNulls[i];
    }

    if (mysql_stmt_bind_result(stmt, resultBind) != 0) {
        // 绑定失败——返回前清理所有堆分配
        m_lastErrNo = mysql_errno(m_sock);
        for (int i = 0; i < nColumn; i++) {
            delete[] buffers[i];
        }
        delete[] buffers;
        delete[] lengths;
        delete[] isNulls;
        delete[] resultBind;
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        for (int i = 0; i < nColumn; i++) {
            if (isNulls[i]) {
                results.push_back("");
            } else {
                results.push_back(std::string(buffers[i], lengths[i]));
            }
        }
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    // 清理
    for (int i = 0; i < nColumn; i++) {
        delete[] buffers[i];
    }
    delete[] buffers;
    delete[] lengths;
    delete[] isNulls;
    delete[] resultBind;

    return true;
}

// ─── executeRaw ─────────────────────────────────────────────────────────
bool MySqlWrapper::executeRaw(const char* sql) {
    if (executeRawOnce(sql)) return true;

    // Task 12 补丁 A：连接报废 → 重连 + 重试一次（begin/commit/rollback 经此自动受益）
    const int lostErr = m_lastErrNo;   // 必须在重连前快照：connect() 会重置 m_lastErrNo
    if (isConnectionLost(lostErr) && tryReconnect()) {
        const bool ok = executeRawOnce(sql);
        fprintf(stderr, "[MySqlWrapper] connection lost (errno=%d), reconnected & retried once "
                        "→ retry=%s\n", lostErr, ok ? "ok" : "failed");
        return ok;
    }
    return false;
}

// 单次尝试：原有实现（不含自愈）
bool MySqlWrapper::executeRawOnce(const char* sql) {
    if (!m_connected || !m_sock) return false;
    if (mysql_query(m_sock, sql) == 0) return true;
    m_lastErrNo = mysql_errno(m_sock);
    return false;
}

// ─── 事务支持（F8-2 修复）────────────────────────────────────
bool MySqlWrapper::begin() {
    return executeRaw("START TRANSACTION");
}
bool MySqlWrapper::commit() {
    return executeRaw("COMMIT");
}
bool MySqlWrapper::rollback() {
    return executeRaw("ROLLBACK");
}
