// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMiniStateSubsystem_Buffs.cpp — ★ R3-c 12.11.2026 (разрез AsdaMiniStateSubsystem.cpp, дословный перенос): подсказки пакетов, иконки баффов и событий, таймеры, подсказки баффов.
//   Общее для частей — в AsdaMiniStateSubsystem_Private.h; шапка и реверс — в AsdaMiniStateSubsystem.cpp.
#include "UI/AsdaMiniStateSubsystem.h"

#include "Resources/AsdaTextureSubsystem.h"    // атласы иконок и цифр таймера (JUI .tex)
#include "Items/AsdaItemTableSubsystem.h"      // ★ Аудит №3 (A3): DT_PlusPackage — только через менеджер
#include "Text/AsdaTextSubsystem.h"

// inline-теги <FC=…>…</F> в тексте тултипа 110922   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/DataTable.h"
#include "AsdaMiniStateSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMiniStateSubsystem*.cpp

// ★★★ ПОКАЗ ОПИСАНИЯ Plus Package = ПО НАВЕДЕНИЮ. Это ЭТАЛОН, доказано маршрутизатором ввода
//   `uiControlDispatchMouseLeft@0x12DD620` (ui_core.c:108003-108069):
//       CControl_OnLButtonUp(this, itemIndex, plane);                 // 6
//       if ( <ХИТ-ТЕСТ ПЛАНА ПОД КУРСОРОМ> ) {                        // ← условие = НАВЕДЕНИЕ
//           CControl_OnLButtonDown (this, itemIndex, plane);          // 5
//           CControl_OnRButtonDown (this, itemIndex, plane);          // 8  ← ВОШЁЛ В КОНТРОЛ
//           CControl_OnRButtonUp   (this, itemIndex, plane);          // 9  ← ДЕРЖИТСЯ НАД НИМ
//       } else {
//           CControl_OnRButtonClick(this, itemIndex, plane);          // 10 ← УШЁЛ С КОНТРОЛА
//       }
//   Имена `On*Button*` — историческое название функций, состояние кнопок они НЕ проверяют:
//   внутри стоят только флаги плана (`0x80`/`0x100`) и `a2 != -1` (ui_core.c:107336-107399).
//   Именно поэтому обработчик и выглядит так: 8 = ПОСТРОИТЬ тултип, 9 = `Tooltip_LayoutNearCursor`
//   (тянуть за курсором), 10 = `CControl_KillFocus` (скрыть). При трактовке «правая кнопка»
//   «переложить к курсору на отпускании» и «скрыть на клике» были бы бессмыслицей.
//   ⇒ ПО УМОЛЧАНИЮ 1 (наведение) — так и в эталонном клиенте у юзера.
// ★ Дефект оригинала со съеданием знаков «%» в описании Plus Package. 0 = ИСПРАВЛЕНО (решение
//   юзера 2026-08-07: текст должен выглядеть правильно — «+ 25%», «+ 20%»); 1 = как в старом
//   клиенте (sprintf съедает проценты) — только для сверки с эталоном.
static ASDA_DEV_CVAR(int32, CVarPlusPackLegacySprintf,
	TEXT("asda.Mini.PlusPackLegacySprintf"),
	0,
	TEXT("Описание Plus Package: 0 = проценты СОХРАНЯЮТСЯ (решение юзера); "
	     "1 = воспроизвести дефект оригинала (sprintf съедал «%») для сверки с эталонным клиентом."),
	ECVF_Default);

// ★ Высота панели тултипа: 0 = по факту содержимого (умолчание, решение юзера — рамка облегает
//   текст); 1 = эталонная формула 3*(L+1)+12*(L+2)+52 (даёт пустую полосу снизу в метриках UE).
static ASDA_DEV_CVAR(int32, CVarTipLegacyHeight,
	TEXT("asda.Mini.TipLegacyHeight"),
	0,
	TEXT("Высота тултипа Plus Package: 0 = подгонка по содержимому; 1 = эталонная формула."),
	ECVF_Default);

// ══════════════════════════════ ТУЛТИП ПРЕМИУМ-ПАКЕТА (5110) ═══════════════════════════════════
// Эталон HUD_PlayerBuffIcon_OnClick case 8, a2 == 5110 (ui_core.c:76801-76819):
//   rec = findRecord140ById(g_WingDataTable, wingId игрока);
//   тело  = sprintf(UI_FormatLocalizedNotice(rec+8), дни);   дни = g_BuffRemainSeconds / 86400
//   заголовок = UI_FormatLocalizedNotice(rec+4);
//   tutorialPopupLayoutLarge(&g_ItemTooltip, заголовок, тело, −1, −1)
//     → ITEM_TIPTITLE (5001) = заголовок, ITEM_TIPCONTENS (5002) = тело,
//       ширина панели = clamp(ширина текста, 90, 218) + 38 (item_inventory.c:62043-62090).
// ★ Запись 140 б PlusPackageTable: id@0, notice@4, notice@8 — в порту это строка DT_PlusPackage
//   (поля NoticeIdMale/NoticeIdFemale = rec+4/rec+8; имена полей из прежней гипотезы импортёра,
//   фактическое назначение по эталону — ЗАГОЛОВОК и ТЕЛО тултипа).
void UAsdaMiniStateSubsystem::ShowPlusPackTooltip()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	if (WingId < 0)
	{
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] ПКМ по 5110: записи крыла нет (6585 не приходил) — тултип не строится (эталон: if (!buffRec) ничего не делает)"));
		return;
	}

	int32 TitleLoc = -1, BodyLoc = -1;
	// ★ Аудит №3 (A3, дубль данных Дд3): `DT_PlusPackage` грузили ТРИЖДЫ (здесь, в `DumpPlusPackages`
	//   и в мире). Теперь таблицу держит менеджер (`GExtraTables` → `GetDataTable("PlusPackage")`),
	//   а адресация осталась прежней — по имени строки (= wingId), как в эталоне `findRecord140ById`.
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (const UDataTable* DT = IT ? IT->GetDataTable(FName(TEXT("PlusPackage"))) : nullptr)
	{
		const FAsdaPlusPackageRow* R = DT->FindRow<FAsdaPlusPackageRow>(FName(*FString::FromInt(WingId)), TEXT("ministate-tip"), false);
		if (R) { TitleLoc = R->NoticeIdMale; BodyLoc = R->NoticeIdFemale; }
	}
	if (TitleLoc <= 0 && BodyLoc <= 0)
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[мини-статус] описание 5110: в DT_PlusPackage нет записи %d"), WingId);
		return;
	}

	const int32 Days = WingRemainSec / 86400;
	// ★★★ ТОЛЬКО GetTextByIdRaw: обычный GetTextById() отдаёт строку с УЖЕ СРЕЗАННОЙ разметкой
	//   (FAsdaText::Loc → FAsdaLoginData::Loc, «Strip применён»). Именно из-за него тело 110922
	//   приходило без своего inline-тега <FC=FF99FF99> и рисовалось сплошь белым — без единой
	//   ошибки в логе. Цвет части строки задаёт сама игра, значит нужна СЫРАЯ строка.
	FString Title = (Txt && TitleLoc > 0) ? Txt->GetTextByIdRaw(TitleLoc) : FString();
	FString BodyFmt = (Txt && BodyLoc > 0) ? Txt->GetTextByIdRaw(BodyLoc) : FString();
	// Эталон подставляет ТОЛЬКО число дней (`sprintf(buf, loc, days)`), хотя разбирает d/h/m/s.
	//   Подстановка ручная (FString::Printf с рантайм-форматом небезопасен): поддерживаем и
	//   C-плейсхолдер `%d`, и наш табличный `[sd1]`.
	// ★ ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ЮЗЕРА: дефект оригинала (sprintf съедал «%») НЕ воспроизводим —
	//   проценты сохраняются, подставляется только число дней. Эталонный вариант доступен
	//   ключом asda.Mini.PlusPackLegacySprintf 1 (для сверки со старым клиентом).
	FString Body;
	if (BodyFmt.IsEmpty())                                              { Body = FString::FromInt(Days); }
	else if (CVarPlusPackLegacySprintf.GetValueOnAnyThread() != 0)      { Body = LegacySprintfOneInt(BodyFmt, Days); }
	else                                                                { Body = SubstituteDaysKeepPercents(BodyFmt, Days); }
	Body = Body.Replace(TEXT("[sd1]"), *FString::FromInt(Days));
	// ★ ЧЕСТНО: в таблице текста часть видов Plus Package — заглушки «-» (см. asda.Mini.PlusPackList).
	//   Эталон в этом случае показал бы ровно «-», поэтому и мы показываем как есть, но громко пишем в лог.
	if (Title.TrimStartAndEnd() == TEXT("-") || Body.TrimStartAndEnd() == TEXT("-"))
	{
		UE_LOG(LogAsdaMini, Warning,
			TEXT("[мини-статус] Plus Package вида %d: в таблице текста ЗАГЛУШКА «-» (заголовок loc %d = «%s», тело loc %d = «%s») — так в ДАННЫХ, не баг порта"),
			WingId, TitleLoc, *Title, BodyLoc, *BodyFmt);
	}

	LayoutAndShowTip(Title, Body, /*bBuffFormula=*/false);
}

