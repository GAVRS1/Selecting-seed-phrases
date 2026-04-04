#include "bip39/wordlist.hpp"

#include <fstream>
#include <stdexcept>

namespace bip39 {

Wordlist::Wordlist(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input) {
        throw std::runtime_error("Failed to open BIP39 wordlist: " + file_path);
    }

    std::string word;
    while (std::getline(input, word)) {
        if (word.empty()) {
            continue;
        }
        index_[word] = static_cast<int>(words_.size());
        words_.push_back(word);
    }

    if (words_.empty()) {
        throw std::runtime_error("Wordlist is empty: " + file_path);
    }
}

bool Wordlist::contains(const std::string& word) const {
    return index_.contains(word);
}

int Wordlist::index_of(const std::string& word) const {
    auto it = index_.find(word);
    return it == index_.end() ? -1 : it->second;
}

const std::vector<std::string>& Wordlist::words() const {
    return words_;
}

} // namespace bip39
