// Copyright (c) 2026 @sitxovski (Discord)
// AsdaTitleSubsystem_Net.cpp — ★ R3-c 12.11.2026 (разрез AsdaTitleSubsystem.cpp, дословный перенос): сеть: выдача, маски, потеря, удаление и находка званий, слоты, звания на табличке имени.
//   Общее для частей — в AsdaTitleSubsystem_Private.h; шапка и реверс — в AsdaTitleSubsystem.cpp.
#include "UI/AsdaTitleSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaUiHelpers.h"   // ★ U20: AsdaUi::BindButton
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "UI/AsdaHudSubsystem.h"       // ★ ShowTitleBanner — баннер «звание получено» (тип 2)
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"

#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "AsdaTitleSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaTitleSubsystem*.cpp

namespace {
	// ★★★ D8 — СНЯТИЕ ЗВАНИЯ ВЖИВУЮ. Снимать ли НАДЕТОЕ звание, если сервер убрал его бит.
	ASDA_DEV_CVAR(int32, CVarUnwearOnLoss,
		TEXT("asda.Title.UnwearOnLoss"), 1,
		TEXT("D8. 1 (по умолчанию) = утраченное звание, если оно было надето, снимается локально\n")
		TEXT("сразу по приходу маски 6066 и убирается с плашки над головой — не дожидаясь 6068.\n")
		TEXT("0 = как в оригинале: маска обновится, а надпись над головой останется до 6068\n")
		TEXT("(или до перезахода) — эталон надетое звание при утрате не гасит вовсе."),
		ECVF_Default);

	// ★★★ D8 — нотис игроку об утрате. ТРЁХПОЗИЦИОННЫЙ — отдельного ключа под «старый текст»
	//   не заводим (требование «не плодить лишних ключей»).
	ASDA_DEV_CVAR(int32, CVarLossNotice,
		TEXT("asda.Title.LossNotice"), 1,
		TEXT("D8. Нотис об утрате звания в СИСТЕМНЫЙ ЧАТ (категория Progress = журнал 2570 + буфер B).\n")
		TEXT("1 (по умолчанию) = НАША строка 990110 «Ты утратил звание „[ss1]“.» с подстановкой\n")
		TEXT("имени из title_list (решение владельца 2026-08-30). Если имени в таблице нет —\n")
		TEXT("печатается оригинальная 20502 (запасной вариант, а не пустое место).\n")
		TEXT("2 = только оригинальная 20502 «Звание потеряно после обновления системы званий.»\n")
		TEXT("без имени. 0 = как в оригинале: молча, только значок HUD."),
		ECVF_Default);
}

