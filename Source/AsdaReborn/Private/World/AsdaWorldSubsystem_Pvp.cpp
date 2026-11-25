// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Pvp.cpp — часть `AsdaWorldSubsystem_Hit.cpp`: PvP: серии, атака/удар/кадр удара, урон, нотисы.
//   ★ R2 рефакторинга (10.11.2026): определения перенесены из `AsdaWorldSubsystem_Hit.cpp` ДОСЛОВНО (сверка `Tools/asda_split_check.py`);
//   общие помощники и объявления — `AsdaWorldSubsystemHitInternal.h`. Шапка системы и ссылки на эталон — в `AsdaWorldSubsystem_Hit.cpp` и заголовке класса.

#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaEntryWindows.h"   // ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll
#include "UI/AsdaEscChain.h"      // ★ 25.10.2026: «кто ударил своего» (эталонное netWord54) для веток состояния ESC
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaAttackRhythmSubsystem.h"   // ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха
#include "World/AsdaWalkGrid.h"
#include "Characters/AsdaCharAssembly.h"      // кости/attach-матрицы оружия «в руке ↔ за спиной»
#include "Camera/CameraComponent.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
#include "UI/AsdaChatSubsystem.h"            // ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "World/AsdaProjectileActor.h"       // ★ снаряды дальнобойного оружия (projectile_re.md)
#include "World/AsdaNockedArrow.h"           // ★ 05.11.2026: жизненный цикл стрелы на тетиве (снятие по событиям эталона)
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "World/AsdaFloatingTextSubsystem.h" // цифры урона (порт CFloatingText)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "Items/AsdaItemDbRows.h"            // ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "AsdaBytes.h"                       // ★ единые читалки сырой записи (DataHex)
#include "Engine/DataTable.h"
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/MeshComponent.h"          // подошва цели: низ ВИДИМОЙ геометрии (TargetSoleZ)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "TimerManager.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "World/AsdaEntityText.h"           // ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaWorldKind.h"            // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "AsdaHeldTables.h"                  // FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "World/AsdaPetActor.h"              // питомцы (6120, pet_system_re.md)
#include "World/AsdaSoulGuardianActor.h"     // ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Text/AsdaTextSubsystem.h"          // loc-нотисы подбора (1011/1012/1030)
#include "UI/AsdaMarkup.h"                   // UAsdaRichText::ApplyFormatted (единая точка текста)
#include "Engine/TextureRenderTarget2D.h"   // RT эмблемы клана — это UTexture-наследник
#include "World/AsdaClanFlag.h"              // FAsdaClanFlagData::FromWire (флаг клана из 6542)
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "World/AsdaBlendAnimInstance.h"     // ★ 29.10.2026: вздрагивание персонажа от удара моба (AsdaAnimSmooth::StartHitFlinch)
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystemHitInternal.h"   // ★ R2: общие помощники и объявления частей AsdaWorldSubsystem_Hit*.cpp (после включений пролога)

static ASDA_DEV_CVAR(int32, CVarAsdaPvpSeriesGuard,
	TEXT("asda.Combat.PvpSeriesGuard"), 1,
	TEXT("1 (по умолчанию) = PvP-серия атакующего снимается его боем с мобом (4027/4029 и их отказы), значения PvP-очереди снимаются ")
	TEXT("только на кадрах замаха по цели серии, свой поздний 4204 (атаки уже нет или цель другая) выбрасывается. 0 = как до 18.10.2026."),
	ECVF_Default);

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 17.10.2026 СИНХРОНИЗАЦИЯ БОЯ, Ф4 — PvP: ОБЫЧНАЯ АТАКА ИГРОКА ПО ИГРОКУ (ТЗ `05_TZ_PVP_FIELD10112.md`).
//   Атакующий (свой): 4201 по второму клику (контроллер) → 4202 код 1: очередь урона и второго канала → контроллер: подход или
//   замах → в точках отправки замаха 4203 (правило «чётный шаг», у 8–11 — выстрел) → 4204 код 0: очередь дописывается.
//   Чужой атакующий: 4202 — только очередь и цель (признак серии 8, без замаха), 4204 код 0 — замах (Ф2: отрезок клипа от пакета,
//   `PeerAcceptSwingPacket`; признак серии 0x40/8 — как у 4029 соседа, network.c:41539–41620).
//   УДАР: на КАЖДОМ кадре удара клипа атакующего (`ConsumeCrossedHitFrames`) снимается значение очереди — [ОРИГИНАЛ]
//   `playerArrayPopFront` в тике анимации (свой — `CCharacter_ResolveBasicAttack` entity_char.c:26886, чужой —
//   `CCharacter_AttackAnimTick` :2068); пара «урон + спутник» на последнем шаге нечётного комбо ближнего боя (правило
//   `AsdaTakesCritPair`); второй канал — `sub_932DD4/sub_928EB0` (урон навыка «прока»: < 0 — по атакующему).
//   Дальнобойное (8–11): на кадре — выстрел, результат на прилёте (`FireProjectile`, как у моба).
//   HP жертвы: сервер 4058 в PvP не шлёт никому (GameServer.c:131556) — каждый клиент вычитает сам на кадре удара
//   (`CCharacter_TakeDamage` вид 1, entity_char.c:25847: не при состояниях 0/1, HP −= урон, ≥ 0; у своего — HUD).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

// ★ 31.10.2026 (баг 9): `AsdaPlayerController_Move.cpp` — свой 4204 код 0 без 4202 принимает PvP-атаку (0 — не этот случай,
//   1 — принята).
int32 AsdaPvpAcceptByFirstHit(AAsdaPlayerController* PC, int32 TargetUserIdx);

namespace AsdaPvpLocal
{
	// Звуки удара по персонажу (3D у цели) — [ОРИГИНАЛ] `CCharacter_AttackAnimTick` entity_char.c:2219–2233 (обычный) и
	//   :2297–2311 (крит); прочие типы оружия звука на кадре нет (LABEL_155).
	static const TCHAR* HitSound(int32 W, bool bCrit)
	{
		switch (W)
		{
		case 0: case 1: case 4: return bCrit ? TEXT("S_c_scri") : TEXT("S_c_sdamage");
		case 2:                 return bCrit ? TEXT("S_c_spcri") : TEXT("S_c_spnormal2");
		case 3:                 return bCrit ? TEXT("S_c_scri") : TEXT("S_c_sdamage");
		case 5:                 return bCrit ? TEXT("S_c_stcri") : TEXT("S_c_stnormal2");
		default:                return nullptr;
		}
	}
	constexpr float kPvpHitFxScale = 1.5f;   // [ОРИГИНАЛ] `CEffectMgr_scaleAll(…, 1.5)` у удара по персонажу (entity_char.c:2213/2283/2294)
}

