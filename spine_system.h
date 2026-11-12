#pragma once

#include "core/foundation/containers/string_map.h"
#include "core/foundation/core/callable.h"
#include "core/foundation/math/names.h"
#include "core/foundation/threading/thread_pool.h"
#include "core/foundation/vfs/asset_pipeline.h"
#include "rendering/render2d/mesh_channel.h"
#include "spine/spine_component.h"

#include <span>

namespace spine {
class SkeletonRenderer;
}

namespace nxe::r2d {
class MaterialSystem;
}

namespace nxe::scene {
class AssetRegistry;
}

namespace nxe::spine2d {

struct GatheredSkeleton {
  SpineSource source;
  nx::string error;
  bool ok = false;
};

struct PreparedSkeleton {
  SkeletonAsset asset;
  nx::string error;
  bool ok = false;
};

using SkeletonLoadPipeline =
    nx::vfs::StagedAssetPipeline<GatheredSkeleton, PreparedSkeleton>;

struct SpineView {
  u32 camera = 0;
  f32 depth_min = -1024.f;
  f32 depth_max = 1024.f;
  const r2d::MaterialSystem *materials = nullptr;
};

class SpineSystem {
public:
  SpineSystem() = default;
  explicit SpineSystem(nx::thread_pool *const threads) noexcept
      : m_threads(threads) {}
  ~SpineSystem();

  SpineSystem(const SpineSystem &) = delete;
  SpineSystem &operator=(const SpineSystem &) = delete;

  static void register_components(scene::registry_t &registry);

  SpineInstance &attach(scene::registry_t &registry, scene::Entity e,
                        const SkeletonAsset &asset);
  /// Points @p e at @p path's skeleton without waiting for it. One not yet
  /// loaded is read on I/O and built on a worker, and @p e gets its
  /// SpineInstance at the first update after it arrives; a path that fails
  /// is logged and not asked for again until a hot reload.
  SpineComponent &request(scene::registry_t &registry, scene::Entity e,
                          nx::string_view path);
  /// Loads and attaches a cache-owned asset generation. Deprecated: it waits
  /// for the load request() makes; use request().
  SpineInstance *attach(scene::registry_t &registry, scene::Entity e,
                        nx::string_view path, nx::string *error = nullptr);

  /// Loads read through @p io and build on @p workers from here on; until
  /// then, and after shutdown_loads, they read and build in place.
  void bind_loads(nx::vfs::AsyncIoService &io, nx::thread_pool &workers);
  void shutdown_loads() noexcept;
  /// Skeletons being read or built, not yet published.
  [[nodiscard]] usize loads_in_flight() const noexcept {
    return m_loading.size();
  }
  /// Whether @p path failed to load and is not asked for again.
  [[nodiscard]] bool failed(nx::string_view path) const {
    return m_failed.find(path) != m_failed.end();
  }

  void set_texture_resolver(TextureResolver resolver) {
    m_resolve = std::move(resolver);
  }
  /// Deprecated: it waits for the load request() makes; use request().
  [[nodiscard]] SkeletonAsset load(nx::string_view path,
                                   nx::string *error = nullptr);
  /// Reloads the cached skeletons whose sources changed and points the
  /// components of @p registries that use them at the new ones.
  [[nodiscard]] usize
  reload_changed(std::span<scene::registry_t *const> registries);
  [[nodiscard]] usize reload_changed(scene::registry_t &registry);
  /// Resolves the cached skeletons' atlas pages again, for textures that have
  /// arrived since they loaded. Returns how many skeletons changed.
  usize refresh_textures();
  void clear_assets();
  /// Drops the cached skeletons no component of @p registries uses, so they
  /// and the textures only they need can go; attaching one again loads it
  /// afresh. Returns how many were dropped.
  usize release_unused(std::span<scene::registry_t *const> registries);
  usize release_unused(scene::registry_t &registry);
  /// Appends the atlas page textures of every attached skeleton, cached or
  /// not, and returns how many components there are.
  usize textures_in_use(scene::registry_t &registry,
                        nx::vector<nx::string_view> &out) const;