// ═════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════
// 6065 — звание получено (flag 0) либо потеряно (flag 1).
void UAsdaTitleSubsystem::HandleGranted(const TArray<uint8>& B)
{
	if (B.Num() < 49) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 UserIdx = AsdaBytes::RdI16(&B[0]);
	const int32 CharId  = AsdaBytes::RdI32(&B[2]);
	const int32 TitleId = AsdaBytes::RdI16(&B[6]);
	const bool  bLost   = (B[8] != 0);

	// ★★★ ЭФФЕКТ НА ПЕРСОНАЖЕ — СТРОГО ДО ГЕЙТА «СВОЙ» (эталон `uiTitle_OnJobChangeResult@0xE9A9F0`,
	//   ui_core.c:82313-82322): персонаж ищется по `userIdx` пакета, эффект спавнится, и ТОЛЬКО
	//   ПОТОМ идёт `if (self->userIdx == charId)` вокруг всей работы с окном. То есть эффект видно
	//   и над ЧУЖИМ игроком, получившим звание. Раньше порт выходил здесь и не показывал ничего.
	//   ⚠ Спавнится он и при флаге «потеряно» — вызов в эталоне стоит ДО разбора флага.
	SpawnTitleEffect(UserIdx, /*«получено»*/ false);

	if (!AsdaSelf::IsSelfCharId(Net, CharId)) { return; }   // дальше — только своё окно и своя маска. ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)

	const bool bWasOwned = MaskGet(MaskOwned, TitleId);
	MaskSet(MaskOwned, TitleId, !bLost);
	// ★ Маркер «потеряно» при утрате ставит ОБЩАЯ ТОЧКА `ApplyTitleLoss` (ниже) — здесь его
	//   выставлять НЕЛЬЗЯ, иначе её дедуп примет собственную утрату за повтор и промолчит.
	if (bLost) { FlagNew.Remove(TitleId); }
	else       { FlagNew.Add(TitleId);  FlagLost.Remove(TitleId); }
	FamePoints = AsdaBytes::RdI32(&B[45]);

	if (!bLost)
	{
		// Блок статов звания приходит прямо здесь (эталон копирует 36 байт в свою карту).
		TArray<int16> Stats; Stats.SetNumZeroed(18);
		for (int32 i = 0; i < 18; ++i) { Stats[i] = (int16)AsdaBytes::RdI16(&B[9 + 2 * i]); }
		TitleStats.Add(TitleId, MoveTemp(Stats));
	}
	// ★ 1:1: эталон зажигает значок «найдено» (`sub_9396B6(this, 1, 0)`) ДАЖЕ при флаге «потеряно»
	//   (`ui_core.c:82324` — вызов стоит ДО разбора флага). Не трогаем: при утрате его тут же
	//   перекроет `ApplyTitleLoss` иконкой «потеряно», а при получении он и должен быть таким.
	ShowNoticeIcon(/*иконка «найдено»*/ 1, /*подсказка 20500 «У тебя новое звание»*/ 0);

	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 6065: звание %d %s (userIdx=%d, очки=%d)"),
		TitleId, bLost ? TEXT("ПОТЕРЯНО") : TEXT("ПОЛУЧЕНО"), UserIdx, FamePoints);

	// ★★★ БАННЕР «ЗВАНИЕ ПОЛУЧЕНО» (`changeJobBannerShow(g_pChangeJobBanner, 2, 500, 500, 500)`,
	//   ui_core.c:82339). Три гейта эталона стоят ровно здесь и ровно в этом порядке:
	//     (а) пакет про своего — проверено выше (`CharId != SelectedCharId` → выход);
	//     (б) окно званий ЗАКРЫТО — у эталона это `else` к ветке `*(BYTE*)(this[1]+292) == 1`
	//         («окно видимо»), в которой он вместо баннера перерисовывает список;
	//     (в) флаг «получено» (`!*((BYTE*)a2 + 8)`), при утрате баннера нет.
	if (!bLost && !bOpen)
	{
		if (UAsdaHudSubsystem* Hud = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>() : nullptr)
		{
			Hud->ShowTitleBanner();
		}
	}

	// ★★★ D8. Второй путь утраты — тот же общий обработчик. Идемпотентно: если бита уже не было
	//   (маска 6066 успела прийти первой), повторно ничего не снимаем и нотис не дублируем.
	if (bLost && bWasOwned) { ApplyTitleLoss({ TitleId }, TEXT("6065")); }

	if (bOpen) { RebuildOrder(); RefreshList(); RefreshHeader(); RefreshSelfTitles(); LayoutScrollBar(); }
}

