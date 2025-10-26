#include "wbk.h"

namespace fs = std::filesystem;

//#define _CODEC_TEST

void test_run()
{
#ifdef _CODEC_TEST
    WBK wbk2;
    wbk2.read(R"(samples\STREAMS_VOICE_IT_PS2.WBK)");
    WAV::writeWAV(R"(samples\STREAMS_VOICE_IT_PS2.WBK.wav)", wbk2.tracks[0], wbk2.entries[0].samples_per_second, WBK::GetNumChannels(wbk2.entries[0]));

    WAV wav;
    wav.readWAV(R"(samples\STREAMS_VOICE_IT_PS2.WBK.wav)");
    wbk2.replace(0, wav);
    wbk2.write("test.wbk");


    WBK wbkReEncoded;
    wbkReEncoded.read("test.wbk");
    WAV::writeWAV(R"(samples\STREAMS_VOICE_IT_PS2_re_encoded_decoded.wav)", wbkReEncoded.tracks[0], wbkReEncoded.entries[0].samples_per_second, WBK::GetNumChannels(wbkReEncoded.entries[0]));

#endif
#ifdef _REPLACE_TEST
 //   WBK wbk2;
    //wbk2.parse(R"(TREYARCH_LOGO_EN.WBK)");
//    WAV::writeWAV(R"(TREYARCH_LOGO_EN.WBK.wav)", wbk2.tracks[0], wbk2.entries[0].samples_per_second / WBK::GetNumChannels(wbk2.entries[0]), WBK::GetNumChannels(wbk2.entries[0]));

  //  WAV replacement_wav;
  //  replacement_wav.readWAV(R"(TREYARCH_LOGO_EN.WBK.wav)");
  //  wbk2.replace(0, replacement_wav);
  //  wbk2.write(R"(TREYARCH_LOGO_EN.WBK_test.WBK)");

  //  WBK wbk3;
    //wbk3.parse(R"(TREYARCH_LOGO_EN.WBK_test.WBK)");
  //  WAV::writeWAV(R"(TREYARCH_LOGO_EN.WBK_test.wav)", wbk3.tracks[0], wbk3.entries[0].samples_per_second, WBK::GetNumChannels(wbk3.entries[0]));
#endif


#ifdef _BATCH_REPLACE_TEST
    WBK wbk;
    wbk.read(R"(STREAMS_MUSIC.WBK)");

    size_t index = 0;
    for (int i = 0; i < wbk.entries.size(); ++i) 
    {
        fs::path wav_file = fs::path("test_output") / (std::to_string(i) + ".wav");

        if (fs::exists(wav_file)) {
            WAV replacement_wav;
            if (replacement_wav.readWAV(wav_file.string())) {
                if (wbk.replace(i, replacement_wav) == WBK_OK) {
                    printf("Replaced index %d\n", i);
                }
                else
                    printf("Failed to replace index %d!\n", i);
            }
        }
        else
            printf("Replacement track not found for index %d!\n", i);
    }    

    fs::path path = fs::path(std::string(R"(STREAMS_MUSIC.WBK)")).replace_extension(".new.wbk").string();
    wbk.write(path);
    printf("Written to %s\n", path.string().c_str());

    // extract
    index = 0;
    for (auto& track : wbk.tracks) {
        WBK::nslWave& entry = wbk.entries[index];
        fs::path output_path = fs::path(std::string(R"(output2)")) / std::to_string(index).append(".wav");
        WAV::writeWAV(output_path.string(), track, entry.samples_per_second, WBK::GetNumChannels(entry));
        ++index;
    }
#endif
    return;
}

int main(int argc, char** argv)
{
    //test_run();
    //return -1;

    if (argc < 3 || argc > 7) {
        printf("Usage:\n");
        printf("  %s -e <.wbk> <output_folder>\n", argv[0]);
        printf("  %s -r <.wbk> <index|folder> <replacement.wav (if index)>\n", argv[0]);
        printf("\nOptions:\n");
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
        if (strstr(argv[i], "-c") && nextIdx <= argc) 
        {
            auto codecType = atoi(argv[nextIdx]);
            if (codecType >= WBK::PCM && codecType <= WBK::IMA_ADPCM)
                codec = (WBK::Codec)codecType;
            else {
                printf("Invalid codec type specified!");
                return -1;
            }
        }
    }
    
    WBK wbk;
    if (extract)
    {
        wbk.read(argv[2]);
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
        wbk.read(argv[2], false);

        bool modified = false;
        if (replace_path.empty() && replace_idx != -1 && replace_idx >= wbk.header.num_entries) {
            printf("Invalid replacement index specified!\n");
        }
        else if (replace_idx != -1 || !replace_path.empty()) {

            if (!replace_path.empty()) {
                auto successes = 0;
                for (int i = 0; i < wbk.entries.size(); ++i) {
                    fs::path wav_file = replace_path / (std::to_string(i) + ".wav");

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
                    if (wbk.replace(replace_idx, replacement_wav) == WBK_OK) {
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
