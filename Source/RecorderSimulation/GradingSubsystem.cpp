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

				const TArray<TSharedPtr<FJsonValue>>* KeywordsArray;
				if (KeywordObject->TryGetArrayField(TEXT("Keywords"), KeywordsArray))
				{
					for (const TSharedPtr<FJsonValue>& K : *KeywordsArray)
					{
						FString KeywordStr = K->AsString().TrimStartAndEnd();
						if (!KeywordStr.IsEmpty())
						{
							NewKeyword.Keywords.Add(KeywordStr);
						}
					}
				}

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

int32 UGradingSubsystem::GradeDay(int32 DayIndex, const TArray<FString>& UserInputs, bool isDayEnd)
{
	const FDayData* FoundDay = DayDataSets.FindByPredicate(
		[DayIndex](const FDayData& Day)
		{
			return Day.DayID == DayIndex;
		});

	// 해당 일차 데이터 없으면 0점 처리
	if (!FoundDay)
		return 0;

	int32 RawScore = 0; // 실제 누적 점수
	int32 MaxScore = 0; // 가능한 최대 점수

	// 해당 일차에 존재하는 모든 키워드의 최대 점수를 계산
	// 키워드 점수 + 문맥 점수를 모두 더해 최대 점수를 만듦
	for (const FKeywordData& KeywordData : FoundDay->Keywords)
	{
		MaxScore += KeywordData.KeywordScore;
		MaxScore += KeywordData.ContextScore;
	}

	// 이미 채점된 키워드를 저장하는 집합
	// 동일 키워드가 여러 입력창에 등장해도 한 번만 점수를 주기 위해 사용
	TSet<int32> ScoredKeywords;

	// 입력 전처리 미리
	TArray<FString> CleanInputs;
	for (const FString& InputRaw : UserInputs)
	{
		// 앞뒤 공백 제거 및 소문자 변환
		CleanInputs.Add(InputRaw.TrimStartAndEnd().ToLower());
	}

	for (const FString& Input : CleanInputs)
	{
		if (Input.IsEmpty())
			continue;

		int32 FoundKeywordIndex = INDEX_NONE;
		int32 KeywordCount = 0;
		FString MatchedKeyword;

		// 키워드 탐색 (최소화)
		for (int32 k = 0; k < FoundDay->Keywords.Num(); k++)
		{
			const FKeywordData& KeywordData = FoundDay->Keywords[k];

			bool bMatched = false;

			for (const FString& KeywordRaw : KeywordData.Keywords)
			{
				FString Keyword = KeywordRaw.TrimStartAndEnd().ToLower();

				// 한국어 조사까지 고려한 단어 단위 키워드 검사
				if (ContainsWholeWordKorean(Input, Keyword))
				{
					bMatched = true;
					MatchedKeyword = Keyword;
					break;
				}
			}

			if (bMatched)
			{
				KeywordCount++;
				FoundKeywordIndex = k;

				if (KeywordCount > 1)
					break;
			}
		}


		UE_LOG(LogTemp, Warning, TEXT("KeywordCount: %d"), KeywordCount);

		// 한 기록창에 키워드 여러 개면 점수 미부여
		if (KeywordCount != 1)
			continue;

		// 이미 작성했던 키워드라면 점수 미부여 (중복 점수 부여 방지)
		if (ScoredKeywords.Contains(FoundKeywordIndex))
			continue;


		const FKeywordData& KeywordData = FoundDay->Keywords[FoundKeywordIndex];

		// 동일 키워드 한 입력창에 여러 개 있는지 확인
		int32 OccurrenceCount = 0;

		for (const FString& KeywordRaw : KeywordData.Keywords)
		{
			FString Keyword = KeywordRaw.TrimStartAndEnd().ToLower();
			OccurrenceCount += CountOccurrences(Input, Keyword);
		}

		if (OccurrenceCount > 1)
			continue;


		// 입력창에서 키워드 위치 찾기
		int32 KeywordIndex = Input.Find(MatchedKeyword);
		if (KeywordIndex == INDEX_NONE)
			continue;


		bool bContextMatched = false; // 문맥 단어가 근처에 존재하는지 여부

		// Context 검사 (while 제거)
		for (const FString& HintRaw : KeywordData.ContextHints)
		{
			FString Hint = HintRaw.TrimStartAndEnd().ToLower();
			if (Hint.IsEmpty())
				continue;

			int32 ContextIndex = Input.Find(Hint);

			if (ContextIndex != INDEX_NONE)
			{
				if (Hint.Len() <= 1)
					continue;

				UE_LOG(LogTemp, Warning, TEXT("Context Find → %s"), *MatchedKeyword);

				int32 Distance = FMath::Abs(ContextIndex - KeywordIndex);

				// 키워드와의 거리 차이가 너무 멀면 그 키워드에 대한 설명글이 아닌 것으로 간주해 가산점 미부여
				if (Distance <= MAX_CONTEXT_DISTACNE)
				{
					bContextMatched = true;
					break;
				}
			}
		}

		RawScore += KeywordData.KeywordScore;

		if (bContextMatched)
		{
			RawScore += KeywordData.ContextScore;
		}

		ScoredKeywords.Add(FoundKeywordIndex);
	}

	int32 FinalScore = 0;

	if (MaxScore > 0)
	{
		FinalScore = FMath::RoundToInt((float)RawScore / (float)MaxScore * 100.f);
	}

	if (isDayEnd) OnDayGraded.Broadcast(FinalScore);
	else OnMidGraded.Broadcast(FinalScore);

	return FinalScore;
}


