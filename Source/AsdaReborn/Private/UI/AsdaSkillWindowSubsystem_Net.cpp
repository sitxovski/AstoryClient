// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Net.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp, дословный перенос): клики по слотам, изучение, приём списков/откатов/сбросов, записи, мост быстрых слотов.
//   Общее для частей — в AsdaSkillWindowSubsystem_Private.h; шапка и реверс — в AsdaSkillWindowSubsystem.cpp.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)

#include "Text/AsdaTextSubsystem.h"

#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

static constexpr int32 kLocConfirmSpend     = 9415;    // подтверждение «потратить очко»

// ★ Аудит №3 (A1, продолжение того же дефекта): подписи кнопок ДВУХкнопочного `TWOBUT_MSGBOX`
//   (2288/2289) — тоже нотисы ОБЩЕГО окна, поэтому берутся из `AsdaLoc::kBtnOk`/`kBtnCancel`,
//   а не объявляются здесь под своими именами (см. шапку `Public/UI/AsdaLocIds.h`).
// ★★★ ИСПРАВЛЕНО ПО БИНАРЮ (жалоба «схуяли при загрузке написало Все очки навыков сброшены»):
//   12855 = «Все навыки сброшены.» — сообщение ПОЛНОГО СБРОСА, показывает ТОЛЬКО обработчик 5462
//   (`skillTreeUiResetSlots_63D0@0xE563D0`) и ТОЛЬКО когда пришедшее число очков > 0.
//   12854 = «Навыки приведены в соответствие с характеристикой [[ss1]]» — это НЕ «навык изучен»:
//   сервер шлёт 5464 из ветки ПЕРЕСЧЁТА навыков под характеристику (GameServer.c:51354, тело 3 б).
static constexpr int32 kLocAllSkillsReset   = 12855;   // «Все навыки сброшены.» (5462, при очках > 0)
static constexpr int32 kLocSkillsRealigned  = 12854;   // «Навыки приведены в соответствие …» (5464)

// ★★ 25.10.2026 [ИСПРАВЛЕНИЕ ПОРТА] (жалоба Player52: «Every skill has been reset in [] characteristic.»): название ветки в 12854
//   подставляется СЛОТОМ [ss1] общей подстановки (`UAsdaTextSubsystem::FormatById`, порт `UI_FormatLocalizedNotice` +
//   `CObject_SetSlotString(ctx, 0, …)`, skill_buff.c:33590-33602). Прежняя `ReplaceInline("[ss1]")` работала по строке, уже
//   прошедшей `LocRaw` → `FormatNotice`, где пустой слот съел маркер, — ветка не попадала в текст НИКОГДА.
//   0 — прежнее поведение (замена в готовой строке).
static ASDA_DEV_CVAR(int32, CVarSkillRealignedSlot,
	TEXT("asda.Skill.RealignedTreeSlot"), 1,
	TEXT("1 — сообщение 12854 «навыки ветки … сброшены» (5464) получает название ветки слотом [ss1], как у оригинала; ")
	TEXT("0 — как прежде (название терялось, на экране «[]»)."),
	ECVF_Default);

// Отказы ответа 5254 (коды 2..8), default = 2200 — тексты из system/error list.bin.
static constexpr int32 kLocFail[9] = { 2200, 2200, 2201, 2202, 2206, 2207, 2208, 111700, 111701 };

bool AsdaCooldownSnapshot5465On();

// ★ Эталон открывает подтверждение (нотис 9415) перед КАЖДЫМ вложением очка. 0 = как в эталоне.
static ASDA_DEV_CVAR(int32, CVarSkillNoConfirm,
	TEXT("asda.Skill.NoConfirm"), 0,
	TEXT("1 = не спрашивать подтверждение перед вложением очка (эталон спрашивает всегда)."),
	ECVF_Default);

// Нотис отказа ответа 5254 код 8 — «недостаточно очков навыка после 4-й профессии» (эталон skill_buff.c:33331).
static constexpr int32 kLocJob4PointsShort = 111701;
// Нотис отказа ответа 5254 код 3 — «Недостаточно очков навыка.» (эталон skill_buff.c:33311).
static constexpr int32 kLocPointsShort = 2202;

