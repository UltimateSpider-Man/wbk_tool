#include <vector>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <array>

const double VagLutDecoder[5][2] = {
    {0.0, 0.0},               // 0
    {60.0 / 64.0, 0.0},       // 1
    {115.0 / 64.0, -52.0 / 64.0},  // 2
    {98.0 / 64.0,  -55.0 / 64.0},  // 3
    {122.0 / 64.0, -60.0 / 64.0}   // 4
};
struct VAGChunk {
    int8_t  shift;    // lower nibble
    int8_t  predict;  // upper nibble
    uint8_t flags;
    uint8_t sample[14];
};

std::vector<uint8_t> EncodeAdpcm1(const std::vector<int16_t>& pcmData, int numChannels = 1)
{
    const int samplesPerChunk = 28;
    std::vector<uint8_t> output;

    if (pcmData.empty())
        return output;

    size_t totalSamples = pcmData.size() / numChannels;
    std::vector<double> hist_1(numChannels, 0.0);
    std::vector<double> hist_2(numChannels, 0.0);

    size_t pos = 0;
    while (pos < totalSamples) {
        for (int ch = 0; ch < numChannels; ++ch) {
            size_t chStart = pos * numChannels + ch;

            // Check bounds
            if (chStart + (samplesPerChunk - 1) * numChannels >= pcmData.size())
                break;

            double bestError = std::numeric_limits<double>::max();
            int bestPredict = 0, bestShift = 0;
            std::array<int, 28> bestQuantized{};

            for (int predict = 0; predict <= 4; ++predict) {
                for (int shift = 0; shift <= 12; ++shift) {
                    double h1 = hist_1[ch];
                    double h2 = hist_2[ch];
                    std::array<int, 28> quantized{};
                    double error = 0.0;

                    for (int i = 0; i < samplesPerChunk; ++i) {
                        size_t idx = chStart + i * numChannels;
                        if (idx >= pcmData.size()) break;

                        double predicted = h1 * VagLutDecoder[predict][0] + h2 * VagLutDecoder[predict][1];
                        double delta = pcmData[idx] - predicted;
                        double scaled = delta * std::pow(2.0, shift) / 4096.0;
                        int q = static_cast<int>(std::lrint(scaled));
                        q = std::clamp(q, -8, 7);

                        double recon = predicted + q * 4096.0 / std::pow(2.0, shift);
                        error += std::pow(pcmData[idx] - recon, 2);

                        quantized[i] = q;
                        h2 = h1;
                        h1 = recon;
                    }

                    if (error < bestError) {
                        bestError = error;
                        bestPredict = predict;
                        bestShift = shift;
                        bestQuantized = quantized;
                    }
                }
            }

            output.push_back((bestPredict << 4) | (bestShift & 0x0F));
            output.push_back(0x00);

            for (int i = 0; i < 14; ++i) {
                int q0 = bestQuantized[i * 2] & 0x0F;
                int q1 = bestQuantized[i * 2 + 1] & 0x0F;
                output.push_back((q1 << 4) | q0);
            }

            // Update history
            for (int i = 0; i < samplesPerChunk; ++i) {
                double predicted = hist_1[ch] * VagLutDecoder[bestPredict][0] + hist_2[ch] * VagLutDecoder[bestPredict][1];
                double recon = predicted + bestQuantized[i] * 4096.0 / std::pow(2.0, bestShift);
                hist_2[ch] = hist_1[ch];
                hist_1[ch] = recon;
            }
        }

        pos += samplesPerChunk;
    }

    for (int ch = 0; ch < numChannels; ++ch) {
        output.push_back(0x00);      // predict/shift
        output.push_back(0x03);      // flags
        for (int i = 0; i < 14; ++i)
            output.push_back(0x00);
    }
    return output;
}


