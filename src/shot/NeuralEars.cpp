// src/shot/NeuralEars.cpp
//
// Compiled only with VALIS_WITH_ONNX. Everything that touches ONNX Runtime is in this file.

#include "valis/NeuralEars.h"

#include "valis/AstFrontend.h"
#include "valis/BpeTokenizer.h"
#include "valis/ClapFrontend.h"

#include <onnxruntime_cxx_api.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace valis {

namespace {

void normalise(std::vector<float>& v)
{
    double sum = 0.0;
    for (float x : v)
        sum += static_cast<double>(x) * x;
    const float norm = static_cast<float>(std::sqrt(sum) + 1.0e-12);
    for (float& x : v)
        x /= norm;
}

std::vector<std::string> names(Ort::Session& session, bool inputs)
{
    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<std::string> out;
    const std::size_t n = inputs ? session.GetInputCount() : session.GetOutputCount();
    for (std::size_t i = 0; i < n; ++i)
        out.push_back((inputs ? session.GetInputNameAllocated(i, allocator) : session.GetOutputNameAllocated(i, allocator)).get());
    return out;
}

std::vector<float> mono48k(const std::vector<float>& mono, double rate)
{
    return rate == kClapRate ? mono : resampleLinear(mono, rate, kClapRate);
}

}  // namespace

struct NeuralEars::Impl
{
    Ort::Env env{ORT_LOGGING_LEVEL_ERROR, "valis-shot"};
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> audio, text, ast;
    std::vector<std::string> audioIn, audioOut, textIn, textOut, astIn, astOut;
    BpeTokenizer tokenizer;
    std::vector<std::string> labels;

    Impl()
    {
        options.SetIntraOpNumThreads(4);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    std::vector<float> clapWindow(const std::vector<float>& window, bool zeroPad)
    {
        const auto mel = clapLogMel(clapPrepare(window, zeroPad));
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 4> shape{1, 1, kClapFrames, kClapMels};
        std::vector<Ort::Value> inputs;
        std::vector<const char*> inputNames;
        bool longer[1] = {false};
        const std::array<int64_t, 2> flagShape{1, 1};
        for (const auto& name : audioIn)
        {
            inputNames.push_back(name.c_str());
            if (name.find("longer") != std::string::npos)
                inputs.push_back(Ort::Value::CreateTensor<bool>(memory, longer, 1, flagShape.data(), 2));
            else
                inputs.push_back(Ort::Value::CreateTensor<float>(memory, const_cast<float*>(mel.data()), mel.size(), shape.data(), 4));
        }
        const char* outputName = audioOut.front().c_str();
        auto result = audio->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(), &outputName, 1);
        const float* data = result.front().GetTensorData<float>();
        std::vector<float> embedding(data, data + result.front().GetTensorTypeAndShapeInfo().GetElementCount());
        normalise(embedding);
        return embedding;
    }
};

NeuralEars::NeuralEars() : impl(std::make_unique<Impl>()) {}
NeuralEars::~NeuralEars() = default;

std::string NeuralEars::defaultModelsDir()
{
    if (const char* env = std::getenv("VALIS_MODELS"))
        return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.local/share/valis/models";
}

bool NeuralEars::hasClap() const { return impl->audio && impl->text && impl->tokenizer.loaded(); }
bool NeuralEars::hasAst() const { return impl->ast != nullptr; }

bool NeuralEars::load(const std::string& dir, std::string& error)
{
    std::string problems;
    try
    {
        const juce::File root(dir);
        const auto audioFile = root.getChildFile("clap/audio_model.onnx"), textFile = root.getChildFile("clap/text_model.onnx");
        if (audioFile.existsAsFile() && textFile.existsAsFile())
        {
            std::string why;
            if (impl->tokenizer.load(root.getChildFile("clap/vocab.json").getFullPathName().toStdString(),
                                     root.getChildFile("clap/merges.txt").getFullPathName().toStdString(), why))
            {
                impl->audio = std::make_unique<Ort::Session>(impl->env, audioFile.getFullPathName().toRawUTF8(), impl->options);
                impl->text = std::make_unique<Ort::Session>(impl->env, textFile.getFullPathName().toRawUTF8(), impl->options);
                impl->audioIn = names(*impl->audio, true);
                impl->audioOut = names(*impl->audio, false);
                impl->textIn = names(*impl->text, true);
                impl->textOut = names(*impl->text, false);
            }
            else
                problems += "CLAP tokenizer: " + why + "; ";
        }
        else
            problems += "no clap/audio_model.onnx and clap/text_model.onnx in " + dir + "; ";

        const auto astFile = root.getChildFile("ast/model.onnx"), astConfig = root.getChildFile("ast/config.json");
        if (astFile.existsAsFile() && astConfig.existsAsFile())
        {
            const juce::var config = juce::JSON::parse(astConfig);
            if (auto* id2label = config.getProperty("id2label", {}).getDynamicObject())
            {
                impl->labels.assign(527, "");
                for (const auto& p : id2label->getProperties())
                {
                    const int index = p.name.toString().getIntValue();
                    if (index >= 0 && index < 527)
                        impl->labels[static_cast<std::size_t>(index)] = p.value.toString().toStdString();
                }
                impl->ast = std::make_unique<Ort::Session>(impl->env, astFile.getFullPathName().toRawUTF8(), impl->options);
                impl->astIn = names(*impl->ast, true);
                impl->astOut = names(*impl->ast, false);
            }
        }
        else
            problems += "no ast/model.onnx and ast/config.json in " + dir + "; ";
    }
    catch (const Ort::Exception& e)
    {
        error = std::string("ONNX Runtime: ") + e.what();
        return false;
    }
    if (! hasClap() && ! hasAst())
    {
        error = problems + "run scripts/shot/fetch-models.sh";
        return false;
    }
    return true;
}

