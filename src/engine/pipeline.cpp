#include "engine/pipeline.hpp"

#include "core/secure_buffer.hpp"
#include "core/thread_pool.hpp"

#include <openssl/evp.h>

#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdlib>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace engine {

namespace {
core::SecureBuffer mnemonic_to_seed(const core::Mnemonic& mnemonic, const std::string& passphrase) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < mnemonic.size(); ++i) {
        joined << mnemonic[i];
        if (i + 1 < mnemonic.size()) {
            joined << ' ';
        }
    }

    const std::string phrase = joined.str();
    const std::string salt = "mnemonic"; // empty passphrase for educational skeleton

    std::vector<std::uint8_t> seed(64, 0);
    const int ok = PKCS5_PBKDF2_HMAC(
        phrase.c_str(),
        static_cast<int>(phrase.size()),
        reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size()),
        2048,
        EVP_sha512(),
        static_cast<int>(seed.size()),
        seed.data());

    if (ok != 1) {
        throw std::runtime_error("Failed to derive BIP-39 seed");
    }

    return core::SecureBuffer(std::move(seed));
}

std::vector<std::string> paths_for_module(const core::AppConfig& cfg, const std::string& name) {
    if (name == "btc") return cfg.paths_btc;
    if (name == "eth") return cfg.paths_eth;
    if (name == "sol") return cfg.paths_sol;
    if (name == "ton") return cfg.paths_ton;
    return {};
}

std::string mnemonic_to_string(const core::Mnemonic& mnemonic) {
    std::ostringstream joined;
    for (std::size_t i = 0; i < mnemonic.size(); ++i) {
        joined << mnemonic[i];
        if (i + 1 < mnemonic.size()) {
            joined << ' ';
        }
    }
    return joined.str();
}

