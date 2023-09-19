//Unreal® Engine, Copyright 1998 – 2023, Epic Games, Inc. All rights reserved.

#include "PXR_DPInput.h"
#include "PXR_DPInputState.h"
#include "CoreMinimal.h"
#include "IXRTrackingSystem.h"
#include "PXR_Log.h"


#define LOCTEXT_NAMESPACE "PICOXRDPInput"

FVector FPICOXRDPInput::OriginOffsetL = FVector::ZeroVector;
FVector FPICOXRDPInput::OriginOffsetR = FVector::ZeroVector;

FPICOXRDPInput::FPICOXRDPInput()
	:MessageHandler(new FGenericApplicationMessageHandler())
	, LeftConnectState(false)
	, RightConnectState(false)
	, LeftControllerPower(0)
	, RightControllerPower(0)
	, LeftControllerTouchPoint(FVector2D::ZeroVector)
	, RightControllerTouchPoint(FVector2D::ZeroVector)
	, LeftControllerTriggerValue(0.0f)
	, RightControllerTriggerValue(0.0f)
	, LeftControllerGripValue(0.0f)
	, RightControllerGripValue(0.0f)
	, MainControllerHandle(-1)
	, SourcePosition(FVector::ZeroVector)
	, SourceOrientation(FQuat::Identity)
	, ControllerType(EPICOInputType::Unknown)
{
	SetKeyMapping();
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	if (PICOXRHMD == nullptr)
	{
		static FName SystemName(TEXT("PICOXRPreview"));
		if (GEngine)
		{
			if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
			{
				PICOXRHMD = static_cast<FPICOXRHMDDP*>(GEngine->XRSystem.Get());
				if (PICOXRHMD)
				{
					PXR_LOGD(PxrUnreal,"PXR_LivePreview PICOXRHMD OK on input!");
					if (PICOXRHMD->InitializePreviewWindows())
					{
						PXR_LOGD(PxrUnreal,"PXR_LivePreview PreviewWindows OK!");
					}
				}
			}
		}
	}
}

FPICOXRDPInput::~FPICOXRDPInput()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

void FPICOXRDPInput::Tick(float DeltaTime)
{
	if (PICOXRHMD&&FPICOXRDPManager::IsStreaming())
	{
		LeftConnectState = FPICOXRDPManager::GetControllerConnectionStatus(EControllerHand::Left);
		RightConnectState = FPICOXRDPManager::GetControllerConnectionStatus(EControllerHand::Right);
	}
}

void FPICOXRDPInput::SendControllerEvents()
{
	ProcessButtonEvent();
	ProcessButtonAxis();
}

void FPICOXRDPInput::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

bool FPICOXRDPInput::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

void FPICOXRDPInput::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{

}

void FPICOXRDPInput::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values)
{

}

FName FPICOXRDPInput::GetMotionControllerDeviceTypeName() const
{
	return FName(TEXT("PICOXRDPInput"));
}

bool FPICOXRDPInput::GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	if (PICOXRHMD && FPICOXRDPManager::IsStreaming())
	{
		if (DeviceHand == EControllerHand::Left)
		{
			FPICOXRDPManager::GetControllerPositionAndRotation(DeviceHand, WorldToMetersScale, OutPosition, OutOrientation);
			return true;
		}
		if (DeviceHand == EControllerHand::Right)
		{
			FPICOXRDPManager::GetControllerPositionAndRotation(DeviceHand, WorldToMetersScale, OutPosition, OutOrientation);
			return true;
		}
	}
	return false;
}

ETrackingStatus FPICOXRDPInput::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
{
	if (ControllerIndex == 0 && (DeviceHand == EControllerHand::Left || DeviceHand == EControllerHand::Right || DeviceHand == EControllerHand::AnyHand))
	{
		return ETrackingStatus::Tracked;
	}
	return ETrackingStatus::NotTracked;
}

void FPICOXRDPInput::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values)
{
	
}

void FPICOXRDPInput::GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const
{
	MinFrequency = 0.f;
	MaxFrequency = 1.f;
}

float FPICOXRDPInput::GetHapticAmplitudeScale() const
{
	return 1.0f;
}

