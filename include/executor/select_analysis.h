#pragma once

#include <optional>
#include <string>

#include "parser/ast.h"

namespace db {

bool is_aggregate_function_name(const std::string &name);
bool is_window_function_name(const std::string &name);
bool expression_has_aggregate(const ExpressionPtr &expr);
bool expression_has_window(const ExpressionPtr &expr);
bool select_has_aggregate(const std::shared_ptr<SelectStatement> &stmt);
bool select_has_window(const std::shared_ptr<SelectStatement> &stmt);
bool needs_grouping(const std::shared_ptr<SelectStatement> &stmt);

bool is_wildcard_select_expression(const ExpressionPtr &expr);
bool expressions_compatible_for_grouping(const ExpressionPtr &a,
                                         const ExpressionPtr &b);

/** Returns error message on failure, std::nullopt on success. */
std::optional<std::string> validate_select_for_grouping(
    const std::shared_ptr<SelectStatement> &stmt);

/** Returns error message on failure, std::nullopt on success. */
std::optional<std::string> validate_select_for_windows(
    const std::shared_ptr<SelectStatement> &stmt);

}  // namespace db
