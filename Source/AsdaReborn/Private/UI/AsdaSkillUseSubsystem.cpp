// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem.cpp — боевой диспетчер применения навыков.
//   Модель, порядок гейтов и ссылки на эталон — в шапке `AsdaSkillUseSubsystem.h`.
//   Полный реверс: `Reports/skill_use_system.md`.

#include "UI/AsdaSkillUseSubsystem.h"
#include "AsdaPerf.h"   // R4, доля a: области замера времени игрового потока
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

DEFINE_LOG_CATEGORY(LogAsdaSkillUse);

ASDA_DEV_CVAR(int32, CVarAsdaSkillLatchGuard, TEXT("asda.Skill.LatchGuard"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] Защёлки диспетчера навыков (баг 7): 1 = отказ 6159/6161 Хранителя и ответ 5232 соулмейта снимают "
	     "состояние действия, предохранитель снимает любое блокирующее состояние без сторожа по сроку (14 — время каста + 2 с, "
	     "прочее — 2 с, ожидание визуала в 24 — 4 с); 0 = прежнее (защёлка могла держаться до перезапуска)."),
	ECVF_Default);

// ★★★ 01.11.2026 [ОРИГИНАЛ] (жалоба игроков «у навыков двуручного мечника не хватает анимаций», аудит skill_anim_audit):
//   навык «вне дальности» посреди автоатаки НИКОГДА не уходил. Порт в `TickSkillApproach` не заводил свой подход, пока идёт
//   боевой цикл, считая, что стоп-точка автоатаки лежит внутри зоны навыка. Это неверно: автоатака встаёт на дальности
//   ОРУЖИЯ с радиусом цели (`AttackTick`: `item@0x8C × 100 + радиус капсулы`), а навык меряет центр–центр против своего радиуса
//   без радиуса цели (эталон ниже). Двуручный меч (2 тайла) у волка встаёт на 301 юн, а «Сильный боевой дух» 507, «Восход
//   дракона» 508, «Удар» 512 — 300 юн: подход ждал 20 с и снимался, заявки 5256 не было, клипа и эффекта тоже (самотест
//   `asda.Skill.SelfTestAnimAudit w=3 fight`). То же у копья (3 тайла) и луков (15–16) с навыками короче дальности оружия.
//   ЭТАЛОН (`Player_UpdateAutoActionState@0xA249A0`, case 0x66, entity_char.c:24852-24872): в состоянии 102 каждый тик
//   `dist² > range²` → `Combat_SendNormalAttack_thunk(…, 2)` (ход к цели); 102 при нажатии навыка ЗАМЕНЯЕТ состояние атаки 5,
//   так что автоатака подходу не мешает и его не подменяет.
//   Ключ: 1 — свой подход ради навыка идёт и при автоатаке, замах и подход автоатаки на это время стоят (`AttackTick`);
//   0 — прежний порт (ждать боевой цикл).
static ASDA_DEV_CVAR(int32, CVarAsdaSkillApproachOverAttack, TEXT("asda.Skill.ApproachOverAttack"), 1,
	TEXT("[ОРИГИНАЛ] Навык вне дальности посреди автоатаки: 1 = подход ради навыка идёт сам (эталон, состояние 102 вместо атаки 5), "
	     "автоатака на это время не бьёт и не подходит; 0 = прежний порт (ждать стоп-точку автоатаки — у двуручного/копья/лука "
	     "она бывает дальше радиуса навыка, и навык не уходил)."),
	ECVF_Default);

// Включён ли ключ `asda.Skill.ApproachOverAttack` (аксессор — имя ключа не попадает в рабочий код; читает и `AttackTick`).
bool AsdaSkillApproachOverAttackOn() { return CVarAsdaSkillApproachOverAttack.GetValueOnGameThread() != 0; }

void UAsdaSkillUseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnSkillUseResult.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetCastResult);
		Net->OnSkillUseCastIdle.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetCastIdle);
		Net->OnSkillCastState.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetCastState);
		// ★ 31.10.2026 (баг 7): 5232 — ответ на навык соулмейта 5231, снимает состояние 25 (см. OnNetSoulSkillState).
		Net->OnSoulSkillState.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetSoulSkillState);
		Net->OnSoulmateState.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetSoulmateState);
		// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Soul.SlotTruth`): `soulmateWnd_handleResponsePacket@0xE67590` на 5207 кодах 1 и 3 пишет `gap_878[47] = −1`
		//   (game.c:473318, 473349), код 2 «Друг вне игры» слот не трогает. 5220 после разрыва сервер не шлёт (`Send_SoulMateInfo`: слот ≤ 0 — выход).
		Net->OnSoulmateBreak.AddWeakLambda(this, [this](int32 Sub, FString /*Name*/)
		{
			if ((Sub == 1 || Sub == 3) && AsdaSoulSlotTruthOn()) { SelfSoulIdx = -1; }
		});
		Net->OnSoulmatePeer.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetSoulmatePeer);
		// ★ PvP-флаг (эталон buffFxSlots[160], опкод 4206) — выбирает площадной отправитель.
		Net->OnPvpMode.AddUObject(this, &UAsdaSkillUseSubsystem::OnNetPvpMode);
	}
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] боевой диспетчер поднят (5256 → 5257/5270/5271)"));
}

void UAsdaSkillUseSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSkillUseResult.RemoveAll(this);
			Net->OnSkillUseCastIdle.RemoveAll(this);
			Net->OnSkillCastState.RemoveAll(this);
			Net->OnSoulSkillState.RemoveAll(this);
			Net->OnSoulmateState.RemoveAll(this);
			Net->OnSoulmateBreak.RemoveAll(this);
			Net->OnSoulmatePeer.RemoveAll(this);
			Net->OnPvpMode.RemoveAll(this);
		}
	}
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSkillUseSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Применение навыка — состояние ОДНОГО каста, привязанное к актору умершего мира и к
	// сессионным userIdx: гасим при любой причине.
	NetQueue.Reset();
	PendingAction = EAsdaCharAction::None;
	PendingSkill = -1;
	ApproachDeadlineS = 0.0;
	ActiveSkill = -1;
	ActiveSkillSentS = 0.0;
	CharState = 0;
	CastShowEndS = 0.0;
	LatchSinceS = 0.0; LatchState = 0; LatchSkill = -1; ShowOverrunSinceS = 0.0;   // ★ баг 7: отсчёты предохранителя — одной сессии
	SelfShownSkill = -1; SoulmatePendingSkill = -1; SilentRefuseRun = 0; bSilentRefuseWarned = false;
	LastSentSkill = -1;
	LastRefuseCode = -1; LastRefuseSkill = -1;
	if (AsdaIsFullSessionReset(Reason))
	{
		SelfSoulIdx = -1;
		bSelfPvpMode = false;
	}
}

