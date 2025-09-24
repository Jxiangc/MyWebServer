#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <iostream>
#include <queue>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <mysql_driver.h>
#include <mysql_connection.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/prepared_statement.h>


class SqlConnPool{
public:
    // 单例模式
    static SqlConnPool& getInstance();

    void init(const std::string& host, const std::string& user,
              const std::string& password, const std::string& database,
              int poolSize = 10, int maxPoolSize = 20,
              int connectionTimeout = 30);

    std::shared_ptr<sql::Connection> getConnection();

    void returnConnection(std::shared_ptr<sql::Connection> conn);

    void shutdown();

private:
    SqlConnPool() : currentSize(0) {}
    ~SqlConnPool() { shutdown(); }

    SqlConnPool(const SqlConnPool&) = delete;
    SqlConnPool& operator=(const SqlConnPool&) = delete;

    std::unique_ptr<sql::Connection> createConnection();

    void addConnection();

    bool isConnectionValid(std::shared_ptr<sql::Connection> conn);

    std::queue<std::shared_ptr<sql::Connection>> connections;
    std::mutex mtx_;
    std::condition_variable cv_;

    std::string host;
    std::string user;
    std::string password;
    std::string database;

    int poolSize;
    int maxPoolSize;
    int connectionTimeout;
    int currentSize;
};

class SqlConnRAII{
public:
    SqlConnRAII() : conn(SqlConnPool::getInstance().getConnection()) {}
    ~SqlConnRAII() {
        if (conn) {
            SqlConnPool::getInstance().returnConnection(conn);
        }
    }

    sql::Connection* operator->() const { return conn.get(); }
    sql::Connection* get() { return conn.get(); }

private:
    std::shared_ptr<sql::Connection> conn;
};

#endif