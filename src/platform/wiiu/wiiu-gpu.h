//
// Created by nhp on 14/08/2020.
//

#ifndef MGBA_WIIU_GPU_H
#define MGBA_WIIU_GPU_H

#include <feature/gui/gui-runner.h>
#define TEX_W 256
#define TEX_H 224

extern GX2Texture coreTexture;
extern struct WHBGfxShaderGroup textureShaderGroup;

typedef enum ScreenMode {
	SM_PA,
	SM_AF,
	SM_SF,
	SM_MAX
} ScreenMode;

void setupGpu();
void drawTex(struct mGUIRunner* runner, unsigned width, unsigned height, bool faded, bool blendTop, bool drc);

#endif // MGBA_WIIU_GPU_H