// Ленивое создание окна ITEM_TIPWIN (общее для тултипа Plus Package и тултипов иконок эффектов).
bool UAsdaMiniStateSubsystem::EnsureTipWidget()
{
	// ★★★ ДЕДУП 2026-08-08: своей копии окна тултипа мини-статус больше НЕ держит.
	//   В оригинале плашка ОДНА на всю игру — глобальный `g_ItemTooltip` (`CItemTipWin_InitUI@0xEA15E0`),
	//   и её показывают и подсказка Plus Package, и подсказка эффекта, и тултип предмета.
	//   Владелец порта — `UAsdaItemTooltipSubsystem`; здесь только берём готовую плашку.
	//   Раньше мини-статус открывал ВТОРОЙ экземпляр WBP_ITEM_TIPWIN под своим id
	//   («MINISTATE_ITEM_TIPWIN») — это и был дубль.
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	SharedTip = Tip ? Tip->GetMainPanel() : nullptr;
	TipWidget = SharedTip ? SharedTip->GetWidget() : nullptr;
	if (!TipWidget)
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[мини-статус] общая плашка тултипа недоступна (WBP_ITEM_TIPWIN не загрузился)"));
		return false;
	}
	return true;
}

// ★★★ ЧИСЛО СТРОК С УЧЁТОМ ПЕРЕНОСА — эталон `textFontCountWrappedLines_Active(текст, ширина)`
//   (uiHelpEntryLayoutAndShow, ui_core.c:83051 и tutorialPopupLayoutLarge, item_inventory.c:62085).
//   ОДНА функция на ОБА тултипа: без неё длинное описание эффекта считалось за одну строку — панель
//   выходила широкой и низкой, а текст переносился уже при отрисовке и вылезал за рамку.
//   Жадный перенос по словам тем же кеглем, каким рисуем (Fnt уже с TipFontPx).
int32 UAsdaMiniStateSubsystem::CountWrappedLines(const FString& Plain, const FSlateFontInfo& Fnt, float WrapW)
{
	// ★ ДЕДУП: единственная реализация — `UAsdaTipPanel::CountWrappedLines` (её же использует тултип
	//   предмета). Метод оставлен как публичная точка входа прежних вызовов.
	return UAsdaTipPanel::CountWrappedLines(Plain, Fnt, WrapW);
}

// Раскладка и показ тултипа. Формулы — по флагу bBuffFormula (см. объявление в .h).
//   ★ ДЕДУП 2026-08-08: сама раскладка (9-slice, замер, перенос, разметка) живёт в `UAsdaTipPanel`;
//   здесь остаётся только ВЫБОР МЕТРИК, который у мини-статуса свой:
//     • ширина = clamp(текст, 90, 218) + 27 (подсказка эффекта) либо + 38 (Plus Package)
//       (`uiHelpEntryLayoutAndShow@0xEA6EC0` / `tutorialPopupLayoutLarge@0xEB6EE0`);
//     • высота — ПО СОДЕРЖИМОМУ (решение юзера 2026-08-07); эталонные формулы доступны ключом
//       `asda.Mini.TipLegacyHeight 1`.
void UAsdaMiniStateSubsystem::LayoutAndShowTip(const FString& Title, const FString& Body, bool bBuffFormula,
                                               EAsdaTipOwner Owner)
{
	if (!EnsureTipWidget() || !SharedTip) { return; }

	FAsdaTipMetrics M;
	M.FontPx     = TipFontPx;
	M.WidthAdd   = bBuffFormula ? 27.f : 38.f;
	M.ExtraLines = 0;                       // мини-статус считает высоту по ФАКТИЧЕСКИМ строкам
	const bool bLegacyH = bTipLegacyHeight || CVarTipLegacyHeight.GetValueOnAnyThread() != 0;
	M.HeightMode = bLegacyH
		? (bBuffFormula ? EAsdaTipHeightMode::BuffLegacy : EAsdaTipHeightMode::PlusPackLegacy)
		: EAsdaTipHeightMode::ByContent;
	M.LegacyLineGap    = (float)TipLineGap;
	M.LegacyLineHeight = (float)TipLineHeight;
	M.LegacyChromeH    = (float)TipChromeH;

	// ★ ЕДИНЫЙ ПУТЬ: показ идёт через владельца плашки (`UAsdaItemTooltipSubsystem`, порт
	//   `g_ItemTooltip`) — он же проставляет владельца и укладывает панель у курсора.
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	Tip->ShowTextTip(Owner, Title, Body, M);
	TipWidget = SharedTip->GetWidget();
	// `bTipShown` мини-статуса = «плашкой владеет ИМЕННО мини-статус» (Plus Package / иконка эффекта).
	//   Для чужих владельцев (хотбар, хранитель, системное меню) флаг не взводим — иначе покадровая
	//   ветка «курсор ушёл с pluscpack» снова начнёт гасить чужую подсказку.
	bTipShown = (Owner == EAsdaTipOwner::MiniState);
	UE_LOG(LogAsdaMini, Log,
		TEXT("[мини-статус] тултип (%s, владелец %d): панель=%.0fx%.0f (заголовок «%s», %d симв.; тело %d симв.)"),
		bBuffFormula ? TEXT("эффект") : TEXT("Plus Package"), (int32)Owner,
		SharedTip->GetLastSize().X, SharedTip->GetLastSize().Y, *Title, Title.Len(), Body.Len());
}

