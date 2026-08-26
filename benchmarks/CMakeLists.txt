add_executable(bench bench.c)
target_link_libraries(bench PRIVATE sound_seg::sound_seg)
# Benchmarks are meaningless without optimisation, so force it on even
# in a Debug build. MSVC spells the flag differently.
if(MSVC)
    target_compile_options(bench PRIVATE /O2)
else()
    target_compile_options(bench PRIVATE -O2)
endif()

add_executable(bench_scaling bench_scaling.c)
target_link_libraries(bench_scaling PRIVATE sound_seg::sound_seg)
if(MSVC)
    target_compile_options(bench_scaling PRIVATE /O2)
else()
    target_compile_options(bench_scaling PRIVATE -O2)
endif()