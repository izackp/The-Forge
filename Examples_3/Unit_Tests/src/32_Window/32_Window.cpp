/*
 * Copyright (c) 2018-2021 The Forge Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/


//Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/IOperatingSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"
#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/IResourceLoader.h"

//Math
#include "../../../../Common_3/OS/Math/MathTypes.h"

#include "../../../../Common_3/OS/Interfaces/IMemory.h"

#define COUNT_OF(a) (sizeof(a) / sizeof(a[0]))

const uint32_t gImageCount = 3;

Renderer* pRenderer = NULL;

Queue*   pGraphicsQueue = NULL;
CmdPool* pCmdPools[gImageCount] = { NULL };
Cmd*     pCmds[gImageCount] = { NULL };

SwapChain*    pSwapChain = NULL;
Fence*        pRenderCompleteFences[gImageCount] = {};
Semaphore*    pImageAcquiredSemaphore = NULL;
Semaphore*    pRenderCompleteSemaphores[gImageCount] = {};

Shader* pBasicShader = NULL;
Pipeline* pBasicPipeline = NULL;

Buffer*   pQuadVertexBuffer = NULL;
Buffer*   pQuadIndexBuffer = NULL;

RootSignature* pRootSignature = NULL;
Sampler*       pSampler = NULL;
Texture*       pTexture = NULL;
DescriptorSet* pDescriptorSetTexture = NULL;

uint32_t gFrameIndex = 0;
ProfileToken gGpuProfileToken = PROFILE_INVALID_TOKEN;

enum WindowMode : int32_t
{
	WM_WINDOWED,
	WM_FULLSCREEN,
	WM_BORDERLESS
};

int32_t gWindowMode = WM_WINDOWED;

Timer gHideTimer;

// up to 30 monitors
int32_t gCurRes[30];
int32_t gLastRes[30];

int32_t gWndX;
int32_t gWndY;
int32_t gWndW;
int32_t gWndH;

bool gCursorHidden = false;
int32_t gCursorInsideWindow = 0;
bool gCursorClipped = false;
bool gMinimizeRequested = false;

/// UI
UIApp* pAppUI = NULL;
GuiComponent* pStandaloneControlsGUIWindow = NULL;
static uint32_t gSelectedApiIndex = 0;

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff00ffff, 18);

const char* gTestScripts[]	 = { "TestFullScreen.lua", "TestCenteredWindow.lua", "TestNonCenteredWindow.lua", "TestBorderless.lua", "TestHideWindow.lua" };
uint32_t gScriptIndexes[]	 = { 0, 1, 2, 3, 4 };
uint32_t gCurrentScriptIndex = 0;

IApp* pApp;

static bool ValidateWindowPos(int32_t x, int32_t y)
{
	WindowsDesc* winDesc = pApp->pWindow;
	int clientWidthStart = (getRectWidth(winDesc->windowedRect) - getRectWidth(winDesc->clientRect)) >> 1;
	int clientHeightStart = getRectHeight(winDesc->windowedRect) - getRectHeight(winDesc->clientRect) - clientWidthStart;

	if (winDesc->centered)
	{
		uint32_t fsHalfWidth = getRectWidth(winDesc->fullscreenRect) >> 1;
		uint32_t fsHalfHeight = getRectHeight(winDesc->fullscreenRect) >> 1;
		uint32_t windowWidth = getRectWidth(winDesc->clientRect);
		uint32_t windowHeight = getRectHeight(winDesc->clientRect);
		uint32_t windowHalfWidth = windowWidth >> 1;
		uint32_t windowHalfHeight = windowHeight >> 1;

		int32_t X = fsHalfWidth - windowHalfWidth;
		int32_t Y = fsHalfHeight - windowHalfHeight;

		if ( (abs(winDesc->windowedRect.left + clientWidthStart - X) > 1) || (abs(winDesc->windowedRect.top + clientHeightStart - Y) > 1) )
			return false;
	}
	else
	{
		if ( (abs(x - winDesc->windowedRect.left - clientWidthStart) > 1) || (abs(y - winDesc->windowedRect.top - clientHeightStart) > 1) )
			return false;
	}

	return true;
}

static bool ValidateWindowSize(int32_t width, int32_t height)
{
	RectDesc clientRect = pApp->pWindow->clientRect;
	if ( (abs(getRectWidth(clientRect) - width) > 1) || (abs(getRectHeight(clientRect) - height) > 1) )
		return false;
	return true;
}

static void SetWindowed()
{
	pApp->pWindow->maximized = false;

	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	int32_t x = pApp->pWindow->windowedRect.left + clientWidthStart,
			y = pApp->pWindow->windowedRect.top + clientHeightStart,
			w = getRectWidth(pApp->pWindow->clientRect),
			h = getRectHeight(pApp->pWindow->clientRect);

	if (pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		LOGF(LogLevel::eINFO, "SetWindowed() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetWindowed() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
	}
	if (pApp->pWindow->borderlessWindow)
	{
		toggleBorderless(pApp->pWindow, getRectWidth(pApp->pWindow->clientRect), getRectHeight(pApp->pWindow->clientRect));

		bool centered = pApp->pWindow->centered;
		pApp->pWindow->centered = false;
		LOGF(LogLevel::eINFO, "SetWindowed() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetWindowed() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
		pApp->pWindow->centered = centered;
	}
	gWindowMode = WindowMode::WM_WINDOWED;
}

static void SetFullscreen()
{
	if (!pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		gWindowMode = WindowMode::WM_FULLSCREEN;
	}
}

static void SetBorderless()
{
	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	int32_t x = pApp->pWindow->windowedRect.left + clientWidthStart,
			y = pApp->pWindow->windowedRect.top + clientHeightStart,
			w = getRectWidth(pApp->pWindow->clientRect),
			h = getRectHeight(pApp->pWindow->clientRect);

	if (pApp->pWindow->fullScreen)
	{
		toggleFullscreen(pApp->pWindow);
		gWindowMode = WindowMode::WM_WINDOWED;
		LOGF(LogLevel::eINFO, "SetBorderless() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetBorderless() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
	}
	else
	{
		gWindowMode = WindowMode::WM_BORDERLESS;
		toggleBorderless(pApp->pWindow, getRectWidth(pApp->pWindow->clientRect), getRectHeight(pApp->pWindow->clientRect));
		if(!pApp->pWindow->borderlessWindow)
			gWindowMode = WindowMode::WM_WINDOWED;

		bool centered = pApp->pWindow->centered;
		pApp->pWindow->centered = false;
		LOGF(LogLevel::eINFO, "SetBorderless() Position check: %s", ValidateWindowPos(x, y) ? "SUCCESS" : "FAIL");
		LOGF(LogLevel::eINFO, "SetBorderless() Size check: %s", ValidateWindowSize(w, h) ? "SUCCESS" : "FAIL");
		pApp->pWindow->centered = centered;
	}
}

static void MaximizeWindow()
{
	maximizeWindow(pApp->pWindow);
}

static void MinimizeWindow()
{
	gMinimizeRequested = true;
}

static void HideWindow()
{
	gHideTimer.Reset();
	hideWindow(pApp->pWindow);
}

static void ShowWindow()
{
	showWindow(pApp->pWindow);
}

static void UpdateResolution()
{
	uint32_t monitorCount = getMonitorCount();
	for (uint32_t i = 0; i < monitorCount; ++i)
	{
		if (gCurRes[i] != gLastRes[i])
		{
			MonitorDesc* monitor = getMonitor(i);

			int32_t resIndex = gCurRes[i];
			setResolution(monitor, &monitor->resolutions[resIndex]);

			gLastRes[i] = gCurRes[i];
		}
	}
}

static void MoveWindow()
{
	SetWindowed();
	int clientWidthStart = (getRectWidth(pApp->pWindow->windowedRect) - getRectWidth(pApp->pWindow->clientRect)) >> 1,
		clientHeightStart = getRectHeight(pApp->pWindow->windowedRect) - getRectHeight(pApp->pWindow->clientRect) - clientWidthStart;
	setWindowRect(pApp->pWindow, { gWndX, gWndY, gWndX + gWndW, gWndY + gWndH });
	LOGF(LogLevel::eINFO, "MoveWindow() Position check: %s", ValidateWindowPos(gWndX + clientWidthStart, gWndY + clientHeightStart) ? "SUCCESS" : "FAIL");
	LOGF(LogLevel::eINFO, "MoveWindow() Size check: %s", ValidateWindowSize(gWndW, gWndH) ? "SUCCESS" : "FAIL");
}

static void SetRecommendedWindowSize()
{
	SetWindowed();

	RectDesc rect;
	getRecommendedResolution(&rect);

	setWindowRect(pApp->pWindow, rect);
}

static void HideCursor2Sec()
{
	gCursorHidden = true;
	gHideTimer.Reset();
	hideCursor();
}

static void ToggleClipCursor()
{
	gCursorClipped = !gCursorClipped;
	setEnableCaptureInput(gCursorClipped);
}

static void RunScript()
{
	runAppUITestScript(pAppUI, gTestScripts[gCurrentScriptIndex]);
}

class WindowTest : public IApp
{
public:
	bool Init()
	{
		pApp = this;

		// FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_SOURCES, "Shaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG, RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG, "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES, "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_MESHES, "Meshes");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS, "Fonts");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_ANIMATIONS, "Animation");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS, "Scripts");

		// window and renderer setup
		RendererDesc settings = { 0 };
		settings.mApi = (RendererApi)gSelectedApiIndex;
		initRenderer(GetName(), &settings, &pRenderer);
		//check for init success
		if (!pRenderer)
			return false;

		QueueDesc queueDesc = {};
		queueDesc.mType = QUEUE_TYPE_GRAPHICS;
		queueDesc.mFlag = QUEUE_FLAG_INIT_MICROPROFILE;
		addQueue(pRenderer, &queueDesc, &pGraphicsQueue);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			CmdPoolDesc cmdPoolDesc = {};
			cmdPoolDesc.pQueue = pGraphicsQueue;
			addCmdPool(pRenderer, &cmdPoolDesc, &pCmdPools[i]);
			CmdDesc cmdDesc = {};
			cmdDesc.pPool = pCmdPools[i];
			addCmd(pRenderer, &cmdDesc, &pCmds[i]);

			addFence(pRenderer, &pRenderCompleteFences[i]);
			addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
		}
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);

		initResourceLoaderInterface(pRenderer);

		TextureLoadDesc textureDesc = {};
		textureDesc.pFileName = "skybox/hw_sahara/sahara_bk";
		textureDesc.ppTexture = &pTexture;
		addResource(&textureDesc, NULL);

		ShaderLoadDesc basicShader = {};
		basicShader.mStages[0] = { "basic.vert", NULL, 0 };
		basicShader.mStages[1] = { "basic.frag", NULL, 0 };
		addShader(pRenderer, &basicShader, &pBasicShader);

		SamplerDesc samplerDesc = {};
		samplerDesc.mAddressU = ADDRESS_MODE_REPEAT;
		samplerDesc.mAddressV = ADDRESS_MODE_REPEAT;
		samplerDesc.mAddressW = ADDRESS_MODE_REPEAT;
		samplerDesc.mMinFilter = FILTER_LINEAR;
		samplerDesc.mMagFilter = FILTER_LINEAR;
		samplerDesc.mMipMapMode = MIPMAP_MODE_LINEAR;
		addSampler(pRenderer, &samplerDesc, &pSampler);

		Shader*           shaders[] = { pBasicShader };
		const char*       pStaticSamplers[] = { "Sampler" };
		RootSignatureDesc rootDesc = {};
		rootDesc.mStaticSamplerCount = 1;
		rootDesc.ppStaticSamplerNames = pStaticSamplers;
		rootDesc.ppStaticSamplers = &pSampler;
		rootDesc.mShaderCount = COUNT_OF(shaders);
		rootDesc.ppShaders = shaders;
		addRootSignature(pRenderer, &rootDesc, &pRootSignature);

		DescriptorSetDesc desc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &desc, &pDescriptorSetTexture);

		float vertices[] =
		{
				 1.0f,  1.0f, 0.0f, 1.0f,  1.0f, 0.0f, // 0 top_right 
				-1.0f,  1.0f, 0.0f, 1.0f,  0.0f, 0.0f, // 1 top_left
				-1.0f, -1.0f, 0.0f, 1.0f,  0.0f, 1.0f, // 2 bot_left
				 1.0f, -1.0f, 0.0f, 1.0f,  1.0f, 1.0f, // 3 bot_right
		};

		uint32_t indices[] =
		{
				0, 1, 2, 0, 2, 3
		};

		BufferLoadDesc vbDesc = {};
		vbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		vbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		vbDesc.mDesc.mSize = sizeof(vertices);
		vbDesc.pData = vertices;
		vbDesc.ppBuffer = &pQuadVertexBuffer;
		addResource(&vbDesc, NULL);

		BufferLoadDesc ibDesc = {};
		ibDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_INDEX_BUFFER;
		ibDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		ibDesc.mDesc.mSize = sizeof(indices);
		ibDesc.pData = indices;
		ibDesc.ppBuffer = &pQuadIndexBuffer;
		addResource(&ibDesc, NULL);

		UIAppDesc appUIDesc = {};
		initAppUI(pRenderer, &appUIDesc, &pAppUI);
		if (!pAppUI)
			return false;

		addAppUITestScripts(pAppUI, gTestScripts, COUNT_OF(gTestScripts));
		initAppUIFont(pAppUI, "TitilliumText/TitilliumText-Bold.otf");

		const TextDrawDesc UIPanelWindowTitleTextDesc = { 0, 0xffff00ff, 16 };

		float   dpiScale = getDpiScale().x;
		vec2    UIPosition = { mSettings.mWidth * 0.01f, mSettings.mHeight * 0.30f };
		vec2    UIPanelSize = vec2(1000.f, 1000.f) / dpiScale;
		GuiDesc guiDesc;
		guiDesc.mStartPosition = UIPosition;
		guiDesc.mStartSize = UIPanelSize;
		guiDesc.mDefaultTextDrawDesc = UIPanelWindowTitleTextDesc;
		pStandaloneControlsGUIWindow = addAppUIGuiComponent(pAppUI, "Window", &guiDesc);

#if defined(USE_MULTIPLE_RENDER_APIS)
		static const char* pApiNames[] =
		{
		#if defined(DIRECT3D12)
			"D3D12",
		#endif
		#if defined(VULKAN)
			"Vulkan",
		#endif
		#if defined(DIRECT3D11)
			"D3D11",
		#endif
		};
		// Select Api 
		DropdownWidget selectApiWidget;
		selectApiWidget.pData = &gSelectedApiIndex;
		for (uint32_t i = 0; i < RENDERER_API_COUNT; ++i)
		{
			selectApiWidget.mNames.push_back((char*)pApiNames[i]);
			selectApiWidget.mValues.push_back(i);
		}
		IWidget* pSelectApiWidget = addGuiWidget(pStandaloneControlsGUIWindow, "Select API", &selectApiWidget, WIDGET_TYPE_DROPDOWN);
		pSelectApiWidget->pOnEdited = onAPISwitch;
		addWidgetLua(pSelectApiWidget);
		const char* apiTestScript = "Test_API_Switching.lua";
		addAppUITestScripts(pAppUI, &apiTestScript, 1);
#endif

		RadioButtonWidget rbWindowed;
		rbWindowed.pData = &gWindowMode;
		rbWindowed.mRadioId = WM_WINDOWED;
		IWidget* pWindowed = addGuiWidget(pStandaloneControlsGUIWindow, "Windowed", &rbWindowed, WIDGET_TYPE_RADIO_BUTTON);
		pWindowed->pOnEdited = SetWindowed;
		pWindowed->mDeferred = true;
		addWidgetLua(pWindowed);

		RadioButtonWidget rbFullscreen;
		rbFullscreen.pData = &gWindowMode;
		rbFullscreen.mRadioId = WM_FULLSCREEN;
		IWidget* pFullscreen = addGuiWidget(pStandaloneControlsGUIWindow, "Fullscreen", &rbFullscreen, WIDGET_TYPE_RADIO_BUTTON);
		pFullscreen->pOnEdited = SetFullscreen;
		pFullscreen->mDeferred = true;
		addWidgetLua(pFullscreen);

		RadioButtonWidget rbBorderless;
		rbBorderless.pData = &gWindowMode;
		rbBorderless.mRadioId = WM_BORDERLESS;
		IWidget* pBorderless = addGuiWidget(pStandaloneControlsGUIWindow, "Borderless", &rbBorderless, WIDGET_TYPE_RADIO_BUTTON);
		pBorderless->pOnEdited = SetBorderless;
		pBorderless->mDeferred = true;
		addWidgetLua(pBorderless);

		ButtonWidget bMaximize;
		IWidget* pMaximize = addGuiWidget(pStandaloneControlsGUIWindow, "Maximize", &bMaximize, WIDGET_TYPE_BUTTON);
		pMaximize->pOnEdited = MaximizeWindow;
		pMaximize->mDeferred = true;
		addWidgetLua(pMaximize);

		ButtonWidget bMinimize;
		IWidget* pMinimize = addGuiWidget(pStandaloneControlsGUIWindow, "Minimize", &bMinimize, WIDGET_TYPE_BUTTON);
		pMinimize->pOnEdited = MinimizeWindow;
		pMinimize->mDeferred = true;
		addWidgetLua(pMinimize);

		ButtonWidget bHide;
		IWidget* pHide = addGuiWidget(pStandaloneControlsGUIWindow, "Hide for 2s", &bHide, WIDGET_TYPE_BUTTON);
		pHide->pOnEdited = HideWindow;
		addWidgetLua(pHide);

		CheckboxWidget rbCentered;
		rbCentered.pData = &(pApp->pWindow->centered);
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Centered", &rbCentered, WIDGET_TYPE_CHECKBOX));

		RectDesc recRes;
		getRecommendedResolution(&recRes);

		uint32_t recWidth = recRes.right - recRes.left;
		uint32_t recHeight = recRes.bottom - recRes.top;

		SliderIntWidget setRectSliderX;
		setRectSliderX.pData = &gWndX;
		setRectSliderX.mMin = 0;
		setRectSliderX.mMax = recWidth;
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "x", &setRectSliderX, WIDGET_TYPE_SLIDER_INT));

		SliderIntWidget setRectSliderY;
		setRectSliderY.pData = &gWndY;
		setRectSliderY.mMin = 0;
		setRectSliderY.mMax = recHeight;
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "y", &setRectSliderY, WIDGET_TYPE_SLIDER_INT));

		SliderIntWidget setRectSliderW;
		setRectSliderW.pData = &gWndW;
		setRectSliderW.mMin = 1;
		setRectSliderW.mMax = recWidth;
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "w", &setRectSliderW, WIDGET_TYPE_SLIDER_INT));

		SliderIntWidget setRectSliderH;
		setRectSliderH.pData = &gWndH;
		setRectSliderH.mMin = 1;
		setRectSliderH.mMax = recHeight;
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "h", &setRectSliderH, WIDGET_TYPE_SLIDER_INT));

		ButtonWidget bSetRect;
		IWidget* pSetRect = addGuiWidget(pStandaloneControlsGUIWindow, "Set window rectangle", &bSetRect, WIDGET_TYPE_BUTTON);
		pSetRect->pOnEdited = MoveWindow;
		pSetRect->mDeferred = true;
		addWidgetLua(pSetRect);

		ButtonWidget bRecWndSize;
		IWidget* pRecWndSize = addGuiWidget(pStandaloneControlsGUIWindow, "Set recommended window rectangle", &bRecWndSize, WIDGET_TYPE_BUTTON);
		pRecWndSize->pOnEdited = SetRecommendedWindowSize;
		pRecWndSize->mDeferred = true;
		addWidgetLua(pRecWndSize);

		uint32_t numMonitors = getMonitorCount();

		char label[64];
		sprintf(label, "Number of displays: ");

		char monitors[10];
		sprintf(monitors, "%u", numMonitors);
		strcat(label, monitors);

		LabelWidget labelWidget;
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, label, &labelWidget, WIDGET_TYPE_LABEL));

		for (uint32_t i = 0; i < numMonitors; ++i)
		{
			MonitorDesc* monitor = getMonitor(i);

			char publicDisplayName[128];
#if defined(_WINDOWS) || defined(XBOX) // Win platform uses wide chars			
			if (128 == wcstombs(publicDisplayName, monitor->publicDisplayName, sizeof(publicDisplayName)))
				publicDisplayName[127] = '\0';
#else
			strcpy(publicDisplayName, monitor->publicDisplayName);
#endif
			char monitorLabel[128];
			sprintf(monitorLabel, "%s", publicDisplayName);
			strcat(monitorLabel, " (");

			char buffer[10];
			sprintf(buffer, "%u", monitor->physicalSize.x);
			strcat(monitorLabel, buffer);
			strcat(monitorLabel, "x");

			sprintf(buffer, "%u", monitor->physicalSize.y);
			strcat(monitorLabel, buffer);
			strcat(monitorLabel, " mm; ");

			sprintf(buffer, "%u", monitor->dpi.x);
			strcat(monitorLabel, buffer);
			strcat(monitorLabel, " dpi; ");

			sprintf(buffer, "%u", monitor->resolutionCount);
			strcat(monitorLabel, buffer);
			strcat(monitorLabel, " resolutions)");

			CollapsingHeaderWidget monitorHeader;

			for (uint32_t j = 0; j < monitor->resolutionCount; ++j)
			{
				Resolution res = monitor->resolutions[j];

				char strRes[64];
				sprintf(strRes, "%u", res.mWidth);
				strcat(strRes, "x");

				char height[10];
				sprintf(height, "%u", res.mHeight);
				strcat(strRes, height);

				if (monitor->defaultResolution.mWidth == res.mWidth &&
					monitor->defaultResolution.mHeight == res.mHeight)
				{
					strcat(strRes, " (native)");
					gCurRes[i] = j;
					gLastRes[i] = j;
				}

				RadioButtonWidget rbRes;
				rbRes.pData = &gCurRes[i];
				rbRes.mRadioId = j;
				IWidget* pRbRes = addCollapsingHeaderSubWidget(&monitorHeader, strRes, &rbRes, WIDGET_TYPE_RADIO_BUTTON);
				pRbRes->pOnEdited = UpdateResolution;
				pRbRes->mDeferred = false;
			}

			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, monitorLabel, &monitorHeader, WIDGET_TYPE_COLLAPSING_HEADER));
		}

		CollapsingHeaderWidget InputCotrolsWidget;

		ButtonWidget bHideCursor;
		IWidget* pHideCursor = addCollapsingHeaderSubWidget(&InputCotrolsWidget, "Hide Cursor for 2s", &bHideCursor, WIDGET_TYPE_BUTTON);
		pHideCursor->pOnEdited = HideCursor2Sec;

		LabelWidget lCursorInWindow;
		addCollapsingHeaderSubWidget(&InputCotrolsWidget, "Cursor inside window?", &lCursorInWindow, WIDGET_TYPE_LABEL);

		RadioButtonWidget rCursorInsideRectFalse;
		rCursorInsideRectFalse.pData = &gCursorInsideWindow;
		rCursorInsideRectFalse.mRadioId = 0;
		addCollapsingHeaderSubWidget(&InputCotrolsWidget, "No", &rCursorInsideRectFalse, WIDGET_TYPE_RADIO_BUTTON);

		RadioButtonWidget rCursorInsideRectTrue;
		rCursorInsideRectTrue.pData = &gCursorInsideWindow;
		rCursorInsideRectTrue.mRadioId = 1;
		addCollapsingHeaderSubWidget(&InputCotrolsWidget, "Yes", &rCursorInsideRectTrue, WIDGET_TYPE_RADIO_BUTTON);

		ButtonWidget bClipCursor;
		IWidget* pClipCursor = addCollapsingHeaderSubWidget(&InputCotrolsWidget, "Clip Cursor to Window", &bClipCursor, WIDGET_TYPE_BUTTON);
		pClipCursor->pOnEdited = ToggleClipCursor;

		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Cursor", &InputCotrolsWidget, WIDGET_TYPE_COLLAPSING_HEADER));

		DropdownWidget ddTestScripts;
		ddTestScripts.pData = &gCurrentScriptIndex;
		for (uint32_t i = 0; i < COUNT_OF(gTestScripts); ++i)
		{
			ddTestScripts.mNames.push_back((char*)gTestScripts[i]);
			ddTestScripts.mValues.push_back(gScriptIndexes[i]);
		}
		addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Test Scripts", &ddTestScripts, WIDGET_TYPE_DROPDOWN));

		ButtonWidget bRunScript;
		IWidget* pRunScript = addGuiWidget(pStandaloneControlsGUIWindow, "Run", &bRunScript, WIDGET_TYPE_BUTTON);
		pRunScript->pOnEdited = RunScript;
		addWidgetLua(pRunScript);

		// Initialize microprofiler and it's UI.
		initProfiler();
		initProfilerUI(pAppUI, mSettings.mWidth, mSettings.mHeight);

		// Gpu profiler can only be added after initProfile.
		gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");
		waitForAllResourceLoads();

		if (!initInputSystem(pWindow))
			return false;

		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_DUMP, [](InputActionContext* ctx) {  dumpProfileData(((Renderer*)ctx->pUserData), ((Renderer*)ctx->pUserData)->pName); return true; }, pRenderer };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx)
				{
						if (gWindowMode == WM_FULLSCREEN || gWindowMode == WM_BORDERLESS)
						{
								gWindowMode = WM_WINDOWED;
								SetWindowed();
						}
						else if (gWindowMode == WM_WINDOWED)
						{
								gWindowMode = WM_FULLSCREEN;
								SetFullscreen();
						}
						return true;
				}, this };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
		addInputAction(&actionDesc);
		actionDesc =
		{
				InputBindings::BUTTON_ANY, [](InputActionContext* ctx)
				{
						bool capture = appUIOnButton(pAppUI, ctx->mBinding, ctx->mBool, ctx->pPosition);
						setEnableCaptureInput(capture && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);
						return true;
				}, this
		};
		addInputAction(&actionDesc);

		gFrameIndex = 0; 

		return true;
	}

	void Exit()
	{
		exitInputSystem();

		exitProfilerUI();

		exitProfiler();

		exitAppUI(pAppUI);

		removeDescriptorSet(pRenderer, pDescriptorSetTexture);

		removeResource(pQuadVertexBuffer);
		removeResource(pQuadIndexBuffer);
		removeResource(pTexture);

		removeSampler(pRenderer, pSampler);
		removeShader(pRenderer, pBasicShader);
		removeRootSignature(pRenderer, pRootSignature);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pRenderCompleteFences[i]);
			removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);

			removeCmd(pRenderer, pCmds[i]);
			removeCmdPool(pRenderer, pCmdPools[i]);
		}
		removeSemaphore(pRenderer, pImageAcquiredSemaphore);

		exitResourceLoaderInterface(pRenderer);
		removeQueue(pRenderer, pGraphicsQueue);
		exitRenderer(pRenderer);
	}

	bool Load()
	{
		if (!addSwapChain())
			return false;

		if (!addAppGUIDriver(pAppUI, pSwapChain->ppRenderTargets))
			return false;

		//layout and pipeline for sphere draw
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_TEXCOORD0;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 4 * sizeof(float);

		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_BACK;

		DepthStateDesc depthStateDesc = {};

		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pShaderProgram = pBasicShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		addPipeline(pRenderer, &desc, &pBasicPipeline);

		// Prepare descriptor sets
		DescriptorData params[2] = {};
		params[0].pName = "Texture";
		params[0].ppTextures = &pTexture;
		updateDescriptorSet(pRenderer, 0, pDescriptorSetTexture, 1, params);

		waitForAllResourceLoads();

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pGraphicsQueue);

		removeAppGUIDriver(pAppUI);

		removePipeline(pRenderer, pBasicPipeline);
		removeSwapChain(pRenderer, pSwapChain);
	}

	void Update(float deltaTime)
	{
		updateInputSystem(mSettings.mWidth, mSettings.mHeight);

		if (pWindow->hide || gCursorHidden)
		{
			unsigned msec = gHideTimer.GetMSec(false);
			if (msec >= 2000)
			{
				ShowWindow();
				showCursor();
				gCursorHidden = false;
			}
		}

		gCursorInsideWindow = isCursorInsideTrackingArea();

		updateAppUI(pAppUI, deltaTime);
	}

	void Draw()
	{
		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		Semaphore*    pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
		Fence*        pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];

		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pRenderCompleteFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pRenderCompleteFence);

		// Reset cmd pool for this frame
		resetCmdPool(pRenderer, pCmdPools[gFrameIndex]);

		Cmd* cmd = pCmds[gFrameIndex];
		beginCmd(cmd);

		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		const uint32_t stride = 6 * sizeof(float);

		RenderTargetBarrier barriers[2];

		barriers[0] = { pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0].r = 0.0f;
		loadActions.mClearColorValues[0].g = 0.0f;
		loadActions.mClearColorValues[0].b = 0.0f;
		loadActions.mClearColorValues[0].a = 0.0f;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Basic Draw");
		cmdBindPipeline(cmd, pBasicPipeline);
		cmdBindDescriptorSet(cmd, 0, pDescriptorSetTexture);
		cmdBindVertexBuffer(cmd, 1, &pQuadVertexBuffer, &stride, NULL);
		cmdBindIndexBuffer(cmd, pQuadIndexBuffer, INDEX_TYPE_UINT32, 0);
		cmdDrawIndexed(cmd, 6, 0, 0);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdBeginGpuTimestampQuery(cmd, gGpuProfileToken, "Draw UI");

		const float txtIndent = 8.f;
		float2 txtSizePx = cmdDrawCpuProfile(cmd, float2(txtIndent, 15.f), &gFrameTimeDraw);
		cmdDrawGpuProfile(cmd, float2(txtIndent, txtSizePx.y + 30.f), gGpuProfileToken, &gFrameTimeDraw);

		cmdDrawProfilerUI();

		appUIGui(pAppUI, pStandaloneControlsGUIWindow);
		drawAppUI(pAppUI, cmd);
		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndGpuTimestampQuery(cmd, gGpuProfileToken);

		barriers[0] = { pRenderTarget, RESOURCE_STATE_RENDER_TARGET, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		cmdEndGpuFrameProfile(cmd, gGpuProfileToken);
		endCmd(cmd);

		QueueSubmitDesc submitDesc = {};
		submitDesc.mCmdCount = 1;
		submitDesc.mSignalSemaphoreCount = 1;
		submitDesc.mWaitSemaphoreCount = 1;
		submitDesc.ppCmds = &cmd;
		submitDesc.ppSignalSemaphores = &pRenderCompleteSemaphore;
		submitDesc.ppWaitSemaphores = &pImageAcquiredSemaphore;
		submitDesc.pSignalFence = pRenderCompleteFence;
		queueSubmit(pGraphicsQueue, &submitDesc);

		QueuePresentDesc presentDesc = {};
		presentDesc.mIndex = swapchainImageIndex;
		presentDesc.mWaitSemaphoreCount = 1;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
		presentDesc.mSubmitDone = true;
		queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gImageCount;

		if (gMinimizeRequested)
		{
			minimizeWindow(pApp->pWindow);
			gMinimizeRequested = false;
		}
	}

	const char* GetName() { return "32_Window"; }

	bool addSwapChain()
	{
		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mWindowHandle = pWindow->handle;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.ppPresentQueues = &pGraphicsQueue;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = gImageCount;
		swapChainDesc.mColorFormat = getRecommendedSwapchainFormat(true);
		swapChainDesc.mEnableVsync = mSettings.mDefaultVSyncEnabled;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}
};
DEFINE_APPLICATION_MAIN(WindowTest)

