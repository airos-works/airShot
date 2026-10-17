#include "SettingsWindow.h"

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <GridView.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <SeparatorView.h>
#include <StringView.h>

#include <math.h>

#include "Messages.h"
#include "capture/ScreenCapture.h"

namespace airshot {

namespace {

constexpr uint32 kMsgKeyChanged = 'KeyC';
constexpr uint32 kMsgOptionChanged = 'OptC';
constexpr uint32 kMsgDelayChanged = 'DlyC';
constexpr uint32 kMsgAfterChanged = 'AftC';
constexpr uint32 kMsgResolutionChanged = 'ResC';
constexpr uint32 kMsgBrowseFolder = 'Brws';
constexpr uint32 kMsgDefaults = 'Dflt';

const int32 kDelays[] = {0, 1, 2, 3, 5, 10};
const int32 kDelayCount = 6;

}  // namespace


SettingsWindow::SettingsWindow(const Settings& settings)
	:
	BWindow(BRect(0, 0, 480, 520), "airShot settings", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE
			| B_CLOSE_ON_ESCAPE),
	fSettings(settings),
	fFolderPanel(NULL),
	fRecording(NULL),
	fLoading(false)
{
	fFullKey = new KeyCaptureControl("fullKey", settings.fullHotKey,
		new BMessage(kMsgKeyChanged));
	fWindowKey = new KeyCaptureControl("windowKey", settings.windowHotKey,
		new BMessage(kMsgKeyChanged));
	fRegionKey = new KeyCaptureControl("regionKey", settings.regionHotKey,
		new BMessage(kMsgKeyChanged));
	fLaunchOnHotKey = new BCheckBox("launch",
		"Start airShot when a shortcut is pressed while it is not running",
		new BMessage(kMsgOptionChanged));

	fIncludeCursor = new BCheckBox("cursor", "Include the mouse pointer",
		new BMessage(kMsgOptionChanged));
	fIncludeDecorations = new BCheckBox("decorations",
		"Include the window tab and border when capturing a window",
		new BMessage(kMsgOptionChanged));
	fAnimate = new BCheckBox("animate", "Animate the selection highlight",
		new BMessage(kMsgOptionChanged));
	fDelay = _MakeDelayField();
	fResolution = _MakeResolutionField();
	fScaleHint = new BStringView("scaleHint", "");
	fScaleHint->SetFont(be_plain_font);
	fScaleHint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DISABLED_LABEL_TINT);

	fAfter = _MakeAfterField();
	fCopyWhenSaving = new BCheckBox("copyWhenSaving", "Also copy to the clipboard when saving",
		new BMessage(kMsgOptionChanged));
	fSaveFolder = new BTextControl("folder", "Save to:", "", new BMessage(kMsgOptionChanged));
	fSaveFolder->SetModificationMessage(new BMessage(kMsgOptionChanged));
	fFilePrefix = new BTextControl("prefix", "File name prefix:", "",
		new BMessage(kMsgOptionChanged));
	fFilePrefix->SetModificationMessage(new BMessage(kMsgOptionChanged));
	BButton* browse = new BButton("browse", "Browse" B_UTF8_ELLIPSIS,
		new BMessage(kMsgBrowseFolder));

	fShowInDeskbar = new BCheckBox("deskbar", "Show an icon in the Deskbar tray",
		new BMessage(kMsgOptionChanged));

	BStringView* hint = new BStringView("hint",
		"Click a shortcut, then press the keys. Backspace clears it.");
	hint->SetFont(be_plain_font);
	hint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DISABLED_LABEL_TINT);

	// A BBox sizes itself from its first child view, so each section's
	// content is a container view with its own layout. The box reserves space
	// for its label. B_USE_* values are sentinels, not pixel measurements.
	BBox* shortcuts = new BBox("shortcuts");
	shortcuts->SetLabel("Keyboard shortcuts");
	BGridView* shortcutsGrid = new BGridView(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
	shortcuts->AddChild(shortcutsGrid);
	BLayoutBuilder::Grid<>(shortcutsGrid)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(new BStringView("l1", "Full screen:"), 0, 0)
		.Add(fFullKey, 1, 0)
		.Add(new BStringView("l2", "Window:"), 0, 1)
		.Add(fWindowKey, 1, 1)
		.Add(new BStringView("l3", "Region:"), 0, 2)
		.Add(fRegionKey, 1, 2)
		.Add(hint, 0, 3, 2)
		.Add(fLaunchOnHotKey, 0, 4, 2);

	BBox* capture = new BBox("capture");
	capture->SetLabel("Capturing");
	BGroupView* captureGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	capture->AddChild(captureGroup);
	BLayoutBuilder::Group<>(captureGroup)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fIncludeCursor)
		.Add(fIncludeDecorations)
		.Add(fAnimate)
		.AddGroup(B_HORIZONTAL)
			.Add(fDelay)
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fResolution)
			.AddGlue()
		.End()
		.Add(fScaleHint);

	BBox* after = new BBox("after");
	after->SetLabel("After capturing");
	BGroupView* afterGroup = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	after->AddChild(afterGroup);
	BLayoutBuilder::Group<>(afterGroup)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL)
			.Add(fAfter)
			.AddGlue()
		.End()
		.Add(fCopyWhenSaving)
		.AddGroup(B_HORIZONTAL)
			.Add(fSaveFolder)
			.Add(browse)
		.End()
		.Add(fFilePrefix);

	BButton* defaults = new BButton("defaults", "Restore defaults", new BMessage(kMsgDefaults));
	BButton* close = new BButton("close", "Close", new BMessage(B_QUIT_REQUESTED));
	close->MakeDefault(true);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(shortcuts)
		.Add(capture)
		.Add(after)
		.Add(fShowInDeskbar)
		.AddGroup(B_HORIZONTAL)
			.Add(defaults)
			.AddGlue()
			.Add(close)
		.End();

	_LoadControls();
	CenterOnScreen();
}


