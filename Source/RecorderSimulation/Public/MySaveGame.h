// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Engine/Texture2D.h"
#include "SaveGameInterface.h"
#include "MySaveGame.generated.h"

/**
 * 
 */
UCLASS()
class RECORDERSIMULATION_API UMySaveGame : public USaveGame, public ISaveGameInterface
{
	GENERATED_BODY()


private:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		int32 CurrentDay = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		FString CurrentSlotName;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		TArray<UTexture2D*> GatheredMemoList;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		E_Difficulty CurrentDifficulty;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		FString PlayerName;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		float Volume;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		float Brightness;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SaveData", meta = (AllowPrivateAccess = "true"))
		float MouseSensitivity;

	
public:
	virtual void SetCurrentDay_Implementation(int32 InDay) override;
	virtual int32 Req_CurrentDay_Implementation() override;

	virtual void SetSlotName_Implementation(const FString& InSlotName) override;
	virtual FString Req_SlotName_Implementation() override;

	virtual void AddGatheredMemo_Implementation(const TArray<UTexture2D*>& NewMemoImages) override;
	virtual void SetInitGatheredMemo_Implementation() override;
	virtual TArray<UTexture2D*> Req_GatheredMemo_Implementation() override;

	virtual void SetDifficulty_Implementation(E_Difficulty InDifficulty) override;
	virtual E_Difficulty Req_Difficulty_Implementation() override;

	virtual void SetPlayerName_Implementation(const FString& InName) override;
	virtual FString Req_PlayerName_Implementation() override;

	virtual void SetVolume_Implementation(float InVolume) override;
	virtual float Req_Volume_Implementation() override;

	virtual void SetBrightness_Implementation(float InBrightness) override;
	virtual float Req_Brightness_Implementation() override;

	virtual void SetMouseSensitivity_Implementation(float InSensitivity) override;
	virtual float Req_MouseSensitivity_Implementation() override;
};
