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
#include <coreinit/foreground.h>
#include <coreinit/thread.h>
#include <proc_ui/procui.h>
#include <vpad/input.h>

#include <whb/file.h>
#include <whb/sdcard.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_udp.h>
#include <whb/log_cafe.h>
#include <whb/crash.h>

#include <romfs-wiiu.h>

#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2r/draw.h>
#include <vpad/input.h>

#include "wiiu-gpu.h"

#define VPAD_INPUT 0x7f87ffff

#define SAMPLES 0x200
#define BUFFER_SIZE 0x1000
#define N_BUFFERS 4
#define mainANALOG_DEADZONE 0x4000

static struct mAVStream stream;
static struct mRotationSource rotation = {0};
static int audioBufferActive;
static struct GBAStereoSample audioBuffer[N_BUFFERS][SAMPLES] __attribute__((__aligned__(0x1000)));
static int enqueuedBuffers;
static bool frameLimiter = true;
static unsigned framecount = 0;
static unsigned framecap = 10;
static uint32_t vwidth = 1920;
static uint32_t vheight = 1080;
static uint32_t drcwidth = 854;
static uint32_t drcheight = 480;
bool sgbCrop = false;
static bool running = true;

extern bool sgbCrop;
GX2Texture coreTexture = {0};
GX2RBuffer offsetBuffer = {0};
GX2UniformVar* dimsUniform;
GX2UniformVar* insizeUniform;
GX2UniformVar* colorUniform;
GX2Sampler coreSampler;
WHBGfxShaderGroup shaderGroup;

#define TEX_W 256
#define TEX_H 224

color_t* outputBuffer;

static const float _offsets[] = {
	0.f, 0.f,
	1.f, 0.f,
	1.f, 1.f,
	0.f, 1.f,
};

enum ScreenMode {
	SM_PA,
	SM_AF,
	SM_SF,
	SM_MAX
} screenMode = SM_PA;

static void _mapKey(struct mInputMap* map, uint32_t binding, int nativeKey, enum GBAKey key) {
	mInputBindKey(map, binding, __builtin_ctz(nativeKey), key);
}

static void _drawStart(void) {
	if (!running)
		return;
	WHBLogPrint("drawstart");

	WHBGfxBeginRenderTV();
	WHBGfxClearColor(1.0, 0.0, 0.0, 0.0);
	WHBGfxBeginRenderDRC();
	WHBGfxClearColor(0.0, 0.0, 0.0, 0.0);
}