// ★ ДЕДУП: 9-slice пересобирает `UAsdaTipPanel::Resize9Slice` (эталон `uiPanel9SliceLayout`).
//   Метод оставлен точкой входа для прочих узлов мини-статуса.
void UAsdaMiniStateSubsystem::Resize9Slice(const TArray<UImage*>& Nine, const FVector2D& NewSize)
{
	TArray<TObjectPtr<UImage>> Tmp;
	Tmp.Reserve(Nine.Num());
	for (UImage* I : Nine) { Tmp.Add(I); }
	UAsdaTipPanel::Resize9Slice(Tmp, NewSize);
}

// ★★★ ОТСТУПЛЕНИЕ ПО РЕШЕНИЮ ЮЗЕРА (2026-08-07): дефект оригинала (см. LegacySprintfOneInt ниже —
//   `sprintf` СЪЕДАЛ знаки процента) НЕ ВОСПРОИЗВОДИМ. Проценты остаются в тексте, подставляется
//   ТОЛЬКО число дней в первое вхождение `%[флаги/ширина]d|i|u`. Всё остальное — обычные символы,
//   поэтому разметка `<FC=…>…</F>` и переносы не затрагиваются, и это одинаково верно для всех
//   видов пакета (у 110921 текст такой же, как у 110922).
//   ⚠ НЕ «возвращать к эталону»: юзер видел оба варианта и выбрал правильный текст.
FString UAsdaMiniStateSubsystem::SubstituteDaysKeepPercents(const FString& Fmt, int32 Days)
{
	const int32 N = Fmt.Len();
	for (int32 i = 0; i < N; ++i)
	{
		if (Fmt[i] != TEXT('%')) { continue; }
		if (i + 1 < N && Fmt[i + 1] == TEXT('%')) { ++i; continue; }   // «%%» — не спецификатор
		int32 j = i + 1;
		while (j < N && (Fmt[j] == TEXT('-') || Fmt[j] == TEXT('+') || Fmt[j] == TEXT(' ')
		              || Fmt[j] == TEXT('#') || Fmt[j] == TEXT('0') || Fmt[j] == TEXT('.')
		              || FChar::IsDigit(Fmt[j]))) { ++j; }
		if (j < N && (Fmt[j] == TEXT('d') || Fmt[j] == TEXT('i') || Fmt[j] == TEXT('u')))
		{
			return Fmt.Left(i) + FString::FromInt(Days) + Fmt.RightChop(j + 1);
		}
		// не число — оставляем «%» как обычный символ и ищем дальше
	}
	return Fmt;
}

// ★ ДЕФЕКТ ОРИГИНАЛА (ОТКЛЮЧЁН, оставлен для сверки с эталонным клиентом). Эталон гонит тело
//   тултипа через C-`sprintf`:
//     v9 = UI_FormatLocalizedNotice(ctx, *(DWORD*)(buffRec + 8), 0, 1);
//     sprintf(noticeText, v9, days);                       // ui_core.c:76812
//   А строка 110922 содержит последовательности «% \n» («+ 25% \n ОЗ + 10% \n …»). Для `sprintf`
//   это `%` + флаг-пробел + недопустимый символ преобразования ⇒ MSVC выбрасывает `%` и флаг,
//   оставляя сам символ. Именно поэтому в ЭТАЛОННОМ КЛИЕНТЕ на экране «Увеличение скорости + 25»,
//   «шанс выпадения предмета + 20» — БЕЗ знаков процента, а «Срок действия - 327 дн.» подставлен
//   верно (там настоящий `%d`). Это дефект оригинала, но вид должен совпадать 1:1 — воспроизводим.
//   Правила: `%%` → «%»; `%[флаги/ширина][diu]` → число; любой другой хвост → `%` и флаги
//   выбрасываются, символ преобразования остаётся.
FString UAsdaMiniStateSubsystem::LegacySprintfOneInt(const FString& Fmt, int32 Arg)
{
	FString Out;
	const int32 N = Fmt.Len();
	bool bArgUsed = false;
	for (int32 i = 0; i < N; ++i)
	{
		const TCHAR C = Fmt[i];
		if (C != TEXT('%')) { Out.AppendChar(C); continue; }
		int32 j = i + 1;
		if (j < N && Fmt[j] == TEXT('%')) { Out.AppendChar(TEXT('%')); i = j; continue; }
		// флаги / ширина / точность
		while (j < N && (Fmt[j] == TEXT('-') || Fmt[j] == TEXT('+') || Fmt[j] == TEXT(' ')
		              || Fmt[j] == TEXT('#') || Fmt[j] == TEXT('0') || Fmt[j] == TEXT('.')
		              || FChar::IsDigit(Fmt[j]))) { ++j; }
		if (j >= N) { i = j; continue; }                    // «%» в самом конце — съедается
		const TCHAR Conv = Fmt[j];
		if (Conv == TEXT('d') || Conv == TEXT('i') || Conv == TEXT('u'))
		{
			Out += bArgUsed ? TEXT("0") : *FString::FromInt(Arg);   // аргумент один, как в эталоне
			bArgUsed = true;
		}
		else
		{
			Out.AppendChar(Conv);                            // «%» и флаги съедены, символ остаётся
		}
		i = j;
	}
	return Out;
}

