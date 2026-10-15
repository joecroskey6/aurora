#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <set>
#include <tuple>
#include <vector>

namespace
{
int checks = 0;

void require (bool condition, const char* description)
{
    ++checks;
    if (! condition)
        throw std::runtime_error (description);
}

bool await (const std::function<bool()>& condition, int timeoutMilliseconds = 2000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMilliseconds;
    do
    {
        if (condition())
            return true;
        juce::Thread::sleep (2);
    }
    while (juce::Time::getMillisecondCounterHiRes() < deadline);
    return condition();
}

void configure (AuroraAudioProcessor& processor, int channels, double rate, int block = 1024)
{
    processor.releaseResources();
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set (0, set);
    layout.outputBuses.set (0, set);
    require (processor.setBusesLayout (layout), "Supported mono/stereo layout must configure");
    processor.setRateAndBufferSizeDetails (rate, block);
    processor.prepareToPlay (rate, block);
}

void fillPattern (juce::AudioBuffer<float>& buffer)
{
    std::uint32_t seed = 0x719af034u;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
        {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            buffer.setSample (channel, frame,
                              static_cast<float> (static_cast<int> (seed % 200001u) - 100000) / 90000.0f);
        }

    // Verify pass-through also preserves signed zero and unusual float bits.
    if (buffer.getNumSamples() >= 7)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* samples = buffer.getWritePointer (channel);
            samples[0] = -0.0f;
            samples[1] = std::numeric_limits<float>::denorm_min();
            samples[2] = std::numeric_limits<float>::infinity();
            samples[3] = -std::numeric_limits<float>::infinity();
            const std::uint32_t quietNanPayload = 0x7fc02345u;
            std::memcpy (samples + 4, &quietNanPayload, sizeof (float));
            samples[5] = 2.0f;
            samples[6] = -2.0f;
        }
}

void assertPassThrough (AuroraAudioProcessor& processor, int channels, int frames)
{
    juce::AudioBuffer<float> buffer (channels, frames);
    fillPattern (buffer);
    std::vector<std::vector<float>> before;
    for (int channel = 0; channel < channels; ++channel)
        if (frames > 0)
            before.emplace_back (buffer.getReadPointer (channel), buffer.getReadPointer (channel) + frames);
        else
            before.emplace_back();
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);
    for (int channel = 0; channel < channels; ++channel)
        require (frames == 0 || std::memcmp (before[static_cast<std::size_t> (channel)].data(),
                                            buffer.getReadPointer (channel),
                                            static_cast<std::size_t> (frames) * sizeof (float)) == 0,
                 "Audio must remain bit exact through the processor");
}

void testPassThroughAndLifecycle()
{
    AuroraAudioProcessor processor;
    require (! processor.acceptsMidi() && ! processor.producesMidi() && ! processor.isMidiEffect(),
             "Meter must not advertise MIDI support");
    require (processor.getLatencySamples() == 0 && processor.getTailLengthSeconds() == 0.0,
             "Meter must add no latency or tail");
    for (int channels : { 1, 2 })
        for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            configure (processor, channels, rate, 8192);
            for (int size : { 0, 1, 7, 63, 128, 511, 1024, 2048, 8192 })
                assertPassThrough (processor, channels, size);
            processor.releaseResources();
            require (! processor.isReceiving.load(), "Release must stop receiving status");
            assertPassThrough (processor, channels, 128);
        }

    // Repeated host starts/stops must safely recreate the FIFO consumer.
    for (int cycle = 0; cycle < 12; ++cycle)
    {
        configure (processor, cycle % 2 + 1, cycle % 2 == 0 ? 44100.0 : 96000.0);
        assertPassThrough (processor, cycle % 2 + 1, 257);
        processor.releaseResources();
    }
    auto unsupported = processor.getBusesLayout();
    unsupported.inputBuses.set (0, juce::AudioChannelSet::mono());
    unsupported.outputBuses.set (0, juce::AudioChannelSet::stereo());
    require (! processor.isBusesLayoutSupported (unsupported), "Mismatched channel layouts must be rejected");
    unsupported.inputBuses.set (0, juce::AudioChannelSet::create5point1());
    unsupported.outputBuses.set (0, juce::AudioChannelSet::create5point1());
    require (! processor.isBusesLayoutSupported (unsupported), "Unsupported surround layout must be rejected");
}

void sendTone (AuroraAudioProcessor& processor, int channels, int frames,
               double rate, float leftGain = 0.5f, float rightGain = 0.25f)
{
    constexpr int block = 512;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer (channels, block);
    for (int start = 0; start < frames; start += block)
    {
        const int count = std::min (block, frames - start);
        buffer.setSize (channels, count, false, false, true);
        for (int frame = 0; frame < count; ++frame)
        {
            const auto sine = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi
                                                          * 1000.0 * (start + frame) / rate));
            buffer.setSample (0, frame, leftGain * sine);
            if (channels > 1)
                buffer.setSample (1, frame, rightGain * sine);
        }
        processor.processBlock (buffer, midi);
        // Allow the asynchronous DSP worker to keep up without depending on
        // machine speed or a realtime output device.
        juce::Thread::sleep (2);
    }
}