// ══ ★★★ T5 (2026-08-11): КАКОЕ СОБЫТИЕ ОКНА НАВЫКОВ КАКОЙ КНОПКЕ СООТВЕТСТВУЕТ — РЕШЕНО ═════════
//   Прежде это была ОТКРЫТАЯ точка: единственной уликой был инвентарь, где обработчики названы
//   `itemSlotWnd_OnLeftClickDispatch` (0xB) и `uiItemSlotOnRightClickDispatch` (0xD), откуда
//   следовал вывод «0xD = ПКМ». **Вывод НЕВЕРЕН, имя `…OnRightClick…` — МИСНЕЙМ** (такой же, как
//   уже разобранный `Target_GetSelectedUserIdx`). Снято по эмиттеру событий, а не по именам.
//
//   ЦЕПОЧКА ДОКАЗАТЕЛЬСТВА (полный декомпил `RE/ida/export/AsdaStory_ida.c`):
//   1) Оконная процедура (`:235380-235520`) раскладывает сообщения Windows по очереди менеджера,
//      передавая ИНДЕКС КНОПКИ вторым аргументом:
//        `WM_LBUTTONDOWN 0x201` → `CWindowMgr_QueueLButton (…, 0, …)`   вид очереди **3**
//        `WM_LBUTTONUP   0x202` → `CWindowMgr_QueueRButton (…, 0, …)`   вид очереди **6**
//        `WM_LBUTTONDBLCLK 0x203`→`CWindowMgr_QueueLButtonUp(…, 0, …)`  вид очереди **4**
//        `WM_RBUTTONDOWN 0x204` / `WM_RBUTTONUP 0x205` — те же функции с индексом **1**,
//        `WM_MBUTTON* 0x207/0x208` — с индексом **2**. (Имена Queue*-функций тоже условные.)
//   2) Разбор очереди (`:1216884-1216990`) пишет БАЙТ СОСТОЯНИЯ кнопки `this + 304 + индекс`:
//        вид 3 (нажатие) → **1** · вид 4 (двойной клик) → **2** · вид 6 (отпускание) → **0**;
//        остальным кнопкам кладётся −1.
//   3) Эмиттеры событий контрола (`CControl_*`, `:1208186-1208330`) читают ровно этот байт:
//        индекс 0 (ЛКМ): состояние 1 → событие **11 (0xB)** `CControl_OnLButtonDblClick@0x12DC190`
//                        состояние 2 → событие **13 (0xD)** `CControl_OnKeyDown@0x12DC240`
//                        состояние 0 → событие **12 (0xC)** `CControl_OnDrag@0x12DC2D0`
//        индекс 1 (ПКМ): 1 → **14 (0xE)** · 2 → **16 (0x10)** · 0 → **15 (0xF)**
//        индекс 2 (СКМ): 1 → **17 (0x11)** · 2 → **19 (0x13)** · 0 → **18 (0x12)**
//      (имена `OnLButtonDblClick` / `OnKeyDown` — тоже миснеймы прежнего прохода IDA; роль задаёт
//       читаемый байт состояния, а не имя).
//   ⇒ ИТОГ ДЛЯ ОКНА НАВЫКОВ (`skillWndOnUICommand@0xE4A8E0`, skill_buff.c:30676-30684):
//        `case 0xB` — ЛКМ НАЖАТА          → `skillBarShowOnlySlot`
//        `case 0xC` — ЛКМ ОТПУЩЕНА (клик) → `skillWndRemoveSlotById` → выбор узла / изучение
//        `case 0xD` — ЛКМ ДВОЙНОЙ КЛИК    → `skillDispatchUseByBaseId` → **ПРИМЕНЕНИЕ НАВЫКА**
//   ⇒ ПРОВЕРКА НА ДРУГИХ ОКНАХ (сходится): в инвентаре `CGameWindow_OnCommand@0xD1B140`
//     `case 0xD` → `CInventory_OnSlotUse` — «двойным кликом использовать предмет», а меню
//     хвастовства висит на `case 0xE` = ПРАВАЯ КНОПКА (это уже было подтверждено отдельно,
//     `showoff_system.md`). Обе точки складываются только при раскладке выше.
//
//   ПОСЛЕДСТВИЕ ДЛЯ ПОРТА: разводить «изучение на ЛКМ» и «применение на ПКМ» НЕ НУЖНО —
//   ранее принятое решение «изучение по левому клику» ЭТАЛОННО и остаётся без изменений
//   (`no-revert-without-asking` не задет). Добавлен недостающий вход: ДВОЙНОЙ левый клик.
//   ⚠ Прежний обходной путь «применяем, если очко вложить нельзя, а навык выучен» СНЯТ: он был
//     заявлен как временный именно до выяснения этой раскладки, и в оригинале одиночный клик
//     навык НЕ применяет никогда.
//
// Эталон `skillWndSelectSlot@0xE53C80`: очки > 0, уровень < maxLevelOrTier, байт «доступен» ≠ 0,
//   затем подтверждение (нотис 9415) и только после него — пакет.
void UAsdaSkillWindowSubsystem::OnSlotClicked(int32 SlotIdx)
{
	if (!Slots.IsValidIndex(SlotIdx) || !Db) { return; }
	const int32 Code = Slots[SlotIdx].SkillCode;
	const FAsdaSkillDef* D = Db->Get(Code);
	if (!D) { return; }

	SelectedSlot = SlotIdx;
	RebuildGrid();

	const FAsdaLearnedSkill* Lg = Learned.Find(Code);
	UE_LOG(LogAsdaSkillWin, Log,
		TEXT("[изучение] шаг 1 — клик по узлу %02d: навык %d «%s», уровень %d/%d, «доступен»=%d, свободных очков=%d"),
		SlotIdx, Code, *D->Name, Lg ? Lg->Level : 0, D->MaxLevel, Lg ? (int32)Lg->bAvailable : -1, SkillPoints);

	// ⚠ ЭТАЛОН ЗДЕСЬ МОЛЧИТ: `skillWndSelectSlot@0xE53C80` при непрохождении любого гейта просто
	//   ничего не делает — ни msgbox, ни строки в чат. Порт раньше показывал нотисы, и после перевода
	//   сообщений окна на однокнопочный msgbox это стало модальным окном на каждый «пустой» клик.
	//   Возвращено поведение эталона; подсказку можно включить обратно ключом (тогда — строкой в чат).
	//
	// ★ ПРИМЕНЕНИЕ НАВЫКА ЗДЕСЬ БОЛЬШЕ НЕ ЖИВЁТ — оно на событии 0xD (двойной клик), см. шапку
	//   функции и `OnSlotDoubleClicked`. Одиночный клик у оригинала навык не применяет никогда.
	const FAsdaLearnedSkill* L = Lg;
	const int32 Level = L ? L->Level : 0;
	if (SkillPoints <= 0)
	{
		RefusalHint(2202);   // «Недостаточно очков навыка.»
		return;
	}
	if (Level >= D->MaxLevel)
	{
		RefusalHint(2201);   // «Навык развит до предела.»
		return;
	}
	if (!L || !L->bAvailable)
	{
		RefusalHint(2206);   // «Тратить очки нельзя: несоответствие требованиям.»
		return;
	}
	// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (`asda.Skill.Job4Pools`): очки есть, но пул навыка пуст — навык 4-й профессии не берёт очки основных
	//   деревьев, основной навык — очки 4-й профессии (лимит 110). Эталон здесь пулов не знает, а отказ сервера — msgbox 5254 код 8
	//   (111701) / код 3 (2202); показываем ТОТ ЖЕ msgbox, что пришёл бы от сервера с P110, и пакет не шлём.
	if (PointsLeftForSkill(Code) <= 0)
	{
		const bool bJob4 = IsJob4TreeSkill(Code);
		const FAsdaSkillPointPools Pools = ComputePointPools();
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[изучение] отказ по пулу: навык %d (%s), свободных %d, основные %d/%d (остаток %d), 4-я профессия вложено %d (остаток %d), уровень %d"),
			Code, bJob4 ? TEXT("дерево 4-й профессии") : TEXT("основное дерево"), Pools.Free, Pools.MainSpent, kMainTreePointCap,
			Pools.MainShown, Pools.Job4Spent, Pools.Job4Shown, Pools.Level);
		MsgBox1(LocRaw(bJob4 ? kLocJob4PointsShort : kLocPointsShort));
		return;
	}

	UE_LOG(LogAsdaSkillWin, Log, TEXT("[изучение] шаг 2 — гейты эталона пройдены (очки > 0, уровень < максимума, «доступен»)"));
	if (CVarSkillNoConfirm.GetValueOnGameThread() != 0)
	{
		RequestLearn(Code);
		return;
	}
	PendingLearnCode = Code;
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const FString Q = FString::Printf(TEXT("%s\n%s"), *LocRaw(kLocConfirmSpend), *D->Name);
	if (WM)
	{
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Q, [this, Code](bool bYes)
		{
			PendingLearnCode = -1;
			if (bYes) { RequestLearn(Code); }
		}, 0.f, AsdaLoc::kBtnOk, AsdaLoc::kBtnCancel, /*очередь: эталон (1, 41) skill_buff.c:33054*/41, 1);
	}
	else
	{
		RequestLearn(Code);
	}
}