// Все виды Plus Package (PlusPackageTable.BIN → DT_PlusPackage): id, пара loc-строк и сами строки.
FString UAsdaMiniStateSubsystem::DumpPlusPackages() const
{
	UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// ★ Аудит №3 (A3, дубль данных Дд3): вторая копия загрузки в ЭТОМ ЖЕ файле снята — таблица одна,
	//   у менеджера (см. `ShowPlusPackTooltip`).
	UAsdaItemTableSubsystem* ITab = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const UDataTable* DT = ITab ? ITab->GetDataTable(FName(TEXT("PlusPackage"))) : nullptr;
	if (!DT) { return TEXT("DT_PlusPackage не найден\n"); }
	// ★ Каждая запись пишется ОТДЕЛЬНОЙ строкой лога: одна длинная многострочная запись в консоли
	//   обрезается, и раньше «подробности не печатались».
	// Возвращаем ОДНУ многострочную простыню — вызывающий (MiniEcho) режет её по '\n' и шлёт
	//   каждую строку и в лог, и в системный журнал чата, чтобы результат был виден В ИГРЕ.
	FString S = TEXT("=== Виды Plus Package (иконка 5110 у всех ОДНА, различается только описание) ===\n");
	int32 Ok = 0, Stub = 0, Total = 0;
	const int32 Days = FMath::Max(WingRemainSec, 0) / 86400;
	// Порядок вывода — по возрастанию id (в RowMap порядок произвольный).
	TArray<const FAsdaPlusPackageRow*> Rows;
	for (const TPair<FName, uint8*>& It : DT->GetRowMap())
	{
		if (const FAsdaPlusPackageRow* R = reinterpret_cast<const FAsdaPlusPackageRow*>(It.Value)) { Rows.Add(R); }
	}
	Rows.Sort([](const FAsdaPlusPackageRow& A, const FAsdaPlusPackageRow& B) { return A.Id < B.Id; });
	for (const FAsdaPlusPackageRow* R : Rows)
	{
		++Total;
		// Достаём РОВНО ТАК ЖЕ, как рантайм тултипа (DT_GameText через UAsdaTextSubsystem).
		const FString T = (Txt && R->NoticeIdMale   > 0) ? Txt->GetTextById(R->NoticeIdMale)   : FString();
		FString B       = (Txt && R->NoticeIdFemale > 0) ? Txt->GetTextById(R->NoticeIdFemale) : FString();
		const bool bTitleFound = !T.IsEmpty();
		const bool bBodyFound  = !B.IsEmpty();
		B = B.Replace(TEXT("%d"), *FString::FromInt(Days)).Replace(TEXT("[sd1]"), *FString::FromInt(Days));
		const bool bStub = (T.TrimStartAndEnd() == TEXT("-") || T.IsEmpty()) && (B.TrimStartAndEnd() == TEXT("-") || B.IsEmpty());
		if (bStub) { ++Stub; } else { ++Ok; }
		S += FString::Printf(TEXT("wingId=%d | заголовок loc %d [%s, %d симв.] = «%s»%s\n"),
			R->Id, R->NoticeIdMale, bTitleFound ? TEXT("НАЙДЕН в DT_GameText") : TEXT("НЕ НАЙДЕН"), T.Len(), *T,
			bStub ? TEXT("   ← ЗАГЛУШКА «-» В ДАННЫХ ИГРЫ") : TEXT(""));
		S += FString::Printf(TEXT("         | тело      loc %d [%s, %d симв.] = «%s»\n"),
			R->NoticeIdFemale, bBodyFound ? TEXT("НАЙДЕН в DT_GameText") : TEXT("НЕ НАЙДЕН"), B.Len(),
			*B.Replace(TEXT("\\n"), TEXT(" / ")).Replace(TEXT("\n"), TEXT(" / ")));
	}
	S += FString::Printf(
		TEXT("итого видов %d: с текстом %d, заглушек «-» %d (состояние ДАННЫХ игры, не порта). Дней подставлено: %d"),
		Total, Ok, Stub, Days);
	return S;
}

// Эмуляция 6585 (для проверки без сервера): ровно тот же путь, что у сетевого обработчика.
void UAsdaMiniStateSubsystem::SimulateWingState(int32 InWingId, int32 RemainSec, bool bOn)
{
	OnWingState(0, InWingId, RemainSec, bOn);
	if (!bOn) { HideTooltip(); }
}

// Скрыть СВОЙ тултип (Plus Package / иконка эффекта). ★ Именно свой: чужого владельца не трогаем.
void UAsdaMiniStateSubsystem::HideTooltip()
{
	if (SharedTip) { SharedTip->HideFor(EAsdaTipOwner::MiniState); }
	else if (TipWidget) { TipWidget->SetVisibility(ESlateVisibility::Collapsed); }
	bTipShown = false;
}

// Эталон case 9 → `Tooltip_LayoutNearCursor@0xEA26F0`: панель ставится у курсора.
//   ★ ДЕДУП: укладка целиком в `UAsdaTipPanel::LayoutAtCursor` (смещение +32/+32 и кламп к экрану
//   с отступом 5 px — как в эталоне; прежняя копия здесь смещала на +16/+16 и экран не учитывала,
//   из-за чего у правого/нижнего края подсказка вылезала за кадр).
void UAsdaMiniStateSubsystem::LayoutTooltipAtCursor()
{
	// Доводим за курсором только СВОЮ подсказку: за чужой следит её владелец (у тултипа предмета —
	//   покадровый `UAsdaItemTooltipSubsystem::Tick`).
	if (SharedTip && SharedTip->GetOwner() == EAsdaTipOwner::MiniState) { SharedTip->LayoutAtCursor(); }
}

// ═══════════════════ ЭТАП 2: ИКОНКИ АКТИВНЫХ ЭФФЕКТОВ (STATE_ICON00..27) ═══════════════════════
//
// Эталон: `HUD_PlayerBuffIcons_Update@0xE7CEB0` (ui_core.c:76454) — иконки + стек;
//         `HUD_PlayerBuffTray_Update@0xE7BFB0` (ui_core.c:76082) — то же + таймеры + авто-снятие
//         зелья скорости / свитка возврата. Второй вызывается раз в секунду из charBuffUpdateExpired.
// Обе функции: сначала ВСЕ 28 слотов гасятся и сбрасываются (id = −1, typeFlag = 0, текст = ""),
//   потом список обходится в порядке наложения и заполняются подряд первые свободные слоты
//   (`if (iconSlot < 28)` — 29-й эффект и далее просто не показывается).
// ★ ОТСТУПЛЕНИЕ ПО РЕАЛИЗАЦИИ (результат тот же): таймер РИСУЕТСЯ ВСЕГДА, а `bWithTimers` гейтит
//   только «хвост» Tray_Update со счётчиками зелья/свитка. Причина: в оригинале контролы таймера —
//   ДЕТИ контрола-иконки, поэтому Icons_Update гасит их вместе с иконкой и они переживают вызов
//   нетронутыми; в UMG-импорте это СОСЕДНИЕ узлы одной канвы, и «не рисовать» означало бы мигание
//   цифр на каждое изменение списка (до следующей секундной перерисовки).
void UAsdaMiniStateSubsystem::RefreshBuffIcons(bool bWithTimers)
{
	if (!bControlsReady || BuffSlots.Num() != 28) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Fx) { return; }

	// Прошлый набор id — эталон сравнивает его и при изменении гасит фокус тултипа.
	TArray<int32> PrevIds; PrevIds.Reserve(28);
	for (const FAsdaBuffSlotUi& S : BuffSlots) { PrevIds.Add(S.BuffId); }

	for (int32 i = 0; i < 28; ++i)
	{
		FAsdaBuffSlotUi& S = BuffSlots[i];
		S.BuffId = -1; S.TypeFlag = 0; S.StackCount = 0;
		SetVis(S.Icon, false);
		if (S.StackText) { S.StackText->SetText(FText::GetEmpty()); }
		HideBuffTimer(i);
	}

	const int32 Self = Fx->SelfUserIdx();
	const TArray<FAsdaEffectEntry>& List = Fx->GetList(Self);
	int32 IconSlot = 0;
	bool bHasSpeedPotion = false, bHasReturnScroll = false;
	bool bChanged = false;

	for (const FAsdaEffectEntry& E : List)
	{
		if (IconSlot >= 28) { break; }                 // эталон: лишние эффекты не показываются
		FAsdaBuffSlotUi& S = BuffSlots[IconSlot];
		FString Atlas, Name, Text; int32 Cell = 0;

		if (E.Type == 1)
		{
			// Соулмейт: иконка из soulstateinfotip (+36 файл, +68 ячейка). Таймер НЕ рисуется.
			if (!Fx->GetSoulStateInfo(E.Id, Name, Atlas, Cell, Text)) { continue; }
			if (!SetSlotIcon(IconSlot, Atlas, Cell)) { continue; }
			S.BuffId = E.Id; S.TypeFlag = 1; S.StackCount = E.IconStack;
		}
		else if (E.Type == 2)
		{
			// Предметный усилитель: сперва table2 (flag_116 == 2 → иконка @197/@229), иначе — путь
			//   «обычного предмета» (эталон CItemManager_GetItemRecord + stateinfotip по id записи).
			int32 SubType = -1, Flag116 = 0, BuffCell = 0; FString BuffIcon;
			const bool bTable2 = Fx->GetTable2(E.ItemCode, SubType, Flag116, BuffIcon, BuffCell);
			if (bTable2)
			{
				if (Flag116 != 2) { continue; }        // эталон: без флага 2 иконка не рисуется вовсе
				if (!SetSlotIcon(IconSlot, BuffIcon, BuffCell)) { continue; }
				S.BuffId = E.ItemCode;                 // ★ в слот кладётся ITEMCODE (нужен тултипу и 6142)
				S.TypeFlag = 2; S.StackCount = E.IconStack;
				DrawBuffTimer(IconSlot, E, /*Buff0SubType=*/0);
			}
			else
			{
				if (!Fx->GetStateInfo(E.Id, Name, Atlas, Cell, Text)) { continue; }
				if (!SetSlotIcon(IconSlot, Atlas, Cell)) { continue; }
				S.BuffId = E.Id; S.TypeFlag = 0; S.StackCount = E.IconStack;
			}
			// ★ «Зелье скорости» (id 52 в table2-пути, 265 в item-пути) и «свиток возврата» (266):
			//   эталон САМ снимает эффект, когда счётчик догоняет предел. Счётчики — это поля
			//   `gap_4C0[12]` / `gap_4C0[16]` персонажа, то есть ТЕ ЖЕ «вторые» HP/MP, что рисуют
			//   слои MINISTATE_HPBuff/MPBuff (ui_core.c:75447/75461) — в порту FAsdaVitals::HpSecond/MpSecond.
			//   ⚠ Второе условие эталона читает НЕВЫРОВНЕННЫЙ dword `&iMaxMp + 1` (0x46A), поле не
			//     разобрано — его НЕ воспроизводим (ссылок на семантику в срезе нет).
			if (E.Id == 52 || E.Id == 265 || E.Id == 266)
			{
				const UAsdaNetSubsystem::FAsdaVitals* V = Net ? Net->GetVitals(Self) : nullptr;
				const bool bScroll = (E.Id == 266);
				if (bScroll) { bHasReturnScroll = true; } else { bHasSpeedPotion = true; }
				const int32 Second  = V ? (bScroll ? V->MpSecond : V->HpSecond) : 0;
				const int32 Limit   = V ? (bScroll ? V->MpMax    : V->HpMax)    : 0;
				const int32 Current = V ? (bScroll ? V->MpCur    : V->HpCur)    : 0;
				// ★★★ ОБА правила эталона (`ui_core.c:76246` и `:76259`), раньше стояло одно и не то.
				//   Эталон снимает эффект, когда ТЕКУЩЕЕ значение дошло до максимума либо догнало
				//   второе — и в обоих случаях сам обнуляет второе, гася слой буффа.
				if (V && Limit > 0 && (Current >= Limit || Second <= Current))
				{
					SetVis(S.Icon, false);
					Fx->RemoveBySkillState(Self, E.Id, 2);
					if (UAsdaNetSubsystem::FAsdaVitals* VW = Net->VitalsByUserIdx.Find(Self))
					{
						if (bScroll) { VW->MpSecond = 0; } else { VW->HpSecond = 0; }
					}
					UE_LOG(LogAsdaMini, Log,
						TEXT("[мини-статус] эффект %d снят: текущее=%d второе=%d максимум=%d (эталон HUD_PlayerBuffIcons_Update)"),
						E.Id, Current, Second, Limit);
					bChanged = true;
					break;
				}
			}
		}
		else
		{
			// Скилловый бафф/дебафф: иконка из stateinfotip, таймер + число стека.
			if (!Fx->GetStateInfo(E.Id, Name, Atlas, Cell, Text)) { continue; }
			const FAsdaSkillEffectMeta& M = Fx->GetSkillMeta(E.Id);
			if (!M.bValid) { continue; }               // эталон требует И запись иконки, И SkillRecord
			if (!SetSlotIcon(IconSlot, Atlas, Cell)) { continue; }
			S.BuffId = E.Id; S.TypeFlag = 0; S.StackCount = E.IconStack;
			DrawBuffTimer(IconSlot, E, M.Buff0SubType);
			// Число стека поверх иконки — ТОЛЬКО при 2..30.
			//   Эталон пишет `<FC=ff00FFFF>N</F>` (buff0SubType == 1) либо `<FC=ffFFFF00>N</F>` (>= 2).
			//   ★ ОТСТУПЛЕНИЕ ПО ФОРМЕ (не по результату): у нас это НЕ разметка, а прямой цвет
			//     обычного UTextBlock — вывод идентичен («число заданного цвета»), зато нет подмены
			//     виджета на URichTextBlock 28 раз в секунду.
			if (E.Stack >= 2 && E.Stack <= 30 && S.StackText)
			{
				S.StackText->SetText(FText::AsNumber(E.Stack));
				const FColor C = (M.Buff0SubType >= 2) ? BuffStackColorDebuff : BuffStackColorBuff;
				S.StackText->SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(C)));
			}
		}
		++IconSlot;
	}

	// Эталонный хвост Tray_Update (ui_core.c:76438-76446): если зелья/свитка в списке НЕТ — счётчик
	//   обнуляется (`gap_4C0[12] = 0` / `gap_4C0[16] = 0`).
	// ⚠ В порту эти же поля — «отстающие» HP/MP (FAsdaVitals::HpSecond/MpSecond), которыми ЭТАП 1
	//   рисует слои MINISTATE_HPBuff/MPBuff. Обнуление раз в секунду гасило бы «отстающую» полосу,
	//   поэтому вынесено в параметр и по умолчанию ВЫКЛЮЧЕНО (решение по нему — за юзером).
	if (bWithTimers && Net && bResetVitalSecondWithoutPotion)
	{
		if (UAsdaNetSubsystem::FAsdaVitals* V = Net->VitalsByUserIdx.Find(Self))
		{
			if (!bHasSpeedPotion)  { V->HpSecond = 0; }
			if (!bHasReturnScroll) { V->MpSecond = 0; }
		}
	}

	// Набор иконок изменился → снять фокус тултипа (эталон bIconsChanged → CControl_KillFocus).
	for (int32 i = 0; i < 28 && !bChanged; ++i)
	{
		if (PrevIds[i] != BuffSlots[i].BuffId) { bChanged = true; }
	}
	if (bChanged && bTipShown && HoverBuffSlot >= 0)
	{
		HideTooltip();
		HoverBuffSlot = -1;
	}
}

