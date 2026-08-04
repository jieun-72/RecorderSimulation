// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Engine/Texture2D.h"
#include "SaveGameInterface.generated.h"


UENUM(BlueprintType)
enum class E_Difficulty : uint8
{
	Easy UMETA(DisplayName = "Easy"),
	Hard UMETA(DisplayName = "Hard")
};

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class USaveGameInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class RECORDERSIMULATION_API ISaveGameInterface
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	
	// day
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetCurrentDay(int32 InDay);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	int32 Req_CurrentDay();


	// slot name
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetSlotName(const FString& InSlotName);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	FString Req_SlotName();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void AddGatheredMemo(const TArray<TSoftObjectPtr<UTexture2D>>& NewMemoImages);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetInitGatheredMemo();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	TArray<TSoftObjectPtr<UTexture2D>> Req_GatheredMemo();

	// Gathered Memo
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetDifficulty(E_Difficulty InDifficulty);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	E_Difficulty Req_Difficulty();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetPlayerName(const FString& InName);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	FString Req_PlayerName();


	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetVolume(float InVolume);


	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	float Req_Volume();


	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetBrightness(float InBrightness);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	float Req_Brightness();

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	void SetMouseSensitivity(float InSens);
	
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "SaveSystem")
	float Req_MouseSensitivity();

};
