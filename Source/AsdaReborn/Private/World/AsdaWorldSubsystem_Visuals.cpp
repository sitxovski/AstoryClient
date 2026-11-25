// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Visuals.cpp — ★ 23.10.2026 часть UAsdaWorldSubsystem, вынесенная из AsdaWorldSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaWorldSubsystem.plan.json).
//   Звания, клан, части тела и причёски, исчезновение сущностей, наземный лут, крылья, свечение оружия, отладочные консоли, примерка экипировки, кольцо цели.
//   #include — те же, что у AsdaWorldSubsystem.cpp, в том же порядке; общее — AsdaWorldSubsystemInternal.h.
#include "World/AsdaWorldSubsystem.h"
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaFadeComponent.h"
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "Items/AsdaGradeColor.h"            // ★ 05.11.2026: drop-палитра `CItemManager_GetDropColor` — имя предмета в строках подбора
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Engine/DataTable.h"
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "AsdaFxNames.h"                     // ★ дедуп №2 В1-C (F5/F6): теги эффектов персонажа, имена одиночных эффект-BP
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "World/AsdaClanFlagData.h"   // FAsdaClanFlagData::FromWire (флаг клана из 6542)   ★ R3: FAsdaClanFlagData, FromWire — узкий заголовок вместо AsdaClanFlag.h
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
// ★ 22.10.2026: окна при своём 4055/4056 — список эталона вместо CloseAll   ★ R3-c: комментарий снятого включения "UI/AsdaEntryWindows.h" (части им не пользуются)
// ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой   ★ R3-c: комментарий снятого включения "UI/AsdaNoticeColors.h" (части им не пользуются)
// ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)   ★ R3-c: комментарий снятого включения "Resources/AsdaAssetPaths.h" (части им не пользуются)
// ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)   ★ R3-c: комментарий снятого включения "World/AsdaMapRegistry.h" (части им не пользуются)
// ★ 15.10.2026: ключ отката ритма (темп предметов), кадры ударов своего замаха   ★ R3-c: комментарий снятого включения "World/AsdaAttackRhythmSubsystem.h" (части им не пользуются)
// ★ системный чат CHAT_NEW_SYSTEM + оверлеи A/B — все лут-строки   ★ R3-c: комментарий снятого включения "UI/AsdaChatSubsystem.h" (части им не пользуются)
// ★ снаряды дальнобойного оружия (projectile_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaProjectileActor.h" (части им не пользуются)
// ★ волна 3 п.14: имена/модели/звук-базы мобов и NPC — одна точка   ★ R3-c: комментарий снятого включения "World/AsdaEntityText.h" (части им не пользуются)
// ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»   ★ R3-c: комментарий снятого включения "World/AsdaWorldKind.h" (части им не пользуются)
// FAsdaHeldTables — удержание таблиц спауна (15.09.2026, dt_gc_hold)   ★ R3-c: комментарий снятого включения "AsdaHeldTables.h" (части им не пользуются)
// питомцы (6120, pet_system_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaPetActor.h" (части им не пользуются)
// ★ компаньон-хранитель «ангел» (6156, soul_guardian_companion_re.md)   ★ R3-c: комментарий снятого включения "World/AsdaSoulGuardianActor.h" (части им не пользуются)
// ★ дедуп №2 В1-C (F10/F16): пути мастеров, имена параметров, экранные ассеты   ★ R3-c: комментарий снятого включения "Resources/AsdaMaterialNames.h" (части им не пользуются)
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3-c: комментарий снятого включения "UI/AsdaMarkup.h" (части им не пользуются)
// RT эмблемы клана — это UTexture-наследник   ★ R3-c: комментарий снятого включения "Engine/TextureRenderTarget2D.h" (части им не пользуются)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3-c: комментарий снятого включения "AsdaOutgoingGate.h" (части им не пользуются)
#include "AsdaWorldSubsystem_Visuals_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Visuals*.cpp

// 6068: ОБА слота титула (префикс+суффикс, эталон NetHandler_UpdateCharGuildTitle) + rankValue → бейдж.
//   ★ Сервер шлёт 6068 и для игроков, чей спавн ещё не пришёл/не дренирован — тогда в кэш до спавна
//   (эталон держит их в очереди g_pCharTitleMgr+228 и дренит, пока чар не найдётся).
void UAsdaWorldSubsystem::OnCharTitle(int32 UserIdx, int32 TitleId0, int32 TitleId1, int32 Rank)
{
	AActor* Target = FindPlayerActor(UserIdx);
	if (!Target)
	{
		PendingTitleByUserIdx.Add(UserIdx, FIntVector(TitleId0, TitleId1, Rank));
		return;
	}
	if (UAsdaNameplateComponent* NP = Target->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->RankValue = Rank;             // эталон: +208972 = rec+6 из того же пакета
		NP->SetTitles(TitleId0, TitleId1);
	}
}

// 6078: только rankValue (эталон charNet_UpdateFieldRefreshPlate: +208972 + перерисовка плашки).
void UAsdaWorldSubsystem::OnCharRank(int32 UserIdx, int32 Rank)
{
	AActor* Target = FindPlayerActor(UserIdx);
	if (!Target)
	{
		FIntVector& P = PendingTitleByUserIdx.FindOrAdd(UserIdx, FIntVector(-1, -1, -1));
		P.Z = Rank;
		return;
	}
	if (UAsdaNameplateComponent* NP = Target->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->SetRank(Rank, NP->RankCat);
	}
}

