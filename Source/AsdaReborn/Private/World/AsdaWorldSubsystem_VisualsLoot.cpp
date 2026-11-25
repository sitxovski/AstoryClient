// Copyright (c) 2026 @sitxovski (Discord)
// AsdaWorldSubsystem_VisualsLoot.cpp — ★ R3-c 12.11.2026 (разрез AsdaWorldSubsystem_Visuals.cpp, дословный перенос): наземный лут: появление, звук, подбор, строки получения.
//   Общее для частей — в AsdaWorldSubsystem_Visuals_Private.h; шапка и реверс — в AsdaWorldSubsystem_Visuals.cpp.
#include "World/AsdaWorldSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaNotice.h"              // ★ 27.09.2026: плашка 221 для отказов 4007 (канал обработчика `Recv_EntityMove`)
#include "HAL/IConsoleManager.h"        // ключ `asda.Move.RefusePlank`
#include "Resources/AsdaModelAnimSubsystem.h"   // Д2/Д8/Д9: ресурс только через менеджер
#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"   // ★ «это я?» — единая точка своего номера (E2, 14.09.2026)
#include "World/AsdaPlayerCharacter.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaHudSubsystem.h"             // HERO_NOTICE_WIN — только чужой редкий дроп (6131)
#include "UI/AsdaMessageSubsystem.h"         // ★★★ ЕДИНЫЙ маршрутизатор сообщений (категория → окно по таблице)
#include "Framework/Application/SlateApplication.h"
#include "Npc/AsdaMobActor.h"
#include "World/AsdaGroundTrace.h"
#include "World/AsdaCoord.h"                 // ★ Аудит №3 (B1/B2): единый перевод координат и тайловая сетка
#include "World/AsdaPlayerController.h"      // снятие таргета при смерти цели-моба (эталон SetActionState(10))
#include "Materials/MaterialInterface.h"      // материал меша стрелы (у ..._1 ссылка битая)
#include "Resources/AsdaGameUserSettings.h"  // ★ 17.10.2026 (Ф5): режим показа цифр урона `DamageTextMode`
#include "Skill/AsdaSkillFxSubsystem.h"      // ★★★ стык «урон ↔ визуал»: момент удара навыка
#include "World/AsdaFxLightSettings.h"         // ★ волна 3 п.16 (15.09.2026): AsdaWeaponGlow — имя свечения заточки (одна таблица баз)
#include "Items/AsdaItemTableSubsystem.h"    // тип оружия атакующего (импакт battk01..04)
#include "UI/AsdaInventorySubsystem.h"       // надетое (контейнер 3): проверка сломанных вещей
#include "Items/AsdaItemDbRows.h"            // ★ сырая запись skill_info (.sif +0x48 = buff0 состояния отражения)
#include "Items/AsdaGradeColor.h"            // ★ 05.11.2026: drop-палитра `CItemManager_GetDropColor` — имя предмета в строках подбора
#include "UI/AsdaSkillDatabase.h"            // ★ Аудит №3: поля записи навыка — только отсюда
#include "AsdaBytes.h"                       // ★ единые читалки сырой записи (DataHex)
#include "Resources/AsdaSoundSubsystem.h"    // звуки ударов (S_c_*normal1, фазовые звуки мобов)
#include "Components/WidgetComponent.h"          // ★ плашка имени — тоже UMeshComponent, но не тело
#include "Engine/StaticMesh.h"
#include "World/AsdaNameplateComponent.h"   // над-головные плашки (порт JUI-окон 1:1)
#include "AsdaDataTableCache.h"             // AsdaDt::Cached — объекты таблиц имён для сильного удержания (HoldEntityNameTables)
#include "World/AsdaLootActor.h"             // наземный лут (порт DropItemMgr, loot_system_re.md)
#include "Misc/ConfigCacheIni.h"             // GConfig — опции показа ангела (FxConfig)
#include "AsdaEffectActor.h"                 // эффекты персонажей: крылья/кольца soulmate (follow-кость)
#include "ProceduralMeshComponent.h"         // asda.VerifyWeaponGlow: чтение мировых вершин секций эффекта
#include "Text/AsdaTextSubsystem.h"          // loc-нотисы подбора (1011/1012/1030)
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UObjectGlobals.h"
#include "World/AsdaCombatTrace.h"           // ★ 18.10.2026: журнал боя (ASDA_CTRACE) — полный цикл действий клиента в бою
#include "AsdaWorldSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaWorldSubsystem*.cpp (LogAsdaWorld, журнал боя, чистые хелперы)
#include "AsdaWorldSubsystem_Visuals_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaWorldSubsystem_Visuals*.cpp