void testAnalysisResetAndOverflow()
{
    AuroraAudioProcessor processor;
    configure (processor, 2, 48000.0);
    sendTone (processor, 2, 48000, 48000.0);
    require (await ([&] { return processor.analyzer.snapshot().momentaryLUFS > -100.0f; }),
             "Worker must receive audio and produce loudness");
    const auto stereo = processor.analyzer.snapshot();
    require (std::abs (stereo.peakL + 6.0206f) < 0.1f
             && std::abs (stereo.peakR + 12.0412f) < 0.1f,
             "Worker must preserve stereo channel levels");
    require (stereo.sampleRate == 48000.0 && processor.isReceiving.load(),
             "Worker must publish rate and receiving status");

    // A single oversized block deterministically exceeds the bounded FIFO,
    // without relying on scheduler timing or starving the analysis thread.
    assertPassThrough (processor, 2, 40000);
    require (processor.droppedSamples.load() >= 40000u,
             "Overflow must expose dropped analysis frames");
    require (await ([&] {
        const auto frame = processor.analyzer.snapshot();
        return frame.sequence > stereo.sequence
            && frame.integratedLUFS == AudioAnalyzer::silenceDB
            && frame.peakL == AudioAnalyzer::silenceDB;
    }), "Overflow must reset measurements instead of spanning missing samples");

    sendTone (processor, 2, 8192, 48000.0);
    require (await ([&] { return processor.analyzer.snapshot().peakL > -100.0f; }),
             "Analysis must recover after overflow");
    const auto beforeReset = processor.analyzer.snapshot().sequence;
    processor.resetMeters();
    require (await ([&] {
        const auto frame = processor.analyzer.snapshot();
        return frame.sequence > beforeReset && frame.peakL == AudioAnalyzer::silenceDB;
    }), "Explicit reset must be consumed by the worker");
    require (processor.droppedSamples.load() == 0, "Reset must clear overload count");

    configure (processor, 1, 96000.0);
    sendTone (processor, 1, 16384, 96000.0);
    require (await ([&] { return processor.analyzer.snapshot().sampleRate == 96000.0; }),
             "Reprepare must deliver the new sample rate");
    const auto mono = processor.analyzer.snapshot();
    require (mono.left == mono.right && std::abs (mono.peakL - mono.peakR) < 0.001f,
             "Mono must duplicate its display channel without reading an absent buffer");
    require (await ([&] { return ! processor.isReceiving.load(); }),
             "Receiving status must clear when host callbacks cease");

    configure (processor, 1, 48000.0);
    sendTone (processor, 1, 48000, 48000.0, 0.5f, 0.5f);
    require (await ([&] { return processor.analyzer.snapshot().momentaryLUFS > -100.0f; }),
             "Mono bus must produce loudness");
    const auto monoLoudness = processor.analyzer.snapshot().momentaryLUFS;
    configure (processor, 2, 48000.0);
    sendTone (processor, 2, 48000, 48000.0, 0.5f, 0.5f);
    require (await ([&] { return processor.analyzer.snapshot().momentaryLUFS > -100.0f; }),
             "Stereo bus must produce loudness");
    require (std::abs (processor.analyzer.snapshot().momentaryLUFS - monoLoudness - 3.0103f) < 0.1f,
             "Processor must tell the analyzer mono channel count instead of measuring a false stereo pair");
    processor.releaseResources();
}

juce::MemoryBlock encode (juce::ValueTree state)
{
    juce::MemoryBlock data;
    if (auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, data);
    return data;
}

void apply (AuroraAudioProcessor& processor, const juce::MemoryBlock& data)
{
    processor.setStateInformation (data.getData(), static_cast<int> (data.getSize()));
}

