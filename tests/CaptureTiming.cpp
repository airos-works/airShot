// Native screen-readback regression probe. An optional millisecond limit
// makes unexpectedly slow captures fail on a known test workstation.
#include <Application.h>
#include <Screen.h>

#include <stdio.h>
#include <stdlib.h>

#include "capture/ScreenCapture.h"

int main(int argc, char** argv)
{
	char* end = NULL;
	long limit = argc == 2 ? strtol(argv[1], &end, 10) : 0;
	if (argc > 2 || (argc == 2 && (end == argv[1] || *end != '\0'
			|| limit <= 0 || limit > 60000))) {
		fprintf(stderr, "usage: capture_timing [max-milliseconds (1..60000)]\n");
		return 2;
	}
	BApplication app("application/x-vnd.airOS-airShot-capture-timing");
	BRect frame = BScreen().Frame();
	bool passed = true;
	for (int i = 0; i < 8; i++) {
		bool cursor = (i % 2) != 0;
		// The last two read the screen's own pixels when it is scaled.
		bool screenPixels = i >= 6;
		float scale = 0;
		bigtime_t start = system_time();
		BBitmap* bitmap = airshot::ScreenCapture::GrabScreen(cursor, screenPixels, &scale);
		bigtime_t elapsed = system_time() - start;
		// A scaled buffer can end in a pixel that is only partly covered.
		BRect expected = airshot::ScreenCapture::ToPixels(frame, scale);
		if (bitmap == NULL || bitmap->InitCheck() != B_OK || scale < 1
			|| (!screenPixels && scale != 1)
			|| bitmap->Bounds().Width() < expected.Width()
			|| bitmap->Bounds().Width() > expected.Width() + (scale != 1 ? 1 : 0)
			|| bitmap->Bounds().Height() < expected.Height()
			|| bitmap->Bounds().Height() > expected.Height() + (scale != 1 ? 1 : 0)) {
			fprintf(stderr, "capture %d: missing or incorrectly sized bitmap\n", i + 1);
			delete bitmap;
			return 1;
		}
		printf("capture %d: %.0fx%.0f, cursor=%d, scale=%g, %.1f ms\n", i + 1,
			bitmap->Bounds().Width() + 1, bitmap->Bounds().Height() + 1, cursor, scale,
			elapsed / 1000.0);
		if (limit != 0 && elapsed > limit * 1000LL)
			passed = false;
		delete bitmap;
	}
	return passed ? 0 : 1;
}
