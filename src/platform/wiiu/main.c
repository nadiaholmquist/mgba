/* Copyright (c) 2013-2018 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "feature/gui/gui-runner.h"
#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/internal/gb/video.h>
#include <mgba/internal/gba/audio.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/gui.h>
#include <mgba-util/gui/font.h>
#include <mgba-util/gui/menu.h>
#include <mgba-util/vfs.h>

#include <string.h>
#include <stdio.h>

#include <coreinit/memdefaultheap.h>
#include <coreinit/filesystem.h>
#include <coreinit/thread.h>
#include <proc_ui/procui.h>
#include <sysapp/launch.h>

#include <gfd.h>
#include <gx2/draw.h>
#include <gx2/shaders.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2r/draw.h>
#include <gx2r/buffer.h>

#include <whb/file.h>
#include <whb/sdcard.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>
#include <whb/proc.h>

#include <romfs-wiiu.h>

#include <vpad/input.h>

#define VPAD_INPUT 0x7f87ffff

#define SAMPLES 0x200
#define BUFFER_SIZE 0x1000
#define N_BUFFERS 4
#define mainANALOG_DEADZONE 0x4000

#define TEX_W 256
#define TEX_H 224

static struct mAVStream stream;
static struct mRotationSource rotation = {0};
static int audioBufferActive;
static struct GBAStereoSample audioBuffer[N_BUFFERS][SAMPLES] __attribute__((__aligned__(0x1000)));
static int enqueuedBuffers;
static bool frameLimiter = true;
static unsigned framecount = 0;
static unsigned framecap = 10;
static uint8_t vmode;
static uint32_t vwidth = 1920;
static uint32_t vheight = 1080;
static uint32_t drcwidth = 800;
static uint32_t drcheight = 480;
static bool interframeBlending = false;
static bool sgbCrop = false;
static bool useLightSensor = true;
static struct mGUIRunnerLux lightSensor;

GX2RBuffer corePositionBuffer = {0};
GX2RBuffer guiPositionBuffer = {0};
GX2RBuffer texCoordBuffer = {0};

color_t* outputBuffer;
GX2Texture coreTexture;
GX2Texture uiTexture;
GX2Sampler coreSampler;
GX2Sampler* uiSampler;
struct WHBGfxShaderGroup textureShaderGroup;

static enum ScreenMode {
	SM_PA,
	SM_AF,
	SM_SF,
	SM_MAX
} screenMode = SM_PA;


static void _mapKey(struct mInputMap* map, uint32_t binding, int nativeKey, enum GBAKey key) {
	mInputBindKey(map, binding, __builtin_ctz(nativeKey), key);
}

static void _drawStart(void) {
	if (!WHBProcIsRunning())
		return;
	WHBGfxBeginRender();
}

static void _drawEnd(void) {
	if (!WHBProcIsRunning())
		return;
	WHBGfxFinishRender();
}

static uint32_t _pollInput(const struct mInputMap* map) {
	VPADStatus status;
	VPADReadError err;
	VPADRead(VPAD_CHAN_0, &status, 1, &err);

	return mInputMapKeyBits(map, VPAD_INPUT, status.hold, 0);
}

static enum GUICursorState _pollCursor(unsigned* x, unsigned* y) {
}

static void _setup(struct mGUIRunner* runner) {
	// todo put this not here
	coreTexture.surface.width = TEX_W;
	coreTexture.surface.height = TEX_H;
	coreTexture.surface.depth = 4;
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
		return 1;
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
		return 1;
	}

	free(shader);

	WHBGfxInitShaderAttribute(&textureShaderGroup, "position", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitShaderAttribute(&textureShaderGroup, "tex_coord_in", 1, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(&textureShaderGroup);

	const float texCoords[] = {
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
		0.0f, 0.0f,
	};
	const float pos[] = {
		-1.0f, -1.0f,
		1.0f, -1.0f,
		1.0f, 1.0f,
		-1.0f, 1.0f,
	};

	void* buffer;

	WHBLogPrint("setup coord buffer");
	texCoordBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
		GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_CPU_WRITE |
		GX2R_RESOURCE_USAGE_GPU_READ;
	texCoordBuffer.elemSize = 2 * 4;
	texCoordBuffer.elemCount = 4;
	GX2RCreateBuffer(&texCoordBuffer);
	buffer = GX2RLockBufferEx(&texCoordBuffer, 0);
	memcpy(buffer, texCoords, texCoordBuffer.elemSize * texCoordBuffer.elemCount);
	GX2RUnlockBufferEx(&texCoordBuffer, 0);

	WHBLogPrint("setup pos buffer");
	corePositionBuffer.flags = GX2R_RESOURCE_BIND_VERTEX_BUFFER |
		GX2R_RESOURCE_USAGE_CPU_READ |
		GX2R_RESOURCE_USAGE_CPU_WRITE |
		GX2R_RESOURCE_USAGE_GPU_READ;
	corePositionBuffer.elemSize = 2 * 4;
	corePositionBuffer.elemCount = 8;
	GX2RCreateBuffer(&corePositionBuffer);
	buffer = GX2RLockBufferEx(&corePositionBuffer, 0);
	memcpy(buffer, pos, corePositionBuffer.elemSize * corePositionBuffer.elemCount);
	GX2RUnlockBufferEx(&corePositionBuffer, 0);

	GX2InitSampler(&coreSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);

	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_A, GBA_KEY_A);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_B, GBA_KEY_B);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_PLUS, GBA_KEY_START);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_MINUS, GBA_KEY_SELECT);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_UP, GBA_KEY_UP);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_DOWN, GBA_KEY_DOWN);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_LEFT, GBA_KEY_LEFT);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_RIGHT, GBA_KEY_RIGHT);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_L, GBA_KEY_L);
	_mapKey(&runner->core->inputMap, VPAD_INPUT, VPAD_BUTTON_R, GBA_KEY_R);

//	runner->core->setPeripheral(runner->core, mPERIPH_RUMBLE, &rumble.d);
//	runner->core->setPeripheral(runner->core, mPERIPH_ROTATION, &rotation);
//	runner->core->setAVStream(runner->core, &stream);

/*	if (runner->core->platform(runner->core) == PLATFORM_GBA && useLightSensor) {
		runner->core->setPeripheral(runner->core, mPERIPH_GBA_LUMINANCE, &lightSensor.d);
	}*/

	unsigned mode;
	if (mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode) && mode < SM_MAX) {
		screenMode = mode;
	}

	outputBuffer = calloc(TEX_W * TEX_H, sizeof(uint32_t));

	runner->core->setAudioBufferSize(runner->core, SAMPLES);
	runner->core->setVideoBuffer(runner->core, outputBuffer, TEX_W);
}