int32 FPICOXRDPInput::UPxr_GetControllerPower(int32 Handness)
{
	if (ControllerType == G2)
	{
		return LeftControllerPower;
	}
	else if (ControllerType == Neo2 || ControllerType == Neo3)
	{
		return  Handness == 0 ? LeftControllerPower : RightControllerPower;
	}
	return 0;
}

bool FPICOXRDPInput::UPxr_GetControllerConnectState(int32 Handness)
{
	return  Handness == 0 ? LeftConnectState : RightConnectState;
}

bool FPICOXRDPInput::UPxr_GetControllerMainInputHandle(int32& Handness)
{
	if (MainControllerHandle != -1)
	{
		Handness = MainControllerHandle;
		return true;
	}
	return  false;
}

bool FPICOXRDPInput::UPxr_SetControllerMainInputHandle(int32 InHandness)
{
	return false;
}

void FPICOXRDPInput::SetKeyMapping()
{
#if WITH_EDITOR
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerX] = FPICOKeyNames::PICOTouch_Left_Thumbstick_X;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerY] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Y;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Home] = FPICOKeyNames::PICOTouch_Left_Home_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::App] = FPICOKeyNames::PICOTouch_Left_Menu_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Rocker] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::VolumeUp] = FPICOKeyNames::PICOTouch_Left_VolumeUp_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::VolumeDown] = FPICOKeyNames::PICOTouch_Left_VolumeDown_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Trigger] = FPICOKeyNames::PICOTouch_Left_Trigger_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::AorX] = FPICOKeyNames::PICOTouch_Left_X_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::BorY] = FPICOKeyNames::PICOTouch_Left_Y_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::Grip] = FPICOKeyNames::PICOTouch_Left_Grip_Click;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerUp] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Up;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerDown] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Down;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerLeft] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Left;
	Buttons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOButton::RockerRight] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Right;

	
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerX] = FPICOKeyNames::PICOTouch_Right_Thumbstick_X;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerY] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Y;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Home] = FPICOKeyNames::PICOTouch_Right_Home_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::App] = FPICOKeyNames::PICOTouch_Right_System_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Rocker] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::VolumeUp] = FPICOKeyNames::PICOTouch_Right_VolumeUp_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::VolumeDown] = FPICOKeyNames::PICOTouch_Right_VolumeDown_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Trigger] = FPICOKeyNames::PICOTouch_Right_Trigger_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::AorX] = FPICOKeyNames::PICOTouch_Right_A_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::BorY] = FPICOKeyNames::PICOTouch_Right_B_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::Grip] = FPICOKeyNames::PICOTouch_Right_Grip_Click;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerUp] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Up;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerDown] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Down;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerLeft] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Left;
	Buttons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOButton::RockerRight] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Right;

	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::AorX] = FPICOKeyNames::PICOTouch_Left_X_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::BorY] = FPICOKeyNames::PICOTouch_Left_Y_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Rocker] = FPICOKeyNames::PICOTouch_Left_Thumbstick_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Trigger] = FPICOKeyNames::PICOTouch_Left_Trigger_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::LeftController][(int32)EPICOTouchButton::Thumbrest] = FPICOKeyNames::PICOTouch_Left_Thumbrest_Touch;
	
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::AorX] = FPICOKeyNames::PICOTouch_Right_A_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::BorY] = FPICOKeyNames::PICOTouch_Right_B_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Rocker] = FPICOKeyNames::PICOTouch_Right_Thumbstick_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Trigger] = FPICOKeyNames::PICOTouch_Right_Trigger_Touch;
	TouchButtons[(int32)EPICOXRControllerHandness::RightController][(int32)EPICOTouchButton::Thumbrest] = FPICOKeyNames::PICOTouch_Right_Thumbrest_Touch;
#endif
}

