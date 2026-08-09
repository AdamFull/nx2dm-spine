#include "spine/spine_assets.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/platform/filesystem.h"
#include "core/foundation/strings/format.h"
#include "core/foundation/vfs/vfs.h"
#include "core/rendering/render2d/render_interop.h"
#include "spine/spine_platform.h"

#include <spine/AnimationStateData.h>
#include <spine/Atlas.h>
#include <spine/SkeletonBinary.h>
#include <spine/SkeletonData.h>
#include <spine/SkeletonJson.h>
#include <spine/SpineString.h>
#include <spine/TextureLoader.h>

namespace nxe::spine2d {
namespace {

/// Hands each page's image to the caller's resolver and keeps the answer in
/// AtlasPage::texture, which is where every attachment finds it later.
class ResolvingLoader final : public ::spine::TextureLoader {
public:
  explicit ResolvingLoader(TextureResolver resolve)
      : m_resolve(std::move(resolve)) {}

  void load(::spine::AtlasPage &page, const ::spine::String &path) override {
    m_premultiplied = m_premultiplied || page.pma;
    const nx::string_view where(path.buffer(), nx::cast<usize>(path.length()));
    const u32 packed = m_resolve ? m_resolve(where, page.pma)
                                 : pack_texture(NX_TEXTURE_NONE, 0);
    // A void* holding an integer rather than a pointer: the field exists for
    // whatever a host wants to associate, and ours is a bindless word.
    page.texture = reinterpret_cast<void *>(static_cast<uintptr_t>(packed));
    if ((packed >> 16) == NX_TEXTURE_NONE)
      nx::logw("spine: no texture for '{}'; its slots will draw untextured",
               where);
  }

  void unload(void *) override {
    // The atlas owns no texture: the registry that resolved it does, and it
    // outlives every skeleton drawn from it.
  }

  [[nodiscard]] bool premultiplied() const noexcept { return m_premultiplied; }

private:
  TextureResolver m_resolve;
  bool m_premultiplied = false;
};

[[nodiscard]] nx::string_view to_view(const ::spine::String &text) noexcept {
  return {text.buffer(), nx::cast<usize>(text.length())};
}

} // namespace

SkeletonAsset::~SkeletonAsset() { reset(); }

SkeletonAsset::SkeletonAsset(SkeletonAsset &&other) noexcept
    : m_loader(other.m_loader), m_atlas(other.m_atlas), m_data(other.m_data),
      m_mixes(other.m_mixes), m_premultiplied(other.m_premultiplied) {
  other.m_loader = nullptr;
  other.m_atlas = nullptr;
  other.m_data = nullptr;
  other.m_mixes = nullptr;
}

SkeletonAsset &SkeletonAsset::operator=(SkeletonAsset &&other) noexcept {
  if (this != &other) {
    reset();
    m_loader = other.m_loader;
    m_atlas = other.m_atlas;
    m_data = other.m_data;
    m_mixes = other.m_mixes;
    m_premultiplied = other.m_premultiplied;
    other.m_loader = nullptr;
    other.m_atlas = nullptr;
    other.m_data = nullptr;
    other.m_mixes = nullptr;
  }
  return *this;
}

void SkeletonAsset::reset() noexcept {
  // Reverse of construction: the mixes reference the data, and the data's
  // attachments reference the atlas's regions.
  delete m_mixes;
  delete m_data;
  delete m_atlas;
  // After the atlas: its destructor calls unload() on this.
  delete m_loader;
  m_mixes = nullptr;
  m_data = nullptr;
  m_atlas = nullptr;
  m_loader = nullptr;
  m_premultiplied = false;
}

usize SkeletonAsset::bone_count() const noexcept {
  return m_data == nullptr ? 0u : nx::cast<usize>(m_data->getBones().size());
}

usize SkeletonAsset::slot_count() const noexcept {
  return m_data == nullptr ? 0u : nx::cast<usize>(m_data->getSlots().size());
}

usize SkeletonAsset::animation_count() const noexcept {
  return m_data == nullptr ? 0u
                           : nx::cast<usize>(m_data->getAnimations().size());
}

usize SkeletonAsset::skin_count() const noexcept {
  return m_data == nullptr ? 0u : nx::cast<usize>(m_data->getSkins().size());
}

bool SkeletonAsset::has_animation(const nx::string_view name) const noexcept {
  if (m_data == nullptr)
    return false;
  const nx::string owned(name);
  return m_data->findAnimation(::spine::String(owned.c_str())) != nullptr;
}

bool load_skeleton(const nx::string_view skeleton_path,
                   const nx::string_view atlas_path, TextureResolver resolve,
                   SkeletonAsset &out, nx::string &error) {
  install_platform();
  out.reset();
  error.clear();

  const auto atlas_text = nx::vfs::read(atlas_path);
  if (!atlas_text) {
    error = nx::format("no atlas at '{}'", atlas_path);
    return false;
  }

  // Each page's image is named relative to the atlas, so the runtime needs the
  // directory the atlas came from rather than the atlas itself.
  const nx::string dir(nx::fs::path::parent_path(atlas_path));

  auto *const loader = new ResolvingLoader(std::move(resolve));
  auto *const atlas = new ::spine::Atlas(
      reinterpret_cast<const char *>(atlas_text->data()),
      nx::cast<int>(atlas_text->size()), dir.c_str(), loader);
  if (atlas->getPages().size() == 0) {
    delete atlas;
    delete loader;
    error = nx::format("'{}' names no pages", atlas_path);
    return false;
  }

  ::spine::SkeletonData *data = nullptr;
  const bool binary = nx::fs::path::extension(skeleton_path) == ".skel";
  if (binary) {
    if (const auto bytes = nx::vfs::read(skeleton_path)) {
      ::spine::SkeletonBinary reader(*atlas);
      data = reader.readSkeletonData(
          reinterpret_cast<const unsigned char *>(bytes->data()),
          nx::cast<int>(bytes->size()));
      if (data == nullptr)
        error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
    } else {
      error = nx::format("no skeleton at '{}'", skeleton_path);
    }
  } else if (const auto text = nx::vfs::read_text(skeleton_path)) {
    ::spine::SkeletonJson reader(*atlas);
    data = reader.readSkeletonData(text->c_str());
    if (data == nullptr)
      error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
  } else {
    error = nx::format("no skeleton at '{}'", skeleton_path);
  }

  if (data == nullptr) {
    delete atlas;
    delete loader;
    return false;
  }

  out.m_loader = loader;
  out.m_atlas = atlas;
  out.m_data = data;
  out.m_mixes = new ::spine::AnimationStateData(*data);
  out.m_premultiplied = loader->premultiplied();
  nx::logi("spine: '{}' - {} bones, {} slots, {} animations{}", skeleton_path,
           out.bone_count(), out.slot_count(), out.animation_count(),
           out.premultiplied() ? ", premultiplied" : "");
  return true;
}

} // namespace nxe::spine2d
