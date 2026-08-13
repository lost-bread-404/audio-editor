# A standalone project that consumes the INSTALLED library, exactly the
# way a real user would. It deliberately does not know about the parent
# source tree -- only about find_package.
cmake_minimum_required(VERSION 3.16)
project(consumer LANGUAGES C)

find_package(sound_seg 0.1 REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE sound_seg::sound_seg)
