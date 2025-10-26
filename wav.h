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
        std::ifstream f(filename, std::ios::binary);
        if (!f.good()) return false;

        samples.clear();

        char riff[4], wave[4];
        uint32_t riffSize = 0;
        if (!f.read(riff, 4) || !f.read(reinterpret_cast<char*>(&riffSize), 4) || !f.read(wave, 4))
            return false;
        if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE")
            return false;

        bool gotFmt = false, gotData = false;
        while (f && !(gotFmt && gotData)) {
            char id[4];
            uint32_t sz = 0;
            if (!f.read(id, 4) || !f.read(reinterpret_cast<char*>(&sz), 4)) break;

            if (std::string(id, 4) == "fmt ") {
                if (sz < 16) return false;
                f.read(reinterpret_cast<char*>(&header.audioFormat), 2);
                f.read(reinterpret_cast<char*>(&header.numChannels), 2);
                f.read(reinterpret_cast<char*>(&header.sampleRate), 4);
                f.read(reinterpret_cast<char*>(&header.byteRate), 4);
                f.read(reinterpret_cast<char*>(&header.blockAlign), 2);
                f.read(reinterpret_cast<char*>(&header.bitsPerSample), 2);
                if (sz > 16) f.seekg(sz - 16, std::ios::cur); // skip extras
                header.subchunk1Size = sz;
                if (header.audioFormat != 1) return false;
                gotFmt = true;
            }
            else if (std::string(id, 4) == "data") {
                header.subchunk2Size = sz;
                samples.resize(sz);
                if (!f.read(reinterpret_cast<char*>(samples.data()), sz)) return false;
                gotData = true;
            }
            else
                f.seekg(sz + (sz & 1u), std::ios::cur);
        }

        if ((!gotFmt || !gotData)  || samples.size() % 2) return false;

        header.blockAlign = static_cast<uint16_t>((header.bitsPerSample / 8) * header.numChannels);
        header.byteRate = header.sampleRate * header.blockAlign;

        return true;
    }
    static bool writeWAV(const std::string& filename, std::vector<int16_t>& samples, uint32_t sampleRate, int nchannels = 1) {
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
            return false;
        else {
            outFile.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
            outFile.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(int16_t));
            outFile.close();
            return true;
        }
    }
};