void UAsdaWorldSubsystem::PostPvpNotice(int32 LocId, const FColor& Col, const TCHAR* Why)
{
#if !UE_BUILD_SHIPPING
	PvpNoticesSeen.Add(LocId);   // наблюдение самотеста
#endif
	if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		FAsdaMsgParams P;
		P.Category = EAsdaMsgCategory::CombatRefused;   // как отказы 4027/4029 — решение владельца: системный чат
		P.LocId = LocId;
		P.bUseColor = true;
		P.Color = Col;
		Msg->Post(P);
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] нотис %d (%s)"), LocId, Why ? Why : TEXT("?"));
}

void UAsdaWorldSubsystem::PostPvpChatLine(int32 NoticeId, const FString& Name, int32 Num, bool bHasNum, const FColor& Col)
{
#if !UE_BUILD_SHIPPING
	PvpNoticesSeen.Add(NoticeId);   // наблюдение самотеста
#endif
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
	if (!Txt || !Chat) { return; }
	TArray<FString> Names; if (!Name.IsEmpty()) { Names.Add(Name); }
	TArray<int32> Nums; if (bHasNum) { Nums.Add(Num); }
	const FString Line = Txt->FormatById(NoticeId, Names, Nums);
	if (Line.IsEmpty()) { UE_LOG(LogAsdaWorld, Warning, TEXT("[PvP] нотис %d пуст в таблице текста — строка не пошла"), NoticeId); return; }
	// [ОРИГИНАЛ] `CChat_AddTokenizedLines(g_pChatWnd, текст, цвет, 10000, 0)` — дорожка A, 10 000 мс.
	Chat->AddSystemMessage(Line, Col, EAsdaSysTrack::A, 10000.f);
}

FString UAsdaWorldSubsystem::PvpNameOf(int32 UserIdx) const
{
	if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx)) { return E->Name; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return (Net && Net->IsSelfUserIdx(UserIdx)) ? GetSelfCharName() : FString();
}

int32 UAsdaWorldSubsystem::PvpWeaponSubOf(int32 UserIdx) const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	int32 Item = 0;
	if (Net && Net->IsSelfUserIdx(UserIdx)) { Item = Net->SelfWeaponItemId; }
	else if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx)) { Item = E->Parts.Weapon; }
	if (Item <= 0) { return -1; }
	const UAsdaItemTableSubsystem* IT = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	const FAsdaItemRecord* R = IT ? IT->GetItem(Item) : nullptr;
	return R ? R->ItemSubType : -1;
}

void UAsdaWorldSubsystem::ResetPvpSeries(int32 AttackerUserIdx, const TCHAR* Why)
{
	if (FAsdaPvpSeries* S = PvpSeries.Find(AttackerUserIdx))
	{
		if (S->Queue.Num() > 0 || S->TargetUserIdx >= 0)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] серия атакующего %d снята (%s): в очереди было %d"), AttackerUserIdx, Why ? Why : TEXT("?"), S->Queue.Num());
			ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerUserIdx, S->TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP-серия атакующего uidx=%d → %d СНЯТА (%s): очередь была %s"), AttackerUserIdx, S->TargetUserIdx,
				Why ? Why : TEXT("?"), *AsdaCtWorld::Queue(S->Queue));
		}
		PvpSeries.Remove(AttackerUserIdx);
	}
}

void UAsdaWorldSubsystem::ResetPvpSeriesOnMobFight(int32 AttackerUserIdx, const TCHAR* Why)
{
	if (CVarAsdaPvpSeriesGuard.GetValueOnGameThread() == 0 || !PvpSeries.Contains(AttackerUserIdx)) { return; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (Net && Net->IsSelfUserIdx(AttackerUserIdx))
	{
		// Свой во время PvP-атаки моба не бьёт (контроллер снимает PvP-атаку переходом к мобу, `StopPvpAttack`): пакет боя с мобом
		//   здесь — поздний ответ прошлого боя, и живую PvP-серию он не снимает.
		const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
		if (PC && PC->bPvpAttack)
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] %s при своей PvP-атаке по %d — поздний ответ боя с мобом, серия остаётся"), Why ? Why : TEXT("?"), PC->PvpTargetUserIdx);
			return;
		}
	}
	ResetPvpSeries(AttackerUserIdx, Why);
}

bool UAsdaWorldSubsystem::IsPvpAttackerBusy(bool bOwn, const AAsdaPlayerController* PC, const AAsdaPlayerCharacter* Atk) const
{
	if (bOwn && PC && (PC->bPvpAttack || PC->bAutoAttack)) { return true; }   // ожидание ответа (22), подход (15), замах (5)
	return Atk && (Atk->IsAttackLoopAlive() || Atk->IsAttackAnimActive() || Atk->IsSkillAnimActive());
}

int32 UAsdaWorldSubsystem::GetPvpQueueLen(int32 AttackerUserIdx) const
{
	const FAsdaPvpSeries* S = PvpSeries.Find(AttackerUserIdx);
	return S ? S->Queue.Num() : 0;
}

void UAsdaWorldSubsystem::PvpFillSeries(FAsdaPvpSeries& S, const UAsdaNetSubsystem::FAsdaPvpAttackPkt& P, bool bReset)
{
	// [ОРИГИНАЛ] `playerCharObject_ResetUiTickCounters` + `sub_93412F` — сброс очереди урона и второго канала; затем
	//   `addEntriesBounded4` (урон) и `sub_93A291` (второй канал, все четыре записи).
	if (bReset) { S.Queue.Reset(); S.SkIds.Reset(); S.SkDmg.Reset(); }
	int32 Dropped = 0;
	for (const int32 V : P.Dmg) { if (S.Queue.Num() < 8) { S.Queue.Add(V); } else { ++Dropped; } }   // потолок 8 — как у очереди моба
	if (P.Dmg.Num() > 0)
	{
		for (int32 k = 0; k < 4; ++k) { if (S.SkIds.Num() < 8) { S.SkIds.Add(P.SkillIds[k]); S.SkDmg.Add(P.SkillDmg[k]); } }
	}
	if (Dropped > 0)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[PvP] очередь атакующего %d переполнена: отброшено %d значени(й)"), P.AttackerUserIdx, Dropped);
	}
	ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Queue, TEXT("PvP-очередь атакующего uidx=%d → %d: %s+%d из пакета %s%s → %s"), P.AttackerUserIdx, P.TargetUserIdx,
		bReset ? TEXT("СБРОС, ") : TEXT(""), P.Dmg.Num() - Dropped, *AsdaCtWorld::Queue(P.Dmg),
		(Dropped > 0) ? *FString::Printf(TEXT(", ОТБРОШЕНО %d сверх предела 8"), Dropped) : TEXT(""), *AsdaCtWorld::Queue(S.Queue));
	S.TargetUserIdx = P.TargetUserIdx;
}

