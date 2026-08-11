#pragma once

/**
 * @file fixture.h
 * @brief Where spineboy is, when he is anywhere (namespace nxm::spine_test).
 *
 * spineboy is Spine's own example art. A copy ships under the spine module
 * template, and the tests' build stages it into `spine/spineboy/export/`, so
 * these cases run out of a plain checkout. NX_SPINE_FIXTURE_DIR points them at
 * another export instead.
 *
 * The fixture can still be absent - a hand-set NX_SPINE_FIXTURE_DIR without the
 * art, say. Then a case skips rather than fails, like the rendering suite does
 * without a GPU, unless NX_REQUIRE_MODULE_FIXTURES turns that skip into a
 * failure so CI cannot go green having tested nothing.
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

/// Skips the case when the fixture is absent - or fails it, under
/// NX_REQUIRE_MODULE_FIXTURES, so a build that was meant to test spineboy and
/// found nothing says so. Every case here needs it: they are written against
/// spineboy's own numbers on purpose, because a loader that half worked would
/// produce plausible ones on invented data.
#if NX_REQUIRE_MODULE_FIXTURES
#define NX_REQUIRE_FIXTURE()                                                   \
  do {                                                                         \
    if (!::nxm::spine_test::have_fixture())                                    \
      FAIL("spineboy fixture absent but NX_REQUIRE_MODULE_FIXTURES is set");   \
  } while (false)
#else
#define NX_REQUIRE_FIXTURE()                                                   \
  do {                                                                         \
    if (!::nxm::spine_test::have_fixture())                                    \
      SKIP("no spineboy; set NX_SPINE_FIXTURE_DIR to a Spine export");         \
  } while (false)
#endif
