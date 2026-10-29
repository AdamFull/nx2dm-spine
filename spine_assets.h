#pragma once

#include "core/foundation/containers/blob.h"
#include "core/foundation/core/callable.h"
#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"
#include "core/foundation/vfs/vfs.h"
#include "rendering/interop/nx_interop.h"

#include <optional>
#include <span>

namespace spine {
class Atlas;
class SkeletonData;
class AnimationStateData;
} // namespace spine

namespace nxe::spine2d {

using TextureResolver =
    nx::function<NxTexture2D<float4>(nx::string_view path, bool premultiplied)>;

namespace detail {
class SkeletonAssetData;
}

struct SpineSource;

/// A cheap, copyable handle to one stable loaded skeleton version. Runtime
/// poses keep their own handle, so replacing or destroying the caller's handle
/// cannot invalidate Spine objects that still refer to that version's data.
class SkeletonAsset {
public:
  SkeletonAsset() = default;
  ~SkeletonAsset() = default;

  SkeletonAsset(const SkeletonAsset &) noexcept = default;
  SkeletonAsset &operator=(const SkeletonAsset &) noexcept = default;
  SkeletonAsset(SkeletonAsset &&) noexcept = default;
  SkeletonAsset &operator=(SkeletonAsset &&) noexcept = default;

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] ::spine::SkeletonData *data() const noexcept;
  [[nodiscard]] ::spine::Atlas *atlas() const noexcept;
  [[nodiscard]] ::spine::AnimationStateData *mixes() const noexcept;

  [[nodiscard]] bool premultiplied() const noexcept;

  [[nodiscard]] bool same_version(const SkeletonAsset &other) const noexcept {
    return m_version == other.m_version;
  }

  [[nodiscard]] usize bone_count() const noexcept;
  [[nodiscard]] usize slot_count() const noexcept;
  [[nodiscard]] usize animation_count() const noexcept;
  [[nodiscard]] usize skin_count() const noexcept;
  [[nodiscard]] bool has_animation(nx::string_view name) const noexcept;
  [[nodiscard]] std::span<const nx::string> dependencies() const noexcept;

private:
  friend bool load_skeleton(nx::string_view, nx::string_view, TextureResolver,
                            SkeletonAsset &, nx::string &);
  friend bool load_skeleton(nx::string_view, TextureResolver, SkeletonAsset &,
                            nx::string &);
  friend bool build_gathered_skeleton(const SpineSource &, TextureResolver,
                                      SkeletonAsset &, nx::string &);

  nx::shared_ptr<const detail::SkeletonAssetData> m_version;
};

[[nodiscard]] bool load_skeleton(nx::string_view skeleton_path,
                                 nx::string_view atlas_path,
                                 TextureResolver resolve, SkeletonAsset &out,
                                 nx::string &error);

/// Resolves the atlas pages again, for textures that were still loading when
/// the skeleton was read. Returns how many pages changed.
usize refresh_textures(const SkeletonAsset &asset,
                       const TextureResolver &resolve);

/// Appends the texture path of each of the skeleton's atlas pages. The views
/// last as long as the asset's version does.
void texture_paths(const SkeletonAsset &asset,
                   nx::vector<nx::string_view> &out);

/// Loads one authored .nxspine descriptor in development and its atomic
/// .nxspine.nxb bundle in Shipping. Relative atlas page paths remain relative
/// to the descriptor's atlas, so existing texture resolvers need no changes.
/// It is gather_skeleton through the VFS, then build_gathered_skeleton.
[[nodiscard]] bool load_skeleton(nx::string_view asset_path,
                                 TextureResolver resolve, SkeletonAsset &out,
                                 nx::string &error);

/// Where gather_skeleton reads from: the VFS itself, or an async I/O context
/// on the VFS service.
class SpineReader {
public:
  virtual ~SpineReader() = default;
  [[nodiscard]] virtual nx::vfs::FileInfo stat(nx::string_view path) = 0;
  /// Null when it is missing or larger than @p max_bytes.
  [[nodiscard]] virtual std::optional<nx::blob<u8>> read(nx::string_view path,
                                                         u64 max_bytes) = 0;
};

class VfsSpineReader final : public SpineReader {
public:
  [[nodiscard]] nx::vfs::FileInfo stat(nx::string_view path) override;
  [[nodiscard]] std::optional<nx::blob<u8>> read(nx::string_view path,
                                                 u64 max_bytes) override;
};

/// Every byte one asset is built from, read before any of it is parsed.
struct SpineSource {
  /// The asset as asked for, for logs.
  nx::string label;
  nx::string skeleton_path;
  nx::string atlas_path;
  nx::blob<u8> skeleton;
  nx::blob<u8> atlas;
  nx::vector<nx::string> dependencies;
};

/// The reading half of load_skeleton: no parsing, so an I/O job can run it.
[[nodiscard]] bool gather_skeleton(nx::string_view asset_path,
                                   SpineReader &reader, SpineSource &out,
                                   nx::string &error);
/// The building half, for a worker: an empty @p resolve leaves the atlas pages
/// unbound until refresh_textures binds them on the owner thread.
[[nodiscard]] bool build_gathered_skeleton(const SpineSource &source,
                                           TextureResolver resolve,
                                           SkeletonAsset &out,
                                           nx::string &error);

} // namespace nxe::spine2d