// ★★★ СОБЫТИЕ 0xD ОКНА НАВЫКОВ = ДВОЙНОЙ ЛЕВЫЙ КЛИК ПО УЗЛУ → ПРИМЕНЕНИЕ НАВЫКА.
//   Эталон `skillWndOnUICommand@0xE4A8E0` `case 0xDu: skillDispatchUseByBaseId(this, a2, a3)`
//   (skill_buff.c:30682). Сам `skillDispatchUseByBaseId@0xE57CB0` (skill_buff.c:34017) НИКАКИХ
//   собственных гейтов не имеет: он переводит базовый id в индекс (`skillBaseIdToIndex`), достаёт
//   код навыка из соответствующего списка и зовёт `combatHandleSkillUse` (боевой список) либо
//   `skillUseOnTargetDispatch` (список «по цели») — обе точки в порту закрыты одним
//   `UAsdaSkillUseSubsystem::UseFromSkillWindow` (он же `DispatchByKind`), где и живут все гейты.
//   ⚠ Отдельного «не выучен» здесь не пишем: гейт эталона (нотис 1018) уже стоит внутри
//     `CheckGates` — дублировать сообщение значило бы показать его дважды.
void UAsdaSkillWindowSubsystem::OnSlotDoubleClicked(int32 SlotIdx)
{
	if (!Slots.IsValidIndex(SlotIdx) || !Db) { return; }
	const int32 Code = Slots[SlotIdx].SkillCode;
	if (Code < 0) { return; }
	const FAsdaSkillDef* D = Db->Get(Code);
	if (!D) { return; }
	UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
	if (!Use)
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навыки] двойной клик по узлу %02d: подсистемы применения нет"), SlotIdx);
		return;
	}
	UE_LOG(LogAsdaSkillWin, Log,
		TEXT("[навыки] ДВОЙНОЙ клик по узлу %02d: навык %d «%s», уровень %d ⇒ ПРИМЕНЯЕМ ")
		TEXT("(эталонное событие 0xD → skillDispatchUseByBaseId@0xE57CB0)"),
		SlotIdx, Code, *D->Name, GetLearnedLevel(Code));
	Use->UseFromSkillWindow(Code);
}

bool UAsdaSkillWindowSubsystem::RequestLearn(int32 SkillCode)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net)
	{
		UE_LOG(LogAsdaSkillWin, Warning, TEXT("[изучение] нет сети — 5253 не отправлен (навык %d)"), SkillCode);
		return false;
	}
	// ⚠ В теле 5253 НЕТ ни charId, ни userNum: сервер адресует по userIdx соединения. Тело ровно 4 байта
	//   (i16 skillCode, u8 flag = 1, u8 pointCount = 1) — любой лишний байт ломает MD5-проверку
	//   `sub_523F00(arg4, 4u, …)` на сервере.
	const bool bSent = Net->SendSkillLearn5253(SkillCode, /*PointCount*/ 1, /*FlagByte*/ 1);
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[изучение] шаг 3 — пакет 5253 (тело 4 б: код %d, флаг 1, очков 1): %s"),
		SkillCode, bSent ? TEXT("отправлен") : TEXT("ОТПРАВИТЬ НЕ УДАЛОСЬ"));
	return bSent;
}

// ══════════════════════════════ СЕТЬ: ОЧЕРЕДЬ И ПРИМЕНЕНИЕ ═════════════════════════════════════
// ★ Аудит №3 (B10): замок и `Add` — внутри общей `TAsdaNetQueue`.
void UAsdaSkillWindowSubsystem::OnNetListFull(const TArray<uint8>& Body)
{ PendingFull.Push(Body); }
void UAsdaSkillWindowSubsystem::OnNetListUpdate(const TArray<uint8>& Body)
{ PendingUpdate.Push(Body); }
void UAsdaSkillWindowSubsystem::OnNetListCompact(const TArray<uint8>& Body)
{ PendingCompact.Push(Body); }
void UAsdaSkillWindowSubsystem::OnNetUpgradeResult(const TArray<uint8>& Body)
{ PendingResult.Push(Body); }
void UAsdaSkillWindowSubsystem::OnNetCooldowns(const TArray<uint8>& Body)
{
	// ★ Ревизия 1.1 (ключ `asda.Cooldown.ArrivalOrder`, шапка у ключа): снимок — в момент приёма, как у эталона и хотбара.
	if (CVarCooldownArrivalOrder.GetValueOnGameThread() == 0) { PendingCool.Push(Body); return; }
	const uint64 Seq = NextCooldownArrivalSeq();
	if (AsdaCooldownSnapshot5465On())
	{
		CooldownSnapshotSeq = Seq;
		CooldownReleasedSeq.Reset();   // 5274 до снимка больше не нужны: всё, что пришло раньше снимка, снимок и отменяет
	}
	ApplyCooldowns(Body);
	bCooldownsRebuild = true;          // сетка — в `DrainPending` (из колбэка UI не трогаем)
}

