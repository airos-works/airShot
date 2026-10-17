#include "CanvasView.h"

#include <Message.h>
#include <Region.h>
#include <ScrollBar.h>
#include <Window.h>

#include <ctype.h>
#include <math.h>

#include "Export.h"
#include "capture/ScreenCapture.h"

namespace airshot {

namespace {

constexpr float kHandleRadius = 5;
constexpr float kHandleHit = 8;
constexpr size_t kUndoLimit = 60;
constexpr float kMinZoom = 0.1f;
constexpr float kMaxZoom = 8.0f;
const rgb_color kBackground = {96, 100, 106, 255};
const rgb_color kSelectionColor = {30, 140, 255, 255};

}  // namespace


Document Document::Clone() const
{
	Document copy;
	copy.base = base;
	copy.nextCounter = nextCounter;
	for (const AnnotationRef& item : items)
		copy.items.push_back(AnnotationRef(item->Clone()));
	return copy;
}


//	#pragma mark - InlineTextView


InlineTextView::InlineTextView(BRect frame, BRect textRect)
	:
	BTextView(frame, "inline text", textRect, B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE)
{
	SetWordWrap(false);
	SetStylable(false);
	SetViewColor(255, 255, 255);
}


void InlineTextView::KeyDown(const char* bytes, int32 numBytes)
{
	uint32 modifiers = Window()->CurrentMessage()->GetInt32("modifiers", 0);
	if (numBytes == 1 && bytes[0] == B_ESCAPE) {
		Window()->PostMessage(kMsgTextCancel, Parent());
		return;
	}
	if (numBytes == 1 && bytes[0] == B_ENTER && (modifiers & B_SHIFT_KEY) == 0) {
		Window()->PostMessage(kMsgTextCommit, Parent());
		return;
	}
	BTextView::KeyDown(bytes, numBytes);
	// Grow with the text so nothing is clipped while typing.
	float width = 0;
	for (int32 line = 0; line < CountLines(); line++)
		width = fmaxf(width, LineWidth(line));
	float height = TextHeight(0, CountLines() - 1);
	BRect frame = Frame();
	float wanted = fmaxf(frame.Width(), width + 24);
	float wantedHeight = fmaxf(frame.Height(), height + 8);
	if (wanted > frame.Width() || wantedHeight > frame.Height()) {
		ResizeTo(wanted, wantedHeight);
		SetTextRect(BRect(4, 2, wanted - 4, wantedHeight - 2));
	}
}


void InlineTextView::MakeFocus(bool focus)
{
	BTextView::MakeFocus(focus);
}


//	#pragma mark - CanvasView


CanvasView::CanvasView(BBitmap* bitmap, const Style& style, float pixelScale)
	:
	BView("canvas", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE),
	fTool(kToolSelect),
	fStyle(style),
	fPixelScale(pixelScale > 0 ? pixelScale : 1),
	fZoom(1),
	fOffset(0, 0),
	fSelected(-1),
	fDirty(false),
	fDrag(kDragNone),
	fHandle(-1),
	fMovedSinceSnapshot(false),
	fCropRect(0, 0, -1, -1),
	fTextView(NULL),
	fLastCursor(-1)
{
	fDocument.base.reset(bitmap);
	SetViewColor(B_TRANSPARENT_COLOR);
}


CanvasView::~CanvasView()
{
}


void CanvasView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_UpdateLayout();
	MakeFocus(true);
}


BRect CanvasView::ImageBounds() const
{
	return fDocument.base->Bounds();
}


//	#pragma mark - coordinates and layout


BPoint CanvasView::_ToImage(BPoint viewPoint) const
{
	return BPoint((viewPoint.x - fOffset.x) / fZoom, (viewPoint.y - fOffset.y) / fZoom);
}


BPoint CanvasView::_ToView(BPoint imagePoint) const
{
	return BPoint(imagePoint.x * fZoom + fOffset.x, imagePoint.y * fZoom + fOffset.y);
}


