// CYS-HUD: Falagard/AchievementWindow. See the header for the evidence from client_GL.
#include "FalagardAchievementWindow.h"
#include "CEGUIWindowManager.h"
#include "CEGUILogger.h"
#include "CEGUIExceptions.h"
#include "elements/CEGUIScrollbar.h"
#include "falagard/CEGUIFalWidgetLookManager.h"
#include "falagard/CEGUIFalWidgetLookFeel.h"
#include <algorithm>
#include <cstdio>

namespace CEGUI
{
    namespace
    {
        // "key:AchievementCatalog name:TLBB_AchievementCatalog_Button" (look) - stored per key, like SceneMap ChildItem.
        class ChildItemProperty : public Property
        {
        public:
            ChildItemProperty() : Property("ChildItem", "type:key name:windowtype - window type created by AddChildElement(key).", "") {}
            String get(const PropertyReceiver*) const { return ""; }
            void set(PropertyReceiver* r, const String& v)
            {
                char key[128] = { 0 }, name[128] = { 0 };
                if (std::sscanf(v.c_str(), "key:%127s name:%127s", key, name) != 2)
                {
                    Logger::getSingleton().logEvent("AchievementWindow: ChildItem '" + v + "' ignored (expected 'key:K name:Type').", Errors);
                    return;
                }
                static_cast<FalagardAchievementWindow*>(r)->addChildItem(key, name);
            }
        };
        ChildItemProperty s_childItem;
        const char* const kAutoParent = "__auto_parent__";
        const char* const kAutoVScroll = "__auto_vscrollbar__";
    }

    const utf8 FalagardAchievementWindow::WidgetTypeName[] = "Falagard/AchievementWindow";
    const String FalagardAchievementWindow::EventNamespace("AchievementWindow");
    const String FalagardAchievementWindow::EventElementClick("ElementClick");
    const String FalagardAchievementWindow::EventElementLClick("ElementLClick");
    const String FalagardAchievementWindow::EventElementRClick("ElementRClick");
    const String FalagardAchievementWindow::EventElementMouseEnter("ElementMouseEnter");
    const String FalagardAchievementWindow::EventElementMouseLeave("ElementMouseLeave");

    FalagardAchievementWindow::FalagardAchievementWindow(const String& type, const String& name) :
        Window(type, name), d_container(0), d_vertScrollbar(0), d_clicked(-1), d_nextIndex(0), d_hideScrollbar(false), d_inLayout(false)
    {
        CEGUI_START_ADD_STATICPROPERTY(FalagardAchievementWindow)
            CEGUI_ADD_STATICPROPERTY(&s_childItem);
        CEGUI_END_ADD_STATICPROPERTY
        addEvent(EventElementClick);
        addEvent(EventElementLClick);
        addEvent(EventElementRClick);
        addEvent(EventElementMouseEnter);
        addEvent(EventElementMouseLeave);
        // element sub-button events subscribed by the GL layouts (SweepAll, SongliaoWarOver); fired by the game layer
        addEvent("EventElementCheckButton1Click");
        for (int i = 1; i <= 6; ++i) { char b[40]; std::sprintf(b, "EventElementButton%dClick", i); addEvent(b); }
    }

    FalagardAchievementWindow::~FalagardAchievementWindow() {}

    void FalagardAchievementWindow::initialise()
    {
        WindowManager& wm = WindowManager::getSingleton();
        if (!d_container && wm.isWindowPresent(getName() + kAutoParent))
        {
            d_container = wm.getWindow(getName() + kAutoParent);
            d_container->setClippedByParent(true);
        }
        if (!d_vertScrollbar && wm.isWindowPresent(getName() + kAutoVScroll))
        {
            d_vertScrollbar = static_cast<Scrollbar*>(wm.getWindow(getName() + kAutoVScroll));
            d_vertScrollbar->subscribeEvent(Scrollbar::EventScrollPositionChanged,
                Event::Subscriber(&FalagardAchievementWindow::handleScrollbarChange, this));
            d_vertScrollbar->setAlwaysOnTop(true);
        }
        configureScrollbars();
    }

    void FalagardAchievementWindow::onChildAdded(WindowEventArgs& e)
    {
        Window::onChildAdded(e);
        Window* child = e.window;
        if (!child) return;
        if (!d_container && child->getName() == getName() + kAutoParent)
        {
            d_container = child;
            d_container->setClippedByParent(true);
        }
        else if (!d_vertScrollbar && child->getName() == getName() + kAutoVScroll && child->testClassName("Scrollbar"))
        {
            d_vertScrollbar = static_cast<Scrollbar*>(child);
            d_vertScrollbar->subscribeEvent(Scrollbar::EventScrollPositionChanged, Event::Subscriber(&FalagardAchievementWindow::handleScrollbarChange, this));
            d_vertScrollbar->setAlwaysOnTop(true);
        }
    }

