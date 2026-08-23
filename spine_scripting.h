#pragma once

namespace nxe {
class ModuleContext;
namespace script {
class Host;
}
}

namespace nxe::spine2d {

void expose_spine_services(script::Host &host, ModuleContext &ctx);

}