// Взвод 5271 с номером прихода `ArrivalSeq` устарел, если после него пришёл снимок 5465 (эталон: снимок стёр бы запись) или 5274 этого
//   навыка (сервер уже отпустил откат). Номер 0 — неизвестен (прежние пути) — не устаревает.
bool UAsdaSkillWindowSubsystem::IsCooldownArmSuperseded(int32 SkillCode, uint64 ArrivalSeq) const
{
	if (ArrivalSeq == 0 || CVarCooldownArrivalOrder.GetValueOnGameThread() == 0) { return false; }
	if (CooldownSnapshotSeq > ArrivalSeq) { return true; }
	const uint64* Rel = CooldownReleasedSeq.Find(SkillCode);
	return Rel && *Rel > ArrivalSeq;
}
// ★★★ 6057 `skillWnd_ResetElementSlots@0xE55910` — ТОЛЬКО очистка внутренних списков навыков.
//   НИКАКОГО сообщения на экран эталон здесь НЕ показывает (вся функция — четыре вызова очистки
//   контейнеров, ни одного `CItem_BuildTooltipText`). Сервер шлёт 6057 перед досылкой свежего списка,
//   в том числе при обычных действиях с навыками, — поэтому наше «Все навыки сброшены.» и вылезало
//   на ровном месте при попытке применить навык. Убрано.
void UAsdaSkillWindowSubsystem::OnNetTreeReset()
{ FScopeLock Lock(&PendingFlagsLock); bPendingListClear = true; }   // ★ B10: защёлка, не очередь

// ★★★ 5462 `skillTreeUiResetSlots_63D0@0xE563D0` — ПОЛНЫЙ сброс навыков.
//   ⚠ Первая строка функции — `if (a2 <= 0) return 0;`: при неположительном числе очков эталон
//   не делает НИЧЕГО (ни очистки, ни сообщения). Гейта у нас не было.
void UAsdaSkillWindowSubsystem::OnNetSlotsReset(int32 NewPoints)
{
	if (NewPoints <= 0)
	{
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5462 с очками %d (<= 0) — эталон такой пакет игнорирует"), NewPoints);
		return;
	}
	// ★ Аудит №3 (B10): защёлка — под своим замком, число очков — в общей очереди. Приём идёт на
	//   игровом потоке, поэтому «флаг и очки в разных кадрах» невозможно: оба пуша завершаются
	//   ДО ближайшего `DrainPending`.
	{
		FScopeLock Lock(&PendingFlagsLock);
		bPendingFullReset = true;
	}
	PendingPoints.Push(int32(NewPoints));
}
// ★★★ 5464 `skillUiOnLearnSkill_6590@0xE56590` — НЕ «навык изучен».
//   Сервер шлёт этот пакет из ветки ПЕРЕСЧЁТА навыков под характеристику (GameServer.c:51354,
//   тело 3 б: `@0 i16 очки`, `@2 u8 grade`), и клиент показывает 12854
//   «Навыки приведены в соответствие с характеристикой [[ss1]]», где `[ss1]` = название дерева
//   `9400 + 3*(полоса−1) + ((grade % 3 + 2) % 3)`; при grade вне 1..9 слот пустой,
//   при grade 10/11/12 подставляется 111800.
//   ⚠ Гейт эталона: `if (a3 <= 0) return 0;` — при неположительном числе очков не делается ничего.
void UAsdaSkillWindowSubsystem::OnNetSkillsRealigned(int32 NewPoints, int32 Grade)
{
	if (NewPoints <= 0)
	{
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5464 с очками %d (<= 0) — эталон такой пакет игнорирует"), NewPoints);
		return;
	}
	PendingPoints.Push(int32(NewPoints));   // ★ Аудит №3 (B10): замок — в очереди
	FString TreeName;
	if (Grade >= 1 && Grade <= 9)
	{
		const int32 Band = GetSelfBand();
		int32 K = Grade % 3 - 1;
		if (K < 0) { K = 2; }
		if (Band >= 1) { TreeName = LocRaw(9400 + 3 * (Band - 1) + K); }
	}
	else if (Grade == 10 || Grade == 11 || Grade == 12) { TreeName = LocRaw(kLocJob4TipTitle); }
	// В таблице заглушка записана как «[[ss1]]» — внешние скобки часть текста, внутренний маркер — слот 0.
	FString Msg;
	const UAsdaTextSubsystem* Txt = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	if (CVarSkillRealignedSlot.GetValueOnGameThread() != 0 && Txt)
	{
		Msg = Txt->FormatById(kLocSkillsRealigned, { TreeName }, {});   // ★ 25.10.2026: слот [ss1] = название ветки
	}
	if (Msg.IsEmpty())
	{
		Msg = LocRaw(kLocSkillsRealigned);   // прежняя замена (ключ 0 или нет подсистемы текста)
		Msg.ReplaceInline(TEXT("[ss1]"), *TreeName, ESearchCase::CaseSensitive);
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5464: ветка grade=%d → «%s» (полоса %d)"), Grade, *TreeName, GetSelfBand());
	MsgBox1(Msg);
}
// ★ Свободные очки: блок char-select 1017 (+39) и все три «очковых» ответа сервера — единый вход.
void UAsdaSkillWindowSubsystem::OnNetSelfSkillPoints(int32 Points)
{ PendingPoints.Push(int32(Points)); }   // ★ Аудит №3 (B10): замок — в очереди

// ★ 18.10.2026: смена профессии — защёлка, применяется в `DrainPending` (тот же Tick, что и очки из этой же ветки).
void UAsdaSkillWindowSubsystem::OnNetSelfJobChanged(int32 /*OldClassId*/, int32 /*Job*/, int32 /*ClassId*/, int32 /*SubJob*/)
{
	// ★ 19.10.2026: из завершения 5052 эталон дерево не сбрасывает (`charApplyFullStatusPacket` зовёт только
	//   `skillUiUpdatePointText`, entity_char.c:46339) — защёлку не ставим; очки придут своим `OnSelfSkillPoints`.
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->bSelfJobChangeFrom5052) { return; }
	FScopeLock Lock(&PendingFlagsLock);
	bPendingJobChanged = true;
}

