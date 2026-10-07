
#include "spine/spine_module.h"

#include "spine/spine_scripting.h"

#include "app/assets/async_texture_set.h"
#include "app/engine.h"
#include "app/module_system/module.h"

#include "core/foundation/diagnostics/log.h"
#include "rendering/rhi/shaders/nx_interop.h"
#include "scene/sampler.h"

namespace nxe::spine2d {
namespace {

constexpr nx::string_view UPDATE_SYSTEM = "spine.update";
constexpr nx::string_view EMIT_SYSTEM = "spine.emit";
constexpr ModuleService PROVIDED_SERVICES[] = {
    {.id = SERVICE, .version = {1, 0, 0}},
};

class SpineModule final : public Module {
public:
  [[nodiscard]] ModuleDescriptor descriptor() const noexcept override {
    ModuleDescriptor out{};
    out.id = "spine";
    out.version = {1, 0, 0};
    out.provided_services = PROVIDED_SERVICES;
    return out;
  }

  bool on_register(ModuleContext &ctx) override {
    m_system.set_threads(&ctx.threads());
    const u32 sampler = ctx.samplers().index(scene::sampler_bilinear());
    m_system.set_texture_resolver(
        TextureResolver([this, &ctx, sampler](const nx::string_view path,
                                              const bool premultiplied) {
          const rhi::TextureHandle texture = m_textures.resolve(ctx, path);
          return texture.valid()
                     ? NxTexture2D<float4>::from_indices(
                           ctx.device().texture_index(texture), sampler,
                           (premultiplied ? NX_TEXTURE_PREMULTIPLIED : 0u))
                     : NxTexture2D<float4>::none();
        }));
    if (!ctx.service_registrar().provide(SERVICE, PROVIDED_SERVICES[0].version,
                                         m_system)) {
      m_system.set_threads(nullptr);
      return false;
    }
    SpineSystem::register_components(ctx.scene().registry());
    return true;
  }

  void on_expose_scripts(script::Host &host, ModuleContext &ctx) override {
    expose_spine_services(host, ctx);
  }

  void on_hot_reload(ModuleContext &ctx) override {
    (void)m_system.reload_changed(ctx.scene().registry());
  }

  bool on_attach(ModuleContext &ctx) override {
    ctx.schedule().define(
        UPDATE_SYSTEM, sys::SystemFn([this, &ctx](const sys::Context &c) {
          if (m_textures.pump(ctx) != 0)
            (void)m_system.refresh_textures();
          (void)m_system.update(ctx.scene().registry(), ctx.scene().assets(),
                                c.dt);
          release_unused(ctx);
        }));
    ctx.schedule().add(sys::Stage::Update, UPDATE_SYSTEM);

    ctx.schedule().define(
        EMIT_SYSTEM, sys::SystemFn([this, &ctx](const sys::Context &) {
          r2d::MeshChannel *const meshes = ctx.mesh_channel();
          const r2d::FramePacket *const packet = ctx.frame_packet();
          if (meshes == nullptr || packet == nullptr)
            return;
          const SpineView view{.camera = packet->active_camera,
                               .depth_min = ctx.renderer().depth_min(),
                               .depth_max = ctx.renderer().depth_max(),
                               .materials = ctx.renderer().materials()};
          (void)m_system.emit(ctx.scene().registry(), *meshes, view);
        }));
    ctx.schedule().add(sys::Stage::Present, EMIT_SYSTEM);
    ctx.schedule()
        .declare<const SpineComponent, SpineInstance,
                 const scene::WorldTransform2D>(EMIT_SYSTEM);
    ctx.schedule().declare_exclusive(EMIT_SYSTEM, ModuleContext::MESH_CHANNEL);

    nx::logi("spine: attached");
    return true;
  }

  void on_detach(ModuleContext &) override { m_system.set_threads(nullptr); }

  void on_unregister(ModuleContext &ctx) override {
    m_system.set_threads(nullptr);
    m_system.clear_assets();
    m_textures.release_all(ctx);
  }

private:
  /// Lets go of the skeletons and texture pages no component uses any more,
  /// once a component has come or gone: an unloaded level must not keep them.
  void release_unused(ModuleContext &ctx) {
    m_in_use.clear();
    const usize components =
        m_system.textures_in_use(ctx.scene().registry(), m_in_use);
    if (components == m_swept_components)
      return;
    m_swept_components = components;
    const usize skeletons = m_system.release_unused(ctx.scene().registry());
    const usize pages =
        m_textures.release_unused(ctx, [this](const nx::string_view path) {
          for (const nx::string_view used : m_in_use)
            if (used == path)
              return true;
          return false;
        });
    if (skeletons != 0 || pages != 0)
      nx::logd("spine: released {} skeletons and {} texture pages no "
               "component uses",
               skeletons, pages);
  }

  SpineSystem m_system;
  AsyncTextureSet m_textures;
  nx::vector<nx::string_view> m_in_use;
  usize m_swept_components = 0;
};

} // namespace

SpineSystem *system(Engine &engine) noexcept {
  return engine.services().find<SpineSystem>(SERVICE);
}

} // namespace nxe::spine2d

NX_DECLARE_MODULE(spine, nxe::spine2d::SpineModule)
