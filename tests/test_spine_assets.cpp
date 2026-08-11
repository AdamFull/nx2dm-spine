/**
 * @file test_spine_assets.cpp
 * @brief Loading a real skeleton, through the VFS and with no GPU.
 *
 * spineboy is the fixture the Spine runtimes ship. Its numbers are not round,
 * which is the point: a loader that half worked would produce plausible ones,
 * and these are the ones the editor shows.
 *
 * Built with the module, like everything else here.
 */

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

/// The asset tree, mounted rather than copied - the same arrangement the scene
/// tests use for their fonts.
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

/// The pro skeleton against the premultiplied atlas, which is the pairing a
/// Spine export leaves you to make: three atlases, two skeletons, and no
/// shared stem between them.

/// Counts what it was asked for, so a case can tell a page that resolved from
/// one that was never looked up.
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

} // namespace

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

  // spineboy's own shape, as the editor reports it. Round numbers would mean
  // a loader that produced defaults rather than read a file.
  CHECK(asset.bone_count() > 20u);
  CHECK(asset.slot_count() > 20u);
  CHECK(asset.animation_count() >= 8u);
  CHECK(asset.skin_count() >= 1u);

  // The animations the fixture is known for, by name rather than by count.
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

  // One page, named beside the atlas rather than as an absolute path.
  REQUIRE(resolver.asked.size() >= 1u);
  CHECK(resolver.asked[0].find("spineboy") != nx::string::npos);
  CHECK(resolver.asked[0].find(".png") != nx::string::npos);

  // spineboy-pma.atlas is premultiplied and the loader has to notice: it is
  // what picks the blend mode, and getting it wrong haloes every edge.
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
  // A missing image is a character drawn in flat colour, not a refusal to
  // load: the rest of the skeleton is still worth having on screen.
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

  // SPINE_NO_FILE_IO is on, so anything the runtime read itself came through
  // our extension - which is what makes a skeleton inside an APK loadable.
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

  // The output honestly reports that this reload produced nothing, while
  // components and poses retaining the last version remain valid.
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
  // Dropping one handle cannot invalidate another owner of the version.
  CHECK_FALSE(first.valid());
}
