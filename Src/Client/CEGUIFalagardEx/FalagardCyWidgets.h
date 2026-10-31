#pragma once
// CYS-CYGUI: the Falagard window types CY 3.71.9609 has and client_GL does not (user 14/09/2026: "Mọi thứ như CY").
//
// Evidence: exported mangled names of CY Bin64\CEGUIFalagard.dll (sha256 4adc5a34...cd1, copy in
// Build\re\cy-gui\bin), the FalagardMapping entries of the CY scheme Schema/WoWLookSkin.scheme.xml and the
// properties the CY layouts set on them (Build\Logs\2026-09-14-cy-gui\cy-window-types.tsv).
// Full table with the per-class exports: Docs\specs\ui\cy-gui_vi.md section 3.
//
//   Falagard/MeshWindow/ParentWindow  frame around a MeshWindow portrait (PlayerFrame, TargetFrame, PetFrame,
//                                     PartyFrame). CY exports FalagardMeshWindow::ParentWindow; layout properties
//                                     RoundEnabled, MaskImage, Mouse*Hollow.
//   Falagard/ActionButtonWindow       grid of item windows with a vertical scrollbar (AddChildElement /
//   Falagard/EventsWindow             ClearAllElement / UpdateAllElement / SetElementsInView / PageEnd /
//                                     Get|SetVertScrollbarPosition, properties Column, HorizontalPadding,
//                                     VerticalPadding) - same shape as the GL AchievementWindow, which is the base
//                                     class used here.
//   Falagard/ActionButtonItem         element of an ActionButtonWindow: image + three texts + one sub button
//                                     (SetImage, SetText1..3, SetButton1Text/Tips/State, ChildImage(Show|Hide),
//                                     events Button1Click, Button2Click, MouseRClick).
//   Falagard/AchievementBarWindowButton, .../AchievementBarCompareWindowButton,
//   .../AchievementBarCompareWindowIntegrationButton, .../AchievementWindowCatalogButton,
//   .../AchievementWindowTextButton, Falagard/GuildButton
//                                     composite item buttons created at run time by the Lua layer; the setters write
//                                     into the named child windows the look defines.
//   Falagard/MultiColumnListNew       MultiColumnList variants (CY adds ColumnHeader/CreateButtonType/SetImageSize/
//   Falagard/MultiColumnListEx        SetButtonSize/SelectColour/LineDrawOffset/DrawOffset).
//   Falagard/ScrollText               scrolling notice board (Speed, TextMode, TextBoardNumber, TextBoardHeight,
//                                     BoardKillTimer; events InfoElementClick/Delete/MoveIn/MoveOut) - ServerNew.
//   Falagard/DragCornerButton         corner grip that resizes DragTarget (DragMode, TargetSizeLimited,
//                                     MinWidth/MaxWidth/MiniHeight/MaxHeight) - ChatFrame.
//   Falagard/PhoneSoftKeyWindow       the soft keyboard of the 0.85/GL code with the CY property names
//   Falagard/SFZSoftKeyWindow         (PhoneKeySize/PhoneNormalImage/... and SFZKeySize/SFZNormalImage/...).
//   Falagard/StaticUVImage            StaticImage + MirrorFormat (horizontal/vertical mirrored UVs).
//   Falagard/CircleProgress           radial progress (ProgressImage, FillDirection, ClockWise, DownDir,
//                                     LightEnable, LightRadius, AutoMove, MoveSpeed).
#include "FalModule.h"
#include "CEGUIWindowFactory.h"
#include "FalagardAchievementWindow.h"
#include "FalagardSoftKeyBoard.h"
#include "FalStaticImage.h"
#include "FalButton.h"
#include "FalMultiColumnList.h"
#include <vector>

namespace CEGUI
{
    // ------------------------------------------------------------------ MeshWindow parent frame
    class FALAGARDBASE_API FalagardMeshWindowParentWindow : public Window
    {
    public:
        static const utf8 WidgetTypeName[];
        static const String EventNamespace;
        FalagardMeshWindowParentWindow(const String& type, const String& name);
        ~FalagardMeshWindowParentWindow() {}

