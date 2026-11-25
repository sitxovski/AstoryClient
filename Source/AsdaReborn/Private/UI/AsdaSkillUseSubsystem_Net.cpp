// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillUseSubsystem_Net.cpp — часть `AsdaSkillUseSubsystem.cpp`: откаты, выбор цели, сетевые события навыков, ApplyNetEvent.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaSkillUseSubsystem.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaSkillUseSubsystemInternal.h`. Шапка системы и ссылки на эталон — в `AsdaSkillUseSubsystem.cpp` и заголовке класса.

#include "UI/AsdaSkillUseSubsystem.h"
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
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaPvpRelation.h"          // ★ 26.10.2026 (bowfx): отношение к цели-персонажу (PvP, дуэль, команда, свои)
#include "AsdaSkillUseSubsystemInternal.h"   // ★ R2: общие помощники и объявления частей AsdaSkillUseSubsystem*.cpp (после включений пролога)

// ★★★ ВЗВОД КУЛДАУНА — порт `skillStartGroupCooldown@0xE57860` (`skill_buff.c:33915-33940`) ЦЕЛИКОМ.
//   Эталон, дословно:
//     v5 = learnedEntry(a2) ? *(learnedEntry + 4) : 0;           // длительность ПРИМЕНЁННОГО навыка
//     rec = skillRecord(a2);
//     if (a2 == rec->chainSkillIds[0..5])                        // код есть в СВОЁМ ЖЕ массиве
//         { skillStartCooldown(a2, v5); for (i=0..5) if (rec->chainSkillIds[i] != -1)
//                                            skillStartCooldown(chainSkillIds[i], v5); }
//     else  skillStartCooldown(a2, v5);
//   То есть ОДИН откат одной длительности ложится на ВСЮ группу. Группа берётся из `.sif +0xA7`
//   (6 × i16), и она НЕ «все уровни навыка» и НЕ «ветка дерева» — это явный список кодов в данных.
//   Непустая цепочка есть у 30 записей из 1255; клиентский `skill_info.sif` и файл порта здесь
//   совпадают побайтно. Для навыков БЕЗ цепочки поведение прежнее — откат только себе.
//   ⚠ Раньше группа не реализовывалась вовсе: применение 707 не гасило 706/708, применение 791 —
//   не гасило 803, хотя СЕРВЕР их гасит (пакет 5465 на логине приходит парой 791 = 803 = 61 с).
// ★★★ [ОРИГИНАЛ] 25.10.2026 — ДЛИТЕЛЬНОСТЬ ОТКАТА НАВЫКА 1:1 С `skillStartCooldown@0xE579E0` (бинарь IDB 13 344 768 б).
//   БЫЛО: `max(1000, (.sif CooldownMs / 1000) · 1000)` — округление до секунд и пол 1000 мс, которых в эталоне НЕТ, и
//   никаких снижений. ЭТАЛОН:
//     • длительность `a3` = запись выученного навыка +4 (`skillStartGroupCooldown@0xE57860`: `v5 = *(learnedEntry + 4)`,
//       нет записи — 0); её прислал сервер в 34-б записи 5251/6056 (GameServer `sub_4769C0`: CUser +49352 = skill_info +47 —
//       БАЗОВЫЙ откат, сервер сниженный не шлёт), у записей 5252 — `.sif +47`. В мс, БЕЗ округления;
//     • если свой персонаж есть (`CClass_GetField142C(&g_InventoryBag)`):
//         1) состояние 135 на себе (`skillCalcValueByTypeId135@0xA380C0`: `listContainsByTypeId(+638, 135, 0)`, навык записи
//            `+2` → его выученная запись → `уровень · s1PerLevel(+0x4C) + s1Base(+0x4A)`; навык не выучен — 0);
//         2) навык 930 при выученном 1027 (`0x403`): `s1Base(930) · (msBase(1027,+0x3E) + уровень·msPerLevel(1027,+0x40) / 100)`
//            (дизасм 0xE57AB1…0xE57AF9: `/100.0` — только к поуровневой части), усечение к целому;
//       каждое ненулевое снижение: `a3 −= v; if (a3 <= 1000) a3 = 1000` — пол 1000 мс ТОЛЬКО после снижения;
//     • группа `.sif +0xA7` получает длительность ПРИМЕНЁННОГО навыка, снижения — по коду каждого члена (ветка 930 — по нему).
//   Сервер (`sub_47B9C0@0x47B9C0`) снижает тем же (135, 930) и сверх того (камни/снаряжение типа 28, 756/764, 767–769) — этих
//   клиент оригинала не знает; конец такого отката синхронизирует 5274 (ключ `asda.Cooldown.Skill5274Sync`, книга навыков).
//   Ключ `asda.Cooldown.SkillEtalon`: 1 — эталон, 0 — прежнее (секунды, `.sif`, без снижений).
static ASDA_DEV_CVAR(int32, CVarCooldownSkillEtalon,
	TEXT("asda.Cooldown.SkillEtalon"),
	1,
	TEXT("[ОРИГИНАЛ skillStartCooldown@0xE579E0] Откат навыка при применении: 1 = как эталон (запись выученного +4 в мс, "
	     "снижения состояния 135 и пары 930/1027, пол 1000 мс после снижения); 0 = прежнее (.sif, округление до секунд)."),
	ECVF_Default);