BRect CanvasView::_ToView(BRect imageRect) const
{
	return BRect(_ToView(imageRect.LeftTop()), _ToView(imageRect.RightBottom()));
}


void CanvasView::_UpdateLayout()
{
	BRect bounds = Bounds();
	float width = (ImageBounds().Width() + 1) * fZoom;
	float height = (ImageBounds().Height() + 1) * fZoom;
	// Centre a small image, else start at the top-left and scroll.
	fOffset.x = width < bounds.Width() + 1 ? floorf((bounds.Width() + 1 - width) / 2) : 0;
	fOffset.y = height < bounds.Height() + 1 ? floorf((bounds.Height() + 1 - height) / 2) : 0;
	_UpdateScrollBars();
	Invalidate();
}


void CanvasView::_UpdateScrollBars()
{
	BRect bounds = Bounds();
	float width = (ImageBounds().Width() + 1) * fZoom;
	float height = (ImageBounds().Height() + 1) * fZoom;
	BScrollBar* horizontal = ScrollBar(B_HORIZONTAL);
	if (horizontal != NULL) {
		float range = fmaxf(0, width - (bounds.Width() + 1));
		horizontal->SetRange(0, range);
		horizontal->SetProportion(range > 0 ? (bounds.Width() + 1) / width : 1);
		horizontal->SetSteps(20, bounds.Width());
	}
	BScrollBar* vertical = ScrollBar(B_VERTICAL);
	if (vertical != NULL) {
		float range = fmaxf(0, height - (bounds.Height() + 1));
		vertical->SetRange(0, range);
		vertical->SetProportion(range > 0 ? (bounds.Height() + 1) / height : 1);
		vertical->SetSteps(20, bounds.Height());
	}
}


void CanvasView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_UpdateLayout();
}


float CanvasView::FitZoom() const
{
	BRect bounds = Bounds();
	float zoomX = (bounds.Width() + 1) / (ImageBounds().Width() + 1);
	float zoomY = (bounds.Height() + 1) / (ImageBounds().Height() + 1);
	return fmaxf(kMinZoom, fminf(ActualZoom(), fminf(zoomX, zoomY)));
}


void CanvasView::ZoomToFit()
{
	SetZoom(FitZoom());
}


void CanvasView::SetZoom(float zoom, BPoint anchor)
{
	zoom = fmaxf(kMinZoom, fminf(kMaxZoom, zoom));
	if (zoom == fZoom)
		return;
	CommitText();
	// Keep the image point under the anchor (or the centre) where it is.
	BRect bounds = Bounds();
	if (anchor.x < 0)
		anchor = BPoint(bounds.left + bounds.Width() / 2, bounds.top + bounds.Height() / 2);
	BPoint imagePoint = _ToImage(anchor);
	fZoom = zoom;
	_UpdateLayout();
	BPoint wanted = _ToView(imagePoint);
	BPoint scroll(bounds.left + wanted.x - anchor.x, bounds.top + wanted.y - anchor.y);
	float width = (ImageBounds().Width() + 1) * fZoom;
	float height = (ImageBounds().Height() + 1) * fZoom;
	scroll.x = fmaxf(0, fminf(scroll.x, width - (bounds.Width() + 1)));
	scroll.y = fmaxf(0, fminf(scroll.y, height - (bounds.Height() + 1)));
	ScrollTo(scroll);
	_Changed();
}


//	#pragma mark - drawing


