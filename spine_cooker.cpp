#include "assetc/cooker_registry.h"

#include "spine/spine_asset_bundle.h"

#include "core/foundation/platform/filesystem.h"
#include "core/foundation/serialization/json_document.h"

#include <cstdio>
#include <cstring>

namespace assetc {
namespace {

inline constexpr nx::asset_contract::Format SPINE_FORMATS[] = {
    {".nxspine", ".nxspine.nxb", nxe::spine2d::SPINE_BUNDLE_FORMAT, true},
};
inline constexpr nx::string_view SPINE_EMBEDDED[] = {".skel", ".spine.json",
                                                     ".atlas"};

struct Inputs {
  nxe::spine2d::SpineDescriptor descriptor;
  nx::blob<u8> skeleton;
  nx::blob<u8> atlas;
};

[[nodiscard]] bool read_inputs(const nx::string_view source, Inputs &out,
                               nx::string &error) {
  const auto text = nx::fs::file_read_text(source);
  if (!text || text->size() > nxe::spine2d::MAX_SPINE_DESCRIPTOR_BYTES) {
    error = "descriptor is missing or exceeds its size limit";
    return false;
  }
  Inputs loaded;
  const auto descriptor_json =
      nx::json::normalize_asset_document(text->view());
  if (!descriptor_json ||
      !nxe::spine2d::parse_spine_descriptor(
          descriptor_json->view(), loaded.descriptor, error))
    return false;
  const nx::string_view parent = nx::fs::path::parent_path(source);
  const nx::string skeleton =
      nx::fs::path_view(parent) / loaded.descriptor.skeleton.view();
  const nx::string atlas =
      nx::fs::path_view(parent) / loaded.descriptor.atlas.view();
  auto skeleton_bytes = nx::fs::file_read(skeleton.view());
  auto atlas_bytes = nx::fs::file_read(atlas.view());
  if (!skeleton_bytes || skeleton_bytes->empty() ||
      skeleton_bytes->size() > nxe::spine2d::MAX_SPINE_RESOURCE_BYTES) {
    error = nx::string("cannot read bounded skeleton '") + skeleton + "'";
    return false;
  }
  if (!atlas_bytes || atlas_bytes->empty() ||
      atlas_bytes->size() > nxe::spine2d::MAX_SPINE_RESOURCE_BYTES) {
    error = nx::string("cannot read bounded atlas '") + atlas + "'";
    return false;
  }
  if (loaded.descriptor.skeleton.ends_with(".json")) {
    const nx::string_view skeleton_text(
        reinterpret_cast<const char *>(skeleton_bytes->data()),
        skeleton_bytes->size());
    const auto normalized = nx::json::normalize_asset_document(skeleton_text);
    if (!normalized) {
      error = nx::string("skeleton '") + skeleton + "' is not valid JSON";
      return false;
    }
    nx::blob<u8> normalized_bytes(normalized->size());
    std::memcpy(normalized_bytes.data(), normalized->data(),
                normalized->size());
    skeleton_bytes = std::move(normalized_bytes);
  }
  nx::vector<nx::string> pages;
  if (!nxe::spine2d::parse_spine_atlas_pages(
          {reinterpret_cast<const char *>(atlas_bytes->data()),
           atlas_bytes->size()},
          pages, error))
    return false;
  const nx::string_view atlas_parent = nx::fs::path::parent_path(atlas.view());
  for (const nx::string &page : pages) {
    const nx::string path = nx::fs::path_view(atlas_parent) / page.view();
    const nx::fs::file_stat texture = nx::fs::stat_file(path.view());
    if (texture.type != nx::fs::file_type::regular || texture.size == 0 ||
        texture.size > nxe::spine2d::MAX_SPINE_RESOURCE_BYTES) {
      error = nx::string("cannot find bounded atlas page '") + path + "'";
      return false;
    }
  }
  loaded.skeleton = std::move(skeleton_bytes.value());
  loaded.atlas = std::move(atlas_bytes.value());
  out = std::move(loaded);
  return true;
}

[[nodiscard]] bool cook_spine(const CookContext &context) {
  Inputs inputs;
  nx::string error;
  if (!read_inputs(context.source, inputs, error)) {
    std::fprintf(stderr, "assetc: Spine '%.*s' is invalid: %.*s\n",
                 static_cast<int>(context.source.size()), context.source.data(),
                 static_cast<int>(error.size()), error.data());
    return false;
  }
  const auto cooked = nxe::spine2d::encode_spine_bundle(
      inputs.descriptor, {inputs.skeleton.data(), inputs.skeleton.size()},
      {inputs.atlas.data(), inputs.atlas.size()});
  return cooked && static_cast<bool>(nx::fs::file_write_atomic(
                       context.output, {cooked->data(), cooked->size()}));
}

void hash_spine_dependencies(nx::fnv1a64 &hash, const CookContext &context) {
  Inputs inputs;
  nx::string error;
  if (!read_inputs(context.source, inputs, error)) {
    hash.combine_bytes(error.data(), error.size());
    return;
  }
  hash.combine_bytes(inputs.descriptor.skeleton.data(),
                     inputs.descriptor.skeleton.size());
  hash.combine_bytes(reinterpret_cast<const char *>(inputs.skeleton.data()),
                     inputs.skeleton.size());
  hash.combine_bytes(inputs.descriptor.atlas.data(),
                     inputs.descriptor.atlas.size());
  hash.combine_bytes(reinterpret_cast<const char *>(inputs.atlas.data()),
                     inputs.atlas.size());
}

} // namespace

bool nx_assetc_register_spine(CookerRegistry &registry, nx::string &error) {
  return registry.add({.name = "spine",
                       .formats = SPINE_FORMATS,
                       .version = nxe::spine2d::SPINE_BUNDLE_VERSION,
                       .cook = cook_spine,
                       .contribute_hash = hash_spine_dependencies,
                       .embedded_sources = SPINE_EMBEDDED},
                      error);
}

} // namespace assetc
