// Copyright (c) 2026 @sitxovski (Discord)
// AsdaEffectActor_Emitters.cpp — ★ R2 10.11.2026 (разрез AsdaEffectActor.cpp, дословный перенос): перезапуск/петли эмиттеров, альфа-зажим, сброс, секции и их геометрия.
//   Общее для частей — в AsdaEffectActorInternal.h; шапка и реверс — в AsdaEffectActor.cpp.
#include "AsdaEffectActor.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaView.h"   // ★ волна 3 п.11 (15.09.2026): общая точка наблюдателя (E7)
#include "AsdaSelfTestBatch.h"   // ★ ревизия В2 дедупа №2 (11.10.2026): Quit — выход самотеста по итогу (перехватывается пакетом)
#include "Resources/AsdaMaterialNames.h"   // ★ дедуп №2 В1-C (F10): имя параметра EmissiveBoost
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "World/AsdaCoord.h"                    // ★ Аудит №3 (B1): единый своп осей игра→UE
#include "ProceduralMeshComponent.h"
#include "AsdaFxNames.h"   // теги свечения заточки (цвет вершин)
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "ScopedTransaction.h"   // ★ Ф17 (С-1): уборка сирот обязана отменяться Ctrl+Z — разбор у DropOldestGlowOrphans
#endif
#include "Kismet/GameplayStatics.h"
// ★ Для консольной проверки яркости `asda.Fx.MatDump` (конец файла): чтение скалярного параметра
//   с ЭКЗЕМПЛЯРА материала на секции, отложенные шаги сценария и выполнение консольных команд.
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"   // FMaterialParameterInfo (MaterialTypes.h объявлен устаревшим)
#include "Materials/MaterialInstanceDynamic.h"  // ★ Ф5: MID «силы свечения» поверх материала эмиттера
#include "TimerManager.h"
#include "EngineUtils.h"   // TActorIterator — чтение живых эффектов мира (`asda.Fx.SectionDump`)
#include "UObject/UObjectIterator.h"   // ★ Ф16 (МЕЛК-4): TObjectIterator — CDO запечённых BP_Fx_* (в мире их нет)
#include "World/AsdaFxLightSettings.h"    // ★ 13.09.2026: переопределения свечения предмета/эффекта и сила свечения семейств
#include "World/AsdaFxLightSubsystem.h"   // ★ 13.09.2026: ключ отката asda.Fx.Glow.Overrides, имя .jeb, перезаведение света
#include "Data/AsdaFxLoopRows.h"          // ★ 08.10.2026: ЦИКЛ 1:1 — endMode диска, контейнер, сид-узлы карт (DT_FxLoop)
#include "Engine/DataTable.h"             // ★ 08.10.2026: чтение DT_FxLoop
#include "AsdaDataTableCache.h"            // ★ дедуп №2 В2-C (F3): AsdaDt::Cached — общая загрузка DT_FxLoop
#include "UI/AsdaEffectsSubsystem.h"        // ★ дедуп №2 В2-C (F3): удержание DT_FxLoop на сессию (FAsdaHeldTables)
#include "Engine/GameInstance.h"           // ★ дедуп №2 В2-C (F3): GameInstance эффекта — владелец удержания
#include "Components/SkeletalMeshComponent.h"   // ★ 08.10.2026: холдер режима 2 — порог анимации носителя («≥ порога» с защёлкой)
#include "World/AsdaWorldKind.h"              // ★ F7 (дедуп №2, В3): общий признак «сцена логина»
#include "World/AsdaFxLightComponent.h"        // ★ 15.10.2026: RestVerts строятся только при свете эффекта (IsCarrierAnchorOn)
#if WITH_EDITOR
#include "HAL/PlatformApplicationMisc.h"      // ★ 27.10.2026: буфер обмена кнопок «Копировать/Вставить свет»
#endif
#include "AsdaEffectActorInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaEffectActor*.cpp

// Путь таблицы ЦИКЛА 1:1 (генератор Tools/asda_build_fx_loop_dt.py).
static const TCHAR* const kAsdaFxLoopTablePath = AsdaFxLoop::kTablePath;   // ★ дедуп №2 В2-C (F3): общая константа (AsdaFxLoopRows.h)

// Таблица ЦИКЛА 1:1: грузится по требованию; при отсутствии — повторная попытка не чаще раза в 10 с
// (иначе каждый новый эффект бился бы о диск).
//   ★★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F3): БЫЛО — свой кэш `static TWeakObjectPtr` + `AddToRoot()` (08.10, МЕЛК-6
//   ревизии fx_loop: слабая ссылка отдавала таблицу GC на смене уровня, первый эффект новой карты синхронно грузил ~6 МБ).
//   `RemoveFromRoot` не было нигде (комментарий «корень снимается вместе с модулем» неверен), и это нарушало запрет
//   `AsdaDataTableCache.h`: в редакторе пересобранная генератором `DT_FxLoop` не доезжала до открытого редактора, а замена
//   объекта в корне — тот же класс аварии `!IsRooted()`, что был у флага клана (26.08).
//   СТАЛО — общие точки: загрузка `AsdaDt::Cached` (слабо — редакторная семантика), удержание на игровую сессию —
//   `UAsdaEffectsSubsystem::HoldFxLoopTable` (`FAsdaHeldTables`, группа `FxLoop`, проверка `asda.DtHold.GcCheck`). Рывок
//   «первый эффект новой карты» не возвращается: в игре таблицу держит подсистема GameInstance, а не корень.
//   `Ctx` — любой объект мира (эффект-актор): по нему находится GameInstance; без него (редактор вне PIE) — только слабо.
static UDataTable* AsdaFxLoopTable(const UObject* Ctx)
{
	static double LastFail = -1.0e9;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastFail < 10.0) { return nullptr; }   // ассета нет — не биться о диск чаще раза в 10 с
	UDataTable* T = AsdaDt::Cached(kAsdaFxLoopTablePath);
	if (!T) { LastFail = Now; return nullptr; }
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (UGameInstance* GI = W ? W->GetGameInstance() : nullptr)
	{
		if (UAsdaEffectsSubsystem* ES = GI->GetSubsystem<UAsdaEffectsSubsystem>()) { ES->HoldFxLoopTable(); }
	}
	return T;
}

// Отпечаток трека — та же формула, что в генераторе (`asda_build_fx_loop_dt.py::track_print`):
// сумма по узлам (время + альфа + RGB + scale + 0.001·(полу-экстенты + высота + радиусы + офсеты)).
//   ★ 08.10.2026 (СРЕДН-2/3 ревизии): РЕШЕНИЕ О ПОДСТАНОВКЕ ПО НЕЙ БОЛЬШЕ НЕ ПРИНИМАЕТСЯ — только лог.
//   Вес 0.001 у офсетов при допуске 0.05 пропускал правку до ~50 ед. и отражённый трек башни alpen
//   (Δ 0.0057). Решает поузловое сравнение по всем полям — `AsdaFxLoopNodesEqual` ниже.
static float AsdaFxLoopTrackPrint(const TArray<FAsdaFxNode>& Nodes)
{
	double S = 0.0;
	for (const FAsdaFxNode& N : Nodes)
	{
		S += N.Time + N.Alpha + N.Color.R + N.Color.G + N.Color.B + N.RingScale
			+ 0.001 * (N.SizeX + N.SizeY + N.RingH + N.RingTopR + N.RingBotR
				+ N.RingBasePos.X + N.RingBasePos.Y + N.RingBasePos.Z + N.RingPosOfs.X + N.RingPosOfs.Y + N.RingPosOfs.Z);
	}
	return (float)S;
}