// Длительность отката навыка `Code` после снижений эталона (`skillStartCooldown@0xE579E0`, тело до `uiControl_ShowWithItem`).
//   `A3` — длительность из записи применённого навыка, мс. `OutWhy` — разбор для журнала.
static int32 AsdaSkillCooldownReduced(const UGameInstance* GI, const UAsdaSkillWindowSubsystem* SW, int32 Code, int32 A3, FString& OutWhy)
{
	const UAsdaSkillDatabase* Db = SW ? SW->GetDatabase() : nullptr;
	const UAsdaEffectsSubsystem* Fx = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Db || !Db->Get(Code)) { return A3; }                       // эталон: нет записи навыка — ничего не делает
	const int32 Self = Fx ? Fx->SelfUserIdx() : -1;
	if (!Fx || Self < 0) { return A3; }                             // своего персонажа нет — снижений нет
	int32 V = 0;
	if (const FAsdaEffectEntry* E = Fx->FindByTypeSub(Self, 135, 0))
	{
		const FAsdaLearnedSkill* L = SW->FindLearned(E->IdFlag);
		const FAsdaSkillDef* R = L ? Db->Get(L->SkillCode) : nullptr;
		if (R) { V = L->Level * R->S1PerLevel + R->S1Base; }
		OutWhy += FString::Printf(TEXT(" состояние 135 (навык %d, ур. %d) −%d мс;"), E->IdFlag, L ? L->Level : 0, V);
	}
	if (V) { A3 -= V; if (A3 <= 1000) { A3 = 1000; } }
	if (Code == 930)
	{
		const FAsdaLearnedSkill* L = SW->FindLearned(1027);
		const FAsdaSkillDef* R930 = Db->Get(930);
		const FAsdaSkillDef* R1027 = Db->Get(1027);
		if (L && R930 && R1027)
		{
			V = (int32)(int64)((double)R930->S1Base * ((double)R1027->MsBase + (double)(L->Level * R1027->MsPerLevel) / 100.0));
			OutWhy += FString::Printf(TEXT(" 1027 ур. %d → −%d мс;"), L->Level, V);
			if (V) { A3 -= V; if (A3 <= 1000) { A3 = 1000; } }
		}
	}
	return A3;
}

void UAsdaSkillUseSubsystem::StartCooldown(int32 SkillCode, uint64 ArrivalSeq)
{
	// ★ Ревизия `wave_1025b_review` 1.1 [ОРИГИНАЛ] (ключ `asda.Cooldown.ArrivalOrder`, шапка в AsdaSkillWindowSubsystem.cpp):
	//   этот 5271 разбирается в тике, а 5465/5274 книга применила при приёме. Эталон всё делает синхронно, поэтому взвод,
	//   за которым пришли снимок 5465 (стёр бы запись) или 5274 навыка (сервер его уже отпустил), не применяется.
	UAsdaSkillWindowSubsystem* SWs = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSkillWindowSubsystem>() : nullptr;
	auto Superseded = [SWs, ArrivalSeq, SkillCode](int32 Id) -> bool
	{
		if (!SWs || !SWs->IsCooldownArmSuperseded(Id, ArrivalSeq)) { return false; }
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] откат навыка %d по 5271 (навык %d, приход №%llu) не взведён: после него пришли 5465 или 5274"),
			Id, SkillCode, ArrivalSeq);
		return true;
	};
	if (CVarCooldownSkillEtalon.GetValueOnGameThread() != 0)
	{
		UAsdaSkillWindowSubsystem* SW = SWs;
		const FAsdaSkillDef* D = GetDef(SkillCode);
		if (!SW || !D) { return; }
		const FAsdaLearnedSkill* L = SW->FindLearned(SkillCode);
		const int32 V5 = L ? L->CooldownMs : 0;                     // `skillStartGroupCooldown`: запись +4, нет записи — 0
		auto StartOne = [&](int32 Id)
		{
			if (Superseded(Id)) { return; }
			FString Why;
			const int32 Ms = AsdaSkillCooldownReduced(GetGameInstance(), SW, Id, V5, Why);
			// ★ Ревизия 2.1 [ОРИГИНАЛ]: откат 0 мс (нет выученной записи и нет снижения) эталон ДОПИСЫВАЕТ в вектор пустой
			//   записью (`skillStartCooldown@0xE579E0` — новый элемент, прежний не трогается), и идущий откат навыка продолжает
			//   идти. Наш `PushCooldownMs(Id, 0)` его удалял — член группы в откате «освобождался». Запись не трогаем.
			if (Ms <= 0)
			{
				UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] откат навыка %d: 0 мс (запись навыка %d +4 = %d мс) — как эталон, идущий откат не тронут"),
					Id, SkillCode, V5);
				return;
			}
			SW->PushCooldownMs(Id, Ms, Ms);
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] откат навыка %d: %d мс (запись навыка %d +4 = %d мс;%s)"),
				Id, Ms, SkillCode, V5, Why.IsEmpty() ? TEXT(" снижений нет") : *Why);
			ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: КУЛДАУН %d мс (база %d)"), Id, Ms, V5);
		};
		StartOne(SkillCode);
		// Гейт эталона: групповая ветка — только если код есть в собственном массиве `.sif +0xA7`.
		if (D->ChainSkillIds.Contains(SkillCode))
		{
			for (int32 Id : D->ChainSkillIds) { if (Id >= 0) { StartOne(Id); } }   // эталон ставит и себе повторно — итог тот же
		}
		return;
	}
	const FAsdaSkillDef* D = GetDef(SkillCode);
	if (!D || D->CooldownMs <= 0) { return; }
	// Эталон `skillStartCooldown@0xE579E0`: пол 1000 мс.
	const int32 Ms = FMath::Max(1000, (D->CooldownMs / 1000) * 1000);
	UAsdaSkillWindowSubsystem* SW = SWs;
	if (!SW) { return; }

	if (!Superseded(SkillCode)) { SW->PushCooldownFromServer(SkillCode, Ms / 1000); }
	ASDA_CTRACE(EAsdaCT::Skill, TEXT("навык %d: КУЛДАУН взведён на %d мс (по 5271 код 1)%s"), SkillCode, Ms,
		D->ChainSkillIds.Contains(SkillCode) ? TEXT(", плюс группа общего отката .sif +0xA7") : TEXT(""));

	// Гейт эталона: групповая ветка включается ТОЛЬКО если код есть в собственном массиве.
	if (!D->ChainSkillIds.Contains(SkillCode))
	{
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] кулдаун навыка %d взведён на %d мс (по ответу сервера, эталон)"),
			SkillCode, Ms);
		return;
	}
	FString GroupStr;
	for (int32 Id : D->ChainSkillIds)
	{
		if (Id == SkillCode) { continue; }        // себе уже поставлен строкой выше
		if (Superseded(Id)) { continue; }         // ★ ревизия 1.1: после этого 5271 пришли 5465/5274
		// ★ Длительность — ПРИМЕНЁННОГО навыка (эталон `skillStartCooldown(chainSkillIds[i], v5)`,
		//   где v5 снят с выученной записи `a2`), поэтому полную длительность передаём явно:
		//   у 706 собственный `.sif`-кулдаун 30 000 мс, а групповой от 707 — 8 000 мс.
		SW->PushCooldownFromServer(Id, Ms / 1000, Ms / 1000);
		GroupStr += FString::Printf(TEXT("%d "), Id);
	}
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] кулдаун навыка %d взведён на %d мс (по ответу сервера, эталон); ")
		TEXT("ГРУППА .sif +0xA7 — тот же откат ушёл на: %s"), SkillCode, Ms, *GroupStr.TrimEnd());
}

