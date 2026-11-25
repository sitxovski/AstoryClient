// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ЗАКРЫТЫХ ТАЙЛОВ (баг владельца 04.11.2026: портал Fireway Forest → Flabis — «прошёл сквозь кольцо, ничего не
//   случилось, ушёл под карту»). [ИСПРАВЛЕНИЕ]
//
// Запуск: `asda.SelfTest.ClosedTiles [delay=N] [quit]` — игра на уровне firewayforest (сетка карты 16):
//   `asda_selftest_batch.py --map "/Game/Map/firewayforest/firewayforest?game=/Script/AsdaReborn.AsdaWorldGameMode" asda.SelfTest.ClosedTiles`.
//   Только офлайн.
// ЧТО ПРОВЕРЯЕТ (настоящие входы — `UAsdaWalkGrid::IsTileWalkable`, `AsdaPath::FindPathTiles`, ход `NetMove`):
//   C0  в `DA_WalkGrid` карты 16 есть `ClosedBits`: все 42 тайла кармана (x 83..87 × z 40..48, внутренность домика-портала
//       `mdel_portal01`) закрыты, исходный бит `.TER` у них — «проходим» (дефект данных оригинала), тайлы зоны портала
//       (83..89 × 49..51, сервер `FirewayForest_PortalConfig.Ini` [PORTAL_INFO_0]) НЕ закрыты.
//   C1  ключ `asda.Walk.ClosedTiles`: 1 — тайл кармана (85,44) непроходим; 0 — проходим (как эталон).
//   C2  автопуть (85,57) → (85,44) (вглубь домика): при 1 цель сдвинута, последний тайл пути — В ЗОНЕ портала (сервер на этом
//       шаге шлёт 5055); при 0 — путь доходит до (85,44) (прежнее поведение, ведёт под пол домика).
//   C3  ход своего персонажа (`NetMove` (85,57) → (85,44), шаг хода проверяет проходимость каждого тайла): при 1 встаёт В ЗОНЕ
//       портала, высота земли там ≥ 7950 (на уровне пола домика, не под ним); при 0 — уходит в карман, где земля ниже 7800
//       (воспроизведение «ушёл под карту» на эталонной проходимости).
//   C4  паритет с сервером: 14 тайлов карты 16, открытых в `.TER` и закрытых в `.sim` GameServer, закрыты; ключ 0 — открыты.
//   C5  регресс трёх порталов карты 16: все проходимые по `.TER` тайлы каждой зоны остаются проходимыми при ключе 1, и в каждую
//       зону ведёт автопуть с точки подхода.
//   C6  отказ сервера «в бою» (баг владельца «под атакой порталы не пускают»): 4207 с пустым телом → нотис 108000 в плашке 221
//       (цвет −11514) и в журнале, перехода нет (эталон `network.c:2112-2117`; сервер `sub_52C4C0` → `sub_556D10`).
//   C7  клик по графике портала (баг владельца «клик по кольцу не ведёт к порталу»): у каждого из трёх порталов карты 16 —
//       центр кольца `mdel_portaleff*`, свечение (ближайший эффект-актор у кольца, если он есть) и основание кольца; настоящий
//       `OnLeftClick` с подменённой экранной точкой → ушёл 4006, и отрезок хода ведёт в зону портала. Ключи
//       `asda.Move.ClickRayTerrain` = 1 (точка хода — луч по земле .TER, эталон `mapObjectsCollideRayBvh`) и
//       `asda.Move.ClickPortalZone` = 1 (земля за кольцом непроходима, луч прошёл над зоной → ход в зону).
// Строки — «САМОТЕСТ-ЗАКРЫТЫЕ», итог — «… ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestRunGate.h"   // FAsdaSelfTestRunGate — гейт прогона самотеста   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"                 // язык сессии (имя кадра самотеста центра Флабиса)
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaCoord.h"
#include "World/AsdaWalkGrid.h"
#include "World/AsdaPathfinder.h"
#include "World/AsdaMapRegistry.h"
#include "World/AsdaPortalGfx.h"         // меши набора портала (зачёт клика по модели)
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiVisibilitySubsystem.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"                     // FScreenshotRequest — кадры самотеста центра Флабиса
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaClosedTilesTest, Log, All);

// Доступ самотеста к закрытому мировому клику контроллера (friend в AsdaPlayerController.h).
struct FAsdaClosedTilesClickAccess
{
	// Мировой клик ТЕМ ЖЕ путём, что от мыши (`OnLeftClick`), с подменённой экранной точкой.
	//   ★ R5-fix8 (14.11.2026) [ИСПРАВЛЕНИЕ ТЕСТА]: гейт «над окном интерфейса» при подменённой точке берёт ЭТУ точку (шов в
	//   `AAsdaPlayerController::IsCursorOverUi`), а не курсор ОС. Точка клика теста задаётся геометрией карты (кольцо портала) и
	//   может лежать под окном интерфейса — такой клик игра законно гасит. Тест проверяет разбор клика по миру, а не гейт: точка
	//   под окном — интерфейс скрывается до конца прогона (хит-тест окон молчит), в конце возвращается (`RestoreUi`).
	static void Click(AAsdaPlayerController* PC, const FVector2D& Screen)
	{
		PC->DebugClickScreen = Screen;
		FName Zn;
		if (!bUiHiddenForPoint && PC->IsCursorOverUi(Zn))
		{
			if (UAsdaUiVisibilitySubsystem* Uv = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr)
			{
				Uv->SetUiShown(false, TEXT("самотест закрытых тайлов: точка клика под окном интерфейса"));
				bUiHiddenForPoint = true;
				UE_LOG(LogAsdaClosedTilesTest, Warning, TEXT("  точка клика (%.0f,%.0f) под окном «%s» — интерфейс скрыт до конца прогона (геометрия сцены теста)"),
					Screen.X, Screen.Y, *Zn.ToString());
			}
		}
		PC->OnLeftClick(/*bDouble*/false);
		PC->DebugClickScreen.Reset();
	}
	static inline bool bUiHiddenForPoint = false;   // интерфейс скрыт тестом — точка клика лежала под окном (вернуть в конце прогона)
	// В конце прогона: вернуть интерфейс, если его скрывал тест.
	static void RestoreUi(UWorld* W)
	{
		if (!bUiHiddenForPoint) { return; }
		bUiHiddenForPoint = false;
		UAsdaUiVisibilitySubsystem* Uv = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaUiVisibilitySubsystem>() : nullptr;
		if (Uv) { Uv->SetUiShown(true, TEXT("самотест закрытых тайлов: возврат интерфейса")); }
	}
	// Пик по мешам в экранной точке (для журнала: во что упирается луч мешей до правки).
	static AAsdaPlayerController::FAsdaPick Pick(AAsdaPlayerController* PC, const FVector2D& Screen)
	{
		PC->DebugClickScreen = Screen;
		const AAsdaPlayerController::FAsdaPick P = PC->PickForClick();
		PC->DebugClickScreen.Reset();
		return P;
	}
	// Конечная точка маршрута клика (game XZ); false — маршрута нет.
	static bool RouteFinal(const AAsdaPlayerController* PC, FVector2D& Out)
	{
		if (!PC || !PC->HasRoute()) { return false; }
		Out = PC->Route.Last();
		return true;
	}
};

namespace AsdaClosedTilesTest
{
	static const TCHAR* const kCmd = TEXT("asda.SelfTest.ClosedTiles");   // имя консольной команды
	static const TCHAR* const kPrefix = TEXT("САМОТЕСТ-ЗАКРЫТЫЕ");         // префикс строк журнала
	static const TCHAR* const kKey = TEXT("asda.Walk.ClosedTiles");        // проверяемый ключ
	static constexpr int32 kMapId = 16;                                     // Fireway Forest
	static constexpr float kSpeed = 0.35f;                                  // скорость хода теста, ед./мс
	static const FIntPoint kApproach(85, 57);                               // точка подхода с севера (открытая земля)
	static const FIntPoint kInside(85, 44);                                 // глубина кармана (земля .TER ≈ 7654)
	static constexpr float kFloorMin = 7950.f;                              // ниже — уже под полом домика (пол ≈ 8000)
	static constexpr float kPocketLow = 7800.f;                             // земля кармана в глубине — заведомо ниже

	// Зона портала: x0..x1 × z0..z1 включительно (серверный прямоугольник, `asda_map_registry.json` portal_edges).
	struct FZone { int32 X0, Z0, X1, Z1; int32 To; FIntPoint From; };
	static const FZone kZones[] = {
		{ 83,  49,  89,  51, 13, FIntPoint(85, 57)  },   // → Flabis
		{ 145, 477, 147, 482, 17, FIntPoint(140, 479) },  // → Castle of Chaos
		{ 464, 91,  469, 93, 18, FIntPoint(460, 92)  },   // → Inferion (подход с запада: южнее и восточнее — стена)
	};
	// Паритет с сервером: тайлы карты 16, открытые в .TER и закрытые битом 0 .sim (asda_portal_pockets.py, 04.11.2026).
	static const FIntPoint kParity[] = {
		{63, 63}, {63, 64}, {63, 65}, {64, 65}, {65, 65}, {64, 66}, {65, 66},
		{64, 70}, {65, 70}, {64, 71}, {65, 71}, {66, 71}, {65, 72}, {66, 72},
	};

	// Кольца `mdel_portaleff*` карты 16 по зонам kZones: центр габаритов (game x, высота, z) и низ кольца (из `.mapraw`).
	struct FRing { FVector CenterGame; float BottomY; };
	static const FRing kRings[] = {
		{ FVector(8551.f, 8221.f, 5002.f),   8037.f },   // → Flabis
		{ FVector(14760.f, 8539.f, 47874.f), 8355.f },   // → Castle of Chaos
		{ FVector(46650.f, 8760.f, 9396.f),  8576.f },   // → Inferion
	};

