#include "App.h"

#include <AboutWindow.h>
#include <Alert.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>
#include <TranslationUtils.h>
#include <View.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capture/OverlayWindow.h"
#include "editor/EditorWindow.h"
#include "editor/Export.h"
#include "ui/DeskbarView.h"
#include "ui/MainWindow.h"
#include "ui/SettingsWindow.h"

namespace airshot {

namespace {

constexpr uint32 kMsgCaptureNow = 'CpNw';
constexpr uint32 kMsgSyncDeskbar = 'SyDb';
// Our own windows need a moment to disappear from the screen before it is
// read back.
constexpr bigtime_t kHideSettleTime = 250000;


void TraceCapture(const char* stage)
{
	if (getenv("AIRSHOT_TRACE") != NULL) {
		fprintf(stderr, "airShot capture %lld ms: %s\n",
			(long long)(system_time() / 1000), stage);
	}
}


}  // namespace


App::App()
	:
	BApplication(kAppSignature),
	fMainWindow(NULL),
	fSettingsWindow(NULL),
	fScreen(NULL),
	fScreenScale(1),
	fPendingKind(kCaptureFull),
	fCapturing(false),
	fCaptureRequested(false),
	fDelayRunner(NULL),
	fDeskbarRetry(NULL),
	fDeskbarTries(0)
{
	fSettings.Load();
	if (fSettings.hotKeysPaused) {
		// A crash while recording a shortcut must not leave them disabled.
		fSettings.hotKeysPaused = false;
		fSettings.Save();
	}
}


App::~App()
{
	delete fDelayRunner;
	delete fDeskbarRetry;
	delete fScreen;
}


void App::ReadyToRun()
{
	_SyncDeskbar();
	if (!fCaptureRequested)
		_ShowMainWindow();
}


void App::ArgvReceived(int32 argc, char** argv)
{
	for (int32 i = 1; i < argc; i++) {
		const char* arg = argv[i];
		if (strcmp(arg, "--full") == 0 || strcmp(arg, "-f") == 0) {
			fCaptureRequested = true;
			_StartCapture(kCaptureFull);
		} else if (strcmp(arg, "--window") == 0 || strcmp(arg, "-w") == 0) {
			fCaptureRequested = true;
			_StartCapture(kCaptureWindow);
		} else if (strcmp(arg, "--region") == 0 || strcmp(arg, "-r") == 0) {
			fCaptureRequested = true;
			_StartCapture(kCaptureRegion);
		} else if (strcmp(arg, "--settings") == 0) {
			fCaptureRequested = true;
			PostMessage(kMsgShowSettings);
		} else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
			printf("airShot [--full | --window | --region | --settings] [image files]\n"
				"  --full     capture the whole screen\n"
				"  --window   pick a window to capture\n"
				"  --region   select a region to capture\n"
				"  --settings open the settings\n"
				"Image files are opened in the editor.\n");
			fCaptureRequested = true;
			PostMessage(B_QUIT_REQUESTED);
		} else if (arg[0] != '-') {
			// A file to open.
			BEntry entry(arg, true);
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				BMessage refs(B_REFS_RECEIVED);
				refs.AddRef("refs", &ref);
				fCaptureRequested = true;
				PostMessage(&refs);
			}
		}
	}
}


void App::RefsReceived(BMessage* message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BBitmap* loaded = BTranslationUtils::GetBitmap(&ref);
		if (loaded == NULL) {
			BPath path(&ref);
			BString text("airShot cannot open\n");
			text << path.Path() << "\nas an image.";
			(new BAlert("Open failed", text.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL,
				B_STOP_ALERT))->Go(NULL);
			continue;
		}
		// The editor and the blur filter expect 32 bit pixels.
		if (loaded->ColorSpace() != B_RGB32 && loaded->ColorSpace() != B_RGBA32) {
			BBitmap* converted = new BBitmap(loaded->Bounds(), B_RGBA32, true);
			BView* view = new BView(loaded->Bounds(), "convert", B_FOLLOW_NONE, 0);
			converted->AddChild(view);
			if (converted->Lock()) {
				view->DrawBitmap(loaded, B_ORIGIN);
				view->Sync();
				converted->Unlock();
			}
			converted->RemoveChild(view);
			delete view;
			delete loaded;
			loaded = converted;
		}
		_OpenEditor(loaded, 1);
	}
}


