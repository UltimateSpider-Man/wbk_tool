#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include "wav.h"

class WBK {
public:
    struct header_t {
        char magic[8];
        char unk[8];
        int flag;
        int size;
        int sample_data_offs;
        int total_bytes;
        char name[32];
        int num_entries;
        int val5, val6, val7;
        int offs;
        int metadata_offs;
        int offs3;
        int offs4;
        int num;
        int entry_desc_offs;
        char padding[152];
    } header;

    struct metadata_t {
        char codec;
        char flags[3];
        uint32_t unk_vals;
        float unk_fvals[6];
    };

    struct nslWave
    {
        int hash;
        unsigned char codec;
        char field_5;
        unsigned char flags;
        char field_7;
        int num_samples;
        unsigned int num_bytes;
        int field_10;
        int field_14;
        int field_18;
        int compressed_data_offs;
        unsigned __int16 samples_per_second;
        __int16 field_22;
        int unk;
    };

    std::vector <nslWave> entries;
    std::vector<std::vector<int16_t>> tracks;
    std::vector<metadata_t> metadata;

    static int GetNumChannels(const nslWave& wave);
    static void SetNumChannels(nslWave& wave, int num_channels);
    static int GetNumSamples(const nslWave& wave);
    static int GetDuration(const nslWave& wave);
    static float GetDurationMs(const nslWave& wave);

    void read(std::filesystem::path path);
    void write(std::filesystem::path path);
    bool replace(int replacement_index, const WAV& wav);

private:
    std::vector<uint8_t> raw_data;
};


