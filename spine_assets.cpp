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

class ResolvingLoader final : public ::spine::TextureLoader {
public:
  explicit ResolvingLoader(TextureResolver resolve)
      : m_resolve(std::move(resolve)) {}

  void load(::spine::AtlasPage &page, const ::spine::String &path) override {
    m_premultiplied = m_premultiplied || page.pma;
    const nx::string_view where(path.buffer(), nx::cast<usize>(path.length()));
    const u32 packed = m_resolve ? m_resolve(where, page.pma)
                                 : pack_texture(NX_TEXTURE_NONE, 0);
    page.texture = reinterpret_cast<void *>(static_cast<uintptr_t>(packed));
    if ((packed >> 16) == NX_TEXTURE_NONE)
      nx::logw("spine: no texture for '{}'; its slots will draw untextured",
               where);
  }

  void unload(void *) override {
  }

  [[nodiscard]] bool premultiplied() const noexcept { return m_premultiplied; }

private:
  TextureResolver m_resolve;
  bool m_premultiplied = false;
};

[[nodiscard]] nx::string_view to_view(const ::spine::String &text) noexcept {
  return {text.buffer(), nx::cast<usize>(text.length())};
}

}

namespace detail {

struct SpineObjectDeleter {
  template <typename T> void operator()(T *const object) const noexcept {
    delete object;
  }
};

class SkeletonAssetData {
public:
  // Declaration order gives the required reverse destruction: mixes, data,
  // atlas, then loader. Their class-specific delete returns memory through
  // VfsExtension to nx::mem_free; the unique pointers provide only ownership.
  nx::unique_ptr<::spine::TextureLoader, SpineObjectDeleter> loader;
  nx::unique_ptr<::spine::Atlas, SpineObjectDeleter> atlas;
  nx::unique_ptr<::spine::SkeletonData, SpineObjectDeleter> data;
  nx::unique_ptr<::spine::AnimationStateData, SpineObjectDeleter> mixes;
  bool premultiplied = false;
};

}

bool SkeletonAsset::valid() const noexcept {
  return m_version != nullptr && m_version->data != nullptr;
}

::spine::SkeletonData *SkeletonAsset::data() const noexcept {
  return m_version == nullptr ? nullptr : m_version->data.get();
}

::spine::Atlas *SkeletonAsset::atlas() const noexcept {
  return m_version == nullptr ? nullptr : m_version->atlas.get();
}

::spine::AnimationStateData *SkeletonAsset::mixes() const noexcept {
  return m_version == nullptr ? nullptr : m_version->mixes.get();
}

bool SkeletonAsset::premultiplied() const noexcept {
  return m_version != nullptr && m_version->premultiplied;
}

usize SkeletonAsset::bone_count() const noexcept {
  return data() == nullptr ? 0u : nx::cast<usize>(data()->getBones().size());
}

usize SkeletonAsset::slot_count() const noexcept {
  return data() == nullptr ? 0u : nx::cast<usize>(data()->getSlots().size());
}

usize SkeletonAsset::animation_count() const noexcept {
  return data() == nullptr ? 0u
                           : nx::cast<usize>(data()->getAnimations().size());
}

usize SkeletonAsset::skin_count() const noexcept {
  return data() == nullptr ? 0u : nx::cast<usize>(data()->getSkins().size());
}

bool SkeletonAsset::has_animation(const nx::string_view name) const noexcept {
  if (data() == nullptr)
    return false;
  const nx::string owned(name);
  return data()->findAnimation(::spine::String(owned.c_str())) != nullptr;
}

bool load_skeleton(const nx::string_view skeleton_path,
                   const nx::string_view atlas_path, TextureResolver resolve,
                   SkeletonAsset &out, nx::string &error) {
  install_platform();
  out.m_version.reset();
  error.clear();

  const auto atlas_text = nx::vfs::read(atlas_path);
  if (!atlas_text) {
    error = nx::format("no atlas at '{}'", atlas_path);
    return false;
  }

  const nx::string dir(nx::fs::path::parent_path(atlas_path));

  nx::shared_ptr<detail::SkeletonAssetData> loaded =
      nx::make_shared<detail::SkeletonAssetData>();
  if (loaded == nullptr) {
    error = "out of memory while loading skeleton";
    return false;
  }

  loaded->loader.reset(new ResolvingLoader(std::move(resolve)));
  loaded->atlas.reset(new ::spine::Atlas(
      reinterpret_cast<const char *>(atlas_text->data()),
      nx::cast<int>(atlas_text->size()), dir.c_str(), loaded->loader.get()));
  if (loaded->atlas->getPages().size() == 0) {
    error = nx::format("'{}' names no pages", atlas_path);
    return false;
  }

  ::spine::SkeletonData *data = nullptr;
  const bool binary = nx::fs::path::extension(skeleton_path) == ".skel";
  if (binary) {
    if (const auto bytes = nx::vfs::read(skeleton_path)) {
      ::spine::SkeletonBinary reader(*loaded->atlas);
      data = reader.readSkeletonData(
          reinterpret_cast<const unsigned char *>(bytes->data()),
          nx::cast<int>(bytes->size()));
      if (data == nullptr)
        error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
    } else {
      error = nx::format("no skeleton at '{}'", skeleton_path);
    }
  } else if (const auto text = nx::vfs::read_text(skeleton_path)) {
    ::spine::SkeletonJson reader(*loaded->atlas);
    data = reader.readSkeletonData(text->c_str());
    if (data == nullptr)
      error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
  } else {
    error = nx::format("no skeleton at '{}'", skeleton_path);
  }

  if (data == nullptr) {
    return false;
  }

  loaded->data.reset(data);
  loaded->mixes.reset(new ::spine::AnimationStateData(*data));
  loaded->premultiplied =
      static_cast<ResolvingLoader *>(loaded->loader.get())->premultiplied();
  out.m_version = std::move(loaded);
  nx::logi("spine: '{}' - {} bones, {} slots, {} animations{}", skeleton_path,
           out.bone_count(), out.slot_count(), out.animation_count(),
           out.premultiplied() ? ", premultiplied" : "");
  return true;
}

}
