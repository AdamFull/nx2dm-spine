/**
 * @file game.cpp
 * @brief {{project}}.
 */

#include "core/app/engine.h"

#include "spine/spine_assets.h"
#include "spine/spine_module.h"
#include "core/app/script_services.h"
#include "core/script/luau/luau_backend.h"
#include "core/script/luau/luau_bindings.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/strings/format.h"
#include "core/foundation/vfs/vfs.h"

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
    start_scripts(engine);

    nx::logi("{{project}}: up");
    return true;
  }

  /// The skeleton. Unlike Live2D's, a Spine asset is loaded by the project
  /// rather than by a component: the atlas needs its pages resolved to bindless
  /// texture words, and only something holding an Engine can do that.
  void add_skeleton(nxe::Engine &engine) {
    const nxe::rhi::TextureHandle page =
        engine.load_texture("/spine/spineboy-pma.png");
    if (!page.valid()) {
      nx::logw("{{project}}: no skeleton art under /spine");
      return;
    }
    const u32 packed = pack_texture(engine.device().texture_index(page),
                                    engine.samplers().index({}));

    nx::string error;
    if (!nxe::spine2d::load_skeleton(
            "/spine/spineboy-pro.skel", "/spine/spineboy-pma.atlas",
            nxe::spine2d::TextureResolver(
                [packed](nx::string_view, bool) { return packed; }),
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

  /// The script host: a backend, the engine's services, and the prelude.
  ///
  /// assets/scripts/host.luau declares every service by name and shape, and
  /// the check below runs *both* ways - a service exposed and not declared is
  /// as much an error as one declared and not exposed. Expose something of
  /// your own and you add a line there in the same change.
  void start_scripts(nxe::Engine &engine) {
    if (!engine.scripts().set_backend(nxe::script::luau_backend())) {
      nx::logw("{{project}}: no script backend; scripts will not run");
      return;
    }

    nxe::script::expose_core_services(engine.scripts());
    nxe::expose_action_services(engine.scripts(), engine.actions());
    nxe::expose_audio_services(engine.scripts(), engine.audio());
    nxe::expose_mixer_services(engine.scripts(), engine.mixer());
    nxe::expose_scene_services(engine.scripts(), engine.scene());
    nxe::expose_render_services(engine.scripts(), engine.render_vars());
    nxe::expose_ui_services(engine.scripts(), engine.ui());
    engine.scripts().expose_as("quit", [&engine] { engine.request_quit(); });

    // Modules a project's own code exposes go in before this: binding is
    // final, and a service offered afterwards is refused.
    if (!engine.scripts().bind()) {
      nx::logw("{{project}}: the VM took no host services");
      return;
    }

    for (const nx::string &name : engine.frame().modules())
      if (!load_script(engine, name))
        return;

    (void)engine.scripts().define(engine.schedule(), "skeleton",
                                  "game.skeleton");
  }

  [[nodiscard]] static bool load_script(nxe::Engine &engine,
                                        const nx::string_view name) {
    const nx::string path = nx::format("/scripts/{}.luau", name);
    const auto text = nx::vfs::read_text(path);
    if (!text) {
      nx::loge("{{project}}: no {}", path);
      return false;
    }

    if (name == "host") {
      const auto generated = nx::vfs::read_text("/scripts/host_modules.luau");
      if (!generated) {
        nx::loge("{{project}}: no /scripts/host_modules.luau; build the "
                 "nx_host_declarations target");
        return false;
      }
      const nx::string_view sources[] = {text.value().view(),
                                         generated.value().view()};
      nx::string disagreement;
      if (!nxe::script::luau_host_types_agree(
              sources, engine.scripts().services(), disagreement)) {
        nx::loge("{{project}}: {} does not match the exposed services: {}",
                 path, disagreement);
        return false;
      }
    }

    return engine.scripts().load(
        name,
        {reinterpret_cast<const std::byte *>(text.value().data()),
         text.value().size()},
        path);
  }


private:
  nxe::spine2d::SkeletonAsset m_skeleton;
};

} // namespace

NX_IMPLEMENT_GAME({{Project}}Game)
