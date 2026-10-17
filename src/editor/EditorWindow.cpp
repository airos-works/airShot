#include "EditorWindow.h"

#include <Alert.h>
#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <ScrollView.h>
#include <ToolBar.h>

#include <math.h>
#include <stdio.h>

#include "Export.h"
#include "Messages.h"
#include "Palette.h"
#include "ui/IconButton.h"

namespace airshot {

namespace {

constexpr uint32 kMsgToolBase = 'Tl00';
constexpr uint32 kMsgColor = 'Colr';
constexpr uint32 kMsgWidth = 'Wdth';
constexpr uint32 kMsgFontSize = 'FnSz';
constexpr uint32 kMsgUndo = 'Undo';
constexpr uint32 kMsgRedo = 'Redo';
constexpr uint32 kMsgDelete = 'Dele';
constexpr uint32 kMsgSave = 'Save';
constexpr uint32 kMsgSaveAs = 'SvAs';
constexpr uint32 kMsgCopy = 'Copy';
constexpr uint32 kMsgZoomIn = 'ZmIn';
constexpr uint32 kMsgZoomOut = 'ZmOu';
constexpr uint32 kMsgZoomActual = 'ZmAc';
constexpr uint32 kMsgZoomFit = 'ZmFt';
constexpr uint32 kMsgToggleFill = 'TgFl';
constexpr uint32 kMsgTogglePixelate = 'TgPx';
constexpr uint32 kMsgInitialZoom = 'InZm';

void AddIconAction(BPrivate::BToolBar* toolbar, uint32 command, BHandler* target,
	const BBitmap* icon, const char* tooltip, const char* text = NULL, bool lockable = false)
{
	IconButton* button = new IconButton(NULL, text, new BMessage(command), lockable);
	button->SetIcon(icon);
	button->SetFlat(true);
	button->SetToolTip(tooltip);
	toolbar->AddView(button);
	button->SetTarget(target);
}

}  // namespace


// A row of colour dots; the chosen one wears a ring.
class ColorSwatchView : public BView {
public:
	ColorSwatchView(int32 selected)
		:
		BView("swatches", B_WILL_DRAW | B_NAVIGABLE),
		fSelected(selected)
	{
		fCell = floorf(be_plain_font->Size() * 1.7f);
		SetExplicitSize(BSize(fCell * kPaletteSize + 4, fCell + 4));
		SetToolTip("Annotation color. Use Left and Right when focused.");
	}

	void AttachedToWindow() override
	{
		AdoptParentColors();
	}

	void Draw(BRect updateRect) override
	{
		if (IsFocus()) {
			SetHighUIColor(B_KEYBOARD_NAVIGATION_COLOR);
			StrokeRect(Bounds());
		}
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		float radius = fCell * 0.34f;
		for (int32 i = 0; i < kPaletteSize; i++) {
			BPoint center = _Center(i);
			if (i == fSelected) {
				SetPenSize(2);
				SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
				StrokeEllipse(center, radius + 3, radius + 3);
				SetPenSize(1);
			}
			SetHighColor(kPalette[i]);
			FillEllipse(center, radius, radius);
			SetHighColor(0, 0, 0, 90);
			StrokeEllipse(center, radius, radius);
		}
	}

	void MouseDown(BPoint where) override
	{
		MakeFocus(true);
		for (int32 i = 0; i < kPaletteSize; i++) {
			BPoint center = _Center(i);
			if (fabsf(center.x - where.x) <= fCell / 2 && fabsf(center.y - where.y) <= fCell / 2) {
				_Choose(i);
				return;
			}
		}
	}

	void MakeFocus(bool focus) override
	{
		BView::MakeFocus(focus);
		Invalidate();
	}

	void KeyDown(const char* bytes, int32 count) override
	{
		if (count == 1 && (bytes[0] == B_LEFT_ARROW || bytes[0] == B_RIGHT_ARROW)) {
			_Choose((fSelected + (bytes[0] == B_RIGHT_ARROW ? 1 : kPaletteSize - 1))
				% kPaletteSize);
			return;
		}
		BView::KeyDown(bytes, count);
	}