// Узел ассета A равен узлу «печки без сидов» B ПО ВСЕМ ПОЛЯМ `FAsdaFxNode`. bReflect — офсеты B (basePos/pos,
//   единственные каналы, которые печка отражает `_nz()` у type-2 attached) сравниваются после отражения об Nrm.
static bool AsdaFxLoopNodesEqual(const FAsdaFxNode& A, const FAsdaFxNode& B, bool bReflect, const FVector& Nrm)
{
	auto V3 = [](const FVector& X, const FVector& Y)
	{
		return AsdaFxLoopNear(X.X, Y.X) && AsdaFxLoopNear(X.Y, Y.Y) && AsdaFxLoopNear(X.Z, Y.Z);
	};
	const FVector BBase = bReflect ? AsdaFxLoopReflect(B.RingBasePos, Nrm) : B.RingBasePos;
	const FVector BPos  = bReflect ? AsdaFxLoopReflect(B.RingPosOfs, Nrm) : B.RingPosOfs;
	return AsdaFxLoopNear(A.Time, B.Time) && AsdaFxLoopNear(A.Rate, B.Rate)
		&& AsdaFxLoopNear(A.SizeX, B.SizeX) && AsdaFxLoopNear(A.SizeY, B.SizeY)
		&& AsdaFxLoopNear(A.Speed, B.Speed) && AsdaFxLoopNear(A.ConeAngle, B.ConeAngle)
		&& AsdaFxLoopNear(A.Alpha, B.Alpha)
		&& AsdaFxLoopNear(A.Color.R, B.Color.R) && AsdaFxLoopNear(A.Color.G, B.Color.G)
		&& AsdaFxLoopNear(A.Color.B, B.Color.B) && AsdaFxLoopNear(A.Color.A, B.Color.A)
		&& AsdaFxLoopNear(A.Gravity, B.Gravity) && V3(A.ConeAxis, B.ConeAxis)
		&& AsdaFxLoopNear(A.RingScale, B.RingScale) && AsdaFxLoopNear(A.RingRoll, B.RingRoll)
		&& V3(A.RingRot, B.RingRot)
		&& AsdaFxLoopNear(A.RingH, B.RingH) && AsdaFxLoopNear(A.RingTopR, B.RingTopR)
		&& AsdaFxLoopNear(A.RingBotR, B.RingBotR)
		&& AsdaFxLoopNear(A.RingU, B.RingU) && AsdaFxLoopNear(A.RingV, B.RingV)
		&& V3(A.RingBasePos, BBase) && V3(A.RingPosOfs, BPos);
}

// Диагностика отказа подстановки (только лог): ПЕРВОЕ расхождение трека ассета с «печкой без сидов» —
//   «узел N поле: ассет=… таблица=…» (офсеты таблицы — после отражения при bReflect). Пусто — треки равны.
static FString AsdaFxLoopFirstDiff(const TArray<FAsdaFxNode>& Asset, const TArray<FAsdaFxNode>& Base, bool bReflect, const FVector& Nrm)
{
	if (Asset.Num() != Base.Num()) { return FString::Printf(TEXT("узлов %d/%d"), Asset.Num(), Base.Num()); }
	for (int32 i = 0; i < Asset.Num(); ++i)
	{
		const FAsdaFxNode& A = Asset[i];
		const FAsdaFxNode& B = Base[i];
		const FVector BB = bReflect ? AsdaFxLoopReflect(B.RingBasePos, Nrm) : B.RingBasePos;
		const FVector BP = bReflect ? AsdaFxLoopReflect(B.RingPosOfs, Nrm) : B.RingPosOfs;
		const struct { const TCHAR* N; double X, Y; } F[] = {
			{TEXT("Time"), A.Time, B.Time}, {TEXT("Rate"), A.Rate, B.Rate}, {TEXT("SizeX"), A.SizeX, B.SizeX},
			{TEXT("SizeY"), A.SizeY, B.SizeY}, {TEXT("Speed"), A.Speed, B.Speed}, {TEXT("ConeAngle"), A.ConeAngle, B.ConeAngle},
			{TEXT("Alpha"), A.Alpha, B.Alpha}, {TEXT("R"), A.Color.R, B.Color.R}, {TEXT("G"), A.Color.G, B.Color.G},
			{TEXT("B"), A.Color.B, B.Color.B}, {TEXT("A"), A.Color.A, B.Color.A}, {TEXT("Gravity"), A.Gravity, B.Gravity},
			{TEXT("ConeAxis.X"), A.ConeAxis.X, B.ConeAxis.X}, {TEXT("ConeAxis.Y"), A.ConeAxis.Y, B.ConeAxis.Y},
			{TEXT("ConeAxis.Z"), A.ConeAxis.Z, B.ConeAxis.Z}, {TEXT("RingScale"), A.RingScale, B.RingScale},
			{TEXT("RingRoll"), A.RingRoll, B.RingRoll}, {TEXT("RingRot.X"), A.RingRot.X, B.RingRot.X},
			{TEXT("RingRot.Y"), A.RingRot.Y, B.RingRot.Y}, {TEXT("RingRot.Z"), A.RingRot.Z, B.RingRot.Z},
			{TEXT("RingH"), A.RingH, B.RingH}, {TEXT("RingTopR"), A.RingTopR, B.RingTopR}, {TEXT("RingBotR"), A.RingBotR, B.RingBotR},
			{TEXT("RingU"), A.RingU, B.RingU}, {TEXT("RingV"), A.RingV, B.RingV},
			{TEXT("RingBasePos.X"), A.RingBasePos.X, BB.X}, {TEXT("RingBasePos.Y"), A.RingBasePos.Y, BB.Y},
			{TEXT("RingBasePos.Z"), A.RingBasePos.Z, BB.Z}, {TEXT("RingPosOfs.X"), A.RingPosOfs.X, BP.X},
			{TEXT("RingPosOfs.Y"), A.RingPosOfs.Y, BP.Y}, {TEXT("RingPosOfs.Z"), A.RingPosOfs.Z, BP.Z} };
		for (const auto& Fi : F)
		{
			if (!AsdaFxLoopNear(Fi.X, Fi.Y)) { return FString::Printf(TEXT("узел %d %s: ассет=%.4f таблица=%.4f"), i, Fi.N, Fi.X, Fi.Y); }
		}
	}
	return FString();
}

// ★★★ 08.10.2026 (СРЕДН-2/3 ревизии fx_loop): трек ассета + НЕДОСТАЮЩИЕ сид-узлы. Узлы ассета остаются СВОИ
//   (никакой подмены трека целиком), дописываются только узлы `SeedTrack`, которых нет в «печке без сидов»
//   (СТАРТ @0 и КОНЕЦ @период). Условие — трек ассета равен `SeedBaseTrack` поузлово по всем полям:
//     проход 0 — как есть; проход 1 — офсеты `SeedBaseTrack` отражены (type-2 attached, печка `_nz()`): тогда
//     отражаются и дописанные узлы — башня alpen больше не съезжает на ~7.6 ед.
//   Нормаль отражения берётся ИЗ САМОГО ТРЕКА АССЕТА (первая пара «ассет ≠ таблица» по офсету при равной длине:
//     n = (таблица − ассет)/|…|, отражение Хаусхолдера однозначно), затем проверяется на ВСЕХ узлах и полях.
//     `ChiralNormal` актора для этого не годится: у башни он расходится с нормалью печки на ~0.002 — ассет
//     −1.5949 против −1.6103 (замер 08.10, лог `fixes_T_cvar2.log`), что больше допуска 1e-3.
//   false — трек ассета не «печка без сидов» (ручная правка владельца или иные данные): НЕ ТРОГАЕМ.
static bool AsdaFxLoopMergeSeeds(const TArray<FAsdaFxNode>& Asset, const FAsdaFxLoopEmitterRow& RE,
	TArray<FAsdaFxNode>& OutTrack, bool& bOutReflected, FVector& OutNormal)
{
	const TArray<FAsdaFxNode>& Base = RE.SeedBaseTrack;
	OutNormal = FVector::ZeroVector;
	if (Base.Num() < 1 || Base.Num() != Asset.Num() || RE.SeedTrack.Num() <= Base.Num()) { return false; }
	// Нормаль отражения из данных ассета (см. шапку): первая расходящаяся пара офсетов равной длины.
	FVector Derived = FVector::ZeroVector;
	for (int32 i = 0; i < Asset.Num() && Derived.IsNearlyZero(); ++i)
	{
		const FVector Pairs[2][2] = { { Asset[i].RingBasePos, Base[i].RingBasePos }, { Asset[i].RingPosOfs, Base[i].RingPosOfs } };
		for (const auto& Pr : Pairs)
		{
			const FVector D = Pr[1] - Pr[0];
			if (D.Size() > 1e-3 && AsdaFxLoopNear(Pr[0].Size(), Pr[1].Size())) { Derived = D.GetSafeNormal(); break; }
		}
	}
	OutNormal = Derived;   // для лога отказа; при успехе ниже — нормаль, которой реально отражали (0 — без отражения)
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const bool bRefl = (Pass == 1);
		if (bRefl && Derived.IsNearlyZero()) { break; }
		const FVector Chiral = bRefl ? Derived : FVector::ZeroVector;
		bool bSame = true;
		for (int32 i = 0; i < Asset.Num() && bSame; ++i) { bSame = AsdaFxLoopNodesEqual(Asset[i], Base[i], bRefl, Chiral); }
		if (!bSame) { continue; }
		// Узлы «печки без сидов» обязаны входить в полный трек ПОДПОСЛЕДОВАТЕЛЬНОСТЬЮ (сиды только добавляются).
		OutTrack.Reset(RE.SeedTrack.Num());
		int32 Bi = 0;
		for (const FAsdaFxNode& Sn : RE.SeedTrack)
		{
			if (Bi < Base.Num() && AsdaFxLoopNodesEqual(Base[Bi], Sn, false, FVector::ZeroVector))
			{
				OutTrack.Add(Asset[Bi]);   // свой узел ассета, как есть
				++Bi;
			}
			else
			{
				FAsdaFxNode N = Sn;        // дописываемый сид-узел
				if (bRefl)
				{
					N.RingBasePos = AsdaFxLoopReflect(N.RingBasePos, Chiral);
					N.RingPosOfs  = AsdaFxLoopReflect(N.RingPosOfs, Chiral);
				}
				OutTrack.Add(N);
			}
		}
		if (Bi != Base.Num()) { OutTrack.Reset(); return false; }
		bOutReflected = bRefl;
		OutNormal = Chiral;
		return true;
	}
	return false;
}

