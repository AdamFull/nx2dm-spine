#pragma once

/**
 * @file spine_component.h
 * @brief A skeleton placed in a scene, and the pose it holds (namespace
 * nxe::spine2d).
 */

#include "core/rendering/render2d/mesh_channel.h"
#include "core/scene/components.h"

#include <glm/glm.hpp>

#include <span>

namespace spine {
class Skeleton;
class AnimationState;
} // namespace spine

namespace nxe::spine2d {

class SkeletonAsset;

enum class SpineEventKind : u8 {
  Started,
  Interrupted,
  Ended,
  Completed,
  Custom
};

struct SpineEvent {
  scene::Entity entity;
  SpineEventKind kind = SpineEventKind::Custom;
  u32 track = 0;
  nx::string animation;
  nx::string name;
  nx::string string_value;
  i32 int_value = 0;
  f32 float_value = 0.f;
  f32 time = 0.f;
};

struct SpineComponent {
  const SkeletonAsset *asset = nullptr;
  glm::vec4 color{1.f, 1.f, 1.f, 1.f};
  f32 time_scale = 1.f;
  i32 layer = 0;
  bool visible = true;
};

namespace detail {
class SpineEventSink;
} // namespace detail

class SpineInstance {
public:
  SpineInstance() = default;
  explicit SpineInstance(const SkeletonAsset &asset, scene::Entity owner = {});
  ~SpineInstance();

  SpineInstance(const SpineInstance &) = delete;
  SpineInstance &operator=(const SpineInstance &) = delete;
  SpineInstance(SpineInstance &&other) noexcept;
  SpineInstance &operator=(SpineInstance &&other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return m_skeleton != nullptr; }

  [[nodiscard]] ::spine::Skeleton *skeleton() const noexcept {
    return m_skeleton;
  }
  [[nodiscard]] ::spine::AnimationState *animation() const noexcept {
    return m_animation;
  }

  [[nodiscard]] scene::Entity entity() const noexcept { return m_entity; }

  bool play(nx::string_view name, bool loop = true, usize track = 0);
  bool queue(nx::string_view name, bool loop = true, f32 delay = 0.f,
             usize track = 0);
  void stop(usize track = 0);

  bool set_skin(nx::string_view name);

  [[nodiscard]] std::span<const SpineEvent> events() const noexcept;
  void take_events(nx::vector<SpineEvent> &out);
  void clear_events() noexcept;

private:
  void reset() noexcept;

  ::spine::Skeleton *m_skeleton = nullptr;
  ::spine::AnimationState *m_animation = nullptr;
  detail::SpineEventSink *m_sink = nullptr;
  scene::Entity m_entity;
};

} // namespace nxe::spine2d
