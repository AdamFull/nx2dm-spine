/**
 * @file test_spine_draw.cpp
 * @brief Posing spineboy and turning him into mesh draws, with no GPU.
 *
 * Everything here runs against the real fixture rather than a hand-built
 * skeleton: the interesting failures - a pose that never moves, a transform
 * that never applies, premultiplication read from the wrong atlas - all look
 * fine on geometry someone made up.
 */

#include "framework/nxtest.h"

#include "fixture.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/vfs/vfs.h"
#include "core/rendering/render2d/scene_renderer.h"
#include "spine/spine_assets.h"
#include "spine/spine_system.h"

#include <spine/BlendMode.h>
#include <spine/SkeletonData.h>
#include <spine/SlotData.h>

namespace {

using namespace nxm::spine_test;

namespace spine2d = nxe::spine2d;
namespace scene = nxe::scene;
namespace r2d = nxe::r2d;

/// The asset tree mounted, with one skeleton loaded out of it.
struct Loaded {
  explicit Loaded(const nx::string_view atlas = ATLAS_PMA,
                  const u32 texture = nx::cast<u32>(pack_texture(7, 1))) {
    nx::vfs::initialize();
    m_device = nx::vfs::make_host_device(
        nx::fs::path_view(nxm::spine_test::fixture_dir()));
    if (m_device == nullptr)
      return;
    nx::vfs::mount("/", m_device);
    nx::string error;
    m_ok = spine2d::load_skeleton(
        SKELETON, atlas,
        spine2d::TextureResolver(
            [texture](nx::string_view, bool) { return texture; }),
        asset, error);
  }
  ~Loaded() {
    asset = spine2d::SkeletonAsset();
    nx::vfs::shutdown();
  }

  Loaded(const Loaded &) = delete;
  Loaded &operator=(const Loaded &) = delete;

  [[nodiscard]] bool ok() const noexcept { return m_ok; }

  spine2d::SkeletonAsset asset;
  nx::vfs::Device *m_device = nullptr;
  bool m_ok = false;
};

/// A registry that has been told what a node and a skeleton are.
[[nodiscard]] scene::registry_t bare_registry() {
  scene::registry_t registry;
  registry.register_component<scene::WorldTransform2D>(
      {.name = "WorldTransform2D"});
  spine2d::SpineSystem::register_components(registry);
  return registry;
}

/// A node at @p at carrying a skeleton, already playing @p animation.
scene::Entity place(scene::registry_t &registry, spine2d::SpineSystem &system,
                    const spine2d::SkeletonAsset &asset, const glm::vec2 at,
                    const nx::string_view animation = "walk") {
  const scene::Entity e = registry.create();
  scene::WorldTransform2D &world = registry.emplace<scene::WorldTransform2D>(e);
  world.world[2][0] = at.x;
  world.world[2][1] = at.y;
  system.attach(registry, e, asset).play(animation);
  return e;
}

[[nodiscard]] glm::vec2 centroid(const r2d::MeshChannel &channel) {
  glm::vec2 sum(0.f);
  for (const r2d::MeshVertex &v : channel.vertices)
    sum += v.position;
  return channel.vertices.empty()
             ? sum
             : sum / nx::cast<f32>(channel.vertices.size());
}

} // namespace

TEST_CASE("spine: a posed skeleton becomes mesh draws") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  place(registry, system, loaded.asset, {0.f, 0.f});

  CHECK(system.update(registry, 0.1f) == 1u);

  r2d::MeshChannel channel;
  const usize draws = system.emit(registry, channel, {});
  REQUIRE(draws > 0u);
  CHECK(channel.draws.size() == draws);
  CHECK(!channel.vertices.empty());

  // Triangles, and every index inside the draw's own vertex block: a mesh
  // that indexes past its vertices reads whatever the next skeleton wrote.
  for (const r2d::MeshDraw &draw : channel.draws) {
    CHECK(draw.index_count % 3u == 0u);
    const usize end = nx::cast<usize>(draw.first_index) + draw.index_count;
    REQUIRE(end <= channel.indices.size());
    u32 highest = 0;
    for (usize i = draw.first_index; i < end; ++i)
      highest = nx::max(highest, channel.indices[i]);
    CHECK(nx::cast<usize>(draw.vertex_offset) + highest <
          channel.vertices.size());
  }
}