FString UGradingSubsystem::GetRandomHintKeyword(int32 DayIndex, const TArray<FString>& UserInputs)
{
	FString HintKeyword = TEXT("");

	const FDayData* FoundDay = DayDataSets.FindByPredicate(
		[DayIndex](const FDayData& Day)
		{
			return Day.DayID == DayIndex;
		});

	if (!FoundDay)
	{
		OnHintArrived.Broadcast(HintKeyword);
		return HintKeyword;
	}

	// 입력 전처리
	TArray<FString> CleanInputs;
	for (const FString& InputRaw : UserInputs)
	{
		CleanInputs.Add(InputRaw.TrimStartAndEnd().ToLower());
	}

	// 아직 맞추지 못한 키워드 인덱스 모음
	TArray<int32> UnsolvedKeywordIndices;

	for (int32 k = 0; k < FoundDay->Keywords.Num(); k++)
	{
		const FKeywordData& KeywordData = FoundDay->Keywords[k];

		bool bSolved = false;

		// 모든 입력을 돌면서 검사
		for (const FString& Input : CleanInputs)
		{
			if (Input.IsEmpty())
				continue;

			// 키워드 후보 중 하나라도 맞으면 solved
			for (const FString& KeywordRaw : KeywordData.Keywords)
			{
				FString Keyword = KeywordRaw.TrimStartAndEnd().ToLower();

				if (ContainsWholeWordKorean(Input, Keyword))
				{
					bSolved = true;
					break;
				}
			}

			if (bSolved)
				break;
		}

		if (!bSolved)
		{
			UnsolvedKeywordIndices.Add(k);
		}
	}

	// 전부 맞췄으면 빈 문자열 반환
	if (UnsolvedKeywordIndices.Num() == 0)
	{
		OnHintArrived.Broadcast(HintKeyword);
		return HintKeyword;
	}

	// 랜덤 선택
	int32 RandomIndex = FMath::RandHelper(UnsolvedKeywordIndices.Num());
	int32 SelectedKeywordIndex = UnsolvedKeywordIndices[RandomIndex];

	const FKeywordData& SelectedKeyword = FoundDay->Keywords[SelectedKeywordIndex];

	// 첫 번째 후보 반환
	if (SelectedKeyword.Keywords.Num() > 0)
	{
		HintKeyword = SelectedKeyword.Keywords[0];
	}

	OnHintArrived.Broadcast(HintKeyword);

	return HintKeyword;
}


void UGradingSubsystem::DayStartReady()
{
	OnDayStartReady.Broadcast();
}

