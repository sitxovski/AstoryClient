// Copyright (c) 2026 @sitxovski (Discord)
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaLoginData.h"            // FAsdaLoginData::kGameTextPath — путь для удержания (15.09.2026, dt_gc_hold)
#include "Login/AsdaText.h"                 // рабочий фасад loose-текста (FAsdaText)
#include "Items/AsdaItemTableSubsystem.h"   // item-БД (имена предметов)

FString UAsdaTextSubsystem::GetTextById(int32 Id) const
{
	HoldGameTextTable();   // ★ 15.09 (dt_gc_hold): DT_GameText — сильно у подсистемы на сессию
	return FAsdaText::Loc(Id);
}

// Сырая строка с inline-тегами (см. комментарий в заголовке).
FString UAsdaTextSubsystem::GetTextByIdRaw(int32 Id) const
{
	HoldGameTextTable();   // ★ 15.09 (dt_gc_hold): DT_GameText — сильно у подсистемы на сессию
	return FAsdaText::LocRaw(Id);
}

// Подстановка слотов в loc-строку: [ssN] ← StringSlots[N-1], [sdN] ← NumberSlots[N-1].
//   Оригинал: CObject_SetSlotString(ctx, idx, str) / CClass_SetSlotPair(ctx, idx, num) + UI_FormatLocalizedNotice.
// ★★★ ПОЛНЫЙ ПОРТ `UI_FormatLocalizedNotice@0x9888D0` (`ui_core.c:2055-2185`) — разбор и таблица
//   маркеров в объявлении. Ключевое отличие от прежней редакции `FormatById`: подставляются ВСЕ
//   маркеры, а не только те, под которые вызывающий передал аргумент. Оригинал крутит
//   `while (UI_NoticeHasPlaceholder(...))`, и незаполненный слот даёт пустую строку / ноль —
//   сырой `[sd1]` на экран не попадает НИКОГДА.
bool UAsdaTextSubsystem::HasNoticeMarkers(const FString& S)
{
	if (S.Contains(TEXT("[/n]"), ESearchCase::CaseSensitive)) { return true; }
	for (int32 i = 1; i <= 5; ++i)
	{
		if (S.Contains(*FString::Printf(TEXT("[ss%d]"), i), ESearchCase::CaseSensitive)) { return true; }
		if (S.Contains(*FString::Printf(TEXT("[sd%d]"), i), ESearchCase::CaseSensitive)) { return true; }
		if (S.Contains(*FString::Printf(TEXT("[sf%d]"), i), ESearchCase::CaseSensitive)) { return true; }
	}
	return false;
}

