// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem_Use.cpp — часть `AsdaSkillUseSubsystem.cpp`: разбор по виду навыка, соулмейт-навыки, оружие/MP, CheckGates, очередь действия.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaSkillUseSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaSkillUseSubsystemInternal.h`. Шапка системы и ссылки на эталон — в `AsdaSkillUseSubsystem.cpp` и заголовке класса.

#include "UI/AsdaSkillUseSubsystem.h"
#include "World/AsdaSkillCastLock.h"   // R5-fix8: SupersedeQueuedMove
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaNoticeColors.h"   // ★ 08.11.2026: золото плашки отказа навыка (−11514)
#include "World/AsdaPossessGate.h"   // ★ гейт действий под GM-«скрыться» (AsdaGmHide)

#include "UI/AsdaCastBarSubsystem.h"
#include "AsdaBytes.h"                  // ★ 31.10.2026 (баг 7): разбор шапки 5232
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaSoulmateSkillSubsystem.h"   // ★ 31.10.2026 (баг 7): вид навыка соулмейта rec[50] для гейта 5231
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "AsdaNetSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайлы
#include "World/AsdaPlayerController.h"
// ★ Сетка проходимости карты — наш аналог `GroundTarget_PickOnMesh_thunk` для точки отброса.
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"          // ★ 08.11.2026: рывок 537/597 — земля на пути до цели (13219)
#include "Npc/AsdaMobActor.h"
#include "World/AsdaWorldSubsystem.h"
// ★ Источник максимума полосы применения (COMBO0) — таблица клипов навыков DT_SkillClips.
#include "Skill/AsdaSkillFxSubsystem.h"
#include "Skill/AsdaSkillClipRow.h"
#include "World/AsdaCombatTrace.h"   // ★ 18.10.2026: журнал боя — нажатие навыка, гейты, подход, заявка, ответ, отказ

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaPvpRelation.h"          // ★ 26.10.2026 (bowfx): отношение к цели-персонажу (PvP, дуэль, команда, свои)
#include "AsdaSkillUseSubsystemInternal.h"   // ★ R2: общие помощники и объявления частей AsdaSkillUseSubsystem*.cpp (после включений пролога)

namespace
{
	// Нотисы гейтов клиента (расшифровка `system\error list.bin`, отчёт §3).
	constexpr int32 kLocFishingRod   = 110707;  // «You cannot attack to use fishing rod.»
	constexpr int32 kLocAutoShovel   = 111204;  // «You cannot attack with the auto-shovel.»
	constexpr int32 kLocOnVehicle    = 2038;    // «You cannot attack while riding a vehicle.»
	constexpr int32 kLocCooldown     = 2203;    // «You cannot use this skill yet.»
	constexpr int32 kLocNoMp         = 1018;    // «Cannot apply the skill due to insufficient MP.»
	constexpr int32 kLocBadWeapon    = 1020;    // «You cannot use this skill with the currently equipped weapon.»
	constexpr int32 kLocNoSoulmate   = 8162;    // отказ слота Type=4 без соулмейта
	constexpr int32 kLocUnderControl = 1155;    // П11: персонаж под обездвиживающим состоянием
	constexpr int32 kLocRushTooClose = 13218;   // ★ 08.11.2026: «The distance is too close to use this skill.» (рывок 537/597)
	constexpr int32 kLocRushObstacle = 13219;   // ★ 08.11.2026: «Cannot use skills since there is an obstacle.» (рывок 537/597)
	constexpr float kRushMinDistU    = 600.f;   // ★ 08.11.2026: литерал `invItemTargetInRange(…, 600.0)` (skill_buff.c:31619)

	// `combo time.bin`: значения в файле — КАДРЫ при 30 fps; загрузчик эталона
	//   (`CComboManager_LoadComboTime@0x9BC5F0`) умножает их на 1000/30.
	// ★ Аудит №3 (Д6): своего `kFrameToMs` здесь больше нет — одна константа
	//   `AAsdaPlayerCharacter::kFrameToMs` на обоих потребителей (было `kFrameToMs` / `kFrameMs`).

	// Состояния действия, из которых эталон НЕ даёт начать новый навык
	//   (`skill_buff.c:31633`): 14, 27, 13, 15, 25, 6, 7, 8, 9.
	FORCEINLINE bool StateBlocksCast(int32 S)
	{
		return S == 14 || S == 27 || S == 13 || S == 15 || S == 25 || S == 6 || S == 7 || S == 8 || S == 9;
	}
}

// ★ 06.11.2026 [ПОРТ] Отказ клиентских гейтов навыка — короткая строка и в выпускном журнале (журнал боя `ASDA_CTRACE` в Shipping
//   пуст, а часть гейтов молчит). Баг «после дуэли навыки не работают»: без неё по журналу игрока не отличить «не нажимал» от
//   «гейт отказал». Вместе с состоянием, активным навыком, контролем и PvP — этого хватает, чтобы назвать гейт.
static void NoteGateRefused(const UAsdaSkillUseSubsystem* SU, int32 SkillCode, int32 CharState, int32 ActiveSkill, const TCHAR* Path)
{
	const UGameInstance* GI = SU ? SU->GetGameInstance() : nullptr;
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навык отказ] %d (%s): гейт клиента, состояние %d, активный %d, контроль %d, PvP %d"), SkillCode, Path,
		CharState, ActiveSkill, (Fx && Fx->HasControlState(Fx->SelfUserIdx())) ? 1 : 0, (SU && SU->IsSelfPvpModeFlag()) ? 1 : 0);
}

bool AsdaMountSelfListGateOn();   // ★ 07.11.2026: ключ `asda.Mount.SelfListGate` (определение — AsdaWorldSubsystem_Peers.cpp)
// ★★★ 08.11.2026 [ОРИГИНАЛ] (пачка 18 баг 8: «Charge применяется на любой дистанции, минимальной нет»; `asda.Skill.RushRangeGate`).
//   Эталон `skillUseOnTargetDispatch@0xE4E9F0` (skill_buff.c:31605-31624) — ТОЛЬКО для 537 «Charge» и 597 «Mad Charge», после гейта
//   оружия (1020), до отправки заявки, три проверки подряд:
//     1) `listHasAnyOfSlotTypes` (состояния 0/1/2/3/14/9/4/8) → нотис 1155 — в порту давно есть (`asda.Skill.TargetControlGate`);
//     2) `targetPathLineOfSightCheck@0xA88CE0` (entity_char.c:30175): 101 точка t = 0…1 шагом 0.01 по прямой «я → цель»
//        (`skillBuildBezierCurve` по двум точкам), точка с x < 0 или z < 0 либо без земли (`GroundTarget_PickOnMesh`) → нотис 13219;
//        цели нет (`g_nInputControlState` 11/−1) — проверка пройдена;
//     3) `invItemTargetInRange(…, 600.0)` (item_inventory.c:3658): ТРЁХМЕРНОЕ расстояние до цели (`mathVec3LengthSq` берёт sqrt —
//        имя-мисней) < 600 → нотис 13218 «The distance is too close to use this skill.».
//   Нотисы — `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`, как 1155. Сервер этого не проверяет (GameServer: для 537/597 только
//   состояние `sub_477F60`, GameServer.c:46746/47006) — проверка жила лишь в клиенте, порт её не перенёс: Charge шёл вплотную.
//   Других вызовов `invItemTargetInRange` в эталоне нет, поля «минимальной дальности» в skill_info.sif нет — минимальная дистанция
//   есть только у 537/597. 1 — как эталон; 0 — прежний порт (проверок 13219/13218 нет).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillRushRangeGate, TEXT("asda.Skill.RushRangeGate"), 1,
	TEXT("[ОРИГИНАЛ] Рывок 537/597: 1 — до заявки проверка пути до цели (нотис 13219) и минимальной дистанции 600 (нотис 13218), как эталон; ")
	TEXT("0 — прежний порт (рывок с любой дистанции)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarAsdaSkillTargetControlGate, TEXT("asda.Skill.TargetControlGate"), 1,
	TEXT("[ОРИГИНАЛ 05.11] Путь «по цели» (Kind 1) под контролем 0/1/3/14/8/4: 1 = навыки 639/615/736/984 разрешены, прочие молча не ")
	TEXT("применяются, 537/597 под 0/1/2/3/14/9/4/8 — нотис 1155 (как эталон); 0 = прежний порт (нотис 1155 всем навыкам пути)."),
	ECVF_Default);

