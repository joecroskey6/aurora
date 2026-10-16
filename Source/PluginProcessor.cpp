#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
               "The realtime overflow counter must be lock-free.");

AuroraAudioProcessor::AuroraAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      Thread ("Aurora analysis")
{
    setLatencySamples (0);
}

AuroraAudioProcessor::~AuroraAudioProcessor()
{
    stopWorker();
}

bool AuroraAudioProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto input = layout.getMainInputChannelSet();
    const auto output = layout.getMainOutputChannelSet();
    return input == output
        && (input == juce::AudioChannelSet::mono()
            || input == juce::AudioChannelSet::stereo());
}

void AuroraAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    // Hosts call prepare/release outside processBlock. Stop the sole FIFO
    // consumer before resetting its indices or changing the analysis rate.
    stopWorker();
    fifo.reset();
    analysisSampleRate = std::isfinite (sampleRate) && sampleRate > 0.0
                       ? sampleRate : 48000.0;
    analysisChannelCount = getTotalNumInputChannels() == 1 ? 1 : 2;
    analyzer.reset();
    resetRequested.store (false, std::memory_order_relaxed);
    discontinuity.store (false, std::memory_order_relaxed);
    droppedSamples.store (0, std::memory_order_relaxed);
    isReceiving.store (false, std::memory_order_relaxed);
    const auto started = startThread (juce::Thread::Priority::normal);
    acceptSamples.store (started, std::memory_order_release);
}

void AuroraAudioProcessor::stopWorker()
{
    acceptSamples.store (false, std::memory_order_release);
    // The worker only performs finite DSP work, then checks threadShouldExit.
    // Never force-kill it while it could hold the analyzer's snapshot mutex.
    stopThread (-1);
    isReceiving.store (false, std::memory_order_relaxed);
}

void AuroraAudioProcessor::releaseResources()
{
    stopWorker();
    fifo.reset();
}

void AuroraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    const int frames = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    if (frames <= 0 || channels <= 0
        || ! acceptSamples.load (std::memory_order_acquire))
        return;

    // There is one writer (host callback) and one reader (analysis worker).
    // A full FIFO drops analysis frames, never blocks or changes host audio.
    if (frames > fifo.getFreeSpace())
    {
        droppedSamples.fetch_add (static_cast<std::uint64_t> (frames),
                                  std::memory_order_relaxed);
        discontinuity.store (true, std::memory_order_release);
        return;
    }

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (frames, start1, size1, start2, size2);
    const auto* left = buffer.getReadPointer (0);
    const auto* right = buffer.getReadPointer (channels > 1 ? 1 : 0);
    std::copy_n (left, size1, fifoLeft.data() + start1);
    std::copy_n (right, size1, fifoRight.data() + start1);
    if (size2 > 0)
    {
        std::copy_n (left + size1, size2, fifoLeft.data() + start2);
        std::copy_n (right + size1, size2, fifoRight.data() + start2);
    }
    fifo.finishedWrite (size1 + size2);
    // Deliberately no event notification here: the worker polls on its own
    // timer, leaving the realtime path with no OS event or mutex operations.
}

void AuroraAudioProcessor::resetMeters() noexcept
{
    resetRequested.store (true, std::memory_order_release);
    droppedSamples.store (0, std::memory_order_relaxed);
}