// Иконка слота = ЯЧЕЙКА 24×24 атласа. Эталон: IconAtlas_IndexToUV = gridIndexToXY_10col@0xEBFF70
//   (X = 24*(i%10), Y = 24*(i/10)) + CControl_DrawTextureRect(ctrl, tex, X, Y, 24, 24).
//   Атласы (state_Icon_1/state_icon/Premium_Buf_icon) — 256×256, то есть ровно 10 колонок по 24 px.
bool UAsdaMiniStateSubsystem::SetSlotIcon(int32 Slot, const FString& AtlasName, int32 Cell)
{
	if (!BuffSlots.IsValidIndex(Slot) || AtlasName.IsEmpty()) { return false; }
	UImage* Img = BuffSlots[Slot].Icon.Get();
	if (!Img) { return false; }
	// ★ 2026-08-29: арифметика ячейки атласа переехала в ОБЩУЮ ТОЧКУ `AsdaUi::ApplyAtlasCellIcon`
	//   — её же зовёт панель ОТРЯДА (28 слотов состояний на соотрядца). Второй копии формулы
	//   в проекте быть не должно (закон `port-shared-points`); поведение тут не изменилось.
	if (!AsdaUi::ApplyAtlasCellIcon(Img, AtlasName, Cell, BuffIconCellPx, BuffIconCols))
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[мини-статус] атлас иконки эффекта '%s' не найден в /Game/AsdaAssets/Textures/JUI"), *AtlasName);
		return false;
	}
	SetVis(Img, true);
	return true;
}

void UAsdaMiniStateSubsystem::HideBuffTimer(int32 Slot)
{
	if (!BuffSlots.IsValidIndex(Slot)) { return; }
	for (const TObjectPtr<UImage>& T : BuffSlots[Slot].Timer) { SetVis(T.Get(), false); }
}