	void SetSelected(int32 index)
	{
		fSelected = index;
		Invalidate();
	}

private:
	void _Choose(int32 index)
	{
		SetSelected(index);
		BMessage message(kMsgColor);
		message.AddInt32("index", index);
		Window()->PostMessage(&message, Window());
	}

	BPoint _Center(int32 index) const
	{
		return BPoint(2 + fCell * index + fCell / 2, 2 + fCell / 2);
	}

	int32 fSelected;
	float fCell;
};


EditorWindow::EditorWindow(BBitmap* bitmap, const Settings& settings, float pixelScale)
	:
	BWindow(BRect(100, 100, 900, 700), "airShot", B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fSettings(settings),
	fSavePanel(NULL),
	fColorIndex(settings.colorIndex),
	fCursorX(-1),
	fCursorY(-1),
	fReported(false)
{
	if (fColorIndex < 0 || fColorIndex >= kPaletteSize)
		fColorIndex = 0;
	Style style;
	style.color = kPalette[fColorIndex];
	style.strokeWidth = settings.strokeWidth > 0 ? settings.strokeWidth : 4;
	style.fontSize = settings.fontSize > 0 ? settings.fontSize : 24;
	style.filled = false;
	style.pixelate = false;

	fCanvas = new CanvasView(bitmap, style, pixelScale);
	fMenuBar = new BMenuBar("menu");
	_BuildMenu();
	_BuildToolBar();
	fStatus = new BStringView("status", "");
	// The whole row, truncated only when the window is narrower than the text.
	fStatus->SetExplicitMinSize(BSize(200, B_SIZE_UNSET));
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_END);
	BScrollView* scroll = new BScrollView("scroll", fCanvas, 0, true, true, B_NO_BORDER);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fMenuBar)
		.Add(fToolBar)
		.Add(fStyleBar)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, 0)
			.SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2)
			.Add(fStatus)
		.End();

	// The image as big as it is on the screen.
	BRect imageBounds = bitmap->Bounds();
	imageBounds.right = (imageBounds.Width() + 1) * fCanvas->ActualZoom() - 1;
	imageBounds.bottom = (imageBounds.Height() + 1) * fCanvas->ActualZoom() - 1;
	BRect frame = _InitialFrame(settings, imageBounds);
	MoveTo(frame.LeftTop());
	ResizeTo(frame.Width(), frame.Height());
	_SetTool(kToolArrow);
	_UpdateTitle();
	_UpdateControls();
	AddShortcut('Z', B_COMMAND_KEY | B_SHIFT_KEY, new BMessage(kMsgRedo));
	AddShortcut('Y', B_COMMAND_KEY, new BMessage(kMsgRedo));
	AddShortcut('=', B_COMMAND_KEY, new BMessage(kMsgZoomIn));
	PostMessage(kMsgInitialZoom);
}


EditorWindow::~EditorWindow()
{
	delete fSavePanel;
}


BRect EditorWindow::_InitialFrame(const Settings& settings, BRect imageBounds) const
{
	BRect screen = BScreen().Frame();
	BRect frame = settings.editorWindowFrame;
	if (!frame.IsValid() || frame.Width() < 500 || frame.Height() < 300
		|| !screen.Contains(frame)) {
		// Big enough for the image at 100%, within most of the screen.
		float chrome = 150;
		float width = fminf(imageBounds.Width() + 40, screen.Width() * 0.9f);
		float height = fminf(imageBounds.Height() + chrome, screen.Height() * 0.9f);
		width = fmaxf(width, 760);
		height = fmaxf(height, 480);
		frame.Set(0, 0, width, height);
		frame.OffsetTo(screen.left + (screen.Width() - width) / 2,
			screen.top + (screen.Height() - height) / 2);
	}
	return frame;
}