void App::MessageReceived(BMessage* message)
{
	if (getenv("AIRSHOT_TRACE") != NULL) {
		fprintf(stderr, "airShot: message '%.4s' capturing=%d\n", (const char*)&message->what,
			(int)fCapturing);
	}
	switch (message->what) {
		case kMsgCaptureFull:
			_StartCapture(kCaptureFull);
			break;
		case kMsgCaptureWindow:
			_StartCapture(kCaptureWindow);
			break;
		case kMsgCaptureRegion:
			_StartCapture(kCaptureRegion);
			break;
		case kMsgCaptureNow:
			_CaptureNow();
			break;
		case kMsgSyncDeskbar:
			_SyncDeskbar();
			break;
		case kMsgOverlayDone:
			_OverlayDone(message);
			break;
		case kMsgOverlayCancelled:
			delete fScreen;
			fScreen = NULL;
			fCapturing = false;
			break;

		case kMsgShowMainWindow:
			_ShowMainWindow();
			break;
		case kMsgShowSettings:
			_ShowSettings();
			break;
		case kMsgQuitApp:
			PostMessage(B_QUIT_REQUESTED);
			break;

		case kMsgSettingsChanged:
		{
			Settings settings = fSettings;
			settings.FromMessage(*message);
			_ApplySettings(settings);
			break;
		}
		case kMsgMainWindowFrame:
			fSettings.mainWindowFrame = message->GetRect("frame", fSettings.mainWindowFrame);
			_SaveSettings();
			break;
		case kMsgEditorState:
			fSettings.colorIndex = message->GetInt32("color_index", fSettings.colorIndex);
			fSettings.strokeWidth = message->GetFloat("stroke_width", fSettings.strokeWidth);
			fSettings.fontSize = message->GetFloat("font_size", fSettings.fontSize);
			fSettings.editorWindowFrame = message->GetRect("frame", fSettings.editorWindowFrame);
			_SaveSettings();
			break;

		default:
			BApplication::MessageReceived(message);
	}
}


//	#pragma mark - capturing


void App::_HideOwnWindows()
{
	if (fMainWindow != NULL && fMainWindow->Lock()) {
		if (!fMainWindow->IsHidden()) {
			fSettings.mainWindowFrame = fMainWindow->Frame();
			fMainWindow->Hide();
		}
		fMainWindow->Unlock();
	}
	if (fSettingsWindow != NULL && fSettingsWindow->Lock()) {
		if (!fSettingsWindow->IsHidden())
			fSettingsWindow->Hide();
		fSettingsWindow->Unlock();
	}
}


void App::_StartCapture(CaptureKind kind)
{
	if (fCapturing)
		return;
	TraceCapture("requested");
	fCapturing = true;
	fPendingKind = kind;
	_HideOwnWindows();
	TraceCapture("own windows hidden");
	bigtime_t delay = kHideSettleTime + (bigtime_t)fSettings.delaySeconds * 1000000;
	delete fDelayRunner;
	BMessage now(kMsgCaptureNow);
	fDelayRunner = new BMessageRunner(BMessenger(this), &now, delay, 1);
}


