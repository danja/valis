// src/dsp/SampleFile.h
//
// Reading a sound file, and checking it is the file the circuit meant.
//
// Shared by val:Granulator and val:SampleLoad, which differ in what they do
// with the samples rather than in how they get them, and available to any
// element that reads one later.
//
// Message thread only: it opens a file and allocates, so it belongs nowhere
// near process().

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace valis::dsp {

/// Reads a sound file into a mono buffer at the engine's rate. Message thread
/// only: it opens a file and allocates, so it belongs nowhere near process().
///
/// Shared by val:Granulator and val:SampleLoad, which differ in what they do
/// with the samples, not in how they get them.
struct SampleFile
{
    std::vector<float> samples;
    std::string name;      ///< the file's own name, for the Controls view

    /// What the file turned out to be, as opposed to what the circuit declared
    /// it would be. A circuit that states any of these has them checked, so a
    /// sample that was replaced or truncated since the circuit was written
    /// fails to load with a message rather than playing something else.
    std::string sha256;
    int    sourceChannels   = 0;
    double sourceSampleRate = 0.0;
    long long sourceFrames  = 0;

    bool load(std::string_view path, double sampleRate, std::string& error)
    {
        const auto file = resolve(path);
        if (! file.existsAsFile())
        {
            error = "no such file: " + file.getFullPathName().toStdString();
            return false;
        }

        // Message thread, so reading the file twice is affordable and the hash
        // is of the bytes on disk rather than of whatever decoding produced.
        {
            juce::FileInputStream stream(file);
            if (stream.openedOk())
                sha256 = juce::SHA256(stream).toHexString().toStdString();
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            error = "unreadable audio file: " + file.getFullPathName().toStdString();
            return false;
        }

        const auto frames = static_cast<int>(std::min<juce::int64>(
            reader->lengthInSamples, static_cast<juce::int64>(sampleRate * 60.0)));
        if (frames <= 0)
        {
            error = "audio file is empty: " + file.getFullPathName().toStdString();
            return false;
        }

        sourceChannels   = static_cast<int>(reader->numChannels);
        sourceSampleRate = reader->sampleRate;
        sourceFrames     = static_cast<long long>(reader->lengthInSamples);

        juce::AudioBuffer<float> source(static_cast<int>(reader->numChannels), frames);
        reader->read(&source, 0, frames, 0, true, true);

        // The circuit model is mono; a stereo file is summed rather than having
        // one of its channels dropped.
        std::vector<float> mono(static_cast<std::size_t>(frames), 0.0f);
        for (int c = 0; c < source.getNumChannels(); ++c)
        {
            const float* channel = source.getReadPointer(c);
            for (int i = 0; i < frames; ++i)
                mono[static_cast<std::size_t>(i)] += channel[i];
        }
        if (source.getNumChannels() > 1)
            for (auto& sample : mono)
                sample /= static_cast<float>(source.getNumChannels());

        const double ratio = reader->sampleRate > 0.0 ? reader->sampleRate / sampleRate : 1.0;
        const auto resampled = static_cast<int>(static_cast<double>(frames) / ratio);

        samples.assign(static_cast<std::size_t>(std::max(resampled, 1)), 0.0f);
        for (int i = 0; i < resampled; ++i)
        {
            const double at = static_cast<double>(i) * ratio;
            const auto   i0 = static_cast<int>(at);
            const auto   f  = static_cast<float>(at - static_cast<double>(i0));
            const float  y0 = mono[static_cast<std::size_t>(std::min(i0, frames - 1))];
            const float  y1 = mono[static_cast<std::size_t>(std::min(i0 + 1, frames - 1))];
            samples[static_cast<std::size_t>(i)] = y0 + f * (y1 - y0);
        }

        name = file.getFileName().toStdString();
        return true;
    }

    bool loaded() const { return sourceFrames > 0; }

    /// A relative path is resolved against the working directory first, then
    /// the shipped examples, so a circuit can name a sample next to itself.
    static juce::File resolve(std::string_view path)
    {
        const juce::String text{std::string(path)};

        if (text.startsWith("~/"))
            return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                       .getChildFile(text.substring(2));

        if (juce::File::isAbsolutePath(text))
            return juce::File(text);

        const auto relative = juce::File::getCurrentWorkingDirectory().getChildFile(text);
        if (relative.existsAsFile())
            return relative;

        const auto shipped = juce::File(VALIS_EXAMPLES_DIR).getChildFile(text);
        if (shipped.existsAsFile())
            return shipped;

        const auto root = juce::File(VALIS_ROOT_DIR).getChildFile(text);
        if (root.existsAsFile())
            return root;

        return relative;
    }
};

/// What a circuit declares the sound file it names will be: its content hash
/// and its dimensions. The survey's point is that a sample is a resource with
/// an identity, not an unexplained path. A circuit that declares none of these
/// behaves exactly as before.
///
/// Checking is order independent. RDF has no statement order, so a declaration
/// may arrive before or after val:file; it is remembered either way and checked
/// as soon as both halves are known.
struct SampleExpectation
{
    std::string sha256;
    int    channels   = 0;
    double sampleRate = 0.0;
    long long frames  = 0;

    /// Whether `key` was one of the declarations, and if so whether its value
    /// could be read. A key nobody recognises is not a failure; a key that is
    /// recognised and malformed is, so a typo does not pass for "not declared".
    enum class Declared { No, Yes, Bad };

    Declared setDeclared(std::string_view key, std::string_view value, std::string& error)
    {
        const juce::String text{std::string(value)};

        const auto positive = [&](double parsed, std::string_view what)
        {
            if (parsed > 0.0)
                return Declared::Yes;

            error = "val:" + std::string(what) + " must be a positive number, not '" +
                    std::string(value) + "'";
            return Declared::Bad;
        };

        if (key == "sha256")
        {
            // 64 hex characters, or it is not a SHA-256 and never will match.
            if (text.length() != 64 || ! text.containsOnly("0123456789abcdefABCDEF"))
            {
                error = "val:sha256 must be 64 hexadecimal characters, not '" +
                        std::string(value) + "'";
                return Declared::Bad;
            }
            sha256 = text.toLowerCase().toStdString();
            return Declared::Yes;
        }

        if (key == "channels")
        {
            channels = text.getIntValue();
            return positive(channels, "channels");
        }
        if (key == "sampleRate")
        {
            sampleRate = text.getDoubleValue();
            return positive(sampleRate, "sampleRate");
        }
        if (key == "frames")
        {
            frames = text.getLargeIntValue();
            return positive(static_cast<double>(frames), "frames");
        }

        return Declared::No;
    }

    bool check(const SampleFile& file, std::string& error) const
    {
        if (! file.loaded())
            return true;   // nothing to check against yet

        const auto mismatch = [&](std::string_view what,
                                  const std::string& declared,
                                  const std::string& actual)
        {
            error = file.name + ": declared " + std::string(what) + " " + declared +
                    ", file has " + actual;
            return false;
        };

        if (! sha256.empty() && sha256 != file.sha256)
            return mismatch("val:sha256", sha256, file.sha256);

        if (channels != 0 && channels != file.sourceChannels)
            return mismatch("val:channels", std::to_string(channels),
                            std::to_string(file.sourceChannels));

        if (sampleRate != 0.0 && std::abs(sampleRate - file.sourceSampleRate) > 0.5)
            return mismatch("val:sampleRate", std::to_string(static_cast<long long>(sampleRate)),
                            std::to_string(static_cast<long long>(file.sourceSampleRate)));

        if (frames != 0 && frames != file.sourceFrames)
            return mismatch("val:frames", std::to_string(frames),
                            std::to_string(file.sourceFrames));

        return true;
    }
};

}  // namespace valis::dsp
