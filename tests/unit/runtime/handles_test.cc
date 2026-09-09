#include "lavanda/runtime/handles.h"

#include <gtest/gtest.h>

namespace lavanda {
namespace {

TEST(VoiceIdTest, DefaultConstructedIsInvalid) {
  VoiceId id;

  EXPECT_FALSE(id.is_valid());
}

TEST(VoiceIdTest, ExplicitIndexIsValid) {
  VoiceId id{3, 1};

  EXPECT_TRUE(id.is_valid());
}

TEST(VoiceIdTest, EqualityComparesIndexAndGeneration) {
  VoiceId a{2, 5};
  VoiceId b{2, 5};
  VoiceId different_generation{2, 6};
  VoiceId different_index{3, 5};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, different_generation);
  EXPECT_NE(a, different_index);
}

TEST(BusIdTest, DefaultConstructedIsInvalid) {
  BusId id;

  EXPECT_FALSE(id.is_valid());
}

TEST(BusIdTest, EqualityComparesIndexAndGeneration) {
  BusId a{1, 2};
  BusId b{1, 2};
  BusId different{1, 3};

  EXPECT_EQ(a, b);
  EXPECT_NE(a, different);
}

TEST(BusIdTest, MasterBusIdIsWellKnownAndValid) {
  EXPECT_TRUE(kMasterBusId.is_valid());
  EXPECT_EQ(kMasterBusId.index, 0u);
  EXPECT_EQ(kMasterBusId.generation, 0u);
}

}  // namespace
}  // namespace lavanda
