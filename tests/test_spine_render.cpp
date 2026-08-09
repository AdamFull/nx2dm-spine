/**
 * @file test_spine_render.cpp
 * @brief spineboy through the real mesh pipeline, and off the GPU again.
 *
 * Everything else here proves the geometry is right in memory. This one puts
 * it through the shader the engine draws with and reads the pixels back, which
 * is the only claim arithmetic cannot make: that a skeleton reaches the screen,
 * and that animating it changes what is on it.
 *
 * Pages resolve to no texture, so the fragment path returns white and what
 * lands is the vertex colour. Deliberate: decoding a PNG is the engine's job
 * and this target links no engine.
 */

#include "framework/nxtest.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/strings/format.h"
#include "core/foundation/vfs/vfs.h"
#include "core/rendering/rhi/rhi.h"
#include "spine/spine_assets.h"
#include "spine/spine_system.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdlib>
#include <cstring>

namespace {

namespace spine2d = nxe::spine2d;
namespace scene = nxe::scene;
namespace r2d = nxe::r2d;
namespace rhi = nxe::rhi;

constexpr u32 TARGET = 256;
constexpr nx::string_view SKELETON = "/spine/spineboy/export/spineboy-pro.skel";
constexpr nx::string_view ATLAS = "/spine/spineboy/export/spineboy-pma.atlas";

/// What the mesh pass pushes, spelled here rather than included: nx_engine owns
/// MeshPushBlock and this target links no engine.
struct MeshPush {
  u64 cameras = 0;
  u64 vertices = 0;
  u64 indices = 0;
  ::MeshPushFields fields = {};
};

struct TestDevice {
  rhi::Device device;
  bool ready = false;

