#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace sql = orchestrator::modules::crawler::infrastructure::music::persistence::sql;
namespace {
// 断言脚本行为，失败时给出固定诊断而不打印迁移正文。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 不完整脚本必须在执行任何语句之前被整个拒绝。
void rejected(const std::string &script)
{
    bool failed = false;
    try {
        (void)sql::SqlFileLoader::splitMigrationStatements(script);
    } catch (const std::runtime_error &) {
        failed = true;
    }
    require(failed, "invalid migration script was accepted");
}
} // namespace

// 验证复合语句、客户端分隔、引号/注释及全部历史迁移的分句数量。
int main()
{
    try {
        const auto statements = sql::SqlFileLoader::splitMigrationStatements(R"SQL(
-- DELIMITER is inert in comments
SELECT 'a;$$', "b;//", `semi;column`;
DELIMITER $$
CREATE TRIGGER sample BEFORE INSERT ON sample_table FOR EACH ROW
BEGIN
    IF NEW.value IS NULL THEN
        SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='quote '' and ; $$ literal';
    END IF;
    /* $$ is inert here */ SET NEW.value=1;
END$$
delimiter ;
SELECT 2;
)SQL");
        require(statements.size() == 3, "compound trigger was split or delimiter was sent to server");
        require(statements[1].starts_with("CREATE TRIGGER") && statements[1].ends_with("END"), "trigger body was changed");
        require(statements[1].find("END IF;") != std::string::npos, "compound semicolon was lost");
        const auto escaped = sql::SqlFileLoader::splitMigrationStatements("SELECT 'escaped\\\';still quoted', `a``;b`; SELECT 3");
        require(escaped.size() == 2, "escaped quote or quoted identifier split incorrectly");
        require(sql::SqlFileLoader::splitMigrationStatements("\xef\xbb\xbf"
                                                             "DELIMITER //\nSELECT 1//\nDELIMITER ;\n")
                        .size()
                    == 1,
                "BOM or slash delimiter failed");
        rejected("SELECT 1\nDELIMITER $$\n");
        rejected("DELIMITER\n");
        rejected("DELIMITER word\nSELECT 1word");
        rejected("DELIMITER $$\nBEGIN SELECT 1; END");
        rejected("SELECT 1; SELECT 'unterminated");
        rejected("SELECT 1; /* unterminated");
        int historical = 0;
        for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations")) {
            if (entry.path().extension() != ".sql" || entry.path().filename().string() > "V2026102901__zz.sql") continue;
            const auto script = sql::SqlFileLoader::loadOrThrow(entry.path().string());
            require(sql::SqlFileLoader::splitStatements(script).size() == sql::SqlFileLoader::splitMigrationStatements(script).size(), "historical migration statement count changed");
            ++historical;
        }
        require(historical >= 33, "historical migration coverage is incomplete");
        std::cout << "PASS: compound migrations, quoted delimiters, rejected malformed scripts and " << historical << " historical migrations\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