static void _teardown(struct mGUIRunner* runner) {
	WHBProcStopRunning();
	WHBLogPrint("_teardown called");
}

static void _gameLoaded(struct mGUIRunner* runner) {
	uint32_t samplerate = 48000;

	double ratio = GBAAudioCalculateRatio(1, 60.0, 1);
	blip_set_rates(runner->core->getAudioChannel(runner->core, 0), runner->core->frequency(runner->core), samplerate * ratio);
	blip_set_rates(runner->core->getAudioChannel(runner->core, 1), runner->core->frequency(runner->core), samplerate * ratio);

	mCoreConfigGetUIntValue(&runner->config, "fastForwardCap", &framecap);

	unsigned mode;
	if (mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode) && mode < SM_MAX) {
		screenMode = mode;
	}

	int fakeBool;
	if (mCoreConfigGetIntValue(&runner->config, "interframeBlending", &fakeBool)) {
		interframeBlending = fakeBool;
	}
	if (mCoreConfigGetIntValue(&runner->config, "sgb.borderCrop", &fakeBool)) {
		sgbCrop = fakeBool;
	}
/*	if (mCoreConfigGetIntValue(&runner->config, "useLightSensor", &fakeBool)) {
		if (useLightSensor != fakeBool) {
			useLightSensor = fakeBool;

			if (runner->core->platform(runner->core) == PLATFORM_GBA) {
				if (useLightSensor) {
					runner->core->setPeripheral(runner->core, mPERIPH_GBA_LUMINANCE, &lightSensor.d);
				} else {
					runner->core->setPeripheral(runner->core, mPERIPH_GBA_LUMINANCE, &runner->luminanceSource.d);
				}
			}
		}
	}*/

	int scale;
	if (mCoreConfigGetUIntValue(&runner->config, "videoScale", &scale)) {
		runner->core->reloadConfigOption(runner->core, "videoScale", &runner->config);
	}

}

