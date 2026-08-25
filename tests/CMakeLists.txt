
add_executable(nx_spine_tests test_spine_assets.cpp test_spine_asset_bundle.cpp
        test_spine_draw.cpp
        test_spine_events.cpp test_spine_render.cpp test_spine_scripting.cpp)

nx_module_registry(nx_spine_tests)

target_link_libraries(nx_spine_tests
        PRIVATE nx::test nx::foundation nx::spine nx::rhi nx::stb
        nx::engine nx::compile_options)
# This module-heavy image exceeds an incremental-linker edge on MSVC: link.exe
# can leave its import table zeroed, so the process crashes before main().
target_link_options(nx_spine_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/INCREMENTAL:NO>)

set(NX_SPINE_FIXTURE_DIR "" CACHE PATH
        "Directory holding spine/spineboy/export/; empty stages the bundled spineboy")
if (NX_SPINE_FIXTURE_DIR STREQUAL "")
    set(_spine_assets "${CMAKE_CURRENT_SOURCE_DIR}/../templates/skeleton/assets/spine")
    set(_spine_export "${CMAKE_CURRENT_BINARY_DIR}/fixture/spine/spineboy/export")
    configure_file("${_spine_assets}/spineboy-pro.skel"  "${_spine_export}/spineboy-pro.skel"  COPYONLY)
    configure_file("${_spine_assets}/spineboy-pma.atlas" "${_spine_export}/spineboy-pma.atlas" COPYONLY)
    configure_file("${_spine_assets}/spineboy-pma.png"   "${_spine_export}/spineboy-pma.png"   COPYONLY)
    configure_file("${_spine_assets}/spineboy.nxspine"  "${_spine_export}/spineboy.nxspine"  COPYONLY)
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

if (TARGET assetc AND NOT ANDROID AND NOT IOS)
    set(_spine_cooked "${CMAKE_CURRENT_BINARY_DIR}/cooked")
    add_custom_target(nx_spine_test_cooked_assets
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_spine_cooked}"
            COMMAND "$<TARGET_FILE:assetc>"
                    "${CMAKE_CURRENT_SOURCE_DIR}/../templates/skeleton/assets"
                    "${_spine_cooked}"
                    "${CMAKE_CURRENT_BINARY_DIR}/cooked.manifest"
                    --root / --shipping --require-metadata
            DEPENDS assetc
            COMMENT "Cooking the bundled Spine skeleton for runtime tests"
            VERBATIM)
    set_target_properties(nx_spine_test_cooked_assets PROPERTIES FOLDER "tests")

    add_executable(nx_spine_shipping_asset_tests
            test_spine_assets_shipping.cpp
            ../spine_assets.cpp ../spine_asset_bundle.cpp
            ../spine_platform.cpp)
    target_include_directories(nx_spine_shipping_asset_tests PRIVATE
            "${CMAKE_SOURCE_DIR}/engine" "${CMAKE_SOURCE_DIR}/modules")
    target_link_libraries(nx_spine_shipping_asset_tests
            PRIVATE nx::test nx::foundation nx::render2d nx::spine_cpp
            nx::compile_options)
    target_compile_definitions(nx_spine_shipping_asset_tests PRIVATE
            NX_BUILD_SHIPPING=1
            NX_SPINE_COOKED_DIR="${_spine_cooked}"
            NX_SPINE_AUTHORED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../templates/skeleton/assets")
    add_dependencies(nx_spine_shipping_asset_tests nx_spine_test_cooked_assets)
    set_target_properties(nx_spine_shipping_asset_tests PROPERTIES
            FOLDER "tests")
    nxtest_discover_tests(nx_spine_shipping_asset_tests
            PREFIX "shipping: " LABELS spine)
endif ()
