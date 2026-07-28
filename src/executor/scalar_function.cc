#include "executor/scalar_function.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "types/type_converter.h"

namespace db {
namespace {

std::string toLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool checkArity(const IScalarFunction &fn, size_t argc, std::string *error_msg) {
  if (argc < fn.getMinArity() || argc > fn.getMaxArity()) {
    if (error_msg) {
      *error_msg = "Wrong number of arguments for function " + fn.getName();
    }
    return false;
  }
  return true;
}

class UpperFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "upper";
    return NAME;
  }
  size_t getMinArity() const override { return 1; }
  size_t getMaxArity() const override { return 1; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)error_msg;
    if (args[0].is_null()) {
      return Value();
    }
    std::string str = args[0].as_string();
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::toupper(c));
                   });
    return Value(str);
  }
};

class LowerFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "lower";
    return NAME;
  }
  size_t getMinArity() const override { return 1; }
  size_t getMaxArity() const override { return 1; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)error_msg;
    if (args[0].is_null()) {
      return Value();
    }
    std::string str = args[0].as_string();
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) {
                     return static_cast<char>(std::tolower(c));
                   });
    return Value(str);
  }
};

class LengthFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "length";
    return NAME;
  }
  size_t getMinArity() const override { return 1; }
  size_t getMaxArity() const override { return 1; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)error_msg;
    if (args[0].is_null()) {
      return Value();
    }
    return Value(static_cast<int64_t>(args[0].as_string().length()));
  }
};

class CoalesceFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "coalesce";
    return NAME;
  }
  size_t getMinArity() const override { return 1; }
  size_t getMaxArity() const override { return 64; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)error_msg;
    for (const Value &arg : args) {
      if (!arg.is_null()) {
        return arg;
      }
    }
    return Value();
  }
};

class NullIfFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "nullif";
    return NAME;
  }
  size_t getMinArity() const override { return 2; }
  size_t getMaxArity() const override { return 2; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)error_msg;
    if (args[0] == args[1]) {
      return Value();
    }
    return args[0];
  }
};

class SubstringFunction : public IScalarFunction {
 public:
  explicit SubstringFunction(std::string name) : name_(std::move(name)) {}
  const std::string &getName() const override { return name_; }
  size_t getMinArity() const override { return 2; }
  size_t getMaxArity() const override { return 3; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    if (args[0].is_null() || args[1].is_null()) {
      return Value();
    }
    if (!args[1].is_int()) {
      if (error_msg) {
        *error_msg = "SUBSTRING start must be INT";
      }
      return Value();
    }
    const std::string source = args[0].as_string();
    int64_t start = args[1].as_int();
    if (start < 1) {
      start = 1;
    }
    const size_t start_index = static_cast<size_t>(start - 1);
    if (start_index >= source.size()) {
      return Value(std::string());
    }
    if (args.size() == 2) {
      return Value(source.substr(start_index));
    }
    if (args[2].is_null()) {
      return Value();
    }
    if (!args[2].is_int()) {
      if (error_msg) {
        *error_msg = "SUBSTRING length must be INT";
      }
      return Value();
    }
    int64_t length = args[2].as_int();
    if (length < 0) {
      length = 0;
    }
    return Value(source.substr(start_index, static_cast<size_t>(length)));
  }

 private:
  std::string name_;
};

class CurrentDateFunction : public IScalarFunction {
 public:
  const std::string &getName() const override {
    static const std::string NAME = "current_date";
    return NAME;
  }
  size_t getMinArity() const override { return 0; }
  size_t getMaxArity() const override { return 0; }
  Value evaluate(const std::vector<Value> &args,
                 std::string *error_msg) const override {
    (void)args;
    (void)error_msg;
    using clock = std::chrono::system_clock;
    const std::time_t now = clock::to_time_t(clock::now());
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now);
#else
    localtime_r(&now, &local_tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%Y-%m-%d");
    return Value(oss.str());
  }
};

}  // namespace

ScalarFunctionRegistry &ScalarFunctionRegistry::instance() {
  static ScalarFunctionRegistry registry;
  return registry;
}

ScalarFunctionRegistry::ScalarFunctionRegistry() { registerDefaults(); }

void ScalarFunctionRegistry::registerDefaults() {
  registerBuiltin(std::make_unique<UpperFunction>());
  registerBuiltin(std::make_unique<LowerFunction>());
  registerBuiltin(std::make_unique<LengthFunction>());
  registerBuiltin(std::make_unique<CoalesceFunction>());
  registerBuiltin(std::make_unique<NullIfFunction>());
  registerBuiltin(std::make_unique<SubstringFunction>("substring"));
  registerBuiltin(std::make_unique<SubstringFunction>("substr"));
  registerBuiltin(std::make_unique<CurrentDateFunction>());
}

void ScalarFunctionRegistry::registerBuiltin(
    std::unique_ptr<IScalarFunction> function) {
  const std::string name = toLowerCopy(function->getName());
  functions_[name] = std::move(function);
}

const IScalarFunction *ScalarFunctionRegistry::find(
    const std::string &name) const {
  const auto it = functions_.find(toLowerCopy(name));
  if (it == functions_.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool ScalarFunctionRegistry::hasFunction(const std::string &name) const {
  return find(name) != nullptr;
}

Value ScalarFunctionRegistry::evaluate(const std::string &name,
                                       const std::vector<Value> &args,
                                       std::string *error_msg) const {
  const IScalarFunction *fn = find(name);
  if (!fn) {
    if (error_msg) {
      *error_msg = "Unknown function: " + name;
    }
    return Value();
  }
  if (!checkArity(*fn, args.size(), error_msg)) {
    return Value();
  }
  return fn->evaluate(args, error_msg);
}

}  // namespace db
