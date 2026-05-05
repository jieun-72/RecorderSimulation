#include "LoadingScreen.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

void SLoadingScreen::Construct(const FArguments& InArgs)
{
	UE_LOG(LogTemp, Warning, TEXT("SLoadingScreen Construct"));

	ChildSlot
		[
			SNew(SOverlay)

				// 검은 배경 추가
				+ SOverlay::Slot()
				[
					SNew(SBorder)
						.BorderBackgroundColor(FLinearColor::Black)
				]

				// 중앙 텍스트
				+ SOverlay::Slot()
				[
					SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(this, &SLoadingScreen::GetLoadingText)
								.ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 70))
						]
				]
		];
}

FText SLoadingScreen::GetLoadingText() const
{
	FString Dots = FString::ChrN(DotCount, '.');
	return FText::FromString(Dots);
}

void SLoadingScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	TimeAccumulator += InDeltaTime;

	if (TimeAccumulator >= 0.3f)
	{
		DotCount = (DotCount + 1) % 7;
		TimeAccumulator = 0.0f;

		Invalidate(EInvalidateWidgetReason::Paint);
	}
}