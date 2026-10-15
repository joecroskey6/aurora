#include "FactoryPresetBrowser.h"
#include "FactoryPresets.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
using namespace juce;
Colour rgb(std::uint32_t value) { return Colour(value | 0xff000000u); }

// These are small, static appearance illustrations. No thumbnail is presented
// as live audio or derived from fabricated analyzer samples.
class PresetCard final : public Button {
public:
    explicit PresetCard(int index) : Button(factoryPresetName(index)), presetIndex(index) {
        setComponentID("factoryPreset_" + String(index));
        setTitle(factoryPresetName(index));
        setDescription("Apply the " + getName() + " factory appearance preset.");
        setTooltip("Apply " + getName() + ". Preview shows its palette and module layout.");
        setWantsKeyboardFocus(true);
        palette = factoryPresetColours(index);
        settings = XmlDocument::parse(factoryPresetXml(index));
        backdrop = colour("backgroundColour", 0x171b1a);
        face = colour("panelColour", 0x0f1715);
        ink = colour("textColour", 0xe2dfcf);
        for (std::size_t i = 0; i < modulePalettes.size(); ++i) {
            const String prefix(modulePrefixes[i]);
            modulePalettes[i] = palette;
            if (value(prefix + "OwnPalette", 0) >= .5f)
                for (std::size_t stop = 0; stop < 3; ++stop)
                    modulePalettes[i][stop] = static_cast<std::uint32_t>(value(prefix + "Colour" + String::charToString(char('A' + stop)), float(palette[stop])));
            if (value("paletteReverse", 0) >= .5f)
                std::swap(modulePalettes[i][0], modulePalettes[i][2]);
        }
    }

    void paintButton(Graphics& g, bool hovered, bool down) override {
        const auto bounds = getLocalBounds().toFloat().reduced(1.5f);
        const bool selected = getToggleState();
        const auto accent = rgb(palette[1]);
        g.setColour(rgb(0x1a2824).interpolatedWith(accent, down ? .17f : hovered ? .08f : .015f));
        g.fillRoundedRectangle(bounds, 8);
        auto preview = bounds.reduced(7.f);
        preview.removeFromBottom(34.f);
        {
            Graphics::ScopedSaveState clip(g);
            Path shape; shape.addRoundedRectangle(preview, 4);
            g.reduceClipRegion(shape);
            g.setColour(backdrop); g.fillRect(preview);
            auto drawing = preview.reduced(5.f);
            auto toolbar = drawing.removeFromTop(6.f);
            g.setColour(ink.withAlpha(.52f));
            g.fillRoundedRectangle(toolbar.removeFromLeft(drawing.getWidth() * .19f).reduced(0, 1), 1);
            for (int i = 0; i < 3; ++i) {
                toolbar.removeFromLeft(3.f);
                g.setColour(rgb(palette[std::size_t(i)]).withAlpha(.9f));
                g.fillRoundedRectangle(toolbar.removeFromLeft(11.f).reduced(0, 1), 1);
            }
            drawing.removeFromTop(4.f);
            const auto modules = moduleAreas(drawing);
            for (std::size_t i = 0; i < modules.size(); ++i)
                if (!modules[i].isEmpty()) drawModule(g, modules[i], int(i));
            ColourGradient sheen(Colours::white.withAlpha(.035f), preview.getX(), preview.getY(),
                                 Colours::transparentWhite, preview.getRight(), preview.getBottom(), false);
            g.setGradientFill(sheen); g.fillRect(preview);
        }
        const auto footer = bounds.withTop(preview.getBottom() + 3.f).reduced(11.f, 0);
        g.setColour(rgb(0xe6ebd8));
        g.setFont(Font(FontOptions(13.5f, selected ? Font::bold : Font::plain)));
        g.drawFittedText(getName(), footer.withTrimmedRight(54.f).toNearestInt(), Justification::centredLeft, 1, .8f);
        for (int i = 0; i < 3; ++i) {
            const float x = footer.getRight() - 42.f + float(i) * 15.f;
            g.setColour(rgb(palette[std::size_t(i)])); g.fillEllipse(x, footer.getCentreY() - 4, 8, 8);
            g.setColour(Colours::white.withAlpha(.18f)); g.drawEllipse(x, footer.getCentreY() - 4, 8, 8, .5f);
        }
        g.setColour(selected ? rgb(0xbce6cb) : hasKeyboardFocus(true) ? rgb(0x92bca8) : rgb(0x40544a).withAlpha(hovered ? .95f : .5f));
        g.drawRoundedRectangle(bounds, 8, selected || hasKeyboardFocus(true) ? 1.5f : .75f);
        if (selected) {
            g.setColour(rgb(0xbce6cb));
            g.fillRoundedRectangle(bounds.getX() + 15, bounds.getY(), bounds.getWidth() - 30, 2, 1);
        }
    }

