#include "ScreenCapture.h"

#include <Application.h>
#include <Autolock.h>
#include <DirectWindow.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <Region.h>
#include <Roster.h>
#include <Screen.h>
#include <View.h>
#include <WindowInfo.h>
#include <WindowPrivate.h>

#include <dlfcn.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace airshot {

namespace {

// B_DIRECT_DEVICE_PIXELS (air/OS): a direct window that stays connected on
// a scaled desktop and is told about the frame buffer in its own pixels.
// Older headers do not have it, and it is an enumerator, hence the value.
constexpr uint32 kDirectDevicePixels = 0x00000400;

// What air/OS adds to direct_buffer_info: the density in percent, its own
// copy of the screen and that copy's row size. Older headers have them as
// the first three reserved words, newer ones name them; in both they are the
// same distance before clip_list_count (airTime reads them the same way).
inline const uint32* DensityWords(const direct_buffer_info* info)
{
	return &info->clip_list_count - 10;
}


// Reads the screen in frame buffer pixels: app_server tells a direct window
// with B_DIRECT_DEVICE_PIXELS where its copy of the whole desktop is. The
// window is one pixel off the screen and avoids the focus, so showing it
// changes nothing that is captured; it only has to be shown to be connected.
class ScreenPixelReader : public BDirectWindow {
public:
	ScreenPixelReader()
		:
		BDirectWindow(BRect(-100, -100, -100, -100), "airShot screen reader",
			B_NO_BORDER_WINDOW_LOOK, B_FLOATING_ALL_WINDOW_FEEL,
			B_AVOID_FOCUS | B_NOT_MOVABLE | B_NOT_RESIZABLE | B_NOT_CLOSABLE | B_NOT_ZOOMABLE
				| B_NOT_MINIMIZABLE | kDirectDevicePixels),
		fConnected(false),
		fArea(-1),
		fBytesPerRow(0),
		fScale(0),
		fFormat(B_NO_COLOR_SPACE),
		fDrawingBuffer(false)
	{
		fReady = create_sem(0, "airShot screen reader");
	}

	virtual ~ScreenPixelReader()
	{
		delete_sem(fReady);
	}