std::string trim_copy(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string escape_sql_literal(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '\'') {
            out += "''";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string wallet_record_key(const std::string& chain_name,
                              const std::string& address,
                              const std::string& mnemonic_words) {
    return chain_name + "|" + address + "|" + mnemonic_words;
}

std::string shell_quote_single(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('\'');
    return out;
}

bool is_valid_sql_identifier(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    const auto is_ident_char = [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_';
    };
    if (!std::isalpha(static_cast<unsigned char>(value.front())) && value.front() != '_') {
        return false;
    }
    return std::all_of(value.begin() + 1, value.end(), is_ident_char);
}

void exec_psql_command(const std::string& conninfo, const std::string& sql) {
    const std::string cmd = "psql " + shell_quote_single(conninfo) +
                            " -v ON_ERROR_STOP=1 -q -c " + shell_quote_single(sql);
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        throw std::runtime_error("Failed to execute PostgreSQL command via psql. Exit code: " + std::to_string(rc));
    }
}

std::unordered_set<std::string> load_seen_mnemonics(const std::string& path) {
    std::unordered_set<std::string> seen;
    std::ifstream in(path);
    if (!in) {
        return seen;
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::string clean = trim_copy(line);
        if (!clean.empty()) {
            seen.insert(clean);
        }
    }
    return seen;
}

bool claim_seen_mnemonic(const std::string& path, const std::string& mnemonic_words) {
    if (!path.empty()) {
        std::filesystem::path file_path(path);
        if (file_path.has_parent_path()) {
            std::filesystem::create_directories(file_path.parent_path());
        }
    }

#ifdef _WIN32
    const int fd = ::_open(path.c_str(), _O_RDWR | _O_CREAT, _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
#endif
    if (fd < 0) {
        throw std::runtime_error("Failed to open seen mnemonics file: " + path);
    }

    auto close_fd = [&fd]() {
        if (fd >= 0) {
#ifdef _WIN32
            ::_close(fd);
#else
            ::close(fd);
#endif
        }
    };

#ifdef _WIN32
    HANDLE file_handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    if (file_handle == INVALID_HANDLE_VALUE) {
        close_fd();
        throw std::runtime_error("Failed to get file handle for seen mnemonics file: " + path);
    }

    OVERLAPPED lock_region{};
    if (!::LockFileEx(file_handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &lock_region)) {
        close_fd();
        throw std::runtime_error("Failed to lock seen mnemonics file: " + path);
    }
#else
    if (::flock(fd, LOCK_EX) != 0) {
        close_fd();
        throw std::runtime_error("Failed to lock seen mnemonics file: " + path);
    }
#endif

    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (trim_copy(line) == mnemonic_words) {
#ifdef _WIN32
            ::UnlockFileEx(file_handle, 0, MAXDWORD, MAXDWORD, &lock_region);
#else
            ::flock(fd, LOCK_UN);
#endif
            close_fd();
            return false;
        }
    }

#ifdef _WIN32
    if (::_lseeki64(fd, 0, SEEK_END) < 0) {
        ::UnlockFileEx(file_handle, 0, MAXDWORD, MAXDWORD, &lock_region);
        close_fd();
        throw std::runtime_error("Failed to seek seen mnemonics file: " + path);
    }
#else
    if (::lseek(fd, 0, SEEK_END) < 0) {
        ::flock(fd, LOCK_UN);
        close_fd();
        throw std::runtime_error("Failed to seek seen mnemonics file: " + path);
    }
#endif

    const std::string payload = mnemonic_words + '\n';
#ifdef _WIN32
    const int written = ::_write(fd, payload.data(), static_cast<unsigned int>(payload.size()));
    if (written != static_cast<int>(payload.size())) {
        ::UnlockFileEx(file_handle, 0, MAXDWORD, MAXDWORD, &lock_region);
        close_fd();
        throw std::runtime_error("Failed to append seen mnemonics file: " + path);
    }
    ::UnlockFileEx(file_handle, 0, MAXDWORD, MAXDWORD, &lock_region);
#else
    const ssize_t written = ::write(fd, payload.data(), payload.size());
    if (written != static_cast<ssize_t>(payload.size())) {
        ::flock(fd, LOCK_UN);
        close_fd();
        throw std::runtime_error("Failed to append seen mnemonics file: " + path);
    }

    ::flock(fd, LOCK_UN);
#endif
    close_fd();
    return true;
}

class SeenFileLockGuard {
public:
    explicit SeenFileLockGuard(std::string lock_path) : lock_path_(std::move(lock_path)) {
        while (!std::filesystem::create_directory(lock_path_)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    ~SeenFileLockGuard() {
        std::error_code ec;
        std::filesystem::remove_all(lock_path_, ec);
    }

private:
    std::string lock_path_;
};

bool mark_seen_mnemonic_atomic(const std::string& path,
                               const std::string& mnemonic_words,
                               std::unordered_set<std::string>& local_seen_cache) {
    if (local_seen_cache.contains(mnemonic_words)) {
        return false;
    }

    SeenFileLockGuard lock(path + ".lock");
    auto latest_seen = load_seen_mnemonics(path);
    if (latest_seen.contains(mnemonic_words)) {
        local_seen_cache.insert(mnemonic_words);
        return false;
    }

    claim_seen_mnemonic(path, mnemonic_words);
    local_seen_cache.insert(mnemonic_words);
    return true;
}

} // namespace

Pipeline::Pipeline(const core::AppConfig& config,
                   const bip39::MnemonicValidator& validator,
                   const bip39::MnemonicGenerator& generator,
                   Matcher& matcher)
    : config_(config), validator_(validator), generator_(generator), matcher_(matcher) {}

void Pipeline::register_chain(std::unique_ptr<chains::IChainModule> module) {
    modules_.push_back(std::move(module));
}

bool Pipeline::is_chain_recovered(const std::string& chain_name) {
    std::lock_guard<std::mutex> lock(recovered_mutex_);
    return recovered_chains_.contains(chain_name);
}

void Pipeline::mark_chain_recovered(const std::string& chain_name) {
    std::lock_guard<std::mutex> lock(recovered_mutex_);
    recovered_chains_.insert(chain_name);
}

void Pipeline::print_console_header() {
    bool expected = false;
    if (!console_header_printed_.compare_exchange_strong(expected, true)) {
        return;
    }

    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cout << "WALLET || ADDRESS || SEED (12 WORDS)\n";
}

void Pipeline::print_console_row(const std::string& chain_name,
                                 const std::string& address,
                                 const std::string& mnemonic_words) {
    std::lock_guard<std::mutex> lock(console_mutex_);
    std::cout << chain_name << " || " << address << " || " << mnemonic_words << '\n';
}

bool Pipeline::wallet_record_exists(const std::string& chain_name,
                                    const std::string& address,
                                    const std::string& mnemonic_words) const {
    const std::string key = wallet_record_key(chain_name, address, mnemonic_words);
    {
        std::lock_guard<std::mutex> lock(wallet_cache_mutex_);
        if (known_wallet_records_.contains(key)) {
            return true;
        }
    }

    if (!config_.postgres_conninfo.empty()) {
        if (!is_valid_sql_identifier(config_.postgres_table)) {
            throw std::runtime_error("Invalid PostgreSQL table name: " + config_.postgres_table);
        }
        std::call_once(postgres_init_once_, [&]() {
            const std::string ddl =
                "CREATE TABLE IF NOT EXISTS " + config_.postgres_table + " ("
                "id BIGSERIAL PRIMARY KEY, "
                "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
                "blockchain TEXT NOT NULL, "
                "address TEXT NOT NULL, "
                "mnemonic TEXT NOT NULL, "
                "UNIQUE(blockchain, address, mnemonic)"
                ");";
            exec_psql_command(config_.postgres_conninfo, ddl);
        });
        return false;
    }

    std::ifstream in(config_.recovered_wallets_path);
    std::string line;
    while (std::getline(in, line)) {
        if (trim_copy(line) == chain_name + "/" + address + "/" + mnemonic_words) {
            std::lock_guard<std::mutex> lock(wallet_cache_mutex_);
            known_wallet_records_.insert(key);
            return true;
        }
    }
    return false;
}

void Pipeline::persist_wallet_record(const std::string& chain_name,
                                     const std::string& address,
                                     const core::Mnemonic& mnemonic) const {
    const std::string mnemonic_words = mnemonic_to_string(mnemonic);
    const std::string key = wallet_record_key(chain_name, address, mnemonic_words);

    if (wallet_record_exists(chain_name, address, mnemonic_words)) {
        return;
    }

    if (!config_.postgres_conninfo.empty()) {
        if (!is_valid_sql_identifier(config_.postgres_table)) {
            throw std::runtime_error("Invalid PostgreSQL table name: " + config_.postgres_table);
        }
        std::call_once(postgres_init_once_, [&]() {
            const std::string ddl =
                "CREATE TABLE IF NOT EXISTS " + config_.postgres_table + " ("
                "id BIGSERIAL PRIMARY KEY, "
                "created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(), "
                "blockchain TEXT NOT NULL, "
                "address TEXT NOT NULL, "
                "mnemonic TEXT NOT NULL, "
                "UNIQUE(blockchain, address, mnemonic)"
                ");";
            exec_psql_command(config_.postgres_conninfo, ddl);
        });

        const std::string insert =
            "INSERT INTO " + config_.postgres_table +
            " (blockchain, address, mnemonic) VALUES ('" +
            escape_sql_literal(chain_name) + "', '" +
            escape_sql_literal(address) + "', '" +
            escape_sql_literal(mnemonic_words) + "') ON CONFLICT DO NOTHING;";

        std::lock_guard<std::mutex> lock(postgres_mutex_);
        exec_psql_command(config_.postgres_conninfo, insert);
        std::lock_guard<std::mutex> cache_lock(wallet_cache_mutex_);
        known_wallet_records_.insert(key);
        return;
    }

    std::lock_guard<std::mutex> file_lock(postgres_mutex_);
    if (wallet_record_exists(chain_name, address, mnemonic_words)) {
        return;
    }
    std::ofstream out(config_.recovered_wallets_path, std::ios::app);
    if (!out) {
        throw std::runtime_error("Failed to open recovered wallets file: " + config_.recovered_wallets_path);
    }
    out << chain_name << "/" << address << "/" << mnemonic_words << '\n';
    std::lock_guard<std::mutex> cache_lock(wallet_cache_mutex_);
    known_wallet_records_.insert(key);
}

std::optional<std::pair<std::string, std::string>> Pipeline::parse_manual_wallet_line(const std::string& line) const {
    const std::string clean = trim_copy(line);
    if (clean.empty() || clean[0] == '#') {
        return std::nullopt;
    }

    static const std::array<char, 3> delimiters{',', ';', ' '};
    for (char delimiter : delimiters) {
        const auto pos = clean.find(delimiter);
        if (pos == std::string::npos) {
            continue;
        }
        const std::string chain_name = trim_copy(clean.substr(0, pos));
        const std::string address = trim_copy(clean.substr(pos + 1));
        if (chain_name.empty() || address.empty()) {
            return std::nullopt;
        }
        return std::make_pair(chain_name, address);
    }

    return std::nullopt;
}

void Pipeline::run_manual_wallet_checks() {
    std::cout << std::unitbuf;

    std::ifstream in(config_.manual_wallets_path);
    if (!in) {
        throw std::runtime_error("Failed to open manual wallets file: " + config_.manual_wallets_path);
    }

    print_console_header();

    std::unordered_map<std::string, chains::IChainModule*> modules_by_name;
    for (const auto& module : modules_) {
        modules_by_name[module->name()] = module.get();
    }

    std::string line;
    while (std::getline(in, line)) {
        const auto parsed = parse_manual_wallet_line(line);
        if (!parsed.has_value()) {
            continue;
        }

        const auto& [chain_name, address] = *parsed;
        const auto module_it = modules_by_name.find(chain_name);
        if (module_it == modules_by_name.end()) {
            std::lock_guard<std::mutex> lock(console_mutex_);
            std::cout << "skip || " << address << " || unknown chain: " << chain_name << '\n';
            continue;
        }

        core::Mnemonic manual_marker{"[manual", "input]"};
        persist_wallet_record(chain_name, address, manual_marker);
        print_console_row(chain_name, address, "[manual input]");
    }
}

void Pipeline::run() {
    std::cout << std::unitbuf;

    if (!config_.manual_wallets_path.empty()) {
        run_manual_wallet_checks();
        return;
    }

    core::ThreadPool pool(config_.threads);
    print_console_header();
    std::atomic<std::uint64_t> processed_candidates{0};
    std::atomic<std::uint64_t> stored_wallets{0};
    auto seen_mnemonics = load_seen_mnemonics(config_.seen_mnemonics_path);

    const std::size_t valid_candidates = generator_.generate(
        config_.template_words,
        validator_,
        config_.max_candidates,
        [&](const core::Mnemonic& mnemonic) {
            if (matcher_.should_stop()) {
                return true;
            }
            ++processed_candidates;

            const std::string mnemonic_words = mnemonic_to_string(mnemonic);
            if (!mark_seen_mnemonic_atomic(config_.seen_mnemonics_path, mnemonic_words, seen_mnemonics)) {
                return false;
            }
            auto seed = mnemonic_to_seed(mnemonic, config_.bip39_passphrase);
            struct ChainMatchResult {
                std::string chain_name;
                std::optional<std::string> matched_address;
            };

            std::vector<std::future<ChainMatchResult>> futures;
            futures.reserve(modules_.size());

            for (const auto& module : modules_) {
                if (is_chain_recovered(module->name())) {
                    continue;
                }
                auto paths = paths_for_module(config_, module->name());
                futures.push_back(pool.enqueue([&, paths, module_ptr = module.get(), seed_copy = seed, mnemonic_words]() mutable {
                    auto derived = module_ptr->derive_addresses(seed_copy, paths, config_.scan_limit);
                    if (matcher_.has_targets()) {
                        for (const auto& address : derived) {
                            persist_wallet_record(module_ptr->name(), address, mnemonic);
                            ++stored_wallets;
                            print_console_row(module_ptr->name(), address, mnemonic_words);
                            if (matcher_.contains_address(address)) {
                                return ChainMatchResult{
                                    module_ptr->name(),
                                    address,
                                };
                            }
                        }
                        return ChainMatchResult{module_ptr->name(), std::nullopt};
                    }

                    for (const auto& address : derived) {
                        persist_wallet_record(module_ptr->name(), address, mnemonic);
                        ++stored_wallets;
                        print_console_row(module_ptr->name(), address, mnemonic_words);
                    }
                    return ChainMatchResult{module_ptr->name(), std::nullopt};
                }));
            }

            if (futures.empty()) {
                matcher_.stop();
                return true;
            }

            for (auto& future : futures) {
                auto result = future.get();
                if (!result.matched_address.has_value()) {
                    continue;
                }

                if (is_chain_recovered(result.chain_name)) {
                    continue;
                }

                mark_chain_recovered(result.chain_name);
                const auto module_it = std::find_if(modules_.begin(), modules_.end(), [&](const auto& module) {
                    return module->name() == result.chain_name;
                });
                (void)module_it;
                std::cout << "Matched " << result.chain_name << " wallet: " << *result.matched_address << '\n';
            }

            return false;
        });

    {
        std::lock_guard<std::mutex> lock(console_mutex_);
        std::cout << "Done. Valid candidates: " << valid_candidates
                  << ", processed: " << processed_candidates.load()
                  << ", stored wallets: " << stored_wallets.load() << ".\n";
        if (valid_candidates == 0) {
            std::cout << "Hint: search finished immediately because there were no valid candidates for the current "
                         "template/wordlist settings.\n";
        }
    }
}

} // namespace engine
