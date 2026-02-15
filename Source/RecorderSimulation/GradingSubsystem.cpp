#include "GradingSubsystem.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"


void UGradingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	FString FilePath = FPaths::ProjectContentDir() + TEXT("System\\ScoreSystem\\Answers.json");

	FString JsonString;
	if (FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		TSharedPtr<FJsonObject> JsonObject;

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* DaysArray;

			if (JsonObject->TryGetArrayField(TEXT("Days"), DaysArray))
			{
				for (const TSharedPtr<FJsonValue>& DayValue : *DaysArray)
				{
					TSharedPtr<FJsonObject> DayObject = DayValue->AsObject();

					FDayAnswerSet NewSet;
					NewSet.Day = DayObject->GetIntegerField(TEXT("Day"));

					const TArray<TSharedPtr<FJsonValue>>* AnswersArray;
					if (DayObject->TryGetArrayField(TEXT("Answers"), AnswersArray))
					{
						for (const TSharedPtr<FJsonValue>& Answer : *AnswersArray)
						{
							NewSet.Answers.Add(Answer->AsString());
						}
					}

					DayAnswerSets.Add(NewSet);
				}
			}
		}
	}
}

int32 UGradingSubsystem::GradeDay(int32 DayIndex, const TArray<FString>& UserKeywords)
{
	int32 Score = 0;

	// 해당 Day 정답 찾기
	const FDayAnswerSet* FoundSet = DayAnswerSets.FindByPredicate(
		[DayIndex](const FDayAnswerSet& Set)
		{
			return Set.Day == DayIndex;
		}
	);

	if (!FoundSet)
	{
		UE_LOG(LogTemp, Warning, TEXT("No answer set found for Day %d"), DayIndex);
		return 0;
	}

	// 중복 점수 방지용
	TSet<FString> CountedKeywords;

	// 3채점
	for (const FString& UserInput : UserKeywords)
	{
		for (const FString& Correct : FoundSet->Answers)
		{
			if (UserInput.Contains(Correct, ESearchCase::IgnoreCase)
				&& !CountedKeywords.Contains(Correct))
			{
				Score += 10;
				CountedKeywords.Add(Correct);
			}
		}
	}

	// 결과 브로드캐스트
	OnDayGraded.Broadcast(Score);

	return Score;
}


void UGradingSubsystem::DayStartReady()
{
	OnDayStartReady.Broadcast();
}

int32 UGradingSubsystem::GetAnswerCountByDay(int32 DayIndex) const
{
	for (const FDayAnswerSet& DaySet : DayAnswerSets)
	{
		if (DaySet.Day == DayIndex)
		{
			return DaySet.Answers.Num();
		}
	}

	return 0;
}
