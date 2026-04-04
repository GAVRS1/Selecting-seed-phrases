#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace bip39 {

class Wordlist {
public:
    explicit Wordlist(const std::string& file_path);

    bool contains(const std::string& word) const;
    int index_of(const std::string& word) const;
    const std::vector<std::string>& words() const;

private:
    std::vector<std::string> words_;
    std::unordered_map<std::string, int> index_;
};

} // namespace bip39
