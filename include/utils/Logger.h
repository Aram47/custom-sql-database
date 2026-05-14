#pragma once

#include <iostream>
#include <string>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace db
{

	enum class LogLevel
	{
		DEBUG,
		INFO,
		WARNING,
		ERROR
	};

	class Logger
	{
	public:
		static Logger &getInstance()
		{
			static Logger instance;
			return instance;
		}

		void setLevel(LogLevel level)
		{
			currentLevel = level;
		}

		template <typename... Args>
		void debug(const Args &...args)
		{
			if (currentLevel <= LogLevel::DEBUG)
				log(LogLevel::DEBUG, args...);
		}

		template <typename... Args>
		void info(const Args &...args)
		{
			if (currentLevel <= LogLevel::INFO)
				log(LogLevel::INFO, args...);
		}

		template <typename... Args>
		void warning(const Args &...args)
		{
			if (currentLevel <= LogLevel::WARNING)
				log(LogLevel::WARNING, args...);
		}

		template <typename... Args>
		void error(const Args &...args)
		{
			if (currentLevel <= LogLevel::ERROR)
				log(LogLevel::ERROR, args...);
		}

	private:
		LogLevel currentLevel = LogLevel::INFO;

		Logger() = default;

		std::string getTimestamp() const
		{
			auto now = std::time(nullptr);
			auto tm = std::localtime(&now);
			std::ostringstream oss;
			oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
			return oss.str();
		}

		std::string getLevelString(LogLevel level) const
		{
			switch (level)
			{
			case LogLevel::DEBUG:
				return "DEBUG";
			case LogLevel::INFO:
				return "INFO";
			case LogLevel::WARNING:
				return "WARN";
			case LogLevel::ERROR:
				return "ERROR";
			default:
				return "UNKNOWN";
			}
		}

		template <typename Arg>
		void logStream(std::ostringstream &oss, const Arg &arg)
		{
			oss << arg;
		}

		template <typename First, typename... Rest>
		void logStream(std::ostringstream &oss, const First &first, const Rest &...rest)
		{
			oss << first;
			logStream(oss, rest...);
		}

		template <typename... Args>
		void log(LogLevel level, const Args &...args)
		{
			std::ostringstream oss;
			logStream(oss, args...);
			std::cout << "[" << getTimestamp() << "] [" << getLevelString(level) << "] "
								<< oss.str() << std::endl;
		}
	};

#define DB_LOG_DEBUG(...) db::Logger::getInstance().debug(__VA_ARGS__)
#define DB_LOG_INFO(...) db::Logger::getInstance().info(__VA_ARGS__)
#define DB_LOG_WARNING(...) db::Logger::getInstance().warning(__VA_ARGS__)
#define DB_LOG_ERROR(...) db::Logger::getInstance().error(__VA_ARGS__)

} // namespace db
