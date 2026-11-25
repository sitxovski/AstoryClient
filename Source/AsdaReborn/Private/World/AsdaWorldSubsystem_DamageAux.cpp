// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_DamageAux.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Damage.cpp, дословный перенос): вспомогательные пакеты боя: OnCombatHitAux, стеки бафов моба, OnTargetHp.
//   Общее для частей — в AsdaWorldSubsystem_Damage_Private.h; шапка и реверс — в AsdaWorldSubsystem_Damage.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
// ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)   ★ R3: комментарий снятого включения "UI/AsdaMessageSubsystem.h" (файл им не пользовался) — сохранён
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Damage_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Damage*.cpp

// ── ★ 4017 (эталон Recv_CombatHitAux@0xF33B60): @0 mobType, @2 rid, @4 flags:
//   flags==0 → СМЕРТЬ (state≠10 → SetActionState(10) + HP=0) — ОСНОВНОЙ канал смерти моба;
//   flags&4 → спавн/снап на тайл @96/@98 + HP @92 + state 1; flags&2 → только бафф-стеки;
//   прочее → HP @92 при state 1/2/7 + СИНХРОНИЗАЦИЯ СПИСКА СОСТОЯНИЙ (28 id @8 + 28 глубин @64,
//   `Skill_Buff_RefreshStackDepth@0xF84090`): совпавшим обновляется стек, НЕ совпавшие снимаются. ──
void UAsdaWorldSubsystem::OnCombatHitAux(const FAsdaCombatHitAux& A)
{
	// Локальные имена сохранены — тело ветвится ровно так же, как раньше; структура добавила только
	//   списки состояний (`A.BuffIds` / `A.BuffLevels`), которых прежний делегат не доносил вовсе.
	const int32 Rid = A.Rid;
	const uint32 Flags = A.Flags;
	const int32 Hp = A.Hp;
	const FIntPoint Tile = A.Tile;
	const int32 MobType = A.MobType;
	TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(Rid);
	AAsdaMobActor* M = Ex ? Ex->Get() : nullptr;
	// ★ Журнал боя: приём 4017 (частый канал: уровень по цели цикла; смерть flags=0 — всегда уровень 1 ниже).
	ASDA_CTRACE_MOB(Rid, EAsdaCT::Recv, TEXT("4017 %s: flags=0x%X HP@92=%d клетка (%d,%d)%s, полоса %d/%d"),
		*AsdaCtWorld::Mob(Rid, M), Flags, Hp, Tile.X, Tile.Y, !M ? TEXT(" — моба нет") : (M->IsDead() ? TEXT(" — труп") : TEXT("")),
		M ? M->CurHp : -1, M ? M->MaxHp : -1);
	if (Flags == 0)
	{
		if (M && !M->IsDead())
		{
			// ══ ★★★ ПОЧЕМУ ЗДЕСЬ БОЛЬШЕ НЕТ ПРИДЕРЖИВАЮЩЕГО ГЕЙТА (§28, 2026-08-11) ═══════════════
			//   Гейт §26.4/§27.7 держал смерть до НАШЕГО кадра удара. Он выключен по умолчанию:
			//   кадр удара существует только там, где идёт ПОКАЗ навыка, а показ в порту запускается
			//   исключительно для локального игрока (`StartSkillVisual` → `GetLocalChar()`). ★ ПОПРАВКА §29 (2026-08-11): показ ЧУЖОГО каста добавлен — `StartSkillVisual` берёт кастера ИЗ ПАКЕТА, и кадр удара теперь есть у наблюдателя тоже. Гейт всё равно оставлен ВЫКЛЮЧЕННЫМ по умолчанию: он привязан к ЛОКАЛЬНОМУ показу, а показ может не запуститься по причинам, которых у эталона нет (сосед ещё не заспавнен, у навыка нет строки в `DT_SkillClips`, клип не импортирован) — и тогда рассинхрон вернулся бы точечно. Смерть по данным одинакова у всех БЕЗУСЛОВНО.
			//   Следствие: кастер клал труп на 1767 мс (навык 1243), сосед-наблюдатель — на 0.35 с
			//   (сторож полки `kCastHitsGraceS`). Разброс до ~1.4 с по одному мобу = рассинхрон,
			//   которого У ЭТАЛОНА НЕТ:
			//     • записи урона рассылает `SendPacket_ClippingSectorInAllUser@0x41E4F0` (GameServer
			//       `sub_52E630`) — их получает ВЕСЬ сектор, а не только кастер;
			//     • `Recv_SkillCast@0xF4E370` ищет кастера через `CharManager_FindByUserIdx` и
			//       запускает показ у ЛЮБОГО персонажа, включая соседа;
			//     • тик удара (`CSkillCast_UpdateMeleeHit@0xA64BB0`,
			//       `SkillEffect_UpdateScaledStrikeHit@0xA72610`) крутится у любого кастера: ветка
			//       локального игрока там отличается только вспышкой экрана и `noop_b(909)`.
			//   ⇒ И цифра, и локальное вычитание урона (`CClass_SubtractStatClamped@0xF8A4F0`,
			//   кламп в 0), и смерть по `entityListUpdateFade@0xF78660` наступают у ВСЕХ клиентов в
			//   один и тот же момент. «Умер раньше вспышки» у оригинала есть, но ОДИНАКОВО у всех.
			//   Закон `fix-original-defects` разрешает оставлять исправление, только если оно не
			//   создаёт рассинхрона между клиентами — это создавало. Ключ отката сохранён:
			//   `asda.Skill.HoldMobDeath 1` возвращает прежнее поведение целиком.
			// ★ Взамен порт получил ЭТАЛОННУЮ реакцию на смертельную запись — метку «обречён»
			//   (`MarkDoomedByCastRecs`, бит 0x20): новых замахов по такой цели боевой цикл не
			//   начинает, подсветка снимается. Именно этим оригинал и «разводит» кил и труп.
			// ★★★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА (закон fix-original-defects), ключ — `asda.Skill.HoldMobDeath 1`.
			//   Сервер считает попадание навыка мгновенно и валит цель ДО кадра удара: живой замер
			//   2026-08-11 09:20:46 — площадной скилл хранителя 1243, ответ 6159 в :46.597 (две записи,
			//   у обеих HpAfter=0), кадр удара COMBO0 в :48.527, а 4017 flags=0 по побочной цели
			//   rid=232 пришёл в :47.679 — моб умирал за 0.85 с ДО удара («один моб сразу помер»).
			//   Эталон эту рассинхронизацию не лечит (`Recv_CombatHitAux@0xF33B60`: flags==0 →
			//   `SetActionState(entity, 10)` безусловно, а покадровый `entityListUpdateFade@0xF78660`
			//   валит и по `i[284] <= 0`), но вся архитектура парковки урона в порту существует ровно
			//   ради совпадения «цифра ↔ кадр удара» — смерть обязана идти с ними в одном кадре.
			//   Держим ТОЛЬКО пока висит уже принятая смертельная запись каста (`HpAfter <= 0`): её
			//   выпуск ГАРАНТИРОВАН одним из пяти путей (порог фазы, прилёт носителя, конец клипа,
			//   снятие показа, сторож полки 0.35 с), и `ApplySkillHit` пометит труп сам.
			// ★★★ ПОЧЕМУ ГЕЙТ НЕЛЬЗЯ УБРАТЬ (доказано по `NpcServer.exe`, отчёт §27). Сервер валит
			//   цели ВРАЗНОБОЙ, и обе задержки ВШИТЫ В КОД боевого процесса:
			//     • моб УЖЕ В БОЮ (`mon+34368 == 0`) — `CMonster::OnDamaged@0x4423C0` обнуляет HP
			//       СРАЗУ и ставит `mon+36722 = GetTickCount()`; `CMonster::ProcessState@0x448350`
			//       выпускает смерть только когда `now − mon+36722 >= 1000` — ВШИТАЯ 1000 мс;
			//     • моб МИРНЫЙ (`mon+34368 == 1`, «первый удар») — урон копится в `mon+34376`, HP не
			//       трогается, и сливается только при `now − mon+34372 >= 2000` — ВШИТАЯ 2000 мс;
			//       `mon+36722` при этом НЕ обновляется, поэтому смерть выходит тем же тиком.
			//   Живой замер 2026-08-11 09:20 подтверждает обе ветки до миллисекунд: rid=232
			//   (`4017 flags=0x3` = бит «в бою» стоит) — HP=0 в :46.642, `flags=0` в :47.679 (+1037 мс);
			//   rid=328 (`4017 flags=0x1` = бита боя нет, hp=108 через 118 мс после ответа) —
			//   `flags=0` в :48.622 (+2025 мс от ответа). Разброс 0.94 с = 2000 − 1000 мс.
			//   ⇒ Клиент НИКОГДА не получит смерть в кадре удара. Но и оригинал её туда не тянет:
			//   он просто валит цель по данным, ОДИНАКОВО у всех клиентов сектора (см. блок §28 выше).
			if (IsMobDeathHeldByPendingHit(Rid))
			{
				UE_LOG(LogAsdaWorld, Log,
					TEXT("[net] смерть моба rid=%d '%s' ОТЛОЖЕНА до кадра удара: по нему уже припаркована ")
					TEXT("смертельная запись каста (HP после ≤ 0) — режим asda.Skill.HoldMobDeath 1 ")
					TEXT("(⚠ рассинхрон с другими игроками)"), Rid, *M->MobName);
				ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("4017 flags=0 %s: смерть ПРИДЕРЖАНА до кадра удара навыка (asda.Skill.HoldMobDeath 1)"), *AsdaCtWorld::Mob(Rid, M));
				return;
			}
			// ★ Эталон здесь снимает метку «обречён» ПЕРЕД смертью: `entity[121] &= ~0x20u`
			//   (`Recv_CombatHitAux@0xF33B60`, network.c:38476) — и только потом `SetActionState(10)`.
			// ★ 17.10.2026: при НАШЕМ замахе по этой цели в полёте смерть ждёт попадания (разбор у `ArmOurImpactWindow`):
			//   сервер отпускает её по вшитым 1000/2000 мс, а выстрел после полного натяжения приходится позже.
			if (LedgerDeferDeath(M, TEXT("4017 flags=0"))) { return; }
			M->bServerDead = false;
			UE_LOG(LogAsdaWorld, Log, TEXT("[net] смерть моба rid=%d '%s' (4017 flags=0 — серверный канал смерти; труп: клип → фейд → удаление)"), Rid, *M->MobName);
			ASDA_CTRACE_MOB(Rid, EAsdaCT::Death, TEXT("4017 flags=0 %s: СМЕРТЬ применена сразу (серверный канал; лента не отложила), HP полосы было %d/%d → NetDeath%s"),
				*AsdaCtWorld::Mob(Rid, M), M->CurHp, M->MaxHp, (Rid == AsdaCombatTrace::CurrentCycleTarget()) ? TEXT(", цель цикла") : TEXT(""));
			if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
			{
				if (PC->TargetActor.Get() == M) { PC->ClearTarget(); }
			}
			M->NetDeath();
			ASDA_CTRACE_END_IF_TARGET(Rid, TEXT("цель умерла (4017 flags=0)"));
		}
		return;
	}
	// ★★★ ЭТАЛОН ПРИ flags&4 СПАВНИТ НЕДОСТАЮЩЕГО (Recv_CombatHitAux@0xF33B60: `if (!entity) {
	//   IndexById = mobTableFindIndexById(g_pMobMgr, *(__int16*)a3); entity = mobListSpawnAndLink(...); }`
	//   — и дальше ставит позицию из тайла @96/@98, HP @92 и SetActionState(entity, 1)).
	//   Это ТРЕТИЙ канал спауна моба, и после введения удаления трупа он реально нужен: труп к моменту
	//   респавна уже удалён, сущности нет, и без создания моб бы просто не появился.
	if (!M && (Flags & 4) != 0 && Hp > 0 && Tile.X >= 0 && MobType > 0)
	{
		M = CreateNetMob(Rid, MobType, Hp, Tile, Tile, /*FaceDeg=*/0, /*Speed=*/0.f, /*StateByte=*/1, TEXT("4017"));
		if (M)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[моб^] 4017 flags&4 rid=%d '%s' hp=%d — СОЗДАН заново в клетке пакета (%d,%d) (эталон mobListSpawnAndLink)"),
				Rid, *M->MobName, Hp, Tile.X, Tile.Y);
		}
		return;
	}
	if (!M) { return; }
	if (Flags & 4)
	{
		// ★ Респавн/реген: прежний прогноз HP по этому rid недействителен — сервер выдал новое HP.
		MobHpForecast.Remove(Rid);
		// ★ РЕГЕН С ЯВНОЙ ПОЗИЦИЕЙ (эталон Recv_CombatHitAux@0xF33B60, ветка flags&4): сущности нет —
		//   создать; есть (в т.ч. ТРУП) — снап на тайл @96/@98, HP=@92 и `SetActionState(entity,1)`,
		//   т.е. моб ОЖИВАЕТ. Без оживления NetStop/HP на трупе глохли (гейт bDead).
		// ★ КРУГ 9: труп, чей ПОСМЕРТНЫЙ фейд уже начался, НЕ оживляем — эталонный флаг 4 не отменяется
		//   ничем (charUpdateActionState23@0xF844F0 доводит до 0x10 → charRemoveEntity независимо от
		//   SetActionState/HP); сущность исчезнет, эталон в этом случае моба до следующего 4013/4019 не
		//   пересоздаёт. Окно коллизии практически пусто: реген идёт через ~5с, труп удалён на ~2.5с.
		// ★ Ревизия 17.10, п.7: реген при смерти, отложенной окном «наш замах в полёте», — смерть отменяется (иначе в конце
		//   окна `NetDeath` убил бы возрождённого моба). HP ниже пишется как обычно.
		CancelDeferredDeathOnRevive(M, Hp, TEXT("4017 flags&4"), /*bSureRevive=*/true);
		if (M->IsDead() && Hp > 0 && M->GetFadeKind() != EAsdaMobFade::Corpse)
		{
			M->NetRevive(Hp);
			ForgetLethalMark(Rid);   // ★ ревизия 18.10: новая жизнь — метка смертельного удара прошлой смерти недействительна
			UE_LOG(LogAsdaWorld, Log, TEXT("[моб^] 4017 РЕГЕН rid=%d '%s' hp=%d тайл пакета=(%d,%d) — оживлён + снап (эталон)"),
				Rid, *M->MobName, Hp, Tile.X, Tile.Y);
			ASDA_CTRACE_MOB(Rid, EAsdaCT::Corpse, TEXT("4017 flags&4 %s: ТРУП ОЖИВЛЁН регеном (HP %d), снап на клетку (%d,%d)"), *AsdaCtWorld::Mob(Rid, M), Hp, Tile.X, Tile.Y);
		}
		if (Hp > 0)
		{
			ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("4017 flags&4 %s: реген/респаун — HP полосы %d → %d / %d"), *AsdaCtWorld::Mob(Rid, M), M->CurHp, Hp, ((AAsdaMobActor::IsMaxHpFixed() && M->MobTypeMaxHp > 0) ? M->MaxHp : FMath::Max(M->MaxHp, Hp)));
			M->CurHp = Hp; M->ApplyPacketHpToMax(Hp, TEXT("4017 flags&4"), /*bSpawnChan=*/true); UpdateMobHpBar(M);   // ★ 18.10.2026: ключ asda.Combat.MobMaxHpFixed
		}
		if (Tile.X >= 0)
		{
			const FVector Was = M->GetActorLocation();
			// ★ Эталон Recv_CombatHitAux@0xF33B60 (flags&4) зовёт playerSetWorldPosition БЕЗУСЛОВНО —
				//   порога 150 тут нет (он только в 4015 kind=1). Раньше здесь стоял NetStop; после
				//   приведения NetStop к эталону у него появился порог, и реген-снап перестал бы работать.
				M->NetSnapPos(AsdaCoord::TileCenterGame(Tile));   // ★ Аудит №3 (B2)
				M->NetHalt();   // SetActionState(entity, 1) — «стоять» (та же ветка эталона)
			const FVector Now = M->GetActorLocation();
			if (!Was.Equals(Now, 1.f))
			{
				UE_LOG(LogAsdaWorld, Log, TEXT("[моб→] 4017 flags&4 rid=%d '%s' СНАП: (%.0f,%.0f) → (%.0f,%.0f) тайл пакета=(%d,%d)"),
					Rid, *M->MobName, Was.X, Was.Y, Now.X, Now.Y, Tile.X, Tile.Y);
			}
		}
		return;
	}
	// ★★★ СБРОС ФЛАГА «В БОЮ» (исправлено 2026-08-11, B9 перепроверки). Эталон
	//   `Recv_CombatHitAux@0xF33B60`, ветка «прочие флаги» (network.c:38464-38470):
	//       actionState = Entity_GetActionState(entity);
	//       if (actionState == 1 || actionState == 2 || actionState == 7) {
	//           *((_WORD *)entity + 237) = 0;                  // ← ЭТОГО В ПОРТУ НЕ БЫЛО
	//           entity[284] = entity[283] = HP@92;
	//       }
	//       Skill_Buff_RefreshStackDepth(...);
	//   Без сброса моб оставался в боевой стойке там, где оригинал из неё выходит (а заодно у 4019
	//   продолжал глушиться эталонный гейт траектории `!ent[237]`).
	// ⚠ Гейта `!(Flags & 2)` здесь НЕТ — сознательно, тем же основанием, что и запись HP ниже: на этом
	//   сервере не-смертельный/не-респавновый 4017 приходит с `flags = 0x3` (бит 2 стоит), и эталон
	//   ушёл бы в ветку «только баффы», потому что HP у него несёт отсутствующий здесь 4027. Порт
	//   трактует весь этот хвост как эталонную ветку «прочие флаги» целиком — иначе правка была бы
	//   мёртвым кодом. Отступление ровно одно и то же, и оно уже задокументировано.
	const int32 St = M->GetEtalonActionState();
	if (St == 1 || St == 2 || St == 7) { M->SetCombatStance(false); }
	// ★ 15.10.2026 (волна mobpos_swing): живая клетка NpcServer (@96/@98, код 67 `sub_425D40`) — сверка видимой позиции.
	CorrectMobToServerCell(M, Tile, TEXT("4017"));
	// ★ HP берём при ЛЮБЫХ флагах и в ЛЮБОМ состоянии (осознанное отступление: эталон при flags&2
	//   читает только баффы, т.к. у него HP приходит пакетом 4027 — на этом сервере 4027 отсутствует,
	//   и 4017 flags=0x3 остаётся единственным источником HP по удару; живой лог 11:54: rid=1
	//   flags=0x3 hp=20).
	// ★ 17.10.2026: при НАШЕМ замахе по этой цели в полёте HP ждёт попадания (применится в конце окна) — иначе полоса
	//   уходила вниз, пока персонаж ещё натягивает оружие (разбор у `ArmOurImpactWindow`).
	NoteMobHpFloor(M, Hp, /*bFrom4029=*/false, TEXT("4017"));   // ★ 18.10.2026 (пол HP): 4017 полом не является — только опускает
	if (!LedgerHoldServerHp(M, Hp, TEXT("4017"), /*bStaleChecked=*/false, /*bAllowNumber=*/true, /*bRegenChan=*/true))
	{
		if (Hp != M->CurHp)
		{
			ASDA_CTRACE_MOB(Rid, EAsdaCT::HpPlate, TEXT("4017 %s: HP=%d — лента не держит, применяется сразу (фильтр прогноза — в ApplyMobHp)"), *AsdaCtWorld::Mob(Rid, M), Hp);
		}
		ApplyMobHp(M, Hp);
	}
	// ★★★ Д6: СИНХРОНИЗАЦИЯ СПИСКА СОСТОЯНИЙ (`Skill_Buff_RefreshStackDepth@0xF84090`). Эталон зовёт
	//   её в ОБЕИХ ветках, которые сюда сходятся, — и `flags&2` («только баффы»), и «прочие флаги»
	//   (game.c:38471 хвост). В ветке `flags&4` эталон её НЕ зовёт — там мы уже вышли по `return`.
	RefreshMobBuffStacks(Rid, A.BuffIds, A.BuffLevels);
}

