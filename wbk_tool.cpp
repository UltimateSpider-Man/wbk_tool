#include "wbk.h"

namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    if (argc < 3 || argc > 7) {
        printf("Usage:\n");
        printf("  %s -e <.wbk> <output_folder>\n", argv[0]);
        printf("  %s -r <.wbk> <index|folder> <replacement.wav (if index)>\n", argv[0]);
        printf("\nOptions:\n");
        printf("  -h           Treat indices as hashes\n");
        printf("  -n           Treat indices as string hashes (requires string_hash_dictionary.txt)\n");
        printf("  -c <codec>   Set codec when replacing:\n");
        printf("               1: PCM\n");
        printf("               2: PCM2\n");
        printf("               3: Reserved\n");
        printf("               4: ADPCM_1\n");
        printf("               5: ADPCM_2\n");
        printf("               6: Reserved3\n");
        printf("               7: IMA_ADPCM\n");
        return -1;
    }

    bool extract = false;
    bool hashSearch = false;
    bool resolveHashes = false;
    int replace_idx = -1;
    std::filesystem::path replace_path;

    if (strstr(argv[1], "-e")) {
        extract = true;
    } else if (strstr(argv[1], "-r")) {
        if (std::filesystem::exists(argv[3])) {
            replace_path = argv[3];
        }
        else {
            auto idx = atoi(argv[3]);
            
            if (idx > INT_MIN && idx < INT_MAX)
                replace_idx = idx;
            else
                printf("Invalid replacement index specified!\n");
        }
    } 
    else {
        printf("Invalid arguments specified!\n");
        return -1;
    }

    WBK::Codec codec = WBK::Keep;
    for (int i = 1; i < argc; ++i)
    {
        int nextIdx = (i + 1 < argc) ? i + 1 : argc;
        if (strstr(argv[i], "-c") && nextIdx < argc) 
        {
            auto codecType = atoi(argv[nextIdx]);
            if (codecType >= WBK::PCM && codecType <= WBK::IMA_ADPCM)
                codec = (WBK::Codec)codecType;
            else {
                printf("Invalid codec type specified!");
                return -1;
            }
        }
        if (strstr(argv[i], "-h"))
            hashSearch = true;
        if (strstr(argv[i], "-n"))
            resolveHashes = true;
    }

    if (!hashSearch && resolveHashes)
        hashSearch = true;

    WBK wbk;


    auto make_filename = [&wbk, &resolveHashes](bool hash, int i) {
        auto h = lookup_string_by_hash(wbk.entries[i].hash);
        return hash ? resolveHashes && !h.empty() ?
                        std::format("{}.wav", h)
                        : std::format("0x{:08x}.wav", wbk.entries[i].hash)
                    : std::format("{}.wav", i);
    };

    if (extract)
    {
        if (wbk.read(argv[2]) != WBK_OK)
            return WBK_PARSE_FAILED;

        auto base_path = std::string(argv[3]);
        if (!fs::exists(base_path))
            fs::create_directories(base_path);
        size_t index = 0;
        for (auto& track : wbk.tracks) {
            WBK::nslWave& entry = wbk.entries[index];
            auto name = make_filename(hashSearch, static_cast<int>(index));
            fs::path output_path = fs::path(base_path) / name;
            WAV::writeWAV(output_path.string(), track, entry.samples_per_second, WBK::GetNumChannels(entry));
            ++index;
        }
        return 1;
    }
    else {
        wbk.read(argv[2], false);

        bool modified = false;
        if (replace_path.empty() && replace_idx != -1 && (!hashSearch && (replace_idx >= wbk.header.num_entries))) {
            printf("Invalid replacement index specified!\n");
        }
        else if (replace_idx != -1 || !replace_path.empty()) {
            if (!replace_path.empty()) {
                auto successes = 0;
                for (int i = 0; i < wbk.entries.size(); ++i) {
                    auto name = make_filename(hashSearch, i);
                    const fs::path wav_file = replace_path / name;

                    if (fs::exists(wav_file)) {
                        WAV replacement_wav;
                        if (replacement_wav.readWAV(wav_file.string())) {
                            if (wbk.replace(i, replacement_wav, codec) == WBK_OK) {
                                printf("Replaced index %d\n", i);
                                modified = true;
                                successes++;
                            }
                            else
                                printf("Failed to replace index %d!\n", i);
                        }
                        else {
                            printf("This WAV failed to parse\n");
                        }
                    }
                    else
                        printf("Replacement track not found for index %d!\n", i);
                }
                printf("Replaced %d/%zd entries\n", successes, wbk.entries.size());
            }
            else {
                WAV replacement_wav;
                if (replacement_wav.readWAV(argv[4])) {
                    if (hashSearch) {
                        if (resolveHashes) 
                            replace_idx = string_hash::to_hash(argv[3]);
                        
                        auto it = std::find_if(wbk.entries.begin(), wbk.entries.end(), [replace_idx](const WBK::nslWave& p) { return p.hash == replace_idx; });
                        if ((it == wbk.entries.end())) 
                            return WBK_HASH_NOT_FOUND;

                        replace_idx = static_cast<int>(std::distance(wbk.entries.begin(), it));
                    }

                    if (wbk.replace(replace_idx, replacement_wav, codec) == WBK_OK) {
                        modified = true;
                        printf("Replaced index %d\n", replace_idx);
                    }
                }
                else {
                    printf("This WAV failed to parse\n");
                }
            }
        }
        
        if (modified) {
            fs::path path = fs::path(std::string(argv[2])).replace_extension(".new.wbk").string();
            wbk.write(path);
            printf("Written to %s\n", path.string().c_str());
            return 1;
        }
    }
    return 0;
}