void CanvasView::Draw(BRect updateRect)
{
	BRect image = _ToView(ImageBounds());
	image.right = ceilf(image.right);
	image.bottom = ceilf(image.bottom);

	// Background around the image only, to avoid painting the image twice.
	SetDrawingMode(B_OP_COPY);
	SetHighColor(kBackground);
	BRegion outside(updateRect);
	outside.Exclude(image);
	for (int32 i = 0; i < outside.CountRects(); i++)
		FillRect(outside.RectAt(i));

	BRect imageUpdate = updateRect & image;
	if (imageUpdate.IsValid()) {
		BRect source(_ToImage(imageUpdate.LeftTop()), _ToImage(imageUpdate.RightBottom()));
		source.left = floorf(source.left);
		source.top = floorf(source.top);
		source.right = ceilf(source.right);
		source.bottom = ceilf(source.bottom);
		source = source & ImageBounds();
		const BBitmap* base = fDocument.base.get();
		// At actual size a capture in screen pixels goes back to the screen
		// one to one, without the scaling that would soften it.
		bool screenPixels = fPixelScale != 1 && fabsf(fZoom * fPixelScale - 1) < 0.001f;
		if (screenPixels) {
			if (base->ColorSpace() == B_RGBA32) {
				SetHighColor(255, 255, 255);
				FillRect(_ToView(source));
				SetDrawingMode(B_OP_ALPHA);
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			}
			DrawBitmap(base, source, _ToView(source), kDevicePixelBitmap);
			SetDrawingMode(B_OP_COPY);
		}
		PushState();
		SetOrigin(fOffset);
		SetScale(fZoom);
		if (!screenPixels) {
			if (base->ColorSpace() == B_RGBA32) {
				SetHighColor(255, 255, 255);
				FillRect(source);
				SetDrawingMode(B_OP_ALPHA);
				SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			}
			DrawBitmap(base, source, source,
				fZoom * fPixelScale < 1 ? B_FILTER_BITMAP_BILINEAR : 0);
		}
		for (const AnnotationRef& item : fDocument.items)
			item->Draw(this, base);
		if (fDrawing.get() != NULL)
			fDrawing->Draw(this, base);
		PopState();
	}

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	if (fDrag == kDragCrop && fCropRect.IsValid()) {
		BRect crop = _ToView(fCropRect);
		SetHighColor(0, 0, 0, 90);
		BRegion dim(image);
		dim.Exclude(crop);
		for (int32 i = 0; i < dim.CountRects(); i++)
			FillRect(dim.RectAt(i) & updateRect);
		SetHighColor(255, 255, 255, 255);
		SetPenSize(1);
		StrokeRect(crop, B_MIXED_COLORS);
	}
	_DrawSelection();
	SetDrawingMode(B_OP_COPY);
}


void CanvasView::_DrawSelection()
{
	if (fSelected < 0 || fSelected >= (int32)fDocument.items.size())
		return;
	const AnnotationRef& item = fDocument.items[fSelected];
	BRect bounds = _ToView(item->Bounds());
	SetPenSize(1);
	SetHighColor(255, 255, 255, 200);
	StrokeRect(bounds);
	SetHighColor(kSelectionColor.red, kSelectionColor.green, kSelectionColor.blue, 200);
	StrokeRect(bounds, B_MIXED_COLORS);
	for (int32 i = 0; i < item->CountHandles(); i++) {
		BPoint handle = _ToView(item->HandleAt(i));
		SetHighColor(kSelectionColor);
		FillEllipse(handle, kHandleRadius, kHandleRadius);
		SetHighColor(255, 255, 255, 255);
		StrokeEllipse(handle, kHandleRadius, kHandleRadius);
	}
}


void CanvasView::_InvalidateItem(const AnnotationRef& item)
{
	if (item.get() == NULL)
		return;
	Invalidate(_ToView(item->Bounds()).InsetByCopy(-kHandleRadius - 2, -kHandleRadius - 2));
}


void CanvasView::_InvalidateSelection()
{
	if (fSelected >= 0 && fSelected < (int32)fDocument.items.size())
		_InvalidateItem(fDocument.items[fSelected]);
}


//	#pragma mark - state


void CanvasView::_Snapshot()
{
	fUndo.push_back(fDocument.Clone());
	if (fUndo.size() > kUndoLimit)
		fUndo.erase(fUndo.begin());
	fRedo.clear();
	fDirty = true;
}


