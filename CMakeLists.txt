
option(NX_MODULE_SPINE "Build the Spine 2D runtime integration" OFF)

# The host cooker is available for every installed module format even when the
# current target does not enable the Spine runtime.
nx_asset_cooker(spine
        SOURCES spine_cooker.cpp spine_asset_bundle.cpp)

if (NOT NX_MODULE_SPINE)
    return()
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/NxSpine.cmake")
nx_add_spine()

set(NX_SPINE_HEADERS spine_assets.h spine_component.h spine_platform.h
        spine_system.h spine_asset_bundle.h)
set(NX_SPINE_SOURCES spine_assets.cpp spine_component.cpp spine_platform.cpp
        spine_system.cpp spine_asset_bundle.cpp)

add_library(nx_spine STATIC ${NX_SPINE_SOURCES} ${NX_SPINE_HEADERS})
add_library(nx::spine ALIAS nx_spine)

target_include_directories(nx_spine PUBLIC "${NX_ENGINE_ROOT}/engine"
        "${NX_ENGINE_ROOT}/modules")

target_link_libraries(nx_spine
        PUBLIC nx::foundation nx::render2d nx::spine_cpp
        PRIVATE nx::compile_options)

set_target_properties(nx_spine PROPERTIES FOLDER "modules")

nx_module(spine
        DISPLAY "the Spine 2D runtime integration"
        SOURCES spine_module.cpp spine_scripting.cpp
        DEPENDS nx::spine)

if (NX_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
