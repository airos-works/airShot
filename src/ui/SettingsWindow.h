// The settings window: shortcuts, capture options and what happens after a
// capture. Every change is applied and saved immediately.
#pragma once
#include <CheckBox.h>
#include <FilePanel.h>
#include <MenuField.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include "KeyCaptureControl.h"
#include "Settings.h"

namespace airshot {

class SettingsWindow : public BWindow {
public:
								SettingsWindow(const Settings& settings);
	virtual						~SettingsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	void				DispatchMessage(BMessage* message, BHandler* handler);
	virtual	bool				QuitRequested();
	virtual	void				ScreenChanged(BRect frame, color_space mode);

			void				Update(const Settings& settings);

private:
			void				_Apply();
			void				_LoadControls();
			BMenuField*			_MakeDelayField();
			BMenuField*			_MakeAfterField();
			BMenuField*			_MakeResolutionField();
			void				_UpdateScaleHint();

			Settings			fSettings;
			KeyCaptureControl*	fFullKey;
			KeyCaptureControl*	fWindowKey;
			KeyCaptureControl*	fRegionKey;
			BCheckBox*			fLaunchOnHotKey;
			BCheckBox*			fIncludeCursor;
			BCheckBox*			fIncludeDecorations;
			BCheckBox*			fAnimate;
			BMenuField*			fDelay;
			BMenuField*			fResolution;
			BStringView*		fScaleHint;
			BMenuField*			fAfter;
			BCheckBox*			fCopyWhenSaving;
			BTextControl*		fSaveFolder;
			BTextControl*		fFilePrefix;
			BCheckBox*			fShowInDeskbar;
			BFilePanel*			fFolderPanel;
			KeyCaptureControl*	fRecording;
			bool				fLoading;
};

}  // namespace airshot