void UAsdaSkillWindowSubsystem::DrainPending()
{
	TArray<TArray<uint8>> F, U, C, R, Cd;
	TArray<int32> P;
	bool bReset = false, bClear = false, bJob = false;
	// ★ Аудит №3 (B10): шесть прежних строк «под замком забрать всё разом и очистить» — общая точка.
	PendingFull.Drain(F);
	PendingUpdate.Drain(U);
	PendingCompact.Drain(C);
	PendingResult.Drain(R);
	PendingCool.Drain(Cd);
	PendingPoints.Drain(P);
	{
		FScopeLock Lock(&PendingFlagsLock);
		bClear = bPendingListClear; bPendingListClear = false;
		bReset = bPendingFullReset; bPendingFullReset = false;
		bJob   = bPendingJobChanged; bPendingJobChanged = false;
	}
	const bool bCdRebuild = bCooldownsRebuild;   // ★ ревизия 1.1: 5465 уже применён при приёме — нужна только перерисовка
	bCooldownsRebuild = false;
	if (!F.Num() && !U.Num() && !C.Num() && !R.Num() && !Cd.Num() && !P.Num() && !bReset && !bClear && !bJob && !bCdRebuild) { return; }
	// ★ 26.10.2026: модель выученного меняется ниже (6057/5462/5251/6056/5252/5254) — панель быстрого доступа перекрасит слоты навыков.
	if (bClear || bReset || F.Num() || U.Num() || C.Num() || R.Num()) { ++GAsdaSkillLearnedRev; }

	// 6057 — тихая очистка списков (эталон ничего не показывает).
	if (bClear)
	{
		Learned.Reset();
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 6057: списки навыков очищены (сообщение эталон не показывает)"));
	}
	// 5462 — настоящий полный сброс: очистка + однокнопочный msgbox 12855 «Все навыки сброшены.»
	if (bReset)
	{
		Learned.Reset();
		Notice(kLocAllSkillsReset);
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5462: все навыки сброшены сервером"));
	}
	for (const TArray<uint8>& B : C) { ApplyCompactList(B); }
	for (const TArray<uint8>& B : F) { ApplyListRecords(B, /*bReplace*/ false); }
	for (const TArray<uint8>& B : U) { ApplyListRecords(B, /*bReplace*/ true); }
	for (const TArray<uint8>& B : R) { ApplyUpgradeResult(B); }
	for (const TArray<uint8>& B : Cd){ ApplyCooldowns(B); }
	for (int32 Pts : P) { SkillPoints = Pts; }
	// ★ 18.10.2026 — смена профессии 5042: `skillWnd_msgProc(12005, 0)` ставит дерево 0 (`m_nCurTreeIdx = a3`) и прокрутку 0
	//   (`m_nSelectedOptionIdx = 0`) НЕЗАВИСИМО от того, открыто ли окно; подписи вкладок (`uiPopulateUpgradeOptionList`) и
	//   сетка перерисовываются ниже — `RebuildAll` читает класс заново (`GetSelfJob` = `SelfClassId`).
	if (bJob)
	{
		CurTree = 0;
		ScrollRow = 0;
		SelectedSlot = -1;
		UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] смена профессии: класс %d, подкласс %d — вкладка 0, окно %s"),
			GetSelfJob(), GetSelfSubJob(), IsWindowOpen() ? TEXT("перерисовано") : TEXT("закрыто"));
	}

	if (IsWindowOpen()) { RebuildAll(); }
}

// Записи 34 б: @0 i16 код · @2 u8 уровень · @3 u8 «можно вкачивать» · @10 i16 требуемый уровень.
//   ★ Аудит №3 (A7): little-endian читаем ЕДИНОЙ точкой `AsdaBytes::RdI16` — открытое выражение
//     `(int16)(B[o] | (B[o+1] << 8))` и есть то место, где заводится off-by-one. Границы уже
//     проверены условием цикла (`Off + 34 <= B.Num()`), поэтому берём быструю `Rd*`, а не `Safe*`.
void UAsdaSkillWindowSubsystem::ApplyListRecords(const TArray<uint8>& B, bool bReplace)
{
	int32 N = 0;
	for (int32 Off = 0; Off + 34 <= B.Num(); Off += 34)
	{
		FAsdaLearnedSkill E;
		E.SkillCode    = AsdaBytes::RdI16(B.GetData() + Off);
		E.Level        = (int32)B[Off + 2];
		E.bAvailable   = B[Off + 3] != 0;
		E.ReqCharLevel = AsdaBytes::RdI16(B.GetData() + Off + 10);
		E.CooldownMs   = AsdaBytes::RdI32(B.GetData() + Off + 4);   // ★ 25.10.2026: +4 i32 — откат от сервера (CUser +49352)
		if (E.SkillCode < 0) { continue; }
		if (bReplace) { Learned.Remove(E.SkillCode); }
		Learned.Add(E.SkillCode, E);
		++N;
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] список (%s): %d записей, всего в модели %d"),
		bReplace ? TEXT("обновление") : TEXT("полный"), N, Learned.Num());
}

