#include "framework/nxtest.h"

#include "spine/spine_assets.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/vfs/vfs.h"

namespace {

constexpr nx::string_view BUNDLE = "/spine/spineboy.nxspine";

struct MountedHost {
  bool ok = false;

  explicit MountedHost(const nx::string_view root) {
    if (!nx::vfs::initialize())
      return;
    nx::vfs::Device *const host =
        nx::vfs::make_host_device(nx::fs::path_view(root));
    ok = host != nullptr && nx::vfs::mount("/", host).valid();
  }
  ~MountedHost() { nx::vfs::shutdown(); }
};

} // namespace

TEST_CASE("spine assets: Shipping loads the atomic cooked skeleton") {
  MountedHost files(NX_SPINE_COOKED_DIR);
  REQUIRE(files.ok);
  nxe::spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(nxe::spine2d::load_skeleton(BUNDLE, {}, asset, error));
  CHECK(error.empty());
  CHECK(asset.valid());
  CHECK(asset.bone_count() > 20u);
  CHECK(asset.has_animation("walk"));
}

TEST_CASE("spine assets: Shipping rejects authored descriptors and pairs") {
  MountedHost files(NX_SPINE_AUTHORED_DIR);
  REQUIRE(files.ok);
  nxe::spine2d::SkeletonAsset asset;
  nx::string error;
  CHECK_FALSE(nxe::spine2d::load_skeleton(BUNDLE, {}, asset, error));
  CHECK_FALSE(error.empty());
  CHECK_FALSE(asset.valid());

  error.clear();
  CHECK_FALSE(nxe::spine2d::load_skeleton("/spine/spineboy-pro.skel",
                                          "/spine/spineboy-pma.atlas", {},
                                          asset, error));
  CHECK_FALSE(error.empty());
}
