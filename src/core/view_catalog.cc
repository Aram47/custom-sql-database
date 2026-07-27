#include "core/view_catalog.h"

#include <filesystem>
#include <fstream>

#include "utils/exceptions.h"

namespace fs = std::filesystem;

namespace db {

bool ViewCatalog::has_view(const std::string &name) const {
  return views_.count(name) > 0;
}

const ViewDefinition *ViewCatalog::get_view(const std::string &name) const {
  auto it = views_.find(name);
  if (it == views_.end()) {
    return nullptr;
  }
  return it->second.get();
}

std::vector<std::string> ViewCatalog::list_views() const {
  std::vector<std::string> names;
  names.reserve(views_.size());
  for (const auto &[name, def] : views_) {
    (void)def;
    names.push_back(name);
  }
  return names;
}

void ViewCatalog::register_view(std::string name, std::string select_sql) {
  if (views_.count(name)) {
    throw ConstraintException("View '" + name + "' already exists");
  }
  auto definition =
      std::make_unique<ViewDefinition>(name, std::move(select_sql));
  views_.emplace(std::move(name), std::move(definition));
}

void ViewCatalog::unregister_view(const std::string &name) {
  if (!views_.count(name)) {
    throw NotFoundException("View '" + name + "' not found");
  }
  views_.erase(name);
}

std::string ViewCatalog::build_view_path(const std::string &storage_directory,
                                         const std::string &name) {
  return (fs::path(storage_directory) / VIEWS_SUBDIR / (name + VIEW_EXTENSION))
      .string();
}

void ViewCatalog::save_view(const std::string &storage_directory,
                            const std::string &name) const {
  const ViewDefinition *view = get_view(name);
  if (!view) {
    throw NotFoundException("View '" + name + "' not found");
  }
  const fs::path dir = fs::path(storage_directory) / VIEWS_SUBDIR;
  fs::create_directories(dir);
  const std::string path = build_view_path(storage_directory, name);
  const std::string tmp_path = path + ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw StorageException("Cannot write view file: " + tmp_path);
    }
    out << view->get_select_sql();
    out.flush();
    if (!out) {
      throw StorageException("Failed writing view file: " + tmp_path);
    }
  }
  fs::rename(tmp_path, path);
}

void ViewCatalog::remove_view_file(const std::string &storage_directory,
                                   const std::string &name) const {
  const std::string path = build_view_path(storage_directory, name);
  std::error_code ec;
  fs::remove(path, ec);
}

void ViewCatalog::load_all(const std::string &storage_directory) {
  views_.clear();
  const fs::path dir = fs::path(storage_directory) / VIEWS_SUBDIR;
  if (!fs::exists(dir) || !fs::is_directory(dir)) {
    return;
  }
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string filename = entry.path().filename().string();
    const std::string ext = VIEW_EXTENSION;
    if (filename.size() <= ext.size() ||
        filename.compare(filename.size() - ext.size(), ext.size(), ext) != 0) {
      continue;
    }
    const std::string name =
        filename.substr(0, filename.size() - ext.size());
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) {
      throw StorageException("Cannot read view file: " + entry.path().string());
    }
    std::string select_sql((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    while (!select_sql.empty() &&
           (select_sql.back() == '\n' || select_sql.back() == '\r' ||
            select_sql.back() == ';')) {
      select_sql.pop_back();
    }
    register_view(name, std::move(select_sql));
  }
}

}  // namespace db
