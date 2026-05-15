#include "executor/aggregate_function.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>

namespace db {

namespace {

std::string lower_copy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool is_wildcard_arg(const ExpressionPtr &arg) {
  if (!arg) return true;
  if (auto id = std::dynamic_pointer_cast<IdentifierExpression>(arg)) {
    return id->get_name() == "*";
  }
  if (auto cr = std::dynamic_pointer_cast<ColumnRefExpression>(arg)) {
    return cr->get_column() == "*";
  }
  return false;
}

class CountAggregate : public IAggregateFunction {
 public:
  explicit CountAggregate(bool count_star) : count_star_(count_star) {}

  void reset() override {
    count_ = 0;
    saw_row_ = false;
  }

  void accumulate(const Value &value) override {
    saw_row_ = true;
    if (count_star_) {
      ++count_;
      return;
    }
    if (!value.is_null()) ++count_;
  }

  Value finalize() const override {
    if (count_star_ && !saw_row_) return Value(static_cast<int64_t>(0));
    return Value(static_cast<int64_t>(count_));
  }

 private:
  bool count_star_;
  int64_t count_ = 0;
  bool saw_row_ = false;
};

class SumAggregate : public IAggregateFunction {
 public:
  void reset() override {
    has_value_ = false;
    sum_ = 0.0;
  }

  void accumulate(const Value &value) override {
    if (value.is_null()) return;
    if (value.is_int()) {
      sum_ += static_cast<double>(value.as_int());
      has_value_ = true;
    } else if (value.is_float()) {
      sum_ += value.as_float();
      has_value_ = true;
    }
  }

  Value finalize() const override {
    if (!has_value_) return Value();
    const double rounded = std::round(sum_);
    if (std::fabs(sum_ - rounded) < 1e-9) {
      return Value(static_cast<int64_t>(rounded));
    }
    return Value(sum_);
  }

 private:
  bool has_value_ = false;
  double sum_ = 0.0;
};

class AvgAggregate : public IAggregateFunction {
 public:
  void reset() override {
    count_ = 0;
    sum_ = 0.0;
  }

  void accumulate(const Value &value) override {
    if (value.is_null()) return;
    if (value.is_int()) {
      sum_ += static_cast<double>(value.as_int());
      ++count_;
    } else if (value.is_float()) {
      sum_ += value.as_float();
      ++count_;
    }
  }

  Value finalize() const override {
    if (count_ == 0) return Value();
    return Value(sum_ / static_cast<double>(count_));
  }

 private:
  int64_t count_ = 0;
  double sum_ = 0.0;
};

class MinMaxAggregate : public IAggregateFunction {
 public:
  explicit MinMaxAggregate(bool is_min) : is_min_(is_min) {}

  void reset() override { has_value_ = false; }

  void accumulate(const Value &value) override {
    if (value.is_null()) return;
    if (!has_value_) {
      current_ = value;
      has_value_ = true;
      return;
    }
    if (is_min_) {
      if (value < current_) current_ = value;
    } else {
      if (value > current_) current_ = value;
    }
  }

  Value finalize() const override {
    if (!has_value_) return Value();
    return current_;
  }

 private:
  bool is_min_;
  bool has_value_ = false;
  Value current_;
};

}  // namespace

bool is_count_star(const FunctionCallExpression &fn) {
  if (lower_copy(fn.get_function_name()) != "count") return false;
  const auto &args = fn.get_arguments();
  if (args.empty()) return true;
  return is_wildcard_arg(args[0]);
}

std::unique_ptr<IAggregateFunction> make_aggregate_function(
    const std::string &name, const std::vector<ExpressionPtr> &args) {
  const std::string n = lower_copy(name);
  if (n == "count") {
    const bool star = args.empty() || is_wildcard_arg(args[0]);
    return std::make_unique<CountAggregate>(star);
  }
  if (n == "sum") return std::make_unique<SumAggregate>();
  if (n == "avg") return std::make_unique<AvgAggregate>();
  if (n == "min") return std::make_unique<MinMaxAggregate>(true);
  if (n == "max") return std::make_unique<MinMaxAggregate>(false);
  return nullptr;
}

}  // namespace db
