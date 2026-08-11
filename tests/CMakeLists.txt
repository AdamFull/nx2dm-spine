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

# spineboy is Spine's own example art, redistributable with the runtimes, and a
# copy already ships under the module template. When no private export is named
# it is staged into the layout the cases expect, so the suite runs by default;
# NX_SPINE_FIXTURE_DIR still points these at another export when set.
set(NX_SPINE_FIXTURE_DIR "" CACHE PATH
        "Directory holding spine/spineboy/export/; empty stages the bundled spineboy")
if (NX_SPINE_FIXTURE_DIR STREQUAL "")
    set(_spine_assets "${CMAKE_CURRENT_SOURCE_DIR}/../templates/skeleton/assets/spine")
    set(_spine_export "${CMAKE_CURRENT_BINARY_DIR}/fixture/spine/spineboy/export")
    configure_file("${_spine_assets}/spineboy-pro.skel"  "${_spine_export}/spineboy-pro.skel"  COPYONLY)
    configure_file("${_spine_assets}/spineboy-pma.atlas" "${_spine_export}/spineboy-pma.atlas" COPYONLY)
    configure_file("${_spine_assets}/spineboy-pma.png"   "${_spine_export}/spineboy-pma.png"   COPYONLY)
    # The straight-alpha atlas the blend case pairs against, derived from the
    # premultiplied one: same regions, only the page name and pma flag differ.
    # The draw cases resolve textures through a stub, so no straight PNG is read.
    file(READ "${_spine_assets}/spineboy-pma.atlas" _spine_pma_atlas)
    string(REPLACE "spineboy-pma.png" "spineboy.png" _spine_straight "${_spine_pma_atlas}")
    string(REPLACE "pma: true" "pma: false" _spine_straight "${_spine_straight}")
    file(WRITE "${_spine_export}/spineboy.atlas" "${_spine_straight}")
    set(NX_SPINE_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixture")
endif ()

if (ANDROID)
    set(NX_TEST_SHADERS "${NX_ANDROID_SHADER_OUT}")
else ()
    set(NX_TEST_SHADERS "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
endif ()
target_compile_definitions(nx_spine_tests PRIVATE
        NX_SPINE_FIXTURE_DIR="${NX_SPINE_FIXTURE_DIR}"
        NX_TEST_SHADER_DIR="${NX_TEST_SHADERS}"
        NX_REQUIRE_MODULE_FIXTURES=$<BOOL:${NX_REQUIRE_MODULE_FIXTURES}>)
if (TARGET nx_rendering_shaders)
    add_dependencies(nx_spine_tests nx_rendering_shaders)
endif ()

set_target_properties(nx_spine_tests PROPERTIES FOLDER "tests")

nxtest_discover_tests(nx_spine_tests LABELS spine)
