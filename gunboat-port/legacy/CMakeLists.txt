# Codex prototype (reference only, not the faithful port): an invented patrol mode on float physics,
# plus translations of original routines that phase 4 re-expresses in the mem[] model. See README.md.
add_library(td3_host_legacy STATIC vendor/td3/host.c vendor/td3/platform/vga.c)
target_include_directories(td3_host_legacy PUBLIC vendor/td3)
target_link_libraries(td3_host_legacy PUBLIC SDL3::SDL3 nuked_opl3)

add_executable(gunboat_legacy src/main.cpp src/assets.cpp src/simulation.cpp src/game.cpp
               src/original_renderer.cpp src/original_audio.cpp src/original_controls.cpp
               src/original_physics.cpp)
target_include_directories(gunboat_legacy PRIVATE src)
target_link_libraries(gunboat_legacy PRIVATE td3_host_legacy)
set_target_properties(gunboat_legacy PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
if(NOT MSVC)
  target_compile_options(gunboat_legacy PRIVATE -Wall -Wextra -Wpedantic)
  if(WIN32)
    target_link_options(gunboat_legacy PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endif()

add_test(NAME legacy_assets COMMAND gunboat_legacy --game-dir "${GB_GAME_DIR}" --check)
add_test(NAME legacy_simulation COMMAND gunboat_legacy --game-dir "${GB_GAME_DIR}" --self-test)
