#ifndef MYSQLWRAPPER_H
#define MYSQLWRAPPER_H

/**
 * @file MySqlWrapper.h
 * @brief 支持参数化查询的 MySQL 封装。
 *
 * 取代 CMySql 基于 sprintf 拼装的 SQL（存在注入漏洞）。
 * 所有查询均使用 MySQL C API 预处理语句（mysql_stmt_*）。
 * execute() 用于 INSERT/UPDATE/DELETE，query() 用于 SELECT 并绑定结果。
 *
 * 线程安全：非线程安全。请通过 DbWorker 单线程任务队列使用。
 *
 * 超时：connect() 内统一设置连接/读/写超时各 5 秒（MYSQL_OPT_CONNECT_TIMEOUT /
 *       MYSQL_OPT_READ_TIMEOUT / MYSQL_OPT_WRITE_TIMEOUT），防止锁等待或服务器
 *       半死时 DbWorker 线程无限挂起。
 *
 * 连接自愈（Task 12 补丁 A）：连接参数在 connect() 成功时保存。
 *       execute()/query()/executeRaw() 失败且 mysql_errno() ∈ {2006 CR_SERVER_GONE_ERROR,
 *       2013 CR_SERVER_LOST}（读超时打断、服务端半死等）时判定句柄报废 →
 *       同一步内 disconnect() + 用保存的参数重连一次 + 重放该语句一次；
 *       重连或重放仍失败则如实返回 false，交由上层处理。
 *       重连在调用线程内同步完成，不改变本类"单线程使用"的约定。
 *       注意：客户端读超时 ≠ 服务端回滚，重放的写语句可能已在服务端生效
 *       （重复执行可能撞唯一键），调用方对写语句仍需幂等/容忍重复的语义。
 */

#include <mysql.h>
#include <string>
#include <vector>
#include <list>
#include <cstdint>

// 兼容 C++11 的带标签联合体（替代 std::variant<std::string, int64_t, double, nullptr_t>）
class SqlValue {
public:
    enum Type { Null, String, Int64, Double };

    SqlValue() : m_type(Null), m_int(0), m_dbl(0.0) {}
    SqlValue(const std::string& s) : m_type(String), m_str(s), m_int(0), m_dbl(0.0) {}
    SqlValue(const char* s) : m_type(String), m_str(s), m_int(0), m_dbl(0.0) {}
    SqlValue(int64_t i) : m_type(Int64), m_int(i), m_dbl(0.0) {}
    SqlValue(int i) : m_type(Int64), m_int(static_cast<int64_t>(i)), m_dbl(0.0) {}
    SqlValue(double d) : m_type(Double), m_int(0), m_dbl(d) {}
    SqlValue(std::nullptr_t) : m_type(Null), m_int(0), m_dbl(0.0) {}

    Type type() const { return m_type; }

    const std::string& asString() const { return m_str; }
    int64_t           asInt64()  const { return m_int; }
    double            asDouble() const { return m_dbl; }

private:
    Type        m_type;
    std::string m_str;
    int64_t     m_int;
    double      m_dbl;
};

class MySqlWrapper {
public:
    MySqlWrapper();
    ~MySqlWrapper();

    bool connect(const char* host, const char* user, const char* pass, const char* db);
    void disconnect();

    // 参数化查询——无 SQL 注入风险
    // 失败且判定为连接报废（errno 2006/2013）时：内部重连 + 重试一次
    bool execute(const char* sql, const std::vector<SqlValue>& params);

    // 参数化 SELECT → 结果集（每行：nColumn 个字符串追加到 results）
    // 失败且判定为连接报废（errno 2006/2013）时：内部重连 + 重试一次
    bool query(const char* sql, const std::vector<SqlValue>& params,
               int nColumn, std::list<std::string>& results);

    // 简单原生查询（仅用于迁移/初始化——不可用于用户输入）
    // 失败且判定为连接报废（errno 2006/2013）时：内部重连 + 重试一次
    bool executeRaw(const char* sql);

    // F8-2 修复：事务支持，保证多语句原子性
    bool begin();
    bool commit();
    bool rollback();

    MYSQL* handle() { return m_sock; }

private:
    // 单次尝试（不含自愈）；失败时把 mysql_errno() 快照存到 m_lastErrNo
    bool executeOnce(const char* sql, const std::vector<SqlValue>& params);
    bool queryOnce(const char* sql, const std::vector<SqlValue>& params,
                   int nColumn, std::list<std::string>& results);
    bool executeRawOnce(const char* sql);

    // 是否已保存连接参数（曾成功 connect 过）
    bool hasSavedParams() const { return !m_host.empty(); }

    // 关闭报废句柄 + 用保存的参数重连；失败打印日志并返回 false
    bool tryReconnect();

    // 连接级错误 → 句柄报废，值得重连重试
    //   2006 CR_SERVER_GONE_ERROR / 2013 CR_SERVER_LOST
    static bool isConnectionLost(int errNo);

    MYSQL*  m_sock;
    bool    m_connected;

    // Task 12 补丁 A：连接自愈所需——连接参数 + 最近一次失败的错误码
    std::string m_host;
    std::string m_user;
    std::string m_pass;
    std::string m_db;
    int         m_lastErrNo;
};

#endif
