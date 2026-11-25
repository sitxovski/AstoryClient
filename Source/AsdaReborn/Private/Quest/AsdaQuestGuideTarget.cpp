// Copyright (c) 2026 @sitxovski (Discord)
// ★ [ИСПРАВЛЕНИЕ] 31.10.2026 (баг 6): место цели строки гида без координат — разбор в шапке `AsdaQuestGuideTarget.h`.
#include "Quest/AsdaQuestGuideTarget.h"
#include "Quest/AsdaQuestTypes.h"
#include "Npc/AsdaNpcDialogTypes.h"   // FAsdaNpcSnqRow / FAsdaSnqIctRow — ячейки «ПРИНИМАЕТ» NPC и их условия
#include "Quest/AsdaQuestMarkerSubsystem.h"   // EvalSnqIctCond / IsQuestReadyToHandIn — вычислитель облачка сдачи
#include "AsdaDataTableCache.h"
#include "AsdaDevConsole.h"
#include "World/AsdaCoord.h"
#include "Engine/DataTable.h"

// 1 — строка гида без координат ведёт к месту цели (зона спавна моба / NPC), как значки карты мира; 0 — не ведёт никуда.
static ASDA_DEV_CVAR(int32, CVarAsdaFollowNoCoordTarget,
	TEXT("asda.Quest.FollowNoCoordTarget"), 1,
	TEXT("1 = кнопка маршрута задания без координат в гиде ведёт к месту цели (спавн моба / NPC); 0 = не ведёт никуда."),
	ECVF_Default);

// 1 — у выполненной цели шага кнопки маршрута ведут к NPC сдачи (имя, карта, клетка); 0 — как в оригинале, строки гида шага.
static ASDA_DEV_CVAR(int32, CVarAsdaFollowDoneToNpc,
	TEXT("asda.Quest.FollowDoneToNpc"), 1,
	TEXT("1 = цель шага выполнена: кнопки маршрута журнала показывают NPC сдачи и ведут к нему; 0 = как в оригинале (к целям шага)."),
	ECVF_Default);

// ★ 31.10.2026 (патч NpcServer N22): 1 — облачко и «!» NPC перебирают ВСЕ его ячейки (слоты ≥ 10 — таблица расширения);
//   0 — как было, не больше 10 ячеек и 10 строк в блоке облачка.
static ASDA_DEV_CVAR(int32, CVarAsdaNpcCellsBeyond10,
	TEXT("asda.Npc.CellsBeyond10"), 1,
	TEXT("1 = облачко сдачи/выдачи и «!» над NPC учитывают ячейки NPC сверх 10 (таблица расширения QuestNpcTableExt, патч сервера N22), блок облачка ≤ 10 строк с прокруткой; 0 = прежний предел 10."),
	ECVF_Default);

namespace
{
	// Таблицы — те же пути, что у журнала и карты мира (одна копия данных на сессию через `AsdaDt::Cached`).
	const TCHAR* kAreaDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTargetArea.DT_QuestTargetArea");
	const TCHAR* kNpcDT  = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");
	const TCHAR* kSnqDT   = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq");
	const TCHAR* kSnqIctDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct");
	const TCHAR* kTurnInDT = TEXT("/Game/AsdaAssets/Data/Quest/DT_QuestTurnIn.DT_QuestTurnIn");

	// Зоны спавна шага задания по фильтру: карта (−1 = любая) и тип моба (−1 = любой засчитываемый за шаг).
	void AreaPlaces(int32 QuestNo, int32 Stage, int32 MapId, int32 MobType, TArray<AsdaQuestGuideTarget::FPlace>& Out)
	{
		const UDataTable* Area = AsdaDt::Cached(kAreaDT);
		if (!Area) { return; }
		for (const TPair<FName, uint8*>& It : Area->GetRowMap())
		{
			const FAsdaQuestTargetAreaRow* A = reinterpret_cast<const FAsdaQuestTargetAreaRow*>(It.Value);
			if (!A || A->QuestNo != QuestNo || A->Stage != Stage) { continue; }
			if (MapId >= 0 && A->MapId != MapId) { continue; }
			if (MobType >= 0 && A->MobType != MobType) { continue; }
			AsdaQuestGuideTarget::FPlace P;
			P.MapId = A->MapId; P.TileX = A->TileX; P.TileY = A->TileY; P.Weight = A->SpawnCount;
			Out.Add(P);
		}
	}

}

