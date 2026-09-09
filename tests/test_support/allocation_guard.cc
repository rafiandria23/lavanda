#include "test_support/allocation_guard.h"

#include <cstdlib>
#include <new>

namespace {

thread_local bool g_tracking_active = false;
thread_local std::size_t g_allocation_count = 0;

}  // namespace

namespace lavanda::test_support {

AllocationGuard::AllocationGuard() {
  baseline_count_ = g_allocation_count;
  g_tracking_active = true;
}

AllocationGuard::~AllocationGuard() { g_tracking_active = false; }

std::size_t AllocationGuard::allocation_count() const noexcept {
  return g_allocation_count - baseline_count_;
}

}  // namespace lavanda::test_support

void* operator new(std::size_t size) {
  void* ptr = std::malloc(size);

  if (ptr == nullptr) {
    throw std::bad_alloc();
  }

  if (g_tracking_active) {
    ++g_allocation_count;
  }

  return ptr;
}

void* operator new[](std::size_t size) { return ::operator new(size); }

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }
