// Copyright (c) 2026 @sitxovski (Discord)
// ★ ЕДИНАЯ система inline-форматирования игрового текста порта (замена среза FAsdaText::Strip).
//   Реверс: RE/reports/TEXT_MARKUP_TAGS_RE.md, парсер textParseColorTag@font_text.c:7924.
//   Грамматика (плоская, </F> сбрасывает формат к дефолту):
//     <FC=AARRGGBB> ИЛИ <FC:AARRGGBB> цвет — эталон textParseColorTag@0x12C8700 читает hex со
//     смещения +3 НЕ проверяя разделитель (сервер 3113/..notice шлёт ':'), <FS##> размер, <FB> жирный,
//     <FO> контур (ЧЁРНЫЙ), <FD> тень, </F> сброс. "\n" (два символа) → перенос строки.
//   ★ РАСШИРЕНИЕ ПОРТА (2026-08-06): <FO=RRGGBB> / <FO:RRGGBB> — контур ЗАДАННЫМ цветом
//     (нужен строкам чата: белый текст + обводка цветом канала). Голый <FO> не изменился.
//   ★ Стили ToRichMarkup теперь БИТЫ (как в эталоне: 0x200000 жирный | 0x400000 тень |
//     0x800000 контур) и КОМБИНИРУЮТСЯ: <FD><FO=..> даёт и тень, и цветную обводку.
//
//   USAGE (единственный способ показать форматированный текст в UI — чат/кнопки/имена/тултипы/диалоги):
//     if (UWidget* W = Root->GetWidgetFromName(TEXT("ANI_TXT")))
//         UAsdaRichText::ApplyFormatted(W, RawTextWithTags);   // RawText — СЫРОЙ из таблицы, С тегами
//   Первый вызов заменяет UTextBlock на URichTextBlock в том же слоте (геометрия/имя сохраняются),
//   повторные — только SetText (виджеты не плодятся). Хранение текста — всегда сырое; формат — здесь.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateTypes.h"        // FTextBlockStyle — базовый стиль rich-контрола
#include "Framework/Text/TextLayout.h" // ETextJustify
#include "AsdaMarkup.generated.h"

class URichTextBlock;
class UWidget;

// ★ Аудит №4 (A29/B2): ПУТЬ ТАБЛИЦЫ СТИЛЕЙ rich-текста — одно объявление на порт.
//   Литерал был выписан дважды (здесь и в окне чата, которое берёт из неё игровой шрифт строки
//   `Default`), и таблица из-за этого грузилась двумя независимыми `LoadObject`.
inline constexpr const TCHAR* kAsdaRichStyleDT = TEXT("/Game/AsdaAssets/UI/DT_AsdaRichStyle.DT_AsdaRichStyle");

// Стиль сегмента (совпадает с состоянием ToRichMarkup): 0 none, 1 bold(FB), 2 outline(FO), 3 shadow(FD).
enum class EAsdaTextStyle : uint8 { None = 0, Bold = 1, Outline = 2, Shadow = 3 };

// Один непрерывный кусок текста с постоянным форматом (для bitmap-рендера SAsdaBitmapText).
//   Text может содержать '\n' — перенос обрабатывает сам виджет при отрисовке.
struct FAsdaTextSegment
{
	FString        Text;
	FLinearColor   Color = FLinearColor::White;   // тинт глифов (FC=..)
	EAsdaTextStyle Style = EAsdaTextStyle::None;   // FB/FO/FD
	int32          Size  = 0;                      // FS## (px); 0 = базовый (14)
};

// Конвертер asda-разметки <F..> → строка UE Rich Text (сегменты <a c=.. b=1 o=1 d=1 s=14>текст</>).
struct ASDAREBORN_API FAsdaMarkup
{
	// Сырой игровой текст с тегами → разметка для URichTextBlock. Обычный текст экранируется (&lt;/&amp;).
	static FString ToRichMarkup(const FString& Raw);

	// Сырой игровой текст с тегами → сегменты для bitmap-виджета (та же грамматика 6 тегов + \n).
	//   Формат-теги делят на сегменты; '\n' остаётся внутри текста сегмента (перенос — на виджете).
	static void ToSegments(const FString& Raw, TArray<FAsdaTextSegment>& Out);

	// ★ Сырой игровой текст → ЧИСТЫЙ текст без разметки (та же грамматика 6 тегов).
	//   Единственный «срез» разметки в порту: FAsdaLoginData::StripMarkup / FAsdaText::Strip
	//   делегируют СЮДА. До 2026-08-01 срез знал только <FB>/<FD>/</F>, из-за чего
	//   <FC=AARRGGBB>, <FS##> и <FO> были ВИДНЫ игроку (титулы, тултипы, чат).
	//   Литеральные escape-последовательности данных: "\n"/"\N" → перенос, "\t" → таб, "\r" → удаляется.
	//   Одиночный '<', не открывающий известный тег, сохраняется как обычный символ.
	static FString StripTags(const FString& Raw);
};