void testState()
{
    AuroraAudioProcessor original;
    require (original.visualMode.load() == 2 && original.palette.load() == auroraDefaultPalette
             && original.palette.load() == 7 && original.themeMode.load() == auroraFlightDeckTheme,
             "New instances must default to Pearl Cloud, Phosphor and Flight Deck");
    require (original.customColourA.load() == 0x214e68u
             && original.customColourB.load() == 0x82bac7u
             && original.customColourC.load() == 0xe4f2e9u,
             "New instances must use the documented custom RGB defaults");
    require (original.cameraYaw.load() == 0.0f && original.cameraPitch.load() == 0.0f
             && original.cameraZoom.load() == 1.0f,
             "New instances must use an unrotated camera at unit zoom");
    require (original.waveformWindow.load() == 1 && original.waveformColour.load() == 0
             && original.waveformStyle.load() == 0 && original.waveformZoom.load() == 0,
             "New instances use a 12 second Silk waveform with frequency colours and 1x display scale");
    original.visualMode.store (5);
    original.palette.store (6);
    original.customColourA.store (0x000000u);
    original.customColourB.store (0xffffffu);
    original.customColourC.store (0x12ab34u);
    original.themeMode.store (2);
    original.layoutMode.store (2);
    original.waveformWindow.store (5);
    original.waveformColour.store (1);
    original.waveformStyle.store (1);
    original.waveformZoom.store (2);
    original.visualGain.store (1.75f);
    original.frozen.store (true);
    original.cameraYaw.store (1.25f);
    original.cameraPitch.store (-0.625f);
    original.cameraZoom.store (1.875f);
    juce::MemoryBlock stored;
    original.getStateInformation (stored);
    require (stored.getSize() > 0, "State must serialize");

    AuroraAudioProcessor restored;
    apply (restored, stored);
    const auto correct = [&] {
        return restored.visualMode.load() == 5 && restored.palette.load() == 6
            && restored.customColourA.load() == 0x000000u
            && restored.customColourB.load() == 0xffffffu
            && restored.customColourC.load() == 0x12ab34u
            && restored.themeMode.load() == 2
            && restored.layoutMode.load() == 2 && restored.visualGain.load() == 1.75f
            && restored.waveformWindow.load() == 5 && restored.waveformColour.load() == 1
            && restored.waveformStyle.load() == 1 && restored.waveformZoom.load() == 2
            && restored.cameraYaw.load() == 1.25f && restored.cameraPitch.load() == -0.625f
            && restored.cameraZoom.load() == 1.875f
            && restored.frozen.load();
    };
    require (correct(), "Every visual setting must survive host state round trip");
    restored.setStateInformation (nullptr, 0);
    const char invalid[] = "malformed host preset";
    restored.setStateInformation (invalid, static_cast<int> (sizeof invalid));
    restored.setStateInformation (stored.getData(), 3);
    require (correct(), "Malformed and truncated state must leave settings unchanged");

    juce::ValueTree wrongTag ("OtherPluginState");
    apply (restored, encode (wrongTag));
    juce::ValueTree future ("AuroraMetersState");
    future.setProperty ("version", 999, nullptr);
    apply (restored, encode (future));
    require (correct(), "Unrecognized state type/version must leave settings unchanged");

    juce::ValueTree legacy ("AuroraMetersState");
    legacy.setProperty ("version", 1, nullptr);
    legacy.setProperty ("visualMode", 0, nullptr);
    legacy.setProperty ("palette", 1, nullptr);
    legacy.setProperty ("layoutMode", 1, nullptr);
    legacy.setProperty ("visualGain", 1.75, nullptr);
    legacy.setProperty ("frozen", true, nullptr);
    apply (restored, encode (legacy));
    require (restored.visualMode.load() == 0 && restored.palette.load() == 1
             && restored.themeMode.load() == 0 && restored.layoutMode.load() == 1
             && restored.visualGain.load() == 1.75f && restored.frozen.load(),
             "Earlier version 1 presets must preserve explicit mode zero and use default theme zero");
    require (restored.customColourA.load() == 0x214e68u
             && restored.customColourB.load() == 0x82bac7u
             && restored.customColourC.load() == 0xe4f2e9u,
             "Earlier presets without custom colours must use the RGB defaults");
    require (restored.cameraYaw.load() == 0.0f && restored.cameraPitch.load() == 0.0f
             && restored.cameraZoom.load() == 1.0f,
             "Earlier presets without camera state must restore the default view");

    require (restored.waveformWindow.load() == 1 && restored.waveformColour.load() == 0
             && restored.waveformStyle.load() == 0 && restored.waveformZoom.load() == 0,
             "Earlier presets without waveform settings restore the 12 second band view at Silk and 1x");
    legacy.setProperty ("waveformWindow", 3, nullptr);
    legacy.setProperty ("waveformColour", 1, nullptr);
    apply (restored, encode (legacy));
    require (restored.waveformWindow.load() == 3 && restored.waveformColour.load() == 1
             && restored.waveformStyle.load() == 0 && restored.waveformZoom.load() == 0,
             "Existing Scope ID 3 and Palette colour survive when newer display properties are absent");

    juce::ValueTree minimal ("AuroraMetersState");
    minimal.setProperty ("version", 1, nullptr);
    apply (restored, encode (minimal));
    require (restored.visualMode.load() == 2 && restored.palette.load() == 0
             && restored.themeMode.load() == 0,
             "A preset missing selectors must retain Pearl Cloud, Glacier and the original Soft Club theme");

    juce::ValueTree bounds ("AuroraMetersState");
    bounds.setProperty ("version", 1, nullptr);
    bounds.setProperty ("visualMode", 99, nullptr);
    bounds.setProperty ("palette", -9, nullptr);
    bounds.setProperty ("themeMode", 99, nullptr);
    bounds.setProperty ("layoutMode", 99, nullptr);
    bounds.setProperty ("waveformWindow", 99, nullptr);
    bounds.setProperty ("waveformColour", 99, nullptr);
    bounds.setProperty ("waveformStyle", 99, nullptr);
    bounds.setProperty ("waveformZoom", 99, nullptr);
    bounds.setProperty ("visualGain", 200.0, nullptr);
    bounds.setProperty ("customColourA", static_cast<juce::int64> (0xab123456), nullptr);
    bounds.setProperty ("customColourB", static_cast<juce::int64> (-1), nullptr);
    bounds.setProperty ("customColourC", static_cast<juce::int64> (0x1000000), nullptr);
    bounds.setProperty ("cameraYaw", 200.0, nullptr);
    bounds.setProperty ("cameraPitch", -200.0, nullptr);
    bounds.setProperty ("cameraZoom", 200.0, nullptr);
    apply (restored, encode (bounds));
    require (restored.visualMode.load() == 5 && restored.palette.load() == 0
             && restored.themeMode.load() == auroraThemeCount - 1
             && restored.layoutMode.load() == 3 && restored.visualGain.load() == 4.0f
             && restored.waveformWindow.load() == 5 && restored.waveformColour.load() == 1
             && restored.waveformStyle.load() == 2 && restored.waveformZoom.load() == 2
             && ! restored.frozen.load(),
             "Out-of-range state must clamp safely and missing settings use defaults");
    require (restored.customColourA.load() == 0x123456u
             && restored.customColourB.load() == 0xffffffu
             && restored.customColourC.load() == 0x000000u,
             "Restored custom colours must retain only their low 24 RGB bits");
    require (restored.cameraYaw.load() == juce::MathConstants<float>::pi
             && restored.cameraPitch.load() == -juce::MathConstants<float>::pi
             && restored.cameraZoom.load() == 2.4f,
             "Restored camera angles and zoom must clamp at their upper and lower bounds");
    bounds.setProperty ("visualMode", -9, nullptr);
    bounds.setProperty ("palette", 99, nullptr);
    bounds.setProperty ("themeMode", -9, nullptr);
    bounds.setProperty ("layoutMode", -9, nullptr);
    bounds.setProperty ("waveformWindow", -9, nullptr);
    bounds.setProperty ("waveformColour", -9, nullptr);
    bounds.setProperty ("waveformStyle", -9, nullptr);
    bounds.setProperty ("waveformZoom", -9, nullptr);
    bounds.setProperty ("visualGain", -200.0, nullptr);
    bounds.setProperty ("cameraYaw", -200.0, nullptr);
    bounds.setProperty ("cameraPitch", 200.0, nullptr);
    bounds.setProperty ("cameraZoom", -200.0, nullptr);
    apply (restored, encode (bounds));
    require (restored.visualMode.load() == 0 && restored.palette.load() == auroraPaletteCount - 1
             && restored.themeMode.load() == 0 && restored.layoutMode.load() == 0
             && restored.visualGain.load() == 0.25f
             && restored.waveformWindow.load() == 0 && restored.waveformColour.load() == 0
             && restored.waveformStyle.load() == 0 && restored.waveformZoom.load() == 0,
             "Every restored selector and gain must respect both bounds");
    require (restored.cameraYaw.load() == -juce::MathConstants<float>::pi
             && restored.cameraPitch.load() == juce::MathConstants<float>::pi
             && restored.cameraZoom.load() == .55f,
             "Camera yaw, pitch and zoom must respect their opposite bounds");

    juce::ValueTree nonfiniteCamera ("AuroraMetersState");
    nonfiniteCamera.setProperty ("version", 1, nullptr);
    nonfiniteCamera.setProperty ("cameraYaw", "nan", nullptr);
    nonfiniteCamera.setProperty ("cameraPitch", "inf", nullptr);
    nonfiniteCamera.setProperty ("cameraZoom", "nan", nullptr);
    apply (restored, encode (nonfiniteCamera));
    require (restored.cameraYaw.load() == 0.0f && restored.cameraPitch.load() == 0.0f
             && restored.cameraZoom.load() == 1.0f,
             "Non-finite camera state must fall back to a finite default view");

    original.customColourA.store (0xff012345u);
    original.customColourB.store (0x80000000u);
    original.customColourC.store (0xffffffffu);
    original.getStateInformation (stored);
    apply (restored, stored);
    require (restored.customColourA.load() == 0x012345u
             && restored.customColourB.load() == 0x000000u
             && restored.customColourC.load() == 0xffffffu,
             "Serialized custom colours must be stored as 24-bit RGB values");

    // Existing numeric palette IDs must keep their exact RGB values, including
    // Custom at 6. This is separate from selector round trips: an index surviving
    // serialization alone would not detect an accidental colour-table reorder.
    static constexpr std::array<std::array<std::uint32_t, 3>, 6> originalPalettes {{
        {{0x193f59, 0x70bbc4, 0xf0f7eb}}, {{0x124d56, 0x50b898, 0xe2edb8}},
        {{0x503b82, 0xb77bab, 0xf5d8d3}}, {{0x723752, 0xdba579, 0xfff0c5}},
        {{0x3e5845, 0x93b484, 0xe4ebc1}}, {{0x626d94, 0xc9dada, 0xf9f5e8}}
    }};
    for (int index = 0; index < static_cast<int> (originalPalettes.size()); ++index)
        require (auroraPalette (index, 0, 0, 0) == originalPalettes[static_cast<std::size_t> (index)],
                 "The original six palette IDs must retain their exact colours");
    require (auroraCustomPalette == 6
             && auroraPalette (6, 0x123456, 0x654321, 0xfedcba)
                == std::array<std::uint32_t, 3> {{0x123456, 0x654321, 0xfedcba}},
             "The existing Custom ID must continue to use all three arbitrary RGB colours");
    for (int index = 7; index < auroraPaletteCount; ++index)
    {
        const auto colours = auroraPalette (index, 0, 0, 0);
        require (colours[0] != colours[1] && colours[1] != colours[2] && colours[0] != colours[2],
                 "Every added palette must resolve to its three distinct colour stops");
        require ((colours[0] | colours[1] | colours[2]) <= 0xffffffu,
                 "Every palette stop must fit the renderer's 24-bit RGB representation");
    }

    // All visual families, palettes, and themes must remain independent when
    // restoring host state, including indices unavailable in older presets.
    for (int mode = 0; mode < 6; ++mode)
        for (int palette = 0; palette < auroraPaletteCount; ++palette)
            for (int theme = 0; theme < auroraThemeCount; ++theme)
            {
                original.visualMode.store (mode);
                original.palette.store (palette);
                original.themeMode.store (theme);
                original.getStateInformation (stored);
                apply (restored, stored);
                require (restored.visualMode.load() == mode && restored.palette.load() == palette
                         && restored.themeMode.load() == theme
                         && restored.customColourA.load() == 0x012345u
                         && restored.customColourB.load() == 0x000000u
                         && restored.customColourC.load() == 0xffffffu,
                         "Every mode, thirteen palettes, four themes and custom RGB values must round-trip independently");
            }

    // Adding the short history windows must not remap the original 0..3 IDs.
    // Display styling/zoom remain independent of layout, colour and scene gain.
    original.visualGain.store (1.75f);
    for (int layout = 0; layout < 4; ++layout)
        for (int timeWindow = 0; timeWindow < 6; ++timeWindow)
            for (int colour = 0; colour < 2; ++colour)
                for (int style = 0; style < 3; ++style)
                    for (int zoom = 0; zoom < 3; ++zoom)
                    {
                        original.layoutMode.store (layout);
                        original.waveformWindow.store (timeWindow);
                        original.waveformColour.store (colour);
                        original.waveformStyle.store (style);
                        original.waveformZoom.store (zoom);
                        original.getStateInformation (stored);
                        apply (restored, stored);
                        require (restored.layoutMode.load() == layout
                                 && restored.waveformWindow.load() == timeWindow
                                 && restored.waveformColour.load() == colour
                                 && restored.waveformStyle.load() == style
                                 && restored.waveformZoom.load() == zoom
                                 && restored.visualGain.load() == 1.75f,
                                 "All waveform windows, styles, display scales and colours must round-trip independently");
                    }

}