// 4109 (чужой)/4105 (свой, UserIdx==-1): клан на плашку. Эталон: строка +2410, id +2408, звание +2432.
void UAsdaWorldSubsystem::OnClanInfo(int32 UserIdx, int32 ClanId, const FString& ClanName, const FString& ClanRank)
{
	AActor* Target = nullptr;
	// ★ 27.09.2026 (СРЕДН-1 ревизии friend_gate): 4109 код 1 приходит и ПРИГЛАШЁННОМУ с @3 = его СОБСТВЕННЫЙ userNum.
	//   [ОРИГИНАЛ] `CharManager_FindByUserIdx(@3)` (network.c:12040) находит и своего персонажа (он в том же реестре —
	//   так же 6152 находит себя по charId, social.c:7078) и пишет clanId/имя клана в СВОИ +0x968/+0x96A.
	//   Прежде такой userNum искался только среди соседей — у вступившего клан не появлялся до 4135/перезахода.
	const UAsdaNetSubsystem* NetCI = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (UserIdx < 0 || (NetCI && NetCI->IsSelfUserIdx(UserIdx)))
	{
		SelfClanId = ClanId;
		// ★ 06.10.2026 (R2-СРЕДН-1): зеркало клана своего — id/имя/звание; без клана флаг и дизайн снимаются.
		if (SelfClan.ClanId != ClanId || ClanId < 0) { SelfClan.bFlagOn = false; SelfClan.Flag40.Reset(); }
		SelfClan.ClanId = ClanId; SelfClan.ClanName = ClanName;
		if (!ClanRank.IsEmpty() || ClanId < 0) { SelfClan.ClanRank = ClanRank; }
		bSelfClanKnown = true;
		Target = LiveSelfActor();   // плашка над собой скрыта, но данные держим (панель цели/HUD); ★ IsValid
	}
	else if (FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx))
	{
		E->ClanId = ClanId;
		Target = E->Actor.Get();
	}
	if (!Target) { return; }
	if (UAsdaNameplateComponent* NP = Target->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->SetClan(ClanId, ClanName, ClanRank);
	}
}

// 4133 case 2: сброс клана. Эталон матчит по charId (CharManager_FindByCharId) со сверкой имени персонажа.
void UAsdaWorldSubsystem::OnClanReset(int32 CharId, const FString& CharName)
{
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		FAsdaNetPlayerEntry& E = It.Value;
		const bool bById   = (E.CharId != 0 && E.CharId == CharId);
		const bool bByName = (!CharName.IsEmpty() && E.Name.Equals(CharName));
		if (!bById && !bByName) { continue; }
		E.ClanId = -1;
		if (AActor* A = E.Actor.Get())
		{
			if (UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>())
			{
				NP->SetClan(-1, FString(), FString());
			}
			// ★ вышел из клана — снять и флажок с транспорта (эталон: clanFlagOn обнуляется вместе с кланом)
			if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(A)) { PC->SetClanFlag(false, FAsdaClanFlagData()); }
		}
		return;
	}
	// Локальный игрок: эталон сверяет имя своего персонажа.
	if (!CharName.IsEmpty() && SelfCharName.Equals(CharName))
	{
		SelfClanId = -1;
		SelfClan = FAsdaPendingClan(); bSelfClanKnown = true;   // ★ 06.10.2026: зеркало — «клана нет» (пустой блок эталона)
		if (AActor* Me = LiveSelfActor())   // ★ IsValid, а не «не null»
		{
			if (UAsdaNameplateComponent* NP = Me->FindComponentByClass<UAsdaNameplateComponent>())
			{
				NP->SetClan(-1, FString(), FString());
			}
			if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Me)) { PC->SetClanFlag(false, FAsdaClanFlagData()); }
		}
	}
}

// Применить клан-данные (4135/кэш) к плашке актора — общая точка.
void UAsdaWorldSubsystem::ApplyClanToActor(AActor* A, const FAsdaPendingClan& C)
{
	if (!A) { return; }
	HoldSpawnTables(EAsdaSpawnTables::Clan);   // ★ 15.09 (dt_gc_hold): DT_Clan_Sign/DT_Clan_BorderImage — сильно у мира
	if (UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>())
	{
		NP->SetClan(C.ClanId, C.ClanName, C.ClanRank);
		if (C.Flag40.Num() >= 40)
		{
			NP->SetClanFlag(C.bFlagOn, FAsdaClanFlagData::FromWire(C.Flag40.GetData()));
		}
	}
	// ★ ФЛАГ КЛАНА НА ТРАНСПОРТЕ (clan_flag_mount_re.md): эталон создаёт объект флага ТИПА 1 у любого
	//   персонажа с clanFlagOn (charSpawnLocalPlayer / charSpawnRemoteEntity@0x9D2470) — и у своего,
	//   и у чужих; виден он в мире только пока игрок в седле. Раньше эти данные шли ТОЛЬКО в плашку.
	if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(A))
	{
		// clanId < 0 ⇒ клана нет: эталон обнуляет и блок дизайна (см. SetClan).
		const bool bOn = (C.ClanId >= 0) && C.bFlagOn && C.Flag40.Num() >= 40;
		PC->SetClanFlag(bOn, bOn ? FAsdaClanFlagData::FromWire(C.Flag40.GetData()) : FAsdaClanFlagData());
	}
}

