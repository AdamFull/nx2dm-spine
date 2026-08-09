#pragma once

/**
 * @file fixture.h
 * @brief Where spineboy is, when he is anywhere (namespace nxm::spine_test).
 *
 * The fixture is not in the repository. It is Spine's own example art, and its
 * licence says the images "may not be used for commercial use of any kind", so
 * a checkout carries none of it and no build stages any.
 *
 * Point NX_SPINE_FIXTURE_DIR at a directory holding `spine/spineboy/export/`
 * to run these cases; without one they skip rather than fail, like the
 * rendering suite does without a GPU.
 */

#include "framework/nxtest.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/strings/utf8_string.h"

namespace nxm::spine_test {

inline constexpr nx::string_view SKELETON =
    "/spine/spineboy/export/spineboy-pro.skel";
inline constexpr nx::string_view ATLAS_PMA =
    "/spine/spineboy/export/spineboy-pma.atlas";
inline constexpr nx::string_view ATLAS_STRAIGHT =
    "/spine/spineboy/export/spineboy.atlas";

/// The host directory the VFS mounts at "/". Empty when the build was given
/// none.
[[nodiscard]] inline nx::string_view fixture_dir() noexcept {
  return NX_SPINE_FIXTURE_DIR;
}

/// Whether the skeleton these cases are written against is actually there.
[[nodiscard]] inline bool have_fixture() {
  if (fixture_dir().empty())
    return false;
  const nx::string path =
      nx::string(fixture_dir()) + "/spine/spineboy/export/spineboy-pro.skel";
  return nx::fs::exists(nx::fs::path_view(path));
}

} // namespace nxm::spine_test

/// Skips the case when the fixture is absent. Every case here needs it: they
/// are written against spineboy's own numbers on purpose, because a loader
/// that half worked would produce plausible ones on invented data.
#define NX_REQUIRE_FIXTURE()                                                   \
  do {                                                                         \
    if (!::nxm::spine_test::have_fixture())                                    \
      SKIP("no spineboy; set NX_SPINE_FIXTURE_DIR to a Spine export");         \
  } while (false)