// Выбранная цель. Эталон держит её ОДНИМ индексом `g_nTargetUserIdx` (вид — `g_nInputControlState`) независимо от того,
//   моб это или игрок; в заявку при цели-персонаже кладёт `Target_GetSelectedUserIdx(targetChar)` = userNum ЦЕЛИ,
//   потому что аргумент — сам персонаж-цель (★ 27.09.2026: `Target_GetSelectedUserIdx` — миснейм IDA, = `Char_GetHoveredUserIdx@0x966490`, возвращает userNum ПЕРЕДАННОГО персонажа). В порту выбор цели ведёт `AAsdaPlayerController::TargetActor`; для моба есть
//   `NetRenderId`, а обратного поиска «актор игрока → userIdx» в срезе порта НЕТ — при цели-игроке
//   честно логируем и шлём свой индекс (как при само-касте).
bool UAsdaSkillUseSubsystem::GetSelectedTarget(int32& OutType, int32& OutIdx, FVector2D& OutGamePos, float& OutDistU) const
{
	OutType = 1; OutIdx = -1; OutGamePos = FVector2D::ZeroVector; OutDistU = 0.f;
	const AAsdaPlayerController* PC = GetLocalPC();
	const AAsdaPlayerCharacter* Ch = GetLocalChar();
	if (!PC || !Ch) { return false; }
	AActor* T = PC->TargetActor.Get();
	if (!T) { return false; }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(T))
	{
		// OutType здесь — ИНФОРМАЦИОННЫЙ (0 = моб): в саму заявку 5256 клиент эталона всегда кладёт 1,
		//   см. комментарий в `SendUse`. Значение оставлено, чтобы дампы показывали, что за цель.
		OutType = 0;
		OutIdx = M->NetRenderId;           // ★ i16 renderId, НЕ charId (закон charid-vs-usernum)
		const FVector L = M->GetActorLocation();
		OutGamePos = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1): UE → игровые координаты
		// ★★★ П10 (2026-08-11): дальность у эталона ТРЁХМЕРНАЯ. `skill_buff.c:31736` и
		//   `entity_char.c:24864` считают `Math_DistanceSquared3D(позЦели.xyz, позКастера.xyz)` и
		//   сравнивают с `rangeSq` слота `+0x574`. Порт брал `Dist2D` — на склонах, лестницах и по
		//   летающим мобам это разрешало каст там, где сервер отвечает «слишком далеко», и наоборот
		//   гнало в лишний подход. Сравнение остаётся линейным (`TDist > RangeU`) — это тождественно
		//   сравнению квадратов, потому что обе величины неотрицательны.
		OutDistU = FVector::Dist(Ch->GetActorLocation(), L);
		return true;
	}
	// ★★ ЦЕЛЬ-ИГРОК. Эталон различает вид цели глобалью `g_nInputControlState` (@0x02905598):
	//   **1 = сущность/моб** (`Entity_FindByRenderId(g_nTargetUserIdx)`), **0 = ПЕРСОНАЖ**
	//   (`CharManager_FindByUserIdx(g_nTargetUserIdx)`) — `skill_buff.c:31652/31789`,
	//   `entity_char.c:30217/30233`. Т.е. один и тот же индекс читается либо как renderId, либо как
	//   сессионный userNum. В заявку при цели-персонаже уходит userNum ЦЕЛИ
	//   (`Skill_SendSkillUseOnTarget` skill_buff.c:2768, `skillSendUseSkillPacket` :3858).
	//   Реестр `NetPlayers` (userIdx → актор) теперь читается и в обратную сторону.
	// ★★ ЦЕЛЬ — МЫ САМИ (клик по себе, правка 2026-08-25). Реестр `NetPlayers` держит ЧУЖИХ игроков,
	//   своего павна в нём нет — без этой ветки заявка ушла бы по пути «цель неизвестна».
	//   Свой номер берём напрямую: умения на дружественную цель должны накладываться на себя.
	if (T == Ch)
	{
		if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
		{
			if (Net->IsSelfUserNumKnown())
			{
				OutType = 1;                          // цель — ПЕРСОНАЖ
				OutIdx  = (int32)Net->SelfUserNum;
				OutGamePos = AsdaCoord::UeToGame2D(Ch->GetActorLocation());
				OutDistU = 0.f;                       // до себя расстояния нет
				return true;
			}
		}
	}
	if (const UAsdaWorldSubsystem* W = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
	{
		const int32 Uidx = W->FindUserIdxByActor(T);   // дубль UserIdxByActor слит 2026-08-10
		if (Uidx >= 0)
		{
			OutType = 1;                       // цель — ПЕРСОНАЖ
			OutIdx = Uidx;                     // ★ i16 userNum, НЕ charId (закон charid-vs-usernum)
			const FVector L = T->GetActorLocation();
			OutGamePos = AsdaCoord::UeToGame2D(L);   // ★ Аудит №3 (B1)
			OutDistU = FVector::Dist(Ch->GetActorLocation(), L);   // ★ П10: 3D, см. выше
			return true;
		}
	}
	static bool bWarnedPlayerTarget = false;
	if (!bWarnedPlayerTarget)
	{
		bWarnedPlayerTarget = true;
		UE_LOG(LogAsdaSkillUse, Warning, TEXT("[навыки] цель — не моб и не игрок из реестра NetPlayers: ")
			TEXT("сессионный userNum неизвестен, в заявку 5256 уйдёт свой индекс (как при само-касте)."));
	}
	return false;
}

