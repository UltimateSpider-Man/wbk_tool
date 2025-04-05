#include "wbk.h"

namespace fs = std::filesystem;

void test_run()
{
#if _DEBUG
    WBK wbk2;
    wbk2.read(R"(TREYARCH_LOGO_EN.WBK)");
    WAV::writeWAV(R"(TREYARCH_LOGO_EN.WBK.wav)", wbk2.tracks[0], wbk2.entries[0].samples_per_second / WBK::GetNumChannels(wbk2.entries[0]), WBK::GetNumChannels(wbk2.entries[0]));

    WAV replacement_wav;
    replacement_wav.readWAV(R"(TREYARCH_LOGO_EN.WBK.wav)");
    wbk2.replace(0, replacement_wav);
    wbk2.write(R"(TREYARCH_LOGO_EN.WBK_test.WBK)");

    WBK wbk3;
    wbk3.read(R"(TREYARCH_LOGO_EN.WBK_test.WBK)");
    WAV::writeWAV(R"(TREYARCH_LOGO_EN.WBK_test.wav)", wbk3.tracks[0], wbk3.entries[0].samples_per_second, WBK::GetNumChannels(wbk3.entries[0]));
#endif
}

int main(int argc, char** argv)
{
    test_run();

    if (argc < 4 || argc > 6) {
        printf("Usage: %s -e|-r <.wbk> <.wav>\n", argv[0]);
        return -1;
    }
    
    bool extract = false;
    int replace_idx = 0;
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

    WBK wbk;
    wbk.read(argv[2]);

    if (extract)
    {
        size_t index = 0;
        for (auto& track : wbk.tracks) {
            WBK::nslWave& entry = wbk.entries[index];
            fs::path output_path = fs::path(std::string(argv[3])) / std::to_string(index).append(".wav");
            WAV::writeWAV(output_path.string(), track, entry.samples_per_second, WBK::GetNumChannels(entry));
            ++index;
        }
        return 1;
    }
    else {
        bool modified = false;
        if (replace_path.empty() && replace_idx > wbk.header.num_entries) {
            printf("Invalid replacement index specified!\n");
        }
        else {
            if (!replace_path.empty()) {
                auto successes = 0;
                for (int i = 0; i < wbk.tracks.size(); ++i) {
                    fs::path wav_file = replace_path / (std::to_string(i) + ".wav");

                    if (fs::exists(wav_file)) {
                        WAV replacement_wav;
                        if (replacement_wav.readWAV(wav_file.string())) {
                            if (wbk.replace(i, replacement_wav)) {
                                printf("Replaced index %d\n", i);
                                modified = true;
                                successes++;
                            }
                            else
                                printf("Failed to replace index %d!\n", i);
                        }
                    } 
                    else
                        printf("Replacement track not found for index %d!\n", i);
                }
                printf("Replaced %d/%zd entries\n", successes, wbk.tracks.size());
            }
            else {
                WAV replacement_wav;
                if (replacement_wav.readWAV(argv[4])) {
                    if (wbk.replace(replace_idx, replacement_wav)) {
                        modified = true;
                        printf("Replaced index %d\n", replace_idx);
                    }
                }
            }
        }
        
        if (modified) {
            fs::path path = fs::path(std::string(argv[2])).replace_extension(".new.wbk").string();
            wbk.write(path);
            printf("Written to %s\n", path.string().c_str());
        }
    }

    return 1;
}