// Режим конца периода эмиттера по эталону (таблица — в шапке блока «ЦИКЛ 1:1» выше).
//   bHolder — эффект размещён на карте (есть холдер); bEnd0Freeze — ключ эталонного «застыть» у одноразовых.
static EAsdaFxLoopMode AsdaFxLoopModeFor(const FAsdaFxEmitter& E, int32 DiskType, int32 DiskEnd, bool bHolder,
	bool bEnd0Freeze, uint32& OutHold)
{
	using namespace AsdaFxLoopCh;
	OutHold = 0;
	if (DiskType < 0 || DiskEnd < 0) { return EAsdaFxLoopMode::Legacy; }
	const bool bQuadRing = E.bSingleQuad || E.bRing;
	if (!bQuadRing && !E.bMeshEmitter) { return EAsdaFxLoopMode::Legacy; }   // частицы — прежние ключи
	const int32 A = E.EndMode;
	// Значение ассета «вынужденное», если сборщик подменил endMode диска своим постоянным:
	// growQuad/mesh12 пишут 1, кольца — 3 вместо 2.
	const bool bForced = (A != DiskEnd) && (A == 1 || (E.bRing && A == 3 && DiskEnd == 2));
	if (A != DiskEnd && !bForced) { return EAsdaFxLoopMode::Legacy; }   // осознанная правка ассета
	if (E.bMeshEmitter)
	{
		// Меши по `effectAnimTickKeyframe@0x1239250` (0x12), `CEmitter_Mesh_Update@0x123BA70` (0x1F),
		// `effectEmitter_tickUpdate@0x12293B0` (0x11). Каналы узла меша (замер живого оригинала,
		// `fx_pipeline_ground_truth.md` §8): [13] альфа, [19..21] RGB, [15/17/28] масштаб (ConeAxis),
		// [30/32/34] поворот (RingRot), [7/9/11] posLocal (RingPosOfs), [1/3/5] offsetWorld (RingBasePos).
		if (DiskEnd == 0) { return (bHolder || bForced || bEnd0Freeze) ? EAsdaFxLoopMode::Freeze : EAsdaFxLoopMode::Stop; }
		if (DiskEnd == 1) { return EAsdaFxLoopMode::Restart; }
		if (DiskEnd == 3)
		{
			// 0x12/0x1F: `age = 0`, индекс ключа НЕ сброшен; снап RGB, альфы, масштаба, u, v на узел —
			// поворот, posLocal и offsetWorld маршируют наклоном последнего сегмента дальше.
			if (DiskType == 0x12 || DiskType == 0x1F) { OutHold = Alpha | Color | Scale3 | U | V; return EAsdaFxLoopMode::Hold; }
			if (DiskType == 0x11) { return EAsdaFxLoopMode::SawFromEnd; }
			return EAsdaFxLoopMode::Legacy;
		}
		if (DiskEnd == 2)
		{
			if (DiskType == 0x12 || DiskType == 0x1F) { return EAsdaFxLoopMode::MarchAll; }   // `return 0`, марш вечный
			if (DiskType == 0x11) { return EAsdaFxLoopMode::PingPong; }   // `effectEmitter_randomizeAndInit@0x12270B0`
			return EAsdaFxLoopMode::Legacy;
		}
		return EAsdaFxLoopMode::Legacy;
	}
	switch (DiskEnd)
	{
	case 0:
		return (bHolder || bForced || bEnd0Freeze) ? EAsdaFxLoopMode::Freeze : EAsdaFxLoopMode::Stop;
	case 1:
		return EAsdaFxLoopMode::Restart;
	case 2:
		switch (DiskType)
		{
		case 0x0B: case 0x0C: case 0x0E: return EAsdaFxLoopMode::MarchAll;
		case 0x18: case 0x19: case 0x1A: OutHold = All; return EAsdaFxLoopMode::Hold;
		case 0x01: case 0x02: case 0x04: return EAsdaFxLoopMode::PingPong;
		case 0x13: case 0x14: case 0x15: return EAsdaFxLoopMode::Restart;
		default: return EAsdaFxLoopMode::Legacy;
		}
	case 3:
		switch (DiskType)
		{
		case 0x0B: OutHold = Alpha | Color | U | V; return EAsdaFxLoopMode::Hold;
		case 0x0C: OutHold = Alpha | Color | SizeX | SizeY | U | V; return EAsdaFxLoopMode::Hold;
		case 0x0E: OutHold = Alpha | Color | Scale | U | V | H | TopR | BotR; return EAsdaFxLoopMode::Hold;
		case 0x18: case 0x19: case 0x1A: OutHold = All; return EAsdaFxLoopMode::Hold;
		case 0x13: case 0x14: case 0x15: return EAsdaFxLoopMode::WrapMarch;
		case 0x01: case 0x02: OutHold = Alpha | Color | SizeX | SizeY | PosOfs | U | V; return EAsdaFxLoopMode::Hold;
		default: return EAsdaFxLoopMode::Legacy;
		}
	default:
		return EAsdaFxLoopMode::Legacy;
	}
}

void AAsdaEffectActor::RestartEmitters()
{
	ElapsedTime = 0.f;
	// ★ ЦИКЛ 1:1: «играть заново» = и контейнер с нуля (эталон vtbl[1] контейнера `effectParticleReset`)
	LoopContainerTimer = 0.f; bLoopContainerDone = false;
	ResetStates();
}

// ★★★ ЦИКЛ 1:1 — рестарт ВСЕГО эффекта: `effectParticleReset@0x114D0F0` (контейнер endMode 1) и холдер
//   карты (контейнер 0/2 у эффекта на карте). Эталон ставит таймеры эмиттеров в −2, и рендер-проход
//   реинитит каждый (vtbl[1]) — у частиц это `CEmitter_InitReset`, гасящий все живые частицы.
//   Время холдера (ElapsedTime: поворот/дрейф шапки, задержки старта) тоже идёт с нуля — у эталона
//   таймер контейнера обнуляется вместе со стартовым `timeGetTime`.
void AAsdaEffectActor::RestartLoopContainer()
{
	ElapsedTime = 0.f;
	LoopContainerTimer = 0.f;
	bLoopContainerDone = false;
	ResetStates();
}

