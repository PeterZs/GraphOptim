#define TEST_NAME "util/random"
#include "util/random.h"

#include <numeric>

#include "util/math.h"
#include "util/testing.h"

using namespace colmap;

BOOST_AUTO_TEST_CASE(TestPRNGSeed) {
  BOOST_CHECK(PRNG == nullptr);
  SetPRNGSeed();
  BOOST_CHECK(PRNG != nullptr);
  SetPRNGSeed(0);
  BOOST_CHECK(PRNG != nullptr);
}

BOOST_AUTO_TEST_CASE(TestRepeatability) {
  SetPRNGSeed(0);
  std::vector<int> numbers1;
  for (size_t i = 0; i < 100; ++i) {
    numbers1.push_back(RandomInteger(0, 10000));
  }
  SetPRNGSeed();
  std::vector<int> numbers2;
  for (size_t i = 0; i < 100; ++i) {
    numbers2.push_back(RandomInteger(0, 10000));
  }
  SetPRNGSeed(0);
  std::vector<int> numbers3;
  for (size_t i = 0; i < 100; ++i) {
    numbers3.push_back(RandomInteger(0, 10000));
  }
  BOOST_CHECK_EQUAL_COLLECTIONS(numbers1.begin(), numbers1.end(),
                                numbers3.begin(), numbers3.end());
  bool all_equal = true;
  for (size_t i = 0; i < numbers1.size(); ++i) {
    if (numbers1[i] != numbers2[i]) {
      all_equal = false;
    }
  }
  BOOST_CHECK(!all_equal);
}

BOOST_AUTO_TEST_CASE(TestRandomInteger) {
  SetPRNGSeed();
  for (size_t i = 0; i < 1000; ++i) {
    BOOST_CHECK_GE(RandomInteger(-100, 100), -100);
    BOOST_CHECK_LE(RandomInteger(-100, 100), 100);
  }
}

BOOST_AUTO_TEST_CASE(TestRandomReal) {
  SetPRNGSeed();
  for (size_t i = 0; i < 1000; ++i) {
    BOOST_CHECK_GE(RandomReal(-100.0, 100.0), -100.0);
    BOOST_CHECK_LE(RandomReal(-100.0, 100.0), 100.0);
  }
}

BOOST_AUTO_TEST_CASE(TestRandomGaussian) {
  SetPRNGSeed(0);
  const double kMean = 1.0;
  const double kSigma = 1.0;
  const size_t kNumValues = 100000;
  std::vector<double> values;
  for (size_t i = 0; i < kNumValues; ++i) {
    values.push_back(RandomGaussian(kMean, kSigma));
  }
  BOOST_CHECK_LE(std::abs(Mean(values) - kMean), 1e-2);
  BOOST_CHECK_LE(std::abs(StdDev(values) - kSigma), 1e-2);
}

BOOST_AUTO_TEST_CASE(TestShuffleNone) {
  SetPRNGSeed();
  std::vector<int> numbers(0);
  Shuffle(0, &numbers);
  numbers = {1, 2, 3, 4, 5};
  std::vector<int> shuffled_numbers = numbers;
  Shuffle(0, &shuffled_numbers);
  BOOST_CHECK_EQUAL_COLLECTIONS(numbers.begin(), numbers.end(),
                                shuffled_numbers.begin(),
                                shuffled_numbers.end());
}

BOOST_AUTO_TEST_CASE(TestShuffleAll) {
  SetPRNGSeed(0);
  std::vector<int> numbers(1000);
  std::iota(numbers.begin(), numbers.end(), 0);
  std::vector<int> shuffled_numbers = numbers;
  Shuffle(1000, &shuffled_numbers);
  size_t num_shuffled = 0;
  for (size_t i = 0; i < numbers.size(); ++i) {
    if (numbers[i] != shuffled_numbers[i]) {
      num_shuffled += 1;
    }
  }
  BOOST_CHECK_GT(num_shuffled, 0);
}
