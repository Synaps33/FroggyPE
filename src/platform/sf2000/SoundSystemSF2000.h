#pragma once

#include "platform/audio/SoundSystem.h"
#include "client/sound/Sound.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>

class SoundSystemSF2000 : public SoundSystem
{
public:
    static constexpr int MAX_VOICES = 8;
    static constexpr int OUTPUT_SAMPLE_RATE = 22050;

    struct Voice
    {
        const int16_t* pcm16 = nullptr;
        const uint8_t* pcm8 = nullptr;
        int numFrames = 0;
        int channels = 1;
        int byteWidth = 2;

        uint32_t currentPos = 0; // Q16.16
        uint32_t step = 0x10000;  // Q16.16

        float volumeLeft = 1.0f;
        float volumeRight = 1.0f;
        bool active = false;
    };

    SoundSystemSF2000();
    virtual ~SoundSystemSF2000();

    static SoundSystemSF2000* instance();

    bool isAvailable() override { return true; }
    void enable(bool status) override { m_enabled = status; }

    void setListenerPos(float x, float y, float z) override
    {
        m_listenerX = x;
        m_listenerY = y;
        m_listenerZ = z;
    }

    void setListenerAngle(float deg) override
    {
        m_listenerAngle = deg;
    }

    void playAt(const SoundDesc& desc, float x, float y, float z, float volume, float pitch) override;

    // Mix active voices into interleaved 16-bit stereo buffer
    void mix(int16_t* output, size_t frames);

private:
    static SoundSystemSF2000* s_instance;
    bool m_enabled = true;
    float m_listenerX = 0.0f;
    float m_listenerY = 0.0f;
    float m_listenerZ = 0.0f;
    float m_listenerAngle = 0.0f;

    Voice m_voices[MAX_VOICES];
};
