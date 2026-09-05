#include "spine/spine_assets.h"

#include "spine/spine_asset_bundle.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/platform/filesystem.h"
#include "core/foundation/serialization/asset_policy.h"
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
    bool known = false;
    for (const nx::string &held : m_pages)
      known |= held == where;
    if (!known)
      m_pages.push_back(nx::string(where));
    const u32 packed = m_resolve ? m_resolve(where, page.pma)
                                 : pack_texture(NX_TEXTURE_NONE, 0);
    page.texture = reinterpret_cast<void *>(static_cast<uintptr_t>(packed));
    if ((packed >> 16) == NX_TEXTURE_NONE)
      nx::logw("spine: no texture for '{}'; its slots will draw untextured",
               where);
  }

  void unload(void *) override {}

  [[nodiscard]] bool premultiplied() const noexcept { return m_premultiplied; }
  [[nodiscard]] const nx::vector<nx::string> &pages() const noexcept {
    return m_pages;
  }

private:
  TextureResolver m_resolve;
  bool m_premultiplied = false;
  nx::vector<nx::string> m_pages;
};

[[nodiscard]] nx::string_view to_view(const ::spine::String &text) noexcept {
  return {text.buffer(), nx::cast<usize>(text.length())};
}

[[nodiscard]] bool ends_with(const nx::string_view value,
                             const nx::string_view suffix) noexcept {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

[[nodiscard]] nx::string beside(const nx::string_view descriptor,
                                const nx::string_view relative) {
  const nx::string_view dir = nx::fs::path::parent_path(descriptor);
  return dir.empty() || dir == "/" ? nx::format("/{}", relative)
                                   : nx::format("{}/{}", dir, relative);
}

} // namespace

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
  nx::vector<nx::string> dependencies;
};

} // namespace detail

namespace {

[[nodiscard]] bool build_skeleton(
    const nx::string_view skeleton_path, const std::span<const u8> skeleton,
    const nx::string_view atlas_path, const std::span<const u8> atlas,
    TextureResolver resolve,
    nx::shared_ptr<detail::SkeletonAssetData> &loaded_out, nx::string &error) {
  if (skeleton.empty() || atlas.empty())
    return false;

  const nx::string dir(nx::fs::path::parent_path(atlas_path));
  nx::shared_ptr<detail::SkeletonAssetData> loaded =
      nx::make_shared<detail::SkeletonAssetData>();
  if (loaded == nullptr) {
    error = "out of memory while loading skeleton";
    return false;
  }

  loaded->loader.reset(new ResolvingLoader(std::move(resolve)));
  loaded->atlas.reset(new ::spine::Atlas(
      reinterpret_cast<const char *>(atlas.data()), nx::cast<int>(atlas.size()),
      dir.c_str(), loaded->loader.get()));
  if (loaded->atlas->getPages().size() == 0) {
    error = nx::format("'{}' names no pages", atlas_path);
    return false;
  }

  ::spine::SkeletonData *data = nullptr;
  if (ends_with(skeleton_path, ".skel")) {
    ::spine::SkeletonBinary reader(*loaded->atlas);
    data = reader.readSkeletonData(
        reinterpret_cast<const unsigned char *>(skeleton.data()),
        nx::cast<int>(skeleton.size()));
    if (data == nullptr)
      error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
  } else {
    const nx::string text(nx::string_view(
        reinterpret_cast<const char *>(skeleton.data()), skeleton.size()));
    ::spine::SkeletonJson reader(*loaded->atlas);
    data = reader.readSkeletonData(text.c_str());
    if (data == nullptr)
      error = nx::format("{}: {}", skeleton_path, to_view(reader.getError()));
  }
  if (data == nullptr)
    return false;

  loaded->data.reset(data);
  loaded->mixes.reset(new ::spine::AnimationStateData(*data));
  loaded->premultiplied =
      static_cast<ResolvingLoader *>(loaded->loader.get())->premultiplied();
  loaded->dependencies =
      static_cast<ResolvingLoader *>(loaded->loader.get())->pages();
  loaded_out = std::move(loaded);
  return true;
}

void log_loaded(const nx::string_view path, const SkeletonAsset &out) {
  nx::logd("spine: '{}' - {} bones, {} slots, {} animations{}", path,
           out.bone_count(), out.slot_count(), out.animation_count(),
           out.premultiplied() ? ", premultiplied" : "");
}

} // namespace

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

std::span<const nx::string> SkeletonAsset::dependencies() const noexcept {
  if (m_version == nullptr)
    return {};
  return {m_version->dependencies.data(), m_version->dependencies.size()};
}