// ★★★ 5252 — НАВЫКИ, КОТОРЫЕ СЕРВЕР ТОЛЬКО ЧТО ОТКРЫЛ (эталон `Net_RecvLearnedSkillList@0xE56090`, network.c:29030).
//   Кто шлёт (GameServer, `RE/ida/gs_migrate/out/GameServer.c`): `sub_48A8C0@0x48A8C0` — базовые навыки нового класса из
//   `SkillTable_Info/ChangeJobAddSkillTable.txt` СРАЗУ за 5042 (`recvRewardQuestProcess`, :37955-37962) и за 5052
//   (`recvPickupQuestItemToMonsterOrObject`, :38182-38188) при смене профессии; `sub_47E9B0@0x47E9B0` — навыки, открытые вкачкой
//   (931 → 932), ДО ответа 5254. Каждой новой строке сервер заводит запись (`sub_475650`) — вкачивать её уже можно.
//   Эталон: `memcpy(learnedList, тело, 0x69)` — РОВНО 35 записей по 3 б (@0 i16 код, @2 u8 уровень), код < 0 — пропуск; запись
//   дерева строится заново: код, уровень, «можно вкачивать» `v6 = 1`, байт +10 = `skillRecord + 58` (у нас `MpCostBase`, см.
//   `FAsdaLearnedSkill::ReqCharLevel`); нет записи `.sif` — `return` без дальнейших строк.
//   ★ ИСПРАВЛЕНИЕ 22.10.2026 (жалоба игроков «после профессии лучника очки не вкладываются до перезапуска»): порт флаг
//     «можно вкачивать» тут НЕ ставил («сохраняем прежний» — у новой записи это false), и все навыки новой профессии, а
//     также открытые вкачкой, отбивались гейтом `skillWndSelectSlot` (нет подсветки, клик молчит) — до входа, где их
//     приносил 5251 с флагом сервера. Плюс читались ВСЕ 3-байтовые куски тела: сервер шлёт 5252 с паддингом конверта
//     (`sub_5643E0`, `a8 = 1`: 0…19 б хвоста; в журналах игроков тела 120/124 б), и хвост разбирался как навыки.
void UAsdaSkillWindowSubsystem::ApplyCompactList(const TArray<uint8>& B)
{
	constexpr int32 kEntries = 35;   // 0x69 / 3 — `memcpy(learnedList, Src + 9, 0x69u)`
	int32 N = 0;
	for (int32 i = 0; i < kEntries && 3 * i + 3 <= B.Num(); ++i)
	{
		const int32 Off  = 3 * i;
		const int32 Code = AsdaBytes::RdI16(B.GetData() + Off);   // ★ A7: @0 i16, границы дал цикл
		if (Code < 0) { continue; }
		const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
		if (Db && !D)
		{
			// [ОРИГИНАЛ] `if (!skillRecord) return skillRecord;` — остаток списка не разбирается. Перерисовку окна (её эталон на этом
			//   выходе пропускает) у нас делает `DrainPending` для всего пакета — [ИСПРАВЛЕНИЕ], принятые строки видны сразу.
			UE_LOG(LogAsdaSkillWin, Warning, TEXT("[навыки] 5252: навыка %d нет в skill_info — разбор остановлен на записи %d (как эталон)"), Code, i);
			break;
		}
		FAsdaLearnedSkill& E = Learned.FindOrAdd(Code);
		E.SkillCode  = Code;
		E.Level      = (int32)B[Off + 2];
		E.bAvailable = true;                          // `v6 = 1`
		if (D) { E.ReqCharLevel = D->MpCostBase; }    // байт +10 записи = `skillRecord + 58`
		if (D) { E.CooldownMs = D->CooldownMs; }      // ★ 25.10.2026: +4 записи = `skillRecord + 47` (`skillCooldown`)
		++N;
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 5252: открыто навыков %d (тело %d б, разбор — первые 105 б), всего в модели %d"),
		N, B.Num(), Learned.Num());
}

// Ответ 5254: @0 код, @1 i16 очки, @3 i16 код навыка, @5 u8 новый уровень.
void UAsdaSkillWindowSubsystem::ApplyUpgradeResult(const TArray<uint8>& B)
{
	if (B.Num() < 7) { return; }
	const int32 Code  = (int32)B[0];
	// ★ A7: @1 и @3 — i16 подряд; длину тела (≥ 7 б) проверила строка выше.
	const int32 Pts   = AsdaBytes::RdI16(B.GetData() + 1);
	const int32 Skill = AsdaBytes::RdI16(B.GetData() + 3);
	const int32 Level = (int32)B[5];

	if (Code == 1)
	{
		SkillPoints = Pts;
		FAsdaLearnedSkill& E = Learned.FindOrAdd(Skill);
		E.SkillCode  = Skill;
		E.Level      = Level;
		E.bAvailable = true;
		// ★ 25.10.2026: эталон `skillHandleUpgradeResponse@0xE55420` правит только уровень СУЩЕСТВУЮЩЕЙ записи (+4 не трогает);
		//   запись, заведённую здесь портом (её не было), заполняем как 5252 — `.sif +47`, иначе откат после применения был бы 0.
		if (E.CooldownMs <= 0 && Db) { if (const FAsdaSkillDef* D = Db->Get(Skill)) { E.CooldownMs = D->CooldownMs; } }
		UE_LOG(LogAsdaSkillWin, Log,
			TEXT("[изучение] шаг 4 — ответ 5254 УСПЕХ: навык %d → уровень %d, свободных очков стало %d ")
			TEXT("(перерисовка сетки и счётчиков — шаг 5, в DrainPending)"), Skill, Level, Pts);
		return;
	}
	const int32 Loc = (Code >= 0 && Code <= 8) ? kLocFail[Code] : 2200;
	Notice(Loc);
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[изучение] шаг 4 — ответ 5254 ОТКАЗ: код=%d → нотис %d «%s»"),
		Code, Loc, *LocRaw(Loc));
}