// ★ Тик = эталонный `charUpdateActionStateTick`: сначала разбираем пришедшее по сети
//   (закон no-reentrant-net-callbacks — из колбэка сеть не трогаем), затем исполняем
//   ОТЛОЖЕННОЕ действие 102, поставленное в этом кадре кликом/клавишей.
void UAsdaSkillUseSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.SkillUse");   // R4, доля a: область замера (AsdaPerf.h; в Shipping пусто)
	if (NetQueue.Num() > 0)
	{
		TArray<FPendingNetEvent> Local = MoveTemp(NetQueue);
		NetQueue.Reset();
		for (const FPendingNetEvent& E : Local) { ApplyNetEvent(E); }
	}
	// ★★ СТОРОЖ «сервер не ответил» — эталонный локальный отсчёт состояния 14 (2000 мс, отчёт §5.4).
	//   Пока он не стоял, единственная заявка без ответа НАВСЕГДА запирала диспетчер: гейт 7
	//   («тот же навык уже кастуется») сравнивает с `ActiveSkill`, а сбрасывал его только ответ.
	if (ActiveSkill >= 0 && ActiveSkillSentS > 0.0
		&& FPlatformTime::Seconds() - ActiveSkillSentS > kNoAnswerGuardS)
	{
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] навык %d: ответа сервера (5257/5270/5271) нет ")
			TEXT("%.1f с — активный навык сброшен (эталон в состоянии 14 отсчитывает те же 2000 мс). ")
			TEXT("Если это повторяется — заявка 5256 до обработчика НЕ доходит."),
			ActiveSkill, kNoAnswerGuardS);
		ASDA_CTRACE(EAsdaCT::Refuse, TEXT("навык %d: сервер НЕ ответил на заявку за %.1f с (5257/5270/5271 нет) — сторож сбросил активный навык, состояние %d → 0"),
			ActiveSkill, kNoAnswerGuardS, CharState);
		ActiveSkill = -1;
		ActiveSkillSentS = 0.0;
		CharState = 0;
		if (UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
		{
			Bar->End(TEXT("сервер не ответил"));
		}
	}
	// ★★★ ВЫХОД ИЗ СОСТОЯНИЯ 24 ПО КОНЦУ КЛИПА — порт эталонного тика состояния 24
	//   (`obj[13] >= obj[11]` → `CChar_SetState(pChar, 0)`, слот показа снят). Без него мгновенные
	//   атакующие навыки ЗАЛИПАЛИ: сервер шлёт 5271 РАНЬШЕ 5257 (живой лог 2026-08-10, см. шапку
	//   поля CastShowEndS в .h), и после 5257 сбросить ActiveSkill/CharState было некому — гейт 7
	//   давал «Навык уже применяется» до следующего каста другого навыка.
	if (ActiveSkill >= 0 && CharState == 24 && CastShowEndS > 0.0
		&& FPlatformTime::Seconds() >= CastShowEndS)
	{
		UAsdaSkillFxSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillFxSubsystem>() : nullptr;
		AAsdaPlayerCharacter* C = GetLocalChar();
		if (!Fx || !C || !Fx->IsSkillVisualActive(C))
		{
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] навык %d: клип показа доиграл — состояние 24 → 0, ")
				TEXT("активный навык снят (эталонный выход тика состояния 24)"), ActiveSkill);
			ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: клип показа доиграл — состояние 24 → 0, активный навык снят"), ActiveSkill);
			ActiveSkill = -1;
			ActiveSkillSentS = 0.0;
			CastShowEndS = 0.0;
			CharState = 0;
			ShowOverrunSinceS = 0.0;
			if (UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr)
			{
				Bar->End(TEXT("клип показа доиграл (выход состояния 24)"));
			}
		}
		else
		{
			// Визуальный слой ещё играет (клип чуть длиннее расчётного) — ждём его, проверяя каждый тик.
			ASDA_CTRACE_V(EAsdaCT::Skill, TEXT("навык %d: срок состояния 24 вышел, но клип ещё играет — ждём"), ActiveSkill);
			CastShowEndS = FPlatformTime::Seconds();
			if (ShowOverrunSinceS <= 0.0) { ShowOverrunSinceS = CastShowEndS; }   // ★ баг 7: начало ожидания — для предохранителя
		}
	}
	// ★★★ 31.10.2026 (баг 7): предохранитель защёлок — ни одно блокирующее состояние не держится вечно (шапка в .h).
	if (AsdaSkillLatchGuardOn()) { TickLatchGuard(); }
	// ★ Порт ветки «вне дальности» состояния 102: пока цель далеко — бежим к ней, каст по прибытии.
	TickSkillApproach();
	FlushPendingAction();
}

