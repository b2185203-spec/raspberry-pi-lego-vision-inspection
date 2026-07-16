cmake_minimum_required(VERSION 3.16)

project(vision_project LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

set(ONNXRUNTIME_ROOT "" CACHE PATH "ONNX Runtime installation directory")

if(NOT ONNXRUNTIME_ROOT AND DEFINED ENV{ONNXRUNTIME_ROOT})
    set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}")
endif()

if(NOT EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "Please set ONNXRUNTIME_ROOT, for example: "
        "cmake -S . -B build -DONNXRUNTIME_ROOT=/path/to/onnxruntime"
    )
endif()

add_executable(gpio_test main.cpp)

target_include_directories(gpio_test PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${ONNXRUNTIME_ROOT}/include
)

target_link_directories(gpio_test PRIVATE
    ${ONNXRUNTIME_ROOT}/lib
)

target_link_libraries(gpio_test PRIVATE
    gpiodcxx
    onnxruntime
    ${OpenCV_LIBS}
)

# 运行时自动找到 libonnxruntime.so。
set_target_properties(gpio_test PROPERTIES
    BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib"
)

target_compile_options(gpio_test PRIVATE
    -Wall
    -Wextra
)
