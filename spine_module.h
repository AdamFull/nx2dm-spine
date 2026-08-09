#pragma once

/**
 * @file spine_module.h
 * @brief Reaching the module's SpineSystem from a game (namespace
 * nxe::spine2d).
 */

#include "spine/spine_system.h"

namespace nxe::spine2d {

[[nodiscard]] SpineSystem &system();

} // namespace nxe::spine2d