void UAsdaWorldSubsystem::OnPvpAttackStart(const UAsdaNetSubsystem::FAsdaPvpAttackPkt& P)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const bool bOwn = Net->IsSelfUserIdx(P.AttackerUserIdx);
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
	AAsdaPlayerCharacter* Atk = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(P.AttackerUserIdx));
	// ── Отказы 4202 (network.c:41063–41378): нотис только своему (`field_40`), стоп атакующего. ──
	if (P.Code != 1)
	{
		int32 Loc = 0;          // нотис
		bool bStop = true;      // атакующий уходит в покой
		bool bClearMove = false;// снять бит 2 «запрос хода в пути» (`gameFlagsClear(&g_InventoryBag, 2)`)
		bool bAnyone = false;   // нотис без проверки «атакующий — я» (коды 0xB/0xC эталон печатает безусловно)
		switch (P.Code)
		{
		case 0:   break;                                        // блок/PvP запрещён — молча в покой (`[104334] = 0`)
		case 2:   Loc = 2320; break;                            // перегруз ≥ 90 %
		case 3:   Loc = 2204; break;                            // баллиста не заряжена
		case 5:   Loc = 1074; break;                            // цель недоступна (покой — только НЕ занятому, ниже)
		case 6:   Loc = 1082; bClearMove = true; break;
		case 7:   Loc = 2036; bClearMove = true; break;         // нет боеприпасов (оружие 8–11)
		case 8:   Loc = 2038; bClearMove = true; break;         // статус 15 (езда)
		case 9:   Loc = 1300; bClearMove = true; break;         // безопасная зона
		case 11:  Loc = 112117; bStop = false; bAnyone = true; break;   // предмет тип 10 — без стопа
		case 12:  Loc = 112118; bStop = false; bAnyone = true; break;
		default:  bStop = false; break;                         // код 4 и прочие — эталон не обрабатывает вовсе (default)
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] 4202 отказ: код %d атакующий %d (%s) цель %d"), P.Code, P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"), P.TargetUserIdx);
		ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), bOwn ? EAsdaCT::Refuse : EAsdaCT::Pvp, TEXT("PvP 4202 ОТКАЗ код %d: атакующий uidx=%d (%s) → цель %d, нотис %d, %s"),
			P.Code, P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"), P.TargetUserIdx, Loc, bStop ? TEXT("атакующий в покой") : TEXT("без стопа"));
		// ★ 18.10.2026 (ревизия Ф4, мелочь 4) [ОРИГИНАЛ]: код 5 — покой `CChar_SetState(…, 0, 0, 20)` только при
		//   `!charIsActionStateNonCombat` (network.c:41304) — это МИСНЕЙМ «персонаж ЗАНЯТ» (разбор у `IsPvpAttackerBusy`): занятый
		//   атакующий (свой — ожидание 4202/подход/замах; сосед — замах/навык) остаётся в своём состоянии, снят только признак серии
		//   (`field11C = 0`). Свой в ожидании 4202 выйдет по стражу ответа (`CombatAckTimeout`, [ПОРТ]).
		if (P.Code == 5 && IsPvpAttackerBusy(bOwn, PC, Atk))
		{
			bStop = false;
			if (FAsdaPvpSeries* Sb = PvpSeries.Find(P.AttackerUserIdx)) { Sb->Flags = 0; }
			UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] 4202 код 5: атакующий %d занят — состояние не меняется (эталон network.c:41304)"), P.AttackerUserIdx);
			ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP 4202 код 5: атакующий uidx=%d ЗАНЯТ — покоя нет, снят только признак серии"), P.AttackerUserIdx);
		}
		if (Loc > 0 && (bOwn || bAnyone)) { PostPvpNotice(Loc, UAsdaChatSubsystem::ColRed(), TEXT("отказ 4202")); }
		if (bClearMove && (bOwn || Atk)) { Net->ClearMoveRequestInFlight(TEXT("отказ PvP 4202")); }
		if (bStop)
		{
			if (bOwn && PC) { PC->StopPvpAttack(TEXT("отказ 4202")); }
			else if (Atk) { Atk->StopAttackAnim(); }
			ResetPvpSeries(P.AttackerUserIdx, TEXT("отказ 4202"));
		}
		return;
	}
	// ── Код 1 — атака принята (network.c:41124–41254). ──
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(P.TargetUserIdx));
	ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP 4202 ПРИНЯТО: атакующий uidx=%d (%s) → цель uidx=%d, очередь %s%s"), P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"),
		P.TargetUserIdx, *AsdaCtWorld::Queue(P.Dmg),
		!Tgt ? TEXT(" — цели нет у нас: атакующий в покой") : ((Tgt->IsIncapacitated() || (Atk && Atk->IsIncapacitated())) ? TEXT(" — кто-то из пары мёртв/обездвижен: выход")
			: ((!bOwn && !Atk) ? TEXT(" — атакующего нет у нас: выход") : TEXT(""))));
	if (!Tgt)
	{
		// Цели нет: атакующий (персонаж) — в покой.
		if (bOwn && PC) { PC->StopPvpAttack(TEXT("4202: цели нет")); }
		else if (Atk) { Atk->StopAttackAnim(); }
		ResetPvpSeries(P.AttackerUserIdx, TEXT("4202: цели нет"));
		return;
	}
	if (Tgt->IsIncapacitated() || (Atk && Atk->IsIncapacitated())) { return; }   // [ОРИГИНАЛ] состояние 6 (мёртв) — выход
	if (!bOwn && !Atk) { return; }                                                  // атакующего нет у нас — выход
	FAsdaPvpSeries& S = PvpSeries.FindOrAdd(P.AttackerUserIdx);
	PvpFillSeries(S, P, /*bReset=*/true);
	S.Flags = 0x08;   // `field11C = 8`
	S.bOwn = bOwn;
	S.WeaponSub = PvpWeaponSubOf(P.AttackerUserIdx);
	if (bOwn)
	{
		if (PC) { PC->OnPvpAccepted(P.TargetUserIdx); }   // подход/замах — контроллер (дистанция, 4006/4008, состояние 5)
	}
	else
	{
		// [ОРИГИНАЛ] Чужой (`field_40 == 0`): только очередь и цель, замаха нет — его начнёт 4204 код 0.
		Atk->AttackFaceTarget = Tgt;
		Atk->FaceGameDir(Tgt->GetGamePosXZ() - Atk->GetGamePosXZ());
	}
}