// ═══════════════════════════ ★ НАЗЕМНЫЙ ЛУТ (порт Reports/loot_system_re.md) ═══════════════════════════
// Таблица звуков дропа 1:1 (CItemManager_InitDropSounds@0x9C4610, std::string[12] @0x290863C).
static const TCHAR* kDropSounds[12] = {
	TEXT("Sod_drop_Booster_0"), TEXT("Sod_drop_melee_0"),  TEXT("Sod_drop_range_0"),  TEXT("Sod_drop_magic_0"),
	TEXT("Sod_drop_fabric_0"),  TEXT("Sod_drop_leather_0"),TEXT("Sod_drop_metal_0"),  TEXT("Sod_drop_Ring_0"),
	TEXT("Sod_drop_sowel_0"),   TEXT("Sod_drop_potion_0"), TEXT("Sod_drop_Etc_0"),    TEXT("Sod_drop_Gold_0"),
};

// ★ Звук падения по коду предмета — общий вход для наземного лута и для выброса вещи из сумки.
//   Категорию резолвит менеджер таблиц (одна точка на порт), имена — таблица выше.
void UAsdaWorldSubsystem::PlayItemDropSound(int32 ItemCode) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;
	if (!IT || !Snd) { return; }
	const int32 Cat = IT->GetDropSoundCat(ItemCode);
	if (Cat < 0 || Cat >= 12)
	{
		UE_LOG(LogAsdaWorld, Verbose, TEXT("[лут] звук выброса: у предмета %d нет категории (%d)"), ItemCode, Cat);
		return;
	}
	Snd->PlaySound2D(FName(kDropSounds[Cat]));
}

AAsdaLootActor* UAsdaWorldSubsystem::FindLootAtTile(const FIntPoint& Tile) const
{
	const TWeakObjectPtr<AAsdaLootActor>* Ex = NetLoot.Find(Tile);
	return Ex ? Ex->Get() : nullptr;
}

// ★ 09.10.2026 (автолопата): `sub_F2F110@0xF2F110` — обход списка билбордов предметов (голова `this+532`, следующий `+632`),
//   выход на первом, у кого `DistanceSquared3D(точка, предмет +408/+412/+416) < 90000`. Позиция предмета — его актор
//   (центр тайла, земля + 10, как `CMap_GetTileAt`).
//   ★ 09.10.2026 (МЕЛК-4 ревизии autoshovel_fishbooks): порядок списка — как у эталона. Новый билборд встаёт в ГОЛОВУ
//   (`CEffectMgr_SpawnModelWithItemTexture`, effects_particles.c:57444-57447: next = head; head->prev = new; head = new),
//   удаление (`billboardListRemoveByKey`, render_core.c:6224) порядок остальных не меняет. Значит «первый в списке» среди
//   подходящих — самый поздний по 5005, т.е. с наибольшим номером появления `NetLootSerial`. Порядок обхода TMap не важен.
AAsdaLootActor* UAsdaWorldSubsystem::FindFirstLootWithin(const FVector& UeFrom, float Radius) const
{
	const double R2 = (double)Radius * (double)Radius;   // порог квадрата расстояния (эталон 90000.0)
	AAsdaLootActor* Best = nullptr;   // подходящий лут, ближайший к голове списка
	uint64 BestSerial = 0;            // его номер появления
	for (const TPair<FIntPoint, TWeakObjectPtr<AAsdaLootActor>>& P : NetLoot)
	{
		AAsdaLootActor* L = P.Value.Get();   // живой актор записи
		if (!IsValid(L)) { continue; }
		if (FVector::DistSquared(UeFrom, L->GetActorLocation()) >= R2) { continue; }
		const uint64* Ser = NetLootSerial.Find(P.Key);
		const uint64 S = Ser ? *Ser : 0;     // номер появления (0 — нет записи: хвост списка)
		if (!Best || S > BestSerial) { Best = L; BestSerial = S; }
	}
	return Best;
}