void UAsdaWorldSubsystem::RefreshMobBuffStacks(int32 Rid, const TArray<int32>& BuffIds,
                                               const TArray<int32>& BuffLevels)
{
	if (BuffIds.Num() != 28 || BuffLevels.Num() != 28) { return; }   // тело короче 92 б — списка нет
	UAsdaEffectsSubsystem* Fx = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr;
	if (!Fx) { return; }
	// ★ 04.11.2026 (status_effect_expiry): remember the server list and which states just left it — BEFORE the empty-list exit:
	//   a state can be applied and ended on the server while the client has not shown its hit frame yet (list still empty here).
	Fx->NoteMobServerStateList(Rid, BuffIds);
	// ★ 06.11.2026 [ИСПРАВЛЕНИЕ]: усиление из списка, которого нет на клиенте (иммунитет Spade и т.п.) — разбор у `asda.Buff.MobListBuffs`.
	AsdaMobAddListedBuffs(Fx, Rid, BuffIds, BuffLevels, /*bSpawnRecord=*/false);
	const TArray<FAsdaEffectEntry>& List = Fx->GetMobList(Rid);
	if (List.Num() == 0) { return; }
	TArray<int32> ToRemove;
	for (const FAsdaEffectEntry& E : List)
	{
		if (E.Type != 0) { continue; }   // эталонный список сущности хранит только состояния (type 0)
		bool bFound = false;
		for (int32 i = 0; i < 28; ++i)
		{
			if (E.Id == BuffIds[i])
			{
				// ⚠ const_cast: мутатора «обновить только глубину стека» у модели эффектов нет, а сама
				//   модель лежит в `Private/UI` — файлах параллельного агента. Правка ровно одного
				//   поля записи, ровно как `*(_BYTE *)(buffRec + 22) = levels[i]` у эталона.
				FAsdaEffectEntry& W = const_cast<FAsdaEffectEntry&>(E);
				W.TargetStack = BuffLevels[i];   // rec+22
				W.Stack       = BuffLevels[i];   // порт держит оба поля равными (см. AddOrUpdateMobSkill)
				bFound = true;
			}
		}
		if (!bFound) { ToRemove.Add(E.Id); }
	}
	for (int32 Id : ToRemove)
	{
		// `buffStopEffectsBySkillId@0xF83930`: гасит оба хэндла эффекта (rec+24/rec+28) и удаляет
		//   запись (`buffListRemoveBySkillState`) — это и есть порт `RemoveMobBySkillState`.
		Fx->RemoveMobBySkillState(Rid, Id);
	}
	if (ToRemove.Num() > 0)
	{
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[состояния] 4017 rid=%d: сервер больше не числит %d состояний — сняты (эталон Skill_Buff_RefreshStackDepth@0xF84090)"),
			Rid, ToRemove.Num());
#if ASDA_COMBAT_TRACE
		FString CtIds;   // снятые id состояний (журнал)
		for (const int32 Id : ToRemove) { CtIds += FString::Printf(TEXT(" %d"), Id); }
		ASDA_CTRACE_MOB(Rid, EAsdaCT::Fx, TEXT("4017 rid=%d: состояния моба сняты сервером (%d):%s — эффекты погашены"), Rid, ToRemove.Num(), *CtIds);
#endif
	}
}