void UAsdaWorldSubsystem::OnPvpHit(const UAsdaNetSubsystem::FAsdaPvpAttackPkt& P)
{
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	const bool bOwn = Net->IsSelfUserIdx(P.AttackerUserIdx);
	AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());
	AAsdaPlayerCharacter* Atk = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(P.AttackerUserIdx));
	AAsdaPlayerCharacter* Tgt = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(P.TargetUserIdx));
	if (!Atk && !bOwn) { return; }   // атакующего у нас нет (network.c:41436: `if (caster)`)
	auto StopCaster = [&](const TCHAR* Why)
	{
		if (bOwn && PC) { PC->StopPvpAttack(Why); }
		else if (Atk) { Atk->StopAttackAnim(); }
		ResetPvpSeries(P.AttackerUserIdx, Why);
	};
	if (!Tgt) { StopCaster(TEXT("4204: цели нет")); return; }   // [ОРИГИНАЛ] network.c:41853 `CChar_SetState(caster, 0, 0, 20)`
	// ── Отказы 4204 (network.c:41630–41848). ──
	if (P.Code != 0)
	{
		int32 Loc = 0;          // нотис (только своему, `CClass_GetField47565` / `field_40`)
		bool bGold = false;     // цвет −11514 (коды 1/3), иначе −65536
		bool bStop = true;      // атакующий уходит в покой
		bool bClearMove = false;// снять бит 2
		switch (P.Code)
		{
		case 1:  Loc = 1024; bGold = true; break;        // цель вне досягаемости
		case 2:  break;                                   // цель под защитой входа (`field_C & 6`) — порт признака не ведёт: ветка «покой»
		case 3:  Loc = 1095; bGold = true; break;        // сбой атаки
		case 4:  Loc = 1082; bClearMove = true; break;
		case 5:  Loc = 1074; break;
		case 6:  Loc = 1082; bClearMove = true; break;
		case 7:  Loc = 2036; bClearMove = true; break;   // нет боеприпасов
		default: bStop = false; break;                    // 8/9/0xB/0xC — эталон не обрабатывает (default, серия не рвётся)
		}
		UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] 4204 отказ: код %d атакующий %d (%s) цель %d"), P.Code, P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"), P.TargetUserIdx);
		ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), bOwn ? EAsdaCT::Refuse : EAsdaCT::Pvp, TEXT("PvP 4204 ОТКАЗ код %d: атакующий uidx=%d (%s) → цель %d, нотис %d, %s"),
			P.Code, P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"), P.TargetUserIdx, Loc, bStop ? TEXT("атакующий в покой") : TEXT("серия не рвётся"));
		// ★ 18.10.2026 (ревизия Ф4, мелочь 4) [ОРИГИНАЛ]: код 5 — как у 4202: покой только НЕ занятому (network.c:41813, тот же
		//   миснейм `charIsActionStateNonCombat`); занятому — только признак серии.
		if (P.Code == 5 && IsPvpAttackerBusy(bOwn, PC, Atk))
		{
			bStop = false;
			if (FAsdaPvpSeries* Sb = PvpSeries.Find(P.AttackerUserIdx)) { Sb->Flags = 0; }
			UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] 4204 код 5: атакующий %d занят — состояние не меняется (эталон network.c:41813)"), P.AttackerUserIdx);
			ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP 4204 код 5: атакующий uidx=%d ЗАНЯТ — покоя нет, снят только признак серии"), P.AttackerUserIdx);
		}
		if (Loc > 0 && bOwn) { PostPvpNotice(Loc, bGold ? UAsdaChatSubsystem::ColGold() : UAsdaChatSubsystem::ColRed(), TEXT("отказ 4204")); }
		if (bClearMove) { Net->ClearMoveRequestInFlight(TEXT("отказ PvP 4204")); }
		if (bStop) { StopCaster(TEXT("отказ 4204")); }
		return;
	}
	// ── Код 0 — удар принят. ──
	ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP 4204 удар ПРИНЯТ: атакующий uidx=%d (%s) → цель uidx=%d, очередь пакета %s%s"), P.AttackerUserIdx, bOwn ? TEXT("я") : TEXT("сосед"),
		P.TargetUserIdx, *AsdaCtWorld::Queue(P.Dmg), Tgt->IsIncapacitated() ? TEXT(" — цель мертва/обездвижена: пакет пропущен") : TEXT(""));
	if (Tgt->IsIncapacitated()) { return; }   // [ОРИГИНАЛ] `Net_GetCurrentMapType(target) != 6` (цель мертва — пакет пропущен)
	// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 2): свой 4204, пришедший после конца своей PvP-атаки (`StopPvpAttack`) или по прежней цели,
	//   серию не заводит: иначе его значения снимал бы следующий замах (по мобу) — ложные 20013 и вычет HP прежней жертвы.
	//   [ОРИГИНАЛ] эталон в покое/подходе по такому пакету ВОЗОБНОВЛЯЕТ атаку (`CharFx_AutoSabzil_State(…, 5, цель)`, network.c:
	//   41456–41493). [ПОРТ] не возобновляем: атаку порта снимают решения самого игрока и гейты контроллера (цель выбыла, режим
	//   выключен, переход к мобу), и поздний пакет не должен их отменять. Ключ отката — `asda.Combat.PvpSeriesGuard 0`.
	if (bOwn && CVarAsdaPvpSeriesGuard.GetValueOnGameThread() != 0 && !(PC && PC->bPvpAttack && PC->PvpTargetUserIdx == P.TargetUserIdx))
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] свой 4204 код 0 по %d вне своей PvP-атаки (атака %s, цель %d) — выброшен, серия не заводится"),
			P.TargetUserIdx, (PC && PC->bPvpAttack) ? TEXT("идёт") : TEXT("снята"), PC ? PC->PvpTargetUserIdx : -1);
		ASDA_CTRACE(EAsdaCT::Pvp, TEXT("свой PvP 4204 по %d ВЫБРОШЕН — вне своей PvP-атаки (атака %s, цель %d): серия не заводится"),
			P.TargetUserIdx, (PC && PC->bPvpAttack) ? TEXT("идёт") : TEXT("снята"), PC ? PC->PvpTargetUserIdx : -1);
		return;
	}
	// ★ 31.10.2026 (баг 9) [ОРИГИНАЛ]: свой 4204 в ожидании ответа на 4201 — сервер обработал первый удар сразу (цель в дальности
	//   оружия ±1 тайл), 4202 не будет: атака принимается этим пакетом, замах сразу (разбор и ключ — у `AsdaPvpAcceptByFirstHit`,
	//   AsdaPlayerController_Move.cpp). Значения пакета идут в очередь в любом случае (ниже).
	if (bOwn && PC) { AsdaPvpAcceptByFirstHit(PC, P.TargetUserIdx); }
	FAsdaPvpSeries& S = PvpSeries.FindOrAdd(P.AttackerUserIdx);
	S.bOwn = bOwn;
	S.WeaponSub = PvpWeaponSubOf(P.AttackerUserIdx);
	if (bOwn)
	{
		// [ОРИГИНАЛ] свой (network.c:41450–41534): очередь дописывается, цель — та же; состояние атаки ведёт контроллер.
		const bool bNewTarget = S.TargetUserIdx >= 0 && S.TargetUserIdx != P.TargetUserIdx;
		PvpFillSeries(S, P, /*bReset=*/bNewTarget);
		return;
	}
	// [ОРИГИНАЛ] чужой (network.c:41535–41620) — признак серии `field11C`: 0x40 — дописать; 8 — дописать и 0x40; иначе — сброс,
	//   наполнение, 0x40; во всех ветках — состояние 5 (замах), если ещё не в нём. [ПОРТ] смена цели — новая серия (как у моба, Ф0).
	const bool bTargetChanged = S.TargetUserIdx >= 0 && S.TargetUserIdx != P.TargetUserIdx;
	bool bNewSeries = false;
	if (bTargetChanged)        { bNewSeries = true; }
	else if (S.Flags & 0x40)   { bNewSeries = false; }
	else if (S.Flags & 0x08)   { bNewSeries = false; }
	else                       { bNewSeries = true; }
	PvpFillSeries(S, P, /*bReset=*/bNewSeries);
	S.Flags = 0x40;
	Atk->AttackFaceTarget = Tgt;
	Atk->FaceGameDir(Tgt->GetGamePosXZ() - Atk->GetGamePosXZ());
	// Замах соседа — отрезок клипа от пакета (Ф2): принятый 4204 = один его 4203. Ключ `SwingPlanFromPackets 0` — замах по времени.
	if (AAsdaPlayerCharacter::IsSwingPlanFromPackets())
	{
		AAsdaPlayerCharacter::FAsdaPeerSwingPlan Plan;   // кадры удара показывает тик PvP (`PvpHitFrameTick`), план — только темп клипа
		Atk->PeerAcceptSwingPacket(S.WeaponSub, IsRangedWeapon(S.WeaponSub), Plan, bNewSeries && Atk->IsAttackLoopAlive());
		ASDA_CTRACE_IF(AsdaCtWorld::Users(P.AttackerUserIdx, P.TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP сосед uidx=%d → %d: отрезок замаха принят%s, старт через %.3f с, длительность %.3f с (кадры удара снимает тик PvP)"),
			P.AttackerUserIdx, P.TargetUserIdx, bNewSeries ? TEXT(" (новая серия)") : TEXT(""), Plan.StartInS, Plan.SegDurS);
	}
	else
	{
		const AAsdaPlayerCharacter::FAsdaComboTiming CT = Atk->GetComboTiming(S.WeaponSub);
		Atk->StartAttackSwing(S.WeaponSub, FMath::Max(CT.bValid ? CT.TotalSec : 1.f, 1.f) + 1.5f);
	}
	Atk->RearmCombatStance();
}

