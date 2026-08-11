#include "spine/spine_system.h"

#include "core/foundation/diagnostics/profiler.h"
#include "core/rendering/render2d/scene_renderer.h"
#include "spine/spine_assets.h"

#include <spine/AnimationState.h>
#include <spine/Physics.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonRenderer.h>

namespace nxe::spine2d {
namespace {

[[nodiscard]] u32 tint(const u32 argb, const glm::vec4 &color,
                       const bool premultiplied) noexcept {
  constexpr f32 INV = 1.f / 255.f;
  glm::vec4 rgba(nx::cast<f32>((argb >> 16) & 0xFFu) * INV * color.x,
                 nx::cast<f32>((argb >> 8) & 0xFFu) * INV * color.y,
                 nx::cast<f32>(argb & 0xFFu) * INV * color.z,
                 nx::cast<f32>((argb >> 24) & 0xFFu) * INV * color.w);
  if (premultiplied) {
    rgba.x *= rgba.w;
    rgba.y *= rgba.w;
    rgba.z *= rgba.w;
  }
  return pack_color(rgba);
}

[[nodiscard]] r2d::MeshBlend blend_of(const ::spine::BlendMode mode,
                                      const bool premultiplied) noexcept {
  switch (mode) {
  case ::spine::BlendMode_Additive:
    return premultiplied ? r2d::MeshBlend::AdditivePremultiplied
                         : r2d::MeshBlend::Additive;
  case ::spine::BlendMode_Multiply:
    return r2d::MeshBlend::Multiply;
  case ::spine::BlendMode_Screen:
    return r2d::MeshBlend::Screen;
  case ::spine::BlendMode_Normal:
    break;
  }
  return premultiplied ? r2d::MeshBlend::NormalPremultiplied
                       : r2d::MeshBlend::Normal;
}

} // namespace

SpineSystem::~SpineSystem() { delete m_renderer; }

void SpineSystem::register_components(scene::registry_t &registry) {
  registry.register_component<SpineComponent>({.name = "SpineComponent"});
  registry.register_component<SpineInstance>({.name = "SpineInstance"});
}

SpineInstance &SpineSystem::attach(scene::registry_t &registry,
                                   const scene::Entity e,
                                   const SkeletonAsset &asset) {
  SpineComponent *component = registry.try_get<SpineComponent>(e);
  if (component == nullptr)
    component = &registry.emplace<SpineComponent>(e);
  component->asset = asset;
  return registry.emplace_or_replace<SpineInstance>(e, asset, e);
}

usize SpineSystem::update(scene::registry_t &registry, const f32 dt) {
  NX_PROFILE_ZONE("spine::update");

  m_pending.clear();
  registry.view<const SpineComponent>().each(
      [&](const scene::Entity e, const SpineComponent &component) {
        const SpineInstance *const instance =
            registry.try_get<SpineInstance>(e);
        if ((component.asset.valid() &&
             (instance == nullptr || !instance->valid() ||
              !instance->uses(component.asset))) ||
            (!component.asset.valid() && instance != nullptr))
          m_pending.push_back(e);
      });
  for (const scene::Entity e : m_pending) {
    const SkeletonAsset &asset = registry.get<SpineComponent>(e).asset;
    if (asset.valid())
      registry.emplace_or_replace<SpineInstance>(e, asset, e);
    else
      (void)registry.remove<SpineInstance>(e);
  }

  m_posed.clear();
  m_posed_data.clear();
  registry.view<const SpineComponent, SpineInstance>().each(
      [&](const scene::Entity, const SpineComponent &component,
          SpineInstance &instance) {
        if (!instance.valid())
          return;
        m_posed.push_back(&instance);
        m_posed_data.push_back(&component);
      });

  const auto pose = [&](const usize i) {
    SpineInstance &instance = *m_posed[i];
    const f32 step = dt * m_posed_data[i]->time_scale;
    instance.animation()->update(step);
    instance.animation()->apply(*instance.skeleton());
    instance.skeleton()->update(step);
    instance.skeleton()->updateWorldTransform(::spine::Physics_Update);
  };

  if (m_threads != nullptr && m_posed.size() >= PARALLEL_THRESHOLD)
    m_threads->parallel_for(0, m_posed.size(), 0, pose);
  else
    for (usize i = 0; i < m_posed.size(); ++i)
      pose(i);

  m_events.clear();
  for (SpineInstance *const instance : m_posed)
    instance->take_events(m_events);

  return m_posed.size();
}

usize SpineSystem::emit(scene::registry_t &registry, r2d::MeshChannel &out,
                        const SpineView &view) {
  NX_PROFILE_ZONE("spine::emit");
  usize appended = 0;

  registry
      .view<const SpineComponent, SpineInstance,
            const scene::WorldTransform2D>()
      .each([&](const scene::Entity, const SpineComponent &component,
                SpineInstance &instance, const scene::WorldTransform2D &node) {
        if (!component.visible || !instance.valid())
          return;

        if (m_renderer == nullptr)
          m_renderer = new ::spine::SkeletonRenderer();

        // Rendering metadata comes from the same retained asset version that
        // constructed the pose, never from a potentially replaced component.
        const bool premultiplied = instance.premultiplied();
        const u32 layer = nx::cast<u32>(component.layer + 2048) & 0xFFFu;
        const u32 key = nx_make_sort_key(layer,
                                         r2d::quantize_depth(node.world[2][1],
                                                             view.depth_min,
                                                             view.depth_max),
                                         0u);

        for (const ::spine::RenderCommand *command =
                 m_renderer->render(*instance.skeleton());
             command != nullptr; command = command->next) {
          if (command->numVertices == 0 || command->numIndices == 0)
            continue;

          const usize vertices = nx::cast<usize>(command->numVertices);
          m_vertices.clear();
          m_vertices.reserve(vertices);
          for (usize i = 0; i < vertices; ++i) {
            const f32 x = command->positions[i * 2];
            const f32 y = command->positions[i * 2 + 1];
            r2d::MeshVertex vertex;
            vertex.position = glm::vec2(
                node.world[0][0] * x + node.world[1][0] * y + node.world[2][0],
                node.world[0][1] * x + node.world[1][1] * y + node.world[2][1]);
            vertex.uv = glm::vec2(command->uvs[i * 2], command->uvs[i * 2 + 1]);
            vertex.color =
                tint(command->colors[i], component.color, premultiplied);
            m_vertices.push_back(vertex);
          }

          const usize indices = nx::cast<usize>(command->numIndices);
          m_indices.clear();
          m_indices.reserve(indices);
          for (usize i = 0; i < indices; ++i)
            m_indices.push_back(nx::cast<u32>(command->indices[i]));

          r2d::MeshDraw &draw = out.append(m_vertices, m_indices);
          draw.texture = nx::cast<u32>(
              reinterpret_cast<uintptr_t>(command->texture) & 0xFFFFFFFFu);
          draw.sort_key = key;
          draw.camera = view.camera;
          draw.blend = blend_of(command->blendMode, premultiplied);
          ++appended;
        }
      });

  return appended;
}

} // namespace nxe::spine2d
