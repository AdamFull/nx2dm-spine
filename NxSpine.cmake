
set(NX_SPINE2D_REPOSITORY "https://github.com/EsotericSoftware/spine-runtimes.git"
    CACHE STRING "Where to fetch the Spine runtimes from")

set(NX_SPINE2D_TAG "51d8d78b5414645875c2641630a6f4cdb2737440"
    CACHE STRING "spine-runtimes commit to build against (4.3)")

set(NX_SPINE2D_SOURCE_DIR "" CACHE PATH
    "An existing spine-runtimes or spine-cpp checkout. Empty fetches NX_SPINE2D_TAG.")

function(_nx_spine_resolve root out_var)
  foreach(candidate "${root}/spine-cpp/spine-cpp" "${root}/spine-cpp" "${root}")
    if(EXISTS "${candidate}/include/spine/Skeleton.h")
      set(${out_var} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(nx_add_spine)
  if(NOT NX_SPINE2D_SOURCE_DIR)
    _nx_spine_resolve("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/third_party/spine-cpp" _local)
    if(_local)
      set(NX_SPINE2D_SOURCE_DIR "${_local}")
    endif()
  endif()

  if(NX_SPINE2D_SOURCE_DIR)
    _nx_spine_resolve("${NX_SPINE2D_SOURCE_DIR}" _spine_dir)
    if(NOT _spine_dir)
      message(FATAL_ERROR
        "NX_SPINE2D_SOURCE_DIR='${NX_SPINE2D_SOURCE_DIR}' holds no spine-cpp: "
        "expected include/spine/Skeleton.h there or under spine-cpp/spine-cpp.")
    endif()
    message(STATUS "nx2d: Spine from ${_spine_dir}")
  else()
    include(FetchContent)
    find_package(Git 2.25 REQUIRED)
    # FetchContent's shallow Git mode clones the repository's current branch
    # before checkout, so an older pinned object is not necessarily present.
    # Fetch the requested object directly and materialize only spine-cpp.
    FetchContent_Declare(spine_runtimes
      DOWNLOAD_COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DREPOSITORY=${NX_SPINE2D_REPOSITORY}"
        "-DREVISION=${NX_SPINE2D_TAG}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FetchSpine.cmake"
      UPDATE_COMMAND ""
      SOURCE_SUBDIR "cmake-is-not-here")
    message(STATUS "nx2d: fetching exact sparse Spine revision ${NX_SPINE2D_TAG}")
    FetchContent_MakeAvailable(spine_runtimes)
    _nx_spine_resolve("${spine_runtimes_SOURCE_DIR}" _spine_dir)
    if(NOT _spine_dir)
      message(FATAL_ERROR
        "fetched spine-runtimes but found no spine-cpp under "
        "${spine_runtimes_SOURCE_DIR}; has the layout moved?")
    endif()
  endif()

  # Our own target rather than spine-cpp's CMakeLists: that one does include(../flags.cmake), which
  # sets flags of its own choosing, and this way the runtime is built with the project's warning and
  # sanitizer settings like everything else.
  file(GLOB _spine_sources CONFIGURE_DEPENDS "${_spine_dir}/src/spine/*.cpp")
  if(NOT _spine_sources)
    message(FATAL_ERROR "no spine-cpp sources under ${_spine_dir}/src/spine")
  endif()

  add_library(nx_spine_cpp STATIC ${_spine_sources})
  add_library(nx::spine_cpp ALIAS nx_spine_cpp)
  target_include_directories(nx_spine_cpp SYSTEM PUBLIC "${_spine_dir}/include")

  # The upstream runtime intentionally uses the portable CRT APIs. Keep the
  # MSVC deprecation policy private to this dependency instead of weakening
  # diagnostics on nx2d targets.
  if(MSVC)
    target_compile_definitions(nx_spine_cpp PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()

  # Physics_None returns before the switch in PhysicsConstraint::update, but
  # Clang still diagnoses that enumerator as unhandled. Contain the upstream
  # false positive to that source file; every other Spine and nx2d switch keeps
  # the configured warning policy.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_property(SOURCE "${_spine_dir}/src/spine/PhysicsConstraint.cpp"
      APPEND PROPERTY COMPILE_OPTIONS -Wno-switch)
  endif()

  # Reading is nx::vfs's job: on Android the assets are inside the APK and
  # there is no path fopen could take. Private because it is read in exactly
  # one .cpp and changes no declaration.
  target_compile_definitions(nx_spine_cpp PRIVATE SPINE_NO_FILE_IO)
  set_target_properties(nx_spine_cpp PROPERTIES FOLDER "third_party")

  set(NX_SPINE2D_DIR "${_spine_dir}" CACHE INTERNAL "resolved spine-cpp root")
endfunction()