	// Состояние прогона (живёт между таймерами).
	struct FState
	{
		int32 Ok = 0, Fail = 0;                     // счётчики проверок
		TWeakObjectPtr<AAsdaPlayerCharacter> Me;    // свой персонаж
		TWeakObjectPtr<UAsdaNetSubsystem> Net;      // сеть (перехват 4006, подача 4207)
		uint32 CapGen = 0;                          // поколение перехвата отправок
		TArray<FVector2D> Sent4006Dst;              // цели перехваченных 4006 (game XZ)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s ok   %s"), kPrefix, *What); }
			else   { ++Fail; UE_LOG(LogAsdaClosedTilesTest, Error, TEXT("%s FAIL %s"), kPrefix, *What); }
		}
	};

	// Исходный бит проходимости .TER (без закрытий и открытой воды).
	static bool OrigWalk(const UAsdaWalkGrid* G, int32 X, int32 Z)
	{
		const int32 Idx = Z * G->Width + X;
		return G->Bits.IsValidIndex(Idx >> 3) && ((G->Bits[Idx >> 3] >> (Idx & 7)) & 1) != 0;
	}

	// Тайл внутри зоны.
	static bool InZone(const FZone& Zn, const FIntPoint& T) { return T.X >= Zn.X0 && T.X <= Zn.X1 && T.Y >= Zn.Z0 && T.Y <= Zn.Z1; }

	// Центр тайла в game XZ.
	static FVector2D TileGame(const FIntPoint& T) { return AsdaCoord::TileCenterGame(T.X, T.Y); }

	// Тайл game-точки.
	static FIntPoint TileOf(const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X / 100.f), FMath::FloorToInt(P.Y / 100.f)); }

	// Запуск шага через Delay секунд от «сейчас».
	using AsdaSelfTestCtx::ByWorld::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Время хода между двумя тайлами при скорости теста (+ запас), с.
	static float WalkTime(const FIntPoint& A, const FIntPoint& B)
	{
		return FVector2D::Distance(TileGame(A), TileGame(B)) / (kSpeed * 1000.f) + 1.0f;
	}

	// Отрезок A→B (game XZ) пересекает зону или кончается в ней (выборка по 200 точкам).
	static bool SegmentHitsZone(const FZone& Zn, const FVector2D& A, const FVector2D& B)
	{
		for (int32 i = 0; i <= 200; ++i)
		{
			if (InZone(Zn, TileOf(FMath::Lerp(A, B, i / 200.f)))) { return true; }
		}
		return false;
	}

	// C8: аудит ВСЕХ зон порталов ВСЕХ карт (`kPortalGraph`, 41 зона на 19 картах) по сеткам `DA_WalkGrid` каждой карты:
	//   закрытых тайлов на карте столько, сколько в `Tools/data/walk_closed/<карта>.json`; ни один тайл зоны не закрыт и все
	//   проходимые по .TER тайлы зоны проходимы; в зону ведёт автопуть с проходимой клетки снаружи (3…12 клеток от зоны);
	//   у silalis → castle_of_chess закрыты 116 тайлов подхода, открытых в .TER и закрытых в .sim сервера (паритет).
	struct FMapDir { int32 Id; const TCHAR* Folder; int32 Closed; };
	static const FMapDir kMapDirs[] = {
		{ 0, TEXT("silalis"), 205 }, { 1, TEXT("rein"), 45 }, { 2, TEXT("conquester"), 25 }, { 3, TEXT("alpen"), 0 },
		{ 4, TEXT("alpen_dungeon"), 0 }, { 5, TEXT("nightvalley"), 4 }, { 6, TEXT("sunnycoast"), 0 }, { 7, TEXT("flammio"), 0 },
		{ 8, TEXT("palace_of_queen"), 0 }, { 9, TEXT("castle_of_chess"), 0 }, { 10, TEXT("t_palace_of_queen"), 0 },
		{ 11, TEXT("t_castle_of_chess"), 0 }, { 12, TEXT("Mountain Flamion"), 7 }, { 13, TEXT("flabis"), 0 },
		{ 14, TEXT("StagnantDesert"), 1 }, { 15, TEXT("dragonlair"), 0 }, { 16, TEXT("firewayforest"), 56 },
		{ 17, TEXT("castle_of_chaos"), 0 }, { 18, TEXT("inferion"), 0 },
	};

	static void CheckAllPortalZones(const TSharedRef<FState>& S)
	{
		int32 Zones = 0, ZonesOk = 0;
		TSet<int32> MapsChecked;
		for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph)
		{
			const FMapDir* Md = nullptr;
			for (const FMapDir& M : kMapDirs) { if (M.Id == E.From) { Md = &M; } }
			if (!Md) { S->Expect(false, FString::Printf(TEXT("C8 карта %d: нет в таблице самотеста"), E.From)); continue; }
			const UAsdaWalkGrid* G = LoadObject<UAsdaWalkGrid>(nullptr, *AsdaMapRegistry::WalkGridObjectPath(Md->Folder));
			if (!G) { S->Expect(false, FString::Printf(TEXT("C8 %s: нет DA_WalkGrid"), Md->Folder)); continue; }
			if (!MapsChecked.Contains(E.From))
			{
				MapsChecked.Add(E.From);
				int32 N = 0;
				for (uint8 B : G->ClosedBits) { N += FMath::CountBits(B); }
				S->Expect(N == Md->Closed, FString::Printf(TEXT("C8 %s: закрыто %d тайлов (данные: %d)"), Md->Folder, N, Md->Closed));
			}
			++Zones;
			int32 Orig = 0, Walk = 0, Closed = 0;
			FIntPoint Mid(-1, -1);
			for (int32 Z = E.Z0; Z <= E.Z1; ++Z)
			{
				for (int32 X = E.X0; X <= E.X1; ++X)
				{
					if (G->IsTileClosed(X, Z)) { ++Closed; }
					if (!OrigWalk(G, X, Z)) { continue; }
					++Orig;
					if (G->IsTileWalkable(X, Z)) { ++Walk; if (Mid.X < 0) { Mid = FIntPoint(X, Z); } }
				}
			}
			// Клетка подхода: проходимая, вне зоны, 3…12 клеток от зоны, с путём в зону.
			const FZone Zn{ E.X0, E.Z0, E.X1, E.Z1, E.To, FIntPoint(-1, -1) };
			bool bReach = false;
			FIntPoint From(-1, -1);
			for (int32 R = 3; R <= 12 && !bReach; ++R)
			{
				for (int32 Dz = -R; Dz <= R && !bReach; ++Dz)
				{
					for (int32 Dx = -R; Dx <= R && !bReach; ++Dx)
					{
						if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dz)) != R) { continue; }
						const FIntPoint T((E.X0 + E.X1) / 2 + Dx, (E.Z0 + E.Z1) / 2 + Dz);
						if (InZone(Zn, T) || !G->IsTileWalkable(T.X, T.Y)) { continue; }
						TArray<FIntPoint> P; FAsdaPathStats St;
						if (Mid.X >= 0 && AsdaPath::FindPathTiles(G, T, Mid, P, &St) && St.bReachedGoal) { bReach = true; From = T; }
					}
				}
			}
			const bool bOk = Orig > 0 && Walk == Orig && Closed == 0 && bReach;
			ZonesOk += bOk ? 1 : 0;
			S->Expect(bOk, FString::Printf(TEXT("C8 %s → %d зона %d..%d×%d..%d: проходимо %d из %d, закрыто %d, автопуть с (%d,%d) доходит=%d"),
				Md->Folder, E.To, E.X0, E.X1, E.Z0, E.Z1, Walk, Orig, Closed, From.X, From.Y, bReach ? 1 : 0));
			if (E.From == 0 && E.To == 9)   // silalis → castle_of_chess: паритет подхода
			{
				int32 Par = 0;
				for (int32 Z = FMath::Max(1, E.Z0 - 12); Z <= FMath::Min(G->Height - 2, E.Z1 + 12); ++Z)
				{
					for (int32 X = FMath::Max(1, E.X0 - 12); X <= FMath::Min(G->Width - 2, E.X1 + 12); ++X)
					{
						if (OrigWalk(G, X, Z) && G->IsTileClosed(X, Z)) { ++Par; }
					}
				}
				S->Expect(Par == 116, FString::Printf(TEXT("C8 silalis → 9: у подхода закрыто %d тайлов паритета с .sim (ожидалось 116)"), Par));
			}
		}
		S->Expect(Zones == 41 && ZonesOk == Zones, FString::Printf(TEXT("C8 итог: зон %d, исправных %d"), Zones, ZonesOk));
	}

	// C6: отказ портала в бою (4207, пустое тело) — нотис 108000 в плашке 221 и журнале, перехода нет.
	static void CheckCombatRefusal(UWorld* W, const TSharedRef<FState>& S)
	{
		UAsdaNetSubsystem* Net = S->Net.Get();
		UGameInstance* GI = W->GetGameInstance();
		UAsdaChatSubsystem* Chat = GI ? GI->GetSubsystem<UAsdaChatSubsystem>() : nullptr;
		if (!Net || !Chat) { S->Expect(false, TEXT("C6 нет сети/чата")); return; }
		const FString Text = AsdaNotice::Loc(GI, 108000);
		S->Expect(!Text.IsEmpty() && !Text.StartsWith(TEXT("[#")), FString::Printf(TEXT("C6 строка 108000 есть: «%s»"), *Text));
		const int32 War0 = Chat->DebugCountWarLines(Text);
		const int32 Sys0 = Chat->DebugLastSystemLineIndex(Text);
		Net->bTransferInProgress = false; Net->bPendingEnterWorld = false; Net->bPortalPending = false;
		Net->DebugFeedPacket(4207, TArray<uint8>());
		FColor Col(0, 0, 0);
		const int32 War1 = Chat->DebugCountWarLines(Text, &Col);
		const int32 Sys1 = Chat->DebugLastSystemLineIndex(Text);
		S->Expect(War1 == War0 + 1 && Col.R == 0xFF && Col.G == 0xD3 && Col.B == 0x06,
			FString::Printf(TEXT("C6 4207 → плашка 221 «%s» (+%d, цвет #%02X%02X%02X)"), *Text, War1 - War0, Col.R, Col.G, Col.B));
		S->Expect(Sys1 >= 0 && Sys1 != Sys0, FString::Printf(TEXT("C6 4207 → строка журнала (индекс %d → %d)"), Sys0, Sys1));
		S->Expect(!Net->bTransferInProgress && !Net->bPendingEnterWorld && !Net->bPortalPending, TEXT("C6 4207 → перехода нет"));
	}

	// Подписи точек клика C7.
	static const TCHAR* const kWhat[] = { TEXT("центр кольца"), TEXT("свечение"), TEXT("основание кольца") };

	// C7: клик по точке Pi (0 центр, 1 свечение, 2 основание) портала Zi; затем следующая точка/портал; в конце Done.
	static void ClickStep(TWeakObjectPtr<UWorld> WW, TSharedRef<FState> S, int32 Zi, int32 Pi, TFunction<void()> Done, int32 Ci = 0)
	{
		UWorld* W = WW.Get();
		AAsdaPlayerCharacter* Me = S->Me.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		UAsdaNetSubsystem* Net = S->Net.Get();
		if (!W || !Me || !PC || !Net || !Me->WalkGrid) { Done(); return; }
		if (Zi >= (int32)UE_ARRAY_COUNT(kZones)) { Done(); return; }
		if (Pi > 2) { ClickStep(WW, S, Zi + 1, 0, Done); return; }
		const FZone& Zn = kZones[Zi];
		const FRing& Rg = kRings[Zi];
		FVector TargetUe = AsdaCoord::GameToUe3D(Rg.CenterGame);
		if (Pi == 1)
		{
			const AActor* Best = nullptr;
			double BestD = 600.0;
			for (TActorIterator<AAsdaEffectActor> It(W); It; ++It)
			{
				const double D = FVector::Dist(It->GetActorLocation(), TargetUe);
				if (D < BestD) { BestD = D; Best = *It; }
			}
			if (!Best)
			{
				UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо C7 портал → %d: эффект-актора у кольца нет (свечение — сам меш кольца, проверено центром)"), kPrefix, Zn.To);
				ClickStep(WW, S, Zi, Pi + 1, Done);
				return;
			}
			TargetUe = Best->GetActorLocation();
		}
		else if (Pi == 2)
		{
			TargetUe = AsdaCoord::GameToUe3D(FVector(Rg.CenterGame.X, Rg.BottomY + 20.f, Rg.CenterGame.Z));
		}
		// Персонаж — перед кольцом против взгляда камеры (кольцо в кадре), на проходимой клетке вне зоны, с открытым по сетке путём
		//   до зоны. Кандидаты: поворот взгляда 0, ±30…±90°, дальность 500…1500; Ci — номер кандидата (не в кадре → следующий).
		FVector Fwd = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector(1, 0, 0);
		Fwd.Z = 0.f;
		Fwd = Fwd.GetSafeNormal();
		if (Fwd.IsNearlyZero()) { Fwd = FVector(1, 0, 0); }
		TArray<FVector2D> Cands;
		for (const float Ang : { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f })
		{
			const FVector Dir = Fwd.RotateAngleAxis(Ang, FVector::UpVector);
			for (const float D : { 900.f, 700.f, 1200.f, 500.f, 1500.f })
			{
				const FIntPoint T = TileOf(AsdaCoord::UeToGame2D(TargetUe - Dir * D));
				if (!Me->WalkGrid->IsTileWalkable(T.X, T.Y) || InZone(Zn, T)) { continue; }
				TArray<FIntPoint> Pth; FAsdaPathStats St;
				if (!AsdaPath::FindPathTiles(Me->WalkGrid, T, FIntPoint((Zn.X0 + Zn.X1) / 2, (Zn.Z0 + Zn.Z1) / 2), Pth, &St) || Pth.Num() == 0
					|| !InZone(Zn, Pth.Last()))
				{
					continue;   // с этой клетки к зоне не дойти — не точка подхода
				}
				const FVector2D G = AsdaCoord::TileCenterGame(T);
				if (!Cands.Contains(G)) { Cands.Add(G); }
			}
		}
		if (Ci >= Cands.Num())
		{
			S->Expect(false, FString::Printf(TEXT("C7 портал → %d (%s): нет точки подхода, из которой кольцо в кадре (кандидатов %d)"), Zn.To, kWhat[Pi], Cands.Num()));
			ClickStep(WW, S, Zi, Pi + 1, Done);
			return;
		}
		const FVector2D Stand = Cands[Ci];
		PC->CancelRoute();
		Me->StopMove();
		Me->NetSnapHard(Stand);
		Net->ClearMoveRequestInFlight(TEXT("самотест закрытых тайлов: новый клик"));
		After(W, 0.6f, [WW, S, Zi, Pi, Done, TargetUe, Stand, Ci]()
		{
			UWorld* W2 = WW.Get();
			AAsdaPlayerCharacter* Me2 = S->Me.Get();
			AAsdaPlayerController* PC2 = W2 ? Cast<AAsdaPlayerController>(W2->GetFirstPlayerController()) : nullptr;
			UAsdaNetSubsystem* Net2 = S->Net.Get();
			if (!W2 || !Me2 || !PC2 || !Net2) { Done(); return; }
			const FZone& Zn2 = kZones[Zi];
			FVector2D Scr;
			int32 VX = 0, VY = 0;
			PC2->GetViewportSize(VX, VY);
			const bool bProj = PC2->ProjectWorldLocationToScreen(TargetUe, Scr) && Scr.X > 4 && Scr.Y > 4 && Scr.X < VX - 4 && Scr.Y < VY - 4;
			if (!bProj)
			{
				ClickStep(WW, S, Zi, Pi, Done, Ci + 1);   // не в кадре — следующий кандидат точки подхода
				return;
			}
			const AAsdaPlayerController::FAsdaPick Pk = FAsdaClosedTilesClickAccess::Pick(PC2, Scr);   // для журнала
			const FString PkName = Pk.Actor.IsValid() ? Pk.Actor->GetName() : FString(TEXT("ничего"));
			const FVector PkPt = Pk.Point;
			S->Sent4006Dst.Reset();
			const bool bWasInGame = Net2->bInGameConnected;
			Net2->bInGameConnected = true;   // 4006 отрезка уходит только «в игре»; пакет забирает перехват, в сеть ничего
			FAsdaClosedTilesClickAccess::Click(PC2, Scr);
			Net2->bInGameConnected = bWasInGame;
			FVector2D Final(-1.f, -1.f);
			const bool bRoute = FAsdaClosedTilesClickAccess::RouteFinal(PC2, Final);   // цель маршрута (первый 4006 — лишь отрезок)
			After(W2, 0.5f, [WW, S, Zi, Pi, Done, Stand, PkName, PkPt, Final, bRoute]()
			{
				UWorld* W3 = WW.Get();
				AAsdaPlayerCharacter* Me3 = S->Me.Get();
				AAsdaPlayerController* PC3 = W3 ? Cast<AAsdaPlayerController>(W3->GetFirstPlayerController()) : nullptr;
				if (!W3 || !Me3 || !PC3) { Done(); return; }
				const FZone& Zn3 = kZones[Zi];
				const bool bSent = S->Sent4006Dst.Num() > 0;
				// Цель — конец маршрута; без маршрута (прямой отрезок) — цель первого 4006.
				const FVector2D Dst = bRoute ? Final : (bSent ? S->Sent4006Dst[0] : FVector2D(-1.f, -1.f));
				const FIntPoint DT = TileOf(Dst);
				const bool bToZone = bSent && (InZone(Zn3, DT) || SegmentHitsZone(Zn3, Stand, Dst));
				S->Expect(bSent && bToZone, FString::Printf(TEXT("C7 портал → %d, клик «%s» (луч по мешам: %s (%.0f,%.0f,%.0f)): 4006 %s, цель маршрута (%d,%d), ход ведёт в зону %d..%d×%d..%d = %d"),
					Zn3.To, kWhat[Pi], *PkName, PkPt.X, PkPt.Y, PkPt.Z, bSent ? TEXT("ушёл") : TEXT("НЕ ушёл"), DT.X, DT.Y,
					Zn3.X0, Zn3.X1, Zn3.Z0, Zn3.Z1, bToZone ? 1 : 0));
				PC3->CancelRoute();
				Me3->StopMove();
				if (S->Net.IsValid()) { S->Net->ClearMoveRequestInFlight(TEXT("самотест закрытых тайлов: клик проверен")); }
				ClickStep(WW, S, Zi, Pi + 1, Done);
			});
		});
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmd, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;

		// t = delay: офлайн-спавн своего павна у портала штатными крюками.
		After(W, A.Delay, [WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), kMapId));
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), kApproach.X, kApproach.Y));
		});

		// Финал (общий для всех веток и отказов).
		auto Finish = [WW, S, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			AsdaCVarSave::RestoreAll(kCmd);
			AsdaPath::InvalidateCostGrid();
			FAsdaClosedTilesClickAccess::RestoreUi(Wd);
			AsdaSelfTestKit::Finish(Wd, kCmd, bQuit, LogAsdaClosedTilesTest, kPrefix, S->Ok, S->Fail, TEXT(" (карта 16)"));
		};

		After(W, A.Delay + 3.f, [WW, Gate, S, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			if (!Me || !Me->WalkGrid) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет своего персонажа / сетки карты")); return; }
			S->Me = Me;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Net) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("нет сети")); return; }
			S->Net = Net;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4006 && P && Len >= 16 + 21)   // MD5 16 + тело: цель @+13/+17 (float, game XZ)
				{
					S->Sent4006Dst.Add(FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17)));
				}
			}, 0, kCmd);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmd, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmd);
			AsdaCVarSave::SetForTest(kKey, 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.ClickRayTerrain"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.ClickPortalZone"), 1);
			const UAsdaWalkGrid* G = Me->WalkGrid;

			// ── C0: данные ассета ──
			int32 NClosed = 0;
			for (uint8 B : G->ClosedBits) { NClosed += FMath::CountBits(B); }
			int32 PocketClosed = 0, PocketOrigOpen = 0, PocketTotal = 0;
			for (int32 Z = 40; Z <= 48; ++Z)
			{
				for (int32 X = 83; X <= 87; ++X)
				{
					if (!OrigWalk(G, X, Z)) { continue; }   // в прямоугольнике 83..87×40..48 у .TER проходимы ровно 42 тайла кармана
					++PocketTotal; ++PocketOrigOpen;
					if (G->IsTileClosed(X, Z)) { ++PocketClosed; }
				}
			}
			int32 ZoneClosed = 0;
			for (int32 Z = kZones[0].Z0; Z <= kZones[0].Z1; ++Z)
			{
				for (int32 X = kZones[0].X0; X <= kZones[0].X1; ++X) { if (G->IsTileClosed(X, Z)) { ++ZoneClosed; } }
			}
			S->Expect(NClosed == 56, FString::Printf(TEXT("C0 ClosedBits карты 16: закрыто %d тайлов (ожидалось 42 кармана + 14 паритета = 56)"), NClosed));
			S->Expect(PocketTotal == 42 && PocketClosed == 42 && PocketOrigOpen == 42,
				FString::Printf(TEXT("C0 карман домика-портала: исходно проходимы по .TER %d, закрыто %d из 42"), PocketOrigOpen, PocketClosed));
			S->Expect(ZoneClosed == 0, FString::Printf(TEXT("C0 зона портала 83..89×49..51 не закрыта (закрыто %d)"), ZoneClosed));

			// ── C1: ключ ──
			S->Expect(!G->IsTileWalkable(kInside.X, kInside.Y), TEXT("C1 ключ 1: тайл кармана (85,44) непроходим"));
			AsdaCVarSave::SetForTest(kKey, 0);
			S->Expect(G->IsTileWalkable(kInside.X, kInside.Y), TEXT("C1 ключ 0: тайл кармана (85,44) проходим (эталон)"));

			// ── C2: автопуть вглубь домика ──
			{
				TArray<FIntPoint> P0; FAsdaPathStats St0;
				const bool b0 = AsdaPath::FindPathTiles(G, kApproach, kInside, P0, &St0);
				AsdaCVarSave::SetForTest(kKey, 1);
				TArray<FIntPoint> P1; FAsdaPathStats St1;
				const bool b1 = AsdaPath::FindPathTiles(G, kApproach, kInside, P1, &St1);
				const FIntPoint L1 = P1.Num() ? P1.Last() : FIntPoint(-1, -1);
				S->Expect(b1 && St1.bGoalMoved && P1.Num() > 0 && InZone(kZones[0], L1),
					FString::Printf(TEXT("C2 ключ 1: цель сдвинута=%d, конец пути (%d,%d) — в зоне портала"), St1.bGoalMoved ? 1 : 0, L1.X, L1.Y));
				bool bAnyPocket = false;
				for (const FIntPoint& T : P1) { bAnyPocket |= (T.X >= 83 && T.X <= 87 && T.Y >= 40 && T.Y <= 48); }
				S->Expect(!bAnyPocket, TEXT("C2 ключ 1: ни один узел пути не в кармане"));
				S->Expect(b0 && St0.bReachedGoal && P0.Num() > 0 && P0.Last() == kInside,
					FString::Printf(TEXT("C2 ключ 0: путь доходит до (85,44) (эталон), узлов %d"), P0.Num()));
			}

			// ── C3 (ключ 1): ход встаёт в зоне ──
			AsdaCVarSave::SetForTest(kKey, 1);
			Me->StopMove();
			Me->NetSnapHard(TileGame(kApproach));
			Me->NetMove(TileGame(kApproach), TileGame(kInside), kSpeed);
			After(Wd, WalkTime(kApproach, kInside), [WW, S, Finish]()
			{
				UWorld* W2 = WW.Get(); AAsdaPlayerCharacter* Me2 = S->Me.Get();
				if (!W2 || !Me2 || !Me2->WalkGrid) { Finish(); return; }
				const FVector2D P = Me2->GetGamePosXZ();
				const FIntPoint T = TileOf(P);
				float Gy = 0.f; Me2->WalkGrid->GetGroundY((float)P.X, (float)P.Y, Gy);
				S->Expect(InZone(kZones[0], T) && Gy >= kFloorMin,
					FString::Printf(TEXT("C3 ключ 1: ход встал на (%d,%d) — в зоне портала, земля %.0f (≥ %.0f, не под полом домика)"), T.X, T.Y, Gy, kFloorMin));

				// ── C3 (ключ 0): эталонная проходимость — уходит в карман ──
				AsdaCVarSave::SetForTest(kKey, 0);
				Me2->StopMove();
				Me2->NetSnapHard(TileGame(kApproach));
				Me2->NetMove(TileGame(kApproach), TileGame(kInside), kSpeed);
				After(W2, WalkTime(kApproach, kInside), [WW, S, Finish]()
				{
					UWorld* W3 = WW.Get(); AAsdaPlayerCharacter* Me3 = S->Me.Get();
					if (!W3 || !Me3 || !Me3->WalkGrid) { Finish(); return; }
					const FVector2D P3 = Me3->GetGamePosXZ();
					const FIntPoint T3 = TileOf(P3);
					float Gy3 = 0.f; Me3->WalkGrid->GetGroundY((float)P3.X, (float)P3.Y, Gy3);
					S->Expect(T3.Y <= 48 && Gy3 < kPocketLow,
						FString::Printf(TEXT("C3 ключ 0: ход ушёл в карман (%d,%d), земля %.0f (< %.0f) — воспроизведение «под картой»"), T3.X, T3.Y, Gy3, kPocketLow));
					Me3->StopMove();
					Me3->NetSnapHard(TileGame(kApproach));
					AsdaCVarSave::SetForTest(kKey, 1);
					const UAsdaWalkGrid* G3 = Me3->WalkGrid;

					// ── C4: паритет с сервером ──
					int32 ParClosed = 0, ParOrigOpen = 0;
					for (const FIntPoint& T : kParity)
					{
						if (OrigWalk(G3, T.X, T.Y)) { ++ParOrigOpen; }
						if (!G3->IsTileWalkable(T.X, T.Y)) { ++ParClosed; }
					}
					S->Expect(ParClosed == UE_ARRAY_COUNT(kParity) && ParOrigOpen == UE_ARRAY_COUNT(kParity),
						FString::Printf(TEXT("C4 паритет .sim: открыты в .TER %d, закрыты при ключе 1 — %d из %d"), ParOrigOpen, ParClosed, (int32)UE_ARRAY_COUNT(kParity)));
					AsdaCVarSave::SetForTest(kKey, 0);
					int32 ParOpen0 = 0;
					for (const FIntPoint& T : kParity) { if (G3->IsTileWalkable(T.X, T.Y)) { ++ParOpen0; } }
					S->Expect(ParOpen0 == UE_ARRAY_COUNT(kParity), FString::Printf(TEXT("C4 ключ 0: паритетные тайлы снова проходимы (%d)"), ParOpen0));
					AsdaCVarSave::SetForTest(kKey, 1);

					// ── C5: регресс всех трёх порталов карты ──
					for (const FZone& Zn : kZones)
					{
						int32 Orig = 0, Walk = 0;
						FIntPoint Mid(-1, -1);
						for (int32 Z = Zn.Z0; Z <= Zn.Z1; ++Z)
						{
							for (int32 X = Zn.X0; X <= Zn.X1; ++X)
							{
								if (!OrigWalk(G3, X, Z)) { continue; }
								++Orig;
								if (G3->IsTileWalkable(X, Z)) { ++Walk; if (Mid.X < 0) { Mid = FIntPoint(X, Z); } }
							}
						}
						TArray<FIntPoint> P; FAsdaPathStats St;
						const bool b = Mid.X >= 0 && AsdaPath::FindPathTiles(G3, Zn.From, Mid, P, &St);
						S->Expect(Orig > 0 && Walk == Orig && b && St.bReachedGoal,
							FString::Printf(TEXT("C5 портал → %d: проходимо %d из %d тайлов зоны, автопуть (%d,%d) → (%d,%d) доходит=%d"),
								Zn.To, Walk, Orig, Zn.From.X, Zn.From.Y, Mid.X, Mid.Y, St.bReachedGoal ? 1 : 0));
					}

					// ── C8: все зоны порталов всех карт ──
					CheckAllPortalZones(S);

					// ── C6: отказ портала в бою ──
					CheckCombatRefusal(W3, S);

					// ── C7: клики по графике порталов ──
					ClickStep(WW, S, 0, 0, Finish);
				});
			});
		});
	}
}

