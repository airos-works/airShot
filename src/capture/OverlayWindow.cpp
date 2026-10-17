#include "OverlayWindow.h"

#include <Application.h>
#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <String.h>
#include <WindowPrivate.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define OTRACE(...) do { if (getenv("AIRSHOT_TRACE") != NULL) { fprintf(stderr, "overlay %lld: ", (long long)(system_time() / 1000)); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

namespace airshot {

namespace {

constexpr uint32 kMsgTick = 'Tick';
constexpr bigtime_t kTickInterval = 16000;
constexpr float kBorderWidth = 2;
constexpr float kHandleSize = 9;
constexpr float kDragThreshold = 4;
constexpr float kChromeMargin = 40;  // room for labels and handles around a selection

const rgb_color kAccent = {30, 140, 255, 255};
const rgb_color kAccentSoft = {255, 255, 255, 255};
const rgb_color kPillColor = {20, 24, 32, 200};
const rgb_color kPillAccent = {30, 140, 255, 230};
const rgb_color kPillText = {255, 255, 255, 255};


float Ease(float from, float to)
{
	float next = from + (to - from) * 0.38f;
	if (fabsf(next - to) < 0.6f)
		return to;
	return next;
}

}  // namespace


//	#pragma mark - OverlayWindow


OverlayWindow::OverlayWindow(const BBitmap* screen, float pixelScale,
	const std::vector<WindowEntry>& windows, CaptureKind mode, bool animate,
	bool includeDecorations, const BMessenger& target)
	:
	// The same window type Haiku's own Screenshot uses for its area
	// selector: borderless, above everything including Deskbar.
	BWindow(BScreen().Frame(), "airShot overlay", kWindowScreenWindow,
		B_NOT_RESIZABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MOVABLE
			| B_NOT_MINIMIZABLE | B_NOT_ANCHORED_ON_ACTIVATE),
	fTarget(target),
	fFinished(false)
{
	fView = new OverlayView(Bounds(), screen, pixelScale, windows, mode, animate,
		includeDecorations);
	AddChild(fView);
	fView->MakeFocus(true);
}


OverlayWindow::~OverlayWindow()
{
	OTRACE("window destroyed, finished=%d", (int)fFinished);
	if (!fFinished) {
		BMessage cancelled(kMsgOverlayCancelled);
		fTarget.SendMessage(&cancelled);
	}
}


bool OverlayWindow::QuitRequested()
{
	OTRACE("QuitRequested");
	return true;
}


void OverlayWindow::Finish(BMessage* result)
{
	if (fFinished)
		return;
	fFinished = true;
	Hide();
	fTarget.SendMessage(result);
	PostMessage(B_QUIT_REQUESTED);
}


//	#pragma mark - OverlayView


OverlayView::OverlayView(BRect frame, const BBitmap* screen, float pixelScale,
	const std::vector<WindowEntry>& windows, CaptureKind mode, bool animate,
	bool includeDecorations)
	:
	BView(frame, "overlay", B_FOLLOW_ALL, B_WILL_DRAW),
	fScreen(screen),
	fPixelScale(pixelScale),
	fDimmed(NULL),
	fWindows(windows),
	fMode(mode),
	fAnimate(animate),
	fIncludeDecorations(includeDecorations),
	fScreenFrame(frame),
	fState(kIdle),
	fHovered(-1),
	fSelectedWindow(-1),
	fTarget(0, 0, -1, -1),
	fShown(0, 0, -1, -1),
	fSelection(0, 0, -1, -1),
	fHandle(kNoHandle),
	fMouse(-1, -1),
	fRunner(NULL),
	fPulse(0),
	fFlash(0),
	fLastCursor(-1),
	fCrossCursor(B_CURSOR_ID_CROSS_HAIR),
	fMoveCursor(B_CURSOR_ID_MOVE),
	fGrabCursor(B_CURSOR_ID_GRABBING),
	fLastClick(0),
	fLastClickPoint(-100, -100)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	_MakeDimmedCopy();
}