// 5005: батч предметов. Эталон: тайл занят → запись молча игнорируется; звук дропа ОДИН на батч
//   (гейт g_bDropTargetValid: хотя бы у одной записи renderId дропнувшего != -1; категория < 12).
void UAsdaWorldSubsystem::OnLootSpawn(const TArray<UAsdaNetSubsystem::FAsdaLootDrop>& Drops)
{
	// ★ 17.10.2026 (синхронизация боя, Ф1, ревизия проекта п.10) [ПОРТ]: батч 5005 моба, чью смерть (или смертельное попадание)
	//   ещё держит лента попаданий, — вместе со смертью: иначе вещь ложится под живым мобом (сервер дропает сразу, а смерть на
	//   экране ждёт попадания до 4.5 с). Батч держится целиком (звук дропа у эталона один на батч). Ключ отката —
	//   `asda.Combat.HitFrameAllChannels 0`.
	int32 DropperRid = -1;   // моб-дропнувший батча (первая запись с ним)
	for (const UAsdaNetSubsystem::FAsdaLootDrop& D : Drops) { if (D.DropperRid >= 0) { DropperRid = D.DropperRid; break; } }
	ASDA_CTRACE_IF(DropperRid < 0 || AsdaCombatTrace::IsEngaged(DropperRid), EAsdaCT::Reward, TEXT("5005 ДОБЫЧА: %d предмет(ов) от %s"), Drops.Num(),
		(DropperRid >= 0) ? *AsdaCtWorld::Mob(DropperRid, FindNetMobByRid(DropperRid)) : TEXT("без моба-источника"));
	if (DropperRid >= 0)
	{
		TWeakObjectPtr<UAsdaWorldSubsystem> WeakThis(this);   // выпуск позже — подсистема могла уйти
		const TArray<UAsdaNetSubsystem::FAsdaLootDrop> Copy = Drops;   // батч (копия: делегат отдаёт ссылку)
		if (HoldUntilMobDeathShown(DropperRid, [WeakThis, Copy]() { if (UAsdaWorldSubsystem* S = WeakThis.Get()) { S->SpawnLootNow(Copy); } },
			TEXT("добыча 5005")))
		{
			return;
		}
	}
	SpawnLootNow(Drops);
}

void UAsdaWorldSubsystem::SpawnLootNow(const TArray<UAsdaNetSubsystem::FAsdaLootDrop>& Drops)
{
	UWorld* W = GetGameInstance()->GetWorld();
	if (!W) { return; }
	bool bAnyDropper = false;
	int32 SoundCat = -1;
	FVector DropAt = FVector::ZeroVector;   // ★ 30.10.2026: клетка добычи — точка 3D-звука падения
	for (const UAsdaNetSubsystem::FAsdaLootDrop& D : Drops)
	{
		if (AAsdaLootActor* Alive = FindLootAtTile(D.Tile)) { continue; }   // тайл занят (эталон)
		// Позиция: центр тайла (game XZ), высота земли + 10 (эталон CMap_GetTileAt → Y+10).
		const FVector2D G = AsdaCoord::TileCenterGame(D.Tile);   // ★ Аудит №3 (B2)
		const FVector2D UeXY = AsdaCoord::GameToUe2D(G);   // ★ Аудит №3 (B1)
		const float RefZ = IsValid(LocalPlayerActor) ? LocalPlayerActor->GetActorLocation().Z : 0.f;   // ★ 06.10.2026: IsValid
		const float Z = AsdaGround::GroundZ(W, UeXY.X, UeXY.Y, RefZ) + 10.f;
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AAsdaLootActor* L = W->SpawnActor<AAsdaLootActor>(AAsdaLootActor::StaticClass(),
			FVector(UeXY.X, UeXY.Y, Z), FRotator::ZeroRotator, SP);
		if (!L) { continue; }
		L->InitFromItemCode(D.ItemCode, D.Count, D.Tile);
		ASDA_CTRACE_IF(D.DropperRid < 0 || AsdaCombatTrace::IsEngaged(D.DropperRid), EAsdaCT::Reward, TEXT("добыча на земле: предмет %d ×%d в клетке (%d,%d) (от rid=%d)"), D.ItemCode, D.Count, D.Tile.X, D.Tile.Y, D.DropperRid);
		NetLoot.Add(D.Tile, L);
		NetLootSerial.Add(D.Tile, ++NetLootSerialNext);   // ★ 09.10.2026: номер появления — голова списка эталона
		// Блёстки (eff_0_item_99_drop01) спавнит САМ актор и держит, пока лежит (эталон: хэндл +624).
		if (D.DropperRid != -1) { bAnyDropper = true; SoundCat = L->DropSoundCat; DropAt = L->GetActorLocation(); }
	}
	if (bAnyDropper && SoundCat >= 0 && SoundCat < 12)
	{
		if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
		{
			// ★ [ИСПРАВЛЕНИЕ] 30.10.2026 (`asda.Sound.WorldEvents3D`): падение добычи — 3D в клетке добычи (эталон —
			//   `CSoundFlag_PlaySound2D`: добыча с чужого моба на краю обзора звенела в ухо). Класс «вещи».
			if (UAsdaSoundSubsystem::WorldEvents3DOn()) { Snd->PlaySound3DAs(FName(kDropSounds[SoundCat]), DropAt, EAsdaSoundClass::Item); }
			else                                        { Snd->PlaySound2D(FName(kDropSounds[SoundCat])); }
		}
	}
}

