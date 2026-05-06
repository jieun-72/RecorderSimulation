#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SLoadingScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLoadingScreen) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText GetLoadingText() const;

	mutable int32 DotCount = 0;
	float TimeAccumulator = 0.0f;
};