void testCustomizationRegistry()
{
    Customization settings;
    std::set<std::string> ids;
    require (!settings.set (nullptr, 2) && !settings.set ("futureUnknown", 2)
             && settings.get (nullptr) == 0 && settings.get ("futureUnknown") == 0
             && Customization::find ("futureUnknown") == nullptr,
             "Unknown and null customization keys must be harmless");
    for (const auto& descriptor : Customization::descriptors)
    {
        settings.resetAll();
        require (ids.insert (descriptor.id).second, "Customization keys must be unique");
        require (Customization::find (descriptor.id) == &descriptor
                 && descriptor.label[0] != 0 && descriptor.min < descriptor.max
                 && descriptor.step > 0 && descriptor.defaultValue >= descriptor.min
                 && descriptor.defaultValue <= descriptor.max,
                 "Every customization must have a valid descriptor and default");
        require (settings.get (descriptor.id) == descriptor.defaultValue,
                 "New registry values must exactly equal documented defaults");
        if (descriptor.kind == Customization::Kind::Choice)
        {
            const auto choices = juce::StringArray::fromTokens (descriptor.choices, "|", "");
            require (choices.size() == static_cast<int> (descriptor.max) + 1,
                     "Choice labels must cover every stable numeric option");
        }
        settings.set (descriptor.id, -std::numeric_limits<float>::max());
        require (settings.get (descriptor.id) == descriptor.min, "Registry must clamp at its lower bound");
        settings.set (descriptor.id, std::numeric_limits<float>::max());
        require (std::abs (settings.get (descriptor.id) - descriptor.max) <= descriptor.step * .0001f,
                 "Registry must clamp at its upper bound");
        for (const auto invalid : { std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity() })
        {
            settings.set (descriptor.id, invalid);
            require (settings.get (descriptor.id) == descriptor.defaultValue,
                     "Nonfinite customization input must restore a finite default");
        }
        settings.set (descriptor.id, descriptor.min + .24f * descriptor.step);
        require (settings.get (descriptor.id) == descriptor.min,
                 "Customization values must snap to the declared control interval");
        if (descriptor.kind == Customization::Kind::Colour)
            for (const auto rgb : { 0u, 1u, 0x123456u, 0xabcdefu, 0xfffffeu, 0xffffffu })
            {
                settings.set (descriptor.id, static_cast<float> (rgb));
                require (static_cast<std::uint32_t> (settings.get (descriptor.id)) == rgb,
                         "All 24-bit RGB values must be represented exactly");
            }
    }
    for (const auto& descriptor : Customization::descriptors)
        settings.set (descriptor.id, descriptor.min);
    std::array<float, Customization::count> beforeReset {};
    for (std::size_t index = 0; index < Customization::count; ++index)
        beforeReset[index] = settings.get (Customization::descriptors[index].id);
    settings.resetSection (Customization::Section::Waveform);
    std::size_t resetIndex = 0;
    for (const auto& descriptor : Customization::descriptors) {
        require (settings.get (descriptor.id) == (descriptor.section == Customization::Section::Waveform
                                                     ? descriptor.defaultValue : beforeReset[resetIndex]),
                 "Section reset must preserve settings in every other section");
        ++resetIndex;
    }
    settings.resetAll();
    for (const auto& descriptor : Customization::descriptors)
        require (settings.get (descriptor.id) == descriptor.defaultValue,
                 "Reset all must restore every registry default");
}