static void _gameUnloaded(struct mGUIRunner* runner) {
}

static void _drawTex(struct mGUIRunner* runner, unsigned width, unsigned height, bool faded, bool blendTop, bool drc) {
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
		0.0f, hpart,
		wpart, hpart,
		wpart, 0.0f,
		0.0f, 0.0f,
	};
	const float pos[] = {
		-aspectX, -aspectY,
		aspectX, -aspectY,
		aspectX, aspectY,
		-aspectX, aspectY,
	};

	void* buffer;

	buffer = GX2RLockBufferEx(&texCoordBuffer, 0);
	memcpy(buffer, texCoords, texCoordBuffer.elemSize * texCoordBuffer.elemCount);
	GX2RUnlockBufferEx(&texCoordBuffer, 0);

	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, &texCoordBuffer, texCoordBuffer.elemCount * texCoordBuffer.elemSize);

	buffer = GX2RLockBufferEx(&corePositionBuffer, 0);
	memcpy(buffer + (drc ? corePositionBuffer.elemSize * 4 : 0), pos, corePositionBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&corePositionBuffer, 0);

	GX2Invalidate(GX2_INVALIDATE_MODE_ATTRIBUTE_BUFFER, &corePositionBuffer, corePositionBuffer.elemCount * corePositionBuffer.elemSize);

	GX2RSetAttributeBuffer(&corePositionBuffer, 0, corePositionBuffer.elemSize, drc ? corePositionBuffer.elemSize * 4 : 0);
	GX2RSetAttributeBuffer(&texCoordBuffer, 1, texCoordBuffer.elemSize, 0);
	GX2SetPixelTexture(&coreTexture, 0);
	GX2SetPixelSampler(&coreSampler, 0);
	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

static void _prepareForFrame(struct mGUIRunner* runner) {
	if (!WHBProcIsRunning())
		return;

	uint32_t* dst = coreTexture.surface.image;
	uint32_t* src = outputBuffer;

	for (uint32_t i = 0; i < TEX_H; i++) {
		memcpy(dst, src, TEX_W * sizeof(uint32_t));
		dst += coreTexture.surface.pitch;
		src += TEX_W;
	}

	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, coreTexture.surface.image, coreTexture.surface.imageSize);
}

static void _drawFrame(struct mGUIRunner* runner, bool faded) {
	if (!WHBProcIsRunning())
		return;

	++framecount;
	if (!frameLimiter && framecount < framecap) {
		return;
	}

	unsigned width, height;
	runner->core->desiredVideoDimensions(runner->core, &width, &height);

	WHBGfxBeginRenderTV();
	WHBGfxClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	GX2SetFetchShader(&textureShaderGroup.fetchShader);
	GX2SetVertexShader(textureShaderGroup.vertexShader);
	GX2SetPixelShader(textureShaderGroup.pixelShader);

	_drawTex(runner, width, height, faded, false, false);
	WHBGfxFinishRenderTV();

	WHBGfxBeginRenderDRC();
	WHBGfxClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	GX2SetFetchShader(&textureShaderGroup.fetchShader);
	GX2SetVertexShader(textureShaderGroup.vertexShader);
	GX2SetPixelShader(textureShaderGroup.pixelShader);
	_drawTex(runner, width, height, faded, false, true);
	WHBGfxFinishRenderDRC();
}

static void _drawScreenshot(struct mGUIRunner* runner, const color_t* pixels, unsigned width, unsigned height, bool faded) {
	if (!WHBProcIsRunning())
		return;

//	_drawTex(runner, width, height, faded, false);
}

