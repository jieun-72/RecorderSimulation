#include "MyGameInstance.h"
#include "MoviePlayer.h"
#include "LoadingScreen.h"
#include "Containers/Ticker.h"

UMyGameInstance::UMyGameInstance()
{
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMyGameInstance::BeginLoadingScreen);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMyGameInstance::EndLoadingScreen);
}

void UMyGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogTemp, Warning, TEXT("GameInstance Init"));
}

void UMyGameInstance::BeginLoadingScreen(const FString& MapName)
{
	UE_LOG(LogTemp, Warning, TEXT("Loading Screen Start"));
	UE_LOG(LogTemp, Warning, TEXT("Loading Map: %s"), *MapName);

	if (IsRunningDedicatedServer()) return;

	FLoadingScreenAttributes LoadingScreen;
	LoadingScreen.bAutoCompleteWhenLoadingCompletes = false;
	LoadingScreen.bWaitForManualStop = true;
	LoadingScreen.MinimumLoadingScreenDisplayTime = 1.0f;
	LoadingScreen.bAllowEngineTick = true;
	LoadingScreen.WidgetLoadingScreen = SNew(SLoadingScreen);

	GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);

	// 렌더 루프 확실히 시작
	if (!GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		GetMoviePlayer()->PlayMovie();
	}
}

void UMyGameInstance::EndLoadingScreen(UWorld* LoadedWorld)
{
	UE_LOG(LogTemp, Warning, TEXT("Loading Screen End"));

	if (GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		GetMoviePlayer()->StopMovie();
	}

	OnPostLoadMapBP();
}