// ★ 4135: снапшот клана по charId — основной путь клана СОСЕДЕЙ (эталон charUpdateVisibleCharsFromPacket:
//   qmemcpy 138 байт в +2408 + пере-бейк эмблемы + charNamePlate_LayoutControls).
void UAsdaWorldSubsystem::OnClanSnapshot(int32 CharId, int32 ClanId, const FString& ClanName,
	const FString& ClanRank, bool bFlagOn, const TArray<uint8>& Flag40)
{
	FAsdaPendingClan C;
	C.ClanId = ClanId; C.ClanName = ClanName; C.ClanRank = ClanRank;
	C.bFlagOn = bFlagOn; C.Flag40 = Flag40;
	// ★ СВОЙ персонаж: снапшот приходит и на свой charId (правило юзера: на себе показываем всё то же).
	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (Net->SelectedCharId != 0 && CharId == Net->SelectedCharId)
		{
			SelfClanId = ClanId;
			// ★ 06.10.2026 (R2-СРЕДН-1): снимок кладём в ПОСТОЯННОЕ зеркало (6153 звания не несёт — прежнее не затираем)
			//   и применяем к ЖИВОМУ павну; павна нет — спавн применит зеркало сам (4135 приходит раньше 4003).
			if (C.ClanRank.IsEmpty() && SelfClan.ClanId == ClanId && ClanId >= 0) { C.ClanRank = SelfClan.ClanRank; }
			SelfClan = C; bSelfClanKnown = true;
			if (AActor* Me = LiveSelfActor()) { ApplyClanToActor(Me, C); }
			return;
		}
	}
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId != CharId) { continue; }
		It.Value.ClanId = ClanId;
		ApplyClanToActor(It.Value.Actor.Get(), C);
		return;
	}
	// Актор ещё не заспавнен (6153/4135 идут в связке ДО дренажа PendingRemotePlayers) — в кэш до спавна.
	//   Эталон делает то же: очередь this+497 дренится, пока чар не найдётся по charId.
	PendingClanByCharId.Add(CharId, MoveTemp(C));
}

// 6542: флаг (эмблема) клана — эталон обходит ВСЕ сущности с +2408 == clanId и перепекает иконку.
void UAsdaWorldSubsystem::OnClanFlag(int32 ClanId, bool bFlagOn, const TArray<uint8>& Flag40)
{
	if (Flag40.Num() < 40) { return; }
	HoldSpawnTables(EAsdaSpawnTables::Clan);   // ★ 15.09 (dt_gc_hold): DT_Clan_Sign/DT_Clan_BorderImage — сильно у мира
	const FAsdaClanFlagData Data = FAsdaClanFlagData::FromWire(Flag40.GetData());
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.ClanId != ClanId) { continue; }
		if (AActor* A = It.Value.Actor.Get())
		{
			if (UAsdaNameplateComponent* NP = A->FindComponentByClass<UAsdaNameplateComponent>())
			{
				NP->SetClanFlag(bFlagOn, Data);
			}
			// ★ и на ФЛАГ НА ТРАНСПОРТЕ (эталон: перебейк RT «clan_flag_<id>.tex» — одна и та же
			//   испечённая эмблема идёт и на плашку, и на полотнище флажка в седле).
			if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(A)) { PC->SetClanFlag(bFlagOn, Data); }
		}
	}
	// Свой клан: данные на своей плашке (скрытой). ★ 06.10.2026: их читает портрет своего персонажа —
	//   `UAsdaMiniStateSubsystem` (эмблема MINISTATE_ClanMark 271, эталон uiDrawPlayerPortrait@0xE81FF0).
	if (SelfClanId == ClanId)
	{
		// ★ 06.10.2026 (R2-СРЕДН-1): флаг — в зеркало клана своего (переживёт портал и пересоздание павна).
		SelfClan.bFlagOn = bFlagOn; SelfClan.Flag40 = Flag40;
		// ★ 06.10.2026 (ревизия R3-МЕЛК-1): 6542 мог прийти раньше первого снимка 4135/6153 сессии — зеркало
		//   отмечаем известным, иначе спавн своего павна флаг бы не применил.
		SelfClan.ClanId = ClanId; bSelfClanKnown = true;
	}
	if (SelfClanId == ClanId && LiveSelfActor())   // ★ IsValid, а не «не null»
	{
		AActor* Me = LiveSelfActor();
		if (UAsdaNameplateComponent* NP = Me->FindComponentByClass<UAsdaNameplateComponent>())
		{
			NP->SetClanFlag(bFlagOn, Data);
		}
		if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Me)) { PC->SetClanFlag(bFlagOn, Data); }
	}
}

