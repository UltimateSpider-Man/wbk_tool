


#include <vector>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cmath>

constexpr int VAG_SAMPLE_BYTES = 14;
constexpr int VAG_SAMPLE_NIBBL = 28;

const double VagLutDecoder[5][2] = {
    {0.0, 0.0},
    {60.0 / 64.0, 0.0},
    {115.0 / 64.0, -52.0 / 64.0},
    {98.0 / 64.0, -55.0 / 64.0},
    {122.0 / 64.0, -60.0 / 64.0}
};

enum VAGFlag {
    VAGF_PLAYBACK_END = 0x03,
    VAGF_LOOP_START = 0x06
};

struct VAGChunk {
    int8_t shift;
    int8_t predict;
    uint8_t flags;
    uint8_t sample[VAG_SAMPLE_BYTES];
};

std::vector<int16_t> DecodePS2ADPCM(const std::vector<uint8_t>& vagData) {
    std::vector<int16_t> pcmData;
    size_t pos = 16; // Skip header

    double hist_1 = 0.0, hist_2 = 0.0;

    while (pos + 16 <= vagData.size()) {
        VAGChunk vc;
        uint8_t decodingCoefficient = vagData[pos++];
        vc.shift = decodingCoefficient & 0xF;
        vc.predict = (decodingCoefficient & 0xF0) >> 4;
        vc.flags = vagData[pos++];

        std::copy(vagData.begin() + pos, vagData.begin() + pos + VAG_SAMPLE_BYTES, vc.sample);
        pos += VAG_SAMPLE_BYTES;

        if (vc.flags == VAGF_PLAYBACK_END) {
            break;
        }

        int samples[VAG_SAMPLE_NIBBL];
        for (int j = 0; j < VAG_SAMPLE_BYTES; j++) {
            samples[j * 2] = vc.sample[j] & 0xF;
            samples[j * 2 + 1] = (vc.sample[j] & 0xF0) >> 4;
        }

        for (int j = 0; j < VAG_SAMPLE_NIBBL; j++) {
            int s = samples[j] << 12;
            if (s & 0x8000) {
                s |= 0xFFFF0000;
            }

            int predict = std::min(static_cast<int>(vc.predict), 4);

            double sample = (s >> vc.shift) + hist_1 * VagLutDecoder[predict][0] + hist_2 * VagLutDecoder[predict][1];
            hist_2 = hist_1;
            hist_1 = sample;

            int16_t pcmSample = static_cast<int16_t>(std::clamp(sample, static_cast<double>(INT16_MIN), static_cast<double>(INT16_MAX)));
            pcmData.push_back(pcmSample);
        }
    }

    return pcmData;
}

// Example usage:
// std::ifstream file("input.vag", std::ios::binary);
// std::vector<uint8_t> vagData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
// auto pcmSamples = DecodeVAG(vagData);
