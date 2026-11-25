// Copyright (c) 2026 @sitxovski (Discord)
#include "UI/AsdaRichDecorator.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ D1: единая sRGB→linear конверсия (дедуп 2026-08-08)
#include "Components/RichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Text/ITextDecorator.h"
#include "HAL/IConsoleManager.h"

// ★★★ ОТКАТ (2026-08-09): 1 — вернуть прежний путь «сегмент = inline-ВИДЖЕТ» со всеми его
//   последствиями (см. разбор ниже). 0 (умолчание) — сегмент = обычный ТЕКСТОВЫЙ run.
static ASDA_DEV_CVAR(int32, CVarRichLegacyWidgetRuns,
	TEXT("asda.UI.RichLegacyWidgetRuns"), 0,
	TEXT("1 = сегменты rich-текста рисовать inline-ВИДЖЕТАМИ (прежнее поведение: НЕ переносятся и ")
	TEXT("наезжают строками). 0 = обычные текстовые run'ы (перенос и межстрочный интервал работают)."),
	ECVF_Default);

// HEX "RRGGBB" → FLinearColor (alpha=1). Пустой/битый → белый.
//   ★ D1 (дедуп UI 2026-08-08): было FColor::FromHex (та же sRGB-кривая по таблице) — теперь единая
//   AsdaUi::SrgbHexToLinear (6-hex ⇒ альфа=1, точный pow ≡ таблица по байтам). Вид не меняется.
static FLinearColor AsdaHexToLinear(const FString& Hex)
{
	return AsdaUi::SrgbHexToLinear(Hex.Left(6));
}

// ── Реализация декоратора: run <a ...> → ТЕКСТОВЫЙ run со своим стилем ──
//
// ★★★ ПОЧЕМУ НЕ inline-ВИДЖЕТ (дефект «тултип не переносит строки», найден 2026-08-09)
//   `FRichTextDecorator::Create` (RichTextBlockDecorator.cpp:35) выбирает путь по тому, вернул ли
//   `CreateDecoratorWidget` виджет:
//     • ВЕРНУЛ  → в модельный текст кладётся ОДИН символ U+200B, а run становится `FSlateWidgetRun`;
//     • НЕ вернул → зовётся `CreateDecoratorText`, run становится обычным `FSlateTextRun`.
//   `FSlateWidgetRun` (SlateWidgetRun.cpp:41) меряется ЦЕЛИКОМ — `Measure()` отдаёт desired size
//   виджета, а в модельном тексте у него ровно один символ. Значит внутри такого run'а у раскладки
//   НЕТ НИ ОДНОЙ точки переноса: длинная строка не переносится вообще и уезжает вправо за границу
//   плашки. Именно это юзер видел в тултипе предмета — у нас КАЖДЫЙ сегмент был форматированным
//   (тултип подставлял базовые теги <FS>/<FC> на всю строку), поэтому не переносилось НИЧЕГО.
//   Второе следствие: `GetMaxHeight()` = `Widget->GetDesiredSize().Y`, а `GetBaseLine()` берётся от
//   шрифта БАЗОВОГО стиля — до первого layout-prepass desired size = 0, и строки печатались одна
//   поверх другой («Вес : 0» поверх подвала). Обе беды снимает обычный текстовый run: он разбивается
//   по словам и участвует в общем расчёте высоты строки.
//   `FTextBlockStyle` несёт ВСЁ, что нам нужно от сегмента (шрифт+кегль+начертание+обводка, цвет,
//   тень), поэтому вид сегмента не меняется — меняется только способ укладки.
class FAsdaRichDecorator : public FRichTextDecorator
{
public:
	explicit FAsdaRichDecorator(URichTextBlock* InOwner) : FRichTextDecorator(InOwner) {}

