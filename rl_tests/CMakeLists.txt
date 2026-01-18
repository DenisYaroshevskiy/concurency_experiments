
function(add_rl_test name source)
    set(compiler_options -g -Wall -Wno-deprecated-copy)
    add_executable(${name})
    target_sources(${name} PRIVATE ${source})
    target_compile_options(${name} PRIVATE ${compiler_options})
endfunction()

add_rl_test(atrocious_mutex_rl_test atrocious_mutex_rl_test.cpp)
add_rl_test(rcu_0_test rcu_0_test.cpp)