std::vector<float> NeuralEars::embedAudio(const std::vector<float>& mono, double rate, bool zeroPad)
{
    if (! hasClap() || mono.empty())
        return {};
    const auto x = mono48k(mono, rate);
    std::vector<std::vector<float>> windows;
    if (x.size() <= static_cast<std::size_t>(kClapSamples))
        windows.push_back(x);
    else
        for (std::size_t start = 0; start < x.size(); start += kClapSamples / 2)
        {
            const std::size_t end = std::min(x.size(), start + static_cast<std::size_t>(kClapSamples));
            windows.emplace_back(x.begin() + static_cast<long>(start), x.begin() + static_cast<long>(end));
            if (end == x.size())
                break;
        }
    std::vector<float> mean(512, 0.0f);
    for (const auto& w : windows)
    {
        const auto e = impl->clapWindow(w, zeroPad);
        mean.resize(e.size(), 0.0f);
        for (std::size_t i = 0; i < e.size(); ++i)
            mean[i] += e[i];
    }
    normalise(mean);
    return mean;
}

std::vector<float> NeuralEars::embedText(const std::string& sentence)
{
    if (! hasClap())
        return {};
    const auto encoded = impl->tokenizer.encode(sentence);
    std::vector<int64_t> ids(encoded.begin(), encoded.end());
    if (ids.size() > 77)
    {
        ids.resize(77);
        ids.back() = 2;
    }
    std::vector<int64_t> mask(ids.size(), 1);
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 2> shape{1, static_cast<int64_t>(ids.size())};
    std::vector<Ort::Value> inputs;
    std::vector<const char*> inputNames;
    for (const auto& name : impl->textIn)
    {
        inputNames.push_back(name.c_str());
        auto& source = name.find("mask") != std::string::npos ? mask : ids;
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(memory, source.data(), source.size(), shape.data(), 2));
    }
    const char* outputName = impl->textOut.front().c_str();
    auto result = impl->text->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputs.data(), inputs.size(), &outputName, 1);
    const float* data = result.front().GetTensorData<float>();
    std::vector<float> embedding(data, data + result.front().GetTensorTypeAndShapeInfo().GetElementCount());
    normalise(embedding);
    return embedding;
}

std::vector<AudioTag> NeuralEars::tags(const std::vector<float>& mono, double rate, int top)
{
    if (! hasAst() || mono.empty())
        return {};
    const auto fbank = astFilterbank(resampleLinear(mono, rate, kAstRate));
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    const std::array<int64_t, 3> shape{1, kAstFrames, kAstMels};
    const char* inputName = impl->astIn.front().c_str();
    Ort::Value input = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(fbank.data()), fbank.size(), shape.data(), 3);
    const char* outputName = impl->astOut.front().c_str();
    auto result = impl->ast->Run(Ort::RunOptions{nullptr}, &inputName, &input, 1, &outputName, 1);
    const float* logits = result.front().GetTensorData<float>();
    std::vector<AudioTag> all;
    for (std::size_t i = 0; i < impl->labels.size(); ++i)
        all.push_back({impl->labels[i], 1.0f / (1.0f + std::exp(-logits[i]))});
    std::sort(all.begin(), all.end(), [](const AudioTag& a, const AudioTag& b) { return a.probability > b.probability; });
    all.resize(std::min<std::size_t>(all.size(), static_cast<std::size_t>(std::max(top, 0))));
    return all;
}

}  // namespace valis
