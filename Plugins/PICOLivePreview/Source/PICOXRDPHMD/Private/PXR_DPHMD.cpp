//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#include "PXR_DPHMD.h"
#include "CoreMinimal.h"
#include "XRThreadUtils.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "PostProcess/PostProcessHMD.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "DefaultSpectatorScreenController.h"
#include "ScreenRendering.h"
#include "PXR_Log.h"


#if PLATFORM_WINDOWS
#include "DynamicRHI.h"
#include "D3D11RHI.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#endif

#include "Slate/SceneViewport.h"
#include "Engine/GameEngine.h"
#include "GameFramework/PlayerController.h"
#include "IHeadMountedDisplayVulkanExtensions.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "CanvasItem.h"
#include "ISettingsModule.h"
#include "PXR_DPSettings.h"
#include "GameFramework/GameUserSettings.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#endif

#ifndef ARRAYSIZE
#define ARRAYSIZE( a ) ( sizeof( ( a ) ) / sizeof( ( a )[ 0 ] ) )
#endif

static constexpr float PreviewFov = 101.f;

DEFINE_LOG_CATEGORY(LogPICODP);
/** Helper function for acquiring the appropriate FSceneViewport */
FSceneViewport* FindSceneViewport()
{
	if (!GIsEditor)
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(GEngine);
		return GameEngine->SceneViewport.Get();
	}
#if WITH_EDITOR
	else
	{
		UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
		FSceneViewport* PIEViewport = (FSceneViewport*)EditorEngine->GetPIEViewport();
		if (PIEViewport != nullptr && PIEViewport->IsStereoRenderingAllowed())
		{
			// PIE is setup for stereo rendering
			return PIEViewport;
		}
		else
		{
			// Check to see if the active editor viewport is drawing in stereo mode
			// @todo vreditor: Should work with even non-active viewport!
			FSceneViewport* EditorViewport = (FSceneViewport*)EditorEngine->GetActiveViewport();
			if (EditorViewport != nullptr && EditorViewport->IsStereoRenderingAllowed())
			{
				return EditorViewport;
			}
		}
	}
#endif
	return nullptr;
}

//---------------------------------------------------
// PICODP Plugin Implementation
//---------------------------------------------------

class FPICODPPlugin : public IPICOXRDPModule
{
	/** IHeadMountedDisplayModule implementation */
	virtual TSharedPtr<class IXRTrackingSystem, ESPMode::ThreadSafe> CreateTrackingSystem() override;

	FString GetModuleKeyName() const
	{
		return FString(TEXT("PICOXRPreview"));
	}

	void* LibusbDllHandle;
	void* Pxr_baseDllHandle;
	void* Pxr_rpcDllHandle;
	void* StreamingDriverDllHandle;

	bool LoadPICODPModule();
	void UnloadPICODPModule();

public:
	FPICODPPlugin()
	{
	}

	virtual void StartupModule() override
	{
		IHeadMountedDisplayModule::StartupModule();
		LoadPICODPModule();
	}

	virtual void ShutdownModule() override
	{
		IHeadMountedDisplayModule::ShutdownModule();
		UnloadPICODPModule();
	}

	bool Initialize()
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview Initialize!");
		return true;
	}


	virtual bool IsHMDConnected() override
	{
		return true;
	}

#if PLATFORM_WINDOWS
	enum class D3DApiLevel
	{
		Undefined,
		Direct3D11,
		Direct3D12
	};

	static inline D3DApiLevel GetD3DApiLevel()
	{
		FString RHIString;
		{
			FString HardwareDetails = FHardwareInfo::GetHardwareDetailsString();
			FString RHILookup = NAME_RHI.ToString() + TEXT("=");

			PXR_LOGD(PxrUnreal, "PXR_LivePreview RHIString:%s", *RHIString);
			if (!FParse::Value(*HardwareDetails, *RHILookup, RHIString))
			{
				// RHI might not be up yet. Let's check the command-line and see if DX12 was specified.
				// This will get hit on startup since we don't have RHI details during stereo device bringup. 
				// This is not a final fix; we should probably move the stereo device init to later on in startup.
				bool bForceD3D12 = FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"));
				return bForceD3D12 ? D3DApiLevel::Direct3D12 : D3DApiLevel::Direct3D11;
			}
		}

		if (RHIString == TEXT("D3D11"))
		{
			return D3DApiLevel::Direct3D11;
		}
		if (RHIString == TEXT("D3D12"))
		{
			return D3DApiLevel::Direct3D12;
		}

		return D3DApiLevel::Undefined;
	}

#endif

private:
	TSharedPtr<IHeadMountedDisplayVulkanExtensions, ESPMode::ThreadSafe> VulkanExtensions;
};

void FPICODPPlugin::UnloadPICODPModule()
{
	if (LibusbDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing LibusbDllHandle."));
		FPlatformProcess::FreeDllHandle(LibusbDllHandle);
		LibusbDllHandle = nullptr;
	}
	if (Pxr_baseDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing Pxr_baseDllHandle."));
		FPlatformProcess::FreeDllHandle(Pxr_baseDllHandle);
		Pxr_baseDllHandle = nullptr;
	}
	if (Pxr_rpcDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing Pxr_rpcDllHandle."));
		FPlatformProcess::FreeDllHandle(Pxr_rpcDllHandle);
		Pxr_rpcDllHandle = nullptr;
	}
	if (StreamingDriverDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Freeing StreamingDriverDllHandle."));
		FPlatformProcess::FreeDllHandle(StreamingDriverDllHandle);
		StreamingDriverDllHandle = nullptr;
	}
}

