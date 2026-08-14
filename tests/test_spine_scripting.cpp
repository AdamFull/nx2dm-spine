/**
 * @file test_spine_scripting.cpp
 * @brief What the module hands a script, and the shape of it.
 *
 * The build writes these signatures into the declarations a type checker reads,
 * so a renamed service or a moved argument is not a compile error anywhere - it
 * is a script that stops type-checking, or worse, one that still does and calls
 * the wrong thing. This is where that gets caught.
 *
 * No backend and no started Engine: Host::expose records, and only bind() needs
 * a VM. The generated manifest is checked against the same surface at startup.
 */

#include "framework/nxtest.h"

#include "core/app/engine.h"
#include "core/script/script_host.h"
#include "spine/spine_scripting.h"

namespace {

using namespace nxe::spine2d;
namespace script = nxe::script;

struct Exposed {
  nxe::Engine engine{nxe::Game{}};
  nxe::ModuleContext ctx{engine};
  script::Host host;
  nx::vector<script::Host::ServiceInfo> services;

  Exposed() {
    expose_spine_services(host, ctx);
    services = host.services();
  }

  [[nodiscard]] const script::Host::ServiceInfo *
  find(const nx::string_view name) const {
    for (const script::Host::ServiceInfo &one : services)
      if (one.name == name)
        return &one;
    return nullptr;
  }
};

} // namespace

TEST_CASE("spine scripting: every service is exposed with the shape a script "
          "is told about") {
  const Exposed exposed;

  // Spelled out rather than counted: the whole point is that a script's
  // declaration and this list cannot drift, and a count would let a rename
  // through.
  static constexpr struct {
    nx::string_view name;
    nx::string_view signature;
  } WANT[] = {
      {"spine_play", "(number,string,boolean,number)->(boolean)"},
      {"spine_queue", "(number,string,boolean,number,number)->(boolean)"},
      {"spine_stop", "(number,number)->(boolean)"},
      {"spine_skin", "(number,string)->(boolean)"},
      {"spine_finished", "(number,number)->(boolean)"},
      {"spine_visible", "(number,boolean)->(boolean)"},
      {"spine_speed", "(number,number)->(boolean)"},
  };

  CHECK(exposed.services.size() == nx::array_size(WANT));
  for (const auto &want : WANT) {
    const script::Host::ServiceInfo *const found = exposed.find(want.name);
    REQUIRE(found != nullptr);
    CHECK(found->signature == want.signature);
  }
}

TEST_CASE("spine scripting: the module hands them over on its own") {
  // Through Module::on_expose_scripts and not the free function, because that
  // hook being wired is the whole of what makes a script reach a skeleton
  // without a game asking. Nothing else would fail if the override were
  // dropped: the module still builds, still attaches, still draws, and the
  // services simply are not there.
  std::unique_ptr<nxe::Module> found;
  for (const nxe::ModuleFactory factory : nxe::enabled_module_factories()) {
    std::unique_ptr<nxe::Module> module = factory();
    if (module != nullptr && module->name() == "spine")
      found = std::move(module);
  }
  REQUIRE(found != nullptr);

  nxe::Engine engine{nxe::Game{}};
  nxe::ModuleContext ctx{engine};
  script::Host host;
  found->on_expose_scripts(host, ctx);

  const Exposed direct;
  CHECK(host.exposed_count() == direct.services.size());
  CHECK(host.exposed_count() > 0u);
}
