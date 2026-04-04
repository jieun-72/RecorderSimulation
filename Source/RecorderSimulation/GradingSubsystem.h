#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GradingSubsystem.generated.h"

USTRUCT()
struct FKeywordData
{
	GENERATED_BODY()

	TArray<FString> Keywords;
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMidGraded, int32, DayScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMidGradeStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMidGradeSuccess);

UCLASS()
class RECORDERSIMULATION_API UGradingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	int32 GradeDay(int32 DayIndex, const TArray<FString>& UserInputs, bool isDayEnd);

	UFUNCTION(BlueprintCallable)
	void DayStartReady();

	UFUNCTION(BlueprintCallable)
	void MidGradeStart();

	UFUNCTION(BlueprintCallable)
	void MidGradeSuccess();

	UPROPERTY(BlueprintAssignable)
	FOnDayGraded OnDayGraded;

	UPROPERTY(BlueprintAssignable)
	FOnDayStartReady OnDayStartReady;

	UPROPERTY(BlueprintAssignable)
	FOnMidGraded OnMidGraded;

	UPROPERTY(BlueprintAssignable)
	FOnMidGradeStart OnMidGradeStart;

	UPROPERTY(BlueprintAssignable)
	FOnMidGradeSuccess OnMidGradeSuccess;

	UFUNCTION(BlueprintCallable)
	int32 GetAnswerCountByDay(int32 DayIndex) const;

	UFUNCTION(BlueprintCallable)
	int32 GetSuccessScore() const;

private:
	UPROPERTY()
	TArray<FDayData> DayDataSets;
	const int32 SUCCESS_SCORE = 70;

	const int MAX_CONTEXT_DISTACNE = 12;
	static bool IsKoreanChar(TCHAR Char);
	static bool HasValidParticle(const FString& Text, int32 EndIndex);
	static bool IsWordBoundary(TCHAR Char);
	static bool ContainsWholeWordKorean(const FString& Word, const FString& Keyword);
	static int32 CountOccurrences(const FString& Text, const FString& Keyword);
};
