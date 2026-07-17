// Fill out your copyright notice in the Description page of Project Settings.


#include "MyGameInstanceSubsystem.h"
#include "Kismet/GameplayStatics.h"

void UMyGameInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentSaveData = Cast<UMySaveGame>(UGameplayStatics::CreateSaveGameObject(UMySaveGame::StaticClass()));
}

void UMyGameInstanceSubsystem::LoadOrCreateSaveGame(const FString& SlotName)
{
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		CurrentSaveData = Cast<UMySaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0));
	}
	else
	{
		CurrentSaveData = Cast<UMySaveGame>(UGameplayStatics::CreateSaveGameObject(UMySaveGame::StaticClass()));
	}
}