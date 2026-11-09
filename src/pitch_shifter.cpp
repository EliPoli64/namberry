#include "namberry/PitchShifter.h"

#include <juce_core/juce_core.h>

#include "signalsmith-stretch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string_view>
#include <vector>

namespace namberry {

namespace {

// Minimal declarations for the optional Rubber Band C API. The library is
// loaded at runtime so the plugin remains buildable without Rubber Band
// headers or binaries. When it is unavailable, the self-contained Stretch
// engine below is used instead.
using RubberBandState = void*;

constexpr unsigned kRubberBandProcessRealTime       = 0x00000001u;
constexpr unsigned kRubberBandPitchHighConsistency = 0x04000000u;
constexpr unsigned kRubberBandChannelsTogether     = 0x10000000u;
constexpr unsigned kRubberBandWindowMedium          = 0x00100000u;
constexpr unsigned kRubberBandFormantShifted       = 0x00000000u;
constexpr unsigned kRubberBandFormantPreserved     = 0x01000000u;

class RubberBandApi
{
public:
    bool load()
    {
        if (loaded_)
            return available_;

        loaded_ = true;

        const char* names[] = {
            "librubberband.so.3",
            "librubberband.so",
            "rubberband",
#if JUCE_WINDOWS
            "rubberband.dll",
            "librubberband.dll",
#elif JUCE_MAC
            "librubberband.3.dylib",
            "librubberband.dylib",
#endif
        };

        for (const char* name : names)
        {
            if (! library.open (name))
                continue;

            liveNew = reinterpret_cast<LiveNew> (library.getFunction ("rubberband_live_new"));
            liveDelete = reinterpret_cast<LiveDelete> (library.getFunction ("rubberband_live_delete"));
            liveReset = reinterpret_cast<LiveReset> (library.getFunction ("rubberband_live_reset"));
            liveSetPitch = reinterpret_cast<LiveSetPitch> (library.getFunction ("rubberband_live_set_pitch_scale"));
            liveGetBlockSize = reinterpret_cast<LiveGetBlockSize> (library.getFunction ("rubberband_live_get_block_size"));
            liveGetStartDelay = reinterpret_cast<LiveGetStartDelay> (library.getFunction ("rubberband_live_get_start_delay"));
            liveShift = reinterpret_cast<LiveShift> (library.getFunction ("rubberband_live_shift"));
            liveSetFormantOption = reinterpret_cast<LiveSetFormantOption> (library.getFunction ("rubberband_live_set_formant_option"));
            liveSetFormantScale = reinterpret_cast<LiveSetFormantScale> (library.getFunction ("rubberband_live_set_formant_scale"));

            if (liveNew != nullptr && liveDelete != nullptr && liveReset != nullptr
                && liveSetPitch != nullptr && liveGetBlockSize != nullptr
                && liveGetStartDelay != nullptr && liveShift != nullptr)
            {
                available_ = true;
                return true;
            }

            library.close();
        }

        return false;
    }

    bool isAvailable() const noexcept { return available_; }

    juce::DynamicLibrary library;

    using LiveNew = RubberBandState (*) (unsigned int, unsigned int, unsigned int);
    using LiveDelete = void (*) (RubberBandState);
    using LiveReset = void (*) (RubberBandState);
    using LiveSetPitch = void (*) (RubberBandState, double);
    using LiveGetBlockSize = unsigned int (*) (RubberBandState);
    using LiveGetStartDelay = unsigned int (*) (RubberBandState);
    using LiveShift = void (*) (RubberBandState, const float* const*, float* const*);
    using LiveSetFormantOption = void (*) (RubberBandState, unsigned int);
    using LiveSetFormantScale = void (*) (RubberBandState, double);

