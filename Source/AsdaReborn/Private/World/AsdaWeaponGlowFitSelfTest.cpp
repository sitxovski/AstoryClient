// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ПОСАДКИ СВЕЧЕНИЯ ЗАТОЧКИ ПО ДЛИНЕ ОРУЖИЯ (05.11.2026): `asda.SelfTest.WeaponGlowFit [all] [коды…] [shot=<папка>] [quit]`. [ПОРТ]
//   Жалоба игроков: «розовая аура заточки на Stalker's Blade (и Tachyon Blade, и многих других) непропорциональна, сдвинута
//   вдоль клинка, выходит за клинок и уходит в землю».
//   Эталон — `CCharacter_UpdateHeldItemEffect@0xA4C070` (матрица = живая кость "8_a1" × мир персонажа, своего масштаба нет)
//   и `effectQuad_updateAndDrawBillboard@0x120AEB0` (квад = ±size узла трека, pos-канал — в пространстве кости).
//   Путь игрока (офлайн, `-AsdaNoNet`): спавн 4003, надевание — ячейка контейнера 3 с заточкой в байте +24 и общая точка ответа 5001
//   (`ApplySelfEquipPart`). Для каждой пары «модель оружия + .jeb свечения» (`all` — все такие пары таблицы предметов; иначе —
//   заданные коды) и ступеней +5/+6/+7 меряется ТО, ЧТО РИСУЕТСЯ:
//     • вершины каждой секции квадов переводятся обратно в сырые game-оси .jeb той же матрицей, что ставит эффект
//       (`ComputeFollowBoneMatrix`), и сравниваются с авторскими числами узла (центр pos-канала и полу-экстент вдоль оси оружия);
//     • все вершины свечения проецируются на ось меша оружия (от хвата к острию): докуда дотягивается свечение против длины меша;
//     • отклонение оси луча от оси меша.
//   Строки «СВЕЧЕНИЕ-ПОСАДКА» по каждой паре и итог «ПОСАДКА ИТОГ: ok=N fail=M». Кадры `shot=` — для заданных кодов.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaTestPackets.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "AsdaFxNames.h"
#include "Characters/AsdaCharAssembly.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры

DEFINE_LOG_CATEGORY_STATIC(LogAsdaGlowFit, Log, All);

