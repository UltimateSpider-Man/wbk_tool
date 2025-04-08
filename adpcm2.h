#pragma once

static const int xindexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 6,
    -1, -1, -1, -1, 2, 4, 6, 6
};

static const int xstepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

std::vector<int16_t> DecodeAdpcm2(const std::vector<uint8_t>& adpcm_data, int num_channels)
{
    size_t offset = 0;
    const size_t blockSize = 36 * num_channels;
    const size_t numBlocks = adpcm_data.size() / blockSize;
    std::vector<int16_t> pcm_output;

    pcm_output.reserve(numBlocks * 64 * num_channels);

    for (size_t block = 0; block < numBlocks; ++block) {
        struct ChannelState {
            int predictor;
            int index;
        } state[2];

        for (int ch = 0; ch < num_channels; ++ch) {
            state[ch].predictor = static_cast<int16_t>(adpcm_data[offset] | (adpcm_data[offset + 1] << 8));
            state[ch].index = std::clamp(static_cast<int>(adpcm_data[offset + 2]), 0, 88);
            offset += 4; //reserved
            pcm_output.push_back(state[ch].predictor);
        }

        for (int sample = 1; sample < 64; sample += 2) {
            for (int ch = 0; ch < num_channels; ++ch) {
                uint8_t byte = adpcm_data[offset++];

                for (int shift = 0; shift <= 4; shift += 4) {
                    int nibble = (byte >> shift) & 0x0F;

                    int step = xstepsizeTable[state[ch].index];
                    int diff = step >> 3;
                    if (nibble & 4) diff += step;
                    if (nibble & 2) diff += step >> 1;
                    if (nibble & 1) diff += step >> 2;

                    if (nibble & 8)
                        state[ch].predictor -= diff;
                    else
                        state[ch].predictor += diff;

                    state[ch].predictor = std::clamp(state[ch].predictor, -32768, 32767);

                    state[ch].index += xindexTable[nibble];
                    state[ch].index = std::clamp(state[ch].index, 0, 88);

                    pcm_output.push_back(static_cast<int16_t>(state[ch].predictor));
                }
            }
        }
    }

    return pcm_output;
}

std::vector<uint8_t> EncodeAdpcm2(const std::vector<int16_t>& pcm, int numChannels)
{
    int16_t predictor[2] = { 0 };
    int index[2] = { 0 };

    const size_t samplesPerBlock = 64;
    const size_t blockSize = 36 * numChannels;
    std::vector<uint8_t> encoded;
    size_t totalSamples = pcm.size() / numChannels;

    for (size_t blockStart = 0; blockStart < totalSamples; blockStart += samplesPerBlock) {
        for (int ch = 0; ch < numChannels; ++ch) {
            size_t chOffset = blockStart * numChannels + ch;
            predictor[ch] = (blockStart + 0 < totalSamples) ? pcm[chOffset] : 0;
            index[ch] = 0;
            encoded.push_back(predictor[ch] & 0xFF);
            encoded.push_back((predictor[ch] >> 8) & 0xFF);
            encoded.push_back(index[ch]);
            encoded.push_back(0);
        }

        for (size_t i = 1; i < samplesPerBlock; i += 2) {
            for (int ch = 0; ch < numChannels; ++ch) {
                uint8_t packed = 0;
                for (int nib = 0; nib < 2; ++nib) {
                    size_t sampleIndex = blockStart + i + nib;
                    size_t pcmIndex = sampleIndex * numChannels + ch;

                    int step = xstepsizeTable[index[ch]];
                    int diff = pcmIndex < pcm.size() ? pcm[pcmIndex] - predictor[ch] : 0;

                    int nibble = 0;
                    if (diff < 0) {
                        nibble = 8;
                        diff = -diff;
                    }

                    int mask = step;
                    if (diff >= mask) { nibble |= 4; diff -= mask; }
                    mask >>= 1;
                    if (diff >= mask) { nibble |= 2; diff -= mask; }
                    mask >>= 1;
                    if (diff >= mask) { nibble |= 1; }

                    int delta = step >> 3;
                    if (nibble & 1) delta += step >> 2;
                    if (nibble & 2) delta += step >> 1;
                    if (nibble & 4) delta += step;
                    if (nibble & 8) delta = -delta;

                    predictor[ch] += delta;
                    predictor[ch] = std::clamp<int16_t>(predictor[ch], -32768, 32767);

                    index[ch] += xindexTable[nibble];
                    index[ch] = std::clamp(index[ch], 0, 88);

                    if (nib == 0)
                        packed |= (nibble & 0x0F);
                    else
                        packed |= (nibble & 0x0F) << 4;
                }
                encoded.push_back(packed);
            }
        }
    }
    return encoded;
}
