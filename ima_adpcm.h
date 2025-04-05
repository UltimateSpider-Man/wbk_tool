#pragma once
#include <vector>

struct ImaAdpcmState {
    int valprev = 0;
    int index = 0;
};
const int stepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
    2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18499, 20350, 22385, 24623, 27086, 29794, 32767
};
const int indexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

// taken from ALSA
static std::vector<uint8_t> EncodeImaAdpcmMono(std::vector<uint8_t>& rawData)
{
    auto conv = [](const std::vector<uint8_t>& rawBytes) -> std::vector<int16_t> {
        std::vector<int16_t> pcmSamples;
        for (size_t i = 0; i + 1 < rawBytes.size(); i += 2) {
            pcmSamples.push_back((int16_t)(rawBytes[i] | (rawBytes[i + 1] << 8)));
        }
        return pcmSamples;
    };

    std::vector<int16_t> samples = conv(rawData);
    std::vector<uint8_t> outBuff;

    ImaAdpcmState state = { 0, 0 };
    bool hasHighNibble = false;
    uint8_t nibbleBuffer = 0;

    for (auto sl : samples) {
        short diff = sl - state.valprev;
        unsigned char sign = (diff < 0) ? 0x8 : 0x0;
        if (sign) diff = -diff;

        short step = stepsizeTable[state.index];
        short pred_diff = step >> 3;

        unsigned char adjust_idx = 0;
        for (int i = 0x4; i; i >>= 1, step >>= 1) {
            if (diff >= step) {
                adjust_idx |= i;
                diff -= step;
                pred_diff += step;
            }
        }

        state.valprev += sign ? -pred_diff : pred_diff;
        state.valprev = std::clamp(state.valprev, -32768, 32767);

        state.index += indexTable[adjust_idx];
        state.index = std::clamp(state.index, 0, 88);

        uint8_t nibble = sign | adjust_idx;
        if (!hasHighNibble) {
            nibbleBuffer = nibble & 0x0F;
            hasHighNibble = true;
        } else {
            nibbleBuffer |= (nibble << 4);
            outBuff.push_back(nibbleBuffer);
            hasHighNibble = false;
        }
    }

    if (hasHighNibble)
        outBuff.push_back(nibbleBuffer);

    return outBuff;
}
static std::vector<uint8_t> EncodeImaAdpcmStereo(const std::vector<uint8_t>& rawData)
{
    std::vector<int16_t> pcmSamples;
    for (size_t i = 0; i + 1 < rawData.size(); i += 2) {
        pcmSamples.push_back((int16_t)(rawData[i] | (rawData[i + 1] << 8)));
    }

    // split
    std::vector<int16_t> left, right;
    for (size_t i = 0; i < pcmSamples.size(); i += 2) {
        left.push_back(pcmSamples[i]);
        if (i + 1 < pcmSamples.size())
            right.push_back(pcmSamples[i + 1]);
    }

    ImaAdpcmState stateL = {}, stateR = {};
    std::vector<uint8_t> outBuff;

    size_t maxSamples = std::max(left.size(), right.size());
    bool hasHighNibble = false;
    uint8_t nibbleBuffer = 0;

    for (size_t i = 0; i < maxSamples; ++i) {
        auto encodeSample = [](int16_t sample, ImaAdpcmState& state) -> uint8_t {
            short diff = sample - state.valprev;
            unsigned char sign = (diff < 0) ? 8 : 0;
            if (sign) diff = -diff;

            short step = stepsizeTable[state.index];
            short pred_diff = step >> 3;

            unsigned char adjust_idx = 0;
            for (int i = 4; i; i >>= 1, step >>= 1) {
                if (diff >= step) {
                    adjust_idx |= i;
                    diff -= step;
                    pred_diff += step;
                }
            }

            state.valprev += sign ? -pred_diff : pred_diff;
            state.valprev = std::clamp(state.valprev, -32768, 32767);

            state.index += indexTable[adjust_idx];
            state.index = std::clamp(state.index, 0, 88);

            return sign | adjust_idx;
        };

        if (i < left.size()) {
            uint8_t nibble = encodeSample(left[i], stateL);
            if (!hasHighNibble) {
                nibbleBuffer = (nibble << 4);
                hasHighNibble = true;
            }
            else {
                nibbleBuffer |= (nibble & 0x0F);
                outBuff.push_back(nibbleBuffer);
                hasHighNibble = false;
            }
        }

        if (i < right.size()) {
            uint8_t nibble = encodeSample(right[i], stateR);
            if (!hasHighNibble) {
                nibbleBuffer = (nibble << 4);
                hasHighNibble = true;
            }
            else {
                nibbleBuffer |= (nibble & 0x0F);
                outBuff.push_back(nibbleBuffer);
                hasHighNibble = false;
            }
        }
    }
    
    if (hasHighNibble)
        outBuff.push_back(nibbleBuffer);

    return outBuff;
}


std::vector<int16_t> DecodeImaAdpcm(const std::vector<uint8_t>& samples, int num_channels = 1)
{    
    std::vector<ImaAdpcmState> states(num_channels);

    size_t sample_count = (samples.size() * 2);
    std::vector<int16_t> outBuff;
    outBuff.resize(sample_count);

    int sample_idx = 0;

    for (uint8_t byte : samples) {
        for (int shift = 0; shift <= 4; shift += 4) {
            uint8_t code = (byte >> shift) & 0x0F;
            int channel = sample_idx % num_channels;

            auto& state = states[channel];

            int step = stepsizeTable[state.index];
            int diff = step >> 3;
            if (code & 1) diff += step >> 2;
            if (code & 2) diff += step >> 1;
            if (code & 4) diff += step;

            if (code & 8)
                state.valprev -= diff;
            else
                state.valprev += diff;

            state.valprev = std::clamp(state.valprev, -32768, 32767);
            state.index += indexTable[code];
            state.index = std::clamp(state.index, 0, 88);

            outBuff[sample_idx++] = static_cast<int16_t>(state.valprev);
        }
    }

    return outBuff;
}