SettingsWindow::~SettingsWindow()
{
	delete fFolderPanel;
}


BMenuField* SettingsWindow::_MakeDelayField()
{
	BPopUpMenu* menu = new BPopUpMenu("delay");
	menu->SetRadioMode(true);
	for (int32 i = 0; i < kDelayCount; i++) {
		BString label;
		if (kDelays[i] == 0)
			label = "None";
		else
			label << kDelays[i] << (kDelays[i] == 1 ? " second" : " seconds");
		BMessage* message = new BMessage(kMsgDelayChanged);
		message->AddInt32("delay", kDelays[i]);
		menu->AddItem(new BMenuItem(label.String(), message));
	}
	return new BMenuField("delay", "Delay before capturing:", menu);
}


BMenuField* SettingsWindow::_MakeAfterField()
{
	BPopUpMenu* menu = new BPopUpMenu("after");
	menu->SetRadioMode(true);
	const char* labels[] = {"Open the editor", "Copy to the clipboard", "Save to the folder"};
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgAfterChanged);
		message->AddInt32("after", i);
		menu->AddItem(new BMenuItem(labels[i], message));
	}
	return new BMenuField("after", "After a capture:", menu);
}


BMenuField* SettingsWindow::_MakeResolutionField()
{
	BPopUpMenu* menu = new BPopUpMenu("resolution");
	menu->SetRadioMode(true);
	const char* labels[] = {"Capture at screen resolution", "Capture at 100% scale"};
	for (int32 i = 0; i < 2; i++) {
		BMessage* message = new BMessage(kMsgResolutionChanged);
		message->AddBool("screen", i == 0);
		menu->AddItem(new BMenuItem(labels[i], message));
	}
	return new BMenuField("resolution", "Scaled displays:", menu);
}


void SettingsWindow::_UpdateScaleHint()
{
	float scale = ScreenCapture::DeviceScale();
	BString text;
	if (scale > 1)
		text.SetToFormat("This screen is scaled to %d%%.", (int)roundf(scale * 100));
	else
		text = "This screen is not scaled; both capture the same pixels.";
	fScaleHint->SetText(text.String());
}


void SettingsWindow::_LoadControls()
{
	fLoading = true;
	fFullKey->SetKey(fSettings.fullHotKey);
	fWindowKey->SetKey(fSettings.windowHotKey);
	fRegionKey->SetKey(fSettings.regionHotKey);
	fLaunchOnHotKey->SetValue(fSettings.launchOnHotKey ? B_CONTROL_ON : B_CONTROL_OFF);
	fIncludeCursor->SetValue(fSettings.includeCursor ? B_CONTROL_ON : B_CONTROL_OFF);
	fIncludeDecorations->SetValue(fSettings.includeDecorations ? B_CONTROL_ON : B_CONTROL_OFF);
	fAnimate->SetValue(fSettings.animateSelection ? B_CONTROL_ON : B_CONTROL_OFF);
	for (int32 i = 0; i < kDelayCount; i++) {
		if (kDelays[i] == fSettings.delaySeconds || (i == 0 && fSettings.delaySeconds < 0))
			fDelay->Menu()->ItemAt(i)->SetMarked(true);
	}
	if (fDelay->Menu()->FindMarked() == NULL)
		fDelay->Menu()->ItemAt(0)->SetMarked(true);
	fResolution->Menu()->ItemAt(fSettings.captureScreenResolution ? 0 : 1)->SetMarked(true);
	_UpdateScaleHint();
	int32 after = fSettings.afterCapture;
	if (after < 0 || after > 2)
		after = 0;
	fAfter->Menu()->ItemAt(after)->SetMarked(true);
	fCopyWhenSaving->SetValue(fSettings.copyWhenSaving ? B_CONTROL_ON : B_CONTROL_OFF);
	fSaveFolder->SetText(fSettings.saveFolder.String());
	fFilePrefix->SetText(fSettings.filePrefix.String());
	fShowInDeskbar->SetValue(fSettings.showInDeskbar ? B_CONTROL_ON : B_CONTROL_OFF);
	fLoading = false;
}