// 5015: мгновенное удаление (без фейда — эталон billboardRemoveByKeyClearTarget@0xF45350);
//   если предмет был целью отложенного подбора — сброс состояния подбора у контроллера.
void UAsdaWorldSubsystem::OnLootRemove(FIntPoint Tile)
{
	TWeakObjectPtr<AAsdaLootActor> L;
	NetLootSerial.Remove(Tile);   // ★ 09.10.2026: номер появления уходит вместе с записью
	if (NetLoot.RemoveAndCopyValue(Tile, L))
	{
		if (AAsdaLootActor* A = L.Get()) { A->Destroy(); }
	}
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		PC->OnLootGone(Tile);
	}
}

// ★ МАРШРУТИЗАЦИЯ ЛУТ-СООБЩЕНИЙ (исправлено по эталону, круг 5).
//   Эталон Net_HandleItemPickupResult@0xF45420 (network.c:42847+) и uiInventoryOnRecvItemAcquired@0xD10E80
//   (ui_core.c:47012+) шлют ВСЕ строки подбора в `CChat_AddTokenizedLines` → СИСТЕМНЫЙ ЧАТ (2572)
//   + плавающий оверлей A/B (2557/2558). HERO_NOTICE_WIN эталон использует ТОЛЬКО для события про
//   ДРУГОГО игрока (6131, грейд ≥3, network.c:3749) — раньше порт слал сюда всё подряд, из-за чего
//   отказ подбора вылезал в серверный бар уведомлений.
//   ★ Круг 7: строка уходит через ЕДИНЫЙ маршрутизатор (категория Loot). Куда именно она попадёт,
//   решает таблица маршрутизации, а не этот код. Цвет/дорожка-буфер вызывающего сохранены перекрытием.
void UAsdaWorldSubsystem::ShowLootNotice(const FString& Text, const FLinearColor& Color, bool bTrackB)
{
	UE_LOG(LogAsdaWorld, Log, TEXT("[лут-нотис] %s"), *Text);
	if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		FAsdaMsgParams P;
		P.Category = EAsdaMsgCategory::Loot;
		P.Text = Text;
		// Прозрачный цвет = эталонный цвет 0 в `CChat_AddTokenizedLines` (строка 20250 «… получает …»): свой цвет не задаём,
		//   строку красит маршрут категории `Loot`.
		P.bUseColor = (Color.A > 0.f);
		P.Color = Color.ToFColor(false);
		// Дорожка буфера — по требованию вызывающего: B = значимое событие (свой подбор/редкий дроп).
		const FAsdaMsgRoute& R = Msg->GetRoute(EAsdaMsgCategory::Loot);
		P.Targets = bTrackB ? (R.Targets | (int32)EAsdaMsgTarget::TrackB)
		                    : (R.Targets & ~(int32)EAsdaMsgTarget::TrackB);
		Msg->Post(P);
		return;
	}
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 5.f, Color.ToFColor(true), Text); }   // фолбэк без маршрутизатора
}

// ★★★ 05.11.2026 — ЦВЕТ ИМЕНИ ПРЕДМЕТА В СТРОКАХ ПОДБОРА (20066 / 20130 / 20131 / 20250).
//   Эталон кладёт имя в слот нотиса С ЦВЕТОМ: `CObject_SetSlotString(g_pLocNoticeCtx, слот, имя, DropColor)`, а
//   `UI_FormatLocalizedNotice@0x9888D0` (ui_core.c:2100-2112) превращает такой слот в « <FC=%08x>[имя]</F> » — квадратные
//   скобки потому, что вызов идёт с highlightMode = 1 и highlightIndex = номеру этого маркера (своё: (1,1), чужое 20250: (1,2)).
//   Откуда цвет (`CItemManager_GetDropColor@0x9C45C0`, индекс > 9 → запись [0]):
//     • 20066 (ui_core.c:47043-47066): item_list → itemGrade; sowell_list → dropColorIndex (+0xA9); special_itemlist →
//       dropColorIndex; ничего не нашлось → GetDropColor(0) (белый);
//     • 20130 / 20131 (ui_core.c:47073-47100): item_list → itemGrade;
//     • 20250 (network.c:42897-42921): имя игрока — слот 0 БЕЗ цвета; предмет: item_list → itemGrade, sowell_list → partGrade
//       (+0x25 — у оригинала здесь другое поле, чем у 20066; переносим как есть).
//   Порт раньше звал `FormatById` без цветов — имя шло цветом всей строки, без скобок.
//   Ключ: 1 — как оригинал (умолчание, им же идёт ритейл); 0 — прежний вид без цвета и скобок.
static ASDA_DEV_CVAR(int32, CVarLootObtainNameGradeColor,
	TEXT("asda.Loot.ObtainNameGradeColor"),
	1,
	TEXT("1 — имя предмета в строках подбора 20066/20130/20131/20250 цветом грейда и в [скобках], как оригинал "
	     "(UI_FormatLocalizedNotice highlightMode 1). 0 — без цвета и скобок (прежний вид порта)."),
	ECVF_Default);