// 0xFB5/0xFB7 (4021 «надел» / 4023 «снял»): точечная смена ОДНОЙ части экипа ЧУЖОГО игрока.
//
// ★★★ ИСПРАВЛЕНО 2026-09-02 (аудит К-6). БЫЛО: `partType` разбирался render-part энумом
//   (диспетчер `tagCHARACTER_SetPartsModelByType_NoUpdate@0x9E16E0`, где 6=оружие, 7=броня,
//   8=шлем, 2=грудь, 4=штаны, 5=обувь, 3=перчатки), плюс выдуманная подпорка «принимаем оба
//   энума». СТАЛО: разбираем ЕДИНСТВЕННЫМ верным — ЭКИП-слотом (`EAsdaEquipSlot`).
//   Чем это было плохо: у соседей штаны надевались как нагрудник (2→Chest), обувь как перчатки
//   (3→Gloves), перчатки как штаны (4→Pants), щит как шлем (8→Helmet), а шлем (0) и грудь (1)
//   уходили в `default` и не применялись вовсе.
//
//   Соответствие «значение в пакете → часть» доказано с ЧЕТЫРЁХ независимых сторон:
//     1) СЕРВЕР: `CharPutOnEquip@0x4CD730` (GameServer.c:81808-81872) и `CharPutOffEquip@0x4CD910`
//        (:81877-81921) кладут в тело пакета `a5[1]` байт своей item-таблицы
//        `*(u8*)(dword_5F5E98 + 71*(code-20551) + 26)` — это НОМЕР ЭКИП-СЛОТА, и его же
//        серверный диспетчер обрабатывает набором {0,1,2,3,4,8,9} (`if (a7 == 9)` — ветка оружия);
//     2) ЭТАЛОН, приёмник: `Recv_CharFullSpawn@0xF3E970` (network.c:40637) зовёт
//        `tagCHARACTER_SetPartsModelByType@0x9E18D0`, а тот принимает ровно {0,1,2,3,4,8,9}:
//        0→CCharacter_LoadHelmetPart (ШЛЕМ), 1→SetPartsModel_Chest, 2→SetPartsModel_Pants,
//        3→CCharacter_SetParts_Shose, 4→SetPartsModel_Gloves, 8→CCharacter_SetParts_Armor (ЩИТ),
//        9→CCharacter_SetupModelFromItem (ОРУЖИЕ); значения 6 и 7 у него уходят
//        в `default: result = 0`, то есть в оригинале не делают НИЧЕГО (entity_char.c:10955-11040);
//     3) ЭТАЛОН, источник: `item_inventory.c:22952-22958` кладёт в это же поле
//        `partsModelId = itemRecord->itemType` — энум провода и есть `itemType` = `EAsdaEquipSlot`;
//     4) САМ ПОРТ: `SpawnRemotePlayer` берёт из снапшота 4005/4065 `EquipItemId[0..4]` как
//        шлем/грудь/штаны/обувь/перчатки, `[8]` как щит и `[9]` как оружие — тот же энум.
//   Render-part энум `_NoUpdate@0x9E16E0` эталон зовёт ТОЛЬКО из внутренней пересборки модели
//   литеральными константами (entity_char.c:21157-21193, 44173-44205) — к пакету 4021 он
//   отношения не имеет, и прежний комментарий порта ссылался на неверную функцию.
//   Подпорка «принимаем оба энума» снята: сервер второго варианта не шлёт ни в одной ветке.
//   Правка КЛИЕНТСКАЯ — меняется только трактовка уже читаемого поля, провод не затронут.
void UAsdaWorldSubsystem::OnCharPartUpdate(int32 UserIdx, int32 PartType, int32 ItemId, uint8 Grade)
{
	FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return; }   // self/несозданный — экип self в мире обновляется своей системой (фаза снаряги)
	AAsdaPlayerCharacter* Ch = E->Actor.Get();
	if (!Ch) { return; }

	// ★ ЗАТОЧКА (свечение): 4-й параметр = grade (эталон network.c:40631 → инстанс+0x18), не «цвет».
	//   Прежний порт определял «оружие это или щит» по `itemType` из НАШЕЙ item-БД. Отказались:
	//   авторитет — сервер (закон `client-server-table-desync`), а на проводе уже лежит его
	//   собственный номер слота; лишний просмотр таблицы мог только разойтись с ним.
	bool bModelChanged = false;   // менялась ли собираемая часть тела → нужна пересборка CharAssembly
	bool bGlowChanged  = false;   // менялось ли оружие/щит → пересчёт свечения заточки
	switch (PartType)
	{
	case (int32)EAsdaEquipSlot::Helmet: E->Parts.Helmet = ItemId; bModelChanged = true; break;
	case (int32)EAsdaEquipSlot::Chest:  E->Parts.Chest  = ItemId; bModelChanged = true; break;
	case (int32)EAsdaEquipSlot::Pants:  E->Parts.Pants  = ItemId; bModelChanged = true; break;
	case (int32)EAsdaEquipSlot::Boots:  E->Parts.Shoes  = ItemId; bModelChanged = true; break;
	case (int32)EAsdaEquipSlot::Gloves: E->Parts.Gloves = ItemId; bModelChanged = true; break;
	case (int32)EAsdaEquipSlot::Shield:
		// ★ 25.10.2026 [ОРИГИНАЛ] Щит соседа: эталонный `CCharacter_SetParts_Armor@0x9EB3C0` рисует МЕШ щита
		//   ("_7_a1" / за спиной "_2_a1") — теперь он в `FAsdaWornParts::Shield` и собирается общей пересборкой.
		//   `ShieldItemId`/`ShieldGrade` остаются источником свечения заточки (как было).
		E->ShieldItemId = ItemId; E->ShieldGrade = (int32)Grade; bGlowChanged = true;
		E->Parts.Shield = ItemId; bModelChanged = true;
		break;
	case (int32)EAsdaEquipSlot::Weapon:
		E->Parts.Weapon = ItemId; E->WeaponGrade = (int32)Grade;
		bModelChanged = true; bGlowChanged = true;
		break;
	default:
		// Прочие значения (5, 6, 7, 10 и выше) эталонный диспетчер @0x9E18D0 отдаёт в
		// `default: result = 0` — оригинал по ним не делает ничего, и мы тоже.
		UE_LOG(LogAsdaWorld, Log, TEXT("[net] partType=%d userIdx=%d — эталон по этому экип-слоту модель не трогает"),
			PartType, UserIdx);
		return;
	}
	if (bGlowChanged)
	{
		ApplyWeaponGlowToActor(Ch, E->Parts.Weapon, E->WeaponGrade, E->ShieldItemId, E->ShieldGrade, UserIdx);
	}
	if (bModelChanged)
	{
		Ch->InitAppearance(E->Sex, E->Parts);   // пересборка частей (CharAssembly)
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[net] экип userIdx=%d: экип-слот=%d → itemId=%d заточка=%d применён"),
		UserIdx, PartType, ItemId, (int32)Grade);
}

// ═══════════════ ПРИЧЁСКА: своя (5471) и чужая (5472) ═══════════════════════════════════════════
//   Реверс — `Reports/npc_hairshop_30029.md` §5.4/§5.5. Оба пути ведут в ОДНУ пересборку
//   внешности, потому что в порту меш волос строит `UAsdaCharAssembly::ApplyHair` по паре
//   (HairStyle, ColorIdx) через `hairtable`.
void UAsdaWorldSubsystem::ApplySelfHair(int32 HairStyle, int32 ColorIdx)
{
	if (HairStyle < 0) { return; }
	if (SelfParts.HairStyle == HairStyle && SelfParts.HairColor == ColorIdx) { return; }
	SelfParts.HairStyle = HairStyle;
	SelfParts.HairColor = ColorIdx;
	if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get()))
	{
		// ★ Модель ИГРОКА собираем снимком С УЧЁТОМ примерочных оверрайдов (`PushEquipTryOn`):
		//   сам снимок остаётся зеркалом авторитета, примерка живёт отдельным слоем.
		Me->InitAppearance(SelfSex, VisualSelfParts());
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[причёска self] стиль=%d цвет=%d — модель пересобрана"),
		HairStyle, ColorIdx);
}