// ══════════════════════════════ ТОЧКИ ВХОДА ════════════════════════════════════════════════════

const FAsdaSkillDef* UAsdaSkillUseSubsystem::GetDef(int32 SkillCode) const
{
	// ★ Аудит №3 (A12): напрямую у подсистемы базы, а не через окно навыков (окно могло не открываться).
	const UAsdaSkillDatabase* Db = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillDatabase>() : nullptr;
	return Db ? Db->Get(SkillCode) : nullptr;
}

AAsdaPlayerController* UAsdaSkillUseSubsystem::GetLocalPC() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()) : nullptr;
}

AAsdaPlayerCharacter* UAsdaSkillUseSubsystem::GetLocalChar() const
{
	AAsdaPlayerController* PC = GetLocalPC();
	return PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
}

int32 UAsdaSkillUseSubsystem::SelfUserIdx() const
{
	// ★ Волна 1 (14.09.2026, В9 / исправление E2): общая точка `AsdaSelf::GetUserIdxOr`. До пакета 4003
	//   своего номера нет — INDEX_NONE, а не 0 (0 — законный номер ЧУЖОГО игрока, закон `charid-vs-usernum`).
	//   Ключ отката ядра `asda.Self.StrictUnknownNum` на выборку номера не действует (`AsdaSelf.h`).
	return AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE);
}

bool UAsdaSkillUseSubsystem::DispatchByKind(int32 SkillCode, int32 SlotParam)
{
	const FAsdaSkillDef* D = GetDef(SkillCode);
	if (!D)
	{
		// Эталон `skillinfo_GetByteFieldAt42` вернул 0 (записи нет) — тоже НИЧЕГО не отправляет.
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] %d: записи в базе нет — эталон в этом случае молчит"), SkillCode);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (слот %d) НЕ применён: записи в базе навыков нет"), SkillCode, SlotParam);
		return false;
	}
	// ★★★ ВЕТВЛЕНИЕ ПО `.sif +0x2A` (Kind), а НЕ по TargetSlotType (+0x34) — отчёт §2.2.
	if (D->Kind == 2) { return UseCombatSkill(SkillCode, SlotParam); }
	if (D->Kind == 1) { return UseTargetSkill(SkillCode); }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: Kind (+0x2A) = %d — эталон таким путём навык не применяет"),
		SkillCode, *D->Name, D->Kind);
	ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d «%s» НЕ применён: Kind=%d (не 1 и не 2) — эталон таким путём не применяет"), SkillCode, *D->Name, D->Kind);
	return false;
}

// Порт `combatHandleSkillUse@0xE4B960` (combat_pvp.c:6772). У боевой ветки те же гейты плюс
//   PvP-специфика (своя фракция, дуэль). Отличие от ветки «по цели» — она НЕ требует цели.
bool UAsdaSkillUseSubsystem::UseCombatSkill(int32 SkillCode, int32 SlotParam)
{
	const FAsdaSkillDef* D = GetDef(SkillCode);
	if (!D)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (боевой путь) НЕ применён: записи в базе нет"), SkillCode);
		return false;
	}
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("НАЖАТИЕ навыка %d «%s» (боевой путь, слот %d): castType=%d вид «%s» res=%d подтип=%d дальность %d м, состояние %d, активный %d"),
		SkillCode, *D->Name, SlotParam, D->CastType, TargetSlotTypeName(D->TargetSlotType), D->ResourceType, D->SubType,
		D->RangeMeters, CharState, ActiveSkill);
	if (!CheckGates(SkillCode, *D, /*bTargetPath=*/false)) { NoteGateRefused(this, SkillCode, CharState, ActiveSkill, TEXT("боевой путь")); return false; }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] боевой путь: %d «%s» (слот книги %d) → отложенное действие 102"),
		SkillCode, *D->Name, SlotParam);
	QueueSkillAction102(SkillCode);
	return true;
}

// Порт `skillUseOnTargetDispatch@0xE4E9F0` (skill_buff.c:31284).
bool UAsdaSkillUseSubsystem::UseTargetSkill(int32 SkillCode)
{
	const FAsdaSkillDef* D = GetDef(SkillCode);
	if (!D)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d (путь «по цели») НЕ применён: записи в базе нет"), SkillCode);
		return false;
	}
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("НАЖАТИЕ навыка %d «%s» (путь «по цели»): castType=%d вид «%s» res=%d подтип=%d дальность %d м, состояние %d, активный %d"),
		SkillCode, *D->Name, D->CastType, TargetSlotTypeName(D->TargetSlotType), D->ResourceType, D->SubType,
		D->RangeMeters, CharState, ActiveSkill);
	if (!CheckGates(SkillCode, *D, /*bTargetPath=*/true)) { NoteGateRefused(this, SkillCode, CharState, ActiveSkill, TEXT("путь «по цели»")); return false; }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] путь «по цели»: %d «%s» → отложенное действие 102"),
		SkillCode, *D->Name);
	QueueSkillAction102(SkillCode);
	return true;
}

// Порт `skillDispatchUseByBaseId@0xE57CB0` (skill_buff.c:34017) — применение из окна навыков.
//   Эталон ищет навык сперва в списке боевых, затем в списке «по цели», и зовёт ту же пару функций.
bool UAsdaSkillUseSubsystem::UseFromSkillWindow(int32 SkillCode)
{
	return DispatchByKind(SkillCode, -1);
}

// ★★★ Хвост эталонного `hudQuickSlot_TryUseSkill@0xEF4600` (ui_core.c:92707-92711): у ряда
//   Хранителя души НЕТ собственного пути применения — он идёт в тот же диспетчер по `Kind`.
//   Серверная таблица (живая, `GameServer\SkillTable_Info\SoulGuarderSkillTable.txt`, 54 строки)
//   подтверждает: 1228..1254 имеют `castType = 3`, `targetSlotType = 6` («на моба ИЛИ игрока»),
//   `range` 2..15 м, `resourceType` 1 или 3 — то есть это ОБЫЧНЫЕ АТАКУЮЩИЕ навыки, а вовсе не
//   само-касты. Клиентский `skill_info.sif` даёт ровно те же значения (kind 2, subType 2).
//   Записи 1201..1227 — это АУРЫ Хранителя (subType 5 тогл, slot 1), рядами не применяются.
bool UAsdaSkillUseSubsystem::UseGuardianSkill(int32 SkillCode)
{
	return DispatchByKind(SkillCode, 0);
}