        void setRoundEnabled(bool round) { d_round = round; requestRedraw(); }
        bool isRoundEnabled() const { return d_round; }
        void setMaskImage(const String& image) { d_maskImage = image; requestRedraw(); }
        const String& getMaskImage() const { return d_maskImage; }

    protected:
        virtual void populateRenderCache();
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardMeshWindowParentWindow") return true;
            return Window::testClassName_impl(class_name);
        }
        bool d_round;
        String d_maskImage;
    };

    // ------------------------------------------------------------------ element grid windows
    //! AchievementWindow + the CY grid properties (Column, HorizontalPadding, VerticalPadding).
    class FALAGARDBASE_API FalagardElementGridWindow : public FalagardAchievementWindow
    {
    public:
        FalagardElementGridWindow(const String& type, const String& name);

        void setColumn(int column) { d_column = column > 0 ? column : 1; performWindowLayout(); }
        int getColumn() const { return d_column; }
        void setHorizontalPadding(float p) { d_hPadding = p; performWindowLayout(); }
        float getHorizontalPadding() const { return d_hPadding; }
        void setVerticalPadding(float p) { d_vPadding = p; performWindowLayout(); }
        float getVerticalPadding() const { return d_vPadding; }

    protected:
        virtual void performWindowLayout();
        virtual float contentHeight() const;
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardElementGridWindow") return true;
            return FalagardAchievementWindow::testClassName_impl(class_name);
        }
        int d_column;
        float d_hPadding;
        float d_vPadding;
    };

    class FALAGARDBASE_API FalagardActionButtonWindow : public FalagardElementGridWindow
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardActionButtonWindow(const String& type, const String& name) : FalagardElementGridWindow(type, name) {}
    };

    class FALAGARDBASE_API FalagardEventsWindow : public FalagardElementGridWindow
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardEventsWindow(const String& type, const String& name) : FalagardElementGridWindow(type, name) {}
    };

    // ------------------------------------------------------------------ composite item buttons
    //! Button with the CY item setters; every Set* writes into the child window the look creates with that name.
    class FALAGARDBASE_API FalagardCyItemButton : public FalagardButton
    {
    public:
        static const String EventNamespace;
        static const String EventElementButton1Click;

        FalagardCyItemButton(const String& type, const String& name);

        //! sets the text of the child window \a child ("" = this window), UTF-8 as everything in the UI.
        void setChildText(const char* child, const String& text);
        void setChildVisible(const char* child, bool visible);
        void setChildImage(const char* child, const String& image);
        void setItemSelected(bool selected);
        bool isItemSelected() const { return d_selected; }
        const String& getHoverSection() const { return d_hoverSection; }
        void setHoverSection(const String& section) { d_hoverSection = section; requestRedraw(); }

    protected:
        //! Looks with a "Normal" state keep the FalagardButton path; CY looks (EnabledBackground only) use populateRenderCache.
        virtual void drawSelf(float z);
        virtual void populateRenderCache();
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardCyItemButton") return true;
            return FalagardButton::testClassName_impl(class_name);
        }
        Window* childByName(const char* child) const;
        bool d_selected;
        String d_hoverSection;
    };

#define CYS_CY_ITEM_BUTTON(cls)                                                                  \
    class FALAGARDBASE_API cls : public FalagardCyItemButton                                     \
    {                                                                                            \
    public:                                                                                      \
        static const utf8 WidgetTypeName[];                                                      \
        cls(const String& type, const String& name) : FalagardCyItemButton(type, name) {}        \
    }

    CYS_CY_ITEM_BUTTON(FalagardActionButtonItem);
    CYS_CY_ITEM_BUTTON(FalagardGuildButton);
    CYS_CY_ITEM_BUTTON(FalagardAchievementBarWindowButton);
    CYS_CY_ITEM_BUTTON(FalagardAchievementBarCompareWindowButton);
    CYS_CY_ITEM_BUTTON(FalagardAchievementBarCompareWindowIntegrationButton);
    CYS_CY_ITEM_BUTTON(FalagardAchievementWindowCatalogButton);
    CYS_CY_ITEM_BUTTON(FalagardAchievementWindowTextButton);
