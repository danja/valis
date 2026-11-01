// src/ops/AudioFile.cpp
#include "valis/AudioFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <memory>

namespace valis {

namespace {

juce::File resolve(const std::string& path)
{
    return juce::File::isAbsolutePath(juce::String(path))
               ? juce::File(juce::String(path))
               : juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(path));
}

} // namespace

bool readAudioFile(const std::string& path, StereoAudio& audio, std::string& error)
{
    const auto file = resolve(path);
    if (! file.existsAsFile())
    {
        error = "no such file: " + file.getFullPathName().toStdString();
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "cannot read " + file.getFullPathName().toStdString() + " as a sound file";
        return false;
    }

    const auto frames = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(std::max(2, static_cast<int>(reader->numChannels)), frames);
    buffer.clear();
    reader->read(&buffer, 0, frames, 0, true, true);
    if (reader->numChannels == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, frames);

    audio.sampleRate = reader->sampleRate;
    audio.left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    audio.right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    return true;
}

bool writeWavFile(const std::string& path, const StereoAudio& audio, std::string& error)
{
    const auto file = resolve(path);
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
    {
        error = "cannot write " + file.getFullPathName().toStdString();
        return false;
    }

    const auto options = juce::AudioFormatWriterOptions()
                             .withSampleRate(audio.sampleRate)
                             .withNumChannels(2)
                             .withBitsPerSample(24);
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream, options));
    if (writer == nullptr)
    {
        error = "cannot create a wav writer for " + file.getFullPathName().toStdString();
        return false;
    }

    const float* channels[] = {audio.left.data(), audio.right.data()};
    writer->writeFromFloatArrays(channels, 2, static_cast<int>(audio.frames()));
    return true;
}

} // namespace valis