// ★★ САМОТЕСТ КЛИКОВ ПО ГРАФИКЕ ПОРТАЛОВ ВСЕХ КАРТ (04.11.2026, баг владельца «клик по кольцу/свечению/модели портала не ведёт
//   к порталу»; продолжение C7 на все 19 карт). [ИСПРАВЛЕНИЕ]
// Запуск: `asda.SelfTest.PortalClicks map=N [delay=N] [quit]` — игра на уровне карты N (`/Game/Map/<папка>/<папка>`), офлайн.
// ЧТО ПРОВЕРЯЕТ: у каждой зоны портала карты N (`kPortalGraph`) — клики настоящим `OnLeftClick` по ВИДИМОЙ графике портала.
//   Персонаж ставится на проходимую клетку у зоны (с путём в зону, против взгляда камеры); экран прочёсывается пиком через каждые
//   kScanStep пикселей: пиксели, где луч попал в меш набора портала (`AsdaPortalGfx`) этой зоны, делятся на «кольцо»
//   (`mdel_portaleff*`) и «модель» (домик, ворота, арка, двери); по каждому виду кликаются до трёх пикселей (верхний, средний,
//   нижний). Свечение — эффект-актор в зоне или в клетке от неё (свечения дальше — не графика портала, это клик в землю).
//   Зачёт клика: ушёл 4006, конец маршрута в зоне либо отрезок хода пересекает зону. У зоны, где `.mapraw` ставит кольцо / модель
//   (таблица kExpect), хотя бы одна точка подхода должна их показать и все клики по ним — вести в зону.
//   Ключи на время прогона: `asda.Walk.ClosedTiles`, `asda.Move.ClickRayTerrain`, `asda.Move.ClickPortalZone`,
//   `asda.Move.ClickPortalGfx` = 1. Кольцо без серверной зоны (rein, данные оригинала) не проверяется (зоны нет).
// Строки — «САМОТЕСТ-ЗАКРЫТЫЕ ok/FAIL …» и «САМОТЕСТ-КЛИК-ПОРТАЛ …», итог — «САМОТЕСТ-КЛИК-ПОРТАЛ ИТОГ: ok=… fail=… (карта N …)».
namespace AsdaPortalClicksTest
{
	using namespace AsdaClosedTilesTest;
	static const TCHAR* const kCmdPc = TEXT("asda.SelfTest.PortalClicks");   // имя консольной команды
	static const TCHAR* const kPrefixPc = TEXT("САМОТЕСТ-КЛИК-ПОРТАЛ");        // префикс строк журнала
	static constexpr int32 kScanStep = 12;                                     // шаг прочёсывания экрана, px
	static constexpr double kGlowReach = 100.0;                                // свечение не дальше клетки от зоны, ед.

