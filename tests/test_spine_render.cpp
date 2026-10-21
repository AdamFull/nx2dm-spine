
#include "framework/nxtest.h"

#include "fixture.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/strings/format.h"
#include "core/foundation/vfs/vfs.h"
#include "nx/gfx/gfx.h"
#include "rendering/owned_device.h"
#include "rendering/pipeline_test_utils.h"
#include "spine/spine_assets.h"
#include "spine/spine_system.h"

#include <stb_image_write.h>

#include <cstdlib>
#include <cstring>

namespace gfx = nx::gfx;

namespace {

using namespace nxm::spine_test;

namespace spine2d = nxe::spine2d;
namespace scene = nxe::scene;
namespace r2d = nxe::r2d;
constexpr u32 TARGET = 256;

struct TestDevice {
  nxe::test::OwnedDevice device;
  bool ready = false;

  TestDevice() {
    gfx::device_desc desc{};
    desc.application_name = "nx spine tests";
    ready = device.init(desc);
  }
  ~TestDevice() {
    if (ready)
      device.shutdown();
  }
  TestDevice(const TestDevice &) = delete;
  TestDevice &operator=(const TestDevice &) = delete;
};

[[nodiscard]] gfx::shader_handle load_mesh_shader(gfx::device &device) {
  const nx::string base = nx::string(NX_TEST_SHADER_DIR) + "/mesh";
  auto code = nx::fs::file_read(nx::fs::path_view(base + ".spv"));
  auto refl = nx::fs::file_read_text(nx::fs::path_view(base + ".refl.json"));
  if (!code.has_value() || !refl.has_value())
    return {};
  return nxe::test::create_shader(
      device, {.name = "mesh", .code = code->data(), .code_size = code->size()},
      refl->view());
}

[[nodiscard]] usize lit(const u8 *const pixels) noexcept {
  usize n = 0;
  for (usize i = 0; i < nx::cast<usize>(TARGET) * TARGET; ++i)
    if (pixels[i * 4] != 0u || pixels[i * 4 + 1] != 0u ||
        pixels[i * 4 + 2] != 0u)
      ++n;
  return n;
}

void dump(const u8 *const pixels, const u32 pass) {
  const char *const dir = std::getenv("NX_SPINE_DUMP");
  if (dir == nullptr)
    return;
  const nx::string path = nx::format("{}/spine_frame{}.png", dir, pass);
  (void)stbi_write_png(path.c_str(), nx::cast<int>(TARGET),
                       nx::cast<int>(TARGET), 4, pixels,
                       nx::cast<int>(TARGET) * 4);
}

[[nodiscard]] usize differing(const u8 *const a, const u8 *const b) noexcept {
  usize n = 0;
  for (usize i = 0; i < nx::cast<usize>(TARGET) * TARGET; ++i)
    if (std::memcmp(a + i * 4, b + i * 4, 3) != 0)
      ++n;
  return n;
}

} // namespace