FString UAsdaTextSubsystem::FormatNoticeText(const FString& Src, const FNoticeSlots& S,
	int32 HighlightMode, int32 HighlightIndex)
{
	// «Маркеров нет ⇒ строка таблицы как есть» — ветка `if (hadPlaceholder) … else` оригинала.
	if (Src.IsEmpty() || !HasNoticeMarkers(Src)) { return Src; }

	FString Out = Src;
	// Оригинальный `while` завершается, когда маркеров не осталось; здесь тот же смысл, но с
	//   потолком итераций: подставленный текст сам может содержать «[ss1]» (например, имя
	//   предмета), и буквальный `while` зациклился бы. Оригинал этим не защищён — это ЕГО дефект.
	for (int32 Pass = 0; Pass < 8 && HasNoticeMarkers(Out); ++Pass)
	{
		Out = Out.Replace(TEXT("[/n]"), *FString::Chr(10), ESearchCase::CaseSensitive);   // 10 = LF, без escape в исходнике
		for (int32 i = 0; i < 5; ++i)
		{
			const FString Key = FString::Printf(TEXT("[ss%d]"), i + 1);
			if (Out.Contains(Key, ESearchCase::CaseSensitive))
			{
				FString Frag;
				if (S.StrColor[i] != 0)
				{
					Frag = (HighlightMode == 1 && HighlightIndex == i + 1)
						? FString::Printf(TEXT(" <FC=%08x>[%s]</F> "), S.StrColor[i], *S.Str[i])
						: FString::Printf(TEXT(" <FC=%08x>%s</F> "), S.StrColor[i], *S.Str[i]);
				}
				else { Frag = S.Str[i]; }
				Out = Out.Replace(*Key, *Frag, ESearchCase::CaseSensitive);
			}
		}
		for (int32 i = 0; i < 5; ++i)
		{
			const FString Key = FString::Printf(TEXT("[sd%d]"), i + 1);
			if (Out.Contains(Key, ESearchCase::CaseSensitive))
			{
				const FString Frag = (S.NumColor[i] != 0)
					? FString::Printf(TEXT("<FC=%08x>%d</F>"), S.NumColor[i], S.Num[i])
					: FString::FromInt(S.Num[i]);
				Out = Out.Replace(*Key, *Frag, ESearchCase::CaseSensitive);
			}
		}
		for (int32 i = 0; i < 5; ++i)
		{
			const FString Key = FString::Printf(TEXT("[sf%d]"), i + 1);
			if (Out.Contains(Key, ESearchCase::CaseSensitive))
			{
				// `%2lf` оригинала = обычный `%f` с минимальной шириной 2 ⇒ шесть знаков после точки.
				const FString Num = FString::Printf(TEXT("%f"), S.Flt[i]);
				const FString Frag = (S.FltColor[i] != 0)
					? FString::Printf(TEXT("<FC=%08x>%s</F>"), S.FltColor[i], *Num)
					: Num;
				Out = Out.Replace(*Key, *Frag, ESearchCase::CaseSensitive);
			}
		}
	}
	return Out;
}

void UAsdaTextSubsystem::NoticeSetString(int32 Slot, const FString& S, uint32 ColorArgb)
{
	if (Slot < 0 || Slot > 4) { return; }                 // `if (slotIndex <= 4)` оригинала
	Slots.Str[Slot] = S;
	Slots.StrColor[Slot] = ColorArgb;
}

void UAsdaTextSubsystem::NoticeSetInt(int32 Slot, int32 Value, uint32 ColorArgb)
{
	if (Slot < 0 || Slot > 4) { return; }
	Slots.Num[Slot] = Value;
	Slots.NumColor[Slot] = ColorArgb;
}

void UAsdaTextSubsystem::NoticeSetFloat(int32 Slot, float Value, uint32 ColorArgb)
{
	if (Slot < 0 || Slot > 4) { return; }
	Slots.Flt[Slot] = Value;
	Slots.FltColor[Slot] = ColorArgb;
}

void UAsdaTextSubsystem::NoticeClearSlots()
{
	Slots.Clear();
}

FString UAsdaTextSubsystem::FormatNotice(int32 Id, int32 HighlightMode, int32 HighlightIndex)
{
	HoldGameTextTable();   // ★ 15.09 (dt_gc_hold): DT_GameText — сильно у подсистемы на сессию
	// ★ Строку берём СЫРУЮ: подстановка вставляет теги `<FC=…>`, и срезать разметку можно только
	//   ПОСЛЕ неё (иначе цвет аргумента пропадёт). Дальше решает вызывающий.
	const FString Src = FAsdaText::LocRaw(Id);
	const FString Out = FormatNoticeText(Src, Slots, HighlightMode, HighlightIndex);
	NoticeClearSlots();          // `objSlotArray_InitClear(v30)` — аргументы живут один вызов
	return Out;
}

