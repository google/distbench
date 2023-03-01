#include "gtest_utils.h"

#include <glog/logging.h>

#include "gtest/gtest.h"

GTEST_API_ int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
