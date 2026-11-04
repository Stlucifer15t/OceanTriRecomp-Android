cmake_minimum_required(VERSION 3.25)
project(OceanTriCheatManagerTests LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The runtime headers require Clang; configure with CMAKE_CXX_COMPILER=clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(mh3_cheat_manager_tests
  test_cheat_manager.cpp
  test_memory.cpp
  ../../runtime/src/cheat_manager.cpp
  ../../runtime/src/custom_codes.cpp
  ../../runtime/src/cheats.cpp
  ../../runtime/src/guest_flat_memory.cpp)
target_include_directories(mh3_cheat_manager_tests PRIVATE ../../runtime/include ../../runtime/src ../..)
target_compile_options(mh3_cheat_manager_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-unused-parameter)
target_link_libraries(mh3_cheat_manager_tests PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME mh3_cheat_manager_tests COMMAND mh3_cheat_manager_tests)

add_executable(mh3_cheat_effects_tests
  test_cheat_effects.cpp
  test_memory.cpp
  generated_guest_stubs.cpp
  ../../runtime/src/cheats.cpp
  ../../runtime/src/cheat_data.cpp
  ../../runtime/src/cheat_items.cpp
  ../../runtime/src/cheat_hunter.cpp
  ../../runtime/src/cheat_monsters.cpp
  ../../runtime/src/guest_flat_memory.cpp)
target_include_directories(mh3_cheat_effects_tests PRIVATE ../../runtime/include ../../runtime/src ../..)
target_compile_options(mh3_cheat_effects_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-unused-parameter)
target_link_libraries(mh3_cheat_effects_tests PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME mh3_cheat_effects_tests COMMAND mh3_cheat_effects_tests)

add_executable(mh3_custom_codes_tests
  test_custom_codes.cpp
  test_memory.cpp
  ../../runtime/src/custom_codes.cpp
  ../../runtime/src/guest_flat_memory.cpp)
target_include_directories(mh3_custom_codes_tests PRIVATE ../../runtime/include ../../runtime/src ../..)
target_compile_options(mh3_custom_codes_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-unused-parameter)
target_link_libraries(mh3_custom_codes_tests PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME mh3_custom_codes_tests COMMAND mh3_custom_codes_tests)

add_executable(mh3_mod_manager_tests
  test_mod_manager.cpp
  ../../runtime/src/mod_manager.cpp)
target_include_directories(mh3_mod_manager_tests PRIVATE ../../runtime/include ../../runtime/src ../..)
target_compile_options(mh3_mod_manager_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-unused-parameter)
target_link_libraries(mh3_mod_manager_tests PRIVATE pthread)
add_test(NAME mh3_mod_manager_tests COMMAND mh3_mod_manager_tests)
