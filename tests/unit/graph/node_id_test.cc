#include "lavanda/graph/node_id.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(NodeIdTest, DefaultConstructedIsInvalid) {
  NodeId id;

  EXPECT_FALSE(id.is_valid());
}

TEST(NodeIdTest, ExplicitIndexIsValid) {
  NodeId id{4, 2};

  EXPECT_TRUE(id.is_valid());
}

TEST(NodeIdTest, EqualityComparesIndexAndGeneration) {
  NodeId a{2, 5};
  NodeId b{2, 5};
  NodeId different_generation{2, 6};
  NodeId different_index{3, 5};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, different_generation);
  EXPECT_NE(a, different_index);
}

}  // namespace
}  // namespace lavanda
