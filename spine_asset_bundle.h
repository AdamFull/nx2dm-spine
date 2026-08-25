#pragma once

#include "core/foundation/serialization/nva.h"
#include "core/foundation/serialization/resource_bundle.h"
#include "core/foundation/strings/utf8_string.h"

#include <optional>
#include <span>

namespace nxe::spine2d {

inline constexpr u32 SPINE_BUNDLE_FORMAT = nx::nva::fourcc('N', 'X', 'S', 'P');
inline constexpr u16 SPINE_BUNDLE_VERSION = 1;
inline constexpr usize MAX_SPINE_DESCRIPTOR_BYTES = 64u * 1024u;
inline constexpr usize MAX_SPINE_RESOURCE_BYTES = 128u * 1024u * 1024u;
inline constexpr usize MAX_SPINE_BUNDLE_BYTES =
    MAX_SPINE_RESOURCE_BYTES * 2u + 128u * 1024u;
inline constexpr u32 MAX_SPINE_ATLAS_PAGES = 4096;

struct SpineDescriptor {
  nx::string skeleton;
  nx::string atlas;
};

[[nodiscard]] bool parse_spine_descriptor(nx::string_view text,
                                          SpineDescriptor &out,
                                          nx::string &error);

/// Extracts the page images from a Spine text atlas. Page references remain
/// separate texture assets, but cooking validates that every name is portable
/// and belongs to the engine texture pipeline.
[[nodiscard]] bool parse_spine_atlas_pages(nx::string_view text,
                                           nx::vector<nx::string> &out,
                                           nx::string &error);

[[nodiscard]] std::optional<nx::vector<u8>>
encode_spine_bundle(const SpineDescriptor &descriptor,
                    std::span<const u8> skeleton, std::span<const u8> atlas);

struct SpineBundleView {
  SpineDescriptor descriptor;
  nx::resource_bundle::View resources;

  [[nodiscard]] std::span<const u8> skeleton() const noexcept {
    return resources.find(descriptor.skeleton.view());
  }
  [[nodiscard]] std::span<const u8> atlas() const noexcept {
    return resources.find(descriptor.atlas.view());
  }
};

[[nodiscard]] std::optional<SpineBundleView>
open_spine_bundle(std::span<const u8> bytes);

} // namespace nxe::spine2d
