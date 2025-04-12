cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED)
project(fastrd VERSION 0.1.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/.gitignore" "*")
add_executable(fastrd main.cpp)
target_link_libraries(fastrd PRIVATE Threads::Threads)
