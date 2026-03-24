# 添加所有测试可执行文件
set(TEST_SOURCES
    logger_test.cpp
    codec_test.cpp
    udp_test.cpp
    daemon_test.cpp
    utils_test.cpp
    conn_test.cpp
    stat-svr_test.cpp
    http_test.cpp
    conf_test.cpp
    file_test.cpp
    status_test.cpp
    thread_pool_test.cpp
    slice_test.cpp
    port_posix_test.cpp
    net_test.cpp
)

# 为每个测试源文件创建可执行文件
foreach(test_source ${TEST_SOURCES})
    # 获取文件名（不含扩展名）作为目标名
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    
    # 链接 handy 库（handy 已经包含了头文件路径和线程库依赖）
    target_link_libraries(${test_name} PRIVATE handy)
endforeach()

# 添加自定义目标：运行所有测试
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS ${TEST_TARGETS}
    COMMENT "Running all tests"
)