// ★ ОБЩАЯ ТОЧКА разбора маски из тела пакета — одна на 6066 и 6086.
//   Формат САМООПИСАТЕЛЕН ПО ДЛИНЕ ТЕЛА: старая часть (32 б) лежит там же, где и была, а
//   расширенные 32 б дописаны В ХВОСТ. Поэтому:
//     • тело старой длины (42 у 6066 / 38 у 6086) — сервер НЕ обновлён: биты 256…511 = 0;
//     • тело новой длины (74 / 70)               — сервер обновлён: хвост = биты 256…511.
//   Отдельного «рукопожатия» не нужно: старый клиент читает фиксированные 42/38 байт и хвост
//   игнорирует (`netOnCharEquipUpdate@0xE9A6F0` копирует ровно 0x20 байт), а новый сервер
//   всегда шлёт расширенный вариант. Обратное направление (6067/6069) не менялось вовсе —
//   там titleId уже i16 и id ≥ 256 в него влезает.
//   Возвращает: сколько бит взведено; bOutExtended — пришёл ли расширенный формат.
int32 UAsdaTitleSubsystem::ParseMaskBody(const TArray<uint8>& B, int32 Offset, uint8* OutMask,
                                         bool& bOutExtended)
{
	const bool  bWant512 = (EffectiveMaskBits() > kMaskBitsLegacy);
	const int32 HiOffset = Offset + kMaskBytesLegacy;              // хвост идёт сразу за старой частью
	bOutExtended = bWant512 && (B.Num() >= HiOffset + kMaskBytesLegacy);

	FMemory::Memzero(OutMask, kMaskBytes);
	FMemory::Memcpy(OutMask, &B[Offset], kMaskBytesLegacy);        // биты 0…255 — как в оригинале
	if (bOutExtended)
	{
		FMemory::Memcpy(OutMask + kMaskBytesLegacy, &B[HiOffset], kMaskBytesLegacy);  // биты 256…511
	}

	int32 N = 0;
	for (int32 i = 0; i < EffectiveMaskBits(); ++i) { if (MaskGet(OutMask, i)) { ++N; } }
	return N;
}

// 6066 — полная маска «получено» + очки славы. Тело 42 б (старый сервер) либо 74 б (новый).
void UAsdaTitleSubsystem::HandleMaskOwned(const TArray<uint8>& B)
{
	if (B.Num() < kBody6066Legacy) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!AsdaSelf::IsSelfCharId(Net, AsdaBytes::RdI32(&B[2]))) { return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	FamePoints = AsdaBytes::RdI32(&B[6]);

	// ★★★ D8. СНИМОК МАСКИ ДО РАЗБОРА — на нём держится «ультимативное снятие».
	//   Эталон этого не делает: `netOnCharEquipUpdate` ветка `a3 == 1` тупо перезаписывает
	//   `memcpy(&char[1].worldMatrix[6], тело+10, 0x20)`. Сравнить старую маску с новой — ЕДИНСТВЕННЫЙ
	//   способ узнать об отнятом сервером звании, когда 6065 «потеряно» не приходит.
	uint8 Prev[kMaskBytes];
	FMemory::Memcpy(Prev, MaskOwned, sizeof(Prev));
	const bool bHadMask = bMaskOwnedSeen;
	const bool bLowOnly = !bHadMask && bCompareLowAfterMapChange;   // первая маска после портала
	bCompareLowAfterMapChange = false;

	bool bExt = false;
	const int32 N = ParseMaskBody(B, /*смещение маски в теле 6066*/ 10, MaskOwned, bExt);
	bMaskOwnedSeen = true;

	// Биты 1→0 = утраты — только относительно маски ЭТОЙ загрузки (`bHadMask`). ★ 18.10.2026: прежняя запись «первая маска
	//   ложится поверх нулей» неверна — после смены карты снимок прошлой карты не нулевой (см. `ResetSessionState`).
	TArray<int32> Lost;
	int32 Gained = 0;
	for (int32 i = 0; i < EffectiveMaskBits(); ++i)
	{
		const bool bWas = MaskGet(Prev, i);
		const bool bNow = MaskGet(MaskOwned, i);
		if (bWas && !bNow && (bHadMask || (bLowOnly && i < kMaskBitsLegacy))) { Lost.Add(i); }
		else if (!bWas && bNow && bHadMask)
		{
			// ★ ПРИОБРЕТЕНИЕ. Гасим маркер «потеряно»: без этого дедуп `ApplyTitleLoss` считал бы
			//   звание «уже утраченным» вечно, и цикл «потерял → вернули → снова забрали» второй
			//   раз промолчал бы. Первую маску сессии (`!bHadMask`) не считаем приобретениями —
			//   там взводятся все биты сразу, это не событие.
			FlagLost.Remove(i);
			++Gained;
		}
	}

	UE_LOG(LogAsdaTitle, Log,
		TEXT("[звания] 6066: маска «получено» — %d бит, очки=%d (тело %d б, формат %s, %s, утрачено %d, приобретено %d)"),
		N, FamePoints, B.Num(), bExt ? TEXT("512 бит") : TEXT("256 бит"),
		bHadMask ? TEXT("обновление") : TEXT("ПЕРВАЯ за сессию"), Lost.Num(), Gained);

	// ★ Применяем утраты ДО перерисовки: маркеры/слоты должны быть готовы к моменту RefreshList.
	if (Lost.Num() > 0) { ApplyTitleLoss(Lost, TEXT("6066")); }

	if (bOpen) { RebuildOrder(); RefreshList(); RefreshHeader(); RefreshSelfTitles(); LayoutScrollBar(); }
}

