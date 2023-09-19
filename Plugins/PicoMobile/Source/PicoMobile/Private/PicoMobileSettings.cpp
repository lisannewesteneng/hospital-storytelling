// Copyright © 2015-2020 Pico Technology Co., Ltd. All Rights Reserved.

#include "PicoMobileSettings.h"
#include "../Launch/Resources/Version.h"
#include "Engine/RendererSettings.h"

UPicoMobileSettings::UPicoMobileSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	  bEnablePSensor(false),
	  bEnableFoveationRendering(false),
	  FoveationLevel(EFoveationLevel::Low),
	  bEnableHomeKey(true),
	  bDisableController6DofModule(false),
	  bDisableHmd6DofModule(false),
	  bEnableNeckModel(true),
	  bUseCustomNeckParameter(false),
	  neckOffset(0, 0.075f, 0.0805f),
	  bEnableVRStandardInput(true),
	  bEnableEyeTracking(false),
	  bEnableEyeTrackingMarker(false),
	  bUseAdvanceInterface(false),
	  refreshRate(ERefreshRate::Default),
	  bUseRecommendedMSAA(false),
	  bUseHWsRGBEncoding(false),
	  bIsForeign(false),
	  bStartTimeEntitlementCheck(false),						
	  EntitlementCheckSimulation(false),
	  bEnableAutoShow(false)
{
}

void UPicoMobileSettings::PostInitProperties()
{
	Super::PostInitProperties();
}

#if WITH_EDITOR
void UPicoMobileSettings::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!bDisableHmd6DofModule ) {
		bUseCustomNeckParameter = false;
	}
	if (!bEnableNeckModel) {
		bUseCustomNeckParameter = false;
	}
	Super::UpdateDefaultConfigFile();
	HandlesRGBHWSupport();
}
void UPicoMobileSettings::HandlesRGBHWSupport()
{
	URendererSettings* const Settings = GetMutableDefault<URendererSettings>();
	static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Mobile.UseHWsRGBEncoding"));

	if (bUseHWsRGBEncoding != Settings->bMobileUseHWsRGBEncoding)
	{
		Settings->bMobileUseHWsRGBEncoding = bUseHWsRGBEncoding;
		Settings->UpdateSinglePropertyInConfigFile(Settings->GetClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(URendererSettings, bMobileUseHWsRGBEncoding)), GetDefaultConfigFilename());
	}

	if (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetInt() != (int)bUseHWsRGBEncoding)
	{
		MobileUseHWsRGBEncodingCVAR->Set((int)bUseHWsRGBEncoding);
	}

}
#endif // #if WITH_EDITOR

