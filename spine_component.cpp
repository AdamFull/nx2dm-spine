#include "spine/spine_component.h"

#include "spine/spine_assets.h"

#include <spine/Animation.h>
#include <spine/AnimationState.h>
#include <spine/AnimationStateData.h>
#include <spine/Event.h>
#include <spine/EventData.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonData.h>
#include <spine/Skin.h>
#include <spine/SpineString.h>

namespace nxe::spine2d {
namespace {

[[nodiscard]] ::spine::String owned(const nx::string_view text) {
  const nx::string copy(text);
  return ::spine::String(copy.c_str());
}

[[nodiscard]] nx::string_view view_of(const ::spine::String &text) noexcept {
  return {text.buffer(), nx::cast<usize>(text.length())};
}

[[nodiscard]] bool kind_of(const ::spine::EventType type,
                           SpineEventKind &out) noexcept {
  switch (type) {
  case ::spine::EventType_Start:
    out = SpineEventKind::Started;
    return true;
  case ::spine::EventType_Interrupt:
    out = SpineEventKind::Interrupted;
    return true;
  case ::spine::EventType_End:
    out = SpineEventKind::Ended;
    return true;
  case ::spine::EventType_Complete:
    out = SpineEventKind::Completed;
    return true;
  case ::spine::EventType_Event:
    out = SpineEventKind::Custom;
    return true;
  case ::spine::EventType_Dispose:
    break;
  }
  return false;
}

}

namespace detail {

class SpineEventSink final : public ::spine::AnimationStateListenerObject {
public:
  explicit SpineEventSink(const scene::Entity owner) noexcept
      : m_entity(owner) {}

  void callback(::spine::AnimationState *, const ::spine::EventType type,
                ::spine::TrackEntry *const entry,
                ::spine::Event *const event) override {
    SpineEvent record;
    record.entity = m_entity;
    if (!kind_of(type, record.kind))
      return;
    if (entry != nullptr) {
      record.track = nx::cast<u32>(entry->getTrackIndex());
      record.animation = nx::string(view_of(entry->getAnimation().getName()));
      record.time = entry->getTrackTime();
    }
    if (event != nullptr) {
      record.name = nx::string(view_of(event->getData().getName()));
      record.string_value = nx::string(view_of(event->getString()));
      record.int_value = event->getInt();
      record.float_value = event->getFloat();
      record.time = event->getTime();
    }
    events.push_back(std::move(record));
  }

