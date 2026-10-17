// The editor's canvas: shows the screenshot at a zoom level, runs the
// annotation tools, and keeps the undo history.
#pragma once
#include <Bitmap.h>
#include <Cursor.h>
#include <TextView.h>
#include <View.h>

#include <memory>
#include <vector>

#include "Annotation.h"
#include "ToolIcons.h"

namespace airshot {

// Sent to the window: "x"/"y" (image coordinates, -1 when outside).
constexpr uint32 kMsgCanvasStatus = 'CvSt';
// Sent to the window after anything changed (undo state, selection, zoom).
constexpr uint32 kMsgCanvasChanged = 'CvCh';
// Sent to the window when a single-key tool shortcut was pressed: "tool".
constexpr uint32 kMsgCanvasTool = 'CvTl';
// Text editing finished (from the inline text view).
constexpr uint32 kMsgTextCommit = 'TxCm';
constexpr uint32 kMsgTextCancel = 'TxCn';

struct Document {
	std::shared_ptr<BBitmap> base;
	std::vector<AnnotationRef> items;
	int32 nextCounter = 1;

	Document Clone() const;
};


class InlineTextView : public BTextView {
public:
								InlineTextView(BRect frame, BRect textRect);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus);
};


class CanvasView : public BView {
public:
	// pixelScale: image pixels per screen coordinate unit, more than 1 for
	// a capture in the pixels of a scaled screen. The style's sizes are in
	// screen units and become that many image pixels.
								CanvasView(BBitmap* bitmap, const Style& style,
									float pixelScale = 1);
	virtual						~CanvasView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit, const BMessage* drag);
	virtual	void				MouseUp(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MessageReceived(BMessage* message);

			void				SetTool(Tool tool);
			Tool				CurrentTool() const { return fTool; }
			void				SetStyle(const Style& style);
			const Style&		CurrentStyle() const { return fStyle; }

			float				Zoom() const { return fZoom; }
			void				SetZoom(float zoom, BPoint anchor = BPoint(-1, -1));
			void				ZoomToFit();
			float				FitZoom() const;
			float				PixelScale() const { return fPixelScale; }
			// The zoom at which an image pixel is a screen pixel.
			float				ActualZoom() const { return 1 / fPixelScale; }

			bool				CanUndo() const { return !fUndo.empty(); }
			bool				CanRedo() const { return !fRedo.empty(); }
			void				Undo();
			void				Redo();
			bool				HasSelection() const { return fSelected >= 0; }
			void				DeleteSelection();
			void				SelectAll() {}
			bool				IsDirty() const { return fDirty; }
			void				SetClean() { fDirty = false; }
			int32				CountAnnotations() const { return fDocument.items.size(); }

			BBitmap*			Flatten() const;
			const BBitmap*		Base() const { return fDocument.base.get(); }
			BRect				ImageBounds() const;

			void				CommitText();
			void				CancelText();

private:
	enum DragMode {
		kDragNone,
		kDragCreate,
		kDragMove,
		kDragHandle,
		kDragCrop,
	};

			BPoint				_ToImage(BPoint viewPoint) const;
			BPoint				_ToView(BPoint imagePoint) const;
			BRect				_ToView(BRect imageRect) const;
			void				_UpdateLayout();
			void				_UpdateScrollBars();
			void				_Snapshot();
			void				_Changed();
			void				_InvalidateItem(const AnnotationRef& item);
			void				_InvalidateSelection();
			int32				_ItemAt(BPoint imagePoint) const;
			int32				_HandleAt(BPoint viewPoint) const;
			void				_Select(int32 index);
			void				_Crop(BRect rect);
			void				_BeginText(BPoint imagePoint);
			void				_DrawSelection();
			void				_UpdateCursor(BPoint viewPoint);
			void				_SendStatus(BPoint imagePoint);
			Style				_ImageStyle() const;

			Document			fDocument;
			std::vector<Document> fUndo;
			std::vector<Document> fRedo;
			Tool				fTool;
			Style				fStyle;
			float				fPixelScale;
			float				fZoom;
			BPoint				fOffset;   // where the image's origin is, in view coordinates
			int32				fSelected;
			bool				fDirty;

			DragMode			fDrag;
			BPoint				fPressImage;
			BPoint				fLastImage;
			int32				fHandle;
			bool				fMovedSinceSnapshot;
			AnnotationRef		fDrawing;
			BRect				fCropRect;

			InlineTextView*		fTextView;
			BPoint				fTextPosition;
			int32				fLastCursor;
};

}  // namespace airshot
