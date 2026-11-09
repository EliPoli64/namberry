// Offline render smoke test for NAMBerry.
//
// Usage:
//   NAMBerryOfflineTest [path/to/model.nam]
//
// Renders a 220 Hz sine through the full processor and checks that the output
// is finite, non-silent, not clipping wildly, and identical on both channels
// (the NAM amp path is mono-summed and copied to both outputs).

#include "PluginProcessor.h"

#include "namberry/Room.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <iostream>
#include <tuple>

namespace {

float estimateFrequency (const std::vector<float>& samples, double sampleRate)
{
    const int n = static_cast<int> (samples.size());

    if (n < 2048)
        return 0.0f;

    double mean = 0.0;
    for (float v : samples)
        mean += v;
    mean /= n;

    std::vector<double> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = samples[(size_t) i] - mean;

    const int minLag = juce::jmax (2, static_cast<int> (sampleRate / 1200.0));
    const int maxLag = juce::jmin (n / 2, static_cast<int> (sampleRate / 50.0));

    std::vector<double> correlation (static_cast<size_t> (maxLag + 1), 0.0);
    double maxCorr = -1.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0;
        double norm1 = 0.0;
        double norm2 = 0.0;

        for (int i = 0; i + lag < n; ++i)
        {
            corr += x[(size_t) i] * x[(size_t) (i + lag)];
            norm1 += x[(size_t) i] * x[(size_t) i];
            norm2 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)];
        }

        const double norm = std::sqrt (norm1 * norm2);

        if (norm > 0.0)
        {
            correlation[(size_t) lag] = corr / norm;
            maxCorr = juce::jmax (maxCorr, correlation[(size_t) lag]);
        }
    }

    if (maxCorr <= 0.0)
        return 0.0f;

    // Pick the first strong local maximum, not the highest one: a periodic
    // signal correlates almost equally well at every multiple of its period.
    int bestLag = 0;
    const double threshold = maxCorr * 0.8;

    for (int lag = minLag + 1; lag < maxLag; ++lag)
    {
        const double c = correlation[(size_t) lag];

        if (c >= threshold
            && c >= correlation[(size_t) (lag - 1)]
            && c >= correlation[(size_t) (lag + 1)])
        {
            bestLag = lag;
            break;
        }
    }

    if (bestLag <= 0)
        return 0.0f;

    return static_cast<float> (sampleRate / bestLag);
}

float goertzelMagnitude (const std::vector<float>& samples, double frequency, double sampleRate)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
    const double c = 2.0 * std::cos (w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;

    for (float v : samples)
    {
        s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }

    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return (float) (std::sqrt (juce::jmax (0.0, power)) / juce::jmax (1, (int) samples.size()));
}

float estimateZeroCrossingFrequency (const std::vector<float>& samples, double sampleRate)
{
    const int n = static_cast<int> (samples.size());

    if (n < 4)
        return 0.0f;

    double mean = 0.0;
    for (float v : samples)
        mean += v;
    mean /= n;

    double firstCrossing = -1.0;
    double lastCrossing = -1.0;
    int crossings = 0;

    for (int i = 1; i < n; ++i)
    {
        const double a = samples[(size_t) (i - 1)] - mean;
        const double b = samples[(size_t) i] - mean;

        if (a <= 0.0 && b > 0.0)
        {
            const double fraction = -a / (b - a);
            const double position = (i - 1) + fraction;

            if (firstCrossing < 0.0)
                firstCrossing = position;

            lastCrossing = position;
            ++crossings;
        }
    }

    if (crossings < 3 || lastCrossing <= firstCrossing)
        return 0.0f;

    return static_cast<float> (sampleRate * (crossings - 1)
                               / (lastCrossing - firstCrossing));
}

int runPitchTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double inputFrequency = 220.0;

    struct Case { int semitones; bool bypass; float mix; const char* name; };

    const Case cases[] = {
        { 12,  false, 1.0f, "+12 st" },
        { 7,   false, 1.0f, "+7 st" },
        { 5,   false, 1.0f, "+5 st" },
        { -5,  false, 1.0f, "-5 st" },
        { -12, false, 1.0f, "-12 st" },
        { 7,   false, 0.0f, "+7 st mix=0" },
        { 7,   true,  1.0f, "+7 st bypass" }
    };

    bool allPassed = true;

    // Latency should only be reported while pitch shifting is active.
    {
        namberry::DigitalCapo latencyCapo;
        juce::dsp::ProcessSpec latencySpec;
        latencySpec.sampleRate = sampleRate;
        latencySpec.maximumBlockSize = blockSize;
        latencySpec.numChannels = 2;
        latencyCapo.prepare (latencySpec);

        juce::AudioBuffer<float> latencyBuffer (2, blockSize);
        juce::MidiBuffer latencyMidi;

        const bool startsInactive = latencyCapo.getReportedLatencySamples() == 0;

        latencyBuffer.clear();
        latencyBuffer.setSample (0, 0, 1.0f);
        latencyBuffer.setSample (1, 0, 1.0f);
        latencyCapo.processBlock (latencyBuffer, latencyMidi);
        const bool immediateDry = std::abs (latencyBuffer.getSample (0, 0) - 1.0f) < 1.0e-6f;

        latencyCapo.setSemitones (7.0f);
        latencyCapo.setMix (1.0f);
        latencyCapo.setBypass (false);
        latencyBuffer.clear();
        latencyCapo.processBlock (latencyBuffer, latencyMidi);
        const bool activeReportsLatency = latencyCapo.getReportedLatencySamples()
                                       == latencyCapo.getLatencySamples()
                                       && latencyCapo.getReportedLatencySamples() > 0;

        latencyCapo.setSemitones (0.0f);
        for (int block = 0; block < 12; ++block)
        {
            latencyBuffer.clear();
            latencyCapo.processBlock (latencyBuffer, latencyMidi);
        }

        const bool inactiveAgain = latencyCapo.getReportedLatencySamples() == 0;
        const bool latencyPassed = startsInactive && immediateDry
                                && activeReportsLatency && inactiveAgain;
        allPassed = allPassed && latencyPassed;

        std::cout << "Stretch latency switching: inactive=" << startsInactive
                  << " immediate-dry=" << immediateDry
                  << " active=" << activeReportsLatency
                  << " inactive-again=" << inactiveAgain
                  << " " << (latencyPassed ? "PASS" : "FAIL") << "\n";
    }

    for (const auto& testCase : cases)
    {
        namberry::DigitalCapo capo;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        capo.prepare (spec);
        capo.setSemitones (static_cast<float> (testCase.semitones));
        capo.setMix (testCase.mix);
        capo.setBypass (testCase.bypass);

        if (&testCase == &cases[0])
            std::cout << "Stretch latency=" << capo.getLatencySamples()
                      << " samples ("
                      << 1000.0 * capo.getLatencySamples() / sampleRate
                      << " ms)\n";

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        const bool shifted = ! testCase.bypass && testCase.mix > 0.0f;
        const double expected = inputFrequency
            * std::pow (2.0, (shifted ? testCase.semitones : 0) / 12.0);

        std::vector<float> captured;
        captured.reserve (32768);

        double phase = 0.0;
        const int totalBlocks = 300;
        const int skipSamples = capo.getLatencySamples() + 48000 / 2;

        for (int block = 0; block < totalBlocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * inputFrequency / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            capo.processBlock (buffer, midi);

            if (block * blockSize >= skipSamples)
            {
                for (int i = 0; i < blockSize && captured.size() < 32768; ++i)
                    captured.push_back (buffer.getSample (0, i));
            }
        }

        const float measured = estimateFrequency (captured, sampleRate);
        const float zeroCrossing = estimateZeroCrossingFrequency (captured, sampleRate);

        constexpr int fftOrder = 13;
        constexpr int fftSize = 1 << fftOrder;
        std::vector<float> fftData (static_cast<size_t> (fftSize * 2), 0.0f);

        for (int i = 0; i < fftSize && i < (int) captured.size(); ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (
                2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
            fftData[(size_t) i] = captured[(size_t) i] * window;
        }

        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        int peakBin = 1;
        for (int i = 2; i < fftSize / 2; ++i)
            if (fftData[(size_t) i] > fftData[(size_t) peakBin])
                peakBin = i;

        const float fftFrequency = (float) peakBin * (float) sampleRate / (float) fftSize;

        const float primary = zeroCrossing > 0.0f ? zeroCrossing : measured;
        const float cents = primary > 0.0f
            ? 1200.0f * std::log2 (primary / (float) expected)
            : 9999.0f;

        const float magExpected = goertzelMagnitude (captured, expected, sampleRate);
        const float magInput = goertzelMagnitude (captured, inputFrequency, sampleRate);

        const bool harmonicOk = shifted
            ? magExpected > magInput
            : magExpected >= magInput * 0.99f;

        const bool passed = std::abs (cents) < 30.0f && harmonicOk;
        allPassed = allPassed && passed;

        std::cout << "Stretch " << testCase.name
                  << ": expected " << expected
                  << " Hz, zero-cross " << zeroCrossing
                  << " Hz, autocorr " << measured
                  << " Hz (" << cents << " cents), FFT " << fftFrequency
                  << " Hz, magExp=" << magExpected
                  << " magIn=" << magInput
                  << " " << (passed ? "PASS" : "FAIL") << "\n";
    }

    std::cout << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 2;
}

int runPitchLowNoteTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    struct Case { double frequency; int semitones; };
    const Case cases[] = {
        { 82.41,   7 },
        { 82.41, -12 },
        { 110.0,  -5 }
    };

    bool allPassed = true;

    for (const auto& testCase : cases)
    {
        namberry::DigitalCapo capo;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        capo.prepare (spec);
        capo.setSemitones (static_cast<float> (testCase.semitones));
        capo.setMix (1.0f);
        capo.setBypass (false);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        std::vector<float> captured;
        captured.reserve (32768);
        double inputEnergy = 0.0;
        double outputEnergy = 0.0;

        const int totalBlocks = 320;
        const int skipSamples = capo.getLatencySamples() + 24000;

        for (int block = 0; block < totalBlocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * testCase.frequency / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
                inputEnergy += static_cast<double> (s) * s;
            }

            capo.processBlock (buffer, midi);

            if (block * blockSize >= skipSamples)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float sample = buffer.getSample (0, i);
                    outputEnergy += static_cast<double> (sample) * sample;

                    if (captured.size() < 32768)
                        captured.push_back (sample);
                }
            }
        }

        const double expected = testCase.frequency
            * std::pow (2.0, testCase.semitones / 12.0);
        const float measured = estimateFrequency (captured, sampleRate);
        const float zeroCrossing = estimateZeroCrossingFrequency (captured, sampleRate);

        constexpr int fftOrder = 14;
        constexpr int fftSize = 1 << fftOrder;
        std::vector<float> fftData (static_cast<size_t> (fftSize * 2), 0.0f);

        for (int i = 0; i < fftSize && i < (int) captured.size(); ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (
                2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
            fftData[(size_t) i] = captured[(size_t) i] * window;
        }

        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        int peakBin = 1;
        for (int i = 2; i < fftSize / 2; ++i)
            if (fftData[(size_t) i] > fftData[(size_t) peakBin])
                peakBin = i;

        const float fftFrequency = (float) peakBin * (float) sampleRate / (float) fftSize;
        const float primary = fftFrequency;
        const float cents = primary > 0.0f
            ? 1200.0f * std::log2 (primary / static_cast<float> (expected))
            : 9999.0f;

        const double energyRatio = inputEnergy > 0.0
            ? std::sqrt (outputEnergy / inputEnergy)
            : 0.0;
        const bool passed = std::abs (cents) < 40.0f && energyRatio > 0.25;

        allPassed = allPassed && passed;
        std::cout << "low pitch " << testCase.frequency << " Hz "
                  << testCase.semitones << " st: expected " << expected
                  << " Hz, zero-cross " << zeroCrossing
                  << " Hz, autocorr " << measured
                  << " Hz, FFT " << fftFrequency
                  << " Hz (" << cents << " cents), energy=" << energyRatio
                  << " " << (passed ? "PASS" : "FAIL") << "\n";
    }

    std::cout << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 2;
}

int runPitchBlockSizeTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr double inputFrequency = 220.0;
    const int blockSizes[] = { 128, 192, 256, 512 };

    bool allPassed = true;

    for (int blockSize : blockSizes)
    {
        namberry::DigitalCapo capo;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 128; // deliberately smaller than the first test block
        spec.numChannels = 2;
        capo.prepare (spec);
        capo.setSemitones (7.0f);
        capo.setMix (1.0f);
        capo.setBypass (false);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        std::vector<float> captured;
        captured.reserve (32768);
        bool finite = true;

        for (int block = 0; block < 220; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * inputFrequency / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            capo.processBlock (buffer, midi);

            for (int i = 0; i < blockSize; ++i)
                finite = finite && std::isfinite (buffer.getSample (0, i));

            if (block * blockSize >= 24000)
                for (int i = 0; i < blockSize && captured.size() < 32768; ++i)
                    captured.push_back (buffer.getSample (0, i));
        }

        const float measured = estimateZeroCrossingFrequency (captured, sampleRate);
        const double expected = inputFrequency * std::pow (2.0, 7.0 / 12.0);
        const float cents = measured > 0.0f
            ? 1200.0f * std::log2 (measured / static_cast<float> (expected))
            : 9999.0f;

        const bool passed = finite && std::abs (cents) < 40.0f;
        allPassed = allPassed && passed;

        std::cout << "pitch block " << blockSize
                  << ": " << measured << " Hz (" << cents << " cents)"
                  << " " << (passed ? "PASS" : "FAIL") << "\n";
    }

    return allPassed ? 0 : 2;
}

int runPitchRobustnessTest()
{
    const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
    const int hostBlocks[] = { 32, 64, 96, 128, 256, 512, 1024, 2048 };
    const namberry::PitchShifter::QualityMode modes[] = {
        namberry::PitchShifter::QualityMode::lowLatency,
        namberry::PitchShifter::QualityMode::quality
    };

    bool allPassed = true;

    for (double sampleRate : sampleRates)
    {
        for (auto quality : modes)
        {
            namberry::DigitalCapo capo;
            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sampleRate;
            spec.maximumBlockSize = 2048;
            spec.numChannels = 2;
            capo.prepare (spec);
            capo.setQualityMode (quality);
            capo.setSemitones (7.0f);
            capo.setTone (0.0f); // isolate engine robustness from tone matching
            capo.setMix (1.0f);
            capo.setBypass (false);

            juce::AudioBuffer<float> buffer (2, 2048);
            juce::MidiBuffer midi;
            std::vector<float> captured;
            captured.reserve (static_cast<size_t> (sampleRate * 3.0));
            double phase = 0.0;
            int total = 0;
            int blockIndex = 0;

            while (total < static_cast<int> (sampleRate * 3.0))
            {
                const int numSamples = hostBlocks[blockIndex++ % 8];
                const int chunk = std::min (numSamples, buffer.getNumSamples());

                for (int i = 0; i < chunk; ++i)
                {
                    const float value = 0.2f * static_cast<float> (
                        std::sin (2.0 * juce::MathConstants<double>::pi * 82.41
                                  * (total + i) / sampleRate));
                    buffer.setSample (0, i, value);
                    buffer.setSample (1, i, value);
                    phase += 1.0;
                }

                if (chunk < buffer.getNumSamples())
                    buffer.clear (0, chunk, buffer.getNumSamples() - chunk);

                juce::AudioBuffer<float> slice (2, chunk);
                for (int ch = 0; ch < 2; ++ch)
                    slice.copyFrom (ch, 0, buffer, ch, 0, chunk);

                capo.processBlock (slice, midi);

                for (int i = 0; i < chunk; ++i)
                {
                    const float value = slice.getSample (0, i);
                    captured.push_back (value);
                    allPassed = allPassed && std::isfinite (value);
                }

                total += chunk;
            }

            const int skip = juce::jmax (capo.getLatencySamples(),
                                         static_cast<int> (sampleRate * 0.5));
            std::vector<float> analysis (captured.begin() + std::min<size_t> (
                static_cast<size_t> (skip), captured.size()), captured.end());

            const float measured = estimateZeroCrossingFrequency (analysis, sampleRate);
            const double expected = 82.41 * std::pow (2.0, 7.0 / 12.0);
            const float cents = measured > 0.0f
                ? 1200.0f * std::log2 (measured / static_cast<float> (expected))
                : 9999.0f;

            double outputEnergy = 0.0;
            for (float value : analysis)
                outputEnergy += static_cast<double> (value) * value;

            const double energyRatio = std::sqrt (outputEnergy / analysis.size()) / 0.1414;
            const bool passed = std::abs (cents) < 20.0f && energyRatio > 0.5;
            allPassed = allPassed && passed;

            std::cout << "pitch robust sr=" << sampleRate
                      << " quality=" << (quality == namberry::PitchShifter::QualityMode::quality ? "HQ" : "fast")
                      << " latency=" << capo.getLatencySamples()
                      << " freq=" << measured
                      << " cents=" << cents
                      << " energy=" << energyRatio
                      << " " << (passed ? "PASS" : "FAIL") << "\n";
        }
    }

    std::cout << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 2;
}

int runDetectorTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    const double frequencies[] = { 82.41, 110.0, 146.83, 220.0, 329.63, 440.0 };

    bool allPassed = true;

    for (double expected : frequencies)
    {
        namberry::DropTuner tuner;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        tuner.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int block = 0; block < 220; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * (float) std::sin (phase);
                phase += 2.0 * juce::MathConstants<double>::pi * expected / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            tuner.processBlock (buffer, midi);
        }

        const float detected = tuner.getDetectedFrequency();
        const float cents = detected > 0.0f
            ? 1200.0f * std::log2 (detected / (float) expected)
            : 9999.0f;

        const bool passed = std::abs (cents) < 25.0f;
        allPassed = allPassed && passed;

        std::cout << "detector " << expected << " Hz -> "
                  << detected << " Hz (" << cents << " cents), period "
                  << tuner.getDetectedPeriodSamples()
                  << ", confidence " << tuner.getDetectionConfidence()
                  << " " << (passed ? "PASS" : "FAIL") << "\n";
    }

    std::cout << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 2;
}

int runDetectedStageTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    const double frequencies[] = { 82.41, 110.0, 220.0 };
    const int semitones[] = { 7, -5, -12 };

    bool allPassed = true;

    for (int semis : semitones)
    {
        for (double inputFrequency : frequencies)
        {
            namberry::DropTuner tuner;
            namberry::DigitalCapo capo;
            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sampleRate;
            spec.maximumBlockSize = blockSize;
            spec.numChannels = 2;
            tuner.prepare (spec);
            capo.prepare (spec);
            capo.setSemitones ((float) semis);
            capo.setMix (1.0f);
            capo.setBypass (false);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            double phase = 0.0;
            std::vector<float> captured;
            captured.reserve (32768);

            const int skipSamples = capo.getLatencySamples() + 24000;

            for (int block = 0; block < 300; ++block)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float s = 0.2f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi
                           * inputFrequency / sampleRate;
                    buffer.setSample (0, i, s);
                    buffer.setSample (1, i, s);
                }

                tuner.processBlock (buffer, midi);
                capo.processBlock (buffer, midi);

                if (block * blockSize >= skipSamples)
                {
                    for (int i = 0; i < blockSize && captured.size() < 32768; ++i)
                        captured.push_back (buffer.getSample (0, i));
                }
            }

            const double expected = inputFrequency
                * std::pow (2.0, semis / 12.0);
            const float measured = estimateZeroCrossingFrequency (captured, sampleRate);
            const float cents = measured > 0.0f
                ? 1200.0f * std::log2 (measured / (float) expected)
                : 9999.0f;

            const float magExpected = goertzelMagnitude (captured, expected, sampleRate);
            const float magInput = goertzelMagnitude (captured, inputFrequency, sampleRate);

            const bool passed = std::abs (cents) < 50.0f
                             && magExpected > magInput;
            allPassed = allPassed && passed;

            std::cout << "Stretch " << semis << " st @ "
                      << inputFrequency << " Hz -> " << measured
                      << " Hz (" << cents << " cents), magExp=" << magExpected
                      << " magIn=" << magInput << " "
                      << (passed ? "PASS" : "FAIL") << "\n";
        }
    }

    std::cout << (allPassed ? "PASS" : "FAIL") << "\n";
    return allPassed ? 0 : 2;
}

int runDryCenterTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    namberry::DigitalCapo capo;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = blockSize;
    spec.numChannels = 2;
    capo.prepare (spec);
    capo.setSemitones (0.0f);
    capo.setMix (0.0f);
    capo.setBypass (false);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double phaseL = 0.0, phaseR = 0.0;
    float maxDiff = 0.0f;

    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, 0.2f * (float) std::sin (phaseL));
            buffer.setSample (1, i, 0.2f * (float) std::sin (phaseR));
            phaseL += 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
            phaseR += 2.0 * juce::MathConstants<double>::pi * 330.0 / sampleRate;
        }

        capo.processBlock (buffer, midi);

        if (block > 4)
        {
            for (int i = 0; i < blockSize; ++i)
                maxDiff = juce::jmax (maxDiff,
                    std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
        }
    }

    std::cout << "dry centre max L/R difference = " << maxDiff << " "
              << (maxDiff < 1.0e-6f ? "PASS" : "FAIL") << "\n";

    return maxDiff < 1.0e-6f ? 0 : 2;
}

int runReferenceTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr double inputFrequency = 432.0;

    auto detect = [] (float reference)
    {
        namberry::DropTuner tuner;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        tuner.prepare (spec);
        tuner.setReferenceFrequency (reference);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;

        for (int block = 0; block < 220; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * inputFrequency / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            tuner.processBlock (buffer, midi);
        }

        return std::make_tuple (tuner.getDetectedNote(),
                                tuner.getCentsDeviation(),
                                tuner.getReferenceFrequency());
    };

    const auto at432 = detect (432.0f);
    const auto at440 = detect (440.0f);

    const bool pass432 = std::abs (std::get<1> (at432)) < 2.0f;
    const bool pass440 = std::abs (std::get<1> (at440) + 31.8f) < 2.0f;
    const bool noteOk = std::get<0> (at432) == "A4" && std::get<0> (at440) == "A4";
    const bool passed = pass432 && pass440 && noteOk;

    std::cout << "ref 432: note=" << std::get<0> (at432)
              << " cents=" << std::get<1> (at432)
              << " | ref 440: note=" << std::get<0> (at440)
              << " cents=" << std::get<1> (at440)
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runReverbDryTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 4096;

    namberry::PostEffects fx;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = blockSize;
    spec.numChannels = 2;
    fx.prepare (spec);

    fx.setBypass (false);
    fx.setChorusBypass (true);
    fx.setFlangerBypass (true);
    fx.setPhaserBypass (true);
    fx.setRoomBlend (0.0f);
    fx.setDelayBypass (true);
    fx.setReverbBypass (false);

    auto directAtMix = [&] (float mix)
    {
        fx.setReverbMix (mix);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int settle = 0; settle < 3; ++settle)
        {
            buffer.clear();
            fx.processBlock (buffer, midi);
        }

        buffer.clear();
        buffer.setSample (0, 0, 0.5f);
        buffer.setSample (1, 0, 0.5f);
        fx.processBlock (buffer, midi);
        return buffer.getSample (0, 0);
    };

    const float dryAtZero = directAtMix (0.0f);
    const float dryAtTwenty = directAtMix (0.2f);

    const bool dryUnity = std::abs (dryAtZero - 0.5f) < 1.0e-4f;
    const bool dryNotBoosted = dryAtTwenty <= 0.5f * 1.01f && dryAtTwenty > 0.25f;
    const bool passed = dryUnity && dryNotBoosted;

    std::cout << "reverb dry0=" << dryAtZero
              << " dry20=" << dryAtTwenty
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runDelayFeedbackTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 4096;
    constexpr int delaySamples = 2400; // 50 ms

    auto echoEnergies = [&] (float feedback)
    {
        namberry::PostEffects fx;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        fx.prepare (spec);
        fx.setBypass (false);
        fx.setChorusBypass (true);
        fx.setFlangerBypass (true);
        fx.setPhaserBypass (true);
        fx.setRoomBlend (0.0f);
        fx.setReverbBypass (true);
        fx.setDelayBypass (false);
        fx.setDelayMix (1.0f);
        fx.setDelayTimeMs (50.0f);
        fx.setDelayFeedback (feedback);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
        fx.processBlock (buffer, midi);

        float first = 0.0f;
        for (int i = delaySamples - 3; i < delaySamples + 4; ++i)
            first += std::abs (buffer.getSample (0, i));

        buffer.clear();
        fx.processBlock (buffer, midi);

        float second = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            second += std::abs (buffer.getSample (0, i));

        return std::pair<float, float> { first, second };
    };

    const auto zero = echoEnergies (0.0f);
    const auto max = echoEnergies (10.0f);

    const bool oneRepeat = zero.first > 0.5f && zero.second < 1.0e-5f;
    const bool longTail = max.first > 0.5f && max.second > 0.5f;
    const bool passed = oneRepeat && longTail;

    std::cout << "delay feedback 0: first=" << zero.first << " second=" << zero.second
              << " | 10: first=" << max.first << " second=" << max.second
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runDelayResetTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 4096;

    namberry::PostEffects fx;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = blockSize;
    spec.numChannels = 2;
    fx.prepare (spec);
    fx.setBypass (false);
    fx.setDelayBypass (false);
    fx.setDelayMix (1.0f);
    fx.setDelayTimeMs (50.0f);
    fx.setDelayFeedback (8.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);
    fx.processBlock (buffer, midi);

    float echoEnergy = 0.0f;
    for (int i = 2400; i < blockSize; ++i)
        echoEnergy += std::abs (buffer.getSample (0, i));

    // Switch the delay off: this must discard the existing feedback tail.
    fx.setDelayBypass (true);

    buffer.clear();
    fx.processBlock (buffer, midi);

    // Re-enable with silence: no old echo may reappear.
    fx.setDelayBypass (false);
    buffer.clear();
    fx.processBlock (buffer, midi);

    float remaining = 0.0f;
    for (int i = 0; i < blockSize; ++i)
        remaining += std::abs (buffer.getSample (0, i));

    const bool passed = echoEnergy > 1.0e-5f && remaining < 1.0e-7f;

    std::cout << "delay echo energy=" << echoEnergy
              << " after bypass=" << remaining
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

