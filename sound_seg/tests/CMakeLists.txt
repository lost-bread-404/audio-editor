# 一个辅助函数：加一个测试文件 -> 编译成可执行文件 -> 注册进 ctest
function(add_ss_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE sound_seg::sound_seg)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_ss_test(test_read_write)
add_ss_test(test_delete)
add_ss_test(test_errors)
add_ss_test(test_context)
add_ss_test(test_alias_destroy)
