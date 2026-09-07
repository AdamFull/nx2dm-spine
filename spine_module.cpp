
#include "spine/spine_module.h"

#include "spine/spine_scripting.h"

#include "core/app/async_texture_set.h"
#include "core/app/engine.h"
#include "core/app/module.h"

#include "core/foundation/diagnostics/log.h"
#include "core/rendering/render2d/render_interop.h"
#include "core/scene/sampler.h"

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
                     ? pack_texture(ctx.device().texture_index(texture),
                                    sampler, premultiplied)
                     : pack_texture(NX_TEXTURE_NONE, 0);
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
            (void)m_system.reload_changed(ctx.scene().registry(), true);
          (void)m_system.update(ctx.scene().registry(), ctx.scene().assets(),
                                c.dt);
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
  SpineSystem m_system;
  AsyncTextureSet m_textures;
};

} // namespace

SpineSystem *system(Engine &engine) noexcept {
  return engine.services().find<SpineSystem>(SERVICE);
}

} // namespace nxe::spine2d

NX_DECLARE_MODULE(spine, nxe::spine2d::SpineModule)
