#pragma once

#include <string>

#include "core/partition.h"
#include "parser/ast.h"

namespace db {

/**
 * Builds a prune request from sargable AND predicates on keyColumn.
 * Non-sargable WHERE yields an empty-constraint request (scan all).
 */
PartitionPruneRequest buildPartitionPruneRequest(
    const std::string &keyColumn, const ExpressionPtr &whereExpr);

}  // namespace db
