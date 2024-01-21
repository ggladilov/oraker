cmake_minimum_required(VERSION 3.5)

project(Oraker LANGUAGES CXX)

find_package(OpenCV 4.8.0 REQUIRED)
find_library(APPLICATION_SERVICES ApplicationServices)
if (NOT APPLICATION_SERVICES)
    message(FATAL_ERROR "ApplicationServices not found")
endif()

add_executable(${PROJECT_NAME} main.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
# target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
target_link_libraries(${PROJECT_NAME} ${APPLICATION_SERVICES} ${OpenCV_LIBS})