bool FPICODPPlugin::LoadPICODPModule()
{
#if PLATFORM_WINDOWS
#if PLATFORM_64BITS
	FString StreamerDLLDir = FPaths::ProjectPluginsDir() / FString::Printf(TEXT("PICOLivePreview/Libs/Include/streamingDriverInterface/"));
	FPlatformProcess::PushDllDirectory(*StreamerDLLDir);
	LibusbDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "libusb-1.0.dll"));
	Pxr_baseDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "pxr_base.dll"));
	Pxr_rpcDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "pxr_rpc.dll"));
	StreamingDriverDllHandle = FPlatformProcess::GetDllHandle(*(StreamerDLLDir + "pxr_driver_sdk.dll"));
	FPlatformProcess::PopDllDirectory(*StreamerDLLDir);

	if (!LibusbDllHandle
		|| !Pxr_baseDllHandle
		|| !Pxr_rpcDllHandle
		|| !StreamingDriverDllHandle)
	{
		UE_LOG(LogHMD, Log, TEXT("Failed to load PICODP library."));
		return false;
	}

#endif
#endif
	return true;
}

IMPLEMENT_MODULE(FPICODPPlugin, PICOXRDPHMD)

TSharedPtr<class IXRTrackingSystem, ESPMode::ThreadSafe> FPICODPPlugin::CreateTrackingSystem()
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview Start CreateTrackingSystem!");

	auto level = FPICODPPlugin::GetD3DApiLevel();

	if (level == FPICODPPlugin::D3DApiLevel::Direct3D11)
	{
		TSharedPtr<FPICOXRHMDDP, ESPMode::ThreadSafe> PICODPHMD = FSceneViewExtensions::NewExtension<FPICOXRHMDDP>(this);
		if (PICODPHMD)
		{
			return PICODPHMD;
		}
	}
	return nullptr;
}


//---------------------------------------------------
// PICODP IHeadMountedDisplay Implementation
//---------------------------------------------------

#if STEAMVR_SUPPORTED_PLATFORMS


bool FPICOXRHMDDP::IsHMDConnected()
{
	return PICODPPlugin->IsHMDConnected();
}

bool FPICOXRHMDDP::IsHMDEnabled() const
{
	return bHmdEnabled;
}

EHMDWornState::Type FPICOXRHMDDP::GetHMDWornState()
{
	return HmdWornState;
}

void FPICOXRHMDDP::EnableHMD(bool enable)
{
	bHmdEnabled = enable;

	if (!bHmdEnabled)
	{
		EnableStereo(false);
	}
}

bool FPICOXRHMDDP::GetHMDMonitorInfo(MonitorInfo& MonitorDesc)
{
	MonitorDesc.MonitorName = "";
	MonitorDesc.MonitorId = 0;
	MonitorDesc.DesktopX = MonitorDesc.DesktopY = MonitorDesc.ResolutionX = MonitorDesc.ResolutionY = 0;
	return false;
}

void FPICOXRHMDDP::GetFieldOfView(float& OutHFOVInDegrees, float& OutVFOVInDegrees) const
{
	OutHFOVInDegrees = 0.0f;
	OutVFOVInDegrees = 0.0f;
}

bool FPICOXRHMDDP::DoesSupportPositionalTracking() const
{
	return true;
}

bool FPICOXRHMDDP::HasValidTrackingPosition()
{
	return true;
}

bool FPICOXRHMDDP::GetTrackingSensorProperties(int32 SensorId, FQuat& OutOrientation, FVector& OutOrigin, FXRSensorProperties& OutSensorProperties)
{
	OutOrigin = FVector::ZeroVector;
	OutOrientation = FQuat::Identity;
	OutSensorProperties = FXRSensorProperties();
	return true;
}
#if ENGINE_MINOR_VERSION >25
FString FPICOXRHMDDP::GetTrackedDevicePropertySerialNumber(int32 DeviceId)
{
	return FString();
}
#endif
void FPICOXRHMDDP::SetInterpupillaryDistance(float NewInterpupillaryDistance)
{
}

float FPICOXRHMDDP::GetInterpupillaryDistance() const
{
	return 0.064f;
}

bool FPICOXRHMDDP::GetCurrentPose(int32 DeviceId, FQuat& CurrentOrientation, FVector& CurrentPosition)
{
	FQuat tempCurrentOrientation = FQuat::Identity;

	if (FPICOXRDPManager::IsStreaming())
	{
		FPICOXRDPManager::GetHMDPositionAndRotation(CurrentPosition, tempCurrentOrientation);
		CurrentPosition = FVector(-CurrentPosition.Z, CurrentPosition.X, CurrentPosition.Y) * 100;
		CurrentOrientation.X = tempCurrentOrientation.Z;
		CurrentOrientation.Y = -tempCurrentOrientation.X;
		CurrentOrientation.Z = -tempCurrentOrientation.Y;
		CurrentOrientation.W = tempCurrentOrientation.W;
		//Todo:Prevent crashes caused by invalid Unnormalized data
		CurrentOrientation.Normalize();

		return true;
	}
	return false;
}

void FPICOXRHMDDP::SetTrackingOrigin(EHMDTrackingOrigin::Type NewOrigin)
{
}

EHMDTrackingOrigin::Type FPICOXRHMDDP::GetTrackingOrigin() const
{
	return EHMDTrackingOrigin::Floor;
}

bool FPICOXRHMDDP::GetFloorToEyeTrackingTransform(FTransform& OutStandingToSeatedTransform) const
{
	bool bSuccess = false;
	return bSuccess;
}
#if ENGINE_MINOR_VERSION >26
FVector2D FPICOXRHMDDP::GetPlayAreaBounds(EHMDTrackingOrigin::Type Origin) const
{
	FVector2D Bounds;
	if (Origin == EHMDTrackingOrigin::Stage)
	{
		return Bounds;
	}

	return FVector2D::ZeroVector;
}
#endif
void FPICOXRHMDDP::RecordAnalytics()
{
}

float FPICOXRHMDDP::GetWorldToMetersScale() const
{
	return 100.0f;
}


bool FPICOXRHMDDP::EnumerateTrackedDevices(TArray<int32>& TrackedIds, EXRTrackedDeviceType DeviceType)
{
	TrackedIds.Empty();
	if (DeviceType == EXRTrackedDeviceType::Any || DeviceType == EXRTrackedDeviceType::HeadMountedDisplay)
	{
		TrackedIds.Add(IXRTrackingSystem::HMDDeviceId);
		return true;
	}
	return false;
}


