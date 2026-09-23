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
    bool execute(const char* sql, const std::vector<SqlValue>& params);

    // 参数化 SELECT → 结果集（每行：nColumn 个字符串追加到 results）
    bool query(const char* sql, const std::vector<SqlValue>& params,
               int nColumn, std::list<std::string>& results);

    // 简单原生查询（仅用于迁移/初始化——不可用于用户输入）
    bool executeRaw(const char* sql);

    // F8-2 修复：事务支持，保证多语句原子性
    bool begin();
    bool commit();
    bool rollback();

    MYSQL* handle() { return m_sock; }

private:
    MYSQL*  m_sock;
    bool    m_connected;
};

#endif
