#include "gtest_utils.h"

#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "gtest/gtest.h"

GTEST_API_ int main(int argc, char** argv) {
  absl::InitializeLog();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
