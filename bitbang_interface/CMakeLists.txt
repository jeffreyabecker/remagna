cmake_minimum_required(VERSION 3.16)
project(bitbang_interface LANGUAGES CXX)

add_library(bitbang_interface STATIC src/controller.cpp src/peripheral.cpp)
target_include_directories(bitbang_interface PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_compile_features(bitbang_interface PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(bitbang_interface PRIVATE /W4 /WX)
else()
  target_compile_options(bitbang_interface PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(bitbang_tests tests/test_bitbang.cpp)
  target_link_libraries(bitbang_tests PRIVATE bitbang_interface)
  target_compile_features(bitbang_tests PRIVATE cxx_std_17)

  if(MSVC)
    target_compile_options(bitbang_tests PRIVATE /W4 /WX)
  else()
    target_compile_options(bitbang_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()

  add_test(NAME bitbang_tests COMMAND bitbang_tests)
endif()