// ★★★ D8. ОБЩАЯ ТОЧКА УТРАТЫ ЗВАНИЙ — одна на диффер маски 6066 и на 6065 с флагом «потеряно».
//   ★ РАБОТАЕТ ПРИ ЗАКРЫТОМ ОКНЕ. Ничего из того, что здесь делается, не требует ни виджета, ни
//   `bControlsReady`: маркеры — множества, значок HUD поднимает себе виджет сам (`SetupNoticeWidget`),
//   плашка над головой живёт в мире, нотис уходит в чат. Перерисовка окна — только если оно открыто.
void UAsdaTitleSubsystem::ApplyTitleLoss(const TArray<int32>& LostIds, const TCHAR* Src)
{
	if (LostIds.Num() == 0) { return; }

	// ★★★ ДЕДУП (контракт P24). Сервер сообщает об ОДНОЙ утрате ДВАЖДЫ: явным 6070 (код 1,
	//   несёт titleId) и неявно — маской 6066, в которой бита уже нет. Порядок сетью НЕ
	//   ГАРАНТИРОВАН, поэтому «кто первый» решать нельзя: фильтруем по СОСТОЯНИЮ.
	//   Признак «эту утрату уже отработали» = маркер «потеряно» стоит И звание сейчас не получено.
	//   Маркер снимается при повторном получении звания (6065 и диффер 6066, ветка «приобретения»),
	//   поэтому «потерял → вернул → снова потерял» announce-ится второй раз, как и должно.
	//   ⚠ ПОЭТОМУ `FlagLost.Add` живёт ТОЛЬКО ЗДЕСЬ: если его выставит вызывающий, фильтр
	//   съест собственную же утрату (на этом и попался первый заход — 6070 ставил маркер сам).
	TArray<int32> Fresh;
	TArray<int32> Dup;
	for (const int32 Id : LostIds)
	{
		if (FlagLost.Contains(Id) && !MaskGet(MaskOwned, Id)) { Dup.Add(Id); } else { Fresh.Add(Id); }
	}
	if (Dup.Num() > 0)
	{
		UE_LOG(LogAsdaTitle, Log,
			TEXT("[звания] УТРАТА (%s): %d id уже отработаны раньше — повтор проигнорирован (дедуп)"),
			Src, Dup.Num());
	}
	if (Fresh.Num() == 0) { return; }

	for (const int32 Id : Fresh)
	{
		FlagLost.Add(Id);        // мигающий маркер «потеряно» в строке (эталонное поле rec+13)
		FlagNew.Remove(Id);      // «новое» и «потеряно» взаимоисключающи
		TitleStats.Remove(Id);   // бонусы утраченного звания больше не действуют
		const FAsdaTitleDef* D = FindDef(Id);
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] УТРАТА (%s): id=%d «%s»"),
			Src, Id, D ? *D->Name : TEXT("<нет в таблице>"));
	}

	// Значок HUD «потеряно» (50001) с подсказкой 20502 — эталонная пара иконка/нотис.
	ShowNoticeIcon(/*иконка «потеряно»*/ 0, /*подсказка 20502*/ 2);

	// Снять надетое утраченное звание и протолкнуть в плашку.
	//   ★ Если сервер прислал 6068 с уже очищенными слотами РАНЬШЕ (так и идёт контракт P24),
	//   снимать нечего — `UnwearLostTitles` вернёт false, и плашку мы не трогаем: её уже
	//   обновил штатный приём 6068 в `AsdaWorldSubsystem::OnCharTitle`.
	const bool bUnworn = (CVarUnwearOnLoss.GetValueOnGameThread() != 0) && UnwearLostTitles(Fresh);
	if (bUnworn) { PushSelfTitlesToNameplate(); }

	if (CVarLossNotice.GetValueOnGameThread() != 0) { NoticeTitleLost(Fresh); }
}