OverlayView::~OverlayView()
{
	delete fDimmed;
}


void OverlayView::_MakeDimmedCopy()
{
	fDimmed = new BBitmap(fScreen->Bounds(), B_RGB32);
	if (fDimmed->InitCheck() != B_OK) {
		delete fDimmed;
		fDimmed = NULL;
		return;
	}
	fDimmed->ImportBits(fScreen);
	// 62.5% brightness, one shift-and-mask per pixel.
	uint32* pixel = (uint32*)fDimmed->Bits();
	uint32* end = pixel + fDimmed->BitsLength() / 4;
	for (; pixel < end; pixel++)
		*pixel = ((*pixel >> 1) & 0x7f7f7f7f) + ((*pixel >> 3) & 0x1f1f1f1f);
}


void OverlayView::_DrawScreen(const BBitmap* bitmap, BRect rect)
{
	if (fPixelScale == 1) {
		DrawBitmap(bitmap, rect, rect);
		return;
	}
	// Screen pixels go back one to one, from the pixel the rectangle starts
	// on, so the frozen screen looks exactly like the live one.
	BRect pixels = ScreenCapture::ToPixels(rect, fPixelScale) & bitmap->Bounds();
	if (pixels.IsValid())
		DrawBitmap(bitmap, pixels, rect, kDevicePixelBitmap);
}


void OverlayView::AttachedToWindow()
{
	OTRACE("AttachedToWindow");
	BView::AttachedToWindow();
	BMessage tick(kMsgTick);
	fRunner = new BMessageRunner(BMessenger(this), &tick, kTickInterval);
	// No permanent event mask: the view covers the whole screen anyway, and
	// a B_POINTER_EVENTS mask with B_NO_POINTER_HISTORY stopped MouseMoved()
	// from arriving at all on the test machine.
	uint32 buttons;
	GetMouse(&fMouse, &buttons, false);
	if (fMode == kCaptureWindow) {
		fHovered = _WindowAt(fMouse);
		if (fHovered >= 0)
			_SetTarget(fWindows[fHovered].CaptureFrame(fIncludeDecorations, fScreenFrame), true);
	}
	_UpdateCursor(fMouse);
}


void OverlayView::DetachedFromWindow()
{
	delete fRunner;
	fRunner = NULL;
	BView::DetachedFromWindow();
}


BRect OverlayView::_Outer(BRect rect) const
{
	return rect.InsetByCopy(-kChromeMargin, -kChromeMargin);
}


void OverlayView::_InvalidateChange(BRect from, BRect to)
{
	BRegion region;
	if (from.IsValid())
		region.Include(_Outer(from));
	if (to.IsValid())
		region.Include(_Outer(to));
	// The part that stays bright in both does not need a repaint.
	if (from.IsValid() && to.IsValid()) {
		BRect common = from.InsetByCopy(kBorderWidth + 1, kBorderWidth + 1)
			& to.InsetByCopy(kBorderWidth + 1, kBorderWidth + 1);
		if (common.IsValid())
			region.Exclude(common);
	}
	for (int32 i = 0; i < region.CountRects(); i++)
		Invalidate(region.RectAt(i));
}


void OverlayView::_SetTarget(BRect target, bool immediate)
{
	if (target == fTarget && (immediate ? target == fShown : true))
		return;
	fTarget = target;
	if (immediate || !fAnimate || !fShown.IsValid() || !target.IsValid()) {
		BRect previous = fShown;
		fShown = target;
		_InvalidateChange(previous, fShown);
	}
}


