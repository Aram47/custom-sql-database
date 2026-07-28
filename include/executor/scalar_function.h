#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "types/value.h"

namespace db {

/**
 * Builtin scalar function (UPPER, COALESCE, …).
 * Implementations are registered once in ScalarFunctionRegistry.
 */
class IScalarFunction {
 public:
  virtual ~IScalarFunction() = default;

  /** Lowercase canonical name used for lookup. */
  virtual const std::string &getName() const = 0;
  virtual size_t getMinArity() const = 0;
  virtual size_t getMaxArity() const = 0;
  virtual Value evaluate(const std::vector<Value> &args,
                         std::string *error_msg) const = 0;
};

/**
 * Open/Closed registry of scalar builtins.
 * Unknown names return nullptr from find.
 */
class ScalarFunctionRegistry {
 public:
  static ScalarFunctionRegistry &instance();

  void registerBuiltin(std::unique_ptr<IScalarFunction> function);
  const IScalarFunction *find(const std::string &name) const;
  bool hasFunction(const std::string &name) const;

  /**
   * Looks up name (case-insensitive) and evaluates with arity checks.
   * On failure sets error_msg and returns NULL Value.
   */
  Value evaluate(const std::string &name, const std::vector<Value> &args,
                 std::string *error_msg) const;

 private:
  ScalarFunctionRegistry();
  void registerDefaults();

  std::unordered_map<std::string, std::unique_ptr<IScalarFunction>> functions_;
};

}  // namespace db
