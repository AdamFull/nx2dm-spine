/**
 * @file spine_module.cpp
 * @brief What tells an Engine about Spine, and the only file here that knows
 * an Engine exists.
 */

#include "spine/spine_module.h"

#include "spine/spine_scripting.h"

#include "core/app/engine.h"
#include "core/app/module.h"

#include "core/foundation/diagnostics/log.h"

namespace nxe::spine2d {
namespace {

constexpr nx::string_view UPDATE_SYSTEM = "spine.update";
constexpr nx::string_view EMIT_SYSTEM = "spine.emit";

class SpineModule final : public Module {
public:
  [[nodiscard]] nx::string_view name() const noexcept override {
    return "spine";
  }

  bool on_register(Engine &engine) override {
    SpineSystem::register_components(engine.scene().registry());
    return true;
  }

  void on_expose_scripts(script::Host &host, Engine &engine) override {
    expose_spine_services(host, engine);
  }

  bool on_attach(Engine &engine) override {
    system().set_threads(&engine.threads());

    engine.schedule().define(
        UPDATE_SYSTEM, sys::SystemFn([&engine](const sys::Context &c) {
          (void)system().update(engine.scene().registry(), c.dt);
        }));
    engine.schedule().add(sys::Stage::Update, UPDATE_SYSTEM);

    engine.schedule().define(
        EMIT_SYSTEM, sys::SystemFn([&engine](const sys::Context &) {
          r2d::MeshChannel *const meshes = engine.mesh_channel();
          const r2d::FramePacket *const packet = engine.frame_packet();
          if (meshes == nullptr || packet == nullptr)
            return;
          const SpineView view{.camera = packet->active_camera,
                               .depth_min = engine.renderer().depth_min(),
                               .depth_max = engine.renderer().depth_max()};
          (void)system().emit(engine.scene().registry(), *meshes, view);
        }));
    engine.schedule().add(sys::Stage::Present, EMIT_SYSTEM);

    nx::logi("spine: attached");
    return true;
  }
};

} // namespace

SpineSystem &system() {
  static SpineSystem instance;
  return instance;
}

} // namespace nxe::spine2d

NX_DECLARE_MODULE(spine, nxe::spine2d::SpineModule)
