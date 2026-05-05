// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "MyGameInstance.generated.h"

/**
 * 
 */
UCLASS()
class RECORDERSIMULATION_API UMyGameInstance : public UGameInstance
{
	GENERATED_BODY()

	public:
		UMyGameInstance();
		virtual void Init() override;

	protected:
		UFUNCTION(BlueprintImplementableEvent)
		void OnPostLoadMapBP(); // BP에서 이어서 처리 가능

	private:
		void BeginLoadingScreen(const FString& MapName);
		void EndLoadingScreen(UWorld* LoadedWorld);
	
};
