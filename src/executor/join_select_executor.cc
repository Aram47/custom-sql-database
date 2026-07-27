#include "executor/join_select_executor.h"

#include <algorithm>

#include "core/database.h"
#include "core/session_context.h"
#include "executor/group_aggregate_operator.h"
#include "executor/index_predicate.h"
#include "executor/select_analysis.h"
#include "executor/select_expression_evaluator.h"
#include "executor/select_pipeline.h"
#include "planner/join_order_planner.h"

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

  bool all_inner_or_cross = true;
  for (const auto &tup : stmt_->get_joins()) {
    const std::string &join_type = std::get<0>(tup);
    if (join_type != "INNER" && join_type != "CROSS") {
      all_inner_or_cross = false;
      break;
    }
  }
  if (all_inner_or_cross && ordered_tables.size() == 2) {
    std::vector<JoinRelation> relations(2);
    relations[0].table_name = ordered_tables[0]->get_name();
    relations[0].estimated_rows = ordered_tables[0]->get_row_count();
    relations[1].table_name = ordered_tables[1]->get_name();
    relations[1].estimated_rows = ordered_tables[1]->get_row_count();
    const ExpressionPtr &on_expr = std::get<3>(stmt_->get_joins()[0]);
    std::string left_table;
    std::string left_column;
    std::string right_table;
    std::string right_column;
    if (on_expr && try_extract_equi_join_columns(on_expr, left_table,
                                                 left_column, right_table,
                                                 right_column)) {
      relations[1].has_indexed_equi_join =
          ordered_tables[1]->has_index(right_column) ||
          ordered_tables[1]->has_index(left_column);
      relations[0].has_indexed_equi_join =
          ordered_tables[0]->has_index(right_column) ||
          ordered_tables[0]->has_index(left_column);
    }
    const std::vector<size_t> order =
        JoinOrderPlanner::planLeftDeepOrder(relations);
    if (order.size() == 2 && order[0] == 1) {
      std::swap(ordered_tables[0], ordered_tables[1]);
      std::swap(aliases[0], aliases[1]);
      from_tbl = ordered_tables[0];
    }
  }

  const std::vector<SelectColumnBinding> full_bindings =
      build_bindings_for_tables(ordered_tables, aliases);
  SelectExpressionEvaluator full_eval(full_bindings);
  if (database_) {
    full_eval.set_correlation_context(database_->get_correlation_context());
    full_eval.set_bind_context(database_->get_active_bind());
  }
  bind_subquery_evaluators(full_eval, database_);

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

  if (auto grouping_err = validate_select_for_grouping(stmt_)) {
    return QueryResult::error_result(*grouping_err);
  }

  /* --- Nested-loop join --- */
  std::vector<std::vector<Value>> current;
  SessionContext *session =
      database_ ? database_->get_active_session() : nullptr;
  const auto load_visible = [&](Table *tbl) {
    if (!database_) {
      return tbl->get_all_rows();
    }
    return tbl->get_visible_rows(database_->get_transaction_manager(),
                                 database_->get_reader_xid(session),
                                 database_->get_reader_snapshot(session));
  };
  for (const auto &r : load_visible(from_tbl)) {
    current.push_back(row_values(r));
  }

  for (size_t ji = 1; ji < ordered_tables.size(); ++ji) {
    Table *rhs = ordered_tables[ji];
    const std::vector<Row> rhs_rows = load_visible(rhs);
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
        for (const auto &rr : rhs_rows) {
          next.push_back(concat_vectors(left_row, row_values(rr)));
        }
      }
    } else if (join_type == "INNER") {
      std::string left_table;
      std::string left_column;
      std::string right_table;
      std::string right_column;
      const bool is_equi =
          on_expr && try_extract_equi_join_columns(on_expr, left_table,
                                                   left_column, right_table,
                                                   right_column);
      const std::string &rhs_alias = aliases[ji];
      const std::string &lhs_alias = aliases[ji - 1];
      int rhs_col_idx = -1;
      int lhs_col_in_combined = -1;
      bool use_rhs_index = false;
      if (is_equi) {
        const bool right_is_rhs =
            (right_table.empty() || right_table == rhs->get_name() ||
             right_table == rhs_alias) &&
            rhs->get_column_index(right_column) >= 0;
        const bool left_is_rhs =
            (left_table.empty() || left_table == rhs->get_name() ||
             left_table == rhs_alias) &&
            rhs->get_column_index(left_column) >= 0;
        if (right_is_rhs && rhs->has_index(right_column)) {
          rhs_col_idx = rhs->get_column_index(right_column);
          const std::string probe_col = left_column;
          for (size_t bi = 0; bi < edge_bindings.size(); ++bi) {
            if (edge_bindings[bi].column_name == probe_col &&
                (left_table.empty() ||
                 edge_bindings[bi].alias == left_table ||
                 edge_bindings[bi].physical_table == left_table ||
                 edge_bindings[bi].alias == lhs_alias)) {
              lhs_col_in_combined = static_cast<int>(bi);
              break;
            }
          }
          if (lhs_col_in_combined < 0) {
            for (size_t bi = 0; bi < edge_bindings.size(); ++bi) {
              if (edge_bindings[bi].column_name == probe_col) {
                lhs_col_in_combined = static_cast<int>(bi);
                break;
              }
            }
          }
          use_rhs_index = lhs_col_in_combined >= 0 && rhs_col_idx >= 0;
          if (use_rhs_index) {
            (void)rhs_col_idx;
          }
        } else if (left_is_rhs && rhs->has_index(left_column)) {
          rhs_col_idx = rhs->get_column_index(left_column);
          const std::string probe_col = right_column;
          for (size_t bi = 0; bi < edge_bindings.size(); ++bi) {
            if (edge_bindings[bi].column_name == probe_col) {
              lhs_col_in_combined = static_cast<int>(bi);
              break;
            }
          }
          use_rhs_index = lhs_col_in_combined >= 0 && rhs_col_idx >= 0;
        }
      }
      if (use_rhs_index) {
        const std::string indexed_col =
            rhs->get_column(static_cast<size_t>(rhs_col_idx)).get_name();
        for (const auto &left_row : current) {
          const Value &key =
              left_row[static_cast<size_t>(lhs_col_in_combined)];
          for (size_t ri : rhs->find_rows_by_value(indexed_col, key)) {
            std::vector<Value> comb =
                concat_vectors(left_row, row_values(rhs->get_row(ri)));
            Row jr(comb);
            if (on_eval.evaluate_condition(jr, on_expr)) {
              next.push_back(std::move(comb));
            }
          }
        }
      } else {
        for (const auto &left_row : current) {
          for (const auto &rr : rhs_rows) {
            std::vector<Value> comb = concat_vectors(left_row, row_values(rr));
            Row jr(comb);
            if (on_eval.evaluate_condition(jr, on_expr)) {
              next.push_back(std::move(comb));
            }
          }
        }
      }
    } else if (join_type == "LEFT") {
      std::string left_table;
      std::string left_column;
      std::string right_table;
      std::string right_column;
      const bool is_equi =
          on_expr && try_extract_equi_join_columns(on_expr, left_table,
                                                   left_column, right_table,
                                                   right_column);
      int rhs_col_idx = -1;
      int lhs_col_in_combined = -1;
      bool use_rhs_index = false;
      if (is_equi) {
        const std::string &rhs_alias = aliases[ji];
        if ((right_table.empty() || right_table == rhs->get_name() ||
             right_table == rhs_alias) &&
            rhs->has_index(right_column)) {
          rhs_col_idx = rhs->get_column_index(right_column);
          for (size_t bi = 0; bi < edge_bindings.size(); ++bi) {
            if (edge_bindings[bi].column_name == left_column) {
              lhs_col_in_combined = static_cast<int>(bi);
              break;
            }
          }
          use_rhs_index = lhs_col_in_combined >= 0;
        } else if ((left_table.empty() || left_table == rhs->get_name() ||
                    left_table == rhs_alias) &&
                   rhs->has_index(left_column)) {
          rhs_col_idx = rhs->get_column_index(left_column);
          for (size_t bi = 0; bi < edge_bindings.size(); ++bi) {
            if (edge_bindings[bi].column_name == right_column) {
              lhs_col_in_combined = static_cast<int>(bi);
              break;
            }
          }
          use_rhs_index = lhs_col_in_combined >= 0;
        }
      }
      if (use_rhs_index) {
        const std::string indexed_col =
            rhs->get_column(static_cast<size_t>(rhs_col_idx)).get_name();
        for (const auto &left_row : current) {
          bool matched = false;
          const Value &key =
              left_row[static_cast<size_t>(lhs_col_in_combined)];
          for (size_t ri : rhs->find_rows_by_value(indexed_col, key)) {
            std::vector<Value> comb =
                concat_vectors(left_row, row_values(rhs->get_row(ri)));
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
      } else {
        for (const auto &left_row : current) {
          bool matched = false;
          for (const auto &rr : rhs_rows) {
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
      }
    } else if (join_type == "RIGHT") {
      for (const auto &rr : rhs_rows) {
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

  std::vector<Row> filtered_rows;
  filtered_rows.reserve(current.size());
  for (const auto &flat : current) {
    Row jr(flat);
    if (stmt_->get_where_condition() &&
        !full_eval.evaluate_condition(jr, stmt_->get_where_condition())) {
      continue;
    }
    filtered_rows.push_back(std::move(jr));
  }

  if (needs_grouping(stmt_)) {
    GroupAggregateOperator grouping(stmt_, full_eval);
    QueryResult grouped = grouping.apply(filtered_rows);
    if (!grouped.success) return grouped;
    return SelectPipeline::apply_post_scan(std::move(grouped), stmt_, full_eval);
  }

  const auto &select_cols = stmt_->get_select_columns();
  if (select_cols.empty()) {
    return QueryResult::error_result("No columns selected");
  }

  result.success = true;
  result.column_names.clear();
  result.rows.clear();

  for (const auto &[expr, al] : select_cols) {
    if (is_wildcard_select_expression(expr)) {
      for (size_t i = 0; i < full_eval.binding_count(); ++i) {
        result.column_names.push_back(full_eval.qualified_header(i));
      }
    } else if (!al.empty()) {
      result.column_names.push_back(al);
    } else if (expr) {
      result.column_names.push_back(expr->to_string());
    }
  }

  for (const auto &jr : filtered_rows) {
    std::vector<Value> out_row;
    for (const auto &col_pair : select_cols) {
      const ExpressionPtr &expr = col_pair.first;
      if (is_wildcard_select_expression(expr)) {
        for (size_t i = 0; i < jr.get_column_count(); ++i) {
          out_row.push_back(jr.get_value(i));
        }
      } else {
        out_row.push_back(full_eval.evaluate_expression(jr, expr, nullptr));
      }
    }
    result.rows.push_back(std::move(out_row));
  }

  result.affected_rows = static_cast<int>(result.rows.size());
  result.message = "SELECT OK";
  return SelectPipeline::apply_post_scan(std::move(result), stmt_, full_eval);
}

}  // namespace db
