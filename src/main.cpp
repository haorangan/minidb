#include "db/database.hpp"

#include <iostream>
#include <string>

static void Trim(std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end   = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        s.clear();
        return;
    }
    s = s.substr(start, end - start + 1);
}

int main() {
    // Opening the database replays committed work from the WAL
    Database db("data.db", "wal.log");

    std::cout << "[WAL] Recovery complete (" << db.RecoveredTransactions()
              << " committed transaction(s) replayed).\n";
    std::cout << "MiniDB CLI\n";
    std::cout << "Commands:\n";
    std::cout << "  SQL statements ending with ';'\n";
    std::cout << "  BEGIN; ... COMMIT; or ROLLBACK; for multi-statement transactions\n";
    std::cout << "  .exit or .quit to leave\n\n";

    std::string line;
    std::string sql_buffer;

    while (true) {
        // prompt
        if (sql_buffer.empty()) {
            std::cout << (db.InTransaction() ? "minidb*> " : "minidb> ");
        } else {
            std::cout << "   ...> ";
        }

        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        // meta commands (only when not accumulating a multi-line statement)
        if (sql_buffer.empty() && (line == ".exit" || line == ".quit")) {
            break;
        }

        sql_buffer += line;
        sql_buffer += "\n";

        // look for ';' that ends a statement
        auto pos = sql_buffer.find(';');
        if (pos == std::string::npos) {
            continue; // keep reading
        }

        // extract one statement
        std::string statement = sql_buffer.substr(0, pos + 1);
        std::string remainder = sql_buffer.substr(pos + 1);
        Trim(remainder);
        sql_buffer = remainder;

        Trim(statement);
        if (statement.empty()) {
            continue;
        }

        std::cout << "SQL> " << statement << "\n";

        try {
            QueryResult result = db.Execute(statement);
            for (const Tuple &t : result.rows) {
                t.PrintAll();
                std::cout << "\n";
            }
            if (!result.message.empty()) {
                std::cout << result.message;
                if (result.message.back() != '\n') std::cout << "\n";
            }
        } catch (const std::exception &e) {
            std::cout << "Error: " << e.what() << "\n";
        }
    }

    std::cout << "Bye.\n";
    return 0;
}
