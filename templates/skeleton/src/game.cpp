
#include "core/app/engine.h"
#include "core/app/script_runtime.h"
#include "core/script/luau/luau_backend.h"
#include "core/script/luau/luau_runtime.h"

#include "spine/spine_assets.h"
#include "spine/spine_module.h"

#include "core/foundation/diagnostics/log.h"

namespace {

class {
  {
    Project
  }
} Game {
public:
  void configure(nxe::EngineConfig & config) {
    config.app.name = "{{project}}";
    config.app.window.title = "{{project}}";
    config.app.window.width = 1280;
    config.app.window.height = 720;

    config.action_map = {};
    config.saved_bindings = {};
    config.ui_styles = {};
    config.physics_rules = {};
    config.audio_bank = {};
  }

  bool on_create(nxe::Engine & engine) {
    const nxe::scene::Entity camera = engine.scene().create_node("camera");
    engine.scene().registry().emplace<nxe::scene::Camera2D>(
        camera, nxe::scene::Camera2D{.ortho_height = 6.f, .active = true});
    engine.scene().set_active_camera(camera);

    add_skeleton(engine);
    if (nxe::start_scripts(engine,
                           {.backend = nxe::script::luau_backend(),
                            .expose_game = {},
                            .load_module = &nxe::script::load_luau_module}))
      (void)engine.scripts().define(engine.schedule(), "skeleton",
                                    "game.skeleton");

    nx::logi("{{project}}: up");
    return true;
  }

  void add_skeleton(nxe::Engine & engine) {
    const nxe::rhi::TextureHandle page =
        engine.load_texture("/spine/spineboy-pma.png");
    if (!page.valid()) {
      nx::logw("{{project}}: no skeleton art under /spine");
      return;
    }
    const u32 index = engine.device().texture_index(page);
    const u32 sampler = engine.samplers().index({});

    nx::string error;
    if (!nxe::spine2d::load_skeleton(
            "/spine/spineboy.nxspine",
            nxe::spine2d::TextureResolver(
                [index, sampler](nx::string_view, const bool premultiplied) {
                  return pack_texture(index, sampler, premultiplied);
                }),
            m_skeleton, error)) {
      nx::logw("{{project}}: {}", error);
      return;
    }

    const nxe::scene::Entity e = engine.scene().create_node("skeleton");
    engine.scene().set_scale(e, {0.004f, 0.004f});
    engine.scene().set_position(e, {0.f, -2.f});
    nxe::spine2d::SpineSystem *const spine = nxe::spine2d::system(engine);
    if (spine == nullptr) {
      nx::loge("{{project}}: Spine service is unavailable");
      return;
    }
    spine->attach(engine.scene().registry(), e, m_skeleton).play("idle");
  }

private:
  nxe::spine2d::SkeletonAsset m_skeleton;
};

} // namespace

NX_IMPLEMENT_GAME_OBJECT({
  {
    Project
  }
} Game)
