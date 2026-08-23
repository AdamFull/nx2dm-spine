#include "spine/spine_scripting.h"

#include "spine/spine_component.h"

#include "core/app/engine.h"
#include "core/script/script_host.h"

#include <spine/AnimationState.h>

namespace nxe::spine2d {
namespace {

[[nodiscard]] SpineInstance *instance_of(ModuleContext &ctx,
                                         const sys::Entity e) {
  if (e == sys::Entity{})
    return nullptr;
  SpineInstance *const instance =
      ctx.scene().registry().try_get<SpineInstance>(e);
  return instance != nullptr && instance->valid() ? instance : nullptr;
}

[[nodiscard]] SpineComponent *component_of(ModuleContext &ctx,
                                           const sys::Entity e) {
  if (e == sys::Entity{})
    return nullptr;
  return ctx.scene().registry().try_get<SpineComponent>(e);
}

[[nodiscard]] usize track_of(const f32 track) noexcept {
  return track > 0.f ? nx::cast<usize>(track) : 0u;
}

}

void expose_spine_services(script::Host &host, ModuleContext &ctx) {
  host.expose_as("spine_play", [&ctx](const sys::Entity e,
                                      const nx::string_view animation,
                                      const bool loop, const f32 track) {
    SpineInstance *const instance = instance_of(ctx, e);
    return instance != nullptr &&
           instance->play(animation, loop, track_of(track));
  });

  host.expose_as("spine_queue",
                 [&ctx](const sys::Entity e, const nx::string_view animation,
                        const bool loop, const f32 delay, const f32 track) {
                   SpineInstance *const instance = instance_of(ctx, e);
                   return instance != nullptr &&
                          instance->queue(animation, loop, delay,
                                          track_of(track));
                 });

  host.expose_as("spine_stop", [&ctx](const sys::Entity e, const f32 track) {
    SpineInstance *const instance = instance_of(ctx, e);
    if (instance == nullptr)
      return false;
    instance->stop(track_of(track));
    return true;
  });

  host.expose_as(
      "spine_skin", [&ctx](const sys::Entity e, const nx::string_view name) {
        SpineInstance *const instance = instance_of(ctx, e);
        return instance != nullptr && instance->set_skin(name);
      });

  // An empty track is finished, and so is a skeleton that is not there: a
  // script waiting on this would otherwise wait for ever rather than move on.
  host.expose_as(
      "spine_finished", [&ctx](const sys::Entity e, const f32 track) {
        const SpineInstance *const instance = instance_of(ctx, e);
        if (instance == nullptr || instance->animation() == nullptr)
          return true;
        ::spine::Array<::spine::TrackEntry *> &tracks =
            instance->animation()->getTracks();
        const usize index = track_of(track);
        if (index >= tracks.size())
          return true;
        const ::spine::TrackEntry *const entry = tracks[index];
        return entry == nullptr ||
               const_cast<::spine::TrackEntry *>(entry)->isComplete();
      });

  host.expose_as("spine_visible",
                 [&ctx](const sys::Entity e, const bool on) {
                   SpineComponent *const component = component_of(ctx, e);
                   if (component == nullptr)
                     return false;
                   component->visible = on;
                   return true;
                 });

  host.expose_as("spine_speed", [&ctx](const sys::Entity e, const f32 rate) {
    SpineComponent *const component = component_of(ctx, e);
    if (component == nullptr)
      return false;
    component->time_scale = rate;
    return true;
  });
}

}
