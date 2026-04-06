#include "cli/args.hpp"

#include <cassert>
#include <iostream>
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
            "abandon,ability,*,*,abandon,ability,abandon,ability,abandon,ability,abandon,ability",
            "--allow-words",
            "bird,body,bridge",
        };
        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (auto& item : storage) {
            argv.push_back(item.data());
        }

        const auto cfg = cli::parse_args(static_cast<int>(argv.size()), argv.data());
        assert(cfg.template_words.size() == 12);
        assert(cfg.allow_words.size() == 3);
    }

    std::cout << "test_cli_args passed\n";
    return 0;
}
