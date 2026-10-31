// CYS-CYGUI: Falagard window types of CY 3.71 that client_GL does not have. Evidence in the header and in
// Docs/specs/ui/cy-gui_vi.md. Rule (Timeline bay 8.2): an unknown property is logged and kept, never thrown.
#include "FalagardCyWidgets.h"
#include "CEGUIPropertyHelper.h"
#include "CEGUIWindowManager.h"
#include "CEGUILogger.h"
#include "CEGUIExceptions.h"
#include "CEGUIImagesetManager.h"
#include "CEGUIImageset.h"
#include "CEGUIFontBase.h"
#include "elements/CEGUIScrollbar.h"
#include "falagard/CEGUIFalWidgetLookManager.h"
#include "falagard/CEGUIFalWidgetLookFeel.h"
#include <algorithm>
#include <cstdio>

namespace CEGUI
{
    namespace
    {
        void renderLookState(Window& w, const String& look, const char* state, const char* fallback)
        {
            if (look.empty()) return;
            const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(look);
            if (wlf.isStateImageryPresent(state)) wlf.getStateImagery(state).render(w);
            else if (fallback && wlf.isStateImageryPresent(fallback)) wlf.getStateImagery(fallback).render(w);
        }

        const Image* imageFromSpec(const String& value)
        {
            // "set:IMAGESET image:IMAGE" (the CY/GL layout spelling) or "IMAGESET/IMAGE".
            char set[128] = { 0 }, image[128] = { 0 };
            if (std::sscanf(value.c_str(), "set:%127s image:%127s", set, image) == 2 ||
                std::sscanf(value.c_str(), "%127[^/]/%127s", set, image) == 2)
            {
                try
                {
                    return &ImagesetManager::getSingleton().getImageset(set)->getImage(image);
                }
                catch (Exception& e)
                {
                    Logger::getSingleton().logEvent("CY widget: image '" + value + "' - " + e.getMessage(), Errors);
                }
            }
            return 0;
        }

        // --- shared property helpers ------------------------------------------------------------
        template <class T> class BoolProp : public Property
        {
        public:
            typedef void (T::*Setter)(bool);
            typedef bool (T::*Getter)() const;
            BoolProp(const char* n, const char* h, const char* d, Setter s, Getter g)
                : Property(n, h, d), d_set(s), d_get(g) {}
            String get(const PropertyReceiver* r) const
            { return PropertyHelper::boolToString((static_cast<const T*>(r)->*d_get)()); }
            void set(PropertyReceiver* r, const String& v)
            { (static_cast<T*>(r)->*d_set)(PropertyHelper::stringToBool(v)); }
        private:
            Setter d_set; Getter d_get;
        };

        template <class T> class FloatProp : public Property
        {
        public:
            typedef void (T::*Setter)(float);
            typedef float (T::*Getter)() const;
            FloatProp(const char* n, const char* h, const char* d, Setter s, Getter g)
                : Property(n, h, d), d_set(s), d_get(g) {}
            String get(const PropertyReceiver* r) const
            { return PropertyHelper::floatToString((static_cast<const T*>(r)->*d_get)()); }
            void set(PropertyReceiver* r, const String& v)
            { (static_cast<T*>(r)->*d_set)(PropertyHelper::stringToFloat(v)); }
        private:
            Setter d_set; Getter d_get;
        };

        template <class T> class IntProp : public Property
        {
        public:
            typedef void (T::*Setter)(int);
            typedef int (T::*Getter)() const;
            IntProp(const char* n, const char* h, const char* d, Setter s, Getter g)
                : Property(n, h, d), d_set(s), d_get(g) {}
            String get(const PropertyReceiver* r) const
            { return PropertyHelper::intToString((static_cast<const T*>(r)->*d_get)()); }
            void set(PropertyReceiver* r, const String& v)
            { (static_cast<T*>(r)->*d_set)(PropertyHelper::stringToInt(v)); }
        private:
            Setter d_set; Getter d_get;
        };

        template <class T> class StringProp : public Property
        {
        public:
            typedef void (T::*Setter)(const String&);
            typedef const String& (T::*Getter)() const;
            StringProp(const char* n, const char* h, const char* d, Setter s, Getter g)
                : Property(n, h, d), d_set(s), d_get(g) {}
            String get(const PropertyReceiver* r) const { return (static_cast<const T*>(r)->*d_get)(); }
            void set(PropertyReceiver* r, const String& v) { (static_cast<T*>(r)->*d_set)(v); }
        private:
            Setter d_set; Getter d_get;
        };

        //! A base class may already own a property of that name (MultiColumnList has SelectColour): adding it a
        //! second time throws, so every CY property is registered only when the factory does not have it yet.
#define CYS_ADD_PROP(prop) \
        if (d_windowFactory && !d_windowFactory->isPropertyPresent((prop)->getName())) d_windowFactory->addProperty(prop)

