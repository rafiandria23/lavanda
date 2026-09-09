#ifndef LAVANDA_TESTS_TEST_SUPPORT_ALLOCATION_GUARD_H_
#define LAVANDA_TESTS_TEST_SUPPORT_ALLOCATION_GUARD_H_

#include <cstddef>

namespace lavanda::test_support {

class AllocationGuard {
 public:
  AllocationGuard();
  ~AllocationGuard();

  AllocationGuard(const AllocationGuard&) = delete;
  AllocationGuard& operator=(const AllocationGuard&) = delete;

  std::size_t allocation_count() const noexcept;

 private:
  std::size_t baseline_count_;
};

inline void DoNotOptimizeAway(const void* pointer) {
  asm volatile("" : : "g"(pointer) : "memory");
}

}  // namespace lavanda::test_support

#endif