TEST_CASE("spine: every draw of one skeleton carries the same key") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 3.f});
  registry.get<spine2d::SpineComponent>(e).layer = 5;

  // spineboy is one page and one colour, so the runtime batches him into a
  // single command. Give a slot in the middle its own blend mode and the
  // batch has to split - which is the only way this case sees more than one
  // draw, and the only way it can say anything about their order.
  loaded.asset.data()->getSlots()[26]->setBlendMode(
      ::spine::BlendMode_Additive);
  system.update(registry, 0.1f);

  const spine2d::SpineView view{2u, -1024.f, 1024.f};
  r2d::MeshChannel channel;
  REQUIRE(system.emit(registry, channel, view) > 1u);

  // One key for the whole character. Slots have to draw in their own order,
  // and the mesh sort is stable only within a run of equal keys - a key that
  // varied per slot would let the sort reorder a face behind its head.
  const u32 expected = nx_make_sort_key(
      nx::cast<u32>(5 + 2048),
      r2d::quantize_depth(3.f, view.depth_min, view.depth_max), 0u);
  for (const r2d::MeshDraw &draw : channel.draws) {
    CHECK(draw.sort_key == expected);
    CHECK(draw.camera == 2u);
    CHECK(draw.texture == pack_texture(7, 1));
  }

  // And the split really is a blend split: the middle run draws additively
  // over the ones either side of it.
  usize additive = 0;
  for (const r2d::MeshDraw &draw : channel.draws)
    if (draw.blend == r2d::MeshBlend::AdditivePremultiplied)
      ++additive;
  CHECK(additive == 1u);
}

TEST_CASE("spine: two skeletons sort by layer, then by depth") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  // Behind on the y axis but on a higher layer, so layer has to win.
  const scene::Entity front = place(registry, system, loaded.asset, {0.f, 8.f});
  place(registry, system, loaded.asset, {0.f, 0.f});
  registry.get<spine2d::SpineComponent>(front).layer = 1;
  system.update(registry, 0.1f);

  r2d::MeshChannel channel;
  REQUIRE(system.emit(registry, channel, {}) > 0u);

  nx::vector<r2d::MeshDraw> sorted(channel.draws.begin(), channel.draws.end());
  r2d::sort_draws(sorted);
  const u32 back_key = sorted.front().sort_key;
  const u32 front_key = sorted.back().sort_key;
  CHECK(back_key < front_key);
  CHECK((front_key >> 20) == nx::cast<u32>(1 + 2048));
  CHECK((back_key >> 20) == nx::cast<u32>(2048));
}

TEST_CASE("spine: the node's transform is what places the skeleton") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  system.update(registry, 0.1f);

  r2d::MeshChannel at_origin;
  REQUIRE(system.emit(registry, at_origin, {}) > 0u);

  // Move the node without touching the pose. Skeleton space is the node's
  // local space, so every vertex has to move with it and by exactly as much.
  registry.get<scene::WorldTransform2D>(e).world[2][0] = 100.f;
  r2d::MeshChannel moved;
  REQUIRE(system.emit(registry, moved, {}) > 0u);
  REQUIRE(moved.vertices.size() == at_origin.vertices.size());
  for (usize i = 0; i < moved.vertices.size(); ++i) {
    CHECK(moved.vertices[i].position.x ==
          nxtest::Approx(at_origin.vertices[i].position.x + 100.f));
    CHECK(moved.vertices[i].position.y ==
          nxtest::Approx(at_origin.vertices[i].position.y));
  }

  // And scale, which a transform applied as a translation alone would miss.
  registry.get<scene::WorldTransform2D>(e).world[2][0] = 0.f;
  registry.get<scene::WorldTransform2D>(e).world[0][0] = 0.5f;
  registry.get<scene::WorldTransform2D>(e).world[1][1] = 0.5f;
  r2d::MeshChannel scaled;
  REQUIRE(system.emit(registry, scaled, {}) > 0u);
  CHECK(centroid(scaled).y == nxtest::Approx(centroid(at_origin).y * 0.5f));
}