void EditorWindow::_BuildMenu()
{
	BMenu* file = new BMenu("File");
	file->AddItem(new BMenuItem("Save", new BMessage(kMsgSave), 'S'));
	file->AddItem(new BMenuItem("Save as" B_UTF8_ELLIPSIS, new BMessage(kMsgSaveAs), 'S',
		B_SHIFT_KEY));
	file->AddItem(new BMenuItem("Copy image to clipboard", new BMessage(kMsgCopy), 'C'));
	file->AddSeparatorItem();
	file->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
	fMenuBar->AddItem(file);

	BMenu* edit = new BMenu("Edit");
	fUndoItem = new BMenuItem("Undo", new BMessage(kMsgUndo), 'Z');
	fRedoItem = new BMenuItem("Redo", new BMessage(kMsgRedo), 'Z', B_SHIFT_KEY);
	edit->AddItem(fUndoItem);
	edit->AddItem(fRedoItem);
	edit->AddSeparatorItem();
	edit->AddItem(new BMenuItem("Delete annotation", new BMessage(kMsgDelete)));
	fMenuBar->AddItem(edit);

	BMenu* tools = new BMenu("Tools");
	tools->SetRadioMode(true);
	for (int32 i = 0; i < kToolCount; i++) {
		BString label(ToolName((Tool)i));
		label << " (" << ToolShortcut((Tool)i) << ")";
		BMessage* message = new BMessage(kMsgToolBase + i);
		fToolItems[i] = new BMenuItem(label.String(), message);
		tools->AddItem(fToolItems[i]);
	}
	fMenuBar->AddItem(tools);

	BMenu* options = new BMenu("Options");
	fFillItem = new BMenuItem("Fill rectangles and ellipses", new BMessage(kMsgToggleFill));
	fPixelateItem = new BMenuItem("Pixelate instead of blur", new BMessage(kMsgTogglePixelate));
	options->AddItem(fFillItem);
	options->AddItem(fPixelateItem);
	fMenuBar->AddItem(options);

	BMenu* view = new BMenu("View");
	view->AddItem(new BMenuItem("Zoom in", new BMessage(kMsgZoomIn), '+'));
	view->AddItem(new BMenuItem("Zoom out", new BMessage(kMsgZoomOut), '-'));
	view->AddItem(new BMenuItem("Actual size", new BMessage(kMsgZoomActual), '0'));
	view->AddItem(new BMenuItem("Fit to window", new BMessage(kMsgZoomFit), '9'));
	fMenuBar->AddItem(view);
}


