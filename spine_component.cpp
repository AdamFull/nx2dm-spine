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

/// spine::String needs a terminated buffer and a string_view has none.
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

} // namespace

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

} // namespace detail

SpineInstance::SpineInstance(const SkeletonAsset &asset,
                             const scene::Entity owner)
    : m_entity(owner) {
  if (!asset.valid())
    return;
  m_skeleton = new ::spine::Skeleton(*asset.data());
  m_animation = new ::spine::AnimationState(*asset.mixes());
  m_sink = new detail::SpineEventSink(owner);
  m_animation->setListener(m_sink);
}

SpineInstance::~SpineInstance() { reset(); }

SpineInstance::SpineInstance(SpineInstance &&other) noexcept
    : m_skeleton(other.m_skeleton), m_animation(other.m_animation),
      m_sink(other.m_sink), m_entity(other.m_entity) {
  other.m_skeleton = nullptr;
  other.m_animation = nullptr;
  other.m_sink = nullptr;
}

SpineInstance &SpineInstance::operator=(SpineInstance &&other) noexcept {
  if (this != &other) {
    reset();
    m_skeleton = other.m_skeleton;
    m_animation = other.m_animation;
    m_sink = other.m_sink;
    m_entity = other.m_entity;
    other.m_skeleton = nullptr;
    other.m_animation = nullptr;
    other.m_sink = nullptr;
  }
  return *this;
}

void SpineInstance::reset() noexcept {
  delete m_animation;
  delete m_skeleton;
  delete m_sink;
  m_animation = nullptr;
  m_skeleton = nullptr;
  m_sink = nullptr;
}

bool SpineInstance::play(const nx::string_view name, const bool loop,
                         const usize track) {
  if (m_animation == nullptr)
    return false;
  ::spine::Animation *const found =
      m_animation->getData().getSkeletonData().findAnimation(owned(name));
  if (found == nullptr)
    return false;
  m_animation->setAnimation(track, *found, loop);
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

} // namespace nxe::spine2d
