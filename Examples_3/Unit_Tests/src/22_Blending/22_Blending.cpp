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

/********************************************************************************************************
*
* The Forge - ANIMATION - BLENDING UNIT TEST
*
* The purpose of this demo is to show how to blend multiple clips using the
* animnation middleware and play them back on a rig
*
*********************************************************************************************************/

// Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"

// Rendering
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/IResourceLoader.h"

// Middleware packages
#include "../../../../Middleware_3/Animation/SkeletonBatcher.h"
#include "../../../../Middleware_3/Animation/AnimatedObject.h"
#include "../../../../Middleware_3/Animation/Animation.h"
#include "../../../../Middleware_3/Animation/Clip.h"
#include "../../../../Middleware_3/Animation/ClipController.h"
#include "../../../../Middleware_3/Animation/Rig.h"

#include "../../../../Middleware_3/UI/AppUI.h"
// tiny stl
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/vector.h"

// Math
#include "../../../../Common_3/OS/Math/MathTypes.h"

// Memory
#include "../../../../Common_3/OS/Interfaces/IMemory.h"

//--------------------------------------------------------------------------------------------
// RENDERING PIPELINE DATA
//--------------------------------------------------------------------------------------------
const uint32_t gImageCount = 3;
ProfileToken   gGpuProfileToken;

uint32_t       gFrameIndex = 0;
Renderer*      pRenderer = NULL;

Queue*   pGraphicsQueue = NULL;
CmdPool* pCmdPools[gImageCount];
Cmd*     pCmds[gImageCount];

SwapChain*    pSwapChain = NULL;
RenderTarget* pDepthBuffer = NULL;
Fence*        pRenderCompleteFences[gImageCount] = { NULL };
Semaphore*    pImageAcquiredSemaphore = NULL;
Semaphore*    pRenderCompleteSemaphores[gImageCount] = { NULL };

VirtualJoystickUI* pVirtualJoystick = NULL;

Shader*   pSkeletonShader = NULL;
Buffer*   pJointVertexBuffer = NULL;
Buffer*   pBoneVertexBuffer = NULL;
Pipeline* pSkeletonPipeline = NULL;
int       gNumberOfJointPoints;
int       gNumberOfBonePoints;

Shader*        pPlaneDrawShader = NULL;
Buffer*        pPlaneVertexBuffer = NULL;
Pipeline*      pPlaneDrawPipeline = NULL;
RootSignature* pRootSignature = NULL;
DescriptorSet* pDescriptorSet = NULL;

struct UniformBlockPlane
{
	mat4 mProjectView;
	mat4 mToWorldMat;
};
UniformBlockPlane gUniformDataPlane;

Buffer* pPlaneUniformBuffer[gImageCount] = { NULL };

//--------------------------------------------------------------------------------------------
// CAMERA CONTROLLER & SYSTEMS (File/Log/UI)
//--------------------------------------------------------------------------------------------

ICameraController* pCameraController = NULL;
UIApp*        pAppUI = NULL;
GuiComponent* pStandaloneControlsGUIWindow = NULL;
static uint32_t	gSelectedApiIndex = 0;

TextDrawDesc gFrameTimeDraw = TextDrawDesc(0, 0xff00ffff, 18);

//--------------------------------------------------------------------------------------------
// ANIMATION DATA
//--------------------------------------------------------------------------------------------

// AnimatedObjects
AnimatedObject gStickFigureAnimObject;

// Animations
Animation gBlendedAnimation;

// Clip Controllers
ClipController gWalkClipController;
ClipController gJogClipController;
ClipController gRunClipController;

// Clips
Clip gWalkClip;
Clip gJogClip;
Clip gRunClip;

// Rigs
Rig gStickFigureRig;

// SkeletonBatcher
SkeletonBatcher gSkeletonBatcher;

// Filenames
const char* gStickFigureName = "stickFigure/skeleton.ozz";
const char* gWalkClipName = "stickFigure/animations/walk.ozz";
const char* gJogClipName = "stickFigure/animations/jog.ozz";
const char* gRunClipName = "stickFigure/animations/run.ozz";

float* pBonePoints = 0;
float* pJointPoints = 0;

const int   gSphereResolution = 30;                   // Increase for higher resolution joint spheres
const float gBoneWidthRatio = 0.2f;                   // Determines how far along the bone to put the max width [0,1]
const float gJointRadius = gBoneWidthRatio * 0.5f;    // set to replicate Ozz skeleton

// Timer to get animationsystem update time
static HiresTimer gAnimationUpdateTimer;

//--------------------------------------------------------------------------------------------
// UI DATA
//--------------------------------------------------------------------------------------------
struct UIData
{
	struct BlendParamsData
	{
		float* mBlendRatio;
		bool*  mAutoSetBlendParams;
		float* mWalkClipWeight;
		float* mJogClipWeight;
		float* mRunClipWeight;
		float* mThreshold;
	};
	BlendParamsData mBlendParams;

	// To preserve effects of BlendRatio, mPlay and mLoop will default as true and
	// can only get set if mAutoBlendParams is off
	struct ClipData
	{
		bool   mPlay = true;
		bool   mLoop = true;
		float  mAnimationTime = 0.0f;
		float* mPlaybackSpeed = NULL;
	};
	ClipData mWalkClip;
	ClipData mJogClip;
	ClipData mRunClip;