	enum class EKind : uint8 { Ring, Model };   // вид графики: кольцо-эффект / модель портала (домик, ворота, арка, двери)

	// Что `.mapraw` ставит у зоны: карта, зона (x0, z0, x1, z1, куда), вид, главный объект (для журнала) и три точки его видимой
	//   поверхности (центры треугольников выше земли .TER на 40+, не дальше 3 клеток от зоны), game (x, высота, z) — для моделей без
	//   коллизии (луч пика их не видит, игрок кликает «по нарисованному»).
	struct FExpect { int32 Map; int32 X0, Z0, X1, Z1, To; EKind Kind; const TCHAR* Name; FVector P[3]; };
	static const FExpect kExpect[] = {
		{ 3, 463, 345, 465, 349, 1, EKind::Model, TEXT("mdel_alpenportal03_a_00"), { FVector(46534.f, 1304.f, 34750.f), FVector(46766.f, 1177.f, 34760.f), FVector(46766.f, 1177.f, 34760.f) } },
		{ 2, 121, 454, 125, 456, 0, EKind::Ring, TEXT("mdel_portaleff_a_00"), { FVector(12353.f, 1763.f, 45538.f), FVector(12414.f, 1779.f, 45529.f), FVector(12251.f, 1526.f, 45534.f) } },
		{ 2, 121, 454, 125, 456, 0, EKind::Model, TEXT("mdel_fortressdoor_a_00_2"), { FVector(12330.f, 1872.f, 45664.f), FVector(12216.f, 2378.f, 45851.f), FVector(12706.f, 1860.f, 45648.f) } },
		{ 2, 159, 51, 164, 53, 4, EKind::Ring, TEXT("mdel_portaleff_a_00"), { FVector(16209.f, 3693.f, 5281.f), FVector(16100.f, 3738.f, 5284.f), FVector(16100.f, 3738.f, 5284.f) } },
		{ 2, 159, 51, 164, 53, 4, EKind::Model, TEXT("mdel_fortressdoor_a_00"), { FVector(16170.f, 3831.f, 5083.f), FVector(16284.f, 4337.f, 4895.f), FVector(16479.f, 3906.f, 4992.f) } },
		{ 2, 460, 80, 462, 84, 7, EKind::Ring, TEXT("mdel_portaleff_a_00"), { FVector(46317.f, 3796.f, 8264.f), FVector(46325.f, 3737.f, 8384.f), FVector(46325.f, 3737.f, 8384.f) } },
		{ 2, 460, 80, 462, 84, 7, EKind::Model, TEXT("mdel_fortressdoor_a_00_2"), { FVector(46384.f, 3892.f, 8191.f), FVector(46571.f, 4398.f, 8078.f), FVector(46409.f, 4130.f, 8564.f) } },
		{ 2, 363, 460, 365, 464, 10, EKind::Model, TEXT("mdel_fortressdoor_a_00_3"), { FVector(36445.f, 2175.f, 46252.f), FVector(36490.f, 2888.f, 46328.f), FVector(36777.f, 2358.f, 46371.f) } },
		{ 15, 403, 490, 423, 495, 7, EKind::Model, TEXT("mdel_steamgate01_a_00"), { FVector(41258.f, 1846.f, 49362.f), FVector(40536.f, 658.f, 49797.f), FVector(40536.f, 658.f, 49797.f) } },
		{ 16, 83, 49, 89, 51, 13, EKind::Ring, TEXT("mdel_portaleff_a_00"), { FVector(8641.f, 8067.f, 5010.f), FVector(8543.f, 8404.f, 4990.f), FVector(8548.f, 8060.f, 5014.f) } },
		{ 16, 83, 49, 89, 51, 13, EKind::Model, TEXT("mdel_portal01_a_00_1"), { FVector(8761.f, 8080.f, 4885.f), FVector(8610.f, 8692.f, 4641.f), FVector(8827.f, 8113.f, 4793.f) } },
		{ 16, 145, 477, 147, 482, 17, EKind::Ring, TEXT("mdel_portaleff_a_00_1"), { FVector(14643.f, 8678.f, 47880.f), FVector(14752.f, 8722.f, 47871.f), FVector(14752.f, 8722.f, 47871.f) } },
		{ 16, 145, 477, 147, 482, 17, EKind::Model, TEXT("mdel_portal01_a_00_6"), { FVector(14637.f, 8849.f, 47985.f), FVector(14880.f, 9066.f, 48278.f), FVector(14955.f, 8716.f, 47982.f) } },
		{ 16, 464, 91, 469, 93, 18, EKind::Ring, TEXT("mdel_portaleff_a_00_1"), { FVector(46665.f, 8877.f, 9258.f), FVector(46647.f, 8943.f, 9388.f), FVector(46647.f, 8943.f, 9388.f) } },
		{ 16, 464, 91, 469, 93, 18, EKind::Model, TEXT("mdel_portal01_a_00_6"), { FVector(46716.f, 8699.f, 9202.f), FVector(47095.f, 9253.f, 9305.f), FVector(46945.f, 9218.f, 9483.f) } },
		{ 13, 34, 128, 36, 133, 7, EKind::Model, TEXT("mdel_portal01_a_00_6"), { FVector(3433.f, 4343.f, 13131.f), FVector(3109.f, 4559.f, 13190.f), FVector(3412.f, 4004.f, 13338.f) } },
		{ 13, 481, 316, 483, 321, 14, EKind::Model, TEXT("mdel_portal01_a_00_21"), { FVector(48372.f, 2944.f, 31961.f), FVector(48577.f, 3117.f, 31961.f), FVector(48431.f, 2899.f, 31705.f) } },
		{ 13, 147, 485, 152, 487, 16, EKind::Model, TEXT("mdel_portal01_a_00_26"), { FVector(15044.f, 3953.f, 48735.f), FVector(15103.f, 4169.f, 49058.f), FVector(14754.f, 3775.f, 48745.f) } },
		{ 7, 90, 29, 95, 31, 2, EKind::Model, TEXT("gate_004"), { FVector(9301.f, 2221.f, 2776.f), FVector(9394.f, 2233.f, 2750.f), FVector(9292.f, 2211.f, 2673.f) } },
		{ 7, 27, 353, 29, 358, 12, EKind::Model, TEXT("gate_004"), { FVector(2656.f, 1130.f, 35608.f), FVector(2630.f, 1500.f, 35484.f), FVector(2553.f, 1230.f, 35731.f) } },
		{ 7, 480, 134, 485, 136, 13, EKind::Model, TEXT("gate_004"), { FVector(48293.f, 1704.f, 13567.f), FVector(48318.f, 1730.f, 13807.f), FVector(48627.f, 1435.f, 13567.f) } },
		{ 7, 389, 24, 394, 26, 15, EKind::Model, TEXT("gate_004"), { FVector(39180.f, 1417.f, 1720.f), FVector(39180.f, 1555.f, 1720.f), FVector(39180.f, 1300.f, 1720.f) } },   // у ворот нет треугольников в 3 клетках от зоны — точки габаритов
		{ 18, 39, 75, 41, 80, 16, EKind::Model, TEXT("inferionportal"), { FVector(3920.f, 3818.f, 7899.f), FVector(3836.f, 3970.f, 7696.f), FVector(3799.f, 3898.f, 7673.f) } },
		{ 5, 193, 477, 198, 479, 3, EKind::Model, TEXT("mdel_steamgate_a_00_5"), { FVector(19834.f, 599.f, 47972.f), FVector(20058.f, 894.f, 48179.f), FVector(19271.f, 565.f, 47972.f) } },
		{ 12, 493, 67, 495, 85, 7, EKind::Model, TEXT("mdel_steamgate_a_00_5"), { FVector(49855.f, 1872.f, 8052.f), FVector(49882.f, 2373.f, 8052.f), FVector(49882.f, 1546.f, 8152.f) } },
		{ 4, 187, 471, 191, 473, 2, EKind::Model, TEXT("mdel_steamgate_a_00_17"), { FVector(19154.f, 1132.f, 47518.f), FVector(18591.f, 1132.f, 47518.f), FVector(18591.f, 1132.f, 47518.f) } },
		{ 1, 488, 349, 490, 353, 0, EKind::Ring, TEXT("mdel_portaleff_a_00"), { FVector(48900.f, 1163.f, 35167.f), FVector(48898.f, 1179.f, 35106.f), FVector(48892.f, 922.f, 35266.f) } },
		{ 1, 488, 349, 490, 353, 0, EKind::Model, TEXT("mdel_fortressdoor_a_00_1"), { FVector(49033.f, 1272.f, 35184.f), FVector(49221.f, 1778.f, 35298.f), FVector(49093.f, 1347.f, 35481.f) } },
		{ 1, 21, 343, 23, 347, 3, EKind::Model, TEXT("mdel_alpenportal01_a_00_1"), { FVector(1982.f, 1373.f, 34633.f), FVector(1917.f, 1498.f, 34633.f), FVector(1933.f, 1437.f, 34743.f) } },
		{ 1, 44, 394, 48, 398, 8, EKind::Model, TEXT("chess_gate"), { FVector(4677.f, 1828.f, 39576.f), FVector(4569.f, 2117.f, 39366.f), FVector(4727.f, 1819.f, 39963.f) } },
		{ 0, 41, 384, 43, 388, 1, EKind::Model, TEXT("mdel_alpenportal01_a_00_1"), { FVector(4066.f, 533.f, 38667.f), FVector(3867.f, 711.f, 38667.f), FVector(4017.f, 41.f, 38870.f) } },
		{ 0, 144, 25, 148, 27, 2, EKind::Model, TEXT("mdel_alpenportal01_a_00_1"), { FVector(14619.f, 533.f, 2285.f), FVector(14686.f, 658.f, 2219.f), FVector(14821.f, 355.f, 2236.f) } },
		{ 0, 366, 27, 370, 29, 9, EKind::Model, TEXT("chess_gate"), { FVector(36820.f, 1154.f, 2509.f), FVector(37024.f, 1306.f, 2425.f), FVector(37294.f, 60.f, 2790.f) } },
		{ 0, 441, 482, 445, 484, 6, EKind::Model, TEXT("mdel_alpenportal01_a_00_6"), { FVector(44303.f, 1605.f, 48353.f), FVector(44236.f, 1827.f, 48701.f), FVector(44587.f, 1161.f, 48335.f) } },
		{ 14, 21, 81, 23, 91, 13, EKind::Model, TEXT("mdel_steamgate01_a_00"), { FVector(2099.f, 3109.f, 8590.f), FVector(2176.f, 3156.f, 8464.f), FVector(2027.f, 2867.f, 8168.f) } },
		{ 6, 337, 35, 342, 37, 0, EKind::Model, TEXT("mdel_alpenportal01_a_00_1"), { FVector(34001.f, 3212.f, 3381.f), FVector(34068.f, 3337.f, 3315.f), FVector(34203.f, 2866.f, 3332.f) } },
		{ 6, 66, 273, 68, 277, 11, EKind::Model, TEXT("mdel_alpenportal01_a_00_1"), { FVector(6670.f, 989.f, 27553.f), FVector(6305.f, 1656.f, 27469.f), FVector(6437.f, 1611.f, 27402.f) } },
	};