void EditorWindow::_BuildToolBar()
{
	fToolBar = new BPrivate::BToolBar(B_HORIZONTAL);
	float iconSize = floorf(be_plain_font->Size() * 1.6f);
	for (int32 i = 0; i < kToolCount; i++) {
		BBitmap* icon = MakeToolIcon((Tool)i, iconSize);
		BString tip(ToolName((Tool)i));
		tip << " (" << ToolShortcut((Tool)i) << ")";
		AddIconAction(fToolBar, kMsgToolBase + i, this, icon, tip.String(), NULL, true);
		delete icon;
	}
	// Keep style controls on a separate row so the editor fits smaller screens.
	fStyleBar = new BPrivate::BToolBar(B_HORIZONTAL);
	fStyleBar->AddView(new BStringView("colorLabel", "Color:"));
	fSwatches = new ColorSwatchView(fColorIndex);
	fStyleBar->AddView(fSwatches);
	fStyleBar->AddSeparator();

	BPopUpMenu* widthMenu = new BPopUpMenu("width");
	widthMenu->SetRadioMode(true);
	const char* widthNames[] = {"Thin", "Normal", "Thick", "Heavy"};
	for (int32 i = 0; i < kStrokeWidthCount; i++) {
		BMessage* message = new BMessage(kMsgWidth);
		message->AddFloat("width", kStrokeWidths[i]);
		BMenuItem* item = new BMenuItem(widthNames[i], message);
		item->SetMarked(fabsf(kStrokeWidths[i] - fCanvas->CurrentStyle().strokeWidth) < 0.5f);
		widthMenu->AddItem(item);
	}
	if (widthMenu->FindMarked() == NULL)
		widthMenu->ItemAt(1)->SetMarked(true);
	fWidthField = new BMenuField("widthField", "Width:", widthMenu);
	fWidthField->SetExplicitMaxSize(fWidthField->PreferredSize());
	fStyleBar->AddView(fWidthField);

	BPopUpMenu* fontMenu = new BPopUpMenu("font");
	fontMenu->SetRadioMode(true);
	for (int32 i = 0; i < kFontSizeCount; i++) {
		BMessage* message = new BMessage(kMsgFontSize);
		message->AddFloat("size", kFontSizes[i]);
		BString label;
		label << (int)kFontSizes[i];
		BMenuItem* item = new BMenuItem(label.String(), message);
		item->SetMarked(fabsf(kFontSizes[i] - fCanvas->CurrentStyle().fontSize) < 0.5f);
		fontMenu->AddItem(item);
	}
	if (fontMenu->FindMarked() == NULL)
		fontMenu->ItemAt(2)->SetMarked(true);
	fFontField = new BMenuField("fontField", "Text:", fontMenu);
	fFontField->SetExplicitMaxSize(fFontField->PreferredSize());
	fStyleBar->AddView(fFontField);
	fStyleBar->AddGlue();

	fToolBar->AddGlue();
	BBitmap* icon = MakeActionIcon(kIconUndo, iconSize);
	AddIconAction(fToolBar, kMsgUndo, this, icon, "Undo (Alt+Z)");
	delete icon;
	icon = MakeActionIcon(kIconRedo, iconSize);
	AddIconAction(fToolBar, kMsgRedo, this, icon, "Redo (Shift+Alt+Z)");
	delete icon;
	fToolBar->AddSeparator();
	icon = MakeActionIcon(kIconCopy, iconSize);
	AddIconAction(fToolBar, kMsgCopy, this, icon, "Copy to clipboard (Alt+C)", "Copy");
	delete icon;
	icon = MakeActionIcon(kIconSave, iconSize);
	AddIconAction(fToolBar, kMsgSave, this, icon, "Save (Alt+S)", "Save");
	delete icon;
}


void EditorWindow::_SetTool(Tool tool)
{
	fCanvas->SetTool(tool);
	for (int32 i = 0; i < kToolCount; i++) {
		fToolBar->SetActionPressed(kMsgToolBase + i, i == tool);
		fToolItems[i]->SetMarked(i == tool);
	}
}


void EditorWindow::_UpdateControls()
{
	fToolBar->SetActionEnabled(kMsgUndo, fCanvas->CanUndo());
	fToolBar->SetActionEnabled(kMsgRedo, fCanvas->CanRedo());
	fUndoItem->SetEnabled(fCanvas->CanUndo());
	fRedoItem->SetEnabled(fCanvas->CanRedo());
	fFillItem->SetMarked(fCanvas->CurrentStyle().filled);
	fPixelateItem->SetMarked(fCanvas->CurrentStyle().pixelate);
	_UpdateStatus();
}


void EditorWindow::_UpdateStatus()
{
	BRect bounds = fCanvas->ImageBounds();
	BString text;
	// The zoom is relative to the actual size: 100% shows screen pixels as they were.
	text.SetToFormat("%d \xc3\x97 %d px   \xc2\xb7   %d%%", (int)bounds.Width() + 1,
		(int)bounds.Height() + 1,
		(int)roundf(fCanvas->Zoom() * fCanvas->PixelScale() * 100));
	if (fCursorX >= 0)
		text << "   \xc2\xb7   " << fCursorX << ", " << fCursorY;
	if (fCanvas->CountAnnotations() > 0)
		text << "   \xc2\xb7   " << fCanvas->CountAnnotations() << " annotation"
			<< (fCanvas->CountAnnotations() == 1 ? "" : "s");
	fStatus->SetText(text.String());
}


