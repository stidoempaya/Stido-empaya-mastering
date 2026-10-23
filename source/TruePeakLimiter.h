#pragma once

#include <juce_dsp/juce_dsp.h>

// Stereo-linked true peak limiter with lookahead.
// Peaks are detected on the 4x oversampled signal (inter-sample peaks), the gain needed to stay under
// the ceiling is held over the lookahead window then smoothed by a moving average of the same length,
// so the gain is already down when the peak reaches the output. Release is exponential.
// Latency (lookahead + oversampling filter) is reported by getLatency().
class TruePeakLimiter
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        lookahead = juce::jmax (2, juce::roundToInt (sampleRate * 0.002));     // 2 ms

        oversampler.initProcessing ((size_t) maxBlock);
        delay = lookahead - 1 + (int) oversampler.getLatencyInSamples();

        delayLine.setSize (2, delay);
        requiredGain.assign ((size_t) lookahead, 1.0f);
        heldGain.assign ((size_t) lookahead, 1.0f);
        setRelease (releaseMs);
        reset();
    }

    void reset()
    {
        oversampler.reset();
        delayLine.clear();
        std::fill (requiredGain.begin(), requiredGain.end(), 1.0f);
        std::fill (heldGain.begin(), heldGain.end(), 1.0f);
        heldSum = (double) lookahead;
        pos = writePos = 0;
        gain = 1.0f;
    }

    int getLatency() const noexcept { return delay; }

    void setCeiling (float dB) noexcept { ceiling = juce::Decibels::decibelsToGain (dB); }

    void setRelease (float ms) noexcept
    {
        releaseMs = ms;
        if (sr > 0.0)
            releaseCoef = 1.0f - std::exp (-1.0f / (ms * 0.001f * (float) sr));
    }

    // limit = false: the audio is only delayed (bypass), so the timing stays the same
    void process (juce::AudioBuffer<float>& buffer, bool limit = true)
    {
        if (buffer.getNumChannels() < 2 || delay <= 0)
            return;

        juce::dsp::AudioBlock<float> block (buffer);
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        auto* dl = delayLine.getWritePointer (0);
        auto* dr = delayLine.getWritePointer (1);
        const int numSamples = buffer.getNumSamples();

        for (int start = 0; start < numSamples; start += maxBlock)
        {
            const int len = juce::jmin (maxBlock, numSamples - start);
            auto up = oversampler.processSamplesUp (block.getSubBlock ((size_t) start, (size_t) len));
            const int factor = (int) (up.getNumSamples() / (size_t) len);

            for (int i = 0; i < len; ++i)
            {
                // True peak of this sample period (all channels)
                float peak = 0.0f;
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const auto* u = up.getChannelPointer (ch) + i * factor;
                    for (int k = 0; k < factor; ++k)
                        peak = juce::jmax (peak, std::abs (u[k]));
                }

                // Gain needed, held over the lookahead window, then averaged over the same window
                requiredGain[(size_t) pos] = peak > ceiling ? ceiling / peak : 1.0f;
                const float held = *std::min_element (requiredGain.begin(), requiredGain.end());
                heldSum += held - heldGain[(size_t) pos];
                heldGain[(size_t) pos] = held;
                pos = (pos + 1) % lookahead;

                const float target = juce::jmin (1.0f, (float) (heldSum / lookahead));
                gain = target < gain ? target : gain + (target - gain) * releaseCoef;

                // Delayed audio, so the gain change happens before the peak
                const int s = start + i;
                const float g = limit ? gain : 1.0f;
                const float outL = dl[writePos] * g, outR = dr[writePos] * g;
                dl[writePos] = l[s];
                dr[writePos] = r[s];
                l[s] = outL;
                r[s] = outR;
                writePos = (writePos + 1) % delay;
            }
        }
    }

private:
    juce::dsp::Oversampling<float> oversampler { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    juce::AudioBuffer<float> delayLine;
    std::vector<float> requiredGain, heldGain;
    double sr = 0.0, heldSum = 0.0;
    int maxBlock = 512, lookahead = 96, delay = 0, pos = 0, writePos = 0;
    float ceiling = 1.0f, gain = 1.0f, releaseMs = 100.0f, releaseCoef = 0.001f;
};
