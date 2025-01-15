cmake_minimum_required(VERSION 3.5)

project(Oraker LANGUAGES CXX)

find_package(OpenCV 4.7.0 REQUIRED)
find_package(OpenVINO 2023.3.0 REQUIRED COMPONENTS Runtime)

find_library(APPLICATION_SERVICES ApplicationServices)
if (NOT APPLICATION_SERVICES)
    message(FATAL_ERROR "ApplicationServices not found")
endif()

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/input_stream.cpp
)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
# target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${APPLICATION_SERVICES}
        ${OpenCV_LIBS}
        openvino::runtime
        pthread
)
