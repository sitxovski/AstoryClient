// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_Spawn.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem.cpp, дословный перенос): появление своего персонажа: OnSelfSpawn, TrySpawnLocalPlayer.
//   Общее для частей — в AsdaWorldSubsystem_Private.h; шапка и реверс — в AsdaWorldSubsystem.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер

#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaMapRegistry.h"   // ★ волна 2 (14.09.2026): реестр карт — генерируемый заголовок из Tools/data/asda_map_registry.json (путь DA_WalkGrid)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWalkGrid.h"
#include "Characters/AsdaWornParts.h"   // кости/attach-матрицы оружия «в руке ↔ за спиной»   ★ R3: FAsdaWornParts — узкий заголовок вместо AsdaCharAssembly.h
#include "Camera/CameraComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaNpcActor.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
// тип оружия атакующего (импакт battk01..04)   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "UI/AsdaEffectsSubsystem.h"         // ★ состояния мобов: 4017 бафф-стеки и бафф отражения
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "ProceduralMeshComponent.h"               // ★ 09.11: площадка города по рельефу
#include "StaticMeshResources.h"                   // ★ 09.11: вершины/UV квада площадки
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaWorldKind.h"            // ★ F7 (дедуп №2, В3): общие признаки «логин» / «загрузка»
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "World/AsdaPetActor.h"              // питомцы (6120, pet_system_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
// loc-нотисы подбора (1011/1012/1030)   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
// UAsdaRichText::ApplyFormatted (единая точка текста)   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"             // DecodeCp1251 (имена монстров в cp1251)
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem*.cpp

// ★ [ИСПРАВЛЕНИЕ 05.11.2026] Ключ отката очистки реестров на 4003 (разбор — в `OnSelfSpawn`): 1 — живые акторы текущего мира остаются
//   в реестрах; 0 — прежнее: все записи стираются.
static ASDA_DEV_CVAR(int32, CVarSelfSpawnKeepLive,
	TEXT("asda.World.SelfSpawnKeepLive"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] 1 = на 4003 из реестров мира уходят только записи погибших/чужих миров (сосед, чей 4005 опередил 4003, ")
	TEXT("остаётся в реестре); 0 = прежнее (реестры очищаются целиком)."),
	ECVF_Default);

void UAsdaWorldSubsystem::OnSelfSpawn()
{
	// 4003 может прийти ДО завершения загрузки уровня (сервер шлёт бурст сразу после 4001) — тогда ждём.
	UAsdaLoadManagerSubsystem* LM = GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UWorld* W = GetGameInstance()->GetWorld();
	const bool bOnTargetMap = (LM && W && LM->CurrentMapId >= 0 &&
		!AsdaWorldKind::IsLoadingMap(W) && !AsdaWorldKind::IsLoginScene(W));   // ★ F7 (дедуп №2, В3): общие признаки
	// ★ КРУГ 11: 4003 = вход в мир (новая карта/перезаход) — авто-оценка AoI начинается заново
	//   (порог монотонный ЗА СЕССИЮ КАРТЫ; переносить максимум прошлой карты на новую нельзя).
	if (AoiAuto.EffTiles != 30)
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[AoI-авто] порог СБРОШЕН %d → 30 тайлов (4003: вход в мир — новая сессия карты)"), AoiAuto.EffTiles);
	}
	AoiAuto.EffTiles = 30;
	AoiAuto.CurSec   = -1;
	AoiAuto.LastObsS = 0.0;
	FMemory::Memzero(AoiAuto.Buckets, sizeof(AoiAuto.Buckets));
	// ★★ ОЧИСТКА КАРТО-РЕЕСТРОВ (живой тест 2026-08-04: мобы прошлой карты «переезжали» на новую).
	//   Акторы гибнут вместе со старым миром при травеле, но ЗАПИСИ (rid → weak) жили в GameInstance-
	//   подсистеме вечно: новый моб с тем же rid попадал в мёртвую запись/старые буферы урона.
	//   4003 = начало сессии карты (здесь же сбрасывается AoI) — единственно верная точка сброса.
	// ★ [ИСПРАВЛЕНИЕ 05.11.2026] (asda.World.SelfSpawnKeepLive, самотест asda.Overhead.SelfTestRelog): записи, чей актор ЖИВ в ТЕКУЩЕМ
	//   мире, — не «прошлой карты»: их создали пакеты этого же входа, пришедшие раньше 4003 (4005 соседа, 4013 моба). Прежняя
	//   безусловная очистка делала из них сирот — тело с плашкой стоит, но движение/HP/уход (4007, 4058, 4011) его не находят, а
	//   следующий 4005 ставит второго. Чистим только записи с погибшими акторами или из другого мира; 0 — прежнее (всё).
	if (CVarSelfSpawnKeepLive.GetValueOnGameThread() != 0 && bOnTargetMap)   // 4003 до загрузки уровня (портал) — мир ещё прежний: всё прежнее
	{
		const UWorld* CurW = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		auto Stale = [CurW](const AActor* A) { return !::IsValid(A) || A->GetWorld() != CurW; };
		int32 Kept = 0;
		const int32 N = NetMobs.Num() + NetNpcs.Num() + NetPlayers.Num() + NetLoot.Num() + PetActors.Num();
		for (auto It = NetMobs.CreateIterator(); It; ++It)    { if (Stale(It.Value().Get())) { It.RemoveCurrent(); } else { ++Kept; } }
		for (auto It = NetNpcs.CreateIterator(); It; ++It)    { if (Stale(It.Value().Get())) { It.RemoveCurrent(); } else { ++Kept; } }
		for (auto It = NetPlayers.CreateIterator(); It; ++It) { if (Stale(It.Value().Actor.Get())) { It.RemoveCurrent(); } else { ++Kept; } }
		for (auto It = NetLoot.CreateIterator(); It; ++It)    { if (Stale(It.Value().Get())) { NetLootSerial.Remove(It.Key()); It.RemoveCurrent(); } else { ++Kept; } }
		for (auto It = PetActors.CreateIterator(); It; ++It)  { if (Stale(It.Value().Get())) { It.RemoveCurrent(); } else { ++Kept; } }
		DmgBuffers.Empty();   // очереди ударов по userIdx атакующего — как прежде, целиком
		PeerSeriesTargetRid.Reset(); PeerSeriesFlags.Reset(); PeerSeriesGen.Reset();
		PvpSeries.Reset();
		ResetImpactLedgers();
		if (N > 0) { UE_LOG(LogAsdaWorld, Log, TEXT("[4003] реестры прошлой карты очищены: %d записей (живых в этом мире оставлено %d)"), N - Kept, Kept); }
	}
	else
	{
		const int32 N = NetMobs.Num() + NetNpcs.Num() + NetPlayers.Num() + NetLoot.Num() + PetActors.Num();
		NetMobs.Empty(); NetNpcs.Empty(); NetPlayers.Empty(); NetLoot.Empty(); NetLootSerial.Empty();   // ★ 09.10.2026: и номера появления лута
		DmgBuffers.Empty(); PetActors.Empty();
		PeerSeriesTargetRid.Reset(); PeerSeriesFlags.Reset(); PeerSeriesGen.Reset();   // ★ 17.10.2026 (Ф0/Ф1): userIdx прошлой карты
		PvpSeries.Reset();   // ★ 17.10.2026 (Ф4): PvP-серии прошлой карты
		ResetImpactLedgers();                                                           // ★ Ф1: и ленты попаданий по rid прошлой карты
		if (N > 0) { UE_LOG(LogAsdaWorld, Log, TEXT("[4003] реестры прошлой карты очищены: %d записей"), N); }
	}
	if (bOnTargetMap) { TrySpawnLocalPlayer(); }
	else
	{
		bPendingSelfSpawn = true;
		UE_LOG(LogAsdaWorld, Log, TEXT("4003 до загрузки уровня — спавн отложен до PostLoadMap"));
	}
}