void UAsdaSkillUseSubsystem::ClearActiveSkill(int32 SkillCode, const TCHAR* Why)
{
	if (ActiveSkill != SkillCode) { return; }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] активный навык %d снят: %s"), SkillCode, Why);
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("активный навык %d снят: %s"), SkillCode, Why);
	ActiveSkill = -1;
	ActiveSkillSentS = 0.0;
	// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] КОРЕНЬ БАГА 7. Сюда приходит ответ 6159 (успех И отказ) на заявку 6158, которая ушла через
	//   общий отправитель и поставила состояние 25. Прежде снимался только активный навык: при ОТКАЗЕ 6159 состояние 25
	//   оставалось навсегда, а сторож «ответа нет» его уже не видел (он смотрит только на заявку при активном навыке) —
	//   гейт 12 молча резал все последующие навыки. Порт отказа 5257 уже делает `CharState = 0` (ApplyNetEvent), а отказ
	//   6159 у эталона — та же `Recv_SkillCast@0xF4E370`. При успехе 6159 следом `OnGuardianCastAccepted` ставит 24 и
	//   полосу — состояние 0 между ними ничего не ломает.
	if (AsdaSkillLatchGuardOn() && (CharState == 25 || CharState == 14))
	{
		ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: %s — состояние %d → 0 (заявка закрыта ответом)"), SkillCode, Why, CharState);
		CharState = 0;
		CastShowEndS = 0.0;
		if (UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
		{
			Bar->End(Why);
		}
	}
}

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] ПРЕДОХРАНИТЕЛЬ ЗАЩЁЛОК (баг 7, шапка у объявления в .h). Зовётся из Tick после сторожа «ответа
//   нет» и выхода из 24. Два случая:
//   (1) состояние 24 ждёт погасания визуала дольше kShowOverrunCapS после срока клипа — снимаем диспетчер (показ не трогаем);
//   (2) блокирующее состояние (StateBlocksCast), которое НЕ охраняет сторож kNoAnswerGuardS (тот работает только при
//       активном навыке и неотвеченной заявке), держится дольше срока: 14 при активном навыке — время каста `.sif +0x2D`
//       (по истечении этого срока сервер `CheckSkillCastingTime@0x47B480` шлёт 8207 на BattleServer, и 5271 приходит ПОЗЖЕ —
//       после ответа 8208 `recvBattleCastingTimeEnd@0x471840`) + kNoAnswerGuardS запаса; прочее — kNoAnswerGuardS.
// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7, ревизия п.5): молчаливые гейты (12 и бит хода) у эталона не пишут ничего, а журнал игрока
//   за пределом 4 МБ хранит только Warning — следующий случай «навыки не уходят» был бы снова невидим. Одно Warning на серию:
//   больше kSilentRefuseWarnCount отказов подряд за kSilentRefuseWindowS (серию прерывает пройденный гейт).
void UAsdaSkillUseSubsystem::NoteSilentRefuse(int32 SkillCode, const TCHAR* Gate)
{
	if (!AsdaSkillLatchGuardOn()) { return; }
	const double Now = FPlatformTime::Seconds();
	if (SilentRefuseRun == 0 || Now - SilentRefuseFirstS > kSilentRefuseWindowS)
	{
		SilentRefuseRun = 0; SilentRefuseFirstS = Now; bSilentRefuseWarned = false;
	}
	++SilentRefuseRun;
	if (SilentRefuseRun <= kSilentRefuseWarnCount || bSilentRefuseWarned) { return; }
	bSilentRefuseWarned = true;
	++SilentRefuseWarns;
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] %d нажатий подряд за %.1f с отклонены молча (%s): навык %d, состояние действия %d, ")
		TEXT("активный навык %d (заявка %.1f с назад), ход без ответа %d, отсчёт предохранителя %.1f с"),
		SilentRefuseRun, Now - SilentRefuseFirstS, Gate, SkillCode, CharState, ActiveSkill,
		ActiveSkillSentS > 0.0 ? Now - ActiveSkillSentS : 0.0, (Net && Net->IsMoveRequestInFlight()) ? 1 : 0,
		LatchSinceS > 0.0 ? Now - LatchSinceS : 0.0);
}

void UAsdaSkillUseSubsystem::TickLatchGuard()
{
	const double Now = FPlatformTime::Seconds();
	UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr;
	if (!(ActiveSkill >= 0 && CharState == 24)) { ShowOverrunSinceS = 0.0; }
	if (ShowOverrunSinceS > 0.0 && Now - ShowOverrunSinceS > kShowOverrunCapS)
	{
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] навык %d: визуал показа не гаснет %.1f с после срока клипа — состояние 24 → 0, ")
			TEXT("активный навык снят (предохранитель защёлок)"), ActiveSkill, Now - ShowOverrunSinceS);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ПРЕДОХРАНИТЕЛЬ — визуал не гаснет %.1f с после срока клипа, состояние 24 → 0"), ActiveSkill, Now - ShowOverrunSinceS);
		ActiveSkill = -1; ActiveSkillSentS = 0.0; CastShowEndS = 0.0; CharState = 0;
		ShowOverrunSinceS = 0.0; LatchSinceS = 0.0;
		++LatchGuardFired;
		if (Bar) { Bar->End(TEXT("предохранитель: визуал не гаснет")); }
		return;
	}
	const bool bWatched = ActiveSkill >= 0 && ActiveSkillSentS > 0.0;   // эту заявку ведёт сторож «ответа нет»
	if (!StateBlocksCast(CharState) || bWatched) { LatchSinceS = 0.0; return; }
	if (LatchSinceS <= 0.0 || LatchState != CharState || LatchSkill != ActiveSkill)
	{
		LatchSinceS = Now; LatchState = CharState; LatchSkill = ActiveSkill;
		return;
	}
	double LimitS = kNoAnswerGuardS;
	if (CharState == 14 && ActiveSkill >= 0)
	{
		if (const FAsdaSkillDef* D = GetDef(ActiveSkill)) { LimitS += (double)FMath::Max(0, D->CastTimeMs) / 1000.0; }
	}
	if (Now - LatchSinceS <= LimitS) { return; }
	UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] состояние действия %d держится %.1f с (активный навык %d, предел %.1f с) без ответа, ")
		TEXT("который его снимает — сброшено в 0 (предохранитель защёлок; иначе все навыки молча отклонялись бы до перезапуска)"),
		CharState, Now - LatchSinceS, ActiveSkill, LimitS);
	ASDA_CTRACE(EAsdaCT::Refuse, TEXT("ПРЕДОХРАНИТЕЛЬ: состояние %d держится %.1f с (навык %d, предел %.1f с) — сброшено в 0"),
		CharState, Now - LatchSinceS, ActiveSkill, LimitS);
	ActiveSkill = -1; ActiveSkillSentS = 0.0; CastShowEndS = 0.0; CharState = 0;
	LatchSinceS = 0.0; ShowOverrunSinceS = 0.0;
	SoulmatePendingSkill = -1;   // ревизия п.2: заявка 5231, если это была она, закрыта сроком
	++LatchGuardFired;
	if (Bar) { Bar->End(TEXT("предохранитель защёлок")); }
}

// ★★★ 6159/6160 = 5257/5270 ДЛЯ ХРАНИТЕЛЯ. Тело — копия ветки успеха 5257 (и 5270 при bIdle),
//   потому что у эталона это ОДНИ И ТЕ ЖЕ поля персонажа: слот показа `effectSlots[100].field_4`,
//   состояние действия 24/14, полоса `uiUpdateActiveSkillSlot` и загрузчик клипа (массив 0
//   таблицы `CharSkillEffect_InitDispatchTable@0xA89510`).
//   СИМПТОМ, который это чинит (живой тест 2026-08-10): «атака хранителем работает, но не
//   отображается полоса и анимация игрока» — ряд хранителя уходил в ОБЩИЙ диспетчер, а ответ
//   приходил опкодом 6159, которого ветка `ApplyNetEvent` не знает; `OnSkillCast6159` только
//   СНИМАЛА активный навык (`ClearActiveSkill`) и ставила мотион компаньону. Ни `Bar->Begin`,
//   ни `StartSkillVisual` не звал никто.
//   ★ Клип у скиллов 1228..1254 в данных ЕСТЬ: `DT_SkillClips` — `ch4_{m|f}_06_co-enervate_01`
//   (anim_162/161), COMBO0 = 1766.7 мс (54 строки, id 1228..1254 × 2 пола).
void UAsdaSkillUseSubsystem::OnGuardianCastAccepted(int32 CasterUserIdx, int32 SkillCode, bool bIdle)
{
	// ★★★ ЛОКАЛЬНОЕ — ТОЛЬКО СВОЁ. Полоса применения, `ActiveSkill`/`CharState` и сторож выхода из
	//   состояния — это НАШ UI и НАШ диспетчер заявок; у эталона они живут в окне навыков и в полях
	//   локального игрока (`uiUpdateActiveSkillSlot@0xEDC480` рисует полосу по `g_pLocalPlayer`).
	//   ПОКАЗ (клип + эффекты) — общий, ставится любому кастеру.
	const bool bMine = (CasterUserIdx == SelfUserIdx());
	const FAsdaSkillCastTiming T = ResolveTiming(SkillCode);
	if (bMine)
	{
		UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr;
		ActiveSkill = SkillCode;
		ActiveSkillSentS = 0.0;                    // ответ пришёл — сторож «сервер не ответил» снят
		CharState = bIdle ? 14 : 24;
		// Срок выхода из состояния 24 — как у 5257 (тик состояния 24: `obj[13] >= obj[11]`).
		//   У 6160 (состояние 14) выход даёт последующий 6159/6161, срок не заводим.
		if (!bIdle)
		{
			const float ShowMs = FMath::Max(T.FullMs, T.Combo0Ms);
			CastShowEndS = FPlatformTime::Seconds() + AsdaCastShowSeconds(GetGameInstance(), ShowMs);
		}
		if (Bar) { Bar->Begin(SkillCode, T.Combo0Ms, T.FullMs, CharState); }
	}
	StartSkillVisual(CasterUserIdx, SkillCode);
	UE_LOG(LogAsdaSkillUse, Log,
		TEXT("[навыки] хранитель: %s принят (кастер uidx=%d%s) — состояние %d, полоса max %.0f мс ")
		TEXT("(клип %.0f мс), показ запущен"),
		bIdle ? TEXT("6160") : TEXT("6159"), CasterUserIdx, bMine ? TEXT(" мой") : TEXT(" СОСЕД"),
		bMine ? (bIdle ? 14 : 24) : 0, T.Combo0Ms, T.FullMs);
	ASDA_CTRACE(bMine ? EAsdaCT::Recv : EAsdaCT::Peer, TEXT("%d хранитель: навык %d ПРИНЯТ, кастер userIdx=%d%s — состояние %d, COMBO0 %.0f мс, клип %.0f мс, показ запущен"),
		bIdle ? 6160 : 6159, SkillCode, CasterUserIdx, bMine ? TEXT(" (я)") : TEXT(" (сосед)"),
		bMine ? (bIdle ? 14 : 24) : 0, T.Combo0Ms, T.FullMs);
}