// ★ D8. Утраченный id стоял в слоте активного звания → слот обнуляется.
//   Выбор игрока (`SelPrefix`/`SelSuffix`) чистим ТОЛЬКО при закрытом окне: при открытом это
//   несохранённый выбор пользователя, и трогать его нельзя — он и так не пройдёт гейт «Применить»
//   (звание больше не получено), а «ОК» отправит 6067 с −1 из надетых слотов.
bool UAsdaTitleSubsystem::UnwearLostTitles(const TArray<int32>& LostIds)
{
	bool bAny = false;
	if (WornPrefix >= 0 && LostIds.Contains(WornPrefix))
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] снята НАДЕТАЯ приставка id=%d (звание утрачено)"), WornPrefix);
		WornPrefix = -1; bAny = true;
	}
	if (WornSuffix >= 0 && LostIds.Contains(WornSuffix))
	{
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] снято НАДЕТОЕ окончание id=%d (звание утрачено)"), WornSuffix);
		WornSuffix = -1; bAny = true;
	}
	if (!bOpen)
	{
		if (SelPrefix >= 0 && LostIds.Contains(SelPrefix)) { SelPrefix = -1; }
		if (SelSuffix >= 0 && LostIds.Contains(SelSuffix)) { SelSuffix = -1; }
	}
	return bAny;
}

// ★ D8. Свои слоты → СВОЯ плашка над головой, тем же путём, что и приход 6068.
//   У чужих персонажей это делает сам сервер рассылкой 6068 (`AsdaWorldSubsystem::OnCharTitle`).
void UAsdaTitleSubsystem::PushSelfTitlesToNameplate()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem*   Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>()   : nullptr;
	UAsdaWorldSubsystem* W   = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!Net || !W || !Net->IsSelfUserNumKnown())
	{
		UE_LOG(LogAsdaTitle, Warning,
			TEXT("[звания] плашка НЕ обновлена: нет мира или свой userNum ещё не известен"));
		return;
	}
	W->ApplyCharTitleSlots((int32)Net->SelfUserNum, WornPrefix, WornSuffix, FameRank);
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] плашка над своей головой обновлена: приставка=%d окончание=%d ранг=%d"),
		WornPrefix, WornSuffix, FameRank);
}

