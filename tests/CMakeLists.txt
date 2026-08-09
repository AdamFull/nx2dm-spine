# Tests for the Spine module, built with it.
#
# Mostly they link nx::spine and not the module target: what is under test is
# the runtime, which knows nothing about an Engine.

add_executable(nx_spine_tests test_spine_assets.cpp test_spine_draw.cpp
        test_spine_events.cpp test_spine_render.cpp)

# nx::rhi and nx::stb for the one case that draws. nx_spine itself links
# neither: proving a skeleton reaches a framebuffer needs a device to reach,
# and a PNG writer to look at what landed.
target_link_libraries(nx_spine_tests
        PRIVATE nx::test nx::foundation nx::spine nx::rhi nx::stb
        nx::compile_options)

# spineboy lives in the source tree and is mounted rather than copied: it is a
# fixture, and its licence forbids commercial use, so it must not travel in a
# staged build.
if (ANDROID)
    set(NX_TEST_SHADERS "${CMAKE_SOURCE_DIR}/android/app/src/main/assets/shaders")
else ()
    set(NX_TEST_SHADERS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
endif ()
target_compile_definitions(nx_spine_tests PRIVATE
        NX_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
        NX_TEST_SHADER_DIR="${NX_TEST_SHADERS}")
if (TARGET nx_rendering_shaders)
    add_dependencies(nx_spine_tests nx_rendering_shaders)
endif ()

set_target_properties(nx_spine_tests PROPERTIES FOLDER "tests")

nxtest_discover_tests(nx_spine_tests LABELS spine)