    void focusGained(FocusChangeType cause) override {
        Button::focusGained(cause);
        if (auto* viewport = findParentComponentOfClass<Viewport>()) {
            const auto visible = viewport->getViewArea();
            const auto card = getBounds();
            if (card.getY() < visible.getY()) viewport->setViewPosition(0, card.getY());
            else if (card.getBottom() > visible.getBottom()) viewport->setViewPosition(0, card.getBottom() - visible.getHeight());
        }
    }

private:
    static constexpr std::array<const char*, 6> modulePrefixes {"scene", "spectrum", "spectrogram", "wave", "stereo", "levels"};
    int presetIndex;
    std::array<std::uint32_t, 3> palette {};
    std::array<std::array<std::uint32_t, 3>, 6> modulePalettes {};
    std::unique_ptr<XmlElement> settings;
    Colour backdrop, face, ink;

    float value(const String& key, float fallback) const {
        return settings == nullptr ? fallback : float(settings->getDoubleAttribute("custom_" + key, fallback));
    }
    Colour colour(const String& key, std::uint32_t fallback) const {
        return rgb(static_cast<std::uint32_t>(value(key, float(fallback))));
    }
    std::array<Rectangle<float>, 6> moduleAreas(Rectangle<float> area) const {
        std::array<Rectangle<float>, 6> result {};
        constexpr float gap = 2.3f;
        const int layout = factoryPresetLayout(presetIndex);
        if (layout == 1) {
            const float width = (area.getWidth() - gap * 4) / 5;
            for (int i = 1; i < 6; ++i)
                result[std::size_t(i)] = {area.getX() + float(i - 1) * (width + gap), area.getY(), width, area.getHeight()};
        } else if (layout == 2) {
            const float right = area.getWidth() * value("layoutLevelWidth", .17f);
            result[3] = area.withTrimmedRight(right + gap);
            result[5] = area.removeFromRight(right);
        } else if (layout == 3) {
            const std::array<const char*, 6> show {"showScene", "showSpectrum", "showSpectrogram", "showWaveform", "showStereo", "showLevels"};
            const std::array<const char*, 6> orderKeys {"orderScene", "orderSpectrum", "orderSpectrogram", "orderWaveform", "orderStereo", "orderLevels"};
            std::array<std::size_t, 6> order {0, 1, 2, 3, 4, 5};
            std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return value(orderKeys[a], float(a + 1)) < value(orderKeys[b], float(b + 1)); });
            int count = 0;
            for (auto key : show) if (value(key, 1) >= .5f) ++count;
            if (count == 0) return result;
            const int columns = std::min(count, jlimit(1, 3, int(value("layoutColumns", 2))));
            const int rows = (count + columns - 1) / columns;
            const float width = (area.getWidth() - float(columns - 1) * gap) / float(columns);
            const float height = (area.getHeight() - float(rows - 1) * gap) / float(rows);
            int cell = 0;
            for (auto i : order) if (value(show[i], 1) >= .5f) {
                result[i] = {area.getX() + float(cell % columns) * (width + gap), area.getY() + float(cell / columns) * (height + gap), width, height};
                ++cell;
            }
        } else {
            const float right = area.getWidth() * value("layoutLevelWidth", .17f);
            const float left = std::min(area.getWidth() * value("layoutSceneWidth", .6f), area.getWidth() - right - area.getWidth() * .15f - gap * 2);
            const float middle = area.getWidth() - left - right - 2 * gap;
            const float top = area.getHeight() * value("layoutTopHeight", .63f), bottom = area.getHeight() - top - gap;
            result[0] = {area.getX(), area.getY(), left, top};
            result[1] = {area.getX() + left + gap, area.getY(), middle, top};
            result[5] = {area.getRight() - right, area.getY(), right, area.getHeight()};
            result[2] = {area.getX(), area.getY() + top + gap, left * .48f, bottom};
            result[3] = {area.getX() + left * .48f + gap, area.getY() + top + gap, left * .52f - gap, bottom};
            result[4] = {area.getX() + left + gap, area.getY() + top + gap, middle, bottom};
        }
        return result;
    }

    void drawModule(Graphics& g, Rectangle<float> area, int module) const {
        Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.toNearestInt());
        g.setColour(face); g.fillRoundedRectangle(area, 1.5f);
        const auto& colours = modulePalettes[std::size_t(module)];
        const auto a = rgb(colours[0]), b = rgb(colours[1]), c = rgb(colours[2]);
        auto r = area.reduced(2.5f);
        if (r.getWidth() < 3 || r.getHeight() < 3) return;
        ColourGradient gradient(a, r.getX(), r.getBottom(), c, r.getRight(), r.getY(), false);
        gradient.addColour(.52, b);
        if (module == 0) {
            g.setGradientFill(ColourGradient(a.withAlpha(.35f), r.getCentreX(), r.getCentreY(), a.withAlpha(0.f), r.getRight(), r.getBottom(), true));
            g.fillRect(r);
            for (int i = 0; i < 5; ++i) {
                const float t = float(i) / 4;
                const auto tint = a.interpolatedWith(b, std::min(1.f, t * 2)).interpolatedWith(c, t * .65f);
                const float width = r.getWidth() * (.82f - t * .35f), height = r.getHeight() * (.76f - t * .35f);
                const float x = r.getX() + r.getWidth() * (.10f + t * .14f), y = r.getY() + r.getHeight() * (.10f + t * .13f);
                g.setGradientFill(ColourGradient(tint.withAlpha(.5f), x + width * .4f, y + height * .3f, tint.withAlpha(0.f), x + width, y + height, true));
                g.fillEllipse(x, y, width, height);
            }
            Path ribbon;
            ribbon.startNewSubPath(r.getX(), r.getY() + r.getHeight() * .67f);
            ribbon.cubicTo(r.getX() + r.getWidth() * .3f, r.getY() - r.getHeight() * .1f,
                           r.getX() + r.getWidth() * .6f, r.getBottom() + r.getHeight() * .1f,
                           r.getRight(), r.getY() + r.getHeight() * .25f);
            g.setGradientFill(gradient); g.strokePath(ribbon, PathStrokeType(std::max(1.2f, r.getHeight() * .04f)));
        } else if (module == 1) {
            Path path;
            for (int i = 0; i <= 38; ++i) {
                const float t = float(i) / 38;
                const float height = .13f + .42f * std::exp(-5.f * t) + .28f * std::pow(std::sin(t * 11.f), 4.f);
                const auto point = Point<float>(r.getX() + t * r.getWidth(), r.getBottom() - height * r.getHeight());
                if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
            }
            g.setGradientFill(gradient); g.strokePath(path, PathStrokeType(1.1f));
            path.lineTo(r.getBottomRight()); path.lineTo(r.getBottomLeft()); path.closeSubPath();
            g.setColour(b.withAlpha(.12f)); g.fillPath(path);
        } else if (module == 2) {
            for (int i = 0; i < 32; ++i) {
                const float t = float(i) / 32;
                const float width = r.getWidth() / 32 + .2f;
                const float alpha = .28f + .6f * std::pow(std::sin(t * 23.f + .8f), 2.f);
                ColourGradient column(a.withAlpha(.8f), 0, r.getY(), c.withAlpha(alpha), 0, r.getBottom(), false);
                column.addColour(.48, b.withAlpha(alpha));
                g.setGradientFill(column); g.fillRect(r.getX() + t * r.getWidth(), r.getY(), width, r.getHeight());
            }
        } else if (module == 3) {
            g.setGradientFill(gradient);
            for (int i = 0; i < 68; ++i) {
                const float t = float(i) / 67;
                const float height = r.getHeight() * (.045f + .16f * std::pow(std::sin(t * 25.f), 2.f) + .21f * std::pow(std::sin(t * 71.f), 8.f));
                const float x = r.getX() + t * r.getWidth();
                g.drawVerticalLine(roundToInt(x), r.getCentreY() - height, r.getCentreY() + height);
            }
        } else if (module == 4) {
            const float radius = std::min(r.getWidth(), r.getHeight()) * .4f;
            const auto centre = r.getCentre();
            g.setColour(ink.withAlpha(.14f));
            g.drawEllipse(centre.x - radius, centre.y - radius, 2 * radius, 2 * radius, .5f);
            for (int i = 0; i < 7; ++i) {
                const float angle = float(i) * .38f - 1.1f;
                const float length = radius * (.52f + .46f * std::abs(std::cos(float(i) * 1.7f)));
                g.setColour(b.interpolatedWith(c, float(i) / 6).withAlpha(.45f));
                g.drawLine(centre.x - std::sin(angle) * length, centre.y - std::cos(angle) * length,
                           centre.x + std::sin(angle) * length, centre.y + std::cos(angle) * length, 1.f);
            }
        } else {
            const float width = std::max(1.f, std::min(r.getWidth() * .22f, 8.f));
            for (int i = 0; i < 2; ++i) {
                const float x = r.getCentreX() + (i == 0 ? -width - 1.5f : 1.5f);
                g.setColour(a.withAlpha(.4f)); g.fillRect(x, r.getY(), width, r.getHeight());
                g.setGradientFill(ColourGradient(b, 0, r.getBottom(), c, 0, r.getY(), false));
                const float height = r.getHeight() * (i == 0 ? .77f : .68f);
                g.fillRect(x, r.getBottom() - height, width, height);
            }
        }
        g.setColour(ink.withAlpha(.09f)); g.drawRoundedRectangle(area.reduced(.3f), 1.5f, .5f);
    }
};