// ══════════════════ ★★★ «ПОДБЕЖАТЬ, ОСТАНОВИТЬСЯ, ПРИМЕНИТЬ» (состояние 102) ═══════════════════
//
// ЭТАЛОН (`Player_UpdateAutoActionState@0xA249A0`, case 0x66 → LABEL_7, entity_char.c:24845-24878):
//     dist² = Math_DistanceSquared3D(центр цели, мой центр)
//     range² = me->effectSlots[ effectSlots[100].field_4 ].soundId
//              ← туда `packPathDispatchEntryHandler` кладёт результат
//                `skillCalcAbilityBonus@0xA870B0` = ((бонусДальности + rec[+0x35]) · 100)²
//     dist² > range² → `Combat_SendNormalAttack_thunk(idx, myPos, targetPos, 2)` — это ОБЫЧНЫЙ
//                      ход 4006 с renderId цели («иди к ней»), состояние 102 СОХРАНЯЕТСЯ,
//                      на следующем тике проверка повторяется;
//     dist² ≤ range² → `if (getFlag_47551(me)) Net_SendActionPacket(...)` (= 4008 стоп)
//                      и затем `Skill_SendSkillUseOnTarget` / `Skill_SendSkillUse` (= 5256).
// Именно поэтому в оригинале к моменту заявки серверный флаг движения всегда снят.
//
// ПОРТ: подход ведём ТЕМ ЖЕ маршрутизатором, что и обычная атака
// (`AAsdaPlayerController::StartRoutedMove`, точка остановки — граница зоны действия), второго
// механизма движения не заводим.

float UAsdaSkillUseSubsystem::SkillRangeUnits(const FAsdaSkillDef& D) const
{
	return (float)D.RangeMeters * 100.f;
}

void UAsdaSkillUseSubsystem::BeginSkillApproach(int32 SkillCode, float RangeU, int32 TType, int32 TIdx)
{
	ApproachSkill      = SkillCode;
	ApproachTargetType = TType;
	ApproachTargetIdx  = TIdx;
	ApproachNextS      = 0.0;                                        // первый ход — сразу
	ApproachDeadlineS  = FPlatformTime::Seconds() + kApproachTimeoutS;
	// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`, ревизия координатора п.1–3): подход ради навыка = эталонное
	//   состояние 102 ВМЕСТО действия атаки 5. Цикл замаха (бесконечное удержание, таймеры отрезков и отложенной отправки) снимается,
	//   боевая стойка гаснет — иначе конец прохода слал бы 4028/4203 «на бегу», а `AsdaSelfInAttackAction` считал бы нас в атаке и
	//   отдал ложный 4024/400 при клике по земле/смене цели во время подхода. 400 здесь НЕ шлём: 5 → 102 — не `CCharacter_SetAction`
	//   case 0 (entity_char.c:16545-16556). Бой вернётся обычным входом в дальность после навыка (`AttackTick`/`PvpAttackTick`).
	//   ★ Снимается В МОМЕНТ ПОСТАНОВКИ 102 (здесь), а не на первом шаге: выход из боя в том же кадре иначе ещё видел стойку → ложный 400.
	AAsdaPlayerController* PC = GetLocalPC();
	AAsdaPlayerCharacter*  Ch = GetLocalChar();
	if (PC && Ch && AsdaSkillApproachOverAttackOn() && (PC->bAutoAttack || PC->bPvpAttack))
	{
		if (Ch->IsAttackLoopAlive() || PC->bInCombatStance)
		{
			ASDA_CTRACE(EAsdaCT::Swing, TEXT("подход навыка %d: замах автоатаки снят (102 вместо 5, без 4024/400), стойка боя погашена"), ApproachSkill);
		}
		Ch->SuspendAttackLoopForSkill();
		PC->GetWorldTimerManager().ClearTimer(PC->DeferredAttackSendTimer);
		PC->bInCombatStance = false;
		PC->NextAttackTickTime = 0.0;
	}
	// Заявка НЕ ушла — активный навык снимаем, иначе гейт 7 («уже кастуется») запрёт повтор.
	ActiveSkill = -1; ActiveSkillSentS = 0.0;
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: цель вне зоны действия (радиус %.0f) — подход к цели ")
		TEXT("(тип %d, idx %d), заявка уйдёт по прибытии (эталонное состояние 102)"),
		SkillCode, RangeU, TType, TIdx);
	ASDA_CTRACE(EAsdaCT::Move, TEXT("навык %d: ПОДХОД к цели %s idx=%d до радиуса %.0f юн (стоп на %.0f%% радиуса), заявка по прибытии (состояние 102), таймаут %.0f с"),
		SkillCode, TType == 0 ? TEXT("моб") : TEXT("игрок"), TIdx, RangeU, kApproachRangeFrac * 100.f, kApproachTimeoutS);
}