void OverlayView::_Tick()
{
	float previousPulse = fPulse;
	fPulse += 0.05f;
	if (fPulse > 1)
		fPulse -= 1;

	if (fState == kConfirming) {
		fFlash -= 0.2f;
		Invalidate(fShown);
		if (fFlash <= 0) {
			fFlash = 0;
			_SendResult();
		}
		return;
	}

	if (fShown != fTarget && fTarget.IsValid()) {
		BRect previous = fShown;
		fShown.left = Ease(fShown.left, fTarget.left);
		fShown.top = Ease(fShown.top, fTarget.top);
		fShown.right = Ease(fShown.right, fTarget.right);
		fShown.bottom = Ease(fShown.bottom, fTarget.bottom);
		_InvalidateChange(previous, fShown);
	} else if (fShown.IsValid() && (int)(previousPulse * 20) != (int)(fPulse * 20)) {
		// Only the frame breathes; repaint its four edges.
		BRect outer = fShown.InsetByCopy(-kBorderWidth - 2, -kBorderWidth - 2);
		float w = kBorderWidth * 2 + 4;
		Invalidate(BRect(outer.left, outer.top, outer.right, outer.top + w));
		Invalidate(BRect(outer.left, outer.bottom - w, outer.right, outer.bottom));
		Invalidate(BRect(outer.left, outer.top, outer.left + w, outer.bottom));
		Invalidate(BRect(outer.right - w, outer.top, outer.right, outer.bottom));
	}
}


void OverlayView::MessageReceived(BMessage* message)
{
	if (message->what == kMsgTick) {
		_Tick();
		return;
	}
	BView::MessageReceived(message);
}


//	#pragma mark - drawing


void OverlayView::Draw(BRect updateRect)
{
	OTRACE("Draw %g,%g-%g,%g state %d", updateRect.left, updateRect.top, updateRect.right,
		updateRect.bottom, (int)fState);
	SetDrawingMode(B_OP_COPY);
	// Copy the frozen screen per clipping rectangle: the crosshair lines
	// alone make the update rectangle span the whole screen.
	BRegion clipping;
	GetClippingRegion(&clipping);
	if (clipping.CountRects() == 0)
		clipping.Set(updateRect);
	for (int32 i = 0; i < clipping.CountRects(); i++) {
		BRect rect = clipping.RectAt(i);
		_DrawScreen(fDimmed != NULL ? fDimmed : fScreen, rect);
		if (fShown.IsValid()) {
			BRect bright = fShown & rect;
			if (bright.IsValid())
				_DrawScreen(fScreen, bright);
		}
	}

	if (fShown.IsValid())
		_DrawSelectionChrome(fShown, updateRect);

	if (fMode == kCaptureRegion && fState == kIdle)
		_DrawCrosshair(updateRect);

	_DrawHint(updateRect);
	SetDrawingMode(B_OP_COPY);
}