	// Обрабатываем только наши run'ы <a ...> (см. FAsdaMarkup::ToRichMarkup).
	virtual bool Supports(const FTextRunParseResults& RunParseResult, const FString& Text) const override
	{
		return RunParseResult.Name == TEXT("a");
	}

protected:
	// Стиль сегмента: атрибуты c/b/o/d/s поверх базового (Default) стиля владельца.
	static void BuildStyle(const FTextRunInfo& RunInfo, const FTextBlockStyle& Base,
	                       FSlateFontInfo& OutFont, FLinearColor& OutColor,
	                       FVector2D& OutShadowOffset, FLinearColor& OutShadowColor)
	{
		const TMap<FString, FString>& Meta = RunInfo.MetaData;

		OutFont  = Base.Font;                                       // база = Default-стиль владельца
		OutColor = Base.ColorAndOpacity.GetSpecifiedColor();

		if (const FString* C = Meta.Find(TEXT("c")))              // FC=цвет текста
			OutColor = AsdaHexToLinear(*C);
		if (const FString* S = Meta.Find(TEXT("s")))              // FS=размер шрифта
		{
			const int32 Sz = FCString::Atoi(**S);
			if (Sz > 0) OutFont.Size = Sz;
		}
		const bool bBold    = Meta.Contains(TEXT("b"));            // FB
		const bool bOutline = Meta.Contains(TEXT("o"));            // FO
		const bool bShadow  = Meta.Contains(TEXT("d"));            // FD

		// ★ 2026-09-24: `FB` — НЕ Bold-гарнитура. Эталон (`font_text.c:6832-6886`) рисует тот же
		//   глиф вторым проходом в `penX+1` и возвращает перо в `penX`, тем же цветом, не меняя
		//   продвижения; жирного начертания в оригинале нет вообще (атлас собран
		//   `CreateFontA(..., вес 400)`). Разбор — `AsdaUi::ApplyPseudoBold`; наш приём 1:1.
		//   Второй проход даём тенью по горизонтали ЦВЕТОМ ТЕКСТА (ниже).
		if (bOutline)
		{
			// FO=контур. Голый <FO> — ЧЁРНАЯ обводка (эталонный флаг 0x800000).
			//   ★ <FO=RRGGBB> кладёт цвет в атрибут `oc` — обводка рисуется ИМ (нужно строкам чата:
			//   белый текст, обводка цветом канала; см. chat_main_window.md Д21 п.6).
			OutFont.OutlineSettings.OutlineSize  = 1;
			OutFont.OutlineSettings.OutlineColor = FColor::Black;
			if (const FString* OC = Meta.Find(TEXT("oc")))
			{
				OutFont.OutlineSettings.OutlineColor = AsdaHexToLinear(*OC);
			}
		}

		// Второй проход глифа: `FB` = псевдо-жирный (penX+1, ЦВЕТОМ ТЕКСТА), `FD` = тень
		//   (penX+1/penY+1, тёмная). Флаги эталона взаимоисключающие, а порядок веток в рендере —
		//   ЖИРНЫЙ ПЕРВЫМ (`font_text.c:6832`), тень второй (`:6887`) ⇒ если в разметке окажутся
		//   оба тега, оригинал показал бы ЖИРНЫЙ. ★ Поправка ревизии 24.09.2026: до неё здесь тень
		//   стояла старше и при `<FB><FD>` мы показали бы не то, что оригинал.
		if (bBold)
		{
			AsdaUi::PseudoBoldShadow(OutColor, OutShadowOffset, OutShadowColor);
		}
		else if (bShadow)
		{
			OutShadowOffset = FVector2D(1.0, 1.0);
			OutShadowColor  = FLinearColor(0.f, 0.f, 0.f, 0.5f);
		}
		else
		{
			OutShadowOffset = FVector2D::ZeroVector;
			OutShadowColor  = FLinearColor::Transparent;
		}
	}

	// ОСНОВНОЙ ПУТЬ: сегмент = текстовый run со своим стилем (переносится, как обычный текст).
	virtual void CreateDecoratorText(const FTextRunInfo& RunInfo, FTextBlockStyle& InOutTextStyle,
	                                 FString& InOutString) const override
	{
		FSlateFontInfo Font; FLinearColor Color; FVector2D ShOff; FLinearColor ShCol;
		BuildStyle(RunInfo, InOutTextStyle, Font, Color, ShOff, ShCol);

		InOutTextStyle.SetFont(Font);
		InOutTextStyle.SetColorAndOpacity(FSlateColor(Color));
		InOutTextStyle.SetShadowOffset(ShOff);
		InOutTextStyle.SetShadowColorAndOpacity(ShCol);

		InOutString += RunInfo.Content.ToString();
	}

	// ЛЕГАСИ-ПУТЬ (только по `asda.UI.RichLegacyWidgetRuns 1`): прежний inline-виджет.
	virtual TSharedPtr<SWidget> CreateDecoratorWidget(const FTextRunInfo& RunInfo, const FTextBlockStyle& DefaultTextStyle) const override
	{
		if (CVarRichLegacyWidgetRuns.GetValueOnAnyThread() == 0)
		{
			return TSharedPtr<SWidget>();       // nullptr ⇒ движок пойдёт в CreateDecoratorText
		}
		FSlateFontInfo Font; FLinearColor TextColor; FVector2D ShadowOffset; FLinearColor ShadowColor;
		BuildStyle(RunInfo, DefaultTextStyle, Font, TextColor, ShadowOffset, ShadowColor);

		return SNew(STextBlock)
			.Text(RunInfo.Content)
			.Font(Font)
			.ColorAndOpacity(FSlateColor(TextColor))
			.ShadowOffset(ShadowOffset)
			.ShadowColorAndOpacity(ShadowColor);
	}
};

UAsdaRichDecorator::UAsdaRichDecorator(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

TSharedPtr<ITextDecorator> UAsdaRichDecorator::CreateDecorator(URichTextBlock* InOwner)
{
	return MakeShareable(new FAsdaRichDecorator(InOwner));
}
