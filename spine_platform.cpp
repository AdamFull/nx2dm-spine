#include "spine/spine_platform.h"

#include "core/foundation/diagnostics/log.h"
#include "core/foundation/vfs/vfs.h"

#include <spine/Bone.h>
#include <spine/Extension.h>
#include <spine/SpineString.h>

#include <cstring>

#include <atomic>
#include <cstdlib>

namespace nxe::spine2d {
namespace {

std::atomic<u64> g_bytes_read{0};

class VfsExtension final : public ::spine::SpineExtension {
protected:
  void *_alloc(const size_t size, const char *, int) override {
    return size == 0 ? nullptr : nx::mem_alloc(size);
  }

  void *_calloc(const size_t size, const char *, int) override {
    if (size == 0)
      return nullptr;
    void *const memory = nx::mem_alloc(size);
    if (memory != nullptr)
      std::memset(memory, 0, size);
    return memory;
  }

  void *_realloc(void *const ptr, const size_t size, const char *,
                 int) override {
    if (size == 0) {
      nx::mem_free(ptr);
      return nullptr;
    }
    return nx::mem_realloc(ptr, size);
  }

  void _free(void *const mem, const char *, int) override { nx::mem_free(mem); }

  char *_readFile(const ::spine::String &path, int *const length) override {
    const nx::string_view where(path.buffer(), nx::cast<usize>(path.length()));
    const auto bytes = nx::vfs::read(where);
    if (!bytes) {
      nx::loge("spine: cannot read '{}'", where);
      if (length != nullptr)
        *length = 0;
      return nullptr;
    }

    const usize size = bytes->size();
    char *const out = static_cast<char *>(nx::mem_alloc(size == 0 ? 1 : size));
    if (out == nullptr) {
      if (length != nullptr)
        *length = 0;
      return nullptr;
    }
    if (size != 0)
      std::memcpy(out, bytes->data(), size);
    if (length != nullptr)
      *length = nx::cast<int>(size);
    g_bytes_read.fetch_add(size, std::memory_order_relaxed);
    return out;
  }
};

} // namespace

void install_platform() {
  ::spine::Bone::setYDown(false);

  static VfsExtension *const extension = [] {
    auto *const fresh = nx::allocate<VfsExtension>();
    if (fresh == nullptr)
      std::abort();
    ::spine::SpineExtension::setInstance(fresh);
    return fresh;
  }();
  (void)extension;
}

u64 bytes_read() noexcept {
  return g_bytes_read.load(std::memory_order_relaxed);
}

} // namespace nxe::spine2d

namespace spine {

SpineExtension *getDefaultExtension() {
  nxe::spine2d::install_platform();
  return SpineExtension::getInstance();
}

} // namespace spine