bool FPICOXRHMDDP::IsTracking(int32 DeviceId)
{
	return true;
}

bool FPICOXRHMDDP::IsChromaAbCorrectionEnabled() const
{
	return false;
}

void FPICOXRHMDDP::OnBeginPlay(FWorldContext& InWorldContext)
{
#if WITH_EDITOR
	if (!InitializedSucceeded)
	{
		return;
	}
	
	if (GIsEditor)
	{
		UEditorEngine* EdEngine = Cast<UEditorEngine>(GEngine);
		if (EdEngine->GetPlayInEditorSessionInfo().IsSet())
		{
			bIsVRPreview = EdEngine->GetPlayInEditorSessionInfo()->OriginalRequestParams.SessionPreviewTypeOverride ==
				EPlaySessionPreviewType::VRPreview;
		}
	}

	if (bIsVRPreview)
	{
		UHeadMountedDisplayFunctionLibrary::SetSpectatorScreenMode(ESpectatorScreenMode::SingleEye);

		GEngine->bUseFixedFrameRate = true;
		GEngine->FixedFrameRate = 72;

		if (FPICOXRDPManager::IsConnectToServiceSucceed()
			&& FPICOXRDPManager::OnBeginPlayStartStreaming())
		{
			PXR_LOGD(PxrUnreal, "PXR_LivePreview  BeginPlay Succeed!");
		}
		else
		{
			PXR_LOGD(PxrUnreal, "PXR_LivePreview  StartStreaming Failed!Please check if PDC is Launching and restart vr preview again");
		}
		
	}

#endif
}

void FPICOXRHMDDP::OnEndPlay(FWorldContext& InWorldContext)
{
	if (!GEnableVREditorHacks)
	{
		EnableStereo(false);
	}

	if (bIsVRPreview)
	{
		GEngine->bUseFixedFrameRate = false;
		GEngine->FixedFrameRate = 30;
		FPICOXRDPManager::OnEndPlayStopStreaming();
	}
}

const FName FPICOXRHMDDP::SystemName(TEXT("PICOXRPreview"));

FString FPICOXRHMDDP::GetVersionString() const
{
	return FString();
}

bool FPICOXRHMDDP::OnStartGameFrame(FWorldContext& WorldContext)
{
	if (bStereoEnabled != bStereoDesired)
	{
		bStereoEnabled = EnableStereo(bStereoDesired);
	}
	return true;
}

void FPICOXRHMDDP::ResetOrientationAndPosition(float yaw)
{
	ResetOrientation(yaw);
	ResetPosition();
}

void FPICOXRHMDDP::ResetOrientation(float Yaw)
{
	BaseOrientation = FQuat::Identity;
}

void FPICOXRHMDDP::ResetPosition()
{
	BaseOffset = FVector();
}

void FPICOXRHMDDP::SetBaseRotation(const FRotator& BaseRot)
{
	BaseOrientation = BaseRot.Quaternion();
}

FRotator FPICOXRHMDDP::GetBaseRotation() const
{
	return FRotator::ZeroRotator;
}

void FPICOXRHMDDP::SetBaseOrientation(const FQuat& BaseOrient)
{
	BaseOrientation = BaseOrient;
}

FQuat FPICOXRHMDDP::GetBaseOrientation() const
{
	return BaseOrientation;
}

void FPICOXRHMDDP::SetBasePosition(const FVector& BasePosition)
{
	BaseOffset = BasePosition;
}

FVector FPICOXRHMDDP::GetBasePosition() const
{
	return BaseOffset;
}

bool FPICOXRHMDDP::IsStereoEnabled() const
{
	return bStereoEnabled && bHmdEnabled;
}

bool FPICOXRHMDDP::EnableStereo(bool bStereo)
{
	if (bStereoEnabled == bStereo)
	{
		return false;
	}

	if ((!bStereo))
	{
		return false;
	}

	bStereoDesired = (IsHMDEnabled()) ? bStereo : false;

	// Set the viewport to match that of the HMD display
	FSceneViewport* SceneVP = FindSceneViewport();
	if (SceneVP)
	{
		TSharedPtr<SWindow> Window = SceneVP->FindWindow();
		if (Window.IsValid() && SceneVP->GetViewportWidget().IsValid())
		{
			// Set MirrorWindow state on the Window
			Window->SetMirrorWindow(bStereo);

			if (bStereo)
			{
				uint32 Width, Height;

				Width = WindowMirrorBoundsWidth;
				Height = WindowMirrorBoundsHeight;

				bStereoEnabled = bStereoDesired;
				SceneVP->SetViewportSize(Width, Height);
			}
			else
			{
				//flush all commands that might call GetStereoProjectionMatrix or other functions that rely on bStereoEnabled 
				FlushRenderingCommands();

				// Note: Setting before resize to ensure we don't try to allocate a new vr rt.
				bStereoEnabled = bStereoDesired;

				FRHIViewport* const ViewportRHI = SceneVP->GetViewportRHI();
				if (ViewportRHI != nullptr)
				{
					ViewportRHI->SetCustomPresent(nullptr);
				}

				FVector2D size = SceneVP->FindWindow()->GetSizeInScreen();
				SceneVP->SetViewportSize(size.X, size.Y);
				Window->SetViewportSizeDrivenByWindow(true);
			}
		}
	}

	// Uncap fps to enable FPS higher than 62
	GEngine->bForceDisableFrameRateSmoothing = bStereoEnabled;

	return bStereoEnabled;
}

void FPICOXRHMDDP::AdjustViewRect(EStereoscopicPass StereoPass, int32& X, int32& Y, uint32& SizeX, uint32& SizeY) const
{
	SizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
	SizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);

	SizeX = SizeX / 2;
	if (StereoPass == eSSP_RIGHT_EYE)
	{
		X += SizeX;
	}
}