void FPICOXRDPInput::ProcessButtonEvent()
{
	if (LeftConnectState)
	{
		int32 currentStatus = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Left);
		int32 LeftControllerEvent[12] = {0};
		LeftControllerEvent[2] = currentStatus & (uint16)EButtonStatus::kSystemButtonClick;
		LeftControllerEvent[3] = currentStatus & (uint16)EButtonStatus::kMenuButtonClick;
		LeftControllerEvent[4] = currentStatus & (uint16)EButtonStatus::kJoystickClick;
		LeftControllerEvent[9] = currentStatus & (uint16)EButtonStatus::kXAClick;
		LeftControllerEvent[10] = currentStatus & (uint16)EButtonStatus::kYBClick;

		for (int32 i = 2; i < EPICOButton::ButtonCount; i++)
		{
			if (LeftControllerEvent[i] != LastLeftControllerButtonState[i] && i != 7 && i != 8 && i < 11)
			{
				LastLeftControllerButtonState[i] = LeftControllerEvent[i];
				if (LeftControllerEvent[i] > 0)
				{
					MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][i], 0, false);
				}
				else if (LeftControllerEvent[i] == 0)
				{
					MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][i], 0, false);
				}
			}
		}

		//AxisValue
		FPICOXRDPManager::GetControllerAxisValue(EControllerHand::Left, LeftControllerTouchPoint.X, LeftControllerTouchPoint.Y, LeftControllerTriggerValue, LeftControllerGripValue);


		//Trigger Grip Button
		if (LeftControllerTriggerValue > 0.67f && LastLeftControllerButtonState[7] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][7], 0, false);
		}
		else if (LastLeftControllerButtonState[7] > 0 && LeftControllerTriggerValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][7], 0, false);
		}
		LastLeftControllerButtonState[7] = LeftControllerTriggerValue > 0.67f ? 1 : 0;

		if (LeftControllerGripValue > 0.67f && LastLeftControllerButtonState[11] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][11], 0, false);
		}
		else if (LastLeftControllerButtonState[11] > 0 && LeftControllerGripValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][11], 0, false);
		}
		LastLeftControllerButtonState[11] = LeftControllerGripValue > 0.67f ? 1 : 0;


		//Rocker Up/Down/Left/Right
		if (LeftControllerTouchPoint.Y > 0.7f && LastLeftControllerButtonState[12] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][12], 0, false);
		}
		else if (LeftControllerTouchPoint.Y <= 0.7f && LastLeftControllerButtonState[12] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][12], 0, false);
		}
		LastLeftControllerButtonState[12] = LeftControllerTouchPoint.Y > 0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.Y < -0.7f && LastLeftControllerButtonState[13] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][13], 0, false);
		}
		else if (LeftControllerTouchPoint.Y >= -0.7f && LastLeftControllerButtonState[13] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][13], 0, false);
		}
		LastLeftControllerButtonState[13] = LeftControllerTouchPoint.Y < -0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.X < -0.7f && LastLeftControllerButtonState[14] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][14], 0, false);
		}
		else if (LeftControllerTouchPoint.X >= -0.7f && LastLeftControllerButtonState[14] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][14], 0, false);
		}
		LastLeftControllerButtonState[14] = LeftControllerTouchPoint.X < -0.7f ? 1 : 0;

		if (LeftControllerTouchPoint.X > 0.7f && LastLeftControllerButtonState[15] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::LeftController][15], 0, false);
		}
		else if (LeftControllerTouchPoint.X <= 0.7f && LastLeftControllerButtonState[15] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::LeftController][15], 0, false);
		}
		LastLeftControllerButtonState[15] = LeftControllerTouchPoint.X > 0.7f ? 1 : 0;

		int TouchArray[5] = {0};
		TouchArray[0] = currentStatus==(uint16)ETouchButtonStatus::XATouch?1:0;
		TouchArray[1] = currentStatus==(uint16)ETouchButtonStatus::YBTouch?1:0;
		TouchArray[2] = currentStatus==(uint16)ETouchButtonStatus::JoystickTouch?1:0;
		TouchArray[3] = currentStatus==(uint16)ETouchButtonStatus::TriggerTouch?1:0;
		TouchArray[4] = currentStatus==(uint16)ETouchButtonStatus::KBlankTouch?1:0;
		
		for (int32 i = 0; i < EPICOTouchButton::ButtonCount; i++)
		{
			if (TouchArray[i] == 1&&TouchArray[i]!=LastLeftTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonPressed(TouchButtons[EPICOXRControllerHandness::LeftController][i], 0,  false);
			}
			else if (TouchArray[i] == 0&&TouchArray[i]!=LastLeftTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonReleased(TouchButtons[EPICOXRControllerHandness::LeftController][i], 0, false);
			}

			LastLeftTouchButtonState[i]=TouchArray[i];
		}
	}
	if (RightConnectState)
	{
		uint16 currentStatus = FPICOXRDPManager::GetControllerButtonStatus(EControllerHand::Right);
		int RightControllerEvent[12] = {0};
		RightControllerEvent[2] = currentStatus & (uint16)EButtonStatus::kSystemButtonClick;
		RightControllerEvent[3] = currentStatus & (uint16)EButtonStatus::kMenuButtonClick;
		RightControllerEvent[4] = currentStatus & (uint16)EButtonStatus::kJoystickClick;
		RightControllerEvent[9] = currentStatus & (uint16)EButtonStatus::kXAClick;
		RightControllerEvent[10] = currentStatus & (uint16)EButtonStatus::kYBClick;

		for (int32 i = 2; i < EPICOButton::ButtonCount; i++)
		{
			if (RightControllerEvent[i] != LastRightControllerButtonState[i] && i != 7 && i != 8 && i < 11)
			{
				LastRightControllerButtonState[i] = RightControllerEvent[i];
				if (RightControllerEvent[i] > 0)
				{
					MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][i], 0, false);
				}
				else if (RightControllerEvent[i] == 0)
				{
					MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][i], 0, false);
				}
			}
		}

		FPICOXRDPManager::GetControllerAxisValue(EControllerHand::Right, RightControllerTouchPoint.X, RightControllerTouchPoint.Y, RightControllerTriggerValue, RightControllerGripValue);

		//Trigger Grip Button
		if (RightControllerTriggerValue > 0.67f && LastRightControllerButtonState[7] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][7], 0, false);
		}
		else if (LastRightControllerButtonState[7] > 0 && RightControllerTriggerValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][7], 0, false);
		}
		LastRightControllerButtonState[7] = RightControllerTriggerValue > 0.67f ? 1 : 0;

		if (RightControllerGripValue > 0.67f && LastRightControllerButtonState[11] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][11], 0, false);
		}
		else if (LastRightControllerButtonState[11] > 0 && RightControllerGripValue <= 0.67f)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][11], 0, false);
		}
		LastRightControllerButtonState[11] = RightControllerGripValue > 0.67f ? 1 : 0;

		//Rocker Up/Down/Left/Right
		if (RightControllerTouchPoint.Y > 0.7f && LastRightControllerButtonState[12] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][12], 0, false);
		}
		else if (RightControllerTouchPoint.Y <= 0.7f && LastRightControllerButtonState[12] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][12], 0,false);
		}
		LastRightControllerButtonState[12] = RightControllerTouchPoint.Y > 0.7f ? 1 : 0;

		if (RightControllerTouchPoint.Y < -0.7f && LastRightControllerButtonState[13] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][13], 0, false);
		}
		else if (RightControllerTouchPoint.Y >= -0.7f && LastRightControllerButtonState[13] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][13], 0,false);
		}
		LastRightControllerButtonState[13] = RightControllerTouchPoint.Y < -0.7f ? 1 : 0;

		if (RightControllerTouchPoint.X < -0.7f && LastRightControllerButtonState[14] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][14], 0, false);
		}
		else if (RightControllerTouchPoint.X >= -0.7f && LastRightControllerButtonState[14] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][14], 0,false);
		}
		LastRightControllerButtonState[14] = RightControllerTouchPoint.X < -0.7f ? 1 : 0;

		if (RightControllerTouchPoint.X > 0.7f && LastRightControllerButtonState[15] == 0)
		{
			MessageHandler->OnControllerButtonPressed(Buttons[EPICOXRControllerHandness::RightController][15], 0, false);
		}
		else if (RightControllerTouchPoint.X <= 0.7f && LastRightControllerButtonState[15] > 0)
		{
			MessageHandler->OnControllerButtonReleased(Buttons[EPICOXRControllerHandness::RightController][15], 0,false);
		}
		LastRightControllerButtonState[15] = RightControllerTouchPoint.X > 0.7f ? 1 : 0;
		
		int TouchArray[5] = {0};
		TouchArray[0] = currentStatus==(uint16)ETouchButtonStatus::XATouch?1:0;
		TouchArray[1] = currentStatus==(uint16)ETouchButtonStatus::YBTouch?1:0;
		TouchArray[2] = currentStatus==(uint16)ETouchButtonStatus::JoystickTouch?1:0;
		TouchArray[3] = currentStatus==(uint16)ETouchButtonStatus::TriggerTouch?1:0;
		TouchArray[4] = currentStatus==(uint16)ETouchButtonStatus::KBlankTouch?1:0;
		
		for (int32 i = 0; i < EPICOTouchButton::ButtonCount; i++)
		{
			if (TouchArray[i] == 1&&TouchArray[i]!=LastRightTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonPressed(TouchButtons[EPICOXRControllerHandness::RightController][i], 0,  false);
			}
			else if (TouchArray[i] == 0&&TouchArray[i]!=LastRightTouchButtonState[i])
			{
				MessageHandler->OnControllerButtonReleased(TouchButtons[EPICOXRControllerHandness::RightController][i], 0, false);
			}

			LastRightTouchButtonState[i]=TouchArray[i];
		}
		
	}
}