// Порт `skillCastActivate@0xE4D900` → `netSkill_SendSoulmateUse@0xA07540` (5231).
//   ⚠ Эталон после отправки ставит состояние **25** (`CharFx_AutoSabzil_State(..., 25, 0, 0)`),
//     а полоса применения в состоянии 25 НЕ показывается (отчёт §1.5) — воспроизводим как есть.
bool UAsdaSkillUseSubsystem::CastSoulmateSkill(int32 SoulmateSkillId)
{
	// ★★★ УБРАН 200-мс ТРОТТЛИНГ (2026-08-11). У эталона `Hotbar_SlotActivate` анти-спам стоит ТОЛЬКО
	//   в `case 3` (слот НАВЫКА, `ui_core.c:67441`), а соулмейт — это `case 4` (`:67459`), и он
	//   `g_dwLastSkillDispatchTime` не читает ВООБЩЕ. Скан бинаря: ссылок на `0x2C30500` ровно две,
	//   обе внутри `Hotbar_SlotActivate` (чтение `0x4D12D2`, запись `0x4D12E8`).
	if (!HasSoulmate())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык соулмейта %d НЕ применён: соулмейта нет (нотис %d)"), SoulmateSkillId, kLocNoSoulmate);
		PostSkillNotice(kLocNoSoulmate);
		return false;
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	// ★★★ 31.10.2026 [ОРИГИНАЛ] (баг 7, ревизия п.2): гейты состояния `skillCastActivate@0xE4D900` (skill_buff.c:30960-31045).
	//   Прежде порт слал 5231 без них и перетирал 14/24 текущего каста на 25. Эталон, всё МОЛЧА:
	//     if ((g_nGameFlags & 2) == 0 && state ∉ {14, 27, 13, 15, 25, 6, 7, 8, 9})
	//       switch (rec[50])                                  // `.ssi +0x32` (`FAsdaSoulmateskillRow::Stat1Type`)
	//         case 0/1/2: нет эффекта 113 && state != 24 → отправка;  case 3: нет эффекта 113 → отправка;
	//     после отправки — состояние 25, кроме навыков 33 и 36.
	//   Выбор цели ведёт `UAsdaSoulmateSkillSubsystem` (лоток), здесь его не дублируем.
	// ★ 07.11.2026 [ОРИГИНАЛ] (`asda.Soul.TrayFlow`): откат — `skillCastActivate` первым делом (`classStatusLookupLocked ≤ 1` → нотис 2203).
	//   Прежняя ссылка на лоток была неверна: ни лоток, ни панель этот гейт не держали.
	if (AsdaSoulTrayFlowOn())
	{
		const UAsdaSoulmateSkillSubsystem* SC = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr;
		if (SC && SC->GetCooldownRemainMs(SoulmateSkillId) > 0)
		{
			PostSkillNotice(kLocCooldown);
			return false;
		}
	}
	if (AsdaSkillLatchGuardOn())
	{
		const UAsdaSoulmateSkillSubsystem* SS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr;
		const FAsdaSoulmateskillRow* Row = SS ? SS->GetSkill(SoulmateSkillId) : nullptr;
		const int32 Kind = Row ? Row->Stat1Type : 0;   // rec[50]
		const TCHAR* Why = nullptr;
		if (Net->IsMoveRequestInFlight()) { Why = TEXT("бит хода 2 (4006 без ответа 4007)"); }
		else if (StateBlocksCast(CharState)) { Why = TEXT("состояние действия запрещает старт"); }
		else
		{
			// `listContainsByTypeId(список состояний, 113, 0)` — тот же разбор, что у эффекта 132 (`HasAreaEffect132`).
			const UAsdaEffectsSubsystem* Eff = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
			if (Eff) { for (const FAsdaEffectEntry& Ef : Eff->GetSelfList()) { if (Ef.Id == 113 && Ef.Type == 0) { Why = TEXT("висит эффект 113"); break; } } }
		}
		if (!Why && Kind != 3 && CharState == 24) { Why = TEXT("идёт показ навыка (состояние 24)"); }
		if (Why)
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] соулмейт: навык %d (вид %d) не применён — %s, состояние %d (эталон молчит)"),
				SoulmateSkillId, Kind, Why, CharState);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык соулмейта %d: ГЕЙТ — %s (состояние %d), молча"), SoulmateSkillId, Why, CharState);
			if (Net->IsMoveRequestInFlight() || StateBlocksCast(CharState)) { NoteSilentRefuse(SoulmateSkillId, Why); }
			return false;
		}
	}

	// ★★★ ИСПРАВЛЕНО 2026-08-10: в 5231 едет **СВОЙ** сессионный userNum, а не индекс цели.
	//   `netSkill_SendSoulmateUse@0xA07540` (network.c:6611-6622):
	//       localPlayer = CharManager_GetLocalPlayer(g_CharManager);
	//       payload[57] = payload[58] = Target_GetSelectedUserIdx(localPlayer);
	//   Имя `Target_GetSelectedUserIdx@0x9254E5` — МИСНЕЙ: это thunk на
	//   `Char_GetHoveredUserIdx@0x966490` = `return *((u16*)this + 95094)`, геттер СОБСТВЕННОГО
	//   userNum ПЕРЕДАННОГО персонажа (доказательство: combat_pvp.c:2849-2850 сравнивает результат
	//   для локального игрока и для цели — «это я?»). Аргумент здесь — локальный игрок ⇒ свой номер.
	//   Прежний порт слал индекс ВЫБРАННОЙ ЦЕЛИ: по мобу это renderId, и заявка адресовалась
	//   несуществующему игроку.
	const bool bOk = Net->SendSoulmateSkillUse5231(SoulmateSkillId, SelfUserIdx());
	if (bOk) { ++Sent5231; }
	ASDA_CTRACE(EAsdaCT::Send, TEXT("5231 навык соулмейта %d (свой userNum=%d, визуал %d) — %s; состояние → 25"),
		SoulmateSkillId, SelfUserIdx(), RemapSoulmateSkillId(SoulmateSkillId), bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	// Состояние 25 — полосу НЕ зажигаем (эталон её в 25 не показывает).
	// ★ 31.10.2026 (баг 7, ревизия п.2): 25 — только если заявка ушла и не для 33/36 (эталон: `if (skillId != 33 && skillId != 36)`);
	//   ключ 0 — прежнее (25 всегда).
	if (!AsdaSkillLatchGuardOn())
	{
		CharState = 25;
	}
	else if (bOk)
	{
		SoulmatePendingSkill = SoulmateSkillId;
		if (SoulmateSkillId != 33 && SoulmateSkillId != 36) { CharState = 25; }
	}
	// ★★★★★ ЧЕТВЁРТЫЙ КАНАЛ ЭФФЕКТОВ — НАВЫКИ СОУЛМЕЙТА (П3, подключено 2026-08-11).
	//   Прежде порт не запускал показ соулмейт-навыка ВООБЩЕ. Эталон запускает его ЛОКАЛЬНО, прямо в
	//   `skillCastActivate@0xE4D900`, ещё до ответа сервера (в отличие от обычных навыков, у которых
	//   показ ставит 5257):
	//       skillActionId = sub_ACEFC0(skillId);                      // ремап, VA 0xE4DC6C и ещё 4 места
	//       packPathDispatchEntryHandler(&g_pSkillInfoTable, char, skillActionId, …);   // массив k0
	//       charFindEffectSlotById(char, …, skillActionId, aimDir);
	//   Обработчики этих id регистрирует ОТДЕЛЬНАЯ функция `charFxInitSoulSkillVtbl@0xACEE20`
	//   (`effects_particles.c:28368`), которую `CSkillMgr_LoadSkillData@0xA79F30` зовёт строкой сразу
	//   после `CharSkillEffect_InitDispatchTable` (`skill_buff.c:17254-17255`), — те же массивы k0/k5,
	//   24 регистрации, коды 1287…1300. Данные сняты в `skill_dispatch_table.tsv` /
	//   `skill_clip_table.tsv` (`Tools/asda_dump_skill_dispatch.py`, константа `FN_SOUL_DISPATCH`).
	const int32 VisualId = RemapSoulmateSkillId(SoulmateSkillId);
	if (VisualId > 0)
	{
		StartSkillVisual(SelfUserIdx(), VisualId);
	}
	else
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] соулмейт: умение %d — ремап sub_ACEFC0 дал 0 ")
			TEXT("(эталон в этом случае показа тоже не заводит)"), SoulmateSkillId);
	}
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] соулмейт: навык %d, СВОЙ userNum=%d (эталон кладёт его в ")
		TEXT("оба слова 57/58) → 5231 %s; состояние 25 (полоса применения в 25 не показывается — эталон)"),
		SoulmateSkillId, SelfUserIdx(), bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

bool UAsdaSkillUseSubsystem::HasSoulmate() const
{
	return SelfSoulIdx > 0;
}

// ★★★ РЕМАП «индекс умения соулмейта → код визуального канала» — `sub_ACEFC0@0xACEFC0`
//   (`effects_particles.c:28399`), дословно:
//       if (a1 < 0x1E)          return a1;      // 0..29 — свой номер, отдельного канала нет
//       if (a1 >= 0x64)         return 0;       // 100 и выше — показа нет
//       if (a1 + 1256 >= 0x54C) return 0;       // за пределами 1355 слотов таблицы
//       return a1 + 1256;
//   Зовут ровно семь мест (скан бинаря по переходнику `0x921DF4`): пять в
//   `skillCastActivate@0xE4D900` (`0xE4DC6C`, `0xE4DE99`, `0xE4DFD3`, `0xE4E34F`, `0xE4E489`),
//   по одному в `net_handleSoulmateSkillState@0xF505E0` (`0xF50787`) и
//   `Net_RecvSoulmateSkillApply@0xF52B80` (`0xF52CCB`).
//   Реально занятые обработчиками коды — 1287…1300 (индексы умений 31…44; 1292 и 1297 пусты),
//   что совпадает с записями 30..44 файла `SoulmateSkill.ssi` (шаг 112 б).
int32 UAsdaSkillUseSubsystem::RemapSoulmateSkillId(int32 SoulmateSkillId)
{
	if (SoulmateSkillId < 0)   { return 0; }
	if (SoulmateSkillId < 30)  { return SoulmateSkillId; }
	if (SoulmateSkillId >= 100) { return 0; }
	const int32 Mapped = SoulmateSkillId + 1256;
	return (Mapped >= 0x54C) ? 0 : Mapped;
}

int32 UAsdaSkillUseSubsystem::FindBestSlotForLevel(int32 SkillCode) const
{
	// `SoulmateSkill.ssi` (45 × 112 б): каждая ступень умения — ОТДЕЛЬНАЯ запись (30..44),
	//   поля «уровень внутри записи» там нет. Подменять нечего — отдаём вход как есть.
	return SkillCode;
}

// ══════════════════════════════ ГЕЙТЫ ══════════════════════════════════════════════════════════

bool UAsdaSkillUseSubsystem::IsFishingRodEquipped() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || Net->SelfWeaponItemId <= 0) { return false; }
	UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* R = IT ? IT->GetItem(Net->SelfWeaponItemId) : nullptr;
	// Удочка = ItemType 10 / ItemSubType 1 (`itemCheckUpgradeCompat@0xFC9CB0`, см. шапку
	//   `AsdaFishingToolSubsystem.h`: «удочка 10/1»).
	return R && R->ItemType == 10 && R->ItemSubType == 1;
}