TEST_CASE("spine: an animation actually moves the pose") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  place(registry, system, loaded.asset, {0.f, 0.f});
  system.update(registry, 0.f);

  r2d::MeshChannel first;
  REQUIRE(system.emit(registry, first, {}) > 0u);

  // Half a second into a walk cycle nothing is where it was. An update that
  // never applied the state would leave the setup pose behind, vertex for
  // vertex.
  system.update(registry, 0.5f);
  r2d::MeshChannel later;
  REQUIRE(system.emit(registry, later, {}) > 0u);
  REQUIRE(later.vertices.size() == first.vertices.size());

  usize moved = 0;
  for (usize i = 0; i < later.vertices.size(); ++i)
    if (later.vertices[i].position != first.vertices[i].position)
      ++moved;
  CHECK(moved > later.vertices.size() / 4);
}

TEST_CASE("spine: time_scale is what a frozen character is frozen by") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  registry.get<spine2d::SpineComponent>(e).time_scale = 0.f;
  system.update(registry, 0.f);

  r2d::MeshChannel first;
  REQUIRE(system.emit(registry, first, {}) > 0u);
  system.update(registry, 0.5f);
  r2d::MeshChannel later;
  REQUIRE(system.emit(registry, later, {}) > 0u);

  REQUIRE(later.vertices.size() == first.vertices.size());
  for (usize i = 0; i < later.vertices.size(); ++i)
    CHECK(later.vertices[i].position == first.vertices[i].position);
}

TEST_CASE("spine: a premultiplied atlas picks a different blend and colour") {
  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  r2d::MeshChannel premultiplied;
  r2d::MeshChannel straight;

  {
    const Loaded loaded(ATLAS_PMA);
    NX_REQUIRE_FIXTURE();
    REQUIRE(loaded.ok());
    REQUIRE(loaded.asset.premultiplied());
    const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
    registry.get<spine2d::SpineComponent>(e).color = {1.f, 1.f, 1.f, 0.5f};
    system.update(registry, 0.1f);
    REQUIRE(system.emit(registry, premultiplied, {}) > 0u);
    registry.clear();
  }

  {
    const Loaded loaded(ATLAS_STRAIGHT);
    NX_REQUIRE_FIXTURE();
    REQUIRE(loaded.ok());
    REQUIRE_FALSE(loaded.asset.premultiplied());
    const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
    registry.get<spine2d::SpineComponent>(e).color = {1.f, 1.f, 1.f, 0.5f};
    system.update(registry, 0.1f);
    REQUIRE(system.emit(registry, straight, {}) > 0u);
    registry.clear();
  }

  // Same skeleton, same tint: only which atlas it was paired with differs, and
  // getting this wrong haloes every edge on screen.
  CHECK(premultiplied.draws[0].blend == r2d::MeshBlend::NormalPremultiplied);
  CHECK(straight.draws[0].blend == r2d::MeshBlend::Normal);

  const u32 pma_colour = premultiplied.vertices[0].color;
  const u32 straight_colour = straight.vertices[0].color;
  CHECK((pma_colour >> 24) == (straight_colour >> 24));
  CHECK((pma_colour & 0xFFu) < (straight_colour & 0xFFu));
}

