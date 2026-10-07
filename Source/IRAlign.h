#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <complex>
#include <vector>

// Automatic time/polarity alignment of one IR against a reference IR, so blended IRs
// sum constructively instead of comb-filtering each other.
//
// Cross-correlates the start of both IRs (L+R summed, via FFT) and picks the lag with
// the largest |correlation| within +-maxLagSamples. Its sign gives the polarity (a
// negative peak means the candidate is inverted relative to the reference) and a
// parabolic fit around the peak gives a sub-sample lag -- at 44.1-96kHz a whole-sample
// error alone is enough to audibly dull the top end of two summed close-mic'd IRs.
namespace IRAlign
{
    struct Result
    {
        // How many samples the candidate must be delayed by to line up with the
        // reference. Negative = the candidate is late and must be moved earlier.
        double lagSamples = 0.0;
        bool invertPolarity = false;
        // Normalised peak correlation (0..1) -- how alike the two IRs are at the best
        // lag. Low values mean the alignment is a best guess, not a clear match.
        float confidence = 0.0f;
        bool valid = false;
    };

    inline std::vector<float> monoSum (const juce::AudioBuffer<float>& buffer, int numSamples)
    {
        std::vector<float> mono ((size_t) numSamples, 0.0f);
        const auto count = juce::jmin (numSamples, buffer.getNumSamples());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* src = buffer.getReadPointer (ch);
            for (int i = 0; i < count; ++i)
                mono[(size_t) i] += src[i];
        }
        return mono;
    }

    inline Result estimate (const juce::AudioBuffer<float>& reference,
                             const juce::AudioBuffer<float>& candidate,
                             int windowSamples, int maxLagSamples)
    {
        Result result;
        windowSamples = juce::jmin (windowSamples, juce::jmax (reference.getNumSamples(), candidate.getNumSamples()));
        if (windowSamples <= 0 || maxLagSamples <= 0)
            return result;

        const auto ref = monoSum (reference, windowSamples);
        const auto cand = monoSum (candidate, windowSamples);

        double refEnergy = 0.0, candEnergy = 0.0;
        for (int i = 0; i < windowSamples; ++i)
        {
            refEnergy += (double) ref[(size_t) i] * ref[(size_t) i];
            candEnergy += (double) cand[(size_t) i] * cand[(size_t) i];
        }
        if (refEnergy < 1.0e-12 || candEnergy < 1.0e-12)
            return result;

        // Zero-padded to at least 2x the window, so the circular correlation has no
        // wrap-around within the searched lag range.
        int order = 1;
        while ((1 << order) < 2 * windowSamples)
            ++order;
        const auto fftSize = 1 << order;

        using Complex = juce::dsp::Complex<float>;
        std::vector<Complex> refSpec ((size_t) fftSize), candSpec ((size_t) fftSize), product ((size_t) fftSize);
        std::vector<Complex> refTime ((size_t) fftSize), candTime ((size_t) fftSize), corr ((size_t) fftSize);
        for (int i = 0; i < windowSamples; ++i)
        {
            refTime[(size_t) i] = ref[(size_t) i];
            candTime[(size_t) i] = cand[(size_t) i];
        }

        juce::dsp::FFT fft (order);
        fft.perform (refTime.data(), refSpec.data(), false);
        fft.perform (candTime.data(), candSpec.data(), false);
        for (int i = 0; i < fftSize; ++i)
            product[(size_t) i] = refSpec[(size_t) i] * std::conj (candSpec[(size_t) i]);
        // corr[k] = sum_n ref[n + k] * cand[n]: a peak at k = L means the reference is
        // the candidate shifted L samples later. Negative lags wrap to fftSize + k.
        fft.perform (product.data(), corr.data(), true);

        const auto maxLag = juce::jmin (maxLagSamples, windowSamples - 1);
        const auto valueAt = [&] (int lag) { return corr[(size_t) ((lag + fftSize) % fftSize)].real(); };

        int bestLag = 0;
        float bestAbs = -1.0f;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            const auto v = std::abs (valueAt (lag));
            if (v > bestAbs)
            {
                bestAbs = v;
                bestLag = lag;
            }
        }

        const auto sign = valueAt (bestLag) < 0.0f ? -1.0f : 1.0f;
        double fraction = 0.0;
        if (bestLag > -maxLag && bestLag < maxLag)
        {
            const auto ym = sign * valueAt (bestLag - 1);
            const auto y0 = sign * valueAt (bestLag);
            const auto yp = sign * valueAt (bestLag + 1);
            const auto denom = ym - 2.0f * y0 + yp;
            if (std::abs (denom) > 1.0e-12f)
                fraction = juce::jlimit (-0.5, 0.5, 0.5 * (double) (ym - yp) / (double) denom);
        }

        // juce::dsp::FFT's inverse transform is already scaled by 1/fftSize.
        result.lagSamples = (double) bestLag + fraction;
        result.invertPolarity = sign < 0.0f;
        result.confidence = (float) juce::jlimit (0.0, 1.0, (double) bestAbs / std::sqrt (refEnergy * candEnergy));
        result.valid = true;
        return result;
    }
}
