#include "Settings.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>

namespace airshot {

namespace {

void AddHotKey(BMessage& message, const char* prefix, const HotKey& hotKey)
{
	BString name;
	message.AddInt32(name.SetToFormat("%s_key", prefix), hotKey.key);
	message.AddInt32(name.SetToFormat("%s_modifiers", prefix), (int32)hotKey.modifiers);
	message.AddString(name.SetToFormat("%s_label", prefix), hotKey.label.c_str());
}


void FindHotKey(const BMessage& message, const char* prefix, HotKey& hotKey)
{
	BString name;
	int32 key;
	if (message.FindInt32(name.SetToFormat("%s_key", prefix), &key) != B_OK)
		return;
	hotKey.key = key;
	hotKey.modifiers = (uint32)message.GetInt32(name.SetToFormat("%s_modifiers", prefix), 0)
		& kModifierMask;
	hotKey.label = message.GetString(name.SetToFormat("%s_label", prefix), "");
}

}  // namespace


Settings::Settings()
{
	SetDefaults();
}


void Settings::SetDefaults()
{
	fullHotKey = HotKey(kKeyPrintScreen, 0, "Print Screen");
	windowHotKey = HotKey(kKeyPrintScreen, kShiftKey, "Print Screen");
	regionHotKey = HotKey(kKeyPrintScreen, kControlKey, "Print Screen");
	launchOnHotKey = true;
	hotKeysPaused = false;

	includeCursor = false;
	delaySeconds = 0;
	includeDecorations = true;
	animateSelection = true;
	captureScreenResolution = true;

	afterCapture = kAfterOpenEditor;
	copyWhenSaving = true;
	BPath desktop;
	if (find_directory(B_DESKTOP_DIRECTORY, &desktop) == B_OK)
		saveFolder = desktop.Path();
	else
		saveFolder = "/boot/home/Desktop";
	filePrefix = "airShot";

	showInDeskbar = true;
	mainWindowFrame = BRect(0, 0, -1, -1);
	editorWindowFrame = BRect(0, 0, -1, -1);

	colorIndex = 0;
	strokeWidth = 4;
	fontSize = 24;
}


status_t Settings::GetPath(BPath& path, bool create)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	path.Append(kSettingsDirectory);
	if (create) {
		status = create_directory(path.Path(), 0755);
		if (status != B_OK)
			return status;
	}
	return path.Append(kSettingsFile);
}


status_t Settings::Load()
{
	BPath path;
	status_t status = GetPath(path, false);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_READ_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	BMessage message;
	status = message.Unflatten(&file);
	if (status != B_OK)
		return status;
	FromMessage(message);
	return B_OK;
}


status_t Settings::Save() const
{
	BPath path;
	status_t status = GetPath(path, true);
	if (status != B_OK)
		return status;
	BMessage message;
	ToMessage(message);
	// Write next to the file and rename, so a reader never sees a torn file.
	BString temporary(path.Path());
	temporary << ".tmp";
	BFile file(temporary.String(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status == B_OK)
		status = message.Flatten(&file);
	if (status == B_OK)
		status = file.Sync();
	if (status == B_OK) {
		BEntry entry(temporary.String());
		status = entry.Rename(path.Leaf(), true);
	}
	return status;
}


void Settings::ToMessage(BMessage& message) const
{
	message.what = 'AsSt';
	AddHotKey(message, "hotkey_full", fullHotKey);
	AddHotKey(message, "hotkey_window", windowHotKey);
	AddHotKey(message, "hotkey_region", regionHotKey);
	message.AddBool("launch_on_hotkey", launchOnHotKey);
	message.AddBool("hotkeys_paused", hotKeysPaused);

	message.AddBool("include_cursor", includeCursor);
	message.AddInt32("delay_seconds", delaySeconds);
	message.AddBool("include_decorations", includeDecorations);
	message.AddBool("animate_selection", animateSelection);
	message.AddBool("capture_screen_resolution", captureScreenResolution);

	message.AddInt32("after_capture", afterCapture);
	message.AddBool("copy_when_saving", copyWhenSaving);
	message.AddString("save_folder", saveFolder);
	message.AddString("file_prefix", filePrefix);

	message.AddBool("show_in_deskbar", showInDeskbar);
	message.AddRect("main_window_frame", mainWindowFrame);
	message.AddRect("editor_window_frame", editorWindowFrame);

	message.AddInt32("color_index", colorIndex);
	message.AddFloat("stroke_width", strokeWidth);
	message.AddFloat("font_size", fontSize);
}


void Settings::FromMessage(const BMessage& message)
{
	FindHotKey(message, "hotkey_full", fullHotKey);
	FindHotKey(message, "hotkey_window", windowHotKey);
	FindHotKey(message, "hotkey_region", regionHotKey);
	launchOnHotKey = message.GetBool("launch_on_hotkey", launchOnHotKey);
	hotKeysPaused = message.GetBool("hotkeys_paused", false);

	includeCursor = message.GetBool("include_cursor", includeCursor);
	delaySeconds = message.GetInt32("delay_seconds", delaySeconds);
	includeDecorations = message.GetBool("include_decorations", includeDecorations);
	animateSelection = message.GetBool("animate_selection", animateSelection);
	captureScreenResolution = message.GetBool("capture_screen_resolution",
		captureScreenResolution);

	afterCapture = message.GetInt32("after_capture", afterCapture);
	copyWhenSaving = message.GetBool("copy_when_saving", copyWhenSaving);
	saveFolder = message.GetString("save_folder", saveFolder.String());
	filePrefix = message.GetString("file_prefix", filePrefix.String());

	showInDeskbar = message.GetBool("show_in_deskbar", showInDeskbar);
	mainWindowFrame = message.GetRect("main_window_frame", mainWindowFrame);
	editorWindowFrame = message.GetRect("editor_window_frame", editorWindowFrame);

	colorIndex = message.GetInt32("color_index", colorIndex);
	strokeWidth = message.GetFloat("stroke_width", strokeWidth);
	fontSize = message.GetFloat("font_size", fontSize);
}


HotKey& Settings::HotKeyFor(CaptureKind kind)
{
	switch (kind) {
		case kCaptureWindow:
			return windowHotKey;
		case kCaptureRegion:
			return regionHotKey;
		default:
			return fullHotKey;
	}
}


const HotKey& Settings::HotKeyFor(CaptureKind kind) const
{
	return const_cast<Settings*>(this)->HotKeyFor(kind);
}

}  // namespace airshot