// ★★★ ОПЦИИ ПОКАЗА (добавлены 2026-08-09, дефект «в доп. окне не те цвета/кегль/выравнивание»).
//   ЗАЧЕМ. `ApplyFormatted` подменяет UTextBlock на URichTextBlock, и НОВЫЙ контрол берёт стиль из
//   строки `Default` таблицы `DT_AsdaRichStyle` (AsdaUIFontRT, кегль 10, БЕЛЫЙ, влево, autowrap).
//   Всё, что импортёр положил в контрол из JUI, при этом теряется: кегль `FONT_PX = 8`, цвет из
//   блока `EXS TextColor` (см. закон ui-text-color-exs), выравнивание из `EXS TextAlign`, чёрная
//   обводка скина и признак «однострочный» (`bWrap = h >= 28`, закон ui-autowrap-canvas-gotcha).
//   Для правой инфо-панели инвентаря это давало ровно жалобу юзера: белый текст 10 px в строках
//   высотой 14 px — «нечитаемо, не те цвета, размер и выравнивание», строки наезжают.
//   Кто вызывает `ApplyFormatted` для ГОТОВОГО контрола из JUI — обязан передать сюда его
//   собственный стиль; тогда неформатированные куски рисуются им, а теги ложатся поверх.
struct FAsdaRichApplyOpts
{
	// Базовый стиль контрола (шрифт+кегль+обводка, цвет, тень). Ставится ДО SetText — иначе
	//   `SRichTextBlock::SetTextStyle` только инвалидирует раскладку, а уже разобранные run'ы
	//   остаются со старым стилем (RichTextBlock.cpp:526 / SRichTextBlock.cpp:188).
	const FTextBlockStyle* BaseStyle = nullptr;
	bool bSetJustify = false;                              // применять ли Justify
	ETextJustify::Type Justify = ETextJustify::Left;       // из `EXS TextAlign` (бит 0x1 центр, 0x2 право)
	bool bWrap = true;                                     // false ⇒ однострочный контрол (перенос выключен)
	float WrapAt = 0.f;                                    // 0 = по ширине canvas-слота
};

// ★★★ ОДНА СТРОКА ОКНА, КОТОРАЯ МОЖЕТ НЕСТИ ТЕГИ `<FC=…>` (перенесено сюда 25.09.2026 из
//   `AsdaCharaInfoSubsystem.h`: второй и третий потребитель — окна 20000 и 50300; прежде у каждого окна
//   была своя копия снятия стиля). Разбор тегов на порт один — `UAsdaRichText::ApplyFormatted`; он
//   ПОДМЕНЯЕТ `UTextBlock` на `URichTextBlock` в том же canvas-слоте и ВОЗВРАЩАЕТ новый узел. Отсюда две
//   обязанности структуры (законы порта, `Reports/char_info_fix2.md` §1.2):
//     • `Node` — ЖИВОЙ узел строки. Кэш переприсваивается после каждой подмены, иначе осиротеет
//       на выброшенном виджете (грабля баннера 599, `minopio_window.md`);
//     • `Style`/`Justify`/`bWrap` — стиль, СНЯТЫЙ из `UTextBlock` ДО первой подмены. Без него
//       rich-контрол возьмёт строку `Default` таблицы `DT_AsdaRichStyle` (кегль 10, БЕЛЫЙ,
//       autowrap) и потеряет кегль 8 и цвет из блока `EXS TextColor` (закон ui-text-color-exs).
//   Работа со структурой — только `UAsdaRichText::CaptureFmt` / `UAsdaRichText::SetFmt`.
USTRUCT()
struct FAsdaFmtText
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UWidget> Node = nullptr;   // текущий узел строки
	UPROPERTY(Transient) FTextBlockStyle Style;                // стиль из JUI (снят до первой подмены)
	FString LastRaw;                                           // последняя СЫРАЯ строка (для дампа проверки)
	bool bStyleOk = false;                                     // стиль удалось снять
	bool bWrap = false;                                        // перенос (`h >= 28`; однострочным — false)
	TEnumAsByte<ETextJustify::Type> Justify = ETextJustify::Left;
};

class UTextBlock;

// Библиотека показа форматированного текста (единая точка формата для всего UI порта).
UCLASS()
class ASDAREBORN_API UAsdaRichText : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// ★ Снять стиль строки из JUI ДО первой подмены на rich (обоснование — в шапке `FAsdaFmtText`).
	//   T == nullptr → структура сброшена (узла нет).
	static void CaptureFmt(FAsdaFmtText& F, UTextBlock* T);
	// ★ Показать СЫРОЙ текст с тегами в строке `F` через `ApplyFormatted` со снятым стилем контрола
	//   и переприсвоить `F.Node` новым узлом (закон `minopio_window.md`). Пустая строка — тоже законный текст.
	static void SetFmt(FAsdaFmtText& F, const FString& Raw);

	// Показать форматированный RawText (с тегами) в ExistingTextBlock. Заменяет текст-контрол на
	//   URichTextBlock в том же canvas-слоте (геометрия/имя сохраняются).
	//   Возвращает созданный/существующий виджет (или nullptr). Idempotent: повторный вызов не плодит виджеты.
	//   Opts (может быть nullptr) — стиль/выравнивание/перенос контрола, см. FAsdaRichApplyOpts.
	static UWidget* ApplyFormatted(UWidget* ExistingTextBlock, const FString& RawText,
	                               const FAsdaRichApplyOpts* Opts = nullptr);
};