bool FPICOXRHMDDP::GetRelativeEyePose(int32 DeviceId, EStereoscopicPass Eye, FQuat& OutOrientation, FVector& OutPosition)
{
	OutOrientation = FQuat::Identity;
	OutPosition = FVector::ZeroVector;
	if (DeviceId == IXRTrackingSystem::HMDDeviceId && (Eye == eSSP_LEFT_EYE || Eye == eSSP_RIGHT_EYE))
	{
		OutPosition = FVector(0, (Eye == EStereoscopicPass::eSSP_LEFT_EYE ? -.5 : .5) * 0.064f * GetWorldToMetersScale(), 0);
		return true;
	}
	else
	{
		return false;
	}
}

void FPICOXRHMDDP::CalculateStereoViewOffset(const enum EStereoscopicPass StereoPassType, FRotator& ViewRotation, const float WorldToMeters, FVector& ViewLocation)
{
	// Needed to transform world locked stereo layers
	PlayerLocation = ViewLocation;

	// Forward to the base implementation (that in turn will call the DefaultXRCamera implementation)
	FHeadMountedDisplayBase::CalculateStereoViewOffset(StereoPassType, ViewRotation, WorldToMeters, ViewLocation);
}

FMatrix FPICOXRHMDDP::GetStereoProjectionMatrix(const enum EStereoscopicPass StereoPassType) const
{
	check(IsStereoEnabled() || IsHeadTrackingEnforced());
	const float ProjectionCenterOffset = 0; // 0.151976421f;
	const float PassProjectionOffset = (StereoPassType == eSSP_LEFT_EYE) ? ProjectionCenterOffset : -ProjectionCenterOffset;
	// correct far and near planes for reversed-Z projection matrix
	const float WorldScale = GetWorldToMetersScale() * (1.0 / 100.0f); // physical scale is 100 UUs/meter
	float ZNear = GNearClippingPlane * WorldScale;
	
	const float HalfUpFov = FPlatformMath::Tan(BothFrustum.FovUp);
	const float HalfDownFov = FPlatformMath::Tan(BothFrustum.FovDown);
	const float HalfLeftFov = FPlatformMath::Tan(BothFrustum.FovLeft);
	const float HalfRightFov = FPlatformMath::Tan(BothFrustum.FovRight);
	float SumRL = (HalfRightFov + HalfLeftFov);
	float SumTB = (HalfUpFov + HalfDownFov);
	float InvRL = (1.0f / (HalfRightFov - HalfLeftFov));
	float InvTB = (1.0f / (HalfUpFov - HalfDownFov));
	FMatrix ProjectionMatrix = FMatrix(
		FPlane((2.0f * InvRL), 0.0f, 0.0f, 0.0f),
		FPlane(0.0f, (2.0f * InvTB), 0.0f, 0.0f),
		FPlane((SumRL * -InvRL), (SumTB * -InvTB), 0.0f, 1.0f),
		FPlane(0.0f, 0.0f, ZNear, 0.0f)) * FTranslationMatrix(FVector(PassProjectionOffset, 0, 0));
	return ProjectionMatrix;
}

void FPICOXRHMDDP::GetEyeRenderParams_RenderThread(const FRenderingCompositePassContext& Context, FVector2D& EyeToSrcUVScaleValue, FVector2D& EyeToSrcUVOffsetValue) const
{
	if (Context.View.StereoPass == eSSP_LEFT_EYE)
	{
		EyeToSrcUVOffsetValue.X = 0.0f;
		EyeToSrcUVOffsetValue.Y = 0.0f;

		EyeToSrcUVScaleValue.X = 0.5f;
		EyeToSrcUVScaleValue.Y = 1.0f;
	}
	else
	{
		EyeToSrcUVOffsetValue.X = 0.5f;
		EyeToSrcUVOffsetValue.Y = 0.0f;

		EyeToSrcUVScaleValue.X = 0.5f;
		EyeToSrcUVScaleValue.Y = 1.0f;
	}
}

bool FPICOXRHMDDP::GetHMDDistortionEnabled(EShadingPath /* ShadingPath */) const
{
	return false;
}

void FPICOXRHMDDP::OnBeginRendering_GameThread()
{
	check(IsInGameThread());
	SpectatorScreenController->BeginRenderViewFamily();
}

void FPICOXRHMDDP::OnBeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& ViewFamily)
{
	check(IsInRenderingThread());
	//UpdatePoses();

	check(pBridge);
	pBridge->BeginRendering_RenderThread(RHICmdList);

	check(SpectatorScreenController);
	SpectatorScreenController->UpdateSpectatorScreenMode_RenderThread();

	// Update PlayerOrientation used by StereoLayers positioning
	const FSceneView* MainView = ViewFamily.Views[0];
	const FQuat ViewOrientation = MainView->ViewRotation.Quaternion();
	PlayerOrientation = ViewOrientation * MainView->BaseHmdOrientation.Inverse();
}

FXRRenderBridge* FPICOXRHMDDP::GetActiveRenderBridge_GameThread(bool /* bUseSeparateRenderTarget */)
{
	check(IsInGameThread());

	return pBridge;
}

void FPICOXRHMDDP::CalculateRenderTargetSize(const class FViewport& Viewport, uint32& InOutSizeX, uint32& InOutSizeY)
{
	check(IsInGameThread());

	if (!IsStereoEnabled())
	{
		return;
	}

	InOutSizeX = FMath::CeilToInt(IdealRenderTargetSize.X * PixelDensity);
	InOutSizeY = FMath::CeilToInt(IdealRenderTargetSize.Y * PixelDensity);

	check(InOutSizeX != 0 && InOutSizeY != 0);
}

bool FPICOXRHMDDP::NeedReAllocateViewportRenderTarget(const FViewport& Viewport)
{
	check(IsInGameThread());

	if (IsStereoEnabled())
	{
		const uint32 InSizeX = Viewport.GetSizeXY().X;
		const uint32 InSizeY = Viewport.GetSizeXY().Y;
		const FIntPoint RenderTargetSize = Viewport.GetRenderTargetTextureSizeXY();
		uint32 NewSizeX = InSizeX, NewSizeY = InSizeY;
		CalculateRenderTargetSize(Viewport, NewSizeX, NewSizeY);
		if (NewSizeX != RenderTargetSize.X || NewSizeY != RenderTargetSize.Y)
		{
			return true;
		}
	}
	return false;
}