struct RoomRender
{
    std::vector<float> left;
    std::vector<float> right;
};

RoomRender renderRoom (namberry::Room& room,
                       float size,
                       float blend,
                       const std::vector<float>& inputLeft,
                       const std::vector<float>& inputRight)
{
    const int numSamples = static_cast<int> (
        juce::jmin (inputLeft.size(), inputRight.size()));

    room.setSize (size);
    room.setBlend (blend);
    room.reset();

    juce::AudioBuffer<float> buffer (2, numSamples);
    buffer.copyFrom (0, 0, inputLeft.data(), numSamples);
    buffer.copyFrom (1, 0, inputRight.data(), numSamples);

    juce::MidiBuffer midi;
    room.processBlock (buffer, midi);

    RoomRender result;
    result.left.assign (buffer.getReadPointer (0),
                        buffer.getReadPointer (0) + numSamples);
    result.right.assign (buffer.getReadPointer (1),
                         buffer.getReadPointer (1) + numSamples);
    return result;
}

double windowEnergy (const std::vector<float>& samples,
                     double sampleRate,
                     double startSeconds,
                     double endSeconds)
{
    const int start = juce::jlimit (0, static_cast<int> (samples.size()),
                                    static_cast<int> (std::round (startSeconds * sampleRate)));
    const int end = juce::jlimit (start, static_cast<int> (samples.size()),
                                  static_cast<int> (std::round (endSeconds * sampleRate)));

    double sum = 0.0;
    for (int i = start; i < end; ++i)
        sum += static_cast<double> (samples[static_cast<size_t> (i)])
             * static_cast<double> (samples[static_cast<size_t> (i)]);

    return sum;
}

float windowRms (const std::vector<float>& samples,
                 double sampleRate,
                 double startSeconds,
                 double endSeconds)
{
    const int start = juce::jlimit (0, static_cast<int> (samples.size()),
                                    static_cast<int> (std::round (startSeconds * sampleRate)));
    const int end = juce::jlimit (start, static_cast<int> (samples.size()),
                                  static_cast<int> (std::round (endSeconds * sampleRate)));

    const int count = juce::jmax (1, end - start);
    return static_cast<float> (std::sqrt (
        windowEnergy (samples, sampleRate, startSeconds, endSeconds) / count));
}

int firstSampleAbove (const std::vector<float>& samples, float threshold)
{
    for (int i = 0; i < static_cast<int> (samples.size()); ++i)
        if (std::abs (samples[static_cast<size_t> (i)]) > threshold)
            return i;

    return -1;
}

double highFrequencyRatio (const std::vector<float>& samples,
                           double sampleRate,
                           double startSeconds,
                           double endSeconds)
{
    const int start = juce::jlimit (0, static_cast<int> (samples.size()),
                                    static_cast<int> (std::round (startSeconds * sampleRate)));
    const int end = juce::jlimit (start, static_cast<int> (samples.size()),
                                  static_cast<int> (std::round (endSeconds * sampleRate)));

    double differenceEnergy = 0.0;
    double totalEnergy = 0.0;

    for (int i = start + 1; i < end; ++i)
    {
        const double current = samples[static_cast<size_t> (i)];
        const double previous = samples[static_cast<size_t> (i - 1)];
        const double difference = current - previous;
        differenceEnergy += difference * difference;
        totalEnergy += current * current;
    }

    return totalEnergy > 1.0e-20 ? std::sqrt (differenceEnergy / totalEnergy) : 0.0;
}

void makeRoomNoise (std::vector<float>& left,
                    std::vector<float>& right,
                    float amplitude)
{
    juce::Random random (0x51a7);
    float stateLeft = 0.0f;
    float stateRight = 0.0f;

    for (size_t i = 0; i < left.size(); ++i)
    {
        stateLeft += 0.25f * ((random.nextFloat() * 2.0f - 1.0f) - stateLeft);
        stateRight += 0.25f * ((random.nextFloat() * 2.0f - 1.0f) - stateRight);

        left[i] = stateLeft * amplitude;
        right[i] = stateRight * amplitude;
    }
}

double stereoCorrelation (const RoomRender& render,
                          double sampleRate,
                          double startSeconds,
                          double endSeconds)
{
    const int start = juce::jlimit (0, static_cast<int> (render.left.size()),
                                    static_cast<int> (std::round (startSeconds * sampleRate)));
    const int end = juce::jlimit (start, static_cast<int> (render.left.size()),
                                  static_cast<int> (std::round (endSeconds * sampleRate)));

    double sumLR = 0.0;
    double sumLL = 0.0;
    double sumRR = 0.0;

    for (int i = start; i < end; ++i)
    {
        const double left = render.left[static_cast<size_t> (i)];
        const double right = render.right[static_cast<size_t> (i)];
        sumLR += left * right;
        sumLL += left * left;
        sumRR += right * right;
    }

    const double denominator = std::sqrt (sumLL * sumRR);
    return denominator > 1.0e-20 ? sumLR / denominator : 1.0;
}

int runRoomTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int totalSamples = static_cast<int> (sampleRate * 2.0);

    namberry::Room room;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    room.prepare (spec);

    std::vector<float> impulseLeft (totalSamples, 0.0f);
    std::vector<float> impulseRight (totalSamples, 0.0f);
    impulseLeft[0] = 1.0f;
    impulseRight[0] = 1.0f;

    bool passed = true;

    const auto check = [&passed] (bool condition, const char* name)
    {
        if (! condition)
        {
            std::cout << "room " << name << " FAIL\n";
            passed = false;
        }
    };

    // Room Mix 0 % must be completely dry and must not run the room.
    const auto dry = renderRoom (room, 0.45f, 0.0f, impulseLeft, impulseRight);
    check (std::abs (dry.left[0] - 1.0f) < 1.0e-6f, "dry-direct");

    double dryResidue = 0.0;
    for (int i = 1; i < totalSamples; ++i)
        dryResidue += std::abs (dry.left[static_cast<size_t> (i)]);
    check (dryResidue < 1.0e-9, "dry-residue");

    // Equal-power dry law. At sample 0 the wet path has not arrived yet, so
    // the direct output is exactly the dry gain.
    for (const float mix : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const auto rendered = renderRoom (room, 0.45f, mix, impulseLeft, impulseRight);
        const float expected = std::cos (juce::MathConstants<float>::halfPi * mix);
        check (std::abs (rendered.left[0] - expected) < 1.0e-4f, "equal-power");
    }

    const auto small = renderRoom (room, 0.0f, 1.0f, impulseLeft, impulseRight);
    const auto large = renderRoom (room, 1.0f, 1.0f, impulseLeft, impulseRight);

    const int smallOnset = firstSampleAbove (small.left, 1.0e-4f);
    const int largeOnset = firstSampleAbove (large.left, 1.0e-4f);

    check (smallOnset > static_cast<int> (0.001 * sampleRate), "predelay-small");
    check (smallOnset < static_cast<int> (0.006 * sampleRate), "predelay-small-short");
    check (largeOnset > static_cast<int> (0.008 * sampleRate), "predelay-large");
    check (largeOnset > smallOnset + static_cast<int> (0.005 * sampleRate),
           "predelay-macro");

    // The top of the Size range must ring noticeably longer than the bottom.
    const double smallLate = windowEnergy (small.left, sampleRate, 0.10, 0.20);
    const double largeLate = windowEnergy (large.left, sampleRate, 0.10, 0.20);
    check (largeLate > 1.0e-7, "decay-length");
    check (smallLate < largeLate * 0.01, "decay-scaling");
    check (windowEnergy (small.left, sampleRate, 0.50, 0.70) < 1.0e-9, "decay-small");
    check (windowEnergy (large.left, sampleRate, 0.50, 0.70) < largeLate * 0.01,
           "decay-large");

    // HF damping: the late tail should be darker than the early tail.
    const double earlyHf = highFrequencyRatio (large.left, sampleRate, 0.02, 0.06);
    const double lateHf = highFrequencyRatio (large.left, sampleRate, 0.15, 0.25);
    check (lateHf < earlyHf * 0.85, "hf-damping");

    // Level should stay close as Size moves from 0 to 100 %.
    std::vector<float> noiseLeft (static_cast<size_t> (totalSamples));
    std::vector<float> noiseRight (static_cast<size_t> (totalSamples));
    makeRoomNoise (noiseLeft, noiseRight, 0.25f);

    const float rmsSmall = windowRms (renderRoom (room, 0.0f, 1.0f, noiseLeft, noiseRight).left,
                                      sampleRate, 0.20, 0.40);
    const float rmsMid = windowRms (renderRoom (room, 0.5f, 1.0f, noiseLeft, noiseRight).left,
                                    sampleRate, 0.20, 0.40);
    const float rmsLarge = windowRms (renderRoom (room, 1.0f, 1.0f, noiseLeft, noiseRight).left,
                                      sampleRate, 0.20, 0.40);

    const float sizeMin = juce::jmin (rmsSmall, rmsMid, rmsLarge);
    const float sizeMax = juce::jmax (rmsSmall, rmsMid, rmsLarge);
    check (sizeMin > 1.0e-5f && sizeMax < sizeMin * 1.5f, "size-level");

    // Equal-power mix should not dip in the middle.
    const float mixDry = windowRms (renderRoom (room, 0.5f, 0.0f, noiseLeft, noiseRight).left,
                                    sampleRate, 0.20, 0.40);
    const float mixHalf = windowRms (renderRoom (room, 0.5f, 0.5f, noiseLeft, noiseRight).left,
                                     sampleRate, 0.20, 0.40);
    const float mixWet = windowRms (renderRoom (room, 0.5f, 1.0f, noiseLeft, noiseRight).left,
                                    sampleRate, 0.20, 0.40);

    const float mixMin = juce::jmin (mixDry, mixHalf, mixWet);
    const float mixMax = juce::jmax (mixDry, mixHalf, mixWet);
    check (mixMin > 1.0e-5f && mixMax < mixMin * 1.5f, "mix-level");

    // The smallest setting is deliberately a wide doubler/stereoizer: a mono
    // source should come out with a low L/R correlation at that setting.
    const auto smallStereo = renderRoom (room, 0.0f, 1.0f, noiseLeft, noiseLeft);
    const auto largeStereo = renderRoom (room, 1.0f, 1.0f, noiseLeft, noiseLeft);
    const double corrSmall = stereoCorrelation (smallStereo, sampleRate, 0.10, 0.40);
    const double corrLarge = stereoCorrelation (largeStereo, sampleRate, 0.10, 0.40);
    check (corrSmall < 0.90, "stereo-width-small");
    check (corrSmall < corrLarge, "stereo-width-macro");

    bool finite = true;
    for (const float value : large.left)
        finite = finite && std::isfinite (value);
    check (finite, "finite");

    std::cout << "room smallOnset=" << smallOnset
              << " largeOnset=" << largeOnset
              << " lateSmall=" << smallLate
              << " lateLarge=" << largeLate
              << " earlyHf=" << earlyHf
              << " lateHf=" << lateHf
              << " rmsSmall=" << rmsSmall
              << " rmsMid=" << rmsMid
              << " rmsLarge=" << rmsLarge
              << " mixDry=" << mixDry
              << " mixHalf=" << mixHalf
              << " mixWet=" << mixWet
              << " corrSmall=" << corrSmall
              << " corrLarge=" << corrLarge
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runModulationTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    namberry::PostEffects fx;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = blockSize;
    spec.numChannels = 2;
    fx.prepare (spec);
    fx.setBypass (false);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double sampleIndex = 0.0;

    auto render = [&] (int blocks)
    {
        float peak = 0.0f;
        bool finite = true;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i, ++sampleIndex)
            {
                const float input = 0.25f * static_cast<float> (
                    std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                              * sampleIndex / sampleRate));

                buffer.setSample (0, i, input);
                buffer.setSample (1, i, input);
            }

            fx.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                {
                    const float x = buffer.getSample (ch, i);
                    finite = finite && std::isfinite (x);
                    peak = juce::jmax (peak, std::abs (x));
                }
        }

        return std::pair<bool, float> { finite, peak };
    };

    auto bypassAll = [&]
    {
        fx.setChorusBypass (true);
        fx.setFlangerBypass (true);
        fx.setPhaserBypass (true);
        fx.setRoomBlend (0.0f);
        fx.setDelayBypass (true);
        fx.setReverbBypass (true);
    };

    bypassAll();

    fx.setChorusBypass (false);
    fx.setChorusRate (0.8f);
    fx.setChorusDepth (50.0f);
    fx.setChorusFeedback (15.0f);
    fx.setChorusDelay (15.0f);
    fx.setChorusMix (50.0f);
    const auto chorus = render (8);

    bypassAll();
    fx.setFlangerBypass (false);
    fx.setFlangerRate (0.4f);
    fx.setFlangerDepth (60.0f);
    fx.setFlangerManual (2.0f);
    fx.setFlangerFeedback (95.0f);
    fx.setFlangerMix (50.0f);
    const auto flanger = render (8);

    bypassAll();
    fx.setPhaserBypass (false);
    fx.setPhaserRate (0.5f);
    fx.setPhaserDepth (70.0f);
    fx.setPhaserFeedback (30.0f);
    fx.setPhaserMix (50.0f);

    std::pair<bool, float> phaser { true, 0.0f };

    for (const int stages : { 4, 8, 12 })
    {
        fx.setPhaserStages (stages);
        const auto result = render (4);
        phaser.first = phaser.first && result.first;
        phaser.second = juce::jmax (phaser.second, result.second);
    }

    bypassAll();
    buffer.clear();
    buffer.setSample (0, 0, 0.5f);
    buffer.setSample (1, 0, 0.5f);
    fx.processBlock (buffer, midi);
    const bool bypassOk = std::abs (buffer.getSample (0, 0) - 0.5f) < 1.0e-6f
                       && std::abs (buffer.getSample (1, 0) - 0.5f) < 1.0e-6f;

    const bool passed = chorus.first && chorus.second > 1.0e-4f
                     && flanger.first && flanger.second > 1.0e-4f
                     && phaser.first && phaser.second > 1.0e-4f
                     && bypassOk;

    std::cout << "modulation chorus=" << chorus.second
              << " flanger=" << flanger.second
              << " phaser=" << phaser.second
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runFxStateMigrationTest()
{
    namberry::NAMBerryProcessor processor;
    auto state = processor.getValueTreeState().copyState();
    state.removeProperty ("fxVersion", nullptr);

    auto setValue = [&] (const juce::String& id, float value)
    {
        auto child = state.getChildWithProperty ("id", id);
        if (child.isValid())
            child.setProperty ("value", value, nullptr);
    };

    auto removeChild = [&] (const juce::String& id)
    {
        for (int i = 0; i < state.getNumChildren(); ++i)
        {
            if (state.getChild (i).getProperty ("id").toString() == id)
            {
                state.removeChild (i, nullptr);
                return;
            }
        }
    };

    setValue ("chorusMix", 0.2f);
    setValue ("chorusDepth", 0.35f);
    setValue ("flangerFeedback", 0.4f);
    setValue ("phaserDepth", 0.5f);
    setValue ("chorusRate", 1.5f);
    setValue ("delayFeedback", 0.4f);
    setValue ("roomBlend", 50.0f);

    juce::ValueTree oldRoomBypass ("PARAM");
    oldRoomBypass.setProperty ("id", "roomBypass", nullptr);
    oldRoomBypass.setProperty ("value", 1.0f, nullptr);
    state.appendChild (oldRoomBypass, nullptr);

    removeChild ("chorusFeedback");
    removeChild ("chorusDelay");
    removeChild ("flangerManual");
    removeChild ("phaserFeedback");
    removeChild ("phaserStages");

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (*xml, block);
    processor.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

    auto& params = processor.getValueTreeState();
    const float chorusMix = params.getRawParameterValue ("chorusMix")->load();
    const float chorusDepth = params.getRawParameterValue ("chorusDepth")->load();
    const float flangerFeedback = params.getRawParameterValue ("flangerFeedback")->load();
    const float phaserDepth = params.getRawParameterValue ("phaserDepth")->load();
    const float chorusFeedback = params.getRawParameterValue ("chorusFeedback")->load();
    const float chorusDelay = params.getRawParameterValue ("chorusDelay")->load();
    const float flangerManual = params.getRawParameterValue ("flangerManual")->load();
    const float phaserFeedback = params.getRawParameterValue ("phaserFeedback")->load();
    const float phaserStages = params.getRawParameterValue ("phaserStages")->load();
    const float delayFeedback = params.getRawParameterValue ("delayFeedback")->load();
    const float roomBlend = params.getRawParameterValue ("roomBlend")->load();

    const bool passed = std::abs (chorusMix - 20.0f) < 0.01f
                     && std::abs (chorusDepth - 35.0f) < 0.01f
                     && std::abs (flangerFeedback - 38.0f) < 0.01f
                     && std::abs (phaserDepth - 50.0f) < 0.01f
                     && std::abs (chorusFeedback - 15.0f) < 0.01f
                     && std::abs (chorusDelay - 15.0f) < 0.01f
                     && std::abs (flangerManual - 2.0f) < 0.01f
                     && std::abs (phaserFeedback - 30.0f) < 0.01f
                     && std::abs (phaserStages - 8.0f) < 0.01f
                     && std::abs (delayFeedback - 4.0f) < 0.01f
                     && std::abs (roomBlend - 0.0f) < 0.01f;

    std::cout << "migration chorusMix=" << chorusMix
              << " chorusDepth=" << chorusDepth
              << " flangerFeedback=" << flangerFeedback
              << " phaserDepth=" << phaserDepth
              << " new=" << chorusFeedback << "/" << chorusDelay << "/"
              << flangerManual << "/" << phaserFeedback << "/" << phaserStages
              << " delayFeedback=" << delayFeedback
              << " roomBlend=" << roomBlend
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runIrLevelTest()
{
    const juce::File irFile ("/home/elipoli/Downloads/amps/IRs/cab Marshall 1960 celestion G12T75 sm57.wav");

    if (! irFile.existsAsFile())
    {
        std::cout << "IR level test SKIP: test IR not found\n";
        return 0;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int blocks = 10;

    auto render = [&] (int loadedSlots)
    {
        namberry::IrLoader loader;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        loader.prepare (spec);

        for (int slot = 0; slot < loadedSlots; ++slot)
        {
            loader.loadIr (slot, irFile);
            loader.setSlotMix (slot, 1.0f);
            loader.setSlotPan (slot, 0.0f);
            loader.setSlotPhaseInvert (slot, false);
        }

        // Convolution IR loading is asynchronous by default; prepare again so
        // every slot is guaranteed to be active before measuring the level.
        loader.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float rms = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = 0.2f * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            loader.processBlock (buffer, midi);

            if (block == blocks - 1)
            {
                float sum = 0.0f;

                for (int i = 0; i < blockSize; ++i)
                    sum += buffer.getSample (0, i) * buffer.getSample (0, i);

                rms = std::sqrt (sum / static_cast<float> (blockSize));
            }
        }

        return rms;
    };

    const float one = render (1);
    const float two = render (2);
    const float three = render (3);
    const float four = render (4);

    const float reference = juce::jmax (1.0e-6f, one);
    const bool passed = std::abs (one - two) / reference < 0.05f
                     && std::abs (one - three) / reference < 0.05f
                     && std::abs (one - four) / reference < 0.05f;

    std::cout << "IR level 1=" << one << " 2=" << two
              << " 3=" << three << " 4=" << four
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runImpersonatorTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr int blocks = 10;
    constexpr float inputAmplitude = 0.2f;
    constexpr float inputRms = inputAmplitude / 1.41421356f;

    auto render = [&] (double frequency, namberry::Impersonator::Mode mode, bool bypassed,
                       float levelDb = 0.0f) -> float
    {
        namberry::Impersonator impersonator;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        impersonator.setMode (mode);
        impersonator.setOutputLevelDb (levelDb);
        impersonator.prepare (spec);
        impersonator.setBypass (bypassed);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        double phase = 0.0;
        float rms = 0.0f;

        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float s = inputAmplitude * static_cast<float> (std::sin (phase));
                phase += 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
                buffer.setSample (0, i, s);
                buffer.setSample (1, i, s);
            }

            impersonator.processBlock (buffer, midi);

            if (block == blocks - 1)
            {
                float sum = 0.0f;

                for (int i = 0; i < blockSize; ++i)
                    sum += buffer.getSample (0, i) * buffer.getSample (0, i);

                rms = std::sqrt (sum / static_cast<float> (blockSize));
            }
        }

        return rms;
    };

    const auto toNeck150 = render (150.0, namberry::Impersonator::Mode::bridgeToNeck, false);
    const auto toNeck3000 = render (3000.0, namberry::Impersonator::Mode::bridgeToNeck, false);
    const auto toNeck6000 = render (6000.0, namberry::Impersonator::Mode::bridgeToNeck, false);

    const auto toBridge400 = render (400.0, namberry::Impersonator::Mode::neckToBridge, false);
    const auto toBridge3000 = render (3000.0, namberry::Impersonator::Mode::neckToBridge, false);
    const auto toBridge6000 = render (6000.0, namberry::Impersonator::Mode::neckToBridge, false);

    const auto bypassed = render (1000.0, namberry::Impersonator::Mode::bridgeToNeck, true);
    const auto levelUnity = render (1000.0, namberry::Impersonator::Mode::bridgeToNeck, false, 0.0f);
    const auto levelCut = render (1000.0, namberry::Impersonator::Mode::bridgeToNeck, false, -6.0f);

    const bool toNeckOk = toNeck150 > inputRms * 1.02f
                       && toNeck3000 < inputRms * 0.95f
                       && toNeck6000 < inputRms * 0.85f;

    const bool toBridgeOk = toBridge400 < inputRms * 0.95f
                         && toBridge3000 > inputRms * 1.02f
                         && toBridge6000 > inputRms * 1.01f;

    const bool bypassOk = std::abs (bypassed - inputRms) < inputRms * 0.02f;
    const bool levelOk = std::abs (levelCut / juce::jmax (1.0e-6f, levelUnity) - 0.501f) < 0.03f;

    const bool passed = toNeckOk && toBridgeOk && bypassOk && levelOk;

    std::cout << "impersonator toNeck=" << toNeck150 << "/" << toNeck3000 << "/" << toNeck6000
              << " toBridge=" << toBridge400 << "/" << toBridge3000 << "/" << toBridge6000
              << " bypass=" << bypassed << " level0=" << levelUnity
              << " level-6=" << levelCut
              << " input=" << inputRms
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runToneMatchTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr float shift = -12.0f;

    // Bright source: a strong harmonic stack with lots of upper partials, so a
    // detune that shifts formants down is clearly audible as lost brightness.
    auto render = [&] (float tone) -> std::vector<float>
    {
        namberry::DigitalCapo capo;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = blockSize;
        spec.numChannels = 2;
        capo.prepare (spec);
        capo.setSemitones (shift);
        capo.setTone (tone);
        capo.setMix (1.0f);
        capo.setBypass (false);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> captured;
        captured.reserve (65536);

        const int skipSamples = capo.getLatencySamples() + 24000;
        long long produced = 0;

        for (int block = 0; block < 400; ++block)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double t = (double) produced / sampleRate;
                double s = 0.0;

                for (int h = 1; h <= 12; ++h)
                    s += std::sin (2.0 * juce::MathConstants<double>::pi
                                   * 110.0 * h * t) / std::sqrt ((double) h);

                buffer.setSample (0, i, 0.18f * (float) s);
                buffer.setSample (1, i, 0.18f * (float) s);
                ++produced;
            }

            capo.processBlock (buffer, midi);

            if (produced >= skipSamples && captured.size() < 48000)
            {
                for (int i = 0; i < blockSize; ++i)
                    captured.push_back (buffer.getSample (0, i));
            }
        }

        return captured;
    };

    // Spectral centroid via FFT energy weighting: brighter tone => higher Hz.
    auto centroid = [&] (const std::vector<float>& x) -> double
    {
        constexpr int fftOrder = 13;
        constexpr int fftSize = 1 << fftOrder;
        std::vector<float> data ((size_t) fftSize * 2, 0.0f);

        for (int i = 0; i < fftSize && i < (int) x.size(); ++i)
        {
            const float window = 0.5f - 0.5f * std::cos (
                2.0f * juce::MathConstants<float>::pi * (float) i / (float) (fftSize - 1));
            data[(size_t) i] = x[(size_t) i] * window;
        }

        juce::dsp::FFT fft (fftOrder);
        fft.performFrequencyOnlyForwardTransform (data.data());

        double num = 0.0, den = 0.0;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double mag = data[(size_t) k];
            num += mag * (k * sampleRate / fftSize);
            den += mag;
        }

        return den > 0.0 ? num / den : 0.0;
    };

    const auto natural = render (0.0f);
    const auto matched = render (1.0f);

    const double cNatural = centroid (natural);
    const double cMatched = centroid (matched);

    // The tone-match path must keep the source brighter than the natural
    // detune. A clear margin keeps this stable across engines.
    const bool passed = cMatched > cNatural * 1.15;

    std::cout << "tone-match shift=" << shift
              << " natural=" << (int) cNatural << " Hz"
              << " matched=" << (int) cMatched << " Hz"
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runCompressorTest()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 4096;

    namberry::Compressor compressor;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = blockSize;
    spec.numChannels = 2;
    compressor.prepare (spec);
    compressor.setBypass (false);
    compressor.setAttackMs (5.0f);
    compressor.setReleaseMs (120.0f);
    compressor.setThresholdDb (-24.0f);
    compressor.setRatio (8.0f);
    compressor.setMakeupGainDb (0.0f);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    auto fillSine = [&] (double startSample)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float value = 0.8f * static_cast<float> (
                std::sin (2.0 * juce::MathConstants<double>::pi * 220.0
                          * (startSample + i) / sampleRate));
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, value);
        }
    };

    double position = 0.0;

    for (int i = 0; i < 4; ++i, position += blockSize)
    {
        fillSine (position);
        compressor.processBlock (buffer, midi);
    }

    fillSine (position);
    juce::AudioBuffer<float> inputCopy (buffer);
    compressor.processBlock (buffer, midi);

    float inputEnergy = 0.0f;
    float outputEnergy = 0.0f;
    bool finite = true;

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float in = inputCopy.getSample (ch, i);
            const float out = buffer.getSample (ch, i);
            inputEnergy += in * in;
            outputEnergy += out * out;
            finite = finite && std::isfinite (out);
        }
    }

    const float inputRms = std::sqrt (inputEnergy / (2.0f * blockSize));
    const float outputRms = std::sqrt (outputEnergy / (2.0f * blockSize));
    const bool compressed = outputRms < inputRms * 0.8f && outputRms > 1.0e-4f;

    compressor.setBypass (true);
    fillSine (position + blockSize);
    juce::AudioBuffer<float> bypassInput (buffer);
    compressor.processBlock (buffer, midi);
    const bool bypassOk =
        std::abs (buffer.getSample (0, 0) - bypassInput.getSample (0, 0)) < 1.0e-7f
        && std::abs (buffer.getSample (0, blockSize - 1)
                     - bypassInput.getSample (0, blockSize - 1)) < 1.0e-7f;

    const bool passed = finite && compressed && bypassOk;

    std::cout << "compressor inputRms=" << inputRms
              << " outputRms=" << outputRms
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runPresetTest()
{
    const juce::File presetFile ("/tmp/opencode/namberry-preset-test.namberrypreset");
    presetFile.deleteFile();

    namberry::NAMBerryProcessor source;
    source.setPlayConfigDetails (2, 2, 48000, 512);
    source.prepareToPlay (48000, 512);

    auto& sourceParams = source.getValueTreeState();

    auto setParameter = [&] (const juce::String& id, float normalised)
    {
        if (auto* parameter = sourceParams.getParameter (id))
            parameter->setValueNotifyingHost (normalised);
    };

    setParameter ("chorusMix", 1.0f);
    setParameter ("phaserStages", 1.0f);
    setParameter ("reverbDecay", 0.8f);

    const juce::File ampFile ("/home/elipoli/Downloads/amps/NAMs/1. BASS/DG ADAM/ADAM Clean.nam");
    const juce::File irFile ("/home/elipoli/Downloads/amps/IRs/cab Marshall 1960 celestion G12T75 sm57.wav");
    const bool haveAmp = ampFile.existsAsFile();
    const bool haveIr = irFile.existsAsFile();

    if (haveAmp)
        source.loadNamModel (ampFile);

    if (haveIr)
        source.loadIr (0, irFile);

    const bool saved = source.savePreset (presetFile);

    namberry::NAMBerryProcessor restored;
    restored.setPlayConfigDetails (2, 2, 48000, 512);
    restored.prepareToPlay (48000, 512);

    const bool loaded = saved && restored.loadPreset (presetFile);
    auto& restoredParams = restored.getValueTreeState();

    const float chorusMix = restoredParams.getRawParameterValue ("chorusMix")->load();
    const float phaserStages = restoredParams.getRawParameterValue ("phaserStages")->load();
    const float reverbDecay = restoredParams.getRawParameterValue ("reverbDecay")->load();

    const bool filesOk = (! haveAmp || restored.getNamModel()->hasModel())
                      && (! haveIr || restored.getIrLoader()->isSlotLoaded (0));

    const bool passed = loaded && filesOk
                     && std::abs (chorusMix - 50.0f) < 0.01f
                     && std::abs (phaserStages - 12.0f) < 0.01f
                     && std::abs (reverbDecay - 0.8f) < 0.01f;

    presetFile.deleteFile();

    std::cout << "preset saved=" << saved
              << " loaded=" << loaded
              << " files=" << filesOk
              << " chorusMix=" << chorusMix
              << " phaserStages=" << phaserStages
              << " reverbDecay=" << reverbDecay
              << " " << (passed ? "PASS" : "FAIL") << "\n";

    return passed ? 0 : 2;
}