// ★ Гейт 2 «надет авто-бур» (нотис 111204) — ЗАКРЫТ 2026-08-10. Эталон
//   `playerHasEquippedItemType10Sub2@0xFE9A40` (entity_char.c:64946-64959): запись НАДЕТОГО оружия
//   (`LocalPlayer->gap_7FE[49]` — тот же слот, что читает проверка удочки) с
//   `itemType == 10 && itemSubType == 2` (имена полей структуры эталона `itemSubType`/`itemVariant`
//   сдвинуты на один — это та же пара полей, по которой удочка = 10/1; вызов стоит гейтом 2 в
//   `combatHandleSkillUse` combat_pvp.c:7016-7020, между удочкой и записью навыка).
bool UAsdaSkillUseSubsystem::IsAutoShovelEquipped() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || Net->SelfWeaponItemId <= 0) { return false; }
	UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* R = IT ? IT->GetItem(Net->SelfWeaponItemId) : nullptr;
	return R && R->ItemType == 10 && R->ItemSubType == 2;
}

// Гейт 8: `weaponMask & (1 << equippedWeaponType)`. `equippedWeaponType` персонажа == `ItemSubType`
//   надетого оружия — подтверждено ветками эталона (`effects_particles.c:22013` «8 || 10», «9»;
//   `skill_buff.c:2441` навыки 905/913/915 требуют тип 5 = посох, а Mage-маска = 0x20 = бит 5).
bool UAsdaSkillUseSubsystem::IsWeaponAllowed(const FAsdaSkillDef& D, int32& OutWeaponType) const
{
	OutWeaponType = -1;
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (const AAsdaPlayerCharacter* Ch = GetLocalChar()) { OutWeaponType = Ch->EquippedWeaponSubType; }
	if (OutWeaponType < 0 && Net && Net->SelfWeaponItemId > 0)
	{
		UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
		if (const FAsdaItemRecord* R = IT ? IT->GetItem(Net->SelfWeaponItemId) : nullptr) { OutWeaponType = R->ItemSubType; }
	}
	// Эталон: `if (mask && (mask & (1 << type)) == 0 && !(lowMask == 0x40000 && highMask == 0 && charField > 0)) отказ`.
	//   Маска 0 = ограничений нет.
	if (D.WeaponMask == 0) { return true; }
	if (OutWeaponType < 0 || OutWeaponType > 63)
	{
		// ★ ПРАВИЛО «НЕТ ДАННЫХ → НЕ БЛОКИРУЕМ»: неизвестный тип оружия не должен молча запрещать игру.
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] тип надетого оружия неизвестен (%d) — гейт маски оружия ПРОПУЩЕН"),
			OutWeaponType);
		return true;
	}
	if ((D.WeaponMask & (1LL << OutWeaponType)) != 0) { return true; }
	// Исключение эталона (`combat_pvp.c:7068`): маска ровно 0x40000 (бит 18) + поле персонажа > 0
	//   ⇒ применять всё равно можно. Поле (`gap_636[448]` локального игрока) в порту не заведено —
	//   честная запись, ветка не срабатывает.
	if (D.WeaponMask == 0x40000)
	{
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] маска оружия 0x40000: у эталона есть обход по полю ")
			TEXT("персонажа (gap_636[448] > 0), которого в порту нет — обход не применён"));
	}
	return false;
}

