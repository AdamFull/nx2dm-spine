#pragma once

/**
 * @file spine_assets.h
 * @brief A skeleton's shared data, loaded (namespace nxe::spine2d).
 */

#include "core/foundation/core/callable.h"
#include "core/foundation/strings/utf8_string.h"

namespace spine {
class Atlas;
class SkeletonData;
class AnimationStateData;
class TextureLoader;
} // namespace spine

namespace nxe::spine2d {

using TextureResolver =
    nx::function<u32(nx::string_view path, bool premultiplied)>;

class SkeletonAsset {
public:
  SkeletonAsset() = default;
  ~SkeletonAsset();

  SkeletonAsset(const SkeletonAsset &) = delete;
  SkeletonAsset &operator=(const SkeletonAsset &) = delete;
  SkeletonAsset(SkeletonAsset &&other) noexcept;
  SkeletonAsset &operator=(SkeletonAsset &&other) noexcept;

  [[nodiscard]] bool valid() const noexcept { return m_data != nullptr; }

  [[nodiscard]] ::spine::SkeletonData *data() const noexcept { return m_data; }
  [[nodiscard]] ::spine::Atlas *atlas() const noexcept { return m_atlas; }
  [[nodiscard]] ::spine::AnimationStateData *mixes() const noexcept {
    return m_mixes;
  }

  [[nodiscard]] bool premultiplied() const noexcept { return m_premultiplied; }

  [[nodiscard]] usize bone_count() const noexcept;
  [[nodiscard]] usize slot_count() const noexcept;
  [[nodiscard]] usize animation_count() const noexcept;
  [[nodiscard]] usize skin_count() const noexcept;
  [[nodiscard]] bool has_animation(nx::string_view name) const noexcept;

private:
  friend bool load_skeleton(nx::string_view, nx::string_view, TextureResolver,
                            SkeletonAsset &, nx::string &);

  void reset() noexcept;

  ::spine::TextureLoader *m_loader = nullptr;
  ::spine::Atlas *m_atlas = nullptr;
  ::spine::SkeletonData *m_data = nullptr;
  ::spine::AnimationStateData *m_mixes = nullptr;
  bool m_premultiplied = false;
};

[[nodiscard]] bool load_skeleton(nx::string_view skeleton_path,
                                 nx::string_view atlas_path,
                                 TextureResolver resolve, SkeletonAsset &out,
                                 nx::string &error);

} // namespace nxe::spine2d