TEST_CASE("spine: a page nobody could back draws untextured") {
  NX_REQUIRE_FIXTURE();
  nx::vfs::initialize();
  nx::vfs::Device *const device = nx::vfs::make_host_device(
      nx::fs::path_view(nxm::spine_test::fixture_dir()));
  REQUIRE(device != nullptr);
  nx::vfs::mount("/", device);

  spine2d::SkeletonAsset asset;
  nx::string error;
  // No resolver at all, which is the loader's own fallback rather than an
  // answer it was given.
  REQUIRE(spine2d::load_skeleton(SKELETON, ATLAS_PMA, {}, asset, error));

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  place(registry, system, asset, {0.f, 0.f});
  system.update(registry, 0.1f);

  r2d::MeshChannel channel;
  REQUIRE(system.emit(registry, channel, {}) > 0u);
  // The shader tests the high half for kTextureNone. A bare NX_TEXTURE_NONE
  // in the low half reads as texture zero, which samples whatever happens to
  // be in the first bindless slot.
  for (const r2d::MeshDraw &draw : channel.draws)
    CHECK((draw.texture >> 16) == NX_TEXTURE_NONE);

  registry.clear();
  asset = spine2d::SkeletonAsset();
  nx::vfs::shutdown();
}

TEST_CASE("spine: an invisible character emits nothing") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  system.update(registry, 0.1f);

  registry.get<spine2d::SpineComponent>(e).visible = false;
  r2d::MeshChannel channel;
  CHECK(system.emit(registry, channel, {}) == 0u);
  CHECK(channel.empty());

  // Still posed, though: hiding a character must not stop its animation, or
  // it reappears wherever it was hidden.
  CHECK(system.update(registry, 0.1f) == 1u);
}

TEST_CASE("spine: a component that arrived without a pose gets one") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  // What loading a .nxscene leaves behind: authoring data and no runtime.
  const scene::Entity e = registry.create();
  registry.emplace<scene::WorldTransform2D>(e);
  registry.emplace<spine2d::SpineComponent>(e).asset = loaded.asset;
  CHECK_FALSE(registry.has<spine2d::SpineInstance>(e));

  CHECK(system.update(registry, 0.1f) == 1u);
  REQUIRE(registry.has<spine2d::SpineInstance>(e));
  CHECK(registry.get<spine2d::SpineInstance>(e).valid());

  r2d::MeshChannel channel;
  CHECK(system.emit(registry, channel, {}) > 0u);
}

TEST_CASE("spine: a component and pose retain their asset version") {
  Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  REQUIRE(registry.get<spine2d::SpineInstance>(e).play("walk"));

  // The game-side handle may be reloaded or destroyed while this entity is
  // alive. Both ECS records retain the immutable version they actually use.
  loaded.asset = {};
  CHECK(registry.get<spine2d::SpineComponent>(e).asset.valid());
  CHECK(system.update(registry, 0.1f) == 1u);

  r2d::MeshChannel channel;
  CHECK(system.emit(registry, channel, {}) > 0u);
}

TEST_CASE("spine: replacing a component asset rebuilds a coherent pose") {
  Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  REQUIRE(registry.get<spine2d::SpineInstance>(e).premultiplied());

  spine2d::SkeletonAsset replacement;
  nx::string error;
  REQUIRE(spine2d::load_skeleton(
      SKELETON, ATLAS_STRAIGHT,
      spine2d::TextureResolver([](nx::string_view, bool) {
        return nx::cast<u32>(pack_texture(8, 1));
      }),
      replacement, error));
  REQUIRE_FALSE(replacement.premultiplied());

  registry.get<spine2d::SpineComponent>(e).asset = replacement;
  CHECK(system.update(registry, 0.1f) == 1u);
  const spine2d::SpineInstance &instance =
      registry.get<spine2d::SpineInstance>(e);
  CHECK(instance.uses(replacement));
  CHECK_FALSE(instance.premultiplied());

  r2d::MeshChannel channel;
  REQUIRE(system.emit(registry, channel, {}) > 0u);
  CHECK(channel.draws[0].blend == r2d::MeshBlend::Normal);
}