inline int WBK::GetNumChannels(const nslWave& wave) {
    int num_channels = 0;
    if (wave.flags)
        num_channels = (((((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) & 0x33) +
                        ((((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) >> 2) & 0x33)) & 0xF)   +
                       (((((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) & 0x33) +
                        ((((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) >> 2) & 0x33)) >> 4);
    else
        num_channels = 1;
    return num_channels;
}

inline void WBK::SetNumChannels(nslWave& wave, int num_channels) {
    unsigned char channel_mask = 0xFF, new_channel_bits = 0;
    for (int i = 0; i < num_channels; ++i)
        new_channel_bits |= (1 << i);
    wave.flags = (wave.flags & ~channel_mask) | new_channel_bits;
}

inline int WBK::GetDuration(const nslWave& wave)
{
    return 1000 * wave.num_bytes / wave.samples_per_second;
}

inline float WBK::GetDurationMs(const nslWave& wave)
{
    return WBK::GetDuration(wave) * 0.001;
}

inline int WBK::GetNumSamples(const nslWave& wave)
{
    unsigned int tmp_flag = (unsigned __int8)((((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) & 0x33) +
        (((unsigned __int8)((wave.flags & 0x55) + ((wave.flags >> 1) & 0x55)) >> 2) & 0x33));
    if (wave.codec == 1)
    {
        if (wave.flags)
            return ((tmp_flag & 0xF) + (tmp_flag >> 4)) * wave.num_bytes;
        else
            return wave.num_bytes;
    }
    else if (wave.codec == 2)
    {
        int bytes = 0;
        if (wave.flags)
            bytes = ((tmp_flag & 0xF) + (tmp_flag >> 4)) * wave.num_bytes;
        else
            bytes = wave.num_bytes;
        return 2 * bytes;
    }
    else
        return wave.num_samples;
}

void WBK::read(std::filesystem::path path)
{
    std::ifstream stream(path, std::ios::binary);
    if (stream.good()) {
        stream.seekg(0, std::ios::end);
        size_t actual_file_size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        raw_data.resize(actual_file_size);
        stream.read((char*)raw_data.data(), actual_file_size);

        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(&header), sizeof header_t);

        // read all entries
        for (int index = 0; index < header.num_entries; ++index) {
            nslWave entry;
            stream.seekg(sizeof header_t + (sizeof nslWave * index), std::ios::beg);
            stream.read(reinterpret_cast<char*>(&entry), sizeof nslWave);

            // calc bits per sample & blockAlign
            int bits_per_sample = 0;
            int size = 0;
            int blockAlign = 0;
            if (entry.codec == 1 || entry.codec == 2) {
                bits_per_sample = 8 * (entry.codec != 1) + 8;
                blockAlign = (GetNumChannels(entry) * bits_per_sample) / 8;
            }
            else if (entry.codec == 4) {
                bits_per_sample = 16;
                blockAlign = (bits_per_sample * GetNumChannels(entry)) / 8;
            }
            else if (entry.codec == 5) {
                bits_per_sample = 4;
                blockAlign = 36 * GetNumChannels(entry);
            }
            else if (entry.codec == 7) {
                bits_per_sample = 16;
                blockAlign = (bits_per_sample * GetNumChannels(entry)) / 8;
            }

            // calc size depending on format
            int fmt_type = entry.codec - 4;
            if (fmt_type) {
                int tmp_type = fmt_type - 1;
                if (!tmp_type)
                    size = blockAlign * (entry.num_bytes >> 6);
                else if (tmp_type != 2)
                    size = entry.num_bytes;
                else
                    size = 4 * entry.num_samples;
            }
            else
                size = 2 * entry.num_bytes;

            printf("[%d] Hash: 0x%08X codec=%d num_samples=%d num_channels=%d rate=%dHz bps=%d length=%fs offs=0x%X\n", index + 1,
                entry.hash, entry.codec,
                GetNumSamples(entry), GetNumChannels(entry),
                entry.samples_per_second, bits_per_sample,
                GetDurationMs(entry), entry.compressed_data_offs);

            // PCM(?)
            if (entry.codec == 1 || entry.codec == 2) {
                stream.seekg(0x1000, std::ios::beg);
                std::vector<int16_t> tmp;
                for (int i = 0; i < size / 4; ++i) {
                    int16_t sample1, sample2;
                    stream.read(reinterpret_cast<char*>(&sample1), 2);
                    stream.read(reinterpret_cast<char*>(&sample2), 2);
                    tmp.push_back(sample1);
                    tmp.push_back(sample2);
                }
                tracks.push_back(tmp);
            }
            // IMA ADPCM
            else if (entry.codec == 7)
            {
                std::vector<uint8_t> bdata;
                stream.seekg(entry.compressed_data_offs, std::ios::beg);
                auto samples_size = entry.num_bytes;
                if (entry.compressed_data_offs + size > actual_file_size)
                    samples_size = actual_file_size - entry.compressed_data_offs;
                bdata.resize(samples_size);
                stream.read((char*)bdata.data(), samples_size);
                tracks.push_back(DecodeImaAdpcm(bdata));
            }
            // @todo: codecs 5 (BINK) and 4 remaining
            else
            {
                printf("Unsupported codec (%d)!\n", entry.codec);
            }
            entries.push_back(entry);
        }

        // read metadata
        if (header.metadata_offs) {
            size_t num_metadata = (header.entry_desc_offs - header.metadata_offs) / sizeof metadata_t;
            if (num_metadata) {
                stream.seekg(header.metadata_offs, std::ios::beg);
                for (int index = 0; index < num_metadata; ++index) {
                    metadata_t tmp_metadata;
                    stream.read(reinterpret_cast<char*>(&tmp_metadata), sizeof metadata_t);
                    if (tmp_metadata.codec != 0) {
                        metadata.push_back(tmp_metadata);
                        printf("metadata #%d\tcodec = %d\t", index + 1, tmp_metadata.codec);
                        for (int i = 0; i < 6; ++i)
                            printf("%f%s", tmp_metadata.unk_fvals[i], i != 5 ? ", " : "\n");
                    }
                }
            }
        }

        char desc[16] = { '\0' };
        stream.read(reinterpret_cast<char*>(&desc), 16);
        printf("Bank Type: %s\n", std::string(desc).c_str());
        stream.close();
    }
}
void WBK::write(std::filesystem::path path) {
    std::ofstream ofs(path, std::ios::binary);
    if (ofs.good()) {
        ofs.write((char*)raw_data.data(), raw_data.size());
        ofs.close();
    }
}



bool WBK::replace(int replacement_index, const WAV& wav)
{
    if (replacement_index < 0 || replacement_index >= header.num_entries)
        return false;

    // copy everything from the original up until the track data we want to replace
    std::vector<uint8_t> new_raw_data(raw_data.begin(), raw_data.begin() + entries[replacement_index].compressed_data_offs);
    
    // encode
    auto samples = wav.samples;
    auto encoded_adpcm = EncodeImaAdpcm(samples);

    // calc the next available data offset
    size_t current_data_offset = entries[replacement_index].compressed_data_offs;
    size_t next_data_offset = (current_data_offset + encoded_adpcm.size() + 0x7FFF) & ~0x7FFF;

    // adjust the replacement track info
    nslWave& entry = *reinterpret_cast<nslWave*>(&new_raw_data.data()[0x100 + (sizeof(nslWave) * replacement_index)]);
    entry.num_bytes = encoded_adpcm.size();
    entry.num_samples = wav.samples.size() / wav.header.numChannels;
    entry.samples_per_second = wav.header.sampleRate;
    SetNumChannels(entry, wav.header.numChannels);

    // 
    new_raw_data.insert(new_raw_data.end(), encoded_adpcm.begin(), encoded_adpcm.end());
    size_t padding_size = next_data_offset - (current_data_offset + encoded_adpcm.size());
    new_raw_data.insert(new_raw_data.end(), padding_size, 0x00);

    for (int index = replacement_index + 1; index < header.num_entries; ++index) 
    {
        nslWave& new_entry = *reinterpret_cast<nslWave*>(&new_raw_data.data()[0x100 + (sizeof(nslWave) * index)]);

        size_t data_start = entries[index].compressed_data_offs;
        size_t data_end = data_start + entries[index].num_bytes;
        new_entry.compressed_data_offs = data_start;

        new_raw_data.insert(new_raw_data.end(), raw_data.begin() + data_start, raw_data.begin() + data_end);
    }

    header_t& p_header = *reinterpret_cast<header_t*>(&new_raw_data.data()[0]);
    p_header.total_bytes = new_raw_data.size();
    raw_data = std::move(new_raw_data);

    return true;
}