void UGradingSubsystem::MidGradeStart()
{
	OnMidGradeStart.Broadcast();
}

void UGradingSubsystem::MidGradeSuccess()
{
	OnMidGradeSuccess.Broadcast();
}

void UGradingSubsystem::HintStart()
{
	OnHintStart.Broadcast();
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

int32 UGradingSubsystem::GetSuccessScore() const
{
	return SUCCESS_SCORE;
}




bool UGradingSubsystem::IsKoreanChar(TCHAR Char)
{
	return (Char >= 0xAC00 && Char <= 0xD7A3);
}

bool UGradingSubsystem::HasValidParticle(
	const FString& Text,
	int32 EndIndex)
{
	static const TArray<FString> Particles =
	{
		TEXT("은"), TEXT("는"),
		TEXT("이"), TEXT("가"),
		TEXT("을"), TEXT("를"),
		TEXT("도"), TEXT("로"),
		TEXT("에"), TEXT("와"), TEXT("과"),

		TEXT("에서"),
		TEXT("으로"),
		TEXT("에게"),
		TEXT("까지"),
		TEXT("부터"),
		TEXT("처럼"),
		TEXT("만큼"),
		TEXT("마다"),
		TEXT("보다")
	};

	for (const FString& Particle : Particles)
	{
		if (Text.Mid(EndIndex).StartsWith(Particle))
			return true;
	}

	return false;
}

bool UGradingSubsystem::IsWordBoundary(TCHAR Char)
{
	if (FChar::IsWhitespace(Char))
		return true;

	if (Char == TEXT('.') ||
		Char == TEXT(',') ||
		Char == TEXT('!') ||
		Char == TEXT('?') ||
		Char == TEXT('"') ||
		Char == TEXT('\''))
		return true;

	return false;
}


bool UGradingSubsystem::ContainsWholeWordKorean(
	const FString& Text,
	const FString& Keyword)
{
	UE_LOG(LogTemp, Warning, TEXT("---- Contains Check ----"));
	UE_LOG(LogTemp, Warning, TEXT("Text: [%s], Keyword: [%s]"), *Text, *Keyword);

	int32 StartIndex = Text.Find(Keyword);

	UE_LOG(LogTemp, Warning, TEXT("StartIndex: %d"), StartIndex);

	if (StartIndex == INDEX_NONE) {
		UE_LOG(LogTemp, Error, TEXT("Keyword NOT FOUND in Text"));
		return false;
	}


	int32 EndIndex = StartIndex + Keyword.Len();

	// 앞 글자 검사
	if (Text.IsValidIndex(StartIndex - 1))
	{
		TCHAR PrevChar = Text[StartIndex - 1];

		UE_LOG(LogTemp, Warning,
			TEXT("PrevChar: %c, IsBoundary: %d"),
			PrevChar,
			IsWordBoundary(PrevChar));

		if (!IsWordBoundary(PrevChar))
			return false;
	}

	// 뒤 검사
	/*if (Text.Len() == EndIndex)
		return true;

	if (HasValidParticle(Text, EndIndex))
		return true;

	TCHAR NextChar = Text[EndIndex];

	UE_LOG(LogTemp, Warning,
		TEXT("NextChar: %c, Boundary: %d"),
		NextChar,
		IsWordBoundary(NextChar));

	if (IsWordBoundary(NextChar))
		return true;

	return false;*/

	return true;
}

int32 UGradingSubsystem::CountOccurrences(const FString& Text, const FString& Keyword)
{
	if (Keyword.IsEmpty() || Text.IsEmpty())
		return 0;

	int32 Count = 0;
	int32 SearchIndex = 0;

	while (SearchIndex < Text.Len())
	{
		int32 FoundIndex = Text.Find(
			Keyword,
			ESearchCase::IgnoreCase,
			ESearchDir::FromStart,
			SearchIndex
		);

		if (FoundIndex == INDEX_NONE)
			break;

		Count++;

		// 다음 탐색 위치 이동 (무한 루프 방지)
		SearchIndex = FoundIndex + Keyword.Len();
	}

	return Count;
}