// The full screen overlay shown over a frozen copy of the screen: the user
// picks a window (highlighted with an animated frame as the mouse moves) or
// drags out a region, adjusts it with handles, and confirms.
#pragma once
#include <Bitmap.h>
#include <Cursor.h>
#include <Messenger.h>
#include <Region.h>
#include <View.h>
#include <Window.h>

#include <vector>

#include "Messages.h"
#include "ScreenCapture.h"

class BMessageRunner;

namespace airshot {

class OverlayView;

class OverlayWindow : public BWindow {
public:
	// pixelScale: screen pixels per coordinate unit (ScreenCapture::GrabScreen).
								OverlayWindow(const BBitmap* screen, float pixelScale,
									const std::vector<WindowEntry>& windows, CaptureKind mode,
									bool animate, bool includeDecorations,
									const BMessenger& target);
	virtual						~OverlayWindow();

	virtual	bool				QuitRequested();

			void				Finish(BMessage* result);

private:
			OverlayView*		fView;
			BMessenger			fTarget;
			bool				fFinished;
};


class OverlayView : public BView {
public:
								OverlayView(BRect frame, const BBitmap* screen, float pixelScale,
									const std::vector<WindowEntry>& windows, CaptureKind mode,
									bool animate, bool includeDecorations);
	virtual						~OverlayView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit, const BMessage* drag);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MessageReceived(BMessage* message);

private:
	enum State {
		kIdle,        // nothing selected; window mode highlights the hovered window
		kPending,     // mouse pressed, not yet moved far enough to drag
		kCreating,    // dragging out a new region
		kSelected,    // a region exists and can be adjusted
		kMoving,
		kResizing,
		kConfirming,  // the flash before the result is delivered
	};

	enum Handle {
		kNoHandle = -1,
		kTopLeft, kTop, kTopRight, kRight, kBottomRight, kBottom, kBottomLeft, kLeft,
	};

			void				_MakeDimmedCopy();
			void				_DrawScreen(const BBitmap* bitmap, BRect rect);
			int32				_WindowAt(BPoint where) const;
			void				_SetTarget(BRect target, bool immediate);
			void				_Tick();
			void				_InvalidateChange(BRect from, BRect to);
			BRect				_Outer(BRect rect) const;
			BRect				_LabelRect(BRect selection, const char* text) const;
			BRect				_ButtonRect(BRect selection) const;
			void				_DrawSelectionChrome(BRect selection, BRect updateRect);
			void				_DrawPill(BRect rect, const char* text, bool accent);
			void				_DrawHint(BRect updateRect);
			void				_DrawCrosshair(BRect updateRect);
			Handle				_HandleAt(BPoint where) const;
			BRect				_HandleRect(Handle handle, BRect selection) const;
			void				_UpdateCursor(BPoint where);
			void				_Confirm();
			void				_SendResult();
			void				_Cancel();
			BRect				_NormalizedRect(BPoint a, BPoint b) const;
			void				_SetSelection(BRect selection);
			BString				_SizeLabel(BRect rect) const;

			const BBitmap*		fScreen;
			float				fPixelScale;
			BBitmap*			fDimmed;
			std::vector<WindowEntry> fWindows;
			CaptureKind			fMode;
			bool				fAnimate;
			bool				fIncludeDecorations;
			BRect				fScreenFrame;

			State				fState;
			int32				fHovered;         // index into fWindows, -1 when none
			int32				fSelectedWindow;  // window the selection came from, or -1
			BRect				fTarget;          // where the highlight is heading
			BRect				fShown;           // where the highlight is drawn now
			BRect				fSelection;       // region mode selection (may be invalid)
			BPoint				fPressPoint;
			BRect				fPressSelection;
			Handle				fHandle;
			BPoint				fMouse;
			BMessageRunner*		fRunner;
			float				fPulse;
			float				fFlash;
			int32				fLastCursor;
			BCursor				fCrossCursor;
			BCursor				fMoveCursor;
			BCursor				fGrabCursor;
			bigtime_t			fLastClick;
			BPoint				fLastClickPoint;
};

}  // namespace airshot
