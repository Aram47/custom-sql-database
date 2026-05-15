#include "executor/join_select_executor.h"

#include <algorithm>

#include "core/database.h"
#include "executor/select_expression_evaluator.h"

namespace db {

namespace {

std::vector<Value> row_values(const Row &row) {
  std::vector<Value> v;
  v.reserve(row.get_column_count());
  for (size_t i = 0; i < row.get_column_count(); ++i) {
    v.push_back(row.get_value(i));
  }
  return v;
}

std::vector<Value> concat_vectors(const std::vector<Value> &a,
                                  const std::vector<Value> &b) {
  std::vector<Value> out;
  out.reserve(a.size() + b.size());
  out.insert(out.end(), a.begin(), a.end());
  out.insert(out.end(), b.begin(), b.end());
  return out;
}

std::vector<Value> null_vector(size_t n) {
  return std::vector<Value>(n, Value());
}

size_t table_width(Table *t) { return t ? t->get_column_count() : 0; }

size_t cumulative_columns(const std::vector<Table *> &ordered,
                          size_t through_inclusive) {
  size_t s = 0;
  for (size_t i = 0; i <= through_inclusive && i < ordered.size(); ++i) {
    s += table_width(ordered[i]);
  }
  return s;
}

std::vector<SelectColumnBinding> build_bindings_for_tables(
    const std::vector<Table *> &ordered,
    const std::vector<std::string> &aliases) {
  std::vector<SelectColumnBinding> b;
  for (size_t ti = 0; ti < ordered.size(); ++ti) {
    Table *tab = ordered[ti];
    const std::string &alias = aliases[ti];
    const std::string phys = tab->get_name();
    for (const auto &col : tab->get_columns()) {
      b.push_back({alias, phys, col.get_name()});
    }
  }
  return b;
}

void sort_unique_result_rows(QueryResult &result) {
  const auto row_less = [](const std::vector<Value> &a,
                           const std::vector<Value> &b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
      if (!(a[i] == b[i])) return a[i] < b[i];
    }
    return a.size() < b.size();
  };
  const auto row_equal = [](const std::vector<Value> &a,
                            const std::vector<Value> &b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) return false;
    }
    return true;
  };
  std::sort(result.rows.begin(), result.rows.end(), row_less);
  const auto last =
      std::unique(result.rows.begin(), result.rows.end(), row_equal);
  result.rows.erase(last, result.rows.end());
}

std::optional<std::string> validate_select_list(
    const std::shared_ptr<SelectStatement> &stmt,
    const SelectExpressionEvaluator &full_ev) {
  const auto isWildcard = [](const ExpressionPtr &e) -> bool {
    auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(e);
    if (col_ref && col_ref->get_column() == "*") return true;
    auto ident = std::dynamic_pointer_cast<IdentifierExpression>(e);
    return ident && ident->get_name() == "*";
  };

  for (const auto &col_entry : stmt->get_select_columns()) {
    if (isWildcard(col_entry.first)) continue;
    if (auto err = full_ev.validate_expression_tree(col_entry.first)) {
      return err;
    }
  }
  return std::nullopt;
}

}  // namespace

JoinSelectExecutor::JoinSelectExecutor(std::shared_ptr<SelectStatement> stmt,
                                       Database *database)
    : stmt_(std::move(stmt)), database_(database) {}

