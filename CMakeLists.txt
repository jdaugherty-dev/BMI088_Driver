cmake_minimum_required(VERSION 3.10)
project(bmi088_test C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(BOSCH_DIR ${CMAKE_SOURCE_DIR}/external/BMI08x_SensorAPI)

add_executable(bmi088_test
    src/main.c
    src/rpi_platform.c
    src/udp_stream.c
    ${BOSCH_DIR}/bmi08a.c
    ${BOSCH_DIR}/bmi08g.c
    ${BOSCH_DIR}/bmi08xa.c
)

target_include_directories(bmi088_test PRIVATE src ${BOSCH_DIR})
target_compile_options(bmi088_test PRIVATE -Wall -Wextra)

find_library(GPIOD_LIBRARY NAMES gpiod)
find_path(GPIOD_INCLUDE_DIR NAMES gpiod.h)

if(NOT GPIOD_LIBRARY OR NOT GPIOD_INCLUDE_DIR)
    message(FATAL_ERROR "libgpiod not found -- install it with: sudo apt install libgpiod-dev")
endif()

target_include_directories(bmi088_test PRIVATE ${GPIOD_INCLUDE_DIR})
target_link_libraries(bmi088_test PRIVATE ${GPIOD_LIBRARY} m)
