#pragma once

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace db {

// PascalCase enumerators avoid Win32 macros (e.g. ERROR from windows.h).
enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
 public:
  static Logger &get_instance() {
    static Logger instance;
    return instance;
  }

  void set_level(LogLevel level) { current_level_ = level; }

  template <typename... Args>
  void debug(const Args &...args) {
    if (current_level_ <= LogLevel::Debug) log(LogLevel::Debug, args...);
  }

  template <typename... Args>
  void info(const Args &...args) {
    if (current_level_ <= LogLevel::Info) log(LogLevel::Info, args...);
  }

  template <typename... Args>
  void warning(const Args &...args) {
    if (current_level_ <= LogLevel::Warning) log(LogLevel::Warning, args...);
  }

  template <typename... Args>
  void error(const Args &...args) {
    if (current_level_ <= LogLevel::Error) log(LogLevel::Error, args...);
  }

 private:
  LogLevel current_level_ = LogLevel::Info;

  Logger() = default;

  std::string get_timestamp() const {
    auto now = std::time(nullptr);
    auto tm = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
  }

  std::string get_level_string(LogLevel level) const {
    switch (level) {
      case LogLevel::Debug:
        return "DEBUG";
      case LogLevel::Info:
        return "INFO";
      case LogLevel::Warning:
        return "WARN";
      case LogLevel::Error:
        return "ERROR";
      default:
        return "UNKNOWN";
    }
  }

  template <typename Arg>
  void log_stream(std::ostringstream &oss, const Arg &arg) {
    oss << arg;
  }

  template <typename First, typename... Rest>
  void log_stream(std::ostringstream &oss, const First &first,
                  const Rest &...rest) {
    oss << first;
    log_stream(oss, rest...);
  }

  template <typename... Args>
  void log(LogLevel level, const Args &...args) {
    std::ostringstream oss;
    log_stream(oss, args...);
    std::cout << "[" << get_timestamp() << "] [" << get_level_string(level)
              << "] " << oss.str() << std::endl;
  }
};

#define DB_LOG_DEBUG(...) db::Logger::get_instance().debug(__VA_ARGS__)
#define DB_LOG_INFO(...) db::Logger::get_instance().info(__VA_ARGS__)
#define DB_LOG_WARNING(...) db::Logger::get_instance().warning(__VA_ARGS__)
#define DB_LOG_ERROR(...) db::Logger::get_instance().error(__VA_ARGS__)

}  // namespace db
