#ifndef LAVANDA_GRAPH_GRAPH_COMPILE_CONTEXT_H_
#define LAVANDA_GRAPH_GRAPH_COMPILE_CONTEXT_H_

#include <cstdint>

namespace lavanda {

class ResourceStore;

struct GraphCompileContext {
  ResourceStore* resource_store = nullptr;
  std::uint32_t sample_rate_hz = 0;
};

}  // namespace lavanda

#endif