static const uint32 PICODPSwapChainLength = 1;
#if ENGINE_MINOR_VERSION > 25
bool FPICOXRHMDDP::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ETextureCreateFlags TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples)
#else
bool FPICOXRHMDDP::AllocateRenderTargetTexture(uint32 Index, uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, uint32 Flags, uint32 TargetableTextureFlags, FTexture2DRHIRef& OutTargetableTexture, FTexture2DRHIRef& OutShaderResourceTexture, uint32 NumSamples /*= 1*/)
#endif
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview AllocatedRT!");
	if (!IsStereoEnabled())
	{
		return false;
	}

	TArray<FTextureRHIRef> SwapChainTextures;
	FTextureRHIRef BindingTexture;

	if (pBridge != nullptr && pBridge->GetSwapChain() != nullptr && pBridge->GetSwapChain()->GetTexture2D() != nullptr && pBridge->GetSwapChain()->GetTexture2D()->GetSizeX() == SizeX && pBridge->GetSwapChain()->GetTexture2D()->GetSizeY() == SizeY)
	{
		OutTargetableTexture = (FTexture2DRHIRef&)pBridge->GetSwapChain()->GetTextureRef();
		OutShaderResourceTexture = OutTargetableTexture;
		return true;
	}

	for (uint32 SwapChainIter = 0; SwapChainIter < PICODPSwapChainLength; ++SwapChainIter)
	{
		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef TargetableTexture, ShaderResourceTexture;
#if ENGINE_MINOR_VERSION > 25
		PXR_LOGD(PxrUnreal,"PXR_LivePreview SizeX:%d,SizeY:%d,InTexFlags:%d,CreateFlags:%d,NumSample:%d", SizeX, SizeY, Flags, TargetableTextureFlags, NumSamples);
#endif
		//TargetableTexture = ShaderResourceTexture = RHICreateTexture2D(SizeX, SizeY, PF_R8G8B8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, CreateInfo);

		RHICreateTargetableShaderResource2D(SizeX, SizeY, PF_R8G8B8A8, 1, TexCreate_None, TexCreate_RenderTargetable | TexCreate_Shared, false, CreateInfo, TargetableTexture, ShaderResourceTexture, NumSamples);

		check(TargetableTexture == ShaderResourceTexture);

		SwapChainTextures.Add((FTextureRHIRef&)TargetableTexture);

		if (BindingTexture == nullptr)
		{
			BindingTexture = GDynamicRHI->RHICreateAliasedTexture((FTextureRHIRef&)TargetableTexture);
		}
	}
	TArray<FTextureRHIRef> LeftRHITextureSwapChain = FPICOXRDPManager::GetLeftRHITextureSwapChain();
	TArray<FTextureRHIRef> RightRHITextureSwapChain = FPICOXRDPManager::GetRightRHITextureSwapChain();
	
	pBridge->CreateLeftSwapChain(FPICOXRDPManager::GetLeftBindingTexture(), MoveTemp(LeftRHITextureSwapChain));
	pBridge->CreateRightSwapChain(FPICOXRDPManager::GetRightBindingTexture(), MoveTemp(RightRHITextureSwapChain));

	pBridge->CreateSwapChain(BindingTexture, MoveTemp(SwapChainTextures));

	// These are the same.
	OutTargetableTexture = (FTexture2DRHIRef&)BindingTexture;
	OutShaderResourceTexture = (FTexture2DRHIRef&)BindingTexture;

	return true;
}

FPICOXRHMDDP::FPICOXRHMDDP(const FAutoRegister& AutoRegister, IPICOXRDPModule* InPICODPPlugin) :
	FHeadMountedDisplayBase(nullptr),
	FSceneViewExtensionBase(AutoRegister),
	bHmdEnabled(true),
	HmdWornState(EHMDWornState::Unknown),
	bStereoDesired(false),
	bStereoEnabled(false),
	bOcclusionMeshesBuilt(false),
	WindowMirrorBoundsWidth(2160),
	WindowMirrorBoundsHeight(1200),
	PixelDensity(1.0f),
	HMDWornMovementThreshold(50.0f),
	HMDStartLocation(FVector::ZeroVector),
	BaseOrientation(FQuat::Identity),
	BaseOffset(FVector::ZeroVector),
	bIsQuitting(false),
	QuitTimestamp(),
	bShouldCheckHMDPosition(false),
	RendererModule(nullptr),
	PICODPPlugin(InPICODPPlugin)
{
	Startup();
}

FPICOXRHMDDP::~FPICOXRHMDDP()
{
	Shutdown();
}

bool FPICOXRHMDDP::IsInitialized() const
{
	return true;
}

bool FPICOXRHMDDP::InitializePreviewWindows() const
{
#if WITH_EDITOR
	//user current ScreenResolution as the NewWindowSize
	ULevelEditorPlaySettings* PlaySettingsConfig = GetMutableDefault<ULevelEditorPlaySettings>();
	if (GEngine->IsValidLowLevel())
	{
		const UGameUserSettings* UserSettings = GEngine->GetGameUserSettings();
		FIntPoint ScreenResolution = FIntPoint(1080, 1080);
		if (UserSettings->IsValidLowLevel())
		{
			ScreenResolution = UserSettings->GetScreenResolution();
		}
		PlaySettingsConfig->NewWindowHeight = ScreenResolution.Y;
		PlaySettingsConfig->NewWindowWidth = ScreenResolution.Y;
	}
	return true;
#endif
	return false;
}
#define LOCTEXT_NAMESPACE "FPICOXRHMDModule"

void FPICOXRHMDDP::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Engine", "PICOXRLivePreview Settings",
			LOCTEXT("PICOXRLivePreviewSettingsName", "PICOXR LivePreview Settings"),
			LOCTEXT("PICOXRLivePreviewSettingsDescription", "Configure the PICOXRLivePreview plugin"),
			GetMutableDefault<UPICOXRDPSettings>()
		);

		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	}
}


