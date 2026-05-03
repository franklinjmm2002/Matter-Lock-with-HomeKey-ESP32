# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "Please set ESP_MATTER_PATH to the path of esp-matter repo")
endif()

set(PROJECT_VER "1.0")
set(PROJECT_VER_NUMBER 1)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components")

project(homekey_provisioner)

# Fix for Arduino-ESP32 I2C Slave compilation on ESP-IDF v5.4 with ESP32-C6
idf_build_set_property(COMPILE_OPTIONS "-Di2c_ll_slave_set_fifo_mode(a,b)=i2c_ll_enable_fifo_mode(a,b)" APPEND)
