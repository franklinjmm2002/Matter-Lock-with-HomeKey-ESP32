if(ESP_PLATFORM)
idf_component_register(
    SRCS "src/pn532.cpp"
         "src/transaction.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver loggable fmt
)

else()
cmake_minimum_required(VERSION 4.1)

project(pn532_cxx)

set(CMAKE_CXX_STANDARD 17)

add_library(pn532_cxx src/pn532.cpp src/transaction.cpp)

target_include_directories(pn532_cxx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

include(FetchContent)

FetchContent_Declare(
  loggable
  GIT_REPOSITORY https://github.com/rednblkx/loggable-thing.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(loggable)

target_link_libraries(pn532_cxx PUBLIC loggable)

endif()