void OverlayView::_DrawSelectionChrome(BRect selection, BRect updateRect)
{
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// Breathing frame: a white line for contrast, the accent on top.
	float glow = 0.55f + 0.45f * (0.5f + 0.5f * sinf(fPulse * 2 * M_PI));
	BRect frame = selection.InsetByCopy(-kBorderWidth / 2 - 0.5f, -kBorderWidth / 2 - 0.5f);
	SetPenSize(kBorderWidth + 2);
	rgb_color soft = kAccentSoft;
	soft.alpha = (uint8)(90 * glow);
	SetHighColor(soft);
	StrokeRect(frame);
	SetPenSize(kBorderWidth);
	rgb_color accent = kAccent;
	accent.alpha = (uint8)(150 + 105 * glow);
	SetHighColor(accent);
	StrokeRect(frame);
	SetPenSize(1);

	// Corner brackets make the frame readable on busy content.
	float bracket = fminf(18, fminf(selection.Width(), selection.Height()) / 3);
	if (bracket > 4) {
		SetPenSize(3);
		SetHighColor(255, 255, 255, 230);
		BRect b = frame.InsetByCopy(-2, -2);
		StrokeLine(BPoint(b.left, b.top + bracket), BPoint(b.left, b.top));
		StrokeLine(BPoint(b.left, b.top), BPoint(b.left + bracket, b.top));
		StrokeLine(BPoint(b.right - bracket, b.top), BPoint(b.right, b.top));
		StrokeLine(BPoint(b.right, b.top), BPoint(b.right, b.top + bracket));
		StrokeLine(BPoint(b.right, b.bottom - bracket), BPoint(b.right, b.bottom));
		StrokeLine(BPoint(b.right, b.bottom), BPoint(b.right - bracket, b.bottom));
		StrokeLine(BPoint(b.left + bracket, b.bottom), BPoint(b.left, b.bottom));
		StrokeLine(BPoint(b.left, b.bottom), BPoint(b.left, b.bottom - bracket));
		SetPenSize(1);
	}

	if (fState == kConfirming && fFlash > 0) {
		SetHighColor(255, 255, 255, (uint8)(140 * fFlash));
		FillRect(selection);
	}

	bool adjustable = fState == kSelected || fState == kMoving || fState == kResizing;
	if (adjustable) {
		for (int32 i = kTopLeft; i <= kLeft; i++) {
			BRect handle = _HandleRect((Handle)i, selection);
			if (!handle.Intersects(updateRect))
				continue;
			SetHighColor(kAccent);
			FillEllipse(handle);
			SetHighColor(255, 255, 255, 255);
			StrokeEllipse(handle);
		}
	}

	// Size (and name) label.
	BString text = _SizeLabel(selection);
	if (fMode == kCaptureWindow || fSelectedWindow >= 0) {
		int32 index = fState == kIdle ? fHovered : fSelectedWindow;
		if (index >= 0 && index < (int32)fWindows.size()) {
			BString name(fWindows[index].name);
			if (name.Length() > 0) {
				be_bold_font->TruncateString(&name, B_TRUNCATE_END, 320);
				name << "  \xe2\x80\x94  " << text;
				text = name;
			}
		}
	}
	BRect label = _LabelRect(selection, text.String());
	if (label.Intersects(updateRect))
		_DrawPill(label, text.String(), false);

	if (adjustable) {
		BRect button = _ButtonRect(selection);
		if (button.Intersects(updateRect))
			_DrawPill(button, "Capture  \xe2\x8f\x8e", true);
	}
}


BRect OverlayView::_LabelRect(BRect selection, const char* text) const
{
	font_height fh;
	be_bold_font->GetHeight(&fh);
	float height = ceilf(fh.ascent + fh.descent) + 10;
	float width = ceilf(be_bold_font->StringWidth(text)) + 20;
	BRect rect(0, 0, width, height);
	// Above the selection when there is room, else inside its top-left corner.
	if (selection.top - height - 8 >= fScreenFrame.top)
		rect.OffsetTo(selection.left, selection.top - height - 8);
	else
		rect.OffsetTo(selection.left + 8, selection.top + 8);
	if (rect.right > fScreenFrame.right)
		rect.OffsetBy(fScreenFrame.right - rect.right, 0);
	if (rect.left < fScreenFrame.left)
		rect.OffsetBy(fScreenFrame.left - rect.left, 0);
	return rect;
}


BRect OverlayView::_ButtonRect(BRect selection) const
{
	font_height fh;
	be_bold_font->GetHeight(&fh);
	float height = ceilf(fh.ascent + fh.descent) + 12;
	float width = ceilf(be_bold_font->StringWidth("Capture  \xe2\x8f\x8e")) + 28;
	BRect rect(0, 0, width, height);
	// Below the selection's bottom-right corner, else above, else inside.
	if (selection.bottom + height + 10 <= fScreenFrame.bottom)
		rect.OffsetTo(selection.right - width, selection.bottom + 10);
	else if (selection.top - height - 10 >= fScreenFrame.top)
		rect.OffsetTo(selection.right - width, selection.top - height - 10);
	else
		rect.OffsetTo(selection.right - width - 10, selection.bottom - height - 10);
	if (rect.left < fScreenFrame.left)
		rect.OffsetBy(fScreenFrame.left - rect.left, 0);
	return rect;
}