// ★★★ ЦИКЛ 1:1 — разрешение данных `DT_FxLoop` для этого актора. Зовётся из SimulateStep, когда
//   изменился снимок ключей (первый шаг или переключение в консоли). Ничего не пишет в ассет:
//   сид-узлы подставляются только в ИГРОВОМ мире (копия уровня), исходный трек сохраняется для отката.
void AAsdaEffectActor::ResolveLoopInfo()
{
	const bool bLoopOn = CVarAsdaFxLoop.GetValueOnGameThread() != 0;
	const bool bSeeds = bLoopOn && CVarAsdaFxLoopSeeds.GetValueOnGameThread() != 0;
	const bool bEnd0 = CVarAsdaFxEnd0Freeze.GetValueOnGameThread() != 0;
	LoopResolvedKey = (bLoopOn ? 1 : 0) | (bSeeds ? 2 : 0) | (bEnd0 ? 4 : 0);

	// откат прежней подстановки сидов (ключ выключили на живой сцене)
	for (int32 i = 0; i < LoopRt.Num() && i < Emitters.Num(); ++i)
	{
		if (LoopRt[i].bSeedPatched) { Emitters[i].Nodes = LoopRt[i].SavedNodes; }
	}
	LoopRt.Reset();
	LoopRt.SetNum(Emitters.Num());
	bLoopHolder = false; bLoopContainer = false; bLoopContainerDone = false;
	LoopContainerTimer = 0.f; LoopContainerDur = 0.f; LoopContainerEnd = 3;
	bSyncWaitCross = false;
	LoopAllFrozenTime = 0.f; bLoopFrozenWarned = false;
	if (!bLoopOn) { return; }

	UDataTable* T = AsdaFxLoopTable(this);   // ★ В2-C F3: удержание у GI
	// ★ 08.10.2026 (МЕЛК-6 ревизии): таблицы нет (ещё не загрузилась / сбой чтения) — снимок ключей НЕ
	//   защёлкиваем: следующий шаг попробует снова (сама загрузка с диска троттлится 10 с внутри
	//   `AsdaFxLoopTable`). Раньше актор навсегда оставался на прежнем пути до переключения ключей.
	if (!T) { LoopResolvedKey = -1; return; }
	const FString Key = LoopRowKey();
	const FAsdaFxLoopRow* Row = Key.IsEmpty() ? nullptr : T->FindRow<FAsdaFxLoopRow>(FName(*Key), TEXT("asda.Fx.Loop"), false);
	if (!Row) { return; }

	// Холдер = эффект ЛЕЖИТ в уровне карты (загружен с уровнем, а не заспавнен кодом). Логин — не
	// карта: его эффекты ведёт сцена логина (форджинг и глифы запускаются кодом).
	UWorld* W = GetWorld();
	const bool bPlaced = HasAnyFlags(RF_WasLoaded) || IsNetStartupActor();
	// ★ F7 (дедуп №2, В3, 12.10.2026): общий признак `AsdaWorldKind::IsLoginScene` (в редакторе вне PIE — по имени карты, как было).
	bLoopHolder = bPlaced && W && !AsdaWorldKind::IsLoginScene(W);
	LoopContainerDur = Row->ContainerDuration;
	LoopContainerEnd = (LoopContainerEndOverride >= 0) ? LoopContainerEndOverride : Row->ContainerEndMode;
	bLoopContainer = LoopContainerDur > 0.f;

	// Сопоставление по ИМЕНИ и НОМЕРУ ПОВТОРА имени (сборщики пропускают эмиттеры — индексы разъезжаются).
	TMap<FString, int32> SeenOcc;
	const bool bGameWorld = W && W->IsGameWorld();
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		FAsdaFxEmitter& E = Emitters[i];
		const int32 Occ = SeenOcc.FindOrAdd(E.Name)++;
		const FAsdaFxLoopEmitterRow* RE = nullptr;
		for (const FAsdaFxLoopEmitterRow& Cand : Row->Emitters)
		{
			if (Cand.Occ == Occ && Cand.Name == E.Name) { RE = &Cand; break; }
		}
		if (!RE) { continue; }
		FAsdaFxLoopRt& L = LoopRt[i];
		L.DiskType = RE->DiskType;
		L.DiskEnd = RE->DiskEndMode;
		// Сид-узлы карты: только игровой мир, только если трек ассета — ровно «печка без сидов» ПОУЗЛОВО по
		//   всем полям (★ 08.10.2026, СРЕДН-2/3 ревизии: отпечаток-сумма снят с решения; узлы ассета остаются
		//   свои — дописываются только недостающие сиды, отражение type-2 переносится на них).
		if (bSeeds && bGameWorld && RE->SeedTrack.Num() >= 2 && RE->SeedBaseTrack.Num() > 0
			&& E.Nodes.Num() == RE->SeedBaseTrack.Num())
		{
			TArray<FAsdaFxNode> Merged;
			bool bRefl = false;
			FVector ReflN = FVector::ZeroVector;
			if (AsdaFxLoopMergeSeeds(E.Nodes, *RE, Merged, bRefl, ReflN))
			{
				L.SavedNodes = E.Nodes;
				E.Nodes = MoveTemp(Merged);
				L.bSeedPatched = true;
				const FString ReflTxt = bRefl
					? FString::Printf(TEXT(", офсеты отражены, нормаль из трека ассета %s (ChiralNormal %s)"), *ReflN.ToString(), *ChiralNormal.ToString())
					: FString();
				UE_LOG(LogTemp, Log, TEXT("[петля] %s эм.%d «%s»: дописаны сид-узлы (%d → %d узлов%s)"),
					*Key, i, *E.Name, L.SavedNodes.Num(), E.Nodes.Num(), *ReflTxt);
			}
			else
			{
				// трек ассета не равен «печке без сидов» — ручная правка или иные данные: не трогаем
				const FString Diff0 = AsdaFxLoopFirstDiff(E.Nodes, RE->SeedBaseTrack, false, FVector::ZeroVector);
				const FString DiffR = ReflN.IsNearlyZero() ? FString(TEXT("нормали из трека нет"))
					: AsdaFxLoopFirstDiff(E.Nodes, RE->SeedBaseTrack, true, ReflN);
				UE_LOG(LogTemp, Log, TEXT("[петля] %s эм.%d «%s»: сид-узлы НЕ дописаны — трек ассета отличается от печки без сидов (допуск 1e-3): как есть — %s; с отражением об %s — %s; отпечаток %.4f / таблица %.4f"),
					*Key, i, *E.Name, *Diff0, *ReflN.ToCompactString(), *DiffR, AsdaFxLoopTrackPrint(E.Nodes), RE->SeedBaseSum);
			}
		}
		L.Mode = AsdaFxLoopModeFor(E, L.DiskType, L.DiskEnd, bLoopHolder, bEnd0, L.HoldMask);
	}
}