bool UAsdaWorldSubsystem::PvpHitFrameTick(float /*Dt*/)
{
	if (PvpSeries.Num() == 0) { return true; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!Net || !W) { return true; }
	const bool bPvpGuard = CVarAsdaPvpSeriesGuard.GetValueOnGameThread() != 0;   // ★ 18.10.2026 (ВАЖНО 1)
	const AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController());   // свой атакующий
	TArray<int32> Gone;   // атакующие, чьих акторов больше нет
	TArray<int32> Keys; PvpSeries.GetKeys(Keys);
	for (const int32 AtkIdx : Keys)
	{
		FAsdaPvpSeries* S = PvpSeries.Find(AtkIdx);
		if (!S) { continue; }
		AAsdaPlayerCharacter* Atk = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(AtkIdx));
		if (!Atk) { Gone.Add(AtkIdx); continue; }
		TArray<int32> Steps;
		if (Atk->ConsumeCrossedHitFrames(Steps) == 0) { continue; }
		// ★ 18.10.2026 (ревизия Ф4, ВАЖНО 1) [ОРИГИНАЛ]: значение снимает тик замаха по ТЕКУЩЕЙ цели атакующего. Кадры замаха по
		//   другой цели (моб, иная жертва) PvP-очередь не трогают: отсчёт кадров идёт дальше, значения остаются серии.
		if (bPvpGuard)
		{
			const bool bAtTarget = S->bOwn
				? (PC && PC->bPvpAttack && PC->bPvpAccepted && PC->PvpTargetUserIdx == S->TargetUserIdx)
				: (Atk->AttackFaceTarget.IsValid() && Atk->AttackFaceTarget.Get() == ActorByUserIdx(S->TargetUserIdx));
			if (!bAtTarget)
			{
				UE_LOG(LogAsdaWorld, Verbose, TEXT("[PvP] атакующий %d: %d кадр(ов) удара не по цели серии %d — значения не сняты"), AtkIdx, Steps.Num(), S->TargetUserIdx);
				ASDA_CTRACE_IF(AsdaCtWorld::Users(AtkIdx, S->TargetUserIdx), EAsdaCT::Pvp, TEXT("PvP атакующий uidx=%d: %d кадр(ов) удара НЕ по цели серии %d — значения PvP-очереди не сняты"), AtkIdx, Steps.Num(), S->TargetUserIdx);
				continue;
			}
		}
		const int32 W0 = S->WeaponSub;
		const AAsdaPlayerCharacter::FAsdaComboTiming CT = Atk->GetComboTiming(W0);
		const int32 N = CT.bValid ? FMath::Max(1, CT.StepCount) : 1;   // шагов комбо (`field_39C`)
		for (const int32 Step : Steps)
		{
			S = PvpSeries.Find(AtkIdx);   // показ мог снять серию (смерть цели и т.п.)
			if (!S || S->Queue.Num() == 0) { break; }
			int32 Raw = S->Queue[0];
			int32 Crit = 0;
			// Пара «урон + спутник» — последний шаг нечётного комбо ближнего боя (то же правило, что у моба: `AsdaTakesCritPair`).
			int32 Take = 1;
			if (AsdaTakesCritPair(W0, N, Step, S->Queue, 0))
			{
				Crit = S->Queue[1]; Take = 2;
				if (Crit <= -1) { if (Crit < -1) { Raw &= ~0x80000000; } Crit = 0; }   // метка, не число (дефект оригинала D3)
			}
			S->Queue.RemoveAt(0, Take);
			int32 SkId = -1, SkDmg = 0;   // второй канал этого кадра
			if (S->SkIds.Num() > 0) { SkId = S->SkIds[0]; SkDmg = S->SkDmg[0]; S->SkIds.RemoveAt(0); S->SkDmg.RemoveAt(0); }
			const int32 Victim = S->TargetUserIdx;
			ASDA_CTRACE_IF(AsdaCtWorld::Users(AtkIdx, Victim), EAsdaCT::HitFrame, TEXT("PvP КАДР УДАРА шаг %d/%d: атакующий uidx=%d%s → игрок uidx=%d, значение %s%s%s; в очереди осталось %s"),
				Step, N, AtkIdx, S->bOwn ? TEXT(" (я)") : TEXT(""), Victim, *AsdaCtWorld::Hit(Raw),
				(Crit != 0) ? *FString::Printf(TEXT(" + спутник %d"), Crit) : TEXT(""),
				(SkId != -1) ? *FString::Printf(TEXT(", второй канал навык %d урон %d"), SkId, SkDmg) : TEXT(""), *AsdaCtWorld::Queue(S->Queue));
			if (IsRangedWeapon(W0))
			{
				// Выстрел на кадре удара, результат по прилёте (эталон `skillFireArrowProjectile`, entity_char.c:2399).
				AActor* VictimA = ActorByUserIdx(Victim);
				if (!VictimA)
				{
					ASDA_CTRACE_IF(AsdaCtWorld::Users(AtkIdx, Victim), EAsdaCT::Pvp, TEXT("PvP выстрел атакующего uidx=%d по %d НЕ сделан — жертвы нет в мире (значение снято)"), AtkIdx, Victim);
					continue;
				}
				TWeakObjectPtr<UAsdaWorldSubsystem> WeakThis = this;
				FireProjectile(Atk, VictimA, W0, AtkIdx, -1, [WeakThis, AtkIdx, Victim, Raw, Crit, SkId, SkDmg, W0, Step]()
				{
					if (UAsdaWorldSubsystem* WS = WeakThis.Get()) { WS->ResolvePvpHit(AtkIdx, Victim, Raw, Crit, SkId, SkDmg, W0, Step); }
				});
				if (W0 == 8 || W0 == 10) { SetNockedArrow(Atk, true); }
			}
			else { ResolvePvpHit(AtkIdx, Victim, Raw, Crit, SkId, SkDmg, W0, Step); }
		}
	}
	for (const int32 G : Gone) { ResetPvpSeries(G, TEXT("атакующего нет в мире")); }
	return true;
}

