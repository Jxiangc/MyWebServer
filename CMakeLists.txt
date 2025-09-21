# 指定cmake最低版本
cmake_minimum_required(VERSION 3.15)

# 设置项目名称
project(MyWebServer)

# 指定C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # 静态库输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib) # 动态库输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin) # 可执行文件输出目录

# 添加 MySQL Connector/C++ 库
find_library(MYSQLCPPCONN_LIBRARY mysqlcppconn)
if (NOT MYSQLCPPCONN_LIBRARY)
    message(FATAL_ERROR "mysqlcppconn library not found")
endif()

# 添加源文件
include_directories(src)
file(GLOB_RECURSE SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

# 创建主程序目标
add_executable(server src/main.cpp ${SOURCES})
target_link_libraries(server ${MYSQLCPPCONN_LIBRARY})

# 创建测试目标
add_executable(Test test/Test.cpp ${SOURCES})
target_link_libraries(Test ${MYSQLCPPCONN_LIBRARY})

# 设置默认目标为 server
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY DEFAULT_TARGET server)

# 链接 MySQL Connector/C++ 库
# target_link_libraries(${PROJECT_NAME} ${MYSQLCPPCONN_LIBRARY})