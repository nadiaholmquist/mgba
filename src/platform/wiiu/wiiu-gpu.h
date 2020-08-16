//
// Created by nhp on 14/08/2020.
//

#include <whb/gfx.h>
#ifndef MGBA_WIIU_GPU_H
#define MGBA_WIIU_GPU_H

void gpuInitTexture(GX2Texture* texture, uint32_t width, uint32_t height, GX2SurfaceFormat format);
void gpuInitBuffer(GX2RBuffer* buffer, uint32_t elemSize, uint32_t elemCount);
void gpuSetShaders(const WHBGfxShaderGroup* group);
void gpuUpdateTexture(GX2Texture* texture, const void* data, size_t pixelSize);
bool gpuInitShaderGroup(WHBGfxShaderGroup* group, const char* path);

#endif // MGBA_WIIU_GPU_H