void EditorWindow::_UpdateTitle()
{
	BString title("airShot");
	if (fSavedPath.InitCheck() == B_OK)
		title << " \xe2\x80\x94 " << fSavedPath.Leaf();
	else {
		BRect bounds = fCanvas->ImageBounds();
		title << " \xe2\x80\x94 " << (int)bounds.Width() + 1 << "\xc3\x97"
			<< (int)bounds.Height() + 1;
	}
	if (fCanvas->IsDirty())
		title << " *";
	SetTitle(title.String());
}


void EditorWindow::_ApplyStyle()
{
	Style style = fCanvas->CurrentStyle();
	style.color = kPalette[fColorIndex];
	BMenuItem* marked = fWidthField->Menu()->FindMarked();
	if (marked != NULL)
		style.strokeWidth = marked->Message()->GetFloat("width", style.strokeWidth);
	marked = fFontField->Menu()->FindMarked();
	if (marked != NULL)
		style.fontSize = marked->Message()->GetFloat("size", style.fontSize);
	style.filled = fFillItem->IsMarked();
	style.pixelate = fPixelateItem->IsMarked();
	fCanvas->SetStyle(style);
	_ReportState();
}


void EditorWindow::_ReportState()
{
	Style style = fCanvas->CurrentStyle();
	BMessage state(kMsgEditorState);
	state.AddInt32("color_index", fColorIndex);
	state.AddFloat("stroke_width", style.strokeWidth);
	state.AddFloat("font_size", style.fontSize);
	state.AddRect("frame", Frame());
	be_app->PostMessage(&state);
}


