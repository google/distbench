#ifndef DISTBENCH_DISTBENCH_SUMMARY_H_
#define DISTBENCH_DISTBENCH_SUMMARY_H_

#include "distbench.pb.h"

namespace distbench {

std::vector<std::string> SummarizeTestResult(const TestResult& test_result);

} // namespace distbench

#endif  // DISTBENCH_DISTBENCH_SUMMARY_H_