TEST_CASE("spine: clearing a component asset removes its stale pose") {
  Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset, {0.f, 0.f});
  REQUIRE(registry.has<spine2d::SpineInstance>(e));

  registry.get<spine2d::SpineComponent>(e).asset = {};
  CHECK(system.update(registry, 0.1f) == 0u);
  CHECK_FALSE(registry.has<spine2d::SpineInstance>(e));
}

TEST_CASE("spine: an invalid component cannot retain a runtime pose") {
  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = registry.create();
  registry.emplace<spine2d::SpineComponent>(e);
  registry.emplace<spine2d::SpineInstance>(e);
  REQUIRE(registry.has<spine2d::SpineInstance>(e));

  CHECK(system.update(registry, 0.1f) == 0u);
  CHECK_FALSE(registry.has<spine2d::SpineInstance>(e));
}

TEST_CASE("spine: an animation the skeleton does not have is refused") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = registry.create();
  registry.emplace<scene::WorldTransform2D>(e);
  spine2d::SpineInstance &instance = system.attach(registry, e, loaded.asset);

  // spine-cpp asserts on a name it cannot find, so a game reading names out of
  // data would take the process down with it.
  CHECK_FALSE(instance.play("no-such-animation"));
  CHECK_FALSE(instance.queue("no-such-animation"));
  CHECK(instance.play("walk"));
  CHECK(instance.queue("jump", false, 0.5f));
  CHECK_FALSE(instance.set_skin("no-such-skin"));
}

TEST_CASE("spine: many skeletons pose the same way across a pool") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  constexpr usize COUNT = spine2d::SpineSystem::PARALLEL_THRESHOLD * 4;

  nx::thread_pool pool({.thread_count = 4});

  scene::registry_t serial_registry = bare_registry();
  spine2d::SpineSystem serial;
  scene::registry_t pooled_registry = bare_registry();
  spine2d::SpineSystem pooled(&pool);
  for (usize i = 0; i < COUNT; ++i) {
    place(serial_registry, serial, loaded.asset, {0.f, 0.f});
    place(pooled_registry, pooled, loaded.asset, {0.f, 0.f});
  }

  CHECK(serial.update(serial_registry, 0.25f) == COUNT);
  CHECK(pooled.update(pooled_registry, 0.25f) == COUNT);

  r2d::MeshChannel one;
  r2d::MeshChannel many;
  REQUIRE(serial.emit(serial_registry, one, {}) > 0u);
  REQUIRE(pooled.emit(pooled_registry, many, {}) > 0u);

  // Posing across workers must produce the same skeleton, vertex for vertex:
  // the pool is a way to spend less time, not a different result.
  REQUIRE(many.vertices.size() == one.vertices.size());
  for (usize i = 0; i < many.vertices.size(); ++i)
    CHECK(many.vertices[i].position == one.vertices[i].position);
}

TEST_CASE("spine: a skeleton stands up from its node, not down") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  place(registry, system, loaded.asset, {0.f, 0.f});
  system.update(registry, 0.1f);

  r2d::MeshChannel channel;
  REQUIRE(system.emit(registry, channel, {}) > 0u);

  // spineboy's origin is between his feet and he is 686 units tall, so almost
  // all of him is above the node. spine-cpp defaults Bone::yDown to true,
  // which negates the skeleton's y scale and hands back exactly this shape
  // mirrored - and every other case here compares one pose against another,
  // so all of them pass just as happily with the character on his head.
  glm::vec2 lo(1e30f);
  glm::vec2 hi(-1e30f);
  for (const r2d::MeshVertex &v : channel.vertices) {
    lo = glm::min(lo, v.position);
    hi = glm::max(hi, v.position);
  }
  CHECK(hi.y > 500.f);
  CHECK(lo.y > -100.f);
  // Wider than nothing but far taller than wide, which is what says the box is
  // a person rather than an axis mix-up.
  CHECK(hi.x - lo.x > 200.f);
  CHECK(hi.y - lo.y > hi.x - lo.x);
}