void UAsdaWorldSubsystem::ApplyRemoteHair(int32 UserNum, int32 HairStyle, int32 ColorIdx)
{
	if (HairStyle < 0) { return; }
	// ★ Свой же userNum может прийти и в броадкасте — тогда идём своей веткой (эталон рассылает
	//   пакет ВСЕЙ зоне через `sub_52E6D0`, отправителя он не исключает).
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (Net->IsSelfUserIdx(UserNum)) { ApplySelfHair(HairStyle, ColorIdx); return; }
	}
	FAsdaNetPlayerEntry* E = NetPlayers.Find(UserNum);
	if (!E) { UE_LOG(LogAsdaWorld, Log, TEXT("[причёска] 5472 userNum=%d — актора нет в зоне"), UserNum); return; }
	E->Parts.HairStyle = HairStyle;
	E->Parts.HairColor = ColorIdx;
	if (AAsdaPlayerCharacter* Ch = E->Actor.Get()) { Ch->InitAppearance(E->Sex, E->Parts); }
	UE_LOG(LogAsdaWorld, Log, TEXT("[причёска] userNum=%d стиль=%d цвет=%d применено"),
		UserNum, HairStyle, ColorIdx);
}

// ═══════════════ ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА: свой и чужой ═══════════════════════════════════════════
//   Эталон: байт `CCharacter+0x32F78` + `CCharacter_ApplyEquippedHelmet@0x9E3360` → `CCharacter_LoadHelmetPart(char,
//   надетый шлем)`; при 0 шлем не грузится и ставится обычная причёска (`LoadHairModel(char, 0)`, entity_char.c:12236).
//   В порту — флаг снимка внешности и та же единственная пересборка, что у причёски и смены экипировки.
void UAsdaWorldSubsystem::ApplySelfHelmetShow(bool bShow, const TCHAR* Src)
{
	if (SelfParts.bHelmetShown == bShow)
	{
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[шлем self] %s: показ=%d — без изменений"), Src, bShow ? 1 : 0);
		return;
	}
	SelfParts.bHelmetShown = bShow;
	if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get()))
	{
		// Снимок С УЧЁТОМ примерки (`VisualSelfParts`): флаг лежит в `SelfParts` и переходит в копию как есть.
		Me->InitAppearance(SelfSex, VisualSelfParts());
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("[шлем self] %s: показ=%d (шлем itemId=%d) — модель пересобрана"),
		Src, bShow ? 1 : 0, SelfParts.Helmet);
}

void UAsdaWorldSubsystem::ApplyRemoteHelmetShow(int32 UserIdx, bool bShow)
{
	// Эталон ветки 111 различает своего (`effectSlots[112].field_40`) и чужого только для глобали `byte_2FFAFA4`
	//   (читателей в срезе нет), байт персонажа пишет обоим. Сервер отправителю 4025 не шлёт, но ветку держим.
	if (const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (Net->IsSelfUserIdx(UserIdx)) { ApplySelfHelmetShow(bShow, TEXT("4025 действие 111")); return; }
	}
	FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E)
	{
		// Эталон: `if (lockChar) { … }` — без персонажа ничего; появившись, сосед принесёт флаг в 4005 [6].
		UE_LOG(LogAsdaWorld, Log, TEXT("[шлем] 4025/111 userIdx=%d показ=%d — соседа нет в зоне"), UserIdx, bShow ? 1 : 0);
		return;
	}
	if (E->Parts.bHelmetShown == bShow) { return; }
	E->Parts.bHelmetShown = bShow;
	if (AAsdaPlayerCharacter* Ch = E->Actor.Get()) { Ch->InitAppearance(E->Sex, E->Parts); }
	UE_LOG(LogAsdaWorld, Log, TEXT("[шлем] userIdx=%d показ=%d (шлем itemId=%d) — модель пересобрана"),
		UserIdx, bShow ? 1 : 0, E->Parts.Helmet);
}

bool UAsdaWorldSubsystem::GetRemoteHelmetShow(int32 UserIdx, bool& bOutShow) const
{
	const FAsdaNetPlayerEntry* E = NetPlayers.Find(UserIdx);
	if (!E) { return false; }
	bOutShow = E->Parts.bHelmetShown;
	return true;
}