void SettingsWindow::Update(const Settings& settings)
{
	fSettings = settings;
	_LoadControls();
}


void SettingsWindow::_Apply()
{
	if (fLoading)
		return;
	fSettings.fullHotKey = fFullKey->Key();
	fSettings.windowHotKey = fWindowKey->Key();
	fSettings.regionHotKey = fRegionKey->Key();
	fSettings.launchOnHotKey = fLaunchOnHotKey->Value() == B_CONTROL_ON;
	fSettings.includeCursor = fIncludeCursor->Value() == B_CONTROL_ON;
	fSettings.includeDecorations = fIncludeDecorations->Value() == B_CONTROL_ON;
	fSettings.animateSelection = fAnimate->Value() == B_CONTROL_ON;
	BMenuItem* marked = fDelay->Menu()->FindMarked();
	if (marked != NULL)
		fSettings.delaySeconds = marked->Message()->GetInt32("delay", 0);
	marked = fResolution->Menu()->FindMarked();
	if (marked != NULL)
		fSettings.captureScreenResolution = marked->Message()->GetBool("screen", true);
	marked = fAfter->Menu()->FindMarked();
	if (marked != NULL)
		fSettings.afterCapture = marked->Message()->GetInt32("after", kAfterOpenEditor);
	fSettings.copyWhenSaving = fCopyWhenSaving->Value() == B_CONTROL_ON;
	fSettings.saveFolder = fSaveFolder->Text();
	fSettings.filePrefix = fFilePrefix->Text();
	fSettings.showInDeskbar = fShowInDeskbar->Value() == B_CONTROL_ON;
	fSettings.hotKeysPaused = fRecording != NULL;

	BMessage update(kMsgSettingsChanged);
	fSettings.ToMessage(update);
	update.what = kMsgSettingsChanged;
	be_app->PostMessage(&update);
}


void SettingsWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	// While a shortcut is recorded, every key goes to the recording control,
	// ahead of the window's own shortcuts and Print Screen handling.
	if (message->what == B_KEY_DOWN && fRecording != NULL) {
		if (fRecording->HandleKeyMessage(message))
			return;
	}
	if ((message->what == B_KEY_UP || message->what == B_UNMAPPED_KEY_DOWN
			|| message->what == B_UNMAPPED_KEY_UP) && fRecording != NULL) {
		if (message->what == B_UNMAPPED_KEY_DOWN)
			fRecording->HandleKeyMessage(message);
		return;
	}
	BWindow::DispatchMessage(message, handler);
}


void SettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgHotKeyRecording:
		{
			KeyCaptureControl* control = NULL;
			message->FindPointer("control", (void**)&control);
			bool recording = message->GetBool("recording", false);
			if (recording) {
				if (fRecording != NULL && fRecording != control)
					fRecording->StopRecording();
				fRecording = control;
			} else if (fRecording == control)
				fRecording = NULL;
			// Pause the filter while recording so the current shortcuts do not
			// fire when they are pressed to be re-assigned.
			_Apply();
			break;
		}

		case kMsgKeyChanged:
		{
			// The same combination cannot start two different captures.
			KeyCaptureControl* changed = NULL;
			message->FindPointer("source", (void**)&changed);
			KeyCaptureControl* controls[] = {fFullKey, fWindowKey, fRegionKey};
			for (KeyCaptureControl* control : controls) {
				if (control != changed && changed != NULL && control->Key().IsSet()
					&& control->Key() == changed->Key())
					control->SetKey(HotKey());
			}
			_Apply();
			break;
		}

		case kMsgOptionChanged:
		case kMsgDelayChanged:
		case kMsgResolutionChanged:
		case kMsgAfterChanged:
			_Apply();
			break;

		case kMsgBrowseFolder:
		{
			if (fFolderPanel == NULL) {
				BMessenger target(this);
				fFolderPanel = new BFilePanel(B_OPEN_PANEL, &target, NULL, B_DIRECTORY_NODE,
					false, new BMessage(B_REFS_RECEIVED));
				fFolderPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Choose");
			}
			if (fSaveFolder->Text()[0] != '\0')
				fFolderPanel->SetPanelDirectory(fSaveFolder->Text());
			fFolderPanel->Show();
			break;
		}

		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				BPath path(&ref);
				fSaveFolder->SetText(path.Path());
				_Apply();
			}
			break;
		}

		case kMsgDefaults:
		{
			Settings defaults;
			defaults.mainWindowFrame = fSettings.mainWindowFrame;
			defaults.editorWindowFrame = fSettings.editorWindowFrame;
			fSettings = defaults;
			_LoadControls();
			_Apply();
			break;
		}

		default:
			BWindow::MessageReceived(message);
	}
}


void SettingsWindow::ScreenChanged(BRect frame, color_space mode)
{
	BWindow::ScreenChanged(frame, mode);
	_UpdateScaleHint();
}


bool SettingsWindow::QuitRequested()
{
	if (fRecording != NULL) {
		fRecording->StopRecording();
		fRecording = NULL;
		_Apply();
	}
	return true;
}

}  // namespace airshot
