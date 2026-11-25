// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPlayerControllerInternal.h — ★ 23.10.2026 (разнос AsdaPlayerController.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPlayerController*.cpp.
//   Только нужное нескольким частям: категория журнала LogAsdaPC (один экземпляр — DEFINE_LOG_CATEGORY в
//   AsdaPlayerController.cpp) и чистые static-константы/хелперы без состояния — текст байт в байт, у каждой части своя копия.
//   Подключать ПОСЛЕ #include пролога.
#pragma once

#include "CoreMinimal.h"
#include "World/AsdaPlayerController.h"

// Категория журнала контроллера: объявление; определение — AsdaPlayerController.cpp (до разноса там был DEFINE_LOG_CATEGORY_STATIC).
DECLARE_LOG_CATEGORY_EXTERN(LogAsdaPC, Log, All);


// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ★★★ ГЕЙТ «ОТКРЫТА СЦЕНА ПРЕМИУМ-ГАЧИ» — `sub_922D5D` → `sub_B6F370@0xB6F370`
//      (`ui_core.c:125071`: `return *(this+468) && *(this+645) == 1`, то есть «InitWindow выполнен»
//       И «сцена открыта»). В порту это `UAsdaPremiumGachaSubsystem::IsSceneOpen()`.
//  Из шестнадцати точек опроса эталона на контроллер приходятся ДВЕ:
//     • `math_geom.c:417`  `updatePlayerMovement@0x95CDF0`   — весь блок клавиатурного движения;
//     • `input.c:3956`     `Input_OnKeyDown_Dispatch@0xE3C7D0` — диспетч команд раскладки;
//   и ТРЕТЬЯ, живущая в порту в том же тике:
//     • `entity_char.c:7978` `LocalPlayer_UpdateRender@0x9D4170` — `uiAnimUpdate` (тряска камеры)
//       + `cameraUpdate@0x959880` (слежение камеры за игроком) не вызываются вовсе.
//  Полный разбор всех шестнадцати точек — `Reports/gacha_gate_impl.md`.
static bool AsdaGachaSceneOpen(const UGameInstance* GI)
{
	const UAsdaPremiumGachaSubsystem* Gacha =
		GI ? GI->GetSubsystem<UAsdaPremiumGachaSubsystem>() : nullptr;
	return Gacha && Gacha->IsSceneOpen();
}

