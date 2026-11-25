// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_EntitiesMob.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Entities.cpp, дословный перенос): мобы: HP, источник удара, создание, окно AOI, нотис сломанной вещи, уход по AOI.
//   Общее для частей — в AsdaWorldSubsystem_Entities_Private.h; шапка и реверс — в AsdaWorldSubsystem_Entities.cpp.
#include "World/AsdaAttackExit.h"   // ★ 01.11.2026: 4024/400 — выход своего из действия атаки (AsdaLeaveAttackAction)
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Entities_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Entities*.cpp

static constexpr double kOurDeltaSourceGraceS = 0.5;   // ★ Ф0: окно «наш замах» по rid считается источником ещё столько после закрытия (с)

void UAsdaWorldSubsystem::ApplyMobHp(AAsdaMobActor* M, int32 NewHp, bool bAllowNumber, bool bSourceOwn)
{
	if (!M || NewHp < 0 || M->IsDead()) { return; }
	// ★ Обнуление полосы тоже ждёт кадра удара, если по цели висит смертельная запись каста: иначе
	//   полоса пустеет раньше, чем над мобом появится цифра урона (см. IsMobDeathHeldByPendingHit).
	//   Сам выпуск записи зовёт ApplyMobHp повторно — уже с authoritative HpAfter.
	//   ⚠ По умолчанию ВЫКЛЮЧЕНО вместе со всем гейтом (§28), ключ — `asda.Skill.HoldMobDeath 1`.
	if (NewHp <= 0 && IsMobDeathHeldByPendingHit(M->NetRenderId))
	{
		ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::HpPlate, TEXT("%s: обнуление полосы ПРИДЕРЖАНО до кадра удара навыка (asda.Skill.HoldMobDeath 1), полоса %d"),
			*AsdaCtWorld::Mob(M->NetRenderId, M), M->CurHp);
		return;
	}
	// ★★★ УСТАРЕВШИЙ СНИМОК HP ОТ СЕРВЕРА (см. `NoteServerHpForecast`): пока свеж прогноз из записи
	//   каста, значение ВЫШЕ прогноза — это живое поле `mon+30853`, куда NpcServer ещё не слил
	//   накопитель `mon+34376` (вшитые 2000 мс). Применять его нельзя: полоса «оживает» до полной
	//   сразу после того, как игрок увидел цифру урона (живой замер 09:20:46.715, rid=328, hp=108).
	if (IsServerHpSnapshotStale(M->NetRenderId, NewHp))
	{
		const FAsdaMobHpForecast* Fc = MobHpForecast.Find(M->NetRenderId);
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[net] моб rid=%d '%s': снимок HP=%d ОТБРОШЕН — сервер сам прогнозировал HP=%d ")
			TEXT("(накопитель урона NpcServer ещё не слит; откат asda.Skill.LegacyDeath 1)"),
			M->NetRenderId, *M->MobName, NewHp, Fc ? Fc->Hp : -1);
		ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::HpPlate, TEXT("%s: снимок HP=%d ОТБРОШЕН как устаревший (прогноз сервера %d), полоса остаётся %d/%d"),
			*AsdaCtWorld::Mob(M->NetRenderId, M), NewHp, Fc ? Fc->Hp : -1, M->CurHp, M->MaxHp);
		return;
	}
	ApplyMobHpUnfiltered(M, NewHp, bAllowNumber, bSourceOwn);
}

void UAsdaWorldSubsystem::ApplyMobHpUnfiltered(AAsdaMobActor* M, int32 NewHp, bool bAllowNumber, bool bSourceOwn)
{
	if (!M || NewHp < 0 || M->IsDead()) { return; }
	// ★ 18.10.2026 (пол HP, разбор у `CVarAsdaHpFloorFromServer`): в бою полоса только опускается — подъём после выхода из боя.
	if (HoldMobHpRaise(M, NewHp, TEXT("серверное HP"))) { return; }
	const int32 Delta = M->CurHp - NewHp;
	if (Delta != 0 || NewHp > M->MaxHp)
	{
		ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::HpPlate, TEXT("%s: HP полосы %d → %d / %d (серверное значение%s)"),
			*AsdaCtWorld::Mob(M->NetRenderId, M), M->CurHp, NewHp, ((AAsdaMobActor::IsMaxHpFixed() && M->MobTypeMaxHp > 0) ? M->MaxHp : FMath::Max(M->MaxHp, NewHp)),
			bAllowNumber ? TEXT(", канал может дать цифру по дельте") : TEXT(", без цифры"));
	}
	M->CurHp = NewHp;
	M->ApplyPacketHpToMax(NewHp, TEXT("серверное HP"));   // ★ 18.10.2026: max не поднимается пакетом (ключ asda.Combat.MobMaxHpFixed)
	UpdateMobHpBar(M);
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!bAllowNumber || Delta <= 0 || !W) { return; }
	// ★★★ 17.10.2026 (синхронизация боя, Ф0, Р2) [ПОРТ]: ЦИФРА «ПО ДЕЛЬТЕ» — ТОЛЬКО ОТ НАШЕГО ЗАМАХА.
	//   Ниже цифра рисуется как НАША: белая, строка 20012 «вы нанесли» в чат, импакт и звук НАШЕГО оружия, флинч в нашу
	//   сторону. Сюда же приходят дельты 4017/4013/4019 и ответы на замахи соседей — и у наблюдателя чужой урон выглядел
	//   нашим (ложные данные). Правило: дельта без известного источника — только полоса (выставлена выше); своя цифра «по
	//   дельте» — только если источник наш замах по ЭТОЙ цели (`IsOurSwingSourceForMob`). Время последней цифры очереди —
	//   по этому мобу (лента попаданий, `FMobImpactLedger::LastQueueHitS`): общее на мир глушило дельту на всех мобах сразу
	//   (ревизия проекта п.14). Ключ отката `asda.Combat.ObserverSync 0` — как до 17.10 (общее время, цифра всегда «наша»).
	//   ★ Ф1 (ревизия Ф0 п.3): `bSourceOwn` — источник известен вызывающему (свой 4029 без ударов, свой снимок на кадре удара).
	if (CVarAsdaObserverSync.GetValueOnGameThread() != 0)
	{
		const FMobImpactLedger* Lq = ImpactLedgers.Find(M->NetRenderId);   // лента моба: последняя цифра очереди по нему
		if (Lq && (W->GetTimeSeconds() - Lq->LastQueueHitS) < 1.0)
		{
			ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::DmgText, TEXT("%s: цифры по дельте %d НЕТ — цифра очереди по этому мобу была %.2f с назад (< 1 с)"),
				*AsdaCtWorld::Mob(M->NetRenderId, M), Delta, W->GetTimeSeconds() - Lq->LastQueueHitS);
			return;
		}
		if (!bSourceOwn && !IsOurSwingSourceForMob(M))
		{
#if !UE_BUILD_SHIPPING
			++DeltaNumbersSuppressed;
#endif
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] rid=%d '%s': дельта HP %d без нашего замаха — только полоса, цифры нет (откат asda.Combat.ObserverSync 0)"),
				M->NetRenderId, *M->MobName, Delta);
			ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::DmgText, TEXT("%s: дельта HP %d без нашего замаха — только полоса, цифры НЕТ (источник не наш)"),
				*AsdaCtWorld::Mob(M->NetRenderId, M), Delta);
			return;
		}
	}
	else if ((W->GetTimeSeconds() - LastQueueHitS) < 1.0) { return; }
	ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::DmgText, TEXT("%s: цифра ПО ДЕЛЬТЕ серверного HP %d (источник — наш замах%s), HP полосы останется серверным %d"),
		*AsdaCtWorld::Mob(M->NetRenderId, M), Delta, bSourceOwn ? TEXT(", известен вызывающему") : TEXT(" по ленте/цели"), NewHp);
	int32 WeaponSub = -1;
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Net->SelfWeaponItemId > 0)
		{
			if (UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>())
			{
				if (const FAsdaItemRecord* R = IT->GetItem(Net->SelfWeaponItemId)) { WeaponSub = R->ItemSubType; }
			}
		}
	}
	ShowHitResult(M, Delta, /*bOurAttack=*/true, WeaponSub, 0);
	// ★ 17.10.2026 (ревизия правки боя, найдено самотестом `asda.Combat.SelfTestHitFrame`, сценарий «снимок без ударов»):
	//   `ShowHitResult` по правилу очереди САМ вычитает урон из `CurHp` (эталон CCharacter_TakeDamage), а здесь HP уже
	//   выставлен серверным значением — дельта вычиталась второй раз (100 → 70 по снимку → 40 после цифры «30»). Цифра
	//   здесь только показ: здоровье — серверное значение пакета. [ПОРТ] дефект порта, не эталона.
	if (M->CurHp != NewHp && !M->IsDead())
	{
		ASDA_CTRACE_MOB(M->NetRenderId, EAsdaCT::HpPlate, TEXT("%s: HP полосы %d → %d — возврат к серверному значению после цифры по дельте (без двойного вычета)"),
			*AsdaCtWorld::Mob(M->NetRenderId, M), M->CurHp, NewHp);
		M->CurHp = NewHp;
		UpdateMobHpBar(M);
	}
}

