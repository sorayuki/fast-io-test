cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
project(fast-read VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_PREFIX_PATH "D:/dev/boost_1_87_0/dist-vc/lib/cmake/Boost-1.87.0")
find_package(Boost CONFIG REQUIRED)
add_executable(fast-read)
target_sources(fast-read PRIVATE
    main.cpp
)
target_link_libraries(fast-read PRIVATE Boost::boost)