	// Точка клика: точка подхода (game XZ), экранная точка, подпись.
	struct FClick { FVector2D Stand; FVector2D Screen; FString Label; TWeakObjectPtr<AActor> Glow; };   // Glow — свечение (экранная точка пересчитывается при клике)

	// Состояние прогона.
	struct FPcState
	{
		TSharedRef<FState> S = MakeShared<FState>();   // счётчики, свой персонаж, сеть, перехват 4006
		TArray<AsdaMapRegistry::FPortalEdge> Zones;     // зоны порталов карты
		int32 MapId = -1;                               // карта прогона
		int32 Clicks = 0;                               // кликов всего
		// Текущая зона: найденные клики по видам и флаги «вид показан».
		TArray<FClick> Pending;                         // клики зоны к выполнению
		bool bRingSeen = false, bModelSeen = false;     // кольцо / модель зоны попали под пик
		TArray<FVector2D> Cands;                        // точки подхода текущей зоны (считаются один раз на зону)
		bool bDrawnTried = false;                       // клики по нарисованным точкам модели без коллизии уже добавлены
		int32 DrawnInFrame = 0;                         // нарисованных точек модели в кадре (с любой точки подхода)
		int32 DrawnCi = 0;                              // точка подхода для нарисованных точек (перебор, пока точки не в кадре)
                        // точки подхода текущей зоны (считаются один раз на зону)
	};

	// Зона текущей карты, ближайшая к точке (game XZ) не дальше Reach; -1 — нет.
	static int32 NearestZone(const FPcState& St, const FVector2D& P, double Reach)
	{
		int32 Best = -1;
		double BestD = Reach;
		for (int32 i = 0; i < St.Zones.Num(); ++i)
		{
			const AsdaMapRegistry::FPortalEdge& E = St.Zones[i];
			const double Dx = FMath::Max3(E.X0 * 100.0 - P.X, 0.0, P.X - (E.X1 + 1) * 100.0);
			const double Dz = FMath::Max3(E.Z0 * 100.0 - P.Y, 0.0, P.Y - (E.Z1 + 1) * 100.0);
			const double D = FMath::Sqrt(Dx * Dx + Dz * Dz);
			if (D <= BestD) { BestD = D; Best = i; }
		}
		return Best;
	}

