/* Copyright (c) 2013-2018 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <mgba-util/gui/font.h>
#include <mgba-util/gui/font-metrics.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>

#include "wiiu-gpu.h"
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/registers.h>
#include <gx2/texture.h>
#include <gx2r/buffer.h>
#include <whb/gfx.h>
#include <whb/log.h>

#include "wiiu-gpu.h"

#define GLYPH_HEIGHT 24
#define CELL_HEIGHT 32
#define CELL_WIDTH 32

static const float _offsets[] = {
	0.f, 0.f,
	1.f, 0.f,
	1.f, 1.f,
	0.f, 1.f,
};

struct GUIFont {
	GX2Texture font;
	GX2Sampler sampler;
	WHBGfxShaderGroup shaders;
	GX2RBuffer offsetBuffer;
	uint32_t dimsLocation;
	uint32_t transformLocation;
	uint32_t colorLocation;
	uint32_t originLocation;
	uint32_t glyphLocation;
	uint32_t cutoffLocation;
};

static bool _loadTexture(const char* path, GX2Texture* texture) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		return false;
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
			WHBLogPrint("png fail");
			success = false;
		}
		if (success) {
			WHBLogPrint("png success");
			gpuInitTexture(texture, width, height, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
			//gpuUpdateTexture(texture, pixels, 1);
			void* tmp = malloc(width * height * 4);
			memset(tmp, 0x7F, width * height * 4);
			gpuUpdateTexture(texture, tmp, 4);
		}
	}
	PNGReadClose(png, info, end);
	if (pixels) {
		free(pixels);
	}
	vf->close(vf);
	WHBLogPrint("texture done");
	return success;
}

struct GUIFont* GUIFontCreate(void) {
	struct GUIFont* font = calloc(1, sizeof(struct GUIFont));
	if (!font) {
		return NULL;
	}
	if (!_loadTexture("romfs:/font-new.png", &font->font)) {
		GUIFontDestroy(font);
		return NULL;
	}
	WHBLogPrintf("texture loaded");

	gpuInitShaderGroup(&font->shaders, "romfs:/font.gsh");
	WHBGfxInitShaderAttribute(&font->shaders, "offset", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(&font->shaders);

	WHBLogPrint("shaders loaded");

	font->colorLocation = GX2GetPixelUniformVar(font->shaders.pixelShader, "color")->offset;
	font->cutoffLocation = GX2GetPixelUniformVar(font->shaders.pixelShader, "cutoff")->offset;

	font->dimsLocation = GX2GetVertexUniformVar(font->shaders.vertexShader, "dims")->offset;
	font->transformLocation = GX2GetVertexUniformVar(font->shaders.vertexShader, "transform")->offset;
	font->originLocation = GX2GetVertexUniformVar(font->shaders.vertexShader, "origin")->offset;
	font->glyphLocation = GX2GetVertexUniformVar(font->shaders.vertexShader, "glyph")->offset;

	gpuInitBuffer(&font->offsetBuffer, 2 * sizeof(float), 4);

	void* buffer = GX2RLockBufferEx(&font->offsetBuffer, 0);
	memcpy(buffer, _offsets, font->offsetBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&font->offsetBuffer, 0);
	GX2InitSampler(&font->sampler, GX2_TEX_CLAMP_MODE_CLAMP_BORDER, GX2_TEX_XY_FILTER_MODE_LINEAR);

	return font;
}

void GUIFontDestroy(struct GUIFont* font) {
	GX2RDestroyBufferEx(&font->offsetBuffer, 0);
	WHBGfxFreeShaderGroup(&font->shaders);
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

void _GUIFontDrawGlyph(const struct GUIFont* font, int x, int y, uint32_t color, uint32_t glyph) {
	if (glyph > 0x7F) {
		glyph = '?';
	}
	struct GUIFontGlyphMetric metric = defaultFontMetrics[glyph];

	gpuSetShaders(&font->shaders);
	GX2SetPixelTexture(&font->font, 0);
	GX2SetPixelSampler(&font->sampler, 0);

	GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD);
	GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);

	float glyphData[] = {(glyph & 15) * CELL_WIDTH + metric.padding.left * 2, (glyph >> 4) * CELL_HEIGHT + metric.padding.top * 2};
	float dimsData[] = {CELL_WIDTH - (metric.padding.left + metric.padding.right) * 2, CELL_HEIGHT - (metric.padding.top + metric.padding.bottom) * 2};
	float originData[] = {x, y - GLYPH_HEIGHT + metric.padding.top * 2, 0};
	float transformData[] = {1.0, 0.0, 0.0, 1.0};

	GX2SetVertexUniformReg(font->glyphLocation, 2, &glyphData);
	GX2SetVertexUniformReg(font->dimsLocation, 2, &dimsData);
	GX2SetVertexUniformReg(font->originLocation, 3, &originData);
	GX2SetVertexUniformReg(font->transformLocation, 2, &transformData);

	float cutoffData = 0.1f;
	float colorData[] = {0.0, 0.0, 0.0, ((color >> 24) & 0xFF) / 128.0f};
	GX2SetPixelUniformReg(font->cutoffLocation, 1, &cutoffData);
	GX2SetPixelUniformReg(font->colorLocation, 4, &colorData);
	//GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);

	float colorData2[] = { (color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f};
	float cutoffData2 = 0.7f;
	GX2SetPixelUniformReg(font->cutoffLocation, 1, &cutoffData2);
	GX2SetPixelUniformReg(font->colorLocation, 4, &colorData2);
	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

void GUIFontDrawGlyph(const struct GUIFont* font, int x, int y, uint32_t color, uint32_t glyph) {
	WHBGfxBeginRenderTV();
	_GUIFontDrawGlyph(font, x, y, color, glyph);
	WHBGfxBeginRenderDRC();
	_GUIFontDrawGlyph(font, x, y, color, glyph);
}

void GUIFontDrawIcon(const struct GUIFont* font, int x, int y, enum GUIAlignment align, enum GUIOrientation orient, uint32_t color, enum GUIIcon icon) {
/*	if (icon >= GUI_ICON_MAX) {
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

	glUseProgram(font->program);
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

	glUseProgram(font->program);
	glBindVertexArray(font->vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, font->font);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUniform1i(font->texLocation, 0);
	glUniform2f(font->glyphLocation, metric.x * 2, metric.y * 2 + 256);
	glUniform2f(font->dimsLocation, metric.width * 2, metric.height * 2);
	glUniform3f(font->originLocation, x, y, 0);
	glUniformMatrix2fv(font->transformLocation, 1, GL_FALSE, (float[4]) {hFlip, 0.0, 0.0, vFlip});

	glUniform1f(font->cutoffLocation, 0.1f);
	glUniform4f(font->colorLocation, 0.0, 0.0, 0.0, ((color >> 24) & 0xFF) / 128.0f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glUniform1f(font->cutoffLocation, 0.7f);
	glUniform4f(font->colorLocation, (color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glBindVertexArray(0);
	glUseProgram(0);*/
}

