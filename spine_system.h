#pragma once

#include "core/foundation/threading/thread_pool.h"
#include "core/foundation/containers/string_map.h"
#include "spine/spine_component.h"

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
  /// Loads and attaches a cache-owned asset generation.
  SpineInstance *attach(scene::registry_t &registry, scene::Entity e,
                        nx::string_view path, nx::string *error = nullptr);

  void set_texture_resolver(TextureResolver resolver) {
    m_resolve = std::move(resolver);
  }
  [[nodiscard]] SkeletonAsset load(nx::string_view path,
                                   nx::string *error = nullptr);
  [[nodiscard]] usize reload_changed(scene::registry_t &registry);
  void clear_assets();

  usize update(scene::registry_t &registry, const scene::AssetRegistry &assets,
               f32 dt);
  usize update(scene::registry_t &registry, f32 dt);

  usize emit(scene::registry_t &registry, r2d::MeshChannel &out,
             const SpineView &view);

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

  TextureResolver m_resolve;
  nx::string_map<CachedAsset> m_assets;
  nx::thread_pool *m_threads = nullptr;
  ::spine::SkeletonRenderer *m_renderer = nullptr;

  nx::vector<scene::Entity> m_pending;
  nx::vector<SpineInstance *> m_posed;
  nx::vector<const SpineComponent *> m_posed_data;
  nx::vector<f32> m_posed_steps;
  nx::vector<SpineEvent> m_events;
  nx::vector<r2d::MeshVertex> m_vertices;
  nx::vector<u32> m_indices;
};

}
