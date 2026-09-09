#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "lavanda/runtime/command_queue.h"

namespace lavanda {
namespace {

TEST(CommandQueueConcurrencyTest,
     SingleProducerSingleConsumerPreservesOrderAndCount) {
  constexpr int kCommandCount = 200000;
  constexpr std::size_t kCapacity = 32;

  CommandQueue queue(kCapacity);

  std::thread producer([&queue]() {
    for (int i = 0; i < kCommandCount; ++i) {
      Command command;

      command.type = CommandType::kSetFrequency;
      command.value = static_cast<float>(i);

      while (!queue.TryPush(command)) {
        std::this_thread::yield();
      }
    }
  });

  std::vector<float> received;
  received.reserve(kCommandCount);

  int consumed = 0;

  while (consumed < kCommandCount) {
    Command command;

    if (queue.TryPop(command)) {
      received.push_back(command.value);
      ++consumed;
    } else {
      std::this_thread::yield();
    }
  }

  producer.join();

  ASSERT_EQ(static_cast<int>(received.size()), kCommandCount);

  for (int i = 0; i < kCommandCount; ++i) {
    EXPECT_FLOAT_EQ(received[static_cast<std::size_t>(i)],
                    static_cast<float>(i))
        << "FIFO order violated at index " << i;
  }
}

}  // namespace
}  // namespace lavanda