// ══════════════════════════════ СЕТЬ ═══════════════════════════════════════════════════════════

void UAsdaSkillUseSubsystem::OnNetCastResult(int32 Result, int32 CasterUserIdx, int32 SkillCode, int32 Level, int32 /*HitCount*/)
{
	FPendingNetEvent E; E.Opcode = 5257; E.Result = Result; E.UserIdx = CasterUserIdx; E.SkillCode = SkillCode; E.Level = Level;
	NetQueue.Add(E);
	ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("5257 в очередь навыков: код %d кастер %d навык %d ур. %d (разбор — следующим тиком)"), Result, CasterUserIdx, SkillCode, Level);
}

bool UAsdaSkillUseSubsystem::HasQueuedCast(int32 CasterUserIdx, int32& OutSkill) const
{
	for (const FPendingNetEvent& E : NetQueue)
	{
		if (E.UserIdx != CasterUserIdx) { continue; }
		if ((E.Opcode == 5257 && E.Result == 1) || E.Opcode == 5270) { OutSkill = E.SkillCode; return true; }
	}
	return false;
}

void UAsdaSkillUseSubsystem::OnNetCastIdle(int32 CasterUserIdx, int32 SkillCode, int32 Level, int32 /*HitCount*/)
{
	FPendingNetEvent E; E.Opcode = 5270; E.Result = 1; E.UserIdx = CasterUserIdx; E.SkillCode = SkillCode; E.Level = Level;
	NetQueue.Add(E);
	ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("5270 в очередь навыков: кастер %d навык %d ур. %d"), CasterUserIdx, SkillCode, Level);
}

void UAsdaSkillUseSubsystem::OnNetCastState(int32 Result, int32 UserIdx, int32 SkillId, int32 Flag, bool bGuardian)
{
	if (bGuardian)
	{
		// 6161 ведёт подсистема Хранителя души (показ, нотис). ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7): своё состояние действия
		//   (25 заявки / 14 долгого каста 6160) она не снимает — разбираем здесь, следующим тиком (ApplyNetEvent, ветка 6161).
		if (AsdaSkillLatchGuardOn())
		{
			FPendingNetEvent G; G.Opcode = 6161; G.Result = Result; G.UserIdx = UserIdx; G.SkillCode = SkillId; G.Level = Flag;
			NetQueue.Add(G);
		}
		return;
	}
	FPendingNetEvent E; E.Opcode = 5271; E.Result = Result; E.UserIdx = UserIdx; E.SkillCode = SkillId; E.Level = Flag;
	E.ArrivalSeq = AsdaNextCdArrivalSeq(GetGameInstance());   // ★ ревизия 1.1: номер прихода — тот же счётчик, что у 5465/5274 книги
	NetQueue.Add(E);
	ASDA_CTRACE_V(EAsdaCT::Recv, TEXT("5271 в очередь навыков: код %d кастер %d навык %d флаг %d"), Result, UserIdx, SkillId, Flag);
}

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7): 5232 (197 б): [0] код, [1..2] userNum кастующего i16, [3..4] навык i16
//   (`Reports/soulmate_skills_recall_roulette.md` A.3). Только постановка в очередь — сеть из колбэка не трогаем.
void UAsdaSkillUseSubsystem::OnNetSoulSkillState(const TArray<uint8>& Body)
{
	if (!AsdaSkillLatchGuardOn() || Body.Num() < 5) { return; }
	FPendingNetEvent E; E.Opcode = 5232; E.Result = (int32)Body[0];
	E.UserIdx = (int32)AsdaBytes::RdI16(&Body[1]); E.SkillCode = (int32)AsdaBytes::RdI16(&Body[3]);
	NetQueue.Add(E);
}

void UAsdaSkillUseSubsystem::OnNetSoulmateState(int32 CharId, int32 UserIdx, int32 SoulIdx, int32 /*PartnerUserIdx*/)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	if ((Net->SelectedCharId != 0 && CharId == Net->SelectedCharId) || AsdaSelf::IsSelfUserIdx(Net, UserIdx))
	{
		SelfSoulIdx = SoulIdx;
	}
}

// ★ 4206 type==1: PvP-бой начался/кончился — эталонный флаг `buffFxSlots[160]` (см. шапку в .h).
//   Только запись флага, сеть из колбэка не трогается (закон no-reentrant-net-callbacks соблюдён).
void UAsdaSkillUseSubsystem::OnNetPvpMode(int32 UserIdx, bool bOn, int32 /*SubType*/)
{
	if (UserIdx != SelfUserIdx()) { return; }
	if (bSelfPvpMode == bOn) { return; }
	ASDA_CTRACE(EAsdaCT::State, TEXT("4206 PvP-режим (buffFxSlots[160]) %s — площадные навыки %s собирают персонажей"),
		bOn ? TEXT("ВКЛ") : TEXT("выкл"), bOn ? TEXT("теперь") : TEXT("больше не"));
	bSelfPvpMode = bOn;
	UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] PvP-режим (флаг buffFxSlots[160], 4206): %s — площадные ")
		TEXT("групповые баффы %s собирают целей-персонажей"),
		bOn ? TEXT("ВКЛ") : TEXT("выкл"), bOn ? TEXT("теперь") : TEXT("больше не"));
}

