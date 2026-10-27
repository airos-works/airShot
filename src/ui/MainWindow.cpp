#include "MainWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Screen.h>
#include <SeparatorView.h>

#include <math.h>

#include "Messages.h"
#include "IconButton.h"
#include "editor/ToolIcons.h"

namespace airshot {

namespace {

BString KeyLabel(const HotKey& hotKey)
{
	return hotKey.IsSet() ? BString(hotKey.label.c_str()) : BString("no shortcut");
}

}  // namespace


MainWindow::MainWindow(const Settings& settings)
	:
	BWindow(BRect(0, 0, 360, 220), "airShot", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE
			| B_NOT_RESIZABLE)
{
	BMenuBar* menuBar = new BMenuBar("menu");
	BMenu* app = new BMenu("airShot");
	app->AddItem(new BMenuItem("About airShot" B_UTF8_ELLIPSIS, new BMessage(B_ABOUT_REQUESTED)));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Settings" B_UTF8_ELLIPSIS, new BMessage(kMsgShowSettings), ','));
	app->AddSeparatorItem();
	app->AddItem(new BMenuItem("Quit", new BMessage(kMsgQuitApp), 'Q'));
	menuBar->AddItem(app);
	BMenu* capture = new BMenu("Capture");
	capture->AddItem(new BMenuItem("Full screen", new BMessage(kMsgCaptureFull), '1'));
	capture->AddItem(new BMenuItem("Window", new BMessage(kMsgCaptureWindow), '2'));
	capture->AddItem(new BMenuItem("Region", new BMessage(kMsgCaptureRegion), '3'));
	menuBar->AddItem(capture);

	BButton* full = _MakeButton("full", "Full screen", kIconFullScreen, kMsgCaptureFull);
	BButton* window = _MakeButton("window", "Window", kIconWindow, kMsgCaptureWindow);
	BButton* region = _MakeButton("region", "Region", kIconRegion, kMsgCaptureRegion);
	BButton* settingsButton = _MakeButton("settings", "Settings" B_UTF8_ELLIPSIS, kIconSettings,
		kMsgShowSettings);

	fFullKey = new BStringView("fullKey", "");
	fWindowKey = new BStringView("windowKey", "");
	fRegionKey = new BStringView("regionKey", "");
	BStringView* keys[] = {fFullKey, fWindowKey, fRegionKey};
	for (BStringView* view : keys) {
		view->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DISABLED_LABEL_TINT);
		view->SetAlignment(B_ALIGN_RIGHT);
		view->SetExplicitMinSize(BSize(be_plain_font->StringWidth("Shift+Ctrl+Print Screen"),
			B_SIZE_UNSET));
	}

	BStringView* title = new BStringView("title", "Take a screenshot");
	BFont bold(be_bold_font);
	// A whole point size: at fractional sizes StringWidth() comes out narrower
	// than the drawn text, and the view, sized to fit, clips the last letter.
	bold.SetSize(roundf(bold.Size() * 1.15f));
	title->SetFont(&bold);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(title)
			.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
				.Add(full, 0, 0)
				.Add(fFullKey, 1, 0)
				.Add(window, 0, 1)
				.Add(fWindowKey, 1, 1)
				.Add(region, 0, 2)
				.Add(fRegionKey, 1, 2)
			.End()
			.AddStrut(B_USE_SMALL_SPACING)
			.Add(new BSeparatorView(B_HORIZONTAL))
			.AddGroup(B_HORIZONTAL)
				.Add(settingsButton)
				.AddGlue()
			.End()
		.End();

	Update(settings);
	if (settings.mainWindowFrame.IsValid()
		&& BScreen().Frame().Contains(settings.mainWindowFrame.LeftTop()))
		MoveTo(settings.mainWindowFrame.LeftTop());
	else
		CenterOnScreen();
}


BButton* MainWindow::_MakeButton(const char* name, const char* label, int32 icon, uint32 command)
{
	BButton* button = new IconButton(name, label, new BMessage(command));
	BBitmap* bitmap = MakeActionIcon((ActionIcon)icon, floorf(be_plain_font->Size() * 1.6f));
	button->SetIcon(bitmap);
	delete bitmap;
	button->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
	button->SetExplicitMinSize(BSize(be_plain_font->StringWidth("Full screen") * 2.2f,
		B_SIZE_UNSET));
	return button;
}


void MainWindow::Update(const Settings& settings)
{
	fFullKey->SetText(KeyLabel(settings.fullHotKey).String());
	fWindowKey->SetText(KeyLabel(settings.windowHotKey).String());
	fRegionKey->SetText(KeyLabel(settings.regionHotKey).String());
}


void MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCaptureFull:
		case kMsgCaptureWindow:
		case kMsgCaptureRegion:
		case kMsgShowSettings:
		case kMsgQuitApp:
		case B_ABOUT_REQUESTED:
			be_app->PostMessage(message);
			break;
		default:
			BWindow::MessageReceived(message);
	}
}


bool MainWindow::QuitRequested()
{
	// Remember where the window was, then hide instead of quitting: the
	// shortcuts keep working in the background.
	BMessage frame(kMsgMainWindowFrame);
	frame.AddRect("frame", Frame());
	be_app->PostMessage(&frame);
	if (!IsHidden())
		Hide();
	return false;
}

}  // namespace airshot
