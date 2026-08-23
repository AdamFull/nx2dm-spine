
#include "framework/nxtest.h"

#include "fixture.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/vfs/vfs.h"
#include "spine/spine_assets.h"
#include "spine/spine_system.h"

#include <spine/ClippingAttachment.h>
#include <spine/Skeleton.h>
#include <spine/Slot.h>
#include <spine/SlotPose.h>

namespace {

using namespace nxm::spine_test;

namespace spine2d = nxe::spine2d;
namespace scene = nxe::scene;
namespace r2d = nxe::r2d;

struct Loaded {
  Loaded() {
    nx::vfs::initialize();
    m_device = nx::vfs::make_host_device(
        nx::fs::path_view(nxm::spine_test::fixture_dir()));
    if (m_device == nullptr)
      return;
    nx::vfs::mount("/", m_device);
    nx::string error;
    m_ok = spine2d::load_skeleton(
        SKELETON, ATLAS_PMA,
        spine2d::TextureResolver(
            [](nx::string_view, bool) { return pack_texture(1, 0); }),
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

[[nodiscard]] scene::registry_t bare_registry() {
  scene::registry_t registry;
  registry.register_component<scene::WorldTransform2D>(
      {.name = "WorldTransform2D"});
  spine2d::SpineSystem::register_components(registry);
  return registry;
}

scene::Entity place(scene::registry_t &registry, spine2d::SpineSystem &system,
                    const spine2d::SkeletonAsset &asset) {
  const scene::Entity e = registry.create();
  registry.emplace<scene::WorldTransform2D>(e);
  system.attach(registry, e, asset);
  return e;
}

[[nodiscard]] usize count_of(const spine2d::SpineSystem &system,
                             const spine2d::SpineEventKind kind) {
  usize n = 0;
  for (const spine2d::SpineEvent &event : system.events())
    if (event.kind == kind)
      ++n;
  return n;
}

}

TEST_CASE("spine: a keyed event arrives with its name and its time") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset);
  REQUIRE(registry.get<spine2d::SpineInstance>(e).play("walk"));

  system.update(registry, 0.9f);

  usize footsteps = 0;
  for (const spine2d::SpineEvent &event : system.events()) {
    if (event.kind != spine2d::SpineEventKind::Custom)
      continue;
    ++footsteps;
    CHECK(event.name == "footstep");
    CHECK(event.animation == "walk");
    CHECK(event.entity == e);
    CHECK(event.track == 0u);
  }
  CHECK(footsteps == 2u);
}

TEST_CASE("spine: a track reports starting, completing and ending") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset);
  spine2d::SpineInstance &instance = registry.get<spine2d::SpineInstance>(e);

  REQUIRE(instance.play("walk"));
  system.update(registry, 0.f);
  CHECK(count_of(system, spine2d::SpineEventKind::Started) == 1u);

  system.update(registry, 2.f);
  CHECK(count_of(system, spine2d::SpineEventKind::Completed) >= 1u);
  CHECK(count_of(system, spine2d::SpineEventKind::Ended) == 0u);

  instance.stop();
  system.update(registry, 0.f);
  CHECK(count_of(system, spine2d::SpineEventKind::Ended) == 1u);
}

TEST_CASE("spine: events are drained, not accumulated") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset);
  REQUIRE(registry.get<spine2d::SpineInstance>(e).play("walk"));

  system.update(registry, 0.9f);
  REQUIRE(!system.events().empty());

  system.update(registry, 0.f);
  CHECK(system.events().empty());
}

TEST_CASE("spine: every skeleton's events name the skeleton that fired them") {
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  nx::thread_pool pool({.thread_count = 4});
  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system(&pool);

  constexpr usize COUNT = spine2d::SpineSystem::PARALLEL_THRESHOLD * 4;
  nx::vector<scene::Entity> entities;
  for (usize i = 0; i < COUNT; ++i) {
    const scene::Entity e = place(registry, system, loaded.asset);
    REQUIRE(registry.get<spine2d::SpineInstance>(e).play("walk"));
    entities.push_back(e);
  }

  CHECK(system.update(registry, 0.9f) == COUNT);

  nx::vector<usize> per_entity(COUNT, 0u);
  for (const spine2d::SpineEvent &event : system.events()) {
    if (event.kind != spine2d::SpineEventKind::Custom)
      continue;
    for (usize i = 0; i < COUNT; ++i)
      if (entities[i] == event.entity)
        ++per_entity[i];
  }
  for (const usize fired : per_entity)
    CHECK(fired == 2u);
}

TEST_CASE("spine: a clipping attachment removes what it covers") {
  ::spine::ClippingAttachment clip(::spine::String("test-clip"));
  const Loaded loaded;
  NX_REQUIRE_FIXTURE();
  REQUIRE(loaded.ok());

  scene::registry_t registry = bare_registry();
  spine2d::SpineSystem system;
  const scene::Entity e = place(registry, system, loaded.asset);
  REQUIRE(registry.get<spine2d::SpineInstance>(e).play("walk"));
  system.update(registry, 0.1f);

  r2d::MeshChannel whole;
  REQUIRE(system.emit(registry, whole, {}) > 0u);
  REQUIRE(whole.indices.size() > 0u);

  ::spine::Array<f32> vertices;
  const f32 box[] = {8000.f, 8000.f, 9000.f, 8000.f,
                     9000.f, 9000.f, 8000.f, 9000.f};
  for (const f32 v : box)
    vertices.add(v);
  clip.setVertices(vertices);
  clip.setWorldVerticesLength(8);
  clip.setEndSlot(nullptr);

  ::spine::Skeleton &skeleton =
      *registry.get<spine2d::SpineInstance>(e).skeleton();
  skeleton.getDrawOrder().getAppliedPose()[0]->getAppliedPose().setAttachment(
      &clip);

  r2d::MeshChannel clipped;
  system.emit(registry, clipped, {});
  CHECK(clipped.indices.size() < whole.indices.size() / 2);

  skeleton.getDrawOrder().getAppliedPose()[0]->getAppliedPose().setAttachment(
      nullptr);
}