void UAsdaWorldSubsystem::OnPostLoadMap(UWorld* World)
{
	// ★ 11.10.2026 (F14): таймеры сердец прежнего мира погибли вместе с ним — счётчик живых обнуляется.
	ActiveSoulHeartTimers = 0;
	if (!bPendingSelfSpawn || !World) { return; }
	if (AsdaWorldKind::IsLoadingMap(World) || AsdaWorldKind::IsLoginScene(World)) { return; }   // ★ F7 (дедуп №2, В3): общие признаки
	bPendingSelfSpawn = false;
	TrySpawnLocalPlayer();
}

// Ключ отката: 1 — как оригинал (надетое своего из контейнера 3 / 1027); 0 — прежний порт (только запись char-list, 1007).
static ASDA_DEV_CVAR(int32, CVarSelfWornFromContainer,
	TEXT("asda.Char.SelfWornFromContainer"), 1,
	TEXT("1 = как оригинал: своя модель одевается из контейнера 3 (1027), разбор 1027 сверяет уже стоящую модель; "
	     "0 = прежний порт: только снимок char-list 1007 (вход без свежего списка давал голую модель)."),
	ECVF_Default);

// Включено ли одевание своей модели из контейнера 3 (аксессор: имя ключа не уходит в рабочий код других .cpp).
bool AsdaSelfWornFromContainerOn() { return CVarSelfWornFromContainer.GetValueOnGameThread() != 0; }

