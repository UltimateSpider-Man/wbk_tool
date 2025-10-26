#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <bitset>
#include <map>

#include "wav.h"
#include "adpcm1.h"
#include "adpcm2.h"

class WBK {
public:
    enum Codec : uint8_t {
        PCM = 1,
        PCM2,
        Reserved,
        ADPCM_1,    // @todo: codecs
        ADPCM_2,
        Reserved3,
        IMA_ADPCM,

        Keep = 255,
    };

#pragma pack(push, 1)
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
        Codec codec;
        char flags[3];
        uint32_t unk_vals;
        float unk_fvals[6];
    };

    struct nslWave
    {
        int hash;
        Codec codec;
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
#   pragma pack(pop)

    std::vector <nslWave> entries;
    std::vector<std::vector<int16_t>> tracks;
    std::vector<metadata_t> metadata;

    char bank_group[16] = { '\0' };

    static int GetNumChannels(const nslWave& wave);
    static void SetNumChannels(nslWave& wave, int num_channels);
    int GetNumSamples(const nslWave& wave);
    static void SetNumSamples(nslWave& wave, int num_samples);
    static int GetDuration(const nslWave& wave);
    static double GetDurationMs(const nslWave& wave);
    static int GetBytesPerSample(Codec codec);

    std::vector<uint8_t> encode(const WAV& wav, Codec codec = Keep);

    std::vector<int16_t> decode(std::vector<uint8_t> samples, const nslWave& entry);

    int parse(std::istream& stream, const bool DecodeTracks = true);
    void read(const std::vector<uint8_t>& data, const bool DecodeTracks = true);
    int read(std::filesystem::path path, const bool DecodeTracks = true);
    int write(std::filesystem::path path);
    int replace(int replacement_index, const WAV& wav, Codec codec = Keep);

private:
    std::vector<uint8_t> raw_data;
};


enum {
    WBK_OK,
    WBK_PARSE_FAILED,
    WBK_FILE_TOO_LARGE,
    WBK_WRITE_ERROR,
    WBK_INVALID_REPLACE_INDEX,
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

class membuf : public std::streambuf {
public:
    membuf(const char* base, size_t size) {
        begin_ = const_cast<char*>(base);
        end_ = begin_ + size;
        setg(begin_, begin_, end_);
    }
protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
        std::ios_base::openmode which) override {
        if (!(which & std::ios_base::in)) return pos_type(off_type(-1));
        char* newpos = nullptr;
        switch (dir) {
            case std::ios_base::beg: newpos = begin_ + off; break;
            case std::ios_base::cur: newpos = gptr() + off; break;
            case std::ios_base::end: newpos = end_ + off;   break;
            default: return pos_type(off_type(-1));
        }
        if (newpos < begin_ || newpos > end_) return pos_type(off_type(-1));
        setg(begin_, newpos, end_);
        return pos_type(newpos - begin_);
    }
    pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
        return seekoff(off_type(sp), std::ios_base::beg, which);
    }
private:
    char* begin_{ nullptr };
    char* end_{ nullptr };
};


void WBK::read(const std::vector<uint8_t>& data, const bool DecodeTracks)
{
    if (data.data() == raw_data.data() && !data.empty()) {
        std::vector<uint8_t> tmp(data);
        membuf sbuf(reinterpret_cast<const char*>(tmp.data()), tmp.size());
        std::istream stream(&sbuf);
        parse(stream, DecodeTracks);
        return;
    }

    membuf sbuf(reinterpret_cast<const char*>(data.data()), data.size());
    std::istream stream(&sbuf);
    parse(stream, DecodeTracks);
}

int WBK::read(std::filesystem::path path, const bool DecodeTracks)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.good()) throw std::runtime_error("Failed to open file");
    return parse(stream, DecodeTracks);
}
inline void WBK::SetNumChannels(nslWave& wave, int num_channels) {
    unsigned char channel_mask = 0xFF, new_channel_bits = 0;
    for (int i = 0; i < num_channels; ++i)
        new_channel_bits |= (1 << i);
    wave.flags = (wave.flags & ~channel_mask) | new_channel_bits;
}


inline int WBK::GetBytesPerSample(Codec codec) {
    switch (codec)
    {
        case WBK::PCM:  return 1;
        case WBK::PCM2: return 2;
        default:   return 2;
    }
}

inline int WBK::GetDuration(const nslWave& wave)
{
    const int sr = wave.samples_per_second ? wave.samples_per_second : 1;
    const int ch = GetNumChannels(wave);

    if (wave.num_samples > 0)
        return int(1000uLL * wave.num_samples / sr);

    const int bps = GetBytesPerSample(wave.codec);
    if (wave.num_bytes > 0) 
        return int(((uint64_t)1000uLL * wave.num_bytes) / (uint64_t(sr) * ch * bps));

    return 0;
}


inline double WBK::GetDurationMs(const nslWave& wave)
{
    return WBK::GetDuration(wave) * 0.001;
}

int WBK::GetNumSamples(const nslWave& wave)
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