int runPersistenceTest()
{
    const juce::File namRoot ("/home/elipoli/Downloads/amps/NAMs");
    const juce::File model ("/home/elipoli/Downloads/amps/NAMs/1. BASS/DG ADAM/ADAM Clean.nam");
    const juce::File pedal ("/home/elipoli/Downloads/amps/NAMs/PEDALS/Dirty-Tree.nam");
    const juce::File ir ("/home/elipoli/Downloads/amps/IRs/cab Marshall 1960 celestion G12T75 sm57.wav");
    const juce::File ir2 ("/home/elipoli/Downloads/amps/IRs/Matchless 2x12 G12H30 sm57.wav");

    if (! namRoot.isDirectory() || ! model.existsAsFile() || ! pedal.existsAsFile()
        || ! ir.existsAsFile() || ! ir2.existsAsFile())
    {
        std::cout << "persistence test SKIP: test files not found\n";
        return 0;
    }

    {
        namberry::NAMBerryProcessor processor;
        processor.setPlayConfigDetails (2, 2, 48000, 512);
        processor.prepareToPlay (48000, 512);
        processor.setNamDirectory (namRoot);
        processor.setPedalDirectory (pedal.getParentDirectory());
        processor.setIrDirectory (ir.getParentDirectory());
        processor.loadNamModel (model);
        processor.loadNamPedalModel (pedal);
        processor.loadIr (0, ir);
        processor.loadIr (1, ir2);
    }

    namberry::NAMBerryProcessor restored;
    restored.setPlayConfigDetails (2, 2, 48000, 512);
    restored.prepareToPlay (48000, 512);

    const bool ampLoaded = restored.getNamModel()->hasModel();
    const bool pedalLoaded = restored.getNamPedal()->hasModel();
    const bool irLoaded = restored.getIrLoader()->isSlotLoaded (0);
    const bool ir2Loaded = restored.getIrLoader()->isSlotLoaded (1);
    const bool namRootOk = restored.getNamDirectory() == namRoot;
    const bool pedalRootOk = restored.getPedalDirectory() == pedal.getParentDirectory();
    const bool irRootOk = restored.getIrDirectory() == ir.getParentDirectory();

    std::cout << "restored amp=" << ampLoaded
              << " (" << restored.getNamModel()->getLoadedFileName() << ")"
              << " pedal=" << pedalLoaded
              << " (" << restored.getNamPedal()->getLoadedFileName() << ")"
              << " ir0=" << irLoaded
              << " (" << restored.getIrLoader()->getLoadedFileName (0) << ")"
              << " ir1=" << ir2Loaded
              << " (" << restored.getIrLoader()->getLoadedFileName (1) << ")"
              << " ampRoot=" << namRootOk
              << " pedalRoot=" << pedalRootOk
              << " irRoot=" << irRootOk << "\n";

    const bool passed = ampLoaded && pedalLoaded && irLoaded && ir2Loaded
                     && namRootOk && pedalRootOk && irRootOk;
    std::cout << (passed ? "PASS" : "FAIL") << "\n";
    return passed ? 0 : 2;
}

} // namespace

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--reference")
        return runReferenceTest();

    if (argc > 1 && juce::String (argv[1]) == "--reverb-dry")
        return runReverbDryTest();

    if (argc > 1 && juce::String (argv[1]) == "--delay-feedback")
        return runDelayFeedbackTest();

    if (argc > 1 && juce::String (argv[1]) == "--delay-reset")
        return runDelayResetTest();

    if (argc > 1 && juce::String (argv[1]) == "--room")
        return runRoomTest();

    if (argc > 1 && juce::String (argv[1]) == "--modulation")
        return runModulationTest();

    if (argc > 1 && juce::String (argv[1]) == "--fx-state")
        return runFxStateMigrationTest();

    if (argc > 1 && juce::String (argv[1]) == "--preset")
        return runPresetTest();

    if (argc > 1 && juce::String (argv[1]) == "--compressor")
        return runCompressorTest();

    if (argc > 1 && juce::String (argv[1]) == "--impersonator")
        return runImpersonatorTest();

    if (argc > 1 && juce::String (argv[1]) == "--tone-match")
        return runToneMatchTest();

    if (argc > 1 && juce::String (argv[1]) == "--ir-level")
        return runIrLevelTest();

    if (argc > 1 && juce::String (argv[1]) == "--persist")
        return runPersistenceTest();

    if (argc > 1 && juce::String (argv[1]) == "--dry-center")
        return runDryCenterTest();

    if (argc > 1 && juce::String (argv[1]) == "--detector")
        return runDetectorTest();

    if (argc > 1 && juce::String (argv[1]) == "--detected-stage")
        return runDetectedStageTest();

    if (argc > 1 && juce::String (argv[1]) == "--pitch-blocks")
        return runPitchBlockSizeTest();

    if (argc > 1 && juce::String (argv[1]) == "--pitch")
        return runPitchTest();

    if (argc > 1 && juce::String (argv[1]) == "--pitch-low")
        return runPitchLowNoteTest();

    if (argc > 1 && juce::String (argv[1]) == "--pitch-robust")
        return runPitchRobustnessTest();

    if (argc > 1 && juce::String (argv[1]) == "--dirs")
    {
        namberry::NAMBerryProcessor processor;
        const juce::File namDir ("/tmp/opencode/namberry-amp-dir");
        const juce::File pedalDir ("/tmp/opencode/namberry-pedal-dir");
        const juce::File irDir ("/tmp/opencode/namberry-ir-dir");
        namDir.createDirectory();
        pedalDir.createDirectory();
        irDir.createDirectory();

        processor.setNamDirectory (namDir);
        processor.setPedalDirectory (pedalDir);
        processor.setIrDirectory (irDir);

        std::cout << "amp=" << processor.getNamDirectory().getFullPathName() << "\n";
        std::cout << "pedal=" << processor.getPedalDirectory().getFullPathName() << "\n";
        std::cout << "ir=" << processor.getIrDirectory().getFullPathName() << "\n";
        return 0;
    }

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr float inputAmplitude = 0.2f;
    constexpr double frequency = 220.0;
    constexpr int totalBlocks = 280; // ~3 s
    constexpr int skipBlocks = 20;   // settle / warm-up

    namberry::NAMBerryProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);

    if (argc > 1)
    {
        const juce::File model { juce::String (argv[1]) };

        if (! processor.loadNamModel (model))
        {
            std::cerr << "FAIL: could not load model: " << argv[1] << "\n";
            return 1;
        }

        std::cout << "Loaded model: " << model.getFileName() << "\n";
    }
    else
    {
        std::cout << "No model supplied; testing the dry/effect chain.\n";
    }

    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    double sumSquares = 0.0;
    float peak = 0.0f;
    float maxChannelDiff = 0.0f;
    bool finite = true;

    for (int block = 0; block < totalBlocks; ++block)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto sample = inputAmplitude
                * static_cast<float> (std::sin (phase));

            phase += 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

            buffer.setSample (0, i, sample);
            buffer.setSample (1, i, sample);
        }

        processor.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float value = buffer.getSample (ch, i);

                if (! std::isfinite (value))
                    finite = false;

                peak = juce::jmax (peak, std::abs (value));

                if (block >= skipBlocks)
                    sumSquares += static_cast<double> (value) * value;
            }
        }

        for (int i = 0; i < blockSize; ++i)
            maxChannelDiff = juce::jmax (
                maxChannelDiff,
                std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
    }

    const double countedSamples = static_cast<double> (totalBlocks - skipBlocks)
                                * blockSize * 2.0;
    const double rms = std::sqrt (sumSquares / juce::jmax (1.0, countedSamples));

    std::cout << "peak=" << peak
              << " rms=" << rms
              << " maxChannelDiff=" << maxChannelDiff
              << " finite=" << (finite ? "yes" : "no") << "\n";

    const bool ok = finite
                 && rms > 1.0e-4
                 && peak < 10.0f
                 && maxChannelDiff < 1.0e-3f;

    std::cout << (ok ? "PASS" : "FAIL") << "\n";
    return ok ? 0 : 2;
}