// 5465: N × 6 б — @0 i32 skillId, @4 i16 остаток секунд. Терминатор skillId == −1.
void UAsdaSkillWindowSubsystem::ApplyCooldowns(const TArray<uint8>& B)
{
	// ★ [ОРИГИНАЛ] 25.10.2026: 5465 — полный снимок. `Recv_SkillCoolTime@0xE4B180` первой строкой зовёт
	//   `skillHotbarRefreshLocked@0xE4A210`, а тот очищает весь реестр CSkillWnd+0xE0 (0x934DD2 → 0xE5A2B0 — освобождение
	//   буфера вектора) и только потом добавляет записи пакета. Прежде порт дописывал поверх — на портале оставались
	//   откаты, которых у сервера уже нет. Ключ `asda.Cooldown.Snapshot5465` (0 — прежнее дописывание).
	if (AsdaCooldownSnapshot5465On()) { Cooldowns.Reset(); }
	int32 N = 0;
	for (int32 Off = 0; Off + 6 <= B.Num(); Off += 6)
	{
		// ★ A7: @0 i32 skillId, @4 i16 остаток; границы дал цикл (`Off + 6 <= B.Num()`).
		const int32 Id = AsdaBytes::RdI32(B.GetData() + Off);
		if (Id < 0) { break; }
		const int32 Sec = AsdaBytes::RdI16(B.GetData() + Off + 4);
		PushCooldownFromServer(Id, Sec);
		++N;
	}
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] кулдаунов принято: %d"), N);
}

void UAsdaSkillWindowSubsystem::PushCooldownFromServer(int32 SkillCode, int32 RemainSec, int32 TotalSecOverride)
{
	if (RemainSec <= 0) { Cooldowns.Remove(SkillCode); return; }
	const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
	const int32 TotalMs = (TotalSecOverride > 0) ? TotalSecOverride * 1000 : (D && D->CooldownMs > 0 ? D->CooldownMs : RemainSec * 1000);
	PushCooldownMs(SkillCode, RemainSec * 1000, TotalMs);
}

// ★ 25.10.2026: общий взвод в мс (5465 — через секунды выше; применение навыка — мс как есть, эталон `skillStartCooldown@0xE579E0`
//   кладёт в запись `(float)a3` мс без округления).
void UAsdaSkillWindowSubsystem::PushCooldownMs(int32 SkillCode, int32 RemainMs, int32 TotalMs)
{
	if (RemainMs <= 0) { Cooldowns.Remove(SkillCode); return; }
	FSkillCooldown& C = Cooldowns.FindOrAdd(SkillCode);
	C.TotalSec  = (float)FMath::Max(TotalMs, RemainMs) / 1000.f;
	C.RemainSec = (float)RemainMs / 1000.f;
	C.EndS      = AsdaCooldownNowS(GetGameInstance()) + (double)RemainMs / 1000.0;   // ★ момент готовности (реальные часы)
	// Дублируем в панель быстрого доступа: у неё свой реестр (в оригинале он тоже жил в CSkillWnd).
	if (UAsdaHotbarSubsystem* Hb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->PushSkillCooldown(SkillCode, RemainMs);
	}
}

// ══════════════════════════════ МОДЕЛЬ ДЛЯ ДРУГИХ СИСТЕМ ═══════════════════════════════════════
int32 UAsdaSkillWindowSubsystem::GetLearnedLevel(int32 SkillCode) const
{
	const FAsdaLearnedSkill* L = Learned.Find(SkillCode);
	return L ? L->Level : 0;
}

bool UAsdaSkillWindowSubsystem::CanDragSkill(int32 SkillCode) const
{
	// Эталон `skillCheckResetEligible@0xE55DA0`: подтип 3/4 не перетаскивается, уровень должен быть > 0.
	const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
	if (!D) { return false; }
	if (D->SubType == 3 || D->SubType == 4) { return false; }
	return GetLearnedLevel(SkillCode) > 0;
}

float UAsdaSkillWindowSubsystem::GetCooldownRemainSec(int32 SkillCode) const
{
	const FSkillCooldown* C = Cooldowns.Find(SkillCode);
	return C ? FMath::Max(0.f, CooldownRemainLive(*C)) : 0.f;
}

// ★ 25.10.2026: живой остаток (см. объявление в заголовке). При ключе `asda.Cooldown.RealClock 1` — от момента готовности,
//   иначе — накопленный тиком `RemainSec` (прежнее поведение).
float UAsdaSkillWindowSubsystem::CooldownRemainLive(const FSkillCooldown& C) const
{
	if (AsdaCooldownRealClockOn() && C.EndS > 0.0)
	{
		return (float)FMath::Max(0.0, C.EndS - AsdaCooldownNowS(GetGameInstance()));
	}
	return C.RemainSec;
}