#undef CYS_CY_ITEM_BUTTON

    // ------------------------------------------------------------------ list variants
    class FALAGARDBASE_API FalagardMultiColumnListNew : public FalagardMultiColumnList
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardMultiColumnListNew(const String& type, const String& name);
    };

    class FALAGARDBASE_API FalagardMultiColumnListEx : public FalagardMultiColumnList
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardMultiColumnListEx(const String& type, const String& name);
    };

    // ------------------------------------------------------------------ scrolling notice board
    class FALAGARDBASE_API FalagardScrollText : public Window
    {
    public:
        static const utf8 WidgetTypeName[];
        static const String EventNamespace;
        static const String EventInfoElementClick;
        static const String EventInfoElementDelete;
        static const String EventInfoElementMoveIn;
        static const String EventInfoElementMoveOut;

        FalagardScrollText(const String& type, const String& name);

        void setSpeed(int speed) { d_speed = speed; }
        int getSpeed() const { return d_speed; }
        void setTextMode(int mode) { d_textMode = mode; requestRedraw(); }
        int getTextMode() const { return d_textMode; }
        void setTextBoardNumber(int n) { d_boardNumber = n; requestRedraw(); }
        int getChatBoardNumber() const { return d_boardNumber; }
        void setTextBoardHeight(float h) { d_boardHeight = h; requestRedraw(); }
        float getTextBoardHeight() const { return d_boardHeight; }
        void setBoardKillTimer(int t) { d_killTimer = t; }
        int getBoardKillTimer() const { return d_killTimer; }

    protected:
        virtual void populateRenderCache();
        virtual void updateSelf(float elapsed);
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardScrollText") return true;
            return Window::testClassName_impl(class_name);
        }
        Rect getTextRenderArea() const;
        int d_speed;
        int d_textMode;
        int d_boardNumber;
        float d_boardHeight;
        int d_killTimer;
        float d_offset;
    };

    // ------------------------------------------------------------------ corner resize grip
    class FALAGARDBASE_API FalagardDragCornerButton : public FalagardButton
    {
    public:
        static const utf8 WidgetTypeName[];
        static const String EventNamespace;

        FalagardDragCornerButton(const String& type, const String& name);

        void setDragTarget(const String& target) { d_target = target; }
        const String& getDragTarget() const { return d_target; }
        void setDragMode(int mode) { d_mode = mode; }
        int getDragMode() const { return d_mode; }
        void setLimited(bool limited) { d_limited = limited; }
        bool getLimited() const { return d_limited; }
        void setMinWidth(float v) { d_minWidth = v; }
        float getMinWidth() const { return d_minWidth; }
        void setMaxWidth(float v) { d_maxWidth = v; }
        float getMaxWidth() const { return d_maxWidth; }
        void setMiniHeight(float v) { d_minHeight = v; }
        float getMiniHeight() const { return d_minHeight; }
        void setMaxHeight(float v) { d_maxHeight = v; }
        float getMaxHeight() const { return d_maxHeight; }

    protected:
        virtual void onMouseButtonDown(MouseEventArgs& e);
        virtual void onMouseButtonUp(MouseEventArgs& e);
        virtual void onMouseMove(MouseEventArgs& e);
        virtual void onCaptureLost(WindowEventArgs& e);
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardDragCornerButton") return true;
            return FalagardButton::testClassName_impl(class_name);
        }
        Window* target() const;
        String d_target;
        int d_mode;
        bool d_limited;
        bool d_dragging;
        Point d_dragPoint;
        Size d_dragSize;
        float d_minWidth, d_maxWidth, d_minHeight, d_maxHeight;
    };

    // ------------------------------------------------------------------ soft keyboards
    class FALAGARDBASE_API FalagardPhoneSoftKeyBoard : public FalagardSoftKeyBoard
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardPhoneSoftKeyBoard(const String& type, const String& name);
    };

    class FALAGARDBASE_API FalagardSFZSoftKeyBoard : public FalagardSoftKeyBoard
    {
    public:
        static const utf8 WidgetTypeName[];
        FalagardSFZSoftKeyBoard(const String& type, const String& name);
    };

    // ------------------------------------------------------------------ mirrored static image
    class FALAGARDBASE_API FalagardStaticUVImage : public FalagardStaticImage
    {
    public:
        enum MirrorFormatting { MirrorNone, MirrorHorizontal, MirrorVertical, MirrorBoth };
        static const utf8 WidgetTypeName[];

        FalagardStaticUVImage(const String& type, const String& name);
        void setMirrorFormatting(MirrorFormatting f) { d_mirror = f; requestRedraw(); }
        MirrorFormatting getMirrorFormatting() const { return d_mirror; }

    protected:
        virtual void populateRenderCache();
        MirrorFormatting d_mirror;
    };

    // ------------------------------------------------------------------ radial progress
    class FALAGARDBASE_API FalagardCircleProgress : public Window
    {
    public:
        static const utf8 WidgetTypeName[];
        static const String EventNamespace;

        FalagardCircleProgress(const String& type, const String& name);

        void setProgressImage(const String& image);
        const String& getProgressImage() const { return d_imageSpec; }
        void setFillDirection(float f) { d_fillDirection = f; requestRedraw(); }
        float getFillDirection() const { return d_fillDirection; }
        void setClockWise(bool cw) { d_clockWise = cw; requestRedraw(); }
        bool getClockWise() const { return d_clockWise; }
        void setDownDir(bool down) { d_downDir = down; requestRedraw(); }
        bool getDownDir() const { return d_downDir; }
        void setLightEnable(bool on) { d_lightEnable = on; requestRedraw(); }
        bool getLightEnable() const { return d_lightEnable; }
        void setLightRadius(float r) { d_lightRadius = r; }
        float getLightRadius() const { return d_lightRadius; }
        void setMoveSpeed(float s) { d_moveSpeed = s; }
        float getMoveSpeed() const { return d_moveSpeed; }
        void setAutoMove(bool on) { d_autoMove = on; }
        bool getAutoMove() const { return d_autoMove; }
        void setProgress(float p);
        float getProgress() const { return d_progress; }

    protected:
        virtual void populateRenderCache();
        virtual void updateSelf(float elapsed);
        virtual bool testClassName_impl(const String& class_name) const
        {
            if (class_name == (const utf8*)"FalagardCircleProgress") return true;
            return Window::testClassName_impl(class_name);
        }
        const Image* d_image;
        String d_imageSpec;
        float d_fillDirection;
        float d_lightRadius;
        float d_moveSpeed;
        float d_progress;
        bool d_clockWise;
        bool d_downDir;
        bool d_lightEnable;
        bool d_autoMove;
    };

    // ------------------------------------------------------------------ factories
