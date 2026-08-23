#pragma once

#include "core/foundation/core/callable.h"
#include "core/foundation/core/foundation.h"
#include "core/foundation/strings/utf8_string.h"

namespace spine {
class Atlas;
class SkeletonData;
class AnimationStateData;
}

namespace nxe::spine2d {

using TextureResolver =
    nx::function<u32(nx::string_view path, bool premultiplied)>;

namespace detail {
class SkeletonAssetData;
}

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

private:
  friend bool load_skeleton(nx::string_view, nx::string_view, TextureResolver,
                            SkeletonAsset &, nx::string &);

  nx::shared_ptr<const detail::SkeletonAssetData> m_version;
};

[[nodiscard]] bool load_skeleton(nx::string_view skeleton_path,
                                 nx::string_view atlas_path,
                                 TextureResolver resolve, SkeletonAsset &out,
                                 nx::string &error);

}