// ★ 22.10.2026: плашка 1019 «персонаж не может двигаться в таком состоянии» — канал эталона `Input_WorldClickHandler` (input.c:545):
//   `CControl_SetTooltipText(g_pChatWnd, текст, −11514, 10000)` = перезаписываемая плашка 221, без журнала и звука.
static void AsdaPlankCannotMove(UGameInstance* GI)
{
	AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 1019, AsdaNoticeColor::Gold, 10000.f, 1019);
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ 17.10.2026 (синхронизация боя, Ф4) PvP: ОБЫЧНАЯ АТАКА ИГРОКА ПО ИГРОКУ — сторона атакующего (ТЗ
//   `Reports/combat_sync_2026-10-17/05_TZ_PVP_FIELD10112.md`). Пакеты и их гейты — `UAsdaNetSubsystem::SendAttackPlayer4201/
//   SendAttackedPlayer4203`; очередь, показ ударов и нотисы ответов — `UAsdaWorldSubsystem` (блок «Ф4 — PvP»).
// ═══════════════════════════════════════════════════════════════════════════════════════════════════════════════════════

// Дальность атаки своего оружия (UU) и его подтип — тот же расчёт, что у боевого тика по мобу (эталон equip case 18).
static void AsdaPvpSelfWeapon(const UGameInstance* GI, const UAsdaNetSubsystem* Net, float DefRange, float& OutRangeU, int32& OutSub)
{
	OutRangeU = DefRange; OutSub = -1;
	if (!GI || !Net || Net->SelfWeaponItemId <= 0) { return; }
	if (const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>())
	{
		if (const FAsdaItemRecord* R = IT->GetItem(Net->SelfWeaponItemId))
		{
			if (R->AttackRangeTiles > 0) { OutRangeU = R->AttackRangeTiles * 100.f; }
			OutSub = R->ItemSubType;
		}
	}
}

// ═══ ★ R2 10.11.2026: общее для новых частей разреза AsdaPlayerController.cpp (перенесено дословно) ═══
// ★ R2-b: включения, которые в AsdaPlayerController.cpp стояли ПОСЛЕ этого заголовка, а общему разделу ниже нужны ДО него
//   (ASDA_COMBAT_TRACE и AsdaCombatTrace:: — для блока журнала боя). Повторное включение безвредно (#pragma once).
#include "World/AsdaCombatTrace.h"
#include "World/AsdaSkillCastLock.h"
#include "UI/AsdaTutorialEvents.h"

// ★ 28.10.2026 [ПОРТ] (решение владельца): плывущему — только ход и предметы (тело — AsdaPlayerCharacter_Move.cpp).
bool AsdaSwimActionLocked(const class AAsdaPlayerCharacter* Ch);

// ★ 26.10.2026 (navattack2): выход из атаки по приказу движения игрока — единая точка в AsdaPlayerController_Move.cpp (разбор там).
//   Объявление вне #if ASDA_COMBAT_TRACE: в Shipping журнал боя выключен, а вызов из StartLootPickup нужен всегда.
bool AsdaExitCombatForAutoPath(AAsdaPlayerController* PC, const TCHAR* Why);

// ★ 29.10.2026 (ключ asda.Map.DestMarkClear): метка цели автобега на картах — единые точки в AsdaMinimapSubsystem.cpp (разбор там).
bool AsdaDestMarkClearOn();
void AsdaDestMarkClear(UGameInstance* GI, const TCHAR* Why);
void AsdaDestMarkSetGame(UGameInstance* GI, const FVector2D& Game);

#if ASDA_COMBAT_TRACE
// ★ Журнал боя: сущность одной строкой — вид, rid/userIdx, имя, HP (моб). Только для строк журнала (в Shipping нет).
static FString AsdaCtActorDesc(const UGameInstance* GI, const AActor* A)
{
	if (!A) { return TEXT("нет"); }
	if (const AAsdaMobActor* M = Cast<AAsdaMobActor>(A))
	{
		return FString::Printf(TEXT("моб rid=%d '%s' mobId=%d HP %d/%d%s%s"), M->NetRenderId, *M->MobName, M->MobId,
			M->CurHp, M->MaxHp, M->IsDead() ? TEXT(" [МЁРТВ]") : TEXT(""), M->bServerDead ? TEXT(" [обречён сервером]") : TEXT(""));
	}
	if (const AAsdaNpcActor* N = Cast<AAsdaNpcActor>(A))
	{
		return FString::Printf(TEXT("NPC rid=%d npcId=%d '%s'"), N->NetRenderId, N->NpcId, *N->NpcName);
	}
	if (A->IsA<AAsdaPlayerCharacter>())
	{
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		return FString::Printf(TEXT("игрок userIdx=%d '%s'"), WS ? WS->FindUserIdxByActor(A) : -1, *A->GetName());
	}
	return FString::Printf(TEXT("'%s'"), *A->GetName());
}

// ★ Журнал боя (волна 2): подход к цели повторяется каждые 0.4 с — первая строка по цели в цикле идёт уровнем 1,
//   повторы по той же цели в том же цикле — уровнем 2. Своя запись на каждый вид строки (подход/маршрут/отрезок).
struct FAsdaCtOnce
{
	int32 Rid = -2;     // rid цели последней строки уровня 1 (-2 — не писали; -1 законен: «ход без цели»)
	int32 Cycle = -1;   // номер цикла, в котором она написана (новый цикл по той же цели — снова уровень 1)
};

// Первая ли строка по цели Rid в текущем цикле (чистая проверка; отметку ставит AsdaCtOnceMark).
static bool AsdaCtOnceFirst(const FAsdaCtOnce& O, int32 Rid)
{
	return O.Rid != Rid || O.Cycle != AsdaCombatTrace::CurrentCycle();
}
// Отметить, что строка уровня 1 по цели Rid в текущем цикле написана.
static void AsdaCtOnceMark(FAsdaCtOnce& O, int32 Rid)
{
	O.Rid = Rid;
	O.Cycle = AsdaCombatTrace::CurrentCycle();
}
#endif // ASDA_COMBAT_TRACE

// ★ 01.11.2026 [ОРИГИНАЛ]: ключ `asda.Skill.ApproachOverAttack` живёт у диспетчера навыков (AsdaSkillUseSubsystem.cpp, разбор там) —
//   читаем аксессором, чтобы имя ключа не попадало в рабочий код. 1 — пока идёт подход ради навыка, автоатака стоит (эталон 102).
extern bool AsdaSkillApproachOverAttackOn();

// Ключ CVarAsdaNpcOpenOnSingleClick: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaNpcOpenOnSingleClick;
#else
extern TAsdaConstCVar<int32> CVarAsdaNpcOpenOnSingleClick;
#endif

// Ключ CVarAsdaFishLegacyDeferredClick: определение — в домашней части (R2, без static).
#if ASDA_DEV_CONSOLE
extern TAutoConsoleVariable<int32> CVarAsdaFishLegacyDeferredClick;
#else
extern TAsdaConstCVar<int32> CVarAsdaFishLegacyDeferredClick;
#endif

// CVarAsdaClickRayTerrain: определение — в домашней части (R2).
extern TAutoConsoleVariable<int32> CVarAsdaClickRayTerrain;

static bool AsdaClickRayTerrainPoint(const APlayerController* PC, const UAsdaWalkGrid* Grid, const FVector2D& ScreenPos, FVector& OutUe)
{
	if (!PC || !Grid || Grid->Width <= 0 || Grid->Height <= 0 || Grid->VertHeights.Num() == 0) { return false; }
	FVector OriginUe, DirUe;
	if (!PC->DeprojectScreenPositionToWorld(ScreenPos.X, ScreenPos.Y, OriginUe, DirUe)) { return false; }
	const FVector O = AsdaCoord::UeToGame3D(OriginUe);   // игра: (x, высота, z)
	const FVector D = AsdaCoord::UeToGame3D(DirUe).GetSafeNormal();
	const double MaxX = Grid->Width * Grid->CellSize, MaxZ = Grid->Height * Grid->CellSize;
	constexpr double kStep = 25.0;        // шаг марша, ед. (четверть тайла)
	constexpr double kMaxLen = 80000.0;   // дальность луча (вся карта по диагонали с запасом)
	// Знак «луч над землёй» в точке t; false в Out — точка вне сетки.
	auto Above = [&](double T, bool& bIn) -> double
	{
		const FVector P = O + D * T;
		bIn = P.X >= 0.0 && P.Z >= 0.0 && P.X < MaxX && P.Z < MaxZ;
		float Gy = 0.f;
		if (!bIn || !Grid->GetGroundY((float)P.X, (float)P.Z, Gy)) { bIn = false; return 1.0; }
		return P.Y - Gy;
	};
	double PrevT = 0.0;
	bool bPrevIn = false;
	double PrevH = Above(0.0, bPrevIn);
	for (double T = kStep; T <= kMaxLen; T += kStep)
	{
		bool bIn = false;
		const double H = Above(T, bIn);
		if (bIn && bPrevIn && PrevH > 0.0 && H <= 0.0)
		{
			double Lo = PrevT, Hi = T;
			for (int32 i = 0; i < 16; ++i)
			{
				const double Mid = 0.5 * (Lo + Hi);
				bool bM = false;
				if (Above(Mid, bM) > 0.0) { Lo = Mid; } else { Hi = Mid; }
			}
			const FVector Hit = O + D * Hi;
			OutUe = AsdaCoord::GameToUe3D(Hit);
			return true;
		}
		PrevT = T; PrevH = H; bPrevIn = bIn;
	}
	return false;
}

// Ближайшая к точке Hit (game XZ, не дальше Reach) зона портала карты MapId: true и её проходимая клетка, ближайшая к персонажу.
static bool AsdaNearestPortalZoneTile(const UAsdaWalkGrid* Grid, int32 MapId, const FVector2D& Hit, double Reach,
	const FVector2D& SelfGame, FIntPoint& OutTile, FIntPoint& OutZoneMin, FIntPoint& OutZoneMax);

static bool AsdaNearestPortalZoneTile(const UAsdaWalkGrid* Grid, int32 MapId, const FVector2D& Hit, double Reach,
	const FVector2D& SelfGame, FIntPoint& OutTile, FIntPoint& OutZoneMin, FIntPoint& OutZoneMax)
{
	if (!Grid || MapId < 0) { return false; }
	const AsdaMapRegistry::FPortalEdge* Best = nullptr;
	double BestD = Reach;
	for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph)
	{
		if (E.From != MapId) { continue; }
		const double Dx = FMath::Max3(E.X0 * 100.0 - Hit.X, 0.0, Hit.X - (E.X1 + 1) * 100.0);
		const double Dz = FMath::Max3(E.Z0 * 100.0 - Hit.Y, 0.0, Hit.Y - (E.Z1 + 1) * 100.0);
		const double D = FMath::Sqrt(Dx * Dx + Dz * Dz);
		if (D <= BestD) { BestD = D; Best = &E; }
	}
	if (!Best) { return false; }
	double BestT = TNumericLimits<double>::Max();
	bool bAny = false;
	for (int32 Z = Best->Z0; Z <= Best->Z1; ++Z)
	{
		for (int32 X = Best->X0; X <= Best->X1; ++X)
		{
			if (!Grid->IsTileWalkable(X, Z)) { continue; }
			const double D = FVector2D::DistSquared(AsdaCoord::TileCenterGame(X, Z), SelfGame);
			if (D < BestT) { BestT = D; OutTile = FIntPoint(X, Z); bAny = true; }
		}
	}
	OutZoneMin = FIntPoint(Best->X0, Best->Z0);
	OutZoneMax = FIntPoint(Best->X1, Best->Z1);
	return bAny;
}