#define CYS_CY_FACTORY(cls)                                                                      \
    class FALAGARDBASE_API cls##Factory : public WindowFactory                                   \
    {                                                                                            \
    public:                                                                                      \
        cls##Factory() : WindowFactory(cls::WidgetTypeName) {}                                   \
        Window* createWindow(const String& name) { return new cls(d_type, name); }               \
        void destroyWindow(Window* window) { delete window; }                                    \
    }

    CYS_CY_FACTORY(FalagardMeshWindowParentWindow);
    CYS_CY_FACTORY(FalagardActionButtonWindow);
    CYS_CY_FACTORY(FalagardEventsWindow);
    CYS_CY_FACTORY(FalagardActionButtonItem);
    CYS_CY_FACTORY(FalagardGuildButton);
    CYS_CY_FACTORY(FalagardAchievementBarWindowButton);
    CYS_CY_FACTORY(FalagardAchievementBarCompareWindowButton);
    CYS_CY_FACTORY(FalagardAchievementBarCompareWindowIntegrationButton);
    CYS_CY_FACTORY(FalagardAchievementWindowCatalogButton);
    CYS_CY_FACTORY(FalagardAchievementWindowTextButton);
    CYS_CY_FACTORY(FalagardMultiColumnListNew);
    CYS_CY_FACTORY(FalagardMultiColumnListEx);
    CYS_CY_FACTORY(FalagardScrollText);
    CYS_CY_FACTORY(FalagardDragCornerButton);
    CYS_CY_FACTORY(FalagardPhoneSoftKeyBoard);
    CYS_CY_FACTORY(FalagardSFZSoftKeyBoard);
    CYS_CY_FACTORY(FalagardStaticUVImage);
    CYS_CY_FACTORY(FalagardCircleProgress);
#undef CYS_CY_FACTORY
}
