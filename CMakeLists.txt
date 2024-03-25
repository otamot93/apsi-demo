cmake_minimum_required(VERSION 3.2.6)
project(ASPI-Demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(receiver_cli)
add_subdirectory(src/receiver)

add_executable(sender_cli)
add_subdirectory(src/sender)

add_library(common_cli OBJECT)
add_subdirectory(src/common)
target_include_directories(common_cli PUBLIC src)

find_package(absl CONFIG REQUIRED)
find_package(APSI CONFIG REQUIRED)
find_package(cppzmq CONFIG REQUIRED)
message(STATUS "apsi include dir is" ${APSI_INCLUDE_DIRS})

#target_compile_features(common_cli PUBLIC cxx_std_17)

target_link_libraries(receiver_cli PRIVATE absl::log APSI::apsi cppzmq cppzmq-static common_cli)
target_link_libraries(sender_cli PRIVATE absl::log APSI::apsi cppzmq cppzmq-static common_cli)
target_link_libraries(common_cli PUBLIC APSI::apsi)
#target_link_libraries(main PRIVATE APSI::apsi cppzmq cppzmq-static absl::log absl::base)