cmake_minimum_required(VERSION 3.10)
project(deribit_trading_system)

set(CMAKE_CXX_STANDARD 17)

# Find required packages
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Threads REQUIRED)

# Add the executable
add_executable(trading_system main.cpp)

# Include directories
target_include_directories(trading_system 
    PRIVATE 
    ${JSONCPP_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(trading_system
    PRIVATE
    CURL::libcurl
    ${JSONCPP_LIBRARIES}
    Boost::system
    Threads::Threads
)

# Add compiler flags
target_compile_options(trading_system
    PRIVATE
    ${JSONCPP_CFLAGS_OTHER}
)