void UAsdaWorldSubsystem::ApplyPvpTakeDamage(int32 UserIdx, int32 Dmg, const TCHAR* Src)
{
	// [ОРИГИНАЛ] `CCharacter_TakeDamage` вид 1 (entity_char.c:25864–25878): при состояниях 0 (лёд) / 1 (камень) — урона нет;
	//   HP −= урон, не ниже 0; у своего персонажа — полоса HUD (`UI_UpdateHPBar`) — у порта её ведёт единый источник HP.
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || UserIdx < 0 || Dmg <= 0) { return; }
	if (const UAsdaEffectsSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		if (Fx->FindByTypeSub(UserIdx, 0, 0) || Fx->FindByTypeSub(UserIdx, 1, 0))
		{
			ASDA_CTRACE_UID(UserIdx, EAsdaCT::Pvp, TEXT("PvP урон %d по uidx=%d НЕ вычтен — на жертве лёд/камень (состояния 0/1)"), Dmg, UserIdx);
			return;
		}
	}
	const UAsdaNetSubsystem::FAsdaVitals* V = Net->VitalsByUserIdx.Find(UserIdx);
	if (!V || !V->HasHp())
	{
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[PvP] урон %d по userIdx=%d — витальных значений нет (%s)"), Dmg, UserIdx, Src);
		return;
	}
	ASDA_CTRACE_UID(UserIdx, Net->IsSelfUserIdx(UserIdx) ? EAsdaCT::HpSelf : EAsdaCT::HpPlate, TEXT("PvP урон по uidx=%d%s: HP %d → %d / %d (локальный вычет, %s)"),
		UserIdx, Net->IsSelfUserIdx(UserIdx) ? TEXT(" (я)") : TEXT(""), V->HpCur, FMath::Max(0, V->HpCur - Dmg), V->HpMax, Src);
	Net->SetVitalsHp(UserIdx, FMath::Max(0, V->HpCur - Dmg), -1, Src, /*bServerSnapshot=*/false);
}