// ★ 07.11.2026 [ПОРТ] Ключ `asda.Combat.DeltaTargetNeedsSwing`: цифра «по дельте» по выбранной цели — только во время своего замаха/навыка.
static ASDA_DEV_CVAR(int32, CVarDeltaTargetNeedsSwing, TEXT("asda.Combat.DeltaTargetNeedsSwing"), 1,
	TEXT("Дельта HP выбранной цели: 1 = наша только при своём замахе или навыке, 0 = прежний порт (достаточно цели и флага боя)"),
	ECVF_Default);

// ★ 17.10.2026 (Ф0, Р2): источник дельты HP — наш замах по этому мобу. Окно «наш замах в полёте» (`PendingImpactRid`) открыто
//   по нему или закрылось не дольше `kOurDeltaSourceGraceS` назад (конец окна сам применяет придержанное HP — источник наш
//   замах), либо контроллер ведёт бой по этой цели (ключ окна `HitFrameAllChannels 0` окно по отправке не открывает).
bool UAsdaWorldSubsystem::IsOurSwingSourceForMob(const AAsdaMobActor* M) const
{
	if (!M) { return false; }
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;   // мир (время окна)
	if (!W) { return false; }
	if (CVarAsdaHitFrameAllChannels.GetValueOnGameThread() == 0)
	{
		// Ключ отката ленты: прежнее одиночное окно.
		if (M->NetRenderId == PendingImpactRid && W->GetTimeSeconds() <= PendingImpactUntilS + kOurDeltaSourceGraceS) { return true; }
	}
	else
	{
		// ★ Ф1 (п.7 проекта): источник — по ленте попаданий. Свои записи (ожидающие или закрытые не дольше `kOurDeltaSourceGraceS`)
		//   и НИ ОДНОЙ чужой — дельта наша; есть чужая (сосед бьёт ту же цель) — источник не различить, авторства нет.
		bool bOurs = false, bOther = false;   // есть своя / чужая запись по мобу
		if (const FMobImpactLedger* L = ImpactLedgers.Find(M->NetRenderId))
		{
			const double Now = W->GetTimeSeconds();
			for (const FAsdaImpactEntry& E : L->Pending) { (AsdaLedgerIsSelf(GetGameInstance(), E.Attacker) ? bOurs : bOther) = true; }
			for (const TPair<int32, double>& KV : L->LastAttackS)
			{
				if (Now - KV.Value > kOurDeltaSourceGraceS) { continue; }
				(AsdaLedgerIsSelf(GetGameInstance(), KV.Key) ? bOurs : bOther) = true;
			}
		}
		if (bOther) { return false; }
		if (bOurs) { return true; }
		// ★ Ревизия Ф0 п.6: свой яд/DoT (ступени 4017 без записей ленты) — источник по кастеру состояния на мобе.
		if (IsOwnDotOnMob(M->NetRenderId)) { return true; }
	}
	if (const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		if (PC->TargetActor.Get() == M && (PC->bCombatStarted || PC->bAutoAttack))
		{
			// ★ 07.11.2026 [ПОРТ] (`asda.Combat.DeltaTargetNeedsSwing`): выбранная цель и флаг боя ещё не удар. Без своего замаха или навыка
			//   дельта HP — чужая (урон по времени соседа, удар вне нашей ленты), а цифра ниже подписывается нашей строкой 20012.
			const AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
			if (CVarDeltaTargetNeedsSwing.GetValueOnGameThread() == 0 || (Me && (Me->IsAttackAnimActive() || Me->IsSkillAnimActive()))) { return true; }
		}
	}
	return false;
}

