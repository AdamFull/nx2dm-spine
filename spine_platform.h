#pragma once

/**
 * @file spine_platform.h
 * @brief What spine-cpp asks of the host (namespace nxe::spine2d).
 */

#include "core/foundation/core/foundation.h"

namespace nxe::spine2d {

void install_platform();

[[nodiscard]] u64 bytes_read() noexcept;

} // namespace nxe::spine2d
