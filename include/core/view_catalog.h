#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/view_definition.h"

namespace db {

/** Catalog of view definitions with SQL-text persistence under `_views/`. */
class ViewCatalog {
 public:
  static constexpr const char *VIEWS_SUBDIR = "_views";
  static constexpr const char *VIEW_EXTENSION = ".view";

  bool has_view(const std::string &name) const;
  const ViewDefinition *get_view(const std::string &name) const;
  std::vector<std::string> list_views() const;

  void register_view(std::string name, std::string select_sql);
  void unregister_view(const std::string &name);

  void save_view(const std::string &storage_directory,
                 const std::string &name) const;
  void remove_view_file(const std::string &storage_directory,
                        const std::string &name) const;
  void load_all(const std::string &storage_directory);

 private:
  std::map<std::string, std::unique_ptr<ViewDefinition>> views_;

  static std::string build_view_path(const std::string &storage_directory,
                                     const std::string &name);
};

}  // namespace db
