/* Copyright (c) 2013-2018 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba-util/gui/font.h>
#include <mgba-util/gui/font-metrics.h>
#include <mgba-util/png-io.h>
#include <mgba-util/string.h>
#include <mgba-util/vfs.h>

#include <gx2/texture.h>


#define GLYPH_HEIGHT 24
#define CELL_HEIGHT 32
#define CELL_WIDTH 32

struct GUIFont {
	uint8_t* data;
};


struct GUIFont* GUIFontCreate(void) {
	struct GUIFont* font = malloc(sizeof(struct GUIFont));
	if (!font) {
		return NULL;
	}

	struct VFile* vf = VFileOpen("romfs://font.png", O_RDONLY);
	if (!vf) {
		return NULL;
	}
	png_structp png = PNGReadOpen(vf, 0);
	png_infop info = png_create_info_struct(png);
	png_infop end = png_create_info_struct(png);
	bool success = false;
	if (png && info && end) {
		success = PNGReadHeader(png, info);
	}
	void* pixels = NULL;
	if (success) {
		unsigned height = png_get_image_height(png, info);
		unsigned width = png_get_image_width(png, info);
		pixels = malloc(width * height);
		if (pixels) {
			success = PNGReadPixels8(png, info, pixels, width, height, width);
			success = success && PNGReadFooter(png, end);
		} else {
			success = false;
		}
		if (success) {
			font->data = pixels;
		}
	}
	PNGReadClose(png, info, end);
	vf->close(vf);

	return font;
}

void GUIFontDestroy(struct GUIFont* font) {
	free(font);
}

unsigned GUIFontHeight(const struct GUIFont* font) {
	UNUSED(font);
	return GLYPH_HEIGHT;
}

unsigned GUIFontGlyphWidth(const struct GUIFont* font, uint32_t glyph) {
	UNUSED(font);
	if (glyph > 0x7F) {
		glyph = '?';
	}
	return defaultFontMetrics[glyph].width * 2;
}

void GUIFontIconMetrics(const struct GUIFont* font, enum GUIIcon icon, unsigned* w, unsigned* h) {
	UNUSED(font);
	if (icon >= GUI_ICON_MAX) {
		if (w) {
			*w = 0;
		}
		if (h) {
			*h = 0;
		}
	} else {
		if (w) {
			*w = defaultIconMetrics[icon].width * 2;
		}
		if (h) {
			*h = defaultIconMetrics[icon].height * 2;
		}
	}
}

void GUIFontDrawGlyph(const struct GUIFont* font, int x, int y, uint32_t color, uint32_t glyph) {
	if (glyph > 0x7F) {
		glyph = '?';
	}
	struct GUIFontGlyphMetric metric = defaultFontMetrics[glyph];
}

void GUIFontDrawIcon(const struct GUIFont* font, int x, int y, enum GUIAlignment align, enum GUIOrientation orient, uint32_t color, enum GUIIcon icon) {
	if (icon >= GUI_ICON_MAX) {
		return;
	}
	struct GUIIconMetric metric = defaultIconMetrics[icon];

	float hFlip = 1.0f;
	float vFlip = 1.0f;
	switch (align & GUI_ALIGN_HCENTER) {
	case GUI_ALIGN_HCENTER:
		x -= metric.width;
		break;
	case GUI_ALIGN_RIGHT:
		x -= metric.width * 2;
		break;
	}
	switch (align & GUI_ALIGN_VCENTER) {
	case GUI_ALIGN_VCENTER:
		y -= metric.height;
		break;
	case GUI_ALIGN_BOTTOM:
		y -= metric.height * 2;
		break;
	}

	switch (orient) {
	case GUI_ORIENT_HMIRROR:
		hFlip = -1.0;
		break;
	case GUI_ORIENT_VMIRROR:
		vFlip = -1.0;
		break;
	case GUI_ORIENT_0:
	default:
		// TODO: Rotate
		break;
	}

}

void GUIFontDrawIconSize(const struct GUIFont* font, int x, int y, int w, int h, uint32_t color, enum GUIIcon icon) {
	if (icon >= GUI_ICON_MAX) {
		return;
	}
	struct GUIIconMetric metric = defaultIconMetrics[icon];

	if (!w) {
		w = metric.width * 2;
	}
	if (!h) {
		h = metric.height * 2;
	}

}