// ★ Единая точка HP моба: обновляет полосу и, если очередь уронов (4027) не пришла, показывает
//   цифру+импакт по ДЕЛЬТЕ HP (иначе урон вообще не виден). Дубли гасит LastQueueHitS.
// ★ HP цели из боевого пакета (4029 @+4 / 4027 @+9) — сервер-авторитет: сразу приводит полосу
//   в соответствие (без него полоса отставала от цифр — рассинхрон, юзер 2026-07-29).
void UAsdaWorldSubsystem::OnTargetHp(int32 TargetRid, int32 Hp)
{
	if (TWeakObjectPtr<AAsdaMobActor>* Ex = NetMobs.Find(TargetRid))
	{
		if (AAsdaMobActor* M = Ex->Get())
		{
			// Цифры уже показывает очередь ударов этого же пакета — здесь только полоса/значение.
			ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Recv, TEXT("боевой HP цели %s: %d (полоса %d/%d) — применяется через ApplyMobHp без цифры"),
				*AsdaCtWorld::Mob(TargetRid, M), Hp, M->CurHp, M->MaxHp);
			ApplyMobHp(M, FMath::Max(0, Hp), /*bAllowNumber=*/false);
			// ★ Тот же гейт «ждём кадра удара», что у 4015/4017 (см. IsMobDeathHeldByPendingHit);
			//   по умолчанию ВЫКЛЮЧЕН (§28), ключ — `asda.Skill.HoldMobDeath 1`.
			if (Hp <= 0 && IsMobDeathHeldByPendingHit(TargetRid))
			{
				ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: смерть по боевому HP ПРИДЕРЖАНА до кадра удара навыка (asda.Skill.HoldMobDeath 1)"), *AsdaCtWorld::Mob(TargetRid, M));
				return;
			}
			// ★★★ СМЕРТЬ ТОЖЕ ЖДЁТ КАДРА УДАРА (владелец 2026-08-25: «если моб умирает с одного
			//   удара, выделение снимается сразу после атаки — ещё до взмаха»).
			//   Сервер регистрирует попадание примерно через тридцать миллисекунд после заявки, а
			//   оружие опускается через полторы секунды. Здоровье мы уже придерживаем до кадра
			//   удара, а СМЕРТЬ шла мимо этого правила: цель снималась немедленно, и у игрока
			//   пропадали кольцо и панель цели, пока персонаж ещё замахивается.
			//   Откладываем ровно до того момента, когда по этой цели придётся наш визуальный
			//   удар — то есть до конца окна `PendingImpact`. Окно конечное, поэтому смерть не
			//   потеряется: не придёт кадр — она сработает по его истечении.
			if (Hp <= 0 && !M->IsDead() && CVarAsdaDeathOnHitFrame.GetValueOnGameThread() != 0
			    && IsOurSwingInFlightAgainst(TargetRid))
			{
				UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
				// ★ Ф1: срок — ближайшее попадание ленты (ключ 0 — прежнее окно).
				double Until = PendingImpactUntilS;
				if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() != 0)
				{
					Until = 0.0;
					if (const FMobImpactLedger* Lt = ImpactLedgers.Find(TargetRid)) { for (const FAsdaImpactEntry& E : Lt->Pending) { Until = FMath::Max(Until, E.AtS); } }
				}
				const float Wait = W ? (float)FMath::Max(0.0, Until - W->GetTimeSeconds()) : 0.f;
				if (W && Wait > 0.f)
				{
					UE_LOG(LogAsdaWorld, Log,
						TEXT("[net] смерть моба rid=%d '%s' ОТЛОЖЕНА на %.2f с — по нему в полёте наш замах ")
						TEXT("(иначе выделение снималось бы до удара; откат asda.Combat.DeathOnHitFrame 0)"),
						TargetRid, *M->MobName, Wait);
					ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: смерть по боевому HP ОТЛОЖЕНА на %.2f с — в полёте наш замах (ключ asda.Combat.DeathOnHitFrame)"),
						*AsdaCtWorld::Mob(TargetRid, M), Wait);
					TWeakObjectPtr<AAsdaMobActor> WeakD = M;
					FTimerHandle TD;
					W->GetTimerManager().SetTimer(TD, [this, WeakD, TargetRid]()
					{
						AAsdaMobActor* Mm = WeakD.Get();
						if (!Mm || Mm->IsDead())
						{
							ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: отложенная смерть по боевому HP не нужна — %s"), *AsdaCtWorld::Mob(TargetRid, Mm),
								Mm ? TEXT("моб уже мёртв") : TEXT("актора нет"));
							return;
						}
						UE_LOG(LogAsdaWorld, Log,
							TEXT("[net] смерть моба rid=%d '%s' (отложенная — кадр удара пришёл)"),
							TargetRid, *Mm->MobName);
						ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: СМЕРТЬ применена (отложенная по боевому HP — кадр удара пришёл), HP полосы было %d/%d → NetDeath"),
							*AsdaCtWorld::Mob(TargetRid, Mm), Mm->CurHp, Mm->MaxHp);
						if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
						{
							if (PC->TargetActor.Get() == Mm) { PC->ClearTarget(); }
						}
						Mm->NetDeath();
						ASDA_CTRACE_END_IF_TARGET(TargetRid, TEXT("цель умерла (боевой HP, отложенная)"));
					}, Wait, false);
					return;
				}
			}
			if (Hp <= 0 && !M->IsDead())
			{
				// Эталон Recv_CastState: hp<=0 → пометка трупа (сущность помечается на удаление).
				UE_LOG(LogAsdaWorld, Log, TEXT("[net] смерть моба rid=%d '%s' (HP из боевого пакета; труп: клип → фейд → удаление)"), TargetRid, *M->MobName);
				ASDA_CTRACE_MOB(TargetRid, EAsdaCT::Death, TEXT("%s: СМЕРТЬ применена сразу (боевой HP цели ≤ 0; отложить не к чему или ключ DeathOnHitFrame 0), HP полосы было %d/%d → NetDeath"),
					*AsdaCtWorld::Mob(TargetRid, M), M->CurHp, M->MaxHp);
				if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
				{
					if (PC->TargetActor.Get() == M) { PC->ClearTarget(); }
				}
				M->NetDeath();
				ASDA_CTRACE_END_IF_TARGET(TargetRid, TEXT("цель умерла (боевой HP)"));
			}
		}
	}
}