void CanvasView::_Changed()
{
	if (Window() != NULL)
		Window()->PostMessage(kMsgCanvasChanged, Window());
}


void CanvasView::Undo()
{
	CommitText();
	if (fUndo.empty())
		return;
	fRedo.push_back(fDocument.Clone());
	fDocument = fUndo.back();
	fUndo.pop_back();
	fSelected = -1;
	fDirty = true;
	_UpdateLayout();
	_Changed();
}


void CanvasView::Redo()
{
	CommitText();
	if (fRedo.empty())
		return;
	fUndo.push_back(fDocument.Clone());
	fDocument = fRedo.back();
	fRedo.pop_back();
	fSelected = -1;
	fDirty = true;
	_UpdateLayout();
	_Changed();
}


void CanvasView::DeleteSelection()
{
	if (fSelected < 0 || fSelected >= (int32)fDocument.items.size())
		return;
	_Snapshot();
	AnnotationRef item = fDocument.items[fSelected];
	fDocument.items.erase(fDocument.items.begin() + fSelected);
	fSelected = -1;
	_InvalidateItem(item);
	_Changed();
}


void CanvasView::_Select(int32 index)
{
	if (index == fSelected)
		return;
	_InvalidateSelection();
	fSelected = index;
	_InvalidateSelection();
	_Changed();
}


void CanvasView::SetTool(Tool tool)
{
	if (tool == fTool)
		return;
	CommitText();
	fTool = tool;
	if (tool != kToolSelect)
		_Select(-1);
	_Changed();
}


Style CanvasView::_ImageStyle() const
{
	Style style = fStyle;
	style.strokeWidth *= fPixelScale;
	style.fontSize *= fPixelScale;
	return style;
}


void CanvasView::SetStyle(const Style& style)
{
	fStyle = style;
	if (fSelected >= 0 && fSelected < (int32)fDocument.items.size()) {
		// Restyle the selected annotation, keeping its own fill/pixelate.
		AnnotationRef item = fDocument.items[fSelected];
		Style current = item->GetStyle();
		Style wanted = _ImageStyle();
		if (current.color != wanted.color || current.strokeWidth != wanted.strokeWidth
			|| current.fontSize != wanted.fontSize || current.filled != wanted.filled
			|| current.pixelate != wanted.pixelate) {
			_Snapshot();
			_InvalidateItem(item);
			fDocument.items[fSelected]->SetStyle(wanted);
			_InvalidateItem(fDocument.items[fSelected]);
			_Changed();
		}
	}
	if (fTextView != NULL) {
		BFont font;
		TextAnnotation::PrepareFont(font, _ImageStyle().fontSize * fZoom);
		fTextView->SetFontAndColor(&font, B_FONT_ALL, &fStyle.color);
	}
}


BBitmap* CanvasView::Flatten() const
{
	return Export::Flatten(fDocument.base.get(), fDocument.items);
}


void CanvasView::_Crop(BRect rect)
{
	rect = rect & ImageBounds();
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = floorf(rect.right);
	rect.bottom = floorf(rect.bottom);
	if (!rect.IsValid() || rect.Width() < 2 || rect.Height() < 2)
		return;
	BBitmap* cropped = new BBitmap(rect.OffsetToCopy(B_ORIGIN), fDocument.base->ColorSpace());
	if (cropped->InitCheck() != B_OK
		|| cropped->ImportBits(fDocument.base.get(), rect.LeftTop(), B_ORIGIN, rect.Size())
			!= B_OK) {
		delete cropped;
		return;
	}
	_Snapshot();
	fDocument.base.reset(cropped);
	BPoint delta(-rect.left, -rect.top);
	for (AnnotationRef& item : fDocument.items) {
		item->MoveBy(delta);
		item->InvalidateCache();
	}
	fSelected = -1;
	_UpdateLayout();
	_Changed();
}


//	#pragma mark - text


