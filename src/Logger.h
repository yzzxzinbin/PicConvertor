#pragma once
#ifndef PICCONVERTOR_LOGGER_H
#define PICCONVERTOR_LOGGER_H

#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip> 
#include <windows.h> 


namespace PicConvertor {

    enum class LogLevel {
        LOG_INFO,
        LOG_WARNING,
        LOG_ERROR
    };

    class Logger {
    public:
        // 获取 Logger 单例实例
        static Logger& getInstance();

        // 初始化日志文件 (应在程序开始时调用)
        bool initialize(const std::string& filename = "picconvertor.log");

        // 关闭日志文件 (应在程序结束时调用)
        void shutdown();

        // 记录日志消息
        void log(LogLevel level, const std::string& message);

        // 便捷方法
        void logInfo(const std::string& message);
        void logWarning(const std::string& message);
        void logError(const std::string& message);

        // 禁用拷贝和赋值
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        Logger() = default; // 私有构造函数
        ~Logger() = default; // 私有析构函数 (通过 shutdown 管理)

        std::ofstream logFile;
        std::mutex logMutex;
        bool initialized = false;

        // 获取当前时间戳字符串
        std::string getCurrentTimestamp();
        // 获取日志级别字符串
        std::string levelToString(LogLevel level);
    };

    // 全局便捷访问宏 (可选，但方便)
    #define LOG_INFO(msg)    PicConvertor::Logger::getInstance().logInfo(msg)
    #define LOG_WARNING(msg) PicConvertor::Logger::getInstance().logWarning(msg)
    #define LOG_ERROR(msg)   PicConvertor::Logger::getInstance().logError(msg)

    // 兼容性宏（用于改造自其他项目的日志调用）
    #define PC_LOG_INFO(msg)    LOG_INFO(msg)
    #define PC_LOG_WARNING(msg) LOG_WARNING(msg)
    #define PC_LOG_ERROR(msg)   LOG_ERROR(msg)

} // namespace PicConvertor

#endif // PICCONVERTOR_LOGGER_H
