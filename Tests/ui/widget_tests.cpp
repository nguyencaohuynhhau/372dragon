#include "CEGUISystem.h"
#include "CEGUIRenderer.h"
#include "CEGUITexture.h"
#include "CEGUIDefaultResourceProvider.h"
#include "CEGUIExceptions.h"
#include "CEGUIWindowManager.h"
#include "CEGUIWindowFactoryManager.h"
#include "CEGUIImagesetManager.h"
#include "CEGUIImageset.h"
#include "CEGUIAnimateManager.h"
#include "CEGUIAnimate.h"
#include "FalagardLoginWidgets.h"
#include "FalagardStar.h"
#include "FalagardSoftKeyBoard.h"
#include "FalagardAnimate.h"
#include "FalBoundWindow.h"
#include "FalDragTitle.h"
#include "FalagardSelfFitWindow.h"
#include "FalTooltip.h"
#include "FalCombobox.h"
#include "FalChatChannel.h"
#include "FalChatHistory.h"
#include "FalChatBoard.h"
#include "FalagardSuperProgress.h"
#include "elements/CEGUIScrollbar.h"
#include "elements/CEGUIPopupMenu.h"
#include "FalMenuItem.h"
#include "FalagardMapImage.h"
#include "FalagardMeshWindow.h"
// CYS-HUD widgets
#include "FalagardNotSuperList.h"
#include "FalagardListBoxEx.h"
#include "FalagardParticleSystem.h"
#include "FalagardScrollInfo.h"
#include "FalagardAchievementWindow.h"
#include "FalagardDragDropTarget.h"
#include "FalagardSuperBarButton.h"
#include "FalagardCyWidgets.h"
#include "FalSuperTooltip.h"
#include "CysStoredProperties.h"
#include "CEGUIScriptModule.h"
#include "elements/CEGUIListbox.h"
#include "elements/CEGUIMultiColumnList.h"
#include "elements/CEGUIListboxTextItem.h"
#include "CysImeClipboard.h"
#include "CEGUIFontManager.h"
#include "CEGUIIme.h"
#include "falagard/CEGUIFalWidgetLookManager.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>
#include <limits>
#include <cmath>
using namespace CEGUI;
static int passed = 0, failed = 0;
static std::vector<int> clickSounds;
static void captureClickSound(int id) { clickSounds.push_back(id); }
struct MeshShownCapture {
    int count = 0;
    Window* window = nullptr;
    bool receive(const EventArgs& event) {
        ++count; window = static_cast<const WindowEventArgs&>(event).window;
        return false;
    }
};
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void test(const char* name, const std::function<void()>& body)
{
    try { body(); ++passed; std::printf("PASS %s\n", name); }
    catch (const Exception& e) { ++failed; std::printf("FAIL %s: %s\n", name, e.getMessage().c_str()); }
    catch (const std::exception& e) { ++failed; std::printf("FAIL %s: %s\n", name, e.what()); }
}
struct ClickableTestButton : FalagardAnimateButton {
    ClickableTestButton(const String& t,const String& n) : FalagardAnimateButton(t,n) {}
    using Window::onMouseClicked;
};
// Pointer to the protected hover flag, so a real CY item button can be put in hover state without a GUI sheet.
struct HoverAccess : ButtonBase { static bool ButtonBase::* flag() { return &HoverAccess::d_hovering; } };
struct ClickableTestFactory : WindowFactory {
    ClickableTestFactory() : WindowFactory("Falagard/AnimateButton") {}
    Window* createWindow(const String& n) override { return new ClickableTestButton(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct DraggableTestTitle : FalagardDragTitle {
    DraggableTestTitle(const String& t,const String& n) : FalagardDragTitle(t,n) {}
    using FalagardDragTitle::onMouseButtonDown;
    using FalagardDragTitle::onMouseMove;
    using FalagardDragTitle::onMouseButtonUp;
};
struct DraggableTestFactory : WindowFactory {
    DraggableTestFactory() : WindowFactory("Falagard/DragTitle") {}
    Window* createWindow(const String& n) override { return new DraggableTestTitle(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
// Selection tests retain native add/insert/property/item rendering; scroll layout is outside these fixtures.
struct PendingTestCombo : FalagardCombobox {
    PendingTestCombo(const String& t,const String& n) : FalagardCombobox(t,n) {}
    void initialise() override {}
};
struct PendingTestFactory : WindowFactory {
    PendingTestFactory() : WindowFactory("Falagard/Combobox") {}
    Window* createWindow(const String& n) override { return new PendingTestCombo(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct SelectionTestList : Listbox {
    SelectionTestList(const String& t,const String& n) : Listbox(t,n) {}
    Rect getListRenderArea() const override { return Rect(0,0,120,60); }
    Scrollbar* createVertScrollbar(const String&) const override { return nullptr; }
    Scrollbar* createHorzScrollbar(const String&) const override { return nullptr; }
    void cacheListboxBaseImagery() override {}
    void initialise() override {}
    void onListContentsChanged(WindowEventArgs&) override { requestRedraw(); }
    void onSized(WindowEventArgs& e) override { Window::onSized(e); }
};
struct SelectionTestFactory : WindowFactory {
    SelectionTestFactory() : WindowFactory("Fixture/Listbox") {}
    Window* createWindow(const String& n) override { return new SelectionTestList(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
// Fixed native header segments provide column IDs/widths; MCL row mutation,
// selection and cell imagery use the production implementation.
struct SelectionTestHeader : ListHeader {
    ListHeaderSegment left,right;
    explicit SelectionTestHeader(const String& name) : ListHeader("Fixture/ListHeader",name),
        left("Fixture/ListHeaderSegment",name+"/Left"),right("Fixture/ListHeaderSegment",name+"/Right") {
        left.setID(10);right.setID(20);left.setSize(Absolute,Size(60,12));right.setSize(Absolute,Size(60,12));
        d_segments.push_back(&left);d_segments.push_back(&right);d_sortSegment=&left;
    }
    ListHeaderSegment* createNewSegment(const String&) const override { throw std::runtime_error("fixed test columns"); }
    void destroyListSegment(ListHeaderSegment*) const override {}
};
struct SelectionTestHeaderFactory : WindowFactory {
    SelectionTestHeaderFactory() : WindowFactory("Fixture/ListHeader") {}
    Window* createWindow(const String& n) override { return new SelectionTestHeader(n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct SelectionTestSegmentFactory : WindowFactory {
    SelectionTestSegmentFactory() : WindowFactory("Fixture/ListHeaderSegment") {}
    Window* createWindow(const String& n) override { return new ListHeaderSegment(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct SelectionTestMcl : MultiColumnList {
    SelectionTestHeader header;
    SelectionTestMcl(const String& type,const String& name) : MultiColumnList(type,name),header(name+"/Header") { d_header=&header; }
    ~SelectionTestMcl() { resetList_impl(); } // clear cells while the embedded header is still alive
    Rect getListRenderArea() const override { return Rect(0,0,120,100); }
    ListHeader* createListHeader(const String&) const override { return nullptr; }
    Scrollbar* createVertScrollbar(const String&) const override { return nullptr; }
    Scrollbar* createHorzScrollbar(const String&) const override { return nullptr; }
    void cacheListboxBaseImagery() override {}
    void initialise() override {}
    void onListContentsChanged(WindowEventArgs&) override { requestRedraw(); }
    void onSized(WindowEventArgs& e) override { Window::onSized(e); }
};
struct SelectionTestMclFactory : WindowFactory {
    SelectionTestMclFactory() : WindowFactory("Fixture/MultiColumnList") {}
    Window* createWindow(const String& n) override { return new SelectionTestMcl(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct SelectionCell : ListboxTextItem {
    SelectionCell() : ListboxTextItem("") {}
    Size getPixelSize(const Window*) override { return Size(10,12); }
};
struct TestChannel : FalagardChatChannel {
    TestChannel(const String& t,const String& n) : FalagardChatChannel(t,n) {}
    using FalagardChatChannel::onMouseMove;
};
struct TestChannelFactory : WindowFactory {
    TestChannelFactory() : WindowFactory("Falagard/ChatChannel") {}
    Window* createWindow(const String& n) override { return new TestChannel(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct TestScroll : Scrollbar {
    TestScroll(const String& t,const String& n) : Scrollbar(t,n) {}
    void initialise() override {}
    PushButton* createIncreaseButton(const String&) const override { return nullptr; }
    PushButton* createDecreaseButton(const String&) const override { return nullptr; }
    Thumb* createThumb(const String&) const override { return nullptr; }
    void updateThumb() override {}
    float getValueFromThumb() const override { return 0; }
    float getAdjustDirectionFromPoint(const Point&) const override { return 0; }
};
struct TestScrollFactory : WindowFactory {
    TestScrollFactory() : WindowFactory("Fixture/Scrollbar") {}
    Window* createWindow(const String& n) override { return new TestScroll(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct TestPopup : PopupMenu {
    TestPopup(const String& t,const String& n) : PopupMenu(t,n) {}
    void populateRenderCache() override {}
    void sizeToContent_impl() override {}
    Rect getItemRenderArea() const override { return Rect(0,0,getAbsoluteWidth(),getAbsoluteHeight()); }
};
struct TestPopupFactory : WindowFactory {
    TestPopupFactory() : WindowFactory("Fixture/PopupMenu") {}
    Window* createWindow(const String& n) override { return new TestPopup(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct TestHistory : FalagardChatHistory {
    TestHistory(const String& t,const String& n) : FalagardChatHistory(t,n) {}
    void initialise() override {}
    void attachNativeChildren(ParentWindow* parent,Scrollbar* scroll) {
        d_parentWindow=parent;d_vertScrollbar=scroll;
        addChildWindow(parent);addChildWindow(scroll);
        parent->setSize(Absolute,Size(100,50));
    }
    size_t rowCount() const { return m_listChatBoard.size(); }
    using FalagardChatHistory::updateSelf;
};
struct TestHistoryFactory : WindowFactory {
    TestHistoryFactory() : WindowFactory("Fixture/ChatHistory") {}
    Window* createWindow(const String& n) override { return new TestHistory(d_type,n); }
    void destroyWindow(Window* w) override { delete w; }
};
struct MemoryTexture : Texture
{
    MemoryTexture(Renderer* owner, ushort size) : Texture(owner), w(size), h(size) {}
    ushort getWidth() const override { return w; }
    ushort getHeight() const override { return h; }
    void loadFromFile(const String&, const String&) override { throw std::runtime_error("unexpected image load"); }
    void loadFromMemory(const void* data, uint width, uint height) override
    {
        w = static_cast<ushort>(width); h = static_cast<ushort>(height);
        pixels.assign(static_cast<const unsigned*>(data), static_cast<const unsigned*>(data) + width * height);
    }
    ushort w, h;
    std::vector<unsigned> pixels;
};
struct MemoryRenderer : Renderer
{
    int reloadTextures = 0;
    std::vector<Rect> quads; std::vector<ColourRect> colours;
    unsigned long addQuad(const Rect& r, float, const Texture*, const Rect&, const ColourRect& c, QuadSplitMode, UserSharps) override { quads.push_back(r); colours.push_back(c); return static_cast<unsigned long>(quads.size()); }
    void adjustQuad(unsigned long, const Texture*, const Rect&, const ColourRect&) override {}
    void doRender() override {}
    void clearRenderList() override { quads.clear(); colours.clear(); }
    void setQueueingEnabled(bool) override {}
    Texture* createTexture() override { return new MemoryTexture(this,256); }
    Texture* createTexture(const String& name, const String&) override {
        if (name == "reload-fixture.dds") { ++reloadTextures; return new MemoryTexture(this,256); }
        throw std::runtime_error("unexpected image load");
    }
    Texture* createTexture(float size) override { return new MemoryTexture(this, static_cast<ushort>(size)); }
    void destroyTexture(Texture* texture) override { delete texture; }
    void destroyAllTextures() override {}
    bool isQueueingEnabled() const override { return true; }
    float getWidth() const override { return 1024; }
    float getHeight() const override { return 768; }
    Size getSize() const override { return Size(1024,768); }
    Rect getRect() const override { return Rect(0,0,1024,768); }
    uint getMaxTextureSize() const override { return 2048; }
    uint getHorzScreenDPI() const override { return 96; }
    uint getVertScreenDPI() const override { return 96; }
};

int main(int argc,char** argv) {
    if(argc != 2) return 2;
    MemoryRenderer renderer;
    DefaultResourceProvider provider;
    System system(&renderer,&provider);
    std::ofstream("empty.animate",std::ios::binary) << "<AnimateManager/>";
    AnimateManager animations("empty.animate");
    setvbuf(stdout,nullptr,_IONBF,0);
    // All test input is synthetic; never read or write the Windows clipboard.
    auto& wm=WindowManager::getSingleton();
    auto& fm=WindowFactoryManager::getSingleton();
    fm.addFactory(new FalagardAnimateImageFactory);
    fm.addFactory(new ClickableTestFactory);
    fm.addFactory(new FalagardSafeIMEEditBoxFactory);
    fm.addFactory(new FalagardIMEEditBoxFactory);
    fm.addFactory(new FalagardStarFactory);
    fm.addFactory(new FalagardSoftKeyFactory);
    fm.addFactory(new FalagardAnimateFactory);
    fm.addFactory(new FalagardBoundWindowFactory);
    fm.addFactory(new DraggableTestFactory);
    fm.addFactory(new FalagardSelfFitFactory);
    fm.addFactory(new FalagardTooltipFactory);
    fm.addFactory(new SelectionTestFactory);
    fm.addFactory(new PendingTestFactory);
    fm.addFactory(new TestChannelFactory);
    fm.addFactory(new TestHistoryFactory);
    fm.addFactory(new TestScrollFactory);
    fm.addFactory(new FalagardChatHistory::ParentWindowFactory);
    fm.addFactory(new FalagardChatBoardFactory);
    fm.addFactory(new FalagardButtonFactory);
    fm.addFactory(new FalagardSuperProgressFactory);
    fm.addFactory(new FalagardMenuItemFactory);
    fm.addFactory(new TestPopupFactory);
    fm.addFactory(new SelectionTestMclFactory);
    fm.addFactory(new SelectionTestHeaderFactory);
    fm.addFactory(new SelectionTestSegmentFactory);
    // CYS-HUD
    fm.addFactory(new FalagardNotSuperListFactory);
    fm.addFactory(new FalagardNotSuperListItemFactory);
    fm.addFactory(new FalagardListBoxExFactory);
    fm.addFactory(new FalagardParticleSystemFactory);
    fm.addFactory(new FalagardScrollInfoFactory);
    fm.addFactory(new FalagardScrollInfoFixedFactory);
    fm.addFactory(new FalagardAchievementWindowFactory);
    fm.addFactory(new FalagardDragDropTargetFactory);
    fm.addFactory(new FalagardDragContainerFactory);
    fm.addFactory(new FalagardSuperBarButtonFactory);
    fm.addFactory(new FalagardTripperTooltipFactory);
    fm.addFactory(new FalagardSimpleTextFactory);
    cysRegisterStoredProperties();
    for (bool scaled : {false, true}) {
        test(scaled ? "scaled atlas keeps authored scaling through lazy load and eviction" :
                      "pixel atlas keeps frame geometry through lazy load and eviction", [&] {
            const char* name = scaled ? "ReloadScaled" : "ReloadPixel";
            std::ofstream f("reload.imageset",std::ios::binary);
            f << "<Imageset Name=\"" << name << "\" Imagefile=\"reload-fixture.dds\" NativeHorzRes=\"512\" NativeVertRes=\"384\" AutoScaled=\"" << (scaled ? "true" : "false") << "\"><Image Name=\"Header\" XPos=\"105\" YPos=\"0\" Width=\"10\" Height=\"54\"/></Imageset>";
            f.close();
            auto* set = ImagesetManager::getSingleton().createImageset("reload.imageset");
            const Image& header = set->getImage("Header");
            const float width = scaled ? 20.f : 10.f, height = scaled ? 108.f : 54.f;
            const int before = renderer.reloadTextures;
            require(header.getWidth()==width && header.getHeight()==height,"initial XML geometry wrong");
            for(int cycle=0;cycle<3;++cycle) {
                set->getTexture();
                require(renderer.reloadTextures==before+cycle+1,"lazy reload not exercised");
                require(set->isAutoScaled()==scaled && header.getWidth()==width && header.getHeight()==height,"loading texture changed atlas geometry");
                set->AutoFree(); set->AutoFree();
            }
            ImagesetManager::getSingleton().destroyImageset(name);
        });
    }
    std::ofstream("test.looknfeel",std::ios::binary) << "<Falagard><WidgetLook name=\"ImageTest\"><StateImagery name=\"Enabled\"/><StateImagery name=\"Disabled\"/></WidgetLook></Falagard>";
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("test.looknfeel");
    auto* image=static_cast<FalagardAnimateImage*>(wm.createWindow("Falagard/AnimateImage","Image"));
    image->setLookNFeel("Falagard/AnimateImage","ImageTest");
    auto* button=static_cast<ClickableTestButton*>(wm.createWindow("Falagard/AnimateButton","Button"));
    auto* safe=static_cast<FalagardSafeIMEEditBox*>(wm.createWindow("Falagard/SafeIMEEditBox","Safe"));
    auto* normal=static_cast<FalagardIMEEditBox*>(wm.createWindow("Falagard/IMEEditBox","Normal"));
    auto* star=static_cast<FalagardStar*>(wm.createWindow("Falagard/Star","Star"));
    auto* atlas=ImagesetManager::getSingleton().createImageset("Fixture",renderer.createTexture());
    atlas->defineImage("First",Point(0,0),Size(16,16),Point(0,0));
    atlas->defineImage("Second",Point(16,0),Size(16,16),Point(0,0));
    auto* animation=AnimateManager::getSingleton().createAnimate("Fixture",123,true,200,false,0);
    const Image* first=&atlas->getImage("First"), *second=&atlas->getImage("Second");
    animation->defineAnimateKey(first); animation->defineAnimateKey(second);
    test("native image factory retains StaticImage properties",[&]{ require(image->testClassName("StaticImage") && image->isPropertyPresent("ImageColours"),"wrong image base"); });
    test("native button retains PushButton behavior",[&]{ require(button->testClassName("PushButton"),"wrong button base"); });
    test("animation interface crosses both native base layouts",[&]{
        auto* a=dynamic_cast<IFalagardAnimate*>(static_cast<Window*>(image));
        auto* b=dynamic_cast<IFalagardAnimate*>(button);
        require(a && b,"cross cast failed"); a->Play(false); b->Play(false); a->Play(true); b->Play(true);
    });
    test("animation selects first frame immediately",[&]{image->setProperty("Animate","Fixture");require(image->getImage()==first,"wrong first frame");});
    test("animation advances with CEGUI time",[&]{system.injectTimePulse(.15f); image->updateSelf(.15f);require(image->getImage()==second,"frame did not advance");});
    test("animation loops",[&]{system.injectTimePulse(.10f);image->updateSelf(.10f);require(image->getImage()==first,"loop did not wrap");});
    test("Play false removes image immediately",[&]{dynamic_cast<IFalagardAnimate*>(static_cast<Window*>(image))->Play(false);require(!image->getImage(),"stopped image remains");});
    test("Play true restarts at first frame",[&]{image->Play(true);require(image->getImage()==first,"restart frame wrong");});
    test("invalid animation rejects atomically",[&]{bool threw=false;try{image->setProperty("Animate","Missing");}catch(const Exception&){threw=true;}require(threw && image->getAnimate()=="Fixture" && image->getImage()==first,"invalid animation changed state");});
    test("empty animation clears frame",[&]{image->setProperty("Animate","");require(!image->getImage(),"empty animation not cleared");});
    test("safe edit uses masked native IME",[&]{require(safe->isTextMasked() && safe->testClassName("Editbox") && safe->testClassName("FalagardIMEEditBox"),"not masked IME");});
    test("safe clipboard is disabled by default",[&]{require(!cysAllowsClipboard(safe),"safe clipboard open");});
    test("ordinary clipboard keeps prior default",[&]{require(cysAllowsClipboard(normal),"normal clipboard disabled");});
    test("layout disables and reenables clipboard",[&]{normal->setProperty("DisableCopyPaste","True");require(!cysAllowsClipboard(normal),"layout ignored");normal->setProperty("DisableCopyPaste","False");require(cysAllowsClipboard(normal),"reenable failed");});
    test("no active input cannot use clipboard",[&]{require(!cysAllowsClipboard(nullptr),"null input allowed");});
    test("safe edit preserves Chinese and Vietnamese raw input",[&]{
        const String synthetic(reinterpret_cast<const utf8*>(u8"中文 Tiếng Việt a\u0306\u0301"));
        safe->setText(synthetic);require(safe->getText()==synthetic,"input normalized or corrupted");
    });
    test("safe edit lowers native length limit without splitting code points",[&]{safe->setText(reinterpret_cast<const utf8*>(u8"中文abcd"));safe->setMaxTextLength(3);require(safe->getText().mbcs_length()<=3 && !safe->getText().empty(),"limit not enforced");});
    test("safe zero length clears text",[&]{safe->setMaxTextLength(0);require(safe->getText().empty(),"zero limit not enforced");});
    test("star parses actual layout ranges",[&]{star->setProperty("StarElement","key:MaxLife value:min:200,max:200");star->setProperty("StarBuilder","key:Number value:10");require(star->getProperty("StarBuilder")=="key:Number value:10","star setting lost");});
    test("star rejects reversed ranges",[&]{bool threw=false;try{star->setProperty("StarElement","key:Size value:min:20,max:10");}catch(const Exception&){threw=true;}require(threw,"bad range accepted");});
    test("star rejects excessive counts",[&]{bool threw=false;try{star->setProperty("StarBuilder","key:Number value:4097");}catch(const Exception&){threw=true;}require(threw,"bad count accepted");});
    test("star rejects nonfinite lifetime",[&]{bool threw=false;try{star->setProperty("Life","nan");}catch(const Exception&){threw=true;}require(threw,"NaN accepted");});
    // Rendering tests use real widget caches and synthetic two-frame imagery.
    star->setProperty("AbsoluteSize","w:128 h:128");
    star->setProperty("StarElement","key:Animate value:Fixture");
    star->setProperty("StarBuilder","key:Life value:1000");
    star->setProperty("StarBuilder","key:StartPos value:x:64,y:64");
    star->setProperty("StarBuilder","key:EndPos value:x:64,y:64");
    star->setProperty("Life","500");
    auto draw=[&](Window* w){renderer.clearRenderList();w->requestRedraw();w->render();};
    test("star emits real cached image quads",[&]{star->updateSelf(.11f);draw(star);require(renderer.quads.size()==1,"no emitted image");});
    test("star lifetime removes rendered quads",[&]{star->updateSelf(.50f);draw(star);require(renderer.quads.empty(),"expired star still rendered");});
    test("star reset restores emission",[&]{star->setProperty("Reset","True");star->updateSelf(.11f);draw(star);require(renderer.quads.size()==1,"reset did not restart");});
    test("star hidden update does not emit",[&]{star->hide();star->updateSelf(.3f);draw(star);require(renderer.quads.empty(),"hidden star rendered");star->show();});
    image->setProperty("AbsoluteSize","w:128 h:128");
    auto* fade=AnimateManager::getSingleton().createAnimate("Fade",124,true,200,true,2);
    fade->defineAnimateKey(first);
    test("animation applies frame alpha to rendered image",[&]{
        image->setImageColours(colour(1,1,1,.8f));image->setAnimate("Fade");system.injectTimePulse(.10f);image->updateSelf(0);draw(image);
        require(renderer.colours.size()==1 && renderer.colours[0].d_top_left.getAlpha()>.39f && renderer.colours[0].d_top_left.getAlpha()<.41f,"frame alpha not modulated");
    });
    test("animation drawing preserves authored image alpha",[&]{draw(image);require(image->getImageColours().d_top_left.getAlpha()>.79f,"colour alpha accumulated");});
    test("safe edit native character route accepts Noto Chinese and Vietnamese",[&]{
        const std::string fontPath=std::string(argv[1])+"/Schema/NotoSansSC-Regular.ttf";
        std::ofstream("input.font",std::ios::binary) << "<Font Name=\"Input\" Type=\"FreeType\" Filename=\"" << fontPath << "\" Size=\"12\"/>";
        FontManager::getSingleton().setCodePage(65001);
        auto* font=FontManager::getSingleton().createFont(FreeType,"input.font");
        safe->setFont(font); safe->setMaxTextLength(32);safe->setText("");Ime::SetActiveIMEEditBox(safe,true);
        safe->addCharater(0x4e2d);safe->addCharater(0x1ebf);
        require(safe->getText()==reinterpret_cast<const utf8*>(u8"中ế") && safe->getCaratIndex()==2,"typed Unicode or caret corrupted");
        Ime::SetActiveIMEEditBox(safe,false);
    });
    test("safe edit ASCII input stops at the configured limit",[&]{
        safe->setText("");safe->setMaxTextLength(4);Ime::SetActiveIMEEditBox(safe,true);
        for(int i=0;i<8;++i)safe->addCharater('a');
        require(safe->getText()=="aaaa","native input limit ignored");Ime::SetActiveIMEEditBox(safe,false);
    });
    // Exercise the keyboard's real mouse/input route with synthetic text only.
    auto* keyboard=static_cast<FalagardSoftKeyBoard*>(wm.createWindow("Falagard/SoftKeyWindow","Keyboard"));
    keyboard->setLookNFeel("Falagard/SoftKeyWindow","ImageTest");
    keyboard->setProperty("AbsoluteSize","w:450 h:150");
    normal->setFont(safe->getFont());normal->setMaxTextLength(32);normal->setText("");
    safe->setMaxTextLength(32);safe->setText("");
    auto clickKey=[&](float x,float y){
        MouseEventArgs e(keyboard);e.position=Point(x,y);e.button=LeftButton;
        keyboard->onMouseMove(e);keyboard->onMouseButtonDown(e);keyboard->onMouseButtonUp(e);
    };
    test("soft keyboard types into the selected ordinary editbox",[&]{
        String target="Normal";keyboard->SetActiveEditBox(target);Ime::SetActiveIMEEditBox(normal,true);clickKey(45,45);
        require(normal->getText().length()==1 && normal->getText()[0]>='0' && normal->getText()[0]<='9' && safe->getText().empty(),"keyboard missed selected target");
    });
    test("soft keyboard retargets the masked editbox without changing the old one",[&]{
        const String old=normal->getText();String target="Safe";keyboard->SetActiveEditBox(target);Ime::SetActiveIMEEditBox(safe,true);clickKey(45,45);
        require(safe->getText().length()==1 && safe->isTextMasked() && normal->getText()==old,"keyboard did not retarget");
    });
    test("soft keyboard backspace keeps Chinese code points intact",[&]{
        safe->setText(reinterpret_cast<const utf8*>(u8"中ế"));safe->setCaratIndex(2);clickKey(405,15);
        require(safe->getText()==reinterpret_cast<const utf8*>(u8"中") && safe->getCaratIndex()==1,"backspace split text");
    });
    test("soft keyboard unknown target reports failure",[&]{
        bool threw=false;try{String missing="MissingFixture";keyboard->SetActiveEditBox(missing);}catch(const Exception&){threw=true;}
        require(threw,"unknown target silently accepted");
    });
    Ime::SetActiveIMEEditBox(safe,false);
    Ime::SetActiveIMEEditBox(normal,false);
    // CYS: actual property effects, with no device audio, GPU, network or clipboard.
    Window::setClickSoundCallback(captureClickSound);
    test("ClickSound dispatches one authored id for a left click",[&]{
        button->setProperty("ClickSound","435"); MouseEventArgs e(button); e.button=LeftButton; button->onMouseClicked(e);
        require(clickSounds.size()==1 && clickSounds[0]==435,"sound callback lost id or doubled");
    });
    test("ClickSound ignores right and disabled clicks",[&]{
        MouseEventArgs e(button); e.button=RightButton; button->onMouseClicked(e);button->disable();e.button=LeftButton;button->onMouseClicked(e);button->enable();
        require(clickSounds.size()==1,"invalid click played a sound");
    });
    test("ClickSound -1 disables a configured sound",[&]{button->setProperty("ClickSound","-1");MouseEventArgs e(button);e.button=LeftButton;button->onMouseClicked(e);require(clickSounds.size()==1,"disabled sound fired");});
    test("ClickSound rejects malformed input without changing its id",[&]{bool threw=false;try{button->setProperty("ClickSound","17oops");}catch(const Exception&){threw=true;}require(threw && button->getProperty("ClickSound")=="-1","malformed sound altered state");});
    test("ClickSound callback can be detached before sound shutdown",[&]{Window::setClickSoundCallback(nullptr);button->setProperty("ClickSound","17");MouseEventArgs e(button);e.button=LeftButton;button->onMouseClicked(e);require(clickSounds.size()==1,"detached callback fired");});
    auto* tooltip=static_cast<Tooltip*>(wm.createWindow("Falagard/Tooltip","Tooltip"));
    system.setTooltip(tooltip);
    test("UseDefaultTooltip gates the system tooltip",[&]{
        require(button->getTooltip()==tooltip,"default tooltip missing");button->setProperty("UseDefaultTooltip","False");require(button->getTooltip()==nullptr && !button->isUsingDefaultTooltip() && button->getTooltipType().empty(),"disabled tooltip still routed or reported");
        button->setProperty("UseDefaultTooltip","True");require(button->getTooltip()==tooltip && button->isUsingDefaultTooltip(),"default tooltip not restored");
    });
    test("UseDefaultTooltip preserves explicit custom tooltip",[&]{button->setTooltip(tooltip);button->setProperty("UseDefaultTooltip","False");require(button->getTooltip()==tooltip,"custom tooltip disabled");button->setTooltip(nullptr);require(button->getTooltip()==nullptr,"disabled default returned");button->setProperty("UseDefaultTooltip","True");});
    system.setTooltip(static_cast<Tooltip*>(nullptr));
    auto* numbered=static_cast<FalagardAnimate*>(wm.createWindow("Falagard/Animate","Numbered"));
    numbered->setProperty("AbsoluteSize","w:32 h:32");
    test("AnimateId resolves the actual table and immediately paints a frame",[&]{numbered->setProperty("AnimateId","123");draw(numbered);require(numbered->getAnimateId()==123 && renderer.quads.size()==1,"id lookup did not render");});
    test("AnimateId rejects unknown id atomically",[&]{bool threw=false;try{numbered->setProperty("AnimateId","99999");}catch(const Exception&){threw=true;}require(threw && numbered->getAnimateId()==123,"unknown id changed animation");});
    test("AnimateId -1 clears rendered image",[&]{numbered->setProperty("AnimateId","-1");draw(numbered);require(renderer.quads.empty() && numbered->getAnimateId()==-1,"cleared animation remained");});
    std::ofstream("frame.looknfeel",std::ios::binary) << R"xml(<Falagard><WidgetLook name="FrameTest">
<ImagerySection name="Frame"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="First"/></ImageryComponent></ImagerySection>
<ImagerySection name="Background"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="16"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="Second"/></ImageryComponent></ImagerySection>
<StateImagery name="Enabled"/><StateImagery name="Disabled"/>
<StateImagery name="EnabledFrame"><Layer><Section section="Frame"/></Layer></StateImagery><StateImagery name="DisabledFrame"><Layer><Section section="Frame"/></Layer></StateImagery>
<StateImagery name="EnabledBackground"><Layer><Section section="Background"/></Layer></StateImagery><StateImagery name="DisabledBackground"><Layer><Section section="Background"/></Layer></StateImagery>
</WidgetLook></Falagard>)xml";
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("frame.looknfeel");
    auto* bound=static_cast<FalagardBoundWindow*>(wm.createWindow("Falagard/BoundWindow","Bound"));
    bound->setLookNFeel("Falagard/BoundWindow","FrameTest");bound->setProperty("AbsoluteSize","w:250 h:100");
    test("BoundWindow draws authored frame and background",[&]{bound->setProperty("FrameEnabled","True");bound->setProperty("BackgroundEnabled","True");draw(bound);require(renderer.quads.size()==2,"frame/background not rendered");});
    test("BoundWindow toggles each imagery layer independently",[&]{bound->setProperty("FrameEnabled","False");draw(bound);require(renderer.quads.size()==1 && renderer.quads[0].d_left==16,"wrong frame removed");bound->setProperty("BackgroundEnabled","False");draw(bound);require(renderer.quads.empty(),"background remains");});
    auto* title=static_cast<DraggableTestTitle*>(wm.createWindow("Falagard/DragTitle","Title"));
    bound->addChildWindow(title);title->setProperty("AbsoluteSize","w:200 h:40");title->setFont(safe->getFont());title->setText("Title");
    test("DragTitle draws text using real horizontal formatting",[&]{title->setProperty("HorzFormatting","LeftAligned");draw(title);require(!renderer.quads.empty(),"title not drawn");const float left=renderer.quads[0].d_left;title->setProperty("HorzFormatting","RightAligned");draw(title);require(renderer.quads[0].d_left>left+30,"horizontal format ignored");});
    test("DragTitle applies vertical text placement",[&]{title->setProperty("VertFormatting","TopAligned");draw(title);const float top=renderer.quads[0].d_top;title->setProperty("VertFormatting","BottomAligned");draw(title);require(renderer.quads[0].d_top>top,"vertical format ignored");});
    test("DragTitle starts without an uninitialized drag target",[&]{require(title->getDragTarget().empty(),"uninitialized drag target");});
    test("DragTitle moves only its bound window and releases capture",[&]{
        title->setProperty("DragTarget","Bound");const Point before=bound->getPixelRect().getPosition();DraggableTestTitle* native=title;
        MouseEventArgs e(native);e.button=LeftButton;e.position=Point(10,10);native->onMouseButtonDown(e);e.position=Point(25,18);native->onMouseMove(e);native->onMouseButtonUp(e);
        const Point after=bound->getPixelRect().getPosition();require(after.d_x-before.d_x==15 && after.d_y-before.d_y==8 && Window::getCaptureWindow()!=title,"drag/input capture failed");
    });
    auto* fitted=static_cast<FalagardSelfFitWindow*>(wm.createWindow("Falagard/SelfFitWindow","Fit"));
    fitted->setFont(safe->getFont());fitted->setText("x");
    test("SelfFit MinHeight keeps a short label tall enough",[&]{fitted->setProperty("MinHeight","39");require(fitted->getAbsoluteHeight()==39,"minimum height ignored");});
    test("SelfFit retains minimum height when text clears",[&]{fitted->setText("");require(fitted->getAbsoluteHeight()==39,"empty text discarded min height");});
    test("SelfFit rejects negative and infinite minimum height",[&]{int errors=0;try{fitted->setMinHeight(-1);}catch(const Exception&){++errors;}try{fitted->setMinHeight(std::numeric_limits<float>::infinity());}catch(const Exception&){++errors;}require(errors==2 && fitted->getMinHeight()==39,"invalid minimum accepted");});
    keyboard->setFont(nullptr);
    test("keyboard renders normal backgrounds without requiring text font",[&]{keyboard->setProperty("NormalImage","type:0 set:Fixture image:First");keyboard->setProperty("NormalImage","type:1 set:Fixture image:Second");keyboard->setProperty("NormalImage","type:2 set:Fixture image:Second");keyboard->setProperty("NormalImage","type:3 set:Fixture image:Second");draw(keyboard);require(renderer.quads.size()>50 && keyboard->getNormalImage(3)==second,"normal key backgrounds missing");});
    test("keyboard missing optional hover image does not crash",[&]{MouseEventArgs e(keyboard);e.position=Point(45,45);keyboard->onMouseMove(e);draw(keyboard);require(renderer.quads.size()>50,"normal keys lost on hover");});
    test("keyboard rejects invalid image index before resource lookup",[&]{int errors=0;for(const char* property:{"NormalImage","HoverImage","PushedImage"}){try{keyboard->setProperty(property,"type:4 set:Missing image:Missing");}catch(const InvalidRequestException&){++errors;}}require(errors==3 && keyboard->getNormalImage(0)==first,"invalid image index accepted");});
    test("keyboard malformed image descriptor is rejected atomically",[&]{bool threw=false;try{keyboard->setProperty("NormalImage","set:Fixture image:First");}catch(const Exception&){threw=true;}require(threw && keyboard->getNormalImage(0)==first,"malformed descriptor changed state");});
    auto* list=static_cast<SelectionTestList*>(wm.createWindow("Fixture/Listbox","Selection"));
    auto* row1=new ListboxTextItem("one"); list->addItem(row1);
    test("SelectBrush updates existing native list rows",[&]{list->setProperty("SelectBrush","set:Fixture image:First");require(row1->getSelectionBrushImage()==first,"existing row kept stale brush");});
    test("SelectColour reaches existing and newly inserted rows",[&]{
        list->setProperty("SelectColour","tl:80FFFFFF tr:80FFFFFF bl:80FFFFFF br:80FFFFFF");auto* row2=new ListboxTextItem("two");list->insertItem(row2,row1);
        require(row2->getSelectionBrushImage()==first && row1->getSelectionColours().d_top_left.getAlpha()>.49f && row2->getSelectionColours().d_top_left.getAlpha()<.51f,"inserted selection style incorrect");
    });
    test("SelectBrush rejects missing resource without losing current brush",[&]{bool threw=false;try{list->setProperty("SelectBrush","set:Missing image:First");}catch(const Exception&){threw=true;}require(threw && row1->getSelectionBrushImage()==first,"bad brush changed rows");});
    test("SelectBrush explicit clear propagates to rows",[&]{list->setProperty("SelectBrush","");require(!row1->getSelectionBrushImage(),"clear ignored");});
    auto* combo=static_cast<FalagardCombobox*>(wm.createWindow("Falagard/Combobox","PendingCombo"));
    test("Combobox stores authored row style before child initialization",[&]{combo->setProperty("SelectBrush","set:Fixture image:Second");combo->setProperty("SelectColour","tl:80FFFFFF tr:80FFFFFF bl:80FFFFFF br:80FFFFFF");require(combo->getSelectBrush()==second && combo->getSelectColour().d_top_left.getAlpha()<.51f,"early combo style lost");});
    auto* mcl=static_cast<SelectionTestMcl*>(wm.createWindow("Fixture/MultiColumnList","MultiSelection"));mcl->setSize(Absolute,Size(120,100));
    auto* cellA=new SelectionCell;cellA->setSelectionBrushImage(second);cellA->setSelectionColours(colour(0xFF123456u));
    test("MCL leaves item selection style intact until explicitly configured",[&]{mcl->addRow(cellA,10);mcl->addRow();require(cellA->getSelectionBrushImage()==second && cellA->getSelectionColours().d_top_left.getARGB()==0xFF123456u,"unconfigured MCL overwrote item style");});
    test("MCL SelectBrush applies to existing cells and skips null cells",[&]{mcl->setProperty("SelectBrush","set:Fixture image:First");require(cellA->getSelectionBrushImage()==first && !mcl->getItemAtGridReference(MCLGridRef(0,1)) && mcl->getProperty("SelectBrush")=="set:Fixture image:First","brush update changed null cells or missed existing cell");});
    const String cellColours="tl:80112233 tr:80445566 bl:80778899 br:80AABBCC";
    auto hasCellStyle=[&](ListboxItem* item){return item->getSelectionBrushImage()==first && item->getSelectionColours().d_top_left.getARGB()==0x80112233u && item->getSelectionColours().d_bottom_right.getARGB()==0x80AABBCCu;};
    test("MCL SelectColour stores and applies all four corner colours",[&]{mcl->setProperty("SelectColour",cellColours);const ColourRect c=cellA->getSelectionColours();require(c.d_top_left.getARGB()==0x80112233u && c.d_top_right.getARGB()==0x80445566u && c.d_bottom_left.getARGB()==0x80778899u && c.d_bottom_right.getARGB()==0x80AABBCCu && mcl->getProperty("SelectColour")==cellColours,"MCL lost corner colours");});
    test("MCL style reaches addRow insertRow and replacement cells",[&]{
        auto* added=new SelectionCell;mcl->addRow(added,10);auto* inserted=new SelectionCell;mcl->insertRow(inserted,20,0);
        auto* replaced=new SelectionCell;mcl->setItem(replaced,20,1);
        require(hasCellStyle(added) && hasCellStyle(inserted) && hasCellStyle(replaced) && replaced->getOwnerWindow()==mcl,"future cell style not applied");
        mcl->setItem(nullptr,MCLGridRef(2,1));require(!mcl->getItemAtGridReference(MCLGridRef(2,1)),"null cell was materialized");
    });
    test("MCL malformed brush rejects atomically for current and future cells",[&]{
        int rejected=0;for(const char* value:{"set:Missing image:First","invalid"}){try{mcl->setProperty("SelectBrush",value);}catch(const Exception&){++rejected;}}
        auto* later=new SelectionCell;mcl->setItem(later,10,2);require(rejected==2 && hasCellStyle(cellA) && hasCellStyle(later),"bad image changed MCL style");
    });
    test("MCL selected cell emits the authored selection quad and colours",[&]{
        mcl->setSelectionMode(MultiColumnList::CellSingle);mcl->setItemSelectState(cellA,true);draw(mcl);
        require(renderer.quads.size()==1 && renderer.quads[0].getWidth()==60 && renderer.quads[0].getHeight()==12,"selected cell brush not drawn at native cell bounds");
        const ColourRect c=renderer.colours[0];require(c.d_top_left.getARGB()==0x80112233u && c.d_top_right.getARGB()==0x80445566u && c.d_bottom_left.getARGB()==0x80778899u && c.d_bottom_right.getARGB()==0x80AABBCCu,"selected cell quad ignored authored colour");
    });
    test("MCL explicit empty brush clears existing future and rendered selection",[&]{
        mcl->setProperty("SelectBrush","");auto* later=new SelectionCell;later->setSelectionBrushImage(second);mcl->addRow(later,20);draw(mcl);
        require(!cellA->getSelectionBrushImage() && !later->getSelectionBrushImage() && renderer.quads.empty(),"MCL clear left a current future or rendered brush");
    });
    test("MCL DrawOffset updates existing future cells and leaves null cells empty",[&]{
        require(mcl->getDrawOffset()==0 && cellA->getDrawOffset()==0,"DrawOffset default changed");mcl->setProperty("DrawOffset","4.5");
        auto* added=new SelectionCell;mcl->addRow(added,10);auto* inserted=new SelectionCell;mcl->insertRow(inserted,20,0);
        auto* replaced=new SelectionCell;mcl->setItem(replaced,10,0);mcl->setItem(nullptr,20,0);
        require(cellA->getDrawOffset()==4.5f && added->getDrawOffset()==4.5f && replaced->getDrawOffset()==4.5f && !mcl->getItemAtGridReference(MCLGridRef(0,1)),"DrawOffset propagation missed native row paths");
    });
    test("MCL DrawOffset rejects nonfinite coordinates without changing cells",[&]{
        bool rejected=false;try{mcl->setDrawOffset(std::numeric_limits<float>::infinity());}catch(const InvalidRequestException&){rejected=true;}
        require(rejected && mcl->getDrawOffset()==4.5f && cellA->getDrawOffset()==4.5f,"nonfinite offset changed geometry");
    });
    test("native cell DrawOffset shifts selection left and text right without clipping glyphs",[&]{
        cellA->setText("A");cellA->setFont(safe->getFont());safe->getFont()->getTextExtent("A");cellA->setSelected(true);mcl->setSelectBrush(first);
        auto cellQuads=[&](float offset){mcl->setDrawOffset(offset);RenderCache cache;cellA->draw(cache,Rect(20,20,80,40),0,1,nullptr);renderer.clearRenderList();cache.render(Point(0,0),0,Rect(0,0,200,100),LookLikeSquare);return renderer.quads;};
        const auto base=cellQuads(0),shifted=cellQuads(4);
        require(base.size()>1 && shifted.size()==base.size(),"offset dropped selection or glyph quads");
        require(std::fabs(shifted[0].d_left-base[0].d_left+4)<.01f && shifted[0].d_right==base[0].d_right,"selection did not extend left");
        for(size_t i=1;i<base.size();++i)require(std::fabs(shifted[i].d_left-base[i].d_left-4)<.01f,"glyph did not move right");
        const auto negative=cellQuads(-3);require(std::fabs(negative[0].d_left-base[0].d_left-3)<.01f,"negative offset direction changed");
        mcl->setDrawOffset(0);cellA->setText("");
    });
    test("ColumnsAdjust False leaves widths and property state unchanged",[&]{
        mcl->header.left.setSize(Size(.25f,1));mcl->header.right.setSize(Size(.20f,1));mcl->setProperty("ColumnsAdjust","False");
        require(std::fabs(mcl->header.left.getWidth()-.25f)<.001f && std::fabs(mcl->header.right.getWidth()-.20f)<.001f && mcl->getProperty("ColumnsAdjust").empty(),"False adjusted columns or invented persistent state");
    });
    test("ColumnsAdjust True fills only the last column once",[&]{
        mcl->setProperty("ColumnsAdjust","True");require(std::fabs(mcl->header.left.getWidth()-.25f)<.001f && std::fabs(mcl->header.right.getWidth()-.75f)<.001f,"remaining relative width was not assigned to last column");
        mcl->header.left.setSize(Size(.10f,1));require(std::fabs(mcl->header.right.getWidth()-.75f)<.001f,"ColumnsAdjust became a persistent autosize mode");
    });
    test("ColumnsAdjust does not shrink headers that already exceed available width",[&]{
        mcl->header.left.setSize(Size(.7f,1));mcl->header.right.setSize(Size(.6f,1));mcl->setProperty("ColumnsAdjust","True");
        require(std::fabs(mcl->header.left.getWidth()-.7f)<.001f && std::fabs(mcl->header.right.getWidth()-.6f)<.001f,"wide columns were shrunk");
    });
    std::ofstream channelLook("channel.looknfeel",std::ios::binary);
    channelLook << R"xml(<Falagard><WidgetLook name="ChannelTest"><StateImagery name="Frame"/>)xml";
    for (const char* name:{"IconSize","ItemBkgLeftSize","ItemBkgMiddleSize","ItemBkgRightSize"})
        channelLook << "<NamedArea name=" << char(34) << name << char(34) << R"xml(><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="10"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area></NamedArea>)xml";
    channelLook << "</WidgetLook></Falagard>";channelLook.close();
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("channel.looknfeel");
    auto* channel=static_cast<TestChannel*>(wm.createWindow("Falagard/ChatChannel","Channel"));
    channel->setLookNFeel("Falagard/ChatChannel","ChannelTest"); channel->setFont(nullptr);channel->setItemSize(Size(80,20));
    channel->addChannel("one","set:Fixture image:First","");channel->addChannel("two","set:Fixture image:Second","");
    test("ChannelBkg renders all three parts for each row",[&]{
        for(const char* part:{"left","middle","right"}) channel->setProperty("ChannelBkg",String("dir:")+part+" set:Fixture image:First");
        draw(channel);require(renderer.quads.size()==8 && renderer.quads[1].d_left==10 && renderer.quads[4].d_top==22,"three-part row geometry missing");
    });
    // CYS-TESTRED 19/09: this case used to require setProperty to THROW on an unknown direction. That contract was
    // deliberately dropped in FalChatChannel::setItemBkg (FalChatChannel.cpp:83-105, CYS-UILAYOUTS): the only
    // ChannelBkg value CY and GL actually ship is "dir:CommonFrame set:Chat image:CommonEditBoxBK" - a direction
    // token matching none of the three, plus an imageset that does not exist - so throwing made ChatFrame, a core
    // HUD window, report an error the real client never has. The half this case is NAMED for is untouched and is
    // what is asserted here: a rejected descriptor must change nothing. Widened from part 0 to all three parts and
    // to the drawn geometry, and followed by a valid descriptor so a widget left in a broken state is caught too.
    test("ChannelBkg ignores a malformed direction without touching any part",[&]{
        channel->setProperty("ChannelBkg","dir:CommonFrame set:Fixture image:Second");
        for(int part=0;part<3;++part) require(channel->getItemBkg(part)==first,"bad descriptor changed a background part");
        draw(channel);require(renderer.quads.size()==8,"bad descriptor changed what is drawn");
        channel->setProperty("ChannelBkg","dir:middle set:Fixture image:Second");
        require(channel->getItemBkg(1)==second && channel->getItemBkg(0)==first && channel->getItemBkg(2)==first,"a valid descriptor after a bad one was lost");
        channel->setProperty("ChannelBkg","dir:middle set:Fixture image:First");
        require(channel->getItemBkg(1)==first,"a valid descriptor could not restore the part");
    });
    test("HoverColor keeps all eight digits after optional 0x prefix",[&]{
        channel->setProperty("HoverColor","0xB3277112");channel->setProperty("HoverChannelBkg","set:Fixture image:Second");
        MouseEventArgs e(channel);e.position=Point(20,5);channel->onMouseMove(e);draw(channel);
        require(renderer.quads.size()==9 && renderer.colours[4].d_top_left.getARGB()==0xB3277112,"hover ARGB truncated or not drawn");
    });
    test("ChatChannel empty rows do not divide or paint stale icons",[&]{channel->clearAllChannel();MouseEventArgs e(channel);e.position=Point(1,1);channel->onMouseMove(e);draw(channel);require(renderer.quads.empty() && channel->getHoverChannel().empty(),"empty channel remained");});
    auto* history=static_cast<TestHistory*>(wm.createWindow("Fixture/ChatHistory","History"));
    test("ChildItem accepts the authored factory before child initialization",[&]{history->setProperty("ChildItem","key:ChatBoard name:TLBB_Chat/ChatBoard");require(history->getChatBoardType()=="TLBB_Chat/ChatBoard","child type was not retained");});
    test("ChildItem rejects unknown keys without changing the row factory",[&]{bool threw=false;try{history->setProperty("ChildItem","key:Other name:DefaultWindow");}catch(const Exception&){threw=true;}require(threw && history->getChatBoardType()=="TLBB_Chat/ChatBoard","bad child key changed factory");});
    test("ChildItem insert validates the actual created native type",[&]{history->setProperty("ChildItem","key:ChatBoard name:DefaultWindow");bool threw=false;try{history->insertChatString("fixture");}catch(const InvalidRequestException&){threw=true;}require(threw,"wrong child native type accepted");});
    auto* overlay=static_cast<FalagardButton*>(wm.createWindow("Falagard/Button","Overlay"));
    overlay->setLookNFeel("Falagard/Button","FrameTest");overlay->setSize(Absolute,Size(100,20));
    test("BtnAnimation selects a resource without starting its overlay",[&]{overlay->setProperty("BtnAnimation","Fixture");draw(overlay);require(overlay->getBtnAnimation()=="Fixture" && !overlay->isAnimateEnabled() && renderer.quads.empty(),"selection started animation");});
    test("EnableAnimation starts and paints a full button overlay",[&]{overlay->setProperty("EnableAnimation","True");draw(overlay);require(renderer.quads.size()==1 && renderer.quads[0].getWidth()==100 && overlay->getBtnAnimationFrame()==first,"button overlay missing");});
    test("button animation advances and repeated enable does not restart",[&]{system.injectTimePulse(.12f);overlay->updateSelf(0);overlay->setProperty("EnableAnimation","True");require(overlay->getBtnAnimationFrame()==second,"animation did not advance or restarted");});
    test("BtnAnimation missing name preserves the current resource",[&]{overlay->setProperty("BtnAnimation","Missing");require(overlay->getBtnAnimation()=="Fixture" && overlay->getBtnAnimationFrame()==second,"missing name changed resource");});
    test("button animation hidden update keeps cached frame",[&]{overlay->hide();system.injectTimePulse(.11f);overlay->updateSelf(0);require(overlay->getBtnAnimationFrame()==second,"hidden animation updated");overlay->show();});
    test("button animation stop clears its rendered overlay",[&]{overlay->setProperty("EnableAnimation","False");draw(overlay);require(renderer.quads.empty() && !overlay->getBtnAnimationFrame(),"stopped overlay remained");});
    test("button animation multiplies frame alpha by window alpha",[&]{overlay->setProperty("BtnAnimation","Fade");overlay->setAlpha(.8f);overlay->enableAnimate(true);system.injectTimePulse(.1f);overlay->updateSelf(0);draw(overlay);require(renderer.colours.size()==1 && renderer.colours[0].d_top_left.getAlpha()>.39f && renderer.colours[0].d_top_left.getAlpha()<.41f,"overlay alpha mismatch");});
    std::ofstream progressLook("progress.looknfeel",std::ios::binary);
    progressLook << R"xml(<Falagard><WidgetLook name="ProgressTest"><NamedArea name="ProgressArea"><Area><Dim type="LeftEdge"><AbsoluteDim value="10"/></Dim><Dim type="TopEdge"><AbsoluteDim value="5"/></Dim><Dim type="Width"><AbsoluteDim value="80"/></Dim><Dim type="Height"><AbsoluteDim value="10"/></Dim></Area></NamedArea><NamedArea name="LightArea"><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="8"/></Dim><Dim type="Height"><AbsoluteDim value="6"/></Dim></Area></NamedArea>)xml";
    for (const char* name:{"Fill","Light"}) progressLook << "<ImagerySection name=" << char(34) << name << char(34) << R"xml(><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><UnifiedDim scale="1" type="Width"/></Dim><Dim type="Height"><UnifiedDim scale="1" type="Height"/></Dim></Area><Image imageset="Fixture" image="First"/><VertFormat type="Stretched"/><HorzFormat type="Stretched"/></ImageryComponent></ImagerySection>)xml";
    progressLook << R"xml(<StateImagery name="Enabled"><Layer><Section section="Fill"/></Layer></StateImagery><StateImagery name="Disabled"><Layer><Section section="Fill"/></Layer></StateImagery><StateImagery name="LightEnable"><Layer><Section section="Light"/></Layer></StateImagery></WidgetLook></Falagard>)xml";progressLook.close();
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("progress.looknfeel");
    auto* progress=static_cast<FalagardSuperProgress*>(wm.createWindow("Falagard/SuperProgress","Progress"));
    progress->setLookNFeel("Falagard/SuperProgress","ProgressTest");progress->setSize(Absolute,Size(100,20));progress->setProgress(.25f);progress->update(2);
    test("SuperProgress renders modern look fill without ProgressImage",[&]{draw(progress);require(renderer.quads.size()==1 && renderer.quads[0].d_left==10 && renderer.quads[0].getWidth()==20 && renderer.colours[0].d_top_left.getAlpha()==1,"modern progress fill missing");});
    test("LightEnable paints the authored size at the progress head",[&]{progress->setProperty("LightEnable","True");draw(progress);require(renderer.quads.size()==2 && renderer.quads[1].d_left==26 && renderer.quads[1].d_top==7 && renderer.quads[1].getWidth()==8 && renderer.quads[1].getHeight()==6,"light head geometry mismatch");});
    test("progress light follows actual progress before smooth effect catches up",[&]{progress->setProgress(.75f);draw(progress);require(renderer.quads.size()==2 && renderer.quads[1].d_left==66 && renderer.quads[0].getWidth()==60,"light used stale progress");});
    test("LightEnable false removes only the light imagery",[&]{progress->setProperty("LightEnable","False");progress->update(2);draw(progress);require(renderer.quads.size()==1 && renderer.quads[0].getWidth()==60,"disabled light remained or fill disappeared");});
    test("cast end fades the visible bar then hides it",[&]{
        progress->show(); progress->setProperty("SetFadeState","0"); progress->setProperty("SetFadeState","2");
        progress->update(.1f); require(progress->isVisible() && progress->getAlpha()>0 && progress->getAlpha()<1,"fade did not start");
        progress->update(.3f); require(!progress->isVisible(),"completed cast bar stayed visible");
    });
    test("new cast cancels the previous pending fade",[&]{
        progress->show(); progress->setProperty("SetFadeState","0"); progress->setProperty("SetFadeState","2"); progress->update(.1f);
        progress->setProperty("SetFadeState","0"); progress->update(2);
        require(progress->isVisible() && progress->getAlpha()==1,"old cast hid the new cast");
    });
    test("repeated cast end does not postpone hiding",[&]{
        progress->setProperty("SetFadeState","2"); progress->update(.2f);
        progress->setProperty("SetFadeState","2"); progress->update(.11f);
        require(!progress->isVisible(),"repeated stop restarted fade");
    });
    test("cast cancel survives a long frame and can reopen",[&]{
        progress->show(); progress->setProperty("SetFadeState","0"); progress->setProperty("SetFadeState","2"); progress->update(5);
        require(!progress->isVisible(),"long frame stranded cast bar");
        progress->show(); progress->setProperty("SetFadeState","0");
        require(progress->isVisible() && progress->getAlpha()==1,"reopened cast remained transparent");
    });
    auto* parent=static_cast<FalagardChatHistory::ParentWindow*>(wm.createWindow("Falagard/ChatHistory/ParentWindow","HistoryParent"));
    auto* scroll=static_cast<Scrollbar*>(wm.createWindow("Fixture/Scrollbar","HistoryScroll"));
    history->attachNativeChildren(parent,scroll);history->setChatBoardType("Falagard/ChatBoard");history->setChatBoardNumber(10);
    test("BoardKillTimer defaults to zero and rejects malformed values",[&]{require(history->getProperty("BoardKillTimer")=="0","timer default changed");int errors=0;for(const char* bad:{"-1","5bad"}){try{history->setProperty("BoardKillTimer",bad);}catch(const InvalidRequestException&){++errors;}}require(errors==2 && history->getBoardKillTimer()==0,"invalid timer accepted");});
    test("normal chat insert never expires under a configured board timer",[&]{history->setProperty("BoardKillTimer","1");history->insertChatString("ordinary");history->update(2);require(history->rowCount()==1 && parent->getChildCount()==1,"ordinary chat expired");});
    test("mode3 expiry removes only its timed row during native parent update",[&]{auto* row=history->insertChatString(7,"timed",3);String name=row->getName();history->update(.4f);require(wm.isWindowPresent(name) && row->getID()==7,"timed row expired early");history->update(.6f);require(!wm.isWindowPresent(name) && history->rowCount()==1,"expiry skipped or removed ordinary chat");});
    test("mode4 expiry handles a large frame without leaving a detached window",[&]{auto* row=history->insertChatString(8,"timed layout",4);String name=row->getName();history->update(20);require(!wm.isWindowPresent(name) && history->rowCount()==1,"large frame stranded timed row");});
    test("mode1 skips expiry and mode2 only requests layout",[&]{history->insertChatString(9,"batched",1);auto* result=history->insertChatString(10,"ignored",2);history->update(2);require(!result && history->rowCount()==2,"batch mode inserted or expired wrong rows");});
    test("zero timer disables expiry even for timed insert mode",[&]{history->setBoardKillTimer(0);history->insertChatString(11,"permanent",4);history->update(20);require(history->rowCount()==3,"zero timer expired row");});
    test("chat clear cancels all timers and destroys managed rows",[&]{history->setBoardKillTimer(1);auto* row=history->insertChatString(12,"to clear",3);String name=row->getName();history->removeAllChatStrings();history->update(2);require(history->rowCount()==0 && parent->getChildCount()==0 && !wm.isWindowPresent(name),"clear left rows or live timers");});
    test("invalid chat insert mode is atomic",[&]{bool threw=false;try{history->insertChatString(0,"bad",9);}catch(const InvalidRequestException&){threw=true;}require(threw && history->rowCount()==0,"invalid mode inserted row");});
    auto* menuRoot=wm.createWindow("DefaultWindow","MenuRoot");menuRoot->setSize(Absolute,Size(1024,768));
    auto* menu=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","Menu"));
    auto* shared=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","Shared"));
    menuRoot->addChildWindow(menu);menuRoot->addChildWindow(shared);
    menu->setPosition(Absolute,Point(100,50));menu->setSize(Absolute,Size(150,100));shared->setSize(Absolute,Size(80,50));shared->hide();
    auto* itemA=static_cast<MenuItem*>(wm.createWindow("Falagard/MenuItem","MenuA"));
    auto* itemB=static_cast<MenuItem*>(wm.createWindow("Falagard/MenuItem","MenuB"));
    itemA->setFont(safe->getFont());itemB->setFont(safe->getFont());menu->addItem(itemA);menu->addItem(itemB);
    itemA->setPosition(Absolute,Point(0,10));itemA->setSize(Absolute,Size(150,20));
    itemB->setPosition(Absolute,Point(0,30));itemB->setSize(Absolute,Size(150,20));
    test("PopMenu allows two items to reference one popup without reparenting",[&]{itemA->setProperty("PopMenu","Shared");itemB->setProperty("PopMenu","Shared");require(itemA->getPopupMenu()==shared && itemB->getPopupMenu()==shared && shared->getParent()==menuRoot,"shared popup ownership changed");});
    test("PopMenu opens beside its parent menu using screen coordinates",[&]{itemA->openPopupMenu();require(shared->isVisible() && shared->getAbsoluteXPosition()==250 && shared->getAbsoluteYPosition()==60,"shared popup position or opening failed");itemA->closePopupMenu();require(!shared->isVisible(),"shared popup did not close");});
    test("shared PopMenu switches between items through native MenuBase",[&]{itemA->openPopupMenu();itemB->openPopupMenu();require(shared->isVisible() && menu->getPopupMenuItem()==itemB && shared->getAbsoluteYPosition()==80,"shared popup switching failed");itemB->closePopupMenu();});
    test("PopMenu rejects a non-popup target atomically",[&]{bool threw=false;try{itemA->setProperty("PopMenu","MenuRoot");}catch(const InvalidRequestException&){threw=true;}require(threw && itemA->getPopupMenu()==shared && itemA->getProperty("PopMenu")=="Shared","wrong target changed binding");});
    test("PopMenu clamps shared submenu to the screen",[&]{menu->setPosition(Absolute,Point(980,750));itemA->openPopupMenu();require(shared->getAbsoluteXPosition()==900 && shared->getAbsoluteYPosition()==718,"shared popup escaped screen");itemA->closePopupMenu();});
    test("destroying a shared popup clears every live menu binding",[&]{itemA->openPopupMenu();wm.destroyWindow(shared);wm.cleanDeadPool();require(!itemA->getPopupMenu() && !itemB->getPopupMenu() && !menu->getPopupMenuItem(),"destroyed popup left dangling references");itemA->openPopupMenu();itemB->closePopupMenu();});
    test("destroying an open menu item disconnects its popup lifetime handler",[&]{
        auto* survivor=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","SurvivorPopup"));menuRoot->addChildWindow(survivor);survivor->setSize(Absolute,Size(80,50));
        itemA->setProperty("PopMenu","SurvivorPopup");itemB->setProperty("PopMenu","SurvivorPopup");itemA->openPopupMenu();
        wm.destroyWindow(itemA);wm.cleanDeadPool();require(!menu->getPopupMenuItem() && menu->getItemCount()==1 && survivor->getParent()==menuRoot,"menu destruction broke shared owner or left a dead list item");
        wm.destroyWindow(survivor);wm.cleanDeadPool();require(!itemB->getPopupMenu(),"remaining binding was not cleared");
    });
    auto makeMenuItem=[&](const char* name,PopupMenu* owner){auto* item=static_cast<MenuItem*>(wm.createWindow("Falagard/MenuItem",name));item->setFont(safe->getFont());owner->addItem(item);return item;};
    test("removing an open item clears menu state before deleting the item",[&]{
        auto* popup=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","RemovePopup"));menuRoot->addChildWindow(popup);popup->setSize(Absolute,Size(80,50));
        auto* item=makeMenuItem("RemoveItem",menu);item->setProperty("PopMenu","RemovePopup");item->openPopupMenu();
        menu->removeItem(item);wm.cleanDeadPool();require(menu->getItemCount()==1 && !menu->getPopupMenuItem() && !popup->isVisible() && wm.isWindowPresent("RemovePopup"),"removeItem retained active/dead item or destroyed shared popup");
    });
    test("detaching and reparenting a menu item updates both native lists",[&]{
        auto* other=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","OtherMenu"));menuRoot->addChildWindow(other);
        auto* item=makeMenuItem("MoveItem",menu);item->setProperty("PopMenu","RemovePopup");item->openPopupMenu();other->addChildWindow(item);
        require(menu->getItemCount()==1 && !menu->getPopupMenuItem() && other->getItemCount()==1 && other->getItemFromIndex(0)==item,"reparent retained item in old menu");
        other->removeChildWindow(item);require(other->getItemCount()==0 && !item->getParent() && wm.isWindowPresent("MoveItem"),"detach destroyed or retained item");wm.destroyWindow(item);wm.cleanDeadPool();
    });
    test("resetList destroys every item once and preserves the shared popup",[&]{
        auto* item=makeMenuItem("ResetItem",menu);item->setProperty("PopMenu","RemovePopup");item->openPopupMenu();
        menu->resetList();wm.cleanDeadPool();require(menu->getItemCount()==0 && !menu->getPopupMenuItem() && !wm.isWindowPresent("ResetItem") && !wm.isWindowPresent("MenuB") && wm.isWindowPresent("RemovePopup"),"reset skipped an item or deleted shared popup");
    });
    test("destroying a menu parent safely releases its list and shared bindings",[&]{
        auto* owned=static_cast<PopupMenu*>(wm.createWindow("Fixture/PopupMenu","OwnedMenu"));menuRoot->addChildWindow(owned);
        auto* item=makeMenuItem("OwnedItem",owned);item->setProperty("PopMenu","RemovePopup");item->openPopupMenu();
        wm.destroyWindow(owned);wm.cleanDeadPool();require(!wm.isWindowPresent("OwnedItem") && wm.isWindowPresent("RemovePopup"),"parent cleanup retained item or deleted shared popup");
        wm.destroyWindow("RemovePopup");wm.cleanDeadPool();
    });
    test("MapImage is registered by the real Falagard factory module",[&]{
        typedef void (*RegisterFactory)(const String&);
        RegisterFactory reg=reinterpret_cast<RegisterFactory>(GetProcAddress(GetModuleHandleA("CEGUIFalagard.dll"),"registerFactory"));
        require(reg!=nullptr,"Falagard registerFactory export missing");reg("Falagard/MapImage");
    });
    auto* mapped=dynamic_cast<FalagardMapImage*>(wm.createWindow("Falagard/MapImage","Mapped"));
    mapped->setLookNFeel("Falagard/MapImage","ImageTest");mapped->setSize(Absolute,Size(42,30));
    test("ChildImage mappings do not display an image until selected",[&]{mapped->setProperty("ChildImage","name:Enemy set:Fixture image:First");mapped->setProperty("ChildImage","name:Friend set:Fixture image:Second");draw(mapped);require(renderer.quads.empty() && mapped->testClassName("FalagardMapImage"),"mapping displayed an unselected image or factory was aliased");});
    test("SetCurrentImage selects and paints the actual named image",[&]{mapped->setProperty("SetCurrentImage","Enemy");draw(mapped);require(mapped->getImage()==first && renderer.quads.size()==1 && renderer.quads[0].getWidth()==42,"named image not rendered");mapped->setProperty("SetCurrentImage","Friend");draw(mapped);require(mapped->getImage()==second && renderer.quads.size()==1,"image did not switch");});
    test("SetCurrentImage unknown key clears the previous picture as B does",[&]{mapped->setProperty("SetCurrentImage","Missing");draw(mapped);require(!mapped->getImage() && renderer.quads.empty(),"unknown key left stale image");});
    test("replacing ChildImage changes the map on the next selection",[&]{mapped->setCurrentImage("Enemy");mapped->setProperty("ChildImage","name:Enemy set:Fixture image:Second");require(mapped->getImage()==first,"map replacement altered active image early");mapped->setCurrentImage("Enemy");require(mapped->getImage()==second,"replaced map was not selected");});
    // CYS-TESTRED 19/09: this case used to count two thrown exceptions. FalagardMapImage::setChildImage no longer
    // throws (FalagardMapImage.cpp:42-62, CYS-CYGUI): a malformed or unresolvable ChildImage in the CY looks must
    // not abort the window that is being built, so it is logged and skipped. The invariant the case is NAMED for
    // never changed and is what is asserted here - and it is now asserted on the property read-back as well,
    // which is the tighter statement: d_childDescriptor is assigned only on the accepted path, so a half-applied
    // descriptor shows up even when the image map happens to survive.
    test("ChildImage malformed resource cannot corrupt an existing mapping",[&]{
        const String accepted=mapped->getProperty("ChildImage");
        for(const char* bad:{"name:Enemy set:Missing image:First","name:Enemy set:Fixture",""}) mapped->setProperty("ChildImage",bad);
        mapped->setCurrentImage("Enemy");
        require(mapped->getImage()==second,"invalid descriptor changed mapping");
        require(mapped->getProperty("ChildImage")==accepted,"invalid descriptor changed the property read-back");
        mapped->setProperty("ChildImage","name:Enemy set:Fixture image:First");mapped->setCurrentImage("Enemy");
        require(mapped->getImage()==first && mapped->getProperty("ChildImage")=="name:Enemy set:Fixture image:First","a valid descriptor after bad ones was lost");
        mapped->setProperty("ChildImage","name:Enemy set:Fixture image:Second");mapped->setCurrentImage("Enemy");
    });
    test("MeshWindow is registered by the real Falagard factory module",[&]{
        typedef void (*RegisterFactory)(const String&);
        RegisterFactory reg=reinterpret_cast<RegisterFactory>(GetProcAddress(GetModuleHandleA("CEGUIFalagard.dll"),"registerFactory"));
        require(reg!=nullptr,"Falagard registerFactory export missing");reg("Falagard/MeshWindow");
    });
    auto* mesh=dynamic_cast<FalagardMeshWindow*>(wm.createWindow("Falagard/MeshWindow","MeshBackground"));
    auto* meshParent=wm.createWindow("DefaultWindow","MeshParent");
    meshParent->setSize(Absolute,Size(1024,768));meshParent->addChildWindow(mesh);
    mesh->setLookNFeel("Falagard/MeshWindow","ImageTest");mesh->setSize(Absolute,Size(64,128));
    MeshShownCapture meshShown;mesh->subscribeShownEvent(Event::Subscriber(&MeshShownCapture::receive,&meshShown));
    test("MeshWindow BackgroundName defaults to an empty texture name",[&]{
        require(mesh->getProperty("BackgroundName").empty() && mesh->getPropertyDefault("BackgroundName").empty() && mesh->testClassName("FalagardMeshWindow"),"wrong native property/default");
    });
    test("visible MeshWindow background assignment raises MeshShown with its window",[&]{
        const int before=meshShown.count;mesh->setProperty("BackgroundName","Common/MeshBack.dds");
        require(mesh->getBackgroundName()=="Common/MeshBack.dds" && meshShown.count==before+1 && meshShown.window==mesh,"background or event was lost");
    });
    test("repeated MeshWindow background assignment raises MeshShown again",[&]{
        const int before=meshShown.count;mesh->setProperty("BackgroundName","Common/MeshBack.dds");
        require(meshShown.count==before+1,"equal background assignment was suppressed");
    });
    test("hidden MeshWindow stores BackgroundName without raising MeshShown",[&]{
        mesh->hide();const int before=meshShown.count;mesh->setProperty("BackgroundName","SelfEquip/jpg_SelfEquipBackground.dds");
        require(mesh->getBackgroundName()=="SelfEquip/jpg_SelfEquipBackground.dds" && meshShown.count==before,"hidden background assignment raised shown");mesh->show();
    });
    test("hidden MeshWindow parent suppresses background MeshShown",[&]{
        meshParent->hide();const int before=meshShown.count;mesh->setProperty("BackgroundName","Common/MeshBack.dds");
        require(mesh->isVisible(true) && !mesh->isVisible(false) && meshShown.count==before,"ancestor visibility was ignored");meshParent->show();
    });
    test("empty MeshWindow background clears the property without MeshShown",[&]{
        const int before=meshShown.count;mesh->setProperty("BackgroundName","");
        require(mesh->getBackgroundName().empty() && meshShown.count==before,"empty background retained value or raised shown");
    });
    test("MeshWindow BackgroundName preserves and stretches its bound RTT image",[&]{
        mesh->setImage(first);mesh->setFakeObject("BoundFixture");mesh->setProperty("BackgroundName","Common/MeshBack.dds");draw(mesh);
        require(mesh->getFakeObject()=="BoundFixture" && mesh->getImage()==first && renderer.quads.size()==1 && renderer.quads[0].getWidth()==64 && renderer.quads[0].getHeight()==128,"background setter replaced the RTT image or bypassed native stretching");
    });
    // ---- CYS-HUD: GL widgets for the HUD / system windows -----------------------------------------------------
    std::ofstream("hud.looknfeel",std::ios::binary) <<
        "<Falagard>"
        "<WidgetLook name=\"HudList\"><Child type=\"DefaultWindow\" nameSuffix=\"__auto_parent__\"><Area><Dim type=\"LeftEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"TopEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"RightEdge\"><UnifiedDim scale=\"1\" type=\"RightEdge\"/></Dim><Dim type=\"BottomEdge\"><UnifiedDim scale=\"1\" type=\"BottomEdge\"/></Dim></Area></Child>"
        "<Child type=\"Fixture/Scrollbar\" nameSuffix=\"__auto_vscrollbar__\"><Area><Dim type=\"LeftEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"TopEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"Width\"><AbsoluteDim value=\"16\"/></Dim><Dim type=\"Height\"><UnifiedDim scale=\"1\" type=\"Height\"/></Dim></Area></Child>"
        "<StateImagery name=\"EnabledBackground\"/><StateImagery name=\"DisabledBackground\"/></WidgetLook>"
        "<WidgetLook name=\"HudItem\"><StateImagery name=\"EnabledBackground\"/><StateImagery name=\"DisabledBackground\"/></WidgetLook>"
        "<WidgetLook name=\"HudListEx\"><NamedArea name=\"ItemRenderingArea\"><Area><Dim type=\"LeftEdge\"><AbsoluteDim value=\"2\"/></Dim><Dim type=\"TopEdge\"><AbsoluteDim value=\"2\"/></Dim><Dim type=\"RightEdge\"><UnifiedDim scale=\"1\" offset=\"-2\" type=\"RightEdge\"/></Dim><Dim type=\"BottomEdge\"><UnifiedDim scale=\"1\" offset=\"-2\" type=\"BottomEdge\"/></Dim></Area></NamedArea>"
        "<Child type=\"Fixture/Scrollbar\" nameSuffix=\"__auto_vscrollbar__\"><Area><Dim type=\"LeftEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"TopEdge\"><AbsoluteDim value=\"0\"/></Dim><Dim type=\"Width\"><AbsoluteDim value=\"16\"/></Dim><Dim type=\"Height\"><UnifiedDim scale=\"1\" type=\"Height\"/></Dim></Area></Child>"
        "<StateImagery name=\"Enabled\"/><StateImagery name=\"Disabled\"/></WidgetLook>"
        "<WidgetLook name=\"HudTarget\"><StateImagery name=\"Normal\"/><StateImagery name=\"Hover\"/><StateImagery name=\"Disabled\"/></WidgetLook>"
        "<WidgetLook name=\"HudTooltip\"><NamedArea name=\"TextArea\"><Area><Dim type=\"LeftEdge\"><AbsoluteDim value=\"2\"/></Dim><Dim type=\"TopEdge\"><AbsoluteDim value=\"2\"/></Dim><Dim type=\"RightEdge\"><UnifiedDim scale=\"1\" offset=\"-2\" type=\"RightEdge\"/></Dim><Dim type=\"BottomEdge\"><UnifiedDim scale=\"1\" offset=\"-2\" type=\"BottomEdge\"/></Dim></Area></NamedArea><StateImagery name=\"Enabled\"/><StateImagery name=\"Disabled\"/></WidgetLook>"
        "</Falagard>";
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("hud.looknfeel");
    auto* nsList=static_cast<FalagardNotSuperList*>(wm.createWindow("Falagard/NotSuperList","NsList"));
    nsList->setSize(Absolute,Size(200,50));nsList->setLookNFeel("Falagard/NotSuperList","HudList");
    auto* nsTpl=static_cast<FalagardNotSuperListItem*>(wm.createWindow("Falagard/NotSuperListItem","NsTpl"));
    nsTpl->setLookNFeel("Falagard/NotSuperListItem","HudItem");nsTpl->setSize(Absolute,Size(200,20));
    auto* nsText=wm.createWindow("DefaultWindow","NsTplText");nsText->setText("hello");nsText->setSize(Absolute,Size(50,10));
    nsText->subscribeEvent("MouseLClick",ScriptFunctor("NsTpl_Clicked()"));nsText->cysRecordScriptedEvent("MouseLClick","NsTpl_Clicked()");
    nsTpl->addChildWindow(nsText);nsList->addChildWindow(nsTpl);
    test("NotSuperList adopts the look's auto children and keeps the layout item as a hidden template",[&]{
        require(nsList->getContainer()==wm.getWindow("NsList__auto_parent__") && nsList->getVertScrollbar()==wm.getWindow("NsList__auto_vscrollbar__"),"auto children not adopted");
        require(nsTpl->isTemplate() && !nsTpl->isVisible() && nsList->getTemplateCount()==1 && nsList->GetListSize()==0,"template not registered/hidden");
    });
    test("NotSuperList AddChild clones the template with its properties, children and scripted events",[&]{
        Window* a=nsList->AddChild("NsTpl");
        require(a && a->getName()=="NsTpl__auto_0__" && a->isVisible() && a->getParent()==nsList->getContainer(),"clone missing or misplaced");
        require(wm.isWindowPresent("NsTplText__auto_0__") && wm.getWindow("NsTplText__auto_0__")->getText()=="hello","sub item not cloned");
        require(wm.getWindow("NsTplText__auto_0__")->cysScriptedEvents().size()==1 && wm.getWindow("NsTplText__auto_0__")->cysScriptedEvents()[0].second=="NsTpl_Clicked()","scripted event not cloned");
        require(static_cast<FalagardNotSuperListItem*>(a)->MapChildName("NsTplText")=="NsTplText__auto_0__" && static_cast<FalagardNotSuperListItem*>(a)->MapChildName("Nope").empty(),"MapChildName wrong");
        require(nsList->GetListSize()==1 && nsList->GetListItem(0)==a && !nsTpl->isVisible(),"list bookkeeping wrong");
    });
    test("NotSuperList stacks clones in columns and drives the scrollbar document",[&]{
        Window* b=nsList->AddChild("NsTpl");Window* c=nsList->AddChild("NsTpl");
        require(b->getName()=="NsTpl__auto_1__" && c->getName()=="NsTpl__auto_2__","clone names not sequential");
        require(nsList->GetListItem(0)->getAbsoluteYPosition()==0 && b->getAbsoluteYPosition()==20 && c->getAbsoluteYPosition()==40,"single column stacking wrong");
        require(nsList->getVertScrollbar()->getDocumentSize()==60 && nsList->getVertScrollbar()->isVisible(),"scrollbar document/visibility wrong");
        nsList->setProperty("MultiColumn","2");
        require(b->getAbsoluteXPosition()>=90 && b->getAbsoluteYPosition()==0 && c->getAbsoluteYPosition()==20,"two column stacking wrong");
        nsList->setProperty("MultiColumn","1");
    });
    test("NotSuperList single selection and property round trip",[&]{
        Window* a=nsList->GetListItem(0);Window* b=nsList->GetListItem(1);
        nsList->SetSelected(a,true);nsList->SetSelected(b,true);
        require(!static_cast<FalagardNotSuperListItem*>(a)->getSelected() && static_cast<FalagardNotSuperListItem*>(b)->getSelected() && nsList->GetSelected()==b,"single select failed");
        nsList->setProperty("SelectMode","Multi");nsList->SetSelected(a,true);
        require(static_cast<FalagardNotSuperListItem*>(a)->getSelected() && static_cast<FalagardNotSuperListItem*>(b)->getSelected(),"multi select failed");
        require(nsList->getProperty("SelectMode")=="Multi" && nsList->getProperty("VScrollMode")=="Auto" && nsList->getProperty("MultiColumn")=="1","property getters wrong");
        nsList->setProperty("VScrollMode","");nsList->setProperty("VScrollMode","True");require(nsList->getProperty("VScrollMode")=="Show","GL VScrollMode spellings not accepted");
    });
    test("NotSuperList Clear destroys the clones and restarts the auto names",[&]{
        nsList->Clear();
        require(nsList->GetListSize()==0 && !wm.isWindowPresent("NsTpl__auto_0__") && !wm.isWindowPresent("NsTplText__auto_1__") && wm.isWindowPresent("NsTpl"),"Clear left windows or removed the template");
        Window* again=nsList->AddChild("NsTpl");require(again && again->getName()=="NsTpl__auto_0__","auto names did not restart");
        require(nsList->AddChild("Missing")==0,"unknown template must return null, not throw");
    });
    // like the game: a Falagard mapping assigns the look before initialise() (Listbox needs its scrollbar children then)
    fm.addFalagardWindowMapping("Hud_ListBoxEx","Falagard/ListBoxEx","HudListEx");
    auto* lbx=static_cast<FalagardListBoxEx*>(wm.createWindow("Hud_ListBoxEx","Lbx"));lbx->setSize(Absolute,Size(200,100));
    test("ListBoxEx adds typed rows, finds them by id and resets cleanly",[&]{
        lbx->AddItemEx("title",7,3,colour(1,1,1),0);lbx->AddItemEx("row",8,0,colour(1,1,1),0,"set:Fixture image:First");
        lbx->RequestLayOut();
        require(lbx->getItemCount()==2 && lbx->GetItemNumByMissionId(8)==1 && lbx->GetItemNumByMissionId(9)==-1 && lbx->GetClickedButtonItem()==-1,"rows not registered");
        require(!wm.isWindowPresent("Lbx__button__eye__00"),"a row button was created without its GL look");
        lbx->ResetListEx();require(lbx->getItemCount()==0,"reset failed");
    });
    auto* info=static_cast<FalagardScrollInfo*>(wm.createWindow("Falagard/ScrollInfo","Info"));info->setSize(Absolute,Size(100,20));
    test("ScrollInfo queues notices, scrolls them out and cools down",[&]{
        info->setProperty("MoveSpeed","1000");info->setProperty("CoolDown","0.5");
        require(info->getProperty("MoveSpeed")=="1000" && info->getCoolDown()==0.5f,"properties lost");
        info->setWaitingInfo("first");info->setWaitingInfo("second");info->setWaitingInfoFirst("urgent");
        require(info->isUpdateRequire() && info->getCurrentText()=="first" && info->getQueueSize()==2,"queue order wrong");
        info->update(0.2f);require(info->getCurrentText().empty() && info->getQueueSize()==2,"notice did not scroll out");
        info->update(0.6f);require(info->getCurrentText()=="urgent","cool-down did not advance to the front notice");
        info->ResetIt();require(!info->isUpdateRequire() && info->getQueueSize()==0,"reset failed");
        require(wm.createWindow("Falagard/ScrollInfoFixed","InfoFixed")->testClassName("FalagardScrollInfoFixed"),"fixed variant missing");
    });
    auto* ps=static_cast<FalagardParticleSystem*>(wm.createWindow("Falagard/PS","Ps"));
    MeshShownCapture psShown,psHidden;
    ps->subscribeEvent(FalagardParticleSystem::EventEffectShown,Event::Subscriber(&MeshShownCapture::receive,&psShown));
    ps->subscribeEvent(FalagardParticleSystem::EventEffectHidden,Event::Subscriber(&MeshShownCapture::receive,&psHidden));
    test("PS stores Effect/EffectAuto and raises EffectShown/EffectHidden with visibility",[&]{
        ps->setProperty("Effect","UI_CBT_2024");ps->setProperty("EffectAuto","True");ps->setProperty("Visible","False");
        require(ps->getProperty("Effect")=="UI_CBT_2024" && ps->getProperty("EffectAuto")=="True" && psShown.count==0,"hidden PS started its effect");
        ps->show();require(psShown.count==1 && ps->isPlaying(),"EffectShown missing");
        ps->hide();require(psHidden.count==1 && !ps->isPlaying(),"EffectHidden missing");
        ps->show();ps->rePlay();require(psShown.count==3 && psHidden.count==2,"rePlay did not restart");
    });
    auto* target=static_cast<FalagardDragDropTarget*>(wm.createWindow("Falagard/DragDropTarget","Target"));target->setLookNFeel("Falagard/DragDropTarget","HudTarget");
    MeshShownCapture hoverChanged;target->subscribeEvent(FalagardDragDropTarget::EventDragDropTargetChanged,Event::Subscriber(&MeshShownCapture::receive,&hoverChanged));
    test("DragDropTarget HoverState property and drag enter/leave",[&]{
        target->setProperty("HoverState","True");require(target->GetHovering() && target->getProperty("HoverState")=="True" && hoverChanged.count==1,"HoverState property wrong");
        target->SetHovering(false);require(!target->GetHovering() && hoverChanged.count==2,"drag leave ignored");
        draw(target);require(wm.createWindow("Falagard/DragContainer","DragItem")->testClassName("DragContainer"),"drag container missing");
    });
    auto* ach=static_cast<FalagardAchievementWindow*>(wm.createWindow("Falagard/AchievementWindow","Ach"));ach->setSize(Absolute,Size(200,40));ach->setLookNFeel("Falagard/AchievementWindow","HudList");
    test("AchievementWindow creates elements from ChildItem keys and clears them",[&]{
        ach->setProperty("ChildItem","key:Row name:DefaultWindow");
        Window* r1=ach->AddChildElement("Row");Window* r2=ach->AddChildElement("Row");
        require(r1 && r2 && ach->getItemCount()==2 && r2->getAbsoluteYPosition()==20 && r1->getParent()==ach->getContainer(),"elements not stacked");
        require(ach->AddChildElement("Nope")==0,"unknown key must return null");
        ach->ClearAllElement();require(ach->getItemCount()==0 && !wm.isWindowPresent("Ach__auto_element0"),"clear failed");
    });
    test("AchievementWindow element names end in digits and restart after clear",[&]{
        ach->setProperty("ChildItem","key:Row name:DefaultWindow");
        for(int i=0;i<3;++i){
            Window* w=ach->AddChildElement("Row");require(w!=0,"add failed");
            char want[32];std::sprintf(want,"Ach__auto_element%d",i);
            require(w->getName()==want && wm.isWindowPresent(want),"element name not CY-compatible");
            const char* tag=std::strstr(w->getName().c_str(),"_auto_element");require(tag!=0,"tag missing");
            char* end=0;const long n=std::strtol(tag+13,&end,10);
            require(end!=tag+13 && *end==0 && n==i,"suffix not an integer");
        }
        ach->ClearAllElement();
        Window* w=ach->AddChildElement("Row");require(w && w->getName()=="Ach__auto_element0","index not reset after clear");
        ach->ClearAllElement();
    });
    auto* tripper=static_cast<FalagardTripperTooltip*>(wm.createWindow("Falagard/TripperTooltip","Tripper"));tripper->setLookNFeel("Falagard/TripperTooltip","HudTooltip");
    test("TripperTooltip keeps one text line per request id",[&]{
        tripper->setTripperText(2,"two");tripper->setTripperText(1,"one");require(tripper->getText()=="one\ntwo","lines not ordered by id");
        tripper->clearTripperText(1);require(tripper->getText()=="two" && tripper->getTripperCount()==1,"clear failed");
        require(wm.createWindow("Falagard/SuperBarButton","Sweep")->testClassName("FalagardSuperBarButton"),"SuperBarButton missing");
    });
    Window* resize=nullptr;
    test("ResizeText accepts GL MinWidth/MaxWidth and renders without frame images",[&]{
        typedef void (*RegisterFactory)(const String&);
        RegisterFactory reg=reinterpret_cast<RegisterFactory>(GetProcAddress(GetModuleHandleA("CEGUIFalagard.dll"),"registerFactory"));
        require(reg!=nullptr,"Falagard registerFactory export missing");reg("Falagard/SuperTooltip/ResizeText");
        resize=wm.createWindow("Falagard/SuperTooltip/ResizeText","Resize");
        resize->setSize(Absolute,Size(300,59));resize->setProperty("MaxWidth","202");resize->setProperty("MinWidth","202");
        require(resize->getAbsoluteWidth()==202 && resize->getProperty("MaxWidth")=="202","width not clamped");
        draw(resize);
    });
    auto* simple=static_cast<FalagardSimpleText*>(wm.createWindow("Falagard/SimpleText","Simple"));
    test("GL layout properties without native effect are stored per window instead of throwing",[&]{
        simple->setProperty("ColorTrans","True");require(simple->getProperty("ColorTrans")=="True" && cysStoredProperty(simple,"ColorTrans")=="True","stored property lost");
        require(simple->getPropertyDefault("VertScrollbar")=="False" && simple->getProperty("VertScrollbar")=="False","default of an unset stored property wrong");
        info->setProperty("ChildItem","key:Child name:TLBB_ChatBoard");info->setProperty("ChildItem","key:Other name:X");
        require(info->getProperty("ChildItem")=="key:Child name:TLBB_ChatBoard\nkey:Other name:X","repeated stored property not accumulated");
        bool threw=false;try{simple->setProperty("NoSuchPropertyXYZ","1");}catch(const UnknownObjectException&){threw=true;}require(threw,"unknown properties must still throw");
    });
    // CY item buttons: looks with only EnabledBackground/DisabledBackground and an ImagerySection hover (SelectServer).
    fm.addFactory(new FalagardActionButtonItemFactory);
    fm.addFactory(new FalagardAchievementWindowCatalogButtonFactory);
    fm.addFactory(new FalagardAchievementWindowTextButtonFactory);
    std::ofstream("cyitem.looknfeel",std::ios::binary) << R"xml(<Falagard><WidgetLook name="CyBackgroundLook">
<ImagerySection name="Bg"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="First"/></ImageryComponent></ImagerySection>
<ImagerySection name="BgOff"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="16"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="Second"/></ImageryComponent></ImagerySection>
<ImagerySection name="HoverSec"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="32"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="First"/></ImageryComponent></ImagerySection>
<StateImagery name="EnabledBackground"><Layer><Section section="Bg"/></Layer></StateImagery><StateImagery name="DisabledBackground"><Layer><Section section="BgOff"/></Layer></StateImagery>
</WidgetLook><WidgetLook name="CyNormalLook">
<ImagerySection name="N"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="0"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="First"/></ImageryComponent></ImagerySection>
<ImagerySection name="S"><ImageryComponent><Area><Dim type="LeftEdge"><AbsoluteDim value="16"/></Dim><Dim type="TopEdge"><AbsoluteDim value="0"/></Dim><Dim type="Width"><AbsoluteDim value="16"/></Dim><Dim type="Height"><AbsoluteDim value="16"/></Dim></Area><Image imageset="Fixture" image="Second"/></ImageryComponent></ImagerySection>
<StateImagery name="Normal"><Layer><Section section="N"/></Layer></StateImagery><StateImagery name="Selected"><Layer><Section section="S"/></Layer></StateImagery>
</WidgetLook></Falagard>)xml";
    WidgetLookManager::getSingleton().parseLookNFeelSpecification("cyitem.looknfeel");
    auto setHover=[&](Window* w,bool on){static_cast<ButtonBase*>(w)->*HoverAccess::flag()=on;w->requestRedraw();};
    for (const char* type : {"Falagard/AchievementWindowCatalogButton","Falagard/AchievementWindowTextButton"}) {
        const bool catalog=std::strstr(type,"Catalog")!=nullptr;
        const char* hoverProperty=catalog?"AchievementWindowCatalogButtonHover":"AchievementWindowButtonTextHover";
        test(catalog?"CY catalog button renders background-only look and its CY hover section":"CY text button renders background-only look and its CY hover section",[&]{
            Window* w=wm.createWindow(type,catalog?"CyCatalogItem":"CyTextItem");
            w->setLookNFeel(type,"CyBackgroundLook");w->setProperty("AbsoluteSize","w:64 h:16");
            w->setProperty(hoverProperty,"HoverSec");require(w->getProperty("HoverSection")=="HoverSec","CY hover alias not shared with HoverSection");
            draw(w);require(renderer.quads.size()==1 && renderer.quads[0].d_left==0,"enabled background not rendered");
            setHover(w,true);draw(w);require(renderer.quads.size()==2 && (renderer.quads[0].d_left==32 || renderer.quads[1].d_left==32),"hover imagery section not rendered");
            setHover(w,false);w->setEnabled(false);draw(w);require(renderer.quads.size()==1 && renderer.quads[0].d_left==16,"disabled background not rendered");
            w->setEnabled(true);
        });
    }
    // Passes before and after the fix by design: a look with "Normal" must stay on FalagardButton::drawSelf,
    // which ignores ItemSelected; populateRenderCache would draw "Selected" at left 16 instead.
    test("CY ActionButtonItem with a Normal look keeps the FalagardButton render path",[&]{
        Window* w=wm.createWindow("Falagard/ActionButtonItem","CyActionItem");
        w->setLookNFeel("Falagard/ActionButtonItem","CyNormalLook");w->setProperty("AbsoluteSize","w:64 h:16");
        w->setProperty("ItemSelected","True");draw(w);
        require(renderer.quads.size()==1 && renderer.quads[0].d_left==0,"Normal look left the FalagardButton path");
    });
    Window::setClickSoundCallback(nullptr);
    wm.destroyAllWindows();
    wm.cleanDeadPool();
    std::printf("RESULT passed=%d failed=%d\n",passed,failed);
    return failed?1:0;
}
