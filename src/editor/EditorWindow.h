// The annotation editor: toolbar, canvas, menus, saving and copying.
#pragma once
#include <FilePanel.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <StringView.h>
#include <Window.h>

#include "CanvasView.h"
#include "Settings.h"

namespace BPrivate {
	class BToolBar;
}

namespace airshot {

class ColorSwatchView;

class EditorWindow : public BWindow {
public:
	// pixelScale: image pixels per screen coordinate unit (see CanvasView).
								EditorWindow(BBitmap* bitmap, const Settings& settings,
									float pixelScale = 1);
	virtual						~EditorWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();
	virtual	void				MenusBeginning();

private:
			void				_BuildMenu();
			void				_BuildToolBar();
			void				_SetTool(Tool tool);
			void				_UpdateControls();
			void				_UpdateStatus();
			void				_UpdateTitle();
			void				_ApplyStyle();
			status_t			_SaveTo(const char* path, bool notify);
			void				_QuickSave();
			void				_Copy();
			void				_ReportState();
			BRect				_InitialFrame(const Settings& settings, BRect imageBounds) const;

			Settings			fSettings;
			CanvasView*			fCanvas;
			BMenuBar*			fMenuBar;
			BPrivate::BToolBar*	fToolBar;
			BPrivate::BToolBar*	fStyleBar;
			ColorSwatchView*	fSwatches;
			BMenuField*			fWidthField;
			BMenuField*			fFontField;
			BStringView*		fStatus;
			BMenuItem*			fToolItems[kToolCount];
			BMenuItem*			fUndoItem;
			BMenuItem*			fRedoItem;
			BMenuItem*			fFillItem;
			BMenuItem*			fPixelateItem;
			BFilePanel*			fSavePanel;
			BPath				fSavedPath;
			int32				fColorIndex;
			int32				fCursorX;
			int32				fCursorY;
			bool				fReported;
};

}  // namespace airshot