	static FZone ZoneOf(const AsdaMapRegistry::FPortalEdge& E) { return FZone{ E.X0, E.Z0, E.X1, E.Z1, E.To, FIntPoint(-1, -1) }; }

	// Что ожидается у зоны: кольцо / модель и их имена.
	static void Expected(const FPcState& St, const AsdaMapRegistry::FPortalEdge& E, bool& bRing, bool& bModel, FString& Names)
	{
		bRing = bModel = false;
		for (const FExpect& X : kExpect)
		{
			if (X.Map != St.MapId || X.X0 != E.X0 || X.Z0 != E.Z0 || X.X1 != E.X1 || X.Z1 != E.Z1) { continue; }
			(X.Kind == EKind::Ring ? bRing : bModel) = true;
			Names += FString::Printf(TEXT("%s«%s»"), Names.IsEmpty() ? TEXT("") : TEXT(", "), X.Name);
		}
	}

	// Точки подхода зоны: поворот взгляда камеры 0…180°, дальность 500…2600; клетка проходима, вне зоны, путь в зону есть.
	static TArray<FVector2D> Stands(AAsdaPlayerController* PC, const UAsdaWalkGrid* G, const AsdaMapRegistry::FPortalEdge& E)
	{
		const FZone Zn = ZoneOf(E);
		FVector Fwd = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : FVector(1, 0, 0);
		Fwd.Z = 0.f;
		Fwd = Fwd.GetSafeNormal();
		if (Fwd.IsNearlyZero()) { Fwd = FVector(1, 0, 0); }
		const FVector2D Cg((E.X0 + E.X1 + 1) * 50.0, (E.Z0 + E.Z1 + 1) * 50.0);
		const FVector CUe = AsdaCoord::GameToUe3D(FVector(Cg.X, 0.0, Cg.Y));
		TArray<FVector2D> Out;
		for (const float Ang : { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f })
		{
			const FVector Dir = Fwd.RotateAngleAxis(Ang, FVector::UpVector);
			for (const float D : { 900.f, 1300.f, 600.f, 1800.f, 2600.f })
			{
				const FIntPoint Tl = TileOf(AsdaCoord::UeToGame2D(CUe - Dir * D));
				if (!G->IsTileWalkable(Tl.X, Tl.Y) || InZone(Zn, Tl)) { continue; }
				TArray<FIntPoint> Pth; FAsdaPathStats Ps;
				if (!AsdaPath::FindPathTiles(G, Tl, FIntPoint((E.X0 + E.X1) / 2, (E.Z0 + E.Z1) / 2), Pth, &Ps) || Pth.Num() == 0 || !InZone(Zn, Pth.Last()))
				{
					continue;
				}
				const FVector2D Gp = AsdaCoord::TileCenterGame(Tl);
				if (!Out.Contains(Gp)) { Out.Add(Gp); }
			}
		}
		return Out;
	}

	// Поставить персонажа на точку подхода (сброс маршрута и хода).
	static void SnapTo(const TSharedRef<FPcState>& St, AAsdaPlayerController* PC, const FVector2D& Stand)
	{
		AAsdaPlayerCharacter* Me = St->S->Me.Get();
		PC->CancelRoute();
		Me->StopMove();
		Me->NetSnapHard(Stand);
		if (St->S->Net.IsValid()) { St->S->Net->ClearMoveRequestInFlight(TEXT("самотест кликов по порталам: точка подхода")); }
	}

	// Выбрать до трёх пикселей (верхний, средний, нижний) из облака.
	static void PickThree(const TArray<FVector2D>& Px, const FVector2D& Stand, const FString& What, TArray<FClick>& Out)
	{
		if (Px.Num() == 0) { return; }
		TArray<FVector2D> S = Px;
		S.Sort([](const FVector2D& A, const FVector2D& B) { return A.Y < B.Y; });
		const int32 Idx[3] = { 0, S.Num() / 2, S.Num() - 1 };
		const TCHAR* Nm[3] = { TEXT("верх"), TEXT("середина"), TEXT("низ") };
		for (int32 k = 0; k < 3; ++k)
		{
			if (k > 0 && Idx[k] == Idx[k - 1]) { continue; }
			Out.Add({ Stand, S[Idx[k]], FString::Printf(TEXT("%s, %s (%.0f,%.0f px)"), *What, Nm[k], S[Idx[k]].X, S[Idx[k]].Y) });
		}
	}

	static void ZoneStep(TWeakObjectPtr<UWorld> WW, TSharedRef<FPcState> St, int32 Zi, int32 Ci, TFunction<void()> Done);