void App::_CaptureNow()
{
	TraceCapture("grabbing screen");
	delete fDelayRunner;
	fDelayRunner = NULL;
	delete fScreen;
	fScreen = ScreenCapture::GrabScreen(fSettings.includeCursor,
		fSettings.captureScreenResolution, &fScreenScale);
	TraceCapture(fScreenScale != 1 ? "screen grabbed in screen pixels" : "screen grabbed");
	if (fScreen == NULL) {
		fCapturing = false;
		(new BAlert("Capture failed", "airShot could not read the screen.", "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(NULL);
		return;
	}
	if (fPendingKind == kCaptureFull) {
		BBitmap* screen = fScreen;
		fScreen = NULL;
		_Finish(screen, fScreenScale);
		return;
	}
	std::vector<WindowEntry> windows;
	ScreenCapture::ListWindows(windows, Team());
	TraceCapture("windows listed");
	OverlayWindow* overlay = new OverlayWindow(fScreen, fScreenScale, windows, fPendingKind,
		fSettings.animateSelection, fSettings.includeDecorations, BMessenger(this));
	TraceCapture("overlay constructed");
	overlay->Show();
	TraceCapture("overlay shown");
}


void App::_OverlayDone(BMessage* message)
{
	BRect frame = message->GetRect("frame", BRect());
	if (fScreen == NULL || !frame.IsValid()) {
		delete fScreen;
		fScreen = NULL;
		fCapturing = false;
		return;
	}
	BBitmap* result = NULL;
	int32 kind = message->GetInt32("kind", kCaptureRegion);
	float tabHeight = message->GetFloat("window_tab_height", 0);
	// Transparent space beside the tab, like Haiku's Screenshot, is disabled:
	// exports are opaque (see Export::Flatten) and would show it white.
	const bool kTransparentTabSpace = false;
	if (kTransparentTabSpace && kind == kCaptureWindow && fSettings.includeDecorations
		&& tabHeight > 0 && !message->GetBool("window_desktop", false)) {
		// Like Haiku's Screenshot: the space beside the tab becomes transparent.
		WindowEntry entry;
		entry.team = message->GetInt32("window_team", -1);
		entry.token = message->GetInt32("window_token", -1);
		entry.frame = message->GetRect("window_frame", BRect());
		entry.tabHeight = tabHeight;
		entry.borderSize = message->GetFloat("window_border", 0);
		entry.desktop = false;
		BRect tabFrame;
		if (ScreenCapture::TabFrameFor(entry, tabFrame) == B_OK) {
			BRect pixels = ScreenCapture::ToPixels(frame, fScreenScale);
			result = ScreenCapture::Crop(fScreen, pixels, true);
			if (result != NULL) {
				ScreenCapture::ClearTabSpace(result, pixels.OffsetToCopy(B_ORIGIN),
					ScreenCapture::ToPixels(tabFrame, fScreenScale)
						.OffsetByCopy(-pixels.left, -pixels.top),
					floorf(tabHeight * fScreenScale));
			}
		}
	}
	if (result == NULL)
		result = ScreenCapture::Crop(fScreen, ScreenCapture::ToPixels(frame, fScreenScale), false);
	delete fScreen;
	fScreen = NULL;
	if (result == NULL) {
		fCapturing = false;
		return;
	}
	_Finish(result, fScreenScale);
}


void App::_Finish(BBitmap* bitmap, float pixelScale)
{
	fCapturing = false;
	switch (fSettings.afterCapture) {
		case kAfterCopy:
			if (Export::CopyToClipboard(bitmap) == B_OK)
				Export::Notify("Copied to clipboard", "The screenshot is ready to paste.");
			delete bitmap;
			break;

		case kAfterSave:
		{
			BString folder(fSettings.saveFolder);
			if (folder.IsEmpty() || create_directory(folder.String(), 0755) != B_OK) {
				BPath desktop;
				find_directory(B_DESKTOP_DIRECTORY, &desktop);
				folder = desktop.Path();
			}
			BString path = Export::UniquePath(folder.String(),
				Export::DefaultFileName(fSettings.filePrefix.String()).String());
			status_t status = Export::SavePNG(bitmap, path.String());
			if (status == B_OK) {
				if (fSettings.copyWhenSaving)
					Export::CopyToClipboard(bitmap);
				BString content(fSettings.copyWhenSaving
					? "Saved and copied to the clipboard\n" : "Saved\n");
				content << path;
				Export::Notify("Screenshot saved", content.String());
			} else {
				BString text("The screenshot could not be saved to\n");
				text << path << "\n\n" << strerror(status);
				(new BAlert("Save failed", text.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL,
					B_STOP_ALERT))->Go(NULL);
			}
			delete bitmap;
			break;
		}

		default:
			_OpenEditor(bitmap, pixelScale);
			break;
	}
}


void App::_OpenEditor(BBitmap* bitmap, float pixelScale)
{
	EditorWindow* editor = new EditorWindow(bitmap, fSettings, pixelScale);
	editor->Show();
}


//	#pragma mark - windows and settings


void App::_ShowMainWindow()
{
	if (fMainWindow == NULL)
		fMainWindow = new MainWindow(fSettings);
	if (fMainWindow->Lock()) {
		fMainWindow->Update(fSettings);
		if (fMainWindow->IsHidden())
			fMainWindow->Show();
		fMainWindow->Activate();
		fMainWindow->Unlock();
	}
}


void App::_ShowSettings()
{
	if (fSettingsWindow == NULL)
		fSettingsWindow = new SettingsWindow(fSettings);
	if (fSettingsWindow->Lock()) {
		fSettingsWindow->Update(fSettings);
		if (fSettingsWindow->IsHidden())
			fSettingsWindow->Show();
		fSettingsWindow->Activate();
		fSettingsWindow->Unlock();
	}
}


void App::_ApplySettings(const Settings& settings)
{
	bool deskbarChanged = settings.showInDeskbar != fSettings.showInDeskbar;
	fSettings = settings;
	_SaveSettings();
	if (deskbarChanged)
		_SyncDeskbar();
	if (fMainWindow != NULL && fMainWindow->Lock()) {
		fMainWindow->Update(fSettings);
		fMainWindow->Unlock();
	}
}


void App::_SyncDeskbar()
{
	BDeskbar deskbar;
	bool present = deskbar.HasItem(DeskbarView::kName);
	if (fSettings.showInDeskbar && !present) {
		app_info info;
		status_t status = GetAppInfo(&info);
		if (status == B_OK)
			status = deskbar.AddItem(&info.ref);
		if (status != B_OK) {
			fprintf(stderr, "airShot: adding the Deskbar icon failed: %s\n", strerror(status));
			// Deskbar may still be starting (at login, or after a restart).
			delete fDeskbarRetry;
			fDeskbarRetry = NULL;
			if (++fDeskbarTries <= 12) {
				BMessage retry(kMsgSyncDeskbar);
				fDeskbarRetry = new BMessageRunner(BMessenger(this), &retry, 5000000, 1);
			}
		}
	} else if (!fSettings.showInDeskbar && present)
		deskbar.RemoveItem(DeskbarView::kName);
}


void App::_CloseOwnWindows()
{
	// The launcher and settings windows hide instead of quitting when the
	// user closes them; when the application quits they must really go.
	BWindow* windows[] = {fMainWindow, fSettingsWindow};
	for (BWindow* window : windows) {
		if (window != NULL && window->Lock()) {
			if (window == fMainWindow && !window->IsHidden())
				fSettings.mainWindowFrame = window->Frame();
			window->Quit();
		}
	}
	fMainWindow = NULL;
	fSettingsWindow = NULL;
}


void App::_SaveSettings()
{
	status_t status = fSettings.Save();
	if (status != B_OK)
		fprintf(stderr, "airShot: saving settings failed: %s\n", strerror(status));
}


void App::AboutRequested()
{
	BAboutWindow* about = new BAboutWindow(kAppName, kAppSignature);
	about->AddDescription("A screenshot and markup tool for air/OS and Haiku.\n\n"
		"Capture the whole screen, a window or a region with a keyboard shortcut, "
		"then annotate with arrows, boxes, text, numbered markers, highlights and blur.");
	about->AddCopyright(2026, "air/OS contributors");
	about->AddExtraInfo("Inspired by Shottr and Flameshot.\n\n"
		"Toolbar icons adapted from Font Awesome Free 6.7.2.\n"
		"Copyright 2024 Fonticons, Inc. Licensed under CC BY 4.0.\n"
		"https://fontawesome.com");
	about->Show();
}


bool App::QuitRequested()
{
	_CloseOwnWindows();
	if (!BApplication::QuitRequested())
		return false;
	if (fSettings.hotKeysPaused) {
		fSettings.hotKeysPaused = false;
		_SaveSettings();
	}
	return true;
}

}  // namespace airshot
