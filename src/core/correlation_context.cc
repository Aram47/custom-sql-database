#include "core/correlation_context.h"

namespace db {

void CorrelationContext::pushFrame(
    const std::vector<SelectColumnBinding> &bindings, const Row &row) {
  Frame frame;
  for (size_t i = 0; i < bindings.size() && i < row.get_column_count(); ++i) {
    const SelectColumnBinding &binding = bindings[i];
    const Value &value = row.get_value(i);
    frame.by_column[binding.column_name] = value;
    if (!binding.alias.empty()) {
      frame.by_qualified[binding.alias + "." + binding.column_name] = value;
    }
    if (!binding.physical_table.empty()) {
      frame.by_qualified[binding.physical_table + "." + binding.column_name] =
          value;
    }
  }
  frames_.push_back(std::move(frame));
}

void CorrelationContext::popFrame() {
  if (!frames_.empty()) {
    frames_.pop_back();
  }
}

bool CorrelationContext::empty() const { return frames_.empty(); }

std::optional<Value> CorrelationContext::lookup(
    const ColumnRefExpression &cref) const {
  if (frames_.empty()) {
    return std::nullopt;
  }
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
    if (!cref.get_table().empty()) {
      const std::string key = cref.get_table() + "." + cref.get_column();
      auto found = it->by_qualified.find(key);
      if (found != it->by_qualified.end()) {
        return found->second;
      }
      continue;
    }
    auto found = it->by_column.find(cref.get_column());
    if (found != it->by_column.end()) {
      return found->second;
    }
  }
  return std::nullopt;
}

}  // namespace db
