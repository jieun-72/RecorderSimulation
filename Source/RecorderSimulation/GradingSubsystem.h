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

USTRUCT(BlueprintType)
struct FCandidateAnswer
{
	GENERATED_BODY()

	UPROPERTY()
	FString Context;

	UPROPERTY()
	FString Keyword;
};

USTRUCT(BlueprintType)
struct FCandidateDay
{
	GENERATED_BODY()

	UPROPERTY()
	int32 DayID;

	UPROPERTY()
	TArray<FCandidateAnswer> Answers;
};

USTRUCT(BlueprintType)
struct FCandidateFakePool
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FString> Contexts;

	UPROPERTY()
	TArray<FString> Keywords;
};


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDayGraded, int32, DayScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDayStartReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMidGradeStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMidGraded, int32, DayScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMidGradeSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnHintStart);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHintArrived, FString, HintKeyword);

UCLASS()
class RECORDERSIMULATION_API UGradingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintCallable)
	int32 GradeDay(int32 DayIndex, const TArray<FString>& UserInputs, bool isDayEnd);

	UFUNCTION(BlueprintCallable)
	FString GetRandomHintKeyword(int32 DayIndex, const TArray<FString>& UserInputs);

	UFUNCTION(BlueprintCallable)
	int32 CandidateGradeDay(int32 DayIndex, const TArray<FString>& UserInputs, bool isDayEnd);


	UFUNCTION(BlueprintCallable)
	void DayStartReady();

	UFUNCTION(BlueprintCallable)
	void MidGradeStart();

	UFUNCTION(BlueprintCallable)
	void MidGradeSuccess();

	UFUNCTION(BlueprintCallable)
	void HintStart();

	UPROPERTY(BlueprintAssignable)
	FOnDayGraded OnDayGraded;

	UPROPERTY(BlueprintAssignable)
	FOnDayStartReady OnDayStartReady;

	UPROPERTY(BlueprintAssignable)
	FOnMidGradeStart OnMidGradeStart;

	UPROPERTY(BlueprintAssignable)
	FOnMidGraded OnMidGraded;

	UPROPERTY(BlueprintAssignable)
	FOnMidGradeSuccess OnMidGradeSuccess;

	UPROPERTY(BlueprintAssignable)
	FOnHintStart OnHintStart;

	UPROPERTY(BlueprintAssignable)
	FOnHintArrived OnHintArrived;


	UFUNCTION(BlueprintCallable)
	int32 GetAnswerCountByDay(int32 DayIndex) const;

	UFUNCTION(BlueprintCallable)
	int32 GetCandidateCountByDay(int32 DayIndex) const;

	UFUNCTION(BlueprintCallable)
	int32 GetSuccessScore() const;

	UFUNCTION(BlueprintCallable)
	TArray<FString> GetCandidateKeywords(
		int32 DayIndex,
		int32 TotalCount);

	UFUNCTION(BlueprintCallable)
	TArray<FString> GetCandidateContexts(
		int32 DayIndex,
		int32 TotalCount);


private:
	const int32 SUCCESS_SCORE = 80;
	const int32 KEYWORD_SCORE = 5;
	const int32 CONTEXT_SCORE = 10;

	UPROPERTY()
	TArray<FDayData> DayDataSets;

	UPROPERTY()
	TArray<FCandidateDay> CandidateDays;

	UPROPERTY()
	FCandidateFakePool CandidateFakePool;

	void LoadAnswerData();
	void LoadCandidateData();

	const int MAX_CONTEXT_DISTACNE = 12;
	static bool IsKoreanChar(TCHAR Char);
	static bool HasValidParticle(const FString& Text, int32 EndIndex);
	static bool IsWordBoundary(TCHAR Char);
	static bool ContainsWholeWordKorean(const FString& Word, const FString& Keyword);
	static int32 CountOccurrences(const FString& Text, const FString& Keyword);
};
