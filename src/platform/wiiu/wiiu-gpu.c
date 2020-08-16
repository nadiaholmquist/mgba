//
// Created by nhp on 14/08/2020.
//
#include <stdint.h>

#include <coreinit/memdefaultheap.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/texture.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <whb/gfx.h>
#include <whb/log.h>

#include "wiiu-gpu.h"

void gpuInitTexture(GX2Texture* texture, uint32_t width, uint32_t height, GX2SurfaceFormat format) {
	texture->surface.width = width;
	texture->surface.height = height;
	texture->surface.depth = 1;
	texture->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	texture->surface.format = format;
	texture->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
	texture->viewNumSlices = 1;
	texture->compMap = 0x03020100;
	GX2CalcSurfaceSizeAndAlignment(&texture->surface);
	GX2InitTextureRegs(texture);
	texture->surface.image = MEMAllocFromDefaultHeapEx(texture->surface.imageSize, texture->surface.alignment);
}

void gpuInitBuffer(GX2RBuffer* buffer, uint32_t elemSize, uint32_t elemCount) {
	buffer->flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER | GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ;
	buffer->elemSize = elemSize;
	buffer->elemCount = elemCount;
	GX2RCreateBuffer(buffer);

	void* content = GX2RLockBufferEx(buffer, 0);
	memset(content, 0, buffer->elemSize * buffer->elemCount);
	GX2RUnlockBufferEx(buffer, 0);
}

void gpuSetShaders(const struct WHBGfxShaderGroup* group) {
	GX2SetFetchShader(&group->fetchShader);
	GX2SetVertexShader(group->vertexShader);
	GX2SetPixelShader(group->pixelShader);
}

void gpuUpdateTexture(GX2Texture* texture, const void* restrict data, size_t pixelSize) {
	uint32_t* dst = texture->surface.image;
	const uint32_t* src = data;

	for (uint32_t i = 0; i < texture->surface.height; i++) {
		memcpy(dst, src, texture->surface.width * pixelSize);
		dst += texture->surface.pitch;
		src += texture->surface.width;
	}

	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, texture->surface.image, texture->surface.imageSize);
}

bool gpuInitShaderGroup(WHBGfxShaderGroup* group, const char* path) {
	FILE* shaderFile = fopen(path, "r");

	if (!shaderFile) {
		WHBLogPrint("failed to load shader");
		return false;
	}

	fseek(shaderFile, 0, SEEK_END);
	size_t size = ftell(shaderFile);
	fseek(shaderFile, 0, SEEK_SET);

	void* content = malloc(size);
	fread(content, size, 1, shaderFile);
	fclose(shaderFile);

	if (!WHBGfxLoadGFDShaderGroup(group, 0, content)) {
		WHBLogPrint("WHBGfxLoadGFDShader returned false");
		return false;
	}

	free(content);
	return true;
}