TEST_CASE("spine: a skeleton reaches the framebuffer, and walking changes it") {
  NX_REQUIRE_FIXTURE();
  TestDevice fixture;
  if (!fixture.ready)
    SKIP("no usable RHI device");
  gfx::device &device = fixture.device;

  const gfx::shader_handle shader = load_mesh_shader(device);
  if (!shader.valid())
    SKIP("shaders are not built in this configuration");

  nx::vfs::initialize();
  nx::vfs::Device *const host = nx::vfs::make_host_device(
      nx::fs::path_view(nxm::spine_test::fixture_dir()));
  REQUIRE(host != nullptr);
  nx::vfs::mount("/", host);

  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(spine2d::load_skeleton(SKELETON, ATLAS_PMA, {}, asset, error));

  scene::registry_t registry;
  registry.register_component<scene::WorldTransform2D>(
      {.name = "WorldTransform2D"});
  spine2d::SpineSystem::register_components(registry);
  spine2d::SpineSystem system;

  const scene::Entity e = registry.create();
  scene::WorldTransform2D &node = registry.emplace<scene::WorldTransform2D>(e);
  node.world[0][0] = 1.f / 900.f;
  node.world[1][1] = 1.f / 900.f;
  node.world[2][0] = 0.5f;
  node.world[2][1] = 0.05f;
  REQUIRE(system.attach(registry, e, asset).play("walk"));

  const gfx::texture_handle target = gfx::create_texture(
      device, {
                  .name = "spine target",
                  .format = gfx::format::RGBA8_UNORM,
                  .width = TARGET,
                  .height = TARGET,
                  .usage = gfx::texture_usage::RenderTarget |
                           gfx::texture_usage::CopySrc,
              });
  REQUIRE(target.valid());

  const gfx::pipeline_handle pipeline = nxe::test::compile_pipeline(
      device,
      {
          .name = "mesh",
          .vertex = {.shader = shader, .entry_point = "vs_main"},
          .fragment = {.shader = shader, .entry_point = "fs_main"},
          .color_formats = {gfx::format::RGBA8_UNORM},
          .color_count = 1,
          .blend = {{.enabled = true, .mode = gfx::blend_mode::Premultiplied}},
      });
  REQUIRE(pipeline.valid());

  GpuCamera2D camera = {};
  glm::mat4 proj(1.f);
  proj[0][0] = 2.f;
  proj[1][1] = 2.f;
  proj[3][0] = -1.f;
  proj[3][1] = -1.f;
  camera.view_proj = proj;

  const auto upload = [&](const nx::string_view name, const void *const data,
                          const u64 bytes) {
    const gfx::buffer_handle b = gfx::create_buffer(
        device, {
                    .name = name,
                    .size = bytes,
                    .usage = gfx::buffer_usage::Storage |
                             gfx::buffer_usage::DeviceAddress |
                             gfx::buffer_usage::Indirect,
                    .memory = gfx::memory_usage::Upload,
                    .persistently_mapped = true,
                });
    REQUIRE(b.valid());
    std::memcpy(gfx::buffer_mapped(device, b), data, bytes);
    return b;
  };
  const gfx::buffer_handle cameras =
      upload("spine cameras", &camera, sizeof(camera));

  nx::vector<u8> frames[2];
  for (u32 pass = 0; pass < 2; ++pass) {
    system.update(registry, pass == 0 ? 0.f : 0.5f);
    r2d::MeshChannel channel;
    REQUIRE(system.emit(registry, channel, {}) > 0u);
    REQUIRE(!channel.vertices.empty());

    const gfx::buffer_handle vertices = upload(
        "spine vertices", channel.vertices.data(),
        nx::cast<u64>(channel.vertices.size()) * sizeof(r2d::MeshVertex));
    const gfx::buffer_handle indices =
        upload("spine indices", channel.indices.data(),
               nx::cast<u64>(channel.indices.size()) * sizeof(u32));

    r2d::MeshStream stream;
    stream.build({channel.draws.data(), channel.draws.size()});
    const gfx::buffer_handle records =
        upload("spine draws", stream.records.data(),
               nx::cast<u64>(stream.records.size()) * sizeof(GpuMeshDraw2D));
    const gfx::buffer_handle commands =
        upload("spine draw commands", stream.commands.data(),
               nx::cast<u64>(stream.commands.size()) *
                   sizeof(gfx::draw_indirect_command));

    GpuMesh2DPush push;
    push.cameras = {gfx::buffer_address(device, cameras)};
    push.vertices = {gfx::buffer_address(device, vertices)};
    push.indices = {gfx::buffer_address(device, indices)};
    push.draws = {gfx::buffer_address(device, records)};

    gfx::command_list *cmd = nullptr;
    REQUIRE(((cmd = gfx::begin_headless_frame(device)) != nullptr));
    gfx::barrier(
        *cmd, gfx::texture_barrier{.texture = target,
                                   .from = gfx::resource_state::Undefined,
                                   .to = gfx::resource_state::ColorAttachment});
    gfx::render_pass_desc render = {};
    render.name = "spine";
    render.color[0].texture = target;
    render.color[0].load = gfx::load_op::Clear;
    render.color[0].store = gfx::store_op::Store;
    render.color[0].clear = gfx::clear_color(0.f, 0.f, 0.f, 1.f);
    render.color_count = 1;
    gfx::begin_render_pass(*cmd, render);
    gfx::set_viewport(*cmd, {.width = nx::cast<f32>(TARGET),
                             .height = nx::cast<f32>(TARGET)});
    gfx::set_scissor(*cmd, {{0, 0}, {TARGET, TARGET}});
    gfx::bind_pipeline(*cmd, pipeline);
    gfx::push_constants(*cmd, &push, sizeof(push));
    gfx::draw_indirect(*cmd, commands, 0, nx::cast<u32>(stream.commands.size()),
                       sizeof(gfx::draw_indirect_command));
    gfx::end_render_pass(*cmd);
    gfx::barrier(
        *cmd, gfx::texture_barrier{.texture = target,
                                   .from = gfx::resource_state::ColorAttachment,
                                   .to = gfx::resource_state::CopySrc});
    REQUIRE(gfx::end_headless_frame(device));
    gfx::wait_idle(device);

    const gfx::readback_result pixels = gfx::read_texture(device, target);
    REQUIRE(pixels.data != nullptr);
    gfx::wait_upload(device, pixels.ticket);
    frames[pass].assign(pixels.data,
                        pixels.data + nx::cast<usize>(TARGET) * TARGET * 4);
    dump(frames[pass].data(), pass);

    gfx::destroy_buffer(device, commands);
    gfx::destroy_buffer(device, records);
    gfx::destroy_buffer(device, indices);
    gfx::destroy_buffer(device, vertices);
  }

  const usize covered = lit(frames[0].data());
  CHECK(covered > (TARGET * TARGET) / 20);
  CHECK(covered < (TARGET * TARGET * 4) / 5);

  CHECK(differing(frames[0].data(), frames[1].data()) > covered / 10);

  gfx::destroy_buffer(device, cameras);
  gfx::destroy_pipeline(device, pipeline);
  gfx::destroy_texture(device, target);
  gfx::destroy_shader(device, shader);
  registry.clear();
  asset = spine2d::SkeletonAsset();
  nx::vfs::shutdown();
}