void OverlayView::_DrawPill(BRect rect, const char* text, bool accent)
{
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	SetHighColor(accent ? kPillAccent : kPillColor);
	float radius = rect.Height() / 2;
	FillRoundRect(rect, radius, radius);
	SetHighColor(255, 255, 255, 60);
	StrokeRoundRect(rect, radius, radius);
	SetHighColor(kPillText);
	SetFont(be_bold_font);
	font_height fh;
	be_bold_font->GetHeight(&fh);
	float x = rect.left + (rect.Width() - be_bold_font->StringWidth(text)) / 2;
	float y = rect.top + (rect.Height() - (fh.ascent + fh.descent)) / 2 + fh.ascent;
	DrawString(text, BPoint(x, y));
}


void OverlayView::_DrawHint(BRect updateRect)
{
	const char* text;
	if (fState == kSelected || fState == kMoving || fState == kResizing) {
		text = "Drag the handles to adjust  \xc2\xb7  Enter or double-click captures  "
			"\xc2\xb7  Esc cancels";
	} else if (fMode == kCaptureWindow) {
		text = "Click a window to capture it  \xc2\xb7  drag to select an area  "
			"\xc2\xb7  Esc cancels";
	} else {
		text = "Drag to select an area  \xc2\xb7  click a window to use its frame  "
			"\xc2\xb7  Esc cancels";
	}
	font_height fh;
	be_bold_font->GetHeight(&fh);
	float height = ceilf(fh.ascent + fh.descent) + 14;
	float width = ceilf(be_bold_font->StringWidth(text)) + 32;
	BRect rect(0, 0, width, height);
	rect.OffsetTo(fScreenFrame.left + (fScreenFrame.Width() - width) / 2, fScreenFrame.top + 24);
	if (!rect.Intersects(updateRect))
		return;
	_DrawPill(rect, text, false);
}


void OverlayView::_DrawCrosshair(BRect updateRect)
{
	if (fMouse.x < 0)
		return;
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	SetHighColor(255, 255, 255, 110);
	SetPenSize(1);
	if (updateRect.left <= fMouse.x && updateRect.right >= fMouse.x)
		StrokeLine(BPoint(fMouse.x, updateRect.top), BPoint(fMouse.x, updateRect.bottom));
	if (updateRect.top <= fMouse.y && updateRect.bottom >= fMouse.y)
		StrokeLine(BPoint(updateRect.left, fMouse.y), BPoint(updateRect.right, fMouse.y));
}


BString OverlayView::_SizeLabel(BRect rect) const
{
	// The size of the picture, which is in screen pixels on a scaled screen.
	rect = ScreenCapture::ToPixels(rect, fPixelScale);
	BString text;
	text.SetToFormat("%d \xc3\x97 %d", (int)rect.Width() + 1, (int)rect.Height() + 1);
	return text;
}


//	#pragma mark - interaction


int32 OverlayView::_WindowAt(BPoint where) const
{
	for (size_t i = 0; i < fWindows.size(); i++) {
		if (fWindows[i].CaptureFrame(fIncludeDecorations, fScreenFrame).Contains(where))
			return (int32)i;
	}
	return -1;
}


BRect OverlayView::_HandleRect(Handle handle, BRect selection) const
{
	float x, y;
	float midX = (selection.left + selection.right) / 2;
	float midY = (selection.top + selection.bottom) / 2;
	switch (handle) {
		case kTopLeft: x = selection.left; y = selection.top; break;
		case kTop: x = midX; y = selection.top; break;
		case kTopRight: x = selection.right; y = selection.top; break;
		case kRight: x = selection.right; y = midY; break;
		case kBottomRight: x = selection.right; y = selection.bottom; break;
		case kBottom: x = midX; y = selection.bottom; break;
		case kBottomLeft: x = selection.left; y = selection.bottom; break;
		case kLeft: x = selection.left; y = midY; break;
		default: return BRect(0, 0, -1, -1);
	}
	float half = kHandleSize / 2;
	return BRect(x - half, y - half, x + half, y + half);
}