void varyCustomization (Customization& settings, int variation)
{
    std::size_t index = static_cast<std::size_t> (variation);
    for (const auto& descriptor : Customization::descriptors)
    {
        const auto value = descriptor.min + (descriptor.max - descriptor.min)
                                           * static_cast<float> ((index * 7u) % 11u) / 10.0f;
        settings.set (descriptor.id, value);
        ++index;
    }
}

void testCustomizationState()
{
    AuroraAudioProcessor original, restored;
    for (int variation = 0; variation < 11; ++variation)
    {
        varyCustomization (original.custom, variation);
        juce::MemoryBlock state;
        original.getStateInformation (state);
        apply (restored, state);
        for (const auto& descriptor : Customization::descriptors)
            require (restored.custom.get (descriptor.id) == original.custom.get (descriptor.id),
                     "Every customization must round-trip independently through host state");
    }
    juce::ValueTree legacy ("AuroraMetersState");
    legacy.setProperty ("version", 1, nullptr);
    legacy.setProperty ("custom_futureColour", "irrelevant", nullptr);
    apply (restored, encode (legacy));
    for (const auto& descriptor : Customization::descriptors)
        require (restored.custom.get (descriptor.id) == descriptor.defaultValue,
                 "Legacy sessions must default missing customization and ignore future keys");
    for (const auto& descriptor : Customization::descriptors)
    {
        const juce::Identifier key (juce::String ("custom_") + descriptor.id);
        legacy.setProperty (key, descriptor.max + 100000000.0, nullptr);
    }
    apply (restored, encode (legacy));
    for (const auto& descriptor : Customization::descriptors)
        if (std::strncmp (descriptor.id, "order", 5) != 0)
        require (std::abs (restored.custom.get (descriptor.id) - descriptor.max) <= descriptor.step * .0001f,
                 "Host state must clamp all customization values before rendering");
    for (const auto& descriptor : Customization::descriptors)
        legacy.setProperty (juce::Identifier (juce::String ("custom_") + descriptor.id), "nan", nullptr);
    apply (restored, encode (legacy));
    for (const auto& descriptor : Customization::descriptors)
        require (restored.custom.get (descriptor.id) == descriptor.defaultValue,
                 "Nonfinite host customization values must restore safe defaults");
}