void UAsdaWorldSubsystem::OnEntityDespawn(int32 RenderId)
{
	// ★ ЭТАЛОН (Recv_LogOutPacket@0xF3AE00): 4011 ищет ТОЛЬКО в игроцких бакетах CharManager_FindByUserIdx
	//   @0x9D1240 (stride 236512) → удаляет ТОЛЬКО ИГРОКОВ. Мобов/NPC оригинал этим пакетом НЕ трогает
	//   (сервер шлёт 4011 и при смерти моба — клиент игнорирует): смерть моба = 4015 newHp<=0 (OnCombatHit,
	//   анимация смерти + фейд трупа), уход из AoI = таймаут 10с (TickAoiDespawn).
	FAsdaNetPlayerEntry PE;
	if (NetPlayers.RemoveAndCopyValue(RenderId, PE))
	{
		// ★ 17.10.2026 (Ф1, ревизия Ф0 п.11): сосед ушёл из видимости — его серия, очередь и записи ленты попаданий снимаются
		//   (назначенные ответом попадания доживают: их цифры уже стоят в таймерах).
		ASDA_CTRACE_UID(RenderId, EAsdaCT::Peer, TEXT("4011 игрок uidx=%d '%s' ушёл из видимости — его серия, очередь ударов, PvP-серия и записи ленты снимаются"), RenderId, *PE.Name);
		PeerSeriesFlags.Remove(RenderId); PeerSeriesTargetRid.Remove(RenderId); PeerSeriesGen.Remove(RenderId);
		// ★ Ф2 (ревизия Ф1 п.9): очередь ударов ушедшего соседа — под ключом наблюдателя (откат `ObserverSync 0` — как до 17.10).
		if (CVarAsdaObserverSync.GetValueOnGameThread() != 0) { DmgBuffers.Remove(RenderId); }
		ResetPvpSeries(RenderId, TEXT("4011: атакующий ушёл из видимости"));   // ★ 18.10.2026 (ревизия Ф4): его PvP-серия — тоже
		CutImpactEntries(RenderId, TEXT("атакующий ушёл из видимости"));
		if (AAsdaPlayerCharacter* A = PE.Actor.Get())
		{
			UE_LOG(LogAsdaWorld, Log, TEXT("[net] деспавн игрока userIdx=%d '%s' (фейд-аут)"), RenderId, *PE.Name);
			// ★ Эффекты гасим СРАЗУ (крылья/кольцо не фейдятся вместе с телом — иначе секунду висят
			//   ярким пятном над исчезающим игроком); сам актор уходит плавно.
			ApplyWingToActor(A, PE.Sex != 2, -1, false);
			ApplySoulRingToActor(A, false);
			// ★ Свечение заточки гасим сразу вместе с крыльями/кольцом — иначе секунду висит над
			//   исчезающим игроком (та же грабля, что была у крыльев).
			ApplyWeaponGlowToActor(A, 0, 0, 0, 0, RenderId);
			ApplyPetToActor(A, -1, FString());   // питомец уходит вместе с хозяином
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F1): фейд-аут ~1с → Destroy через общую точку `Begin`. Было: второй
			//   `NewObject` поверх ещё идущего фейд-ина появления (уход раньше 1,5 с) — два компонента каждый кадр пересоздавали
			//   MID друг поверх друга («альфа вверх-вниз», мусор в GC). `BeginFade` сам перезапускается поверх фейд-ина.
			UAsdaFadeComponent::Begin(A, /*bIn*/false, 1.0f);
		}
		// ★★★ ЧИСТКА КЭШЕЙ СОСТОЯНИЙ (баг 2026-07-31: «вышел игрок на транспорте — следующий вошедший
		//   появился верхом на ЕГО транспорте»). Причина: сервер ПЕРЕИСПОЛЬЗУЕТ userIdx (слот сессии),
		//   а наши кэши «применить при спавне» жили вечно — новый игрок в том же слоте наследовал
		//   транспорт/крылья/питомца предыдущего. Эталон такой проблемы не имеет: Recv_LogOutPacket@0xF3AE00
		//   удаляет ОБЪЕКТ персонажа целиком, вместе со всеми его состояниями — держать их отдельно
		//   от сущности незачем. Чистим по ОБОИМ ключам: userIdx (слот) и charId (персонаж).
		MountByUserIdx.Remove(RenderId);
		SoulByUserIdx.Remove(RenderId);   // партнёра НЕ трогаем: пара существует, у него кольцо остаётся
		if (PE.CharId != 0)
		{
			WingByCharId.Remove(PE.CharId);
			PetByCharId.Remove(PE.CharId);
			FactionByCharId.Remove(PE.CharId);
			SoulIdxByCharId.Remove(PE.CharId);
		}
		// ★★★ Одержимость живёт в сетевом слое (кэши 6152/5220) — чистим ТЕМ ЖЕ правилом и по обоим
		//   ключам, иначе метка «(Одержим)» и эталонный цвет имени перейдут следующему игроку,
		//   занявшему освободившийся слот userIdx.
		if (UAsdaNetSubsystem* NetS = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			NetS->SoulStateByUserIdx.Remove(RenderId);
			if (PE.CharId != 0) { NetS->SoulStateByCharId.Remove(PE.CharId); }
		}
		// Игрок ушёл: разрешить будущий повторный 5416, если он снова появится рядом.
		if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->RequestedUserInfo.Remove(RenderId);
		}
		return;
	}
	if (NetMobs.Contains(RenderId) || NetNpcs.Contains(RenderId))
	{
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[net] 4011 rid=%d — моб/NPC: игнор (эталон; смерть=4015, уход=AoI)"), RenderId);
	}
}

