#include "PluginEditor.h"
#include "Typography.h"
#include "SpectralProjection.h"
#include "Palette.h"
#include "CustomizationPanel.h"
#include "FactoryPresets.h"
#include <cmath>

using namespace juce;
namespace { constexpr float pi = MathConstants<float>::pi;
Colour rgb(uint32 c) { return Colour(c | 0xff000000); }
float unit(float v) { return jlimit(0.0f, 1.0f, v); }
double waveformWindowSeconds(int selection) {
    constexpr std::array<double,6> seconds{6.,12.,24.,0.,3.,1.};
    return seconds[std::size_t(jlimit(0,5,selection))];
}
Path consoleShape(Rectangle<float> r,float cut=5.f) {
    Path p;p.startNewSubPath(r.getX()+cut,r.getY());p.lineTo(r.getRight(),r.getY());
    p.lineTo(r.getRight(),r.getBottom()-cut);p.lineTo(r.getRight()-cut,r.getBottom());
    p.lineTo(r.getX(),r.getBottom());p.lineTo(r.getX(),r.getY()+cut);p.closeSubPath();return p;
}
int namedFactoryPreset(const String& name) {
    for(int i=0;i<factoryPresetCount();++i)if(name==factoryPresetName(i))return i;
    return -1;
}
int namedPalette(const String& name) {
    for(int i=0;i<auroraPaletteCount;++i)if(name==auroraPaletteNames[size_t(i)])return i;
    return -1;
}

class GradientPicker final : public Component, private ChangeListener {
public:
    GradientPicker(Colour initial, Colour background, Colour foreground,
                   std::function<void(Colour)> changed)
        : selector(ColourSelector::showColourspace | ColourSelector::showSliders),
          onColour(std::move(changed)), panelColour(background) {
        pickerLook.setColour(Slider::textBoxTextColourId,foreground);
        pickerLook.setColour(Slider::textBoxBackgroundColourId,background);
        pickerLook.setColour(Slider::textBoxOutlineColourId,foreground.withAlpha(.2f));
        pickerLook.setColour(Slider::thumbColourId,foreground.interpolatedWith(background,.35f));
        pickerLook.setColour(Slider::trackColourId,foreground.withAlpha(.3f));
        pickerLook.setColour(TextEditor::textColourId,foreground);
        pickerLook.setColour(Label::textColourId,foreground);
        setLookAndFeel(&pickerLook);
        selector.setCurrentColour(initial,dontSendNotification);
        selector.setColour(ColourSelector::backgroundColourId,background);
        selector.setColour(ColourSelector::labelTextColourId,foreground);
        selector.addChangeListener(this);
        addAndMakeVisible(selector);
        for(auto* child:selector.getChildren())if(auto* slider=dynamic_cast<Slider*>(child)){
            slider->setColour(Slider::textBoxTextColourId,foreground);
            slider->setColour(Slider::textBoxBackgroundColourId,background);
            slider->setColour(Slider::textBoxOutlineColourId,foreground.withAlpha(.2f));
            for(auto* item:slider->getChildren())if(auto* label=dynamic_cast<Label*>(item)){
                label->setColour(Label::textColourId,foreground);
                label->setColour(Label::textWhenEditingColourId,foreground);
                label->setFont(Font(FontOptions(auroraTypography::monoTypeface(),13,Font::plain)));
            }
        }
        hex.setTitle("Hex colour");
        hex.setFont(Font(FontOptions(auroraTypography::monoTypeface(),13,Font::plain)));
        hex.setJustification(Justification::centred);
        hex.setInputRestrictions(7,"#0123456789abcdefABCDEF");
        hex.setText("#"+initial.toDisplayString(false),false);
        hex.setColour(TextEditor::backgroundColourId,background);
        hex.setColour(TextEditor::textColourId,foreground);
        hex.setColour(TextEditor::outlineColourId,foreground.withAlpha(.2f));
        hex.setColour(TextEditor::focusedOutlineColourId,foreground.withAlpha(.6f));
        hex.applyColourToAllText(foreground);
        hex.onTextChange=[this]{
            const auto value=hex.getText().trimCharactersAtStart("#");
            if(value.length()==6) {
                const auto colour=Colour(0xff000000 | value.getHexValue32());
                selector.setCurrentColour(colour,dontSendNotification);
                onColour(colour);
            }
        };
        hex.onEscapeKey=[this]{if(auto* box=findParentComponentOfClass<CallOutBox>())box->dismiss();};
        hex.onReturnKey=hex.onEscapeKey;
        addAndMakeVisible(hex);
        setSize(310,338);
    }
    ~GradientPicker() override {selector.removeChangeListener(this);setLookAndFeel(nullptr);}
    void paint(Graphics& g) override {g.fillAll(panelColour);}
    void resized() override {hex.setBounds(14,12,getWidth()-28,30);selector.setBounds(0,52,getWidth(),getHeight()-52);}
private:
    LookAndFeel_V4 pickerLook;
    ColourSelector selector;
    TextEditor hex;
    std::function<void(Colour)> onColour;
    Colour panelColour;
    void changeListenerCallback(ChangeBroadcaster*) override {
        const auto colour=selector.getCurrentColour().withAlpha(1.f);
        hex.setText("#"+colour.toDisplayString(false),false);
        onColour(colour);
    }
};
}
SoftClubLookAndFeel::SoftClubLookAndFeel() {
    setColour(ComboBox::textColourId, rgb(0x27434c));
    setColour(PopupMenu::backgroundColourId, rgb(0xe6eeee));
    setColour(PopupMenu::textColourId, rgb(0x27434c));
    setColour(PopupMenu::highlightedBackgroundColourId, rgb(0xbed5db));
    setColour(TextButton::textColourOffId, rgb(0x27434c));
    setColour(TextButton::textColourOnId, rgb(0xf3f7f6));
    setColour(Slider::thumbColourId, rgb(0x416f80));
    setColour(Slider::trackColourId, rgb(0x86aab5));
    setColour(Slider::backgroundColourId, rgb(0xb8ced2));
}
void SoftClubLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) {
    auto r=b.getLocalBounds().toFloat().reduced(.5f);
    auto colour=b.getToggleState()?b.findColour(TextButton::buttonOnColourId):b.findColour(TextButton::buttonColourId);
    g.setColour(colour.brighter(over||down?.08f:0.f));
    if(instrumentStyle) {
        auto shape=consoleShape(r,3);g.fillPath(shape);
        g.setColour(b.findColour(ComboBox::outlineColourId));g.strokePath(shape,PathStrokeType(.8f));
        if(b.hasKeyboardFocus(true)){g.setColour(b.findColour(TextButton::textColourOffId));g.strokePath(consoleShape(r.reduced(2),2),PathStrokeType(1));}
    } else {g.fillRoundedRectangle(r,5);g.setColour(b.findColour(ComboBox::outlineColourId));g.drawRoundedRectangle(r,5,.7f);}
}
void SoftClubLookAndFeel::drawButtonText(Graphics& g,TextButton& button,bool over,bool down) {
    const auto id=button.getComponentID();
    if(id=="factoryPresetPrevious" || id=="factoryPresetNext"){
        const float direction=id=="factoryPresetPrevious"?-1.f:1.f;
        const auto centre=button.getLocalBounds().toFloat().getCentre();
        Path chevron;chevron.startNewSubPath(centre.x-direction*2.f,centre.y-4.f);
        chevron.lineTo(centre.x+direction*2.f,centre.y);chevron.lineTo(centre.x-direction*2.f,centre.y+4.f);
        g.setColour(button.findColour(TextButton::textColourOffId));
        g.strokePath(chevron,PathStrokeType(1.2f,PathStrokeType::curved,PathStrokeType::rounded));return;
    }
    if(id=="customise") {
        g.setColour(button.findColour(button.getToggleState()?TextButton::textColourOnId:TextButton::textColourOffId));
        g.setFont(getTextButtonFont(button,button.getHeight()));
        g.drawText(button.getButtonText(),button.getLocalBounds().withTrimmedRight(17),Justification::centred);
        const float x=float(button.getWidth()-13),y=float(button.getHeight())*.5f;
        const float direction=button.getToggleState()?-1.f:1.f;
        Path arrow;arrow.startNewSubPath(x-3,y-direction*1.5f);arrow.lineTo(x,y+direction*1.5f);arrow.lineTo(x+3,y-direction*1.5f);
        g.strokePath(arrow,PathStrokeType(1.2f,PathStrokeType::curved,PathStrokeType::rounded));return;
    }
    LookAndFeel_V4::drawButtonText(g,button,over,down);
}
void SoftClubLookAndFeel::drawComboBox(Graphics& g,int w,int h,bool,int,int,int,int,ComboBox& box) {
    const auto r=Rectangle<float>(.5f,.5f,float(w-1),float(h-1));
    g.setColour(box.findColour(ComboBox::backgroundColourId));
    if(instrumentStyle)g.fillPath(consoleShape(r,3));else g.fillRoundedRectangle(r,5);
    g.setColour(box.findColour(ComboBox::outlineColourId));
    if(instrumentStyle)g.strokePath(consoleShape(r,3),PathStrokeType(.8f));else g.drawRoundedRectangle(r,5,.7f);
    Path arrow;arrow.startNewSubPath(float(w-18),float(h/2-2));arrow.lineTo(float(w-14),float(h/2+2));arrow.lineTo(float(w-10),float(h/2-2));
    g.setColour(box.findColour(ComboBox::textColourId).withAlpha(.7f));g.strokePath(arrow,PathStrokeType(1));
    if(instrumentStyle){g.setColour(box.findColour(ComboBox::outlineColourId));g.drawVerticalLine(w-28,7.f,float(h-7));}
}
Font SoftClubLookAndFeel::getComboBoxFont(ComboBox&) {return Font(FontOptions(instrumentStyle?auroraTypography::monoTypeface():auroraTypography::sansTypeface(),instrumentStyle?11.f:12.f,Font::plain));}
Font SoftClubLookAndFeel::getTextButtonFont(TextButton& b,int height) {
    return instrumentStyle?Font(FontOptions(auroraTypography::monoTypeface(),11,Font::plain)):LookAndFeel_V4::getTextButtonFont(b,height);
}
void SoftClubLookAndFeel::drawPopupMenuItemWithOptions(Graphics& g,const Rectangle<int>& area,bool highlighted,
        const PopupMenu::Item& item,const PopupMenu::Options& options) {
    const auto* target=options.getTargetComponent();
    const ScopedValueSetter<bool> scoped(factoryMenuContext,target && target->getComponentID()=="factoryPresetChoice");
    LookAndFeel_V4::drawPopupMenuItemWithOptions(g,area,highlighted,item,options);
}
void SoftClubLookAndFeel::getIdealPopupMenuItemSizeWithOptions(const String& label,bool separator,int standard,int& w,int& h,
        const PopupMenu::Options& options) {
    const auto* target=options.getTargetComponent();
    const ScopedValueSetter<bool> scoped(factoryMenuContext,target && target->getComponentID()=="factoryPresetChoice");
    LookAndFeel_V4::getIdealPopupMenuItemSizeWithOptions(label,separator,standard,w,h,options);
}
void SoftClubLookAndFeel::getIdealPopupMenuItemSize(const String& label,bool separator,int standard,int& w,int& h) {
    LookAndFeel_V4::getIdealPopupMenuItemSize(label,separator,standard,w,h);
    if(factoryMenuContext && namedFactoryPreset(label)>=0){w=std::max(w,304);h=36;}
    else if(namedPalette(label)>=0){w=std::max(w,264);h=34;}
    else if(!separator && instrumentStyle){w=std::max(w,184);h=30;}
}
void SoftClubLookAndFeel::drawPopupMenuItem(Graphics& g,const Rectangle<int>& area,bool separator,bool active,bool highlighted,bool ticked,bool submenu,
                                           const String& label,const String& shortcut,const Drawable* icon,const Colour* colour) {
    const int index=namedPalette(label),presetIndex=factoryMenuContext?namedFactoryPreset(label):-1;
    if(index<0 && presetIndex<0){LookAndFeel_V4::drawPopupMenuItem(g,area,separator,active,highlighted,ticked,submenu,label,shortcut,icon,colour);return;}
    auto r=area.toFloat().reduced(5,2);
    if(highlighted){g.setColour(findColour(PopupMenu::highlightedBackgroundColourId));g.fillRoundedRectangle(r,2);}
    const auto foreground=findColour(PopupMenu::textColourId);
    if(ticked){g.setColour(foreground);g.fillRect(r.getX()+5,r.getCentreY()-2,4.f,4.f);}
    g.setColour(foreground.withAlpha(active?1.f:.4f));g.setFont(Font(FontOptions(auroraTypography::monoTypeface(),11,Font::plain)));
    g.drawText(label,r.withTrimmedLeft(18).withTrimmedRight(87),Justification::centredLeft);
    if(presetIndex>=0 || index!=auroraCustomPalette) {
        const auto stops=presetIndex>=0?factoryPresetColours(presetIndex):auroraPalette(index,0,0,0);
        const auto strip=Rectangle<float>(r.getRight()-80,r.getY()+7,70,r.getHeight()-14);
        ColourGradient gradient(rgb(stops[0]),strip.getX(),0,rgb(stops[2]),strip.getRight(),0,false);
        gradient.addColour(.5,rgb(stops[1]));g.setGradientFill(gradient);g.fillRoundedRectangle(strip,1.5f);
        g.setColour(foreground.withAlpha(.15f));g.drawRoundedRectangle(strip,1.5f,.5f);
    }
}
void SoftClubLookAndFeel::drawLinearSlider(Graphics& g,int x,int y,int w,int h,float pos,float min,float max,Slider::SliderStyle style,Slider& slider) {
    if(!instrumentStyle || style!=Slider::LinearHorizontal){LookAndFeel_V4::drawLinearSlider(g,x,y,w,h,pos,min,max,style,slider);return;}
    const float cy=float(y)+float(h)*.5f;
    g.setColour(slider.findColour(Slider::backgroundColourId));g.drawLine(float(x),cy,float(x+w),cy,2);
    g.setColour(slider.findColour(Slider::thumbColourId));g.drawLine(float(x),cy,pos,cy,2);
    for(int i=0;i<=8;++i){const float xx=float(x)+float(w)*float(i)/8;g.setColour(slider.findColour(Slider::backgroundColourId));g.drawLine(xx,cy+5,xx,cy+(i%4==0?9:7),1);}
    g.setColour(slider.findColour(Slider::thumbColourId));g.fillRect(pos-3,cy-5,6.f,10.f);
    g.setColour(Colours::black.withAlpha(.55f));g.drawVerticalLine(int(pos),cy-3,cy+3);
}

