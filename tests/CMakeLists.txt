# Tests for the Spine module, built with it.
#
# They link nx::spine and not the module target: what is under test is the
# runtime, which knows nothing about an Engine, so none of this needs one.

add_executable(nx_spine_tests test_spine_assets.cpp test_spine_draw.cpp
        test_spine_events.cpp)

target_link_libraries(nx_spine_tests
        PRIVATE nx::test nx::foundation nx::spine nx::compile_options)

# spineboy lives in the source tree and is mounted rather than copied: it is a
# fixture, and its licence forbids commercial use, so it must not travel in a
# staged build.
target_compile_definitions(nx_spine_tests PRIVATE
        NX_TEST_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")

set_target_properties(nx_spine_tests PROPERTIES FOLDER "tests")

nxtest_discover_tests(nx_spine_tests LABELS spine)