void AuroraAudioProcessor::run()
{
    juce::ScopedNoDenormals noDenormals;
    std::array<float, analysisChunkSize> left {}, right {};
    auto lastSamples = juce::Time::getMillisecondCounterHiRes();
    while (! threadShouldExit())
    {
        const auto reset = resetRequested.exchange (false, std::memory_order_acq_rel);
        const auto gap = discontinuity.exchange (false, std::memory_order_acq_rel);
        if (reset || gap)
        {
            // Discard the backlog at the reset boundary. Missing samples must
            // not be joined into apparently contiguous LUFS / FFT windows.
            // Only this consumer changes the read index, while the producer
            // can safely continue publishing newer frames beyond this point.
            int first = 0, firstSize = 0, second = 0, secondSize = 0;
            fifo.prepareToRead (fifo.getNumReady(), first, firstSize, second, secondSize);
            fifo.finishedRead (firstSize + secondSize);
            if (gap)
                droppedSamples.fetch_add (static_cast<std::uint64_t> (firstSize + secondSize),
                                          std::memory_order_relaxed);
            analyzer.reset();
            isReceiving.store (false, std::memory_order_relaxed);
        }

        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToRead (analysisChunkSize, start1, size1, start2, size2);
        const int frames = size1 + size2;
        if (frames == 0)
        {
            if (juce::Time::getMillisecondCounterHiRes() - lastSamples > 500.0)
                isReceiving.store (false, std::memory_order_relaxed);
            wait (4);
            continue;
        }

        std::copy_n (fifoLeft.data() + start1, size1, left.data());
        std::copy_n (fifoRight.data() + start1, size1, right.data());
        if (size2 > 0)
        {
            std::copy_n (fifoLeft.data() + start2, size2, left.data() + size1);
            std::copy_n (fifoRight.data() + start2, size2, right.data() + size1);
        }
        fifo.finishedRead (frames);
        analyzer.process (left.data(), right.data(), frames, analysisSampleRate, analysisChannelCount);
        lastSamples = juce::Time::getMillisecondCounterHiRes();
        isReceiving.store (true, std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* AuroraAudioProcessor::createEditor()
{
    return new AuroraAudioProcessorEditor (*this);
}

void AuroraAudioProcessor::writeAppearanceState (juce::ValueTree& state) const
{
    state.setProperty ("visualMode", visualMode.load(), nullptr);
    state.setProperty ("cameraYaw", cameraYaw.load(), nullptr);
    state.setProperty ("cameraPitch", cameraPitch.load(), nullptr);
    state.setProperty ("cameraZoom", cameraZoom.load(), nullptr);
    state.setProperty ("palette", palette.load(), nullptr);
    state.setProperty ("customColourA", static_cast<juce::int64> (customColourA.load() & 0x00ffffffu), nullptr);
    state.setProperty ("customColourB", static_cast<juce::int64> (customColourB.load() & 0x00ffffffu), nullptr);
    state.setProperty ("customColourC", static_cast<juce::int64> (customColourC.load() & 0x00ffffffu), nullptr);
    state.setProperty ("themeMode", themeMode.load(), nullptr);
    state.setProperty ("layoutMode", layoutMode.load(), nullptr);
    state.setProperty ("waveformWindow", waveformWindow.load(), nullptr);
    state.setProperty ("waveformColour", waveformColour.load(), nullptr);
    state.setProperty ("waveformStyle", waveformStyle.load(), nullptr);
    state.setProperty ("waveformZoom", waveformZoom.load(), nullptr);
    state.setProperty ("visualGain", visualGain.load(), nullptr);
    for (const auto& descriptor : Customization::descriptors)
        state.setProperty (juce::Identifier (juce::String ("custom_") + descriptor.id),
                           custom.get (descriptor.id), nullptr);
}

void AuroraAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::ValueTree state ("AuroraMetersState");
    state.setProperty ("version", 1, nullptr);
    writeAppearanceState (state);
    state.setProperty ("frozen", frozen.load(), nullptr);
    state.setProperty ("sceneExpanded", sceneExpanded.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void AuroraAudioProcessor::setStateInformation (const void* data, int bytes)
{
    if (data == nullptr || bytes <= 0)
        return;
    const auto xml = getXmlFromBinary (data, bytes);
    if (xml == nullptr || ! xml->hasTagName ("AuroraMetersState"))
        return;
    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || static_cast<int> (state.getProperty ("version", 1)) != 1)
        return;

    readAppearanceState (state);
    frozen.store (static_cast<bool> (state.getProperty ("frozen", false)));
    // XML stores booleans as text. Only explicit true values enable expansion;
    // older sessions and malformed properties restore the normal dashboard.
    const auto expanded = state.getProperty ("sceneExpanded", false).toString().trim();
    sceneExpanded.store (expanded == "1" || expanded.equalsIgnoreCase ("true"));
}

void AuroraAudioProcessor::readAppearanceState (const juce::ValueTree& state)
{
    auto cameraValue = [&state] (const char* key, float fallback, float lo, float hi) {
        const float value = static_cast<float> (state.getProperty (key, fallback));
        return std::isfinite(value) ? juce::jlimit(lo, hi, value) : fallback;
    };
    cameraYaw.store(cameraValue("cameraYaw", 0, -juce::MathConstants<float>::pi, juce::MathConstants<float>::pi));
    cameraPitch.store(cameraValue("cameraPitch", 0, -juce::MathConstants<float>::pi, juce::MathConstants<float>::pi));
    cameraZoom.store(cameraValue("cameraZoom", 1, .55f, 2.4f));
    visualMode.store (juce::jlimit (0, 5, static_cast<int> (state.getProperty ("visualMode", 2))));
    // Missing selectors retain the appearance of older host sessions rather
    // than adopting the defaults used when a brand-new processor is created.
    palette.store (juce::jlimit (0, auroraPaletteCount - 1, static_cast<int> (state.getProperty ("palette", 0))));
    const auto readRGB = [&state] (const juce::Identifier& property, std::uint32_t fallback) {
        const auto value = static_cast<juce::int64> (state.getProperty (property, static_cast<juce::int64> (fallback)));
        return static_cast<std::uint32_t> (value) & 0x00ffffffu;
    };
    customColourA.store (readRGB ("customColourA", 0x214e68u));
    customColourB.store (readRGB ("customColourB", 0x82bac7u));
    customColourC.store (readRGB ("customColourC", 0xe4f2e9u));
    // Theme is independent of the visualization's palette. Version 1 presets
    // saved before this property existed retain the original default theme.
    themeMode.store (juce::jlimit (0, auroraThemeCount - 1, static_cast<int> (state.getProperty ("themeMode", 0))));
    layoutMode.store (juce::jlimit (0, 3, static_cast<int> (state.getProperty ("layoutMode", 0))));
    waveformWindow.store (juce::jlimit (0, 5, static_cast<int> (state.getProperty ("waveformWindow", 1))));
    waveformColour.store (juce::jlimit (0, 1, static_cast<int> (state.getProperty ("waveformColour", 0))));
    waveformStyle.store (juce::jlimit (0, 2, static_cast<int> (state.getProperty ("waveformStyle", 0))));
    waveformZoom.store (juce::jlimit (0, 2, static_cast<int> (state.getProperty ("waveformZoom", 0))));
    const auto gain = static_cast<float> (state.getProperty ("visualGain", 1.0));
    visualGain.store (std::isfinite (gain) ? juce::jlimit (0.25f, 4.0f, gain) : 1.0f);
    for (const auto& descriptor : Customization::descriptors)
        custom.set (descriptor.id, static_cast<float> (state.getProperty (
            juce::Identifier (juce::String ("custom_") + descriptor.id), descriptor.defaultValue)));
}

juce::String AuroraAudioProcessor::createAppearancePreset() const
{
    juce::ValueTree state ("AuroraAppearancePreset");
    state.setProperty ("version", 1, nullptr);
    writeAppearanceState (state);
    if (auto xml = state.createXml())
        return xml->toString (juce::XmlElement::TextFormat().withoutHeader());
    return {};
}

namespace
{
bool readPresetNumber (const juce::String& source, double& value)
{
    const auto text = source.trim();
    if (text.isEmpty() || text.length() > 64) return false;
    auto p = text.toRawUTF8();
    if (*p == '+' || *p == '-') ++p;
    bool digits = false;
    while (*p >= '0' && *p <= '9') { digits = true; ++p; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { digits = true; ++p; }
    }
    if (!digits) return false;
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        bool exponent = false;
        while (*p >= '0' && *p <= '9') { exponent = true; ++p; }
        if (!exponent) return false;
    }
    if (*p != 0) return false;
    value = text.getDoubleValue();
    return std::isfinite (value);
}
}

bool AuroraAudioProcessor::applyAppearancePreset (const juce::String& xmlText)
{
    if (xmlText.getNumBytesAsUTF8() > 256 * 1024) return false;
    auto body = xmlText.trim();
    if (body.startsWith ("<?xml ")) {
        const auto end = body.indexOf ("?>");
        if (end < 0) return false;
        body = body.substring (end + 2).trim();
    }
    // Presets are a single flat element. Reject DTD/entity declarations before
    // parsing, and wrap the body so extra trailing roots/text cannot be silently
    // ignored by the XML reader. No file/network input source is ever attached.
    if (body.isEmpty() || body.contains ("<!") || body.contains ("<?")
        || body.contains ("</AuroraPresetEnvelope")) return false;
    const auto envelope = juce::XmlDocument::parse (
        juce::String ("<AuroraPresetEnvelope>") + body + "</AuroraPresetEnvelope>");
    if (envelope == nullptr || envelope->getNumChildElements() != 1) return false;
    const auto* xml = envelope->getFirstChildElement();
    if (xml == nullptr || !xml->hasTagName ("AuroraAppearancePreset")
        || xml->getStringAttribute ("version") != "1" || xml->getNumChildElements() != 0) return false;
    auto staged = juce::ValueTree::fromXml (*xml);
    if (!staged.isValid()) return false;

    struct Field { const char* id; double minimum, maximum; bool integer; };
    const std::array fields {
        Field {"visualMode", 0, 5, true},
        Field {"palette", 0, auroraPaletteCount - 1, true},
        Field {"customColourA", 0, 0xffffff, true},
        Field {"customColourB", 0, 0xffffff, true},
        Field {"customColourC", 0, 0xffffff, true},
        Field {"themeMode", 0, auroraThemeCount - 1, true},
        Field {"layoutMode", 0, 3, true},
        Field {"waveformWindow", 0, 5, true},
        Field {"waveformColour", 0, 1, true},
        Field {"waveformStyle", 0, 2, true},
        Field {"waveformZoom", 0, 2, true},
        Field {"visualGain", .25, 4, false},
        Field {"cameraYaw", -juce::MathConstants<double>::pi, juce::MathConstants<double>::pi, false},
        Field {"cameraPitch", -juce::MathConstants<double>::pi, juce::MathConstants<double>::pi, false},
        Field {"cameraZoom", .55, 2.4, false}
    };
    for (const auto& field : fields) {
        if (!xml->hasAttribute (field.id)) continue;
        double value = 0;
        if (!readPresetNumber (xml->getStringAttribute (field.id), value)
            || (field.integer && std::trunc (value) != value)) return false;
        value = std::clamp (value, field.minimum, field.maximum);
        staged.setProperty (field.id, field.integer ? juce::var (static_cast<juce::int64> (value))
                                                   : juce::var (value), nullptr);
    }
    for (const auto& descriptor : Customization::descriptors) {
        const juce::String key = juce::String ("custom_") + descriptor.id;
        if (!xml->hasAttribute (key)) continue;
        double value = 0;
        if (!readPresetNumber (xml->getStringAttribute (key), value)
            || (descriptor.kind != Customization::Kind::Slider && std::trunc (value) != value)) return false;
        const auto finite = static_cast<float> (std::clamp (value, double(descriptor.min), double(descriptor.max)));
        staged.setProperty (juce::Identifier (key), Customization::sanitise (descriptor, finite), nullptr);
    }
    // Validation is complete before the first atomic write. Appearance changes
    // do not touch frozen, audio buffers, worker state, or meter history.
    readAppearanceState (staged);
    return true;
}

void AuroraAudioProcessor::resetAppearance()
{
    juce::ValueTree defaults ("AuroraAppearancePreset");
    defaults.setProperty ("palette", auroraDefaultPalette, nullptr);
    defaults.setProperty ("themeMode", auroraDefaultTheme, nullptr);
    readAppearanceState (defaults);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuroraAudioProcessor();
}