void UAsdaWorldSubsystem::TrySpawnLocalPlayer()
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UWorld* W = GetGameInstance()->GetWorld();
	if (!Net || !W || Net->SelfTileX < 0) { return; }

	// Мир игры: wx=тайл*100+50 (game-ед.=см); конверсия осей game→UE: (x,y,z)→(-z,x,y).
	const FVector2D SelfG = AsdaCoord::TileCenterGame(Net->SelfTileX, Net->SelfTileZ);   // ★ Аудит №3 (B2)
	const FVector2D UeXY = AsdaCoord::GameToUe2D(SelfG);   // ★ Аудит №3 (B1)

	// ★★★ A1 (аудит №4 `system_dedup_audit_4.md`): СЕТКА ВЫСОТ ПОДНИМАЕТСЯ ДО РАСЧЁТА Z.
	//   Блок загрузки DA_WalkGrid стоял НИЖЕ спавна павна, поэтому на самый первый кадр входа/варпа
	//   сетки ещё не было и высота бралась своим трейсом `ECC_Visibility` — ЕДИНСТВЕННЫМ в порту
	//   (закон `ground-height-from-ter`: высота из .TER, трейса по мешу в оригинале НЕТ вовсе;
	//   эталон `charMove → terrainGetCellByPos_A840@0x95A840 → terrainInterpHeightAtXY@0x1130F90`).
	//   Тот трейс цеплял чужие PickHitbox (лут 30/30, игроки 35/80) и проваливал персонажа под
	//   мост/площадку (расхождение .TER и рендер-меша до 2403 юнитов на alpen).
	//   Сетка не зависит от павна — грузим её ПЕРВОЙ, павну отдаём готовый указатель ниже.
	UAsdaWalkGrid* MapGrid = nullptr;
	if (UAsdaLoadManagerSubsystem* LM = GetGameInstance()->GetSubsystem<UAsdaLoadManagerSubsystem>())
	{
		const FString Dir = LM->GetMapDirName(LM->CurrentMapId);
		if (!Dir.IsEmpty())
		{
			const FString WPath = AsdaMapRegistry::WalkGridObjectPath(Dir);   // ★ волна 2: одна точка пути (реестр карт)
			MapGrid = LoadObject<UAsdaWalkGrid>(nullptr, *WPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
			UE_LOG(LogAsdaWorld, Log, TEXT("WalkGrid карты '%s': %s"), *Dir, MapGrid ? TEXT("загружена") : TEXT("НЕТ (fail-open)"));
			// ★ Тот же ассет несёт СЕТКУ ВЫСОТ .TER — регистрируем как единый источник высоты земли
			//   для персонажа, мобов, NPC, питомцев и лута (AsdaGround::GroundZ).
			if (MapGrid) { AsdaGround::SetGrid(MapGrid); }
		}
	}
	// ★★★ A1: высота своего персонажа — ОБЩЕЙ ТОЧКОЙ, как у мобов/NPC/пета/лута. Сетки нет ⇒ внутри
	//   GroundZ отрабатывает фолбэк-трейс СТРОГО по компоненту террейна (чужие хитбоксы недостижимы).
	const float UeZ = AsdaGround::GroundZ(W, UeXY.X, UeXY.Y, 0.f);
	const FVector SpawnLoc(UeXY.X, UeXY.Y, UeZ);

	if (AActor* Old = LiveSelfActor()) { Old->Destroy(); }   // ★ 06.10.2026: IsValid, а не «не null»
	LocalPlayerActor = nullptr;

	// ★ Полноценный павн игрока (управление/анимации/экип — AsdaPlayerCharacter.h). AlwaysSpawn: спавн-коллизия
	//   НЕ должна сдвигать павн от серверной позиции; локацию выставляем явно после спавна.
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AAsdaPlayerCharacter* Ch = W->SpawnActor<AAsdaPlayerCharacter>(AAsdaPlayerCharacter::StaticClass(), SpawnLoc, FRotator::ZeroRotator, SP);
	if (!Ch) { UE_LOG(LogAsdaWorld, Warning, TEXT("Спавн self: павн не создан")); return; }
	Ch->SetActorLocation(SpawnLoc);   // гарантированно на серверной точке

	// ★★★ ПОЛ И ВНЕШНОСТЬ СВОЕГО ПЕРСОНАЖА — КАК В ЭТАЛОНЕ (game_main.c:2792-2805):
	//   локального игрока собирают из блока 1017 (`g_aCharSelectData` → char+1052, пол = блок+27 =
	//   char+1079) и из `dword_29062B0` = тело 1017 [72..75] → char+1120..1123 (созвездие/причёска/
	//   цвет/лицо). Запись char-list (0x3EE) несёт ТЕ ЖЕ поля и остаётся запасным входом.
	//   ★ ИСПРАВЛЕНО 2026-08-07: раньше пол брался ТОЛЬКО из записи char-list по LastSelectedRawSlot,
	//     и при любом непопадании в запись молча подставлялся «мужчина» (uint8 Sex = 1) — расхождение
	//     нигде не было видно. Теперь источник — эталонный (1017), расхождения кричат в лог.
	FString GenderDiag;
	uint8 Sex = Net->ResolveSelfGender(&GenderDiag);
	FAsdaWornParts Parts;
	// Внешность из блока 1017 (первична, как в эталоне); 0xFF = поле не приезжало.
	if (Net->SelfHair      != 0xFF) { Parts.HairStyle = (int32)Net->SelfHair; }
	if (Net->SelfHairColor != 0xFF) { Parts.HairColor = (int32)Net->SelfHairColor; }
	if (Net->SelfFace      != 0xFF) { Parts.FaceIndex = (int32)Net->SelfFace; }
	FString SelfName;
	int32 SelfHpCur = 0, SelfHpMax = 0, SelfMpCur = 0, SelfMpMax = 0;
	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Possess.BodyIdentity`): запись выбранного персонажа — общей точкой `AsdaSelf::FindSelectedCharRecord`.
	//   При одержимости в СВОЁМ char-list тела нет (номер слота — Charidx персонажа соулмейта, и он совпадал с номером СВОЕГО
	//   персонажа: игрок появлялся чужим для себя персонажем со своим именем); тогда записи нет, тело собирается ниже.
	if (const FAsdaCharRecord* SelRec = AsdaSelf::FindSelectedCharRecord(Net))
	{
		const FAsdaCharRecord& R = *SelRec;
		SelfName = R.Name;
		SelfCharName = R.Name;   // член: нужен клан-сбросу 4133 (эталон сверяет имя своего персонажа)
		SelfHpCur = R.HpCur; SelfHpMax = R.HpMax;
		SelfMpCur = R.MpCur; SelfMpMax = R.MpMax;
		// Внешность из char-list — ТОЛЬКО если блок 1017 её не принёс (эталонный вход первичен).
		if (Net->SelfHair      == 0xFF) { Parts.HairStyle = R.Hair; }
		if (Net->SelfHairColor == 0xFF) { Parts.HairColor = R.Color; }
		if (Net->SelfFace      == 0xFF) { Parts.FaceIndex = R.Face; }
		auto Eq = [&R](int32 SlotIdx) { return R.Equip.IsValidIndex(SlotIdx) ? R.Equip[SlotIdx].ItemId : 0; };
		Parts.Helmet = Eq(0); Parts.Chest = Eq(1); Parts.Pants = Eq(2); Parts.Shoes = Eq(3); Parts.Gloves = Eq(4);
		Parts.Weapon = Eq(9);   // оружие = слот enum A 9 (charselect_equip.md §A: 0..4 броня, 9 weapon)
		Parts.Shield = Eq(8);   // ★ 25.10.2026 [ОРИГИНАЛ] щит = экип-слот 8 → `CCharacter_SetParts_Armor` (меш на "_7_a1")
	}
	// ★★★ 06.11.2026 [ОРИГИНАЛ] НАДЕТОЕ — ИЗ КОНТЕЙНЕРА 3 (`asda.Char.SelfWornFromContainer`; баг игроков «после разрыва на выборе
	//   персонажа броня не видна»). Оригинал одевает своего из 1027 (`game_main.c:2807`: `g_aEnterWorldCharData` → CCharacter+1588),
	//   а запись char-list — только превью выбора. Порт брал запись: вход без свежего 1007 (её экип снят `SendLogin`) давал
	//   голую модель при целом контейнере 3. Здесь: контейнер уже разобран (портал, канал) — надетое из него; на первом входе
	//   1027 ещё в очереди инвентаря — запись как черновик, а разбор очереди сверяет модель (`ApplyPendingBatches`).
	if (AsdaSelfWornFromContainerOn())
	{
		const UAsdaInventorySubsystem* WornInv = GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>();
		if (WornInv && WornInv->IsContainerReceived(3))
		{
			auto Worn = [WornInv](int32 Slot) -> int32
			{
				const FAsdaItemInstance* C = WornInv->GetCell(3, Slot);
				return (C && !C->IsEmpty()) ? C->ItemCode : 0;
			};
			Parts.Helmet = Worn(0); Parts.Chest = Worn(1); Parts.Pants = Worn(2); Parts.Shoes = Worn(3); Parts.Gloves = Worn(4);
			Parts.Weapon = Worn(9);
			Parts.Shield = Worn(8);
		}
	}
	// ★★★ 30.10.2026 [ОРИГИНАЛ] ТЕЛО ПРИ ОДЕРЖИМОСТИ (`asda.Possess.BodyIdentity`; баг игроков «вход одержимым — не та модель и не то имя»).
	//   Оригинал собирает локального игрока НЕ из char-list: имя — блок 1017 +4 (`game_main.c:2801` кладёт `g_aCharSelectData` в char+1052,
	//   имя char+1056 — 20 б, `ui_core.c:72785` `memcpy(…, a2 + 1056, 0x14)`), надетое — 1027 (`g_aEnterWorldCharData` → char+1588, 11×57,
	//   индекс = экип-слот), пол и внешность — 1017 (+27, тело [72..75]). Логин-сервер при режиме 2 грузит ВЕСЬ персонаж владельца тела
	//   (`Send_SelectChar@0x40AD30`: UPCharInfo/UPCharApp/UPPlateItem владельца), значит это имя и вещи ТЕЛА. Портал локального игрока не
	//   пересоздаёт — у порта павн новый, поэтому то же правило на каждом спавне: вход, портал, смена канала.
	//   Надетое: контейнер 3 (1027 + все 5001 после него) — если он уже применён (после портала); на первом входе 1027 ещё в очереди
	//   инвентаря (её разбирает первый кадр в мире), тогда — запись тела 1024 из набора соулмейта (тот же снимок, что видно на выборе).
	if (AsdaPossessBodyIdentityOn() && Net->SelfSoulState == 2)
	{
		const FAsdaSoulmateSharedChar* Body = nullptr;
		for (const FAsdaSoulmateSharedChar& S : Net->GetSoulmateSharedChars())
		{
			if (!S.IsEmpty() && S.Charidx == Net->LastSelectedRawSlot) { Body = &S; break; }
		}
		FString BodyName;
		if (Net->CharSelectData.Num() >= 4 + 20) { BodyName = FAsdaLoginData::DecodeCp1251(Net->CharSelectData.GetData() + 4, 20); }
		if (BodyName.IsEmpty() && Body) { BodyName = Body->CharName; }
		SelfName = BodyName;
		SelfCharName = BodyName;
		if (Body)
		{
			SelfHpCur = Body->Hp; SelfHpMax = Body->MaxHp;
			SelfMpCur = Body->Mp; SelfMpMax = Body->MaxMp;
			if (Net->SelfHair      == 0xFF) { Parts.HairStyle = Body->Hair; }
			if (Net->SelfHairColor == 0xFF) { Parts.HairColor = Body->HairColor; }
			if (Net->SelfFace      == 0xFF) { Parts.FaceIndex = Body->Face; }
		}
		const UAsdaInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>();
		bool bWornKnown = Inv && Inv->IsContainerReceived(3);
		for (int32 S = 0; Inv && !bWornKnown && S < 11; ++S)
		{
			const FAsdaItemInstance* C = Inv->GetCell(3, S);
			bWornKnown = C && !C->IsEmpty();
		}
		auto Worn = [Inv, bWornKnown, Body](int32 Slot) -> int32
		{
			if (bWornKnown)
			{
				const FAsdaItemInstance* C = Inv->GetCell(3, Slot);
				return (C && !C->IsEmpty()) ? C->ItemCode : 0;
			}
			return (Body && Body->Equip.IsValidIndex(Slot)) ? Body->Equip[Slot].ItemId : 0;
		};
		Parts.Helmet = Worn(0); Parts.Chest = Worn(1); Parts.Pants = Worn(2); Parts.Shoes = Worn(3); Parts.Gloves = Worn(4);
		Parts.Weapon = Worn(9);
		Parts.Shield = Worn(8);
		UE_LOG(LogAsdaWorld, Log, TEXT("[одержимость] спавн self телом '%s' (Charidx=%u, charId=%d): надетое из %s, HP %d/%d"),
			*BodyName, (uint32)Net->LastSelectedRawSlot, Net->SelectedCharId,
			bWornKnown ? TEXT("контейнера 3 (1027/5001)") : (Body ? TEXT("записи тела 1024") : TEXT("— нет источника")),
			SelfHpCur, SelfHpMax);
	}
	// ★★★ 25.10.2026 [ОРИГИНАЛ] ПОКАЗ ШЛЕМА из 4003 [8] (`Recv_SpawnSelf` → `CCharacter_ApplyEquippedHelmet`): вход и портал.
	Parts.bHelmetShown = (Net->SelfHelmetShow != 0);
	Ch->InitAppearance(Sex, Parts);
	// ★★★ СНИМОК СВОЕЙ ВНЕШНОСТИ. Дальше его точечно правит `ApplySelfEquipPart` (смена экипировки
	//   применяется ЛОКАЛЬНО по ответу 5001 — сервер себе 4021/4023 не шлёт, см. заголовок).
	//   Без этого снимка любая пересборка теряла бы причёску/лицо/остальные надетые части.
	SelfParts = Parts;
	SelfSex   = Sex;
	// ★ A1 (аудит №4): сетка проходимости карты уже загружена ВЫШЕ (до расчёта Z) — павну отдаём
	//   готовый указатель. Второй загрузки здесь больше нет.
	Ch->WalkGrid = MapGrid;
	LocalPlayerActor = Ch;
	// ★ СВОЯ плашка над головой (просьба юзера 2026-07-29): имя как у других игроков, а полоса HP —
	//   ВСЕГДА видна (в отличие от чужих, где HP показывается только у выбранной цели).
	// ★★★ Начальные HP/MP кладём в ЕДИНЫЙ ИСТОЧНИК ИСТИНЫ, а не в плашку. Запись char-list —
	//   СНИМОК НА МОМЕНТ ВЫБОРА ПЕРСОНАЖА и может быть сильно устаревшим (живой лог 2026-08-04:
	//   1426/2853, тогда как реальный HP на сервере уже был 2853). Актуализирует его первый же
	//   пакет 4058 (регенерация каждые 5 с) — раньше порт его не слушал, отсюда «плашка = 0».
	if (Net)
	{
		Net->SetVitalsHp((int32)Net->SelfUserNum, SelfHpCur, SelfHpMax, TEXT("char-list 0x3FF"), /*bServerSnapshot=*/false);
		Net->SetVitalsMp((int32)Net->SelfUserNum, SelfMpCur, SelfMpMax, TEXT("char-list 0x3FF"), /*bServerSnapshot=*/false);
	}
	if (UAsdaNameplateComponent* SelfNP = FillNameplate(Ch, SelfName))
	{
		SelfNP->bHideForLocalPlayer = false;   // гейт «над собой не рисуем» выключен — юзер хочет свою плашку
		SelfNP->bShowHp = true;
		// Значения — ТОЛЬКО из единого источника (он уже наполнен строкой выше). Прямых записей
		//   HP в плашку игрока в порту больше нет — иначе снова получим два расходящихся пути.
		if (Net) { ApplyVitalsToNameplate((int32)Net->SelfUserNum); }
		// ★ ПРАВИЛО ЮЗЕРА (2026-07-30): на себе показываем ВСЁ, что показываем на других — клан/титулы/
		//   ранг/эмблему из кэшей, если их пакеты (6153/4135/6068) пришли до спавна собственного павна.
		// ★ 06.10.2026 (R2-СРЕДН-1): клан своего — из ПОСТОЯННОГО зеркала `SelfClan`, при КАЖДОМ спавне (как живой
		//   CCharacter оригинала). Одноразовый кэш `PendingClanByCharId` для своего больше не нужен.
		if (bSelfClanKnown)
		{
			SelfClanId = SelfClan.ClanId;
			ApplyClanToActor(Ch, SelfClan);
		}
		if (const FIntVector* T = Net->IsSelfUserNumKnown() ? PendingTitleByUserIdx.Find((int32)Net->SelfUserNum) : nullptr)
		{
			SelfNP->RankValue = T->Z;
			SelfNP->SetTitles(T->X, T->Y);
			PendingTitleByUserIdx.Remove((int32)Net->SelfUserNum);
		}
		// Фракция из кэша (6713 приходит в DB-бурсте до спавна павна): полоса лагеря + эмблема звания.
		if (SelfCampId != -2) { SelfNP->SetCamp(SelfCampId, SelfFactionGrade); }
		// ★★★ ОДЕРЖИМОСТЬ СВОЕГО персонажа (закон self-nameplate-same-as-others).
		//   ⚠ ОПРОВЕРГНУТО 2026-09-03 (см. `Reports/possess_broadcast_soulstate.md`): прежний
		//   комментарий здесь утверждал, будто чужой SoulState по проводу не приходит. Это ложное
		//   «нет» (искали байт 99027 по литеральному смещению, а он уезжает внутри 95-байтового
		//   `memcpy` от базы 99001). SoulState ЛЮБОГО игрока приходит каналами 6152 (байт +12) и
		//   5220 (байт +32); эталонный клиент их разбирает и красит имя одержимой цели.
		//   Свой признак — единое поле `SelfSoulState` (0/1/2, ответ 1017 — известно ДО любого пакета);
		//   сетевой кэш для себя только сверяется в журнале. Через `ApplySoulStateToActor`, чтобы
		//   режим «душа» (1) сразу дал полупрозрачную модель, а не только поле плашки.
		{
			const int32 NetSoul = Net ? ResolveSoulStateFor((int32)Net->SelectedCharId,
				Net->IsSelfUserNumKnown() ? (int32)Net->SelfUserNum : -1) : 0;
			ApplySoulStateToActor(Ch, SelfSoulStateAuthoritative(TEXT("спавн self"), NetSoul > 0 ? NetSoul : -1));
		}
		SelfNP->RefreshNameplate();
		// ★ Крылья/кольцо СВОЕГО персонажа из кэша (6585 по charId, 5220 по userIdx — до спавна павна).
		if (const FAsdaWingState* WSt = (Net->SelectedCharId != 0) ? WingByCharId.Find((int32)Net->SelectedCharId) : nullptr)
		{
			if (WSt->bOn) { ApplyWingToActor(Ch, Sex != 2, WSt->WingId, true); }
		}
		if (const FAsdaSoulState* SSt = Net->IsSelfUserNumKnown() ? SoulByUserIdx.Find((int32)Net->SelfUserNum) : nullptr)
		{
			if (SSt->SoulIdx > 0) { ApplySoulRingToActor(Ch, true); }
		}
		// Своё кольцо из 6152-кэша (по charId) — если сервер прислал стейт до спавна павна.
		if (const int32* SIdx = (Net->SelectedCharId != 0) ? SoulIdxByCharId.Find((int32)Net->SelectedCharId) : nullptr)
		{
			if (*SIdx > 0) { ApplySoulRingToActor(Ch, true); }
		}
	}
	// ★ СВЕЧЕНИЕ ЗАТОЧКИ СВОЕГО оружия/щита: заточка приехала в батче 4048 (57-байтные инстансы,
	//   байт +0x18) ДО спавна павна — применяем сразу (эталон зовёт обе Update*-функции и на себе,
	//   entity_char.c:8077).
	RefreshSelfWeaponGlow();

	// ★★★ 30.10.2026 [ОРИГИНАЛ] ТРАНСПОРТ СВОЕГО ПАВНА (`asda.Mount.PortalReapply`, разбор у ключа в AsdaWorldSubsystem_Peers.cpp).
	//   Оригинал вешает транспорт локальному игроку из его списка состояний на шаге 12 загрузки (`CCharacter_StopAllActiveBuffActions`
	//   при портале, `charReapplyActiveSkillEffects` при входе). У нас список повторно шлёт транспорт на первой своей 4003
	//   (`UAsdaEffectsSubsystem::OnSelfSpawnCarry`); если это случилось ДО павна, мир запомнил код — применяем его здесь, как соседу.
	if (AsdaMountPortalReapplyOn())
	{
		const int32 SelfIdx = AsdaSelf::GetUserIdxOr(Net, INDEX_NONE);
		const int32* MountItem = (SelfIdx >= 0) ? MountByUserIdx.Find(SelfIdx) : nullptr;
		FString MModel, MAnimSet; float MH = 0.f;
		if (MountItem && LookupMount(*MountItem, MModel, MAnimSet, MH))
		{
			HoldSpawnTables(EAsdaSpawnTables::Mount);
			FString RFx, RSnd, RSnd2, RSnd3; LookupRideFx(*MountItem, RFx, RSnd, RSnd2, RSnd3);
			Ch->SetMount(MModel, MAnimSet, MH, RFx, RSnd, RSnd2, RSnd3);
			UE_LOG(LogAsdaWorld, Log, TEXT("[транспорт] спавн self: верхом на %d (модель %s)"), *MountItem, *MModel);
		}
	}

	// ★★★ GM-«СКРЫТЬСЯ»: состояние приходит РАНЬШЕ павна. Сервер (`CharMake@0x543A10`,
	//   `CharPortalMake`) ставит операторам «скрыт» СРАЗУ, а наш признак взводится в обработчике
	//   4003 — то есть ещё до того, как этот павн создан (4003 и есть сигнал «спавнить себя»).
	//   Без этой строки прозрачность и гашение не применились бы до первого 6005 вручную.
	//   Тот же класс граблей, что у клана/титулов/крыльев выше (закон `no-state-latch-before-shown`).
	// ★★★ [ПОРТ] ДООПРЕДЕЛЕНИЕ ОПЕРАТОРСКОГО КРИТЕРИЯ ПО ИМЕНИ ПЕРСОНАЖА (10.09.2026).
	//   `SelfCharName` садится ВЫШЕ в этой же функции (из записи char-list), то есть ПОЗЖЕ, чем
	//   отрабатывает обработчик 4003. Доказательство — живой лог `Saved/Logs/AsdaReborn_4.log`:
	//       23:47:01.850  SpawnSelf 4003 …
	//       23:47:01.935  Self заспавнен … плашка 'ADM_sitxovski'   ← на 85 мс позже
	//   Обычно это безразлично: сетевая подсистема берёт имя напрямую из своей записи char-list
	//   (она приезжает за минуты до входа). Но если записи нет (повтор 1005 после ошибки 1025,
	//   режим XyT, офлайн-крюк FakeSelfSpawn), критерий остаётся НЕПРОВЕРЕННЫМ — и вот последняя
	//   точка, где его ещё можно пересчитать ДО того, как павн покажется: прозрачность применяется
	//   строкой ниже, в том же кадре. Если ничего не ждали — вызов ничего не делает.
	if (Net) { Net->ReevaluateGmHideOnEntryIfNamePending(); }
	if (Net && Net->IsGmHidden())
	{
		UE_LOG(LogAsdaWorld, Log, TEXT("[GM-скрытие] спавн self: состояние СКРЫТ уже известно — применяем сразу"));
		SetCharHiddenByGm((int32)Net->SelfUserNum, true);
	}
	UE_LOG(LogAsdaWorld, Log, TEXT("Self заспавнен: тайл(%d,%d) → UE(%s), пол=%d (%s), причёска=%d цвет=%d лицо=%d, плашка '%s' HP %d/%d"),
		Net->SelfTileX, Net->SelfTileZ, *SpawnLoc.ToString(), Sex, *GenderDiag,
		Parts.HairStyle, Parts.HairColor, Parts.FaceIndex, *SelfName, SelfHpCur, SelfHpMax);

	// Possess: мировой контроллер (click-to-move/орбит-камера) берёт павна.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0))
	{
		PC->Possess(Ch);
		PC->SetControlRotation(FRotator(-20.f, 0.f, 0.f));
		PC->SetViewTargetWithBlend(Ch, 0.f);
		const FVector CamL = Ch->Camera ? Ch->Camera->GetComponentLocation() : FVector::ZeroVector;
		UE_LOG(LogAsdaWorld, Log, TEXT("Possess: контроллер=%s павн=%s камера-мир=%s"),
			*PC->GetClass()->GetName(), *Ch->GetActorLocation().ToString(), *CamL.ToString());
	}
	else { UE_LOG(LogAsdaWorld, Warning, TEXT("Possess: PlayerController(0) == null — павн не запоссешен!")); }

	// ★ ПОКАЗАТЬ МИР: спавн игрока (4003) = мир готов и виден. Снимаем загрузочный экран + закрываем login-UI
	//   + режим ввода Game СЕЙЧАС (НЕ ждём 4055/4056 — оно ненадёжно/позднее; иначе тёмно-синий экран висит).
	RevealWorld();
	// ★★★ ПРОГРЕВ ИНВЕНТАРЯ (load_hitch 11.09.2026): окно собираем СКРЫТЫМ здесь — сразу ПОСЛЕ RevealWorld
	//   (его CloseAll снимает с экрана все окна и опустошает реестр). Кадр спавна ещё не показан: игрок до его
	//   конца видит загрузочный экран, поэтому цена сборки (≈45 мс) в мир не попадает.
	//   ⚠ Именно здесь, а НЕ в RevealWorld: RevealWorld зовёт ещё и OnWorldReady (4055/4056), который в порте
	//   приходит через секунды после показа мира, — прогрев там стал бы новым рывком посреди игры.
	if (UAsdaInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>())
	{
		Inv->PrewarmWindow();
	}

	// ★ ЭФФЕКТ ПОЯВЛЕНИЯ на входе (эталон CRenderer_RenderWorldFrame@0x9773B0: первый кадр мира после
	//   загрузки — cmchar01 + S_c_entry.wav на self). Порт-эквивалент первого кадра = момент показа мира.
	PlayEntryFx(Ch, /*bSelf*/true);

	// ★ ФИНАЛИЗАЦИЯ ВХОДА (эталон prog11/prog13): анти-чит-токены 4122+4124+4301 — подтверждают серверу
	//   завершение загрузки/«я в мире». Эталон шлёт их ПОСЛЕ спавна; без них сервер может держать вход
	//   незавершённым. (Сверка флоу FLOW_compare.md §4.)
	Net->SendEntryFinalize();

	// ★★ ВХОД В МИР УЖЕ МЁРТВЫМ (4003 regenState == 200). Делаем ПОСЛЕ Possess/RevealWorld, чтобы
	//   поза, обесцвечивание и окно легли на готовый павн и готовый вьюпорт. 4053 в этом случае НЕ
	//   придёт никогда — флаг «мёртв» сервер восстановил из БД, и без этой ветки игрок оставался бы
	//   с полным управлением, а сервер резал бы каждое действие (6001 причина 1).
	if (Net->bSelfDead)
	{
		// Состояние смерти уже взведено ДО появления павна (например, 6001 прилетел раньше) — меню и
		//   рассылка уже отработали, здесь остаётся только применить позу и обесцвечивание к новому павну.
		Ch->EnterDeadPoseInstant();
		Ch->SetDeathDesaturation(true);
		UE_LOG(LogAsdaWorld, Warning, TEXT("[смерть] павн создан в состоянии смерти — поза трупа + ч/б применены"));
	}
	else if (Net->bSelfSpawnDead)
	{
		Net->EnterSelfDeadState(TEXT("вход в мир мёртвым (4003 regenState=200)"), /*bAlreadyDead*/true);
	}

	// ★ 30.10.2026 [ИСПРАВЛЕНИЕ]: свой призванный питомец — сразу со своим персонажем (вход и портал), см. `RestoreSelfPetFromList`.
	RestoreSelfPetFromList(TEXT("спавн своего персонажа"));

	// ★ 4054 (CharRegenVillage) НА ВХОДЕ НЕ ШЛЁМ. Эталон (Send_CharRegen@0x9FC9A0) шлёт его ТОЛЬКО из диалога
	//   СМЕРТИ (респаун в деревне) и по свитку телепорта — НЕ при заходе из авторизации. SendCharRegen(0)
	//   оставлен для будущего диалога смерти. Спавн-позиция приходит из 4003 (сохранённый тайл БД CharPos).
}
