#ifndef MYSQLWRAPPER_H
#define MYSQLWRAPPER_H

/**
 * @file MySqlWrapper.h
 * @brief MySQL wrapper with parameterized query support.
 *
 * Replaces CMySql's sprintf-based SQL (vulnerable to injection).
 * Uses MySQL C API prepared statements (mysql_stmt_*) for all queries.
 * execute() for INSERT/UPDATE/DELETE, query() for SELECT with result binding.
 *
 * Thread safety: NOT thread-safe. Use via DbWorker single-threaded task queue.
 */

#include <mysql.h>
#include <string>
#include <vector>
#include <list>
#include <cstdint>

// C++11-compatible tagged union (replaces std::variant<std::string, int64_t, double, nullptr_t>)
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

    // Parameterized query — no SQL injection
    bool execute(const char* sql, const std::vector<SqlValue>& params);

    // Parameterized SELECT → results (each row: nColumn strings pushed to results)
    bool query(const char* sql, const std::vector<SqlValue>& params,
               int nColumn, std::list<std::string>& results);

    // Simple raw query (for migration/setup only — NOT for user input)
    bool executeRaw(const char* sql);

    // F8-2 fix: Transaction support for multi-statement atomicity
    bool begin();
    bool commit();
    bool rollback();

    MYSQL* handle() { return m_sock; }

private:
    MYSQL*  m_sock;
    bool    m_connected;
};

#endif
