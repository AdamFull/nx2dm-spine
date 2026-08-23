
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