namespace AsdaGlowFitTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.WeaponGlowFit");   // имя команды
	constexpr int32 kMe = 4321;                                        // свой userNum (4003)

	// Одна проверяемая пара: предмет-представитель, модель, база .jeb без ступени.
	struct FCase
	{
		int32 Code = 0;        // код предмета
		FString Mesh;          // имя меша оружия
		FString JebBase;       // имя .jeb без «_0N»
		bool bShot = false;    // снять кадр (заданный код)
	};

	// Шаг сценария: true — шаг завершён.
	using FStep = TFunction<bool(float)>;

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров
		int32 Ok = 0, Fail = 0;                       // счётчики проверок
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TWeakObjectPtr<UAsdaItemTableSubsystem> Items;// предметы
		TWeakObjectPtr<UAsdaInventorySubsystem> Inv;  // инвентарь
		TArray<FCase> Cases;                          // пары
		int32 OrigWeaponCell = 0;                     // надетое в 3/9 до прогона
		float OrigArm = -1.f;                         // длина рычага камеры до прогона
		FRotator OrigRot = FRotator::ZeroRotator;     // поворот своего актёра до прогона
		int32 NMismatch = 0;                          // пар, где рисуемое расходится с .jeb (дефект порта)
		int32 NOvershoot = 0;                         // пар, где свечение по данным длиннее меша (замысел .jeb)
		FString Boost;                                // `boost=X` — сила свечения эффекта для кадров (сравнение яркости), пусто — штатно

		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaGlowFit, Log,   TEXT("ПОСАДКА ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaGlowFit, Error, TEXT("ПОСАДКА FAIL %s"), *What); }
		}
	};

	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Тело 4003 (9 б).
	using AsdaTestPackets::Body4003;   // ★ R5-c (12.11.2026): общая точка (`AsdaTestPackets.h`) — копия была дословной.

	// Надеть путём игрока: инстанс в контейнере 3 с заточкой +24 и общая точка ответа 5001.
	static void WearGraded(FState& S, int32 ItemId, int32 Grade)
	{
		if (UAsdaInventorySubsystem* Inv = S.Inv.Get(); Inv && Inv->DebugPutItem(3, 9, ItemId, 1))
		{
			if (FAsdaItemInstance* C = Inv->GetCellMutable(3, 9)) { C->Raw[24] = (uint8)Grade; C->SyncFromRaw(); }
		}
		if (UAsdaWorldSubsystem* WS = S.WS.Get()) { WS->ApplySelfEquipPart(9, ItemId, Grade); }
	}

	// Меш оружия своего персонажа.
	static UStaticMeshComponent* WeaponComp(AActor* A)
	{
		if (!A) { return nullptr; }
		TInlineComponentArray<UStaticMeshComponent*> Comps(A);
		for (UStaticMeshComponent* C : Comps)
		{
			if (IsValid(C) && C->ComponentHasTag(UAsdaCharAssembly::WeaponMeshTag) && C->GetStaticMesh()) { return C; }
		}
		return nullptr;
	}

	// Ось меша оружия в мире (самая длинная локальная ось, от хвата к дальнему торцу) и длины по ней в мире.
	static bool WeaponAxis(const UStaticMeshComponent* WC, FVector& OutGrip, FVector& OutAxis, double& OutBack, double& OutTip)
	{
		const FBox LB = WC->GetStaticMesh()->GetBoundingBox();
		const FVector Ext = LB.Max - LB.Min;
		const int32 C = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : ((Ext.Y >= Ext.Z) ? 1 : 2);
		const double Lo = LB.Min[C], Hi = LB.Max[C];
		const bool bNeg = FMath::Abs(Lo) > FMath::Abs(Hi);
		FVector Dir(0, 0, 0); Dir[C] = bNeg ? -1.0 : 1.0;
		const FTransform WT = WC->GetComponentTransform();
		const double Sc = FMath::Abs(WT.GetScale3D()[C]);
		OutGrip = WT.GetLocation();
		OutAxis = WT.TransformVector(Dir).GetSafeNormal();
		OutTip = (bNeg ? -Lo : Hi) * Sc;          // от хвата до острия
		OutBack = (bNeg ? -Hi : Lo) * Sc;         // от хвата до навершия (отрицательно)
		return !OutAxis.IsNearlyZero();
	}

	// Итог замера одной ступени.
	struct FMeasure
	{
		bool bHave = false;          // свечение и меш есть
		FString Jeb;                 // имя эффекта
		double MeshBack = 0, MeshTip = 0;   // меш вдоль оси (мир, от хвата)
		double GlowLo = 0, GlowHi = 0;      // квады свечения вдоль оси меша (мир)
		double AxisDev = 0;          // отклонение оси луча от оси меша, градусы
		int32 NQuad = 0;             // секций-квадов с осевой меркой
		int32 NBad = 0;              // секций, где рисуемое ≠ .jeb
		double MaxDz = 0;            // наибольшее расхождение по оси (game-ед.)
		FString BadList;             // имена расходящихся секций
		double JebHi = -1e9;         // по .jeb: дальний край квадов вдоль оси (game-ед.)
		double RawTip = 0;           // острие меша в game-ед. (мир / масштаб кости)
		int32 NUv = 0;               // секций с проверкой стороны текстуры вдоль оси
		int32 NUvBad = 0;            // из них: верх текстуры не на том крае, что в эталоне (0x1208B40)
	};

	static FMeasure Measure(FState& S)
	{
		FMeasure M;
		AAsdaPlayerCharacter* Me = MeOf(S);
		UStaticMeshComponent* WC = WeaponComp(Me);
		if (!Me || !WC) { return M; }
		FVector Grip, Axis; double Back = 0, Tip = 0;
		if (!WeaponAxis(WC, Grip, Axis, Back, Tip)) { return M; }
		AAsdaEffectActor* Fx = nullptr;
		TArray<AActor*> Att; Me->GetAttachedActors(Att);
		for (AActor* A : Att)
		{
			if (AAsdaEffectActor* E = Cast<AAsdaEffectActor>(A); E && E->ActorHasTag(FName(AsdaCharFxTags::kWeaponGlow))) { Fx = E; break; }
		}
		FMatrix A;
		if (!Fx || !Fx->ComputeFollowBoneMatrix(A)) { return M; }
		const FMatrix Ainv = A.Inverse();
		const double WorldPerGame = A.GetScaleVector().GetMax() > 0 ? A.GetScaleVector().X : 1.0;   // мир-ед. на game-ед. по оси
		UProceduralMeshComponent* Pmc = Fx->FindComponentByClass<UProceduralMeshComponent>();
		if (!Pmc) { return M; }
		M.bHave = true; M.Jeb = Fx->EffectName; M.MeshBack = Back; M.MeshTip = Tip;
		M.RawTip = Tip / FMath::Max(1e-3, WorldPerGame);
		M.AxisDev = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp<double>(
			FVector::DotProduct(Fx->GetActorQuat().RotateVector(FVector(-1, 0, 0)).GetSafeNormal(), Axis), -1.0, 1.0)));
		M.GlowLo = 1e9; M.GlowHi = -1e9;
		auto ToGame = [](const FVector& U) { return FVector(U.Y, U.Z, -U.X); };
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			const FAsdaFxEmitter& E = Fx->Emitters[i];
			const FProcMeshSection* Sec = Pmc->GetProcMeshSection(i);
			if (!Sec || Sec->ProcVertexBuffer.Num() == 0 || E.bRing || E.Nodes.Num() == 0) { continue; }
			const bool bQuadType = (E.JebType == 0x0B || E.JebType == 0x18) && E.bSingleQuad;
			if (!bQuadType) { continue; }
			double Zlo = 1e9, Zhi = -1e9;
			double ZAtVmin = 0, VMin = 1e9, ZAtVmax = 0, VMax = -1e9;   // game-z вершин с верхом и низом текстуры
			for (const FProcMeshVertex& V : Sec->ProcVertexBuffer)
			{
				const double T = FVector::DotProduct(V.Position - Grip, Axis);
				M.GlowLo = FMath::Min(M.GlowLo, T); M.GlowHi = FMath::Max(M.GlowHi, T);
				const FVector G = ToGame(Ainv.TransformPosition(V.Position));
				Zlo = FMath::Min(Zlo, G.Z); Zhi = FMath::Max(Zhi, G.Z);
				if (V.UV0.Y < VMin - 1e-4) { VMin = V.UV0.Y; ZAtVmin = G.Z; }
				if (V.UV0.Y > VMax + 1e-4) { VMax = V.UV0.Y; ZAtVmax = G.Z; }
			}
			// Авторское по .jeb: квад без канальных вращений лежит в плоскости game-XZ, вдоль оси оружия (game Z) — ±SizeY,
			//   при roll 90/270 — ±SizeX; центр — pos-канал. Узел 0 (у свечения трек постоянный).
			const FAsdaFxNode& N = E.Nodes[0];
			if (!N.RingRot.IsNearlyZero(0.5f)) { continue; }   // повернутые каналами — только габарит выше
			// Трек может менять размер/позицию (пульсация): ожидание — объединение по всем узлам.
			double NodeLo = 1e9, NodeHi = -1e9;
			const float RollMod = FMath::Fmod(FMath::Abs(N.RingRoll), 180.f);
			const bool bRoll90 = FMath::IsNearlyEqual(RollMod, 90.f, 1.f);
			if (!bRoll90 && !FMath::IsNearlyZero(RollMod, 1.f)) { continue; }
			const double Half = bRoll90 ? N.SizeX : N.SizeY;
			// Эталон 0x1208B40: до roll край z=−size — верх текстуры (v мин.); roll 180 переворачивает край. При roll 90 ось V поперёк — не меряем.
			if (!bRoll90 && VMax > VMin)
			{
				const bool bRoll180 = FMath::IsNearlyEqual(FMath::Fmod(FMath::Abs(N.RingRoll), 360.f), 180.f, 1.f);
				const bool bTopAtLow = ZAtVmin < ZAtVmax;   // верх текстуры у меньшего z
				++M.NUv;
				if (bTopAtLow == bRoll180) { ++M.NUvBad; M.BadList += FString::Printf(TEXT(" %s[V перевёрнут]"), *E.Name); }
			}
			for (const FAsdaFxNode& Nk : E.Nodes)
			{
				const double Hk = bRoll90 ? Nk.SizeX : Nk.SizeY;
				const double Ck = Nk.RingPosOfs.Z + Nk.RingBasePos.Z;
				NodeLo = FMath::Min(NodeLo, Ck - Hk); NodeHi = FMath::Max(NodeHi, Ck + Hk);
			}
			const double ExpLo = NodeLo, ExpHi = NodeHi;
			M.JebHi = FMath::Max(M.JebHi, ExpHi);
			++M.NQuad;
			// Текущий кадр трека лежит ВНУТРИ объединения узлов; за его пределы рисуемое выходить не должно.
			const double Dz = FMath::Max(FMath::Max(0.0, ExpLo - Zlo), FMath::Max(0.0, Zhi - ExpHi));
			if (Dz > FMath::Max(3.0, 0.05 * Half * 2.0))
			{
				++M.NBad; M.BadList += FString::Printf(TEXT(" %s[z %.0f..%.0f, .jeb %.0f..%.0f]"), *E.Name, Zlo, Zhi, ExpLo, ExpHi);
			}
			M.MaxDz = FMath::Max(M.MaxDz, Dz);
		}
		if (M.GlowLo > M.GlowHi) { M.GlowLo = M.GlowHi = 0; }
		return M;
	}

	// Камера сбоку на оружие (кадр).
	static void Aim(FState& S)
	{
		UWorld* W = S.W.Get();
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		AAsdaPlayerCharacter* Me = MeOf(S);
		if (!PC || !Me) { return; }
		if (Me->CameraArm) { Me->CameraArm->TargetArmLength = 260.f; }
		Me->SetActorRotation(FRotator(0.f, PC->GetControlRotation().Yaw + 180.f + 75.f, 0.f));
	}
	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaGlowFit, Log, TEXT("ПОСАДКА кадр %s"), *Path);
	}

	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (AAsdaPlayerCharacter* Me = MeOf(S))
		{
			if (Me->CameraArm && S.OrigArm > 0.f) { Me->CameraArm->TargetArmLength = S.OrigArm; }
			Me->SetActorRotation(S.OrigRot);
		}
		WearGraded(S, S.OrigWeaponCell, 0);
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get(); N && MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		UE_LOG(LogAsdaGlowFit, Log, TEXT("ПОСАДКА сводка: пар %d, расхождение рисуемого с .jeb %d, свечение по данным длиннее меша %d"),
			S.Cases.Num(), S.NMismatch, S.NOvershoot);
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaGlowFit, TEXT("ПОСАДКА"), S.Ok, S.Fail);
	}

	static void Tick(const TSharedRef<FState>& InSP, UWorld* InW)
	{
		const TSharedRef<FState> SP = InSP;
		FState& S = *SP;
		UWorld* W = S.W.Get();
		if (!W) { FWorldDelegates::OnWorldPostActorTick.Remove(S.H); return; }
		if (InW != W) { return; }
		while (S.Steps.IsValidIndex(S.Idx))
		{
			const float T = (float)(W->GetTimeSeconds() - S.StepT0);
			if (!S.Steps[S.Idx](T)) { return; }
			++S.Idx;
			S.StepT0 = W->GetTimeSeconds();
		}
		Finish(SP);
	}

	static void Build(const TSharedRef<FState>& SP)
	{
		FState* S = &SP.Get();
		auto Do = [](TFunction<void()> Fn) { return FStep([Fn](float) { Fn(); return true; }); };
		auto Wait = [](float Sec) { return FStep([Sec](float T) { return T >= Sec; }); };
		S->Steps.Add(Do([S]()
		{
			if (const UAsdaInventorySubsystem* Inv = S->Inv.Get())
			{
				const FAsdaItemInstance* C9 = Inv->GetCell(3, 9);
				S->OrigWeaponCell = (C9 && !C9->IsEmpty()) ? C9->ItemCode : 0;
			}
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			S->Net->DebugFeedPacket(4003, Body4003(kMe));
		}));
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me && S->Cases.Num() > 0, FString::Printf(TEXT("P0 павн %s, пар модель+.jeb: %d"), Me ? TEXT("есть") : TEXT("НЕТ"), S->Cases.Num()));
			if (Me) { S->OrigArm = Me->CameraArm ? Me->CameraArm->TargetArmLength : -1.f; S->OrigRot = Me->GetActorRotation(); }
		}));
		for (int32 ci = 0; ci < S->Cases.Num(); ++ci)
		{
			for (const int32 Grade : { 5, 6, 7 })
			{
				S->Steps.Add(Do([S, ci, Grade]() { WearGraded(*S, S->Cases[ci].Code, Grade); if (S->Cases[ci].bShot) { Aim(*S); } }));
				S->Steps.Add(Wait(0.2f));
				S->Steps.Add(Do([S, ci]()
				{
					if (S->Boost.IsEmpty() || !S->Cases[ci].bShot) { return; }
					GEngine->Exec(S->W.Get(), *FString::Printf(TEXT("asda.Fx.Glow.Set %s override 1"), *S->Cases[ci].JebBase));
					GEngine->Exec(S->W.Get(), *FString::Printf(TEXT("asda.Fx.Glow.Set %s boost %s"), *S->Cases[ci].JebBase, *S->Boost));
				}));
				S->Steps.Add(Wait(S->Cases[ci].bShot ? 0.9f : 0.2f));
				S->Steps.Add(Do([S, ci, Grade]()
				{
					const FCase& C = S->Cases[ci];
					const FMeasure M = Measure(*S);
					if (!M.bHave && C.JebBase.Contains(TEXT("_le_")))
					{
						// Редкость 4 (le): .jeb свечения нет ни в одной сборке клиента — у оригинала ауры нет (weapon_glow_re.md §E).
						S->Check(true, FString::Printf(TEXT("%d +%d %s «%s»: ауры нет, как у оригинала (.jeb редкости le в данных нет)"), C.Code, Grade, *C.Mesh, *C.JebBase));
						return;
					}
					if (!M.bHave)
					{
						S->Check(false, FString::Printf(TEXT("%d +%d %s: свечения или меша нет"), C.Code, Grade, *C.Mesh));
						return;
					}
					// Луки/арбалеты/баллисты: свечение по замыслу .jeb лежит вдоль ПЛЕЧ (±z), а самая длинная ось меша — ложе/плечи
					//   в другую сторону; ось для них не критерий (рисуемое против .jeb сверяется так же).
					const bool bRanged = M.Jeb.StartsWith(TEXT("bow_")) || M.Jeb.StartsWith(TEXT("xbow_")) || M.Jeb.StartsWith(TEXT("bali_"));
					const bool bFaithful = M.NBad == 0 && M.NUvBad == 0 && (bRanged || M.AxisDev <= 12.0);
					if (!bFaithful) { ++S->NMismatch; }
					if (M.GlowHi > M.MeshTip * 1.15) { ++S->NOvershoot; }
					UE_LOG(LogAsdaGlowFit, Log,
						TEXT("СВЕЧЕНИЕ-ПОСАДКА\t%d\t+%d\t%s\t%s\tмеш %.0f..%.0f\tсвечение %.0f..%.0f\tза острием %+.0f (%.0f%%)\tось %.1f°\tквадов %d\tрасхождений %d (макс %.1f)\tV неверно %d/%d%s\t.jeb до %.0f, острие %.0f game-ед."),
						C.Code, Grade, *C.Mesh, *M.Jeb, M.MeshBack, M.MeshTip, M.GlowLo, M.GlowHi, M.GlowHi - M.MeshTip,
						M.MeshTip > 0 ? 100.0 * (M.GlowHi - M.MeshTip) / M.MeshTip : 0.0, M.AxisDev, M.NQuad, M.NBad, M.MaxDz, M.NUvBad, M.NUv, *M.BadList,
						M.JebHi, M.RawTip);
					S->Check(bFaithful, FString::Printf(TEXT("%d +%d %s «%s»: рисуемое = .jeb (квадов %d, расхождений %d, V неверно %d из %d, ось %.1f°)"),
						C.Code, Grade, *C.Mesh, *M.Jeb, M.NQuad, M.NBad, M.NUvBad, M.NUv, M.AxisDev));
					if (C.bShot) { Shot(*S, FString::Printf(TEXT("fit_%d_%d%s"), C.Code, Grade, S->Boost.IsEmpty() ? TEXT("") : *(TEXT("_b") + S->Boost))); }
				}));
				if (S->Cases[ci].bShot) { S->Steps.Add(Wait(0.4f)); }
			}
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWeaponGlowFit(
	TEXT("asda.SelfTest.WeaponGlowFit"),
	TEXT("Самотест посадки свечения заточки по длине оружия: каждая пара «модель + .jeb» (`all`) или заданные коды, ступени +5/+6/+7, "
	     "рисуемые квады против чисел .jeb и против длины меша. Офлайн, карта мира. `shot=<папка>` — кадры заданных кодов, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaGlowFitTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Net || !WS || !Items || !Inv) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS; SP->Items = Items; SP->Inv = Inv;
		TSet<int32> ShotCodes;
		bool bAll = false;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else if (P.Equals(TEXT("all"), ESearchCase::IgnoreCase)) { bAll = true; }
			else if (P.StartsWith(TEXT("boost="), ESearchCase::IgnoreCase)) { SP->Boost = P.Mid(6); }
			else if (P.IsNumeric()) { ShotCodes.Add(FCString::Atoi(*P)); }
		}
		auto AddCase = [&](int32 Code, bool bShot)
		{
			const FString Jeb = WS->BuildGlowJebName(Code, 7, false);
			if (Jeb.Len() < 4) { return false; }
			SP->Cases.Add({ Code, Items->GetItemMeshName(Code, true), Jeb.LeftChop(3), bShot });
			return true;
		};
		for (const int32 C : ShotCodes) { AddCase(C, true); }
		if (bAll)
		{
			TSet<FString> Seen;
			for (const FCase& C : SP->Cases) { Seen.Add(C.Mesh + TEXT("|") + C.JebBase); }
			for (const FAsdaItemRecord& R : Items->GetAllItems())
			{
				if (R.ItemType != 9) { continue; }
				const FString Jeb = WS->BuildGlowJebName(R.ItemCode, 7, false);
				const FString Mesh = Items->GetItemMeshName(R.ItemCode, true);
				if (Jeb.Len() < 4 || Mesh.IsEmpty()) { continue; }
				const FString Key = Mesh + TEXT("|") + Jeb.LeftChop(3);
				if (Seen.Contains(Key)) { continue; }
				Seen.Add(Key);
				SP->Cases.Add({ R.ItemCode, Mesh, Jeb.LeftChop(3), false });
			}
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