// ★★★ ЦИКЛ 1:1 — каналы ПОСЛЕ конца периода. Эталон марширует `cur += dt·rate` до проверки конца;
//   rate после последнего перехода — наклон ПОСЛЕДНЕГО сегмента (трек: курсор стоит на предпоследнем
//   узле, цель — последний). Удержанные каналы ветка конца снапает на узел — их не трогаем.
//   WrapMarch (0x13/0x14, endMode 3): `lifeTimer = 0`, а линейный марш идёт дальше ⇒ к значению в
//   фазе прибавляется (КОНЕЦ − СТАРТ) за каждый оборот. Цвет/альфа у эталона уходят в вершину как
//   `(uint8)(uint64)(v·255)` — при выходе за 1 байт заворачивается; это и воспроизводится.
void AAsdaEffectActor::ApplyLoopChannels(const FAsdaFxEmitter& E, const FAsdaFxEmitterState& S,
	const FAsdaFxLoopRt& L, FAsdaFxNode& Cur) const
{
	using namespace AsdaFxLoopCh;
	const int32 N = E.Nodes.Num();
	if (N < 2) { return; }
	const FAsdaFxNode* A = nullptr;
	const FAsdaFxNode* B = nullptr;
	double K = 0.0;
	uint32 Hold = 0;
	bool bByteWrap = false;
	switch (L.Mode)
	{
	case EAsdaFxLoopMode::Hold:
	case EAsdaFxLoopMode::MarchAll:
		if (S.OverTime <= 0.f) { return; }
		A = &E.Nodes[N - 2]; B = &E.Nodes[N - 1];
		K = (double)S.OverTime / FMath::Max((double)(B->Time - A->Time), 1e-4);
		Hold = (L.Mode == EAsdaFxLoopMode::Hold) ? L.HoldMask : 0u;
		bByteWrap = (L.Mode == EAsdaFxLoopMode::MarchAll);
		break;
	case EAsdaFxLoopMode::WrapMarch:
		if (S.WrapCount <= 0) { return; }
		A = &E.Nodes[0]; B = &E.Nodes[N - 1];
		K = (double)S.WrapCount;
		bByteWrap = true;
		break;
	case EAsdaFxLoopMode::SawFromEnd:
		// меш 0x11, endMode 3: каждый период стартует со снапа КОНЦА и марширует дальше ⇒ ровно один
		// сдвиг (КОНЕЦ − СТАРТ) поверх фазы, начиная со второго оборота
		if (S.WrapCount <= 0) { return; }
		A = &E.Nodes[0]; B = &E.Nodes[N - 1];
		K = 1.0;
		break;
	default:
		return;
	}
	auto D = [K](float From, float To) { return (float)(((double)To - (double)From) * K); };
	auto DV = [K](const FVector& From, const FVector& To) { return (To - From) * K; };
	// (uint8)(uint64)(v·255): отрицательное у x86 cvttsd2si даёт 0x8000…0 ⇒ младший байт 0.
	auto Byte = [](float Val) { if (Val <= 0.f) { return 0.f; } return (float)(FMath::Fmod(FMath::FloorToDouble((double)Val * 255.0), 256.0) / 255.0); };
	if (!(Hold & Alpha) && A->Alpha != B->Alpha)
	{
		Cur.Alpha += D(A->Alpha, B->Alpha);
		// ★ ЦИКЛ-3: у типов с обрезкой альфы в draw (0x0B/0x0C/0x0E/0x18/0x19) канал > 1 эталон режет до 1
		//   ДО упаковки в байт — заворота нет; значение > 1 оставляем CommitAlphaClamp.
		if (bByteWrap && !(Cur.Alpha > 1.f && IsAlphaClampKind(E))) { Cur.Alpha = Byte(Cur.Alpha); }
	}
	if (!(Hold & Color) && A->Color != B->Color)
	{
		Cur.Color.R += D(A->Color.R, B->Color.R);
		Cur.Color.G += D(A->Color.G, B->Color.G);
		Cur.Color.B += D(A->Color.B, B->Color.B);
		if (bByteWrap) { Cur.Color.R = Byte(Cur.Color.R); Cur.Color.G = Byte(Cur.Color.G); Cur.Color.B = Byte(Cur.Color.B); }
	}
	if (!(Hold & SizeX))   { Cur.SizeX     += D(A->SizeX, B->SizeX); }
	if (!(Hold & SizeY))   { Cur.SizeY     += D(A->SizeY, B->SizeY); }
	if (!(Hold & Scale))   { Cur.RingScale += D(A->RingScale, B->RingScale); }
	if (!(Hold & Roll))    { Cur.RingRoll  += D(A->RingRoll, B->RingRoll); }
	if (!(Hold & Rot))     { Cur.RingRot   += DV(A->RingRot, B->RingRot); }
	if (!(Hold & PosOfs))  { Cur.RingPosOfs  += DV(A->RingPosOfs, B->RingPosOfs); }
	if (!(Hold & BasePos)) { Cur.RingBasePos += DV(A->RingBasePos, B->RingBasePos); }
	if (!(Hold & U))       { Cur.RingU     += D(A->RingU, B->RingU); }
	if (!(Hold & V))       { Cur.RingV     += D(A->RingV, B->RingV); }
	if (!(Hold & H))       { Cur.RingH     += D(A->RingH, B->RingH); }
	if (!(Hold & TopR))    { Cur.RingTopR  += D(A->RingTopR, B->RingTopR); }
	if (!(Hold & BotR))    { Cur.RingBotR  += D(A->RingBotR, B->RingBotR); }
	if (!(Hold & Scale3))  { Cur.ConeAxis  += DV(A->ConeAxis, B->ConeAxis); }   // масштаб меша (у квадов/колец константа)
}

// ★★★ ЦИКЛ 1:1 — переопределение endMode контейнера владельцем (см. объявление). Действует сразу:
//   если данные уже разрешены, новое значение подхватывается без повторного чтения таблицы.
void AAsdaEffectActor::SetLoopContainerEndOverride(int32 Mode)
{
	LoopContainerEndOverride = Mode;
	// ★ 08.10.2026 (ревизия fx_parity3 МЕЛК-2): признак «контейнер окончен» НЕ снимаем. Эталон по концу контейнера 0/2
	//   ставит `lifeTimer = −1` и больше не обновляет эффект — позднее переопределение режима его не оживляет. Поднять
	//   погашенный эффект могут только перезапуск (RestartEmitters / RestartLoopContainer) и смена ключей (ResolveLoopInfo).
	//   До гашения признак и так false — вызов сразу после спавна (все нынешние места) ведёт себя как прежде.
	if (bLoopContainer && Mode >= 0) { LoopContainerEnd = Mode; }
}

// ★★★ ЦИКЛ 1:1 — ключ строки DT_FxLoop (та же формула, что `row_key` генератора).
FString AAsdaEffectActor::LoopRowKey() const
{
	FString Key = EffectName.IsEmpty() ? UAsdaFxLightSubsystem::ResolveJebName(this) : EffectName;
	Key = Key.ToLower().Replace(TEXT("-"), TEXT("_"));
	if (Key.EndsWith(TEXT(".jeb"))) { Key.LeftChopInline(4); }
	return Key;
}

// ★★★ ЦИКЛ 1:1 — контейнер без побочных эффектов (см. объявление). Если данные уже разрешены шагом —
//   берём их, иначе читаем строку таблицы напрямую (эффект только что заспавнен, шага ещё не было).
bool AAsdaEffectActor::PeekLoopContainer(float& OutDur, int32& OutEnd) const
{
	OutDur = 0.f; OutEnd = 3;
	if (CVarAsdaFxLoop.GetValueOnGameThread() == 0) { return false; }
	if (LoopResolvedKey >= 0 && (LoopResolvedKey & 1))
	{
		if (!bLoopContainer) { return false; }
		OutDur = LoopContainerDur; OutEnd = LoopContainerEnd;
		return true;
	}
	UDataTable* T = AsdaFxLoopTable(this);   // ★ В2-C F3: удержание у GI
	if (!T) { return false; }
	const FString Key = LoopRowKey();
	const FAsdaFxLoopRow* Row = Key.IsEmpty() ? nullptr : T->FindRow<FAsdaFxLoopRow>(FName(*Key), TEXT("asda.Fx.Loop"), false);
	if (!Row || Row->ContainerDuration <= 0.f) { return false; }
	OutDur = Row->ContainerDuration;
	OutEnd = (LoopContainerEndOverride >= 0) ? LoopContainerEndOverride : Row->ContainerEndMode;
	return true;
}

// ★★★ ЦИКЛ 1:1 (СРЕДН-5 ревизии) — единая выборка узла текущего кадра (см. объявление). Повторяет ровно то,
//   что раньше стояло в двух местах сборки секции: квад/кольцо (`Period ≥ 0.05`) и меш (`Period ≥ 0.1`);
//   у частиц и на прежнем пути (`asda.Fx.Loop 0`, режим Legacy) — голый `NodeAt(LifeTimer)`, бит-в-бит как было.
void AAsdaEffectActor::SampleEmitterNode(int32 EmitterIdx, FAsdaFxNode& Out) const
{
	if (!Emitters.IsValidIndex(EmitterIdx) || !States.IsValidIndex(EmitterIdx)) { Out = FAsdaFxNode(); return; }
	const FAsdaFxEmitter& E = Emitters[EmitterIdx];
	const FAsdaFxEmitterState& S = States[EmitterIdx];
	const bool bLoopOn = CVarAsdaFxLoop.GetValueOnGameThread() != 0;
	const bool bLoopKind = E.bMeshEmitter || E.bSingleQuad || E.bRing;
	const FAsdaFxLoopRt* LR = (bLoopOn && bLoopKind && LoopRt.IsValidIndex(EmitterIdx)
		&& LoopRt[EmitterIdx].Mode != EAsdaFxLoopMode::Legacy) ? &LoopRt[EmitterIdx] : nullptr;
	const float Period = FMath::Max(E.Period, E.bMeshEmitter ? 0.1f : 0.05f);
	const float T = (LR && LR->Mode == EAsdaFxLoopMode::PingPong && (S.WrapCount & 1)) ? (Period - S.LifeTimer) : S.LifeTimer;
	NodeAt(E, T, Out);
	// ★★★ ЦИКЛ-3: записанная обрезкой поправка канала альфы — до марша сверх конца (марш эталона идёт от канала).
	//   Ключ проверяется тот же, что ставит PrepareAlphaClamp: после снапа узла поправка не действует.
	if (bLoopOn && IsAlphaClampKind(E) && S.AlphaClampKey == (SegIndexAt(E, T) + 4096 * (S.WrapCount & 0x7FFFF)))
	{
		Out.Alpha += S.AlphaClampOfs;
	}
	if (LR) { ApplyLoopChannels(E, S, *LR, Out); }
}