void GUIFontDrawIconSize(const struct GUIFont* font, int x, int y, int w, int h, uint32_t color, enum GUIIcon icon) {
	/*if (icon >= GUI_ICON_MAX) {
		return;
	}
	struct GUIIconMetric metric = defaultIconMetrics[icon];

	if (!w) {
		w = metric.width * 2;
	}
	if (!h) {
		h = metric.height * 2;
	}

	glUseProgram(font->program);
	glBindVertexArray(font->vao);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, font->font);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUniform1i(font->texLocation, 0);
	glUniform2f(font->glyphLocation, metric.x * 2, metric.y * 2 + 256);
	glUniform2f(font->dimsLocation, metric.width * 2, metric.height * 2);
	glUniform3f(font->originLocation, x + w / 2 - metric.width, y + h / 2 - metric.height, 0);
	glUniformMatrix2fv(font->transformLocation, 1, GL_FALSE, (float[4]) {w * 0.5f / metric.width, 0.0, 0.0, h * 0.5f / metric.height});

	glUniform1f(font->cutoffLocation, 0.1f);
	glUniform4f(font->colorLocation, 0.0, 0.0, 0.0, ((color >> 24) & 0xFF) / 128.0f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glUniform1f(font->cutoffLocation, 0.7f);
	glUniform4f(font->colorLocation, ((color >> 16) & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, (color & 0xFF) / 255.0f, ((color >> 24) & 0xFF) / 255.0f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glBindVertexArray(0);
	glUseProgram(0);*/
}
