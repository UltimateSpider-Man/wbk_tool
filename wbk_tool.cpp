// main.cpp — WBK extract/reimport with optional name resolution via dictionary
// Usage:
//   Extract:  tool -e <input.wbk> <out_dir> [-h] [-n] [-d <dict.txt>]
//   Replace:  tool -r <input.wbk> <index|0xHASH|name|folder> <replacement.wav(if single)> [-h] [-n] [-d <dict.txt>] [-c <codec>]
//
// Notes:
// - -h      : treat the third argument (single replace) as a raw 32-bit hash, or make extracted filenames 0xHASH.wav
// - -n      : resolve names using dictionary; for single replace, the 3rd arg is a *name* that will be hashed
// - -d file : path to string_hash_dictionary.txt (one name per line is fine; hashes auto-computed)
// - With -n, extraction names are <resolved>.wav when possible; otherwise fall back to 0xHASH.wav
// - Folder replace tries (in order): <i>.wav, <resolved>.wav (if -n and found), 0xHASH.wav
// - Writes <input>.new.wbk when changes were made

#include "wbk.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------
// Small helpers (hash, trim, lower)
// ---------------------------
static inline bool is_alpha(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}
static inline unsigned char to_lower_uc(unsigned char c) {
    constexpr auto delta = 'a' - 'A';
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + delta) : c;
}
// Engine string-hash (case-insensitive letters, multiplier 33)
static inline uint32_t engine_to_hash(std::string_view s) {
    uint32_t res = 0;
    for (unsigned char c : s) {
        int cl = is_alpha(c) ? to_lower_uc(c) : c;
        res = (uint32_t)(cl + 33u * res);
    }
    return res;
}
static inline std::string trim_copy(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
    size_t a = 0, b = s.size();
    while (a < b && issp((unsigned char)s[a])) ++a;
    while (b > a && issp((unsigned char)s[b - 1])) --b;
    s.erase(b);
    s.erase(0, a);
    return s;
}
static inline std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// ---------------------------
// Dictionary (name <-> hash)
// ---------------------------
static std::unordered_map<uint32_t, std::string> g_hash_to_name;
static std::unordered_map<std::string, uint32_t> g_name_to_hash; // lowercased key

// Accepts flexible formats per line:
//   - raw name (e.g., "sfx/ambience/wind_light")
//   - "0x12345678 some/name" (hash ignored; we recompute from the name to be consistent)
//   - comments starting with '#' or '//'
static bool load_dictionary(const fs::path& dict_path) {
    std::ifstream f(dict_path);
    if (!f) {
        std::fprintf(stderr, "Failed to open dictionary: %s\n", dict_path.string().c_str());
        return false;
    }
    size_t added = 0;
    std::string line;
    while (std::getline(f, line)) {
        line = trim_copy(line);
        if (line.empty()) continue;
        if (line.rfind("#", 0) == 0 || line.rfind("//", 0) == 0) continue;

        std::string name = line;
        // If it begins with 0x..., split and take the rest as name (for human notes files)
        if (line.size() > 2 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
            // find first space after the hex
            auto sp = line.find_first_of(" \t");
            if (sp != std::string::npos) {
                name = trim_copy(line.substr(sp + 1));
                if (name.empty()) continue;
            }
            else {
                // only a hex? skip; we need a name
                continue;
            }
        }
        // normalize to lower for map key (engine hash ignores case for letters)
        std::string name_norm = to_lower_copy(name);
        uint32_t h = engine_to_hash(name_norm);
        // prefer first seen mapping; skip duplicates unless we want to override
        if (!g_hash_to_name.count(h)) g_hash_to_name[h] = name; // store the original-casing name
        if (!g_name_to_hash.count(name_norm)) g_name_to_hash[name_norm] = h;
        ++added;
    }
    std::fprintf(stderr, "Loaded %zu dictionary entries from %s\n", added, dict_path.string().c_str());
    return true;
}



static std::optional<uint32_t> lookup_hash_by_name(std::string name) {
    std::string key = to_lower_copy(trim_copy(name));
    auto it = g_name_to_hash.find(key);
    if (it != g_name_to_hash.end()) return it->second;
    // Not in dict? Still allow hashing the provided string directly.
    return engine_to_hash(key);
}

