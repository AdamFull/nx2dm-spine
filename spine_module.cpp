
#include "spine/spine_module.h"

#include "spine/spine_scripting.h"

#include "core/app/engine.h"
#include "core/app/module.h"

#include "core/foundation/diagnostics/log.h"

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

  bool on_attach(ModuleContext &ctx) override {
    ctx.schedule().define(UPDATE_SYSTEM,
                          sys::SystemFn([this, &ctx](const sys::Context &c) {
                            (void)m_system.update(ctx.scene().registry(),
                                                  ctx.scene().assets(), c.dt);
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

  void on_unregister(ModuleContext &) override {
    m_system.set_threads(nullptr);
  }

private:
  SpineSystem m_system;
};

}

SpineSystem *system(Engine &engine) noexcept {
  return engine.services().find<SpineSystem>(SERVICE);
}

}

NX_DECLARE_MODULE(spine, nxe::spine2d::SpineModule)
