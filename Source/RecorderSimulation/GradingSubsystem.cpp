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
	// 해당 DayIndex에 해당하는 채점 데이터를 찾는다.
	const FDayData* FoundDay = DayDataSets.FindByPredicate(
		[DayIndex](const FDayData& Day)
		{
			return Day.DayID == DayIndex;
		});

	// 해당 일차 데이터가 없으면 점수는 0점 처리
	if (!FoundDay)
		return 0;

	int32 RawScore = 0;   // 실제 누적 점수
	int32 MaxScore = 0;   // 가능한 최대 점수

	// 해당 일차에 존재하는 모든 키워드의 최대 점수를 계산
	// 키워드 점수 + 문맥 점수를 모두 더해 최대 점수를 만든다
	for (const FKeywordData& KeywordData : FoundDay->Keywords)
	{
		MaxScore += KeywordData.KeywordScore;
		MaxScore += KeywordData.ContextScore;
	}

	// 이미 채점된 키워드를 저장하는 집합
	// 동일 키워드가 여러 입력창에 등장해도 한 번만 점수를 주기 위해 사용
	TSet<int32> ScoredKeywords;

	// 사용자가 입력한 각 기록창을 순회하며 채점 수행
	for (const FString& InputRaw : UserInputs)
	{
		// 앞뒤 공백 제거 및 소문자 변환
		FString Input = InputRaw.TrimStartAndEnd().ToLower();

		// 빈 입력창은 무시
		if (Input.IsEmpty())
			continue;

		int32 FoundKeywordIndex = INDEX_NONE; // 발견된 키워드 인덱스
		int32 KeywordCount = 0;               // 해당 입력창에서 발견된 키워드 개수

		// 현재 입력창에서 등장하는 키워드를 검사
		for (int32 k = 0; k < FoundDay->Keywords.Num(); k++)
		{
			const FString Keyword =
				FoundDay->Keywords[k].Keyword.TrimStartAndEnd().ToLower();

			// 한국어 조사까지 고려한 단어 단위 키워드 검사
			if (ContainsWholeWordKorean(Input, Keyword))
			{
				KeywordCount++;
				FoundKeywordIndex = k;

				// 키워드가 2개 이상 발견되면 더 검사할 필요 없음
				if (KeywordCount > 1)
					break;
			}
		}

		// 키워드가 정확히 1개인 경우만 채점 대상
		// 0개 또는 2개 이상이면 해당 입력창은 0점 처리
		if (KeywordCount != 1)
			continue;

		// 이미 점수를 준 키워드라면 중복 채점 방지
		if (ScoredKeywords.Contains(FoundKeywordIndex))
			continue;


		const FKeywordData& KeywordData = FoundDay->Keywords[FoundKeywordIndex];
		const FString Keyword = KeywordData.Keyword.ToLower();

		// 동일 키워드 여러 번 등장하면 입력창 무효
		if (CountOccurrences(Input, Keyword) > 1)
			continue;


		bool bContextMatched = false; // 문맥 단어가 근처에 존재하는지 여부

		// 입력창에서 키워드 위치 찾기
		int32 KeywordIndex = Input.Find(Keyword);

		// 해당 키워드의 문맥 힌트들을 검사
		for (const FString& HintRaw : KeywordData.ContextHints)
		{
			FString Hint = HintRaw.TrimStartAndEnd().ToLower();

			// 빈 문맥 단어는 무시
			if (Hint.IsEmpty())
				continue;

			int32 SearchIndex = 0;

			// 입력 문자열 내에서 문맥 단어를 반복 검색
			while (true)
			{
				int32 ContextIndex = Input.Find(
					Hint,
					ESearchCase::IgnoreCase,
					ESearchDir::FromStart,
					SearchIndex);

				// 더 이상 문맥 단어가 발견되지 않으면 종료
				if (ContextIndex == INDEX_NONE)
					break;

				// 키워드와 문맥 단어 사이 거리 계산
				int32 Distance = FMath::Abs(ContextIndex - KeywordIndex);

				// 거리가 일정 거리 이하이면 문맥이 가까운 것으로 판단
				if (Distance <= MAX_CONTEXT_DISTACNE)
				{
					bContextMatched = true;
					break;
				}

				// 다음 위치부터 다시 검색
				SearchIndex = ContextIndex + 1;
			}

			// 하나라도 가까운 문맥이 발견되면 추가 검사 중단
			if (bContextMatched)
				break;
		}

		// 키워드 점수 추가
		RawScore += KeywordData.KeywordScore;

		// 문맥이 가까운 위치에 존재하면 문맥 점수 추가
		if (bContextMatched)
			RawScore += KeywordData.ContextScore;

		// 해당 키워드를 채점 완료 목록에 등록
		ScoredKeywords.Add(FoundKeywordIndex);
	}

	int32 FinalScore = 0;

	// 최대 점수가 존재하면 퍼센트 점수 계산
	if (MaxScore > 0)
	{
		FinalScore =
			FMath::RoundToInt((float)RawScore / (float)MaxScore * 100.f);
	}

	// 채점 완료 이벤트 브로드캐스트
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
	int32 StartIndex = Text.Find(Keyword);

	if (StartIndex == INDEX_NONE)
		return false;

	int32 EndIndex = StartIndex + Keyword.Len();

	// 앞 글자 검사
	if (Text.IsValidIndex(StartIndex - 1))
	{
		TCHAR PrevChar = Text[StartIndex - 1];

		if (!IsWordBoundary(PrevChar))
			return false;
	}

	// 뒤 검사
	if (Text.Len() == EndIndex)
		return true;

	if (HasValidParticle(Text, EndIndex))
		return true;

	TCHAR NextChar = Text[EndIndex];

	if (IsWordBoundary(NextChar))
		return true;

	return false;
}

int32 UGradingSubsystem::CountOccurrences(const FString& Text, const FString& Word)
{
	int32 Count = 0;
	int32 SearchIndex = 0;

	while (true)
	{
		int32 Index = Text.Find(
			Word,
			ESearchCase::IgnoreCase,
			ESearchDir::FromStart,
			SearchIndex);

		if (Index == INDEX_NONE)
			break;

		Count++;
		SearchIndex = Index + Word.Len();
	}

	return Count;
}