void WBK::SetNumSamples(nslWave& wave, int num_samples)
{
    int active_channels = (int)std::bitset<8>(wave.flags).count();
    if (wave.codec == 1)
    {
        if (active_channels > 0)
            wave.num_bytes = num_samples / active_channels;
        else
            wave.num_bytes = num_samples;
    }
    else if (wave.codec == 2)
    {
        if (active_channels > 0)
            wave.num_bytes = num_samples / (2 * active_channels);
        else
            wave.num_bytes = num_samples / 2;
    }
    else
    {
        wave.num_samples = num_samples;
    }
}


int WBK::parse(std::istream& stream, const bool DecodeTracks)
{
    // stay fresh
    entries.clear();
    tracks.clear();
    metadata.clear();

    if (stream.good()) 
    {
        stream.seekg(0, std::ios::end);
        size_t actual_file_size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        raw_data.resize(actual_file_size);
        stream.read((char*)raw_data.data(), actual_file_size);

        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(&header), sizeof header_t);

        if (header.total_bytes >= INT_MAX) {
            printf("ERROR: Max file size, this WBK won't work in-game.\n");
            return WBK_FILE_TOO_LARGE;
        }

        const auto numEntries = header.num_entries;
        tracks.reserve(numEntries);
        entries.reserve(numEntries);

        // read all entries
        for (int32_t index = 0; index < numEntries; ++index) {
            nslWave entry;
            stream.seekg(sizeof header_t + (sizeof nslWave * index), std::ios::beg);
            stream.read(reinterpret_cast<char*>(&entry), sizeof nslWave);

            // calc bits per sample & blockAlign
            int bits_per_sample = 0;
            int size = 0;
            int blockAlign = 0;
            if (entry.codec == PCM || entry.codec == PCM2) {
                bits_per_sample = 8 * (entry.codec != PCM) + 8;
                blockAlign = (GetNumChannels(entry) * bits_per_sample) / 8;
            }
            else if (entry.codec == ADPCM_1) {
                bits_per_sample = 16;
                blockAlign = (bits_per_sample * GetNumChannels(entry)) / 8;
            }
            else if (entry.codec == ADPCM_2) {
                bits_per_sample = 4;
                blockAlign = 36 * GetNumChannels(entry);
            }
            else if (entry.codec == IMA_ADPCM) {
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

            int num_channels = GetNumChannels(entry);

#           if _DEBUG
                printf("[%d] Hash: 0x%08X codec=%d num_samples=%d num_channels=%d rate=%dHz bps=%d length=%fs offs=0x%X\n", index,
                    entry.hash, entry.codec,
                    GetNumSamples(entry), num_channels,
                    entry.samples_per_second, bits_per_sample,
                    GetDurationMs(entry), entry.compressed_data_offs);
#           endif

            entries.push_back(entry);

            if (!DecodeTracks)
                continue;

            if (entry.codec == PCM || entry.codec == PCM2) {        // @todo: not seen these yet, but this won't work (seeks to 0x1000)
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
            // both IMA ADPCM and ADPCM (and other variants)
            else if (entry.codec >= Reserved && entry.codec <= IMA_ADPCM) {
                if (entry.codec == ADPCM_2)
                    SetNumChannels(entry, 1);

                std::vector<uint8_t> bdata;
                stream.seekg(entry.compressed_data_offs, std::ios::beg);
                auto samples_size = entry.num_bytes;
                bdata.resize(samples_size);
                stream.read((char*)bdata.data(), samples_size);
                bdata.shrink_to_fit();

                auto decoded_samples = decode(bdata, entry);
                bdata.clear();
                decoded_samples.shrink_to_fit();
                tracks.push_back(decoded_samples);
            }
            else
                throw std::runtime_error((std::ostringstream{} << "Unsupported codec (" << entry.codec << ")").str());

        }

        // read metadata
        if (header.metadata_offs) {
            size_t num_metadata = (header.entry_desc_offs - header.metadata_offs) / sizeof metadata_t;
            if (num_metadata) {
                metadata.reserve(num_metadata);
                stream.seekg(header.metadata_offs, std::ios::beg);
                for (int index = 0; index < num_metadata; ++index) {
                    metadata_t tmp_metadata;
                    stream.read(reinterpret_cast<char*>(&tmp_metadata), sizeof metadata_t);
                    if (tmp_metadata.codec != 0) {
                        metadata.push_back(tmp_metadata);
#                       if _DEBUG
                            printf("metadata #%d\tcodec = %d\t", index + 1, tmp_metadata.codec);
                            for (int i = 0; i < 6; ++i)
                                printf("%f%s", tmp_metadata.unk_fvals[i], i != 5 ? ", " : "\n");
#                       endif
                    }
                }
            }
        }

        entries.shrink_to_fit();
        tracks.shrink_to_fit();

        char desc[16] = { '\0' };
        stream.read(reinterpret_cast<char*>(&bank_group), 16);
        if (desc[0] != 0)
            printf("Bank Type: %s\n", std::string(bank_group).c_str());
        return WBK_OK;
    }
    return WBK_PARSE_FAILED;
}

int WBK::write(std::filesystem::path path) {
    if (header.total_bytes >= INT_MAX)
        return WBK_FILE_TOO_LARGE;

    std::ofstream ofs(path, std::ios::binary);
    if (ofs.good()) {
        ofs.write((char*)raw_data.data(), raw_data.size());
        ofs.close();
        return WBK_OK;
    }
    return WBK_WRITE_ERROR;
}

std::vector<uint8_t> WBK::encode(const WAV& wav, Codec codec)
{
    std::vector<uint8_t> res;

    if (codec == IMA_ADPCM)
        res = EncodeImaAdpcm(wav.samples, wav.header.numChannels);
    else if (codec == ADPCM_1)
    {
        std::vector<int16_t> pcmSamples(wav.samples.size() / 2);
        std::memcpy(pcmSamples.data(), wav.samples.data(), wav.samples.size());

        res = EncodeAdpcm1(pcmSamples, wav.header.numChannels);
    }
    else if (codec == ADPCM_2)
    {
        std::vector<int16_t> pcmSamples(wav.samples.size() / 2);
        std::memcpy(pcmSamples.data(), wav.samples.data(), wav.samples.size());

        res = EncodeAdpcm2(pcmSamples, wav.header.numChannels);
    }

    return res;
}
std::vector<int16_t> WBK::decode(std::vector<uint8_t> samples, const nslWave& entry)
{
    std::vector<int16_t> decoded_samples(2 * samples.size());
    switch (entry.codec) {
        case ADPCM_1: {
            decoded_samples = DecodeAdpcm1(samples);
            break;
        }
        case ADPCM_2: {
            decoded_samples = DecodeAdpcm2(samples, GetNumChannels(entry));
            break;
        }
        case IMA_ADPCM: {
            decoded_samples = DecodeImaAdpcm(samples, GetNumChannels(entry));
            break;
        }
    }
    return decoded_samples;
}

int WBK::replace(int replacement_index, const WAV& wav, Codec codec)
{
    if (replacement_index < 0 || replacement_index >= header.num_entries)
        return WBK_INVALID_REPLACE_INDEX;

    const nslWave orig = entries[replacement_index];
    const Codec target_codec = (codec == Keep ? orig.codec : codec);

    // copy everything from the original up until the track data we want to replace
    std::vector<uint8_t> encoded_samples = encode(wav, target_codec);
    std::vector<uint8_t> new_raw_data(raw_data.begin(), raw_data.begin() + orig.compressed_data_offs);

    // insert the new track samples and calc the next available data offset
    size_t next_data_offset = (orig.compressed_data_offs + encoded_samples.size() + 0x7FFF) & ~size_t(0x7FFF);
    new_raw_data.insert(new_raw_data.end(), encoded_samples.begin(), encoded_samples.end());
    new_raw_data.insert(new_raw_data.end(), next_data_offset - new_raw_data.size(), 0x00);


    // for each entry after the replaced one, we insert it at the right location and modify its start offset.
    for (int index = replacement_index + 1; index < header.num_entries; ++index) 
    {
        size_t data_start = entries[index].compressed_data_offs;
        size_t data_end = (index + 1 != header.num_entries) ? 
                            entries[index + 1].compressed_data_offs : raw_data.size();
        size_t data_size = data_end - data_start;

        auto* new_entry = reinterpret_cast<nslWave*>(new_raw_data.data() + sizeof header_t + (sizeof nslWave * index));
        new_entry->compressed_data_offs = static_cast<int>(next_data_offset);

        new_raw_data.insert(new_raw_data.end(), raw_data.begin() + data_start, raw_data.begin() + data_end);
        next_data_offset = (next_data_offset + data_size + 0x7FFF) & ~size_t(0x7FFF);
        new_raw_data.insert(new_raw_data.end(), next_data_offset - new_raw_data.size(), 0x00);
    }


    // update codec
    auto* replaced = reinterpret_cast<nslWave*>(new_raw_data.data() + sizeof header_t + ( sizeof nslWave * replacement_index ));
    replaced->codec = target_codec;

    // update channels
    if (GetNumChannels(*replaced) != wav.header.numChannels)
        SetNumChannels(*replaced, wav.header.numChannels);

    // update sample rate
    replaced->samples_per_second = static_cast<unsigned short>(wav.header.sampleRate);

    if (target_codec == PCM || target_codec == PCM2) {
        replaced->num_bytes = static_cast<unsigned>(wav.samples.size());
        replaced->num_samples = GetNumSamples(*replaced);
    }
    else {
        replaced->num_bytes = static_cast<unsigned>(encoded_samples.size());
        const int ch = wav.header.numChannels ? wav.header.numChannels : 1;
        const int frames = int(wav.samples.size() / (2 * ch));
        replaced->num_samples = frames;
    }

    // update the total bytes and parse again
    reinterpret_cast<header_t*>(new_raw_data.data())->total_bytes = static_cast<int>(new_raw_data.size());
    raw_data.swap(new_raw_data);
    std::vector<uint8_t> tmp = raw_data; // yes, a copy, but only during replace()
    membuf sbuf(reinterpret_cast<const char*>(tmp.data()), tmp.size());
    std::istream s(&sbuf);
    parse(s, false);

    return WBK_OK;
}