void UAsdaSkillUseSubsystem::CancelSkillApproach(const TCHAR* Why)
{
	if (ApproachSkill < 0) { return; }
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] подход ради навыка %d отменён: %s"), ApproachSkill, Why);
	ASDA_CTRACE(EAsdaCT::Move, TEXT("подход ради навыка %d ОТМЕНЁН: %s (цель idx=%d)"), ApproachSkill, Why, ApproachTargetIdx);
	ApproachSkill = -1; ApproachTargetType = -1; ApproachTargetIdx = -1;
	ApproachNextS = 0.0; ApproachDeadlineS = 0.0;
}

void UAsdaSkillUseSubsystem::TickSkillApproach()
{
	if (ApproachSkill < 0) { return; }

	AAsdaPlayerController* PC = GetLocalPC();
	AAsdaPlayerCharacter*  Ch = GetLocalChar();
	const FAsdaSkillDef*   D  = GetDef(ApproachSkill);
	if (!PC || !Ch || !D) { CancelSkillApproach(TEXT("нет персонажа / записи навыка")); return; }

	if (PC->IsSelfDead()) { CancelSkillApproach(TEXT("персонаж мёртв")); return; }

	const double Now = FPlatformTime::Seconds();
	if (Now > ApproachDeadlineS) { CancelSkillApproach(TEXT("цель так и не достигнута (таймаут)")); return; }

	int32 TType = 0, TIdx = -1; FVector2D TPos = FVector2D::ZeroVector; float TDist = 0.f;
	if (!GetSelectedTarget(TType, TIdx, TPos, TDist))
	{
		CancelSkillApproach(TEXT("цель потеряна (умерла / исчезла / снят выбор)"));
		return;
	}
	if (TType != ApproachTargetType || TIdx != ApproachTargetIdx)
	{
		CancelSkillApproach(TEXT("выбрана другая цель"));
		return;
	}

	const float RangeU = SkillRangeUnits(*D);
	// ★ 27.09.2026 (эталонная схема хода): авто-действие эталона (`Player_UpdateAutoActionState@0xA249A0`,
	//   0xA2595F: `and eax,2 / jne` мимо всего блока) при взведённом бите 2 не останавливается и не перезапрашивает
	//   подход — ждёт ответа на прошлый 4006. Иначе опоздавший грант увёл бы остановленного перед кастом персонажа.
	if (AAsdaPlayerController::IsEtalonSpeedSync())
	{
		UAsdaNetSubsystem* GateNet = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (GateNet && GateNet->IsMoveRequestInFlight())
		{
			ASDA_CTRACE_V(EAsdaCT::Move, TEXT("подход навыка %d: ждём ответ 4007 на прошлый 4006 (бит 2), до цели %.0f / %.0f"), ApproachSkill, TDist, RangeU);
			return;
		}
	}
	if (TDist <= RangeU)
	{
		const int32 Code = ApproachSkill;
		ApproachSkill = -1; ApproachTargetType = -1; ApproachTargetIdx = -1;
		// ★ Сначала ОСТАНОВИТЬСЯ (локально + 4008), и только потом заявка — порядок эталона.
		PC->StopMoveAndNotify(TEXT("дошли до зоны действия навыка"));
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: цель в зоне действия (%.0f ≤ %.0f) — остановились, ")
			TEXT("заявка уходит этим же тиком"), Code, TDist, RangeU);
		ASDA_CTRACE(EAsdaCT::Range, TEXT("навык %d: цель %s idx=%d В ЗОНЕ действия: %.0f ≤ %.0f юн — стоп (4008) и заявка этим тиком"),
			Code, TType == 0 ? TEXT("моб") : TEXT("игрок"), TIdx, TDist, RangeU);
		QueueSkillAction102(Code);            // FlushPendingAction ниже в этом же тике отправит 5256
		return;
	}

	// ★ НЕ ЗАВОДИМ ВТОРОЙ ПОДХОД: если уже работает боевой цикл (AttackTick сам ведёт к той же цели
	//   маршрутом и останавливается на дальности удара), просто ждём — его стоп-точка лежит внутри
	//   зоны действия навыка (дальность удара ≤ дальности навыка у атакующих навыков), а если нет —
	//   мы всё равно каждый тик проверяем дистанцию и выстрелим заявкой, как только она сойдётся.
	//   ★★★ 01.11.2026: допущение «стоп-точка боевого цикла внутри зоны навыка» неверно (шапка у ключа `asda.Skill.ApproachOverAttack`) —
	//   при ключе 1 подход ведём сами, как эталонное состояние 102; боевой цикл на это время стоит (`AttackTick`).
	if (PC->bAutoAttack && PC->TargetActor.IsValid() && !AsdaSkillApproachOverAttackOn())
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("подход навыка %d: ведёт боевой цикл (авто-атака) — свой ход не заводим, до цели %.0f / %.0f"), ApproachSkill, TDist, RangeU);
		return;
	}
