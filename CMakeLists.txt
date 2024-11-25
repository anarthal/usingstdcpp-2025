
cmake_minimum_required(VERSION 3.15.0)
project(usingstdcpp_2025)

find_package(Boost REQUIRED COMPONENTS headers charconv)
find_package(OpenSSL REQUIRED)

function(add_example EXE)
    add_executable(${EXE} ${EXE}.cpp)
    target_link_libraries(${EXE} PRIVATE Boost::headers Boost::charconv OpenSSL::SSL)
    target_compile_features(${EXE} PRIVATE cxx_std_20)
endfunction()

add_example(cancellations)
