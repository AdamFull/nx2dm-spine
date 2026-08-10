#pragma once

/**
 * @file spine_scripting.h
 * @brief What a script may do to a skeleton (namespace nxe::spine2d).
 */

namespace nxe {
class Engine;
namespace script {
class Host;
}
} // namespace nxe

namespace nxe::spine2d {

void expose_spine_services(script::Host &host, Engine &engine);

} // namespace nxe::spine2d