static uint16_t _pollGameInput(struct mGUIRunner* runner) {
	return _pollInput(&runner->core->inputMap);
}

static void _incrementScreenMode(struct mGUIRunner* runner) {
	UNUSED(runner);
	screenMode = (screenMode + 1) % SM_MAX;
	mCoreConfigSetUIntValue(&runner->config, "screenMode", screenMode);
}

static void _setFrameLimiter(struct mGUIRunner* runner, bool limit) {
	UNUSED(runner);
}

static bool _running(struct mGUIRunner* runner) {
	UNUSED(runner);
	return WHBProcIsRunning();
}

static void _postAudioBuffer(struct mAVStream* stream, blip_t* left, blip_t* right) {
	UNUSED(stream);
	if (!frameLimiter && enqueuedBuffers >= N_BUFFERS) {
		blip_clear(left);
		blip_clear(right);
		return;
	}
	if (enqueuedBuffers >= N_BUFFERS - 1) {
	}
	if (enqueuedBuffers >= N_BUFFERS) {
		blip_clear(left);
		blip_clear(right);
		return;
	}

	struct GBAStereoSample* samples = audioBuffer[audioBufferActive];
	blip_read_samples(left, &samples[0].left, SAMPLES, true);
	blip_read_samples(right, &samples[0].right, SAMPLES, true);
	audioBufferActive += 1;
	audioBufferActive %= N_BUFFERS;
	++enqueuedBuffers;
}

void _setRumble(struct mRumble* rumble, int enable) {
}

int32_t _readTiltX(struct mRotationSource* source) {
	return 0;
}

int32_t _readTiltY(struct mRotationSource* source) {
	return 0;
}

int32_t _readGyroZ(struct mRotationSource* source) {
	return 0;
}

static void _lightSensorSample(struct GBALuminanceSource* lux) {
	struct mGUIRunnerLux* runnerLux = (struct mGUIRunnerLux*) lux;
	float luxLevel = 0;
	runnerLux->luxLevel = cbrtf(luxLevel) * 8;
}

static uint8_t _lightSensorRead(struct GBALuminanceSource* lux) {
	struct mGUIRunnerLux* runnerLux = (struct mGUIRunnerLux*) lux;
	return 0xFF - runnerLux->luxLevel;
}

static int _batteryState(void) {
	return 0xFF;
}

static void _guiPrepare(void) {
}

