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
		UserInput = UserInput.TrimStartAndEnd().ToLower();

		for (const FKeywordData& KeywordData : FoundDay->Keywords)
		{
			FString CleanKeyword = KeywordData.Keyword.TrimStartAndEnd().ToLower();

			// 이미 점수 줬으면 스킵
			if (CountedKeywords.Contains(CleanKeyword))
				continue;

			if (ContainsWholeWordFlexible(UserInput, CleanKeyword))
			{
				RawScore += KeywordData.KeywordScore;

				bool bContextMatched = false;

				for (const FString& Hint : KeywordData.ContextHints)
				{
					FString CleanHint = Hint.TrimStartAndEnd().ToLower();

					if (ContainsContextFlexible(UserInput, CleanHint))
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


bool UGradingSubsystem::IsKoreanChar(TCHAR Char)
{
	return (Char >= 0xAC00 && Char <= 0xD7A3);
}

bool UGradingSubsystem::IsAllowedParticle(TCHAR Char)
{
	static TSet<TCHAR> Allowed =
	{
		TEXT('은'), TEXT('는'), TEXT('이'), TEXT('가'),
		TEXT('을'), TEXT('를'), TEXT('와'), TEXT('과'),
		TEXT('도'), TEXT('로'), TEXT('에')
	};

	return Allowed.Contains(Char);
}

bool UGradingSubsystem::IsWordBoundary(TCHAR Char)
{
	// 공백
	if (FChar::IsWhitespace(Char))
		return true;

	// 문장 부호
	if (Char == TEXT('.') || Char == TEXT(',') ||
		Char == TEXT('!') || Char == TEXT('?') ||
		Char == TEXT('"') || Char == TEXT('\''))
		return true;

	return false;
}



bool UGradingSubsystem::ContainsWholeWordKorean(const FString& Text, const FString& Keyword)
{
	int32 StartIndex = Text.Find(Keyword);

	if (StartIndex == INDEX_NONE)
		return false;

	int32 EndIndex = StartIndex + Keyword.Len();

	// 앞 경계 체크
	if (Text.IsValidIndex(StartIndex - 1))
	{
		TCHAR PrevChar = Text[StartIndex - 1];

		if (!IsWordBoundary(PrevChar))
		{
			return false;
		}
	}

	// 뒤 경계 체크
	if (Text.IsValidIndex(EndIndex))
	{
		TCHAR NextChar = Text[EndIndex];

		// 조사 허용
		if (IsKoreanChar(NextChar))
		{
			if (!IsAllowedParticle(NextChar))
			{
				return false;
			}
		}
		else if (!IsWordBoundary(NextChar))
		{
			return false;
		}
	}

	return true;
}

bool UGradingSubsystem::ContainsWholeWordFlexible(const FString& Text, const FString& Keyword)
{
	FString CleanKeyword = Keyword.TrimStartAndEnd().ToLower();

	// 일반 문장 기준 검사
	if (ContainsWholeWordKorean(Text, CleanKeyword))
	{
		return true;
	}

	// 공백 제거한 버전 검사
	FString NoSpaceText = Text;
	NoSpaceText.ReplaceInline(TEXT(" "), TEXT(""));

	int32 StartIndex = NoSpaceText.Find(CleanKeyword);
	if (StartIndex == INDEX_NONE)
		return false;

	int32 EndIndex = StartIndex + CleanKeyword.Len();

	// 앞 검사
	//if (NoSpaceText.IsValidIndex(StartIndex - 1))
	//{
	//	TCHAR PrevChar = NoSpaceText[StartIndex - 1];

	//	// 앞이 한글이면 단어 내부일 가능성 높음 → 차단
	//	if (IsKoreanChar(PrevChar) && !IsAllowedParticle(PrevChar))
	//	{
	//		return false;
	//	}
	//}

	// 뒤 검사
	if (NoSpaceText.IsValidIndex(EndIndex))
	{
		TCHAR NextChar = NoSpaceText[EndIndex];

		if (IsKoreanChar(NextChar) && !IsAllowedParticle(NextChar))
		{
			return false;
		}
	}

	return true;
}

bool UGradingSubsystem::ContainsContextFlexible(const FString& Text, const FString& Hint)
{
	FString NormalText = Text.ToLower();
	FString CleanHint = Hint.ToLower();

	if (CleanHint.Len() < 2)
		return false; // 1글자 힌트는 오탐 위험

	// 공백 포함 검사
	if (NormalText.Contains(CleanHint))
		return true;

	// 공백 제거 검사
	FString NoSpaceText = NormalText;
	NoSpaceText.ReplaceInline(TEXT(" "), TEXT(""));

	return NoSpaceText.Contains(CleanHint);
}