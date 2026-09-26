cmake_minimum_required(VERSION 3.24)
project(gunboat_port VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3-3.4.16/cmake")
  list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3-3.4.16/cmake")
endif()
find_package(SDL3 REQUIRED CONFIG)
set(GB_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Original DOS version" CACHE PATH "Folder with the original game files, for the tests")

# Nuked-OPL3 (LGPL-2.1), its own library; see THIRD_PARTY.md.
add_library(nuked_opl3 STATIC vendor/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC vendor/nuked-opl3)

set(GB_WARNINGS -Wall -Wextra -Wpedantic)

# The faithful core: memory model, loader and every ported function. No SDL here: the executable
# adds the SDL host (src/host.cpp), the differential tests a stub host.
file(GLOB_RECURSE GB_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER GB_CORE_SOURCES EXCLUDE REGEX "/src/(main|host)\\.cpp$")
add_library(gbcore STATIC ${GB_CORE_SOURCES})
target_include_directories(gbcore PUBLIC src)
set_target_properties(gbcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(gbcore PRIVATE ${GB_WARNINGS})
endif()

add_executable(gunboat src/main.cpp src/host.cpp)
target_link_libraries(gunboat PRIVATE gbcore SDL3::SDL3 nuked_opl3)
if(NOT MSVC)
  target_compile_options(gunboat PRIVATE ${GB_WARNINGS})
  if(WIN32)
    target_link_options(gunboat PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()
if(WIN32)
  add_custom_command(TARGET gunboat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:gunboat>)
endif()

# Differential tests (tests/difftest): the core plus a stub host as a library that Python loads next
# to the original code in Unicorn. Development only.
file(GLOB GB_DIFFTEST_SOURCES CONFIGURE_DEPENDS tests/difftest/*.cpp)
add_library(gb_difftest SHARED ${GB_DIFFTEST_SOURCES})
target_include_directories(gb_difftest PRIVATE tests/difftest)
target_link_libraries(gb_difftest PRIVATE gbcore)
set_target_properties(gb_difftest PROPERTIES PREFIX "")
if(NOT MSVC)
  target_compile_options(gb_difftest PRIVATE ${GB_WARNINGS})
  if(WIN32)
    target_link_options(gb_difftest PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

enable_testing()

add_test(NAME load_exe COMMAND gunboat --game-dir "${GB_GAME_DIR}" --check)
add_test(NAME host_timer COMMAND gunboat --host-test)
set_tests_properties(host_timer PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME difftest COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tests/difftest/run_all.py"
           --dll "$<TARGET_FILE:gb_difftest>")
  set_tests_properties(difftest PROPERTIES TIMEOUT 1800)
endif()