void testPanelOrder()
{
    AuroraAudioProcessor source, target;
    const std::array keys { "orderScene", "orderSpectrum", "orderSpectrogram", "orderWaveform", "orderStereo", "orderLevels" };
    std::array<int, 6> desired {{ 1, 2, 3, 4, 5, 6 }};
    do {
        for (std::size_t panel = 0; panel < keys.size(); ++panel) {
            source.custom.set (keys[panel], static_cast<float> (desired[panel]));
            std::set<int> ranks;
            for (const auto* key : keys)
                ranks.insert (static_cast<int> (source.custom.get (key)));
            require (ranks == std::set<int> { 1, 2, 3, 4, 5, 6 },
                     "Every panel order edit must preserve a unique six-position permutation");
        }
        for (std::size_t panel = 0; panel < keys.size(); ++panel)
            require (source.custom.get (keys[panel]) == desired[panel],
                     "Sequential rank swaps must restore every possible panel order");
        juce::MemoryBlock state;
        source.getStateInformation (state);
        apply (target, state);
        for (std::size_t panel = 0; panel < keys.size(); ++panel)
            require (target.custom.get (keys[panel]) == desired[panel],
                     "Every panel permutation must survive host state round-trip");
        require (target.applyAppearancePreset (source.createAppearancePreset()),
                 "Every custom panel permutation must be a valid appearance preset");
        for (std::size_t panel = 0; panel < keys.size(); ++panel)
            require (target.custom.get (keys[panel]) == desired[panel],
                     "Every panel permutation must survive appearance preset round-trip");
    } while (std::next_permutation (desired.begin(), desired.end()));
}

