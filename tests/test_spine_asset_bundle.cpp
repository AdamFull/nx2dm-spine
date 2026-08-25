#include "framework/nxtest.h"

#include "spine/spine_asset_bundle.h"

namespace {

[[nodiscard]] std::span<const u8> bytes(const nx::string_view text) {
  return {reinterpret_cast<const u8 *>(text.data()), text.size()};
}

} // namespace

TEST_CASE("spine bundle: descriptor and two resources round-trip") {
  constexpr nx::string_view descriptor = R"({
    "version":1,
    "skeleton":"hero.skel",
    "atlas":"art/hero.atlas"
  })";
  nxe::spine2d::SpineDescriptor parsed;
  nx::string error;
  REQUIRE(nxe::spine2d::parse_spine_descriptor(descriptor, parsed, error));
  const auto encoded = nxe::spine2d::encode_spine_bundle(
      parsed, bytes("skeleton"), bytes("atlas"));
  REQUIRE(encoded);
  const auto opened =
      nxe::spine2d::open_spine_bundle({encoded->data(), encoded->size()});
  REQUIRE(opened);
  CHECK(opened->descriptor.skeleton == "hero.skel");
  CHECK(opened->descriptor.atlas == "art/hero.atlas");
  CHECK(opened->skeleton().size() == 8u);
  CHECK(opened->atlas().size() == 5u);
}

TEST_CASE("spine bundle: descriptor paths are bounded and portable") {
  nxe::spine2d::SpineDescriptor parsed;
  nx::string error;
  CHECK_FALSE(nxe::spine2d::parse_spine_descriptor(
      R"({"version":1,"skeleton":"../hero.skel","atlas":"hero.atlas"})", parsed,
      error));
  CHECK_FALSE(nxe::spine2d::parse_spine_descriptor(
      R"({"version":1,"skeleton":"hero.json","atlas":"hero.atlas"})", parsed,
      error));
  CHECK_FALSE(nxe::spine2d::parse_spine_descriptor(
      R"({"version":2,"skeleton":"hero.skel","atlas":"hero.atlas"})", parsed,
      error));
  CHECK_FALSE(nxe::spine2d::parse_spine_descriptor(
      R"({"version":1,"version":1,"skeleton":"hero.skel","atlas":"hero.atlas"})",
      parsed, error));
}

TEST_CASE("spine bundle: encoded metadata retains the authored contract") {
  const nxe::spine2d::SpineDescriptor invalid{"hero.bin", "hero.atlas"};
  CHECK_FALSE(nxe::spine2d::encode_spine_bundle(invalid, bytes("skeleton"),
                                                bytes("atlas")));

  nx::nva::Writer metadata;
  metadata.str("hero.bin");
  metadata.str("hero.atlas");
  const nx::resource_bundle::Source resources[] = {
      {"hero.bin", bytes("skeleton")}, {"hero.atlas", bytes("atlas")}};
  const auto encoded = nx::resource_bundle::encode(
      nxe::spine2d::SPINE_BUNDLE_FORMAT, nxe::spine2d::SPINE_BUNDLE_VERSION,
      metadata.span(), resources);
  REQUIRE(encoded);
  CHECK_FALSE(
      nxe::spine2d::open_spine_bundle({encoded->data(), encoded->size()}));
}

TEST_CASE("spine bundle: atlas pages are portable texture references") {
  constexpr nx::string_view atlas =
      "pages/first.png\r\n\tsize: 64, 64\r\nregion\r\n"
      "\tbounds: 0, 0, 32, 32\r\n\r\nsecond.hdr\n\tsize: 16, 16\n";
  nx::vector<nx::string> pages;
  nx::string error;
  REQUIRE(nxe::spine2d::parse_spine_atlas_pages(atlas, pages, error));
  REQUIRE(pages.size() == 2u);
  CHECK(pages[0] == "pages/first.png");
  CHECK(pages[1] == "second.hdr");
  CHECK_FALSE(nxe::spine2d::parse_spine_atlas_pages(
      "../escape.png\n\tsize: 1, 1\n", pages, error));
  const char invalid_utf8[] = {'p', 'a', 'g', 'e', static_cast<char>(0xff),
                               '.', 'p', 'n', 'g'};
  CHECK_FALSE(nxe::spine2d::parse_spine_atlas_pages(
      nx::string_view(invalid_utf8, sizeof(invalid_utf8)), pages, error));
}