	struct GeneralSettingsData
	{
		bool mShowBindPose = false;
		bool mDrawPlane = true;
	};
	GeneralSettingsData mGeneralSettings;
};
UIData gUIData;

// For this sample mPlay and mLoop must have a callback as a layer of separation
// so that we can control if they can be set by the GUI or not
void WalkClipPlayCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gWalkClipController.SetPlay(gUIData.mWalkClip.mPlay);
	}
	else
	{
		gUIData.mWalkClip.mPlay = true;
	}
}
void WalkClipLoopCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gWalkClipController.SetLoop(gUIData.mWalkClip.mLoop);
	}
	else
	{
		gUIData.mWalkClip.mLoop = true;
	}
}
void JogClipPlayCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gJogClipController.SetPlay(gUIData.mJogClip.mPlay);
	}
	else
	{
		gUIData.mJogClip.mPlay = true;
	}
}
void JogClipLoopCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gJogClipController.SetLoop(gUIData.mJogClip.mLoop);
	}
	else
	{
		gUIData.mJogClip.mLoop = true;
	}
}
void RunClipPlayCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gRunClipController.SetPlay(gUIData.mRunClip.mPlay);
	}
	else
	{
		gUIData.mRunClip.mPlay = true;
	}
}
void RunClipLoopCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gRunClipController.SetLoop(gUIData.mRunClip.mLoop);
	}
	else
	{
		gUIData.mRunClip.mLoop = true;
	}
}

// Hard set the controller's time ratio via callback when it is set in the UI
void WalkClipTimeChangeCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gWalkClipController.SetTimeRatioHard(gUIData.mWalkClip.mAnimationTime);
		gUIData.mWalkClip.mPlay = false;
	}
}
void JogClipTimeChangeCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gJogClipController.SetTimeRatioHard(gUIData.mJogClip.mAnimationTime);
		gUIData.mJogClip.mPlay = false;
	}
}
void RunClipTimeChangeCallback()
{
	if (!*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		gRunClipController.SetTimeRatioHard(gUIData.mRunClip.mAnimationTime);
		gUIData.mRunClip.mPlay = false;
	}
}

// When mAutoSetBlendParams is turned on we need to reset the clip controllers
void AutoSetBlendParamsCallback()
{
	if (*gUIData.mBlendParams.mAutoSetBlendParams)
	{
		// Reset the internal values
		gWalkClipController.Reset();
		gWalkClipController.SetLoop(true);
		gJogClipController.Reset();
		gJogClipController.SetLoop(true);
		gRunClipController.Reset();
		gRunClipController.SetLoop(true);

		// Reset the UI values
		gUIData.mWalkClip.mPlay = true;
		gUIData.mJogClip.mPlay = true;
		gUIData.mRunClip.mPlay = true;
		gUIData.mWalkClip.mLoop = true;
		gUIData.mJogClip.mLoop = true;
		gUIData.mRunClip.mLoop = true;
	}
}

const char* gTestScripts[] = { "Test.lua" };
uint32_t gScriptIndexes[] = { 0 };
uint32_t gCurrentScriptIndex = 0;
void RunScript()
{
	runAppUITestScript(pAppUI, gTestScripts[gCurrentScriptIndex]);
}

