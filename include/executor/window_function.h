#pragma once

#include <memory>
#include <string>

#include "types/value.h"

namespace db {

/**
 * Strategy for one window function within a partition.
 */
class IWindowFunction {
 public:
  virtual ~IWindowFunction() = default;
  /** Resets state at the start of a new partition. */
  virtual void resetPartition() = 0;
  /**
   * Consumes the next row in ORDER BY sequence.
   * @param arg Evaluated argument (NULL for ranking functions).
   * @param isPeerWithPrevious True when ORDER BY keys equal the previous row.
   */
  virtual void consumeRow(const Value &arg, bool isPeerWithPrevious) = 0;
  /** Returns the value for the row just consumed. */
  virtual Value currentValue() const = 0;
};

/**
 * Creates a window function strategy by name.
 * @param name Function name (case-insensitive).
 * @return Strategy or nullptr if unsupported.
 */
std::unique_ptr<IWindowFunction> make_window_function(const std::string &name);

}  // namespace db
