//
// Created by nhp on 14/08/2020.
//

#include <stdio.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>

#include <gx2/display.h>
#include <gx2/texture.h>
#include <gx2r/buffer.h>

#include <coreinit/memdefaultheap.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2r/draw.h>
#include <math.h>
#include <mgba/internal/gb/video.h>
#include <whb/gfx.h>
#include <whb/log.h>

#include "wiiu-gpu.h"

extern bool sgbCrop;
extern ScreenMode screenMode;

GX2Texture coreTexture;
struct GX2RBuffer positionBuffer;
struct GX2RBuffer texCoordBuffer;
GX2Sampler coreSampler;
struct WHBGfxShaderGroup textureShaderGroup;

#define TEX_W 256
#define TEX_H 224

int vwidth = 1920;
int vheight = 1080;
int drcwidth = 854;
int drcheight = 480;

void setupGpu() {
	WHBGfxInit();

	coreTexture.surface.width = TEX_W;
	coreTexture.surface.height = TEX_H;
	coreTexture.surface.depth = 1;
	coreTexture.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	coreTexture.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	coreTexture.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	coreTexture.viewNumSlices = 1;
	coreTexture.compMap = 0x03020100;
	GX2CalcSurfaceSizeAndAlignment(&coreTexture.surface);
	GX2InitTextureRegs(&coreTexture);
	coreTexture.surface.image = MEMAllocFromDefaultHeapEx(coreTexture.surface.imageSize, coreTexture.surface.alignment);

	FILE* textureShader = fopen("romfs:/texture_shader.gsh", "r");

	if (!textureShader) {
		WHBLogPrint("failed to load texture shader");
		return;
	}

	fseek(textureShader, 0, SEEK_END);
	size_t size = ftell(textureShader);
	WHBLogPrintf("size = %d", size);
	fseek(textureShader, 0, SEEK_SET);

	void* shader = malloc(size);
	fread(shader, size, 1, textureShader);
	fclose(textureShader);

	if (!WHBGfxLoadGFDShaderGroup(&textureShaderGroup, 0, shader)) {
		WHBLogPrint("WHBGfxLoadGFDShader returned false");
		return;
	}

	free(shader);

	WHBGfxInitShaderAttribute(&textureShaderGroup, "position", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitShaderAttribute(&textureShaderGroup, "tex_coord_in", 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(&textureShaderGroup);
	WHBLogPrint("inited shader");

	// texcoords for the GUI
	const float texCoords[] = {
		0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f,
	};

	// Position for the GUI
	const float pos[] = {
		-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
	};

	void* buffer;

	texCoordBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
	texCoordBuffer.elemSize = 2 * 4;
	texCoordBuffer.elemCount = 8;
	GX2RCreateBuffer(&texCoordBuffer);
	buffer = GX2RLockBufferEx(&texCoordBuffer, 0);
	memcpy(buffer + texCoordBuffer.elemSize * 4, texCoords, texCoordBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&texCoordBuffer, 0);

	positionBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
	positionBuffer.elemSize = 2 * 4;
	positionBuffer.elemCount = 12;
	GX2RCreateBuffer(&positionBuffer);
	buffer = GX2RLockBufferEx(&positionBuffer, 0);
	memcpy(buffer + positionBuffer.elemSize * 8, pos, texCoordBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&positionBuffer, 0);

	GX2InitSampler(&coreSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
}

void drawTex(struct mGUIRunner* runner, unsigned width, unsigned height, bool faded, bool blendTop, bool drc) {
	float inwidth = width;
	float inheight = height;
	float screenWidth = vwidth;
	float screenHeight = vheight;

	if (drc) {
		screenWidth = drcwidth;
		screenHeight = drcheight;
	}

	if (sgbCrop && width == 256 && height == 224) {
		inwidth = GB_VIDEO_HORIZONTAL_PIXELS;
		inheight = GB_VIDEO_VERTICAL_PIXELS;
	}
	float aspectX = inwidth / screenWidth;
	float aspectY = inheight / screenHeight;
	float max = 1.f;

	switch (screenMode) {
	case SM_PA:
		if (aspectX > aspectY) {
			max = floor(1.0 / aspectX);
		} else {
			max = floor(1.0 / aspectY);
		}
		if (max >= 1.0 && !drc) {
			break;
		}
		// Fall through
	case SM_AF:
		if (aspectX > aspectY) {
			max = 1.0 / aspectX;
		} else {
			max = 1.0 / aspectY;
		}
		break;
	case SM_SF:
		aspectX = 1.0;
		aspectY = 1.0;
		break;
	}

	if (screenMode != SM_SF) {
		aspectX = width / (float) screenWidth;
		aspectY = height / (float) screenHeight;
	}

	aspectX *= max;
	aspectY *= max;

	float wpart = (1.0f / TEX_W) * (float) width;
	float hpart = (1.0f / TEX_H) * (float) height;

	const float texCoords[] = {
		0.0f, hpart, wpart, hpart, wpart, 0.0f, 0.0f, 0.0f,
	};
	const float pos[] = {
		-aspectX, -aspectY, aspectX, -aspectY, aspectX, aspectY, -aspectX, aspectY,
	};

	void* buffer;

	buffer = GX2RLockBufferEx(&texCoordBuffer, 0);
	memcpy(buffer, texCoords, texCoordBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&texCoordBuffer, 0);

	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, &texCoordBuffer,
	              texCoordBuffer.elemCount * texCoordBuffer.elemSize);

	buffer = GX2RLockBufferEx(&positionBuffer, 0);
	memcpy(buffer + (drc ? positionBuffer.elemSize * 4 : 0), pos, positionBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&positionBuffer, 0);

	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, &positionBuffer,
	              positionBuffer.elemCount * positionBuffer.elemSize);

	GX2RSetAttributeBuffer(&positionBuffer, 0, positionBuffer.elemSize, drc ? positionBuffer.elemSize * 4 : 0);
	GX2RSetAttributeBuffer(&texCoordBuffer, 1, texCoordBuffer.elemSize, 0);
	GX2SetPixelTexture(&coreTexture, 0);
	GX2SetPixelSampler(&coreSampler, 0);
	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}