// ★★★ ЦИКЛ-3 (08.10.2026, решение владельца) — см. объявление. Три draw-функции эталона с обрезкой
//   (E:97289–97290, E:99368–99370, render_core.c:49351–49352) обслуживают 0x0B/0x18, 0x0E и 0x0C/0x19.
//   Меши (0x11/0x12/0x1F), частицы и 0x13/0x14 рисуются другими функциями — обрезки с записью у них нет.
//   ★ 08.10.2026 (ревизия fx_parity3 МЕЛК-1): те же три draw-функции рисуют и 0x01 (0x120AEB0), 0x02 (0x120CF10) и
//   0x04 (0x1215C10) — таксономия `docs/client/logic/effect_runtime.md` §7 (строки 478–481); у порта это bSingleQuad/bRing.
bool AAsdaEffectActor::IsAlphaClampKind(const FAsdaFxEmitter& E) const
{
	if (E.bMeshEmitter || !(E.bSingleQuad || E.bRing)) { return false; }
	const int32 Ty = E.JebType;
	return Ty == 0x0B || Ty == 0x18 || Ty == 0x0C || Ty == 0x19 || Ty == 0x0E
		|| Ty == 0x01 || Ty == 0x02 || Ty == 0x04;
}

// ★★★ ЦИКЛ-3 — снап канала альфы ДО выборки кадра. У эталона (`effectUpdateKeyframeTrack@0x11F7B10`, E:92512 марш
//   `alpha += dt·node[rate]`, E:92539 снап `alpha = node[+52]` при `lifeTimer > node.time`) канал получает значение
//   узла при каждом переходе узла, при реините (endMode 1, рестарт контейнера) и при снапе КОНЦА (endMode 0, удержание
//   альфы endMode 3/2 у 0x18/0x19 — каждый кадр). Во всех этих случаях поправка от прежних обрезок теряется.
void AAsdaEffectActor::PrepareAlphaClamp(int32 EmitterIdx)
{
	if (!Emitters.IsValidIndex(EmitterIdx) || !States.IsValidIndex(EmitterIdx)) { return; }
	const FAsdaFxEmitter& E = Emitters[EmitterIdx];
	FAsdaFxEmitterState& S = States[EmitterIdx];
	if (CVarAsdaFxLoop.GetValueOnGameThread() == 0 || !IsAlphaClampKind(E)) { return; }
	const FAsdaFxLoopRt* LR = (LoopRt.IsValidIndex(EmitterIdx) && LoopRt[EmitterIdx].Mode != EAsdaFxLoopMode::Legacy)
		? &LoopRt[EmitterIdx] : nullptr;
	const float Period = FMath::Max(E.Period, 0.05f);
	const float T = (LR && LR->Mode == EAsdaFxLoopMode::PingPong && (S.WrapCount & 1)) ? (Period - S.LifeTimer) : S.LifeTimer;
	const int32 Key = SegIndexAt(E, T) + 4096 * (S.WrapCount & 0x7FFFF);   // сегмент трека + оборот (как в SampleEmitterNode)
	const bool bHeldAlpha = S.bFrozen
		|| (LR && LR->Mode == EAsdaFxLoopMode::Hold && S.OverTime > 0.f && (LR->HoldMask & AsdaFxLoopCh::Alpha));
	if (bHeldAlpha || Key != S.AlphaClampKey || S.LifeTimer + 1e-6f < S.AlphaClampPrevLife)
	{
		S.AlphaClampOfs = 0.f;
	}
	S.AlphaClampKey = Key;
	S.AlphaClampPrevLife = S.LifeTimer;
}

// ★★★ ЦИКЛ-3 — обрезка с записью: `if (alpha > 1) alpha = 1`; разница уходит в поправку канала, поэтому дальше
//   канал марширует ОТ 1 (у `mon_hiskaiyanormal_bo_00` Object1: 39.2 → 1 и вниз наклоном −76/с), а не от
//   времени трека. RGB > 1 НЕ режется (решение владельца 08.10.2026; эталон его тоже не режет).
void AAsdaEffectActor::CommitAlphaClamp(int32 EmitterIdx, FAsdaFxNode& Cur)
{
	if (!Emitters.IsValidIndex(EmitterIdx) || !States.IsValidIndex(EmitterIdx)) { return; }
	if (CVarAsdaFxLoop.GetValueOnGameThread() == 0 || !IsAlphaClampKind(Emitters[EmitterIdx])) { return; }
	if (Cur.Alpha > 1.f)
	{
		States[EmitterIdx].AlphaClampOfs += 1.f - Cur.Alpha;
		Cur.Alpha = 1.f;
	}
}

// ★★★ ДИАГНОСТИКА ЦИКЛА (`asda.Fx.LoopProbe`): один шаг ТЕМ ЖЕ SimulateStep, мимо гейтов видимости.
void AAsdaEffectActor::DebugLoopStep(float Dt)
{
	if (Emitters.Num() == 0) { return; }
	if (States.Num() != Emitters.Num()) { ResetStates(); }
	FAsdaFxViewSample V;
	SampleView(V);
	SimulateStep(FMath::Min(Dt * TimeScale, 0.1f), V.bHasLocation ? &V.Location : nullptr, V);
}

bool AAsdaEffectActor::GetLoopDebug(int32 EmitterIdx, int32& OutMode, int32& OutDiskType, int32& OutDiskEnd,
	float& OutLife, float& OutOver, int32& OutWrap, bool& bOutStopped, bool& bOutSeed) const
{
	if (!States.IsValidIndex(EmitterIdx)) { return false; }
	const FAsdaFxEmitterState& S = States[EmitterIdx];
	const bool bL = LoopRt.IsValidIndex(EmitterIdx);
	OutMode = bL ? (int32)LoopRt[EmitterIdx].Mode : 0;
	OutDiskType = bL ? LoopRt[EmitterIdx].DiskType : -1;
	OutDiskEnd = bL ? LoopRt[EmitterIdx].DiskEnd : -1;
	bOutSeed = bL && LoopRt[EmitterIdx].bSeedPatched;
	OutLife = S.LifeTimer; OutOver = S.OverTime; OutWrap = S.WrapCount; bOutStopped = S.bStopped;
	return true;
}

void AAsdaEffectActor::GetLoopContainerDebug(bool& bOutKnown, bool& bOutHolder, float& OutDur, int32& OutEnd, float& OutTimer) const
{
	bOutKnown = bLoopContainer; bOutHolder = bLoopHolder; OutDur = LoopContainerDur; OutEnd = LoopContainerEnd;
	OutTimer = LoopContainerTimer;
}

void AAsdaEffectActor::ResetStates()
{
	States.SetNum(Emitters.Num());
	SectionVerts.SetNum(Emitters.Num() * 2);   // [i] = основная секция, [Emitters.Num()+i] = парная ModAdd (MaterialB)
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		States[i].Slots.SetNum(kSlots);
		for (FAsdaFxParticle& P : States[i].Slots) P.Age = -1.f;
		States[i].EmitAccum = 0.f; States[i].LifeTimer = 0.f; States[i].bStopped = false; States[i].bStarted = false;
		// ★ ЦИКЛ 1:1: живое состояние конца периода — с нуля (реинит эмиттера эталона)
		States[i].OverTime = 0.f; States[i].WrapCount = 0; States[i].FlipTime = 0.f; States[i].bFrozen = false;
		// ★ ЦИКЛ-3: реинит эмиттера эталона снапает каналы — поправка альфы с нуля
		States[i].AlphaClampOfs = 0.f; States[i].AlphaClampKey = -1; States[i].AlphaClampPrevLife = 0.f;
		ResetEmitterMotion(Emitters[i], States[i]);
	}
	for (int32& sv : SectionVerts) sv = -1;
	if (Mesh) Mesh->ClearAllMeshSections();
}

// ★★★ 1:1 `CEmitter_InitReset@0x120F0E0` (см. FAsdaFxEmitter::bEmitterMotion):
//   emitPos = posBase + rand_pm·posRange ;  emitPosVel = (posTarget − emitPos)/период
//   rotCur  = rotStart + rand_pm·rotVar   ;  rotVel    = (rotEnd   − rotCur )/период
//   (в эталоне делитель — РАНДОМИЗИРОВАННОЕ время жизни эмиттера `lifeBaseSeed + rand_pm·lifeRange`;
//    у нас период уже собран сборщиком из того же поля, разброса периода в порте нет — см. §7 отчёта).
void AAsdaEffectActor::ResetEmitterMotion(const FAsdaFxEmitter& E, FAsdaFxEmitterState& S)
{
	S.EmitPos = FVector::ZeroVector; S.EmitPosVel = FVector::ZeroVector;
	S.EmitRot = FVector::ZeroVector; S.EmitRotVel = FVector::ZeroVector;
	if (!E.bEmitterMotion) { return; }
	const float Per = FMath::Max(E.Period, 0.05f);
	S.EmitPos = E.PosBase + FVector(RandPM() * E.PosRange.X, RandPM() * E.PosRange.Y, RandPM() * E.PosRange.Z);
	S.EmitPosVel = (E.PosTarget - S.EmitPos) / Per;
	S.EmitRot = E.EmitRotStart + FVector(RandPM() * E.EmitRotVar.X, RandPM() * E.EmitRotVar.Y, RandPM() * E.EmitRotVar.Z);
	S.EmitRotVel = (E.EmitRotEnd - S.EmitRot) / Per;
}

