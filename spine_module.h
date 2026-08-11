#pragma once

/**
 * @file spine_module.h
 * @brief Reaching the Spine module instance's system from a game (namespace
 * nxe::spine2d).
 */

#include "spine/spine_system.h"

namespace nxe::spine2d {

/// The runtime owned by the enabled Spine module instance.
[[nodiscard]] SpineSystem &system();

} // namespace nxe::spine2d
