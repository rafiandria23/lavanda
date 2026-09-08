#ifndef LAVANDA_TESTS_TEST_SUPPORT_DETERMINISTIC_SIGNAL_H_
#define LAVANDA_TESTS_TEST_SUPPORT_DETERMINISTIC_SIGNAL_H_

#include "lavanda/core/audio_buffer.h"

namespace lavanda::test_support {

void FillWithRamp(AudioBufferView view);

}

#endif
