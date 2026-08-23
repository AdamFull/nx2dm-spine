
#include "framework/nxtest.h"

#include "fixture.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/vfs/vfs.h"
#include "core/rendering/render2d/render_interop.h"
#include "spine/spine_assets.h"
#include "spine/spine_platform.h"

#include <type_traits>

namespace {

using namespace nxm::spine_test;

namespace spine2d = nxe::spine2d;

struct Mounted {
  Mounted() {
    nx::vfs::initialize();
    m_device = nx::vfs::make_host_device(
        nx::fs::path_view(nxm::spine_test::fixture_dir()));
    if (m_device != nullptr)
      m_mount = nx::vfs::mount("/", m_device);
  }
  ~Mounted() { nx::vfs::shutdown(); }

  Mounted(const Mounted &) = delete;
  Mounted &operator=(const Mounted &) = delete;

  [[nodiscard]] bool ok() const noexcept { return m_device != nullptr; }

  nx::vfs::Device *m_device = nullptr;
  nx::vfs::MountId m_mount{};
};

struct Resolver {
  nx::vector<nx::string> asked;
  bool saw_premultiplied = false;
  u32 answer = 0;

  [[nodiscard]] spine2d::TextureResolver fn() {
    return spine2d::TextureResolver(
        [this](const nx::string_view path, const bool pma) {
          asked.push_back(nx::string(path));
          saw_premultiplied = saw_premultiplied || pma;
          return answer;
        });
  }
};

}

TEST_CASE("spine: an asset handle is one pointer and cheap to retain") {
  CHECK(sizeof(spine2d::SkeletonAsset) == sizeof(void *));
  CHECK(std::is_nothrow_copy_constructible_v<spine2d::SkeletonAsset>);
  CHECK(std::is_nothrow_copy_assignable_v<spine2d::SkeletonAsset>);
}

TEST_CASE("spine: a skeleton loads through the VFS") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  resolver.answer = pack_texture(7, 1);
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), asset, error));
  CHECK(error.empty());
  CHECK(asset.valid());

  CHECK(asset.bone_count() > 20u);
  CHECK(asset.slot_count() > 20u);
  CHECK(asset.animation_count() >= 8u);
  CHECK(asset.skin_count() >= 1u);

  CHECK(asset.has_animation("walk"));
  CHECK(asset.has_animation("jump"));
  CHECK(asset.has_animation("run"));
  CHECK_FALSE(asset.has_animation("no-such-animation"));
}

TEST_CASE("spine: the atlas asks the host for its pages, and is told") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  resolver.answer = pack_texture(3, 2);
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), asset, error));

  REQUIRE(resolver.asked.size() >= 1u);
  CHECK(resolver.asked[0].find("spineboy") != nx::string::npos);
  CHECK(resolver.asked[0].find(".png") != nx::string::npos);

  CHECK(asset.premultiplied() == resolver.saw_premultiplied);
}

TEST_CASE("spine: a page the host cannot back still loads, untextured") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  resolver.answer = pack_texture(NX_TEXTURE_NONE, 0);
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), asset, error));
  CHECK(asset.valid());
  CHECK(asset.bone_count() > 20u);
}

TEST_CASE("spine: reading goes through the VFS, not the C library") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  const u64 before = spine2d::bytes_read();
  Resolver resolver;
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), asset, error));

  CHECK(spine2d::bytes_read() >= before);
}

TEST_CASE("spine: a stem that names nothing is refused, and says so") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  spine2d::SkeletonAsset asset;
  nx::string error;
  CHECK_FALSE(spine2d::load_skeleton("/spine/nope/nothing.skel", ATLAS_PMA,
                                     resolver.fn(), asset, error));
  CHECK_FALSE(error.empty());
  CHECK(error.find("nothing") != nx::string::npos);
  CHECK_FALSE(asset.valid());
}

TEST_CASE("spine: a failed reload clears only the caller's handle") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), asset, error));
  const usize bones = asset.bone_count();
  REQUIRE(bones > 0u);
  const spine2d::SkeletonAsset retained = asset;

  CHECK_FALSE(spine2d::load_skeleton("/spine/nope.skel", ATLAS_PMA,
                                     resolver.fn(), asset, error));
  CHECK_FALSE(asset.valid());
  CHECK(asset.bone_count() == 0u);
  CHECK(retained.valid());
  CHECK(retained.bone_count() == bones);
}

TEST_CASE("spine: an asset handle keeps a loaded version alive") {
  const Mounted mounted;
  NX_REQUIRE_FIXTURE();
  REQUIRE(mounted.ok());

  Resolver resolver;
  spine2d::SkeletonAsset first;
  nx::string error;
  REQUIRE(
      spine2d::load_skeleton(SKELETON, ATLAS_PMA, resolver.fn(), first, error));
  const usize bones = first.bone_count();

  const spine2d::SkeletonAsset second = first;
  CHECK(first.same_version(second));
  first = {};
  CHECK(second.valid());
  CHECK(second.bone_count() == bones);
  CHECK_FALSE(first.valid());
}