//--------------------------------------------------------------------------------------------
// APP CODE
//--------------------------------------------------------------------------------------------
class Blending: public IApp
{
	public:
	bool Init()
	{
        // FILE PATHS
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SHADER_SOURCES,  "Shaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_DEBUG,   RD_SHADER_BINARIES, "CompiledShaders");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_GPU_CONFIG,      "GPUCfg");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_TEXTURES,        "Textures");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_MESHES,          "Meshes");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_FONTS,           "Fonts");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_ANIMATIONS,      "Animation");
		fsSetPathForResourceDir(pSystemFileIO, RM_CONTENT, RD_SCRIPTS,		   "Scripts");

		// GENERATE VERTEX BUFFERS
		//

		// Generate joint vertex buffer
		generateSpherePoints(&pJointPoints, &gNumberOfJointPoints, gSphereResolution, gJointRadius);



		// Generate bone vertex buffer
		generateBonePoints(&pBonePoints, &gNumberOfBonePoints, gBoneWidthRatio);

		/************************************************************************/
		// SETUP ANIMATION STRUCTURES
		/************************************************************************/

		// RIGS
		// Initialize the rig with the path to its ozz file
		gStickFigureRig.Initialize(RD_ANIMATIONS, gStickFigureName);

		// CLIPS
		//
		gWalkClip.Initialize(RD_ANIMATIONS, gWalkClipName, &gStickFigureRig);

		gJogClip.Initialize(RD_ANIMATIONS, gJogClipName, &gStickFigureRig);

		gRunClip.Initialize(RD_ANIMATIONS, gRunClipName, &gStickFigureRig);

		// CLIP CONTROLLERS
		//

		// Initialize with the length of the animation they are controlling and an
		// optional external time to set based on their updating
		gWalkClipController.Initialize(gWalkClip.GetDuration(), &gUIData.mWalkClip.mAnimationTime);
		gJogClipController.Initialize(gJogClip.GetDuration(), &gUIData.mJogClip.mAnimationTime);
		gRunClipController.Initialize(gRunClip.GetDuration(), &gUIData.mRunClip.mAnimationTime);

		// ANIMATIONS
		//

		// Set up the description of how these clips parameters will be auto blended
		AnimationDesc animationDesc{};
		animationDesc.mRig = &gStickFigureRig;
		animationDesc.mNumLayers = 3;

		animationDesc.mLayerProperties[0].mClip = &gWalkClip;
		animationDesc.mLayerProperties[0].mClipController = &gWalkClipController;

		animationDesc.mLayerProperties[1].mClip = &gJogClip;
		animationDesc.mLayerProperties[1].mClipController = &gJogClipController;

		animationDesc.mLayerProperties[2].mClip = &gRunClip;
		animationDesc.mLayerProperties[2].mClipController = &gRunClipController;

		animationDesc.mBlendType = BlendType::CROSS_DISSOLVE_SYNC;

		gBlendedAnimation.Initialize(animationDesc);

		// ANIMATED OBJECTS
		//
		gStickFigureAnimObject.Initialize(&gStickFigureRig, &gBlendedAnimation);

		// WINDOW AND RENDERER SETUP
		//
		RendererDesc settings = { 0 };
		settings.mApi = (RendererApi)gSelectedApiIndex;
		initRenderer(GetName(), &settings, &pRenderer);
		if (!pRenderer)    //check for init success
			return false;

		// CREATE COMMAND LIST AND GRAPHICS/COMPUTE QUEUES
		//
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
		}

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			addFence(pRenderer, &pRenderCompleteFences[i]);
			addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
		}
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);

		// INITIALIZE RESOURCE/DEBUG SYSTEMS
		//
		initResourceLoaderInterface(pRenderer);

		pVirtualJoystick = initVirtualJoystickUI(pRenderer, "circlepad");
		if (!pVirtualJoystick)
			return false;

		// INITIALIZE THE USER INTERFACE

		UIAppDesc appUIDesc = {};
		initAppUI(pRenderer, &appUIDesc, &pAppUI);
		if (!pAppUI)
			return false;

		addAppUITestScripts(pAppUI, gTestScripts, sizeof(gTestScripts) / sizeof(gTestScripts[0]));
		initAppUIFont(pAppUI, "TitilliumText/TitilliumText-Bold.otf");

		initProfiler();
		initProfilerUI(pAppUI, mSettings.mWidth, mSettings.mHeight);

		gGpuProfileToken = addGpuProfiler(pRenderer, pGraphicsQueue, "Graphics");

		// INITIALIZE PIPILINE STATES
		//
		ShaderLoadDesc planeShader = {};
		planeShader.mStages[0] = { "plane.vert", NULL, 0 };
		planeShader.mStages[1] = { "plane.frag", NULL, 0 };
		ShaderLoadDesc basicShader = {};
		basicShader.mStages[0] = { "basic.vert", NULL, 0 };
		basicShader.mStages[1] = { "basic.frag", NULL, 0 };

		addShader(pRenderer, &planeShader, &pPlaneDrawShader);
		addShader(pRenderer, &basicShader, &pSkeletonShader);

		Shader*           shaders[] = { pSkeletonShader, pPlaneDrawShader };
		RootSignatureDesc rootDesc = {};
		rootDesc.mShaderCount = 2;
		rootDesc.ppShaders = shaders;
		addRootSignature(pRenderer, &rootDesc, &pRootSignature);

		DescriptorSetDesc setDesc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_DRAW, gImageCount };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSet);


		uint64_t       jointDataSize = gNumberOfJointPoints * sizeof(float);
		BufferLoadDesc jointVbDesc = {};
		jointVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		jointVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		jointVbDesc.mDesc.mSize = jointDataSize;
		jointVbDesc.pData = pJointPoints;
		jointVbDesc.ppBuffer = &pJointVertexBuffer;
		addResource(&jointVbDesc, NULL);

		uint64_t       boneDataSize = gNumberOfBonePoints * sizeof(float);
		BufferLoadDesc boneVbDesc = {};
		boneVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		boneVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		boneVbDesc.mDesc.mSize = boneDataSize;
		boneVbDesc.pData = pBonePoints;
		boneVbDesc.ppBuffer = &pBoneVertexBuffer;
		addResource(&boneVbDesc, NULL);

		//Generate plane vertex buffer
		float planePoints[] = { -10.0f, 0.0f, -10.0f, 1.0f, 0.0f, 0.0f, -10.0f, 0.0f, 10.0f,  1.0f, 1.0f, 0.0f,
								10.0f,  0.0f, 10.0f,  1.0f, 1.0f, 1.0f, 10.0f,  0.0f, 10.0f,  1.0f, 1.0f, 1.0f,
								10.0f,  0.0f, -10.0f, 1.0f, 0.0f, 1.0f, -10.0f, 0.0f, -10.0f, 1.0f, 0.0f, 0.0f };

		uint64_t       planeDataSize = 6 * 6 * sizeof(float);
		BufferLoadDesc planeVbDesc = {};
		planeVbDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_VERTEX_BUFFER;
		planeVbDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		planeVbDesc.mDesc.mSize = planeDataSize;
		planeVbDesc.pData = planePoints;
		planeVbDesc.ppBuffer = &pPlaneVertexBuffer;
		addResource(&planeVbDesc, NULL);

		BufferLoadDesc ubDesc = {};
		ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
		ubDesc.mDesc.mSize = sizeof(UniformBlockPlane);
		ubDesc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
		ubDesc.pData = NULL;
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			ubDesc.ppBuffer = &pPlaneUniformBuffer[i];
			addResource(&ubDesc, NULL);
		}

		// SKELETON RENDERER
		//

		// Set up details for rendering the skeletons
		SkeletonRenderDesc skeletonRenderDesc = {};
		skeletonRenderDesc.mRenderer = pRenderer;
		skeletonRenderDesc.mSkeletonPipeline = pSkeletonPipeline;
		skeletonRenderDesc.mRootSignature = pRootSignature;
		skeletonRenderDesc.mJointVertexBuffer = pJointVertexBuffer;
		skeletonRenderDesc.mNumJointPoints = gNumberOfJointPoints;
		skeletonRenderDesc.mDrawBones = true;
		skeletonRenderDesc.mBoneVertexBuffer = pBoneVertexBuffer;
		skeletonRenderDesc.mNumBonePoints = gNumberOfBonePoints;
		skeletonRenderDesc.mBoneVertexStride = sizeof(float) * 6;
		skeletonRenderDesc.mJointVertexStride = sizeof(float) * 6;
		gSkeletonBatcher.Initialize(skeletonRenderDesc);

		// Add the rig to the list of skeletons to render
		gSkeletonBatcher.AddRig(&gStickFigureRig);

		// Add the GUI Panels / Windows
		const TextDrawDesc UIPanelWindowTitleTextDesc = { 0, 0xffff00ff, 14 };

		vec2    UIPosition = { mSettings.mWidth * 0.01f, mSettings.mHeight * 0.15f };
		vec2    UIPanelSize = { 650, 1000 };
		GuiDesc guiDesc;
		guiDesc.mStartPosition = UIPosition;
		guiDesc.mStartSize = UIPanelSize;
		guiDesc.mDefaultTextDrawDesc = UIPanelWindowTitleTextDesc;
		pStandaloneControlsGUIWindow = addAppUIGuiComponent(pAppUI, "Blended Animation", &guiDesc);

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

		// SET gUIData MEMBERS THAT NEED POINTERS TO ANIMATION DATA
		//

		// Blend Params
		gUIData.mBlendParams.mBlendRatio = gBlendedAnimation.GetBlendRatioPtr();
		gUIData.mBlendParams.mAutoSetBlendParams = gBlendedAnimation.GetAutoSetBlendParamsPtr();

		gUIData.mBlendParams.mWalkClipWeight = gWalkClipController.GetWeightPtr();
		gUIData.mBlendParams.mJogClipWeight = gJogClipController.GetWeightPtr();
		gUIData.mBlendParams.mRunClipWeight = gRunClipController.GetWeightPtr();

		gUIData.mBlendParams.mThreshold = gBlendedAnimation.GetThresholdPtr();

		// Walk Clip
		gUIData.mWalkClip.mPlaybackSpeed = gWalkClipController.GetPlaybackSpeedPtr();

		// Jog Clip
		gUIData.mJogClip.mPlaybackSpeed = gJogClipController.GetPlaybackSpeedPtr();

		// Run Clip
		gUIData.mRunClip.mPlaybackSpeed = gRunClipController.GetPlaybackSpeedPtr();

		// SET UP GUI BASED ON gUIData STRUCT
		//
		{
			// BLEND PARAMETERS
			//
			CollapsingHeaderWidget CollapsingBlendParamsWidgets;

			SeparatorWidget separator;

			// BlendRatio - Slider
			float fValMin = 0.0f;
			float fValMax = 1.0f;
			float sliderStepSize = 0.01f;

			// AutoSetBlendParams - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			SliderFloatWidget sliderFloat;
			sliderFloat.pData = gUIData.mBlendParams.mBlendRatio;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Blend Ratio", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			CheckboxWidget checkbox;
			checkbox.pData = gUIData.mBlendParams.mAutoSetBlendParams;
			IWidget* pAutoBlend = addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Auto Set Blend Params", &checkbox, WIDGET_TYPE_CHECKBOX);
			pAutoBlend->pOnEdited = AutoSetBlendParamsCallback;

			// Walk Clip Weight - Slider
			fValMin = 0.0f;
			fValMax = 1.0f;
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mBlendParams.mWalkClipWeight;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Clip Weight [Walk]", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			// Jog Clip Weight - Slider
			fValMin = 0.0f;
			fValMax = 1.0f;
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mBlendParams.mJogClipWeight;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Clip Weight [Jog]", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			// Run Clip Weight - Slider
			fValMin = 0.0f;
			fValMax = 1.0f;
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mBlendParams.mRunClipWeight;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Clip Weight [Run]", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			// Threshold - Slider
			fValMin = 0.01f;
			fValMax = 1.0f;
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mBlendParams.mThreshold;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "Threshold", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			addCollapsingHeaderSubWidget(&CollapsingBlendParamsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			// WALK CLIP
			//
			CollapsingHeaderWidget CollapsingWalkClipWidgets;

			// Play/Pause - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mWalkClip.mPlay;
			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "Play", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = WalkClipPlayCallback;

			// Loop - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mWalkClip.mLoop;
			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "Loop", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = WalkClipLoopCallback;

			// Animation Time - Slider
			fValMin = 0.0f;
			fValMax = gWalkClipController.GetDuration();
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = &gUIData.mWalkClip.mAnimationTime;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			IWidget* pAnimTime = addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "Animation Time", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);
			pAnimTime->pOnActive = WalkClipTimeChangeCallback;

			// Playback Speed - Slider
			fValMin = -5.0f;
			fValMax = 5.0f;
			sliderStepSize = 0.1f;

			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mWalkClip.mPlaybackSpeed;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingWalkClipWidgets, "Playback Speed", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			// JOG CLIP
			//
			CollapsingHeaderWidget CollapsingJogClipWidgets;

			// Play/Pause - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mJogClip.mPlay;
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "Play", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = JogClipPlayCallback;

			// Loop - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mJogClip.mLoop;
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "Loop", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = JogClipLoopCallback;

			// Animation Time - Slider
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			fValMin = 0.0f;
			fValMax = gJogClipController.GetDuration();
			sliderStepSize = 0.01f;

			sliderFloat.pData = &gUIData.mJogClip.mAnimationTime;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "Animation Time", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT)->pOnActive = JogClipTimeChangeCallback;

			// Playback Speed - Slider
			fValMin = -5.0f;
			fValMax = 5.0f;
			sliderStepSize = 0.1f;

			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mJogClip.mPlaybackSpeed;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingJogClipWidgets, "Playback Speed", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			// RUN CLIP
			//
			CollapsingHeaderWidget CollapsingRunClipWidgets;

			// Play/Pause - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mRunClip.mPlay;
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "Play", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = RunClipPlayCallback;

			// Loop - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mRunClip.mLoop;
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "Loop", &checkbox, WIDGET_TYPE_CHECKBOX)->pOnEdited = RunClipLoopCallback;


			// Animation Time - Slider
			fValMin = 0.0f;
			fValMax = gRunClipController.GetDuration();
			sliderStepSize = 0.01f;

			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = &gUIData.mRunClip.mAnimationTime;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "Animation Time", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT)->pOnActive = RunClipTimeChangeCallback;


			// Playback Speed - Slider
			fValMin = -5.0f;
			fValMax = 5.0f;
			sliderStepSize = 0.1f;

			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			sliderFloat.pData = gUIData.mRunClip.mPlaybackSpeed;
			sliderFloat.mMin = fValMin;
			sliderFloat.mMax = fValMax;
			sliderFloat.mStep = sliderStepSize;
			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "Playback Speed", &sliderFloat, WIDGET_TYPE_SLIDER_FLOAT);

			addCollapsingHeaderSubWidget(&CollapsingRunClipWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);


			// GENERAL SETTINGS
			//
			CollapsingHeaderWidget CollapsingGeneralSettingsWidgets;

			// ShowBindPose - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingGeneralSettingsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mGeneralSettings.mShowBindPose;
			addCollapsingHeaderSubWidget(&CollapsingGeneralSettingsWidgets, "Show Bind Pose", &checkbox, WIDGET_TYPE_CHECKBOX);

			// DrawPlane - Checkbox
			addCollapsingHeaderSubWidget(&CollapsingGeneralSettingsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			checkbox.pData = &gUIData.mGeneralSettings.mDrawPlane;
			addCollapsingHeaderSubWidget(&CollapsingGeneralSettingsWidgets, "Draw Plane", &checkbox, WIDGET_TYPE_CHECKBOX);

			addCollapsingHeaderSubWidget(&CollapsingGeneralSettingsWidgets, "", &separator, WIDGET_TYPE_SEPARATOR);

			// Add all widgets to the window

			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Blend Parameters", &CollapsingBlendParamsWidgets, WIDGET_TYPE_COLLAPSING_HEADER));
			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Walk Clip", &CollapsingWalkClipWidgets, WIDGET_TYPE_COLLAPSING_HEADER));
			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Jog Clip", &CollapsingJogClipWidgets, WIDGET_TYPE_COLLAPSING_HEADER));
			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Run Clip", &CollapsingRunClipWidgets, WIDGET_TYPE_COLLAPSING_HEADER));
			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "General settings", &CollapsingGeneralSettingsWidgets, WIDGET_TYPE_COLLAPSING_HEADER));

			DropdownWidget ddTestScripts;
			ddTestScripts.pData = &gCurrentScriptIndex;
			for (uint32_t i = 0; i < sizeof(gTestScripts) / sizeof(gTestScripts[0]); ++i)
			{
				ddTestScripts.mNames.push_back((char*)gTestScripts[i]);
				ddTestScripts.mValues.push_back(gScriptIndexes[i]);
			}
			addWidgetLua(addGuiWidget(pStandaloneControlsGUIWindow, "Test Scripts", &ddTestScripts, WIDGET_TYPE_DROPDOWN));

			ButtonWidget bRunScript;
			IWidget* pRunScript = addGuiWidget(pStandaloneControlsGUIWindow, "Run", &bRunScript, WIDGET_TYPE_BUTTON);
			pRunScript->pOnEdited = RunScript;
			addWidgetLua(pRunScript);
		}

		waitForAllResourceLoads();

		// Prepare descriptor sets
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			DescriptorData params[1] = {};
			params[0].pName = "uniformBlock";
			params[0].ppBuffers = &pPlaneUniformBuffer[i];
			updateDescriptorSet(pRenderer, i, pDescriptorSet, 1, params);
		}

		/************************************************************************/
		// SETUP THE MAIN CAMERA
		//
		CameraMotionParameters cmp{ 50.0f, 75.0f, 150.0f };
		vec3                   camPos{ -1.3f, 1.8f, 3.8f };
		vec3                   lookAt{ 1.2f, 0.0f, 0.4f };

		pCameraController = initFpsCameraController(camPos, lookAt);
		pCameraController->setMotionParameters(cmp);


		if (!initInputSystem(pWindow))
			return false;

		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx) { toggleFullscreen(((IApp*)ctx->pUserData)->pWindow); return true; }, this };
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
		typedef bool (*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
		static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
		{
			if (!appUIIsFocused(pAppUI) && *ctx->pCaptured)
			{
				virtualJoystickUIOnMove(pVirtualJoystick, index, ctx->mPhase != INPUT_ACTION_PHASE_CANCELED, ctx->pPosition);
				index ? pCameraController->onRotate(ctx->mFloat2) : pCameraController->onMove(ctx->mFloat2);
			}
			return true;
		};
		actionDesc = { InputBindings::FLOAT_RIGHTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL, 20.0f, 200.0f, 0.5f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::FLOAT_LEFTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_NORTH, [](InputActionContext* ctx) { pCameraController->resetView(); return true; } };
		addInputAction(&actionDesc);

		gFrameIndex = 0; 
		
		return true;
	}

	void Exit()
	{
		exitInputSystem();

		exitCameraController(pCameraController);

		// Animation data
		gStickFigureRig.Exit();
		gWalkClip.Exit();
		gJogClip.Exit();
		gRunClip.Exit();
		gBlendedAnimation.Exit();
		gStickFigureAnimObject.Exit();
	
		// Need to free memory;
		tf_free(pJointPoints);
		tf_free(pBonePoints);

		gSkeletonBatcher.Exit();
		exitProfilerUI();
		exitProfiler();
		exitVirtualJoystickUI(pVirtualJoystick);
		exitAppUI(pAppUI);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeResource(pPlaneUniformBuffer[i]);
		}
		removeResource(pJointVertexBuffer);
		removeResource(pBoneVertexBuffer);
		removeResource(pPlaneVertexBuffer);

		removeShader(pRenderer, pSkeletonShader);
		removeShader(pRenderer, pPlaneDrawShader);
		removeDescriptorSet(pRenderer, pDescriptorSet);
		removeRootSignature(pRenderer, pRootSignature);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pRenderCompleteFences[i]);
			removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);
		}
		removeSemaphore(pRenderer, pImageAcquiredSemaphore);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeCmd(pRenderer, pCmds[i]);
			removeCmdPool(pRenderer, pCmdPools[i]);
		}

		exitResourceLoaderInterface(pRenderer);
		removeQueue(pRenderer, pGraphicsQueue);
		exitRenderer(pRenderer);
	}

	bool Load()
	{
		// INITIALIZE SWAP-CHAIN AND DEPTH BUFFER
		//
		if (!addSwapChain())
			return false;
		if (!addDepthBuffer())
			return false;

		// LOAD USER INTERFACE
		//
		if (!addAppGUIDriver(pAppUI, pSwapChain->ppRenderTargets))
			return false;

		if (!addVirtualJoystickUIPipeline(pVirtualJoystick, pSwapChain->ppRenderTargets[0]))
			return false;

		//layout and pipeline for skeleton draw
		VertexLayout vertexLayout = {};
		vertexLayout.mAttribCount = 2;
		vertexLayout.mAttribs[0].mSemantic = SEMANTIC_POSITION;
		vertexLayout.mAttribs[0].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[0].mBinding = 0;
		vertexLayout.mAttribs[0].mLocation = 0;
		vertexLayout.mAttribs[0].mOffset = 0;
		vertexLayout.mAttribs[1].mSemantic = SEMANTIC_NORMAL;
		vertexLayout.mAttribs[1].mFormat = TinyImageFormat_R32G32B32_SFLOAT;
		vertexLayout.mAttribs[1].mBinding = 0;
		vertexLayout.mAttribs[1].mLocation = 1;
		vertexLayout.mAttribs[1].mOffset = 3 * sizeof(float);

		RasterizerStateDesc rasterizerStateDesc = {};
		rasterizerStateDesc.mCullMode = CULL_MODE_NONE;

		RasterizerStateDesc skeletonRasterizerStateDesc = {};
		skeletonRasterizerStateDesc.mCullMode = CULL_MODE_FRONT;

		DepthStateDesc depthStateDesc = {};
		depthStateDesc.mDepthTest = true;
		depthStateDesc.mDepthWrite = true;
		depthStateDesc.mDepthFunc = CMP_LEQUAL;

		PipelineDesc desc = {};
		desc.mType = PIPELINE_TYPE_GRAPHICS;
		GraphicsPipelineDesc& pipelineSettings = desc.mGraphicsDesc;
		pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
		pipelineSettings.mRenderTargetCount = 1;
		pipelineSettings.pDepthState = &depthStateDesc;
		pipelineSettings.pColorFormats = &pSwapChain->ppRenderTargets[0]->mFormat;
		pipelineSettings.mSampleCount = pSwapChain->ppRenderTargets[0]->mSampleCount;
		pipelineSettings.mSampleQuality = pSwapChain->ppRenderTargets[0]->mSampleQuality;
		pipelineSettings.mDepthStencilFormat = pDepthBuffer->mFormat;
		pipelineSettings.pRootSignature = pRootSignature;
		pipelineSettings.pShaderProgram = pSkeletonShader;
		pipelineSettings.pVertexLayout = &vertexLayout;
		pipelineSettings.pRasterizerState = &skeletonRasterizerStateDesc;
		addPipeline(pRenderer, &desc, &pSkeletonPipeline);

		// Update the mSkeletonPipeline pointer now that the pipeline has been loaded
		gSkeletonBatcher.LoadPipeline(pSkeletonPipeline);

		//layout and pipeline for plane draw
		vertexLayout = {};
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

		pipelineSettings.pDepthState = NULL;
		pipelineSettings.pRasterizerState = &rasterizerStateDesc;
		pipelineSettings.pShaderProgram = pPlaneDrawShader;
		addPipeline(pRenderer, &desc, &pPlaneDrawPipeline);

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pGraphicsQueue);

		removeAppGUIDriver(pAppUI);

		removeVirtualJoystickUIPipeline(pVirtualJoystick);

		removePipeline(pRenderer, pPlaneDrawPipeline);
		removePipeline(pRenderer, pSkeletonPipeline);

		removeSwapChain(pRenderer, pSwapChain);
		removeRenderTarget(pRenderer, pDepthBuffer);
	}

	void Update(float deltaTime)
	{
		updateInputSystem(mSettings.mWidth, mSettings.mHeight);

		pCameraController->update(deltaTime);

		/************************************************************************/
		// Scene Update
		/************************************************************************/

		// update camera with time
		mat4 viewMat = pCameraController->getViewMatrix();

		const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
		const float horizontal_fov = PI / 2.0f;
		mat4        projMat = mat4::perspective(horizontal_fov, aspectInverse, 0.1f, 1000.0f);
		mat4        projViewMat = projMat * viewMat;

		vec3 lightPos = vec3(0.0f, 10.0f, 2.0f);
		vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

		/************************************************************************/
		// Animation
		/************************************************************************/
		gAnimationUpdateTimer.Reset();

		// Update the animated object for this frame
		if (!gStickFigureAnimObject.Update(deltaTime))
			LOGF(eINFO, "Animation NOT Updating!");

		if (!gUIData.mGeneralSettings.mShowBindPose)
		{
			// Pose the rig based on the animated object's updated values
			gStickFigureAnimObject.PoseRig();
		}
		else
		{
			// Ignore the updated values and pose in bind
			gStickFigureAnimObject.PoseRigInBind();
		}

		// Record animation update time
		gAnimationUpdateTimer.GetUSec(true);

		// Update uniforms that will be shared between all skeletons
		gSkeletonBatcher.SetSharedUniforms(projViewMat, lightPos, lightColor);

		/************************************************************************/
		// Plane
		/************************************************************************/
		gUniformDataPlane.mProjectView = projViewMat;
		gUniformDataPlane.mToWorldMat = mat4::identity();

		/************************************************************************/
		// GUI
		/************************************************************************/
		updateAppUI(pAppUI, deltaTime);
	}

	void Draw()
	{
		uint32_t swapchainImageIndex;
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &swapchainImageIndex);

		// UPDATE UNIFORM BUFFERS
		//

		// Update all the instanced uniform data for each batch of joints and bones
		gSkeletonBatcher.SetPerInstanceUniforms(gFrameIndex);

		BufferUpdateDesc planeViewProjCbv = { pPlaneUniformBuffer[gFrameIndex] };
		beginUpdateResource(&planeViewProjCbv);
		*(UniformBlockPlane*)planeViewProjCbv.pMappedData = gUniformDataPlane;
		endUpdateResource(&planeViewProjCbv, NULL);

		// FRAME SYNC & ACQUIRE SWAPCHAIN RENDER TARGET
		//
		// Stall if CPU is running "Swap Chain Buffer Count" frames ahead of GPU
		Fence*      pNextFence = pRenderCompleteFences[gFrameIndex];
		FenceStatus fenceStatus;
		getFenceStatus(pRenderer, pNextFence, &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pNextFence);

		resetCmdPool(pRenderer, pCmdPools[gFrameIndex]);

		// Acquire the main render target from the swapchain
		RenderTarget* pRenderTarget = pSwapChain->ppRenderTargets[swapchainImageIndex];
		Semaphore*    pRenderCompleteSemaphore = pRenderCompleteSemaphores[gFrameIndex];
		Fence*        pRenderCompleteFence = pRenderCompleteFences[gFrameIndex];
		Cmd*          cmd = pCmds[gFrameIndex];
		beginCmd(cmd);    // start recording commands

		// start gpu frame profiler
		cmdBeginGpuFrameProfile(cmd, gGpuProfileToken);

		RenderTargetBarrier barriers[] =    // wait for resource transition
		{
			{ pRenderTarget, RESOURCE_STATE_PRESENT, RESOURCE_STATE_RENDER_TARGET },
		};
		cmdResourceBarrier(cmd, 0, NULL, 0, NULL, 1, barriers);

		// bind and clear the render target
		LoadActionsDesc loadActions = {};    // render target clean command
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0] = pRenderTarget->mClearValue;
		loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
		loadActions.mClearDepth.depth = 1.0f;
		loadActions.mClearDepth.stencil = 0;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, pDepthBuffer, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(cmd, 0.0f, 0.0f, (float)pRenderTarget->mWidth, (float)pRenderTarget->mHeight, 0.0f, 1.0f);
		cmdSetScissor(cmd, 0, 0, pRenderTarget->mWidth, pRenderTarget->mHeight);

		//// draw plane
		if (gUIData.mGeneralSettings.mDrawPlane)
		{
			const uint32_t stride = sizeof(float) * 6;
			cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Plane");
			cmdBindPipeline(cmd, pPlaneDrawPipeline);
			cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSet);
			cmdBindVertexBuffer(cmd, 1, &pPlaneVertexBuffer, &stride, NULL);
			cmdDraw(cmd, 6, 0);
			cmdEndDebugMarker(cmd);
		}

		//// draw the skeleton of the rig
		cmdBeginDebugMarker(cmd, 1, 0, 1, "Draw Skeletons");
		gSkeletonBatcher.Draw(cmd, gFrameIndex);
		cmdEndDebugMarker(cmd);

		//// draw the UI
		cmdBeginDebugMarker(cmd, 0, 1, 0, "Draw UI");
		loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_LOAD;
		cmdBindRenderTargets(cmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);

		float4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		drawVirtualJoystickUI(pVirtualJoystick, cmd, &color);

		appUIGui(pAppUI, pStandaloneControlsGUIWindow);    // adds the gui element to AppUI::ComponentsToUpdate list
        float2 txtSize = cmdDrawCpuProfile(cmd, float2(8.0f, 15.0f), &gFrameTimeDraw);

		char text[64];
		sprintf(text, "Animation Update %f ms", gAnimationUpdateTimer.GetUSecAverage() / 1000.0f);

		float2 screenCoords(8.f, txtSize.y + 30.f);
		drawAppUIText(pAppUI, 
			cmd, &screenCoords, text,
			&gFrameTimeDraw);