  TestDevice() {
    rhi::DeviceDesc desc{};
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

[[nodiscard]] rhi::ShaderHandle load_mesh_shader(rhi::Device &device) {
  const nx::string base = nx::string(NX_TEST_SHADER_DIR) + "/mesh";
  auto code = nx::fs::file_read(nx::fs::path_view(base + ".spv"));
  auto refl = nx::fs::file_read_text(nx::fs::path_view(base + ".refl.json"));
  if (!code.has_value() || !refl.has_value())
    return {};
  return device.create_shader({
      .name = "mesh",
      .code = code->data(),
      .code_size = code->size(),
      .reflection_json = refl->view(),
  });
}

/// How many pixels the clear colour did not survive.
[[nodiscard]] usize lit(const u8 *const pixels) noexcept {
  usize n = 0;
  for (usize i = 0; i < nx::cast<usize>(TARGET) * TARGET; ++i)
    if (pixels[i * 4] != 0u || pixels[i * 4 + 1] != 0u ||
        pixels[i * 4 + 2] != 0u)
      ++n;
  return n;
}

/// Writes the frame out when NX_SPINE_DUMP names a directory, so "what did it
/// actually draw" is answerable without a screen. Off in every ordinary run.
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
  TestDevice fixture;
  if (!fixture.ready)
    SKIP("no usable RHI device");
  rhi::Device &device = fixture.device;

  const rhi::ShaderHandle shader = load_mesh_shader(device);
  if (!shader.valid())
    SKIP("shaders are not built in this configuration");

  nx::vfs::initialize();
  nx::vfs::Device *const host =
      nx::vfs::make_host_device(nx::fs::path_view(NX_TEST_ASSET_DIR));
  REQUIRE(host != nullptr);
  nx::vfs::mount("/", host);

  spine2d::SkeletonAsset asset;
  nx::string error;
  REQUIRE(spine2d::load_skeleton(SKELETON, ATLAS, {}, asset, error));

  scene::registry_t registry;
  registry.register_component<scene::WorldTransform2D>(
      {.name = "WorldTransform2D"});
  spine2d::SpineSystem::register_components(registry);
  spine2d::SpineSystem system;

  const scene::Entity e = registry.create();
  scene::WorldTransform2D &node = registry.emplace<scene::WorldTransform2D>(e);
  // spineboy is 686 units tall and stands on his own origin; this puts the
  // whole of him inside the unit square the camera below covers.
  node.world[0][0] = 1.f / 900.f;
  node.world[1][1] = 1.f / 900.f;
  node.world[2][0] = 0.5f;
  node.world[2][1] = 0.05f;
  REQUIRE(system.attach(registry, e, asset).play("walk"));

  const rhi::TextureHandle target = device.create_texture({
      .name = "spine target",
      .format = rhi::Format::RGBA8_UNORM,
      .width = TARGET,
      .height = TARGET,
      .usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySrc,
  });
  REQUIRE(target.valid());

  const rhi::PipelineHandle pipeline = device.create_graphics_pipeline({
      .name = "mesh",
      .vertex = {.shader = shader, .entry_point = "vs_main"},
      .fragment = {.shader = shader, .entry_point = "fs_main"},
      .color_formats = {rhi::Format::RGBA8_UNORM},
      .color_count = 1,
      .blend = {{.enabled = true, .mode = rhi::BlendMode::Premultiplied}},
  });
  REQUIRE(pipeline.valid());

  // World [0,1]x[0,1] onto the whole target, as the sprite draw test does.
  GpuCamera2D camera = {};
  glm::mat4 proj(1.f);
  proj[0][0] = 2.f;
  proj[1][1] = 2.f;
  proj[3][0] = -1.f;
  proj[3][1] = -1.f;
  camera.view_proj = proj;

  const auto upload = [&](const nx::string_view name, const void *const data,
                          const u64 bytes) {
    const rhi::BufferHandle b = device.create_buffer({
        .name = name,
        .size = bytes,
        .usage = rhi::BufferUsage::Storage | rhi::BufferUsage::DeviceAddress,
        .memory = rhi::MemoryUsage::Upload,
        .persistently_mapped = true,
    });
    REQUIRE(b.valid());
    std::memcpy(device.buffer_mapped(b), data, bytes);
    return b;
  };
  const rhi::BufferHandle cameras =
      upload("spine cameras", &camera, sizeof(camera));

  nx::vector<u8> frames[2];
  for (u32 pass = 0; pass < 2; ++pass) {
    // Half a second apart: far enough into the walk cycle that a leg has
    // swung, and the same skeleton either way.
    system.update(registry, pass == 0 ? 0.f : 0.5f);
    r2d::MeshChannel channel;
    REQUIRE(system.emit(registry, channel, {}) > 0u);
    REQUIRE(!channel.vertices.empty());

    const rhi::BufferHandle vertices = upload(
        "spine vertices", channel.vertices.data(),
        nx::cast<u64>(channel.vertices.size()) * sizeof(r2d::MeshVertex));
    const rhi::BufferHandle indices =
        upload("spine indices", channel.indices.data(),
               nx::cast<u64>(channel.indices.size()) * sizeof(u32));

    MeshPush push;
    push.cameras = device.buffer_address(cameras);
    push.vertices = device.buffer_address(vertices);
    push.indices = device.buffer_address(indices);

    rhi::CommandContext cmd;
    REQUIRE(device.begin_headless_frame(cmd));
    cmd.barrier(rhi::TextureBarrier{.texture = target,
                                    .from = rhi::ResourceState::Undefined,
                                    .to = rhi::ResourceState::ColorAttachment});
    rhi::RenderPassDesc render = {};
    render.name = "spine";
    render.color[0].texture = target;
    render.color[0].load = rhi::LoadOp::Clear;
    render.color[0].store = rhi::StoreOp::Store;
    render.color[0].clear = rhi::clear_color(0.f, 0.f, 0.f, 1.f);
    render.color_count = 1;
    cmd.begin_render_pass(render);
    cmd.set_viewport(
        {.width = nx::cast<f32>(TARGET), .height = nx::cast<f32>(TARGET)});
    cmd.set_scissor({{0, 0}, {TARGET, TARGET}});
    cmd.bind_pipeline(pipeline);
    for (const r2d::MeshDraw &draw : channel.draws) {
      push.fields.index_offset = draw.first_index;
      push.fields.vertex_offset = draw.vertex_offset;
      push.fields.texture = draw.texture;
      push.fields.camera = draw.camera;
      cmd.push_constants(&push, sizeof(push));
      cmd.draw(draw.index_count);
    }
    cmd.end_render_pass();
    cmd.barrier(rhi::TextureBarrier{.texture = target,
                                    .from = rhi::ResourceState::ColorAttachment,
                                    .to = rhi::ResourceState::CopySrc});
    REQUIRE(device.end_headless_frame());
    device.wait_idle();

    const rhi::ReadbackResult pixels = device.uploader().read_texture(target);
    REQUIRE(pixels.data != nullptr);
    device.uploader().wait(pixels.ticket);
    frames[pass].assign(pixels.data,
                        pixels.data + nx::cast<usize>(TARGET) * TARGET * 4);
    dump(frames[pass].data(), pass);

    device.destroy_buffer(indices);
    device.destroy_buffer(vertices);
  }

  // A character, not a stray triangle and not a full screen: spineboy standing
  // in a square this size covers a decent slice of it and nowhere near all.
  const usize covered = lit(frames[0].data());
  CHECK(covered > (TARGET * TARGET) / 20);
  CHECK(covered < (TARGET * TARGET * 4) / 5);

  // And half a second of walking moves enough of them that no fixed pose could
  // have produced both frames.
  CHECK(differing(frames[0].data(), frames[1].data()) > covered / 10);

  device.destroy_buffer(cameras);
  device.destroy_pipeline(pipeline);
  device.destroy_texture(target);
  device.destroy_shader(shader);
  registry.clear();
  asset = spine2d::SkeletonAsset();
  nx::vfs::shutdown();
}