void AsdaQuestGuideTarget::NpcPlaces(int32 NpcId, int32 MapId, TArray<FPlace>& Out)
{
	const UDataTable* Npc = AsdaDt::Cached(kNpcDT);
	if (!Npc) { return; }
	for (const TPair<FName, uint8*>& It : Npc->GetRowMap())
	{
		const FAsdaNpcQuestNpcRow* N = reinterpret_cast<const FAsdaNpcQuestNpcRow*>(It.Value);
		if (!N || N->NpcId != NpcId || N->MapId < 0) { continue; }
		if (MapId >= 0 && N->MapId != MapId) { continue; }
		FPlace P;
		P.MapId = N->MapId;
		// Клетка — как у меток карты (`WorldX / kTileUnits`, вниз).
		P.TileX = FMath::FloorToInt(N->WorldX / AsdaCoord::kTileUnits);
		P.TileY = FMath::FloorToInt(N->WorldY / AsdaCoord::kTileUnits);
		P.Weight = 1;
		Out.Add(P);
	}
}

void AsdaQuestGuideTarget::TurnInNpcs(const UAsdaQuestMarkerSubsystem* QM, int32 QuestNo, int32 Stage, bool bByStep, TArray<FTurnIn>& Out)
{
	Out.Reset();
	if (QuestNo < 0) { return; }
	TSet<int32> Seen;
	if (!bByStep)
	{
		// Прежний отбор значка «NPC сдачи» карты мира (ключ `asda.Quest.FollowDoneToNpc 0`): любая ячейка «ПРИНИМАЕТ» задания
		//   клиентского `DT_NpcSnq` с ключом > 0, место из `npc_list`.
		const UDataTable* Snq = AsdaDt::Cached(kSnqDT);
		if (!Snq) { return; }
		for (const TPair<FName, uint8*>& It : Snq->GetRowMap())
		{
			const FAsdaNpcSnqRow* R = reinterpret_cast<const FAsdaNpcSnqRow*>(It.Value);
			if (!R || Seen.Contains(R->NpcId)) { continue; }
			const bool bTakes = R->Take.ContainsByPredicate([QuestNo](const FAsdaSnqEntry& E)
			{
				return E.QuestId == QuestNo && E.Keys.ContainsByPredicate([](int32 K) { return K > 0; });
			});
			if (!bTakes) { continue; }
			Seen.Add(R->NpcId);
			TArray<FPlace> Places;
			NpcPlaces(R->NpcId, -1, Places);
			for (const FPlace& P : Places) { FTurnIn T; T.NpcId = R->NpcId; T.Place = P; Out.Add(T); }
		}
		return;
	}
	// По шагу — правило СЕРВЕРА (NpcServer `Process_Quest@0x420C10`, таблица `DT_QuestTurnIn`): ячейка принимает, только если
	//   (задание, стадия слота) == (ячейка +0, ячейка +66), и проходит ХОТЯ БЫ ОДНО из её условий. Условие считает клиентский
	//   вычислитель облачка сдачи (`EvalSnqIctCond`, те же ключи `SNQICT` = `ConditionTable`, проверено на всех 470 ячейках).
	//   Ячейки «выдать предмет» (действие 0, условие «предмета НЕТ») при собранных предметах сами не проходят — остаётся тот,
	//   кто реально примет сдачу. Место — серверный спавн NPC (`NpcInfo/<карта>.npc`), как его ставит сервер.
	const UDataTable* Tbl = AsdaDt::Cached(kTurnInDT);
	if (!Tbl || !QM) { return; }
	for (const TPair<FName, uint8*>& It : Tbl->GetRowMap())
	{
		const FAsdaQuestTurnInRow* R = reinterpret_cast<const FAsdaQuestTurnInRow*>(It.Value);
		if (!R || R->QuestNo != QuestNo || R->Stage != Stage || R->MapId < 0 || Seen.Contains(R->NpcId)) { continue; }
		const bool bPass = R->Keys.ContainsByPredicate([QM](int32 K)
		{
			if (K <= 0) { return false; }
			const FAsdaSnqIctRow* C = AsdaDt::RowByInt<FAsdaSnqIctRow>(kSnqIctDT, K, TEXT("QuestGuideTarget"));
			return C && C->Key >= 0 && QM->EvalSnqIctCond(*C);
		});
		if (!bPass) { continue; }
		Seen.Add(R->NpcId);
		FTurnIn T;
		T.NpcId = R->NpcId;
		T.Action = R->Action;
		T.Place.MapId = R->MapId; T.Place.TileX = R->TileX; T.Place.TileY = R->TileY; T.Place.Weight = 1;
		Out.Add(T);
	}
}