bool load_skeleton(const nx::string_view skeleton_path,
                   const nx::string_view atlas_path, TextureResolver resolve,
                   SkeletonAsset &out, nx::string &error) {
  install_platform();
  out.m_version.reset();
  error.clear();
#if defined(NX_BUILD_SHIPPING)
  (void)skeleton_path;
  (void)atlas_path;
  (void)resolve;
  error = "Shipping requires an atomic .nxspine.nxb asset";
  return false;
#else
  const auto skeleton = nx::vfs::read(skeleton_path);
  const auto atlas = nx::vfs::read(atlas_path);
  if (!skeleton || skeleton->empty() ||
      skeleton->size() > MAX_SPINE_RESOURCE_BYTES) {
    error = nx::format("no bounded skeleton at '{}'", skeleton_path);
    return false;
  }
  if (!atlas || atlas->empty() || atlas->size() > MAX_SPINE_RESOURCE_BYTES) {
    error = nx::format("no bounded atlas at '{}'", atlas_path);
    return false;
  }
  nx::shared_ptr<detail::SkeletonAssetData> loaded;
  if (!build_skeleton(skeleton_path, {skeleton->data(), skeleton->size()},
                      atlas_path, {atlas->data(), atlas->size()},
                      std::move(resolve), loaded, error))
    return false;
  loaded->dependencies.push_back(nx::string(skeleton_path));
  loaded->dependencies.push_back(nx::string(atlas_path));
  out.m_version = std::move(loaded);
  log_loaded(skeleton_path, out);
  return true;
#endif
}

bool load_skeleton(const nx::string_view asset_path, TextureResolver resolve,
                   SkeletonAsset &out, nx::string &error) {
  install_platform();
  out.m_version.reset();
  error.clear();
  try {
    const bool explicit_cooked = ends_with(asset_path, ".nxb");
    const nx::string descriptor_path =
        explicit_cooked
            ? nx::string(asset_path.substr(0, asset_path.size() - 4))
            : nx::string(asset_path);
    nx::string cooked_path(asset_path);
    if (!explicit_cooked)
      cooked_path += ".nxb";
    const nx::vfs::FileInfo cooked_info = nx::vfs::stat(cooked_path.view());
    if (cooked_info.exists) {
      if (cooked_info.is_directory ||
          cooked_info.size > MAX_SPINE_BUNDLE_BYTES) {
        error = nx::format("cooked Spine asset '{}' exceeds its size limit",
                           cooked_path);
        return false;
      }
      const auto bytes = nx::vfs::read(cooked_path.view());
      const auto bundle =
          bytes ? open_spine_bundle({bytes->data(), bytes->size()})
                : std::nullopt;
      if (!bundle) {
        error = nx::format("cooked Spine asset '{}' is malformed", cooked_path);
        return false;
      }
      const nx::string skeleton_path =
          beside(descriptor_path.view(), bundle->descriptor.skeleton.view());
      const nx::string atlas_path =
          beside(descriptor_path.view(), bundle->descriptor.atlas.view());
      nx::shared_ptr<detail::SkeletonAssetData> loaded;
      if (!build_skeleton(skeleton_path.view(), bundle->skeleton(),
                          atlas_path.view(), bundle->atlas(),
                          std::move(resolve), loaded, error))
        return false;
      loaded->dependencies.push_back(cooked_path);
      out.m_version = std::move(loaded);
      log_loaded(descriptor_path.view(), out);
      return true;
    }
    if (explicit_cooked || !nx::asset_policy::can_fallback_to_authored_source(
                               cooked_info.exists)) {
      error = nx::format("no cooked Spine asset at '{}'", cooked_path);
      return false;
    }

    const auto text = nx::vfs::read_text(descriptor_path.view());
    if (!text || text->size() > MAX_SPINE_DESCRIPTOR_BYTES) {
      error =
          nx::format("no bounded Spine descriptor at '{}'", descriptor_path);
      return false;
    }
    SpineDescriptor descriptor;
    if (!parse_spine_descriptor(text->view(), descriptor, error)) {
      error = nx::format("{}: {}", descriptor_path, error);
      return false;
    }
    const nx::string skeleton_path =
        beside(descriptor_path.view(), descriptor.skeleton.view());
    const nx::string atlas_path =
        beside(descriptor_path.view(), descriptor.atlas.view());
    const auto skeleton = nx::vfs::read(skeleton_path.view());
    const auto atlas = nx::vfs::read(atlas_path.view());
    if (!skeleton || skeleton->empty() ||
        skeleton->size() > MAX_SPINE_RESOURCE_BYTES) {
      error = nx::format("no bounded skeleton at '{}'", skeleton_path);
      return false;
    }
    if (!atlas || atlas->empty() || atlas->size() > MAX_SPINE_RESOURCE_BYTES) {
      error = nx::format("no bounded atlas at '{}'", atlas_path);
      return false;
    }
    nx::shared_ptr<detail::SkeletonAssetData> loaded;
    if (!build_skeleton(skeleton_path.view(),
                        {skeleton->data(), skeleton->size()}, atlas_path.view(),
                        {atlas->data(), atlas->size()}, std::move(resolve),
                        loaded, error))
      return false;
    loaded->dependencies.push_back(descriptor_path);
    loaded->dependencies.push_back(cooked_path);
    loaded->dependencies.push_back(skeleton_path);
    loaded->dependencies.push_back(atlas_path);
    out.m_version = std::move(loaded);
    log_loaded(descriptor_path.view(), out);
    return true;
  } catch (...) {
    error = nx::format("resource exhaustion while loading Spine asset '{}'",
                       asset_path);
    return false;
  }
}

} // namespace nxe::spine2d