        //! Property that only keeps its value on the window (CY behaviour not implemented yet).
        class KeptProperty : public Property
        {
        public:
            KeptProperty(const char* n, const char* h, const char* d) : Property(n, h, d) {}
            String get(const PropertyReceiver* r) const
            {
                std::map<const PropertyReceiver*, String>::const_iterator i = d_values.find(r);
                return i == d_values.end() ? getDefault(0) : i->second;
            }
            void set(PropertyReceiver* r, const String& v) { d_values[r] = v; }
        private:
            mutable std::map<const PropertyReceiver*, String> d_values;
        };
    }

    // ==================================================================== MeshWindow parent frame
    namespace
    {
        BoolProp<FalagardMeshWindowParentWindow> s_mwpRound("RoundEnabled",
            "CY: True - the portrait inside the frame is clipped to a round mask.", "False",
            &FalagardMeshWindowParentWindow::setRoundEnabled, &FalagardMeshWindowParentWindow::isRoundEnabled);
        StringProp<FalagardMeshWindowParentWindow> s_mwpMask("MaskImage",
            "CY: \"set:S image:I\" mask drawn over the portrait.", "",
            &FalagardMeshWindowParentWindow::setMaskImage, &FalagardMeshWindowParentWindow::getMaskImage);
    }

    const utf8 FalagardMeshWindowParentWindow::WidgetTypeName[] = "Falagard/MeshWindow/ParentWindow";
    const String FalagardMeshWindowParentWindow::EventNamespace("MeshWindowParentWindow");

