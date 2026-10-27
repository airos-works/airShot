// Native layout regressions: run on Haiku with an app_server (make check-ui).
#include <Application.h>
#include <Bitmap.h>
#include <Box.h>
#include <Layout.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Screen.h>
#include <TextControl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "ui/MainWindow.h"
#include "ui/SettingsWindow.h"
#include "editor/EditorWindow.h"
using namespace airshot;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void CheckView(BView* view)
{
    if (view->IsHidden()) return;
    CHECK(view->Frame().IsValid());
    for (int32 i = 0; BView* child = view->ChildAt(i); ++i) {
        if (child->IsHidden()) continue;
        // Scroll targets may exceed their viewport; all controls must fit.
        if (dynamic_cast<BControl*>(child) != nullptr)
            CHECK(view->Bounds().InsetByCopy(-1, -1).Contains(child->Frame()));
        CheckView(child);
    }
}

static void CheckWindow(BWindow* window)
{
    CHECK(window->Bounds().IsValid());
    for (int32 i = 0; BView* view = window->ChildAt(i); ++i) CheckView(view);
}

int main()
{
    BApplication app("application/x-vnd.airOS-airShot-UITests");
    const BFont originalPlain(*be_plain_font), originalBold(*be_bold_font);
    for (float fontSize : {12.0f, 18.0f}) {
        const_cast<BFont*>(be_plain_font)->SetSize(fontSize);
        const_cast<BFont*>(be_bold_font)->SetSize(fontSize);
        Settings settings;
        auto* window = new SettingsWindow(settings);
        window->Show();
        snooze(100000);
        window->Lock();
        CheckWindow(window);
        BRect previous;
        for (const char* name : {"shortcuts", "capture", "after"}) {
            BView* section = window->FindView(name);
            CHECK(section != nullptr);
            if (section == nullptr) continue;
            CHECK(section->Frame().Height() > fontSize * 4);
            if (previous.IsValid()) CHECK(section->Frame().top > previous.bottom);
            previous = section->Frame();
        }
        auto* key = dynamic_cast<KeyCaptureControl*>(window->FindView("fullKey"));
        CHECK(key != nullptr);
        if (key) {
            CHECK(key->Bounds().Height() < fontSize * 4);
            key->StartRecording();
            BMessage escape(B_KEY_DOWN);
            escape.AddInt32("key", kKeyEscape);
            key->HandleKeyMessage(&escape);
            CHECK(!key->IsRecording());
            CHECK(key->Key() == settings.fullHotKey);
        }
        // Captures on scaled displays default to the screen's resolution.
        auto* resolution = dynamic_cast<BMenuField*>(window->FindView("resolution"));
        CHECK(resolution != nullptr);
        if (resolution) {
            BMenuItem* marked = resolution->Menu()->FindMarked();
            CHECK(marked != nullptr && resolution->Menu()->IndexOf(marked) == 0);
        }
        // Print Screen starts a full screen capture; giving it to region
        // capture swaps the two shortcuts.
        auto* printScreen = dynamic_cast<BMenuField*>(window->FindView("printScreen"));
        auto* fullKey = dynamic_cast<KeyCaptureControl*>(window->FindView("fullKey"));
        auto* regionKey = dynamic_cast<KeyCaptureControl*>(window->FindView("regionKey"));
        CHECK(printScreen != nullptr && fullKey != nullptr && regionKey != nullptr);
        if (printScreen && fullKey && regionKey) {
            BMenuItem* marked = printScreen->Menu()->FindMarked();
            CHECK(marked != nullptr && printScreen->Menu()->IndexOf(marked) == 0);
            BMessage choose(*printScreen->Menu()->ItemAt(2)->Message());
            window->MessageReceived(&choose);
            CHECK(regionKey->Key() == settings.fullHotKey);
            CHECK(fullKey->Key() == settings.regionHotKey);
            marked = printScreen->Menu()->FindMarked();
            CHECK(marked != nullptr && printScreen->Menu()->IndexOf(marked) == 2);
            // Recording Print Screen for a window capture takes it from region.
            auto* windowKey = dynamic_cast<KeyCaptureControl*>(window->FindView("windowKey"));
            windowKey->StartRecording();
            BMessage press(B_KEY_DOWN);
            press.AddInt32("key", kKeyPrintScreen);
            press.AddInt32("modifiers", 0);
            windowKey->HandleKeyMessage(&press);
            BMessage changed(*windowKey->Message());
            changed.AddPointer("source", windowKey);
            window->MessageReceived(&changed);
            CHECK(!regionKey->Key().IsSet());
            marked = printScreen->Menu()->FindMarked();
            CHECK(marked != nullptr && printScreen->Menu()->IndexOf(marked) == 1);
        }
        window->Quit();

        auto* main = new MainWindow(settings);
        main->Show(); snooze(100000); main->Lock();
        CheckWindow(main); main->Quit();

        auto* editor = new EditorWindow(new BBitmap(BRect(0, 0, 319, 239), B_RGB32), settings);
        editor->Show(); snooze(100000); editor->Lock();
        // Two rows must fit a 1024 px screen at the normal font size.
        float minW, maxW, minH, maxH;
        editor->GetSizeLimits(&minW, &maxW, &minH, &maxH);
        std::printf("font %.0f: editor minimum %.0f x %.0f\n", fontSize, minW, minH);
        if (fontSize == 12) CHECK(minW < 1000);
        editor->ResizeTo(minW, 480);
        editor->UpdateIfNeeded();
        CheckWindow(editor);
        editor->Quit();

        // A capture in the pixels of a 200% screen opens at actual size,
        // half an image pixel per coordinate unit.
        auto* scaled = new EditorWindow(new BBitmap(BRect(0, 0, 319, 239), B_RGB32), settings, 2);
        scaled->Show(); snooze(100000); scaled->Lock();
        auto* canvas = dynamic_cast<CanvasView*>(scaled->FindView("canvas"));
        CHECK(canvas != nullptr);
        if (canvas) CHECK(std::fabs(canvas->Zoom() - 0.5f) < 0.01f);
        scaled->Quit();
    }
    *const_cast<BFont*>(be_plain_font) = originalPlain;
    *const_cast<BFont*>(be_bold_font) = originalBold;
    std::printf("UI tests: %d failures\n", failures);
    return failures ? 1 : 0;
}
