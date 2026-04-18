#include "cli/args.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main() {
    {
        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon ability * * abandon ability abandon ability abandon ability abandon ability",
            "--allow-words",
            "bird body bridge",
            "--postgres-conn",
            "postgresql://postgres:postgres@localhost:5432/recovery",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.template_words.size() == 12);
        assert(cfg.template_words[0] == "abandon");
        assert(cfg.template_words[1] == "ability");
        assert(cfg.template_words[2] == "*");
        assert(cfg.template_words[3] == "*");
        assert(cfg.allow_words.size() == 3);
        assert(cfg.allow_words[0] == "bird");
        assert(cfg.allow_words[1] == "body");
        assert(cfg.allow_words[2] == "bridge");
    }

    {
        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--chains",
            "btc,eth,sol",
            "--postgres-conn",
            "postgresql://postgres:postgres@localhost:5432/recovery",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.template_words.size() == 12);
        assert(cfg.chains.size() == 3);
        assert(cfg.chains[0] == "btc");
    }

    {
        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--postgres-conn",
            "postgresql://postgres:postgres@localhost:5432/recovery",
            "--postgres-result-table-btc",
            "wallet_hits_btc",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.postgres_conninfo == "postgresql://postgres:postgres@localhost:5432/recovery");
        assert(cfg.postgres_result_table_btc == "wallet_hits_btc");
    }

    {
        const std::string env_file = "test_cli_args.env";
        {
            std::ofstream out(env_file);
            out << "RECOVERY_POSTGRES_CONN=postgresql://env:env@localhost:5432/envdb\n";
            out << "RECOVERY_POSTGRES_RESULT_TABLE_BTC=env_wallet_hits_btc\n";
        }

        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--env-file",
            env_file,
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.postgres_conninfo == "postgresql://env:env@localhost:5432/envdb");
        assert(cfg.postgres_result_table_btc == "env_wallet_hits_btc");
        std::remove(env_file.c_str());
    }

    std::cout << "test_cli_args passed\n";
    return 0;
}