    String FalagardAchievementWindow::getChildItem(const String& key) const
    {
        std::map<String, String>::const_iterator i = d_childTypes.find(key);
        return i == d_childTypes.end() ? String("") : i->second;
    }

    Size FalagardAchievementWindow::GetWindowSizeByType(const String& key) const
    {
        if (!d_lookName.empty())
        {
            const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(d_lookName);
            if (wlf.isNamedAreaDefined(key))
            {
                const Rect r = wlf.getNamedArea(key).getArea().getPixelRect(*this);
                return Size(r.getWidth(), r.getHeight());
            }
        }
        return Size(getClientRectArea().getWidth(), 20.0f);
    }

    Window* FalagardAchievementWindow::AddChildElement(const String& key)
    {
        const String type = getChildItem(key);
        if (type.empty())
        {
            Logger::getSingleton().logEvent("AchievementWindow '" + getName() + "': no ChildItem for key '" + key + "'.", Errors);
            return 0;
        }
        WindowManager& wm = WindowManager::getSingleton();
        char buf[48];
        String name;
        // Lua parses tonumber(suffix after "_auto_element"), so the name must end in digits.
        do { std::sprintf(buf, "__auto_element%d", d_nextIndex++); name = getName() + buf; } while (wm.isWindowPresent(name));
        Window* w = 0;
        try
        {
            w = wm.createWindow(type, name);
            w->setDestroyedByParent(true);
            const Size size = GetWindowSizeByType(key);
            w->setSize(Absolute, size);
            w->setUserData(getUserData());
            w->subscribeEvent(Window::EventMouseLClick, Event::Subscriber(&FalagardAchievementWindow::handleItemLClick, this));
            w->subscribeEvent(Window::EventMouseRClick, Event::Subscriber(&FalagardAchievementWindow::handleItemRClick, this));
            w->subscribeEvent(Window::EventMouseEnters, Event::Subscriber(&FalagardAchievementWindow::handleItemEnter, this));
            w->subscribeEvent(Window::EventMouseLeaves, Event::Subscriber(&FalagardAchievementWindow::handleItemLeave, this));
            (d_container ? d_container : this)->addChildWindow(w);
        }
        catch (Exception& e)
        {
            Logger::getSingleton().logEvent("AchievementWindow '" + getName() + "': AddChildElement(" + key + ") failed - " + e.getMessage(), Errors);
            if (w) wm.destroyWindow(w);
            return 0;
        }
        d_items.push_back(w);
        configureScrollbars();
        return w;
    }

    void FalagardAchievementWindow::ClearAllElement()
    {
        std::vector<Window*> items;
        items.swap(d_items);
        for (size_t i = 0; i < items.size(); ++i) WindowManager::getSingleton().destroyWindow(items[i]);
        d_nextIndex = 0;
        d_clicked = -1;
        if (d_vertScrollbar) d_vertScrollbar->setScrollPosition(0);
        configureScrollbars();
    }

    int FalagardAchievementWindow::indexOf(const Window* w) const
    {
        for (size_t i = 0; i < d_items.size(); ++i) if (d_items[i] == w) return static_cast<int>(i);
        return -1;
    }

    bool FalagardAchievementWindow::handleItemLClick(const EventArgs& e)
    {
        Window* w = static_cast<const WindowEventArgs&>(e).window;
        d_clicked = indexOf(w);
        WindowEventArgs args(w);
        fireEvent(EventElementLClick, args, EventNamespace);
        fireEvent(EventElementClick, args, EventNamespace);
        return true;
    }
    bool FalagardAchievementWindow::handleItemRClick(const EventArgs& e)
    {
        Window* w = static_cast<const WindowEventArgs&>(e).window;
        d_clicked = indexOf(w);
        WindowEventArgs args(w);
        fireEvent(EventElementRClick, args, EventNamespace);
        fireEvent(EventElementClick, args, EventNamespace);
        return true;
    }
    bool FalagardAchievementWindow::handleItemEnter(const EventArgs& e)
    {
        WindowEventArgs args(static_cast<const WindowEventArgs&>(e).window);
        fireEvent(EventElementMouseEnter, args, EventNamespace);
        return true;
    }
    bool FalagardAchievementWindow::handleItemLeave(const EventArgs& e)
    {
        WindowEventArgs args(static_cast<const WindowEventArgs&>(e).window);
        fireEvent(EventElementMouseLeave, args, EventNamespace);
        return true;
    }