int main(int argc, char* argv[]) {
	WHBProcInit();

	WHBLogUdpInit();
	WHBGfxInit();
	VPADInit();
	romfsInit();

	char* mountPath = NULL;

	if (WHBMountSdCard()) {
		size_t len = strlen(WHBGetSdCardMountPath());
		mountPath = malloc(len + 2);
		strcpy(mountPath, WHBGetSdCardMountPath());
		mountPath[len] = '/';
		mountPath[len + 2] = '\0';
		WHBLogPrintf("SD mounted at %s", mountPath);
	} else {
		mountPath = "/";
	}

	struct GUIFont* font = GUIFontCreate();
	rotation.readTiltX = _readTiltX;
	rotation.readTiltY = _readTiltY;
	rotation.readGyroZ = _readGyroZ;

	lightSensor.d.readLuminance = _lightSensorRead;
	lightSensor.d.sample = _lightSensorSample;

	stream.videoDimensionsChanged = NULL;
	stream.postVideoFrame = NULL;
	stream.postAudioFrame = NULL;
	stream.postAudioBuffer = _postAudioBuffer;

	memset(audioBuffer, 0, sizeof(audioBuffer));
	audioBufferActive = 0;
	enqueuedBuffers = 0;
	size_t i;
	for (i = 0; i < N_BUFFERS; ++i) {
/*		audoutBuffer[i].next = NULL;
		audoutBuffer[i].buffer = audioBuffer[i];
		audoutBuffer[i].buffer_size = BUFFER_SIZE;
		audoutBuffer[i].data_size = SAMPLES * 4;
		audoutBuffer[i].data_offset = 0;*/
	}


	bool illuminanceAvailable = false;

	struct mGUIRunner runner = {
		.params = {
			800, 480,
			font, mountPath,
			_drawStart, _drawEnd,
			_pollInput, _pollCursor,
			_batteryState,
			_guiPrepare, NULL,
		},
		.keySources = (struct GUIInputKeys[]) {
			{
				.name = "Wii U GamePad",
				.id = VPAD_INPUT,
				.keyNames = (const char*[]) {
					"A",
					"B",
					"X",
					"Y",
					"L Stick",
					"R Stick",
					"L",
					"R",
					"ZL",
					"ZR",
					"+",
					"-",
					"Left",
					"Up",
					"Right",
					"Down",
					"L Left",
					"L Up",
					"L Right",
					"L Down",
					"R Left",
					"R Up",
					"R Right",
					"R Down"
				},
				.nKeys = 24
			},
			{ .id = 0 }
		},
		.configExtra = (struct GUIMenuItem[]) {
			{
				.title = "Screen mode",
				.data = "screenMode",
				.submenu = 0,
				.state = SM_PA,
				.validStates = (const char*[]) {
					"Pixel-Accurate",
					"Aspect-Ratio Fit",
					"Stretched",
				},
				.nStates = 3
			},
			{
				.title = "Fast forward cap",
				.data = "fastForwardCap",
				.submenu = 0,
				.state = 7,
				.validStates = (const char*[]) {
					"2", "3", "4", "5", "6", "7", "8", "9",
					"10", "11", "12", "13", "14", "15",
					"20", "30"
				},
				.stateMappings = (const struct GUIVariant[]) {
					GUI_V_U(2),
					GUI_V_U(3),
					GUI_V_U(4),
					GUI_V_U(5),
					GUI_V_U(6),
					GUI_V_U(7),
					GUI_V_U(8),
					GUI_V_U(9),
					GUI_V_U(10),
					GUI_V_U(11),
					GUI_V_U(12),
					GUI_V_U(13),
					GUI_V_U(14),
					GUI_V_U(15),
					GUI_V_U(20),
					GUI_V_U(30),
				},
				.nStates = 16
			},
			{
				.title = "Use built-in brightness sensor for Boktai",
				.data = "useLightSensor",
				.submenu = 0,
				.state = illuminanceAvailable,
				.validStates = (const char*[]) {
					"Off",
					"On",
				},
				.nStates = 2
			},
		},
		.nConfigExtra = 3,
		.setup = _setup,
		.teardown = _teardown,
		.gameLoaded = _gameLoaded,
		.gameUnloaded = _gameUnloaded,
		.prepareForFrame = _prepareForFrame,
		.drawFrame = _drawFrame,
		.drawScreenshot = _drawScreenshot,
		.paused = _gameUnloaded,
		.unpaused = _gameLoaded,
		.incrementScreenMode = _incrementScreenMode,
		.setFrameLimiter = _setFrameLimiter,
		.pollGameInput = _pollGameInput,
		.running = _running
	};
	mGUIInit(&runner, "wiiu");

	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_A, GUI_INPUT_SELECT);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_B, GUI_INPUT_BACK);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_X, GUI_INPUT_CANCEL);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_UP, GUI_INPUT_UP);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_DOWN, GUI_INPUT_DOWN);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_LEFT, GUI_INPUT_LEFT);
	_mapKey(&runner.params.keyMap, VPAD_INPUT, VPAD_BUTTON_RIGHT, GUI_INPUT_RIGHT);

	if (argc > 1) {
		size_t i;
		for (i = 0; runner.keySources[i].id; ++i) {
			mInputMapLoad(&runner.params.keyMap, runner.keySources[i].id, mCoreConfigGetInput(&runner.config));
		}
		mGUIRun(&runner, argv[1]);
	} else {
		mGUIRunloop(&runner);
	}
	WHBLogPrint("runloop done");

	mGUIDeinit(&runner);
	WHBLogPrint("deinit GUI");
	GUIFontDestroy(font);
	WHBLogPrint("deinit font");

	WHBUnmountSdCard();

	WHBLogPrint("Exiting");

	VPADShutdown();
	WHBGfxShutdown();
	WHBLogUdpDeinit();

	WHBProcShutdown();

	return 0;
}
