#include "spine/spine_system.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/diagnostics/profiler.h"
#include "core/foundation/vfs/vfs.h"
#include "rendering/render2d/material_system.h"
#include "rendering/render2d/scene_renderer.h"
#include "scene/animation/animation_graph.h"
#include "scene/asset/assets.h"
#include "spine/spine_assets.h"

#include <spine/AnimationState.h>
#include <spine/Physics.h>
#include <spine/Skeleton.h>
#include <spine/SkeletonRenderer.h>

namespace nxe::spine2d {
namespace {

[[nodiscard]] u64
source_stamp(const std::span<const nx::string> dependencies) noexcept {
  return nx::vfs::files_generation(dependencies);
}

/// An additive slot in premultiplied content is drawn as a normal one with
/// alpha zero: the premultiplied over operator then adds its colour and leaves
/// what is behind it, so the skeleton stays on one pipeline.
[[nodiscard]] u32 tint(const u32 argb, const glm::vec4 &color,
                       const bool premultiplied, const bool additive) noexcept {
  constexpr f32 INV = 1.f / 255.f;
  glm::vec4 rgba(nx::cast<f32>((argb >> 16) & 0xFFu) * INV * color.x,
                 nx::cast<f32>((argb >> 8) & 0xFFu) * INV * color.y,
                 nx::cast<f32>(argb & 0xFFu) * INV * color.z,
                 nx::cast<f32>((argb >> 24) & 0xFFu) * INV * color.w);
  if (premultiplied) {
    rgba.x *= rgba.w;
    rgba.y *= rgba.w;
    rgba.z *= rgba.w;
    if (additive)
      rgba.w = 0.f;
  }
  return nx_pack_rgba8(rgba);
}

[[nodiscard]] r2d::MeshBlend blend_of(const ::spine::BlendMode mode,
                                      const bool premultiplied) noexcept {
  switch (mode) {
  case ::spine::BlendMode_Additive:
    return premultiplied ? r2d::MeshBlend::NormalPremultiplied
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

SpineSystem::~SpineSystem() {
  for (const EmitChunk &chunk : m_emit_chunks)
    delete chunk.renderer;
}

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
  component->source.clear();
  return registry.emplace_or_replace<SpineInstance>(e, asset, e);
}

SkeletonAsset SpineSystem::load(const nx::string_view path, nx::string *error) {
  const auto cached = m_assets.find(path);
  if (cached != m_assets.end())
    return cached->second.asset;

  SkeletonAsset asset;
  nx::string why;
  if (!load_skeleton(path, m_resolve, asset, why)) {
    if (error != nullptr)
      *error = std::move(why);
    return {};
  }
  CachedAsset entry{asset, source_stamp(asset.dependencies())};
  m_assets.emplace(nx::string(path), std::move(entry));
  if (error != nullptr)
    error->clear();
  return asset;
}

SpineInstance *SpineSystem::attach(scene::registry_t &registry,
                                   const scene::Entity e,
                                   const nx::string_view path,
                                   nx::string *error) {
  const SkeletonAsset asset = load(path, error);
  if (!asset.valid())
    return nullptr;
  SpineComponent *component = registry.try_get<SpineComponent>(e);
  if (component == nullptr)
    component = &registry.emplace<SpineComponent>(e);
  component->asset = asset;
  component->source = nx::string(path);
  return &registry.emplace_or_replace<SpineInstance>(e, asset, e);
}

usize SpineSystem::reload_changed(scene::registry_t &registry) {
  usize count = 0;
  for (auto &[path, cached] : m_assets) {
    const u64 changed = source_stamp(cached.asset.dependencies());
    if (changed == cached.stamp)
      continue;
    cached.stamp = changed;

    SkeletonAsset fresh;
    nx::string error;
    if (!load_skeleton(path.view(), m_resolve, fresh, error)) {
      nx::logw("spine: '{}' changed but its last valid generation remains: {}",
               path, error);
      continue;
    }
    cached.asset = fresh;
    cached.stamp = source_stamp(fresh.dependencies());
    registry.view<SpineComponent>().each(
        [&](const scene::Entity, SpineComponent &component) {
          if (component.source == path)
            component.asset = fresh;
        });
    ++count;
    nx::logd("spine: reloaded '{}'", path);
  }
  return count;
}

usize SpineSystem::refresh_textures() {
  usize changed = 0;
  for (auto &[_, cached] : m_assets)
    changed +=
        spine2d::refresh_textures(cached.asset, m_resolve) != 0 ? 1u : 0u;
  return changed;
}

void SpineSystem::clear_assets() {
  m_assets.clear();
  m_resolve = {};
}

usize SpineSystem::update(scene::registry_t &registry,
                          const scene::AssetRegistry &assets, const f32 dt) {
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
    if (asset.valid()) {
      if (SpineInstance *const instance = registry.try_get<SpineInstance>(e);
          instance != nullptr && instance->valid())
        (void)instance->rebind(asset);
      else
        registry.emplace_or_replace<SpineInstance>(e, asset, e);
    } else
      (void)registry.remove<SpineInstance>(e);
  }

  m_posed.clear();
  m_posed_data.clear();
  m_posed_steps.clear();
  registry.view<const SpineComponent, SpineInstance>().each(
      [&](const scene::Entity e, const SpineComponent &component,
          SpineInstance &instance) {
        if (!instance.valid())
          return;
        f32 step = dt * component.time_scale;
        if (auto *controller =
                registry.try_get<scene::AnimationGraphComponent>(e);
            controller != nullptr && !controller->clip_set.valid()) {
          const scene::AnimationGraph *const graph =
              assets.graph(controller->graph);
          if (graph != nullptr) {
            const auto duration = [&](const u32 slot, const u16, f32 &seconds) {
              return instance.animation_duration(graph->clip_slot_name(slot),
                                                 seconds);
            };
            scene::GraphTick tick;
            if (scene::update_animation_state_machine(*controller, *graph,
                                                      duration, step, tick)) {
              const scene::AnimationState *const state =
                  graph->state(controller->state);
              if (state != nullptr) {
                const f32 speed = state->speed * controller->speed;
                if (tick.entered != scene::INVALID_STATE)
                  (void)instance.play(
                      graph->clip_slot_name(state->clip_slot),
                      state->mode == scene::PlayMode::Loop, 0,
                      controller->blending() ? controller->blend_duration : 0.f,
                      speed, controller->time);
                else
                  (void)instance.set_speed(speed);
              }
              if (!controller->playing)
                step = 0.f;
            }
          }
        }
        m_posed.push_back(&instance);
        m_posed_data.push_back(&component);
        m_posed_steps.push_back(step);
      });

  const auto pose = [&](const usize i) {
    SpineInstance &instance = *m_posed[i];
    const f32 step = m_posed_steps[i];
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

usize SpineSystem::update(scene::registry_t &registry, const f32 dt) {
  static const scene::AssetRegistry no_graph_assets;
  return update(registry, no_graph_assets, dt);
}

void SpineSystem::emit_range(EmitChunk &chunk, const usize begin,
                             const usize end, const SpineView &view) const {
  chunk.meshes.clear();
  if (chunk.renderer == nullptr)
    chunk.renderer = new ::spine::SkeletonRenderer();

  for (usize item = begin; item < end; ++item) {
    const SpineComponent &component = *m_emit_items[item].component;
    SpineInstance &instance = *m_emit_items[item].instance;
    const scene::WorldTransform2D &node = *m_emit_items[item].node;

    const bool premultiplied = instance.premultiplied();
    u32 batch = 0u;
    u32 material_offset = 0u;
    if (view.materials != nullptr && component.material != 0u) {
      batch = view.materials->batch_of(component.material);
      material_offset = view.materials->offset_of(component.material);
    }
    const u32 layer =
        nx::cast<u32>(nx::clamp(component.layer, -32768, 32767) + 32768);
    const u32 key = nx_make_sort_key(
        layer,
        r2d::quantize_depth(node.world[2][1], view.depth_min, view.depth_max),
        0u);

    for (const ::spine::RenderCommand *command =
             chunk.renderer->render(*instance.skeleton());
         command != nullptr; command = command->next) {
      if (command->numVertices == 0 || command->numIndices == 0)
        continue;

      const bool additive = command->blendMode == ::spine::BlendMode_Additive;
      const usize vertices = nx::cast<usize>(command->numVertices);
      chunk.vertices.clear();
      chunk.vertices.reserve(vertices);
      for (usize i = 0; i < vertices; ++i) {
        const f32 x = command->positions[i * 2];
        const f32 y = command->positions[i * 2 + 1];
        r2d::MeshVertex vertex;
        vertex.position = glm::vec2(
            node.world[0][0] * x + node.world[1][0] * y + node.world[2][0],
            node.world[0][1] * x + node.world[1][1] * y + node.world[2][1]);
        vertex.uv = glm::vec2(command->uvs[i * 2], command->uvs[i * 2 + 1]);
        vertex.color =
            tint(command->colors[i], component.color, premultiplied, additive);
        chunk.vertices.push_back(vertex);
      }

      const usize indices = nx::cast<usize>(command->numIndices);
      chunk.indices.clear();
      chunk.indices.reserve(indices);
      for (usize i = 0; i < indices; ++i)
        chunk.indices.push_back(nx::cast<u32>(command->indices[i]));

      // The atlas page holds its texture's word as spine's renderer object.
      const NxTexture2D<float4> texture =
          NxTexture2D<float4>::from_packed(nx::cast<u32>(
              reinterpret_cast<uintptr_t>(command->texture) & 0xFFFFFFFFu));
      const r2d::MeshBlend blend = blend_of(command->blendMode, premultiplied);

      // spine-cpp also splits where only the slot colour changes; colour is
      // per vertex here, so such a command joins the draw before it.
      r2d::MeshChannel &out = chunk.meshes;
      if (!out.draws.empty()) {
        r2d::MeshDraw &last = out.draws.back();
        if (last.texture == texture && last.blend == blend &&
            last.sort_key == key && last.camera == view.camera &&
            last.batch == batch && last.material == material_offset &&
            last.first_index + last.index_count == out.indices.size()) {
          const u32 base =
              nx::cast<u32>(out.vertices.size()) - last.vertex_offset;
          out.vertices.insert(out.vertices.end(), chunk.vertices.begin(),
                              chunk.vertices.end());
          for (const u32 index : chunk.indices)
            out.indices.push_back(index + base);
          last.index_count += nx::cast<u32>(chunk.indices.size());
          continue;
        }
      }

      r2d::MeshDraw &draw = out.append(chunk.vertices, chunk.indices);
      draw.texture = texture;
      draw.sort_key = key;
      draw.camera = view.camera;
      draw.blend = blend;
      draw.batch = batch;
      draw.material = material_offset;
    }
  }
}

usize SpineSystem::emit(scene::registry_t &registry, r2d::MeshChannel &out,
                        const SpineView &view) {
  NX_PROFILE_ZONE("spine::emit");

  m_emit_items.clear();
  registry
      .view<const SpineComponent, SpineInstance,
            const scene::WorldTransform2D>()
      .each([&](const scene::Entity, const SpineComponent &component,
                SpineInstance &instance, const scene::WorldTransform2D &node) {
        if (component.visible && instance.valid())
          m_emit_items.push_back({&component, &instance, &node});
      });
  const usize count = m_emit_items.size();
  if (count == 0)
    return 0;

  // Skeletons render into a buffer per chunk, then join the channel in their
  // original order, so the draws come out as they did from one thread.
  const bool parallel = m_threads != nullptr && m_threads->worker_count() > 0 &&
                        count >= PARALLEL_THRESHOLD;
  const usize chunks =
      parallel ? nx::min<usize>(count, m_threads->worker_count() + 1u) : 1u;
  if (m_emit_chunks.size() < chunks)
    m_emit_chunks.resize(chunks);
  const usize per_chunk = (count + chunks - 1) / chunks;
  const auto run = [&](const usize c) {
    const usize begin = nx::min(c * per_chunk, count);
    emit_range(m_emit_chunks[c], begin, nx::min(begin + per_chunk, count),
               view);
  };
  if (parallel)
    m_threads->parallel_for(0, chunks, 1, run);
  else
    run(0);

  usize appended = 0;
  for (usize c = 0; c < chunks; ++c) {
    const r2d::MeshChannel &local = m_emit_chunks[c].meshes;
    const u32 base_vertex = nx::cast<u32>(out.vertices.size());
    const u32 base_index = nx::cast<u32>(out.indices.size());
    out.vertices.insert(out.vertices.end(), local.vertices.begin(),
                        local.vertices.end());
    out.indices.insert(out.indices.end(), local.indices.begin(),
                       local.indices.end());
    for (r2d::MeshDraw draw : local.draws) {
      draw.first_index += base_index;
      draw.vertex_offset += base_vertex;
      out.draws.push_back(draw);
    }
    appended += local.draws.size();
  }
  return appended;
}

} // namespace nxe::spine2d
