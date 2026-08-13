add_executable(bench bench.c)
target_link_libraries(bench PRIVATE sound_seg::sound_seg)
# Benchmarks are meaningless without optimisation. Force -O2 here even
# if the surrounding build is a Debug build.
target_compile_options(bench PRIVATE -O2)

add_executable(bench_scaling bench_scaling.c)
target_link_libraries(bench_scaling PRIVATE sound_seg::sound_seg)
target_compile_options(bench_scaling PRIVATE -O2)