	virtual void DirectConnected(direct_buffer_info* info)
	{
		BAutolock _(fLock);
		if ((info->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_STOP)
			fConnected = false;
		else {
			const uint32* words = DensityWords(info);
			fScale = words[0];
			fArea = (area_id)words[1];
			fBytesPerRow = (int32)words[2];
			// Without a copy of its own, app_server draws straight into the
			// frame buffer.
			fDrawingBuffer = fArea >= 0;
			if (!fDrawingBuffer) {
				fArea = info->bits_area;
				fBytesPerRow = info->bytes_per_row;
			}
			fFormat = info->pixel_format;
			fConnected = true;
		}
		release_sem(fReady);
	}

	bool WaitConnected()
	{
		acquire_sem_etc(fReady, 1, B_RELATIVE_TIMEOUT, 500000);
		BAutolock _(fLock);
		return fConnected;
	}

	// Whether the pixels are app_server's own copy of the desktop, which
	// never has the mouse pointer in it.
	bool ReadsDrawingBuffer()
	{
		BAutolock _(fLock);
		return fDrawingBuffer;
	}

	// The screen's pixels, opaque, or NULL when the desktop is not scaled or
	// they cannot be read. The lock keeps app_server from disconnecting the
	// window while they are copied.
	BBitmap* Grab(BRect frame, const display_mode& mode, float& scale)
	{
		BAutolock _(fLock);
		if (!fConnected || fScale <= 100 || fArea < 0 || fBytesPerRow <= 0
			|| (fFormat != B_RGB32 && fFormat != B_RGBA32))
			return NULL;
		const uint8* bits = NULL;
		area_id clone = clone_area("airShot screen pixels", (void**)&bits, B_ANY_ADDRESS,
			B_READ_AREA, fArea);
		if (clone < 0)
			return NULL;
		area_info info;
		if (get_area_info(clone, &info) != B_OK) {
			delete_area(clone);
			return NULL;
		}
		// The buffer is the logical size times the scale, which need not
		// be a whole number of pixels: the mode has the real size.
		float readScale = fScale / 100.0f;
		int32 width = (int32)ceilf((frame.Width() + 1) * readScale);
		int32 height = (int32)ceilf((frame.Height() + 1) * readScale);
		if (mode.virtual_width > frame.Width() && mode.virtual_height > frame.Height()) {
			width = std::min(width, (int32)mode.virtual_width);
			height = std::min(height, (int32)mode.virtual_height);
		}
		width = std::min(width, fBytesPerRow / 4);
		height = std::min(height, (int32)(info.size / fBytesPerRow));
		BBitmap* bitmap = NULL;
		if (width > 0 && height > 0)
			bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (bitmap != NULL && bitmap->InitCheck() == B_OK) {
			uint8* destination = (uint8*)bitmap->Bits();
			int32 destinationBytesPerRow = bitmap->BytesPerRow();
			for (int32 y = 0; y < height; y++) {
				const uint32* from = (const uint32*)(bits + y * fBytesPerRow);
				uint32* to = (uint32*)(destination + y * destinationBytesPerRow);
				for (int32 x = 0; x < width; x++)
					to[x] = from[x] | 0xff000000;
			}
			scale = readScale;
		} else {
			delete bitmap;
			bitmap = NULL;
		}
		delete_area(clone);
		return bitmap;
	}

private:
	BLocker			fLock;
	sem_id			fReady;
	bool			fConnected;
	area_id			fArea;
	int32			fBytesPerRow;
	uint32			fScale;
	color_space		fFormat;
	bool			fDrawingBuffer;
};


// Draws the mouse pointer into a bitmap of screen pixels where app_server
// draws it. System pointers are made at the screen's density (22 pixels at
// 100%, 38 at 175%); pointers that programs bring along are made for 100%
// and enlarged by app_server, which is done here the same way.
void DrawPointer(BBitmap* bitmap, float scale)
{
	BPoint where;
	uint32 buttons;
	BBitmap* pointer = NULL;
	BPoint hotSpot;
	if (get_mouse(&where, &buttons) != B_OK || get_mouse_bitmap(&pointer, &hotSpot) != B_OK
		|| pointer == NULL)
		return;
	if (pointer->ColorSpace() != B_RGBA32 || bitmap->ColorSpace() != B_RGB32) {
		delete pointer;
		return;
	}
	int32 pointerWidth = pointer->Bounds().IntegerWidth() + 1;
	int32 pointerHeight = pointer->Bounds().IntegerHeight() + 1;
	float factor = pointerWidth + 1 >= 22 * scale ? 1 : scale;
	int32 width = (int32)roundf(pointerWidth * factor);
	int32 height = (int32)roundf(pointerHeight * factor);
	int32 left = (int32)(floorf(where.x * scale) - floorf(hotSpot.x * factor));
	int32 top = (int32)(floorf(where.y * scale) - floorf(hotSpot.y * factor));
	int32 right = bitmap->Bounds().IntegerWidth();
	int32 bottom = bitmap->Bounds().IntegerHeight();
	const uint8* source = (const uint8*)pointer->Bits();
	int32 sourceBytesPerRow = pointer->BytesPerRow();
	uint8* destination = (uint8*)bitmap->Bits();
	int32 destinationBytesPerRow = bitmap->BytesPerRow();
	for (int32 y = std::max((int32)0, -top); y < height && top + y <= bottom; y++) {
		int32 sourceY = std::min((int32)(y / factor), pointerHeight - 1);
		const uint8* from = source + sourceY * sourceBytesPerRow;
		uint8* to = destination + (top + y) * destinationBytesPerRow;
		for (int32 x = std::max((int32)0, -left); x < width && left + x <= right; x++) {
			// Blended like app_server's software pointer: unassociated alpha.
			const uint8* pixel = from + std::min((int32)(x / factor), pointerWidth - 1) * 4;
			uint8* target = to + (left + x) * 4;
			int32 alpha = pixel[3];
			for (int32 channel = 0; channel < 3; channel++)
				target[channel] = (target[channel] * (255 - alpha) + pixel[channel] * alpha) / 255;
		}
	}
	delete pointer;
}


// The screen in frame buffer pixels, or NULL when the desktop is not scaled
// or this system cannot read or show them; then the caller grabs it at 100%.
BBitmap* GrabScreenPixels(bool includeCursor, float& scale)
{
	if (ScreenCapture::DeviceScale() <= 1)
		return NULL;
	BScreen screen;
	BRect frame = screen.Frame();
	display_mode mode;
	if (screen.GetMode(&mode) != B_OK)
		return NULL;

	ScreenPixelReader* reader = new ScreenPixelReader();
	reader->Show();
	BBitmap* bitmap = NULL;
	if (reader->WaitConnected()) {
		// Without app_server's copy of the desktop the pixels come from the
		// frame buffer, which can have a software pointer in it.
		bool hidden = false;
		if (!reader->ReadsDrawingBuffer() && be_app != NULL && !be_app->IsCursorHidden()) {
			be_app->HideCursor();
			hidden = true;
			snooze(150000);
		}
		bitmap = reader->Grab(frame, mode, scale);
		if (hidden)
			be_app->ShowCursor();
	}
	reader->Lock();
	reader->Quit();
	if (bitmap != NULL && includeCursor)
		DrawPointer(bitmap, scale);
	return bitmap;
}

}  // namespace


BRect WindowEntry::CaptureFrame(bool includeDecorations, BRect screenFrame) const
{
	BRect result = includeDecorations ? decoratedFrame : frame;
	return result & screenFrame;
}


BBitmap* ScreenCapture::GrabScreen(bool includeCursor, bool screenPixels, float* pixelScale)
{
	if (pixelScale != NULL)
		*pixelScale = 1;
	if (screenPixels) {
		float scale = 1;
		BBitmap* pixels = GrabScreenPixels(includeCursor, scale);
		if (pixels != NULL) {
			if (pixelScale != NULL)
				*pixelScale = scale;
			return pixels;
		}
	}

	BBitmap* bitmap = NULL;
	BScreen screen;
	// app_server does not reliably leave the cursor out (Haiku tickets #2988
	// and #2997), so hide it ourselves when it must not show.
	bool hidden = false;
	if (!includeCursor && be_app != NULL && !be_app->IsCursorHidden()) {
		be_app->HideCursor();
		hidden = true;
		// The software cursor is removed asynchronously; give it a moment.
		snooze(150000);
	}
	status_t status = screen.GetBitmap(&bitmap, includeCursor);
	if (hidden)
		be_app->ShowCursor();
	if (status != B_OK) {
		delete bitmap;
		return NULL;
	}
	return bitmap;
}


float ScreenCapture::DeviceScale()
{
	// BScreen::DeviceScale() came to air/OS with B_DEVICE_PIXEL_BITMAP, which
	// the overlay and the editor need to show screen pixels as they are. It
	// is looked up at run time so that airShot builds and runs without it.
	typedef float (*DeviceScaleFunction)(BScreen* screen);
	static DeviceScaleFunction deviceScale = NULL;
	static bool looked = false;
	if (!looked) {
		void* libbe = dlopen("libbe.so", RTLD_LAZY);
		if (libbe != NULL)
			deviceScale = (DeviceScaleFunction)dlsym(libbe, "_ZN7BScreen11DeviceScaleEv");
		looked = true;
	}
	if (deviceScale == NULL)
		return 1;
	BScreen screen;
	float scale = deviceScale(&screen);
	return scale > 1 ? scale : 1;
}


BRect ScreenCapture::ToPixels(BRect rect, float pixelScale)
{
	if (pixelScale == 1)
		return rect;
	return BRect(floorf(rect.left * pixelScale), floorf(rect.top * pixelScale),
		floorf((rect.right + 1) * pixelScale) - 1, floorf((rect.bottom + 1) * pixelScale) - 1);
}


void ScreenCapture::ListWindows(std::vector<WindowEntry>& windows, team_id exclude)
{
	windows.clear();
	BRect screenFrame = BScreen().Frame();
	int32* tokens = NULL;
	int32 count = 0;
	if (BPrivate::get_window_order(current_workspace(), &tokens, &count) != B_OK
		|| tokens == NULL)
		return;

	WindowEntry desktop;
	bool haveDesktop = false;
	for (int32 i = 0; i < count; i++) {
		client_window_info* info = get_window_info(tokens[i]);
		if (info == NULL)
			continue;
		bool visible = info->layer >= 2 && !info->is_mini && info->show_hide_level <= 0;
		if (visible && info->team != exclude && info->feel != kMenuWindowFeel) {
			WindowEntry entry;
			entry.team = info->team;
			entry.token = tokens[i];
			entry.name = info->name;
			entry.frame.Set(info->window_left, info->window_top, info->window_right,
				info->window_bottom);
			entry.tabHeight = info->tab_height;
			entry.borderSize = info->border_size;
			entry.decoratedFrame = entry.frame;
			entry.decoratedFrame.InsetBy(-entry.borderSize, -entry.borderSize);
			entry.decoratedFrame.top -= entry.tabHeight;
			entry.desktop = info->feel == kDesktopWindowFeel;
			if (entry.desktop) {
				entry.frame = screenFrame;
				entry.decoratedFrame = screenFrame;
				entry.name = "Desktop";
				if (!haveDesktop) {
					desktop = entry;
					haveDesktop = true;
				}
			} else if (entry.decoratedFrame.Intersects(screenFrame))
				windows.push_back(entry);
		}
		free(info);
	}
	free(tokens);
	if (haveDesktop)
		windows.push_back(desktop);
}


status_t ScreenCapture::TabFrameFor(const WindowEntry& entry, BRect& tabFrame)
{
	if (entry.desktop || entry.tabHeight <= 0)
		return B_ERROR;
	BMessenger messenger(NULL, entry.team);
	if (!messenger.IsValid())
		return B_ERROR;
	// Find the window's index in its application by comparing frames, then
	// ask for its tab.
	for (int32 index = 0; index < 64; index++) {
		BMessage request(B_GET_PROPERTY);
		request.AddSpecifier("Frame");
		request.AddSpecifier("Window", index);
		BMessage reply;
		if (messenger.SendMessage(&request, &reply, 200000, 200000) != B_OK)
			return B_ERROR;
		if (reply.what == B_MESSAGE_NOT_UNDERSTOOD || reply.what == B_ERROR)
			return B_ERROR;
		BRect frame;
		if (reply.FindRect("result", &frame) != B_OK)
			continue;
		if (fabs(frame.left - entry.frame.left) > 1 || fabs(frame.top - entry.frame.top) > 1
			|| fabs(frame.right - entry.frame.right) > 1
			|| fabs(frame.bottom - entry.frame.bottom) > 1)
			continue;
		request.MakeEmpty();
		request.what = B_GET_PROPERTY;
		request.AddSpecifier("TabFrame");
		request.AddSpecifier("Window", index);
		reply.MakeEmpty();
		if (messenger.SendMessage(&request, &reply, 200000, 200000) != B_OK)
			return B_ERROR;
		return reply.FindRect("result", &tabFrame);
	}
	return B_ERROR;
}


BBitmap* ScreenCapture::Crop(const BBitmap* source, BRect rect, bool transparent)
{
	rect = rect & source->Bounds();
	if (!rect.IsValid())
		return NULL;
	rect.left = floorf(rect.left);
	rect.top = floorf(rect.top);
	rect.right = floorf(rect.right);
	rect.bottom = floorf(rect.bottom);
	BBitmap* result = new BBitmap(rect.OffsetToCopy(B_ORIGIN),
		transparent ? B_RGBA32 : B_RGB32);
	if (result->InitCheck() != B_OK
		|| result->ImportBits(source, rect.LeftTop(), B_ORIGIN, rect.Size()) != B_OK) {
		delete result;
		return NULL;
	}
	if (transparent) {
		// Screens have no alpha channel; make the copy opaque first.
		uint8* bits = (uint8*)result->Bits();
		int32 length = result->BitsLength();
		for (int32 i = 3; i < length; i += 4)
			bits[i] = 255;
	}
	return result;
}


void ScreenCapture::ClearTabSpace(BBitmap* bitmap, BRect frame, BRect tabFrame, float tabHeight)
{
	if (bitmap->ColorSpace() != B_RGBA32 || !frame.Contains(tabFrame) || tabHeight <= 0)
		return;
	BRegion space(frame);
	BRect below(frame);
	below.top += tabHeight;
	space.Exclude(below);
	space.Exclude(tabFrame);
	space.OffsetBy(-frame.left, -frame.top);

	uint8* bits = (uint8*)bitmap->Bits();
	int32 rowBytes = bitmap->BytesPerRow();
	BRect bounds = bitmap->Bounds();
	for (int32 i = 0; i < space.CountRects(); i++) {
		BRect rect = space.RectAt(i) & bounds;
		if (!rect.IsValid())
			continue;
		for (int32 y = (int32)rect.top; y <= (int32)rect.bottom; y++) {
			uint32* row = (uint32*)(bits + y * rowBytes);
			for (int32 x = (int32)rect.left; x <= (int32)rect.right; x++)
				row[x] = 0;
		}
	}
}

}  // namespace airshot
