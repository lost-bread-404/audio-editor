add_executable(fuzz_track fuzz_track.c)
target_link_libraries(fuzz_track PRIVATE sound_seg::sound_seg)
target_compile_options(fuzz_track PRIVATE -fsanitize=fuzzer,address -g)
target_link_options(fuzz_track    PRIVATE -fsanitize=fuzzer,address)
