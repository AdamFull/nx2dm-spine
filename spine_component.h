#pragma once

#include "core/rendering/render2d/mesh_channel.h"
#include "core/scene/components.h"
#include "spine/spine_assets.h"

#include <glm/glm.hpp>

#include <span>

namespace spine {
class Skeleton;
class AnimationState;
}

namespace nxe::spine2d {

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
  SkeletonAsset asset;
  /// Non-empty for assets owned by SpineSystem's path cache. Direct handles
  /// remain supported, but have no file generation to follow.
  nx::string source;
  glm::vec4 color{1.f, 1.f, 1.f, 1.f};
  f32 time_scale = 1.f;
  i32 layer = 0;
  bool visible = true;
  u32 material = 0;
};

namespace detail {
class SpineEventSink;
struct SpineInstanceObjectDeleter {
  void operator()(::spine::Skeleton *skeleton) const noexcept;
  void operator()(::spine::AnimationState *animation) const noexcept;
};
struct SpineEventSinkDeleter {
  void operator()(SpineEventSink *sink) const noexcept;
};
}

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
    return m_skeleton.get();
  }
  [[nodiscard]] ::spine::AnimationState *animation() const noexcept {
    return m_animation.get();
  }

  [[nodiscard]] scene::Entity entity() const noexcept { return m_entity; }

  [[nodiscard]] bool uses(const SkeletonAsset &asset) const noexcept {
    return m_asset.same_version(asset);
  }
  [[nodiscard]] bool premultiplied() const noexcept {
    return m_asset.premultiplied();
  }

  /// Recreates Spine's runtime objects on a fresh asset version while
  /// preserving active track names, playheads, loop flags, speeds and skin.
  [[nodiscard]] bool rebind(const SkeletonAsset &asset);

  bool play(nx::string_view name, bool loop = true, usize track = 0,
            f32 mix_duration = -1.f, f32 speed = 1.f,
            f32 start_time = 0.f);
  bool queue(nx::string_view name, bool loop = true, f32 delay = 0.f,
             usize track = 0);
  void stop(usize track = 0);
  bool set_speed(f32 speed, usize track = 0);
  [[nodiscard]] bool animation_duration(nx::string_view name,
                                        f32 &duration) const;

  bool set_skin(nx::string_view name);

  [[nodiscard]] std::span<const SpineEvent> events() const noexcept;
  void take_events(nx::vector<SpineEvent> &out);
  void clear_events() noexcept;

private:
  void reset() noexcept;

  SkeletonAsset m_asset;
  nx::unique_ptr<::spine::Skeleton, detail::SpineInstanceObjectDeleter>
      m_skeleton;
  nx::unique_ptr<::spine::AnimationState, detail::SpineInstanceObjectDeleter>
      m_animation;
  nx::unique_ptr<detail::SpineEventSink, detail::SpineEventSinkDeleter> m_sink;
  scene::Entity m_entity;
};

}
