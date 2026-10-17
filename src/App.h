// The airShot application: receives capture requests (from the input_server
// filter, the Deskbar icon, the launcher window or the command line), grabs
// the screen, runs the overlay and hands the result to the editor, the
// clipboard or a file.
#pragma once
#include <Application.h>
#include <Bitmap.h>
#include <MessageRunner.h>

#include <vector>

#include "Messages.h"
#include "Settings.h"
#include "capture/ScreenCapture.h"

namespace airshot {

class MainWindow;
class SettingsWindow;

class App : public BApplication {
public:
								App();
	virtual						~App();

	virtual	void				ReadyToRun();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				ArgvReceived(int32 argc, char** argv);
	virtual	void				RefsReceived(BMessage* message);
	virtual	void				AboutRequested();
	virtual	bool				QuitRequested();

private:
			void				_StartCapture(CaptureKind kind);
			void				_CaptureNow();
			void				_OverlayDone(BMessage* message);
			void				_Finish(BBitmap* bitmap, float pixelScale);
			void				_OpenEditor(BBitmap* bitmap, float pixelScale);
			void				_ShowMainWindow();
			void				_ShowSettings();
			void				_ApplySettings(const Settings& settings);
			void				_SyncDeskbar();
			void				_CloseOwnWindows();
			void				_SaveSettings();
			void				_HideOwnWindows();

			Settings			fSettings;
			MainWindow*			fMainWindow;
			SettingsWindow*		fSettingsWindow;
			BBitmap*			fScreen;
			float				fScreenScale;  // fScreen pixels per screen coordinate unit
			CaptureKind			fPendingKind;
			bool				fCapturing;
			bool				fCaptureRequested;
			BMessageRunner*		fDelayRunner;
			BMessageRunner*		fDeskbarRetry;
			int32				fDeskbarTries;
};

}  // namespace airshot