	// Выполнить клики зоны Zi по одному (Ki — номер), затем следующая зона.
	static void ClickNext(TWeakObjectPtr<UWorld> WW, TSharedRef<FPcState> St, int32 Zi, int32 Ki, TFunction<void()> Done)
	{
		UWorld* W = WW.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		if (!W || !PC || !St->S->Me.IsValid() || !St->S->Net.IsValid()) { Done(); return; }
		if (Ki >= St->Pending.Num()) { ZoneStep(WW, St, Zi + 1, 0, Done); return; }
		const FClick C = St->Pending[Ki];
		SnapTo(St, PC, C.Stand);
		After(W, 1.0f, [WW, St, Zi, Ki, Done, C]()
		{
			UWorld* W2 = WW.Get();
			AAsdaPlayerController* PC2 = W2 ? Cast<AAsdaPlayerController>(W2->GetFirstPlayerController()) : nullptr;
			UAsdaNetSubsystem* Net2 = St->S->Net.Get();
			if (!W2 || !PC2 || !Net2) { Done(); return; }
			// Пиксель кольца/модели перепроверяется: камера могла осесть иначе, чем при прочёсывании — ищем рядом (±2 шага) пиксель,
			//   где луч всё ещё попадает в меш набора портала этой зоны; нет — клик не по графике (инфо, не зачёт).
			FVector2D Scr = C.Screen;
			if (C.Glow.IsValid())
			{
				// Свечение: экранная точка — проекция актора сейчас (камера после постановки могла осесть иначе).
				FVector2D Gs;
				if (PC2->ProjectWorldLocationToScreen(C.Glow->GetActorLocation(), Gs)) { Scr = Gs; }
			}
			const bool bGfxClick = C.Label.StartsWith(TEXT("кольцо")) || C.Label.StartsWith(TEXT("модель,"));
			if (bGfxClick)
			{
				bool bOk = false;
				for (int32 R = 0; R <= 2 && !bOk; ++R)
				{
					for (int32 Dy = -R; Dy <= R && !bOk; ++Dy)
					{
						for (int32 Dx = -R; Dx <= R && !bOk; ++Dx)
						{
							if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != R) { continue; }
							const FVector2D Q = C.Screen + FVector2D(Dx, Dy) * (kScanStep / 2);
							const AAsdaPlayerController::FAsdaPick Pk = FAsdaClosedTilesClickAccess::Pick(PC2, Q);
							const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(Pk.Component.Get());
							if (Sm && Sm->GetStaticMesh() && AsdaPortalGfx::IsPortalMesh(St->MapId, Sm->GetStaticMesh()->GetName())
								&& NearestZone(*St, AsdaCoord::UeToGame2D(Pk.Point), 800.0) == Zi)
							{
								Scr = Q; bOk = true;
							}
						}
					}
				}
				if (!bOk)
				{
					UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо %s: при клике луч уже не попадает в графику (камера осела иначе) — пропуск"), kPrefixPc, *C.Label);
					ClickNext(WW, St, Zi, Ki + 1, Done);
					return;
				}
			}
			St->S->Sent4006Dst.Reset();
			const bool bWasInGame = Net2->bInGameConnected;
			Net2->bInGameConnected = true;   // 4006 уходит только «в игре»; пакет забирает перехват, в сеть ничего
			FAsdaClosedTilesClickAccess::Click(PC2, Scr);
			Net2->bInGameConnected = bWasInGame;
			FVector2D Final(-1.f, -1.f);
			const bool bRoute = FAsdaClosedTilesClickAccess::RouteFinal(PC2, Final);
			After(W2, 0.5f, [WW, St, Zi, Ki, Done, C, Final, bRoute]()
			{
				const FZone Zn = ZoneOf(St->Zones[Zi]);
				const bool bSent = St->S->Sent4006Dst.Num() > 0;
				const FVector2D Dst = bRoute ? Final : (bSent ? St->S->Sent4006Dst[0] : FVector2D(-1.f, -1.f));
				const FIntPoint DT = TileOf(Dst);
				const bool bToZone = bSent && (InZone(Zn, DT) || SegmentHitsZone(Zn, C.Stand, Dst));
				++St->Clicks;
				St->S->Expect(bToZone, FString::Printf(TEXT("зона → %d %d..%d×%d..%d, %s, подход (%.0f,%.0f): 4006 %s, цель маршрута (%d,%d), в зону = %d"),
					Zn.To, Zn.X0, Zn.X1, Zn.Z0, Zn.Z1, *C.Label, C.Stand.X, C.Stand.Y, bSent ? TEXT("ушёл") : TEXT("НЕ ушёл"), DT.X, DT.Y, bToZone ? 1 : 0));
				if (AAsdaPlayerController* PC3 = WW.IsValid() ? Cast<AAsdaPlayerController>(WW->GetFirstPlayerController()) : nullptr) { PC3->CancelRoute(); }
				if (AAsdaPlayerCharacter* Me3 = St->S->Me.Get()) { Me3->StopMove(); }
				ClickNext(WW, St, Zi, Ki + 1, Done);
			});
		});
	}

	// Зона Zi, точка подхода Ci: прочесать экран, собрать клики; кольцо/модель ещё не показаны — следующая точка подхода.
	static void ZoneStep(TWeakObjectPtr<UWorld> WW, TSharedRef<FPcState> St, int32 Zi, int32 Ci, TFunction<void()> Done)
	{
		UWorld* W = WW.Get();
		AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
		AAsdaPlayerCharacter* Me = St->S->Me.Get();
		if (!W || !PC || !Me || !Me->WalkGrid) { Done(); return; }
		if (Zi >= St->Zones.Num()) { Done(); return; }
		const AsdaMapRegistry::FPortalEdge& E = St->Zones[Zi];
		if (Ci == 0) { St->Pending.Reset(); St->bRingSeen = St->bModelSeen = false; St->bDrawnTried = false; St->DrawnCi = 0; St->DrawnInFrame = 0; St->Cands = Stands(PC, Me->WalkGrid, E); }
		bool bWantRing = false, bWantModel = false;
		FString Names;
		Expected(*St, E, bWantRing, bWantModel, Names);
		const TArray<FVector2D>& Cands = St->Cands;
		const bool bNeedMore = (bWantRing && !St->bRingSeen) || (bWantModel && !St->bModelSeen);
		if (Ci >= Cands.Num() && bWantModel && !St->bModelSeen && !St->bDrawnTried && Cands.Num() > 0)
		{
			// Модель ни с одной точки подхода не попала под луч пика (меш без коллизии): кликаем по её нарисованным точкам, которые
			//   ничем не заслонены (луч доходит до точки или дальше), — так их видит игрок.
			const FVector2D Stand0 = Cands[St->DrawnCi];
			St->bDrawnTried = ++St->DrawnCi >= Cands.Num();   // последняя точка подхода — больше не пробовать
			SnapTo(St, PC, Stand0);
			After(W, 1.0f, [WW, St, Zi, Ci, Done, Stand0]()
			{
				UWorld* W2 = WW.Get();
				AAsdaPlayerController* PC2 = W2 ? Cast<AAsdaPlayerController>(W2->GetFirstPlayerController()) : nullptr;
				if (!W2 || !PC2) { Done(); return; }
				const AsdaMapRegistry::FPortalEdge& E2 = St->Zones[Zi];
				FVector CamLoc; FRotator CamRot; PC2->GetPlayerViewPoint(CamLoc, CamRot);
				int32 VX = 0, VY = 0;
				PC2->GetViewportSize(VX, VY);
				for (const FExpect& X : kExpect)
				{
					if (X.Map != St->MapId || X.Kind != EKind::Model || X.X0 != E2.X0 || X.Z0 != E2.Z0 || X.X1 != E2.X1 || X.Z1 != E2.Z1) { continue; }
					for (int32 k = 0; k < 3; ++k)
					{
						const FVector Ue = AsdaCoord::GameToUe3D(X.P[k]);
						FVector2D Scr;
						if (!PC2->ProjectWorldLocationToScreen(Ue, Scr) || Scr.X < 4 || Scr.Y < 4 || Scr.X > VX - 4 || Scr.Y > VY - 4)
						{
							UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо нарисованная точка %d «%s»: вне кадра"), kPrefixPc, k + 1, X.Name);
							continue;
						}
						++St->DrawnInFrame;
						const AAsdaPlayerController::FAsdaPick Pk = FAsdaClosedTilesClickAccess::Pick(PC2, Scr);
						if (Pk.Actor.IsValid() && FVector::Dist(CamLoc, Pk.Point) < FVector::Dist(CamLoc, Ue) - 50.0)
						{
							const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(Pk.Component.Get());
							UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо нарисованная точка %d «%s»: заслонена «%s» на %.0f ед. ближе"), kPrefixPc, k + 1, X.Name,
								Sm && Sm->GetStaticMesh() ? *Sm->GetStaticMesh()->GetName() : *GetNameSafe(Pk.Actor.Get()), FVector::Dist(CamLoc, Ue) - FVector::Dist(CamLoc, Pk.Point));
							continue;
						}
						St->bModelSeen = true;
						St->Pending.Add({ Stand0, Scr, FString::Printf(TEXT("модель без коллизии «%s», нарисованная точка %d"), X.Name, k + 1) });
					}
				}
				if (St->bModelSeen) { St->bDrawnTried = true; }
				UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s зона → %d: модель не попадает под луч пика ни с одного подхода — клик по нарисованным точкам с подхода (%.0f,%.0f): %d"),
					kPrefixPc, E2.To, Stand0.X, Stand0.Y, St->Pending.Num());
				ZoneStep(WW, St, Zi, Ci, Done);
			});
			return;
		}
		if (Ci >= Cands.Num() || (Ci > 0 && !bNeedMore))
		{
			// Итог зоны: показанные виды, затем клики.
			if (bWantRing) { St->S->Expect(St->bRingSeen, FString::Printf(TEXT("зона → %d %d..%d×%d..%d: кольцо (%s) видно с точки подхода (подходов %d)"), E.To, E.X0, E.X1, E.Z0, E.Z1, *Names, Cands.Num())); }
			if (bWantModel && !St->bModelSeen && St->bDrawnTried && St->DrawnInFrame == 0)
			{
				// Модель у зоны целиком над кадром (арка над зоной, ворота над обрывом): с подхода игрок её не видит и не кликает —
				//   клик в этом месте идёт по земле/столбу зоны (проверено кликами по земле и свечению). Не зачёт и не провал.
				UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо зона → %d %d..%d×%d..%d: модель (%s) ни с одной из %d точек подхода не попадает в кадр (над камерой) — кликнуть её нельзя"),
					kPrefixPc, E.To, E.X0, E.X1, E.Z0, E.Z1, *Names, Cands.Num());
				bWantModel = false;
			}
			if (bWantModel) { St->S->Expect(St->bModelSeen, FString::Printf(TEXT("зона → %d %d..%d×%d..%d: модель (%s) видна с точки подхода (подходов %d)"), E.To, E.X0, E.X1, E.Z0, E.Z1, *Names, Cands.Num())); }
			if (St->Pending.Num() == 0)
			{
				UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s инфо зона → %d %d..%d×%d..%d: графики портала и свечения у зоны нет (портал без модели в оригинале)"), kPrefixPc, E.To, E.X0, E.X1, E.Z0, E.Z1);
			}
			ClickNext(WW, St, Zi, 0, Done);
			return;
		}
		const FVector2D Stand = Cands[Ci];
		SnapTo(St, PC, Stand);
		After(W, 1.0f, [WW, St, Zi, Ci, Done, Stand]()
		{
			UWorld* W2 = WW.Get();
			AAsdaPlayerController* PC2 = W2 ? Cast<AAsdaPlayerController>(W2->GetFirstPlayerController()) : nullptr;
			AAsdaPlayerCharacter* Me2 = St->S->Me.Get();
			if (!W2 || !PC2 || !Me2) { Done(); return; }
			const AsdaMapRegistry::FPortalEdge& E2 = St->Zones[Zi];
			int32 VX = 0, VY = 0;
			PC2->GetViewportSize(VX, VY);
			TArray<FVector2D> RingPx, ModelPx;
			for (int32 Y = kScanStep; Y < VY - kScanStep; Y += kScanStep)
			{
				for (int32 X = kScanStep; X < VX - kScanStep; X += kScanStep)
				{
					const AAsdaPlayerController::FAsdaPick Pk = FAsdaClosedTilesClickAccess::Pick(PC2, FVector2D(X, Y));
					if (Pk.Actor.Get() == Me2) { continue; }
					const UStaticMeshComponent* Sm = Cast<UStaticMeshComponent>(Pk.Component.Get());
					if (!Sm || !Sm->GetStaticMesh() || !AsdaPortalGfx::IsPortalMesh(St->MapId, Sm->GetStaticMesh()->GetName())) { continue; }
					if (NearestZone(*St, AsdaCoord::UeToGame2D(Pk.Point), 800.0) != Zi) { continue; }
					(Sm->GetStaticMesh()->GetName().Contains(TEXT("portaleff")) ? RingPx : ModelPx).Add(FVector2D(X, Y));
				}
			}
			const FString Where = FString::Printf(TEXT("подход %d"), Ci);
			if (RingPx.Num() && !St->bRingSeen) { St->bRingSeen = true; PickThree(RingPx, Stand, TEXT("кольцо"), St->Pending); }
			if (ModelPx.Num() && !St->bModelSeen) { St->bModelSeen = true; PickThree(ModelPx, Stand, TEXT("модель"), St->Pending); }
			// Свечение: эффект-актор в зоне или в клетке от неё, в кадре с этой точки подхода (первый раз).
			if (Ci == 0 || !St->Pending.ContainsByPredicate([](const FClick& C) { return C.Label.StartsWith(TEXT("свечение")); }))
			{
				for (TActorIterator<AAsdaEffectActor> It(W2); It; ++It)
				{
					const FVector2D P = AsdaCoord::UeToGame2D(It->GetActorLocation());
					const double Dx = FMath::Max3(E2.X0 * 100.0 - P.X, 0.0, P.X - (E2.X1 + 1) * 100.0);
					const double Dz = FMath::Max3(E2.Z0 * 100.0 - P.Y, 0.0, P.Y - (E2.Z1 + 1) * 100.0);
					if (FMath::Sqrt(Dx * Dx + Dz * Dz) > kGlowReach) { continue; }
					FVector2D Scr;
					if (!PC2->ProjectWorldLocationToScreen(It->GetActorLocation(), Scr) || Scr.X < 4 || Scr.Y < 4 || Scr.X > VX - 4 || Scr.Y > VY - 4) { continue; }
					const AAsdaPlayerController::FAsdaPick Pk = FAsdaClosedTilesClickAccess::Pick(PC2, Scr);
					if (Pk.Actor.Get() == Me2) { continue; }
					// Свечение заслонено (холм, стена ближе него; не земля под самим свечением) — с этой точки подхода игрок его не видит.
					FVector GCam; FRotator GRot; PC2->GetPlayerViewPoint(GCam, GRot);
					if (Pk.Actor.IsValid() && FVector::Dist(GCam, Pk.Point) < FVector::Dist(GCam, It->GetActorLocation()) - 150.0
						&& FVector::Dist(Pk.Point, It->GetActorLocation()) > 300.0) { continue; }   // луч встал далеко от свечения (не у самого свечения на земле)
					St->Pending.Add({ Stand, Scr, FString::Printf(TEXT("свечение «%s»"), *It->GetName()), *It });
					break;
				}
			}
			UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s зона → %d, %s (%.0f,%.0f): пикселей кольца %d, модели %d, кликов набрано %d"),
				kPrefixPc, E2.To, *Where, Stand.X, Stand.Y, RingPx.Num(), ModelPx.Num(), St->Pending.Num());
			ZoneStep(WW, St, Zi, Ci + 1, Done);
		});
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmdPc, A.bQuit, TEXT("нет мира")); return; }
		int32 MapId = -1;
		for (const FString& Ar : Args) { if (Ar.StartsWith(TEXT("map="))) { MapId = FCString::Atoi(*Ar.Mid(4)); } }
		if (MapId < 0) { AsdaSelfTestKit::Refused(W, kCmdPc, A.bQuit, TEXT("нужен map=N")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmdPc, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmdPc, A.bQuit);
		TSharedRef<FPcState> St = MakeShared<FPcState>();
		St->MapId = MapId;
		for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph) { if (E.From == MapId) { St->Zones.Add(E); } }
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		const FIntPoint Spawn = St->Zones.Num() ? FIntPoint((St->Zones[0].X0 + St->Zones[0].X1) / 2, (St->Zones[0].Z0 + St->Zones[0].Z1) / 2) : FIntPoint(256, 256);

		After(W, A.Delay, [WW, Gate, MapId, Spawn]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.Load.CurrentMap %d"), MapId));
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), Spawn.X, Spawn.Y));
		});

		auto Finish = [WW, St, bQuit]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			const TSharedRef<FState>& S = St->S;
			if (AAsdaPlayerCharacter* Me = S->Me.Get()) { Me->StopMove(); }
			if (S->Net.IsValid() && S->CapGen) { AsdaSelfTestKit::DisarmCapture(S->Net.Get(), S->CapGen); S->CapGen = 0; }
			AsdaCVarSave::RestoreAll(kCmdPc);
			AsdaPath::InvalidateCostGrid();
			FAsdaClosedTilesClickAccess::RestoreUi(Wd);
			AsdaSelfTestKit::Finish(Wd, kCmdPc, bQuit, LogAsdaClosedTilesTest, kPrefixPc, S->Ok, S->Fail,
				*FString::Printf(TEXT(" (карта %d, зон %d, кликов %d)"), St->MapId, St->Zones.Num(), St->Clicks));
		};

		After(W, A.Delay + 3.f, [WW, Gate, St, bQuit, Finish]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			const TSharedRef<FState>& S = St->S;
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			if (!Me || !Me->WalkGrid) { AsdaSelfTestKit::Refused(Wd, kCmdPc, bQuit, TEXT("нет своего персонажа / сетки карты")); return; }
			S->Me = Me;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Net) { AsdaSelfTestKit::Refused(Wd, kCmdPc, bQuit, TEXT("нет сети")); return; }
			S->Net = Net;
			S->CapGen = AsdaSelfTestKit::ArmCapture(Net, [S](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
			{
				if (Op == 4006 && P && Len >= 16 + 21) { S->Sent4006Dst.Add(FVector2D(AsdaBytes::RdF32(P + 16 + 13), AsdaBytes::RdF32(P + 16 + 17))); }
			}, 0, kCmdPc);
			if (S->CapGen == 0) { AsdaSelfTestKit::Refused(Wd, kCmdPc, bQuit, TEXT("перехват отправок занят")); return; }
			AsdaCVarSave::ArmWorldGuard(Wd, kCmdPc);
			AsdaCVarSave::SetForTest(kKey, 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.ClickRayTerrain"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.ClickPortalZone"), 1);
			AsdaCVarSave::SetForTest(TEXT("asda.Move.ClickPortalGfx"), 1);
			UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("%s карта %d: зон порталов %d"), kPrefixPc, St->MapId, St->Zones.Num());
			ZoneStep(WW, St, 0, 0, Finish);
		});
	}
}