bool AsdaQuestGuideTarget::PickTurnIn(const TArray<FTurnIn>& Npcs, int32 CurMapId, const FIntPoint& PlayerTile, FTurnIn& Out)
{
	// Сервер принимает у любого из кандидатов (104/2, 112/0 — две точки сдачи с одним условием «стадия»; посредники 20/0, 28/0 при выполненной цели не проходят: их QCT — «предметов нет»): ведём к ближайшему.
	//   На своей карте — ближайший к персонажу; иначе — первый по таблице (у законных пар обе точки на одной карте).
	const FTurnIn* Best = nullptr;
	for (const FTurnIn& T : Npcs)
	{
		if (T.Place.MapId != CurMapId) { continue; }
		const double D = FVector2D::DistSquared(FVector2D(T.Place.TileX, T.Place.TileY), FVector2D(PlayerTile));
		if (!Best || D < FVector2D::DistSquared(FVector2D(Best->Place.TileX, Best->Place.TileY), FVector2D(PlayerTile))) { Best = &T; }
	}
	if (!Best && Npcs.Num() > 0) { Best = &Npcs[0]; }
	if (!Best) { return false; }
	Out = *Best;
	return true;
}

bool AsdaQuestGuideTarget::CellsBeyond10On()
{
	return CVarAsdaNpcCellsBeyond10.GetValueOnGameThread() != 0;
}

int32 AsdaQuestGuideTarget::NpcCellLimit()
{
	return CellsBeyond10On() ? MAX_int32 : 10;
}

bool AsdaQuestGuideTarget::FollowDoneToNpcOn()
{
	return CVarAsdaFollowDoneToNpc.GetValueOnGameThread() != 0;
}

bool AsdaQuestGuideTarget::FollowNoCoordOn()
{
	return CVarAsdaFollowNoCoordTarget.GetValueOnGameThread() != 0;
}

void AsdaQuestGuideTarget::Places(int32 QuestNo, int32 Stage, int32 Kind, int32 TargetId, int32 RouteMapId, TArray<FPlace>& Out)
{
	Out.Reset();
	if (QuestNo < 0) { return; }
	if (Kind == 1)
	{
		// Цель гида — СТРОКА таблицы мобов, серверный тип = строка + 1 (`AsdaEntityText::QuestTargetName`, аудит 25.10).
		const int32 MobType = TargetId + 1;
		AreaPlaces(QuestNo, Stage, RouteMapId, MobType, Out);
		// Тип цели на карте строки не стоит, но сервер засчитывает за шаг местных мобов (горы Пламении 392–396) — их зоны.
		if (Out.Num() == 0) { AreaPlaces(QuestNo, Stage, RouteMapId, -1, Out); }
		// На карте строки засчитываемых мобов нет вовсе — зоны этого моба на других картах.
		if (Out.Num() == 0) { AreaPlaces(QuestNo, Stage, -1, MobType, Out); }
		return;
	}
	// Место NPC — `npc_list`: эта ветка не зависит от ключа `asda.Quest.FollowDoneToNpc`.
	NpcPlaces(TargetId, RouteMapId, Out);
	if (Out.Num() == 0) { NpcPlaces(TargetId, -1, Out); }
}

bool AsdaQuestGuideTarget::PickForFollow(const TArray<FPlace>& Places, int32 CurMapId, const FIntPoint& PlayerTile, FPlace& Out)
{
	const FPlace* Best = nullptr;
	// Своя карта: ближайшее к персонажу место (как игрок выбрал бы сам по значкам карты мира).
	for (const FPlace& P : Places)
	{
		if (P.MapId != CurMapId) { continue; }
		if (!Best || FVector2D::DistSquared(FVector2D(P.TileX, P.TileY), FVector2D(PlayerTile))
			< FVector2D::DistSquared(FVector2D(Best->TileX, Best->TileY), FVector2D(PlayerTile)))
		{
			Best = &P;
		}
	}
	// Чужая карта: самая населённая зона (порядок таблицы — при равенстве первая).
	if (!Best)
	{
		for (const FPlace& P : Places)
		{
			if (!Best || P.Weight > Best->Weight) { Best = &P; }
		}
	}
	if (!Best) { return false; }
	Out = *Best;
	return true;
}