// ★★★ 17.10.2026 (синхронизация боя, Ф0) [ОРИГИНАЛ]: КОНЕЦ СЕРИИ СОСЕДА ПО ОТКАЗУ 4029.
//   NpcServer шлёт отказы 8204 → 4029 (код 1 — дистанция, 2 — моб мёртв, 4) В СЕКТОР (NS:63065/63095/63105, GS:78645), т.е. и
//   наблюдателю. Эталон `Recv_CastState` (network.c:42084–42281) для любого атакующего: коды 1/3/4/6/7 — `CChar_SetState(caster,
//   0, 20)` (боевой покой) и `field11C = 0`; код 5 — то же, если сосед в боевом состоянии; код 2 — покой, только если HP цели
//   ≤ 0 (`target[283] <= 0`), при живой цели серия продолжается. Покой сбрасывает и очередь ударов (entity_char.c:19301–19305).
//   До правки для чужого был ранний `return` — замах соседа доигрывал до SwingLen + 1.5 с (2.5–3.5 с) после конца серии.
void UAsdaWorldSubsystem::StopPeerAttackSeries(int32 PeerUserIdx, uint8 Code)
{
	if (Code < 1 || Code > 7) { return; }   // эталон: прочие коды — `default: return 0`, состояние соседа не меняется
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	FAsdaNetPlayerEntry* PE = NetPlayers.Find(PeerUserIdx);
	AAsdaPlayerCharacter* Ch = PE ? PE->Actor.Get() : nullptr;   // сосед (эталон: `CharManager_FindByUserIdx`)
	const bool bInSwing = Ch && (Ch->IsAttackAnimActive() || Ch->IsAttackLoopAlive());   // сосед в боевом состоянии (замах)
	if (Code == 2)
	{
		// ★ Ф1 (ревизия Ф0 п.2/п.3) [ОРИГИНАЛ]: цель — из ПАКЕТА (@2 rid → её HP `target[283]`, network.c:41898/42197), а не из
		//   цели разворота (при погасшем замахе живая цель считалась мёртвой). Живая — эталон ДОПИСЫВАЕТ очередь пакета, ставит
		//   соседу состояние 5 и снимает признак серии (network.c:42204–42216): следующий 4029 начнёт очередь заново.
		//   [ПОРТ] эталон тут же обнуляет HP цели (`target[284] = 0` — труп следующим кадром); порт ждёт серверную смерть (4017),
		//   её момент ведёт лента попаданий.
		//   ★ Ф2 (ревизия Ф1 п.12) [ОРИГИНАЛ]: у эталона здесь две ветки (network.c:42108–42204). Ветка `CClass_GetField47565(caster)`
		//   — СВОЙ персонаж: тот же признак в коде 1 (network.c:42086) выводит нотис 1024 в чат, а поле 47565 — «объект — локальный
		//   игрок» (190260 = 47565·4, разбор у `ApplySkillHit`); там очередь подменяется значением «−2»-заглушки, выпускается
		//   последний снаряд и состояние не трогается. Сосед всегда идёт веткой `else` — она и перенесена здесь (дописать
		//   очередь, состояние 5, цель). Свой код 2 ведёт путь отказа своего замаха (контроллер), а не этот.
		const int32 Rid = Net ? Net->CombatRefusedRid : -1;   // цель пакета
		AAsdaMobActor* Tgt = FindNetMobByRid(Rid);
		if (Tgt && !Tgt->IsDead() && Tgt->CurHp > 0)
		{
			const TArray<int32> Q = Net->CombatRefusedQueue;   // очередь пакета (копия: поле живёт только в бродкасте)
			const int32 Hp = Net->CombatRefusedTargetHp;       // HP цели из пакета
			bPeerCode2Append = true;
			OnCombatAnim(/*Case=*/1, PeerUserIdx, Rid, Q, Hp);   // тот же путь замаха и показа, что у принятого 4029
			bPeerCode2Append = false;
			PeerSeriesFlags.Add(PeerUserIdx, 0);
			UE_LOG(LogAsdaWorld, Log, TEXT("[бой] сосед %d: отказ 4029 код 2 при живой цели rid=%d — очередь пакета (%d) дописана, признак серии снят (эталон network.c:42204)"),
				PeerUserIdx, Rid, Q.Num());
			ASDA_CTRACE_IF(AsdaCtWorld::Ours(Rid, PeerUserIdx), EAsdaCT::Peer, TEXT("сосед uidx=%d: отказ 4029 код 2 при живой цели %s — очередь пакета %s дописана через путь замаха, признак серии снят"),
				PeerUserIdx, *AsdaCtWorld::Mob(Rid, Tgt), *AsdaCtWorld::Queue(Q));
			return;
		}
	}
	PeerSeriesFlags.Add(PeerUserIdx, 0);   // эталон: `field11C = 0` во всех кодах
	// [ОРИГИНАЛ] код 5 — в покой только из НЕ занятого состояния: `!charIsActionStateNonCombat` (network.c:42253).
	//   ★ 18.10.2026 (ревизия Ф4, мелочь 4): `charIsActionStateNonCombat@0xA2A400` — МИСНЕЙМ: возвращает 0 для состояний покоя
	//   0/1/6–9/29/33–47/51/60–68 и 1 для прочих (замах 5, подход 15, ожидание 22, навык 24/25), т.е. «персонаж ЗАНЯТ»
	//   (entity_char.c:25398–25443; поле — состояние `CChar_SetState`, entity_char.c:19258; то же — `Reports/skill_use_system.md`).
	//   Прежде условие стояло наоборот (покой — только из замаха). Сосед занят — серия и замах идут, снят только признак серии.
	const bool bBusy = Ch && (bInSwing || Ch->IsSkillAnimActive());
	if (Code == 5 && bBusy)
	{
		ASDA_CTRACE_UID(PeerUserIdx, EAsdaCT::Peer, TEXT("сосед uidx=%d: отказ 4029 код 5 — сосед занят (замах/навык), серия идёт, снят только признак серии"), PeerUserIdx);
		return;
	}
	const int32 Was = GetDamageQueueLen(PeerUserIdx);   // для журнала: сколько ударов было в очереди
	if (TArray<int32>* Buf = DmgBuffers.Find(PeerUserIdx)) { Buf->Reset(); }
	PeerSeriesTargetRid.Remove(PeerUserIdx);
	++PeerSeriesGen.FindOrAdd(PeerUserIdx);   // ★ Ф1 (ревизия Ф0 п.10): покой прерывает показ уже поставленных цифр прохода
	const bool bWasSwinging = Ch && Ch->IsAttackAnimActive();   // для журнала: замах шёл
	if (Ch) { Ch->StopAttackAnim(); }
	// ★ Ф1: конец серии прерывает записи ленты соседа (назначенные ответом попадания доживают, без ответа — сняты).
	CutImpactEntries(PeerUserIdx, TEXT("отказ 4029 соседа"));
	UE_LOG(LogAsdaWorld, Log, TEXT("[бой] сосед %d: отказ 4029 код %u — серия окончена (замах %s, очередь %d сброшена; откат asda.Combat.ObserverSync 0)"),
		PeerUserIdx, (uint32)Code, bWasSwinging ? TEXT("погашен") : TEXT("не шёл"), Was);
	ASDA_CTRACE_UID(PeerUserIdx, EAsdaCT::Peer, TEXT("сосед uidx=%d: отказ 4029 код %d — СЕРИЯ ОКОНЧЕНА: замах %s, очередь %d сброшена, поставленные цифры прохода гасятся (новое поколение серии)"),
		PeerUserIdx, (int32)Code, bWasSwinging ? TEXT("погашен") : TEXT("не шёл"), Was);
}