OverlayView::Handle OverlayView::_HandleAt(BPoint where) const
{
	if (!fSelection.IsValid())
		return kNoHandle;
	for (int32 i = kTopLeft; i <= kLeft; i++) {
		BRect rect = _HandleRect((Handle)i, fSelection).InsetByCopy(-4, -4);
		if (rect.Contains(where))
			return (Handle)i;
	}
	return kNoHandle;
}


void OverlayView::_UpdateCursor(BPoint where)
{
	int32 wanted = B_CURSOR_ID_CROSS_HAIR;
	if (fState == kSelected) {
		Handle handle = _HandleAt(where);
		switch (handle) {
			case kTopLeft:
			case kBottomRight:
				wanted = B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST;
				break;
			case kTopRight:
			case kBottomLeft:
				wanted = B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST;
				break;
			case kTop:
			case kBottom:
				wanted = B_CURSOR_ID_RESIZE_NORTH_SOUTH;
				break;
			case kLeft:
			case kRight:
				wanted = B_CURSOR_ID_RESIZE_EAST_WEST;
				break;
			default:
				if (_ButtonRect(fSelection).Contains(where))
					wanted = B_CURSOR_ID_FOLLOW_LINK;
				else if (fSelection.Contains(where))
					wanted = B_CURSOR_ID_MOVE;
		}
	} else if (fState == kMoving)
		wanted = B_CURSOR_ID_GRABBING;
	else if (fMode == kCaptureWindow && fState == kIdle)
		wanted = B_CURSOR_ID_FOLLOW_LINK;

	if (wanted == fLastCursor)
		return;
	fLastCursor = wanted;
	BCursor cursor((BCursorID)wanted);
	SetViewCursor(&cursor, true);
}


BRect OverlayView::_NormalizedRect(BPoint a, BPoint b) const
{
	BRect rect(fminf(a.x, b.x), fminf(a.y, b.y), fmaxf(a.x, b.x), fmaxf(a.y, b.y));
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = floorf(rect.right);
	rect.bottom = floorf(rect.bottom);
	return rect & fScreenFrame;
}


void OverlayView::_SetSelection(BRect selection)
{
	fSelection = selection;
	_SetTarget(selection, true);
}


void OverlayView::MouseDown(BPoint where)
{
	if (fState == kConfirming)
		return;
	int32 buttons = Window()->CurrentMessage()->GetInt32("buttons", B_PRIMARY_MOUSE_BUTTON);
	OTRACE("MouseDown %g,%g buttons %d state %d", where.x, where.y, (int)buttons, (int)fState);
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		// Right click: drop the selection, or leave.
		if (fState == kSelected) {
			fState = kIdle;
			fSelectedWindow = -1;
			BRect previous = fSelection;
			fSelection = BRect(0, 0, -1, -1);
			_SetTarget(BRect(0, 0, -1, -1), true);
			_InvalidateChange(previous, previous);
			Invalidate();
		} else
			_Cancel();
		return;
	}

	fPressPoint = where;
	fMouse = where;
	bigtime_t now = system_time();
	bool doubleClick = now - fLastClick < 400000
		&& fabsf(where.x - fLastClickPoint.x) < 5 && fabsf(where.y - fLastClickPoint.y) < 5;
	fLastClick = now;
	fLastClickPoint = where;

	if (fState == kSelected) {
		if (_ButtonRect(fSelection).Contains(where)) {
			_Confirm();
			return;
		}
		fHandle = _HandleAt(where);
		fPressSelection = fSelection;
		if (fHandle != kNoHandle) {
			fState = kResizing;
			fSelectedWindow = -1;
			return;
		}
		if (fSelection.Contains(where)) {
			if (doubleClick) {
				_Confirm();
				return;
			}
			fState = kMoving;
			fSelectedWindow = -1;
			_UpdateCursor(where);
			return;
		}
	}
	fState = kPending;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS);
}


