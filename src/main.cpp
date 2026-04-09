
#include "server/webserver.h"

int main() {
    // 守护进程，后台运行
    WebServer server(
        7878,               // port
        3,                  // trigMode: ET + ET
        60000,              // timeoutMS: 60s
        false,              // OptLinger
        3306,               // sqlport
        "jxc",              // sqlUser
        "128040",           // sqlPwd
        "webserver",        // dbname
        12,                 // connPoolNum
        4,                  // subReactorNum
        true,               // openLog
        1,                  // loglevel
        1024                // logQueSize
    );

    server.Start();

    return 0;
}