// ★★★ МОСТ К ПАНЕЛИ БЫСТРОГО ДОСТУПА — ПОЛНЫЙ (было: только иконка/вид/кулдаун/подсказка).
//   До этого захода `UseCombatSkill` / `UseTargetSkill` / `CastAuto` / `HasSoulmate` оставались
//   пустыми ⇒ нажатие по навыку в панели было ТИХОЙ ПУСТЫШКОЙ (разбор — `hotbar_inventory_drag.md`
//   §14). Тела применения живут в `UAsdaSkillUseSubsystem` (порт `combatHandleSkillUse@0xE4B960`,
//   `skillUseOnTargetDispatch@0xE4E9F0`, `skillCastActivate@0xE4D900`); окно навыков их только
//   ПРОБРАСЫВАЕТ — как и в эталоне, где `Hotbar_SlotActivate` зовёт эти функции через `g_pSkillWnd`.
FAsdaHotbarSkillBridge UAsdaSkillWindowSubsystem::MakeHotbarBridge()
{
	FAsdaHotbarSkillBridge Br;
	// ★ 26.10.2026 (жалоба «после свитка сброса навыки остаются на панели»): «недоступен» = список выученного УЖЕ пришёл
	//   (`Learned` не пуст — до 5251 и сразу после сброса сессии слоты не гасим) и уровень навыка 0 или записи нет.
	//   Навыки Хранителя (> 1201) в книгу не входят — их не гасим никогда (как гейт уровня `UAsdaSkillUseSubsystem::CheckGates`).
	Br.GetSkillIconName = [this](int32 Code, bool& bOutUnavailable) -> FString
	{
		const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
		if (!D || D->IconName.IsEmpty()) { bOutUnavailable = true; return FString(); }
		bOutUnavailable = Learned.Num() > 0 && Code <= 1201 && GetLearnedLevel(Code) <= 0;
		return D->IconName;
	};
	Br.GetTargetKind = [this](int32 Code) -> int32
	{
		const FAsdaSkillDef* D = Db ? Db->Get(Code) : nullptr;
		if (!D) { return 0; }
		// ★★★ ИСПРАВЛЕНО: эталон `Hotbar_SlotActivate` ветвится по `skillinfo_GetByteFieldAt42`
		//   @0xE52BF0, а это `skill_info.sif` **+0x2A** (`Kind`), НЕ `targetSlotType` (+0x34):
		//     2 → `combatHandleSkillUse`, 1 → `skillUseOnTargetDispatch`, 0 → не применяется.
		//   Раньше здесь стояло `TargetSlotType == 6 ? 1 : 2` — другое поле, из-за чего боевые
		//   навыки уходили не в ту ветку моста. Разбор: `skill_use_system.md` §2.2 и §8.2.
		return D->Kind;
	};
	Br.GetCooldown = [this](int32 Code) -> FAsdaHotbarCooldown
	{
		FAsdaHotbarCooldown Out;
		if (const FSkillCooldown* C = Cooldowns.Find(Code))
		{
			Out.State = 1;
			const float RemainSec = CooldownRemainLive(*C);   // ★ 25.10.2026: живой остаток (реальные часы)
			Out.RemainMs = RemainSec * 1000.f;
			// ★★★ СТАРТОВЫЙ КАДР ШТОРКИ = доля УЖЕ ПРОШЕДШЕГО отката. Эталон `Recv_SkillCoolTime@0xE4B180`
			//   (`network.c:28783-28789`): `coolPercent = (cooldownMs − remainSec·1000)/cooldownMs·100`,
			//   `coolValue = coolPercent · 1.1799999` — и именно `coolValue` уходит первым аргументом
			//   `uiControl_ShowWithItem(ctrl, coolValue, 117, 118, 1000·remainSec, 1)`.
			//   Без него откаты, восстановленные пакетом 5465 на ЛОГИНЕ (у нас это 784 = 245 с и
			//   791/803 = 61 с), рисовались с кадра 0, то есть «как будто их только что повесили»
			//   на навыки, которых игрок в этой сессии не касался.
			if (C->TotalSec > 0.f)
			{
				const float Elapsed = FMath::Clamp((C->TotalSec - RemainSec) / C->TotalSec, 0.f, 1.f);
				Out.IconFrame = FMath::Clamp(FMath::FloorToInt(Elapsed * 100.f * 1.1799999f), 0, 117);
			}
		}
		return Out;
	};
	// ★ Боевой навык (Kind == 2) — порт `combatHandleSkillUse@0xE4B960`.
	Br.UseCombatSkill = [this](int32 Code, int32 SlotParam)
	{
		if (UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			Use->UseCombatSkill(Code, SlotParam);
		}
	};
	// ★ Навык по цели (Kind == 1) — порт `skillUseOnTargetDispatch@0xE4E9F0`.
	Br.UseTargetSkill = [this](int32 Code)
	{
		if (UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			Use->UseTargetSkill(Code);
		}
	};
	// ★ Слот Type = 4 (навык соулмейта) — порт `skillCastActivate@0xE4D900` → пакет 5231.
	Br.CastAuto = [this](int32 Code)
	{
		if (UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr)
		{
			Use->CastSoulmateSkill(Use->FindBestSlotForLevel(Code));
		}
	};
	Br.FindBestSlotForLevel = [this](int32 Code) -> int32
	{
		const UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		return Use ? Use->FindBestSlotForLevel(Code) : Code;
	};
	// ★ Эталон `Hotbar_SlotActivate` case 4: сперва проверка «есть ли соулмейт» (иначе нотис 8162).
	Br.HasSoulmate = [this]() -> bool
	{
		const UAsdaSkillUseSubsystem* Use = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillUseSubsystem>() : nullptr;
		return Use && Use->HasSoulmate();
	};
	// ★★★ ИСПРАВЛЕНО 2026-08-11. Прежний мост искал код в СЕТКЕ ОТКРЫТОГО ОКНА
	//   (`Slots[i].SkillCode`), а она заполняется только в `RebuildGrid()` при открытом окне и
	//   обнуляется в `ResetWidgets()`. Из-за этого подсказка навыка на панели быстрого доступа не
	//   появлялась НИКОГДА при закрытой книге навыков, а при открытой жила ≤ 1 кадра — её гасил
	//   покадровый `TickPointer`/`HideOwnTooltip` окна (владелец показа был `SkillWin`, а хотбар
	//   помечал себе владельца `Item`). Теперь мост только СТРОИТ ТЕКСТ (как эталонный
	//   `skillShowTooltipForId@0xE54120` — по коду навыка, без сетки), а показывает его хотбар от
	//   своего имени.
	Br.BuildTooltip = [this](int32 Code, bool bSoulmate, FString& OutTitle, FString& OutBody) -> bool
	{
		OutTitle.Reset(); OutBody.Reset();
		if (bSoulmate) { return BuildSoulmateSkillTooltip(Code, OutTitle, OutBody); }
		if (!Db || !Db->Get(Code)) { return false; }
		const FAsdaLearnedSkill* L = Learned.Find(Code);
		const int32 Level = L ? L->Level : 0;
		const bool bAvail = L ? L->bAvailable : false;
		BuildSkillTooltip(Code, Level, bAvail, /*bAllowNextPreview*/ bAvail, OutTitle, OutBody);
		return !OutTitle.IsEmpty() || !OutBody.IsEmpty();
	};
	return Br;
}