// ★ D8. НОТИС ОБ УТРАТЕ. ★★★ РЕШЕНИЕ ВЛАДЕЛЬЦА 2026-08-30: заведена НАША строка 990110
//   «Ты утратил звание „[ss1]“.» (диапазон порта > 990000, как 990001/990010/990020/990100/990101)
//   с подстановкой ИМЕНИ звания в строковый слот `[ss1]` (закон `loc-string-placeholders`).
//   Имя берём из таблицы `title_list` по id, который несут ОБА пути утраты: 6070 (поле titleId)
//   и диффер маски 6066 (номер бита). Подставляет общая точка `FormatById` через `PostLocFmt` —
//   своей склейки строк не заводим.
//   ★ ОРИГИНАЛЬНАЯ 20502 НЕ ТРОНУТА и служит ЗАПАСНЫМ вариантом: её печатаем, если имени звания
//   в таблице нет (запись id отсутствует либо `Name` пустое) — молчать или показывать пустые
//   кавычки нельзя (`no-scope-cutting`).
//   Канал НЕ МЕНЯЛСЯ (правило `audit-notices-on-new-system`): категория `Progress` = системный
//   журнал 2570 + буфер B.
//   Ключ `asda.Title.LossNotice`: 0 = молча, 1 = наша строка с именем (по умолчанию),
//   2 = только оригинальная 20502 без имени. Отдельного ключа под «старый текст» не заводим.
void UAsdaTitleSubsystem::NoticeTitleLost(const TArray<int32>& LostIds)
{
	UAsdaMessageSubsystem* M = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!M) { return; }

	const int32 Mode = CVarLossNotice.GetValueOnGameThread();   // 1 = с именем, 2 = оригинальная 20502
	const FString Fallback = Loc(TLoc::kNoticeLost);            // 20502, она же запасная

	// Разделяем утраченные на «имя есть» и «имени нет»: первым — именная строка (по одной на
	//   звание, чтобы каждое было названо), вторым — ОДНА общая 20502 без имени.
	TArray<int32> Named;
	int32 Unnamed = 0;
	for (const int32 Id : LostIds)
	{
		const FAsdaTitleDef* D = FindDef(Id);
		if (Mode == 1 && D && !D->Name.IsEmpty()) { Named.Add(Id); } else { ++Unnamed; }
	}

	for (const int32 Id : Named)
	{
		const FAsdaTitleDef* D = FindDef(Id);
		M->PostLocFmt(EAsdaMsgCategory::Progress, TLoc::kNoticeLostNamed, { D->Name }, {});
		++LossNoticeCount;   // счётчик для проверки идемпотентности (`asda.Title.RevokeTest`)
		UE_LOG(LogAsdaTitle, Log, TEXT("[звания] нотис %d в системный чат: утрачено звание %d «%s»"),
			TLoc::kNoticeLostNamed, Id, *D->Name);
	}

	if (Unnamed > 0)
	{
		if (Fallback.IsEmpty())
		{
			UE_LOG(LogAsdaTitle, Warning,
				TEXT("[звания] нотис утраты пропущен для %d званий: строки %d нет в таблице текста"),
				Unnamed, TLoc::kNoticeLost);
			return;
		}
		M->PostText(EAsdaMsgCategory::Progress, Fallback);
		++LossNoticeCount;
		UE_LOG(LogAsdaTitle, Log,
			TEXT("[звания] нотис %d (запасной, без имени) в системный чат: званий без имени в таблице — %d, режим=%d"),
			TLoc::kNoticeLost, Unnamed, Mode);
	}
}

// ★ D8. Тело 6066 из ТЕКУЩЕЙ маски «получено» — для теста снятия без сервера
//   (`asda.Title.RevokeTest`). Раскладка ровно та же, что собирает `asda.Title.Sim owned`.
// ★ Тело маски с ДОПОЛНИТЕЛЬНО взведённым битом — только для самотеста «пришла ТОЛЬКО маска».
//   Собственную маску подсистемы не меняем: бит ставится, тело собирается, бит возвращается на
//   место. Дальше штатный приёмник увидит переход 0→1 и обязан обновить данные БЕЗ «празднования».
TArray<uint8> UAsdaTitleSubsystem::BuildMaskBodyForTest(bool bOwned, int32 Bit)
{
	uint8* M = bOwned ? MaskOwned : MaskFound;
	const bool bHad = MaskGet(M, Bit);
	MaskSet(M, Bit, true);
	TArray<uint8> B = BuildMaskBody(bOwned, EffectiveMaskBits() > kMaskBitsLegacy);
	MaskSet(M, Bit, bHad);
	return B;
}

TArray<uint8> UAsdaTitleSubsystem::BuildMaskBody(bool bOwned, bool bExtBody) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 CharId = Net ? Net->SelectedCharId : 0;
	const int32 Uidx   = Net ? (int32)Net->SelfUserNum : 0;
	TArray<uint8> B;
	B.SetNumZeroed(bOwned ? (bExtBody ? kBody6066Ext : kBody6066Legacy)
	                      : (bExtBody ? kBody6086Ext : kBody6086Legacy));
	// ★ Порядок полей у 6086 ОБРАТНЫЙ к 6066 (сначала charId, потом userIdx) — так в эталоне.
	if (bOwned) { AsdaBytes::WrI16(&B[0], Uidx); AsdaBytes::WrI32(&B[2], CharId); AsdaBytes::WrI32(&B[6], FamePoints); }
	else        { AsdaBytes::WrI32(&B[0], CharId); AsdaBytes::WrI16(&B[4], Uidx); }
	const uint8* Src = bOwned ? MaskOwned : MaskFound;
	const int32  Off = bOwned ? 10 : 6;
	const int32  Cap = bExtBody ? kMaskBits : kMaskBitsLegacy;
	for (int32 i = 0; i < Cap; ++i)
	{
		if (MaskGet(Src, i)) { B[Off + (i >> 3)] |= (uint8)(1u << (i & 7)); }
	}
	return B;
}