void OverlayView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	OTRACE("MouseMoved %g,%g state %d", where.x, where.y, (int)fState);
	if (fState == kConfirming)
		return;
	BPoint previousMouse = fMouse;
	fMouse = where;

	switch (fState) {
		case kIdle:
			if (fMode == kCaptureWindow) {
				int32 index = _WindowAt(where);
				if (index != fHovered) {
					fHovered = index;
					if (index >= 0) {
						_SetTarget(fWindows[index].CaptureFrame(fIncludeDecorations,
							fScreenFrame), false);
					} else
						_SetTarget(BRect(0, 0, -1, -1), true);
				}
			} else {
				// Move the crosshair: repaint the old and the new lines only.
				Invalidate(BRect(previousMouse.x, fScreenFrame.top, previousMouse.x,
					fScreenFrame.bottom));
				Invalidate(BRect(fScreenFrame.left, previousMouse.y, fScreenFrame.right,
					previousMouse.y));
				Invalidate(BRect(where.x, fScreenFrame.top, where.x, fScreenFrame.bottom));
				Invalidate(BRect(fScreenFrame.left, where.y, fScreenFrame.right, where.y));
			}
			break;

		case kPending:
			if (fabsf(where.x - fPressPoint.x) >= kDragThreshold
				|| fabsf(where.y - fPressPoint.y) >= kDragThreshold) {
				fState = kCreating;
				fHovered = -1;
				fSelectedWindow = -1;
				Invalidate();
				_SetSelection(_NormalizedRect(fPressPoint, where));
			}
			break;

		case kCreating:
			_SetSelection(_NormalizedRect(fPressPoint, where));
			break;

		case kMoving:
		{
			BRect moved = fPressSelection.OffsetByCopy(floorf(where.x - fPressPoint.x),
				floorf(where.y - fPressPoint.y));
			// Keep the whole selection on screen.
			if (moved.left < fScreenFrame.left)
				moved.OffsetBy(fScreenFrame.left - moved.left, 0);
			if (moved.top < fScreenFrame.top)
				moved.OffsetBy(0, fScreenFrame.top - moved.top);
			if (moved.right > fScreenFrame.right)
				moved.OffsetBy(fScreenFrame.right - moved.right, 0);
			if (moved.bottom > fScreenFrame.bottom)
				moved.OffsetBy(0, fScreenFrame.bottom - moved.bottom);
			_SetSelection(moved);
			break;
		}

		case kResizing:
		{
			BRect rect = fPressSelection;
			float dx = floorf(where.x - fPressPoint.x);
			float dy = floorf(where.y - fPressPoint.y);
			switch (fHandle) {
				case kTopLeft: rect.left += dx; rect.top += dy; break;
				case kTop: rect.top += dy; break;
				case kTopRight: rect.right += dx; rect.top += dy; break;
				case kRight: rect.right += dx; break;
				case kBottomRight: rect.right += dx; rect.bottom += dy; break;
				case kBottom: rect.bottom += dy; break;
				case kBottomLeft: rect.left += dx; rect.bottom += dy; break;
				case kLeft: rect.left += dx; break;
				default: break;
			}
			_SetSelection(_NormalizedRect(rect.LeftTop(), rect.RightBottom()));
			break;
		}

		default:
			break;
	}
	_UpdateCursor(where);
}