void FPICOXRDPInput::ProcessButtonAxis()
{
#if WITH_EDITOR
	if (LeftConnectState)
	{
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Thumbstick_X, 0, LeftControllerTouchPoint.X);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Thumbstick_Y, 0, LeftControllerTouchPoint.Y);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Trigger_Axis, 0,LeftControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Left_Grip_Axis, 0, LeftControllerGripValue);
	}
	if (RightConnectState)
	{
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Thumbstick_X, 0, RightControllerTouchPoint.X);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Thumbstick_Y, 0, RightControllerTouchPoint.Y);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Trigger_Axis, 0, RightControllerTriggerValue);
		MessageHandler->OnControllerAnalog(FPICOKeyNames::PICOTouch_Right_Grip_Axis, 0, RightControllerGripValue);
	}
#endif
}

void FPICOXRDPInput::AddNonExistingKey(const TArray<FKey>& ExistAllKeys, const FKeyDetails& KeyDetails)
{
	if (!ExistAllKeys.Contains(KeyDetails.GetKey()))
	{
		EKeys::AddKey(KeyDetails);
	}
}

void FPICOXRDPInput::SetHeadPosition(FVector Position)
{
	SourcePosition = Position;
}

void FPICOXRDPInput::SetHeadOrientation(FQuat Orientation)
{
	SourceOrientation = Orientation;
}