    FalagardMeshWindowParentWindow::FalagardMeshWindowParentWindow(const String& type, const String& name)
        : Window(type, name), d_round(false)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardMeshWindowParentWindow)
            CYS_ADD_PROP(&s_mwpRound);
            CYS_ADD_PROP(&s_mwpMask);
        CEGUI_END_ADD_STATICPROPERTY
    }

    void FalagardMeshWindowParentWindow::populateRenderCache()
    {
        renderLookState(*this, d_lookName, isDisabled() ? "Disabled" : "Enabled", 0);
        if (!d_maskImage.empty())
        {
            const Image* img = imageFromSpec(d_maskImage);
            if (img)
            {
                Rect area(Point(0, 0), getAbsoluteSize());
                ColourRect cols(colour(1, 1, 1, d_alpha));
                d_renderCache.cacheImage(*img, area, 0, cols, 0, false);
            }
        }
    }

    // ==================================================================== element grid windows
    namespace
    {
        IntProp<FalagardElementGridWindow> s_gridColumn("Column",
            "CY: number of element columns of the window.", "1",
            &FalagardElementGridWindow::setColumn, &FalagardElementGridWindow::getColumn);
        FloatProp<FalagardElementGridWindow> s_gridHPad("HorizontalPadding",
            "CY: horizontal gap in pixels between two elements.", "0",
            &FalagardElementGridWindow::setHorizontalPadding, &FalagardElementGridWindow::getHorizontalPadding);
        FloatProp<FalagardElementGridWindow> s_gridVPad("VerticalPadding",
            "CY: vertical gap in pixels between two element rows.", "0",
            &FalagardElementGridWindow::setVerticalPadding, &FalagardElementGridWindow::getVerticalPadding);
    }

    const utf8 FalagardActionButtonWindow::WidgetTypeName[] = "Falagard/ActionButtonWindow";
    const utf8 FalagardEventsWindow::WidgetTypeName[] = "Falagard/EventsWindow";

    FalagardElementGridWindow::FalagardElementGridWindow(const String& type, const String& name)
        : FalagardAchievementWindow(type, name), d_column(1), d_hPadding(0), d_vPadding(0)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardElementGridWindow)
            CYS_ADD_PROP(&s_gridColumn);
            CYS_ADD_PROP(&s_gridHPad);
            CYS_ADD_PROP(&s_gridVPad);
        CEGUI_END_ADD_STATICPROPERTY
    }

    float FalagardElementGridWindow::contentHeight() const
    {
        const int columns = d_column > 0 ? d_column : 1;
        float height = 0, rowHeight = 0;
        int inRow = 0;
        for (size_t i = 0; i < getItemCount(); ++i)
        {
            const Window* w = getItemWindow(i);
            if (!w) continue;
            rowHeight = std::max(rowHeight, w->getAbsoluteHeight());
            if (++inRow == columns)
            {
                height += rowHeight + d_vPadding;
                rowHeight = 0;
                inRow = 0;
            }
        }
        return height + rowHeight;
    }

    void FalagardElementGridWindow::performWindowLayout()
    {
        if (d_inLayout) return;
        d_inLayout = true;
        const Rect client = getClientRectArea();
        Window* container = getContainer();
        if (container)
        {
            container->setPosition(Absolute, Point(client.d_left, client.d_top));
            container->setSize(Absolute, Size(std::max(1.0f, client.getWidth()), std::max(1.0f, client.getHeight())));
        }
        Scrollbar* bar = getVertScrollbar();
        const float scroll = bar ? bar->getScrollPosition() : 0.0f;
        const int columns = d_column > 0 ? d_column : 1;
        const float originX = container ? 0.0f : client.d_left;
        const float originY = container ? 0.0f : client.d_top;
        float x = 0, y = 0, rowHeight = 0;
        int inRow = 0;
        for (size_t i = 0; i < getItemCount(); ++i)
        {
            Window* w = getItemWindow(i);
            if (!w) continue;
            w->setPosition(Absolute, Point(originX + x, originY + y - scroll));
            x += w->getAbsoluteWidth() + d_hPadding;
            rowHeight = std::max(rowHeight, w->getAbsoluteHeight());
            if (++inRow == columns)
            {
                x = 0;
                y += rowHeight + d_vPadding;
                rowHeight = 0;
                inRow = 0;
            }
        }
        d_inLayout = false;
        requestRedraw();
    }

    // ==================================================================== composite item buttons
    namespace
    {
        StringProp<FalagardCyItemButton> s_itemHover("HoverSection",
            "CY: name of the imagery section drawn while the mouse is over the item.", "",
            &FalagardCyItemButton::setHoverSection, &FalagardCyItemButton::getHoverSection);
        // CY layouts/Lua spell the hover property per class (SelectServer.lua); both alias HoverSection.
        StringProp<FalagardCyItemButton> s_itemCatalogHover("AchievementWindowCatalogButtonHover",
            "CY: alias of HoverSection.", "",
            &FalagardCyItemButton::setHoverSection, &FalagardCyItemButton::getHoverSection);
        StringProp<FalagardCyItemButton> s_itemTextHover("AchievementWindowButtonTextHover",
            "CY: alias of HoverSection.", "",
            &FalagardCyItemButton::setHoverSection, &FalagardCyItemButton::getHoverSection);
        BoolProp<FalagardCyItemButton> s_itemSelected("ItemSelected",
            "CY: True - the item is the selected one (SetButtonSelected).", "False",
            &FalagardCyItemButton::setItemSelected, &FalagardCyItemButton::isItemSelected);
    }

    const String FalagardCyItemButton::EventNamespace("CyItemButton");
    const String FalagardCyItemButton::EventElementButton1Click("ElementButton1Click");

    const utf8 FalagardActionButtonItem::WidgetTypeName[] = "Falagard/ActionButtonItem";
    const utf8 FalagardGuildButton::WidgetTypeName[] = "Falagard/GuildButton";
    const utf8 FalagardAchievementBarWindowButton::WidgetTypeName[] = "Falagard/AchievementBarWindowButton";
    const utf8 FalagardAchievementBarCompareWindowButton::WidgetTypeName[] =
        "Falagard/AchievementBarCompareWindowButton";
    const utf8 FalagardAchievementBarCompareWindowIntegrationButton::WidgetTypeName[] =
        "Falagard/AchievementBarCompareWindowIntegrationButton";
    const utf8 FalagardAchievementWindowCatalogButton::WidgetTypeName[] = "Falagard/AchievementWindowCatalogButton";
    const utf8 FalagardAchievementWindowTextButton::WidgetTypeName[] = "Falagard/AchievementWindowTextButton";

    FalagardCyItemButton::FalagardCyItemButton(const String& type, const String& name)
        : FalagardButton(type, name), d_selected(false)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardCyItemButton)
            CYS_ADD_PROP(&s_itemHover);
            CYS_ADD_PROP(&s_itemCatalogHover);
            CYS_ADD_PROP(&s_itemTextHover);
            CYS_ADD_PROP(&s_itemSelected);
        CEGUI_END_ADD_STATICPROPERTY
        addEvent(EventElementButton1Click);
    }

    Window* FalagardCyItemButton::childByName(const char* child) const
    {
        if (!child || !*child) return const_cast<FalagardCyItemButton*>(this);
        const String full = getName() + child;
        WindowManager& wm = WindowManager::getSingleton();
        if (wm.isWindowPresent(full)) return wm.getWindow(full);
        if (wm.isWindowPresent(child)) return wm.getWindow(child);
        return 0;
    }

    void FalagardCyItemButton::setChildText(const char* child, const String& text)
    {
        Window* w = childByName(child);
        if (w) w->setText(text);
    }

    void FalagardCyItemButton::setChildVisible(const char* child, bool visible)
    {
        Window* w = childByName(child);
        if (w) w->setVisible(visible);
    }

    void FalagardCyItemButton::setChildImage(const char* child, const String& image)
    {
        Window* w = childByName(child);
        if (w && w->isPropertyPresent("Image")) w->setProperty("Image", image);
        else if (w && w->isPropertyPresent("NormalImage")) w->setProperty("NormalImage", image);
    }

    void FalagardCyItemButton::setItemSelected(bool selected)
    {
        if (d_selected == selected) return;
        d_selected = selected;
        requestRedraw();
    }

    void FalagardCyItemButton::drawSelf(float z)
    {
        // FalagardButton draws only Normal/Hover/Pushed/Disabled and silently draws nothing for the CY looks
        // (EnabledBackground/DisabledBackground only, FalButton.cpp doButtonRender), so those go through
        // Window::drawSelf -> populateRenderCache. Looks with "Normal" keep the old path unchanged.
        WidgetLookManager& looks = WidgetLookManager::getSingleton();
        if (d_needsRedraw && !d_lookName.empty() && looks.isWidgetLookAvailable(d_lookName) &&
            !looks.getWidgetLook(d_lookName).isStateImageryPresent("Normal"))
            Window::drawSelf(z);
        else
            FalagardButton::drawSelf(z);
    }

    void FalagardCyItemButton::populateRenderCache()
    {
        if (d_lookName.empty()) return;
        const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(d_lookName);
        const char* state = "Normal";
        if (isDisabled()) state = "Disabled";
        else if (isPushed()) state = "Pushed";
        else if (d_selected && wlf.isStateImageryPresent("Selected")) state = "Selected";
        else if (isHovering() && wlf.isStateImageryPresent("Hover")) state = "Hover";
        const char* background = isDisabled() ? "DisabledBackground" : "EnabledBackground";
        if (wlf.isStateImageryPresent(state)) wlf.getStateImagery(state).render(*this);
        else if (!isDisabled() && wlf.isStateImageryPresent("Normal")) wlf.getStateImagery("Normal").render(*this);
        else if (wlf.isStateImageryPresent(background)) wlf.getStateImagery(background).render(*this);
        if (d_hoverSection.empty() || !isHovering() || isDisabled()) return;
        if (wlf.isStateImageryPresent(d_hoverSection))
        {
            wlf.getStateImagery(d_hoverSection).render(*this);
            return;
        }
        // CY hover targets an ImagerySection; 0.4.1 has no isImagerySectionPresent, so a missing name is ignored.
        try
        {
            wlf.getImagerySection(d_hoverSection).render(*this, -0.0000001f); // z of layer priority 1: above the background
        }
        catch (UnknownObjectException&) {}
    }

    // ==================================================================== list variants
    namespace
    {
        KeptProperty s_mclCreateButtonType("CreateButtonType",
            "CY: window type MultiColumnListNew creates for button cells.", "");
        KeptProperty s_mclImageSize("SetImageSize", "CY: pixel size of image cells.", "");
        KeptProperty s_mclButtonSize("SetButtonSize", "CY: pixel size of button cells.", "");
        KeptProperty s_mclLineDrawOffset("LineDrawOffset", "CY: pixel offset of the row separator.", "0");
        KeptProperty s_mclSelectColour("SelectColour", "CY: selection colour of the list rows.", "FFFFFFFF");
        KeptProperty s_mclShowImageType("ShowImageType",
            "CY: \"type:N set:S image:I\" - image drawn for an image cell of that type.", "");
        KeptProperty s_mclChildItem("ChildItem",
            "CY: \"key:K name:WindowType\" - window type created for a cell of kind K.", "");
    }

    const utf8 FalagardMultiColumnListNew::WidgetTypeName[] = "Falagard/MultiColumnListNew";
    const utf8 FalagardMultiColumnListEx::WidgetTypeName[] = "Falagard/MultiColumnListEx";

    FalagardMultiColumnListNew::FalagardMultiColumnListNew(const String& type, const String& name)
        : FalagardMultiColumnList(type, name)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardMultiColumnListNew)
            CYS_ADD_PROP(&s_mclCreateButtonType);
            CYS_ADD_PROP(&s_mclImageSize);
            CYS_ADD_PROP(&s_mclButtonSize);
            CYS_ADD_PROP(&s_mclLineDrawOffset);
            CYS_ADD_PROP(&s_mclSelectColour);
            CYS_ADD_PROP(&s_mclShowImageType);
            CYS_ADD_PROP(&s_mclChildItem);
        CEGUI_END_ADD_STATICPROPERTY
    }

    FalagardMultiColumnListEx::FalagardMultiColumnListEx(const String& type, const String& name)
        : FalagardMultiColumnList(type, name)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardMultiColumnListEx)
            CYS_ADD_PROP(&s_mclCreateButtonType);
            CYS_ADD_PROP(&s_mclImageSize);
            CYS_ADD_PROP(&s_mclButtonSize);
            CYS_ADD_PROP(&s_mclLineDrawOffset);
            CYS_ADD_PROP(&s_mclSelectColour);
            CYS_ADD_PROP(&s_mclShowImageType);
            CYS_ADD_PROP(&s_mclChildItem);
        CEGUI_END_ADD_STATICPROPERTY
    }

    // ==================================================================== scrolling notice board
    namespace
    {
        IntProp<FalagardScrollText> s_stSpeed("Speed", "CY: scroll speed in pixels per second.", "0",
            &FalagardScrollText::setSpeed, &FalagardScrollText::getSpeed);
        IntProp<FalagardScrollText> s_stMode("TextMode", "CY: 0 = static, 1 = scrolling text mode.", "0",
            &FalagardScrollText::setTextMode, &FalagardScrollText::getTextMode);
        IntProp<FalagardScrollText> s_stNumber("TextBoardNumber", "CY: number of text boards kept.", "1",
            &FalagardScrollText::setTextBoardNumber, &FalagardScrollText::getChatBoardNumber);
        FloatProp<FalagardScrollText> s_stHeight("TextBoardHeight", "CY: pixel height of one text board.", "0",
            &FalagardScrollText::setTextBoardHeight, &FalagardScrollText::getTextBoardHeight);
        KeptProperty s_stChildItem("ChildItem",
            "CY: \"key:Child name:WindowType\" - board type ScrollText draws each notice with.", "");
        IntProp<FalagardScrollText> s_stKill("BoardKillTimer", "CY: seconds a board stays before it is dropped.", "0",
            &FalagardScrollText::setBoardKillTimer, &FalagardScrollText::getBoardKillTimer);
    }

    const utf8 FalagardScrollText::WidgetTypeName[] = "Falagard/ScrollText";
    const String FalagardScrollText::EventNamespace("ScrollText");
    const String FalagardScrollText::EventInfoElementClick("InfoElementClick");
    const String FalagardScrollText::EventInfoElementDelete("InfoElementDelete");
    const String FalagardScrollText::EventInfoElementMoveIn("InfoElementMoveIn");
    const String FalagardScrollText::EventInfoElementMoveOut("InfoElementMoveOut");

    FalagardScrollText::FalagardScrollText(const String& type, const String& name)
        : Window(type, name), d_speed(0), d_textMode(0), d_boardNumber(1), d_boardHeight(0), d_killTimer(0),
          d_offset(0)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardScrollText)
            CYS_ADD_PROP(&s_stSpeed);
            CYS_ADD_PROP(&s_stMode);
            CYS_ADD_PROP(&s_stNumber);
            CYS_ADD_PROP(&s_stHeight);
            CYS_ADD_PROP(&s_stKill);
            CYS_ADD_PROP(&s_stChildItem);
        CEGUI_END_ADD_STATICPROPERTY
        addEvent(EventInfoElementClick);
        addEvent(EventInfoElementDelete);
        addEvent(EventInfoElementMoveIn);
        addEvent(EventInfoElementMoveOut);
    }

    Rect FalagardScrollText::getTextRenderArea() const
    {
        if (!d_lookName.empty())
        {
            const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(d_lookName);
            if (wlf.isNamedAreaDefined("TextArea"))
                return wlf.getNamedArea("TextArea").getArea().getPixelRect(*this);
        }
        return Rect(Point(0, 0), getAbsoluteSize());
    }

    void FalagardScrollText::updateSelf(float elapsed)
    {
        Window::updateSelf(elapsed);
        if (d_textMode != 0 && d_speed != 0)
        {
            const float height = d_boardHeight > 0 ? d_boardHeight * d_boardNumber : getAbsoluteHeight();
            d_offset += elapsed * static_cast<float>(d_speed);
            if (height > 0 && d_offset > height) d_offset -= height;
            requestRedraw();
        }
    }

    void FalagardScrollText::populateRenderCache()
    {
        renderLookState(*this, d_lookName, isDisabled() ? "Disabled" : "Enabled", 0);
        const FontBase* font = getFont();
        if (!font || d_text.empty()) return;
        Rect area(getTextRenderArea());
        Rect clipper(area);
        area.d_top -= d_offset;
        ColourRect cols(colour(1, 1, 1, d_alpha));
        d_renderCache.cacheText(this, d_text, font, WordWrapLeftAligned, area, 0, cols, &clipper, true, 1.0f, 1.0f);
    }

    // ==================================================================== corner resize grip
    namespace
    {
        StringProp<FalagardDragCornerButton> s_dcTarget("DragTarget",
            "CY: name of the window this grip resizes.", "",
            &FalagardDragCornerButton::setDragTarget, &FalagardDragCornerButton::getDragTarget);
        IntProp<FalagardDragCornerButton> s_dcMode("DragMode",
            "CY: 0 = width and height, 1 = width only, 2 = height only.", "0",
            &FalagardDragCornerButton::setDragMode, &FalagardDragCornerButton::getDragMode);
        BoolProp<FalagardDragCornerButton> s_dcLimited("TargetSizeLimited",
            "CY: True - the target size stays between MinWidth/MaxWidth and MiniHeight/MaxHeight.", "False",
            &FalagardDragCornerButton::setLimited, &FalagardDragCornerButton::getLimited);
        FloatProp<FalagardDragCornerButton> s_dcMinW("MinWidth", "CY: smallest target width in pixels.", "0",
            &FalagardDragCornerButton::setMinWidth, &FalagardDragCornerButton::getMinWidth);
        FloatProp<FalagardDragCornerButton> s_dcMaxW("MaxWidth", "CY: largest target width in pixels.", "0",
            &FalagardDragCornerButton::setMaxWidth, &FalagardDragCornerButton::getMaxWidth);
        FloatProp<FalagardDragCornerButton> s_dcMinH("MiniHeight", "CY: smallest target height in pixels.", "0",
            &FalagardDragCornerButton::setMiniHeight, &FalagardDragCornerButton::getMiniHeight);
        FloatProp<FalagardDragCornerButton> s_dcMaxH("MaxHeight", "CY: largest target height in pixels.", "0",
            &FalagardDragCornerButton::setMaxHeight, &FalagardDragCornerButton::getMaxHeight);
    }

    const utf8 FalagardDragCornerButton::WidgetTypeName[] = "Falagard/DragCornerButton";
    const String FalagardDragCornerButton::EventNamespace("DragCornerButton");

    FalagardDragCornerButton::FalagardDragCornerButton(const String& type, const String& name)
        : FalagardButton(type, name), d_mode(0), d_limited(false), d_dragging(false), d_dragPoint(0, 0),
          d_dragSize(0, 0), d_minWidth(0), d_maxWidth(0), d_minHeight(0), d_maxHeight(0)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardDragCornerButton)
            CYS_ADD_PROP(&s_dcTarget);
            CYS_ADD_PROP(&s_dcMode);
            CYS_ADD_PROP(&s_dcLimited);
            CYS_ADD_PROP(&s_dcMinW);
            CYS_ADD_PROP(&s_dcMaxW);
            CYS_ADD_PROP(&s_dcMinH);
            CYS_ADD_PROP(&s_dcMaxH);
        CEGUI_END_ADD_STATICPROPERTY
    }

    Window* FalagardDragCornerButton::target() const
    {
        if (d_target.empty()) return getParent();
        WindowManager& wm = WindowManager::getSingleton();
        if (wm.isWindowPresent(d_target)) return wm.getWindow(d_target);
        return getParent();
    }

    void FalagardDragCornerButton::onMouseButtonDown(MouseEventArgs& e)
    {
        FalagardButton::onMouseButtonDown(e);
        if (e.button == LeftButton && captureInput())
        {
            Window* t = target();
            d_dragging = t != 0;
            d_dragPoint = e.position;
            if (t) d_dragSize = t->getAbsoluteSize();
            e.handled = true;
        }
    }

    void FalagardDragCornerButton::onMouseMove(MouseEventArgs& e)
    {
        FalagardButton::onMouseMove(e);
        Window* t = d_dragging ? target() : 0;
        if (!t) return;
        Size size = d_dragSize;
        if (d_mode != 2) size.d_width += e.position.d_x - d_dragPoint.d_x;
        if (d_mode != 1) size.d_height += e.position.d_y - d_dragPoint.d_y;
        if (d_limited)
        {
            if (d_minWidth > 0) size.d_width = std::max(size.d_width, d_minWidth);
            if (d_maxWidth > 0) size.d_width = std::min(size.d_width, d_maxWidth);
            if (d_minHeight > 0) size.d_height = std::max(size.d_height, d_minHeight);
            if (d_maxHeight > 0) size.d_height = std::min(size.d_height, d_maxHeight);
        }
        size.d_width = std::max(1.0f, size.d_width);
        size.d_height = std::max(1.0f, size.d_height);
        t->setSize(Absolute, size);
        e.handled = true;
    }

    void FalagardDragCornerButton::onMouseButtonUp(MouseEventArgs& e)
    {
        FalagardButton::onMouseButtonUp(e);
        if (e.button == LeftButton && d_dragging)
        {
            releaseInput();
            e.handled = true;
        }
    }

    void FalagardDragCornerButton::onCaptureLost(WindowEventArgs& e)
    {
        FalagardButton::onCaptureLost(e);
        d_dragging = false;
    }

    // ==================================================================== soft keyboards
    namespace
    {
        const Image* keyImageSpec(const String& value, int& type)
        {
            char set[128] = { 0 }, image[128] = { 0 };
            if (std::sscanf(value.c_str(), "type:%d set:%127s image:%127s", &type, set, image) != 3)
            {
                Logger::getSingleton().logEvent("CY soft key image '" + value +
                    "' ignored (expected 'type:N set:S image:I').", Errors);
                return 0;
            }
            try
            {
                return &ImagesetManager::getSingleton().getImageset(set)->getImage(image);
            }
            catch (Exception& ex)
            {
                Logger::getSingleton().logEvent("CY soft key image '" + value + "' - " + ex.getMessage(), Errors);
                return 0;
            }
        }

        class SoftKeySizeProperty : public Property
        {
        public:
            SoftKeySizeProperty(const char* n) : Property(n, "CY: pixel size of one soft key ('w,h').", "") {}
            String get(const PropertyReceiver*) const { return ""; }
            void set(PropertyReceiver* r, const String& v)
            { static_cast<FalagardSoftKeyBoard*>(r)->setKeySize(PropertyHelper::stringToSize(v)); }
        };
        class SoftKeyImageProperty : public Property
        {
        public:
            enum Kind { Normal, Hover, Pushed };
            SoftKeyImageProperty(const char* n, Kind k)
                : Property(n, "CY: soft key image, 'type:N set:S image:I'.", ""), d_kind(k) {}
            String get(const PropertyReceiver*) const { return ""; }
            void set(PropertyReceiver* r, const String& v)
            {
                int type = -1;
                const Image* img = keyImageSpec(v, type);
                if (!img) return;
                FalagardSoftKeyBoard* kb = static_cast<FalagardSoftKeyBoard*>(r);
                if (d_kind == Normal) kb->setNormalImage(type, img);
                else if (d_kind == Hover) kb->setHoverImage(type, img);
                else kb->setPushedImage(type, img);
            }
        private:
            Kind d_kind;
        };

        SoftKeySizeProperty s_phoneKeySize("PhoneKeySize");
        SoftKeyImageProperty s_phoneNormal("PhoneNormalImage", SoftKeyImageProperty::Normal);
        SoftKeyImageProperty s_phoneHover("PhoneHoverImage", SoftKeyImageProperty::Hover);
        SoftKeyImageProperty s_phonePushed("PhonePushedImage", SoftKeyImageProperty::Pushed);
        SoftKeySizeProperty s_sfzKeySize("SFZKeySize");
        SoftKeyImageProperty s_sfzNormal("SFZNormalImage", SoftKeyImageProperty::Normal);
        SoftKeyImageProperty s_sfzHover("SFZHoverImage", SoftKeyImageProperty::Hover);
        SoftKeyImageProperty s_sfzPushed("SFZPushedImage", SoftKeyImageProperty::Pushed);
    }

    const utf8 FalagardPhoneSoftKeyBoard::WidgetTypeName[] = "Falagard/PhoneSoftKeyWindow";
    const utf8 FalagardSFZSoftKeyBoard::WidgetTypeName[] = "Falagard/SFZSoftKeyWindow";

    FalagardPhoneSoftKeyBoard::FalagardPhoneSoftKeyBoard(const String& type, const String& name)
        : FalagardSoftKeyBoard(type, name)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardPhoneSoftKeyBoard)
            CYS_ADD_PROP(&s_phoneKeySize);
            CYS_ADD_PROP(&s_phoneNormal);
            CYS_ADD_PROP(&s_phoneHover);
            CYS_ADD_PROP(&s_phonePushed);
        CEGUI_END_ADD_STATICPROPERTY
    }

    FalagardSFZSoftKeyBoard::FalagardSFZSoftKeyBoard(const String& type, const String& name)
        : FalagardSoftKeyBoard(type, name)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardSFZSoftKeyBoard)
            CYS_ADD_PROP(&s_sfzKeySize);
            CYS_ADD_PROP(&s_sfzNormal);
            CYS_ADD_PROP(&s_sfzHover);
            CYS_ADD_PROP(&s_sfzPushed);
        CEGUI_END_ADD_STATICPROPERTY
    }

    // ==================================================================== mirrored static image
    namespace
    {
        class MirrorFormatProperty : public Property
        {
        public:
            MirrorFormatProperty() : Property("MirrorFormat",
                "CY: None | Horizontal | Vertical | Both - mirrored UVs of the image.", "None") {}
            String get(const PropertyReceiver* r) const
            {
                switch (static_cast<const FalagardStaticUVImage*>(r)->getMirrorFormatting())
                {
                case FalagardStaticUVImage::MirrorHorizontal: return "Horizontal";
                case FalagardStaticUVImage::MirrorVertical: return "Vertical";
                case FalagardStaticUVImage::MirrorBoth: return "Both";
                default: return "None";
                }
            }
            void set(PropertyReceiver* r, const String& v)
            {
                FalagardStaticUVImage::MirrorFormatting f = FalagardStaticUVImage::MirrorNone;
                if (v == "Horizontal") f = FalagardStaticUVImage::MirrorHorizontal;
                else if (v == "Vertical") f = FalagardStaticUVImage::MirrorVertical;
                else if (v == "Both") f = FalagardStaticUVImage::MirrorBoth;
                static_cast<FalagardStaticUVImage*>(r)->setMirrorFormatting(f);
            }
        };
        MirrorFormatProperty s_mirrorFormat;
    }

    const utf8 FalagardStaticUVImage::WidgetTypeName[] = "Falagard/StaticUVImage";

    FalagardStaticUVImage::FalagardStaticUVImage(const String& type, const String& name)
        : FalagardStaticImage(type, name), d_mirror(MirrorNone)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardStaticUVImage)
            CYS_ADD_PROP(&s_mirrorFormat);
        CEGUI_END_ADD_STATICPROPERTY
    }

    void FalagardStaticUVImage::populateRenderCache()
    {
        // CYS-CYGUI: the mirrored UV draw of CY is not implemented; the image is drawn unmirrored and the
        // requested mode stays readable through the property.
        FalagardStaticImage::populateRenderCache();
    }

    // ==================================================================== radial progress
    namespace
    {
        class ProgressImageProperty : public Property
        {
        public:
            ProgressImageProperty() : Property("ProgressImage",
                "CY: \"set:S image:I\" image filled by the progress.", "") {}
            String get(const PropertyReceiver* r) const
            { return static_cast<const FalagardCircleProgress*>(r)->getProgressImage(); }
            void set(PropertyReceiver* r, const String& v)
            { static_cast<FalagardCircleProgress*>(r)->setProgressImage(v); }
        };
        ProgressImageProperty s_cpImage;
        FloatProp<FalagardCircleProgress> s_cpFill("FillDirection", "CY: start angle of the fill in degrees.", "0",
            &FalagardCircleProgress::setFillDirection, &FalagardCircleProgress::getFillDirection);
        BoolProp<FalagardCircleProgress> s_cpClock("ClockWise", "CY: True - the fill runs clockwise.", "True",
            &FalagardCircleProgress::setClockWise, &FalagardCircleProgress::getClockWise);
        BoolProp<FalagardCircleProgress> s_cpDown("DownDir", "CY: True - the fill runs downwards.", "False",
            &FalagardCircleProgress::setDownDir, &FalagardCircleProgress::getDownDir);
        BoolProp<FalagardCircleProgress> s_cpLight("LightEnable", "CY: True - a light follows the fill head.",
            "False", &FalagardCircleProgress::setLightEnable, &FalagardCircleProgress::getLightEnable);
        FloatProp<FalagardCircleProgress> s_cpRadius("LightRadius", "CY: radius of that light in pixels.", "0",
            &FalagardCircleProgress::setLightRadius, &FalagardCircleProgress::getLightRadius);
        FloatProp<FalagardCircleProgress> s_cpSpeed("MoveSpeed", "CY: speed of the automatic fill.", "0",
            &FalagardCircleProgress::setMoveSpeed, &FalagardCircleProgress::getMoveSpeed);
        BoolProp<FalagardCircleProgress> s_cpAuto("AutoMove", "CY: True - the progress advances on its own.",
            "False", &FalagardCircleProgress::setAutoMove, &FalagardCircleProgress::getAutoMove);
        FloatProp<FalagardCircleProgress> s_cpProgress("Progress", "CY: 0..1 filled fraction.", "0",
            &FalagardCircleProgress::setProgress, &FalagardCircleProgress::getProgress);
    }

    const utf8 FalagardCircleProgress::WidgetTypeName[] = "Falagard/CircleProgress";
    const String FalagardCircleProgress::EventNamespace("CircleProgress");

    FalagardCircleProgress::FalagardCircleProgress(const String& type, const String& name)
        : Window(type, name), d_image(0), d_fillDirection(0), d_lightRadius(0), d_moveSpeed(0), d_progress(0),
          d_clockWise(true), d_downDir(false), d_lightEnable(false), d_autoMove(false)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardCircleProgress)
            CYS_ADD_PROP(&s_cpImage);
            CYS_ADD_PROP(&s_cpFill);
            CYS_ADD_PROP(&s_cpClock);
            CYS_ADD_PROP(&s_cpDown);
            CYS_ADD_PROP(&s_cpLight);
            CYS_ADD_PROP(&s_cpRadius);
            CYS_ADD_PROP(&s_cpSpeed);
            CYS_ADD_PROP(&s_cpAuto);
            CYS_ADD_PROP(&s_cpProgress);
        CEGUI_END_ADD_STATICPROPERTY
    }

    void FalagardCircleProgress::setProgressImage(const String& image)
    {
        d_imageSpec = image;
        d_image = imageFromSpec(image);
        requestRedraw();
    }

    void FalagardCircleProgress::setProgress(float p)
    {
        d_progress = p < 0 ? 0 : (p > 1 ? 1 : p);
        requestRedraw();
    }

    void FalagardCircleProgress::updateSelf(float elapsed)
    {
        Window::updateSelf(elapsed);
        if (d_autoMove && d_moveSpeed != 0)
        {
            float p = d_progress + elapsed * d_moveSpeed;
            while (p > 1.0f) p -= 1.0f;
            setProgress(p);
        }
    }

    void FalagardCircleProgress::populateRenderCache()
    {
        renderLookState(*this, d_lookName, isDisabled() ? "Disabled" : "Enabled", 0);
        if (!d_image) return;
        // CYS-CYGUI: CY fills a ring; without a mesh renderer the fraction is drawn as a linear cut of the image
        // (vertical when DownDir, else horizontal). The angles (FillDirection/ClockWise) are kept for the game layer.
        const Size size = getAbsoluteSize();
        Rect area(Point(0, 0), size);
        if (d_downDir) area.d_bottom = area.d_top + size.d_height * d_progress;
        else area.d_right = area.d_left + size.d_width * d_progress;
        ColourRect cols(colour(1, 1, 1, d_alpha));
        Rect clipper(Point(0, 0), size);
        d_renderCache.cacheImage(*d_image, area, 0, cols, &clipper, false);
    }
}
