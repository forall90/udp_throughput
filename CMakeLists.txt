cmake_minimum_required(VERSION 3.20.0)
project(udp_throughput VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(Boost_DEBUG ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_ARCHITECTURE "-x64")
set(Boost_ROOT C:/Dev/boost_1_85_0 CACHE PATH "Path to the Boost root directory")
find_package(Boost REQUIRED COMPONENTS system timer)

add_executable(sender sender.cpp)
target_link_libraries(sender Boost::system)
target_link_libraries(sender -static)

add_executable(receiver receiver.cpp)
target_link_libraries(receiver Boost::system Boost::timer)
target_link_libraries(receiver -static)

if(MINGW)
    target_link_libraries(sender ws2_32)
    target_link_libraries(receiver ws2_32)
endif()

target_precompile_headers(receiver 
    PRIVATE
    <chrono>
    <iomanip>
    <iostream>
    <thread>
    <boost/array.hpp>
    <boost/asio.hpp>
    <boost/timer/timer.hpp>
    )

target_precompile_headers(sender 
    PRIVATE
    <chrono>
    <iostream>
    <boost/array.hpp>
    <boost/asio.hpp>
    )