// Запись drop-палитры как ARGB-число для тега <FC=%08x> (эталон кладёт в слот именно dword палитры).
static uint32 LootDropColorArgb(int32 PaletteIndex)
{
	const FColor C = FAsdaGradeColor::Raw(PaletteIndex);
	return 0xFF000000u | ((uint32)C.R << 16) | ((uint32)C.G << 8) | (uint32)C.B;
}

// Индекс палитры для строки 20066 (своё, штучное) — три таблицы в порядке эталона; −1 = кода нет ни в одной (→ запись [0]).
static int32 LootObtainPaletteIndex(const UAsdaItemTableSubsystem* Items, int32 ItemCode)
{
	if (!Items) { return -1; }
	if (const FAsdaItemRecord* R = Items->GetItem(ItemCode)) { return R->ItemGrade; }
	FAsdaSowellRow Sw;
	if (Items->IsSowelCode(ItemCode) && Items->GetSowell(ItemCode, Sw)) { return Sw.GradeColorIndex; }
	FAsdaSpecialItemlistRow Sp;
	if (Items->GetSpecialItem(ItemCode, Sp)) { return Sp.DropColorIndex; }
	return -1;
}

// Собрать строку подбора по слотам эталона. Str/Col — строковые слоты и их цвета (0 = без тега), Num — [sd1],
//   HighlightIndex — номер маркера предмета (скобки). Ключ 0 обнуляет цвета: подстановка тогда как у прежнего `FormatById`.
static FString FormatLootObtain(const UAsdaTextSubsystem* Txt, int32 LocId, const TArray<FString>& Str,
	const TArray<uint32>& Col, int32 Num, int32 HighlightIndex)
{
	if (!Txt) { return FString(); }
	const bool bColour = CVarLootObtainNameGradeColor.GetValueOnAnyThread() != 0;
	UAsdaTextSubsystem::FNoticeSlots S;
	for (int32 i = 0; i < Str.Num() && i < 5; ++i)
	{
		S.Str[i] = Str[i];
		S.StrColor[i] = (bColour && Col.IsValidIndex(i)) ? Col[i] : 0u;
	}
	S.Num[0] = Num;
	return UAsdaTextSubsystem::FormatNoticeText(Txt->GetTextById(LocId), S, bColour ? 1 : 0, HighlightIndex);
}