void FPICOXRHMDDP::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Engine", "PICOXRLivePreview Settings");
	}
}
#undef LOCTEXT_NAMESPACE


bool FPICOXRHMDDP::Startup()
{
	RegisterSettings();
	// grab a pointer to the renderer module for displaying our mirror window
	static const FName RendererModuleName("Renderer");
	RendererModule = FModuleManager::GetModulePtr<IRendererModule>(RendererModuleName);
	PXR_LOGD(PxrUnreal, "PXR_LivePreview startup begin");
	// Re-initialize the plugin if we're canceling the shutdown
	HMDSettings = GetMutableDefault<UPICOXRDPSettings>();
	switch (HMDSettings->GraphicQuality)
	{
		case EGraphicQuality::High:
			{
				IdealRenderTargetSize = FIntPoint(3840, 1920);
			}
			break;
		case EGraphicQuality::Medium:
			{
				IdealRenderTargetSize = FIntPoint(2880, 1440);
			}
			break;
		case EGraphicQuality::Low:
			{
				IdealRenderTargetSize = FIntPoint(1920, 960);
			}
			break;
		default: ;
	}


	static const auto PixelDensityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vr.PixelDensity"));
	if (PixelDensityCVar)
	{
		PixelDensity = 1;
	}
	// enforce finishcurrentframe
	static IConsoleVariable* CFCFVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.finishcurrentframe"));
	CFCFVar->Set(false);

	{
		pBridge = new D3D11Bridge(this);
		ensure(pBridge != nullptr);
	}

	CreateSpectatorScreenController();

	if (!FPICOXRDPManager::InitializeLivePreview())
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview Initialized Failed!");
		return false;
	}
	
	InitializedSucceeded = true;
	
	if (!FPICOXRDPManager::ConnectStreamingServer())
	{
		PXR_LOGD(PxrUnreal, "PXR_LivePreview ConnectServer Failed!");
		return false;
	}

	CopyInfoLeft.Size = FIntVector(1920, 1920, 0);
	CopyInfoLeft.SourcePosition.X=0;
		
	CopyInfoRight.Size = FIntVector(1920, 1920, 0);
	CopyInfoRight.SourcePosition.X=1920;
	OnFovUpdatedFromServiceEvent.BindRaw(this, &FPICOXRHMDDP::OnFovStateChanged);
	FPICOXRDPManager::SetFovUpdatedFromServiceEvent(OnFovUpdatedFromServiceEvent);
	PXR_LOGD(PxrUnreal, "PXR_LivePreview start up finished!");
	return true;
}
void FPICOXRHMDDP::TransferImage_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bLeft, bool bNoAlpha) const
{
	check(IsInRenderingThread());
	bool sRGBSource = false;
	FIntPoint SrcSize;
	FIntPoint DstSize;
	if (SrcTexture->GetTexture2D() && DstTexture->GetTexture2D())
	{
		SrcSize = FIntPoint(SrcTexture->GetTexture2D()->GetSizeX(), SrcTexture->GetTexture2D()->GetSizeY());
		DstSize = FIntPoint(DstTexture->GetTexture2D()->GetSizeX(), DstTexture->GetTexture2D()->GetSizeY());
	}
	else
	{
		return;
	}

	if (SrcRect.IsEmpty())
	{
		SrcRect = FIntRect(FIntPoint::ZeroValue, SrcSize);
	}

	if (DstRect.IsEmpty())
	{
		DstRect = FIntRect(FIntPoint::ZeroValue, DstSize);
	}

	const uint32 viewportWidth = DstRect.Width();
	const uint32 viewportHeight = DstRect.Height();
	float U = SrcRect.Min.X / (float)SrcSize.X;
	float V = SrcRect.Min.Y / (float)SrcSize.Y;
	float USize = SrcRect.Width() / (float)SrcSize.X;
	float VSize = SrcRect.Height() / (float)SrcSize.Y;

	FRHITexture* SrcTextureRHI = SrcTexture;
#if ENGINE_MINOR_VERSION > 25
	RHICmdList.Transition(FRHITransitionInfo(SrcTextureRHI, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
#else
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, &SrcTextureRHI, 1);
#endif
	FGraphicsPipelineStateInitializer GraphicsPSOInit;

	if (bNoAlpha)
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
	}
	else
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
	}

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;

	const auto FeatureLevel = GMaxRHIFeatureLevel;
	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
#if ENGINE_MINOR_VERSION > 24
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#else
	GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
#endif

	if (DstTexture->GetTexture2D())
	{
		sRGBSource &= ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0);
		uint32 NumMips = SrcTexture->GetNumMips();

		for (uint32 MipIndex = 0; MipIndex < NumMips; MipIndex++)
		{
			FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
			RPInfo.ColorRenderTargets[0].MipIndex = MipIndex;

			RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));
			{
				const uint32 ViewportWidth = viewportWidth >> MipIndex;
				const uint32 ViewportHeight = viewportHeight >> MipIndex;
				const FIntPoint TargetSize(viewportWidth, viewportHeight);

				if (bNoAlpha)
				{
					RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0.0f, DstRect.Max.X, DstRect.Max.Y, 1.0f);
					DrawClearQuad(RHICmdList, FLinearColor::Black);
				}

				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				FRHISamplerState* SamplerState = DstRect.Size() == SrcRect.Size() ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();
#if ENGINE_MINOR_VERSION > 24
				if (!sRGBSource)
				{
					TShaderMapRef<FScreenPSMipLevel> PixelShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					PixelShader->SetParameters(RHICmdList, SamplerState, SrcTextureRHI, MipIndex);
				}
				else
				{
					TShaderMapRef<FScreenPSsRGBSourceMipLevel> PixelShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					PixelShader->SetParameters(RHICmdList, SamplerState, SrcTextureRHI, MipIndex);
				}

				RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0.0f, DstRect.Min.X + ViewportWidth, DstRect.Min.Y + ViewportHeight, 1.0f);
