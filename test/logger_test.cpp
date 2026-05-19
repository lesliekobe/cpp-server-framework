/*
 * test/logger_test.cpp
 */

#include <iostream>
#include "base/logger/logger.h"

using namespace framework;

int main() {
    Logger::instance()->init("test.log", LogLevel::DEBUG);

    LOG_DEBUG("debug message");
    LOG_INFO("info message");
    LOG_WARN("warn message");
    LOG_ERROR("error message");

    Logger::instance()->flush();
    std::cout << "logger test done, check test.log\n";
    return 0;
}