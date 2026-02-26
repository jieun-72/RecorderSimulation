#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GradingSubsystem.generated.h"

USTRUCT()
struct FKeywordData
{
	GENERATED_BODY()

	FString Keyword;
	TArray<FString> ContextHints;
	int32 KeywordScore;
	int32 ContextScore;
};

USTRUCT()
struct FDayData
{
	GENERATED_BODY()

	int32 DayID;
	TArray<FKeywordData> Keywords;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayGraded, int32, DayScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDayStartReady);

UCLASS()
class RECORDERSIMULATION_API UGradingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	int32 GradeDay(int32 DayIndex, const TArray<FString>& UserKeywords);

	UFUNCTION(BlueprintCallable)
	void DayStartReady();

	UPROPERTY(BlueprintAssignable)
	FOnDayGraded OnDayGraded;

	UPROPERTY(BlueprintAssignable)
	FOnDayStartReady OnDayStartReady;

	UFUNCTION(BlueprintCallable)
	int32 GetAnswerCountByDay(int32 DayIndex) const;

private:
	UPROPERTY()
	TArray<FDayData> DayDataSets;

	static bool IsKoreanChar(TCHAR Char);
	static bool IsAllowedParticle(TCHAR Char);
	static bool IsWordBoundary(TCHAR Char);
	static bool ContainsWholeWordKorean(const FString& Text, const FString& Keyword);
	static bool ContainsWholeWordFlexible(const FString& Text, const FString& Keyword);
	static bool ContainsContextFlexible(const FString& Text, const FString& Hint);
};