  usize update(scene::registry_t &registry, const scene::AssetRegistry &assets,
               f32 dt);
  usize update(scene::registry_t &registry, f32 dt);
  /// Poses @p registry's skeletons as update() does, for a scene other than
  /// the game's: their events are dropped, and events() stays update()'s.
  usize pose(scene::registry_t &registry, const scene::AssetRegistry &assets,
             f32 dt);

  usize emit(scene::registry_t &registry, r2d::MeshChannel &out,
             const SpineView &view);

  using PickFn = nx::function_ref<void(scene::Entity, i32 layer)>;
  /// Each visible skeleton of @p registry whose drawn triangles hold
  /// @p world_point, with its component's layer, for the editor's picking.
  void pick(const scene::registry_t &registry, float2 world_point,
            PickFn found);
  /// Each visible skeleton whose drawn vertices' bounds overlap the world
  /// rectangle from @p lo to @p hi.
  void pick_within(const scene::registry_t &registry, float2 lo, float2 hi,
                   PickFn found);

  [[nodiscard]] std::span<const SpineEvent> events() const noexcept {
    return {m_events.data(), m_events.size()};
  }

  void set_threads(nx::thread_pool *const threads) noexcept {
    m_threads = threads;
  }

  static constexpr usize PARALLEL_THRESHOLD = 4;

private:
  struct CachedAsset {
    SkeletonAsset asset;
    u64 stamp = 0;
  };

  struct EmitItem {
    const SpineComponent *component = nullptr;
    SpineInstance *instance = nullptr;
    const scene::WorldTransform2D *node = nullptr;
  };

  /// One per concurrent emit: spine's renderer reuses its buffers, so its
  /// commands last only until the same renderer renders again.
  struct EmitChunk {
    ::spine::SkeletonRenderer *renderer = nullptr;
    r2d::MeshChannel meshes;
    nx::vector<r2d::MeshVertex> vertices;
    nx::vector<u32> indices;
  };

  struct LoadingSkeleton {
    nx::string path;
    nx::vfs::AssetLoadHandle handle;
  };

  /// Starts loading @p path unless it is cached, loading or failed.
  void want(nx::string_view path);
  /// The cached asset of @p path, loaded in place first if need be.
  [[nodiscard]] SkeletonAsset load_cached(nx::string_view path,
                                          nx::string *error);
  /// Publishes finished loads into the cache, binding their textures.
  void take_loads();
  void cache(nx::string_view path, const SkeletonAsset &asset);

  void emit_range(EmitChunk &chunk, usize begin, usize end,
                  const SpineView &view) const;
  /// Attaches, rebinds and poses, leaving the posed instances in m_posed.
  usize pose_all(scene::registry_t &registry,
                 const scene::AssetRegistry &assets, f32 dt);

  /// Renders skeletons for picking, apart from the emit chunks' renderers.
  ::spine::SkeletonRenderer *m_pick_renderer = nullptr;
  TextureResolver m_resolve;
  nx::string_map<CachedAsset> m_assets;
  nx::thread_pool *m_threads = nullptr;
  SkeletonLoadPipeline m_loads;
  bool m_async = false;
  u64 m_load_generation = 0;
  nx::vector<LoadingSkeleton> m_loading;
  /// Why each path that failed did, until a hot reload clears it.
  nx::string_map<nx::string> m_failed;

  nx::vector<scene::Entity> m_pending;
  nx::vector<SpineInstance *> m_posed;
  nx::vector<const SpineComponent *> m_posed_data;
  nx::vector<f32> m_posed_steps;
  nx::vector<SpineEvent> m_events;
  nx::vector<EmitItem> m_emit_items;
  nx::vector<EmitChunk> m_emit_chunks;
};

} // namespace nxe::spine2d