void CanvasView::_BeginText(BPoint imagePoint)
{
	CommitText();
	fTextPosition = imagePoint;
	BFont font;
	float fontSize = _ImageStyle().fontSize * fZoom;
	TextAnnotation::PrepareFont(font, fontSize);
	font_height fh;
	font.GetHeight(&fh);
	float height = ceilf(fh.ascent + fh.descent + fh.leading) + 6;
	BPoint origin = _ToView(imagePoint);
	// The text starts where the user clicked; the box hangs off it.
	BRect frame(origin.x - 4, origin.y - 2, origin.x + fmaxf(160, 14 * fontSize),
		origin.y + height);
	fTextView = new InlineTextView(frame, BRect(4, 2, frame.Width() - 4, frame.Height() - 2));
	fTextView->SetFontAndColor(&font, B_FONT_ALL, &fStyle.color);
	AddChild(fTextView);
	fTextView->MakeFocus(true);
}


void CanvasView::CommitText()
{
	if (fTextView == NULL)
		return;
	BString text(fTextView->Text());
	fTextView->RemoveSelf();
	delete fTextView;
	fTextView = NULL;
	MakeFocus(true);
	TextAnnotation* annotation = new TextAnnotation(_ImageStyle());
	annotation->SetPosition(fTextPosition);
	annotation->SetText(text.String());
	if (!annotation->IsUsable()) {
		delete annotation;
		return;
	}
	_Snapshot();
	fDocument.items.push_back(AnnotationRef(annotation));
	_InvalidateItem(fDocument.items.back());
	_Changed();
}


void CanvasView::CancelText()
{
	if (fTextView == NULL)
		return;
	fTextView->RemoveSelf();
	delete fTextView;
	fTextView = NULL;
	MakeFocus(true);
}


//	#pragma mark - mouse


int32 CanvasView::_ItemAt(BPoint imagePoint) const
{
	for (int32 i = (int32)fDocument.items.size() - 1; i >= 0; i--) {
		if (fDocument.items[i]->HitTest(imagePoint))
			return i;
	}
	return -1;
}


int32 CanvasView::_HandleAt(BPoint viewPoint) const
{
	if (fSelected < 0 || fSelected >= (int32)fDocument.items.size())
		return -1;
	const AnnotationRef& item = fDocument.items[fSelected];
	for (int32 i = 0; i < item->CountHandles(); i++) {
		BPoint handle = _ToView(item->HandleAt(i));
		if (fabsf(handle.x - viewPoint.x) <= kHandleHit && fabsf(handle.y - viewPoint.y) <= kHandleHit)
			return i;
	}
	return -1;
}


void CanvasView::_UpdateCursor(BPoint viewPoint)
{
	int32 wanted = B_CURSOR_ID_CROSS_HAIR;
	if (fTool == kToolSelect) {
		wanted = B_CURSOR_ID_SYSTEM_DEFAULT;
		if (fDrag == kDragMove)
			wanted = B_CURSOR_ID_GRABBING;
		else if (_HandleAt(viewPoint) >= 0)
			wanted = B_CURSOR_ID_GRAB;
		else if (_ItemAt(_ToImage(viewPoint)) >= 0)
			wanted = B_CURSOR_ID_GRAB;
	} else if (fTool == kToolText)
		wanted = B_CURSOR_ID_I_BEAM;
	if (!_ToView(ImageBounds()).Contains(viewPoint) && fDrag == kDragNone)
		wanted = B_CURSOR_ID_SYSTEM_DEFAULT;
	if (wanted == fLastCursor)
		return;
	fLastCursor = wanted;
	BCursor cursor((BCursorID)wanted);
	SetViewCursor(&cursor, true);
}


void CanvasView::_SendStatus(BPoint imagePoint)
{
	if (Window() == NULL)
		return;
	BMessage status(kMsgCanvasStatus);
	if (ImageBounds().Contains(imagePoint)) {
		status.AddInt32("x", (int32)floorf(imagePoint.x));
		status.AddInt32("y", (int32)floorf(imagePoint.y));
	} else {
		status.AddInt32("x", -1);
		status.AddInt32("y", -1);
	}
	Window()->PostMessage(&status, Window());
}