// ★★★ ТАЙМЕР БАФФА = СПРАЙТЫ 9×9, а не круговой индикатор.
// Эталон Skill_DrawBuffTimerIcon@0xE830B0 (skill_buff.c:34917) 1:1:
//   secs → d/h/m/s, каждый компонент клампится к 99, берётся ПЕРВЫЙ ненулевой;
//   скрыть всё, если ненулевого нет, ИЛИ rec+2 >= 1201 (баффы «хранителя»), ИЛИ rec+7 < 0;
//   атлас: type 2 → GreenBufftime.tex; skill buff0SubType < 2 → bufftime.tex; >= 2 → RedBufftime.tex;
//   глиф цифры d → u = 9*d; глиф единицы (0=d,1=h,2=m,3=s) → u = 9*(idx+10); ':' → u = 126; v = 0;
//   особый режим «меньше 4 минут и не 0»: показывается M:SS.
void UAsdaMiniStateSubsystem::DrawBuffTimer(int32 Slot, const FAsdaEffectEntry& E, int32 Buff0SubType)
{
	if (!BuffSlots.IsValidIndex(Slot)) { return; }
	FAsdaBuffSlotUi& S = BuffSlots[Slot];
	if (S.Timer.Num() != 5) { return; }

	const int32 Total = E.RemainSec;
	int32 Units[4];
	Units[0] = Total / 86400;                    // d
	Units[1] = (Total % 86400) / 3600;           // h
	Units[2] = ((Total % 86400) % 3600) / 60;    // m
	Units[3] = Total % 60;                       // s
	int32 FirstIdx = -1, FirstVal = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		if (Units[i] > 99) { Units[i] = 99; }
		if (Units[i] != 0 && FirstIdx < 0) { FirstIdx = i; FirstVal = Units[i]; }
	}
	if (FirstIdx == -1 || E.IdFlag >= 1201 || E.RemainSec < 0)
	{
		HideBuffTimer(Slot);
		return;
	}
	// Атлас цифр.
	FString TexName = BuffTimeTexNormal;
	if (E.Type == 2)              { TexName = BuffTimeTexItem; }
	else if (Buff0SubType >= 2)   { TexName = BuffTimeTexDebuff; }
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): короткий доступ к менеджеру текстур
	UTexture2D* Font = TexMgr ? TexMgr->LoadUITexture(FName(*TexName)) : nullptr;
	UTexture2D* Back = TexMgr ? TexMgr->LoadUITexture(FName(*BuffTimeBackTex)) : nullptr;
	if (!Font) { HideBuffTimer(Slot); return; }
	const FIntPoint FI = AsdaUi::LogicalAtlasSize(Font);
	const float FW = (float)FMath::Max(1, FI.X), FH = (float)FMath::Max(1, FI.Y);

	for (int32 i = 0; i < 5; ++i) { SetVis(S.Timer[i].Get(), true); }
	// Подложка (эталон HUD_InitControls: DrawTextureRect("bufftimeback.tex", 0,0,24,10) @ (0,24)).
	if (S.Timer[0] && Back)
	{
		FSlateBrush B; B.SetResourceObject(Back); B.ImageSize = FVector2D(24.f, 10.f);
		B.SetUVRegion(FBox2D(FVector2D(0.f, 0.f), FVector2D(1.f, 1.f)));
		S.Timer[0]->SetBrush(B);
	}
	// Позиция глифа внутри слота (локальные X эталона, Y всегда 24).
	auto PlaceGlyph = [this, &S](int32 GlyphIdx, float LocalX)
	{
		UImage* W = S.Timer.IsValidIndex(GlyphIdx) ? S.Timer[GlyphIdx].Get() : nullptr;
		UCanvasPanelSlot* CS = W ? AsdaUi::SlotOf(W) : nullptr;
		UCanvasPanelSlot* IS = S.Icon ? AsdaUi::SlotOf(S.Icon) : nullptr;
		if (!CS || !IS) { return; }
		const FMargin IO = IS->GetOffsets();
		CS->SetOffsets(FMargin(IO.Left + LocalX, IO.Top + BuffTimerY, 9.f, 9.f));
	};
	// Нарисовать глиф `Index` (0..9 цифры, 10..13 dhms, 14 ':') в контрол `GlyphIdx` (1..4).
	auto DrawGlyph = [&S, Font, FW, FH](int32 GlyphIdx, int32 Index)
	{
		UImage* W = S.Timer.IsValidIndex(GlyphIdx) ? S.Timer[GlyphIdx].Get() : nullptr;
		if (!W) { return; }
		const float U = 9.f * (float)Index;
		FSlateBrush B; B.SetResourceObject(Font); B.ImageSize = FVector2D(9.f, 9.f);
		B.SetUVRegion(FBox2D(FVector2D(U / FW, 0.f), FVector2D((U + 9.f) / FW, 9.f / FH)));
		W->SetBrush(B);
	};

	const FString NumStr = FString::FromInt(FirstVal);
	const int32 Len = NumStr.Len();
	const int32 Days = Units[0], Hours = Units[1], Mins = Units[2], Secs = Units[3];

	if (Days || Hours || Mins > 3 || Mins == 0)
	{
		// Формат «NNx» — число + буква единицы.
		PlaceGlyph(1, 1.f); PlaceGlyph(2, 7.f); PlaceGlyph(3, 14.f); PlaceGlyph(4, 4.f);
		SetVis(S.Timer[4].Get(), false);
		if (Len == 1)
		{
			DrawGlyph(1, 0);                                   // ведущий ноль (эталон: u = 0)
			DrawGlyph(2, (int32)(NumStr[0] - TCHAR('0')));
			DrawGlyph(3, FirstIdx + 10);
		}
		else if (Len == 2)
		{
			DrawGlyph(1, (int32)(NumStr[0] - TCHAR('0')));
			DrawGlyph(2, (int32)(NumStr[1] - TCHAR('0')));
			DrawGlyph(3, FirstIdx + 10);
		}
		else   // 3 разряда: недостижимо (компоненты клампятся к 99), но ветка эталона есть
		{
			PlaceGlyph(1, -1.f); PlaceGlyph(2, 4.f); PlaceGlyph(3, 10.f); PlaceGlyph(4, 16.f);
			SetVis(S.Timer[4].Get(), true);
			DrawGlyph(1, (int32)(NumStr[0] - TCHAR('0')));
			DrawGlyph(2, (int32)(NumStr[1] - TCHAR('0')));
			// ★ ДЕФЕКТ ОРИГИНАЛА: третью цифру эталон рисует ПОВЕРХ второго глифа (адрес +1384
			//   вместо +1388), из-за чего третий контрол показывает мусор прошлого кадра.
			//   По закону fix-original-defects НЕ воспроизводим — рисуем в свой контрол.
			DrawGlyph(3, (int32)(NumStr[2] - TCHAR('0')));
			DrawGlyph(4, FirstIdx + 10);
		}
	}
	else
	{
		// Особый режим «меньше 4 минут и не 0»: M:SS.
		const FString SecStr = FString::FromInt(Secs);
		PlaceGlyph(1, 0.f); PlaceGlyph(2, 8.f); PlaceGlyph(3, 14.f); PlaceGlyph(4, 4.f);
		SetVis(S.Timer[4].Get(), true);
		DrawGlyph(1, Mins);
		if (SecStr.Len() == 1)
		{
			DrawGlyph(2, 0);
			DrawGlyph(3, (int32)(SecStr[0] - TCHAR('0')));
		}
		else
		{
			DrawGlyph(2, (int32)(SecStr[0] - TCHAR('0')));
			DrawGlyph(3, (int32)(SecStr[1] - TCHAR('0')));
		}
		DrawGlyph(4, 14);   // разделитель ':' (эталон: u = 126 = 9*14)
	}
}

// Ивент-иконки: контейнер + одна из 7 EXP + одна из 3 Drop (эталон gameUi_updateScheduledClockDisplay).
void UAsdaMiniStateSubsystem::RefreshEventIcons()
{
	if (!bControlsReady) { return; }
	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	// Эталон СНАЧАЛА гасит контейнер и все 10 иконок.
	SetVis(EventBuffGroup, false);
	for (const TObjectPtr<UImage>& I : ExpBuffIcons)  { SetVis(I.Get(), false); }
	for (const TObjectPtr<UImage>& I : DropBuffIcons) { SetVis(I.Get(), false); }

	bool bCont = false; int32 ExpIdx = -1, DropIdx = -1;
	Fx->GetEventIconIndices(bCont, ExpIdx, DropIdx);
	if (!bCont) { return; }
	SetVis(EventBuffGroup, true);
	if (ExpBuffIcons.IsValidIndex(ExpIdx))   { SetVis(ExpBuffIcons[ExpIdx].Get(), true); }
	if (DropBuffIcons.IsValidIndex(DropIdx)) { SetVis(DropBuffIcons[DropIdx].Get(), true); }
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] ивент-бонус: множитель ×%.2f → EXP idx=%d, Drop idx=%d"),
		Fx->EventRate, ExpIdx, DropIdx);
}

void UAsdaMiniStateSubsystem::OnEffectsChanged(int32 UserIdx, bool bWithTimers)
{
	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx || UserIdx != Fx->SelfUserIdx()) { return; }   // мини-статус рисует ТОЛЬКО свои эффекты
	RefreshBuffIcons(bWithTimers);
}

void UAsdaMiniStateSubsystem::OnEventBonusChanged()
{
	RefreshEventIcons();
}

