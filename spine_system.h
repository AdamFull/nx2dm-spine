#pragma once

/**
 * @file spine_system.h
 * @brief Posing a scene's skeletons and drawing them (namespace nxe::spine2d).
 */

#include "core/foundation/threading/thread_pool.h"
#include "spine/spine_component.h"

namespace spine {
class SkeletonRenderer;
}

namespace nxe::spine2d {

struct SpineView {
  u32 camera = 0;
  f32 depth_min = -1024.f;
  f32 depth_max = 1024.f;
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
  nx::thread_pool *m_threads = nullptr;
  ::spine::SkeletonRenderer *m_renderer = nullptr;

  /// Reused between frames so a steady one allocates nothing.
  nx::vector<scene::Entity> m_pending;
  nx::vector<SpineInstance *> m_posed;
  nx::vector<const SpineComponent *> m_posed_data;
  nx::vector<SpineEvent> m_events;
  nx::vector<r2d::MeshVertex> m_vertices;
  nx::vector<u32> m_indices;
};

} // namespace nxe::spine2d