#if !defined(__ANDROID__)
        cmdDrawGpuProfile(cmd, float2(8.f, txtSize.y * 2.f + 45.f), gGpuProfileToken, &gFrameTimeDraw);
#endif

		cmdDrawProfilerUI();
		drawAppUI(pAppUI, cmd);

		cmdBindRenderTargets(cmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);
		cmdEndDebugMarker(cmd);

		// PRESENT THE GRPAHICS QUEUE
		//
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
		presentDesc.ppWaitSemaphores = &pRenderCompleteSemaphore;
		presentDesc.pSwapChain = pSwapChain;
		presentDesc.mSubmitDone = true;
		queuePresent(pGraphicsQueue, &presentDesc);
		flipProfiler();

		gFrameIndex = (gFrameIndex + 1) % gImageCount;
	}

	const char* GetName() { return "22_Blending"; }

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
		swapChainDesc.mColorClearValue = { { 0.39f, 0.41f, 0.37f, 1.0f } };
		swapChainDesc.mEnableVsync = mSettings.mDefaultVSyncEnabled;
		::addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);

		return pSwapChain != NULL;
	}

	bool addDepthBuffer()
	{
		// Add depth buffer
		RenderTargetDesc depthRT = {};
		depthRT.mArraySize = 1;
		//depthRT.mClearValue = { 1.0f, 0 };
		depthRT.mClearValue.depth = 1.0f;
		depthRT.mClearValue.stencil = 0;
		depthRT.mDepth = 1;
		depthRT.mFormat = TinyImageFormat_D32_SFLOAT;
		depthRT.mStartState = RESOURCE_STATE_DEPTH_WRITE;
		depthRT.mHeight = mSettings.mHeight;
		depthRT.mSampleCount = SAMPLE_COUNT_1;
		depthRT.mSampleQuality = 0;
		depthRT.mWidth = mSettings.mWidth;
		depthRT.mFlags = TEXTURE_CREATION_FLAG_ON_TILE;
		addRenderTarget(pRenderer, &depthRT, &pDepthBuffer);

		return pDepthBuffer != NULL;
	}
};

DEFINE_APPLICATION_MAIN(Blending)