AuroraAudioProcessorEditor::AuroraAudioProcessorEditor(AuroraAudioProcessor& p) : AudioProcessorEditor(p), processor(p) {
    setLookAndFeel(&theme);
    mode.addItemList({"Folded Light", "Liquid Aperture", "Pearl Cloud", "Night Transit", "Diffraction Veil", "Orbit Loom"},1);
    themeChoice.addItem("Flight Deck",4);themeChoice.addSeparator();
    themeChoice.addItemList({"Soft Club", "Afterhours", "Chrome"},1);
    themeChoice.setSelectedId(p.themeMode.load()+1,dontSendNotification);
    themeChoice.onChange=[this]{processor.themeMode.store(themeChoice.getSelectedId()-1);setPalette();repaint();};
    for(int i=7;i<auroraPaletteCount;++i)palette.addItem(auroraPaletteNames[size_t(i)],i+1);
    palette.addSeparator();
    for(int i=0;i<6;++i)palette.addItem(auroraPaletteNames[size_t(i)],i+1);
    palette.addSeparator();palette.addItem("Custom",auroraCustomPalette+1);
    layout.addItemList({"Studio", "Meter strip", "Waveform", "Custom"},1);
    waveWindowChoice.addItem("1 s",6);waveWindowChoice.addItem("3 s",5);
    waveWindowChoice.addItemList({"6 s","12 s","24 s","Scope"},1);
    waveStyleChoice.addItemList({"Silk","Prism","Trace"},1);
    waveZoomChoice.addItemList({"1x amplitude","2x amplitude","4x amplitude"},1);
    waveStyleChoice.setTitle("Waveform material");waveZoomChoice.setTitle("Waveform amplitude zoom");
    waveStyleChoice.setTooltip("Silk: measured sample density. Prism: filtered band contours. Trace: fine RMS and band lines. All preserve the signed sample-peak envelope.");
    waveZoomChoice.setTooltip("Waveform display magnification only. 1x shows +/-1; 2x shows +/-0.5; 4x shows +/-0.25. Measurements and clipping detection are unchanged.");
    waveStyleChoice.onChange=[this]{processor.waveformStyle.store(waveStyleChoice.getSelectedId()-1);repaint();};
    waveZoomChoice.onChange=[this]{processor.waveformZoom.store(waveZoomChoice.getSelectedId()-1);repaint();};
    waveStyleChoice.setSelectedId(p.waveformStyle.load()+1,dontSendNotification);
    waveZoomChoice.setSelectedId(p.waveformZoom.load()+1,dontSendNotification);
    waveColourChoice.addItemList({"Bands","Palette"},1);
    waveWindowChoice.setTitle("Waveform time window");waveColourChoice.setTitle("Waveform colour mode");
    waveWindowChoice.setTooltip("Scrolling PCM history: 1, 3, 6, 12 or 24 seconds. Scope shows the original triggered close-up.");
    waveColourChoice.setTooltip("Bands: apricot lows, mint mids, violet highs. Palette: your selected gradient maps waveform level.");
    waveWindowChoice.setSelectedId(p.waveformWindow.load()+1,dontSendNotification);
    waveColourChoice.setSelectedId(p.waveformColour.load()+1,dontSendNotification);
    waveWindowChoice.onChange=[this]{if(waveWindowChoice.getSelectedId()<1)return;processor.custom.set("waveCustomTime",0);processor.waveformWindow.store(waveWindowChoice.getSelectedId()-1);positionWaveControls();repaint();};
    waveColourChoice.onChange=[this]{processor.waveformColour.store(waveColourChoice.getSelectedId()-1);repaint();};
    mode.setSelectedId(p.visualMode.load()+1, dontSendNotification);
    palette.setSelectedId(p.palette.load()+1, dontSendNotification);
    layout.setSelectedId(p.layoutMode.load()+1, dontSendNotification);
    mode.onChange=[this]{ processor.visualMode.store(mode.getSelectedId()-1); repaint(); };
    palette.onChange=[this]{ processor.palette.store(palette.getSelectedId()-1); setPalette(); repaint(); };
    layout.onChange=[this]{
        if(layout.getSelectedId()<1)return;
        processor.layoutMode.store(layout.getSelectedId()-1);
        setCustomizationOpen(false);applyLayout();repaint();
    };
    factoryPresetChoice.setComponentID("factoryPresetChoice");factoryPresetChoice.setTitle("Appearance preset");
    factoryPresetChoice.setTextWhenNothingSelected("Presets");
    factoryPresetChoice.setTooltip("Choose a complete look. Every setting remains editable; * marks changes. Save your version in Customise > Presets > Saved.");
    for(int i=0;i<factoryPresetCount();++i)factoryPresetChoice.addItem(factoryPresetName(i),i+1);
    factoryPresetChoice.onChange=[this]{if(factoryPresetChoice.getSelectedId()>0)applyFactoryPreset(factoryPresetChoice.getSelectedId()-1);};
    factoryPresetPrevious.setComponentID("factoryPresetPrevious");factoryPresetNext.setComponentID("factoryPresetNext");
    factoryPresetPrevious.setTitle("Previous appearance preset");factoryPresetNext.setTitle("Next appearance preset");
    factoryPresetPrevious.setTooltip("Previous complete look");factoryPresetNext.setTooltip("Next complete look");
    factoryPresetPrevious.onClick=[this]{applyFactoryPreset(lastFactoryPreset<0?factoryPresetCount()-1:(lastFactoryPreset+factoryPresetCount()-1)%factoryPresetCount());};
    factoryPresetNext.onClick=[this]{applyFactoryPreset((lastFactoryPreset+1)%factoryPresetCount());};
    customise.onClick=[this]{showCustomization();};
    customise.setComponentID("customise");
    customizationDropdown.setComponentID("customizationDropdown");
    customizationDropdown.setScrollBarsShown(true,false);customizationDropdown.setScrollBarThickness(8);
    customizationDropdown.setScrollOnDragEnabled(false);
    addChildComponent(customizationDropdown);
    customise.setTitle("Customise Aurora");customise.setTooltip("Colours, waveform materials, meter ranges, scene motion, dashboard and presets.");
    dashboardScroll.addListener(this);
    addAndMakeVisible(dashboardScroll);
    freeze.setClickingTogglesState(true); freeze.setToggleState(p.frozen.load(),dontSendNotification);
    freeze.onClick=[this]{processor.frozen.store(freeze.getToggleState()); freeze.setButtonText(freeze.getToggleState()?"Resume":"Freeze");};
    reset.onClick=[this]{processor.resetMeters(); heldL=heldR=-120; clearVisualHistory(); frame=AnalysisFrame{}; repaint();};
    intensity.setSliderStyle(Slider::LinearHorizontal); intensity.setTextBoxStyle(Slider::NoTextBox,false,0,0);
    intensity.setRange(0.25,4,0.05); intensity.setValue(p.visualGain.load(),dontSendNotification);
    intensity.onValueChange=[this]{processor.visualGain.store(float(intensity.getValue()));};
    mode.setTooltip("Choose the reactive visual. Every shape responds to the incoming audio.");
    palette.setTooltip("Choose a gradient, or edit any of its three colour stops. Custom colours are saved with the session.");
    intensity.setTooltip("Visual intensity only. Audio and measured levels are unchanged.");
    reset.setTooltip("Reset loudness integration, peak holds and visual history.");
    freeze.setTooltip("Hold the display. Audio and analysis continue.");
    themeChoice.setTitle("Material theme"); themeChoice.setTooltip("Theme changes the material, light and atmosphere independently of colour.");
    mode.setTitle("Reactive visual"); palette.setTitle("Colour palette"); layout.setTitle("Layout"); intensity.setTitle("Visual intensity");
    for (Component* c : std::initializer_list<Component*>{&mode,&themeChoice,&palette,&layout,&freeze,&reset,&customise,&intensity,&waveWindowChoice,&waveColourChoice,&waveStyleChoice,&waveZoomChoice,&factoryPresetChoice,&factoryPresetPrevious,&factoryPresetNext}) addAndMakeVisible(c);
    for(int i=0;i<3;i++) {
        auto& swatch=colourSwatches[size_t(i)];
        swatch.setTitle(String("Gradient colour ")+String(i+1));
        swatch.setTooltip(String("Edit the ")+(i==0?"shadow":i==1?"body":"highlight")+" colour using RGB or hex. All three stops accept any colour.");
        swatch.onClick=[this,i]{editColour(i);};
        addAndMakeVisible(swatch);
    }
    setResizable(true,true);setResizeLimits(1100,(p.layoutMode.load()==0||p.layoutMode.load()==3)?620:390,2000,1300);
    addAndMakeVisible(sceneClip);sceneClip.setInterceptsMouseClicks(false,true);sceneClip.addAndMakeVisible(visualSurface);
    visualSurface.setTooltip(CharPointer_UTF8("Drag to orbit \xc2\xb7 Scroll to zoom \xc2\xb7 Double-click to reset"));
    visualSurface.onCameraChanged=[this](float yaw,float pitch,float zoom) {
        processor.cameraYaw.store(yaw);processor.cameraPitch.store(pitch);processor.cameraZoom.store(zoom);
    };
    setSize(1280,p.layoutMode.load()==1?390:p.layoutMode.load()==2?570:820);
    setPalette(); frame=processor.analyzer.snapshot();
    lastTimerTime=Time::getMillisecondCounterHiRes()*0.001;
    appliedLayout=p.layoutMode.load();
    clearVisualHistory();syncControls();syncFactoryPreset();startTimerHz(60);
}
AuroraAudioProcessorEditor::~AuroraAudioProcessorEditor() { stopTimer(); customizationDropdown.setViewedComponent(nullptr,false); customizationPanel.reset(); setLookAndFeel(nullptr); }
std::array<std::uint32_t,3> AuroraAudioProcessorEditor::currentColours() const {
    auto colours=auroraPalette(processor.palette.load(),processor.customColourA.load(),
                         processor.customColourB.load(),processor.customColourC.load());
    if(processor.custom.get("paletteReverse")>.5f)std::swap(colours[0],colours[2]);
    return colours;
}
std::array<std::uint32_t,3> AuroraAudioProcessorEditor::moduleColours(const char* prefix) const {
    auto colours=currentColours();const auto key=String(prefix);
    if(processor.custom.get((key+"OwnPalette").toRawUTF8())>.5f){
        for(int i=0;i<3;++i)colours[std::size_t(i)]=std::uint32_t(processor.custom.get((key+"Colour"+String::charToString(char('A'+i))).toRawUTF8()));
        if(processor.custom.get("paletteReverse")>.5f)std::swap(colours[0],colours[2]);
    }
    return colours;
}
bool AuroraAudioProcessorEditor::usesScope() const {
    return processor.waveformWindow.load()==3 && processor.custom.get("waveCustomTime")<.5f;
}
double AuroraAudioProcessorEditor::waveformSeconds() const {
    return processor.custom.get("waveCustomTime")>.5f?processor.custom.get("waveSeconds"):waveformWindowSeconds(processor.waveformWindow.load());
}
void AuroraAudioProcessorEditor::showCustomization() { setCustomizationOpen(!customizationOpen); }
void AuroraAudioProcessorEditor::setCustomizationOpen(bool open) {
    if(customizationOpen==open)return;
    if(open && !customizationPanel){
        Component::SafePointer<AuroraAudioProcessorEditor> safe(this);
        customizationPanel=createCustomizationPanel(processor,[safe]{if(safe!=nullptr)safe->customizationChanged();});
        customizationPanel->setOpaque(true);
        customizationDropdown.setViewedComponent(customizationPanel.get(),false);
    }
    customizationOpen=open;
    customise.setToggleState(open,dontSendNotification);
    customizationDropdown.setVisible(open);
    if(open)customizationDropdown.toFront(false);
    if(!open)rebuildStereo();
    resized();repaint();
    if(!open)customise.grabKeyboardFocus();
}
bool AuroraAudioProcessorEditor::keyPressed(const KeyPress& key) {
    if(customizationOpen && key==KeyPress::escapeKey){setCustomizationOpen(false);return true;}
    return false;
}
void AuroraAudioProcessorEditor::applyLayout() {
    const int selected=processor.layoutMode.load();
    if(appliedLayout==selected)return;
    appliedLayout=selected;dashboardOffset=0;
    setResizeLimits(1100,(selected==0||selected==3)?620:390,2000,1300);
    setSize(getWidth(),selected==1?390:selected==2?570:820);
    resized();
}
void AuroraAudioProcessorEditor::syncControls() {
    // A ComboBox menu delivers onChange asynchronously. Only a changed model
    // value may update it, otherwise a timer can erase a user's pending click.
    const std::array<int,8> values {processor.visualMode.load(),processor.palette.load(),processor.themeMode.load(),
        processor.layoutMode.load(),processor.waveformColour.load(),processor.waveformStyle.load(),processor.waveformZoom.load(),
        processor.custom.get("waveCustomTime")>.5f?-2:processor.waveformWindow.load()};
    const std::array<ComboBox*,8> boxes {&mode,&palette,&themeChoice,&layout,&waveColourChoice,&waveStyleChoice,&waveZoomChoice,&waveWindowChoice};
    bool changed=false;
    for(std::size_t i=0;i<values.size();++i)if(values[i]!=observedChoices[i]){
        changed=true;observedChoices[i]=values[i];
        if(values[i]>=0)boxes[i]->setSelectedId(values[i]+1,dontSendNotification);
    }
    const double seconds=values[7]==-2?waveformSeconds():-1.;
    if(seconds!=observedWaveSeconds){
        changed=true;observedWaveSeconds=seconds;
        if(seconds>=0)waveWindowChoice.setText(String(seconds,seconds==std::floor(seconds)?0:2)+" s",dontSendNotification);
    }
    applyLayout();
    if(changed)repaint();
}
void AuroraAudioProcessorEditor::syncFactoryPreset() {
    const auto appearance=processor.createAppearancePreset();
    if(appearance==lastPresetAppearance)return;
    lastPresetAppearance=appearance;
    const int matched=findFactoryPreset(appearance);
    factoryPresetChoice.setSelectedId(matched+1,dontSendNotification);
    if(matched>=0)lastFactoryPreset=matched;
    else factoryPresetChoice.setText(lastFactoryPreset>=0?String(factoryPresetName(lastFactoryPreset))+" *":"Presets",dontSendNotification);
}
void AuroraAudioProcessorEditor::applyFactoryPreset(int index) {
    if(index<0 || index>=factoryPresetCount() || !processor.applyAppearancePreset(factoryPresetXml(index)))return;
    lastFactoryPreset=index;dashboardOffset=0;
    customizationChanged();syncFactoryPreset();
}
void AuroraAudioProcessorEditor::scrollBarMoved(ScrollBar*,double position) {dashboardOffset=position;resized();repaint();}
void AuroraAudioProcessorEditor::customizationChanged() {
    syncControls();
    for(std::size_t i=0;i<Customization::descriptors.size();++i)cachedCustom[i]=processor.custom.get(Customization::descriptors[i].id);
    setPalette();rebuildSpectrogram();rebuildStereo();resized();syncFactoryPreset();repaint();
}
void AuroraAudioProcessorEditor::editColour(int index) {
    const auto colours=currentColours();
    Component::SafePointer<AuroraAudioProcessorEditor> safe(this);
    auto picker=std::make_unique<GradientPicker>(rgb(colours[size_t(index)]),paper,ink,
        [safe,index](Colour colour){
            if(safe==nullptr)return;
            auto values=safe->currentColours();values[size_t(index)]=colour.getARGB()&0xffffff;
            if(safe->processor.custom.get("paletteReverse")>.5f)std::swap(values[0],values[2]);
            auto& p=safe->processor;
            p.customColourA.store(values[0]);p.customColourB.store(values[1]);p.customColourC.store(values[2]);
            p.palette.store(auroraCustomPalette);
            safe->setPalette();safe->repaint();
        });
    CallOutBox::launchAsynchronously(std::move(picker),colourSwatches[size_t(index)].getScreenBounds(),nullptr);
}
void AuroraAudioProcessorEditor::setPalette() {
    const int t=processor.themeMode.load();const bool cockpit=isFlightDeck(),dark=isDark(),customInterface=processor.custom.get("interfaceOverride")>.5f;
    const auto colours=currentColours();
    const std::array<std::uint32_t,9> appearance {colours[0],colours[1],colours[2],std::uint32_t(t),std::uint32_t(customInterface),
        std::uint32_t(processor.custom.get("textColour")),std::uint32_t(processor.custom.get("backgroundColour")),
        std::uint32_t(processor.custom.get("gridColour")),std::uint32_t(processor.custom.get("panelColour"))};
    if(interfaceAppearanceValid && interfaceAppearance==appearance)return;
    interfaceAppearance=appearance;interfaceAppearanceValid=true;
    accent=rgb(colours[1]);secondary=rgb(colours[2]);
    for(int i=0;i<3;i++)colourSwatches[size_t(i)].setColour(TextButton::buttonColourId,rgb(colours[size_t(i)]));
    ink=cockpit?rgb(0xe2dfcf):t==1?rgb(0xd7e7ec):t==2?rgb(0x37434d):rgb(0x274b5b);
    paper=cockpit?rgb(0x171b1a):t==1?rgb(0x111b2b):t==2?rgb(0xd2d6db):rgb(0xdbe7e9);
    line=cockpit?rgb(0x35413b):ink.withAlpha(dark?.13f:.18f);
    if(customInterface){
        ink=rgb(std::uint32_t(processor.custom.get("textColour")));paper=rgb(std::uint32_t(processor.custom.get("backgroundColour")));line=rgb(std::uint32_t(processor.custom.get("gridColour")));
    }
    const Colour control=customInterface?rgb(std::uint32_t(processor.custom.get("panelColour"))).interpolatedWith(ink,.075f):cockpit?rgb(0x202824):paper.interpolatedWith(t==1?Colour(0xff203044):Colours::white,.26f);
    if(theme.instrumentStyle!=cockpit){theme.instrumentStyle=cockpit;sendLookAndFeelChange();}
    theme.setColour(ComboBox::backgroundColourId,control);theme.setColour(ComboBox::textColourId,ink);
    for(auto* box:{&mode,&themeChoice,&palette,&layout,&waveWindowChoice,&waveColourChoice,&waveStyleChoice,&waveZoomChoice,&factoryPresetChoice})box->setColour(ComboBox::textColourId,ink);
    theme.setColour(ComboBox::outlineColourId,line);
    theme.setColour(PopupMenu::backgroundColourId,paper);theme.setColour(PopupMenu::textColourId,ink);
    theme.setColour(PopupMenu::highlightedBackgroundColourId,customInterface?control.interpolatedWith(accent,.3f):cockpit?rgb(0x34483c):accent.withAlpha(.4f));
    theme.setColour(PopupMenu::highlightedTextColourId,ink);
    theme.setColour(TextButton::buttonColourId,control);
    theme.setColour(TextButton::buttonOnColourId,customInterface?control.interpolatedWith(accent,.3f):cockpit?rgb(0x385443):accent.darker(.2f));
    theme.setColour(TextButton::textColourOffId,ink);theme.setColour(TextButton::textColourOnId,ink);
    theme.setColour(Slider::thumbColourId,cockpit && !customInterface?rgb(0xc5c7a4):accent);
    theme.setColour(Slider::trackColourId,secondary);theme.setColour(Slider::backgroundColourId,line);
    theme.setColour(TooltipWindow::backgroundColourId,paper);
    theme.setColour(TooltipWindow::textColourId,ink);theme.setColour(TooltipWindow::outlineColourId,line);
}
void AuroraAudioProcessorEditor::resized() {
    const int y=65,h=30;
    mode.setBounds(20,y,178,h);themeChoice.setBounds(208,y,142,h);palette.setBounds(360,y,150,h);
    for(int i=0;i<3;i++)colourSwatches[size_t(i)].setBounds(532+i*32,y,28,23);
    intensity.setBounds(660,y,120,h);
    waveStyleChoice.setBounds(805,y,112,h);waveZoomChoice.setBounds(930,y,145,h);
    for(Component* control:std::initializer_list<Component*>{&mode,&themeChoice,&palette,&intensity,&waveStyleChoice,&waveZoomChoice})
        control->setVisible(!customizationOpen);
    for(auto& swatch:colourSwatches)swatch.setVisible(!customizationOpen);
    factoryPresetPrevious.setBounds(285,17,28,28);
    factoryPresetChoice.setBounds(319,17,226,28);
    factoryPresetNext.setBounds(551,17,28,28);
    customise.setBounds(getWidth()-459,17,104,28);
    layout.setBounds(getWidth()-343,17,126,28);
    freeze.setBounds(getWidth()-201,17,77,28);reset.setBounds(getWidth()-114,17,94,28);
    const Rectangle<float> area(16,112,float(getWidth()-32),float(getHeight()-128));
    const bool custom=processor.layoutMode.load()==3;
    int count=0;for(const char* key:{"showScene","showSpectrum","showSpectrogram","showWaveform","showStereo","showLevels"})if(processor.custom.get(key)>.5f)++count;
    const int columns=int(processor.custom.get("layoutColumns"));const int rows=(count+columns-1)/columns;
    const float gap=processor.custom.get("layoutGap");
    const float contentHeight=std::max(area.getHeight(),float(rows)*220.f+float(std::max(0,rows-1))*gap);
    dashboardScroll.setVisible(!customizationOpen && custom && contentHeight>area.getHeight());
    dashboardOffset=custom?jlimit(0.,double(contentHeight-area.getHeight()),dashboardOffset):0;
    dashboardScroll.setBounds(getWidth()-13,112,10,getHeight()-128);
    dashboardScroll.setRangeLimits(0,contentHeight,dontSendNotification);
    dashboardScroll.setCurrentRange(dashboardOffset,area.getHeight(),dontSendNotification);
    sceneClip.setBounds(area.toNearestInt());
    const auto visual=moduleAreas()[0];
    if(!customizationOpen && !visual.isEmpty() && visual.intersects(area))
        visualSurface.setBounds(visual.reduced(6).translated(-area.getX(),-area.getY()).toNearestInt());
    else
        // Hiding a JUCE OpenGL component destroys its context. Park the native
        // view outside the host's clipped bounds instead, retaining shaders and
        // fluid textures while settings or a meter-only layout cover the scene.
        visualSurface.setTopLeftPosition(-30000,-30000);
    if(customizationPanel){
        customizationDropdown.setBounds(16,56,getWidth()-32,getHeight()-72);
        const int contentHeight=std::max(520,customizationDropdown.getHeight());
        const int scrollbar=contentHeight>customizationDropdown.getHeight()?customizationDropdown.getScrollBarThickness():0;
        customizationPanel->setSize(customizationDropdown.getWidth()-scrollbar,contentHeight);
    }
    positionWaveControls();
}
std::array<Rectangle<float>,6> AuroraAudioProcessorEditor::moduleAreas() const {
    Rectangle<float> area(16,112,float(getWidth()-32),float(getHeight()-128));
    const float gap=processor.custom.get("layoutGap");std::array<Rectangle<float>,6> result{};
    const int view=processor.layoutMode.load();
    if(view==3){
        const std::array<const char*,6> keys{"showScene","showSpectrum","showSpectrogram","showWaveform","showStereo","showLevels"};
        int count=0;for(auto key:keys)if(processor.custom.get(key)>.5f)++count;
        if(count==0)return result;
        const int columns=std::min(count,int(processor.custom.get("layoutColumns"))),rows=(count+columns-1)/columns;
        const float width=(area.getWidth()-12.f-float(columns-1)*gap)/float(columns);
        const float height=std::max(220.f,(area.getHeight()-float(rows-1)*gap)/float(rows));
        const std::array<const char*,6> orderKeys{"orderScene","orderSpectrum","orderSpectrogram","orderWaveform","orderStereo","orderLevels"};
        std::array<std::size_t,6> order{0,1,2,3,4,5};
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return processor.custom.get(orderKeys[a])<processor.custom.get(orderKeys[b]);});
        int cell=0;for(auto i:order)if(processor.custom.get(keys[i])>.5f){
            result[i]={area.getX()+float(cell%columns)*(width+gap),area.getY()+float(cell/columns)*(height+gap)-float(dashboardOffset),width,height};++cell;
        }
        return result;
    }
    if(view==1){const float w=(area.getWidth()-4*gap)/5;for(int i=1;i<6;++i)result[std::size_t(i)]={area.getX()+float(i-1)*(w+gap),area.getY(),w,area.getHeight()};return result;}
    const float right=area.getWidth()*processor.custom.get("layoutLevelWidth");
    if(view==2){result[3]={area.getX(),area.getY(),area.getWidth()-right-gap,area.getHeight()};result[5]={area.getRight()-right,area.getY(),right,area.getHeight()};return result;}
    const float left=std::min(area.getWidth()*processor.custom.get("layoutSceneWidth"),area.getWidth()-right-190.f-2*gap);
    const float middle=area.getWidth()-left-right-2*gap,top=area.getHeight()*processor.custom.get("layoutTopHeight"),bottom=area.getHeight()-top-gap;
    result[0]={area.getX(),area.getY(),left,top};result[1]={area.getX()+left+gap,area.getY(),middle,top};result[5]={area.getRight()-right,area.getY(),right,area.getHeight()};
    result[2]={area.getX(),area.getY()+top+gap,left*.48f,bottom};result[3]={area.getX()+left*.48f+gap,area.getY()+top+gap,left*.52f-gap,bottom};result[4]={area.getX()+left+gap,area.getY()+top+gap,middle,bottom};return result;
}
Rectangle<float> AuroraAudioProcessorEditor::waveformArea() const {return moduleAreas()[3];}
void AuroraAudioProcessorEditor::positionWaveControls() {
    const auto r=waveformArea();
    const bool visible=!customizationOpen && !r.isEmpty() && r.getY()>=112.f && r.getY()<float(getHeight()-50);
    waveWindowChoice.setVisible(visible);
    waveWindowChoice.setBounds(int(r.getRight()-77),int(r.getY()+4),64,24);
    waveColourChoice.setVisible(visible && !usesScope());
    waveStyleChoice.setEnabled(visible && !usesScope());
    waveZoomChoice.setEnabled(visible && !usesScope());
    if(r.getWidth()>=300)waveColourChoice.setBounds(int(r.getRight()-166),int(r.getY()+4),83,24);
    else waveColourChoice.setBounds(int(r.getCentreX()-42),int(r.getBottom()-28),84,21);
}
void AuroraAudioProcessorEditor::timerCallback() {
    const double now=Time::getMillisecondCounterHiRes()*0.001;
    const double elapsed=std::max(0.0,now-lastTimerTime);
    lastTimerTime=now;
    if(now-lastPresetSync>.4){lastPresetSync=now;syncFactoryPreset();}
    syncControls();
    positionWaveControls();
    intensity.setValue(processor.visualGain.load(),dontSendNotification);
    freeze.setToggleState(processor.frozen.load(),dontSendNotification);
    freeze.setButtonText(processor.frozen.load()?"Resume":"Freeze");
    bool customChanged=false;
    for(std::size_t i=0;i<Customization::descriptors.size();++i){
        const float value=processor.custom.get(Customization::descriptors[i].id);
        if(cachedCustom[i]!=value){cachedCustom[i]=value;customChanged=true;}
    }
    if(customChanged)customizationChanged();
    setPalette();
    if (!processor.frozen.load()) {
        meterTime+=elapsed;
        auto next=processor.analyzer.snapshot();
        if(next.resetGeneration!=frame.resetGeneration || next.sampleRate!=frame.sampleRate)
            clearVisualHistory();
        float maximum=0.075f;
        for(float sample:next.left) maximum=std::max(maximum,std::abs(sample));
        for(float sample:next.right) maximum=std::max(maximum,std::abs(sample));
        // This is display gain only: fast expansion, quiet release prevents
        // normalization from making the stereo field jump between snapshots.
        stereoEnvelope=std::max(maximum,stereoEnvelope*float(std::exp(-elapsed/0.8)));
        if(next.sequence!=lastSequence) {
            lastSequence=next.sequence;
            frame=std::move(next);
            updateVisualHistory(frame);
        } else {
            frame=std::move(next);
        }
        heldL=frame.peakHoldL; heldR=frame.peakHoldR;
        while(!spectrumTrails.empty() && meterTime-spectrumTrails.front().time>processor.custom.get("spectrumTrail"))
            spectrumTrails.pop_front();
        while(!stereoTrails.empty() && meterTime-stereoTrails.front().time>processor.custom.get("stereoTrail"))
            stereoTrails.pop_front();
        if(!customizationOpen && !moduleAreas()[4].isEmpty())rebuildStereo();
    }
    if(cachedColours!=currentColours() || cachedTheme!=processor.themeMode.load()) {
        rebuildSpectrogram();
        rebuildStereo();
        repaint();
    }
    updateContinuousWave(now);
    visualSurface.setCamera(processor.cameraYaw.load(),processor.cameraPitch.load(),processor.cameraZoom.load());
    visualSurface.setCustomization(processor.custom);
    const int sceneFps=int(processor.custom.get("sceneFrameRate"));
    const double fps=sceneFps==0?15.:sceneFps==2?60.:30.;
    if(!customizationOpen && now-lastSceneTime>=1.0/fps-.001 && !moduleAreas()[0].isEmpty()){
        lastSceneTime=now;
        visualSurface.update(frame,processor.visualMode.load(),moduleColours("scene"),processor.themeMode.load(),processor.visualGain.load(),processor.frozen.load() || !processor.isReceiving.load());
    }
    if(!customizationOpen && !processor.frozen.load())
        repaint(16,112,getWidth()-32,getHeight()-128);
}
void AuroraAudioProcessorEditor::clearVisualHistory() {
    history.clear();spectrumTrails.clear();stereoTrails.clear();spectrogramAppearanceValid=false;
    continuousWave.clear();projectedWave.clear();waveformRibbon.reset();waveformRetinaRibbon.reset();waveformClock.reset();waveCursor=waveGeneration=waveTotalSamples=0;waveHead=0;waveHasTail=false;
    spectrogramCache=Image(Image::ARGB,historyColumns,historyRows,true);
    stereoCache=Image(Image::ARGB,stereoResolution,stereoResolution,true);
    stereoDensity.assign(size_t(stereoResolution*stereoResolution),0.f);
    for(auto& channel:triggeredWave)channel.clear();
    waveEnvelope=0.125f;waveRange=1.0f;waveTriggered=false;waveTime=meterTime;
    lastSequence=std::numeric_limits<std::uint64_t>::max();
    lastHistoryTime=meterTime-0.05;
    stereoEnvelope=0.075f;
}
void AuroraAudioProcessorEditor::updateContinuousWave(double now) {
    if(!processor.frozen.load()){
        // Provisional samples are real PCM, but not a completed cursor entry.
        // Replace this tail on every poll so it is never counted twice.
        if(waveHasTail && !continuousWave.empty())continuousWave.pop_back();
        waveHasTail=false;
        auto incoming=processor.analyzer.waveformSnapshot(waveCursor);
        if(incoming.generation!=waveGeneration || incoming.sampleRate!=waveSampleRate){
            continuousWave.clear();waveformRibbon.reset();waveformRetinaRibbon.reset();waveformClock.reset();waveCursor=0;
            waveGeneration=incoming.generation;waveSampleRate=incoming.sampleRate;
        }
        waveTotalSamples=incoming.totalSamples;waveBucketSamples=incoming.bucketSamples;
        for(auto& column:incoming.columns){waveCursor=column.sequence;continuousWave.push_back(std::move(column));}
        if(incoming.tail){continuousWave.push_back(*incoming.tail);waveHasTail=true;}
        while(continuousWave.size()>32768 || (!continuousWave.empty() && double(continuousWave.back().endSample-continuousWave.front().endSample)>waveSampleRate*24.25))continuousWave.pop_front();
    }
    const auto available=continuousWave.empty()?std::uint64_t(0):continuousWave.back().endSample;
    waveHead=waveformClock.update(waveTotalSamples,available,waveSampleRate,waveGeneration,now,processor.frozen.load());
}
void AuroraAudioProcessorEditor::updateVisualHistory(const AnalysisFrame& current) {
    updateWaveform(current);
    spectrumTrails.push_back({current.spectrum,meterTime});
    if(spectrumTrails.size()>100) spectrumTrails.pop_front();
    StereoTrace trace; trace.time=meterTime;
    const auto count=std::min(current.left.size(),current.right.size());
    trace.points.reserve(count);
    for(size_t i=0;i<count;++i)
        trace.points.emplace_back((current.left[i]-current.right[i])*0.5f,
                                  -(current.left[i]+current.right[i])*0.5f);
    stereoTrails.push_back(std::move(trace));
    if(stereoTrails.size()>128) stereoTrails.pop_front();

    // UI history is sampled at 20 columns/second. It is deliberately bounded,
    // and missed UI intervals are left blank rather than invented/interpolated.
    // meterTime stops while frozen, so resuming does not erase the held history.
    const double delta=meterTime-lastHistoryTime;
    if(delta<0.05) return;
    const int advance=jlimit(1,historyColumns,int(std::min(double(historyColumns),std::floor(delta/0.05))));
    lastHistoryTime=meterTime-std::fmod(delta,0.05);
    for(int i=1;i<advance;++i) history.emplace_back();
    history.push_back(current.spectrum);
    while(history.size()>size_t(historyColumns)) history.pop_front();
    if(advance<historyColumns)
        spectrogramCache.moveImageSection(0,0,advance,0,historyColumns-advance,historyRows);
    spectrogramCache.clear({historyColumns-advance,0,advance,historyRows},Colours::transparentBlack);
    writeSpectrogramColumn(historyColumns-1,current.spectrum);
}
void AuroraAudioProcessorEditor::writeSpectrogramColumn(int column,const std::vector<float>& bins) {
    Image::BitmapData pixels(spectrogramCache,column,0,1,historyRows,Image::BitmapData::writeOnly);
    if(bins.empty())return;
    const float floor=processor.custom.get("spectrogramFloor"),ceiling=processor.custom.get("spectrogramCeiling");
    for(int row=0;row<historyRows;row++) {
        const float x=(float(row)+0.5f)/float(historyRows);
        const float db=spectralPeak(bins,frame.sampleRate,x,1.0f/float(historyRows));
        const auto colour=spectralLut[size_t(jlimit(0,255,int(std::round(unit((db-floor)/(ceiling-floor))*255.f))))];
        pixels.setPixelColour(0,historyRows-1-row,colour);
    }
}
void AuroraAudioProcessorEditor::rebuildSpectrogram() {
    cachedColours=currentColours(); cachedTheme=processor.themeMode.load();
    const auto stops=moduleColours("spectrogram");
    const std::array<float,13> appearance {float(stops[0]),float(stops[1]),float(stops[2]),float(cachedTheme),
        float(paper.getARGB()&0xffffff),float(ink.getARGB()&0xffffff),processor.custom.get("interfaceOverride"),
        processor.custom.get("panelColour"),processor.custom.get("paletteMiddle"),processor.custom.get("spectrogramFloor"),
        processor.custom.get("spectrogramCeiling"),processor.custom.get("spectrogramContrast"),float(frame.sampleRate)};
    // History appends update individual columns. Unrelated settings must not
    // rebuild the entire retained image during slider drags.
    if(spectrogramAppearanceValid && spectrogramAppearance==appearance)return;
    spectrogramAppearance=appearance;spectrogramAppearanceValid=true;
    const bool dark=isDark();
    const auto colours=moduleColours("spectrogram");const auto accent=rgb(colours[1]),secondary=rgb(colours[2]);
    const auto base=processor.custom.get("interfaceOverride")>.5f?rgb(std::uint32_t(processor.custom.get("panelColour"))):isFlightDeck()?rgb(0x0f1715):paper.interpolatedWith(ink,dark?.06f:.055f);
    // A 256-entry map recolours retained dB values, including existing history.
    // Display contrast is independent of spectral measurements and all three
    // user stops remain part of the map even when their luminances are equal.
    const float contrast=processor.custom.get("spectrogramContrast"),middle=.18f+.68f*processor.custom.get("paletteMiddle");
    for(size_t i=0;i<spectralLut.size();++i) {
        const float v=std::pow(float(i)/255.f,contrast);
        Colour colour;
        if(v<.18f) colour=base.interpolatedWith(rgb(colours[0]),v/.18f);
        else if(v<middle) colour=rgb(colours[0]).interpolatedWith(accent,(v-.18f)/(middle-.18f));
        else colour=accent.interpolatedWith(secondary,(v-middle)/(1.f-middle));
        colour=colour.interpolatedWith(dark?Colours::white:ink,unit((v-.76f)/.24f)*(isFlightDeck()?.18f:.62f));
        spectralLut[i]=colour.withAlpha(v<.015f?0.f:std::pow(v,.56f));
    }
    spectrogramCache=Image(Image::ARGB,historyColumns,historyRows,true);
    int column=historyColumns-int(history.size());
    for(const auto& bins:history) writeSpectrogramColumn(column++,bins);
}
void AuroraAudioProcessorEditor::updateWaveform(const AnalysisFrame& current) {
    const size_t available=std::min(current.left.size(),current.right.size());
    if(available<4) {for(auto& channel:triggeredWave)channel.clear();return;}
    float maximum=0.f;double energyL=0,energyR=0;
    for(size_t i=0;i<available;++i) {
        maximum=std::max(maximum,std::max(std::abs(current.left[i]),std::abs(current.right[i])));
        energyL+=double(current.left[i])*current.left[i];energyR+=double(current.right[i])*current.right[i];
    }
    waveEnvelope=std::max(maximum,waveEnvelope*float(std::exp(-(meterTime-waveTime)/1.2)));
    waveTime=meterTime;
    // Explicit amplitude ticks disclose display scaling; samples are never
    // normalised independently, so stereo amplitude differences stay intact.
    waveRange=.125f;
    while(waveRange<waveEnvelope*1.04f && waveRange<128.f)waveRange*=2.f;
    const auto& trigger=energyR>energyL*1.25?current.right:current.left;
    const size_t count=std::min(size_t(768),available-1);
    const size_t search=available-count-1;
    size_t start=search/2;
    bool armed=false;waveTriggered=false;
    const float hysteresis=maximum*.08f;
    for(size_t i=0;i<=search;++i) {
        if(trigger[i]<-hysteresis)armed=true;
        if(i>0 && armed && trigger[i-1]<=0 && trigger[i]>0 && maximum>.0001f) {
            start=i;waveTriggered=true;break;
        }
    }
    for(size_t ch=0;ch<2;++ch) {
        const auto& data=ch==0?current.left:current.right;
        auto& output=triggeredWave[ch];output.resize(count);
        // Keep original samples: fractional resampling here could hide a
        // single-sample peak before the per-pixel min/max envelope sees it.
        std::copy_n(data.begin()+std::ptrdiff_t(start),count,output.begin());
    }
}
void AuroraAudioProcessorEditor::rebuildStereo() {
    constexpr int n=stereoResolution;
    std::fill(stereoDensity.begin(),stereoDensity.end(),0.f);
    const float normal=processor.custom.get("stereoAutoGain")>.5f?std::max(.075f,stereoEnvelope):1.f;
    const float scale=.46f/normal*float(n-1)*processor.custom.get("stereoGain");
    const float trail=processor.custom.get("stereoTrail");
    for(const auto& trace:stereoTrails) {
        const float age=float(meterTime-trace.time);
        const float weight=std::exp(-age/(trail*.31f))*unit(1.f-age/trail);
        if(weight<=.001f)continue;
        for(const auto& point:trace.points) {
            const float x=float(n-1)*.5f+point.x*scale,y=float(n-1)*.5f+point.y*scale;
            if(x<1 || y<1 || x>=float(n-2) || y>=float(n-2))continue;
            const int ix=int(x),iy=int(y);const float fx=x-float(ix),fy=y-float(iy);
            // Bilinear additive splats preserve repeated-point density. A
            // single filled Path would merge overlapping dots into one shape.
            stereoDensity[size_t(iy*n+ix)]+=weight*(1-fx)*(1-fy);
            stereoDensity[size_t(iy*n+ix+1)]+=weight*fx*(1-fy);
            stereoDensity[size_t((iy+1)*n+ix)]+=weight*(1-fx)*fy;
            stereoDensity[size_t((iy+1)*n+ix+1)]+=weight*fx*fy;
        }
    }
    stereoCache.clear(stereoCache.getBounds(),Colours::transparentBlack);
    Image::BitmapData pixels(stereoCache,Image::BitmapData::writeOnly);
    const bool dark=isDark();
    const auto colours=moduleColours("stereo");
    const auto low=rgb(colours[0]).interpolatedWith(rgb(colours[1]),.65f).interpolatedWith(ink,dark?.06f:.4f);
    const auto high=rgb(colours[2]).interpolatedWith(ink,dark?.04f:.5f);
    const float brightness=processor.custom.get("stereoBrightness");
    for(int y=1;y<n-1;++y)for(int x=1;x<n-1;++x) {
        const size_t i=size_t(y*n+x);
        const float density=stereoDensity[i];
        const float halo=(stereoDensity[i-1]+stereoDensity[i+1]+stereoDensity[i-n]+stereoDensity[i+n])*.085f;
        const float alpha=1.f-std::exp(-(density+halo)*.85f*brightness);
        if(alpha>.004f)pixels.setPixelColour(x,y,low.interpolatedWith(high,unit(density*.32f)).withAlpha(alpha));
    }
}
void AuroraAudioProcessorEditor::text(Graphics& g,const String& s,Rectangle<float> r,float size,Colour c,bool bold,int align) {
    g.setColour(c); g.setFont(Font(FontOptions(auroraTypography::sansTypeface(),size,bold?Font::bold:Font::plain)));
    g.drawText(s,r,Justification(align),true);
}
void AuroraAudioProcessorEditor::digits(Graphics& g,const String& s,Rectangle<float> r,float size,Colour c,int align) {
    g.setColour(c);g.setFont(Font(FontOptions(auroraTypography::monoTypeface(),size,Font::plain)));
    g.drawText(s,r,Justification(align),true);
}
String AuroraAudioProcessorEditor::number(float v,int decimals) { return std::isfinite(v)&&v>-119 ? String(v,decimals) : String(CharPointer_UTF8("\xe2\x88\x92\xe2\x88\x9e")); }
void AuroraAudioProcessorEditor::panel(Graphics& g,Rectangle<float> r,const String& title) {
    if(isFlightDeck()) {
        const auto face=consoleShape(r,6);
        g.setColour(processor.custom.get("interfaceOverride")>.5f?rgb(std::uint32_t(processor.custom.get("panelColour"))):rgb(0x0f1715));g.fillPath(face);
        g.setColour(line);g.strokePath(face,PathStrokeType(.85f));
        g.setColour(rgb(0x617367).withAlpha(.48f));
        g.drawLine(r.getX()+7,r.getY()+1,r.getX()+35,r.getY()+1,.8f);
        g.drawLine(r.getRight()-1,r.getBottom()-35,r.getRight()-1,r.getBottom()-7,.8f);
        if(title.isNotEmpty()) {
            g.setColour(processor.custom.get("interfaceOverride")>.5f?rgb(std::uint32_t(processor.custom.get("panelColour"))).interpolatedWith(ink,.05f):rgb(0x1c2822));g.fillRect(r.getX()+1,r.getY()+1,r.getWidth()-2,30.f);
            g.setColour(accent.withAlpha(.8f));g.fillRect(r.getX()+15,r.getY()+15,4.f,4.f);
            digits(g,title.toUpperCase(),{r.getX()+26,r.getY()+9,r.getWidth()-40,17},10,ink.withAlpha(.86f));
            g.setColour(line.withMultipliedAlpha(.72f));g.drawHorizontalLine(int(r.getY()+31),r.getX()+1,r.getRight()-1);
        }
    } else {
        g.setColour(processor.custom.get("interfaceOverride")>.5f?rgb(std::uint32_t(processor.custom.get("panelColour"))):Colours::white.withAlpha(isDark()?.025f:.28f));g.fillRoundedRectangle(r,7);
        g.setColour(line);g.drawRoundedRectangle(r,7,.7f);
        if(title.isNotEmpty()){
            digits(g,title.toUpperCase(),r.reduced(15,0).withY(r.getY()+10).withHeight(18),10,ink.withAlpha(.85f));
            g.setColour(line.withMultipliedAlpha(.48f));g.drawHorizontalLine(int(r.getY()+32),r.getX()+15,r.getRight()-15);
        }
    }
}
void AuroraAudioProcessorEditor::paint(Graphics& g) {
    g.fillAll(paper);
    if(isFlightDeck()) {
        g.setGradientFill(ColourGradient(processor.custom.get("interfaceOverride")>.5f?paper.brighter(.12f):rgb(0x282e29),0,0,paper,0,110,false));g.fillRect(0,0,getWidth(),108);
        g.setColour(line);g.drawHorizontalLine(54,16.f,float(getWidth()-16));
        g.setColour(rgb(0xbcb79b));g.fillRect(20,53,108,2);
        g.setColour(rgb(0x778b79));g.fillRect(132,53,24,2);
        g.setColour(rgb(0x7d6052));g.fillRect(160,53,12,2);
        g.setColour(ink);g.drawEllipse(22,19,19,19,1.2f);g.drawEllipse(29,19,19,19,1.2f);
        auto brand=Font(FontOptions(auroraTypography::sansTypeface(),24,Font::bold));brand.setExtraKerningFactor(.10f);
        g.setFont(brand);g.drawText("AURORA",59,11,210,35,Justification::centredLeft);
        g.setColour(line);g.drawVerticalLine(521,66,94);g.drawVerticalLine(644,66,94);
    } else {
        g.setGradientFill(ColourGradient(paper.brighter(isDark()?.04f:.18f),0,0,paper.darker(.035f),float(getWidth()),float(getHeight()),false));g.fillAll();
        g.setColour(ink);g.drawEllipse(22,19,19,19,1.1f);g.drawEllipse(29,19,19,19,1.1f);
        text(g,"aurora",{59,8,200,36},27,ink);
    }
    if(customizationOpen){
        g.setColour(line);g.drawRect(customizationDropdown.getBounds().expanded(1),1);
        return;
    }
    const auto colours=currentColours();
    ColourGradient stops(rgb(colours[0]),532,0,rgb(colours[2]),624,0,false);
    stops.addColour(processor.custom.get("paletteMiddle"),rgb(colours[1]));g.setGradientFill(stops);g.fillRect(532.f,92.f,92.f,3.f);
    {
        Graphics::ScopedSaveState saved(g);
        const Rectangle<float> area(16,112,float(getWidth()-32),float(getHeight()-128));g.reduceClipRegion(area.toNearestInt());
        spectrumBounds=spectrogramBounds=waveformBounds=stereoBounds=levelsBounds={};
        const auto modules=moduleAreas();
        for(std::size_t i=0;i<modules.size();++i){
            const auto r=modules[i];if(r.isEmpty() || !r.intersects(area))continue;
            Graphics::ScopedSaveState clipped(g);g.reduceClipRegion(r.toNearestInt());
            switch(i){case 0:field(g,r);break;case 1:spectrum(g,r);break;case 2:spectrogram(g,r);break;case 3:waveform(g,r);break;case 4:stereo(g,r);break;case 5:levels(g,r);break;}
        }
        if(std::all_of(modules.begin(),modules.end(),[](auto r){return r.isEmpty();}))
            text(g,"Choose modules in Customise",area,16,ink,false,Justification::centred);
    }
    if(processor.droppedSamples.load()>0) {
        // A measurement discontinuity must remain discoverable, but ordinary
        // playback carries no diagnostic footer.
        text(g,"Analysis reset after overload",{24,float(getHeight()-22),300,16},9,ink);
    }

}
float AuroraAudioProcessorEditor::binAt(float x,const std::vector<float>& s,float span) const {
    return spectralPeak(s,frame.sampleRate,x,span,processor.custom.get("spectrumMinHz"),processor.custom.get("spectrumMaxHz"));
}
void AuroraAudioProcessorEditor::spectrum(Graphics& g,Rectangle<float> r) {
    spectrumBounds=r;panel(g,r,"Spectrum");
    const auto colours=moduleColours("spectrum");const auto accent=rgb(colours[1]),secondary=rgb(colours[2]);
    const float floor=processor.custom.get("spectrumFloor"),ceiling=processor.custom.get("spectrumCeiling"),range=ceiling-floor;
    const float lower=processor.custom.get("spectrumMinHz"),configuredUpper=processor.custom.get("spectrumMaxHz");const bool grid=processor.custom.get("spectrumGrid")>.5f;
    digits(g,"dBFS",{r.getRight()-53,r.getY()+12,38,13},9,ink.withAlpha(.5f),Justification::centredRight);
    auto a=r.reduced(15).withTrimmedTop(23).withTrimmedBottom(15);
    const float upper=std::max(21.f,std::min(configuredUpper,float(frame.sampleRate*.5)));
    const auto frequencyX=[&](float frequency){return a.getX()+std::log(frequency/lower)/std::log(upper/lower)*a.getWidth();};
    if(grid)for(int db=int(std::floor(ceiling/10))*10;db>=floor;db-=10) {
        const float y=a.getY()+(ceiling-float(db))/range*a.getHeight();
        g.setColour(line.withMultipliedAlpha(db%30==0?.7f:.24f));g.drawHorizontalLine(int(y),a.getX(),a.getRight());
        if(db%30==0)digits(g,String(db),{a.getX()+3,float(db)<=floor?y-13:y+2,30,11},8,ink.withAlpha(.48f));
        g.setColour(line);g.drawLine(a.getRight()-3,y,a.getRight(),y,.7f);
    }
    if(grid)for(float frequency : {50.f,100.f,200.f,500.f,1000.f,2000.f,5000.f,10000.f})if(frequency>=lower && frequency<upper) {
        const float x=frequencyX(frequency);
        g.setColour(line.withMultipliedAlpha(frequency==100 || frequency==1000 || frequency==10000?.65f:.22f));
        g.drawVerticalLine(int(x),a.getY(),a.getBottom());
    }
    const int steps=jlimit(128,1400,int(std::ceil(a.getWidth()*1.5f)));
    const auto makeTrace=[&](const std::vector<float>& bins) {
        Path path;
        for(int i=0;i<=steps;++i) {
            const float x=float(i)/float(steps);
            const float y=a.getBottom()-unit((spectralPeak(bins,frame.sampleRate,x,1.f/float(steps),lower,configuredUpper)-floor)/range)*a.getHeight();
            if(i==0)path.startNewSubPath(a.getX(),y);else path.lineTo(a.getX()+x*a.getWidth(),y);
        }
        return path;
    };
    // These faint curves are recent measurements of the combined spectrum.
    if(processor.custom.get("spectrumTrail")>0)for(size_t i=0;i+1<spectrumTrails.size();++i) {
        const auto& trace=spectrumTrails[i];
        const float fade=unit(1.f-float(meterTime-trace.time)/std::max(.001f,processor.custom.get("spectrumTrail")));
        g.setColour(accent.interpolatedWith(ink,.2f).withAlpha(fade*.17f));
        g.strokePath(makeTrace(trace.bins),PathStrokeType(.8f));
    }
    Path path=makeTrace(frame.spectrum),fill=path;
    fill.lineTo(a.getRight(),a.getBottom());fill.lineTo(a.getX(),a.getBottom());fill.closeSubPath();
    const float fillStrength=processor.custom.get("spectrumFill");
    ColourGradient body(secondary.withAlpha(.25f*fillStrength),0,a.getY(),rgb(colours[0]).withAlpha(.025f*fillStrength),0,a.getBottom(),false);
    body.addColour(processor.custom.get("paletteMiddle"),accent.withAlpha(.19f*fillStrength));g.setGradientFill(body);g.fillPath(fill);
    g.setColour(accent.withAlpha(.1f));g.strokePath(path,PathStrokeType(4.f));
    const auto foreground=accent.interpolatedWith(ink,isDark()?.08f:.52f);
    g.setGradientFill(ColourGradient(secondary.interpolatedWith(ink,.36f),0,a.getY(),foreground,0,a.getBottom(),false));
    g.strokePath(path,PathStrokeType(processor.custom.get("spectrumLine"),PathStrokeType::curved,PathStrokeType::rounded));
    for(float frequency : {lower,std::sqrt(lower*upper),upper}) {
        const String label=frequency>=1000?String(frequency/1000,0)+"k":String(int(frequency));
        const float x=jlimit(a.getX(),a.getRight()-25,frequencyX(frequency)-10);
        digits(g,label,{x,a.getBottom()+5,28,12},8,ink.withAlpha(.6f));
    }
    if(a.contains(hoverPosition)) {
        const float position=unit((hoverPosition.x-a.getX())/a.getWidth());
        const float frequency=lower*std::pow(upper/lower,position),db=binAt(position,frame.spectrum,1.f/float(steps));
        const float y=a.getBottom()-unit((db-floor)/range)*a.getHeight();
        g.setColour(ink.withAlpha(.25f));g.drawVerticalLine(int(hoverPosition.x),a.getY(),a.getBottom());
        g.setColour(paper);g.fillEllipse(hoverPosition.x-3,y-3,6,6);g.setColour(ink);g.drawEllipse(hoverPosition.x-3,y-3,6,6,1);
        const String readout=String(int(std::round(frequency)))+" Hz  "+number(db)+" dB";
        auto box=Rectangle<float>(a.getX()+3,a.getY()+3,a.getWidth()-6,19);
        g.setColour(paper.withAlpha(.93f));g.fillRoundedRectangle(box,3);
        digits(g,readout,box.reduced(5,0),9,ink,Justification::centredRight);
    }
}
void AuroraAudioProcessorEditor::spectrogram(Graphics& g,Rectangle<float> r) {
    spectrogramBounds=r;panel(g,r,"Spectrogram");
    auto a=r.reduced(15).withTrimmedTop(23).withTrimmedBottom(16);
    g.setColour(ink.withAlpha(.045f));g.fillRoundedRectangle(a,2);
    g.setColour(Colours::white);g.setImageResamplingQuality(Graphics::mediumResamplingQuality);
    const int columns=jlimit(1,historyColumns,int(processor.custom.get("spectrogramSeconds")*20));
    const auto image=spectrogramCache.getClippedImage({historyColumns-columns,0,columns,historyRows});
    if(processor.custom.get("spectrogramReverse")>.5f){Graphics::ScopedSaveState saved(g);g.addTransform(AffineTransform(-1,0,a.getX()+a.getRight(),0,1,0));g.drawImage(image,a,RectanglePlacement::stretchToFit);}
    else g.drawImage(image,a,RectanglePlacement::stretchToFit);
    const float upper=std::max(21.f,std::min(20000.f,float(frame.sampleRate*.5)));
    if(processor.custom.get("spectrogramGrid")>.5f)for(float frequency : {100.f,1000.f,10000.f})if(frequency<upper) {
        const float y=a.getBottom()-std::log(frequency/20.f)/std::log(upper/20.f)*a.getHeight();
        g.setColour(ink.withAlpha(.1f));g.drawHorizontalLine(int(y),a.getX(),a.getRight());
        const String label=frequency>=1000?String(int(frequency/1000))+"k":String(int(frequency));
        const auto box=Rectangle<float>(a.getRight()-31,y-6,29,12);
        g.setColour(paper.withAlpha(.78f));g.fillRoundedRectangle(box,2);
        digits(g,label,box.reduced(3,0),8,ink.withAlpha(.7f),Justification::centredRight);
    }
    if(processor.custom.get("spectrogramGrid")>.5f)for(int i=1;i<4;++i) {
        g.setColour(ink.withAlpha(.065f));g.drawVerticalLine(int(a.getX()+float(i)*a.getWidth()*.25f),a.getY(),a.getBottom());
    }
    digits(g,processor.custom.get("spectrogramReverse")>.5f?"now":String(int(processor.custom.get("spectrogramSeconds")))+" s",{a.getX(),a.getBottom()+5,45,12},8,ink.withAlpha(.55f));
    digits(g,processor.custom.get("spectrogramReverse")>.5f?String(int(processor.custom.get("spectrogramSeconds")))+" s":"now",{a.getRight()-30,a.getBottom()+5,30,12},8,ink.withAlpha(.55f),Justification::centredRight);
    const auto legend=Rectangle<float>(r.getRight()-76,r.getY()+17,61,3);
    ColourGradient map(spectralLut[28],legend.getX(),0,spectralLut[255],legend.getRight(),0,false);
    map.addColour(.5,spectralLut[140]);g.setGradientFill(map);g.fillRoundedRectangle(legend,1.5f);
}
void AuroraAudioProcessorEditor::waveform(Graphics& g,Rectangle<float> r) {
    if(usesScope()){scope(g,r);return;}
    waveformBounds=r;panel(g,r,"Waveform");
    const double seconds=waveformSeconds();
    const auto a=r.reduced(15).withTrimmedTop(27).withTrimmedBottom(18);
    const double span=waveSampleRate*seconds,start=waveHead-span;
    const int channels=int(processor.custom.get("waveChannels"));
    const bool split=channels==0;const float laneH=a.getHeight()*(split?.5f:1.f),half=laneH*.40f;
    const auto visibleChannel=[&](int ch){return channels==0 || channels==3 || (channels==1 && ch==0) || (channels==2 && ch==1);};
    const auto centre=[&](int ch){return a.getY()+laneH*(split?float(ch)+.5f:.5f);};
    if(processor.custom.get("waveGrid")>.5f)for(int ch=0;ch<2;++ch){
        if(!visibleChannel(ch))continue;const float cy=centre(ch);
        g.setColour(line.withMultipliedAlpha(.38f));g.drawHorizontalLine(int(cy),a.getX(),a.getRight());
        for(float level:{-1.f,1.f}){g.setColour(line.withMultipliedAlpha(.16f));g.drawHorizontalLine(int(cy+level*half),a.getX(),a.getRight());}
    }
    // Time guides are carried by the same sample clock as the audio, so both
    // stereo lanes and their labels move together without integer-pixel steps.
    const double tick=seconds/4.0;
    if(processor.custom.get("waveGrid")>.5f)for(double t=std::ceil(start/waveSampleRate/tick)*tick;t<=waveHead/waveSampleRate;t+=tick){
        const float x=a.getX()+float((t*waveSampleRate-start)/span)*a.getWidth();
        g.setColour(line.withMultipliedAlpha(.22f));g.drawVerticalLine(int(x),a.getY(),a.getBottom());
    }
    const float gain=float(1<<jlimit(0,2,processor.waveformZoom.load()))*processor.custom.get("waveGain");
    {
        Graphics::ScopedSaveState saved(g);g.reduceClipRegion(a.toNearestInt());
        const double pixelScale=std::max(1.0,double(g.getInternalContext().getPhysicalPixelScaleFactor()));
        const auto projection=projectWaveform(continuousWave,waveHead,span,a.getWidth()*pixelScale,waveBucketSamples,projectedWave);
        // Host display and native 1x snapshots may paint the same editor at
        // different densities. Keep both caches so neither invalidates the other.
        auto& ribbon=pixelScale>1.25?waveformRetinaRibbon:waveformRibbon;
        ribbon.draw(g,a,projectedWave,waveHead,span,projection.gridSamples,moduleColours("wave"),
                            processor.waveformColour.load(),processor.waveformStyle.load(),processor.themeMode.load(),gain,&processor.custom);
        // Full-scale flags use raw PCM and exact event times, regardless of
        // display magnification. Cropping at 2x/4x is never reported as clipping.
        for(const auto& group:projectedWave)for(std::size_t ch=0;ch<2;++ch){
            if(!visibleChannel(int(ch)))continue;const float cy=centre(int(ch));
            const auto markPeak=[&](std::uint64_t sample,float y){
                if(double(sample)<start || double(sample)>=waveHead)return;
                const float x=a.getX()+float((double(sample)-start)/span)*a.getWidth();
                g.setColour(rgb(std::uint32_t(processor.custom.get("clipColour"))));g.fillRect(x-float(.75/pixelScale),y,float(1.5/pixelScale),float(1.5/pixelScale));
            };
            if(group.maximum[ch]>=1.f)markPeak(group.maximumSample[ch],cy-half);
            if(group.minimum[ch]<=-1.f)markPeak(group.minimumSample[ch],cy+half-float(1.5/pixelScale));
        }
    }
    if(processor.custom.get("waveLabels")<.5f)return;
    digits(g,"-"+String(seconds,seconds==std::floor(seconds)?0:2)+" s",{a.getX(),a.getBottom()+6,45,12},8,ink.withAlpha(.55f));
    if(r.getWidth()>=300)digits(g,String(CharPointer_UTF8("±"))+String(1.f/gain,5).trimCharactersAtEnd("0").trimCharactersAtEnd("."),{a.getCentreX()-24,a.getBottom()+6,48,12},8,ink.withAlpha(.5f),Justification::centred);
    digits(g,"0 s",{a.getRight()-30,a.getBottom()+6,30,12},8,ink.withAlpha(.55f),Justification::centredRight);
    // A fixed, explicitly labelled amplitude axis never pumps as events leave.
    for(int ch=0;ch<2;++ch){
        if(!visibleChannel(ch))continue;const float cy=centre(ch);
        auto tag=Rectangle<float>(a.getX()+2+(channels==3?float(ch)*18.f:0.f),cy-half-2,16,13);
        g.setColour(paper.withAlpha(.88f));g.fillRect(tag);digits(g,ch==0?"L":"R",tag,9,ink,Justification::centred);
    }
}
void AuroraAudioProcessorEditor::scope(Graphics& g,Rectangle<float> r) {
    waveformBounds=r;panel(g,r,"Waveform");
    const auto colours=moduleColours("wave");const auto accent=rgb(colours[1]),secondary=rgb(colours[2]);
    const size_t samples=triggeredWave[0].size();
    const float milliseconds=samples>1?float(samples-1)/float(frame.sampleRate)*1000.f:0.f;
    const bool showLabels=processor.custom.get("waveLabels")>.5f;
    if(showLabels)digits(g,String(milliseconds,1)+" ms",{r.getRight()-80,r.getBottom()-14,65,12},8,ink.withAlpha(.5f),Justification::centredRight);
    auto a=r.reduced(15).withTrimmedTop(25).withTrimmedBottom(10);
    const int channels=int(processor.custom.get("waveChannels"));
    const bool split=channels==0;
    const float halfHeight=a.getHeight()*(split?.215f:.43f);
    const float range=waveRange/processor.custom.get("waveGain");
    const float fill=processor.custom.get("waveFill"),crest=processor.custom.get("waveCrest");
    const int columns=std::max(2,int(std::ceil(a.getWidth())));
    for(int ch=0;ch<2;++ch) {
        if((channels==1 && ch==1)||(channels==2 && ch==0))continue;
        const auto& data=triggeredWave[size_t(ch)];
        const float cy=a.getY()+a.getHeight()*(split?(ch==0?.25f:.75f):.5f);
        if(processor.custom.get("waveGrid")>.5f && (channels!=3 || ch==0)){
        for(int grid=0;grid<=4;++grid) {
            const float x=a.getX()+float(grid)*a.getWidth()*.25f;
            g.setColour(line.withMultipliedAlpha(.42f));g.drawLine(x,cy-halfHeight,x,cy+halfHeight,.5f);
        }
        for(float level : {-1.f,-.5f,0.f,.5f,1.f}) {
            const float y=cy-level*halfHeight;
            g.setColour(line.withMultipliedAlpha(level==0?.9f:.34f));g.drawLine(a.getX(),y,a.getRight(),y,level==0?.8f:.5f);
        }
        }
        Path trace,envelope,extrema;
        std::vector<Point<float>> bottom;bottom.reserve(size_t(columns));
        for(int column=0;column<columns && !data.empty();++column) {
            // Common boundaries partition every original sample exactly once
            // when reducing; expansion repeats a sample instead of inventing
            // intermediate extrema. The thin trace is interpolated separately.
            const size_t from=std::min(data.size()-1,size_t(column)*data.size()/size_t(columns));
            const size_t end=std::min(data.size(),std::max(from+1,size_t(column+1)*data.size()/size_t(columns)));
            float minimum=data[from],maximum=minimum;
            for(size_t i=from+1;i<end;++i){minimum=std::min(minimum,data[i]);maximum=std::max(maximum,data[i]);}
            const float x=a.getX()+float(column)/float(columns-1)*a.getWidth();
            const float high=cy-jlimit(-1.f,1.f,maximum/range)*halfHeight;
            const float low=cy-jlimit(-1.f,1.f,minimum/range)*halfHeight;
            const float position=float(column)*float(data.size()-1)/float(columns-1);
            const size_t index=std::min(data.size()-1,size_t(position)),next=std::min(index+1,data.size()-1);
            const float sample=data[index]+(data[next]-data[index])*(position-float(index));
            const float y=cy-jlimit(-1.f,1.f,sample/range)*halfHeight;
            if(column==0){trace.startNewSubPath(x,y);envelope.startNewSubPath(x,high);}else {trace.lineTo(x,y);envelope.lineTo(x,high);}
            extrema.startNewSubPath(x,high);extrema.lineTo(x,low);bottom.emplace_back(x,low);
        }
        for(auto it=bottom.rbegin();it!=bottom.rend();++it)envelope.lineTo(*it);
        envelope.closeSubPath();
        const auto colour=(ch==0?accent:secondary).interpolatedWith(ink,isDark()?.1f:.49f);
        const float alpha=channels==3?(ch==0?.78f:.52f):1.f;
        g.setColour(colour.withAlpha(.17f*fill*alpha));g.fillPath(envelope);
        g.setColour(colour.withAlpha(.12f*crest*alpha));g.strokePath(trace,PathStrokeType(4.f,PathStrokeType::curved,PathStrokeType::rounded));
        g.setColour(colour.withAlpha(jlimit(0.f,1.f,.46f*crest*alpha)));g.strokePath(extrema,PathStrokeType(.8f));
        g.setColour(colour.withAlpha(jlimit(0.f,1.f,crest*alpha)));g.strokePath(trace,PathStrokeType(1.15f,PathStrokeType::curved,PathStrokeType::rounded));
        if(!showLabels)continue;
        auto label=Rectangle<float>(a.getX()+2+(channels==3?float(ch)*18.f:0.f),cy-halfHeight,16,13);
        g.setColour(paper.withAlpha(.88f));g.fillRoundedRectangle(label,2);
        digits(g,ch==0?"L":"R",label,9,ink,Justification::centred);
        const String rangeLabel=String(CharPointer_UTF8("\xc2\xb1"))+String(range,5).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        auto rangeBounds=Rectangle<float>(a.getRight()-54,cy-halfHeight,52,12);
        g.setColour(paper.withAlpha(.88f));g.fillRoundedRectangle(rangeBounds,2);
        digits(g,rangeLabel,rangeBounds,8,ink.withAlpha(.6f),Justification::centredRight);
    }
}
void AuroraAudioProcessorEditor::stereo(Graphics& g,Rectangle<float> r) {
    stereoBounds=r;panel(g,r,"Stereo");
    const auto colours=moduleColours("stereo");const auto accent=rgb(colours[1]);
    digits(g,(frame.correlation>0?"+":"")+String(frame.correlation,2),{r.getRight()-66,r.getY()+11,51,16},11,ink,Justification::centredRight);
    auto a=r.reduced(15).withTrimmedTop(24).withTrimmedBottom(34);
    const float size=std::min(a.getWidth()-14,a.getHeight()-8),cx=a.getCentreX(),cy=a.getCentreY();
    const auto scope=Rectangle<float>(cx-size*.5f,cy-size*.5f,size,size);
    {Graphics::ScopedSaveState saved(g);g.addTransform(AffineTransform::rotation(processor.custom.get("stereoRotation")*pi/180.f,cx,cy));
    if(processor.custom.get("stereoGrid")>.5f){
    for(float radius : {.25f,.5f,1.f}) {
        g.setColour(line.withMultipliedAlpha(radius==1?.95f:.4f));
        g.drawEllipse(cx-size*.5f*radius,cy-size*.5f*radius,size*radius,size*radius,.65f);
    }
    for(int tick=0;tick<24;++tick) {
        const float angle=float(tick)*MathConstants<float>::twoPi/24.f;
        const float inner=tick%3==0?.455f:.48f;
        g.setColour(line.withMultipliedAlpha(tick%3==0?1.f:.6f));
        g.drawLine(cx+std::sin(angle)*size*inner,cy+std::cos(angle)*size*inner,cx+std::sin(angle)*size*.5f,cy+std::cos(angle)*size*.5f,.8f);
    }
    g.setColour(line.withMultipliedAlpha(.65f));
    g.drawLine(cx-size*.5f,cy,cx+size*.5f,cy,.6f);g.drawLine(cx,cy-size*.5f,cx,cy+size*.5f,.6f);
    const float diagonal=size*.35355f;
    g.drawLine(cx-diagonal,cy-diagonal,cx+diagonal,cy+diagonal,.5f);g.drawLine(cx-diagonal,cy+diagonal,cx+diagonal,cy-diagonal,.5f);
    }
    g.setColour(Colours::white);g.setImageResamplingQuality(Graphics::highResamplingQuality);
    g.drawImage(stereoCache,scope,RectanglePlacement::stretchToFit);
    const float diagonal=size*.35355f;
    if(processor.custom.get("stereoGrid")>.5f){
    digits(g,"M",{cx-7,scope.getY()-12,14,12},8,ink.withAlpha(.6f),Justification::centred);
    digits(g,"S",{scope.getRight()+2,cy-6,12,12},8,ink.withAlpha(.6f));
    digits(g,"R",{cx-diagonal-12,cy-diagonal-9,12,12},8,ink.withAlpha(.48f));
    digits(g,"L",{cx+diagonal+2,cy-diagonal-9,12,12},8,ink.withAlpha(.48f));
    }}
    const float y=r.getBottom()-31,left=r.getX()+18,right=r.getRight()-18;
    g.setColour(line);g.drawLine(left,y,right,y,2);
    const float x=left+unit((frame.correlation+1.f)*.5f)*(right-left);
    g.setColour(accent.interpolatedWith(ink,.25f).withAlpha(.65f));g.drawLine((left+right)*.5f,y,x,y,2.2f);
    for(int i=0;i<=4;++i) {const float xx=left+float(i)*(right-left)*.25f;g.setColour(line);g.drawLine(xx,y-3,xx,y+3,.8f);}
    g.setColour(paper);g.fillEllipse(x-3.5f,y-3.5f,7,7);g.setColour(ink);g.drawEllipse(x-3.5f,y-3.5f,7,7,1.1f);
    digits(g,"-1",{left-3,y+8,28,11},8,ink.withAlpha(.6f));
    digits(g,"0",{(left+right)*.5f-10,y+8,20,11},8,ink.withAlpha(.4f),Justification::centred);
    digits(g,"+1",{right-25,y+8,28,11},8,ink.withAlpha(.6f),Justification::centredRight);
}
void AuroraAudioProcessorEditor::levels(Graphics& g,Rectangle<float> r) {
    levelsBounds=r;panel(g,r,"Levels");
    const auto colours=moduleColours("levels");const auto accent=rgb(colours[1]),secondary=rgb(colours[2]);
    const float floor=processor.custom.get("levelsFloor"),range=-floor;
    const bool loudness=processor.custom.get("levelsLoudness")>.5f;
    digits(g,"dBFS",{r.getRight()-56,r.getY()+12,40,13},9,ink.withAlpha(.5f),Justification::centredRight);
    const bool compact=r.getHeight()<590;
    const float meterH=!loudness?std::max(48.f,r.getHeight()-92.f):compact?std::max(48.f,r.getHeight()-151.f):std::min(340.f,r.getHeight()*.47f);
    const float y=r.getY()+39,w=std::min(42.f,(r.getWidth()-77.f)*.5f),x=r.getCentreX()-w-3;
    const auto warning=rgb(std::uint32_t(processor.custom.get("clipColour")));
    for(int ch=0;ch<2;++ch) {
        const float xx=x+float(ch)*(w+6),peak=ch==0?frame.peakL:frame.peakR,rms=ch==0?frame.rmsL:frame.rmsR,hold=ch==0?heldL:heldR;
        g.setColour(rgb(colours[0]).interpolatedWith(ink,.1f).withAlpha(.20f));g.fillRoundedRectangle(xx,y,w,meterH,2);
        const auto colour=(ch==0?accent:secondary).interpolatedWith(ink,isDark()?.03f:.31f);
        for(int step=0;step<int(range);++step) {
            const float threshold=floor+float(step);
            const float segmentY=y+meterH-(float(step)+1.f)*meterH/range,segmentH=std::max(.5f,meterH/range-(processor.custom.get("levelsSegments")>.5f?.65f:0.f));
            if(peak<threshold)continue;
            const float opacity=processor.custom.get("levelsRms")<.5f?.85f:threshold<rms?.94f:.36f;
            g.setColour((threshold>=processor.custom.get("levelsWarning")?warning:colour).withAlpha(opacity));g.fillRect(xx,segmentY,w,segmentH);
        }
        if(peak>floor) {const float py=y+meterH*(1.f-unit((peak-floor)/range));g.setColour(colour);g.drawLine(xx,py,xx+w,py,1.3f);}
        if(processor.custom.get("levelsHold")>.5f && hold>-119) {const float hy=y+meterH*(1.f-unit((hold-floor)/range));g.setColour(hold>=0?warning:ink);g.drawLine(xx-2,hy,xx+w+2,hy,1.1f);}
        g.setColour(hold>=0?warning:line.withMultipliedAlpha(.35f));g.fillRoundedRectangle(xx,y-6,w,2.5f,1.2f);
        digits(g,ch==0?"L":"R",{xx,y+meterH+4,w,12},9,ink.withAlpha(.6f),Justification::centred);
        digits(g,number(peak),{xx-8,y+meterH+19,w+16,17},12,ink,Justification::centred);
    }
    for(int db=0;db>=int(floor);db-=6) {
        const float yy=y+float(-db)/range*meterH;
        g.setColour(line.withMultipliedAlpha(.65f));g.drawLine(x-5,yy,x-2,yy,.7f);g.drawLine(x+2*w+8,yy,x+2*w+11,yy,.7f);
        if(db%12==0)digits(g,String(db),{r.getX()+7,yy-6,std::max(22.f,x-r.getX()-15),12},8,ink.withAlpha(.55f),Justification::centredRight);
    }
    if(!loudness)return;
    float row=y+meterH+47;
    g.setColour(line.withMultipliedAlpha(.7f));g.drawHorizontalLine(int(row-4),r.getX()+15,r.getRight()-15);
    if(compact) {
        text(g,"Integrated",{r.getX()+15,row,r.getWidth()-65,12},9,ink.withAlpha(.6f));
        digits(g,"LUFS",{r.getRight()-49,row,34,12},8,ink.withAlpha(.5f),Justification::centredRight);
        digits(g,number(frame.integratedLUFS),{r.getX()+15,row+11,r.getWidth()-30,30},25,ink);
        const float footer=r.getBottom()-20;
        digits(g,"M "+number(frame.momentaryLUFS),{r.getX()+15,footer,r.getWidth()*.5f-16,12},8,ink.withAlpha(.7f));
        digits(g,"S "+number(frame.shortTermLUFS),{r.getCentreX(),footer,r.getWidth()*.5f-15,12},8,ink.withAlpha(.7f),Justification::centredRight);
        return;
    }
    text(g,"Integrated",{r.getX()+15,row+7,r.getWidth()-30,16},11,ink.withAlpha(.7f));
    const float valueSize=jlimit(29.f,42.f,(r.getWidth()-32.f)/4.6f);
    digits(g,number(frame.integratedLUFS),{r.getX()+12,row+24,r.getWidth()-27,51},valueSize,ink);
    digits(g,"LUFS",{r.getX()+16,row+75,r.getWidth()-32,15},9,ink.withAlpha(.5f));
    row+=108;
    for(const auto& item : std::initializer_list<std::pair<String,float>>{{"Momentary",frame.momentaryLUFS},{"Short term",frame.shortTermLUFS}}) {
        g.setColour(line.withMultipliedAlpha(.6f));g.drawHorizontalLine(int(row),r.getX()+15,r.getRight()-15);
        text(g,item.first,{r.getX()+15,row+8,r.getWidth()-30,13},10,ink.withAlpha(.62f));
        digits(g,number(item.second),{r.getX()+14,row+22,r.getWidth()-59,29},23,ink);
        digits(g,"LUFS",{r.getRight()-49,row+30,34,12},8,ink.withAlpha(.5f),Justification::centredRight);
        row+=61;
    }
}
void AuroraAudioProcessorEditor::mouseMove(const MouseEvent& event) {hoverPosition=event.position;repaint(spectrumBounds.toNearestInt());repaint(waveformBounds.toNearestInt());}
void AuroraAudioProcessorEditor::mouseExit(const MouseEvent&) {hoverPosition={-1,-1};repaint(spectrumBounds.toNearestInt());repaint(waveformBounds.toNearestInt());}
String AuroraAudioProcessorEditor::getTooltip() {
    const auto point=getMouseXYRelative().toFloat();
    if(point.y<112)return {};
    if(processor.layoutMode.load()!=2 && spectrumBounds.contains(point))return "Combined stereo spectrum. Hover for frequency and dBFS; faint traces retain recent measured curves.";
    if(processor.layoutMode.load()!=2 && spectrogramBounds.contains(point))return "Sampled spectral history with a configurable time window, dBFS range, contrast and palette. Missed display intervals remain blank.";
    if(waveformBounds.contains(point) && !usesScope()){
        const auto plot=waveformBounds.reduced(15).withTrimmedTop(27).withTrimmedBottom(18);
        const double seconds=waveformSeconds();
        const double at=waveHead-waveSampleRate*seconds+(point.x-plot.getX())/plot.getWidth()*waveSampleRate*seconds;
        if(plot.contains(point))for(const auto& group:projectedWave){
            if(at<double(group.sampleStart) || at>=double(group.sampleEnd))continue;
            const auto db=[](double v){return v>0?String(20.0*std::log10(v),2):String(CharPointer_UTF8("−∞"));};
            String result=String((double(group.sampleStart)-waveHead)/waveSampleRate,3)+" s  |  "+String(int(group.sampleCount))+" samples";
            for(size_t ch=0;ch<2;++ch){
                const int channels=int(processor.custom.get("waveChannels"));
                if((channels==1 && ch==1)||(channels==2 && ch==0))continue;
                const double peak=std::max(std::abs(double(group.minimum[ch])),std::abs(double(group.maximum[ch])));
                const auto peakSample=std::abs(group.minimum[ch])>std::abs(group.maximum[ch])?group.minimumSample[ch]:group.maximumSample[ch];
                result+=String("\n")+(ch==0?"L":"R")+"  Peak "+db(peak)+" dBFS  |  RMS "+db(group.rms[ch])+" dBFS";
                result+="\n   Peak at "+String((double(peakSample)-waveHead)/waveSampleRate,6)+" s";
                if(processor.waveformColour.load()==0)result+="  |  Band energy "+String(group.bandPower[ch][0]*100.0,1)+" / "+String(group.bandPower[ch][1]*100.0,1)+" / "+String(group.bandPower[ch][2]*100.0,1)+"%";
            }
            return result;
        }
        return "Signed sample-peak envelope with measured PCM density, RMS and filtered-band detail. The labelled amplitude scale is fixed; red marks actual full-scale sample positions. Hover the history for measured peak, RMS and sample timing. Bands shows relative low/mid/high filtered energy; Palette maps RMS level. Freeze to inspect.";
    }
    if(waveformBounds.contains(point))return String("L and R share a rising-edge trigger and the same amplitude scale. Envelope preserves sample extremes. ")+(waveTriggered?"Trigger locked. ":"No trigger in this snapshot. ")+"The displayed window is up to 768 samples; amplitude ticks show the current scale.";
    if(processor.layoutMode.load()!=2 && stereoBounds.contains(point))return "M and S mark the mid/side axes; L/R diagonals show one-sided audio. Rotation, persistence and display gain are adjustable. Brighter points mean greater sample density. Correlation runs from -1 to +1.";
    if(levelsBounds.contains(point))return "Sample peak and RMS in dBFS; the thin hold marks the maximum since reset. Top indicators retain samples at or above 0 dBFS. Loudness: momentary 400 ms, short term 3 s, and a gated integrated estimate since reset.";
    return {};
}
void AuroraAudioProcessorEditor::field(Graphics& g,Rectangle<float> r) {
    panel(g,r,{});
}
