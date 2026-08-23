#pragma once

#include "spine/spine_system.h"

namespace nxe {
class Engine;
}

namespace nxe::spine2d {

inline constexpr nx::string_view SERVICE = "spine.animation";

[[nodiscard]] SpineSystem *system(Engine &engine) noexcept;

}
