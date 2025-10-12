#include <unistd.h>
#include "server/webserver.h"

int main() {
    // 守护进程，后台运行
    WebServer server(
        7878, 3, 60000, false,
        3306, "jxc", "128040", "webserver",
        12, 6, true, 1, 1024
    );
    server.Start();
    return 0;
}