QueryResult JoinSelectExecutor::execute() {
  QueryResult result;
  if (!database_) {
    return QueryResult::error_result("Internal error: no database");
  }
  if (!stmt_) {
    return QueryResult::error_result("Internal error: no statement");
  }

  Table *from_tbl = database_->get_table(stmt_->get_from_table());
  if (!from_tbl) {
    return QueryResult::error_result("Table '" + stmt_->get_from_table() +
                                     "' not found");
  }

  std::vector<Table *> ordered_tables{from_tbl};
  std::vector<std::string> aliases{stmt_->get_from_alias()};

  for (const auto &tup : stmt_->get_joins()) {
    const std::string &join_type = std::get<0>(tup);
    const std::string &join_table_name = std::get<1>(tup);
    const std::string &join_alias = std::get<2>(tup);
    const ExpressionPtr &on_cond = std::get<3>(tup);

    if ((join_type == "LEFT" || join_type == "RIGHT" || join_type == "FULL") &&
        !on_cond) {
      return QueryResult::error_result(join_type + " JOIN requires ON clause");
    }

    Table *jt = database_->get_table(join_table_name);
    if (!jt) {
      return QueryResult::error_result("Table '" + join_table_name +
                                       "' not found");
    }
    ordered_tables.push_back(jt);
    aliases.push_back(join_alias);
  }

  const std::vector<SelectColumnBinding> full_bindings =
      build_bindings_for_tables(ordered_tables, aliases);
  const SelectExpressionEvaluator full_eval(full_bindings);

  /* Per-join ON: only columns visible at that join edge. */
  for (size_t ji = 1; ji < ordered_tables.size(); ++ji) {
    const ExpressionPtr &on_expr = std::get<3>(stmt_->get_joins()[ji - 1]);
    if (!on_expr) continue;
    const size_t prefix_bindings =
        cumulative_columns(ordered_tables, ji);
    std::vector<SelectColumnBinding> slice(
        full_bindings.begin(), full_bindings.begin() + prefix_bindings);
    SelectExpressionEvaluator edge_eval(slice);
    auto err = edge_eval.validate_expression_tree(on_expr);
    if (err) {
      return QueryResult::error_result(*err);
    }
  }

  if (stmt_->get_where_condition()) {
    auto err =
        full_eval.validate_expression_tree(stmt_->get_where_condition());
    if (err) return QueryResult::error_result(*err);
  }

  auto sel_err = validate_select_list(stmt_, full_eval);
  if (sel_err) return QueryResult::error_result(*sel_err);

  /* --- Nested-loop join --- */
  std::vector<std::vector<Value>> current;
  for (const auto &r : from_tbl->get_all_rows()) {
    current.push_back(row_values(r));
  }

  for (size_t ji = 1; ji < ordered_tables.size(); ++ji) {
    Table *rhs = ordered_tables[ji];
    const size_t bindings_through =
        cumulative_columns(ordered_tables, ji);
    std::vector<SelectColumnBinding> edge_bindings(
        full_bindings.begin(), full_bindings.begin() + bindings_through);
    SelectExpressionEvaluator on_eval(edge_bindings);

    const std::string join_type =
        std::get<0>(stmt_->get_joins()[ji - 1]);
    const ExpressionPtr on_expr = std::get<3>(stmt_->get_joins()[ji - 1]);

    const size_t lw = cumulative_columns(ordered_tables, ji - 1);
    const size_t rw = table_width(rhs);
    std::vector<std::vector<Value>> next;

    const bool cartesian =
        join_type == "CROSS" ||
        (join_type == "INNER" && !on_expr);

    if (cartesian) {
      for (const auto &left_row : current) {
        for (const auto &rr : rhs->get_all_rows()) {
          next.push_back(concat_vectors(left_row, row_values(rr)));
        }
      }
    } else if (join_type == "INNER") {
      for (const auto &left_row : current) {
        for (const auto &rr : rhs->get_all_rows()) {
          std::vector<Value> comb = concat_vectors(left_row, row_values(rr));
          Row jr(comb);
          if (on_eval.evaluate_condition(jr, on_expr)) {
            next.push_back(std::move(comb));
          }
        }
      }
    } else if (join_type == "LEFT") {
      for (const auto &left_row : current) {
        bool matched = false;
        for (const auto &rr : rhs->get_all_rows()) {
          std::vector<Value> comb = concat_vectors(left_row, row_values(rr));
          Row jr(comb);
          if (on_eval.evaluate_condition(jr, on_expr)) {
            next.push_back(std::move(comb));
            matched = true;
          }
        }
        if (!matched) {
          next.push_back(concat_vectors(left_row, null_vector(rw)));
        }
      }
    } else if (join_type == "RIGHT") {
      for (const auto &rr : rhs->get_all_rows()) {
        std::vector<Value> rv = row_values(rr);
        bool matched = false;
        for (const auto &left_row : current) {
          std::vector<Value> comb = concat_vectors(left_row, rv);
          Row jr(comb);
          if (on_eval.evaluate_condition(jr, on_expr)) {
            next.push_back(std::move(comb));
            matched = true;
          }
        }
        if (!matched) {
          next.push_back(concat_vectors(null_vector(lw), rv));
        }
      }
    } else if (join_type == "FULL") {
      const auto rhs_rows = rhs->get_all_rows();
      std::vector<char> rhs_matched(rhs_rows.size(), 0);
      for (const auto &left_row : current) {
        bool left_matched = false;
        size_t ri = 0;
        for (const auto &rr : rhs_rows) {
          std::vector<Value> comb = concat_vectors(left_row, row_values(rr));
          Row jr(comb);
          if (on_eval.evaluate_condition(jr, on_expr)) {
            next.push_back(std::move(comb));
            left_matched = true;
            rhs_matched[ri] = 1;
          }
          ++ri;
        }
        if (!left_matched) {
          next.push_back(concat_vectors(left_row, null_vector(rw)));
        }
      }
      size_t ri = 0;
      for (const auto &rr : rhs_rows) {
        if (!rhs_matched[ri]) {
          next.push_back(
              concat_vectors(null_vector(lw), row_values(rr)));
        }
        ++ri;
      }
    } else {
      return QueryResult::error_result("Unsupported JOIN kind: " + join_type);
    }

    current = std::move(next);
  }

  /* WHERE + projection */
  const auto isWildcardColumn = [](const ExpressionPtr &e) -> bool {
    auto col_ref = std::dynamic_pointer_cast<ColumnRefExpression>(e);
    if (col_ref && col_ref->get_column() == "*") return true;
    auto ident = std::dynamic_pointer_cast<IdentifierExpression>(e);
    return ident && ident->get_name() == "*";
  };

  result.success = true;
  result.column_names.clear();
  result.rows.clear();

  const auto &select_cols = stmt_->get_select_columns();
  if (select_cols.empty()) {
    return QueryResult::error_result("No columns selected");
  }

  for (const auto &[expr, al] : select_cols) {
    if (isWildcardColumn(expr)) {
      for (size_t i = 0; i < full_eval.binding_count(); ++i) {
        result.column_names.push_back(full_eval.qualified_header(i));
      }
    } else if (!al.empty()) {
      result.column_names.push_back(al);
    } else if (expr) {
      result.column_names.push_back(expr->to_string());
    }
  }

  for (const auto &flat : current) {
    Row jr(flat);
    if (stmt_->get_where_condition()) {
      if (!full_eval.evaluate_condition(jr, stmt_->get_where_condition())) {
        continue;
      }
    }

    std::vector<Value> out_row;
    for (const auto &col_pair : select_cols) {
      const ExpressionPtr &expr = col_pair.first;
      if (isWildcardColumn(expr)) {
        for (size_t i = 0; i < flat.size(); ++i) {
          out_row.push_back(flat[i]);
        }
      } else {
        out_row.push_back(
            full_eval.evaluate_expression(jr, expr, nullptr));
      }
    }
    result.rows.push_back(std::move(out_row));
  }

  if (stmt_->is_distinct()) {
    sort_unique_result_rows(result);
  }

  result.affected_rows = static_cast<int>(result.rows.size());
  result.message = "SELECT OK";
  return result;
}

}  // namespace db
