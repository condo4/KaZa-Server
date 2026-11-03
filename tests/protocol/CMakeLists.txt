cmake_minimum_required(VERSION 3.16)
project(tst_kazaprotocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Test)

add_executable(tst_kazaprotocol
    tst_kazaprotocol.cpp
    ../../protocol/kazaprotocol.cpp
    ../../protocol/kazaprotocol.h
)
target_include_directories(tst_kazaprotocol PRIVATE ../../protocol)
target_link_libraries(tst_kazaprotocol PRIVATE Qt6::Core Qt6::Network Qt6::Test)

enable_testing()
add_test(NAME tst_kazaprotocol COMMAND tst_kazaprotocol)
