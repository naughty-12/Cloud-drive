#include "MySqlWrapper.h"
#include <cstring>
#include <cstdlib>

MySqlWrapper::MySqlWrapper()
    : m_sock(nullptr)
    , m_connected(false)
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

    mysql_set_character_set(m_sock, "utf8");

    if (!mysql_real_connect(m_sock, host, user, pass, db, 0, nullptr, 0)) {
        mysql_close(m_sock);
        m_sock = nullptr;
        return false;
    }

    m_connected = true;
    return true;
}

void MySqlWrapper::disconnect() {
    if (m_sock) {
        mysql_close(m_sock);
        m_sock = nullptr;
    }
    m_connected = false;
}

// ─── Helper struct: owns bind buffers for the lifetime of execute ───────
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

    // Non-copyable, movable
    BindBuf(const BindBuf&) = delete;
    BindBuf& operator=(const BindBuf&) = delete;
    BindBuf(BindBuf&&) = default;
    BindBuf& operator=(BindBuf&&) = default;
};

// ─── bindParams ─────────────────────────────────────────────────────────
// Returns a BindBuf that MUST stay alive until after mysql_stmt_execute.
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
            // Null
            buf.bind[i].buffer_type = MYSQL_TYPE_NULL;
            buf.bind[i].is_null     = nullptr;
        }
    }

    mysql_stmt_bind_param(stmt, buf.bind);
    return buf; // move-eligible; BindBuf owns the heap arrays
}
// ─── execute ────────────────────────────────────────────────────────────
bool MySqlWrapper::execute(const char* sql,
                           const std::vector<SqlValue>& params) {
    if (!m_connected || !m_sock) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(m_sock);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    // BindBuf lives until end of scope — past mysql_stmt_execute
    BindBuf buf = bindParams(stmt, params);

    if (mysql_stmt_execute(stmt) != 0) {
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
    if (!m_connected || !m_sock) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(m_sock);
    if (!stmt) return false;

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    // BindBuf lives until end of scope — past mysql_stmt_execute
    BindBuf buf = bindParams(stmt, params);

    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt) != 0) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if (nColumn <= 0) {
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return true;
    }

    // Allocate result bindings (buffers zero-initialised for safe cleanup on error)
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
        // Bind failed — clean up all heap allocations before returning
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

    // Cleanup
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
    if (!m_connected || !m_sock) return false;
    return mysql_query(m_sock, sql) == 0;
}

// ─── Transaction support (F8-2 fix) ────────────────────────────────────
bool MySqlWrapper::begin() {
    return executeRaw("START TRANSACTION");
}
bool MySqlWrapper::commit() {
    return executeRaw("COMMIT");
}
bool MySqlWrapper::rollback() {
    return executeRaw("ROLLBACK");
}
