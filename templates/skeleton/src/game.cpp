/**
 * @file game.cpp
 * @brief {{project}}.
 */

#include "core/app/engine.h"
#include "core/app/script_runtime.h"
#include "core/script/luau/luau_backend.h"
#include "core/script/luau/luau_runtime.h"

#include "spine/spine_assets.h"
#include "spine/spine_module.h"

#include "core/foundation/diagnostics/log.h"

namespace {

class {{Project}}Game final : public nxe::IGame {
public:
  void configure(nxe::EngineConfig &config) override {
    config.app.name = "{{project}}";
    config.app.window.title = "{{project}}";
    config.app.window.width = 1280;
    config.app.window.height = 720;

    // Empty rather than the engine's default paths: those name files a project
    // is expected to bring, and a scaffolded one has none yet. Each has a
    // built-in fallback - no bindings, the dark style, default physics rules.
    // Point them at your own once you have them.
    config.action_map = {};
    config.saved_bindings = {};
    config.ui_styles = {};
    config.physics_rules = {};
    config.audio_bank = {};
  }

  bool on_create(nxe::Engine &engine) override {
    // A camera, because a scene with none draws nothing and says nothing about
    // why. ortho_height is how much of the world fits top to bottom.
    const nxe::scene::Entity camera = engine.scene().create_node("camera");
    engine.scene().registry().emplace<nxe::scene::Camera2D>(
        camera, nxe::scene::Camera2D{.ortho_height = 6.f, .active = true});
    engine.scene().set_active_camera(camera);

    add_skeleton(engine);
    if (nxe::start_scripts(
            engine, {.backend = nxe::script::luau_backend(),
                     .expose_game = {},
                     .load_module = &nxe::script::load_luau_module}))
      (void)engine.scripts().define(engine.schedule(), "skeleton",
                                    "game.skeleton");

    nx::logi("{{project}}: up");
    return true;
  }

  /// The skeleton. Unlike Live2D's, a Spine asset is loaded by the project
  /// rather than by a component: the atlas needs its pages resolved to bindless
  /// texture words, and only something holding an Engine can do that.
  void add_skeleton(nxe::Engine &engine) {
    // The atlas is a -pma export: its colour is already multiplied by alpha,
    // which decides how it is decoded as well as how it blends.
    const nxe::rhi::TextureHandle page =
        engine.load_texture("/spine/spineboy-pma.png", true);
    if (!page.valid()) {
      nx::logw("{{project}}: no skeleton art under /spine");
      return;
    }
    const u32 index = engine.device().texture_index(page);
    const u32 sampler = engine.samplers().index({});

    nx::string error;
    if (!nxe::spine2d::load_skeleton(
            "/spine/spineboy-pro.skel", "/spine/spineboy-pma.atlas",
            // The page says whether it is premultiplied, and the word carries
            // it to the shader that samples it.
            nxe::spine2d::TextureResolver(
                [index, sampler](nx::string_view, const bool premultiplied) {
                  return pack_texture(index, sampler, premultiplied);
                }),
            m_skeleton, error)) {
      nx::logw("{{project}}: {}", error);
      return;
    }

    const nxe::scene::Entity e = engine.scene().create_node("skeleton");
    // Spine authors in pixels; the scene is in units, so a skeleton wants
    // scaling down by about the height of a character in pixels.
    engine.scene().set_scale(e, {0.004f, 0.004f});
    engine.scene().set_position(e, {0.f, -2.f});
    nxe::spine2d::system()
        .attach(engine.scene().registry(), e, m_skeleton)
        .play("idle");
  }

private:
  nxe::spine2d::SkeletonAsset m_skeleton;
};

} // namespace

NX_IMPLEMENT_GAME({{Project}}Game)