#if ASDA_COMBAT_TRACE
	if (PC->bAutoAttack && PC->TargetActor.IsValid() && ApproachNextS == 0.0)
	{
		ASDA_CTRACE(EAsdaCT::Move, TEXT("подход навыка %d: автоатака стоит на %.0f юн, радиус навыка %.0f — шагаем сами (эталон 102 вместо атаки 5)"),
			ApproachSkill, TDist, RangeU);
	}
#endif

	if (Now < ApproachNextS) { return; }
#if ASDA_COMBAT_TRACE
	const bool bTrFirstLeg = (ApproachNextS == 0.0);   // первый ход подхода — уровень 1, повторы — уровень 2
#endif
	ApproachNextS = Now + 0.4;                // тот же троттл подхода, что у боевого цикла (AttackTick)

	// Точка остановки — ВНУТРИ зоны действия у её границы (не в упор к цели).
	const FVector2D Cur = Ch->GetGamePosXZ();
	FVector2D Dir = TPos - Cur;
	const float Dd = Dir.Size();
	FVector2D Stop = TPos;
	if (Dd > KINDA_SMALL_NUMBER)
	{
		Dir /= Dd;
		Stop = TPos - Dir * FMath::Min(RangeU * kApproachRangeFrac, Dd);
		// ★★★ 01.11.2026 [ОРИГИНАЛ] (`asda.Skill.ApproachOverAttack`): отрезок до точки 85 % радиуса бывает КОРОЧЕ допуска прибытия
		//   маршрута (`kRouteArriveDist` = 45): двуручный у волка стоит на 301, точка — 255 от цели, шаг 46 → маршрут «пройден»
		//   в тот же кадр, персонаж не двигается, а подход перезапускается каждые 0.4 с до таймаута (самотест AnimAudit fight).
		//   Эталон шлёт ход прямо к цели (`Combat_SendNormalAttack_thunk(…, myPos, targetPos, 2)`) и останавливается проверкой
		//   `dist² ≤ range²` на тике — она выше (`TDist <= RangeU` → стоп 4008 и заявка). Поэтому шаг не короче допуска прибытия
		//   с запасом, но и не в упор к цели; остановит его проверка дальности.
		if (AsdaSkillApproachOverAttackOn())
		{
			const float MinLeg = AAsdaPlayerController::kRouteArriveDist + 15.f;   // запас над допуском прибытия
			float Leg = FMath::Max(Dd - RangeU * kApproachRangeFrac, MinLeg);
			Leg = FMath::Min(Leg, FMath::Max(0.f, Dd - 30.f));                    // не входить в цель
			Stop = Cur + Dir * Leg;
		}
	}
	// renderId в ход (4006 @+25) кладём только для цели-СУЩНОСТИ — как эталонный Combat_SendNormalAttack.
	// ★ 27.09.2026 (исправление К1 ревизии хода 4007): это ПОДХОД — при эталонной схеме отрезок внутри своей клетки
	//   100×100 уходит без 4006 и идёт шагом (эталонное авто-действие шлёт подход-4006 только в чужую клетку,
	//   сервер на свою отвечает result 0; разбор — у `bRouteApproach` в AsdaPlayerController.h).
	PC->StartRoutedMove(Stop, TType == 0 ? TIdx : -1, /*bApproach=*/true);
