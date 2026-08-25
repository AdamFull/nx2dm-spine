#include "spine/spine_asset_bundle.h"

#include "core/foundation/serialization/json.h"

namespace nxe::spine2d {
namespace {

[[nodiscard]] bool fail(nx::string &error, const nx::string_view message) {
  error = nx::string(message);
  return false;
}

[[nodiscard]] bool ends_with(const nx::string_view value,
                             const nx::string_view suffix) noexcept {
  return value.size() >= suffix.size() &&
         value.substr(value.size() - suffix.size()) == suffix;
}

[[nodiscard]] bool
valid_descriptor(const SpineDescriptor &descriptor) noexcept {
  return nx::resource_bundle::valid_name(descriptor.skeleton.view()) &&
         (ends_with(descriptor.skeleton.view(), ".skel") ||
          ends_with(descriptor.skeleton.view(), ".spine.json")) &&
         nx::resource_bundle::valid_name(descriptor.atlas.view()) &&
         ends_with(descriptor.atlas.view(), ".atlas") &&
         descriptor.skeleton != descriptor.atlas;
}

[[nodiscard]] nx::resource_bundle::Limits limits() noexcept {
  return {.max_entries = 2,
          .max_name_bytes = 4096,
          .max_metadata_bytes = MAX_SPINE_DESCRIPTOR_BYTES,
          .max_data_bytes = MAX_SPINE_RESOURCE_BYTES * 2u};
}

} // namespace

bool parse_spine_descriptor(const nx::string_view text, SpineDescriptor &out,
                            nx::string &error) {
  error.clear();
  try {
    const nx::json::parse_options options{.reject_duplicate_keys = true,
                                          .validate_utf8 = true};
    auto parsed = nx::json::parse(text, options);
    if (!parsed || !parsed.value().is_object())
      return fail(error, "the Spine descriptor is not a JSON object");
    const nx::json::value &root = parsed.value();
    for (const nx::json::member &field : root.as_object())
      if (field.key != "version" && field.key != "skeleton" &&
          field.key != "atlas")
        return fail(error, "the Spine descriptor contains an unknown field");

    const nx::json::value *const version = root.find("version");
    const bool version_one =
        version != nullptr &&
        ((version->is_u64() && version->as_u64() == SPINE_BUNDLE_VERSION) ||
         (version->is_i64() && version->as_i64() == SPINE_BUNDLE_VERSION));
    if (!version_one)
      return fail(error, "the Spine descriptor version must be 1");
    const nx::json::value *const skeleton = root.find("skeleton");
    const nx::json::value *const atlas = root.find("atlas");
    if (skeleton == nullptr || !skeleton->is_string() || atlas == nullptr ||
        !atlas->is_string())
      return fail(error, "the Spine descriptor must name skeleton and atlas");

    SpineDescriptor decoded{nx::string(skeleton->as_string()),
                            nx::string(atlas->as_string())};
    if (!nx::resource_bundle::valid_name(decoded.skeleton.view()) ||
        (!ends_with(decoded.skeleton.view(), ".skel") &&
         !ends_with(decoded.skeleton.view(), ".spine.json")))
      return fail(error,
                  "skeleton must be a safe relative .skel or .spine.json path");
    if (!nx::resource_bundle::valid_name(decoded.atlas.view()) ||
        !ends_with(decoded.atlas.view(), ".atlas"))
      return fail(error, "atlas must be a safe relative .atlas path");
    if (decoded.skeleton == decoded.atlas)
      return fail(error, "skeleton and atlas must be different resources");
    out = std::move(decoded);
    return true;
  } catch (...) {
    return fail(error, "resource exhaustion while parsing Spine descriptor");
  }
}

bool parse_spine_atlas_pages(const nx::string_view text,
                             nx::vector<nx::string> &out, nx::string &error) {
  error.clear();
  try {
    if (!nx::utf8::validate_utf8(text.data(), text.size()))
      return fail(error, "the Spine atlas is not well-formed UTF-8");
    nx::vector<nx::string> pages;
    bool expect_page = true;
    usize cursor = 0;
    while (cursor < text.size()) {
      const usize newline = text.find('\n', cursor);
      const usize end =
          newline == nx::string_view::npos ? text.size() : newline;
      nx::string_view line = text.substr(cursor, end - cursor);
      if (!line.empty() && line.back() == '\r')
        line.remove_suffix(1);
      cursor = newline == nx::string_view::npos ? text.size() : newline + 1u;

      bool blank = true;
      for (const char c : line)
        if (c != ' ' && c != '\t') {
          blank = false;
          break;
        }
      if (blank) {
        expect_page = true;
        continue;
      }
      if (!expect_page)
        continue;
      if (!nx::resource_bundle::valid_name(line) ||
          (!ends_with(line, ".png") && !ends_with(line, ".hdr")))
        return fail(error,
                    "an atlas page must be a safe relative .png or .hdr path");
      if (pages.size() >= MAX_SPINE_ATLAS_PAGES)
        return fail(error, "the Spine atlas names too many pages");
      pages.push_back(nx::string(line));
      expect_page = false;
    }
    if (pages.empty())
      return fail(error, "the Spine atlas names no pages");
    out = std::move(pages);
    return true;
  } catch (...) {
    return fail(error, "resource exhaustion while parsing Spine atlas pages");
  }
}

std::optional<nx::vector<u8>>
encode_spine_bundle(const SpineDescriptor &descriptor,
                    const std::span<const u8> skeleton,
                    const std::span<const u8> atlas) {
  if (!valid_descriptor(descriptor) || skeleton.empty() || atlas.empty() ||
      skeleton.size() > MAX_SPINE_RESOURCE_BYTES ||
      atlas.size() > MAX_SPINE_RESOURCE_BYTES)
    return std::nullopt;
  nx::nva::Writer metadata;
  metadata.str(descriptor.skeleton.view());
  metadata.str(descriptor.atlas.view());
  const nx::resource_bundle::Source sources[] = {
      {descriptor.skeleton.view(), skeleton}, {descriptor.atlas.view(), atlas}};
  return nx::resource_bundle::encode(SPINE_BUNDLE_FORMAT, SPINE_BUNDLE_VERSION,
                                     metadata.span(), sources, limits());
}

std::optional<SpineBundleView>
open_spine_bundle(const std::span<const u8> bytes) {
  auto resources = nx::resource_bundle::View::open(
      bytes, SPINE_BUNDLE_FORMAT, SPINE_BUNDLE_VERSION, limits());
  if (!resources)
    return std::nullopt;
  nx::nva::Reader metadata(resources->metadata());
  SpineDescriptor descriptor{nx::string(metadata.str()),
                             nx::string(metadata.str())};
  if (!metadata.ok() || metadata.remaining() != 0 ||
      !valid_descriptor(descriptor) || resources->size() != 2u ||
      resources->find(descriptor.skeleton.view()).empty() ||
      resources->find(descriptor.atlas.view()).empty())
    return std::nullopt;
  return SpineBundleView{std::move(descriptor), std::move(resources.value())};
}

} // namespace nxe::spine2d