int32 UAsdaSkillUseSubsystem::GetCurrentMp() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return -1; }
	if (const UAsdaNetSubsystem::FAsdaVitals* V = Net->VitalsByUserIdx.Find(AsdaSelf::GetUserIdxOr(GetGameInstance(), INDEX_NONE))) { return V->MpCur; }
	return -1;
}

// Стоимость ОМ текущего уровня. Эталон `skillCheckLevelRequirement@0xE57DE0` берёт её из ВЫУЧЕННОЙ
//   записи: `v5 = (i16)le[5]` — слово 5 = БАЙТ 10 записи. В порту байт 10 разобран в
//   `FAsdaLearnedSkill::ReqCharLevel` — ★ имя ВРЁТ: сравнение `текущая ОМ >= le[5]` бессмысленно
//   для «требуемого уровня персонажа», это стоимость ОМ (отчёт §3, поправка к skill_mechanics.md).
//   Поле не переименовано намеренно: оно читается окном навыков, переименование — отдельная правка.
//   Фолбэк (записи нет / значение 0): формула тултипа порта `Level·MpPerLevel + MpBase`.
int32 UAsdaSkillUseSubsystem::GetMpCost(int32 SkillCode, const FAsdaSkillDef& D) const
{
	const UAsdaSkillWindowSubsystem* SW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	const int32 Level = SW ? SW->GetLearnedLevel(SkillCode) : 0;
	// Навыки > 1201 (Хранитель) эталон считает по `.sif mpCostBase` — учтено формулой ниже.
	const int32 ByTable = Level * D.MpCostPerLevel + D.MpCostBase;
	return FMath::Max(0, ByTable);
}