void testAppearancePresets()
{
    AuroraAudioProcessor source, target, defaults;
    source.visualMode.store (5);
    source.palette.store (auroraCustomPalette);
    source.themeMode.store (2);
    source.layoutMode.store (3);
    source.customColourA.store (0x123456u);
    source.customColourB.store (0xabcdefu);
    source.customColourC.store (0xfedcbau);
    source.waveformWindow.store (5);
    source.waveformColour.store (1);
    source.waveformStyle.store (2);
    source.waveformZoom.store (2);
    source.visualGain.store (2.125f);
    source.cameraYaw.store (.875f);
    source.cameraPitch.store (-1.125f);
    source.cameraZoom.store (1.875f);
    source.frozen.store (true);
    varyCustomization (source.custom, 3);
    const auto preset = source.createAppearancePreset();
    const auto xml = juce::XmlDocument::parse (preset);
    require (xml != nullptr && xml->hasTagName ("AuroraAppearancePreset")
             && xml->getIntAttribute ("version") == 1 && !xml->hasAttribute ("frozen"),
             "Appearance export must use its own versioned schema and omit transport freeze");
    require (target.applyAppearancePreset (preset) && target.createAppearancePreset() == preset,
             "All old and new appearance controls must round-trip as a standalone preset");
    require (!target.frozen.load(), "An appearance preset must not restore its author's freeze state");

    // Use measured history without an asynchronous worker, so any unintended
    // meter reset or buffer mutation can be distinguished from normal new audio.
    std::vector<float> pcm (4097);
    for (std::size_t sample = 0; sample < pcm.size(); ++sample)
        pcm[sample] = .37f * static_cast<float> (std::sin (sample * .13));
    target.analyzer.process (pcm.data(), pcm.data(), static_cast<int> (pcm.size()), 48000.0, 2);
    const auto measurements = target.analyzer.snapshot();
    const auto waveform = target.analyzer.waveformSnapshot();
    target.droppedSamples.store (987);
    target.isReceiving.store (true);
    const auto analysisUnchanged = [&] {
        const auto after = target.analyzer.snapshot();
        const auto history = target.analyzer.waveformSnapshot();
        return after.sequence == measurements.sequence && after.peakL == measurements.peakL
            && after.rmsL == measurements.rmsL && after.integratedLUFS == measurements.integratedLUFS
            && history.generation == waveform.generation && history.totalSamples == waveform.totalSamples
            && history.columns.size() == waveform.columns.size()
            && history.tail.has_value() == waveform.tail.has_value()
            && target.droppedSamples.load() == 987 && target.isReceiving.load();
    };
    for (bool frozen : { false, true })
    {
        target.frozen.store (frozen);
        target.resetAppearance();
        require (target.createAppearancePreset() == defaults.createAppearancePreset(),
                 "Reset appearance must restore new-instance defaults including all custom controls");
        require (target.frozen.load() == frozen && analysisUnchanged(),
                 "Reset appearance must preserve freeze and measured audio history");
        require (target.applyAppearancePreset (preset) && target.frozen.load() == frozen && analysisUnchanged(),
                 "Applying a preset must preserve freeze, meters, history and worker diagnostics");
    }
    auto withUnknown = juce::XmlDocument::parse (preset);
    withUnknown->setAttribute ("custom_futureControl", "unknown future format");
    withUnknown->setAttribute ("frozen", "0");
    require (target.applyAppearancePreset (withUnknown->toString()) && target.frozen.load()
             && target.createAppearancePreset() == preset,
             "Unknown preset attributes must be ignored, including freeze from another application");
    require (target.applyAppearancePreset (juce::String ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") + preset),
             "Presets may include a standard XML declaration");

    const auto beforeInvalid = target.createAppearancePreset();
    const auto reject = [&] (const juce::String& malformed) {
        require (!target.applyAppearancePreset (malformed), "Invalid appearance preset must be rejected");
        require (target.createAppearancePreset() == beforeInvalid && target.frozen.load() && analysisUnchanged(),
                 "Failed preset validation must not partially change any appearance or measurement state");
    };
    for (const auto* malformed : {
        "", "plain text", "<", "<Other version=\"1\"/>",
        "<AuroraAppearancePreset/>", "<AuroraAppearancePreset version=\"2\"/>",
        "<AuroraAppearancePreset version=\"1\"><extra/></AuroraAppearancePreset>",
        "<AuroraAppearancePreset version=\"1\">text</AuroraAppearancePreset>",
        "<AuroraAppearancePreset version=\"1\"/> trailing text",
        "<AuroraAppearancePreset version=\"1\"/><Other/>",
        "<!DOCTYPE AuroraAppearancePreset><AuroraAppearancePreset version=\"1\"/>",
        "<?xml malformed",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\"nan\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\"inf\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\"1e999\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\"1wat\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\"1e\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3\" custom_waveDensity=\".\"/>",
        "<AuroraAppearancePreset version=\"1\" palette=\"3.5\"/>",
        "<AuroraAppearancePreset version=\"1\" cameraZoom=\"nan\"/>",
        "<AuroraAppearancePreset version=\"1\" custom_waveChannels=\"1.5\"/>",
        "<AuroraAppearancePreset version=\"1\" custom_waveLowColour=\"123.5\"/>",
        "<AuroraAppearancePreset version=\"1\" custom_waveGrid=\"0.5\"/>"
    }) reject (malformed);
    reject (preset + juce::String::repeatedString (" ", 256 * 1024));

    require (target.applyAppearancePreset (
        "<AuroraAppearancePreset version=\"1\" visualGain=\"1e200\" layoutMode=\"999\" "
        "custom_waveDensity=\"1e200\" custom_waveLowColour=\"99999999999999\" "
        "custom_waveGain=\"-1e200\" custom_spectrumTrail=\".234\"/>"),
        "Finite numeric presets must clamp before conversion even at extreme magnitudes");
    require (target.visualGain.load() == 4 && target.layoutMode.load() == 3
             && target.custom.get ("waveDensity") == 2 && target.custom.get ("waveLowColour") == 16777215
             && target.custom.get ("waveGain") == .25f
             && std::abs (target.custom.get ("spectrumTrail") - .23f) < .000001f,
             "Imported presets must enforce bounds and control steps consistently");
    require (target.custom.get ("sceneExposure") == 1 && target.custom.get ("waveMidColour") == 0x62ebd0,
             "Partial presets must fill missing new customization with the documented defaults");
    require (target.frozen.load() && analysisUnchanged(), "Clamped imports must also preserve measured state");
}

bool sameWaveformColumn (const WaveformColumn& a, const WaveformColumn& b)
{
    // Compare the actual sample statistics, not only cursors or object padding.
    return std::tie (a.sequence, a.endSample, a.sampleCount, a.minimum, a.maximum, a.rms,
                     a.minimumSample, a.maximumSample, a.sumSquares, a.bandEnergy,
                     a.amplitudeCounts, a.bandPower)
        == std::tie (b.sequence, b.endSample, b.sampleCount, b.minimum, b.maximum, b.rms,
                     b.minimumSample, b.maximumSample, b.sumSquares, b.bandEnergy,
                     b.amplitudeCounts, b.bandPower);
}

double colourContrast (std::uint32_t a, std::uint32_t b)
{
    const auto luminance = [] (std::uint32_t rgb) {
        const auto linear = [] (std::uint32_t byte) {
            const double value = double (byte) / 255.0;
            return value <= .04045 ? value / 12.92 : std::pow ((value + .055) / 1.055, 2.4);
        };
        return .2126 * linear ((rgb >> 16) & 255u) + .7152 * linear ((rgb >> 8) & 255u)
             + .0722 * linear (rgb & 255u);
    };
    const auto x = luminance (a), y = luminance (b);
    return (std::max (x, y) + .05) / (std::min (x, y) + .05);
}

void testFactoryPresets()
{
    AuroraAudioProcessor target, clean, roundTrip;
    const auto defaultXml = juce::XmlDocument::parse (clean.createAppearancePreset());
    require (factoryPresetCount() >= 20, "Factory collection must offer at least twenty complete looks");
    for (const auto invalid : { -1, factoryPresetCount(), std::numeric_limits<int>::max() })
        require (juce::String (factoryPresetName (invalid)).isEmpty()
                 && factoryPresetXml (invalid).isEmpty() && factoryPresetLayout (invalid) == -1
                 && factoryPresetColours (invalid) == std::array<std::uint32_t, 3> {},
                 "Invalid factory indices must be harmless and must not alias a real preset");

    std::set<std::string> names, appearances;
    std::set<std::array<std::uint32_t, 3>> palettes;
    std::set<int> layouts, scenes;
    std::vector<float> left (8197), right (8197);
    for (std::size_t sample = 0; sample < left.size(); ++sample) {
        left[sample] = .43f * static_cast<float> (std::sin (sample * .081));
        right[sample] = .27f * static_cast<float> (std::sin (sample * .13 + .7));
    }
    target.analyzer.process (left.data(), right.data(), static_cast<int> (left.size()), 48000.0, 2);
    const auto measured = target.analyzer.snapshot();
    const auto pcmHistory = target.analyzer.waveformSnapshot();
    target.droppedSamples.store (731);
    target.isReceiving.store (true);
    const auto unchangedAnalysis = [&] {
        const auto after = target.analyzer.snapshot();
        const auto history = target.analyzer.waveformSnapshot();
        const bool frameEqual = std::tie (after.left, after.right, after.spectrum, after.sampleRate,
            after.channelCount, after.peakL, after.peakR, after.peakHoldL, after.peakHoldR,
            after.rmsL, after.rmsR, after.correlation, after.momentaryLUFS, after.shortTermLUFS,
            after.integratedLUFS, after.low, after.mid, after.high, after.sequence, after.resetGeneration)
            == std::tie (measured.left, measured.right, measured.spectrum, measured.sampleRate,
            measured.channelCount, measured.peakL, measured.peakR, measured.peakHoldL, measured.peakHoldR,
            measured.rmsL, measured.rmsR, measured.correlation, measured.momentaryLUFS, measured.shortTermLUFS,
            measured.integratedLUFS, measured.low, measured.mid, measured.high, measured.sequence, measured.resetGeneration);
        const bool metadataEqual = std::tie (history.generation, history.totalSamples, history.sampleRate,
            history.bucketSamples, history.channelCount)
            == std::tie (pcmHistory.generation, pcmHistory.totalSamples, pcmHistory.sampleRate,
            pcmHistory.bucketSamples, pcmHistory.channelCount);
        return frameEqual && metadataEqual && history.columns.size() == pcmHistory.columns.size()
            && std::equal (history.columns.begin(), history.columns.end(), pcmHistory.columns.begin(), sameWaveformColumn)
            && history.tail.has_value() == pcmHistory.tail.has_value()
            && (!history.tail || sameWaveformColumn (*history.tail, *pcmHistory.tail))
            && target.droppedSamples.load() == 731 && target.isReceiving.load();
    };
    for (int index = 0; index < factoryPresetCount(); ++index) {
        const juce::String name (factoryPresetName (index));
        const auto factory = factoryPresetXml (index);
        const auto xml = juce::XmlDocument::parse (factory);
        require (name.trim().isNotEmpty() && name.length() <= 32 && names.insert (name.toStdString()).second,
                 "Factory look names must be concise, nonempty and unique");
        require (xml != nullptr && xml->hasTagName ("AuroraAppearancePreset")
                 && xml->getIntAttribute ("version") == 1 && !xml->hasAttribute ("frozen"),
                 "Every factory look must be a valid appearance-only preset");
        for (int attribute = 0; attribute < defaultXml->getNumAttributes(); ++attribute)
            require (xml->hasAttribute (defaultXml->getAttributeName (attribute)),
                     "Every factory look must explicitly contain every appearance field");
        for (const auto& descriptor : Customization::descriptors) {
            const auto value = xml->getDoubleAttribute (juce::String ("custom_") + descriptor.id);
            require (std::isfinite (value) && value >= descriptor.min - .00001
                     && value <= descriptor.max + .00001,
                     "Factory customization values must be finite and in their declared ranges");
        }
        require (clean.applyAppearancePreset (factory), "Every bundled factory preset must apply successfully");
        const auto canonical = clean.createAppearancePreset();
        require (findFactoryPreset (factory) == index && findFactoryPreset (canonical) == index,
                 "Factory identity must survive the processor's canonical numeric serialization");
        auto modified = juce::XmlDocument::parse (canonical);
        modified->setAttribute ("custom_waveGain", modified->getDoubleAttribute ("custom_waveGain") + .123);
        require (findFactoryPreset (modified->toString()) == -1,
                 "Editing an appearance must clear its exact factory identity");
        modified = juce::XmlDocument::parse (canonical);
        modified->removeAttribute ("custom_waveGain");
        require (findFactoryPreset (modified->toString()) == -1,
                 "An incomplete appearance must not be mistaken for a complete factory look");
        require (appearances.insert (canonical.toStdString()).second,
                 "Factory look appearances must be distinct, not aliases with different names");
        require (factoryPresetLayout (index) == clean.layoutMode.load(),
                 "Factory gallery layout metadata must describe the actual saved layout");
        layouts.insert (clean.layoutMode.load()); scenes.insert (clean.visualMode.load());
        auto actualPalette = auroraPalette (clean.palette.load(), clean.customColourA.load(),
                                            clean.customColourB.load(), clean.customColourC.load());
        if (clean.custom.get ("paletteReverse") > .5f) std::swap (actualPalette[0], actualPalette[2]);
        require (actualPalette == factoryPresetColours (index),
                 "Factory gallery swatches must match the palette actually loaded");
        palettes.insert (actualPalette);
        if (clean.custom.get ("interfaceOverride") > .5f) {
            const auto text = static_cast<std::uint32_t> (clean.custom.get ("textColour"));
            require (colourContrast (text, static_cast<std::uint32_t> (clean.custom.get ("panelColour"))) >= 4.5
                     && colourContrast (text, static_cast<std::uint32_t> (clean.custom.get ("backgroundColour"))) >= 4.5,
                     "Factory foreground text must remain readable against both interface surfaces");
        }
        require (roundTrip.applyAppearancePreset (canonical)
                 && roundTrip.createAppearancePreset() == canonical,
                 "Every factory look must survive exporting and importing as a user preset");
        juce::MemoryBlock state;
        clean.getStateInformation (state); apply (roundTrip, state);
        require (roundTrip.createAppearancePreset() == canonical,
                 "Every factory look must survive saving and reopening a host session");
        for (const bool frozen : { false, true }) {
            varyCustomization (target.custom, index);
            target.visualMode.store ((index + 1) % 6); target.layoutMode.store ((index + 2) % 4);
            target.palette.store ((index + 3) % auroraPaletteCount); target.themeMode.store ((index + 1) % auroraThemeCount);
            target.customColourA.store (0x123456); target.customColourB.store (0xabcdef); target.customColourC.store (0xfedcba);
            target.cameraYaw.store (2); target.cameraPitch.store (-2); target.cameraZoom.store (2.4f);
            target.visualGain.store (3.25f); target.waveformWindow.store (3); target.waveformColour.store (1);
            target.waveformStyle.store (2); target.waveformZoom.store (2); target.frozen.store (frozen);
            require (target.applyAppearancePreset (factory) && target.createAppearancePreset() == canonical,
                     "Factory selection must replace a previously modified look completely and deterministically");
            require (target.frozen.load() == frozen && unchangedAnalysis(),
                     "Factory selection must preserve every measured value, PCM statistic, freeze and worker diagnostic");
        }
    }
    require (palettes.size() >= 16 && layouts.size() >= 3 && scenes.size() >= 4,
             "Factory collection must span distinct palettes, working layouts and visual families");
    for (const auto* invalid : { "", "<", "<Other/>", "<AuroraAppearancePreset version=\"999\"/>" })
        require (findFactoryPreset (invalid) == -1, "Malformed or unsupported appearance XML must have no factory identity");
    target.resetAppearance(); clean.resetAppearance();
    require (target.createAppearancePreset() == clean.createAppearancePreset() && target.frozen.load() && unchangedAnalysis(),
             "Reset after browsing factory looks must restore defaults without losing analysis or freeze");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    try
    {
        testPassThroughAndLifecycle();
        testAnalysisResetAndOverflow();
        testState();
        testCustomizationRegistry();
        testCustomizationState();
        testAppearancePresets();
        testPanelOrder();
        testFactoryPresets();
        std::cout << "Processor tests passed: " << checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Processor test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