// Наведение и клик по иконкам эффектов. Эталон HUD_PlayerBuffIcon_OnClick@0xE7DB40:
//   case 8  (курсор вошёл в контрол id 60..88, кроме 81) → построить тултип:
//              typeFlag 0 → uiHelpEntryLayoutAndShow    (stateinfotip: имя +4, текст +72)
//              typeFlag 1 → uiHelpEntryLayoutAndShowAlt (soulstateinfotip)
//              typeFlag 2 → ItemTooltip_BuildTable2Info (specialitem_tooltiplist по itemCode)
//   case 9  → Tooltip_LayoutNearCursor; case 10 → CControl_KillFocus;
//   case 11 (клик) → sendInventoryActionRequest → опкод 6142.
void UAsdaMiniStateSubsystem::TickBuffPointer()
{
	if (!bControlsReady || BuffSlots.Num() != 28) { return; }
	int32 Over = -1;
	for (int32 i = 0; i < 28; ++i)
	{
		if (BuffSlots[i].BuffId < 0) { continue; }
		if (IsCursorOver(BuffSlots[i].Icon.Get())) { Over = i; break; }
	}
	if (Over != HoverBuffSlot)
	{
		HoverBuffSlot = Over;
		if (Over >= 0)                                        { ShowBuffTooltip(Over); }            // case 8
		else if (bTipShown && !IsCursorOver(PlusPackIcon))    { HideTooltip(); }                    // case 10
	}
	else if (Over >= 0 && bTipShown)
	{
		LayoutTooltipAtCursor();                                                                    // case 9
	}

	// Отмена баффа (эталон case 11 → sendInventoryActionRequest → опкод 6142).
	// ★ ТОТ ЖЕ код события 11 обслуживает и `MiniState_Click` (id 97), который ЭТАП 1 реализовал как
	//   ДВОЙНОЙ ЛКМ (в оригинале событие приходит готовым от менеджера окон, различия в срезе нет).
	//   Держим одну трактовку на оба случая: по умолчанию отмена — по ДВОЙНОМУ клику
	//   (`bBuffCancelOnDoubleClick`), иначе бафф снимался бы от случайного одиночного клика.
	// ★ Аудит №3 (B3): ВТОРОЙ экземпляр опроса — свой латч у иконок эффектов (см. поле `BuffMouse`).
	//   Slate не поднят ⇒ общая точка читает «кнопки отпущены» и обнуляет латч — ровно как прежнее
	//   `bLmbNow = IsInitialized() && Contains(...)` с последующей записью латча.
	BuffMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал, — здесь это ОТМЕНА эффекта (уходит 6142).
	//   ★ Наведение разбирается ВЫШЕ опроса (так стоял прежний опрос), поэтому плашку эффекта,
	//     зажжённую этим же кадром, гасим здесь.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (HoverBuffSlot >= 0) { HoverBuffSlot = -1; if (bTipShown) { HideTooltip(); } }
		BuffDbl.Reset();
		BuffMouse.Reset();
		return;
	}
	if (Over >= 0 && BuffMouse.bLmbDown)
	{
		bool bFire = true;
		if (bBuffCancelOnDoubleClick)
		{
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время (было — `DoubleClickSec` игровым временем).
			double Now = 0.0, Win = 0.0;
			DblClickNowAndWindow(Now, Win);
			bFire = BuffDbl.ClickAt(Over, -1, Now, Win);
		}
		if (bFire)
		{
			UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (Fx)
			{
				UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] отмена баффа по клику: слот %d id=%d typeFlag=%d"),
					Over, BuffSlots[Over].BuffId, BuffSlots[Over].TypeFlag);
				Fx->RequestCancel(BuffSlots[Over].TypeFlag, BuffSlots[Over].BuffId);
			}
		}
	}
	// ★ Аудит №3 (B3): запись латча делает `BuffMouse.Update()` выше.
}

void UAsdaMiniStateSubsystem::ShowBuffTooltip(int32 Slot)
{
	if (!BuffSlots.IsValidIndex(Slot)) { return; }
	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	const FAsdaBuffSlotUi& S = BuffSlots[Slot];
	FString Name, Icon, Text; int32 Cell = 0;
	bool bOk = false;
	if (S.TypeFlag == 0)      { bOk = Fx->GetStateInfo(S.BuffId, Name, Icon, Cell, Text); }
	else if (S.TypeFlag == 1) { bOk = Fx->GetSoulStateInfo(S.BuffId, Name, Icon, Cell, Text); }
	else                      { bOk = Fx->GetItemTooltipText(S.BuffId, Name, Text); }
	if (!bOk)
	{
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] тултип эффекта: записи нет (слот %d, id %d, typeFlag %d)"),
			Slot, S.BuffId, S.TypeFlag);
		return;
	}
	LayoutAndShowTip(Name, Text, /*bBuffFormula=*/true);
}

// Дамп слотов эффектов + ивент-иконок (доказательная строка asda.Mini.BuffDump).
FString UAsdaMiniStateSubsystem::DumpBuffSlots() const
{
	FString S;
	S += FString::Printf(TEXT("=== ИКОНКИ ЭФФЕКТОВ (STATE_ICON00..27) === слотов=%d, прототипов таймера=%d\n"),
		BuffSlots.Num(), TimerProtos.Num());
	int32 Used = 0;
	for (int32 i = 0; i < BuffSlots.Num(); ++i)
	{
		const FAsdaBuffSlotUi& B = BuffSlots[i];
		if (B.BuffId < 0) { continue; }
		++Used;
		FVector2D P, Sz;
		const bool bR = GetSlotRect(B.Icon.Get(), P, Sz);
		int32 VisTimer = 0;
		for (const TObjectPtr<UImage>& T : B.Timer)
		{
			if (T && T->GetVisibility() != ESlateVisibility::Collapsed) { ++VisTimer; }
		}
		S += FString::Printf(TEXT("  слот %02d: id=%d typeFlag=%d стек=%d текст='%s' иконка=%s%s глифов таймера видимо=%d\n"),
			i, B.BuffId, B.TypeFlag, B.StackCount,
			B.StackText ? *B.StackText->GetText().ToString() : TEXT("—"),
			(B.Icon && B.Icon->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("видима") : TEXT("скрыта"),
			bR ? *FString::Printf(TEXT("(%.0f,%.0f %.0fx%.0f)"), P.X, P.Y, Sz.X, Sz.Y) : TEXT(""),
			VisTimer);
	}
	S += FString::Printf(TEXT("  занято слотов: %d из 28 (лишние эффекты не показываются — эталон if (iconSlot < 28))\n"), Used);
	{
		FString E;
		for (int32 i = 0; i < ExpBuffIcons.Num(); ++i)
		{
			if (ExpBuffIcons[i] && ExpBuffIcons[i]->GetVisibility() != ESlateVisibility::Collapsed) { E += FString::Printf(TEXT("EXP%d "), i); }
		}
		for (int32 i = 0; i < DropBuffIcons.Num(); ++i)
		{
			if (DropBuffIcons[i] && DropBuffIcons[i]->GetVisibility() != ESlateVisibility::Collapsed) { E += FString::Printf(TEXT("Drop%d "), i); }
		}
		S += FString::Printf(TEXT("  ивент-иконки: контейнер=%s видимы [%s] (EXP-иконок в ассете %d, Drop-иконок %d)\n"),
			(EventBuffGroup && EventBuffGroup->GetVisibility() != ESlateVisibility::Collapsed) ? TEXT("видим") : TEXT("скрыт"),
			E.IsEmpty() ? TEXT("нет") : *E, ExpBuffIcons.Num(), DropBuffIcons.Num());
	}
	return S;
}