#else
				if (!sRGBSource)
				{
					TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					PixelShader->SetParameters(RHICmdList, SamplerState, SrcTextureRHI);
				}
				else {
					TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
					PixelShader->SetParameters(RHICmdList, SamplerState, SrcTextureRHI);
				}

				RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0.0f, DstRect.Max.X, DstRect.Max.Y, 1.0f);
#endif

				if (bLeft)
				{
					RendererModule->DrawRectangle(
						RHICmdList,
						0, 0, ViewportWidth, ViewportHeight,
						U, V, 0.5, VSize,
						TargetSize,
						FIntPoint(1, 1),
#if ENGINE_MINOR_VERSION > 24
						VertexShader,
#else
						* VertexShader,
#endif
						EDRF_Default);
				}
				else
				{
					RendererModule->DrawRectangle(
						RHICmdList,
						0, 0, ViewportWidth, ViewportHeight,
						0.5, V, 0.5, VSize,
						TargetSize,
						FIntPoint(1, 1),
#if ENGINE_MINOR_VERSION > 24
						VertexShader,
#else
						* VertexShader,
#endif
						EDRF_Default);
				}
			}
			RHICmdList.EndRenderPass();
		}
	}
}

void FPICOXRHMDDP::Shutdown()
{
	FPICOXRDPManager::ShutDownLivePreview();
	UnregisterSettings();
}

void FPICOXRHMDDP::OnFovStateChanged(const DeviceFovInfo& FovInfo)
{
	BothFrustum.FovUp = FovInfo.up;
	BothFrustum.FovDown = FovInfo.down;
	BothFrustum.FovLeft = FovInfo.left;
	BothFrustum.FovRight = FovInfo.right;
	PXR_LOGD(PxrUnreal, "PXR_LivePreview OnFovStateChanged FovUp:%f FovDown:%f FovLeft:%f FovRight:%f!",
	         BothFrustum.FovUp,
	         BothFrustum.FovDown,
	         BothFrustum.FovLeft,
	         BothFrustum.FovRight
	);
}

const FPICOXRHMDDP::FTrackingFrame& FPICOXRHMDDP::GetTrackingFrame() const
{
	if (IsInRenderingThread())
	{
		return RenderTrackingFrame;
	}
	else
	{
		return GameTrackingFrame;
	}
}

//necessary, brush the rt on the Spectator screen, which is the window on the PC side
void FPICOXRHMDDP::CreateSpectatorScreenController()
{
	SpectatorScreenController = MakeUnique<FDefaultSpectatorScreenController>(this);
}

FIntRect FPICOXRHMDDP::GetFullFlatEyeRect_RenderThread(FTexture2DRHIRef EyeTexture) const
{
	static FVector2D SrcNormRectMin(0.05f, 0.2f);
	static FVector2D SrcNormRectMax(0.45f, 0.8f);
	return FIntRect(EyeTexture->GetSizeX() * SrcNormRectMin.X, EyeTexture->GetSizeY() * SrcNormRectMin.Y, EyeTexture->GetSizeX() * SrcNormRectMax.X, EyeTexture->GetSizeY() * SrcNormRectMax.Y);
}

void FPICOXRHMDDP::CopyTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* SrcTexture, FIntRect SrcRect, FRHITexture2D* DstTexture, FIntRect DstRect, bool bClearBlack, bool bNoAlpha) const
{
	check(IsInRenderingThread());

	FRHITexture2D* LeftTexture2DRHI = pBridge->GetLeftSwapChain()->GetTexture2D();
	FRHITexture2D* RightTexture2DRHI = pBridge->GetRightSwapChain()->GetTexture2D();

	if (FPICOXRDPManager::IsStreaming())
	{
		if(HMDSettings->GraphicQuality==EGraphicQuality::High)
		{
#if ENGINE_MINOR_VERSION > 25
			RHICmdList.CopyTexture(SrcTexture, LeftTexture2DRHI, CopyInfoLeft);
			RHICmdList.CopyTexture(SrcTexture, RightTexture2DRHI, CopyInfoRight);
#else
			TransferImage_RenderThread(RHICmdList, SrcTexture, FIntRect(), LeftTexture2DRHI, FIntRect(), true, true);
			TransferImage_RenderThread(RHICmdList, SrcTexture, FIntRect(), RightTexture2DRHI, FIntRect(), false, true);
#endif
		}
		else
		{
			TransferImage_RenderThread(RHICmdList, SrcTexture, FIntRect(), LeftTexture2DRHI, FIntRect(), true, true);
			TransferImage_RenderThread(RHICmdList, SrcTexture, FIntRect(), RightTexture2DRHI, FIntRect(), false, true);
		}

		uint32 SwapChainIndex=pBridge->GetLeftSwapChain()->GetSwapChainIndex_RHIThread();
		FPICOXRDPManager::SendMessage(SwapChainIndex);
	}

	const uint32 ViewportWidth = DstRect.Width();
	const uint32 ViewportHeight = DstRect.Height();
	const FIntPoint TargetSize(ViewportWidth, ViewportHeight);

	const float SrcTextureWidth = SrcTexture->GetSizeX();
	const float SrcTextureHeight = SrcTexture->GetSizeY();
	float U = 0.f, V = 0.f, USize = 1.f, VSize = 1.f;
	if (!SrcRect.IsEmpty())
	{
		U = SrcRect.Min.X / SrcTextureWidth;
		V = SrcRect.Min.Y / SrcTextureHeight;
		USize = SrcRect.Width() / SrcTextureWidth;
		VSize = SrcRect.Height() / SrcTextureHeight;
	}
	FRHITexture* SrcTextureRHI = SrcTexture;
#if ENGINE_MINOR_VERSION > 25
	RHICmdList.Transition(FRHITransitionInfo(SrcTexture, ERHIAccess::Unknown, ERHIAccess::SRVGraphics));
#else
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, &SrcTextureRHI, 1);
#endif


	// #todo-renderpasses Possible optimization here - use DontLoad if we will immediately clear the entire target
	FRHIRenderPassInfo RPInfo(DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));
	{
		if (bClearBlack)
		{
			const FIntRect ClearRect(0, 0, DstTexture->GetSizeX(), DstTexture->GetSizeY());
			RHICmdList.SetViewport(ClearRect.Min.X, ClearRect.Min.Y, 0, ClearRect.Max.X, ClearRect.Max.Y, 1.0f);
			DrawClearQuad(RHICmdList, FLinearColor::Black);
		}

		RHICmdList.SetViewport(DstRect.Min.X, DstRect.Min.Y, 0, DstRect.Max.X, DstRect.Max.Y, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = bNoAlpha ? TStaticBlendState<>::GetRHI() : TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		const auto FeatureLevel = GMaxRHIFeatureLevel;
		auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
#if ENGINE_MINOR_VERSION > 24
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
#else
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
#endif
		const bool bSameSize = DstRect.Size() == SrcRect.Size();
		FRHISamplerState* PixelSampler = bSameSize ? TStaticSamplerState<SF_Point>::GetRHI() : TStaticSamplerState<SF_Bilinear>::GetRHI();

#if ENGINE_MINOR_VERSION > 24
		if ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0)
		{
			TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
		}
		else
		{
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
		}
#else
		if ((SrcTexture->GetFlags() & TexCreate_SRGB) != 0)
		{
			TShaderMapRef<FScreenPSsRGBSource> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
		}
		else
		{
			TShaderMapRef<FScreenPS> PixelShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
			PixelShader->SetParameters(RHICmdList, PixelSampler, SrcTexture);
		}
#endif

		RendererModule->DrawRectangle(
			RHICmdList,
			0, 0,
			ViewportWidth, ViewportHeight,
			U, V,
			USize, VSize,
			TargetSize,
			FIntPoint(1, 1),
#if ENGINE_MINOR_VERSION > 24
			VertexShader,
#else
			* VertexShader,
#endif
			EDRF_Default);
	}
	RHICmdList.EndRenderPass();
}

