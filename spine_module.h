#pragma once

/**
 * @file spine_module.h
 * @brief Reaching the Spine module instance's system from a game (namespace
 * nxe::spine2d).
 */

#include "spine/spine_system.h"

namespace nxe {
class Engine;
}

namespace nxe::spine2d {

inline constexpr nx::string_view SERVICE = "spine.animation";

[[nodiscard]] SpineSystem *system(Engine &engine) noexcept;

} // namespace nxe::spine2d
