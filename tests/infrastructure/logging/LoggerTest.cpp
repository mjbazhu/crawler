#include "crawler/infrastructure/observability/logging/Logger.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>

namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;

namespace {

// 测试断言失败时抛出明确错误，避免静默通过。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 读取完整文件内容用于断言日志落盘结果。
std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::in | std::ios::binary);
    require(static_cast<bool>(input), "failed to open log file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void testTextLogFileOutput()
{
    const std::filesystem::path log_path = "build/logger-test-text.log";
    std::filesystem::remove(log_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_DEBUG;
    options.console_enabled = false;
    options.text_file_enabled = true;
    options.text_file_path = log_path.string();
    logging::Logger::instance().configure(std::move(options));
    logging::Logger::instance().log(logging::LogLevel::L_INFO, "LoggerTest", "text log ready");

    const std::string content = readFile(log_path);
    require(content.find("[INFO]") != std::string::npos, "text log should contain level");
    require(content.find("[LoggerTest]") != std::string::npos, "text log should contain component");
    require(content.find("text log ready") != std::string::npos, "text log should contain message");
}

void testTextEventPayloadIsReadableFields()
{
    const std::filesystem::path log_path = "build/logger-test-text-event.log";
    std::filesystem::remove(log_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_DEBUG;
    options.console_enabled = false;
    options.text_file_enabled = true;
    options.text_file_path = log_path.string();
    logging::Logger::instance().configure(std::move(options));
    logging::Logger::instance().logEvent(logging::LogLevel::L_INFO, "ResourceProcess", "resourceClaimStarted", "claim started", R"({"stage":"resource_claim_started","batch_id":"batch-1","limit":1,"statuses":[0,3]})");

    const std::string content = readFile(log_path);
    require(content.find("event=resourceClaimStarted") != std::string::npos, "text event log should contain event type");
    require(content.find("stage=resource_claim_started") != std::string::npos, "text event log should render payload as fields");
    require(content.find("batch_id=batch-1") != std::string::npos, "text event log should render string field");
    require(content.find("limit=1") != std::string::npos, "text event log should render numeric field");
    require(content.find("statuses=[0,3]") != std::string::npos, "text event log should render scalar arrays compactly");
    require(content.find("payload={") == std::string::npos, "text event log should not append raw JSON payload");
    require(content.find(R"({"stage")") == std::string::npos, "text event log should not contain raw JSON object");
}

void testJsonLogFileOutput()
{
    const std::filesystem::path log_path = "build/logger-test-json.log";
    std::filesystem::remove(log_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_DEBUG;
    options.console_enabled = false;
    options.json_file_enabled = true;
    options.json_file_path = log_path.string();
    logging::Logger::instance().configure(std::move(options));
    logging::Logger::instance().logEvent(logging::LogLevel::L_WARN, "ResourceProcess", "resourceClaimStarted", "claim started", R"({"batch_id":"batch-1","limit":1})");

    const std::string content = readFile(log_path);
    require(content.find(R"("level":"WARN")") != std::string::npos, "json log should contain level");
    require(content.find(R"("component":"ResourceProcess")") != std::string::npos, "json log should contain component");
    require(content.find(R"("event_type":"resourceClaimStarted")") != std::string::npos, "json log should contain event type");
    require(content.find(R"("payload":{"batch_id":"batch-1","limit":1})") != std::string::npos, "json log should preserve object payload");
}

void testSplitTextAndJsonFilesWithTextConsole()
{
    const std::filesystem::path text_path = "build/logger-test-split.log";
    const std::filesystem::path json_path = "build/logger-test-split.jsonl";
    std::filesystem::remove(text_path);
    std::filesystem::remove(json_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_DEBUG;
    options.console_enabled = true;
    options.text_file_enabled = true;
    options.text_file_path = text_path.string();
    options.json_file_enabled = true;
    options.json_file_path = json_path.string();
    logging::Logger::instance().configure(std::move(options));

    std::ostringstream captured;
    auto *old_buffer = std::cout.rdbuf(captured.rdbuf());
    logging::Logger::instance().logEvent(logging::LogLevel::L_INFO, "ResourceProcess", "batchFinished", "finished", R"({"trace":"batch:batch-1","batch_id":"batch-1","duration_ms":12})");
    std::cout.rdbuf(old_buffer);

    const std::string console_output = captured.str();
    require(console_output.find("event=batchFinished") != std::string::npos, "console should render event as text");
    require(console_output.find("trace=batch:batch-1") != std::string::npos, "console should render trace field as text");
    require(console_output.find(R"("timestamp")") == std::string::npos, "console should not render json even when json file is enabled");

    const std::string text_content = readFile(text_path);
    require(text_content.find("event=batchFinished") != std::string::npos, "split text file should contain text event");
    require(text_content.find("duration_ms=12") != std::string::npos, "split text file should contain duration field");
    require(text_content.find(R"({"trace")") == std::string::npos, "split text file should not contain raw json object");

    const std::string json_content = readFile(json_path);
    require(json_content.find(R"("event_type":"batchFinished")") != std::string::npos, "split json file should contain event type");
    require(json_content.find(R"("trace":"batch:batch-1")") != std::string::npos, "split json file should preserve trace field");
    require(json_content.find(R"("duration_ms":12)") != std::string::npos, "split json file should preserve duration field");
}

void testConsoleOutput()
{
    logging::Logger::Options options;
    options.level = logging::LogLevel::L_DEBUG;
    options.console_enabled = true;
    logging::Logger::instance().configure(std::move(options));

    std::ostringstream captured;
    auto *old_buffer = std::cout.rdbuf(captured.rdbuf());
    logging::Logger::instance().log(logging::LogLevel::L_INFO, "LoggerTest", "console log ready");
    std::cout.rdbuf(old_buffer);

    const std::string output = captured.str();
    require(output.find("[LoggerTest]") != std::string::npos, "console log should contain component");
    require(output.find("console log ready") != std::string::npos, "console log should contain message");
}

} // namespace

int main()
{
    try {
        testTextLogFileOutput();
        testTextEventPayloadIsReadableFields();
        testJsonLogFileOutput();
        testSplitTextAndJsonFilesWithTextConsole();
        testConsoleOutput();
        logging::Logger::instance().configure(logging::Logger::Options {});
        std::cout << "LoggerTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "LoggerTest failed: " << e.what() << std::endl;
        return 1;
    }
}