    LiveNew liveNew = nullptr;
    LiveDelete liveDelete = nullptr;
    LiveReset liveReset = nullptr;
    LiveSetPitch liveSetPitch = nullptr;
    LiveGetBlockSize liveGetBlockSize = nullptr;
    LiveGetStartDelay liveGetStartDelay = nullptr;
    LiveShift liveShift = nullptr;
    LiveSetFormantOption liveSetFormantOption = nullptr;
    LiveSetFormantScale liveSetFormantScale = nullptr;

private:
    bool loaded_ = false;
    bool available_ = false;
};

RubberBandApi& rubberBandApi()
{
    static RubberBandApi api;
    api.load();
    return api;
}

class AudioRing
{
public:
    void prepare (int channels, size_t capacity)
    {
        channels_ = std::max (1, channels);
        capacity_ = std::max<size_t> (1, capacity);
        data_.assign (static_cast<size_t> (channels_),
                      std::vector<float> (capacity_, 0.0f));
        read_ = 0;
        write_ = 0;
        count_ = 0;
    }

    void clear()
    {
        for (auto& channel : data_)
            std::fill (channel.begin(), channel.end(), 0.0f);

        read_ = 0;
        write_ = 0;
        count_ = 0;
    }

    void pushSilence (size_t numSamples)
    {
        const size_t n = std::min<size_t> (numSamples, capacity_ - count_);

        for (int ch = 0; ch < channels_; ++ch)
        {
            auto& dst = data_[static_cast<size_t> (ch)];

            for (size_t i = 0; i < n; ++i)
                dst[(write_ + i) % capacity_] = 0.0f;
        }

        write_ = (write_ + n) % capacity_;
        count_ += n;
    }

    size_t size() const noexcept { return count_; }

    void push (const float* const* input, int numSamples)
    {
        if (numSamples <= 0)
            return;

        const int n = std::min<int> (numSamples, static_cast<int> (capacity_ - count_));

        for (int ch = 0; ch < channels_; ++ch)
        {
            auto& dst = data_[static_cast<size_t> (ch)];
            const float* src = input[static_cast<size_t> (ch)];

            for (int i = 0; i < n; ++i)
                dst[(write_ + static_cast<size_t> (i)) % capacity_] = src[i];
        }

        write_ = (write_ + static_cast<size_t> (n)) % capacity_;
        count_ += static_cast<size_t> (n);
    }

    int pop (float* const* output, int numSamples)
    {
        if (numSamples <= 0)
            return 0;

        const int n = std::min<int> (numSamples, static_cast<int> (count_));

        for (int ch = 0; ch < channels_; ++ch)
        {
            auto& src = data_[static_cast<size_t> (ch)];
            float* dst = output[static_cast<size_t> (ch)];

            for (int i = 0; i < n; ++i)
                dst[i] = src[(read_ + static_cast<size_t> (i)) % capacity_];
        }

        read_ = (read_ + static_cast<size_t> (n)) % capacity_;
        count_ -= static_cast<size_t> (n);
        return n;
    }

    void advance (size_t numSamples)
    {
        const size_t n = std::min<size_t> (numSamples, count_);
        read_ = (read_ + n) % capacity_;
        count_ -= n;
    }

private:
    int channels_ = 2;
    size_t capacity_ = 1;
    std::vector<std::vector<float>> data_;
    size_t read_ = 0;
    size_t write_ = 0;
    size_t count_ = 0;
};

class PitchEngine
{
public:
    virtual ~PitchEngine() = default;

    virtual bool prepare (const juce::dsp::ProcessSpec& spec,
                          PitchShifter::QualityMode quality) = 0;
    virtual void reset() = 0;
    virtual void setTransposeSemitones (float) = 0;
    virtual void setTone (float tone) = 0;
    virtual void process (const float* const*, float* const*, int) = 0;