// 6086 — полная маска «найдено». Тело 38 б (старый сервер) либо 70 б (новый).
// ★ Порядок полей ОБРАТНЫЙ к 6066 (сначала charId, потом userIdx) — так в эталоне.
// ★★★ ДИФФЕРА ЗДЕСЬ НЕТ И БЫТЬ НЕ ДОЛЖНО (контракт P24, п.2). «Найдено» — это «секрет звания
//   известен», а не владение: сброс бита в этой маске утратой НЕ является, и путать её с 6066
//   нельзя. При снятии сервер, наоборот, СТАВИТ бит «найдено» (то же делает и наш обработчик
//   6070), так что 6086 после снятия приходит с ДОБАВЛЕННЫМ битом. Маска применяется безусловно,
//   ложной «утраты» не порождает — проверено вариантами «полный» и «обратный» в `RevokeTest`
//   (там после 6086 печатается `найдено=1`, а «получено» остаётся снятым).
void UAsdaTitleSubsystem::HandleMaskFound(const TArray<uint8>& B)
{
	if (B.Num() < kBody6086Legacy) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!AsdaSelf::IsSelfCharId(Net, AsdaBytes::RdI32(&B[0]))) { return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	bool bExt = false;
	const int32 N = ParseMaskBody(B, /*смещение маски в теле 6086*/ 6, MaskFound, bExt);
	UE_LOG(LogAsdaTitle, Log,
		TEXT("[звания] 6086: маска «найдено» — %d бит (тело %d б, формат %s)"),
		N, B.Num(), bExt ? TEXT("512 бит") : TEXT("256 бит"));
	if (bOpen) { RebuildOrder(); RefreshList(); LayoutScrollBar(); }
}

// 6070 — результат удаления: снимает бит «получено» и ставит бит «найдено».
void UAsdaTitleSubsystem::HandleDeleted(const TArray<uint8>& B)
{
	if (B.Num() < 13) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!AsdaSelf::IsSelfCharId(Net, AsdaBytes::RdI32(&B[2]))) { return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	const int32 Code = (int32)B[6];
	if (Code != 1)
	{
		// Эталон в этой ветке печатает отладочную строку и НИЧЕГО не меняет.
		UE_LOG(LogAsdaTitle, Warning, TEXT("[звания] 6070: сервер отказал в удалении, код=%d"), Code);
		return;
	}
	const int32 TitleId = AsdaBytes::RdI16(&B[7]);
	const bool bWasOwned = MaskGet(MaskOwned, TitleId);
	MaskSet(MaskOwned, TitleId, false);
	MaskSet(MaskFound, TitleId, true);
	FamePoints = AsdaBytes::RdI32(&B[9]);
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 6070 код=1: звание %d снято, очки=%d (было получено=%d)"),
		TitleId, FamePoints, bWasOwned ? 1 : 0);

	// ★★★ КОНТРАКТ P24: 6070 — ОСНОВНОЙ путь снятия, а не побочный. Живой обработчик эталона
	//   (`@0xE9BE80`) при коде 1 снимает бит и правит очки БЕЗУСЛОВНО, а `*(BYTE*)(*(this+1)+292)`
	//   гейтит ТОЛЬКО перерисовку списка — то есть на «окно закрыто» снятие не завязано и в
	//   оригинале. Раньше порт тут снимал бит, но НЕ звал общую точку: не гасил надетое звание,
	//   не трогал плашку и молчал. Теперь — через `ApplyTitleLoss`, как и диффер 6066.
	//   ★ 6070 несёт titleId ⇒ имя звания в точке вывода ЕСТЬ (см. `NoticeTitleLost`).
	//   `FlagLost`/`TitleStats` здесь НЕ трогаем — это делает общая точка (иначе дедуп съест утрату).
	ApplyTitleLoss({ TitleId }, TEXT("6070"));

	if (bOpen) { RebuildOrder(); RefreshList(); RefreshHeader(); RefreshSelfTitles(); ClearInfoPanel(); LayoutScrollBar(); }
}

