#pragma once

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
inline constexpr nx::string_view BUNDLE =
    "/spine/spineboy/export/spineboy.nxspine";

[[nodiscard]] inline nx::string_view fixture_dir() noexcept {
  return NX_SPINE_FIXTURE_DIR;
}

[[nodiscard]] inline bool have_fixture() {
  if (fixture_dir().empty())
    return false;
  const nx::string path =
      nx::string(fixture_dir()) + "/spine/spineboy/export/spineboy-pro.skel";
  return nx::fs::exists(nx::fs::path_view(path));
}

} // namespace nxm::spine_test

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
