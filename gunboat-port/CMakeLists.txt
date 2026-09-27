cmake_minimum_required(VERSION 3.24)
project(gunboat_port VERSION 1.1.3 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3-3.4.16/cmake")
  list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL3-3.4.16/cmake")
endif()
find_package(SDL3 REQUIRED CONFIG)
set(GB_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Game" CACHE PATH "Folder with the original game files, for the tests")

# Nuked-OPL3 (LGPL-2.1), its own library; see THIRD_PARTY.md.
add_library(nuked_opl3 STATIC vendor/nuked-opl3/opl3.c)
target_include_directories(nuked_opl3 PUBLIC vendor/nuked-opl3)

set(GB_WARNINGS -Wall -Wextra -Wpedantic)

# The faithful core: memory model, loader and every ported function. No SDL here: the executable
# adds the SDL host (src/host.cpp) and the presentation layer (src/enhanced/: the launcher and the
# optional enhancements), the differential tests a stub host.
file(GLOB_RECURSE GB_CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER GB_CORE_SOURCES EXCLUDE REGEX "/src/(main|host)\\.cpp$|/src/enhanced/")
file(GLOB GB_ENHANCED_SOURCES CONFIGURE_DEPENDS src/enhanced/*.cpp)

# The AdLib sound effects' instruments the port ships with (data/sfx.ini; the editor saves it when
# it runs from this tree), built into the game and the editor as a string. A change of the file
# makes the next build configure again and rebuild them.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS data/sfx.ini)
file(READ data/sfx.ini GB_SFX_BANK)
file(WRITE ${CMAKE_BINARY_DIR}/generated/sfx_bank_default.inc.new "R\"GBSFX(${GB_SFX_BANK})GBSFX\"\n")
configure_file(${CMAKE_BINARY_DIR}/generated/sfx_bank_default.inc.new
               ${CMAKE_BINARY_DIR}/generated/sfx_bank_default.inc COPYONLY)

add_library(gbcore STATIC ${GB_CORE_SOURCES})
target_include_directories(gbcore PUBLIC src)
set_target_properties(gbcore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(gbcore PRIVATE ${GB_WARNINGS})
endif()

# The executables' file icon on Windows: the port's own (dist/gunboat.ico, tools/make_icon.py).
set(GB_ICON_RC "")
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_BINARY_DIR}/generated/gunboat.rc.new "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/dist/gunboat.ico\"\n")
  configure_file(${CMAKE_BINARY_DIR}/generated/gunboat.rc.new ${CMAKE_BINARY_DIR}/generated/gunboat.rc COPYONLY)
  set(GB_ICON_RC ${CMAKE_BINARY_DIR}/generated/gunboat.rc)
endif()

add_executable(gunboat src/main.cpp src/host.cpp ${GB_ENHANCED_SOURCES} ${GB_ICON_RC})
target_link_libraries(gunboat PRIVATE gbcore SDL3::SDL3 nuked_opl3)
target_include_directories(gunboat PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(gunboat PRIVATE GB_VERSION="${PROJECT_VERSION}")
if(NOT MSVC)
  target_compile_options(gunboat PRIVATE ${GB_WARNINGS})
  if(WIN32)
    target_link_options(gunboat PRIVATE -static -static-libgcc -static-libstdc++)
  else()
    target_link_options(gunboat PRIVATE -static-libgcc -static-libstdc++)  # runs without the compiler's libraries
  endif()
endif()
if(WIN32)
  add_custom_command(TARGET gunboat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:gunboat>)
endif()

# The sound-effects editor (tools/sfx_editor): the AdLib instruments of the effects. Dear ImGui (MIT,
# vendor/imgui) for its window; the core runs the original's effects driver for the notes, with the
# tests' stub host as its host.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui/imgui.cpp")
  add_library(imgui STATIC vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp vendor/imgui/imgui_tables.cpp
              vendor/imgui/imgui_widgets.cpp vendor/imgui/backends/imgui_impl_sdl3.cpp
              vendor/imgui/backends/imgui_impl_sdlrenderer3.cpp)
  target_include_directories(imgui PUBLIC vendor/imgui vendor/imgui/backends)
  target_link_libraries(imgui PUBLIC SDL3::SDL3)
  add_executable(gunboat_sfx_editor WIN32 tools/sfx_editor/main.cpp src/enhanced/sfx_fm.cpp
                 src/enhanced/settings.cpp tests/difftest/host_stub.cpp ${GB_ICON_RC})
  target_include_directories(gunboat_sfx_editor PRIVATE tests/difftest ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(gunboat_sfx_editor PRIVATE gbcore imgui nuked_opl3 SDL3::SDL3)
  if(NOT MSVC)
    target_compile_options(gunboat_sfx_editor PRIVATE ${GB_WARNINGS})
    if(WIN32)
      target_link_options(gunboat_sfx_editor PRIVATE -static -static-libgcc -static-libstdc++)
    else()
      target_link_options(gunboat_sfx_editor PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
endif()

# The controller mapping tool (tools/controller_editor): released with the game, so that players choose
# what each control of their controller does. Dear ImGui for its window; no game code.
if(TARGET imgui)
  add_executable(gunboat_controller WIN32 tools/controller_editor/main.cpp src/enhanced/controls.cpp ${GB_ICON_RC})
  target_include_directories(gunboat_controller PRIVATE src)
  target_link_libraries(gunboat_controller PRIVATE imgui SDL3::SDL3)
  if(NOT MSVC)
    target_compile_options(gunboat_controller PRIVATE ${GB_WARNINGS})
    if(WIN32)
      target_link_options(gunboat_controller PRIVATE -static -static-libgcc -static-libstdc++)
    else()
      target_link_options(gunboat_controller PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
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
