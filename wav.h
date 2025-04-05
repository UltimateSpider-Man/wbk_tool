#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include "ima_adpcm.h"

struct WAV {
    #pragma pack(push, 1)
    struct WAVHeader {
        char riff[4] = { 'R', 'I', 'F', 'F' };
        uint32_t chunkSize;
        char wave[4] = { 'W', 'A', 'V', 'E' };
        char fmt[4] = { 'f', 'm', 't', ' ' };
        uint32_t subchunk1Size = 16;
        uint16_t audioFormat = 1;
        uint16_t numChannels = 1;
        uint32_t sampleRate;
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample = 16;
        char data[4] = { 'd', 'a', 't', 'a' };
        uint32_t subchunk2Size;
    } header;
    #pragma pack(pop)
    std::vector<uint8_t> samples;

    bool readWAV(const std::filesystem::path& filename) {
        std::ifstream inputFile(filename, std::ios::binary);
        if (!inputFile.good()) {
            return false;
        }

        samples.clear();
        inputFile.read(reinterpret_cast<char*>(&header), sizeof(WAVHeader));
        if (inputFile.gcount() != sizeof(WAVHeader)) {
            printf("Failed to read WAV header.\n");
            return false;
        }
        if (std::string(header.riff, 4) != "RIFF" || std::string(header.wave, 4) != "WAVE") {
            printf("Invalid WAV file format.\n");
            return false;
        }
        samples.resize(header.subchunk2Size);
        inputFile.read(reinterpret_cast<char*>(samples.data()), header.subchunk2Size);
        if (inputFile.gcount() != static_cast<std::streamsize>(header.subchunk2Size)) {
            printf("Failed to read WAV sample data.\n");
            return false;
        }
        return true;
    }
    static void writeWAV(const std::string& filename, std::vector<int16_t>& samples, uint32_t sampleRate, int nchannels = 1) {
        WAVHeader header;
        header.sampleRate = sampleRate;
        header.numChannels = nchannels;
        header.bitsPerSample = 16;
        header.blockAlign = (header.bitsPerSample * header.numChannels) / 8;
        header.byteRate = header.sampleRate * header.blockAlign;
        header.subchunk2Size = (int)samples.size() * sizeof(int16_t);
        header.chunkSize = 36 + header.subchunk2Size;

        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile)
            printf("Failed to write to WAV file.\n");
        else {
            outFile.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
            outFile.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));
            outFile.close();
            printf("Written to %s\n", filename.c_str());
        }
    }
};
