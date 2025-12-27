cmake_minimum_required(VERSION 3.16)
project(zygisk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -llog -ldl -pie -fPIC")

include_directories(src/main/cpp)
file(GLOB SRC src/main/cpp/*.cpp)

add_library(zygisk SHARED ${SRC})
target_link_libraries(zygisk log dl android)