// ★ Ф1: непоказанный известный урон записей ДРУГИХ атакующих по мобу (bUnknown — у кого-то исход неизвестен).
int64 UAsdaWorldSubsystem::LedgerOtherCover(int32 Rid, int32 Attacker, bool& bUnknown) const
{
	bUnknown = false;
	int64 Cover = 0;   // сумма непоказанного урона чужих записей
	if (const FMobImpactLedger* L = ImpactLedgers.Find(Rid))
	{
		for (const FAsdaImpactEntry& E : L->Pending)
		{
			if (E.Attacker == Attacker) { continue; }
			bUnknown |= E.bOutcomeUnknown;
			Cover += E.PendingDamage;
		}
	}
	return Cover;
}

void UAsdaWorldSubsystem::ResetImpactLedgers()
{
	if (UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		for (TPair<int32, FMobImpactLedger>& KV : ImpactLedgers) { W->GetTimerManager().ClearTimer(KV.Value.Timer); }
	}
	// ★ Ф2 (ревизия Ф1 п.10): удержанная добыча прошлого мира/сессии не выпускается (её мира больше нет), а показ опыта
	//   (`bReleaseOnReset`) — выпускается: значение уже записано персонажу, терять его надпись и строки чата нельзя.
	TArray<TFunction<void()>> ReleaseNow;   // выпуски, переживающие сброс (вызываются после очистки карты)
	for (TPair<int32, FMobImpactLedger>& KV : ImpactLedgers)
	{
		for (FAsdaHeldPresentation& H : KV.Value.HeldLoot) { if (H.bReleaseOnReset && H.Release) { ReleaseNow.Add(MoveTemp(H.Release)); } }
	}
	ASDA_CTRACE(EAsdaCT::Note, TEXT("ленты попаданий сброшены (сессия/карта): лент %d, выпускается удержанных показов опыта %d"), ImpactLedgers.Num(), ReleaseNow.Num());
	ImpactLedgers.Reset();
	PlannedHits.Reset();   // ★ 18.10.2026: поставленные удары и моменты смертельных — данные того же мира/сессии
	for (TPair<int32, FAsdaLethalMark>& KV : LethalMarks)
	{
		if (UWorld* Wl = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { Wl->GetTimerManager().ClearTimer(KV.Value.Backup); }
	}
	LethalMarks.Reset();
	for (TPair<int32, FAsdaMobHpFloor>& KV : MobHpFloors)
	{
		if (UWorld* Wf = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr) { Wf->GetTimerManager().ClearTimer(KV.Value.QuietTimer); }
	}
	MobHpFloors.Reset();   // ★ 18.10.2026 (пол HP): данные того же мира/сессии
	for (TFunction<void()>& F : ReleaseNow) { F(); }
}

void UAsdaWorldSubsystem::FlushAllLedgers(const TCHAR* Why)
{
	TArray<int32> Keys; ImpactLedgers.GetKeys(Keys);   // ключи заранее: сброс трогает карту
	for (const int32 Rid : Keys)
	{
		const FMobImpactLedger* L = ImpactLedgers.Find(Rid);
		if (L && (L->Pending.Num() > 0 || L->bDeathPending || L->HeldLoot.Num() > 0)) { FlushLedger(Rid, Why); }
	}
}

// ── ★★★ СОЗДАНИЕ СЕТЕВОГО МОБА — общий код ТРЁХ каналов спауна оригинала ──────────────────────
//   • 4013 Recv_MobSpawn@0xF305E0, ветка «rid НЕ найден» → mobListSpawnAndLink@0xF77890 + CMob_Init@0xF83100;
//   • 4019 Recv_MobSpawnLoop@0xF30330, else-ветка → RecvPacket_MobSpawn (тот же путь);
//   • 4017 Recv_CombatHitAux@0xF33B60, ветка flags&4 при !entity → mobTableFindIndexById + mobListSpawnAndLink.
//   Всегда: модель по type-id, позиция = cur-тайл ПАКЕТА, HP из пакета, ФЕЙД-ИН 1500 мс, плашка, звуки.
//   Именно сюда попадает РЕСПАВН убитого моба: его труп к этому времени уже погас и удалён (флаги 4→0x10),
//   rid свободен, и сервер присылает мобa заново — в СВОЕЙ клетке, а не на месте смерти.
AAsdaMobActor* UAsdaWorldSubsystem::CreateNetMob(int32 Rid, int32 Type, int32 Hp, FIntPoint CurTile, FIntPoint DstTile,
	int32 FaceDeg, float SpeedUnitsPerMs, int32 StateByte, const TCHAR* Chan)
{
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!W) { return nullptr; }
	// ★ Новый (в т.ч. переиспользованный) rid — прогноз HP от прошлой жизни моба недействителен.
	MobHpForecast.Remove(Rid);
	DropLedger(Rid);   // ★ 17.10.2026 (Ф0/Ф1, ревизия Ф0 п.11): лента попаданий прошлой жизни этого rid (и время цифр очереди)
	const FVector2D CurG = AsdaCoord::TileCenterGame(CurTile);   // ★ Аудит №3 (B2)
	const FVector2D DstG = AsdaCoord::TileCenterGame(DstTile);   // ★ Аудит №3 (B2)
	const FVector2D UeXY = AsdaCoord::GameToUe2D(CurG);   // ★ Аудит №3 (B1)
	// Поворот из пакета (deg вокруг вертикали; конверсия как у авторских: -deg + базовый -90).
	const FRotator Rot(0.f, -(float)FaceDeg - 90.f, 0.f);
	const FTransform SpawnXf(Rot, FVector(UeXY.X, UeXY.Y, TerrainZ(UeXY)), FVector(0.01f));
	// ★ Динамический спаун по type-id (модель из DT_MobNames.Model — порт Mob list.BIN +0x24).
	AAsdaMobActor* C = SpawnNetMobDynamic(Type, SpawnXf);
	if (!C)
	{
		++MobStats.FailNoModel;   // ★ динамический спаун не удался (нет строки в DT_MobNames / нет ассета меша)
		// Фолбэк: копия запечённого шаблона уровня (модель не заимпортирована отдельным ассетом).
		AAsdaMobActor* Tmpl = FindFreeMob(Type, CurTile.X, CurTile.Y);
		if (!Tmpl)
		{
			++MobStats.FailNoTemplate;
			UE_LOG(LogAsdaWorld, Warning, TEXT("[net] МОБ type=%d tile=(%d,%d) rid=%d: ни модели, ни шаблона"), Type, CurTile.X, CurTile.Y, Rid);
			return nullptr;
		}
		FActorSpawnParameters SP;
		SP.Template = Tmpl;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SP.bDeferConstruction = true;
		C = W->SpawnActor<AAsdaMobActor>(Tmpl->GetClass(), SP);
		if (!C) { ++MobStats.FailSpawn; return nullptr; }
		FVector MScale = Tmpl->GetActorScale3D();
		if (MScale.IsNearlyZero()) { MScale = FVector(0.01f); }
		C->bNetSpawned = true;
		C->FinishSpawning(FTransform(Rot, SpawnXf.GetLocation(), MScale));
		C->SetActorScale3D(MScale);
	}
	C->MobId = Type;
	C->NetRenderId = Rid;
	ForgetLethalState(Rid);   // ★ ревизия 18.10 (п.6): rid переиспользован новым мобом — учёт ударов и метка прежнего не его
	// ★★★ Д7 — MaxHp БЕРЁТСЯ ИЗ ТАБЛИЦЫ ТИПА, А НЕ ИЗ ПАКЕТА. Эталон при спауне делает
	//     `ent[285] = sub_932483(g_pMobMgr, ent[117])` = `Mob list.BIN + 272·idx + 0x94`
	//     (`Recv_MobSpawn@0xF305E0`; дословный повтор — game.c:548216-548219 в ветке спауна из пакета
	//     удара), а из пакета ставит ТОЛЬКО текущий HP: `ent[283] = ent[284] = @25` (4015) / `@92`
	//     (4017) / `@9` (4013/4019). Порт клал `MaxHp = Hp`, поэтому моб, впервые увиденный раненым
	//     (вход в сектор, AoI-возврат, респавн), показывал ПОЛНУЮ полосу, а следующий апдейт HP её
	//     «ронял» — визуальный скачок на всех ТРЁХ каналах спауна сразу. Правка чинит все три.
	//   ⚠ ПОРЯДОК: `ResolveMobStats()` перенесён СЮДА (был ниже, у плашки) — MaxHp типа, ранг,
	//   «бьёт первым» и «жизни» нужны РАНЬШЕ первой отрисовки полосы HP. Метод идемпотентен и не
	//   зависит ни от имени, ни от меша, так что переезд ничего не ломает.
	// ★ 15.09 (dt_gc_hold): таблицы типа моба — сильно у мира. Здесь это ПОВТОР без загрузки: группу `Mob` уже удержал
	//   `SpawnNetMobDynamic` (выше, первой строкой после проверки мира) — до ОБОИХ `SpawnActor`, т.е. до `OnConstruction`
	//   и динамического моба, и клона шаблона (`FinishSpawning` читает DT_MobStats/DT_MobTextures). Вызов оставлен как
	//   гарантия перед `ResolveMobStats` ниже. (Ревизия 15.09: прежняя пометка «до первого чтения» относилась к нему неточно.)
	HoldSpawnTables(EAsdaSpawnTables::Mob);
	C->ResolveMobStats();
	{
		C->CurHp = Hp;
		C->MaxHp = (C->MobTypeMaxHp > 0) ? C->MobTypeMaxHp : Hp;   // строки нет ⇒ прежнее поведение
		// Сервер может прислать текущий HP выше табличного максимума (баф/ивент) — полоса не должна
		//   переполняться; тот же кламп стоит в `ApplyMobHp` и `NetRevive`.
		// ★ 18.10.2026 [ОРИГИНАЛ]: max — только из таблицы типа (`sub_932483`); при ключе asda.Combat.MobMaxHpFixed 1 HP выше него —
		//   строка «АНОМАЛИЯ», полоса упирается в 100 % (кламп доли в отрисовке), max не растягивается. Без строки типа — как было.
		//   ★ Ревизия 18.10: 4013/4019/4017 — каналы спауна (HP выше таблицы — серверный максимум, max поднимается), спаун «на лету»
		//   из боевого 4015 — нет.
		C->ApplyPacketHpToMax(C->CurHp, Chan, /*bSpawnChan=*/!(Chan && FCString::Strstr(Chan, TEXT("4015"))));
		if (C->MobTypeMaxHp <= 0)
		{
			UE_LOG(LogAsdaWorld, Warning,
				TEXT("[моб+] %s rid=%d type=%d: MaxHp типа из DT_MobStats не получен — взят из пакета (%d), ")
				TEXT("эталон здесь читает Mob list.BIN +0x94 (sub_932483 → 0xF31040)"), Chan, Rid, Type, Hp);
		}
	}
	C->bCallMob = false;   // пакет = сервер решил, что моб существует
	C->LastNetUpdateS = W->GetTimeSeconds();
	C->SetActorHiddenInGame(false);
	C->SetActorEnableCollision(true);   // Template-копия наследует ВЫКЛЮЧЕННУЮ коллизию шаблона
	// ★ ФЕЙД-ИН СПАУНА (эталон: бит 0 флагов сущности +0x1E4 → charUpdateActionState23@0xF844F0
	//   `if ((flags & 1) && cat == 23) { f146 += dt; alpha = f146/1500; }` — 1500 мс; тот же бит
	//   ставит реген после AoI-фейда, Recv_MobSpawn@0xF30670 `mob[121] = 1`).
	//   ЭТО И ЕСТЬ «появление с фейдом»: убитый моб исчезает трупным фейдом, а возрождённый
	//   ПРОЯВЛЯЕТСЯ здесь — новой сущностью в серверной клетке.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F1): общая точка `UAsdaFadeComponent::Begin` вместо безусловного `NewObject`
	//   (второй компонент поверх идущего фейда ломал свап материалов).
	UAsdaFadeComponent::Begin(C, /*bIn*/true, AAsdaMobActor::FadeInSeconds);
	NetMobs.Add(Rid, C);
	++MobStats.Created;
	// ★ Цель движения при спауне — ТОЛЬКО в состоянии 2 (эталон switch(--state) case 1) и только если
	//   dst ОТЛИЧАЕТСЯ от cur и скорость осмысленна (cur==dst → SetActionState(mob,1), без траектории).
	//   ★ КРУГ 8: раньше траектория строилась при ЛЮБОМ состоянии — моб, заспавненный в замахе (st=3)
	//   или касте (st=5/7), уезжал в dst, хотя эталон в этих ветках движение не назначает вовсе.
	//   Канал 4017 (flags&4) зовёт CreateNetMob со StateByte=1 — там траектории тоже нет (эталон
	//   Recv_CombatHitAux: снап + SetActionState(1)).
	if (StateByte == 2 && DstTile != CurTile && SpeedUnitsPerMs > 0.f) { C->NetMove(CurG, DstG, SpeedUnitsPerMs); }
	// ★ Над-головное ИМЯ моба (постоянно, эталон). Имя модели = имя скелет-меша шаблона (Mob list @+4);
	//   читаемое имя по MobId из Mob list.BIN @+100. Пусто → не показываем.
	{
		FString Model;
		if (C->Mesh && C->Mesh->GetSkeletalMeshAsset()) { Model = C->Mesh->GetSkeletalMeshAsset()->GetName(); }
		HoldEntityNameTables();   // ★ 15.09: таблицы имён — сильно у мира (как прежний EnsureNameTables в GetMobName)
		C->MobName = AsdaEntityText::MobName(C->MobId, Model);         // ★ волна 3 п.14: единая точка имён
		C->SoundBase = AsdaEntityText::MobSoundBase(C->MobId);         // ★ фазовые звуки (смерть/замах/рык)
		// ★ Эталонные поля ТИПА (DT_MobStats) уже подтянуты выше — там же, где считался MaxHp типа
		//   (`ResolveMobStats()` сразу после `C->MobId = Type`). Остаётся высота плашки: полу-размер
		//   AABB → ФИКСИРОВАННАЯ константа 2·halfY+50 (OnConstruction видел MobId шаблона, не наш).
		C->ApplyNameplateHeight();
		if (UAsdaNameplateComponent* NP = FillNameplate(C, C->MobName))
		{
			// ★ Д8: сегменты полосы = «жизни» типа (`Mob list.BIN +0x10C` → сущность +18700).
			//   `ResolveMobStats()` отработал выше (перед расчётом MaxHp), поэтому `MobLives` заполнен.
			NP->SetHp(C->CurHp, C->MaxHp, /*Segments=*/C->MobLives);   // полоса скрыта до выбора целью (эталон)
		}
	}
	// ★ ДОКАЗАТЕЛЬНАЯ ДИАГНОСТИКА (нужна для сверки «сервер vs мир»): что именно пришло в записи
	//   и куда сел актор. Один моб = одна строка = ровно один актор.
	// ★ Д7/Д8 в логе: видно, что MaxHp пришёл ИЗ ТАБЛИЦЫ (а не равен текущему HP), и что ранг/
	//   «бьёт первым»/«жизни» типа доехали до сущности — без этого правку нечем доказать по логу.
	UE_LOG(LogAsdaWorld, Log, TEXT("[моб+] %s rid=%d type=%d st=%d hp=%d/%d тайл=(%d,%d)→(%d,%d) мир=(%.0f,%.0f,%.0f) '%s' (ранг=%d первый=%d жизней=%d, фейд-ин %.1fс)"),
		Chan, Rid, Type, StateByte, Hp, C->MaxHp, CurTile.X, CurTile.Y, DstTile.X, DstTile.Y,
		C->GetActorLocation().X, C->GetActorLocation().Y, C->GetActorLocation().Z, *C->MobName,
		C->MobRank, C->MobAttackFirst, C->MobLives,
		AAsdaMobActor::FadeInSeconds);
	return C;
}