#if ENGINE_MINOR_VERSION >25
#define FloatAxis Axis1D
#else
#define FloatAxis FloatAxis
#endif
void FPICOXRDPInput::RegisterKeys()
{
#if WITH_EDITOR
	const FName NAME_PICOTouchController(TEXT("PICOTouch"));
	if (EKeys::GetMenuCategoryDisplayName(NAME_PICOTouchController).ToString()!="PICO Touch")
	{
		EKeys::AddMenuCategoryDisplayInfo(NAME_PICOTouchController, LOCTEXT("PICOTouchSubCategory", "PICO Touch"), TEXT("GraphEditor.PadEvent_16x"));
	}

	const FName NAME_PICOHandController(TEXT("PICOHand"));
	if (EKeys::GetMenuCategoryDisplayName(NAME_PICOHandController).ToString()!="PICO Hand")
	{
		EKeys::AddMenuCategoryDisplayInfo(NAME_PICOHandController, LOCTEXT("PICOHandSubCategory", "PICO Hand"), TEXT("GraphEditor.PadEvent_16x"));
	}
	
	TArray<FKey> ExistAllKeys;
	EKeys::GetAllKeys(ExistAllKeys);
	
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_X_Click, LOCTEXT("PICOTouch_Left_X_Click", "PICO Touch (L) X Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Y_Click, LOCTEXT("PICOTouch_Left_Y_Click", "PICO Touch (L) Y Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_X_Touch, LOCTEXT("PICOTouch_Left_X_Touch", "PICO Touch (L) X Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Y_Touch, LOCTEXT("PICOTouch_Left_Y_Touch", "PICO Touch (L) Y Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Menu_Click, LOCTEXT("PICOTouch_Left_Menu_Click", "PICO Touch (L) Menu"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Grip_Click, LOCTEXT("PICOTouch_Left_Grip_Click", "PICO Touch (L) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Grip_Axis, LOCTEXT("PICOTouch_Left_Grip_Axis", "PICO Touch (L) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Click, LOCTEXT("PICOTouch_Left_Trigger_Click", "PICO Touch (L) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Axis, LOCTEXT("PICOTouch_Left_Trigger_Axis", "PICO Touch (L) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Trigger_Touch, LOCTEXT("PICOTouch_Left_Trigger_Touch", "PICO Touch (L) Trigger Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_X, LOCTEXT("PICOTouch_Left_Thumbstick_X", "PICO Touch (L) Thumbstick X"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Y, LOCTEXT("PICOTouch_Left_Thumbstick_Y", "PICO Touch (L) Thumbstick Y"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Click, LOCTEXT("PICOTouch_Left_Thumbstick_Click", "PICO Touch (L) Thumbstick"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Touch, LOCTEXT("PICOTouch_Left_Thumbstick_Touch", "PICO Touch (L) Thumbstick Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Home_Click, LOCTEXT("PICOTouch_Left_Home_Click", "PICO Touch (L) Home Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_VolumeUp_Click, LOCTEXT("PICOTouch_Left_VolumeUp_Click", "PICO Touch (L) Volume Up Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_VolumeDown_Click, LOCTEXT("PICOTouch_Left_VolumeDown_Click", "PICO Touch (L) Volume Down Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbrest_Touch, LOCTEXT("PICOTouch_Left_Thumbrest_Touch", "PICO Touch (L) Thumbrest Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Up, LOCTEXT("PICOTouch_Left_Thumbstick_Up", "PICO Touch (L) Thumbstick Up"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Down, LOCTEXT("PICOTouch_Left_Thumbstick_Down", "PICO Touch (L) Thumbstick Down"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Left, LOCTEXT("PICOTouch_Left_Thumbstick_Left", "PICO Touch (L) Thumbstick Left"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Left_Thumbstick_Right, LOCTEXT("PICOTouch_Left_Thumbstick_Right", "PICO Touch (L) Thumbstick Right"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_A_Click, LOCTEXT("PICOTouch_Right_A_Click", "PICO Touch (R) A Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_B_Click, LOCTEXT("PICOTouch_Right_B_Click", "PICO Touch (R) B Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_A_Touch, LOCTEXT("PICOTouch_Right_A_Touch", "PICO Touch (R) A Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_B_Touch, LOCTEXT("PICOTouch_Right_B_Touch", "PICO Touch (R) B Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_System_Click, LOCTEXT("PICOTouch_Right_System_Click", "PICO Touch (R) System"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Grip_Click, LOCTEXT("PICOTouch_Right_Grip_Click", "PICO Touch (R) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Grip_Axis, LOCTEXT("PICOTouch_Right_Grip_Axis", "PICO Touch (R) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Click, LOCTEXT("PICOTouch_Right_Trigger_Click", "PICO Touch (R) Trigger"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Axis, LOCTEXT("PICOTouch_Right_Trigger_Axis", "PICO Touch (R) Trigger Axis"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Trigger_Touch, LOCTEXT("PICOTouch_Right_Trigger_Touch", "PICO Touch (R) Trigger Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_X, LOCTEXT("PICOTouch_Right_Thumbstick_X", "PICO Touch (R) Thumbstick X"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Y, LOCTEXT("PICOTouch_Right_Thumbstick_Y", "PICO Touch (R) Thumbstick Y"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Click, LOCTEXT("PICOTouch_Right_Thumbstick_Click", "PICO Touch (R) Thumbstick"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Touch, LOCTEXT("PICOTouch_Right_Thumbstick_Touch", "PICO Touch (R) Thumbstick Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Home_Click, LOCTEXT("PICOTouch_Right_Home_Click", "PICO Touch (R) Home Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_VolumeUp_Click, LOCTEXT("PICOTouch_Right_VolumeUp_Click", "PICO Touch (R) Volume Up Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_VolumeDown_Click, LOCTEXT("PICOTouch_Right_VolumeDown_Click", "PICO Touch (R) Volume Down Press"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbrest_Touch, LOCTEXT("PICOTouch_Right_Thumbrest_Touch", "PICO Touch (R) Thumbrest Touch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Up, LOCTEXT("PICOTouch_Right_Thumbstick_Up", "PICO Touch (R) Thumbstick Up"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Down, LOCTEXT("PICOTouch_Right_Thumbstick_Down", "PICO Touch (R) Thumbstick Down"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Left, LOCTEXT("PICOTouch_Right_Thumbstick_Left", "PICO Touch (R) Thumbstick Left"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOTouch_Right_Thumbstick_Right, LOCTEXT("PICOTouch_Right_Thumbstick_Right", "PICO Touch (R) Thumbstick Right"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOTouch"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_ThumbClick, LOCTEXT("PICOHand_Left_ThumbClick", "PICO Hand (L) Thumb Click"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_IndexPinch, LOCTEXT("PICOHand_Left_IndexPinch", "PICO Hand (L) Index Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_MiddlePinch, LOCTEXT("PICOHand_Left_MiddlePinch", "PICO Hand (L) Middle Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_RingPinch, LOCTEXT("PICOHand_Left_RingPinch", "PICO Hand (L) Ring Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_PinkyPinch, LOCTEXT("PICOHand_Left_PinkyPinch", "PICO Hand (L) Pinky Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_ThumbClick, LOCTEXT("PICOHand_Right_ThumbClick", "PICO Hand (R) Thumb Click"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_IndexPinch, LOCTEXT("PICOHand_Right_IndexPinch", "PICO Hand (R) Index Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_MiddlePinch, LOCTEXT("PICOHand_Right_MiddlePinch", "PICO Hand (R) Middle Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_RingPinch, LOCTEXT("PICOHand_Right_RingPinch", "PICO Hand (R) Ring Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_PinkyPinch, LOCTEXT("PICOHand_Right_PinkyPinch", "PICO Hand (R) Pinky Pinch"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_SystemGesture, LOCTEXT("PICOHand_Left_SystemGesture", "PICO Hand (L) System Gesture"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_SystemGesture, LOCTEXT("PICOHand_Right_SystemGesture", "PICO Hand (R) System Gesture"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_ThumbClickStrength, LOCTEXT("PICOHand_Left_ThumbClickStrength", "PICO Hand (L) Thumb Click Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_IndexPinchStrength, LOCTEXT("PICOHand_Left_IndexPinchStrength", "PICO Hand (L) Index Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_MiddlePinchStrength, LOCTEXT("PICOHand_Left_MiddlePinchStrength", "PICO Hand (L) Middle Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_RingPinchStrength, LOCTEXT("PICOHand_Left_RingPinchStrength", "PICO Hand (L) Ring Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Left_PinkyPinchStrength, LOCTEXT("PICOHand_Left_PinkyPinchStrength", "PICO Hand (L) Pinky Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));

	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_ThumbClickStrength, LOCTEXT("PICOHand_Right_ThumbClickStrength", "PICO Hand (R) Thumb Click Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_IndexPinchStrength, LOCTEXT("PICOHand_Right_IndexPinchStrength", "PICO Hand (R) Index Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_MiddlePinchStrength, LOCTEXT("PICOHand_Right_MiddlePinchStrength", "PICO Hand (R) Middle Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_RingPinchStrength, LOCTEXT("PICOHand_Right_RingPinchStrength", "PICO Hand (R) Ring Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
	AddNonExistingKey(ExistAllKeys,FKeyDetails(FPICOTouchKey::PICOHand_Right_PinkyPinchStrength, LOCTEXT("PICOHand_Right_PinkyPinchStrength", "PICO Hand (R) Pinky Pinch Strength"), FKeyDetails::GamepadKey | FKeyDetails::FloatAxis| FKeyDetails::NotBlueprintBindableKey, "PICOHand"));
#endif
	
}
#undef FloatAxis
#undef LOCTEXT_NAMESPACE