bool UAsdaSkillUseSubsystem::CheckGates(int32 SkillCode, const FAsdaSkillDef& D, bool bTargetPath)
{
	// ★★★ 0. АНТИ-СПАМ ЗДЕСЬ УБРАН (2026-08-11) — В ЭТАЛОНЕ ЕГО НА ЭТОМ УРОВНЕ НЕТ.
	//   `g_dwLastSkillDispatchTime` (VA `0x2C30500`) имеет в бинаре РОВНО ДВЕ ссылки, и обе лежат
	//   внутри `Hotbar_SlotActivate` (чтение `0x4D12D2 sub eax,[0x2C30500]` + `cmp eax, 0C8h` +
	//   `jbe`, запись `0x4D12E8 mov [0x2C30500], eax`), в `case 3` — слот НАВЫКА (`ui_core.c:67441`).
	//   Ни `combatHandleSkillUse@0xE4B960`, ни `skillUseOnTargetDispatch@0xE4E9F0`, ни
	//   `skillDispatchUseByBaseId@0xE57CB0`, ни `skillCastActivate@0xE4D900`, ни
	//   `hudQuickSlot_TryUseSkill@0xEF4600` его не читают.
	//   Порт держал ВТОРОЙ независимый таймер прямо в диспетчере — он глушил нажатия из ОКНА
	//   НАВЫКОВ, ряда соулмейта и ряда Хранителя (у последнего в эталоне свой глобал
	//   `g_dwQuickSlotMapCheckLastTick`, `ui_core.c:92702`), причём `return false` уходил БЕЗ ЕДИНОЙ
	//   СТРОКИ В ЛОГ — потеря нажатия была невидима. Эталонный троттлинг остался ровно там, где он
	//   и есть у оригинала: `UAsdaHotbarSubsystem::ActivateSlot`, ветка `EAsdaHotbarSlotType::Skill`.

	// ★★★ 0-бис. GM-«СКРЫТЬСЯ»: навыки запрещены. Сервер под `CUser+0x3BC3B == 300` МОЛЧА глотает
	//   и 5256 (`RecvChar_SkillUse`, case 0x1488), и 6158 (`RecvChar_SkillUse_0`, case 0x180E) —
	//   поэтому клиент не должен ни проигрывать каст, ни ставить состояние показа навыка.
	//   Гейт стоит ЗДЕСЬ, потому что это единственная общая точка всех четырёх путей применения
	//   (боевой ряд, ряд по цели, окно навыков, ряд Хранителя).
	//   МОЛЧА и БЕЗ НОТИСА, в отличие от гейтов ниже: у скрытия в эталоне нотисов нет ни одного
	//   (закон `never-invent-only-original`; таблица «событие → нотис → канал» пуста).
	//   Ключ отката: `asda.Gm.HideAttackGate 0`.
	if (AsdaGmHide::BlockCombatAction(GetGameInstance()))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d не применён: оператор скрыт (GM-hide) — сервер отбросил бы пакет молча"), SkillCode);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ GM-hide — оператор скрыт, навык не применён (молча)"), SkillCode);
		return false;
	}
	// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущий навыки не применяет — МОЛЧА (нотиса «в воде» у оригинала нет).
	//   Общая точка всех четырёх путей (боевой ряд, ряд по цели, окно навыков, ряд Хранителя). Ключ `asda.Swim.ActionGate`.
	if (AsdaSwimActionLocked(GetLocalChar()))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d не применён: персонаж плывёт"), SkillCode);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ плавания — персонаж в глубокой воде, навык не применён (молча)"), SkillCode);
		return false;
	}
	// 1. Удочка (нотис 110707).
	if (IsFishingRodEquipped())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 1 — в руках удочка (нотис %d)"), SkillCode, kLocFishingRod);
		PostSkillNotice(kLocFishingRod);
		return false;
	}
	// 2. Авто-бур (нотис 111204) — ГЕЙТ ЗАКРЫТ: эталон `playerHasEquippedItemType10Sub2@0xFE9A40`
	//    проверяет запись надетого оружия на ItemType 10 / ItemSubType 2 (см. IsAutoShovelEquipped).
	if (IsAutoShovelEquipped())
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 2 — надет авто-бур (нотис %d)"), SkillCode, kLocAutoShovel);
		PostSkillNotice(kLocAutoShovel);
		return false;
	}
	// 3. Верхом (нотис 2038 для атаки / 2039 для навыка на сервере).
	if (const AAsdaPlayerCharacter* Ch = GetLocalChar())
	{
		// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Mount.ListHoldsModel`): эталон смотрит на запись (15, 2), а не на модель (skill_buff.c:31555).
		// ★ 07.11.2026 (`asda.Mount.SelfListGate`): только состояние; модель в окне между 5455 и 5454 ещё видна, а гейт эталона уже свободен.
		const bool bMountGate = AsdaMountSelfListGateOn() ? Ch->IsMountHeldByServer() : (Ch->IsMounted() || Ch->IsMountHeldByServer());
		if (bMountGate)
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 3 — персонаж верхом (нотис %d)"), SkillCode, kLocOnVehicle);
			PostSkillNotice(kLocOnVehicle);
			return false;
		}
	}
	// 4. Подтипы 3 и 4 не применяются вообще — эталон выходит МОЛЧА (`skill_buff.c:31563`).
	if (D.SubType == 3 || D.SubType == 4)
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: подтип %d (пассив/авто) — эталон молча не применяет"),
			SkillCode, D.SubType);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 4 — подтип %d (пассив/авто), не применяется (молча)"), SkillCode, D.SubType);
		return false;
	}
	// Навык должен быть ВЫУЧЕН: эталон берёт запись `Skill_GetLearnedSkillEntry`; её отсутствие
	//   роняет проверку ОМ в 0 (сервер отвечает кодом 25 «навык не выучен»).
	//   ★ НЕ ПРИМЕНЯЕТСЯ К СКИЛЛАМ ХРАНИТЕЛЯ (≥ 1201): их нет в книге навыков (пакет 5251 их не
	//     присылает), доступность ряда решает сам Хранитель по монетам и `SoulGuarderTable`.
	//     Эталон здесь гейта не имеет вовсе — `Skill_GetLearnedSkillLevel` у него лишь ЗНАЧЕНИЕ,
	//     которое едет в шапку заявки.
	const UAsdaSkillWindowSubsystem* SW = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	const int32 Level = SW ? SW->GetLearnedLevel(SkillCode) : 0;
	// ★★★ П25 + П36 (2026-08-11): ЭТАЛОН ЗДЕСЬ НЕ МОЛЧИТ — он печатает нотис **1018**.
	//   `skillCheckLevelRequirement@0xE57DE0` (`skill_buff.c:34058-34092`) возвращает 0 в трёх случаях:
	//     · `!isInRange_501_1254(id)` — код вне [501, 1255)                       ← это и есть П36;
	//     · записи навыка в `.sif` нет;
	//     · `Skill_GetLearnedSkillEntry` вернул null (навык НЕ ВЫУЧ�ёН) и `id <= 1201`
	//       (при `id > 1201` эталон подставляет синтетическую запись с `mpCostBase` из `.sif`).
	//   Вызывающий (`skill_buff.c:31574-31579`, `combat_pvp.c:7035`) на 0 печатает
	//   `UI_FormatLocalizedNotice(1018)` через `CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000)`.
	//   Порт выходил ТИХО — игрок не понимал, почему нажатие не даёт ничего.
	//   ⚠ Гейт «есть эффект 109» (`skill_buff.c:34069`: возвращает 1 БЕЗУСЛОВНО) в порту не заведён —
	//   эффект 109 в списке состояний не появляется, ветка честно не воспроизводится.
	const bool bCodeInRange = (SkillCode >= 501 && SkillCode < 1255);   // isInRange_501_1254@0xE4B090
	if (!bCodeInRange || (Level <= 0 && SkillCode <= 1201))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: %s — эталонный нотис 1018 ")
			TEXT("(skillCheckLevelRequirement@0xE57DE0 вернул 0)"), SkillCode, *D.Name,
			!bCodeInRange ? TEXT("код вне диапазона 501..1254") : TEXT("навык не выучен (уровень 0)"));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ уровня — %s (нотис %d)"), SkillCode,
			!bCodeInRange ? TEXT("код вне диапазона 501..1254") : TEXT("навык не выучен (уровень 0)"), kLocNoMp);
		PostSkillNotice(kLocNoMp);
		return false;
	}
	// 5. Кулдаун (нотис 2203). Эталон читает вектор `CSkillWnd + 56` (`classListFindStateByField4`) —
	//    в порту тот же реестр живёт в окне навыков и дублируется в панель быстрого доступа.
	if (SW && SW->GetCooldownRemainSec(SkillCode) > 0.f)
	{
		// ★ В лог обязательно КОД и ОСТАТОК: без них по логу нельзя отличить «отказ своему навыку»
		//   от «отказ чужому» — а именно это надо было доказать по жалобе «кулдаун вешается на другие».
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: гейт кулдауна — осталось %.1f с%s"),
			SkillCode, *D.Name, SW->GetCooldownRemainSec(SkillCode),
			D.ChainSkillIds.Contains(SkillCode)
				? TEXT(" (навык входит в группу общего отката .sif +0xA7)") : TEXT(""));
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 5 КУЛДАУН — осталось %.1f с (нотис %d)"), SkillCode, SW->GetCooldownRemainSec(SkillCode), kLocCooldown);
		PostSkillNotice(kLocCooldown);
		return false;
	}
	// 6. ★ ОМ (нотис 1018). Эталонная функция названа «проверкой уровня» — это проверка ОМ.
	{
		const int32 Mp = GetCurrentMp();
		const int32 Cost = GetMpCost(SkillCode, D);
		if (Mp >= 0 && Cost > 0 && Mp < Cost)
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 6 MP — есть %d, нужно %d (нотис %d)"), SkillCode, Mp, Cost, kLocNoMp);
			PostSkillNotice(kLocNoMp);
			return false;
		}
		if (Mp < 0)
		{
			UE_LOG(LogAsdaSkillUse, Verbose, TEXT("[навыки] текущая ОМ неизвестна — гейт ОМ пропущен (сервер проверит сам, код отказа 2)"));
		}
	}
	// ★★★ 6а. П11 (2026-08-11): ГЕЙТ «ПЕРСОНАЖ ПОД КОНТРОЛЕМ» — нотис **1155**.
	//   Эталон `combat_pvp.c:7041-7046`: стоит СРАЗУ после `skillCheckLevelRequirement` (1018) и ДО
	//   гейта «навык уже применяется» — здесь же и стоит.
	//       if ( charHasAnyEquippedItemType(g_LocalPlayer) )
	//       { UI_FormatLocalizedNotice(1155) → CControl_SetTooltipText(g_pChatWnd, …, −11514, 10000); return; }
	//   ⚠ Имя эталонной функции — МИСНЕЙМ (разбор в объявлении `UAsdaEffectsSubsystem::HasControlState`):
	//     она не смотрит экипировку, а проверяет СПИСОК СОСТОЯНИЙ на шесть обездвиживающих id
	//     {0, 1, 3, 14, 8, 4} с Type == 0. Прежняя причина отсрочки «в порту нет источника про надетые
	//     предметы» снята вместе с миснеймом — источник у нас есть и он полный.
	// ★ 05.11.2026 [ОРИГИНАЛ]: путь «по цели» — свой гейт эталона (разбор у ключа `asda.Skill.TargetControlGate`).
	if (bTargetPath && CVarAsdaSkillTargetControlGate.GetValueOnGameThread() != 0)
	{
		if (const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
		{
			const int32 Self = Fx->SelfUserIdx();
			const bool bCleanse = SkillCode == 639 || SkillCode == 615 || SkillCode == 736 || SkillCode == 984;   // снимают контроль
			if (!bCleanse && Fx->HasControlState(Self))
			{
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: путь «по цели» под контролем (0/1/3/14/8/4) — молча не применяется (эталон)"),
					SkillCode, *D.Name);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: путь «по цели», персонаж под контролем — НЕ применён, нотиса нет (эталон skillUseOnTargetDispatch)"), SkillCode);
				return false;
			}
			if (SkillCode == 537 || SkillCode == 597)
			{
				// `listHasAnyOfSlotTypes@0xA38970`: 0, 1, 2, 3, 14, 9, 4, 8 (рывок к цели — и ловушка держит).
				static const int32 kRushLock[] = { 0, 1, 2, 3, 14, 9, 4, 8 };
				for (const int32 Id : kRushLock)
				{
					if (!Fx->FindByTypeSub(Self, Id, 0)) { continue; }
					UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: рывок под состоянием %d — нотис 1155 (эталон)"), SkillCode, *D.Name, Id);
					ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: рывок под состоянием %d — нотис %d"), SkillCode, Id, kLocUnderControl);
					PostSkillNotice(kLocUnderControl);
					return false;
				}
			}
		}
	}
	else if (const UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		if (Fx->HasControlState(Fx->SelfUserIdx()))
		{
			UE_LOG(LogAsdaSkillUse, Log,
				TEXT("[навыки] %d «%s»: гейт 1155 — персонаж под контролем (состояние из набора 0/1/3/14/8/4)"),
				SkillCode, *D.Name);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 6а ОБЕЗДВИЖЕН — персонаж под контролем (состояние 0/1/3/14/8/4), нотис %d"), SkillCode, kLocUnderControl);
			PostSkillNotice(kLocUnderControl);
			return false;
		}
	}
	// 7. Тот же навык уже кастуется, и подтип не 5 (тогл) — нотис 2203.
	if (ActiveSkill == SkillCode && D.SubType != 5)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 7 — этот навык уже применяется (состояние %d), нотис %d"), SkillCode, CharState, kLocCooldown);
		PostSkillNotice(kLocCooldown);
		return false;
	}
	// 8. Оружие по маске (нотис 1020).
	//   ★ Скиллы Хранителя маску НЕ проверяют: эталон `combatHandleSkillUse` (combat_pvp.c:7061-7065)
	//     прямо перед этой проверкой ОБНУЛЯЕТ маску записи при `skillId > 1201`
	//     (`*(_DWORD*)pSkillRecord->weaponMask = 0; *(_DWORD*)&weaponMask[4] = 0;`).