status_t EditorWindow::_SaveTo(const char* path, bool notify)
{
	BBitmap* flat = fCanvas->Flatten();
	if (flat == NULL)
		return B_NO_MEMORY;
	status_t status = Export::SavePNG(flat, path);
	if (status == B_OK && fSettings.copyWhenSaving)
		Export::CopyToClipboard(flat);
	delete flat;
	if (status != B_OK) {
		BString text("The screenshot could not be saved to\n");
		text << path << "\n\n" << strerror(status);
		BAlert* alert = new BAlert("Save failed", text.String(), "OK", NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go(NULL);
		return status;
	}
	fSavedPath.SetTo(path);
	fCanvas->SetClean();
	_UpdateTitle();
	if (notify) {
		BString content(fSettings.copyWhenSaving ? "Saved and copied to the clipboard\n" : "Saved\n");
		content << path;
		Export::Notify("Screenshot saved", content.String());
	}
	return B_OK;
}


void EditorWindow::_QuickSave()
{
	fCanvas->CommitText();
	if (fSavedPath.InitCheck() == B_OK) {
		_SaveTo(fSavedPath.Path(), true);
		return;
	}
	BString folder(fSettings.saveFolder);
	if (folder.IsEmpty() || create_directory(folder.String(), 0755) != B_OK) {
		BPath desktop;
		find_directory(B_DESKTOP_DIRECTORY, &desktop);
		folder = desktop.Path();
	}
	BString path = Export::UniquePath(folder.String(),
		Export::DefaultFileName(fSettings.filePrefix.String()).String());
	_SaveTo(path.String(), true);
}


void EditorWindow::_Copy()
{
	fCanvas->CommitText();
	BBitmap* flat = fCanvas->Flatten();
	if (flat == NULL)
		return;
	if (Export::CopyToClipboard(flat) == B_OK)
		Export::Notify("Copied to clipboard", "The screenshot is ready to paste.");
	delete flat;
}


void EditorWindow::MenusBeginning()
{
	_UpdateControls();
}


void EditorWindow::MessageReceived(BMessage* message)
{
	if (message->what >= kMsgToolBase && message->what < kMsgToolBase + kToolCount) {
		_SetTool((Tool)(message->what - kMsgToolBase));
		return;
	}
	switch (message->what) {
		case kMsgInitialZoom:
			// The canvas has its final size now: actual size, or smaller to
			// fit a big image.
			fCanvas->ZoomToFit();
			_UpdateStatus();
			break;

		case kMsgCanvasTool:
		{
			int32 tool = message->GetInt32("tool", kToolSelect);
			if (tool >= 0 && tool < kToolCount)
				_SetTool((Tool)tool);
			break;
		}

		case kMsgCanvasChanged:
			_UpdateControls();
			_UpdateTitle();
			break;

		case kMsgCanvasStatus:
			fCursorX = message->GetInt32("x", -1);
			fCursorY = message->GetInt32("y", -1);
			_UpdateStatus();
			break;

		case kMsgColor:
		{
			int32 index = message->GetInt32("index", 0);
			if (index >= 0 && index < kPaletteSize) {
				fColorIndex = index;
				fSwatches->SetSelected(index);
				_ApplyStyle();
			}
			break;
		}

		case kMsgWidth:
		case kMsgFontSize:
		case kMsgToggleFill:
		case kMsgTogglePixelate:
			if (message->what == kMsgToggleFill)
				fFillItem->SetMarked(!fFillItem->IsMarked());
			if (message->what == kMsgTogglePixelate)
				fPixelateItem->SetMarked(!fPixelateItem->IsMarked());
			_ApplyStyle();
			break;

		case kMsgUndo:
			fCanvas->Undo();
			break;
		case kMsgRedo:
			fCanvas->Redo();
			break;
		case kMsgDelete:
			fCanvas->DeleteSelection();
			break;

		case kMsgZoomIn:
			fCanvas->SetZoom(fCanvas->Zoom() * 1.25f);
			break;
		case kMsgZoomOut:
			fCanvas->SetZoom(fCanvas->Zoom() / 1.25f);
			break;
		case kMsgZoomActual:
			fCanvas->SetZoom(fCanvas->ActualZoom());
			break;
		case kMsgZoomFit:
			fCanvas->ZoomToFit();
			break;

		case kMsgSave:
			_QuickSave();
			break;

		case kMsgSaveAs:
		{
			fCanvas->CommitText();
			if (fSavePanel == NULL) {
				BMessenger target(this);
				fSavePanel = new BFilePanel(B_SAVE_PANEL, &target, NULL, 0, false);
			}
			BString folder(fSettings.saveFolder);
			if (fSavedPath.InitCheck() == B_OK) {
				BPath parent;
				fSavedPath.GetParent(&parent);
				folder = parent.Path();
			}
			if (!folder.IsEmpty())
				fSavePanel->SetPanelDirectory(folder.String());
			BString name = fSavedPath.InitCheck() == B_OK ? BString(fSavedPath.Leaf())
				: Export::DefaultFileName(fSettings.filePrefix.String());
			fSavePanel->SetSaveText(name.String());
			fSavePanel->Show();
			break;
		}

		case B_SAVE_REQUESTED:
		{
			entry_ref directory;
			const char* name;
			if (message->FindRef("directory", &directory) != B_OK
				|| message->FindString("name", &name) != B_OK)
				break;
			BPath path(&directory);
			path.Append(name);
			_SaveTo(path.Path(), true);
			break;
		}

		case kMsgCopy:
			_Copy();
			break;

		default:
			BWindow::MessageReceived(message);
	}
}


bool EditorWindow::QuitRequested()
{
	fCanvas->CommitText();
	if (fCanvas->IsDirty() && fCanvas->CountAnnotations() > 0) {
		BAlert* alert = new BAlert("Close", "Close the editor without saving the annotated "
			"screenshot?", "Cancel", "Save", "Don't save", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		int32 choice = alert->Go();
		if (choice == 0)
			return false;
		if (choice == 1) {
			_QuickSave();
			if (fCanvas->IsDirty())
				return false;
		}
	}
	if (!fReported) {
		fReported = true;
		_ReportState();
	}
	return true;
}

}  // namespace airshot