#if ASDA_COMBAT_TRACE
	if (bTrFirstLeg)
	{
		ASDA_CTRACE(EAsdaCT::Move, TEXT("подход навыка %d: БЕГ к цели %s idx=%d — до цели %.0f, радиус %.0f, точка остановки (%.0f,%.0f)"),
			ApproachSkill, TType == 0 ? TEXT("моб") : TEXT("игрок"), TIdx, TDist, RangeU, Stop.X, Stop.Y);
	}
	else
	{
		ASDA_CTRACE_V(EAsdaCT::Move, TEXT("подход навыка %d: перестройка хода — до цели %.0f, радиус %.0f, точка (%.0f,%.0f)"),
			ApproachSkill, TDist, RangeU, Stop.X, Stop.Y);
	}
#endif
}

// Стоп перед заявкой — эталон делает это ровно перед каждым отправителем 5256.
void UAsdaSkillUseSubsystem::StopBeforeCast(int32 SkillCode)
{
	AAsdaPlayerController* PC = GetLocalPC();
	if (!PC) { return; }
	if (!PC->IsMovingNow())
	{
		// Даже стоя шлём 4008, если у нас есть НЕЗАКРЫТЫЙ ход (эталонный `g_bActionPending`):
		//   сервер снимает флаг движения ТОЛЬКО по 4008 (`CharStop@0x545C70`), а локально мы могли
		//   уже «доехать» и погасить анимацию — для сервера это ничего не значит.
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		AAsdaPlayerCharacter* Ch = GetLocalChar();
		if (Net && Ch && Net->bInGameConnected && Net->bActionPending)
		{
			const FVector2D Cur = Ch->GetGamePosXZ();
			Net->SendStop4008(Cur.X, Cur.Y);   // гейт bActionPending — как в Net_SendActionPacket
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: перед заявкой послан 4008 (был незакрытый ход)"), SkillCode);
			ASDA_CTRACE(EAsdaCT::Send, TEXT("4008 стоп перед заявкой навыка %d: стоим, но ход не закрыт (bActionPending) — поз (%.0f,%.0f)"), SkillCode, Cur.X, Cur.Y);
		}
		return;
	}
	ASDA_CTRACE(EAsdaCT::Move, TEXT("навык %d: персонаж двигался — остановка (4008) перед заявкой"), SkillCode);
	PC->StopMoveAndNotify(TEXT("остановка перед применением навыка"));
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] %d: персонаж двигался — остановлен перед заявкой (эталон 4008 → 5256)"),
		SkillCode);
}