std::vector<int16_t> DecodeAdpcm1(
    const std::vector<uint8_t>& vagData,
    bool enableDithering = false,
    double ditherAmount = 0.2,
    bool applyLowPassFilter = false,
    double lpFilterAlpha = 0.95,
    bool removeDC = false
)
{
    const size_t MIN_SIZE = 16;
    if (vagData.size() < MIN_SIZE)
        return {};

    std::vector<int16_t> pcmData;
    pcmData.reserve(vagData.size() * 2);

    size_t pos = 16; // Skip the 16-byte VAG header

    double hist_1 = 0.0, hist_2 = 0.0;
    while (pos + 16 <= vagData.size()) {
        // ----------------------
        // Parse one 16-byte VAG chunk
        // ----------------------
        VAGChunk vc;

        // Byte 0: shift and predict nibble
        // lower nibble = shift
        // upper nibble = predict index
        {
            uint8_t decodingCoefficient = vagData[pos++];
            vc.shift = decodingCoefficient & 0x0F;
            vc.predict = (decodingCoefficient & 0xF0) >> 4;
        }

        // Byte 1: flags
        vc.flags = vagData[pos++];

        // Next 14 bytes: nibble-packed samples
        std::copy(vagData.begin() + pos,
            vagData.begin() + pos + 14,
            vc.sample);
        pos += 14;

        // If end-flag encountered, break
        if (vc.flags == 0x03) {
            break;
        }

        // ----------------------
        // Unpack 28 4-bit samples from the 14 bytes
        // ----------------------
        int samples[28];
        for (int j = 0; j < 14; ++j) {
            // Low nibble
            samples[j * 2 + 0] = (vc.sample[j] & 0x0F);
            // High nibble
            samples[j * 2 + 1] = ((vc.sample[j] & 0xF0) >> 4);
        }

        // ----------------------
        // Decode each 4-bit sample into 16-bit PCM
        // ----------------------
        const int predictIndex = std::clamp<int>(vc.predict, 0, 4);
        for (int j = 0; j < 28; j++) {
            // Sign-extend 4-bit to 32-bit
            // s is in the high nibble
            int s = samples[j];
            // If the 4-bit is >= 8, it should be negative
            if (s & 0x08) {
                s |= 0xFFFFFFF0; // sign-extend into 32 bits
            }

            // Shift into place. Original formula typically does: s << 12
            // Then shift it right by vc.shift. We can combine:
            // final_sample = (s << 12) >> vc.shift
            // We'll do it in floating point:
            double sample = static_cast<double>(s << 12) / std::pow(2.0, vc.shift);

            // Apply the ADPCM predictor filter
            sample += hist_1 * VagLutDecoder[predictIndex][0]
                + hist_2 * VagLutDecoder[predictIndex][1];

            // Update history
            hist_2 = hist_1;
            hist_1 = sample;

            // Optional dithering
            if (enableDithering) {
                // Add random noise in [-0.5, +0.5), then multiply by ditherAmount
                double randVal = (double(rand()) / double(RAND_MAX) - 0.5);
                sample += randVal * ditherAmount;
            }

            // Clamp to 16-bit range
            double clamped = std::clamp(sample, -32768.0, 32767.0);

            // Convert to int16
            pcmData.push_back(static_cast<int16_t>(std::lrint(clamped)));
        }
    }

    if (applyLowPassFilter && !pcmData.empty()) {
        int16_t prevOut = pcmData[0];
        for (size_t i = 1; i < pcmData.size(); ++i) {
            double in = pcmData[i];
            double out = lpFilterAlpha * static_cast<double>(prevOut)
                + (1.0 - lpFilterAlpha) * in;
            // Convert back to 16-bit
            int16_t filtered = static_cast<int16_t>(std::lrint(std::clamp(out, -32768.0, 32767.0)));
            pcmData[i] = filtered;
            prevOut = filtered;
        }
    }

    if (removeDC && !pcmData.empty()) {
        // alpha near 1.0 -> slower high-pass
        double alpha = 0.995;
        int16_t prevIn = pcmData[0];
        int16_t prevOut = pcmData[0];
        for (size_t i = 1; i < pcmData.size(); ++i) {
            int16_t currentIn = pcmData[i];
            double out = static_cast<double>(currentIn)
                - static_cast<double>(prevIn)
                + alpha * static_cast<double>(prevOut);

            int16_t filtered = static_cast<int16_t>(std::lrint(std::clamp(out, -32768.0, 32767.0)));

            pcmData[i] = filtered;

            // Update for next iteration
            prevIn = currentIn;
            prevOut = filtered;
        }
    }

    return pcmData;
}