// 5013: результат подбора (реверс §6). result: 1=ok, 0/3→нотис 1012, 2→1011 (инвентарь полон),
//   4→1030 (чужой лут), 5→снять фантом-биллборд по тайлам @8/@10.
void UAsdaWorldSubsystem::OnLootPickupResult(uint8 Result, int32 PickerUserNum, const TArray<uint8>& Body, int32 NewWeight)
{
	UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaItemTableSubsystem* Items = GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>();
	UAsdaTextSubsystem* Txt = GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>();
	// ★ N6 (В1 дедупа №2): формула чтения — `AsdaBytes` (`Safe*` — вне границ 0, ровно как прежние лямбды).
	auto I32 = [&Body](int32 O) { return AsdaBytes::SafeI32(Body, O); };
	auto I16 = [&Body](int32 O) { return AsdaBytes::SafeI16(Body, O); };
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → нейтральная метка «[#id]».
	auto NoticeById = [&Txt](int32 Id) -> FString
	{
		const FString S = Txt ? Txt->GetTextById(Id) : FString();
		return S.IsEmpty() ? FAsdaLang::MissingTag(Id) : S;
	};
	// Сброс отложенного подбора (эталон: в конце хендлера всегда player[1].id58 = -1).
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		PC->ClearPendingLoot();
	}
	switch (Result)
	{
	case 1:
	{
		const bool bOurs = (PickerUserNum < 0) || AsdaSelf::IsSelfUserIdx(Net, PickerUserNum);
		const int32 ItemCode = I32(3);
		const int32 Cnt      = I32(12);
		if (bOurs)
		{
			// Свой успешный подбор: звук + нотис получения (эталон 20131/20130/20066; золото 20551 → 20262).
			if (UAsdaSoundSubsystem* Snd = GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>())
			{
				// ★ [ОРИГИНАЛ] 30.10.2026: эталон — 3D в позиции игрока (loot_system_re.md §5); громкость у себя та же, что 2D.
				AActor* Me = LocalPlayerActor.Get();
				if (Me && UAsdaSoundSubsystem::WorldEvents3DOn()) { Snd->PlaySound3DAs(FName(TEXT("Snd_char_n_0_charpickup")), Me->GetActorLocation(), EAsdaSoundClass::Item, Me); }
				else                                              { Snd->PlaySound2D(FName(TEXT("Snd_char_n_0_charpickup"))); }
			}
			int32 Grade = 0;
			if (Items) { if (const FAsdaItemRecord* R = Items->GetItem(ItemCode)) { Grade = R->ItemGrade; } }
			const FString Name = Items ? Items->GetItemName(ItemCode) : FString::Printf(TEXT("item %d"), ItemCode);
			// ★ Текст 1:1 по эталону uiInventoryOnRecvItemAcquired@0xD10E80 (ui_core.c:47069-47105):
			//     itemCode == 20551 (ЗОЛОТО)      → loc 20262 «Получено [sd1] золота.»
			//     количество <= 0 (штучный предмет) → loc 20066 «Ты получаешь предмет: [ss1].»
			//     иначе                              → loc 20131 «[ss1]: получено [sd1] шт.»
			//   Цвет строки эталона = -7930543 (0xFF86FD51), дорожка B (track=1).
			// ★★★ ЗНАЧЕНИЕ В СТРОКЕ (исправлено 2026-08-04 по живому логу).
			//   Раскладка тела 5013 (эталон: Src = body+3):
			//       body+0    u8   result
			//       body+1..2 i16  picker (−1 = свой)
			//       body+3..6 i32  itemCode           (Src+0)
			//       body+8..9 i16  индекс слота сумки (Src+5)
			//       body+12   i32  ЗНАЧЕНИЕ           (Src+9) — СМЫСЛ ЗАВИСИТ ОТ ВЕТКИ:
			//         • itemCode 20551 (ЗОЛОТО) → НОВЫЙ ИТОГ КОШЕЛЬКА, а не сумма подбора;
			//         • стопка                  → НОВОЕ количество в слоте, а не прибавка;
			//         • штучный предмет         → ≤ 0 (числа в строке нет).
			//   Живой лог 2026-08-03: body=[01 FF FF 47 50 00 00 02 00 00 00 00 2D EC A9 00 …]
			//   → itemCode 20551, слот 0, значение 0x00A9EC2D = 11 136 045 = ВЕСЬ кошелёк игрока.
			//   Порт печатал это число как «получено» — отсюда «там не было столько золота».
			FString Msg;
			if (ItemCode == 20551)
			{
				const int64 NewTotal = (int64)Cnt;
				const int64 Delta = (SelfGold >= 0) ? (NewTotal - SelfGold) : -1;
				SelfGold = NewTotal;                       // эталон: uiSetHonorPointsDisplay(new total)
				if (Delta > 0)
				{
					Msg = NoticeById(20262);
					Msg.ReplaceInline(TEXT("[sd1]"), *FString::Printf(TEXT("%lld"), Delta), ESearchCase::CaseSensitive);
				}
				else
				{
					// Эталон печатает строку ТОЛЬКО при delta > 0. Первый пакет за сессию лишь сеет базу.
					UE_LOG(LogAsdaWorld, Log,
						TEXT("[лут] золото: новый итог %lld (прежний неизвестен — строка не выводится, эталон печатает только при delta>0)"),
						NewTotal);
				}
			}
			else if (Cnt <= 0)
			{
				// Штучный предмет (Src+9 ≤ 0): эталон ui_core.c:47043-47071 — loc 20066 «[ss1]» без числа
				//   для любой из трёх таблиц (item_list / sowell_list / special_itemlist).
				//   Имя — цветом таблицы, где нашёлся код (ui_core.c:47043-47066), в [скобках] (highlight 1,1).
				Msg = FormatLootObtain(Txt, 20066, { Name }, { LootDropColorArgb(LootObtainPaletteIndex(Items, ItemCode)) }, 0, 1);
				if (Msg.IsEmpty()) { Msg = FAsdaLang::MissingTag(20066); }
			}
			else if (const FAsdaItemRecord* Rec = Items ? Items->GetItem(ItemCode) : nullptr)
			{
				// ★★★ 05.11.2026 — жалоба игроков «в чате "Ты получаешь предмет: Реагент C." без количества».
				//   Прежний порт для стопок печатал 20066 (строку БЕЗ числа), потому что когда-то модели сумки не было
				//   и прежнее количество в ячейке взять было неоткуда. Модель (`UAsdaInventorySubsystem`) давно есть —
				//   возвращаем эталонные ветки ui_core.c:47074-47105 целиком:
				//     • `flagWord176 != 0` (колчан со стрелами) → loc 20130, [sd1] = ИТОГ в записи (Src+9), не прибавка;
				//     • иначе                                   → loc 20131, [sd1] = Src+9 − прежнее число в ячейке.
				//   Без записи item_list (самоцвет/спец-предмет со стопкой) эталон строки не собирает — молчим так же.
				if (Rec->FlagWord176 != 0)
				{
					Msg = FormatLootObtain(Txt, 20130, { Name }, { LootDropColorArgb(Rec->ItemGrade) }, Cnt, 1);   // pDropColor = itemGrade
					if (Msg.IsEmpty()) { Msg = FAsdaLang::MissingTag(20130); }
				}
				else
				{
					// Ячейка назначения — из самой записи: контейнер = байт +7 тела (запись+4), слот = i16 +8 (запись+5).
					const int32 DstCont = Body.IsValidIndex(7) ? (int32)Body[7] : 0;
					const int32 DstSlot = I16(8);
					const int32 SeenKey = (DstCont << 16) | (DstSlot & 0xFFFF);
					// Прежнее число: запись 5013 уже ЭТОГО кадра в ту же ячейку (модель её ещё не применила) — иначе модель сумки.
					int32 OldCount = 0;
					const FLootStackSeen* Seen = LootStackSeenThisFrame.Find(SeenKey);
					if (Seen && Seen->Frame == GFrameCounter && Seen->ItemCode == ItemCode)
					{
						OldCount = Seen->Count;
					}
					else if (const UAsdaInventorySubsystem* Inv = GetGameInstance()->GetSubsystem<UAsdaInventorySubsystem>())
					{
						// [ИСПРАВЛЕНИЕ] эталон вычитает `g_aInvSlotItemCode[слот]` не глядя на код предмета в ячейке; у нас
						//   число чужого предмета (устаревшая ячейка) не вычитается — прибавка тогда = весь итог.
						const FAsdaItemInstance* Cell = Inv->GetCell(DstCont, DstSlot);
						if (Cell && !Cell->IsEmpty() && Cell->ItemCode == ItemCode) { OldCount = Cell->Amount; }
					}
					LootStackSeenThisFrame.Add(SeenKey, FLootStackSeen{ ItemCode, Cnt, GFrameCounter });
					const int32 Delta = Cnt - OldCount;   // эталон печатает разность как есть (без проверки знака)
					Msg = FormatLootObtain(Txt, 20131, { Name }, { LootDropColorArgb(Rec->ItemGrade) }, Delta, 1);   // pDropColor = itemGrade
					if (Msg.IsEmpty()) { Msg = FAsdaLang::MissingTag(20131); }
					UE_LOG(LogAsdaWorld, Log, TEXT("[лут] %s: ячейка %d:%d было %d → стало %d, получено %d (loc 20131)"),
						*Name, DstCont, DstSlot, OldCount, Cnt, Delta);
				}
			}
			if (!Msg.IsEmpty())
			{
				ShowLootNotice(Msg, FLinearColor(0x86 / 255.f, 0xFD / 255.f, 0x51 / 255.f, 1.f), /*bTrackB=*/true);
			}
			// ★ Эталон ui_core.c:47110 — СРАЗУ после текста подбора вызывается CItemManager_NotifyItemInfo:
			//   при грейде >= 3 показывает свой HERO_NOTICE и шлёт серверу 6616 для анонса всем.
			//   Вызов безусловный (как в оригинале) — фильтр грейда/типа внутри NotifyItemInfo;
			//   золото 20551 имеет грейд 0 и отсеивается там же.
			if (UAsdaHudSubsystem* Hud = GetGameInstance()->GetSubsystem<UAsdaHudSubsystem>())
			{
				// ★ Контейнер и слот лежат в ЭТОМ ЖЕ пакете: запись предмета начинается со смещения 3,
				//   значит контейнер = байт +7 (запись+4), слот = i16 +8 (запись+5). Они нужны
				//   третьему пакету ветки — 6129 «похвастаться добычей всем вокруг».
				const int32 ShowOffCont = Body.IsValidIndex(7) ? (int32)Body[7] : -1;
				Hud->NotifyItemInfo(ItemCode, /*bForce=*/false, ShowOffCont, /*SlotIndex=*/I16(8));
			}
			UE_LOG(LogAsdaWorld, Log, TEXT("[лут] подобрано itemCode=%d ×%d грейд=%d, вес=%d (инвентарь-модель — фаза UI)"),
				ItemCode, Cnt, Grade, NewWeight);
		}
		else
		{
			// ★★★ 05.11.2026 — ПОДБОР ДРУГИМ ЧЛЕНОМ ОТРЯДА (распределение добычи). Раньше этой ветки в порту не было:
			//   сервер (`CharItemPicking`, GameServer.c:84334-84341) при дележе шлёт 5013 КАЖДОМУ члену отряда с
			//   picker = userIdx подобравшего, а порт молчал — участники не видели, кому что досталось.
			//   Эталон `Net_HandleItemPickupResult@0xF45420` (network.c:42893-42925): подобравший не в поле зрения → ничего;
			//   запись item_list или sowell_list → loc 20250 «[ss1] получает [ss2].» (ss1 = имя игрока, ss2 = предмет),
			//   `CChat_AddTokenizedLines(чат, текст, 0, 0, 1)` — цвет 0 (цвет строки по маршруту), дорожка B.
			//   Для special_itemlist эталон строки не собирает — молчим так же. Числа в 20250 у оригинала нет.
			FString PickerName;
			if (const FAsdaNetPlayerEntry* E = NetPlayers.Find(PickerUserNum)) { PickerName = E->Name; }
			const bool bKnownTable = Items && (Items->GetItem(ItemCode) != nullptr || Items->IsSowelCode(ItemCode));
			if (!PickerName.IsEmpty() && bKnownTable)
			{
				const FString Name = Items->GetItemName(ItemCode);
				// Слот 0 — имя игрока БЕЗ цвета; слот 1 — предмет: item_list → itemGrade, sowell_list → partGrade (network.c:42904/42916),
				//   скобки на маркере [ss2] (highlight 1,2).
				int32 PalIdx = 0;
				if (const FAsdaItemRecord* PR = Items->GetItem(ItemCode)) { PalIdx = PR->ItemGrade; }
				else { FAsdaSowellRow Sw; if (Items->GetSowell(ItemCode, Sw)) { PalIdx = Sw.PartGrade; } }
				FString Msg = FormatLootObtain(Txt, 20250, { PickerName, Name }, { 0u, LootDropColorArgb(PalIdx) }, 0, 2);
				if (Msg.IsEmpty()) { Msg = FAsdaLang::MissingTag(20250); }
				ShowLootNotice(Msg, FLinearColor::Transparent, /*bTrackB=*/true);
			}
			UE_LOG(LogAsdaWorld, Log, TEXT("[лут] подбор другим: userIdx=%d имя='%s' itemCode=%d"), PickerUserNum, *PickerName, ItemCode);
		}
		break;
	}
	// Эталон network.c:42933/42938/42941 — цвета отказов: 1011/1030 золотой -11514, 1012 янтарный -21248.
	case 2:  ShowLootNotice(NoticeById(1011),
	                        FLinearColor(1.f, 0xD3 / 255.f, 0x06 / 255.f, 1.f)); break;
	case 4:  ShowLootNotice(NoticeById(1030),
	                        FLinearColor(1.f, 0xD3 / 255.f, 0x06 / 255.f, 1.f)); break;
	case 5:
	{
		// Фантом: на тайле сервера предмета нет — снять биллборд (тайлы @8/@10 абс. оффсеты тела).
		const FIntPoint Tile(I16(8), I16(10));
		OnLootRemove(Tile);
		break;
	}
	case 0: case 3: default:
		// Эталон: default-ветка = loc 1012 цветом -21248 (0xFFFFAD00). ВНИМАНИЕ: серверный result=0 —
		//   это ОБЩИЙ отказ CharItemPicking() (LABEL_103), а вовсе не «нет места»: текст 1012 в таблице
		//   говорит про рюкзак, но приходит и когда персонаж МЁРТВ (IsCheck_CharFlag код 1, опкод 6001).
		ShowLootNotice(NoticeById(1012),
			FLinearColor(1.f, 0xAD / 255.f, 0.f, 1.f));
		break;
	}
}

// 5017: сервер указал предмет для подбора (result==1) — тот же флоу, что клик по луту.
void UAsdaWorldSubsystem::OnLootPickHint(uint8 Result, FIntPoint Tile)
{
	if (Result != 1) { return; }
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GetGameInstance()->GetFirstLocalPlayerController()))
	{
		PC->StartLootPickup(Tile);
	}
}