    virtual int getLatencySamples() const noexcept = 0;
    virtual bool isReady() const noexcept = 0;
    virtual const char* getName() const noexcept = 0;
};

class StretchEngine final : public PitchEngine
{
public:
    bool prepare (const juce::dsp::ProcessSpec& spec,
                  PitchShifter::QualityMode quality) override
    {
        numChannels_ = std::min (8, std::max (1, static_cast<int> (spec.numChannels)));
        maxBlockSize_ = std::max (1, static_cast<int> (spec.maximumBlockSize));
        sampleRate_ = spec.sampleRate;

        const auto sr = static_cast<float> (sampleRate_);
        const float blockSeconds = quality == PitchShifter::QualityMode::quality
            ? 0.100f
            : 0.050f;
        const float hopSeconds = quality == PitchShifter::QualityMode::quality
            ? 0.010f
            : 0.005f;

        const int blockSamples = std::max (256, static_cast<int> (sr * blockSeconds));
        const int intervalSamples = std::max (64, static_cast<int> (sr * hopSeconds));

        stretch_.configure (numChannels_, blockSamples, intervalSamples, false);
        latencySamples_ = stretch_.inputLatency() + stretch_.outputLatency();

        inputPointers_.fill (nullptr);
        outputPointers_.fill (nullptr);

        reset();
        return true;
    }

    void reset() override { stretch_.reset(); }

    void setTransposeSemitones (float semitones) override
    {
        transposeSemitones_ = semitones;
        stretch_.setTransposeSemitones (semitones);
        applyFormant();
    }

    void setTone (float tone) override
    {
        tone_ = juce::jlimit (0.0f, 1.0f, tone);
        applyFormant();
    }

    void process (const float* const* input, float* const* output, int numSamples) override
    {
        if (numSamples <= 0)
            return;

        for (int offset = 0; offset < numSamples; offset += maxBlockSize_)
        {
            const int chunk = std::min (maxBlockSize_, numSamples - offset);

            for (int ch = 0; ch < numChannels_; ++ch)
            {
                inputPointers_[static_cast<size_t> (ch)] =
                    const_cast<float*> (input[static_cast<size_t> (ch)]) + offset;
                outputPointers_[static_cast<size_t> (ch)] =
                    output[static_cast<size_t> (ch)] + offset;
            }

            stretch_.process (inputPointers_.data(), chunk,
                              outputPointers_.data(), chunk);
        }
    }

    int getLatencySamples() const noexcept override { return latencySamples_; }
    bool isReady() const noexcept override { return true; }
    const char* getName() const noexcept override { return "Stretch"; }

private:
    // Tone controls how strongly the original formant envelope is preserved.
    // tone=1 matches the source formants (retains brightness on down-tuning);
    // tone=0 lets the formants shift naturally with the transposition.
    void applyFormant()
    {
        const float formantScale = std::pow (2.0f, transposeSemitones_ * (1.0f - tone_) / 12.0f);
        stretch_.setFormantFactor (formantScale, false);
    }

