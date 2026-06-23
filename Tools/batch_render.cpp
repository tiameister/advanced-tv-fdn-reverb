/**
 * batch_render — offline WAV processor for ReverbEngine.
 *
 * Usage:
 *   batch_render <input.wav> [output_directory]
 *
 * Reads a stereo (or mono) WAV file, renders one output file per parameter
 * configuration (distance × stereo width), and writes 32-bit float WAVs.
 *
 * Example:
 *   batch_render audio.wav renders/
 *   → renders/dist_0.0_width_0.0.wav
 *     renders/dist_0.5_width_1.0.wav
 *     ...
 */

#include "DSP/ReverbEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{

constexpr int kBlockSize = 512;

struct RenderConfig
{
    float distance;
    float stereoWidth;
};

// Batch matrix: distance × FDN stereo width
const std::vector<RenderConfig> kBatchConfigs {
    { 0.0f, 0.0f },
    { 0.0f, 1.0f },
    { 0.0f, 2.0f },
    { 0.5f, 0.0f },
    { 0.5f, 1.0f },
    { 0.5f, 2.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f },
    { 1.0f, 2.0f },
};

std::string formatOutputName(float distance, float stereoWidth)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "dist_%.1f_width_%.1f.wav",
                  static_cast<double>(distance),
                  static_cast<double>(stereoWidth));
    return std::string(buf);
}

bool readInputWav(const juce::File& inputFile,
                  juce::AudioFormatManager& formatManager,
                  juce::AudioBuffer<float>& dest,
                  double& sampleRateOut)
{
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(inputFile));

    if (reader == nullptr)
    {
        std::fprintf(stderr, "ERROR: Could not read input file: %s\n",
                     inputFile.getFullPathName().toRawUTF8());
        return false;
    }

    if (reader->numChannels < 1)
    {
        std::fprintf(stderr, "ERROR: Input has no audio channels.\n");
        return false;
    }

    sampleRateOut = reader->sampleRate;

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    const int numChannels = static_cast<int>(reader->numChannels);

    dest.setSize(numChannels, numSamples, false, true, true);

    if (!reader->read(&dest, 0, numSamples, 0, true, true))
    {
        std::fprintf(stderr, "ERROR: Failed to decode samples from: %s\n",
                     inputFile.getFullPathName().toRawUTF8());
        return false;
    }

    std::printf("Loaded %s — %d ch, %d samples @ %.0f Hz (%.2f s)\n",
                inputFile.getFileName().toRawUTF8(),
                numChannels,
                numSamples,
                sampleRateOut,
                static_cast<double>(numSamples) / sampleRateOut);

    return true;
}

bool writeOutputWav(const juce::File& outputFile,
                    const juce::AudioBuffer<float>& buffer,
                    double sampleRate)
{
    outputFile.getParentDirectory().createDirectory();

    auto outputStream = std::unique_ptr<juce::FileOutputStream>(outputFile.createOutputStream());
    if (outputStream == nullptr || !outputStream->openedOk())
    {
        std::fprintf(stderr, "ERROR: Could not open output file: %s\n",
                     outputFile.getFullPathName().toRawUTF8());
        return false;
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(outputStream.get(),
                                  sampleRate,
                                  static_cast<unsigned int>(buffer.getNumChannels()),
                                  32,
                                  {},
                                  0));

    if (writer == nullptr)
    {
        std::fprintf(stderr, "ERROR: Could not create WAV writer for: %s\n",
                     outputFile.getFullPathName().toRawUTF8());
        return false;
    }

    outputStream.release();

    if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
    {
        std::fprintf(stderr, "ERROR: Failed to write samples to: %s\n",
                     outputFile.getFullPathName().toRawUTF8());
        return false;
    }

    return true;
}

