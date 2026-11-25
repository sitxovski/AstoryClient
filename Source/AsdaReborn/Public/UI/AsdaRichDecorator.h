// Copyright (c) 2026 @sitxovski (Discord)
// Кастомный rich-декоратор: run <a ...> → inline STextBlock с игровым шрифтом AsdaUIFontRT и стилем из
//   атрибутов (c=цвет RRGGBB, b=псевдо-bold, o=outline, d=shadow, s=размер). Реверс тегов FC/FS/FB/FO/FD.
#pragma once
#include "CoreMinimal.h"
#include "Components/RichTextBlockDecorator.h"
#include "AsdaRichDecorator.generated.h"

// Фабрика декоратора для URichTextBlock (регистрируется через SetDecorators).
UCLASS()
class ASDAREBORN_API UAsdaRichDecorator : public URichTextBlockDecorator
{
	GENERATED_BODY()
public:
	UAsdaRichDecorator(const FObjectInitializer& ObjectInitializer);
	virtual TSharedPtr<ITextDecorator> CreateDecorator(URichTextBlock* InOwner) override;
};
