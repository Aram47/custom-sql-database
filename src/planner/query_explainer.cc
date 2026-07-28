#include "planner/query_explainer.h"

#include <map>
#include <sstream>

#include "core/database.h"
#include "core/table.h"
#include "executor/index_predicate.h"
#include "executor/select_analysis.h"
#include "planner/access_path_chooser.h"
#include "planner/join_method_plan.h"
#include "planner/join_order_planner.h"

namespace db {

namespace {

bool predicateMatchesTable(const IndexColumnPredicate &pred, Table *table,
                           const std::string &alias) {
  if (pred.table_qualifier.empty()) {
    return true;
  }
  if (pred.table_qualifier == table->get_name()) {
    return true;
  }
  return !alias.empty() && pred.table_qualifier == alias;
}

std::string accessPathKindName(AccessPathKind kind) {
  if (kind == AccessPathKind::IndexScan) {
    return "IndexScan";
  }
  return "SeqScan";
}

std::string joinTypeLabel(const std::string &joinType) {
  if (joinType.empty()) {
    return "JOIN";
  }
  return joinType + " JOIN";
}

}  // namespace

QueryExplainer::QueryExplainer(Database *database) : database_(database) {}

bool QueryExplainer::hasIndexAccessPath(Table *table, const std::string &alias,
                                        const ExpressionPtr &whereExpr) const {
  if (!table || !whereExpr) {
    return false;
  }
  auto preds = extract_index_predicates(whereExpr);
  if (!preds || preds->empty()) {
    return false;
  }
  std::map<std::string, Value> equalByColumn;
  for (const auto &pred : *preds) {
    if (!predicateMatchesTable(pred, table, alias)) {
      continue;
    }
    if (pred.op == IndexCompareOp::Equal) {
      equalByColumn[pred.column_name] = pred.literal;
    }
  }
  for (const auto &[indexName, columns] : table->get_secondary_indexes()) {
    (void)indexName;
    size_t prefixLen = 0;
    for (const std::string &columnName : columns) {
      if (equalByColumn.find(columnName) == equalByColumn.end()) {
        break;
      }
      ++prefixLen;
    }
    if (prefixLen > 0) {
      return true;
    }
  }
  for (const auto &pred : *preds) {
    if (!predicateMatchesTable(pred, table, alias)) {
      continue;
    }
    if (table->has_index(pred.column_name)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> QueryExplainer::explainSelect(
    const std::shared_ptr<SelectStatement> &stmt) const {
  std::vector<std::string> lines;
  if (!stmt) {
    lines.push_back("SELECT (empty)");
    return lines;
  }
  const std::string &tableName = stmt->get_from_table();
  if (database_ && database_->has_view(tableName)) {
    lines.push_back("ViewScan(" + tableName + ")");
  }
  Table *table = database_ ? database_->get_table(tableName) : nullptr;
  if (!stmt->get_joins().empty()) {
    lines.push_back("JoinSelect on " + tableName);
    std::vector<Table *> orderedTables;
    std::vector<std::string> aliases;
    if (table) {
      orderedTables.push_back(table);
      aliases.push_back(stmt->get_from_alias().empty() ? tableName
                                                      : stmt->get_from_alias());
    }
    for (const auto &joinTup : stmt->get_joins()) {
      const std::string &joinType = std::get<0>(joinTup);
      const std::string &joinTable = std::get<1>(joinTup);
      const std::string &joinAlias = std::get<2>(joinTup);
      const ExpressionPtr &onExpr = std::get<3>(joinTup);
      lines.push_back("  " + joinTypeLabel(joinType) + " " + joinTable);
      if (database_ && database_->has_view(joinTable)) {
        lines.push_back("    ViewScan(" + joinTable + ")");
      }
      if (onExpr) {
        lines.push_back("    ON " + onExpr->to_string());
      }
      if (database_) {
        Table *jt = database_->get_table(joinTable);
        if (jt) {
          orderedTables.push_back(jt);
          aliases.push_back(joinAlias.empty() ? joinTable : joinAlias);
        }
      }
    }
    bool allInnerOrCross = true;
    for (const auto &joinTup : stmt->get_joins()) {
      const std::string &joinType = std::get<0>(joinTup);
      if (joinType != "INNER" && joinType != "CROSS") {
        allInnerOrCross = false;
        break;
      }
    }
    if (allInnerOrCross && orderedTables.size() == 2) {
      std::vector<JoinRelation> relations(2);
      relations[0].table_name = orderedTables[0]->get_name();
      relations[0].estimated_rows = orderedTables[0]->get_row_count();
      relations[1].table_name = orderedTables[1]->get_name();
      relations[1].estimated_rows = orderedTables[1]->get_row_count();
      const ExpressionPtr &onExpr = std::get<3>(stmt->get_joins()[0]);
      std::string leftTable;
      std::string leftColumn;
      std::string rightTable;
      std::string rightColumn;
      if (onExpr && try_extract_equi_join_columns(onExpr, leftTable, leftColumn,
                                                 rightTable, rightColumn)) {
        relations[1].has_indexed_equi_join =
            orderedTables[1]->has_index(rightColumn) ||
            orderedTables[1]->has_index(leftColumn);
        relations[0].has_indexed_equi_join =
            orderedTables[0]->has_index(rightColumn) ||
            orderedTables[0]->has_index(leftColumn);
      }
      const std::vector<size_t> order =
          JoinOrderPlanner::planLeftDeepOrder(relations);
      if (order.size() == 2) {
        std::ostringstream orderLine;
        orderLine << "  Join order: " << relations[order[0]].table_name << ", "
                  << relations[order[1]].table_name;
        lines.push_back(orderLine.str());
        Table *leftTable = orderedTables[order[0]];
        Table *rightTable = orderedTables[order[1]];
        const std::string &leftAlias = aliases[order[0]];
        const std::string &rightAlias = aliases[order[1]];
        std::vector<SelectColumnBinding> leftBindings;
        for (const auto &col : leftTable->get_columns()) {
          leftBindings.push_back(
              {leftAlias, leftTable->get_name(), col.get_name()});
        }
        const ExpressionPtr &joinOn = std::get<3>(stmt->get_joins()[0]);
        const EquiJoinPlan equiPlan = planEquiJoinMethod(
            database_, leftTable, rightTable, leftAlias, rightAlias, joinOn,
            leftTable->get_row_count(), rightTable->get_row_count(),
            leftBindings);
        std::ostringstream methodLine;
        methodLine << "  Join method: "
                   << joinMethodKindName(equiPlan.method.kind);
        lines.push_back(methodLine.str());
      }
    }
  } else if (table && table->isPartitioned()) {
    const auto children =
        database_->listPrunedPartitions(table, stmt->get_where_condition());
    const size_t total =
        table->getPartitionMetadata()->getPartitions().size();
    std::ostringstream pruneLine;
    pruneLine << "PartitionPrune on " << tableName << " -> " << children.size()
              << " of " << total << " partitions";
    lines.push_back(pruneLine.str());
    for (const std::string &childName : children) {
      Table *child = database_->get_table(childName);
      const bool hasIndexPath =
          hasIndexAccessPath(child, stmt->get_from_alias(),
                             stmt->get_where_condition());
      const size_t rowCount = child ? child->get_row_count() : 0;
      const double selectivity =
          rowCount == 0 ? 1.0 : 1.0 / static_cast<double>(rowCount);
      const AccessPathChoice path =
          AccessPathChooser::choose(rowCount, hasIndexPath, selectivity);
      std::ostringstream scanLine;
      scanLine << "  " << accessPathKindName(path.kind) << " on " << childName
               << " (cost=" << path.cost << ")";
      lines.push_back(scanLine.str());
    }
  } else {
    const bool hasIndexPath =
        hasIndexAccessPath(table, stmt->get_from_alias(),
                           stmt->get_where_condition());
    const size_t rowCount = table ? table->get_row_count() : 0;
    const double selectivity =
        rowCount == 0 ? 1.0 : 1.0 / static_cast<double>(rowCount);
    const AccessPathChoice path =
        AccessPathChooser::choose(rowCount, hasIndexPath, selectivity);
    std::ostringstream scanLine;
    scanLine << accessPathKindName(path.kind) << " on " << tableName
             << " (cost=" << path.cost << ")";
    lines.push_back(scanLine.str());
  }
  if (stmt->get_where_condition()) {
    lines.push_back("  Filter: " + stmt->get_where_condition()->to_string());
  }
  if (!stmt->get_group_by_columns().empty()) {
    lines.push_back("  GroupAggregate");
  }
  if (select_has_window(stmt)) {
    lines.push_back("  Window");
  }
  if (stmt->is_distinct()) {
    lines.push_back("  Distinct");
  }
  if (!stmt->get_order_by_columns().empty()) {
    lines.push_back("  Sort");
  }
  if (stmt->get_limit() > 0 || stmt->get_offset() > 0) {
    lines.push_back("  LimitOffset");
  }
  return lines;
}

namespace {

std::string setOperationLabel(SetOperationKind kind, bool isAll) {
  switch (kind) {
    case SetOperationKind::Union:
      return isAll ? "Union All" : "Union";
    case SetOperationKind::Intersect:
      return "Intersect";
    case SetOperationKind::Except:
      return "Except";
  }
  return "Union";
}

void appendIndented(std::vector<std::string> &dest,
                    const std::vector<std::string> &source) {
  for (const std::string &line : source) {
    dest.push_back("  " + line);
  }
}

}  // namespace

std::vector<std::string> QueryExplainer::explainSetOperation(
    const std::shared_ptr<SetOperationStatement> &stmt) const {
  std::vector<std::string> lines;
  if (!stmt) {
    lines.push_back("SetOperation (empty)");
    return lines;
  }
  lines.push_back(setOperationLabel(stmt->get_kind(), stmt->is_all()));
  auto explainOperand =
      [&](const SetOperationStatement::Operand &operand) {
        if (auto select =
                std::get_if<std::shared_ptr<SelectStatement>>(&operand)) {
          return explainSelect(*select);
        }
        return explainSetOperation(
            std::get<std::shared_ptr<SetOperationStatement>>(operand));
      };
  appendIndented(lines, explainOperand(stmt->get_left()));
  appendIndented(lines, explainOperand(stmt->get_right()));
  if (!stmt->get_order_by_columns().empty()) {
    lines.push_back("  Sort");
  }
  if (stmt->get_limit() > 0 || stmt->get_offset() > 0) {
    lines.push_back("  LimitOffset");
  }
  return lines;
}

std::vector<std::string> QueryExplainer::explainInsert(
    const std::shared_ptr<InsertStatement> &stmt) const {
  std::vector<std::string> lines;
  if (!stmt) {
    lines.push_back("INSERT (empty)");
    return lines;
  }
  std::ostringstream line;
  line << "Insert on " << stmt->get_table() << " (" << stmt->get_values().size()
       << " row(s))";
  lines.push_back(line.str());
  Table *table = database_ ? database_->get_table(stmt->get_table()) : nullptr;
  if (table && table->isPartitioned() && !stmt->get_values().empty() &&
      !stmt->get_values()[0].empty()) {
    lines.push_back("  PartitionRoute by " +
                    table->getPartitionMetadata()->getKeyColumn());
  }
  return lines;
}

std::vector<std::string> QueryExplainer::explainUpdate(
    const std::shared_ptr<UpdateStatement> &stmt) const {
  std::vector<std::string> lines;
  if (!stmt) {
    lines.push_back("UPDATE (empty)");
    return lines;
  }
  std::ostringstream line;
  line << "Update on " << stmt->get_table() << " ("
       << stmt->get_set_clauses().size() << " assignment(s))";
  lines.push_back(line.str());
  if (stmt->get_where_condition()) {
    lines.push_back("  Filter: " + stmt->get_where_condition()->to_string());
  } else {
    lines.push_back("  Filter: (none)");
  }
  return lines;
}

std::vector<std::string> QueryExplainer::explainDelete(
    const std::shared_ptr<DeleteStatement> &stmt) const {
  std::vector<std::string> lines;
  if (!stmt) {
    lines.push_back("DELETE (empty)");
    return lines;
  }
  lines.push_back("Delete on " + stmt->get_table());
  if (stmt->get_where_condition()) {
    lines.push_back("  Filter: " + stmt->get_where_condition()->to_string());
  } else {
    lines.push_back("  Filter: (none)");
  }
  return lines;
}

std::vector<std::string> QueryExplainer::buildPlanLines(
    const ParsedStatement &stmt) const {
  if (auto p = std::get_if<std::shared_ptr<SelectStatement>>(&stmt)) {
    return explainSelect(*p);
  }
  if (auto p = std::get_if<std::shared_ptr<SetOperationStatement>>(&stmt)) {
    return explainSetOperation(*p);
  }
  if (auto p = std::get_if<std::shared_ptr<InsertStatement>>(&stmt)) {
    return explainInsert(*p);
  }
  if (auto p = std::get_if<std::shared_ptr<UpdateStatement>>(&stmt)) {
    return explainUpdate(*p);
  }
  if (auto p = std::get_if<std::shared_ptr<DeleteStatement>>(&stmt)) {
    return explainDelete(*p);
  }
  if (auto p = std::get_if<std::shared_ptr<CreateTableStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<DropTableStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<AlterTableStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<CreateIndexStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<DropIndexStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<CreateViewStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<DropViewStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<BeginStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<CommitStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<RollbackStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<PrepareStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<ExecutePreparedStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p =
          std::get_if<std::shared_ptr<DeallocatePreparedStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  if (auto p = std::get_if<std::shared_ptr<VacuumStatement>>(&stmt)) {
    return {(*p)->to_string()};
  }
  return {"Unknown statement"};
}

}  // namespace db
