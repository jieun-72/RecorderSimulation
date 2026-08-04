// Fill out your copyright notice in the Description page of Project Settings.


#include "MySaveGame.h"

void UMySaveGame::SetCurrentDay_Implementation(int32 InDay)
{
	CurrentDay = InDay;
}

int32 UMySaveGame::Req_CurrentDay_Implementation()
{
	return CurrentDay;
}

void UMySaveGame::SetSlotName_Implementation(const FString& InSlotName)
{
	CurrentSlotName = InSlotName;
}

FString UMySaveGame::Req_SlotName_Implementation()
{
	return CurrentSlotName;
}

void UMySaveGame::AddGatheredMemo_Implementation(const TArray<TSoftObjectPtr<UTexture2D>>& NewMemoImages)
{
	GatheredMemoList.Append(NewMemoImages);
}

void UMySaveGame::SetInitGatheredMemo_Implementation()
{
	GatheredMemoList.Empty();
}

TArray<TSoftObjectPtr<UTexture2D>> UMySaveGame::Req_GatheredMemo_Implementation()
{
	return GatheredMemoList;
}

void UMySaveGame::SetDifficulty_Implementation(E_Difficulty InDifficulty)
{
	CurrentDifficulty = InDifficulty;
}

E_Difficulty UMySaveGame::Req_Difficulty_Implementation()
{
	return CurrentDifficulty;
}

void UMySaveGame::SetPlayerName_Implementation(const FString& InName)
{
	PlayerName = InName;
}

FString UMySaveGame::Req_PlayerName_Implementation()
{
	return PlayerName;
}

void UMySaveGame::SetVolume_Implementation(float InVolume)
{
	Volume = InVolume;
}

float UMySaveGame::Req_Volume_Implementation()
{
	return Volume;
}

void UMySaveGame::SetBrightness_Implementation(float InBrightness)
{
	Brightness = InBrightness;
}

float UMySaveGame::Req_Brightness_Implementation()
{
	return Brightness;
}

void UMySaveGame::SetMouseSensitivity_Implementation(float InSensitivity)
{
	MouseSensitivity = InSensitivity;
}

float UMySaveGame::Req_MouseSensitivity_Implementation()
{
	return MouseSensitivity;
}
