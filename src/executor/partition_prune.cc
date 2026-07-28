#include "executor/partition_prune.h"

#include "executor/index_predicate.h"

namespace db {

PartitionPruneRequest buildPartitionPruneRequest(
    const std::string &keyColumn, const ExpressionPtr &whereExpr) {
  PartitionPruneRequest request;
  if (!whereExpr) {
    return request;
  }
  auto preds = extract_index_predicates(whereExpr);
  if (!preds) {
    return request;
  }
  for (const IndexColumnPredicate &pred : *preds) {
    if (pred.column_name != keyColumn) {
      continue;
    }
    PartitionPruneConstraint constraint;
    switch (pred.op) {
      case IndexCompareOp::Equal:
        constraint.op = PartitionPruneConstraint::Op::Equal;
        constraint.lower = pred.literal;
        break;
      case IndexCompareOp::Less:
        constraint.op = PartitionPruneConstraint::Op::Less;
        constraint.upper = pred.literal;
        constraint.upperInclusive = false;
        break;
      case IndexCompareOp::LessEqual:
        constraint.op = PartitionPruneConstraint::Op::LessEqual;
        constraint.upper = pred.literal;
        constraint.upperInclusive = true;
        break;
      case IndexCompareOp::Greater:
        constraint.op = PartitionPruneConstraint::Op::Greater;
        constraint.lower = pred.literal;
        constraint.lowerInclusive = false;
        break;
      case IndexCompareOp::GreaterEqual:
        constraint.op = PartitionPruneConstraint::Op::GreaterEqual;
        constraint.lower = pred.literal;
        constraint.lowerInclusive = true;
        break;
    }
    if (pred.has_upper) {
      constraint.op = PartitionPruneConstraint::Op::Between;
      constraint.lower = pred.literal;
      constraint.upper = pred.upper_literal;
      constraint.lowerInclusive = pred.lower_inclusive;
      constraint.upperInclusive = pred.upper_inclusive;
    }
    request.constraints.push_back(constraint);
  }
  return request;
}

}  // namespace db