bool renderConfiguration(const juce::AudioBuffer<float>& dryInput,
                         double sampleRate,
                         const RenderConfig& config,
                         const juce::File& outputFile)
{
    // Set targets BEFORE prepare() so AdvancedFDN snaps width on prepare,
    // and ReverbEngine::reset() (called at end of prepare) snaps distance.
    ReverbEngine engine;
    engine.setDistance(config.distance);
    engine.setFdnStereoWidth(config.stereoWidth);
    engine.setMasterWet(1.0f);
    engine.setReverbTime(2.5f);                    // plugin default (replaces setFdnFeedback)
    engine.setDecayShape(1.4f, 1.0f, 0.2f);        // bass / mid / HF tilt multipliers
    engine.setSize(0.33f);                         // medium room
    engine.setFdnModDepth(2.5f);

    engine.prepare(sampleRate, kBlockSize);

    const int totalSamples = dryInput.getNumSamples();
    const int numChannels  = dryInput.getNumChannels();

    juce::AudioBuffer<float> wetBuffer(numChannels, totalSamples);
    wetBuffer.clear();

    std::vector<float> blockL(static_cast<std::size_t>(kBlockSize));
    std::vector<float> blockR(static_cast<std::size_t>(kBlockSize));

    int samplePos = 0;
    while (samplePos < totalSamples)
    {
        const int numThisBlock = std::min(kBlockSize, totalSamples - samplePos);

        if (numChannels >= 2)
        {
            std::memcpy(blockL.data(), dryInput.getReadPointer(0, samplePos),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
            std::memcpy(blockR.data(), dryInput.getReadPointer(1, samplePos),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
        }
        else
        {
            std::memcpy(blockL.data(), dryInput.getReadPointer(0, samplePos),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
            std::memcpy(blockR.data(), blockL.data(),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
        }

        // ReverbEngine::processBlock is in-place (L/R read and written).
        engine.processBlock(blockL.data(), blockR.data(), numThisBlock);

        if (numChannels >= 2)
        {
            std::memcpy(wetBuffer.getWritePointer(0, samplePos), blockL.data(),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
            std::memcpy(wetBuffer.getWritePointer(1, samplePos), blockR.data(),
                        static_cast<std::size_t>(numThisBlock) * sizeof(float));
        }
        else
        {
            for (int i = 0; i < numThisBlock; ++i)
                wetBuffer.setSample(0, samplePos + i,
                                  0.5f * (blockL[static_cast<std::size_t>(i)]
                                        + blockR[static_cast<std::size_t>(i)]));
        }

        samplePos += numThisBlock;
    }

    if (!writeOutputWav(outputFile, wetBuffer, sampleRate))
        return false;

    std::printf("  Wrote %s (distance=%.1f, width=%.1f)\n",
                outputFile.getFileName().toRawUTF8(),
                config.distance,
                config.stereoWidth);
    return true;
}

void printUsage(const char* argv0)
{
    std::fprintf(stderr,
                 "Usage: %s <input.wav> [output_directory]\n"
                 "\n"
                 "Renders the input through ReverbEngine for each distance ×\n"
                 "stereo-width configuration and writes uniquely named WAV files.\n"
                 "\n"
                 "Default output directory: ./batch_output/\n",
                 argv0);
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        printUsage(argv[0]);
        return 1;
    }

    const juce::File inputFile  = juce::File::getCurrentWorkingDirectory()
                                      .getChildFile(juce::String(argv[1]));
    const juce::File outputDir  = (argc >= 3)
        ? juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[2]))
        : juce::File::getCurrentWorkingDirectory().getChildFile("batch_output");

    if (!inputFile.existsAsFile())
    {
        std::fprintf(stderr, "ERROR: Input file not found: %s\n",
                     inputFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    juce::AudioBuffer<float> dryInput;
    double sampleRate = 0.0;

    if (!readInputWav(inputFile, formatManager, dryInput, sampleRate))
        return 1;

    if (!outputDir.createDirectory())
    {
        std::fprintf(stderr, "ERROR: Could not create output directory: %s\n",
                     outputDir.getFullPathName().toRawUTF8());
        return 1;
    }

    std::printf("Rendering %zu configurations → %s\n",
                kBatchConfigs.size(),
                outputDir.getFullPathName().toRawUTF8());

    int failures = 0;
    for (const RenderConfig& config : kBatchConfigs)
    {
        const juce::File outFile = outputDir.getChildFile(
            juce::String(formatOutputName(config.distance, config.stereoWidth)));

        if (!renderConfiguration(dryInput, sampleRate, config, outFile))
            ++failures;
    }

    if (failures > 0)
    {
        std::fprintf(stderr, "Completed with %d failure(s).\n", failures);
        return 1;
    }

    std::printf("PASS: batch_render completed (%zu files).\n", kBatchConfigs.size());
    return 0;
}
