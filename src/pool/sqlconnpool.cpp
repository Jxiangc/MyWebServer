#include "sqlconnpool.h"

SqlConnPool& SqlConnPool::getInstance(){
    static SqlConnPool instance;
    return instance;
}

void SqlConnPool::init(const std::string& host, const std::string& user,
                       const std::string& password, const std::string& database,
                       int poolSize, int maxPoolSize,
                       int connectionTimeout) {
    this->host = host;
    this->user = user;
    this->password = password;
    this->database = database;
    this->poolSize = poolSize;
    this->maxPoolSize = maxPoolSize;
    this->connectionTimeout = connectionTimeout;

    for (int i = 0; i < poolSize; i++) {
        addConnection();
    }
}

std::shared_ptr<sql::Connection> SqlConnPool::getConnection() {
    std::unique_lock<std::mutex> lock(mtx_);

    if (connections.empty() && currentSize < maxPoolSize) {
        addConnection();
    }

    if (connections.empty()) {
        if (cv_.wait_for(lock, std::chrono::seconds(connectionTimeout)) == std::cv_status::timeout) {
            throw std::runtime_error("获取数据库连接超时");
        }
    }

    auto conn = connections.front();
    connections.pop();

    if (!isConnectionValid(conn)) {
        conn.reset(createConnection().release());
    }

    return conn;
}

void SqlConnPool::returnConnection(std::shared_ptr<sql::Connection> conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mtx_);

    if (!isConnectionValid(conn)) {
        currentSize--;
        return;
    }

    connections.emplace(conn);
    cv_.notify_one();
}

void SqlConnPool::shutdown() {
    std::lock_guard<std::mutex> lock(mtx_);
    while (!connections.empty()) {
        connections.pop();
    }
    currentSize = 0;
}

std::unique_ptr<sql::Connection> SqlConnPool::createConnection() {
    try { 
        // get_mysql_driver_instance() 返回的是一个单例对象，不需要手动释放
        sql::mysql::MySQL_Driver* driver = sql::mysql::get_mysql_driver_instance();
        std::unique_ptr<sql::Connection> conn(driver->connect(host, user, password));
        conn->setSchema(database);
        return conn;
    } catch (sql::SQLException& e) {
        std::cerr << "MySQL连接错误: " << e.what() << std::endl;
        return nullptr;
    }
}

void SqlConnPool::addConnection() {
    auto conn = createConnection();
    if (conn) {
        connections.emplace(std::shared_ptr<sql::Connection>(conn.release()));
        currentSize++;
    }
}

bool SqlConnPool::isConnectionValid(std::shared_ptr<sql::Connection> conn) {
    if (!conn) return false;

    try {
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
        return true;
    } catch (sql::SQLException&) {
        return false;
    }
}