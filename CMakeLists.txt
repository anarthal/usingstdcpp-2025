
cmake_minimum_required(VERSION 3.15.0)
project(usingstdcpp_2025)

find_package(boost_headers REQUIRED)
find_package(boost_charconv REQUIRED)
find_package(OpenSSL REQUIRED)

function(add_example EXE)
    add_executable(${EXE} ${EXE}.cpp)
    target_link_libraries(${EXE} PRIVATE Boost::headers Boost::charconv OpenSSL::SSL)
    target_compile_features(${EXE} PRIVATE cxx_std_20)
endfunction()

add_example(1_sync)
add_example(2_async)
add_example(3_parallel_requests)
add_example(4_timeouts)
