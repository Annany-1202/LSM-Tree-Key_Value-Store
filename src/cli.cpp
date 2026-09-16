#include "lsm/kv_store.h"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

static std::string toUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return str;
}

static std::vector<std::string> getActiveSSTables(const std::string& db_path) {
    std::vector<std::string> tables;
    std::string manifest_path = db_path + "/manifest.txt";
    if (fs::exists(manifest_path)) {
        std::ifstream mf(manifest_path);
        std::string line;
        while (std::getline(mf, line)) {
            line = trim(line);
            if (!line.empty()) {
                tables.push_back(line);
            }
        }
    }
    return tables;
}

int main(int argc, char** argv) {
    std::string db_path = "./demo_db";
    if (argc > 1) {
        db_path = argv[1];
    }

    lsm::KVStore store;
    lsm::Status s = store.open(db_path);
    if (!s.ok()) {
        std::cerr << "Failed to open database at '" << db_path << "': " << s.toString() << std::endl;
        return 1;
    }

    std::cout << "========================================\n";
    std::cout << "       LSM-Tree Key-Value Store         \n";
    std::cout << "========================================\n";
    std::cout << "Database: " << db_path << "\n";
    std::cout << "Type HELP for commands.\n\n";

    std::string line;
    while (true) {
        std::cout << "annany_lsm> ";
        if (!std::getline(std::cin, line)) {
            break; // EOF
        }

        line = trim(line);
        if (line.empty()) continue;

        // Split first token (command)
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::string cmd_upper = toUpper(cmd);

        if (cmd_upper == "EXIT" || cmd_upper == "QUIT") {
            break;
        } else if (cmd_upper == "HELP") {
            std::cout << "Supported Commands:\n";
            std::cout << "  PUT <key> <value>    Insert or update key-value pair (supports spaces in value)\n";
            std::cout << "  GET <key>            Look up value for key\n";
            std::cout << "  DELETE <key>         Delete key (writes tombstone)\n";
            std::cout << "  FLUSH                Flush MemTable to a new SSTable on disk\n";
            std::cout << "  COMPACT              Merge all active SSTables into a single SSTable\n";
            std::cout << "  STATS                Display active SSTable count and MemTable size\n";
            std::cout << "  HELP                 Show this help message\n";
            std::cout << "  EXIT / QUIT          Close database and exit\n";
        } else if (cmd_upper == "PUT") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "Error: Missing key. Usage: PUT <key> <value>\n";
                continue;
            }

            // Remainder of line is the value
            std::string remaining;
            std::getline(iss, remaining);
            std::string val = trim(remaining);
            if (val.empty()) {
                std::cout << "Error: Missing value. Usage: PUT <key> <value>\n";
                continue;
            }

            lsm::Status put_status = store.put(key, val);
            if (put_status.ok()) {
                std::cout << "OK [WAL appended]\n";
            } else {
                std::cout << "Error: " << put_status.toString() << "\n";
            }
        } else if (cmd_upper == "GET") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "Error: Missing key. Usage: GET <key>\n";
                continue;
            }

            lsm::Result r = store.get(key);
            if (r.ok()) {
                std::cout << r.value << "\n";
            } else if (r.status.isNotFound()) {
                std::cout << "NOT_FOUND\n";
            } else {
                std::cout << "Error: " << r.status.toString() << "\n";
            }
        } else if (cmd_upper == "DELETE") {
            std::string key;
            if (!(iss >> key)) {
                std::cout << "Error: Missing key. Usage: DELETE <key>\n";
                continue;
            }

            lsm::Status del_status = store.remove(key);
            if (del_status.ok()) {
                std::cout << "OK [TOMBSTONE written to WAL/MemTable]\n";
            } else {
                std::cout << "Error: " << del_status.toString() << "\n";
            }
        } else if (cmd_upper == "FLUSH") {
            std::vector<std::string> before_ssts = getActiveSSTables(db_path);

            lsm::Status flush_status = store.flush();
            if (!flush_status.ok()) {
                std::cout << "Error: " << flush_status.toString() << "\n";
                continue;
            }

            std::vector<std::string> after_ssts = getActiveSSTables(db_path);
            if (after_ssts.size() > before_ssts.size()) {
                std::string new_sst = after_ssts.back();
                std::cout << "FLUSH complete\n";
                std::cout << "SSTable created: " << new_sst << "\n";
                std::cout << "Active SSTables: " << after_ssts.size() << "\n";
            } else {
                std::cout << "FLUSH complete (MemTable was empty, no new SSTable created)\n";
            }
        } else if (cmd_upper == "COMPACT") {
            std::vector<std::string> before_ssts = getActiveSSTables(db_path);
            if (before_ssts.size() < 2) {
                std::cout << "Compaction skipped: requires at least 2 SSTables (currently "
                          << before_ssts.size() << ")\n";
                continue;
            }

            std::cout << "Compaction started\n";
            std::cout << "Input SSTables: ";
            for (size_t i = 0; i < before_ssts.size(); ++i) {
                std::cout << before_ssts[i] << (i + 1 < before_ssts.size() ? ", " : "\n");
            }

            lsm::Status compact_status = store.compact();
            if (!compact_status.ok()) {
                std::cout << "Error: " << compact_status.toString() << "\n";
                continue;
            }

            std::vector<std::string> after_ssts = getActiveSSTables(db_path);
            std::string out_sst = after_ssts.empty() ? "(none)" : after_ssts.front();
            std::cout << "Output SSTable: " << out_sst << "\n";
            std::cout << "Old SSTables removed: ";
            for (size_t i = 0; i < before_ssts.size(); ++i) {
                std::cout << before_ssts[i] << (i + 1 < before_ssts.size() ? ", " : "\n");
            }
            std::cout << "Compaction complete\n";
        } else if (cmd_upper == "STATS") {
            std::vector<std::string> ssts = getActiveSSTables(db_path);
            std::cout << "Database Path:       " << db_path << "\n";
            std::cout << "Active SSTables:     " << ssts.size() << "\n";
            for (const auto& sst : ssts) {
                std::cout << "  - " << sst << "\n";
            }
            std::cout << "MemTable Approx Size:" << store.memtableSize() << " bytes\n";
        } else {
            std::cout << "Unknown command: '" << cmd << "'. Type HELP for available commands.\n";
        }
    }

    store.close();
    std::cout << "Database closed. Goodbye!\n";
    return 0;
}