// 6085 — «секрет звания открыт»: ставит бит «найдено» и зажигает значок с нотисом 20501.
void UAsdaTitleSubsystem::HandleFound(const TArray<uint8>& B)
{
	if (B.Num() < 8) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	// ★★★ ЭФФЕКТ НА ПЕРСОНАЖЕ — СТРОГО ДО ГЕЙТА «СВОЙ» (эталон `netHandler_OnGuildTitleResult@0xE9A850`,
	//   network.c:31456-31468): восклицательный знак `ex_mark_003.jeb` спавнится над ЛЮБЫМ
	//   персонажем, чей `userIdx` пришёл в пакете, и лишь потом идёт `if (self == charId)`.
	//   ⚠ У 6085 ПОРЯДОК ПОЛЕЙ ОБРАТНЫЙ: `[0..3]` charId i32, `[4..5]` userIdx i16, `[6..7]` titleId.
	SpawnTitleEffect(AsdaBytes::RdI16(&B[4]), /*«найдено»*/ true);

	if (!AsdaSelf::IsSelfCharId(Net, AsdaBytes::RdI32(&B[0]))) { return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	const int32 TitleId = AsdaBytes::RdI16(&B[6]);
	MaskSet(MaskFound, TitleId, true);
	FlagNew.Add(TitleId);
	ShowNoticeIcon(/*иконка «найдено»*/ 1, /*подсказка 20501 «секрет известен»*/ 1);
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 6085: открыт секрет звания %d"), TitleId);
	if (bOpen) { RebuildOrder(); RefreshList(); LayoutScrollBar(); }
}

// 2255 — батч блоков статов уже полученных званий.
void UAsdaTitleSubsystem::HandleStatBatch(const TArray<uint8>& B)
{
	if (B.Num() < 6) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!AsdaSelf::IsSelfCharId(Net, AsdaBytes::RdI32(&B[2]))) { return; }   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	int32 Cnt = 0;
	for (int32 Off = 6; Off + 46 <= B.Num(); Off += 46)
	{
		const int32 TitleId = AsdaBytes::RdI16(&B[Off]);
		// Эталон кладёт блок ТОЛЬКО для полученных званий (`charTestStateFlagA == 1`).
		if (!IsOwned(TitleId)) { continue; }
		TArray<int16> Stats; Stats.SetNumZeroed(18);
		for (int32 i = 0; i < 18; ++i) { Stats[i] = (int16)AsdaBytes::RdI16(&B[Off + 10 + 2 * i]); }
		TitleStats.Add(TitleId, MoveTemp(Stats));
		++Cnt;
	}
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 2255: принято блоков статов %d (записей в теле %d)"),
		Cnt, (B.Num() - 6) / 46);
	if (bOpen && SelectedRow >= 0) { RefreshInfoPanel(); }
}

// 6068 — активные слоты и ранг славы (тот же пакет питает плашку над головой).
void UAsdaTitleSubsystem::HandleSlots(int32 UserIdx, int32 T0, int32 T1, int32 Rank)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !AsdaSelf::IsSelfUserIdx(Net, UserIdx)) { return; }
	WornPrefix = T0; WornSuffix = T1; FameRank = Rank;
	if (!bOpen) { SelPrefix = T0; SelSuffix = T1; }
	UE_LOG(LogAsdaTitle, Log, TEXT("[звания] 6068: свои слоты — приставка=%d окончание=%d, ранг=%d"),
		T0, T1, Rank);
	if (bOpen) { RefreshSelfTitles(); RefreshHeader(); }
}
