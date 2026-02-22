#include "GradingSubsystem.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"


void UGradingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	DayDataSets.Empty();

	FString FilePath = FPaths::ProjectContentDir() + TEXT("System/ScoreSystem/Answers.json");

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		return;

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	TSharedPtr<FJsonObject> JsonObject;

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		return;

	const TArray<TSharedPtr<FJsonValue>>* DaysArray;

	if (!JsonObject->TryGetArrayField(TEXT("Days"), DaysArray))
		return;

	for (const TSharedPtr<FJsonValue>& DayValue : *DaysArray)
	{
		TSharedPtr<FJsonObject> DayObject = DayValue->AsObject();

		FDayData NewDay;
		NewDay.DayID = DayObject->GetIntegerField(TEXT("DayID"));

		const TArray<TSharedPtr<FJsonValue>>* KeywordArray;

		if (DayObject->TryGetArrayField(TEXT("Keywords"), KeywordArray))
		{
			for (const TSharedPtr<FJsonValue>& KeywordValue : *KeywordArray)
			{
				TSharedPtr<FJsonObject> KeywordObject = KeywordValue->AsObject();

				FKeywordData NewKeyword;

				NewKeyword.Keyword =
					KeywordObject->GetStringField(TEXT("Keyword")).TrimStartAndEnd();

				NewKeyword.KeywordScore =
					KeywordObject->GetIntegerField(TEXT("KeywordScore"));

				NewKeyword.ContextScore =
					KeywordObject->GetIntegerField(TEXT("ContextScore"));

				const TArray<TSharedPtr<FJsonValue>>* HintArray;

				if (KeywordObject->TryGetArrayField(TEXT("ContextHints"), HintArray))
				{
					for (const TSharedPtr<FJsonValue>& HintValue : *HintArray)
					{
						FString Hint = HintValue->AsString().TrimStartAndEnd();
						if (!Hint.IsEmpty())
						{
							NewKeyword.ContextHints.Add(Hint);
						}
					}
				}

				NewDay.Keywords.Add(NewKeyword);
			}
		}

		DayDataSets.Add(NewDay);
	}
}

int32 UGradingSubsystem::GradeDay(int32 DayIndex, const TArray<FString>& UserInputs)
{
	const FDayData* FoundDay = DayDataSets.FindByPredicate(
		[DayIndex](const FDayData& Day)
		{
			return Day.DayID == DayIndex;
		}
	);

	if (!FoundDay)
	{
		return 0;
	}

	int32 RawScore = 0;
	int32 MaxScore = 0;

	TSet<FString> CountedKeywords;

	// 최대 점수 계산 (Context는 1회 기준)
	for (const FKeywordData& KeywordData : FoundDay->Keywords)
	{
		MaxScore += KeywordData.KeywordScore;
		MaxScore += KeywordData.ContextScore;
	}

	// 입력창 단위로 평가
	for (FString UserInput : UserInputs)
	{
		UserInput.ReplaceInline(TEXT(" "), TEXT(""));
		UserInput = UserInput.ToLower();

		for (const FKeywordData& KeywordData : FoundDay->Keywords)
		{
			FString CleanKeyword = KeywordData.Keyword;
			CleanKeyword.ReplaceInline(TEXT(" "), TEXT(""));
			CleanKeyword = CleanKeyword.ToLower();

			// 이미 점수 줬으면 스킵
			if (CountedKeywords.Contains(CleanKeyword))
				continue;

			// 같은 입력창 안에 Keyword 있는지 확인
			if (UserInput.Contains(CleanKeyword))
			{
				RawScore += KeywordData.KeywordScore;

				// 같은 입력창 안에서 Context OR 검사
				bool bContextMatched = false;

				for (const FString& Hint : KeywordData.ContextHints)
				{
					FString CleanHint = Hint;
					CleanHint.ReplaceInline(TEXT(" "), TEXT(""));
					CleanHint = CleanHint.ToLower();

					if (UserInput.Contains(CleanHint))
					{
						bContextMatched = true;
						break;
					}
				}

				if (bContextMatched)
				{
					RawScore += KeywordData.ContextScore;
				}

				CountedKeywords.Add(CleanKeyword);
			}
		}
	}

	int32 FinalScore = 0;

	if (MaxScore > 0)
	{
		FinalScore = FMath::RoundToInt((float)RawScore / (float)MaxScore * 100.f);
	}

	OnDayGraded.Broadcast(FinalScore);

	return FinalScore;
}


void UGradingSubsystem::DayStartReady()
{
	OnDayStartReady.Broadcast();
}

int32 UGradingSubsystem::GetAnswerCountByDay(int32 DayIndex) const
{
	const FDayData* FoundDay = DayDataSets.FindByPredicate(
		[DayIndex](const FDayData& Day)
		{
			return Day.DayID == DayIndex;
		});

	if (!FoundDay)
		return 0;

	return FoundDay->Keywords.Num();
}