    signalsmith::stretch::SignalsmithStretch<float> stretch_;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    int latencySamples_ = 0;
    float transposeSemitones_ = 0.0f;
    float tone_ = 1.0f;
    std::array<float*, 8> inputPointers_ {};
    std::array<float*, 8> outputPointers_ {};
};

class RubberBandEngine final : public PitchEngine
{
public:
    bool prepare (const juce::dsp::ProcessSpec& spec,
                  PitchShifter::QualityMode quality) override
    {
        auto& api = rubberBandApi();

        if (! api.isAvailable())
            return false;

        numChannels_ = std::min (2, std::max (1, static_cast<int> (spec.numChannels)));
        maxBlockSize_ = std::max (1, static_cast<int> (spec.maximumBlockSize));
        sampleRate_ = spec.sampleRate;

        const unsigned options =
            kRubberBandProcessRealTime
            | kRubberBandPitchHighConsistency
            | kRubberBandChannelsTogether
            | (quality == PitchShifter::QualityMode::quality
                   ? kRubberBandWindowMedium
                   : 0u);

        state_ = api.liveNew (static_cast<unsigned> (sampleRate_),
                             static_cast<unsigned> (numChannels_),
                             options);

        if (state_ == nullptr)
            return false;

        blockSize_ = std::max (1, static_cast<int> (api.liveGetBlockSize (state_)));
        startDelay_ = std::max (0, static_cast<int> (api.liveGetStartDelay (state_)));

        // The fixed-block scheduler emits output one engine block at a time.
        // Count that scheduling quantisation as part of the reported latency
        // and pre-fill the output FIFO accordingly, so the wet/dry alignment
        // stays fixed for any host block size.
        latencySamples_ = startDelay_ + blockSize_;

        inputChunk_.assign (static_cast<size_t> (numChannels_),
                            std::vector<float> (static_cast<size_t> (blockSize_), 0.0f));
        outputChunk_.assign (static_cast<size_t> (numChannels_),
                             std::vector<float> (static_cast<size_t> (blockSize_), 0.0f));

        for (int ch = 0; ch < numChannels_; ++ch)
        {
            inputChunkPointers_[static_cast<size_t> (ch)] =
                inputChunk_[static_cast<size_t> (ch)].data();
            outputChunkPointers_[static_cast<size_t> (ch)] =
                outputChunk_[static_cast<size_t> (ch)].data();
        }

        const size_t ringCapacity = static_cast<size_t> (
            std::max (maxBlockSize_, blockSize_) * 4
            + std::max (0, latencySamples_) + 16);

        inputRing_.prepare (numChannels_, ringCapacity);
        outputRing_.prepare (numChannels_, ringCapacity);

        reset();
        return true;
    }

    ~RubberBandEngine() override
    {
        auto& api = rubberBandApi();

        if (state_ != nullptr && api.liveDelete != nullptr)
            api.liveDelete (state_);

        state_ = nullptr;
    }

    void reset() override
    {
        auto& api = rubberBandApi();

        if (state_ != nullptr && api.liveReset != nullptr)
            api.liveReset (state_);

        inputRing_.clear();
        outputRing_.clear();

        // Make the fixed-block scheduler a deterministic part of the reported
        // latency instead of letting host block sizes decide when output is
        // first available.
        outputRing_.pushSilence (static_cast<size_t> (blockSize_));
    }

    void setTransposeSemitones (float semitones) override
    {
        transposeSemitones_ = semitones;

        auto& api = rubberBandApi();

        if (state_ != nullptr && api.liveSetPitch != nullptr)
            api.liveSetPitch (state_, std::pow (2.0, semitones / 12.0));

        applyFormant();
    }

    void setTone (float tone) override
    {
        auto& api = rubberBandApi();

        if (state_ == nullptr)
            return;

        tone_ = juce::jlimit (0.0f, 1.0f, tone);
        applyFormant();
    }

    void process (const float* const* input, float* const* output, int numSamples) override
    {
        if (state_ == nullptr || numSamples <= 0)
        {
            for (int ch = 0; ch < numChannels_; ++ch)
                std::fill (output[ch], output[ch] + numSamples, 0.0f);

            return;
        }

        auto& api = rubberBandApi();

        inputRing_.push (input, numSamples);

        while (inputRing_.size() >= static_cast<size_t> (blockSize_))
        {
            inputRing_.pop (inputChunkPointers_.data(), blockSize_);
            api.liveShift (state_,
                           const_cast<const float* const*> (inputChunkPointers_.data()),
                           outputChunkPointers_.data());
            outputRing_.push (outputChunkPointers_.data(), blockSize_);
        }

        const int written = outputRing_.pop (output, numSamples);

        for (int ch = 0; ch < numChannels_; ++ch)
            std::fill (output[ch] + written, output[ch] + numSamples, 0.0f);
    }

