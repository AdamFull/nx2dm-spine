#pragma once

/**
 * @file spine_platform.h
 * @brief What spine-cpp asks of the host (namespace nxe::spine2d).
 */

#include "core/foundation/core/foundation.h"

namespace nxe::spine2d {

/// Installs the Spine allocator/file bridge. SpineObject's class-specific
/// new/delete then route through nx::mem_alloc/nx::mem_free.
void install_platform();

[[nodiscard]] u64 bytes_read() noexcept;

} // namespace nxe::spine2d
