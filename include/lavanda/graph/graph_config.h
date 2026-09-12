#ifndef LAVANDA_GRAPH_GRAPH_CONFIG_H_
#define LAVANDA_GRAPH_GRAPH_CONFIG_H_

#include <cstddef>

namespace lavanda {

struct GraphConfig {
  std::size_t max_nodes = 64;
  std::size_t max_connections = 256;
};

}  // namespace lavanda

#endif
