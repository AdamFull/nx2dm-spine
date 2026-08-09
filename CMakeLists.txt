# The Spine 2D runtime, as a module.
#
# Two targets, not one. nx_spine is the runtime itself and links no engine and
# no RHI, so every loading and posing case runs headless; nx_module_spine is
# the twenty lines that tell an Engine about it. A module may build whatever it
# likes - nx_module() only cares about the one that carries NX_DECLARE_MODULE.
#
# The option is declared here rather than left to nx_module() because the
# dependency has to be fetched before the module can link it, and fetching a
# 500 MB repository for a module nobody enabled would be rude.

option(NX_MODULE_SPINE "Build the Spine 2D runtime integration" OFF)
if (NOT NX_MODULE_SPINE)
    return()
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/NxSpine.cmake")
nx_add_spine()

set(NX_SPINE_HEADERS spine_assets.h spine_component.h spine_platform.h
        spine_system.h)
set(NX_SPINE_SOURCES spine_assets.cpp spine_component.cpp spine_platform.cpp
        spine_system.cpp)

add_library(nx_spine STATIC ${NX_SPINE_SOURCES} ${NX_SPINE_HEADERS})
add_library(nx::spine ALIAS nx_spine)

target_include_directories(nx_spine PUBLIC "${CMAKE_SOURCE_DIR}/engine"
        "${CMAKE_SOURCE_DIR}/modules")

# render2d for the mesh channel and the sort key, and nx::scene comes with it
# for the registry the components live in. No RHI and no engine: a skeleton is
# geometry until something draws it.
target_link_libraries(nx_spine
        PUBLIC nx::foundation nx::render2d nx::spine_cpp
        PRIVATE nx::compile_options)

set_target_properties(nx_spine PROPERTIES FOLDER "modules")

nx_module(spine
        DISPLAY "the Spine 2D runtime integration"
        SOURCES spine_module.cpp
        DEPENDS nx::spine)

if (NX_BUILD_TESTS)
    add_subdirectory(tests)
endif ()