class FactoryPresetBrowser final : public Component, private Timer {
public:
    FactoryPresetBrowser(AuroraAudioProcessor& processor, std::function<void()> changed)
        : p(processor), onChange(std::move(changed)) {
        setName("Factory appearance presets"); setComponentID("factoryPresetBrowser");
        viewport.setComponentID("factoryPresetViewport");
        content.setComponentID("factoryPresetCards");
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false); viewport.setScrollBarThickness(7);
        viewport.getVerticalScrollBar().setColour(ScrollBar::thumbColourId, rgb(0x557466));
        addAndMakeVisible(viewport);
        for (int i = 0; i < factoryPresetCount(); ++i) {
            auto card = std::make_unique<PresetCard>(i);
            card->onClick = [this, i] {
                if (p.applyAppearancePreset(factoryPresetXml(i))) {
                    syncSelection();
                    if (onChange) onChange();
                }
            };
            content.addAndMakeVisible(*card);
            cards.push_back(std::move(card));
        }
        setSize(340, 440);
        syncSelection();
        startTimerHz(4);
    }
    ~FactoryPresetBrowser() override {
        stopTimer();
        viewport.setViewedComponent(nullptr, false);
    }
    void resized() override {
        viewport.setBounds(getLocalBounds());
        const int width = std::max(1, getWidth() - 10);
        const int columns = getWidth() >= 480 ? 2 : 1;
        const int gap = 10;
        const int cardWidth = (width - (columns - 1) * gap) / columns;
        const int cardHeight = jlimit(142, 204, int(float(cardWidth) * .48f) + 40);
        const int count = int(cards.size()), rows = (count + columns - 1) / columns;
        content.setSize(width, std::max(getHeight(), rows * (cardHeight + gap) - gap));
        for (int i = 0; i < count; ++i)
            cards[std::size_t(i)]->setBounds((i % columns) * (cardWidth + gap), (i / columns) * (cardHeight + gap), cardWidth, cardHeight);
    }
private:
    AuroraAudioProcessor& p;
    std::function<void()> onChange;
    Viewport viewport;
    Component content;
    std::vector<std::unique_ptr<PresetCard>> cards;
    String previousAppearance;
    void timerCallback() override { if (isShowing()) syncSelection(); }
    void syncSelection() {
        const auto appearance = p.createAppearancePreset();
        if (appearance == previousAppearance) return;
        previousAppearance = appearance;
        const int selected = findFactoryPreset(appearance);
        for (std::size_t i = 0; i < cards.size(); ++i)
            cards[i]->setToggleState(int(i) == selected, dontSendNotification);
    }
};
}

std::unique_ptr<juce::Component> createFactoryPresetBrowser(
    AuroraAudioProcessor& processor, std::function<void()> onChange) {
    return std::make_unique<FactoryPresetBrowser>(processor, std::move(onChange));
}
