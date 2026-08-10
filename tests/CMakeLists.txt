# Tests for the Spine module, built with it.
#
# Mostly they link nx::spine and not the module target: what is under test is
# the runtime, which knows nothing about an Engine.

add_executable(nx_spine_tests test_spine_assets.cpp test_spine_draw.cpp
        test_spine_events.cpp test_spine_render.cpp test_spine_scripting.cpp)

# The scripting case needs the module and the generated registry that hands
# modules over: what it checks is that the module exposes on its own.
nx_module_registry(nx_spine_tests)

# nx::rhi and nx::stb for the one case that draws. nx_spine itself links
# neither: proving a skeleton reaches a framebuffer needs a device to reach,
# and a PNG writer to look at what landed.
target_link_libraries(nx_spine_tests
        PRIVATE nx::test nx::foundation nx::spine nx::rhi nx::stb
        nx::engine nx::compile_options)

# The fixture is not in this repository: spineboy is Spine's own example art and
# its licence forbids commercial use of the images, so nothing here carries or
# stages any. Point NX_SPINE_FIXTURE_DIR at a directory holding
# spine/spineboy/export/ to run these; without one every case skips.
set(NX_SPINE_FIXTURE_DIR "" CACHE PATH
        "Directory holding spine/spineboy/export/ for the Spine tests")

if (ANDROID)
    set(NX_TEST_SHADERS "${CMAKE_SOURCE_DIR}/android/app/src/main/assets/shaders")
else ()
    set(NX_TEST_SHADERS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
endif ()
target_compile_definitions(nx_spine_tests PRIVATE
        NX_SPINE_FIXTURE_DIR="${NX_SPINE_FIXTURE_DIR}"
        NX_TEST_SHADER_DIR="${NX_TEST_SHADERS}")
if (TARGET nx_rendering_shaders)
    add_dependencies(nx_spine_tests nx_rendering_shaders)
endif ()

set_target_properties(nx_spine_tests PROPERTIES FOLDER "tests")

nxtest_discover_tests(nx_spine_tests LABELS spine)
