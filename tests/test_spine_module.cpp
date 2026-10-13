#include "framework/nxtest.h"

#include "fixture.h"

#include "engine/stub_platform.h"

#include "app/engine.h"
#include "app/module_system/module.h"
#include "core/foundation/vfs/vfs.h"
#include "spine/spine_assets.h"
#include "spine/spine_module.h"

#include <spine/AnimationState.h>

namespace {

using namespace nxe;
using namespace nxm::spine_test;

void quiet_configure(EngineConfig &config) {
  config.calibrate = false;
  config.action_map = {};
  config.physics_rules = {};
  config.audio = false;
}

struct Harness {
  test::StubPlatform platform;
  std::unique_ptr<Engine> engine;
  bool ready = false;

  Harness() {
    engine = std::make_unique<Engine>(Game{.configure = quiet_configure});
    for (const ModuleFactory factory : enabled_module_factories())
      engine->add_module(factory());
    platform.set_assets_path(NX_TEST_RUNTIME_DIR);
    rt::AppConfig config;
    engine->configure(config);
    ready = engine->on_create(platform);
  }
  ~Harness() {
    if (engine && ready)
      engine->on_destroy();
  }

  Harness(const Harness &) = delete;
  Harness &operator=(const Harness &) = delete;
};

/// How far @p e's first track has played.
[[nodiscard]] f32 track_time(scene::Scene &scene, const scene::Entity e) {
  spine2d::SpineInstance &instance =
      scene.registry().get<spine2d::SpineInstance>(e);
  return instance.animation()->getTrack(0)->getTrackTime();
}

} // namespace

TEST_CASE("spine: a preview scene's skeletons play while the game is paused, "
          "and a game scene's wait for it") {
  NX_REQUIRE_FIXTURE();
  Harness h;
  if (!h.ready)
    SKIP("no usable RHI device");
  spine2d::SpineSystem *const system = spine2d::system(*h.engine);
  REQUIRE(system != nullptr);

  const nx::vfs::MountId fixture = nx::vfs::mount(
      "/fixture", nx::vfs::make_host_device(nx::fs::path_view(fixture_dir())),
      100);
  REQUIRE(fixture.valid());
  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(spine2d::load_skeleton(
      nx::string("/fixture") + nx::string(SKELETON),
      nx::string("/fixture") + nx::string(ATLAS_PMA),
      spine2d::TextureResolver([](nx::string_view, bool) {
        return NxTexture2D<float4>::from_indices(1, 0);
      }),
      asset, error));

  const auto walking = [&](const scene::SceneId id) {
    scene::Scene &held = *h.engine->scene(id);
    const scene::Entity e = held.create_node("walker");
    REQUIRE(system->attach(held.registry(), e, asset).play("walk"));
    return e;
  };
  const scene::SceneId game = h.engine->create_scene();
  const scene::SceneId preview = h.engine->create_preview_scene();
  const scene::Entity in_game = walking(game);
  const scene::Entity in_preview = walking(preview);

  h.engine->set_paused(true);
  for (u32 i = 0; i < 5; ++i)
    h.engine->on_tick(h.platform, 1.0 / 60.0);
  CHECK(track_time(*h.engine->scene(preview), in_preview) > 0.f);
  CHECK(track_time(*h.engine->scene(game), in_game) == 0.f);

  h.engine->set_paused(false);
  for (u32 i = 0; i < 5; ++i)
    h.engine->on_tick(h.platform, 1.0 / 60.0);
  CHECK(track_time(*h.engine->scene(game), in_game) > 0.f);

  asset = spine2d::SkeletonAsset();
  nx::vfs::unmount(fixture);
}

TEST_CASE("spine: a skeleton a preview shows stays cached when the game's "
          "last one goes") {
  NX_REQUIRE_FIXTURE();
  Harness h;
  if (!h.ready)
    SKIP("no usable RHI device");
  spine2d::SpineSystem *const system = spine2d::system(*h.engine);
  REQUIRE(system != nullptr);
  const nx::vfs::MountId fixture = nx::vfs::mount(
      "/", nx::vfs::make_host_device(nx::fs::path_view(fixture_dir())), 100);
  REQUIRE(fixture.valid());

  scene::Scene &preview = *h.engine->scene(h.engine->create_preview_scene());
  const scene::Entity shown = preview.create_node("shown");
  nx::string error;
  REQUIRE(system->attach(preview.registry(), shown, BUNDLE, &error) != nullptr);
  scene::Scene &game = h.engine->scene();
  const scene::Entity played = game.create_node("played");
  REQUIRE(system->attach(game.registry(), played, BUNDLE, &error) != nullptr);
  for (u32 i = 0; i < 3; ++i)
    h.engine->on_tick(h.platform, 1.0 / 60.0);

  // The game's goes, which sweeps the cache: the preview's still uses it.
  game.destroy_node(played);
  for (u32 i = 0; i < 3; ++i)
    h.engine->on_tick(h.platform, 1.0 / 60.0);
  const spine2d::SkeletonAsset again = system->load(BUNDLE);
  CHECK(again.same_version(
      preview.registry().get<spine2d::SpineComponent>(shown).asset));
  nx::vfs::unmount(fixture);
}