#if UE_BUILD_SHIPPING
	const bool bWeaponGate = (SkillCode <= 1201);
#else
	const bool bWeaponGate = (SkillCode <= 1201) && !bDebugSkipWeaponGate;   // самотест: шаг 11 Grenade
#endif
	if (bWeaponGate)
	{
		int32 WType = -1;
		if (!IsWeaponAllowed(D, WType))
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: маска оружия 0x%llX не содержит бит %d"),
				SkillCode, *D.Name, (long long)D.WeaponMask, WType);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 8 ОРУЖИЕ — маска 0x%llX без бита %d (нотис %d)"), SkillCode, (long long)D.WeaponMask, WType, kLocBadWeapon);
			PostSkillNotice(kLocBadWeapon);
			return false;
		}
	}
	// ★ 08.11.2026 [ОРИГИНАЛ] рывок 537/597: путь до цели (13219) и минимальная дистанция 600 (13218) — разбор у `asda.Skill.RushRangeGate`.
	if (bTargetPath && (SkillCode == 537 || SkillCode == 597) && CVarAsdaSkillRushRangeGate.GetValueOnGameThread() != 0)
	{
		int32 RType = 0, RIdx = -1; FVector2D RPos; float RDist = 0.f;
		const AAsdaPlayerCharacter* Me = GetLocalChar();
		if (Me && GetSelectedTarget(RType, RIdx, RPos, RDist))
		{
			const FVector2D From = AsdaCoord::UeToGame2D(Me->GetActorLocation());
			bool bClear = true;
			for (int32 s = 0; s <= 100 && bClear; ++s)
			{
				const FVector2D P = FMath::Lerp(From, RPos, s * 0.01f);
				if (P.X < 0.f || P.Y < 0.f || (Me->WalkGrid && !AsdaPath::IsLineWalkable(Me->WalkGrid, P / 100.f, P / 100.f))) { bClear = false; }   // земля под точкой (как 8215: GroundTarget_PickOnMesh ↔ сетка проходимости)
			}
			if (!bClear)
			{
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: рывок — на пути до цели нет земли/препятствие, нотис 13219 (эталон targetPathLineOfSightCheck)"), SkillCode, *D.Name);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: рывок — путь до цели перекрыт, нотис %d"), SkillCode, kLocRushObstacle);
				PostSkillNotice(kLocRushObstacle);
				return false;
			}
			if (RDist < kRushMinDistU)
			{
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d «%s»: рывок — до цели %.0f < %.0f, нотис 13218 (эталон invItemTargetInRange)"), SkillCode, *D.Name, RDist, kRushMinDistU);
				ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: рывок — цель ближе минимума (%.0f < %.0f), нотис %d"), SkillCode, RDist, kRushMinDistU, kLocRushTooClose);
				PostSkillNotice(kLocRushTooClose);
				return false;
			}
		}
	}
	// 12. Состояние действия персонажа — эталон выходит МОЛЧА.
	// ★★★ 27.09.2026 (эталонная схема хода, `asda.Move.EtalonSpeedSync 1`): в ТОМ ЖЕ условии эталон проверяет
	//   бит 2 «запрос хода в пути» — `if ((g_nGameFlags & 2) == 0 && state != 14 && … != 9) { применить }`
	//   (`combatHandleSkillUse@0xE4B960`, game.c:462741; так же `skillCastActivate@0xE4D900` :463455 и
	//   `skillUseOnTargetDispatch@0xE4E9F0` :464105). Пока 4006 без ответа 4007 — навык молча не применяется.
	if (AAsdaPlayerController::IsEtalonSpeedSync())
	{
		UAsdaNetSubsystem* GateNet = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (GateNet && GateNet->IsMoveRequestInFlight())
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: запрос хода ещё без ответа 4007 (бит 2) — эталон молчит"), SkillCode);
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ хода — 4006 ещё без ответа 4007 (бит 2), навык не применён (молча)"), SkillCode);
			NoteSilentRefuse(SkillCode, TEXT("бит хода 2 (4006 без ответа 4007)"));
			return false;
		}
	}
	if (StateBlocksCast(CharState))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: состояние действия %d запрещает старт (эталон молчит)"),
			SkillCode, CharState);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: ГЕЙТ 12 — состояние действия %d запрещает старт (14/27/13/15/25/6/7/8/9), молча"), SkillCode, CharState);
		NoteSilentRefuse(SkillCode, TEXT("гейт 12 (состояние действия)"));
		return false;
	}
	// Ветка «по цели»: цель нужна, если `targetSlotType` требует её. Эталон отдельного нотиса тут
	//   не печатает — «нет цели» приходит с сервера кодом 20 (нотис 1083 «Choose a target»).
	if (bTargetPath && D.TargetSlotType == 6)
	{
		int32 TType = 0, TIdx = -1; FVector2D TPos; float TDist = 0.f;
		if (!GetSelectedTarget(TType, TIdx, TPos, TDist))
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: нужна цель (targetSlotType 6), а она не выбрана — ")
				TEXT("эталон всё равно шлёт заявку, отказ придёт кодом 20 (нотис 1083)"), SkillCode);
			ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: нужна цель (вид 6), а она не выбрана — заявка всё равно идёт (отказ придёт кодом 20)"), SkillCode);
		}
	}
	// ★ R5-fix8 (14.11.2026): навык прошёл гейты — новое намерение игрока; отложенный очередью клик хода больше не исполняется
	//   (разбор — у ключа `asda.Input.ClickQueueSupersede`).
	AsdaSkillCastLock::SupersedeQueuedMove(TEXT("применён навык"), 2);
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d «%s»: гейты ПРОЙДЕНЫ (уровень %d, MP %d / стоимость %d, состояние %d)"),
		SkillCode, *D.Name, Level, GetCurrentMp(), GetMpCost(SkillCode, D), CharState);
	SilentRefuseRun = 0; bSilentRefuseWarned = false;   // ★ баг 7 (ревизия п.5): серия молчаливых отказов прервана
	return true;
}

// ══════════════════════════════ ОТЛОЖЕННОЕ ДЕЙСТВИЕ 102 ════════════════════════════════════════

// Эталон `charObject_SetSkillAction102@0xA28150`: пишет код действия 102 и pending-слот; сама
//   отправка происходит на СЛЕДУЮЩЕМ ТИКЕ в `charUpdateActionStateTick` case 0x66.
void UAsdaSkillUseSubsystem::QueueSkillAction102(int32 SkillCode)
{
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: отложенное действие 102 поставлено (уйдёт следующим тиком)%s"),
		SkillCode, PendingSkill >= 0 && PendingSkill != SkillCode ? *FString::Printf(TEXT(", вытеснено прежнее %d"), PendingSkill) : TEXT(""));
	PendingAction = EAsdaCharAction::UseSkill;
	PendingSkill = SkillCode;
}

void UAsdaSkillUseSubsystem::FlushPendingAction()
{
	if (PendingAction != EAsdaCharAction::UseSkill || PendingSkill < 0) { return; }
	const int32 Code = PendingSkill;
	PendingAction = EAsdaCharAction::None;
	PendingSkill = -1;

	const FAsdaSkillDef* D = GetDef(Code);
	if (!D)
	{
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: действие 102 снято — записи в базе нет"), Code);
		return;
	}
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d «%s»: действие 102 исполняется — активный навык, сборка заявки"), Code, *D->Name);
	// Эталон первым делом делает pending → active (`pChar[43134] = pChar[43135]`).
	ActiveSkill = Code;
	ActiveSkillSentS = FPlatformTime::Seconds();   // ★ отсчёт сторожа «ответа нет» (см. Tick)
	SendUse(Code, *D);
}
