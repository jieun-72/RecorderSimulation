// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
// 세이브 게임 .h 포함
#include "MySaveGame.h"
#include "MyGameInstanceSubsystem.generated.h"

/**
 * 
 */
UCLASS()
class RECORDERSIMULATION_API UMyGameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	

public:
	
	// 게임 켜질 때 자동으로 한 번 실행되는 초기화 함수
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// 실제 세이브 데이터 본체
	UPROPERTY(BlueprintReadWrite, Category = "SaveSystem")
	UMySaveGame* CurrentSaveData;

	UFUNCTION(BlueprintCallable, Category = "SaveSystem")
	void LoadOrCreateSaveGame(const FString& SlotName);
};