void UAsdaSkillUseSubsystem::OnNetSoulmatePeer(int32 CharId, int32 SoulIdx, int32 /*PartnerCharId*/)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->SelectedCharId != 0 && CharId == Net->SelectedCharId) { SelfSoulIdx = SoulIdx; }
}

// Разбор сетевого события — ровно то, что делает эталон в `Recv_SkillCast` / `Recv_SkillCastIdle` /
//   `recvOtherCharSkillUse`.
//
// ★★★★★ ЧТО ЗДЕСЬ ЛОКАЛЬНОЕ, А ЧТО ОБЩЕЕ — ПОСТРОЧНО ПО ЭТАЛОНУ (закрытие дыры §28.8).
//   Все три обработчика начинаются с `pChar = CharManager_FindByUserIdx(g_CharManager, пакет+1)` —
//   КАСТЕР БЕРЁТСЯ ИЗ ПАКЕТА, локальным он быть не обязан. Дальше признак «это я»
//   (`CClass_GetField47565` = `selectionFlag`, entity_char.c:4009) закрывает РОВНО ЭТО и ничего больше:
//     · `Recv_SkillCast@0xF4E370` (5257/6159), network.c:44838-44843:
//         `classRegistrySetFieldByType(g_pSkillWnd, skillId, 0)` — сброс иконки в ОКНЕ НАВЫКОВ;
//         network.c:44961-44964 — текст отказа `dword_15B5D78[result]` в чат;
//       а вот `pChar->effectSlots[100].field_4 = -1` при отказе (network.c:44959) стоит СНАРУЖИ
//       этого условия — показ гасится ЛЮБОМУ кастеру;
//     · `Recv_SkillCastIdle@0xF4D9C0` (5270/6160), network.c:44786 — `skillMgr_LockLookupSkill`
//       (та же блокировка иконки в окне навыков), и ВСЁ; загрузчик клипа (`packPathDispatchEntryHandler`)
//       и `CharFx_AutoSabzil_State(pChar, 14)` идут для любого кастера;
//     · `recvOtherCharSkillUse@0xF4D7D0` (5271/6161), network.c:44622 — `sub_9216EC(g_pSkillWnd, id)`
//       = ОТКАТ ГРУППЫ навыков, и network.c:44636 — текст отказа. Гашение слота показа при отказе
//       (`field_4 = -1`, network.c:44634) и при состоянии кастера 6/смерть (network.c:44615) —
//       снова СНАРУЖИ, для любого кастера.
//   Плюс сугубо портовые локальные вещи, эталонного двойника не имеющие: наш диспетчер заявок
//   (`ActiveSkill`/`ActiveSkillSentS`/`CharState`/`CastShowEndS`) и полоса применения
//   `UAsdaCastBarSubsystem` (эталонная `uiUpdateActiveSkillSlot@0xEDC480` рисует её по
//   `g_pLocalPlayer` — у соседа полосы нет в принципе).
void UAsdaSkillUseSubsystem::ApplyNetEvent(const FPendingNetEvent& E)
{
	// ★★★ «МОЙ ЛИ ЭТО ОТВЕТ». Обычно — по userIdx кастера. НО: серверный отказ, случившийся ДО
	//   `v25 = a2` (первые ветки `RecvChar_SkillUse@0x46F900`), уходит с ЕЩЁ НЕ ЗАПОЛНЕННЫМИ
	//   полями: userIdx = −1 и skillId = −1 (конструктор `sub_4742F0` ставит сентинелы).
	//   Живой лог 2026-08-09 21:02:00: `5257 body=[00 FF FF FF FF 00 FF FF]` — код 0, userIdx −1.
	//   Такой пакет `sub_4C28F0(..., 5257, 8, a2, …)` шлётся ЛИЧНО кастеру, значит он ВСЕГДА наш.
	//   Раньше проверка `E.UserIdx == SelfUserIdx()` его отбрасывала, и отказ не показывался вовсе.
	const bool bMine = (E.UserIdx == SelfUserIdx()) || (E.UserIdx == -1);
	UAsdaCastBarSubsystem* Bar = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaCastBarSubsystem>() : nullptr;

	switch (E.Opcode)
	{
	case 5257:
		++Recv5257;
		if (E.Result != 1)
		{
			// Эталон: `effectSlots[100].field_4 = −1` — ЛЮБОМУ кастеру; нотис `dword_15B5D78[result]`
			//   и сброс иконки в окне навыков — только у себя (network.c:44838-44843 / 44959-44964).
			ASDA_CTRACE(bMine ? EAsdaCT::Refuse : EAsdaCT::Peer, TEXT("5257 ОТКАЗ сервера: код %d (нотис %d), навык %d (последний отправленный %d), кастер userIdx=%d%s"),
				E.Result, UAsdaNetSubsystem::SkillFailNoticeLocId(E.Result), E.SkillCode, LastSentSkill, E.UserIdx,
				E.UserIdx == -1 ? TEXT(" (сентинел — отказ до разбора, всегда мой)") : (bMine ? TEXT(" (я)") : TEXT(" (сосед)")));
			// ★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7, ревизия п.3): у эталона слот один, и поздний отказ по ПРЕЖНЕЙ заявке там
			//   невозможен — новая заявка не уходит, пока старая не закрыта. У порта она уходит после предохранителя/сторожа, и
			//   поздний отказ по другому навыку снимал НОВУЮ заявку вместе со сторожем и гасил показ ТЕКУЩЕГО каста (а поздний
			//   успех запускал его снова). Отказ по навыку, который сейчас НЕ активен и НЕ показывается, трогает только нотис.
			//   Сентинельный отказ (навык −1, отказ до разбора) — всегда о текущей заявке.
			const bool bStaleMine = bMine && AsdaSkillLatchGuardOn() && E.SkillCode >= 0 && ActiveSkill >= 0 && ActiveSkill != E.SkillCode;
			if (bMine)
			{
				++Refused; LastRefuseCode = E.Result; LastRefuseSkill = E.SkillCode;
				if (!bStaleMine)
				{
					ActiveSkill = -1; ActiveSkillSentS = 0.0; CastShowEndS = 0.0; CharState = 0;
					if (Bar) { Bar->End(TEXT("отказ сервера (5257)")); }
				}
				else
				{
					ASDA_CTRACE(EAsdaCT::Refuse, TEXT("5257 отказ по навыку %d пришёл, когда активен %d — заявка %d и её показ не тронуты"), E.SkillCode, ActiveSkill, ActiveSkill);
				}
				PostFailNotice(E.Result, E.SkillCode >= 0 ? E.SkillCode : LastSentSkill);
			}
			// Эталон `Recv_SkillCast@0xF4E370`, ветка `LOBYTE(header[0]) != 1`:
			//   `pChar->effectSlots[100].field_4 = -1` — слот показа гасится СНАРУЖИ гейта «своё».
			//   ⚠ Сентинельный отказ (userIdx = −1) всегда наш — гасим свой показ.
			//   ★ ревизия п.3: свой показ ДРУГОГО навыка (текущий каст) поздний отказ не гасит.
			const bool bOtherShow = bMine && AsdaSkillLatchGuardOn() && E.SkillCode >= 0 && SelfShownSkill >= 0 && SelfShownSkill != E.SkillCode;
			if (!bStaleMine && !bOtherShow)
			{
				StopSkillVisual(E.UserIdx >= 0 ? E.UserIdx : SelfUserIdx(), TEXT("отказ сервера (5257)"));
			}
			return;
		}
		if (bMine)
		{
			// Успех: слот становится активным, `CChar_SetState(..., 24)` ⇒ ПОЛОСА ЗАЖИГАЕТСЯ.
			ActiveSkill = E.SkillCode; ActiveSkillSentS = 0.0;   // ответ пришёл — сторож снят
			CharState = 24;
			const FAsdaSkillCastTiming T = ResolveTiming(E.SkillCode);
			// ★ Срок выхода из состояния 24 = конец клипа показа (эталонный тик 24: obj[13] >= obj[11]).
			//   Данных о клипе нет — выходим немедленно (у эталона длительность несозданного анимобъекта 0).
			//   Нужен, потому что 5271 у мгновенных навыков приходит РАНЬШЕ 5257 (см. шапку CastShowEndS).
			const float ShowMs = FMath::Max(T.FullMs, T.Combo0Ms);
			CastShowEndS = FPlatformTime::Seconds() + AsdaCastShowSeconds(GetGameInstance(), ShowMs);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("5257 УСПЕХ: мой навык %d ур. %d ПРИНЯТ — состояние 24, клип «%s» COMBO0 %.0f мс (кадр 1-го удара), полный %.0f мс, показ %.2f с"),
				E.SkillCode, E.Level, *T.ClipName, T.Combo0Ms, T.FullMs, AsdaCastShowSeconds(GetGameInstance(), ShowMs));
			if (Bar) { Bar->Begin(E.SkillCode, T.Combo0Ms, T.FullMs, 24); }
		}
		else
		{
			ASDA_CTRACE(EAsdaCT::Peer, TEXT("5257 УСПЕХ у СОСЕДА userIdx=%d: навык %d ур. %d — показ его каста"), E.UserIdx, E.SkillCode, E.Level);
		}
		{
			// ★★★ ЗДЕСЬ ЖЕ ЗАПУСКАЕТСЯ ПОКАЗ. Эталон грузит КЛИП ПРИМЕНЕНИЯ именно в этой ветке:
			//   `Recv_SkillCast@0xF4E370` (`network.c:44930-44938`) — `packPathDispatchEntryHandler`
			//   (это МАССИВ 0 таблицы `CharSkillEffect_InitDispatchTable@0xA89510`, т.е. загрузчик)
			//   → `charSkillResetEffectSlotsAndDispatch` → `CChar_SetState(pChar, 24)`.
			//   Дальше показ ведёт ТИК (массив 5): пороги COMBO0/1/2, эффекты и звуки удара,
			//   носители снарядов — всё это `UAsdaSkillFxSubsystem`.
			StartSkillVisual(E.UserIdx, E.SkillCode);   // ★★★ кастер — ИЗ ПАКЕТА, не «я» (см. шапку функции)
		}
		return;

	case 5270:
		++Recv5270;
		if (!bMine)
		{
			ASDA_CTRACE(EAsdaCT::Peer, TEXT("5270 у СОСЕДА userIdx=%d: навык %d ур. %d (каст-ожидание) — показ его каста"), E.UserIdx, E.SkillCode, E.Level);
		}
		if (bMine)
		{
			// Эталон: заводит слот, `field_56C = 0` («ждём сервер»), состояние 14.
			ActiveSkill = E.SkillCode; ActiveSkillSentS = 0.0;   // ответ пришёл — сторож снят
			CharState = 14;
			const FAsdaSkillCastTiming T = ResolveTiming(E.SkillCode);
			ASDA_CTRACE(EAsdaCT::Recv, TEXT("5270 КАСТ-ОЖИДАНИЕ: мой навык %d ур. %d — состояние 14 (ждём 5271), клип «%s» COMBO0 %.0f мс, полный %.0f мс"),
				E.SkillCode, E.Level, *T.ClipName, T.Combo0Ms, T.FullMs);
			if (Bar) { Bar->Begin(E.SkillCode, T.Combo0Ms, T.FullMs, 14); }
		}
		{
			// ★★★ ПОКАЗ ЗАПУСКАЕТСЯ И ЗДЕСЬ (симптом живого теста 2026-08-10: «Зазубренная стрела»,
			//   код 787, не проигрывает НИ анимации, НИ эффекта). Прежде `StartSkillVisual` звала
			//   только ветка 5257, и все навыки подтипов 1/7 (ответ 5270) оставались немыми —
			//   это и был честно записанный открытый хвост §19.8 п.1 отчёта.
			//   ДОКАЗАТЕЛЬСТВО, ЧТО ЭТАЛОН ГРУЗИТ КЛИП И В ЭТОЙ ВЕТКЕ — тело `Recv_SkillCastIdle@0xF4D9C0`
			//   (`RE/etalon/network.c:44786-44797`), последние строки:
			//     `pChar->effectSlots[100].field_4 = packPathDispatchEntryHandler(&g_pSkillInfoTable, pChar,
			//                                        hdr[1], hdr[2], SkillInfo[273*id+70], v3);`
			//     `pChar->effectSlots[…].field_56C = 0;`
			//     `CharFx_AutoSabzil_State(pChar, 14, 0, 0);`        // ← отличие от 5257 только в СОСТОЯНИИ
			//     `charSkillResetEffectSlotsAndDispatch(pChar, skillFlags, recBuf);`
			//   `packPathDispatchEntryHandler@0xA81000` — тот же МАССИВ 0 таблицы
			//   `CharSkillEffect_InitDispatchTable@0xA89510`, т.е. ТОТ ЖЕ загрузчик клипа и эффектов,
			//   что и в `Recv_SkillCast@0xF4E370`. Разница веток — состояние 14 против 24 (и то, что
			//   слот ждёт подтверждения сервера), а не наличие показа.
			//   Проверка на данных: у 787 в `DT_SkillClips` есть строка (клип `ch4_m_07_focus_01.JBB`,
			//   anim_179, COMBO0 400 мс, звук каста `S_c_SPskill047.wav`) — в живом логе она уже
			//   находилась полосой применения, но показ по ней никто не ставил.
			// ★ 06.11.2026 [ОРИГИНАЛ] (`asda.Skill.CastCharge`): показ от 5270 — удар ждёт конца заряда 13/27 (разбор у ключа).
			AsdaSkillFxSetShowFromCastIdle(true);
			StartSkillVisual(E.UserIdx, E.SkillCode);   // ★★★ кастер — ИЗ ПАКЕТА, не «я» (см. шапку функции)
			AsdaSkillFxSetShowFromCastIdle(false);
		}
		return;

	case 5271:
		++Recv5271;
		// ★★★ ЭТАЛОН НЕ ФИЛЬТРУЕТ ЭТОТ ПАКЕТ ПО «СВОЕМУ» ИГРОКУ (было `if (!bMine) return;` — из-за
		//   него у соседа не гасился показ ни при отказе, ни при его смерти).
		//   `recvOtherCharSkillUse@0xF4D7D0` (network.c:44611-44639) целиком:
		//     pChar = CharManager_FindByUserIdx(...);                        // любой кастер
		//     if (pChar->effectSlots[112].field_8 == 6) { field_4 = -1; return; }   // СМЕРТЬ — любому
		//     if (*a1 == 1) { if (локальный) { откат группы + красный нотис } }
		//     else          { field_4 = -1;  if (локальный) { текст отказа } }      // гашение — любому
		//   Значит локальны РОВНО две вещи: откат группы навыков (`sub_9216EC` = окно навыков) и
		//   текст отказа. Гашение показа — общее.
		{
			// Ветка `effectSlots[112].field_8 == 6` (network.c:44615): кастер МЁРТВ ⇒ показ снимается
			//   и обработчик выходит — тоже без гейта «свой». Порт: состояние `Dead` павна.
			//   ⚠ РАННЕГО RETURN здесь НЕ делаем: у эталона за этой строкой ничего нашего не стоит, а
			//   у порта дальше идёт СВОЙ диспетчер заявок (`ActiveSkill`/`CharState`/полоса), который
			//   иначе завис бы на мёртвом персонаже до истечения `CastShowEndS`. Показ гасим здесь,
			//   локальную уборку оставляем общему пути ниже.
			const AAsdaPlayerCharacter* CasterCh = CharByUserIdx(E.UserIdx);
			if (CasterCh && CasterCh->IsIncapacitated())
			{
				ASDA_CTRACE(bMine ? EAsdaCT::Skill : EAsdaCT::Peer, TEXT("5271: кастер userIdx=%d недееспособен (смерть) — показ навыка %d снят"), E.UserIdx, E.SkillCode);
				StopSkillVisual(E.UserIdx >= 0 ? E.UserIdx : SelfUserIdx(), TEXT("кастер мёртв (5271)"));
			}
		}
		if (E.Result != 1)
		{
			// Отказ: показ гасится ЛЮБОМУ кастеру; наши счётчики и нотис — только своему.
			//   ★ 31.10.2026 (баг 7, ревизия п.3): свой показ ДРУГОГО навыка (текущий каст) поздний отказ не гасит.
			const bool bOtherShow = bMine && AsdaSkillLatchGuardOn() && E.SkillCode >= 0 && SelfShownSkill >= 0 && SelfShownSkill != E.SkillCode;
			if (!bOtherShow) { StopSkillVisual(E.UserIdx >= 0 ? E.UserIdx : SelfUserIdx(), TEXT("отказ (5271)")); }
		}
		ASDA_CTRACE(bMine ? (E.Result == 1 ? EAsdaCT::Recv : EAsdaCT::Refuse) : EAsdaCT::Peer,
			TEXT("5271 %s: навык %d кастер userIdx=%d%s, флаг %d%s"),
			E.Result == 1 ? TEXT("КОНЕЦ КАСТА (успех)") : *FString::Printf(TEXT("ОТКАЗ код %d (нотис %d)"), E.Result, UAsdaNetSubsystem::SkillFailNoticeLocId(E.Result)),
			E.SkillCode, E.UserIdx, bMine ? TEXT(" (я)") : TEXT(" (сосед)"), E.Level,
			bMine ? TEXT(" — полоса и активный слот гасятся, состояние → 0") : TEXT(""));
		if (!bMine) { return; }   // дальше — исключительно локальные вещи (см. разбор выше)
		if (E.Result == 1)
		{
			// ★ ИМЕННО ЗДЕСЬ эталон взводит кулдаун: `recvOtherCharSkillUse@0xF4D7D0`
			//   (network.c:44622-44624) — `if (result == 1 && локальный игрок) sub_9216EC(skillId)`
			//   = `skillStartGroupCooldown@0xE57860`. Не на отправке.
			StartCooldown(E.SkillCode, E.ArrivalSeq);   // ★ ревизия 1.1: номер прихода 5271
		}
		else
		{
			++Refused; LastRefuseCode = E.Result; LastRefuseSkill = E.SkillCode;
			PostFailNotice(E.Result, E.SkillCode);
			// ★ ПОКАЗ ГАСИТСЯ ТОЛЬКО ПРИ ОТКАЗЕ. `recvOtherCharSkillUse@0xF4D7D0`
			//   (`network.c:44618-44639`) обнуляет `effectSlots[100].field_4` в ветке `*a1 != 1`
			//   (и ещё при состоянии персонажа 6 = смерть); при `*a1 == 1` слот НЕ трогается —
			//   клип и его пороги доигрывают сами. Поэтому успешный 5271 показ не прерывает:
			//   иначе COMBO0 (400…2133 мс) не успевал бы отработать и удар пропадал.
			//   (Само гашение выполнено выше — до гейта «своё», как в эталоне.)
		}
		// Поле каста персонажа: полоса и активный слот гасятся в любом случае (см. выше — сам
		//   ПОКАЗ при успехе продолжается до конца клипа).
		//   ⚠ Порядок пакетов у мгновенных навыков ОБРАТНЫЙ (5271 раньше 5257) — тогда состояние 24
		//   ставит ПОСЛЕДУЮЩИЙ 5257, а снимает его выход по концу клипа (CastShowEndS, Tick).
		// ★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7, ревизия п.1): эталонный `recvOtherCharSkillUse` при успехе вообще не трогает ни
		//   состояние, ни слот (только откат группы), при отказе гасит слот своего же каста. Поздний 5271 СТАРОГО навыка
		//   снимал у порта НОВУЮ заявку вместе со сторожем «ответа нет» — снимаем свой диспетчер только для этого навыка
		//   (или когда активного нет).
		if (AsdaSkillLatchGuardOn() && ActiveSkill >= 0 && E.SkillCode >= 0 && ActiveSkill != E.SkillCode)
		{
			ASDA_CTRACE(EAsdaCT::Skill, TEXT("5271 по навыку %d пришёл, когда активен %d — заявка %d не тронута"), E.SkillCode, ActiveSkill, ActiveSkill);
			return;
		}
		ActiveSkill = -1; ActiveSkillSentS = 0.0; CastShowEndS = 0.0;
		CharState = 0;
		if (Bar) { Bar->End(E.Result == 1 ? TEXT("конец каста (5271)") : TEXT("отказ (5271)")); }
		return;

	case 6161:
		// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7): 6161 Хранителя = 5271 обычного навыка (`recvOtherCharSkillUse@0xF4D7D0`, одна
		//   функция эталона). Сервер (`RecvBattle_SoulGuarderSkillUse`) шлёт 6161 ПЕРВЫМ, следом 6159/6160. Снимаем только
		//   СВОЁ состояние и только для этого навыка (или без активного навыка): отказ — заявка 25 / каст 14 закрыты;
		//   успех — закрыт только долгий каст 14 (как 5271); при успехе мгновенного навыка 25 снимет следующий 6159.
		if (!bMine) { return; }
		if ((CharState == 25 || CharState == 14) && (ActiveSkill == E.SkillCode || ActiveSkill < 0)
			&& (E.Result != 1 || CharState == 14))
		{
			ASDA_CTRACE(EAsdaCT::Refuse, TEXT("6161 %s: мой навык Хранителя %d — состояние %d → 0, активный навык снят"),
				E.Result == 1 ? TEXT("конец долгого каста") : *FString::Printf(TEXT("ОТКАЗ код %d"), E.Result), E.SkillCode, CharState);
			UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] 6161 %s по навыку Хранителя %d — состояние %d → 0"),
				E.Result == 1 ? TEXT("конец каста") : TEXT("отказ"), E.SkillCode, CharState);
			ActiveSkill = -1; ActiveSkillSentS = 0.0; CastShowEndS = 0.0; CharState = 0;
			if (Bar) { Bar->End(E.Result == 1 ? TEXT("конец каста (6161)") : TEXT("отказ (6161)")); }
		}
		return;

	case 5232:
		// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] (баг 7): ответ на навык соулмейта 5231. Эталон `net_handleSoulmateSkillState`
		//   (network.c:45393) кастеру при отказе ставит состояние 0 (каждая ветка `case 0/2..0x10`), при успехе — 24, а тик 24
		//   при пустом активном слоте сразу выходит в 0 (entity_char.c, `case 0x18`). У порта 5231 ставит 25 без активного
		//   навыка — снимаем его здесь в обоих исходах.
		//   ★ ревизия п.2: ответ сверяется с ЗАЯВКОЙ 5231 (`SoulmatePendingSkill`), а не с «нет активного навыка» — у эталона
		//   5231 и обычная заявка одновременно не живут (гейт состояний в `skillCastActivate`), а 5232 снимает свою.
		if (E.UserIdx != SelfUserIdx() || SoulmatePendingSkill < 0) { return; }
		ASDA_CTRACE(EAsdaCT::Recv, TEXT("5232 навык соулмейта %d (заявка %d): код %d — состояние %d → 0"), E.SkillCode, SoulmatePendingSkill, E.Result, CharState);
		UE_LOG(LogAsdaSkillUse, Log, TEXT("[навыки] 5232 навык соулмейта %d: код %d — заявка закрыта, состояние %d → 0"), E.SkillCode, E.Result, CharState);
		SoulmatePendingSkill = -1;
		if (CharState == 25) { CharState = 0; }
		return;

	default:
		return;
	}
}