// ★★ САМОТЕСТ «ПОРТАЛ В ЦЕНТРЕ ФЛАБИСА» (08.11.2026, пачка 19 №8: «портал в центре Флабиса (x270 y264) не переносит»).
// Запуск: `asda.SelfTest.FlabisCenter [delay=N] [quit]` — игра на уровне flabis, офлайн.
// ЧТО ПРОВЕРЯЕТ (путь игрока):
//   F1  в точке (270,264) стоит эффект-портал `eff_mdel_move_a_00` (актор уровня из `.mapraw` оригинала);
//   F2  серверной зоны портала рядом нет: ни одна зона `kPortalGraph` карты 13 (= живые и стоковые `Flabis_PortalConfig.Ini`,
//       3 зоны у краёв карты) не ближе 6 клеток — переход здесь не предусмотрен данными оригинала;
//   F3  клетка — открытая вода (плавание с 28.10), путь туда есть; настоящий ход `NetMove` (270,258) → (270,264) доводит персонажа
//       до круга и уходит 4006, перехода нет (флаги перехода не подняты).
//   Кадры: `asda.UI.Shot flabis_center_far_<язык>` (с подхода) и `flabis_center_on_<язык>` (на круге).
namespace AsdaFlabisCenterTest
{
	using namespace AsdaClosedTilesTest;
	static const TCHAR* const kCmdFc = TEXT("asda.SelfTest.FlabisCenter");   // имя консольной команды
	static const FIntPoint kCircle(270, 264);                                   // клетка эффекта-портала
	static const FIntPoint kFrom(270, 258);                                     // клетка подхода (вода)

	// Кадр вьюпорта с UI в Reports/batch19/flabis_portal/<имя>_<язык>.png (как AsdaDeathHpSelfTest).
	static void Shot(const TCHAR* Name, const FString& Lang)
	{
		const FString Path = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()), TEXT("../Reports/batch19/flabis_portal"),
			FString::Printf(TEXT("%s_%s.png"), Name, *Lang));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/true, /*bAddUniqueSuffix=*/false);
		UE_LOG(LogAsdaClosedTilesTest, Display, TEXT("САМОТЕСТ-ЦЕНТР-ФЛАБИСА кадр %s"), *Path);
	}

	static void Run(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmdFc, A.bQuit, TEXT("нет мира")); return; }
		if (AsdaSelfTestKit::RefuseStart(W, kCmdFc, A)) { return; }
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(kCmdFc, A.bQuit);
		TSharedRef<FState> S = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WW(W);
		const bool bQuit = A.bQuit;
		const FString Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		After(W, A.Delay, [WW, Gate]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd) || !GEngine) { return; }
			GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 13"));
			GEngine->Exec(Wd, *FString::Printf(TEXT("Asda.World.FakeSelfSpawn %d %d"), kFrom.X, kFrom.Y));
		});
		auto Finish = [WW, S, bQuit]()
		{
			if (UWorld* Wd = WW.Get())
			{
				FAsdaClosedTilesClickAccess::RestoreUi(Wd);
				AsdaSelfTestKit::Finish(Wd, kCmdFc, bQuit, LogAsdaClosedTilesTest, TEXT("САМОТЕСТ-ЦЕНТР-ФЛАБИСА"), S->Ok, S->Fail, TEXT(""));
			}
		};
		After(W, A.Delay + 6.f, [WW, Gate, S, Finish, Lang]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd || !Gate->Pass(Wd)) { return; }
			AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(Wd->GetFirstPlayerController());
			AAsdaPlayerCharacter* Me = PC ? Cast<AAsdaPlayerCharacter>(PC->GetPawn()) : nullptr;
			UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(Wd);
			if (!Me || !Me->WalkGrid || !Net) { Finish(); return; }
			S->Me = Me; S->Net = Net;
			// F1: эффект-портал в точке.
			FString FxName;
			for (TActorIterator<AAsdaEffectActor> It(Wd); It; ++It)
			{
				if (TileOf(AsdaCoord::UeToGame2D(It->GetActorLocation())) == kCircle) { FxName = It->EffectName; }
			}
			S->Expect(FxName.Contains(TEXT("move")), FString::Printf(TEXT("F1 в клетке (%d,%d) эффект-портал: «%s»"), kCircle.X, kCircle.Y, *FxName));
			// F2: серверных зон рядом нет.
			double Best = 1e9; int32 BestTo = -1;
			for (const AsdaMapRegistry::FPortalEdge& E : AsdaMapRegistry::kPortalGraph)
			{
				if (E.From != 13) { continue; }
				const double Dx = FMath::Max3(double(E.X0 - kCircle.X), 0.0, double(kCircle.X - E.X1));
				const double Dz = FMath::Max3(double(E.Z0 - kCircle.Y), 0.0, double(kCircle.Y - E.Z1));
				const double D = FMath::Sqrt(Dx * Dx + Dz * Dz);
				if (D < Best) { Best = D; BestTo = E.To; }
			}
			S->Expect(Best > 6.0, FString::Printf(TEXT("F2 ближайшая серверная зона Флабиса (→ %d) в %.0f клетках — у круга зоны нет (данные оригинала)"), BestTo, Best));
			// F3: путь и настоящий ход.
			TArray<FIntPoint> P; FAsdaPathStats St;
			const bool bPath = AsdaPath::FindPathTiles(Me->WalkGrid, kFrom, kCircle, P, &St) && St.bReachedGoal;
			S->Expect(Me->WalkGrid->IsTileWalkable(kCircle.X, kCircle.Y) && bPath, FString::Printf(TEXT("F3 клетка круга проходима (вода), путь с (%d,%d) доходит=%d"), kFrom.X, kFrom.Y, bPath ? 1 : 0));
			Shot(TEXT("flabis_center_far"), Lang);
			After(Wd, 1.5f, [WW, S, Finish, Lang]()
			{
				UWorld* W2 = WW.Get(); AAsdaPlayerCharacter* Me2 = S->Me.Get();
				if (!W2 || !Me2) { Finish(); return; }
				Me2->StopMove();
				Me2->NetSnapHard(TileGame(kFrom));
				Me2->NetMove(TileGame(kFrom), TileGame(kCircle), kSpeed);
				After(W2, WalkTime(kFrom, kCircle) + 1.f, [WW, S, Finish, Lang]()
				{
					UWorld* W3 = WW.Get(); AAsdaPlayerCharacter* Me3 = S->Me.Get(); UAsdaNetSubsystem* Net3 = S->Net.Get();
					if (!W3 || !Me3 || !Net3) { Finish(); return; }
					const FIntPoint T = TileOf(Me3->GetGamePosXZ());
					S->Expect(T == kCircle, FString::Printf(TEXT("F3 ход довёл до круга: клетка (%d,%d), плавание=%d"), T.X, T.Y, Me3->IsSwimming() ? 1 : 0));
					S->Expect(!Net3->bPortalPending && !Net3->bTransferInProgress, TEXT("F3 на круге перехода нет (флаги перехода не подняты; сервер зоны тоже не держит)"));
					Shot(TEXT("flabis_center_on"), Lang);
					After(W3, 2.f, Finish);
				});
			});
		});
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaFlabisCenterSelfTest(
	TEXT("asda.SelfTest.FlabisCenter"),
	TEXT("Самотест «портал в центре Флабиса»: эффект-портал (270,264), серверной зоны нет, настоящий ход по воде до круга, кадры. "
	     "`delay=N` `quit`; игра — на уровне flabis. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaFlabisCenterTest::Run(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaPortalClicksSelfTest(
	TEXT("asda.SelfTest.PortalClicks"),
	TEXT("Самотест кликов по графике порталов карты: видимые пиксели кольца и модели портала (прочёсывание экрана), свечение → ход в зону "
	     "портала. `map=N` (обязателен; игра — на уровне этой карты) `delay=N` `quit`. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaPortalClicksTest::Run(A, W);
	}));

static FAutoConsoleCommandWithWorldAndArgs GAsdaClosedTilesSelfTest(
	TEXT("asda.SelfTest.ClosedTiles"),
	TEXT("Самотест закрытых тайлов (портал Fireway Forest → Flabis): карман домика-портала и паритет с .sim сервера в "
	     "DA_WalkGrid.ClosedBits, ключ asda.Walk.ClosedTiles, автопуть и ход в домик встают в зоне портала, регресс трёх порталов "
	     "карты 16. `delay=N` `quit`; игра — на уровне firewayforest. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaClosedTilesTest::Run(A, W);
	}));

#endif // !UE_BUILD_SHIPPING