void FPICOXRHMDDP::RenderTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* BackBuffer, FRHITexture2D* SrcTexture, FVector2D WindowSize) const
{
	check(IsInRenderingThread());

	if (bSplashIsShown || !IsBackgroundLayerVisible())
	{
		FRHIRenderPassInfo RPInfo(SrcTexture, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
		{
			DrawClearQuad(RHICmdList, FLinearColor(0, 0, 0, 0));
		}
		RHICmdList.EndRenderPass();
	}

	check(SpectatorScreenController);
	SpectatorScreenController->RenderSpectatorScreen_RenderThread(RHICmdList, BackBuffer, SrcTexture, WindowSize);
}

void FPICOXRHMDDP::PostRenderView_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneView& InView)
{
}
#if ENGINE_MINOR_VERSION >26
bool FPICOXRHMDDP::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return GEngine && GEngine->IsStereoscopic3D(Context.Viewport);
}
#endif


void FPICOXRHMDDP::BridgeBaseImpl::BeginRendering_RenderThread(FRHICommandListImmediate& RHICmdList)
{
}

void FPICOXRHMDDP::BridgeBaseImpl::BeginRendering_RHI()
{
	check(!IsRunningRHIInSeparateThread() || IsInRHIThread());
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	SwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateLeftSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateLeftSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	LeftSwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

void FPICOXRHMDDP::BridgeBaseImpl::CreateRightSwapChain(const FTextureRHIRef& BindingTexture, TArray<FTextureRHIRef>&& SwapChainTextures)
{
	PXR_LOGD(PxrUnreal, "PXR_LivePreview CreateRightSwapChain!");
	check(IsInRenderingThread());
	check(SwapChainTextures.Num());
	RightSwapChain = CreateXRSwapChain(MoveTemp(SwapChainTextures), BindingTexture);
}

bool FPICOXRHMDDP::BridgeBaseImpl::Present(int& SyncInterval)
{
	//This  must return true。
	check(IsRunningRHIInSeparateThread() ? IsInRHIThread() : IsInRenderingThread());

	//necessary, brush the RT to steam
	FinishRendering();
	// Increment swap chain index post-swap.
	SwapChain->IncrementSwapChainIndex_RHIThread();

	if (FPICOXRDPManager::IsStreaming())
	{
		GetLeftSwapChain()->IncrementSwapChainIndex_RHIThread();
		GetRightSwapChain()->IncrementSwapChainIndex_RHIThread();
	}
	SyncInterval = 0;

	return true;
}

bool FPICOXRHMDDP::BridgeBaseImpl::NeedsNativePresent()
{
	//This return value does not affect the PC display
	return true;
}

void FPICOXRHMDDP::BridgeBaseImpl::PostPresent()
{
}

FPICOXRHMDDP::D3D11Bridge::D3D11Bridge(FPICOXRHMDDP* plugin)
	: BridgeBaseImpl(plugin)
{
}

//necessary, brush the RT to steam
void FPICOXRHMDDP::D3D11Bridge::FinishRendering()
{
	// FTexture2DRHIRef Texture2DRHI = SwapChain->GetTextureRef()->GetTexture2D();
	// if (Texture2DRHI)
	// {
	// 	if (GEngine)
	// 	{
	// 		if (GEngine->XRSystem.IsValid())
	// 		{
	// 			ID3D11Device* D3D11Device = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
	// 			ID3D11DeviceContext* D3D11DeviceContext = nullptr;
	//
	// 			D3D11Device->GetImmediateContext(&D3D11DeviceContext);
	// 			if (D3D11DeviceContext)
	// 			{
	// 				
	// 			}
	// 		}
	// 	}
	// }
}


void FPICOXRHMDDP::D3D11Bridge::Reset()
{
}

void FPICOXRHMDDP::D3D11Bridge::UpdateViewport(const FViewport& Viewport, FRHIViewport* InViewportRHI)
{
}

#endif // STEAMVR_SUPPORTED_PLATFORMS
