# The Spine 2D runtime, which is the one dependency that is neither vendored
# nor a submodule.
#
# The other twenty-two are submodules because every build needs them, so a
# recursive clone pulling them is right. This one is different on two counts.
# It is optional, and its licence says "each user of the Products must obtain
# their own Spine Editor license" - so a submodule entry, which lands in every
# recursive clone whether or not NX_MODULE_SPINE is on, would put a Spine
# obligation in front of people who will never enable it. Fetching only when
# asked is what ties the dependency to the switch.
#
# spine-runtimes carries every runtime it ships - C, C#, Unity, Unreal, Godot,
# Haxe, TypeScript - and their example assets, so the download is far larger
# than the eighty-eight source files we compile. NX_SPINE2D_SOURCE_DIR points
# at a checkout you already have and skips it.

set(NX_SPINE2D_REPOSITORY "https://github.com/EsotericSoftware/spine-runtimes.git"
    CACHE STRING "Where to fetch the Spine runtimes from")

# A commit rather than the 4.3 branch head: spine-cpp carries no tags of its
# own, and a branch would change the runtime under us between two builds of the
# same nx2d revision. Bump deliberately, and only against data exported by a
# matching editor - 4.3 data does not load in a 4.2 runtime, and the reverse is
# no better.
set(NX_SPINE2D_TAG "51d8d78b5414645875c2641630a6f4cdb2737440"
    CACHE STRING "spine-runtimes commit to build against (4.3)")

set(NX_SPINE2D_SOURCE_DIR "" CACHE PATH
    "An existing spine-runtimes or spine-cpp checkout. Empty fetches NX_SPINE2D_TAG.")

# Three layouts are accepted, most specific first: the root of a spine-runtimes
# checkout (4.3 keeps the runtime in spine-cpp/, older ones in
# spine-cpp/spine-cpp/), and the runtime folder on its own, which is what
# someone who copied only that will have.
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
  # A checkout already sitting in third_party/spine-cpp wins over the fetch
  # without anyone having to say so. It is gitignored, so putting one there is
  # how you avoid a half-gigabyte download per build directory; deleting it
  # goes back to fetching.
  if(NOT NX_SPINE2D_SOURCE_DIR)
    _nx_spine_resolve("${CMAKE_SOURCE_DIR}/third_party/spine-cpp" _local)
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
    # SOURCE_SUBDIR names a directory with no CMakeLists.txt on purpose:
    # MakeAvailable then downloads without add_subdirectory'ing the whole
    # spine-runtimes tree, which would build runtimes for six other languages.
    FetchContent_Declare(spine_runtimes
      GIT_REPOSITORY "${NX_SPINE2D_REPOSITORY}"
      GIT_TAG "${NX_SPINE2D_TAG}"
      GIT_SHALLOW TRUE
      GIT_SUBMODULES ""
      GIT_PROGRESS TRUE
      SOURCE_SUBDIR "cmake-is-not-here")
    message(STATUS "nx2d: fetching Spine ${NX_SPINE2D_TAG} (this is a large repository)")
    FetchContent_MakeAvailable(spine_runtimes)
    _nx_spine_resolve("${spine_runtimes_SOURCE_DIR}" _spine_dir)
    if(NOT _spine_dir)
      message(FATAL_ERROR
        "fetched spine-runtimes but found no spine-cpp under "
        "${spine_runtimes_SOURCE_DIR}; has the layout moved?")
    endif()
  endif()

  # Our own target rather than spine-cpp's CMakeLists: that one does
  # include(../flags.cmake), which sets flags of its own choosing, and this way
  # the runtime is built with the project's warning and sanitizer settings like
  # everything else.
  file(GLOB _spine_sources CONFIGURE_DEPENDS "${_spine_dir}/src/spine/*.cpp")
  if(NOT _spine_sources)
    message(FATAL_ERROR "no spine-cpp sources under ${_spine_dir}/src/spine")
  endif()

  add_library(nx_spine_cpp STATIC ${_spine_sources})
  add_library(nx::spine_cpp ALIAS nx_spine_cpp)
  # SYSTEM so its warnings never reach an engine build with
  # NX_WARNINGS_AS_ERRORS on.
  target_include_directories(nx_spine_cpp SYSTEM PUBLIC "${_spine_dir}/include")
  # Reading is nx::vfs's job: on Android the assets are inside the APK and
  # there is no path fopen could take. Private because it is read in exactly
  # one .cpp and changes no declaration.
  target_compile_definitions(nx_spine_cpp PRIVATE SPINE_NO_FILE_IO)
  set_target_properties(nx_spine_cpp PROPERTIES FOLDER "third_party")

  set(NX_SPINE2D_DIR "${_spine_dir}" CACHE INTERNAL "resolved spine-cpp root")
endfunction()