// ── ★ КРУГ 10б: АВТО-ОЦЕНКА СЕРВЕРНОГО AoI ПО НАБЛЮДАЕМЫМ ПАКЕТАМ ──
// Сдвиг кольца посекундных максимумов на текущую секунду (протухшие бакеты обнуляются).
void UAsdaWorldSubsystem::AdvanceAoiBuckets(int64 NowSec)
{
	if (AoiAuto.CurSec < 0 || NowSec - AoiAuto.CurSec >= 60)
	{
		FMemory::Memzero(AoiAuto.Buckets, sizeof(AoiAuto.Buckets));
	}
	else
	{
		for (int64 S = AoiAuto.CurSec + 1; S <= NowSec; ++S) { AoiAuto.Buckets[S % 60] = 0; }
	}
	AoiAuto.CurSec = NowSec;
}

int32 UAsdaWorldSubsystem::AoiWindowMax() const
{
	int32 M = 0;
	for (int32 I = 0; I < 60; ++I) { M = FMath::Max(M, AoiAuto.Buckets[I]); }
	return M;
}

// Наблюдение: ПРИМЕНЁННАЯ запись 4013/4019 (прошла гейт +27 — координаты отброшенных гейтом записей
// НЕ использовать: при шаге 174 там сдвинутый мусор). Дистанция Чебышёва в тайлах до игрока на момент
// приёма. РАСШИРЕНИЕ эффективного сектора — мгновенно (свежий дальний моб не должен ждать тика).
void UAsdaWorldSubsystem::NoteAoiObservedTile(int32 TileX, int32 TileZ)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	// Тайл игрока: живой актор (точнее), иначе спавн-тайл из 4003 (бурст 4013 приходит ДО спавна павна).
	int32 PTx, PTz;
	if (const AActor* Player = LocalPlayerActor.Get())
	{
		// ★ Аудит №3 (B1/B2): UE→game, затем game→тайл через общую точку.
		const FIntPoint PTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Player->GetActorLocation()));
		PTx = PTile.X;
		PTz = PTile.Y;
	}
	else
	{
		UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
		if (!Net || Net->SelfTileX < 0) { return; }
		PTx = Net->SelfTileX; PTz = Net->SelfTileZ;
	}
	const int32 Dist = FMath::Max(FMath::Abs(TileX - PTx), FMath::Abs(TileZ - PTz));
	const double Now = W->GetTimeSeconds();
	AdvanceAoiBuckets((int64)Now);
	int32& B = AoiAuto.Buckets[((int64)Now) % 60];
	B = FMath::Max(B, Dist);
	AoiAuto.LastObsS = Now;
	if (Dist > AoiAuto.EffTiles)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[AoI-авто] порог РАСШИРЕН %d → %d тайлов (моб-тайл (%d,%d), игрок (%d,%d))"),
			AoiAuto.EffTiles, Dist, TileX, TileZ, PTx, PTz);
		AoiAuto.EffTiles = Dist;   // расширение мгновенно
	}
}

