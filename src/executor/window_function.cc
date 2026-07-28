#include "executor/window_function.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace db {

namespace {

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

class RowNumberWindowFunction : public IWindowFunction {
 public:
  void resetPartition() override { row_number_ = 0; }
  void consumeRow(const Value &arg, bool isPeerWithPrevious) override {
    (void)arg;
    (void)isPeerWithPrevious;
    ++row_number_;
  }
  Value currentValue() const override {
    return Value(static_cast<int64_t>(row_number_));
  }

 private:
  int64_t row_number_{0};
};

class RankWindowFunction : public IWindowFunction {
 public:
  void resetPartition() override {
    row_number_ = 0;
    rank_ = 0;
  }
  void consumeRow(const Value &arg, bool isPeerWithPrevious) override {
    (void)arg;
    ++row_number_;
    if (!isPeerWithPrevious || row_number_ == 1) {
      rank_ = row_number_;
    }
  }
  Value currentValue() const override {
    return Value(static_cast<int64_t>(rank_));
  }

 private:
  int64_t row_number_{0};
  int64_t rank_{0};
};

class DenseRankWindowFunction : public IWindowFunction {
 public:
  void resetPartition() override {
    dense_rank_ = 0;
    is_first_ = true;
  }
  void consumeRow(const Value &arg, bool isPeerWithPrevious) override {
    (void)arg;
    if (is_first_ || !isPeerWithPrevious) {
      ++dense_rank_;
      is_first_ = false;
    }
  }
  Value currentValue() const override {
    return Value(static_cast<int64_t>(dense_rank_));
  }

 private:
  int64_t dense_rank_{0};
  bool is_first_{true};
};

class RunningSumWindowFunction : public IWindowFunction {
 public:
  void resetPartition() override {
    sum_ = Value(static_cast<int64_t>(0));
    has_value_ = false;
  }
  void consumeRow(const Value &arg, bool isPeerWithPrevious) override {
    (void)isPeerWithPrevious;
    if (arg.is_null()) {
      return;
    }
    if (!has_value_) {
      sum_ = arg;
      has_value_ = true;
      return;
    }
    sum_ = sum_ + arg;
  }
  Value currentValue() const override {
    if (!has_value_) {
      return Value();
    }
    return sum_;
  }

 private:
  Value sum_;
  bool has_value_{false};
};

class RunningAvgWindowFunction : public IWindowFunction {
 public:
  void resetPartition() override {
    sum_ = 0.0;
    count_ = 0;
  }
  void consumeRow(const Value &arg, bool isPeerWithPrevious) override {
    (void)isPeerWithPrevious;
    if (arg.is_null()) {
      return;
    }
    sum_ += arg.as_float();
    ++count_;
  }
  Value currentValue() const override {
    if (count_ == 0) {
      return Value();
    }
    return Value(sum_ / static_cast<double>(count_));
  }

 private:
  double sum_{0.0};
  int64_t count_{0};
};

}  // namespace

std::unique_ptr<IWindowFunction> make_window_function(const std::string &name) {
  const std::string n = lower_copy(name);
  if (n == "row_number") {
    return std::make_unique<RowNumberWindowFunction>();
  }
  if (n == "rank") {
    return std::make_unique<RankWindowFunction>();
  }
  if (n == "dense_rank") {
    return std::make_unique<DenseRankWindowFunction>();
  }
  if (n == "sum") {
    return std::make_unique<RunningSumWindowFunction>();
  }
  if (n == "avg") {
    return std::make_unique<RunningAvgWindowFunction>();
  }
  return nullptr;
}

}  // namespace db
