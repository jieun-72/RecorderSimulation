#include "MyGameInstance.h"
#include "MoviePlayer.h"
#include "LoadingScreen.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameUserSettings.h"

UMyGameInstance::UMyGameInstance()
{
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UMyGameInstance::BeginLoadingScreen);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMyGameInstance::EndLoadingScreen);
}

void UMyGameInstance::Init()
{
	Super::Init();

	UE_LOG(LogTemp, Warning, TEXT("GameInstance Init"));

	UGameUserSettings* Settings = GEngine->GetGameUserSettings();

	if (Settings)
	{
		// 현재 모니터 해상도 가져오기
		FIntPoint DesktopResolution = Settings->GetDesktopResolution();

		// 해상도 적용
		Settings->SetScreenResolution(DesktopResolution);

		// 보더리스 전체화면 (추천)
		Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);

		// 적용
		Settings->ApplySettings(false);

		UE_LOG(LogTemp, Warning, TEXT("Resolution Applied: %d x %d"),
			DesktopResolution.X,
			DesktopResolution.Y);
	}
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