namespace {
// ── ★ AoI-деспаун мобов (эталон entityListUpdateFade@0xF78660: таймер i[147]=10000мс, рефреш каждой
//   4013-записью; истёк И сектор-дистанция >1 — сектор = world/3000 = 30 тайлов → фейд 1с → удаление;
//   рядом с игроком таймер держится, моб живёт) ──
	// Контейнер надетого и число слотов экипировки, которые перебирает эталон (`game_main.c:1511`).
	constexpr int32 kEquipContainer = 3;
	constexpr int32 kEquipSlotCount = 11;
	// «Прочность одной из твоих вещей упала до 0. До ремонта предмет бесполезен.»
	constexpr int32 kLocBrokenGear  = 110303;
}

// ═══════ ★★★ «ПРОЧНОСТЬ ОДНОЙ ИЗ ТВОИХ ВЕЩЕЙ УПАЛА ДО 0» — эталон `game_main.c:1508-1524` ═══════
//   Оригинал (внутри главного тика игры):
//       if (timeGetTime() - g_dwDurabilityCheckLastTime > 0x2710)      // раз в 10 секунд
//         for (i = 0; i < 11; ++i)                                     // 11 слотов экипировки
//           rec = GetItemRecord(надетое[i]);
//           if (rec && (rec->itemType <= 4 || (rec->itemType >= 8 && rec->itemType <= 9))
//               && !надетое[i].прочность)                              // байт +13 записи == 0
//             notice = UI_FormatLocalizedNotice(110303);
//             CChat_AddTokenizedLines(g_pChatWnd, notice, -11514, 10000, 0);   // плавающая строка
//             CChat_AddTokenizedLines(g_pChatWnd, notice, 0, 0, 1);            // журнал
//             break;                                                   // ОДНА строка за проход
//
//   ★ РЕШЕНИЕ ВЛАДЕЛЬЦА: выводим в СИСТЕМНЫЙ ЧАТ, а не в обычный. Маршрут уже заложен в
//     `UAsdaMessageSubsystem` («прочность упала до 0» → системный чат, см. шапку его заголовка),
//     поэтому здесь просто отдаём событие с категорией `ItemRefused` («отказ предмета, прочность,
//     вес») и не решаем за таблицу маршрутизации, куда и каким цветом его класть.
//   ⚠ О ЦВЕТЕ: в чате оригинал красит эту строку значением −11514 = 0xFFFFD306, то есть
//     ЗОЛОТИСТО-ЖЁЛТЫМ, а не красным. Красный (−65536) у прочности только в ТУЛТИПЕ предмета
//     (`item_inventory.c:37018`). Цвет здесь берёт таблица маршрутов и правится без пересборки.
void UAsdaWorldSubsystem::TickBrokenGearNotice()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
	UAsdaItemTableSubsystem* IT  = GI->GetSubsystem<UAsdaItemTableSubsystem>();
	if (!Inv || !IT) { return; }

	// Пока содержимое надетого не пришло с сервера целиком, ПУСТАЯ ячейка не значит «слот пуст»,
	//   а нулевая прочность — тем более не факт. Молчим до полной выдачи контейнера.
	if (!Inv->IsContainerReceived(kEquipContainer)) { return; }

	const int32 Slots = FMath::Min(kEquipSlotCount, Inv->GetContainerSize(kEquipContainer));
	for (int32 i = 0; i < Slots; ++i)
	{
		const FAsdaItemInstance* It = Inv->GetCell(kEquipContainer, i);
		if (!It || It->IsEmpty()) { continue; }
		if (It->Durability != 0) { continue; }                  // цела — эталонное `!прочность`
		const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
		if (!Rec) { continue; }
		// Гейт вида предмета 1:1 с эталоном: только снаряжение, у которого прочность вообще есть.
		const int32 T = Rec->ItemType;
		if (!(T <= 4 || (T >= 8 && T <= 9))) { continue; }

		if (UAsdaMessageSubsystem* Msg = GI->GetSubsystem<UAsdaMessageSubsystem>())
		{
			FAsdaMsgParams P;
			P.Category = EAsdaMsgCategory::ItemRefused;
			P.LocId    = kLocBrokenGear;
			// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#110303]».
			Msg->Post(P);
		}
		UE_LOG(LogAsdaWorld, Log,
			TEXT("[прочность] надетый предмет %d («%s», слот %d) сломан (прочность 0) — нотис %d"),
			It->ItemCode, *IT->GetItemName(It->ItemCode), i, kLocBrokenGear);
		break;   // эталонный `break`: не больше одной строки за проход, сколько бы вещей ни сломалось
	}
}