    Rect FalagardAchievementWindow::getClientRectArea() const
    {
        if (!d_lookName.empty())
        {
            const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(d_lookName);
            const bool scroll = d_vertScrollbar && d_vertScrollbar->isVisible();
            if (scroll && wlf.isNamedAreaDefined("ClientRectVScroll"))
                return wlf.getNamedArea("ClientRectVScroll").getArea().getPixelRect(*this);
            if (wlf.isNamedAreaDefined("ClientRect"))
                return wlf.getNamedArea("ClientRect").getArea().getPixelRect(*this);
        }
        Rect r(Point(0, 0), getAbsoluteSize());
        if (d_vertScrollbar && d_vertScrollbar->isVisible()) r.d_right -= d_vertScrollbar->getAbsoluteWidth();
        return r;
    }

    float FalagardAchievementWindow::contentHeight() const
    {
        float content = 0;
        for (size_t i = 0; i < d_items.size(); ++i) content += d_items[i]->getAbsoluteHeight();
        return content;
    }

    void FalagardAchievementWindow::configureScrollbars()
    {
        if (d_vertScrollbar)
        {
            const float content = contentHeight();
            const float page = getAbsoluteHeight();
            d_vertScrollbar->setVisible(!d_hideScrollbar && content > page + 0.5f);
            d_vertScrollbar->setDocumentSize(content);
            d_vertScrollbar->setPageSize(page);
            d_vertScrollbar->setStepSize(std::max(1.0f, page / 10.0f));
            d_vertScrollbar->setScrollPosition(d_vertScrollbar->getScrollPosition());
        }
        performWindowLayout();
    }

    void FalagardAchievementWindow::performWindowLayout()
    {
        if (d_inLayout) return;
        d_inLayout = true;
        const Rect client = getClientRectArea();
        if (d_container)
        {
            d_container->setPosition(Absolute, Point(client.d_left, client.d_top));
            d_container->setSize(Absolute, Size(std::max(1.0f, client.getWidth()), std::max(1.0f, client.getHeight())));
        }
        const float scroll = d_vertScrollbar ? d_vertScrollbar->getScrollPosition() : 0.0f;
        float y = 0;
        for (size_t i = 0; i < d_items.size(); ++i)
        {
            d_items[i]->setPosition(Absolute, Point(d_container ? 0.0f : client.d_left, y - scroll + (d_container ? 0.0f : client.d_top)));
            y += d_items[i]->getAbsoluteHeight();
        }
        d_inLayout = false;
        requestRedraw();
    }

    bool FalagardAchievementWindow::handleScrollbarChange(const EventArgs&) { performWindowLayout(); return true; }

    void FalagardAchievementWindow::SetElementsInView(size_t index)
    {
        if (!d_vertScrollbar || index >= d_items.size()) return;
        float y = 0;
        for (size_t i = 0; i < index; ++i) y += d_items[i]->getAbsoluteHeight();
        d_vertScrollbar->setScrollPosition(y);
    }

    void FalagardAchievementWindow::PageEnd()
    {
        if (d_vertScrollbar) d_vertScrollbar->setScrollPosition(d_vertScrollbar->getDocumentSize());
    }

    float FalagardAchievementWindow::GetVertScrollbarPosition() const { return d_vertScrollbar ? d_vertScrollbar->getScrollPosition() : 0.0f; }
    void FalagardAchievementWindow::SetVertScrollbarPosition(float position) { if (d_vertScrollbar) d_vertScrollbar->setScrollPosition(position); }
    void FalagardAchievementWindow::SetVertScrollbarHide(bool hide) { d_hideScrollbar = hide; configureScrollbars(); }

    void FalagardAchievementWindow::populateRenderCache()
    {
        if (d_lookName.empty()) return;
        const WidgetLookFeel& wlf = WidgetLookManager::getSingleton().getWidgetLook(d_lookName);
        const char* state = isDisabled() ? "Disabled" : "Enabled";
        if (wlf.isStateImageryPresent(state)) wlf.getStateImagery(state).render(*this);
    }

    void FalagardAchievementWindow::onSized(WindowEventArgs& e)
    {
        Window::onSized(e);
        configureScrollbars();
    }

    void FalagardAchievementWindow::onMouseWheel(MouseEventArgs& e)
    {
        Window::onMouseWheel(e);
        if (d_vertScrollbar && d_vertScrollbar->isVisible())
        {
            d_vertScrollbar->setScrollPosition(d_vertScrollbar->getScrollPosition() - d_vertScrollbar->getStepSize() * e.wheelChange);
            e.handled = true;
        }
    }
}