// ═══════════════ ★ КРЫЛЬЯ Plus Package (6585) + КОЛЬЦА SOULMATE (5220) — wings_soulmate_re.md ═══════════════
// Уничтожить attached-эффект персонажа по тегу (смена состояния/повторное применение).
// ★ Дедуп №2 В1-C (F6): тело вынесено в общую точку `AsdaCharFxTags::DestroyAttached` (AsdaFxNames.h) — ею же гасится
//   свечение заточки на смерти (`AAsdaPlayerCharacter`); прежняя функция файла была её дословной копией с одним тегом.
// ★ Аудит №3 (A11): параметр `FollowBone` УБРАН, вместе с ним — сырая настройка слежения
//   (`FollowActor`/`FollowBone`/`FollowOffset`/`bFollowPosition` прямо в полях актора). Она была
//   ЛИШНЕЙ и вдобавок неполной: все вызывающие с костью тут же перенастраивали эффект через единую
//   точку `ConfigureBoneAttach`, а флаги режима (`bStrictQuadAttach`/`bFollowBoneFull`) выставлялись
//   только этим вторым вызовом. Теперь привязка задаётся РОВНО в одном месте — у вызывающего.
AActor* UAsdaWorldSubsystem::SpawnCharFx(const FString& JebBase, AActor* Owner,
	const FVector& Offset, FName Tag)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W || !Owner) { return nullptr; }
	// Д4: единый спавн эффект-BP (имя + LoadClass + SpawnActor) — общий с эффектами хранителя.
	AActor* Fx = AAsdaEffectActor::SpawnByJebName(W, JebBase, Owner->GetActorLocation() + Offset, FRotator::ZeroRotator, Owner);
	if (!Fx)
	{
		UE_LOG(LogAsdaWorld, Warning, TEXT("[char-fx] BP не найден/не заспавнен: BP_Fx_%s (запусти asda_build_char_fx.py)"),
			*JebBase.Replace(TEXT("-"), TEXT("_")));
		return nullptr;
	}
	Fx->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	Fx->Tags.Add(Tag);
	// ★ ФИКС «эффекты оружия дёргаются/отстают при беге» (2026-08-01): ВСЕ рантайм-эффекты НА НОСИТЕЛЕ
	//   (свечение заточки, крылья, кольца соулмейта, шлейф снаряда) тикают ПОСЛЕ движения носителя и
	//   обновления его поз — иначе геометрия строится по позе предыдущего кадра (отставание ~скорость/FPS
	//   плюс дрожание от неопределённого порядка акторов внутри TG_PrePhysics). Эффекты карт этот путь
	//   не проходят и остаются как были (флаг по умолчанию выключен).
	if (AAsdaEffectActor* EO = Cast<AAsdaEffectActor>(Fx))
	{
		EO->bFollowTickAfterPose = true;
		EO->ApplyFollowTickOrder();
	}
	// ★★★ [ПОРТ] GM-«СКРЫТЬСЯ»: НОСИТЕЛЬ УЖЕ СПРЯТАН — прячем и новорождённый эффект.
	//   Эффекты появляются и умирают на ходу (смена оружия, бафф, крылья из пакета), и скрытый
	//   оператор иначе «проявлялся» бы кусками: тела нет, а свечение меча зажглось. У эталона такой
	//   развилки нет — там эффект живёт внутри персонажа и его бит показа общий (`char+0x40C`).
	//   Прозрачность СВОЕГО персонажа этой строки не касается: там носитель не скрыт, а множитель
	//   раздаётся секундным подхватом `AAsdaPlayerCharacter::TickSoulTranslucency`.
	if (Owner->IsHidden()) { Fx->SetActorHiddenInGame(true); }
	// Привязку к кости ставит ВЫЗЫВАЮЩИЙ через `ConfigureBoneAttach` — см. шапку метода (Аудит №3, A11).
	return Fx;
}

// Таблица PlusPackage (эталон CPlusPackage_LoadTable@0xEE3440): header 20 (count@4), записи 140б:
//   id i32@0, noticeId i32@4/@8, jeb-база муж char[64]@12, жен char[64]@76.
// ★ ИСТОЧНИК = АССЕТ `/Game/AsdaAssets/Data/DT_PlusPackage` (импортёр Tools/asda_build_plus_package_dt.py).
//   Раньше рантайм читал PlusPackageTable.BIN из Content/ — в шипнутом паке этого файла НЕТ (кукер
//   пакует только .uasset/.umap) ⇒ крылья молча сваливались бы на фолбэк-модель.
bool UAsdaWorldSubsystem::LookupWingJeb(int32 WingId, bool bMale, FString& OutJebBase)
{
	if (!bPlusPackageLoaded)
	{
		bPlusPackageLoaded = true;
		// ★ Аудит №3 (A3, дубль данных Дд3): таблицу держит `UAsdaItemTableSubsystem` (`GExtraTables`).
		//   Это была третья её загрузка (ещё две — в мини-статусе); свой `LoadObject` = вторая копия строк.
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UDataTable* T = IT ? IT->GetDataTable(FName(TEXT("PlusPackage"))) : nullptr;
		if (T)
		{
			for (const TPair<FName, uint8*>& It : T->GetRowMap())
			{
				const FAsdaPlusPackageRow* R = reinterpret_cast<const FAsdaPlusPackageRow*>(It.Value);
				if (!R) { continue; }
				PlusPackageTable.Add(R->Id, TPair<FString, FString>(R->JebMale, R->JebFemale));
			}
			UE_LOG(LogAsdaWorld, Log, TEXT("[крылья] DT_PlusPackage: %d записей"), PlusPackageTable.Num());
		}
		else { UE_LOG(LogAsdaWorld, Warning, TEXT("[крылья] DT_PlusPackage не найден — фолбэк-крылья")); }
	}
	if (const TPair<FString, FString>* P = PlusPackageTable.Find(WingId))
	{
		OutJebBase = bMale ? P->Key : P->Value;
		if (!OutJebBase.IsEmpty()) { return true; }
	}
	// Фолбэк эталона (dword_15B5C08=0) с поправкой на наш сервер: малые крылья по полу.
	OutJebBase = bMale ? TEXT("eff_wing_m_ss_00") : TEXT("eff_wing_f_ss_00");
	return true;
}