void CanvasView::MouseDown(BPoint where)
{
	MakeFocus(true);
	int32 buttons = Window()->CurrentMessage()->GetInt32("buttons", B_PRIMARY_MOUSE_BUTTON);
	bool wasEditing = fTextView != NULL;
	CommitText();
	if ((buttons & B_PRIMARY_MOUSE_BUTTON) == 0)
		return;
	if (wasEditing && fTool == kToolText) {
		// The click only ended the previous text.
		return;
	}
	BPoint image = _ToImage(where);
	fPressImage = image;
	fLastImage = image;
	fMovedSinceSnapshot = false;
	SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS);

	switch (fTool) {
		case kToolSelect:
		{
			int32 handle = _HandleAt(where);
			if (handle >= 0) {
				fHandle = handle;
				fDrag = kDragHandle;
				return;
			}
			int32 index = _ItemAt(image);
			_Select(index);
			fDrag = index >= 0 ? kDragMove : kDragNone;
			break;
		}
		case kToolText:
			if (ImageBounds().Contains(image))
				_BeginText(image);
			break;
		case kToolCounter:
		{
			if (!ImageBounds().Contains(image))
				break;
			_Snapshot();
			fDocument.items.push_back(AnnotationRef(new CounterAnnotation(_ImageStyle(), image,
				fDocument.nextCounter++)));
			_InvalidateItem(fDocument.items.back());
			_Changed();
			break;
		}
		case kToolCrop:
			fDrag = kDragCrop;
			fCropRect = BRect(image, image);
			break;
		default:
		{
			AnnotationType type = kAnnotationArrow;
			switch (fTool) {
				case kToolArrow: type = kAnnotationArrow; break;
				case kToolLine: type = kAnnotationLine; break;
				case kToolRectangle: type = kAnnotationRectangle; break;
				case kToolEllipse: type = kAnnotationEllipse; break;
				case kToolPen: type = kAnnotationPen; break;
				case kToolHighlighter: type = kAnnotationHighlighter; break;
				case kToolBlur: type = kAnnotationBlur; break;
				default: break;
			}
			fDrawing.reset(Annotation::Create(type, _ImageStyle()));
			fDrawing->SetEndPoints(image, image);
			fDrawing->AddPoint(image);
			fDrag = kDragCreate;
			break;
		}
	}
	_UpdateCursor(where);
}


void CanvasView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag)
{
	BPoint image = _ToImage(where);
	_SendStatus(image);
	switch (fDrag) {
		case kDragCreate:
			if (fDrawing.get() != NULL) {
				_InvalidateItem(fDrawing);
				fDrawing->SetEndPoints(fPressImage, image);
				fDrawing->AddPoint(image);
				_InvalidateItem(fDrawing);
			}
			break;
		case kDragMove:
			if (fSelected >= 0 && fSelected < (int32)fDocument.items.size()) {
				if (!fMovedSinceSnapshot) {
					_Snapshot();
					fMovedSinceSnapshot = true;
				}
				AnnotationRef item = fDocument.items[fSelected];
				_InvalidateItem(item);
				item->MoveBy(image - fLastImage);
				_InvalidateItem(item);
			}
			break;
		case kDragHandle:
			if (fSelected >= 0 && fSelected < (int32)fDocument.items.size()) {
				if (!fMovedSinceSnapshot) {
					_Snapshot();
					fMovedSinceSnapshot = true;
				}
				AnnotationRef item = fDocument.items[fSelected];
				_InvalidateItem(item);
				item->MoveHandleTo(fHandle, image);
				_InvalidateItem(item);
			}
			break;
		case kDragCrop:
		{
			fCropRect = BRect(fminf(fPressImage.x, image.x), fminf(fPressImage.y, image.y),
				fmaxf(fPressImage.x, image.x), fmaxf(fPressImage.y, image.y)) & ImageBounds();
			Invalidate(_ToView(ImageBounds()));
			break;
		}
		default:
			break;
	}
	fLastImage = image;
	_UpdateCursor(where);
}