void OverlayView::MouseUp(BPoint where)
{
	switch (fState) {
		case kPending:
		{
			// A click without a drag.
			int32 index = _WindowAt(where);
			if (fMode == kCaptureWindow) {
				fState = kIdle;
				fHovered = index;
				if (index >= 0) {
					fSelectedWindow = index;
					_SetTarget(fWindows[index].CaptureFrame(fIncludeDecorations, fScreenFrame),
						true);
					_Confirm();
				}
			} else if (index >= 0) {
				fSelectedWindow = index;
				fState = kSelected;
				Invalidate();
				_SetSelection(fWindows[index].CaptureFrame(fIncludeDecorations, fScreenFrame));
			} else
				fState = kIdle;
			break;
		}

		case kCreating:
			_SetSelection(_NormalizedRect(fPressPoint, where));
			if (fSelection.Width() < 2 || fSelection.Height() < 2) {
				fState = kIdle;
				fSelection = BRect(0, 0, -1, -1);
				_SetTarget(fSelection, true);
				Invalidate();
			} else {
				fState = kSelected;
				Invalidate(_Outer(fSelection));
			}
			break;

		case kMoving:
		case kResizing:
			fState = kSelected;
			fHandle = kNoHandle;
			Invalidate(_Outer(fSelection) | _Outer(fPressSelection));
			break;

		default:
			break;
	}
	_UpdateCursor(where);
}


void OverlayView::KeyDown(const char* bytes, int32 numBytes)
{
	OTRACE("KeyDown 0x%02x state %d", numBytes > 0 ? (unsigned char)bytes[0] : 0, (int)fState);
	if (numBytes < 1 || fState == kConfirming)
		return;
	uint32 modifiers = Window()->CurrentMessage()->GetInt32("modifiers", 0);
	float step = (modifiers & B_SHIFT_KEY) != 0 ? 10 : 1;
	switch (bytes[0]) {
		case B_ESCAPE:
			_Cancel();
			return;
		case B_ENTER:
		case B_SPACE:
			if (fState == kSelected || (fMode == kCaptureWindow && fState == kIdle
					&& fHovered >= 0)) {
				if (fState == kIdle)
					fSelectedWindow = fHovered;
				_Confirm();
			}
			return;
		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
			if (fState == kSelected) {
				BPoint delta(0, 0);
				if (bytes[0] == B_LEFT_ARROW) delta.x = -step;
				if (bytes[0] == B_RIGHT_ARROW) delta.x = step;
				if (bytes[0] == B_UP_ARROW) delta.y = -step;
				if (bytes[0] == B_DOWN_ARROW) delta.y = step;
				BRect moved = fSelection.OffsetByCopy(delta) & fScreenFrame;
				if (moved.IsValid() && moved.Width() == fSelection.Width()
					&& moved.Height() == fSelection.Height()) {
					fSelectedWindow = -1;
					_SetSelection(moved);
				}
			}
			return;
	}
	BView::KeyDown(bytes, numBytes);
}


void OverlayView::_Confirm()
{
	if (!fShown.IsValid())
		return;
	fShown = fTarget;
	fState = kConfirming;
	fFlash = fAnimate ? 1.0f : 0.0f;
	Invalidate();
	if (!fAnimate)
		_SendResult();
}


void OverlayView::_SendResult()
{
	BMessage result(kMsgOverlayDone);
	result.AddRect("frame", fShown);
	result.AddInt32("kind", fSelectedWindow >= 0 ? kCaptureWindow : kCaptureRegion);
	if (fSelectedWindow >= 0 && fSelectedWindow < (int32)fWindows.size()) {
		const WindowEntry& entry = fWindows[fSelectedWindow];
		result.AddInt32("window_team", entry.team);
		result.AddInt32("window_token", entry.token);
		result.AddString("window_name", entry.name);
		result.AddRect("window_frame", entry.frame);
		result.AddFloat("window_tab_height", entry.tabHeight);
		result.AddFloat("window_border", entry.borderSize);
		result.AddBool("window_desktop", entry.desktop);
	}
	static_cast<OverlayWindow*>(Window())->Finish(&result);
}


void OverlayView::_Cancel()
{
	OTRACE("cancel");
	BMessage cancelled(kMsgOverlayCancelled);
	static_cast<OverlayWindow*>(Window())->Finish(&cancelled);
}

}  // namespace airshot