FString UAsdaTextSubsystem::FormatById(int32 Id, const TArray<FString>& StringSlots, const TArray<int32>& NumberSlots) const
{
	HoldGameTextTable();   // ★ 15.09 (dt_gc_hold): DT_GameText — сильно у подсистемы на сессию
	// ★ Тонкая обёртка над общей точкой: собираем слоты из массивов и зовём ТОТ ЖЕ форматтер,
	//   чтобы правила подстановки жили в одном месте (раньше здесь была своя, неполная копия:
	//   ни `[sfN]`, ни `[/n]`, ни цветов, и НЕПЕРЕДАННЫЕ маркеры оставались на экране).
	FNoticeSlots S;
	for (int32 i = 0; i < StringSlots.Num() && i < 5; ++i) { S.Str[i] = StringSlots[i]; }
	for (int32 i = 0; i < NumberSlots.Num() && i < 5; ++i) { S.Num[i] = NumberSlots[i]; }
	return FormatNoticeText(FAsdaText::Loc(Id), S);
}

// ★★★ printf-подстановка в loc-строку (разбор — в заголовке). Эталон: `uiItemTooltipShow@0xB70A60`
//   (ui_core.c:7726-7732) берёт имя предмета `itemGetTooltipDataById(код)`, кладёт его в буфер и
//   зовёт `sprintf(Str, UI_FormatLocalizedNotice(ctx, 110621, 0, 1), Buffer)` — то есть loc-строка
//   служит ФОРМАТОМ, а `%s` — местом подстановки. `FString::Printf` здесь неприменим: формат не
//   литерал (компилятор его не видит), поэтому идём по строке сами.
FString UAsdaTextSubsystem::FormatPrintf(const FString& Fmt, const TArray<FString>& Args)
{
	if (Fmt.IsEmpty()) { return Fmt; }
	FString Out;
	Out.Reserve(Fmt.Len() + 32);
	int32 ArgIdx = 0;
	for (int32 i = 0; i < Fmt.Len(); ++i)
	{
		const TCHAR C = Fmt[i];
		if (C != TEXT('%') || i + 1 >= Fmt.Len()) { Out.AppendChar(C); continue; }
		const TCHAR Spec = Fmt[i + 1];
		if (Spec == TEXT('%'))                                   // «%%» = литеральный процент
		{
			Out.AppendChar(TEXT('%'));
			++i;
			continue;
		}
		if (Spec != TEXT('s') && Spec != TEXT('d'))               // прочие спецификаторы не трогаем
		{
			Out.AppendChar(C);
			continue;
		}
		if (!Args.IsValidIndex(ArgIdx))                           // аргумент кончился — оставляем как есть
		{
			Out.AppendChar(C);
			continue;
		}
		Out += Args[ArgIdx++];
		++i;
	}
	return Out;
}

FString UAsdaTextSubsystem::FormatPrintfById(int32 Id, const TArray<FString>& Args) const
{
	HoldGameTextTable();   // ★ 15.09 (dt_gc_hold): DT_GameText — сильно у подсистемы на сессию
	return FormatPrintf(FAsdaText::Loc(Id), Args);
}

FString UAsdaTextSubsystem::GetText(const FString& Table, int32 Id, const FString& Field) const
{
	// ★ 15.09 (dt_gc_hold): текст-таблица — сильно у подсистемы на сессию (группа = имя таблицы, попытка одна).
	const FName Group(*Table);
	if (!HeldTables.IsHeld(Group)) { HeldTables.HoldTable(Group, FAsdaText::FindTable(Table)); }
	return FAsdaText::Get(Table, Id, Field);
}

// ★ Удержание DT_GameText на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении. Путь — `FAsdaLoginData::kGameTextPath`.
int32 UAsdaTextSubsystem::HoldGameTextTable() const
{
	static const FName Group(TEXT("GameText"));
	const TCHAR* const Paths[] = { FAsdaLoginData::kGameTextPath };
	return HeldTables.Hold(Group, Paths);
}

FString UAsdaTextSubsystem::GetItemName(int32 ItemId) const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaItemTableSubsystem* Items = GI->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			return Items->GetItemName(ItemId);
		}
	}
	return FString();
}

FString UAsdaTextSubsystem::Strip(const FString& In) const
{
	return FAsdaText::Strip(In);
}