    int getLatencySamples() const noexcept override { return latencySamples_; }
    bool isReady() const noexcept override { return state_ != nullptr; }
    const char* getName() const noexcept override { return "RubberBand"; }

private:
    // tone=1 preserves the original formant envelope (retains brightness on
    // down-tuning); tone=0 lets the formants shift naturally with the pitch.
    // Rubber Band expresses this as a formant pitch-scale, where the "matched"
    // scale is the reciprocal of the transposition ratio.
    void applyFormant()
    {
        auto& api = rubberBandApi();

        if (state_ == nullptr)
            return;

        const bool matched = tone_ > 0.001f;

        if (api.liveSetFormantOption != nullptr)
            api.liveSetFormantOption (state_, matched
                ? kRubberBandFormantPreserved
                : kRubberBandFormantShifted);

        if (matched && api.liveSetFormantScale != nullptr)
        {
            // Continuous blend: tone=1 -> full reciprocal scale (formants kept
            // in place), tone=0 -> 1.0 (formants follow the pitch).
            const double ratio = std::pow (2.0, transposeSemitones_ / 12.0);
            const double scale = std::pow (ratio, -static_cast<double> (tone_));
            api.liveSetFormantScale (state_, scale);
        }
    }

    RubberBandState state_ = nullptr;
    double sampleRate_ = 48000.0;
    int numChannels_ = 2;
    int maxBlockSize_ = 512;
    int blockSize_ = 512;
    int startDelay_ = 0;
    int latencySamples_ = 0;
    float transposeSemitones_ = 0.0f;
    float tone_ = 1.0f;

