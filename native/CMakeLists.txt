cmake_minimum_required(VERSION 3.13)

project(consteval C)
set(CMAKE_C_STANDARD 11)

add_executable(${PROJECT_NAME}
        consteval.c
        )
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/..
        )
