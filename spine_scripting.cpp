#include "spine/spine_scripting.h"

#include "spine/spine_component.h"

#include "core/app/engine.h"
#include "core/script/script_host.h"

#include <spine/AnimationState.h>

namespace nxe::spine2d {
namespace {

[[nodiscard]] SpineInstance *instance_of(Engine &engine, const sys::Entity e) {
  if (e == sys::Entity{})
    return nullptr;
  SpineInstance *const instance =
      engine.scene().registry().try_get<SpineInstance>(e);
  return instance != nullptr && instance->valid() ? instance : nullptr;
}

[[nodiscard]] SpineComponent *component_of(Engine &engine,
                                           const sys::Entity e) {
  if (e == sys::Entity{})
    return nullptr;
  return engine.scene().registry().try_get<SpineComponent>(e);
}

/// A track index arrives as a number, because every argument does. Negatives
/// would index a vector from the wrong end inside spine-cpp.
[[nodiscard]] usize track_of(const f32 track) noexcept {
  return track > 0.f ? nx::cast<usize>(track) : 0u;
}

} // namespace

void expose_spine_services(script::Host &host, Engine &engine) {
  host.expose_as("spine_play", [&engine](const sys::Entity e,
                                         const nx::string_view animation,
                                         const bool loop, const f32 track) {
    SpineInstance *const instance = instance_of(engine, e);
    return instance != nullptr &&
           instance->play(animation, loop, track_of(track));
  });

  // Queued rather than played: what makes a two-part action one call from a
  // script instead of a timer it has to keep itself.
  host.expose_as("spine_queue",
                 [&engine](const sys::Entity e, const nx::string_view animation,
                           const bool loop, const f32 delay, const f32 track) {
                   SpineInstance *const instance = instance_of(engine, e);
                   return instance != nullptr &&
                          instance->queue(animation, loop, delay,
                                          track_of(track));
                 });

  host.expose_as("spine_stop", [&engine](const sys::Entity e, const f32 track) {
    SpineInstance *const instance = instance_of(engine, e);
    if (instance == nullptr)
      return false;
    instance->stop(track_of(track));
    return true;
  });

  host.expose_as(
      "spine_skin", [&engine](const sys::Entity e, const nx::string_view name) {
        SpineInstance *const instance = instance_of(engine, e);
        return instance != nullptr && instance->set_skin(name);
      });

  // An empty track is finished, and so is a skeleton that is not there: a
  // script waiting on this would otherwise wait for ever rather than move on.
  host.expose_as(
      "spine_finished", [&engine](const sys::Entity e, const f32 track) {
        const SpineInstance *const instance = instance_of(engine, e);
        if (instance == nullptr || instance->animation() == nullptr)
          return true;
        // getTracks() rather than a getCurrent(): this spine-cpp has no such
        // accessor, and a track never set is simply past the end of the array.
        ::spine::Array<::spine::TrackEntry *> &tracks =
            instance->animation()->getTracks();
        const usize index = track_of(track);
        if (index >= tracks.size())
          return true;
        const ::spine::TrackEntry *const entry = tracks[index];
        return entry == nullptr ||
               const_cast<::spine::TrackEntry *>(entry)->isComplete();
      });

  // On the component rather than the instance: these survive a save, and a
  // skeleton the scene has not attached yet should still remember what it was
  // told.
  host.expose_as("spine_visible",
                 [&engine](const sys::Entity e, const bool on) {
                   SpineComponent *const component = component_of(engine, e);
                   if (component == nullptr)
                     return false;
                   component->visible = on;
                   return true;
                 });

  host.expose_as("spine_speed", [&engine](const sys::Entity e, const f32 rate) {
    SpineComponent *const component = component_of(engine, e);
    if (component == nullptr)
      return false;
    component->time_scale = rate;
    return true;
  });
}

} // namespace nxe::spine2d
