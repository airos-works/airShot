// Persistent settings, stored as a flattened BMessage in
// ~/config/settings/airShot/settings. The input_server filter reads the same
// file (it only needs the shortcuts), so this class must not depend on
// be_app.
#pragma once
#include <Message.h>
#include <Path.h>
#include <Rect.h>
#include <String.h>

#include "HotKey.h"
#include "Messages.h"

namespace airshot {

class Settings {
public:
								Settings();

			void				SetDefaults();
			status_t			Load();
			status_t			Save() const;

	static	status_t			GetPath(BPath& path, bool create);

			void				ToMessage(BMessage& message) const;
			void				FromMessage(const BMessage& message);

			HotKey&				HotKeyFor(CaptureKind kind);
			const HotKey&		HotKeyFor(CaptureKind kind) const;

	// Shortcuts
			HotKey				fullHotKey;
			HotKey				windowHotKey;
			HotKey				regionHotKey;
			bool				launchOnHotKey;
			bool				hotKeysPaused;  // while a shortcut is being recorded

	// Capture
			bool				includeCursor;
			int32				delaySeconds;
			bool				includeDecorations;
			bool				animateSelection;
			// On a scaled display: the screen's own pixels rather than 100%.
			bool				captureScreenResolution;

	// After a capture
			int32				afterCapture;
			bool				copyWhenSaving;
			BString				saveFolder;
			BString				filePrefix;

	// Application
			bool				showInDeskbar;
			BRect				mainWindowFrame;
			BRect				editorWindowFrame;

	// Editor defaults
			int32				colorIndex;
			float				strokeWidth;
			float				fontSize;
};

}  // namespace airshot