static void _drawEnd(void) {
	if (!running)
		return;

	WHBGfxFinishRenderTV();
	WHBGfxFinishRenderDRC();
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


static void _initGpu() {
	WHBGfxInit();
	gpuInitShaderGroup(&shaderGroup, "romfs:/main.gsh");

	WHBGfxInitShaderAttribute(&shaderGroup, "offset", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	WHBGfxInitFetchShader(&shaderGroup);
	dimsUniform = GX2GetVertexUniformVar(shaderGroup.vertexShader, "dims");
	insizeUniform = GX2GetVertexUniformVar(shaderGroup.vertexShader, "insize");
	colorUniform = GX2GetPixelUniformVar(shaderGroup.pixelShader, "color");

	gpuInitBuffer(&offsetBuffer, 2 * sizeof(float), 4);
	gpuInitTexture(&coreTexture, TEX_W, TEX_H, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
	GX2InitSampler(&coreSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
}

static void _setup(struct mGUIRunner* runner) {
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

//	runner->core->setAVStream(runner->core, &stream);

	unsigned mode;
	if (mCoreConfigGetUIntValue(&runner->config, "screenMode", &mode) && mode < SM_MAX) {
		screenMode = mode;
	}

	outputBuffer = calloc(TEX_W * TEX_H, sizeof(uint32_t));

	runner->core->setAudioBufferSize(runner->core, SAMPLES);
	runner->core->setVideoBuffer(runner->core, outputBuffer, TEX_W);
}

static void _teardown(struct mGUIRunner* runner) {
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
	if (mCoreConfigGetIntValue(&runner->config, "sgb.borderCrop", &fakeBool)) {
		sgbCrop = fakeBool;
	}

	int scale;
	if (mCoreConfigGetUIntValue(&runner->config, "videoScale", &scale)) {
		runner->core->reloadConfigOption(runner->core, "videoScale", &runner->config);
	}
}

static void _gameUnloaded(struct mGUIRunner* runner) {
}

static void _prepareForFrame(struct mGUIRunner* runner) {
	if (!running)
		return;
	WHBLogPrint("prepareForFrame");

	gpuUpdateTexture(&coreTexture, outputBuffer, sizeof(uint32_t));
}

void _drawTex(struct mGUIRunner* runner, unsigned width, unsigned height, bool faded, bool blendTop, bool drc) {
	float inwidth = width;
	float inheight = height;
	float screenWidth = vwidth;
	float screenHeight = vheight;

	if (drc) {
		screenWidth = drcwidth;
		screenHeight = drcheight; }

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

	uint32_t cwidth, cheight;
	runner->core->desiredVideoDimensions(runner->core, &cwidth, &cheight);

	float insize[] = { 1.0 / TEX_W * cwidth, 1.0 / TEX_H * cheight };
	float dims[] = { aspectX, aspectY };
	float color[] = {1.0f, 1.0f, 1.0f, 1.0f};
	float fadedColor[] = {0.5f, 0.5f, 0.5f, 1.0f};

	void* buffer = GX2RLockBufferEx(&offsetBuffer, 0);
	memcpy(buffer, _offsets, offsetBuffer.elemSize * 4);
	GX2RUnlockBufferEx(&offsetBuffer, 0);

	gpuSetShaders(&shaderGroup);
	GX2RSetAttributeBuffer(&offsetBuffer, 0, offsetBuffer.elemSize, 0);
	GX2SetVertexUniformReg(dimsUniform->offset, 2, &dims);
	GX2SetVertexUniformReg(insizeUniform->offset, 2, &insize);
	if (faded) {
		GX2SetPixelUniformReg(colorUniform->offset, 4, &fadedColor);
	} else {
		GX2SetPixelUniformReg(colorUniform->offset, 4, &color);
	}
	GX2SetPixelTexture(&coreTexture, 0);
	GX2SetPixelSampler(&coreSampler, 0);
	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
}

static void _drawFrame(struct mGUIRunner* runner, bool faded) {
	if (!running)
		return;
	WHBLogPrint("drawFrame");

	++framecount;
	if (!frameLimiter && framecount < framecap) {
		return;
	}

	unsigned width, height;
	runner->core->desiredVideoDimensions(runner->core, &width, &height);

	WHBGfxBeginRenderTV();
	_drawTex(runner, width, height, faded, false, false);

	WHBGfxBeginRenderDRC();
	_drawTex(runner, width, height, faded, false, true);
}

static void _drawScreenshot(struct mGUIRunner* runner, const color_t* pixels, unsigned width, unsigned height, bool faded) {
	WHBLogPrint("drawScreenshot");
	if (!running)
		return;

	WHBGfxBeginRenderTV();
	_drawTex(runner, width, height, faded, false, false);
	WHBGfxFinishRenderTV();
	_drawTex(runner, width, height, faded, false, true);
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
	ProcUIStatus status = ProcUIProcessMessages(true);

	switch (status) {
		case PROCUI_STATUS_RELEASE_FOREGROUND:
		    ProcUIDrawDoneRelease();
		    break;
	    case PROCUI_STATUS_EXITING:
		    running = false;
		    ProcUIShutdown();
		    return false;
	    case PROCUI_STATUS_IN_FOREGROUND:
	    case PROCUI_STATUS_IN_BACKGROUND:
		    break;
	}

	return true;
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

static int _batteryState(void) {
	return 0xFF;
}

static void _guiPrepare(void) {
	WHBLogPrint("guiPrepare");
	WHBGfxBeginRender();
}

static void _guiFinish(void) {
	WHBLogPrint("guifinish");

/*	WHBGfxFinishRenderTV();
	WHBGfxFinishRenderDRC();
	WHBGfxFinishRender();*/
}

void save_callback() {
	OSSavesDone_ReadyToRelease();
}

int main(int argc, char* argv[]) {
	ProcUIInit(save_callback);

	WHBLogCafeInit();
//	WHBLogUdpInit();
	VPADInit();
	romfsInit();
	WHBInitCrashHandler();

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

	_initGpu();
	WHBLogPrint("Init GPU");

	struct GUIFont* font = GUIFontCreate();

	stream.videoDimensionsChanged = NULL;
	stream.postVideoFrame = NULL;
	stream.postAudioFrame = NULL;
	stream.postAudioBuffer = _postAudioBuffer;

	memset(audioBuffer, 0, sizeof(audioBuffer));
	audioBufferActive = 0;
	enqueuedBuffers = 0;

	struct mGUIRunner runner = {
		.params = {
			1280, 720,
			font, mountPath,
			_drawStart, _drawEnd,
			_pollInput, _pollCursor,
			_batteryState,
			_guiPrepare, _guiFinish,
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
		},
		.nConfigExtra = 2,
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

	WHBLogPrint("starting runner");
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
	WHBLogCafeDeinit();
	WHBLogUdpDeinit();

	return 0;
}