void UAsdaWorldSubsystem::ResolvePvpHit(int32 AttackerIdx, int32 VictimIdx, int32 Raw, int32 CritBonus, int32 SkId, int32 SkDmg,
	int32 WeaponSub, int32 Step)
{
	using namespace AsdaPvpLocal;
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaFloatingTextSubsystem* FT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaFloatingTextSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Victim = Cast<AAsdaPlayerCharacter>(ActorByUserIdx(VictimIdx));
	if (!Net || !Victim)
	{
		ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerIdx, VictimIdx), EAsdaCT::Pvp, TEXT("PvP удар %d → %d (%s) НЕ показан — жертвы нет в мире"), AttackerIdx, VictimIdx, *AsdaCtWorld::Hit(Raw));
		return;
	}
	if (Victim->IsIncapacitated())
	{
		ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerIdx, VictimIdx), EAsdaCT::Pvp, TEXT("PvP удар %d → %d (%s) НЕ показан — жертва мертва/обездвижена"), AttackerIdx, VictimIdx, *AsdaCtWorld::Hit(Raw));
		return;
	}   // [ОРИГИНАЛ] `targetChar->field_8 != 6` — по трупу ни цифр, ни урона
	const bool bOwn = Net->IsSelfUserIdx(AttackerIdx);       // я бью (`CCharacter_ResolveBasicAttack`)
	const bool bVictimMe = Net->IsSelfUserIdx(VictimIdx);    // бьют меня (`targetChar->field_40`)
	// Показ надписи: свой удар — без гейта режима (у эталона его там нет; режим 2 глушит сам `SpawnStyled`); чужой — «жертва я или
	//   режим 1» (entity_char.c:2098). Ф5: `ShouldShowDamageText`.
	const bool bShow = bOwn || ShouldShowDamageText(bVictimMe);
	const FString AtkName = PvpNameOf(AttackerIdx);
	const FString VicName = PvpNameOf(VictimIdx);
	int32 ShownStyle = -1;   // стиль показанной надписи (самотест)
	int32 HpDmg = 0;         // урон, ушедший в HP жертвы
	auto Spawn = [&](int32 Style, const FString& Text)
	{
		ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerIdx, VictimIdx), EAsdaCT::DmgText, TEXT("PvP цифра «%s» стиль 0x%X над игроком uidx=%d (бьёт uidx=%d) — %s"), *Text, Style, VictimIdx, AttackerIdx,
			(!bShow || !FT || GetDamageTextMode() == 2) ? TEXT("СКРЫТА режимом показа") : TEXT("показана"));
		if (!bShow || !FT || GetDamageTextMode() == 2) { return; }   // режим 2 — надписей нет вовсе (гейт `SpawnStyled`)
		FT->SpawnOverActor(Style, Text, Victim);
		ShownStyle = Style;
	};
	// ── Второй канал (урон «прока»): `CCharacter_TakeDamage(…, 3)` → только надпись `combatFloatingDamageText` (HP не меняется). ──
	if (SkId != -1 && SkDmg != 0)
	{
		const int32 OnIdx = (SkId < 0) ? AttackerIdx : VictimIdx;   // < 0 — по атакующему
		AActor* OnA = ActorByUserIdx(OnIdx);
		const bool bOnMe = Net->IsSelfUserIdx(OnIdx);
		// [ОРИГИНАЛ] combat_pvp.c:1603–1630 (damageType 0): «персонаж я» — стиль 15 + 20241 (−33517); «бью я / цель — моя
		//   выбранная» — 7 + 20013 (белый); иначе при режиме 1 — 9 без строки. Стиль — общая точка `Damage4102TextStyle`.
		const bool bConcern = bOnMe || bOwn || GetSelectedTargetUserIdx() == OnIdx;
		if (OnA && FT && ShouldShowDamageText(bConcern)) { FT->SpawnOverActor(Damage4102TextStyle(bOwn, bOnMe), FString::FromInt(SkDmg), OnA); }
		if (bOnMe)     { PostPvpChatLine(20241, PvpNameOf(OnIdx), SkDmg, true, AsdaChatColHitOnMe()); }
		else if (bOwn) { PostPvpChatLine(20013, PvpNameOf(OnIdx), SkDmg, true, UAsdaChatSubsystem::ColWhite()); }
	}
	const FColor Red = UAsdaChatSubsystem::ColRed(), White = UAsdaChatSubsystem::ColWhite();
	if (Raw == -1)
	{
		Spawn(bOwn ? 0x6 : 0xE, TEXT("DODGE"));
		if (bOwn) { PostPvpChatLine(20011, VicName, 0, false, White); }            // цель уклонилась
		else if (bVictimMe) { PostPvpChatLine(20014, AtkName, 0, false, Red); }    // я уклонился
	}
	else if (Raw == -2 || Raw == 0 || Raw == -6 || Raw == -7)
	{
		// −2/0 — замыкающий «MISS» (entity_char.c:27207, :2322); −6 — MISS; −7 — MISS ветки навыка (порт, как у моба).
		if (Raw == -6 || Raw == -7 || CVarShowMissOnCode2.GetValueOnGameThread() != 0)
		{
			Spawn(bOwn ? 0x5 : 0xD, TEXT("MISS"));
			if (bOwn) { PostPvpChatLine(Raw == -7 ? 1032 : 1159, FString(), 0, false, White); }
			else if (bVictimMe) { PostPvpChatLine(Raw == -6 ? 1159 : 20015, Raw == -6 ? FString() : AtkName, 0, false, Red); }
		}
	}
	else if (Raw >= -5 && Raw <= -3)
	{
		Spawn(bOwn ? 0x5 : 0xD, TEXT("IMMUNE"));
		if (Raw == -3 && (bOwn || bVictimMe)) { PostPvpChatLine(1092, FString(), 0, false, White); }   // −3: 1092, цвет −1 у обеих ролей
	}
	else
	{
		const bool bCrit = Raw < -1;   // [ОРИГИНАЛ] `dmg >= -1` — обычный, иначе крит (бит 31)
		const int32 Dmg = bCrit ? (CritBonus + (Raw & 0x7FFFFFFF)) : (Raw + CritBonus);
		// [ОРИГИНАЛ] свой удар при состоянии 162 у атакующего — урон ДВУМЯ цифрами (`rngSplitValueTwoParts`, combat_pvp.c:3802:
		//   крит 58…71 %, обычный 40…59 % первой частью), две строки в чат и два вычета.
		const UAsdaEffectsSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>();
		const bool bSplit = bOwn && Fx && Fx->FindByTypeSub(AttackerIdx, 162, 0) != nullptr;
		if (bOwn)
		{
			if (bSplit)
			{
				const int32 H1 = (bCrit ? (FMath::RandRange(0, 13) + 58) : (FMath::RandRange(0, 19) + 40)) * Dmg / 100;
				const int32 H2 = Dmg - H1;
				Spawn(bCrit ? 0x2 : 0x1, FString::FromInt(H1));
				Spawn(0x1, FString::FromInt(H2));
				PostPvpChatLine(bCrit ? 20012 : 20013, VicName, H1, true, White);
				PostPvpChatLine(20013, VicName, H2, true, White);
			}
			else
			{
				Spawn(bCrit ? 0x2 : 0x1, FString::FromInt(Dmg));
				PostPvpChatLine(bCrit ? 20012 : 20013, VicName, Dmg, true, White);
			}
		}
		else
		{
			Spawn(bCrit ? 0xA : 0x9, FString::FromInt(Dmg));
			if (bVictimMe) { PostPvpChatLine(bCrit ? 20243 : 20242, AtkName, Dmg, true, Red); }
		}
		HpDmg = Dmg;
		// ★ 30.10.2026 [ОРИГИНАЛ] ВЗДРАГИВАНИЕ ЖЕРТВЫ PvP-удара (`gfxBias_LazyInitHalfTexel@0xA2C7B0`, разбор мест — заметка flinch_sites_pvp_skills):
		//   ближний бой — сосед бьёт `CCharacter_AttackAnimTick@0x9A2200` (entity_char.c:2169 обычный — без гейта; :2256 крит — только «жертва — я»,
		//   cmp @0x9A31F8), бью я — `CCharacter_ResolveBasicAttack@0xA47DC0` (:27057/:27169, всегда); стрелок по прилёте —
		//   `Combat_ShowDamageResultText@0xA56930` (combat_pvp.c:3185/3229/3349/3397): бью я, или жертва я и у атакующего нет состояния 206.
		if (AsdaAnimSmooth::HitFlinchOn())
		{
			const bool bRangedHit = IsRangedWeapon(WeaponSub);
			const bool bFlinch = bRangedHit ? (bOwn || (bVictimMe && !(Fx && Fx->FindByTypeSub(AttackerIdx, 206, 0))))
			                                : (!bCrit || bOwn || bVictimMe);
			if (bFlinch && Victim->BaseSkel && Victim->BaseSkel->GetSkeletalMeshAsset())
			{
				const bool bStarted = AsdaAnimSmooth::StartHitFlinch(Victim->BaseSkel);
				ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerIdx, VictimIdx), EAsdaCT::Pvp, TEXT("PvP удар %d → %d: вздрагивание жертвы %s (%s, %s)"), AttackerIdx, VictimIdx,
					bStarted ? TEXT("ПУСК") : TEXT("уже идёт"), bRangedHit ? TEXT("стрелок") : TEXT("ближний"), bCrit ? TEXT("крит") : TEXT("обычный"));
			}
		}
		ApplyPvpTakeDamage(VictimIdx, Dmg, bOwn ? TEXT("PvP: мой удар") : TEXT("PvP: удар соседа"));
		// Эффект удара на цели (масштаб 1.5) и 3D-звук у цели.
		FVector O, E; Victim->GetActorBounds(false, O, E);
		if (bCrit)
		{
			// [ОРИГИНАЛ] крит: `eff_0_char_99_cattk01-a_00.jeb` — атакующий я, иначе `…-a_00_1.jeb` (entity_char.c:2283/2294).
			UWorld* Wd = GetGameInstance()->GetWorld();
			const FString Jeb = bOwn ? TEXT("eff_0_char_99_cattk01-a_00") : TEXT("eff_0_char_99_cattk01-a_00_1");
			if (AAsdaEffectActor* Fx2 = Wd ? AAsdaEffectActor::SpawnByJebName(Wd, Jeb, O, FRotator::ZeroRotator, nullptr) : nullptr)
			{
				Fx2->SetActorScale3D(FVector(kPvpHitFxScale));
				Fx2->SetLifeSpan(Fx2->NaturalLifeSpan());
			}
			else
			{
				UE_LOG(LogAsdaWorld, Verbose, TEXT("[PvP] эффект крита BP_Fx_%s не собран — показан импакт обычного удара"), *Jeb.Replace(TEXT("-"), TEXT("_")));
				SpawnImpact(BasicAttackImpactBP(WeaponSub, Step), O, kPvpHitFxScale);
			}
		}
		else { SpawnImpact(BasicAttackImpactBP(WeaponSub, Step), O, kPvpHitFxScale); }
		if (!IsRangedWeapon(WeaponSub))
		{
			if (const TCHAR* Snd = HitSound(WeaponSub, bCrit))
			{
				if (UAsdaSoundSubsystem* Sm = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>()) { Sm->PlaySound3DAs(FName(Snd), O, EAsdaSoundClass::Hit, Victim); }   // ★ 30.10: «удар»
			}
		}
	}
