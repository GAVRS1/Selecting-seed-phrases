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
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--allow-words",
            "bird,body,bridge",
            "--paths-ton",
            "m/44'/607'/0'/{i}'",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.template_words.size() == 24);
        assert(cfg.allow_words.size() == 3);
        assert(cfg.paths_ton.size() == 1);
        assert(cfg.paths_ton[0] == "m/44'/607'/0'/{i}'");
    }

    {
        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--chains",
            "ton",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        bool threw = false;
        try {
            static_cast<void>(cli::parse_args(static_cast<int>(argv.size()), argv.data()));
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    }

    {
        std::vector<std::string> storage{
            "recovery_tool",
            "--template",
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--postgres-conn",
            "postgresql://postgres:postgres@localhost:5432/recovery",
            "--postgres-table",
            "wallet_hits",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.postgres_conninfo == "postgresql://postgres:postgres@localhost:5432/recovery");
        assert(cfg.postgres_table == "wallet_hits");
    }

    {
        const std::string env_file = "test_cli_args.env";
        {
            std::ofstream out(env_file);
            out << "RECOVERY_POSTGRES_CONN=postgresql://env:env@localhost:5432/envdb\n";
            out << "RECOVERY_POSTGRES_TABLE=env_wallet_hits\n";
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
        assert(cfg.postgres_table == "env_wallet_hits");
        std::remove(env_file.c_str());
    }

    std::cout << "test_cli_args passed\n";
    return 0;
}
