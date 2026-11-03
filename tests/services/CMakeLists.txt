cmake_minimum_required(VERSION 3.16)
project(tst_services LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Qml Sql Test)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(PROTO ${CMAKE_CURRENT_SOURCE_DIR}/../../protocol)

add_executable(tst_services
    tst_services.cpp
    ${SRC}/kzparam.cpp ${SRC}/kzservice.cpp ${SRC}/kazaelement.cpp ${SRC}/kzservicerequest.cpp ${SRC}/kazaservicemanager.cpp
    ${PROTO}/kazaprotocol.cpp
)
target_include_directories(tst_services PRIVATE ${SRC} ${PROTO})
target_link_libraries(tst_services PRIVATE Qt6::Core Qt6::Network Qt6::Qml Qt6::Sql Qt6::Test)

enable_testing()
add_test(NAME tst_services COMMAND tst_services)