void UAsdaWorldSubsystem::TickAoiDespawn()
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	const AActor* Player = LocalPlayerActor.Get();
	if (!Player) { return; }
	const double Now = W->GetTimeSeconds();
	// ★ КРУГ 10б: режим из CVar — 0=АВТО (дефолт), >0=ручной сектор, <0=деспаун выключен.
	const int32 CVarV = CVarAsdaAoiSectorTiles.GetValueOnGameThread();
	if (CVarV != AoiAuto.LastLoggedCVar)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[AoI-авто] режим: %s (CVar Asda.Mobs.AoiSectorTiles=%d)"),
			CVarV == 0 ? TEXT("АВТО (оценка по пакетам)") : (CVarV > 0 ? TEXT("ручной") : TEXT("деспаун ВЫКЛЮЧЕН")), CVarV);
		AoiAuto.LastLoggedCVar = CVarV;
	}
	const bool bAoiOn = CVarV >= 0;
	int32 SecTiles = CVarV;   // >0 — ручной размер
	if (CVarV == 0)
	{
		// ★ КРУГ 11: АВТО-порог МОНОТОННЫЙ — max(30, максимум наблюдений ЗА СЕССИЮ КАРТЫ); расширение
		//   применяется мгновенно в NoteAoiObservedTile, СЖАТИЯ НЕТ (сброс к 30 — только по 4003).
		//   Прежнее сжатие к максимуму окна 60 с (≤8 тайл/с) на сервере без повторных 4013 приводило
		//   к безвозвратному массовому деспауну после ~100 с без наблюдений (город/AFK) — см. шапку.
		AdvanceAoiBuckets((int64)Now);   // окно живёт только как диагностика для asda.Mobs.Dump
		SecTiles = AoiAuto.EffTiles;
	}
	const float SecUnits = bAoiOn ? SecTiles * AsdaCoord::kTileUnits : 3000.f;   // при выкл значение не используется
	// game-координаты игрока.   // ★ Аудит №3 (B1)
	// ⚠ Делитель ЗДЕСЬ — РАЗМЕР СЕКТОРА (SecTiles тайлов), а не тайл: `GameToTile` здесь НЕ подходит.
	const FVector2D PG = AsdaCoord::UeToGame2D(Player->GetActorLocation());
	const int32 PSecX = FMath::FloorToInt(PG.X / SecUnits);
	const int32 PSecZ = FMath::FloorToInt(PG.Y / SecUnits);
	TArray<int32> Drop;
	for (TPair<int32, TWeakObjectPtr<AAsdaMobActor>>& It : NetMobs)
	{
		AAsdaMobActor* M = It.Value.Get();
		if (!M) { Drop.Add(It.Key); continue; }
		// ★ Труп — такая же сущность: AoI-ветка эталона (`i[116]==23`) про HP ничего не знает.
		//   ★ R5-fix3 (13.11.2026, уточнение по декомпилу): `i[116]==23` в `entityListUpdateFade@0xF78660` (entity_char.c:54514) —
		//   «моб ЗАГРУЖЕН» (`loadSubState`, 23 = конец покадровой загрузки), а не «категория моб»: отсчёт ухода из обзора идёт
		//   только у загруженного моба. В порту моб готов сразу при создании, отсчёт идёт всегда.
		// ★★★ НО у трупа СВОЙ фейд (посмертный, флаг 4) — он стартует раньше и уже ведёт к удалению;
		//   второй фейд-компонент поверх первого сломал бы свап материалов (двойной фейд).
		// ★★★ КРУГ 9: гаснущий НЕ выкидываем из NetMobs! Эталон держит сущность в списке до самого
		//   charRemoveEntity/charDespawnRemove — rid занят, и 4013 в окне фейда находит СУЩЕСТВУЮЩУЮ
		//   сущность (AoI-фейд отменяется, посмертный — нет), а НЕ создаёт дубль. Прежний Drop.Add
		//   освобождал rid на ~0.5-1с раньше эталона: свежая 4013-запись с rid гаснущего ТРУПА уходила
		//   в ветку «rid не найден» → CreateNetMob → рядом с гаснущим трупом МГНОВЕННО вставал живой
		//   моб с полным HP («мгновенное воскрешение», симптом 2 круга 9). Weak-ptr обнулится сам
		//   после Destroy фейдом — тогда 4013 законно создаст сущность заново (как эталон после удаления).
		if (M->IsFadingOut()) { continue; }
		if (!bAoiOn) { continue; }                            // ★ КРУГ 10: AoI-деспаун выключен CVar'ом
		if (Now - M->LastNetUpdateS <= 10.0) { continue; }   // сервер ещё ведёт моба
		const FVector ML = M->GetActorLocation();
		const FVector2D MG = AsdaCoord::UeToGame2D(ML);   // ★ Аудит №3 (B1); дальше делим на СЕКТОР, а не на тайл
		const int32 MSecX = FMath::FloorToInt(MG.X / SecUnits);
		const int32 MSecZ = FMath::FloorToInt(MG.Y / SecUnits);
		if (FMath::Abs(MSecX - PSecX) <= 1 && FMath::Abs(MSecZ - PSecZ) <= 1) { continue; }   // рядом — живёт
		UE_LOG(LogAsdaWorld, Log, TEXT("[net] AoI-деспаун моба rid=%d '%s' (10с без 4013, сектор далеко), фейд %.1fс"),
			It.Key, *M->MobName, AAsdaMobActor::AoiFadeSeconds);
		++MobStats.AoiDespawn;
		M->StartFadeOut(EAsdaMobFade::Aoi, AAsdaMobActor::AoiFadeSeconds);   // эталон: флаг 0x100 → 1000мс → charDespawnRemove
		// ★★★ ИСПРАВЛЕНО 2026-08-11 (B1 перепроверки): rid НЕ освобождаем на СТАРТЕ фейда — эталон
		//   (entityListUpdateFade@0xF78660) держит сущность в списке ВЕСЬ фейд и удаляет её только
		//   charDespawnRemove. Прежний `Drop.Add(It.Key)` прямо здесь противоречил комментарию выше и
		//   давал две ошибки: (а) 4013, пришедшая в окно ~1 с фейда, уходила в ветку «rid не найден» →
		//   CreateNetMob → рядом с гаснущим вставал живой дубль; (б) `CancelAoiFade()` и ветка
		//   «AoI-фейд ОТМЕНЁН повторной записью» в обработчике 4013 были ФИЗИЧЕСКИ НЕДОСТИЖИМЫ.
		//   Теперь запись уходит из NetMobs сама — по обнулению weak-ptr после Destroy фейдом
		//   (ветка `if (!M) { Drop.Add(It.Key); continue; }` в начале цикла), как charDespawnRemove.
	}
	for (int32 K : Drop) { NetMobs.Remove(K); DropLedger(K); }   // ★ Ф1 (ревизия Ф0 п.11): и лента пропавшего моба
	// ★ ЛУТ: клиентский AoI-деспаун (эталон billboardList_Update: таймер 10000мс; в пределах ±1 сектора
	//   (3000 юнитов) — таймер клампится (живёт), иначе по истечении — удаление ноды; без фейда).
	TArray<FIntPoint> LootDrop;
	for (TPair<FIntPoint, TWeakObjectPtr<AAsdaLootActor>>& It : NetLoot)
	{
		AAsdaLootActor* L = It.Value.Get();
		if (!L) { LootDrop.Add(It.Key); continue; }
		const FVector LL = L->GetActorLocation();
		const FVector2D LG = AsdaCoord::UeToGame2D(LL);   // ★ Аудит №3 (B1); делитель — СЕКТОР
		const int32 LSecX = FMath::FloorToInt(LG.X / SecUnits);
		const int32 LSecZ = FMath::FloorToInt(LG.Y / SecUnits);
		// ★ КРУГ 10: сектор из CVar; при выключенном AoI лут «всегда рядом» (таймер клампится).
		if (!bAoiOn || (FMath::Abs(LSecX - PSecX) <= 1 && FMath::Abs(LSecZ - PSecZ) <= 1))
		{
			L->SpawnedAtS = Now;   // рядом — таймер клампится (эталон)
			continue;
		}
		if (Now - L->SpawnedAtS > 10.0)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[лут] AoI-деспаун тайл(%d,%d) (10с вне ±1 сектора)"), It.Key.X, It.Key.Y);
			L->Destroy();
			LootDrop.Add(It.Key);
		}
	}
	for (const FIntPoint& K : LootDrop) { NetLoot.Remove(K); NetLootSerial.Remove(K); }   // ★ 09.10.2026: и номер появления
	// ★ Сердечки-линк соулмейтов (таймер 1..5с; гранулярность тика 1с достаточна).
	TickSoulHearts();
}