    AudioRing inputRing_;
    AudioRing outputRing_;
    std::vector<std::vector<float>> inputChunk_;
    std::vector<std::vector<float>> outputChunk_;
    std::array<float*, 8> inputChunkPointers_ {};
    std::array<float*, 8> outputChunkPointers_ {};
};

std::unique_ptr<PitchEngine> createEngine (bool preferRubberBand)
{
    if (preferRubberBand)
        return std::make_unique<RubberBandEngine>();

    return std::make_unique<StretchEngine>();
}

} // namespace

struct PitchShifter::Impl
{
    std::unique_ptr<PitchEngine> engines[2];
    int active = 0;
    float semitones = 0.0f;
    float tone = 1.0f;
};

PitchShifter::PitchShifter() : impl_ (std::make_unique<Impl>()) {}
PitchShifter::~PitchShifter() = default;

void PitchShifter::prepare (const juce::dsp::ProcessSpec& spec, QualityMode quality)
{
    numChannels_ = std::max (1, static_cast<int> (spec.numChannels));
    maxBlockSize_ = std::max (1, static_cast<int> (spec.maximumBlockSize));
    quality_ = quality;

    const bool preferRubberBand = rubberBandApi().isAvailable();

    for (int i = 0; i < 2; ++i)
    {
        const auto mode = i == 0 ? QualityMode::lowLatency : QualityMode::quality;
        auto engine = createEngine (preferRubberBand);

        if (! engine->prepare (spec, mode))
        {
            engine = std::make_unique<StretchEngine>();
            engine->prepare (spec, mode);
        }

        impl_->engines[static_cast<size_t> (i)] = std::move (engine);
    }

    impl_->active = quality == QualityMode::quality ? 1 : 0;
    impl_->engines[static_cast<size_t> (impl_->active)]->setTransposeSemitones (impl_->semitones);
    impl_->engines[static_cast<size_t> (impl_->active)]->setTone (impl_->tone);
    prepared_ = true;
}

void PitchShifter::reset()
{
    if (impl_ == nullptr)
        return;

    for (auto& engine : impl_->engines)
        if (engine != nullptr)
            engine->reset();
}

void PitchShifter::setQualityMode (QualityMode quality)
{
    quality_ = quality;

    if (impl_ == nullptr || ! prepared_)
        return;

    const int next = quality == QualityMode::quality ? 1 : 0;

    if (impl_->active != next)
    {
        impl_->active = next;
        impl_->engines[static_cast<size_t> (next)]->reset();
        impl_->engines[static_cast<size_t> (next)]->setTransposeSemitones (impl_->semitones);
        impl_->engines[static_cast<size_t> (next)]->setTone (impl_->tone);
    }
}

void PitchShifter::setTransposeSemitones (float semitones)
{
    if (impl_ == nullptr)
        return;

    impl_->semitones = semitones;

    if (prepared_)
        impl_->engines[static_cast<size_t> (impl_->active)]->setTransposeSemitones (semitones);
}

void PitchShifter::setTone (float tone)
{
    tone_ = juce::jlimit (0.0f, 1.0f, tone);

    if (impl_ == nullptr)
        return;

    impl_->tone = tone_;

    if (prepared_)
        impl_->engines[static_cast<size_t> (impl_->active)]->setTone (tone_);
}

int PitchShifter::getLatencySamples() const noexcept
{
    return impl_ != nullptr && prepared_
        ? impl_->engines[static_cast<size_t> (impl_->active)]->getLatencySamples()
        : 0;
}

int PitchShifter::getMaxLatencySamples() const noexcept
{
    if (impl_ == nullptr || ! prepared_)
        return 0;

    int maximum = 0;

    for (const auto& engine : impl_->engines)
        if (engine != nullptr)
            maximum = std::max (maximum, engine->getLatencySamples());

    return maximum;
}

bool PitchShifter::isRubberBandAvailable() const noexcept
{
    return impl_ != nullptr && prepared_
        && std::string_view (impl_->engines[static_cast<size_t> (impl_->active)]->getName())
               == "RubberBand";
}

const char* PitchShifter::getEngineName() const noexcept
{
    return impl_ != nullptr && prepared_
        ? impl_->engines[static_cast<size_t> (impl_->active)]->getName()
        : "None";
}

void PitchShifter::process (const juce::AudioBuffer<float>& input,
                            juce::AudioBuffer<float>& output,
                            int numSamples)
{
    if (! prepared_ || numSamples <= 0)
        return;

    const int channels = std::min ({ 8,
                                     numChannels_,
                                     input.getNumChannels(),
                                     output.getNumChannels() });

    if (channels <= 0)
        return;

    const int samples = std::min ({ numSamples,
                                    input.getNumSamples(),
                                    output.getNumSamples() });

    std::array<const float*, 8> inputPointers {};
    std::array<float*, 8> outputPointers {};

    for (int ch = 0; ch < channels; ++ch)
    {
        inputPointers[static_cast<size_t> (ch)] = input.getReadPointer (ch);
        outputPointers[static_cast<size_t> (ch)] = output.getWritePointer (ch);
    }

    process (inputPointers.data(), outputPointers.data(), samples);

    for (int ch = channels; ch < output.getNumChannels(); ++ch)
        output.clear (ch, 0, samples);
}

void PitchShifter::process (const float* const* input,
                            float* const* output,
                            int numSamples)
{
    if (! prepared_ || impl_ == nullptr
        || impl_->engines[static_cast<size_t> (impl_->active)] == nullptr
        || numSamples <= 0)
        return;

    std::array<const float*, 8> inputChunk {};
    std::array<float*, 8> outputChunk {};

    for (int offset = 0; offset < numSamples; offset += maxBlockSize_)
    {
        const int chunk = std::min (maxBlockSize_, numSamples - offset);

        for (int ch = 0; ch < numChannels_ && ch < 8; ++ch)
        {
            inputChunk[static_cast<size_t> (ch)] = input[static_cast<size_t> (ch)] + offset;
            outputChunk[static_cast<size_t> (ch)] = output[static_cast<size_t> (ch)] + offset;
        }

        impl_->engines[static_cast<size_t> (impl_->active)]->process (
            inputChunk.data(), outputChunk.data(), chunk);
    }
}

} // namespace namberry
