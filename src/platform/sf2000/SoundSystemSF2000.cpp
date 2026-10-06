#include "SoundSystemSF2000.h"
#include <cmath>
#include <cstring>

SoundSystemSF2000* SoundSystemSF2000::s_instance = nullptr;

SoundSystemSF2000::SoundSystemSF2000()
{
    s_instance = this;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        m_voices[i].active = false;
    }
}

SoundSystemSF2000::~SoundSystemSF2000()
{
    if (s_instance == this)
        s_instance = nullptr;
}

SoundSystemSF2000* SoundSystemSF2000::instance()
{
    return s_instance;
}

void SoundSystemSF2000::playAt(const SoundDesc& desc, float x, float y, float z, float volume, float pitch)
{
    if (!m_enabled || !desc.isValid() || volume <= 0.001f || desc.numFrames <= 0)
        return;

    if (pitch < 0.2f) pitch = 0.2f;
    if (pitch > 3.0f) pitch = 3.0f;

    // Panning relative to listener
    float pan = (x == 0.0f && z == 0.0f) ? 0.0f : (x / 16.0f);
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;

    float volL = volume * (1.0f - pan * 0.5f);
    float volR = volume * (1.0f + pan * 0.5f);
    if (volL > 1.0f) volL = 1.0f;
    if (volR > 1.0f) volR = 1.0f;

    // Find a free voice or steal the one with lowest volume
    int slot = -1;
    float lowestVol = 999.0f;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!m_voices[i].active)
        {
            slot = i;
            break;
        }
        float v = m_voices[i].volumeLeft + m_voices[i].volumeRight;
        if (v < lowestVol)
        {
            lowestVol = v;
            slot = i;
        }
    }
    if (slot < 0) return;

    Voice& v = m_voices[slot];
    v.pcm16 = (desc.byteWidth == 2) ? reinterpret_cast<const int16_t*>(desc.frames) : nullptr;
    v.pcm8  = (desc.byteWidth == 1) ? reinterpret_cast<const uint8_t*>(desc.frames) : nullptr;
    v.numFrames = desc.numFrames;
    v.channels = desc.channels;
    v.byteWidth = desc.byteWidth;
    v.currentPos = 0;

    // Step in Q16.16: (frameRate * pitch / OUTPUT_SAMPLE_RATE) * 65536
    uint32_t step = (uint32_t)(((uint64_t)desc.frameRate * (uint32_t)(pitch * 65536.0f)) / OUTPUT_SAMPLE_RATE);
    if (step == 0) step = 0x10000;
    v.step = step;

    v.volumeLeft = volL;
    v.volumeRight = volR;
    v.active = true;
}

void SoundSystemSF2000::mix(int16_t* output, size_t frames)
{
    if (!output || frames == 0) return;
    std::memset(output, 0, frames * 2 * sizeof(int16_t));

    for (int i = 0; i < MAX_VOICES; ++i)
    {
        Voice& v = m_voices[i];
        if (!v.active) continue;

        int32_t volL_q8 = (int32_t)(v.volumeLeft * 256.0f);
        int32_t volR_q8 = (int32_t)(v.volumeRight * 256.0f);

        for (size_t f = 0; f < frames; ++f)
        {
            uint32_t frameIdx = v.currentPos >> 16;
            if ((int)frameIdx >= v.numFrames)
            {
                v.active = false;
                break;
            }

            int32_t sampleL = 0;
            int32_t sampleR = 0;

            if (v.pcm16)
            {
                if (v.channels == 1)
                {
                    int16_t s = v.pcm16[frameIdx];
                    sampleL = s;
                    sampleR = s;
                }
                else
                {
                    sampleL = v.pcm16[frameIdx * 2 + 0];
                    sampleR = v.pcm16[frameIdx * 2 + 1];
                }
            }
            else if (v.pcm8)
            {
                if (v.channels == 1)
                {
                    int16_t s = (int16_t)((v.pcm8[frameIdx] - 128) << 8);
                    sampleL = s;
                    sampleR = s;
                }
                else
                {
                    sampleL = (int16_t)((v.pcm8[frameIdx * 2 + 0] - 128) << 8);
                    sampleR = (int16_t)((v.pcm8[frameIdx * 2 + 1] - 128) << 8);
                }
            }

            int32_t outL = output[f * 2 + 0] + ((sampleL * volL_q8) >> 8);
            int32_t outR = output[f * 2 + 1] + ((sampleR * volR_q8) >> 8);

            if (outL > 32767) outL = 32767;
            else if (outL < -32768) outL = -32768;
            if (outR > 32767) outR = 32767;
            else if (outR < -32768) outR = -32768;

            output[f * 2 + 0] = (int16_t)outL;
            output[f * 2 + 1] = (int16_t)outR;

            v.currentPos += v.step;
        }
    }
}