#if !UE_BUILD_SHIPPING
	if (bRecordPvpHits)
	{
		FAsdaPvpHitSeen& H = PvpHitsSeen.AddDefaulted_GetRef();
		H.Attacker = AttackerIdx; H.Victim = VictimIdx; H.Raw = Raw; H.Style = ShownStyle; H.Dmg = HpDmg;
		const UAsdaNetSubsystem::FAsdaVitals* Vv = Net->VitalsByUserIdx.Find(VictimIdx);
		H.HpAfter = Vv ? Vv->HpCur : -1;
		H.AtS = GetGameInstance()->GetWorld() ? GetGameInstance()->GetWorld()->GetTimeSeconds() : -1.0;
	}
#endif
	UE_LOG(LogAsdaWorld, Log, TEXT("[PvP] удар %d → %d: значение %d (+%d), стиль %d, урон в HP %d%s"), AttackerIdx, VictimIdx, Raw, CritBonus,
		ShownStyle, HpDmg, bShow ? TEXT("") : TEXT(" (цифра скрыта режимом показа)"));
	ASDA_CTRACE_IF(AsdaCtWorld::Users(AttackerIdx, VictimIdx), EAsdaCT::Pvp, TEXT("PvP УДАР uidx=%d%s → uidx=%d%s: %s (спутник %d), урон в HP %d, стиль цифры %d"), AttackerIdx, bOwn ? TEXT(" (я)") : TEXT(""),
		VictimIdx, bVictimMe ? TEXT(" (я)") : TEXT(""), *AsdaCtWorld::Hit(Raw), CritBonus, HpDmg, ShownStyle);
}