// ★ Точка эмиссии в локальных UE-осях = EmitPos, повёрнутая КАНАЛЬНЫМИ вращениями эмиттера и
//   RotY холдера (эталон `CEmitter_Update_v3`: R = RotX·RotY·RotZ, затем ·RotY(node.angle324)).
//   Углы авторены в GAME-осях row-vector D3DX; перевод в UE (= (−gz, gx, gy)) даёт ровно те же
//   три формулы, что уже выведены и сверены с live.dmp для меш-каналов (§H6) и орбиты квадов:
//     game RotX: X' = X·c − Z·s ; Z' = X·s + Z·c
//     game RotY: X' = X·c + Y·s ; Y' = −X·s + Y·c
//     game RotZ: Y' = Y·c − Z·s ; Z' = Y·s + Z·c
FVector AAsdaEffectActor::EmitterLocalPos(const FAsdaFxEmitter& E, const FAsdaFxEmitterState& S,
                                          float HolderAngleDeg) const
{
	if (!E.bEmitterMotion) { return E.PosBase; }
	FVector P = S.EmitPos;
	const float DegR = PI / 180.f;
	auto RotYue = [](const FVector& V, float A)
	{ const float c = FMath::Cos(A), s = FMath::Sin(A); return FVector(V.X * c + V.Y * s, -V.X * s + V.Y * c, V.Z); };
	if (!FMath::IsNearlyZero(S.EmitRot.X))
	{
		const float c = FMath::Cos(S.EmitRot.X * DegR), s = FMath::Sin(S.EmitRot.X * DegR);
		P = FVector(P.X * c - P.Z * s, P.Y, P.X * s + P.Z * c);
	}
	if (!FMath::IsNearlyZero(S.EmitRot.Y)) { P = RotYue(P, S.EmitRot.Y * DegR); }
	if (!FMath::IsNearlyZero(S.EmitRot.Z))
	{
		const float c = FMath::Cos(S.EmitRot.Z * DegR), s = FMath::Sin(S.EmitRot.Z * DegR);
		P = FVector(P.X, P.Y * c - P.Z * s, P.Y * s + P.Z * c);
	}
	if (!FMath::IsNearlyZero(HolderAngleDeg)) { P = RotYue(P, HolderAngleDeg * DegR); }
	return P;
}

void AAsdaEffectActor::ClearFxSection(int32 SectionIdx)
{
	if (SectionVerts.IsValidIndex(SectionIdx) && SectionVerts[SectionIdx] != 0)
	{ Mesh->ClearMeshSection(SectionIdx); SectionVerts[SectionIdx] = 0; }
	const int32 Pair = Emitters.Num() + SectionIdx;
	if (SectionVerts.IsValidIndex(Pair) && SectionVerts[Pair] != 0)
	{ Mesh->ClearMeshSection(Pair); SectionVerts[Pair] = 0; }
	if (RestVerts.IsValidIndex(SectionIdx)) { RestVerts[SectionIdx].Reset(); }   // ★ 15.10.2026: облако в кадре носителя — вместе с секцией
}

// ★ Точка эмиссии в МИРЕ — ровно та же арифметика, что в `SpawnParticle` (см. зонд asda.Fx.MotionProbe).
FVector AAsdaEffectActor::EmitterProbePos(int32 EmitterIdx) const
{
	if (!Emitters.IsValidIndex(EmitterIdx) || !States.IsValidIndex(EmitterIdx)) { return GetActorLocation(); }
	const FAsdaFxEmitter& E = Emitters[EmitterIdx];
	const FVector Local = EmitterLocalPos(E, States[EmitterIdx], HolderYawStart + HolderYawSpeed * ElapsedTime);
	// Та же развилка «attached (только позиция) / полный трансформ», что в SpawnParticle.
	const bool bAttachedMap = !bBaseAlreadyUE && !bFollowBoneFull && !bFollowCharWorld &&
		(FollowActor != nullptr || (GetRootComponent() && GetRootComponent()->GetAttachParent() != nullptr));
	return bAttachedMap ? (GetActorLocation() + Local) : GetActorTransform().TransformPosition(Local);
}

