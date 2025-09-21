#include "../src/log/log.h"
#include "../src/pool/sqlconnpool.h"
#include "../src/pool/threadpool.h"

void TestLog() {
    int cnt = 0, level = 0;
    Log::Instance().init(level, "./test_log1", ".log", 0);
    for (level = 3; level >= 0; level--) {
        Log::Instance().SetLevel(level);
        for(int j = 0; j < 10000; j++ ){
            for(int i = 0; i < 4; i++) {
                LOG_BASE(i,"%s 111111111 %d ============= ", "Test", cnt++);
            }
        }
    }
    cnt = 0;
    Log::Instance().init(level, "./test_log2", ".log", 5000);
    for(level = 0; level < 4; level++) {
        Log::Instance().SetLevel(level);
        for(int j = 0; j < 10000; j++ ){
            for(int i = 0; i < 4; i++) {
                LOG_BASE(i,"%s 222222222 %d ============= ", "Test", cnt++);
            }
        }
    }
}

void ThreadLogTask(int i, int cnt) {
    for (int j = 0; j < 10000; j++) {
        LOG_BASE(i, "PID:[%04d]======= %05d ========= ", gettid(), cnt++);
    }
}

void TestThreadPool() {
    Log::Instance().init(0, "test_threadpool", ".log", 5000);
    ThreadPool threadpool(6);
    for (int i = 0; i < 18; i++) {
        threadpool.enqueue(std::bind(ThreadLogTask, i % 4, i * 10000));
    }
}

void TestDataBase() {
    SqlConnPool::getInstance().init(
        "localhost",
        "jxc",
        "128040",
        "itheima"
    );

    SqlConnRAII connWarpper;

    std::unique_ptr<sql::Statement> stmt(connWarpper->createStatement());
    std::unique_ptr<sql::ResultSet> res(
        stmt->executeQuery("SELECT name FROM tb_user;")
    );

    while (res->next()) {
        std::cout << res->getString("name") << std::endl;
    }

}

int main()
{
    // TestLog();
    // TestThreadPool();
    TestDataBase();
    return 0;
}