void CanvasView::MouseUp(BPoint where)
{
	BPoint image = _ToImage(where);
	switch (fDrag) {
		case kDragCreate:
			if (fDrawing.get() != NULL) {
				_InvalidateItem(fDrawing);
				if (fDrawing->IsUsable()) {
					_Snapshot();
					fDocument.items.push_back(fDrawing);
				}
				fDrawing.reset();
				_Changed();
			}
			break;
		case kDragCrop:
			fDrag = kDragNone;
			if (fCropRect.IsValid() && fCropRect.Width() >= 2 && fCropRect.Height() >= 2)
				_Crop(fCropRect);
			else
				Invalidate();
			fCropRect = BRect(0, 0, -1, -1);
			break;
		case kDragMove:
		case kDragHandle:
			if (fMovedSinceSnapshot)
				_Changed();
			break;
		default:
			break;
	}
	fDrag = kDragNone;
	fHandle = -1;
	(void)image;
	_UpdateCursor(where);
}


//	#pragma mark - keyboard


void CanvasView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes < 1)
		return;
	uint32 modifiers = Window()->CurrentMessage()->GetInt32("modifiers", 0);
	if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) != 0) {
		BView::KeyDown(bytes, numBytes);
		return;
	}
	switch (bytes[0]) {
		case B_DELETE:
		case B_BACKSPACE:
			DeleteSelection();
			return;
		case B_ESCAPE:
			if (fDrawing.get() != NULL) {
				_InvalidateItem(fDrawing);
				fDrawing.reset();
				fDrag = kDragNone;
			} else
				_Select(-1);
			return;
		case B_LEFT_ARROW:
		case B_RIGHT_ARROW:
		case B_UP_ARROW:
		case B_DOWN_ARROW:
			if (fSelected >= 0 && fSelected < (int32)fDocument.items.size()) {
				float step = (modifiers & B_SHIFT_KEY) != 0 ? 10 : 1;
				BPoint delta(0, 0);
				if (bytes[0] == B_LEFT_ARROW) delta.x = -step;
				if (bytes[0] == B_RIGHT_ARROW) delta.x = step;
				if (bytes[0] == B_UP_ARROW) delta.y = -step;
				if (bytes[0] == B_DOWN_ARROW) delta.y = step;
				_Snapshot();
				AnnotationRef item = fDocument.items[fSelected];
				_InvalidateItem(item);
				item->MoveBy(delta);
				_InvalidateItem(item);
				_Changed();
				return;
			}
			break;
	}
	// Single letter tool shortcuts.
	char key = toupper(bytes[0]);
	for (int32 i = 0; i < kToolCount; i++) {
		if (ToolShortcut((Tool)i) == key) {
			BMessage message(kMsgCanvasTool);
			message.AddInt32("tool", i);
			Window()->PostMessage(&message, Window());
			return;
		}
	}
	BView::KeyDown(bytes, numBytes);
}


void CanvasView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgTextCommit:
			CommitText();
			return;
		case kMsgTextCancel:
			CancelText();
			return;
		case B_MOUSE_WHEEL_CHANGED:
		{
			uint32 modifiers = ::modifiers();
			if ((modifiers & (B_COMMAND_KEY | B_CONTROL_KEY)) != 0) {
				float delta = message->GetFloat("be:wheel_delta_y", 0);
				if (delta != 0) {
					BPoint where;
					uint32 buttons;
					GetMouse(&where, &buttons, false);
					SetZoom(fZoom * (delta < 0 ? 1.25f : 0.8f), where);
				}
				return;
			}
			break;
		}
	}
	BView::MessageReceived(message);
}

}  // namespace airshot