void UAsdaWorldSubsystem::ApplyWingToActor(AActor* Ch, bool bMale, int32 WingId, bool bOn)
{
	if (!Ch) { return; }
	static const FName kWingTag(AsdaCharFxTags::kWing);   // ★ В1-C F6
	AsdaCharFxTags::DestroyAttached(Ch, kWingTag);
	// ★ Аним-трек 35 (эталон §A1, charUpdateActionStateTick@0xA216B0): при крыльях бег-клип подменяется
	//   move_00 «бег со взмахами» (ассет previewanim_35); SetWingsOn обновит клип, если персонаж уже бежит.
	if (AAsdaPlayerCharacter* PC = Cast<AAsdaPlayerCharacter>(Ch)) { PC->SetWingsOn(bOn); }
	if (!bOn) { return; }
	FString JebBase;
	LookupWingJeb(WingId, bMale, JebBase);
	// Кость крепления (эталон List_FindIndexByName: "M" муж / "G" жен). НАШ СКЕЛЕТ: экспортёр
	// (export_char_cremmy_showcase.load_clips) ремапит женские root-кости G→M, G_DUMMY→M_DUMMY,
	// чтобы женские клипы легли на общий скелет cre_cremmy ⇒ имени "G" в ассете нет, но это ТА ЖЕ
	// кость крыла (сверено с ch4_f_00_*.jbb: позы/матрицы совпадают 1:1, отчёт §H5).
	// Раньше DoesSocketExist("G") тихо проваливался → крыло замирало У НОГ в мировой ориентации.
	FName Bone = bMale ? FName(TEXT("M")) : FName(TEXT("G"));
	if (const USkeletalMeshComponent* Sk = Ch->FindComponentByClass<USkeletalMeshComponent>())
	{
		if (Sk->GetBoneIndex(Bone) == INDEX_NONE)
		{
			const FName Alt(TEXT("M"));
			UE_LOG(LogTemp, Warning, TEXT("[Wing] кости '%s' нет в скелете '%s' (%s) — беру '%s' (ремап экспортёра G→M, кость та же)"),
				*Bone.ToString(), *GetNameSafe(Sk->GetSkeletalMeshAsset()), bMale ? TEXT("муж") : TEXT("жен"), *Alt.ToString());
			Bone = Alt;
			if (Sk->GetBoneIndex(Bone) == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Wing] фолбэк-кость '%s' тоже отсутствует — крыло НЕ будет следовать за костью!"), *Alt.ToString());
			}
		}
	}
	if (AActor* Fx = SpawnCharFx(JebBase, Ch, FVector::ZeroVector, kWingTag))
	{
		// ★ Д1: 1:1 с эталоном (attach-10) через ЕДИНУЮ точку. Крылья: без strictQuad, skel по
		//   FindComponentByClass (nullptr), FollowOffset = WingBoneOffset (правится в редакторе).
		if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(Fx))
		{
			E->ConfigureBoneAttach(Ch, /*Skel*/nullptr, Bone, EAsdaFxAttachMode::Wing, WingBoneOffset);
			// ★ 19.09.2026 (glow_fix5, С2): крылья в группу свечения НЕ входят — `bGlowWithCarrier` не ставится.
			//   Эталон `CCharacter_UpdateWingEffect@0xA4BAA0`: `Effect_SpawnByName` менеджера 0x349E860 + узел в
			//   очередь 0x3537FA8 (`CRenderQueue_AddNode`), её рисует `renderShadowMeshQueue@0x97E870` на шаге «5.1.5»
			//   — ПОСЛЕ сведения свечения (шаг «5»). Разбор — у объявления `AAsdaEffectActor::bGlowWithCarrier`.
		}
	}
}

// ★ Временно скрыть/вернуть крылья СВОЕГО персонажа. Показ чисто локальный: серверу ничего не
//   уходит, срок действия Plus Package не трогается, соседи крылья видят по-прежнему.
//   Возврат берёт состояние из последнего пакета 6585 — крыло, которого нет, не появится.
void UAsdaWorldSubsystem::SetLocalWingsHidden(bool bHide)
{
	if (bLocalWingsHidden == bHide) { return; }
	bLocalWingsHidden = bHide;

	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get());
	if (!Net || !Me)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[крылья] показ %s (персонажа в мире пока нет)"),
			bHide ? TEXT("выключен") : TEXT("включён"));
		return;
	}
	const FAsdaWingState* St = WingByCharId.Find((int32)Net->SelectedCharId);
	const bool bHasWing = (St && St->bOn);
	const int32 WingId  = St ? St->WingId : -1;

	ApplyWingToActor(Me, Me->Sex != 2, WingId, bHasWing && !bHide);
	UE_LOG(LogAsdaWorld, Log, TEXT("[крылья] показ %s (крыло по данным сервера: %s, id=%d)"),
		bHide ? TEXT("ВЫКЛЮЧЕН") : TEXT("ВКЛЮЧЁН"),
		bHasWing ? TEXT("надето") : TEXT("нет"), WingId);
}

void UAsdaWorldSubsystem::OnWingState(int32 CharId, int32 WingId, int32 /*RemainSec*/, bool bOn)
{
	FAsdaWingState& S = WingByCharId.FindOrAdd(CharId);
	S.WingId = WingId; S.bOn = bOn;
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	// Свой персонаж.
	if (AsdaSelf::IsSelfCharId(Net, CharId))   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, N2): «charId мой?» — `AsdaSelf::IsSelfCharId` (свой ещё неизвестен ⇒ не мой; было 0 == 0 ⇒ мой)
	{
		if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(LocalPlayerActor.Get()))
		{
			// ★ Если владелец временно скрыл крылья (`asda.Wings.Hide 1`), новый пакет их не
			//   возвращает: состояние запомнено в WingByCharId и применится, когда скрытие снимут.
			ApplyWingToActor(Me, Me->Sex != 2, WingId, bOn && !bLocalWingsHidden);
		}
		return;
	}
	// Чужой игрок по charId (снапшот 4005/4065 принёс CharId в реестр).
	for (TPair<int32, FAsdaNetPlayerEntry>& It : NetPlayers)
	{
		if (It.Value.CharId == CharId)
		{
			if (AAsdaPlayerCharacter* A = It.Value.Actor.Get())
			{
				ApplyWingToActor(A, It.Value.Sex != 2, WingId, bOn);
			}
			return;
		}
	}
	// Актора ещё нет — применится при спавне из WingByCharId.
}
