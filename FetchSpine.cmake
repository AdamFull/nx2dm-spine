cmake_minimum_required(VERSION 3.21)

foreach(_required GIT_EXECUTABLE REPOSITORY REVISION SOURCE_DIR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "nx2d: Spine fetch requires ${_required}")
  endif()
endforeach()

cmake_path(ABSOLUTE_PATH SOURCE_DIR NORMALIZE OUTPUT_VARIABLE _source_dir)
cmake_path(GET _source_dir FILENAME _source_leaf)
if(NOT _source_leaf STREQUAL "spine_runtimes-src")
  message(FATAL_ERROR
    "nx2d: refusing to replace unexpected Spine fetch directory '${_source_dir}'")
endif()

function(_nx_spine_git description)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_TERMINAL_PROMPT=0"
            "${GIT_EXECUTABLE}" ${ARGN}
    RESULT_VARIABLE _result)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "nx2d: ${description} failed with exit code ${_result}")
  endif()
endfunction()

# FetchContent may leave a partial clone behind after an interrupted configure.
# This directory is generated for this dependency alone; rebuilding it makes
# retries deterministic and avoids accidentally accepting stale objects.
file(REMOVE_RECURSE "${_source_dir}")
file(MAKE_DIRECTORY "${_source_dir}")

_nx_spine_git("initializing the Spine checkout"
  init --quiet "${_source_dir}")
_nx_spine_git("adding the Spine remote"
  -C "${_source_dir}" remote add origin "${REPOSITORY}")
_nx_spine_git("enabling sparse checkout for Spine"
  -C "${_source_dir}" sparse-checkout init --cone)
_nx_spine_git("selecting spine-cpp"
  -C "${_source_dir}" sparse-checkout set spine-cpp)
_nx_spine_git("fetching Spine revision ${REVISION}"
  -c protocol.version=2 -C "${_source_dir}"
  fetch --depth=1 --filter=blob:none --no-tags --progress origin "${REVISION}")
_nx_spine_git("checking out Spine revision ${REVISION}"
  -c advice.detachedHead=false -C "${_source_dir}"
  checkout --detach --force FETCH_HEAD)

execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${_source_dir}" rev-parse HEAD FETCH_HEAD
  RESULT_VARIABLE _resolve_result
  OUTPUT_VARIABLE _resolved_revisions
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _resolve_result EQUAL 0)
  message(FATAL_ERROR "nx2d: could not verify the fetched Spine revision")
endif()

string(REPLACE "\n" ";" _resolved_revisions "${_resolved_revisions}")
list(LENGTH _resolved_revisions _resolved_count)
if(NOT _resolved_count EQUAL 2)
  message(FATAL_ERROR "nx2d: Git returned an unexpected Spine revision result")
endif()
list(GET _resolved_revisions 0 _head_revision)
list(GET _resolved_revisions 1 _fetched_revision)
if(NOT "${_head_revision}" STREQUAL "${_fetched_revision}")
  message(FATAL_ERROR
    "nx2d: checked out Spine ${_head_revision}, expected fetched object ${_fetched_revision}")
endif()

string(LENGTH "${REVISION}" _revision_length)
if(_revision_length EQUAL 40 AND REVISION MATCHES "^[0-9a-fA-F]+$")
  string(TOLOWER "${REVISION}" _requested_revision)
  string(TOLOWER "${_head_revision}" _actual_revision)
  if(NOT "${_actual_revision}" STREQUAL "${_requested_revision}")
    message(FATAL_ERROR
      "nx2d: checked out Spine ${_actual_revision}, expected ${_requested_revision}")
  endif()
endif()

if(NOT EXISTS "${_source_dir}/spine-cpp/include/spine/Skeleton.h"
   AND NOT EXISTS "${_source_dir}/spine-cpp/spine-cpp/include/spine/Skeleton.h")
  message(FATAL_ERROR
    "nx2d: fetched Spine revision has no spine-cpp runtime")
endif()

message(STATUS "nx2d: fetched sparse Spine runtime at ${_head_revision}")