  nx::vector<SpineEvent> events;

private:
  scene::Entity m_entity;
};

void SpineInstanceObjectDeleter::operator()(
    ::spine::Skeleton *const skeleton) const noexcept {
  delete skeleton;
}

void SpineInstanceObjectDeleter::operator()(
    ::spine::AnimationState *const animation) const noexcept {
  delete animation;
}

void SpineEventSinkDeleter::operator()(
    SpineEventSink *const sink) const noexcept {
  nx::release(sink);
}

}

SpineInstance::SpineInstance(const SkeletonAsset &asset,
                             const scene::Entity owner)
    : m_asset(asset), m_entity(owner) {
  if (!asset.valid())
    return;
  m_skeleton.reset(new ::spine::Skeleton(*asset.data()));
  m_animation.reset(new ::spine::AnimationState(*asset.mixes()));
  m_sink.reset(nx::allocate<detail::SpineEventSink>(owner));
  if (m_sink == nullptr) {
    reset();
    return;
  }
  m_animation->setListener(m_sink.get());
}

SpineInstance::~SpineInstance() { reset(); }

SpineInstance::SpineInstance(SpineInstance &&other) noexcept
    : m_asset(std::move(other.m_asset)),
      m_skeleton(std::move(other.m_skeleton)),
      m_animation(std::move(other.m_animation)),
      m_sink(std::move(other.m_sink)), m_entity(other.m_entity) {}

SpineInstance &SpineInstance::operator=(SpineInstance &&other) noexcept {
  if (this != &other) {
    reset();
    m_asset = std::move(other.m_asset);
    m_skeleton = std::move(other.m_skeleton);
    m_animation = std::move(other.m_animation);
    m_sink = std::move(other.m_sink);
    m_entity = other.m_entity;
  }
  return *this;
}

void SpineInstance::reset() noexcept {
  m_animation.reset();
  m_skeleton.reset();
  m_sink.reset();
}

bool SpineInstance::play(const nx::string_view name, const bool loop,
                         const usize track, const f32 mix_duration,
                         const f32 speed, const f32 start_time) {
  if (m_animation == nullptr)
    return false;
  ::spine::Animation *const found =
      m_animation->getData().getSkeletonData().findAnimation(owned(name));
  if (found == nullptr)
    return false;
  ::spine::TrackEntry &entry = m_animation->setAnimation(track, *found, loop);
  if (mix_duration >= 0.f)
    entry.setMixDuration(mix_duration);
  entry.setTimeScale(nx::max(speed, 0.f));
  if (start_time > 0.f)
    entry.setTrackTime(start_time);
  return true;
}

bool SpineInstance::rebind(const SkeletonAsset &asset) {
  if (!asset.valid())
    return false;

  struct Track {
    nx::string name;
    usize index = 0;
    f32 time = 0.f;
    f32 speed = 1.f;
    bool loop = true;
  };
  nx::vector<Track> tracks;
  if (m_animation != nullptr) {
    auto &active = m_animation->getTracks();
    tracks.reserve(nx::cast<usize>(active.size()));
    for (usize i = 0; i < nx::cast<usize>(active.size()); ++i) {
      ::spine::TrackEntry *const entry = active[nx::cast<int>(i)];
      if (entry == nullptr)
        continue;
      tracks.push_back(
          {.name = nx::string(view_of(entry->getAnimation().getName())),
           .index = nx::cast<usize>(entry->getTrackIndex()),
           .time = entry->getTrackTime(),
           .speed = entry->getTimeScale(),
           .loop = entry->getLoop()});
    }
  }

  nx::string skin;
  if (m_skeleton != nullptr && m_skeleton->getSkin() != nullptr)
    skin = nx::string(view_of(m_skeleton->getSkin()->getName()));

  SpineInstance fresh(asset, m_entity);
  if (!fresh.valid())
    return false;
  if (!skin.empty())
    (void)fresh.set_skin(skin.view());
  for (const Track &track : tracks)
    (void)fresh.play(track.name.view(), track.loop, track.index, 0.f,
                     track.speed, track.time);
  *this = std::move(fresh);
  return true;
}

bool SpineInstance::animation_duration(const nx::string_view name,
                                       f32 &duration) const {
  if (m_animation == nullptr)
    return false;
  ::spine::Animation *const found =
      m_animation->getData().getSkeletonData().findAnimation(owned(name));
  if (found == nullptr)
    return false;
  duration = found->getDuration();
  return true;
}

bool SpineInstance::set_speed(const f32 speed, const usize track) {
  if (m_animation == nullptr)
    return false;
  ::spine::TrackEntry *const entry = m_animation->getTrack(track);
  if (entry == nullptr)
    return false;
  entry->setTimeScale(nx::max(speed, 0.f));
  return true;
}

bool SpineInstance::queue(const nx::string_view name, const bool loop,
                          const f32 delay, const usize track) {
  if (m_animation == nullptr)
    return false;
  ::spine::Animation *const found =
      m_animation->getData().getSkeletonData().findAnimation(owned(name));
  if (found == nullptr)
    return false;
  m_animation->addAnimation(track, *found, loop, delay);
  return true;
}

void SpineInstance::stop(const usize track) {
  if (m_animation != nullptr)
    m_animation->clearTrack(track);
}

bool SpineInstance::set_skin(const nx::string_view name) {
  if (m_skeleton == nullptr)
    return false;
  ::spine::Skin *const skin = m_skeleton->getData().findSkin(owned(name));
  if (skin == nullptr)
    return false;
  m_skeleton->setSkin(skin);
  m_skeleton->setupPoseSlots();
  return true;
}

std::span<const SpineEvent> SpineInstance::events() const noexcept {
  if (m_sink == nullptr)
    return {};
  return {m_sink->events.data(), m_sink->events.size()};
}

void SpineInstance::take_events(nx::vector<SpineEvent> &out) {
  if (m_sink == nullptr)
    return;
  for (SpineEvent &event : m_sink->events)
    out.push_back(std::move(event));
  m_sink->events.clear();
}

void SpineInstance::clear_events() noexcept {
  if (m_sink != nullptr)
    m_sink->events.clear();
}

}