// ★ Центроид построенной геометрии секции — см. шапку метода в заголовке.
bool AAsdaEffectActor::EmitterSectionCentroid(int32 EmitterIdx, FVector& Out) const
{
	if (!Mesh) { return false; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	if (!S || S->ProcVertexBuffer.Num() == 0) { return false; }
	FVector Sum = FVector::ZeroVector;
	for (const FProcMeshVertex& V : S->ProcVertexBuffer) { Sum += V.Position; }
	Out = Sum / (double)S->ProcVertexBuffer.Num();
	return true;
}

// ★ Радиус построенной геометрии секции — см. шапку метода в заголовке.
float AAsdaEffectActor::EmitterSectionRadius(int32 EmitterIdx) const
{
	FVector C;
	if (!EmitterSectionCentroid(EmitterIdx, C)) { return -1.f; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	float R = 0.f;
	for (const FProcMeshVertex& V : S->ProcVertexBuffer) { R = FMath::Max(R, (float)FVector::Dist(V.Position, C)); }
	return R;
}

// ★★★ ВОЛНА «форма-2» (21.09.2026), п. 2 ревизии: «есть ли геометрия» за O(1) — см. шапку в заголовке.
int32 AAsdaEffectActor::EmitterSectionVertCount(int32 EmitterIdx) const
{
	if (!Mesh) { return 0; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	return S ? S->ProcVertexBuffer.Num() : 0;
}

// ★★★ ВОЛНА Ф4: центроид + радиус + число вершин секции за один вызов — см. шапку метода в заголовке.
bool AAsdaEffectActor::EmitterSectionStats(int32 EmitterIdx, FVector& OutCentroid, float& OutRadius, int32& OutVerts, bool bCarrierFrame) const
{
	OutVerts = 0; OutRadius = 0.f;
	if (!Mesh) { return false; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	if (!S || S->ProcVertexBuffer.Num() == 0) { return false; }
	// ★ 15.10.2026: облако «в кадре носителя» — только если оно построено ТЕМ ЖЕ шагом (число вершин совпадает).
	const TArray<FVector>* Rest = CarrierFrameVerts(EmitterIdx, bCarrierFrame, S->ProcVertexBuffer.Num());
	auto Pos = [S, Rest](int32 k) -> FVector { return Rest ? (*Rest)[k] : S->ProcVertexBuffer[k].Position; };
	OutVerts = S->ProcVertexBuffer.Num();
	FVector Sum = FVector::ZeroVector;
	for (int32 k = 0; k < OutVerts; ++k) { Sum += Pos(k); }
	OutCentroid = Sum / (double)OutVerts;
	for (int32 k = 0; k < OutVerts; ++k) { OutRadius = FMath::Max(OutRadius, (float)FVector::Dist(Pos(k), OutCentroid)); }
	return true;
}

// ★ 15.10.2026: вершины секции «в кадре носителя» (см. RestVerts в .h) или nullptr — тогда читаются мировые вершины.
const TArray<FVector>* AAsdaEffectActor::CarrierFrameVerts(int32 EmitterIdx, bool bCarrierFrame, int32 SectionVertNum) const
{
	if (!bCarrierFrame || !RestVerts.IsValidIndex(EmitterIdx)) { return nullptr; }
	const TArray<FVector>& R = RestVerts[EmitterIdx];
	return (R.Num() > 0 && R.Num() == SectionVertNum) ? &R : nullptr;
}

// ★★★ ДИАГНОСТИКА ЦИКЛА (`asda.Fx.LoopProbe`, 08.10.2026) — см. объявление в заголовке. Только чтение.
bool AAsdaEffectActor::EmitterSectionLook(int32 EmitterIdx, FVector& OutCentroid, float& OutRadius, float& OutIntensity,
	FVector2D& OutUV, FVector& OutDir0) const
{
	OutRadius = 0.f; OutIntensity = 0.f; OutUV = FVector2D::ZeroVector; OutDir0 = FVector::ZeroVector;
	if (!Mesh) { return false; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	if (!S || S->ProcVertexBuffer.Num() == 0) { return false; }
	FVector Sum = FVector::ZeroVector; double ISum = 0.0; FVector2D UvSum = FVector2D::ZeroVector;
	for (const FProcMeshVertex& V : S->ProcVertexBuffer)
	{
		Sum += V.Position;
		ISum += (V.Color.A / 255.0) * (FMath::Max3(V.Color.R, V.Color.G, V.Color.B) / 255.0);
		UvSum += V.UV0;
	}
	const double N = (double)S->ProcVertexBuffer.Num();
	OutCentroid = Sum / N;
	OutIntensity = (float)(ISum / N);
	OutUV = UvSum / N;
	for (const FProcMeshVertex& V : S->ProcVertexBuffer) { OutRadius = FMath::Max(OutRadius, (float)FVector::Dist(V.Position, OutCentroid)); }
	OutDir0 = (S->ProcVertexBuffer[0].Position - OutCentroid).GetSafeNormal();
	return true;
}

// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ВОЛНА Ф8 (10.09.2026): ОРИЕНТИРОВАННАЯ КОРОБКА СЕКЦИИ — ГЛАВНЫЕ ОСИ ОБЛАКА ВЕРШИН.
//   Полное обоснование — в шапке метода (AsdaEffectActor.h). Здесь только механика.
// ═══════════════════════════════════════════════════════════════════════════════════════════════════
// Симметричное разложение 3×3 методом Якоби. Матрица маленькая и всегда симметричная (ковариация),
// поэтому свипов хватает четырёх; десять взято с запасом — цена одного свипа считанные умножения.
// На выходе: V — столбцы-собственные векторы, D — собственные значения (разбросы вдоль осей).
static void AsdaJacobiEigen3(double A[3][3], double V[3][3], double D[3])
{
	for (int32 i = 0; i < 3; ++i) { for (int32 j = 0; j < 3; ++j) { V[i][j] = (i == j) ? 1.0 : 0.0; } }
	for (int32 Sweep = 0; Sweep < 10; ++Sweep)
	{
		// Наибольший внедиагональный элемент — его и обнуляем поворотом
		double Off = 0.0; int32 p = 0, q = 1;
		for (int32 i = 0; i < 3; ++i)
		{
			for (int32 j = i + 1; j < 3; ++j)
			{
				if (FMath::Abs(A[i][j]) > Off) { Off = FMath::Abs(A[i][j]); p = i; q = j; }
			}
		}
		if (Off < 1.e-12) { break; }
		const double Theta = (A[q][q] - A[p][p]) / (2.0 * A[p][q]);
		const double T = (Theta >= 0.0 ? 1.0 : -1.0) / (FMath::Abs(Theta) + FMath::Sqrt(Theta * Theta + 1.0));
		const double C = 1.0 / FMath::Sqrt(T * T + 1.0);
		const double S = T * C;
		const double App = A[p][p], Aqq = A[q][q], Apq = A[p][q];
		A[p][p] = App - T * Apq; A[q][q] = Aqq + T * Apq; A[p][q] = A[q][p] = 0.0;
		for (int32 k = 0; k < 3; ++k)
		{
			if (k != p && k != q)
			{
				const double Akp = A[k][p], Akq = A[k][q];
				A[k][p] = A[p][k] = C * Akp - S * Akq;
				A[k][q] = A[q][k] = S * Akp + C * Akq;
			}
			const double Vkp = V[k][p], Vkq = V[k][q];
			V[k][p] = C * Vkp - S * Vkq;
			V[k][q] = S * Vkp + C * Vkq;
		}
	}
	for (int32 i = 0; i < 3; ++i) { D[i] = A[i][i]; }
}

bool AAsdaEffectActor::EmitterSectionFrame(int32 EmitterIdx, FVector& OutCenter,
	FVector& OutAxisA, FVector& OutAxisB, FVector& OutAxisN, FVector& OutHalf, bool bCarrierFrame) const
{
	OutCenter = FVector::ZeroVector;
	OutAxisA = FVector::ForwardVector; OutAxisB = FVector::RightVector; OutAxisN = FVector::UpVector;
	OutHalf = FVector::ZeroVector;
	if (!Mesh) { return false; }
	const FProcMeshSection* S = Mesh->GetProcMeshSection(EmitterIdx);
	if (!S || S->ProcVertexBuffer.Num() < 3) { return false; }
	// ★ 15.10.2026: облако «в кадре носителя» (см. EmitterSectionStats).
	const TArray<FVector>* Rest = CarrierFrameVerts(EmitterIdx, bCarrierFrame, S->ProcVertexBuffer.Num());
	auto Pos = [S, Rest](int32 k) -> FVector { return Rest ? (*Rest)[k] : S->ProcVertexBuffer[k].Position; };

	// Проход 1: центр
	FVector Sum = FVector::ZeroVector;
	const int32 N = S->ProcVertexBuffer.Num();
	for (int32 k = 0; k < N; ++k) { Sum += Pos(k); }
	OutCenter = Sum / (double)N;

	// Проход 2: ковариация (симметрична — считаем шесть чисел, не девять)
	double Cov[3][3] = { {0,0,0},{0,0,0},{0,0,0} };
	for (int32 k = 0; k < N; ++k)
	{
		const FVector D = Pos(k) - OutCenter;
		Cov[0][0] += D.X * D.X; Cov[1][1] += D.Y * D.Y; Cov[2][2] += D.Z * D.Z;
		Cov[0][1] += D.X * D.Y; Cov[0][2] += D.X * D.Z; Cov[1][2] += D.Y * D.Z;
	}
	Cov[1][0] = Cov[0][1]; Cov[2][0] = Cov[0][2]; Cov[2][1] = Cov[1][2];
	for (int32 i = 0; i < 3; ++i) { for (int32 j = 0; j < 3; ++j) { Cov[i][j] /= (double)N; } }

	double Vec[3][3], Val[3];
	AsdaJacobiEigen3(Cov, Vec, Val);

	// Сортировка осей ПО УБЫВАНИЮ разброса: [0] самая длинная, [2] — нормаль плоскости
	int32 Ord[3] = { 0, 1, 2 };
	for (int32 i = 0; i < 2; ++i)
	{
		for (int32 j = i + 1; j < 3; ++j)
		{
			if (Val[Ord[j]] > Val[Ord[i]]) { const int32 T = Ord[i]; Ord[i] = Ord[j]; Ord[j] = T; }
		}
	}
	auto Col = [&Vec](int32 c) { return FVector(Vec[0][c], Vec[1][c], Vec[2][c]).GetSafeNormal(); };
	OutAxisA = Col(Ord[0]); OutAxisB = Col(Ord[1]); OutAxisN = Col(Ord[2]);
	// Вырожденный случай (все вершины на прямой / в точке): нормализация вернула ноль — чиним правой тройкой
	if (OutAxisA.IsNearlyZero()) { OutAxisA = FVector::ForwardVector; }
	if (OutAxisB.IsNearlyZero() || FMath::Abs(OutAxisB | OutAxisA) > 0.99) { OutAxisB = FVector::CrossProduct(OutAxisA, FVector::UpVector).GetSafeNormal(); }
	if (OutAxisB.IsNearlyZero()) { OutAxisB = FVector::CrossProduct(OutAxisA, FVector::ForwardVector).GetSafeNormal(); }
	OutAxisN = FVector::CrossProduct(OutAxisA, OutAxisB).GetSafeNormal();

	// Проход 3: ПОЛУ-ГАБАРИТЫ В НАЙДЕННЫХ ОСЯХ. Именно габариты, а не корень из разброса: владелец
	// задаёт РАЗМЕР ЛАМПЫ, и «плоскость по краям геометрии» он проверит глазами, а «две сигмы» — нет.
	double HA = 0.0, HB = 0.0, HN = 0.0;
	for (int32 k = 0; k < N; ++k)
	{
		const FVector D = Pos(k) - OutCenter;
		HA = FMath::Max(HA, FMath::Abs(D | OutAxisA));
		HB = FMath::Max(HB, FMath::Abs(D | OutAxisB));
		HN = FMath::Max(HN, FMath::Abs(D | OutAxisN));
	}
	OutHalf = FVector(HA, HB, HN);
	return true;
}