// ---------------------------
// MAIN
// ---------------------------
int main(int argc, char** argv)
{
    int nine = 0;
    if (argc < 3 || argc >  nine /*placeholder to keep compile error visible if edited*/) {}

    // Simple usage guard (kept from your original, adjusted to show -d)
    if (argc < 3 || argc >  nine /*remove this placeholder and keep the block below*/) {}

    // Real usage guard
    if (argc < 3 || argc > 11) {
        std::printf("Usage:\n");
        std::printf("  %s -e <.wbk> <output_folder> [-h] [-n] [-d <dict.txt>]\n", argv[0]);
        std::printf("  %s -r <.wbk> <index|0xHASH|name|folder> <replacement.wav (if single)> [-h] [-n] [-d <dict.txt>] [-c <codec>]\n", argv[0]);
        std::printf("\nOptions:\n");
        std::printf("  -h           Treat indices as raw 32-bit hashes (and name extracted files as 0xHASH.wav)\n");
        std::printf("  -n           Resolve string names via dictionary; for single replace, treat 3rd arg as NAME\n");
        std::printf("  -d <file>    Path to string_hash_dictionary.txt (one name per line)\n");
        std::printf("  -c <codec>   Set codec when replacing: 1=PCM, 2=PCM2, 4=ADPCM_1, 5=ADPCM_2, 7=IMA_ADPCM (others reserved)\n");
        return -1;
    }

    bool extract = false;
    bool hashSearch = false;     // interpret arg3 as hash for single replace; use 0xHASH filenames on extract
    bool resolveHashes = false;  // use dictionary to resolve names for extract/replace
    fs::path dictPath;

    // Quick mode detection and parsing of the positional part
    if (std::strstr(argv[1], "-e")) {
        extract = true;
    }
    else if (!std::strstr(argv[1], "-r")) {
        std::printf("Invalid mode. Use -e or -r.\n");
        return -1;
    }

    // Options parse
    WBK::Codec codec = WBK::Keep;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            int codecType = std::atoi(argv[i + 1]);
            if (codecType >= (int)WBK::PCM && codecType <= (int)WBK::IMA_ADPCM) {
                codec = (WBK::Codec)codecType;
            }
            else {
                std::printf("Invalid codec type specified!\n");
                return -1;
            }
        }
        else if (std::strcmp(argv[i], "-h") == 0) {
            hashSearch = true;
        }
        else if (std::strcmp(argv[i], "-n") == 0) {
            resolveHashes = true;
            hashSearch = true; // -n implies hashed-based addressing
        }
        else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            dictPath = fs::path(argv[i + 1]);
        }
    }

    // Load dictionary if requested
    if (resolveHashes) {
        if (dictPath.empty()) {
            std::fprintf(stderr, "Warning: -n provided but no -d <dict.txt>. I will still hash names directly.\n");
        }
        else {
            if (!load_dictionary(dictPath)) {
                std::fprintf(stderr, "Warning: dictionary load failed; continuing without name resolution.\n");
            }
        }
    }

    WBK wbk;

    // Helper that decides filename for a given entry index
    auto make_filename = [&wbk, &resolveHashes](bool hash, int i) {
        const auto& e = wbk.entries[i];
        if (hash) {
            if (resolveHashes) {
                auto hname = lookup_string_by_hash(e.hash);
                if (!hname.empty())
                    return std::format("{}.wav", hname);
            }
            return std::format("0x{:08x}.wav", e.hash);
        }
        else {
            return std::format("{}.wav", i);
        }
        };

    if (extract) {
        if (wbk.read(argv[2]) != WBK_OK) return WBK_PARSE_FAILED;

        auto base_path = std::string(argv[3]);
        if (!fs::exists(base_path)) fs::create_directories(base_path);

        for (size_t i = 0; i < wbk.tracks.size(); ++i) {
            auto name = make_filename(hashSearch, (int)i);
            fs::path out = fs::path(base_path) / name;
            const auto& entry = wbk.entries[i];
            std::vector<int16_t>& pcm = wbk.tracks[i];
            WAV::writeWAV(out.string(), pcm, entry.samples_per_second, WBK::GetNumChannels(entry));
        }
        return 1;
    }

    // Replace mode
    // argv[2] = input.wbk
    // argv[3] = index | 0xHASH | NAME (with -n) | folder
    if (wbk.read(argv[2], /*load_samples=*/false) != WBK_OK) return WBK_PARSE_FAILED;

    bool modified = false;
    fs::path third = argv[3];
    int replace_idx = -1;
    fs::path replace_path;

    // Folder or single?
    if (fs::exists(third) && fs::is_directory(third)) {
        replace_path = third;
        int successes = 0;

        for (int i = 0; i < (int)wbk.entries.size(); ++i) {
            const auto& e = wbk.entries[i];
            // Candidate filenames to look up
            std::vector<fs::path> candidates;
            candidates.emplace_back(replace_path / std::format("{}.wav", i)); // index.wav

            if (hashSearch) {
                if (resolveHashes) {
                    auto nice = lookup_string_by_hash(e.hash);
                    if (!nice.empty())
                        candidates.emplace_back(replace_path / std::format("{}.wav", nice)); // name.wav
                }
                candidates.emplace_back(replace_path / std::format("0x{:08x}.wav", e.hash)); // 0xHASH.wav
            }

            bool done = false;
            for (const auto& wav_file : candidates) {
                if (!fs::exists(wav_file)) continue;

                WAV wav;
                if (!wav.readWAV(wav_file.string())) {
                    std::fprintf(stderr, "Failed to parse WAV: %s\n", wav_file.string().c_str());
                    continue;
                }
                if (wbk.replace(i, wav, codec) == WBK_OK) {
                    std::printf("Replaced index %d (%s)\n", i, wav_file.filename().string().c_str());
                    modified = true;
                    successes++;
                    done = true;
                    break;
                }
                else {
                    std::fprintf(stderr, "Replace failed for %s\n", wav_file.string().c_str());
                }
            }
            if (!done) {
                // Not fatal; just report missing
                std::printf("No replacement for index %d\n", i);
            }
        }

        std::printf("Replaced %d/%zu entries\n", successes, wbk.entries.size());
    }
    else {
        // Single replacement
        // Interpret argv[3] based on switches
        if (!hashSearch) {
            // must be index
            char* endp = nullptr;
            long idx = std::strtol(argv[3], &endp, 10);
            if (!endp || *endp != '\0' || idx < 0 || idx >= (long)wbk.entries.size()) {
                std::printf("Invalid replacement index specified!\n");
                return -1;
            }
            replace_idx = (int)idx;
        }
        else {
            // hash or name
            uint32_t target_hash = 0;
            if (resolveHashes) {
                // Treat arg as a NAME (hash via engine hash; dict helps only for nicer naming)
                auto maybe = lookup_hash_by_name(argv[3]);
                target_hash = maybe.value(); // always produced even if not in dict
            }
            else {
                // Expect 0xHASH or decimal
                std::string s = argv[3];
                if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
                    target_hash = (uint32_t)std::strtoul(s.c_str() + 2, nullptr, 16);
                }
                else {
                    target_hash = (uint32_t)std::strtoul(s.c_str(), nullptr, 10);
                }
            }
            auto it = std::find_if(wbk.entries.begin(), wbk.entries.end(),
                [target_hash](const WBK::nslWave& w) { return w.hash == target_hash; });
            if (it == wbk.entries.end()) {
                std::fprintf(stderr, "WBK_HASH_NOT_FOUND (0x%08X)\n", target_hash);
                return WBK_HASH_NOT_FOUND;
            }
            replace_idx = (int)std::distance(wbk.entries.begin(), it);
        }

        if (argc < 5) {
            std::fprintf(stderr, "Missing <replacement.wav> for single replace.\n");
            return -1;
        }
        WAV wav;
        if (!wav.readWAV(argv[4])) {
            std::printf("This WAV failed to parse\n");
            return -1;
        }
        if (wbk.replace(replace_idx, wav, codec) == WBK_OK) {
            modified = true;
            std::printf("Replaced index %d\n", replace_idx);
        }
        else {
            std::fprintf(stderr, "Replace failed for index %d\n", replace_idx);
        }
    }

    if (modified) {
        fs::path out = fs::path(std::string(argv[2])).replace_extension(".new.wbk");
        wbk.write(out);
        std::printf("Written to %s\n", out.string().c_str());
        return 1;
    }
    return 0;
}

