// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ V ТЕКСТУРЫ ПЛОСКИХ КВАДОВ ЭФФЕКТОВ (05.11.2026): `asda.SelfTest.FlatQuadUv <jeb…> [shot=<папка>] [quit]`. [ПОРТ]
//   Эталон: таблица вершин `CEmitter_Type1_ctor@0x1208B40` + `effectQuad_updateAndDrawBillboard@0x120AEB0` — край z=−size
//   плоского квада 0xB/0x18 получает ВЕРХ текстуры (v=0). Ключи `asda.Fx.FlatQuadUvEtalon` (все прочие квады) и
//   `asda.Fx.AttachQuadUvEtalon` (эффекты на кости). Офлайн, карта мира, свой персонаж (4003).
//   Каждый эффект ставится дважды: в мир перед персонажем (без носителя) и на персонажа (attach по позиции — ветка π-«швартовки»),
//   снимается при ключе 0 (как было) и 1 (эталон). Проверка — по вершинам секций: у каждой плоской секции вершины 0/1 (край
//   z=−size до вращений) при ключе 1 несут min V ячейки, при ключе 0 — max V; позиции вершин при смене ключа не меняются.
//   Аргумент `map:<часть имени>` — эффект, уже стоящий на карте (актор уровня): персонаж ставится рядом, та же проверка и кадры.
//   Итог «КВАД-V ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaEffectActor.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "Test/AsdaSelfTestKit.h"
#include "AsdaCVarSave.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры
#include "EngineUtils.h"     // TActorIterator — эффекты, уже стоящие на карте (`map:<часть имени>`)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaQuadUv, Log, All);

namespace AsdaQuadUvTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.FlatQuadUv");   // имя команды
	static const TCHAR* kKeyFlat = TEXT("asda.Fx.FlatQuadUvEtalon");   // ключ прочих квадов
	constexpr int32 kMe = 4321;                                     // свой userNum (4003)

	using FStep = TFunction<bool(float)>;   // шаг сценария: true — завершён

	// Состояние прогона.
	struct FState
	{
		TWeakObjectPtr<UWorld> W;                     // мир
		bool bQuit = false;                           // `quit` в конце
		FString ShotDir;                              // папка кадров
		int32 Ok = 0, Fail = 0;                       // счётчики
		TArray<FStep> Steps;                          // сценарий
		int32 Idx = 0;                                // текущий шаг
		double StepT0 = 0.0;                          // начало шага
		FDelegateHandle H;                            // подписка на тик мира
		TUniquePtr<FAsdaNetSelfScope> NetScope;       // свой номер — вернуть
		TUniquePtr<FAsdaCVarScope> CVars;             // ключи — вернуть
		TWeakObjectPtr<UAsdaNetSubsystem> Net;        // сеть
		TWeakObjectPtr<UAsdaWorldSubsystem> WS;       // мир
		TArray<FString> Jebs;                         // эффекты
		TWeakObjectPtr<AAsdaEffectActor> FxWorld;     // текущий эффект в мире
		TWeakObjectPtr<AAsdaEffectActor> FxAtt;       // текущий эффект на персонаже
		TArray<FVector> PosKey1;                      // позиции вершин при ключе 1 (сверка «геометрия не меняется»)
		float OrigArm = -1.f;                         // рычаг камеры до прогона
		double SpawnT = 0.0;                          // время спавна текущего эффекта (мир)
		float PeakT = 0.f;                            // фаза максимальной видимости плоских секций (с от спавна)
		float PeakA = -1.f;                           // её видимость
		TArray<FVector> PosKey0;                      // вершины плоских секций в кадре ключа 0 (та же фаза при ключе 1)

		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaQuadUv, Log,   TEXT("КВАД-V ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaQuadUv, Error, TEXT("КВАД-V FAIL %s"), *What); }
		}
	};

	static AAsdaPlayerCharacter* MeOf(const FState& S)
	{
		const UAsdaWorldSubsystem* WS = S.WS.Get();
		return WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
	}

	// Плоские секции эффекта (0xB/0x18, одиночный квад, не биллборд): сколько их, у скольких V как у эталона/как было; позиции вершин.
	static void Scan(const AAsdaEffectActor* Fx, int32& OutN, int32& OutEtalon, int32& OutOld, TArray<FVector>* OutPos)
	{
		OutN = OutEtalon = OutOld = 0;
		const UProceduralMeshComponent* Pmc = Fx ? Fx->FindComponentByClass<UProceduralMeshComponent>() : nullptr;
		if (!Pmc) { return; }
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			const FAsdaFxEmitter& E = Fx->Emitters[i];
			if (!((E.JebType == 0x0B || E.JebType == 0x18) && E.bSingleQuad && E.BillboardMode == 0 && !E.bRing)) { continue; }
			const FProcMeshSection* Sec = const_cast<UProceduralMeshComponent*>(Pmc)->GetProcMeshSection(i);
			if (!Sec || Sec->ProcVertexBuffer.Num() < 4) { continue; }
			const float V0 = Sec->ProcVertexBuffer[0].UV0.Y, V2 = Sec->ProcVertexBuffer[2].UV0.Y;
			if (FMath::IsNearlyEqual(V0, V2)) { continue; }
			++OutN;
			(V0 < V2 ? OutEtalon : OutOld) += 1;
			if (OutPos) { for (const FProcMeshVertex& V : Sec->ProcVertexBuffer) { OutPos->Add(V.Position); } }
		}
	}

	// Видимость плоских секций сейчас: сумма альфы вершин (цвет вершины = альфа узла трека .jeb × множитель эффекта).
	static float FlatAlpha(const AAsdaEffectActor* Fx)
	{
		const UProceduralMeshComponent* Pmc = Fx ? Fx->FindComponentByClass<UProceduralMeshComponent>() : nullptr;
		if (!Pmc) { return 0.f; }
		float Sum = 0.f;
		for (int32 i = 0; i < Fx->Emitters.Num(); ++i)
		{
			const FAsdaFxEmitter& E = Fx->Emitters[i];
			if (!((E.JebType == 0x0B || E.JebType == 0x18) && E.bSingleQuad && E.BillboardMode == 0 && !E.bRing)) { continue; }
			const FProcMeshSection* Sec = const_cast<UProceduralMeshComponent*>(Pmc)->GetProcMeshSection(i);
			if (!Sec) { continue; }
			for (const FProcMeshVertex& V : Sec->ProcVertexBuffer) { Sum += V.Color.A / 255.f; }
		}
		return Sum;
	}

	static void Shot(const FState& S, const FString& Name)
	{
		if (S.ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(S.ShotDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaQuadUv, Log, TEXT("КВАД-V кадр %s"), *Path);
	}

	static void Finish(const TSharedRef<FState>& SP)
	{
		FState& S = *SP;
		FWorldDelegates::OnWorldPostActorTick.Remove(S.H);
		if (AAsdaEffectActor* F = S.FxWorld.Get(); F && F->GetOwner() != nullptr) { F->Destroy(); }   // эффект карты не сносим
		if (AAsdaEffectActor* F = S.FxAtt.Get()) { F->Destroy(); }
		if (AAsdaPlayerCharacter* Me = MeOf(S); Me && Me->CameraArm && S.OrigArm > 0.f) { Me->CameraArm->TargetArmLength = S.OrigArm; }
		S.CVars.Reset();
		S.NetScope.Reset();
		if (UAsdaNetSubsystem* N = S.Net.Get(); N && MeOf(S) && !N->IsSelfUserNumKnown()) { N->SetSelfUserNumForDebug(N->SelfUserNum); }
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.bQuit, LogAsdaQuadUv, TEXT("КВАД-V"), S.Ok, S.Fail);
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
			GEngine->Exec(S->W.Get(), TEXT("Asda.Load.CurrentMap 3"));
			TArray<uint8> B; B.SetNumZeroed(9);
			AsdaBytes::WrI16(B.GetData(), (int16)kMe); AsdaBytes::WrI16(B.GetData() + 2, 256); AsdaBytes::WrI16(B.GetData() + 4, 256); B[8] = 1;
			S->Net->DebugFeedPacket(4003, B);
		}));
		S->Steps.Add(Wait(4.0f));
		S->Steps.Add(Do([S]()
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			S->Check(Me != nullptr, FString::Printf(TEXT("P0 павн %s, эффектов %d"), Me ? TEXT("есть") : TEXT("НЕТ"), S->Jebs.Num()));
			if (Me && Me->CameraArm) { S->OrigArm = Me->CameraArm->TargetArmLength; Me->CameraArm->TargetArmLength = 420.f; }
		}));
		// ★ КАДР В ПИКЕ ВИДИМОСТИ (05.11, просьба координатора): фаза берётся не фиксированной задержкой, а по самим данным .jeb —
		//   прогон эффекта меряет сумму альфы плоских секций (альфа узлов трека) каждый кадр и запоминает время максимума; затем
		//   эффект ставится заново, доводится до этой фазы и ЗАМОРАЖИВАЕТСЯ (CustomTimeDilation 0). Кадры ключа 0 и 1 — одной фазы:
		//   вершины обоих кадров сверяются (геометрия не должна меняться, меняется только V).
		auto FindMap = [](FState* S, const FString& Part) -> AAsdaEffectActor*
		{
			for (TActorIterator<AAsdaEffectActor> It(S->W.Get()); It; ++It)
			{
				// Плоские эмиттеры смотрим по ДАННЫМ (секции у далёкого эффекта гейт видимости не строит).
				bool bFlat = false;
				for (const FAsdaFxEmitter& E : It->Emitters)
				{
					bFlat |= (E.JebType == 0x0B || E.JebType == 0x18) && E.bSingleQuad && E.BillboardMode == 0 && !E.bRing;
				}
				if (It->GetOwner() == nullptr && It->EffectName.Contains(Part) && bFlat) { return *It; }
			}
			return nullptr;
		};
		auto Spawn = [](FState* S, int32 ji) -> AAsdaEffectActor*
		{
			AAsdaPlayerCharacter* Me = MeOf(*S);
			if (!Me) { return nullptr; }
			const FVector At = Me->GetActorLocation() + Me->GetActorForwardVector() * 120.f;
			return AAsdaEffectActor::SpawnByJebName(S->W.Get(), S->Jebs[ji], At, FRotator::ZeroRotator, Me);
		};
		for (int32 ji = 0; ji < S->Jebs.Num(); ++ji)
		{
			const bool bMapFx = S->Jebs[ji].StartsWith(TEXT("map:"));
			const FString Tag = S->Jebs[ji].Replace(TEXT(":"), TEXT("_"));
			// 1) Поиск фазы пика: эффект идёт сам, 4 с (у карты — вечный), меряем видимость каждый кадр.
			S->Steps.Add(Do([S, ji, bMapFx, FindMap, Spawn]()
			{
				if (AAsdaEffectActor* F = S->FxWorld.Get(); F && F->GetOwner() != nullptr) { F->Destroy(); }
				S->FxWorld = nullptr; S->PeakA = -1.f; S->PeakT = 0.f;
				S->CVars->Set(kKeyFlat, 0);
				AAsdaPlayerCharacter* Me = MeOf(*S);
				if (!Me) { return; }
				AAsdaEffectActor* Fx = nullptr;
				if (bMapFx)
				{
					Fx = FindMap(S, S->Jebs[ji].Mid(4));
					if (Fx)
					{
						Fx->CustomTimeDilation = 1.f;
						Me->SetActorLocation(Fx->GetActorLocation() + FVector(260.f, 0.f, 120.f));
						UE_LOG(LogAsdaQuadUv, Log, TEXT("КВАД-V эффект карты %s (%s) у %s"), *Fx->EffectName, *Fx->GetName(), *Fx->GetActorLocation().ToString());
					}
				}
				else { Fx = Spawn(S, ji); }
				S->FxWorld = Fx;
				S->SpawnT = S->W->GetTimeSeconds();
			}));
			S->Steps.Add(FStep([S](float T)
			{
				if (AAsdaEffectActor* Fx = S->FxWorld.Get())
				{
					const float A = FlatAlpha(Fx);
					if (A > S->PeakA) { S->PeakA = A; S->PeakT = (float)(S->W->GetTimeSeconds() - S->SpawnT); }
				}
				return T >= 4.0f || !S->FxWorld.IsValid();
			}));
			// 2) Заново (карта — ждём ту же фазу следующего периода нельзя гарантировать: доводим заново поставленный эффект
			//    только для навыков; эффект карты замораживаем сразу в ближайший пик ≥ 90 % найденного).
			S->Steps.Add(Do([S, ji, bMapFx, Spawn]()
			{
				UE_LOG(LogAsdaQuadUv, Log, TEXT("КВАД-V %s: пик видимости плоских секций %.2f в %.2f с"), *S->Jebs[ji], S->PeakA, S->PeakT);
				if (bMapFx) { return; }
				if (AAsdaEffectActor* F = S->FxWorld.Get()) { F->Destroy(); }
				S->FxWorld = Spawn(S, ji);
				S->SpawnT = S->W->GetTimeSeconds();
			}));
			S->Steps.Add(FStep([S, bMapFx](float T)
			{
				AAsdaEffectActor* Fx = S->FxWorld.Get();
				if (!Fx) { return true; }
				const bool bReach = bMapFx ? (FlatAlpha(Fx) >= 0.9f * S->PeakA || T > 6.f)
				                          : ((float)(S->W->GetTimeSeconds() - S->SpawnT) >= S->PeakT);
				if (bReach) { Fx->CustomTimeDilation = 0.f; }
				return bReach;
			}));
			S->Steps.Add(Wait(0.25f));
			S->Steps.Add(Do([S, Tag]()
			{
				AAsdaEffectActor* Fx = S->FxWorld.Get();
				int32 N = 0, Et = 0, Old = 0;
				S->PosKey0.Reset();
				Scan(Fx, N, Et, Old, &S->PosKey0);
				S->Check(Fx && N > 0 && Old == N, FString::Printf(TEXT("%s ключ 0 (как было), пик %.2f: плоских секций %d, V как было %d"), *Tag, FlatAlpha(Fx), N, Old));
				Shot(*S, Tag + TEXT("_key0"));
			}));
			S->Steps.Add(Wait(0.3f));
			S->Steps.Add(Do([S]() { S->CVars->Set(kKeyFlat, 1); }));
			S->Steps.Add(Wait(0.3f));
			S->Steps.Add(Do([S, Tag]()
			{
				AAsdaEffectActor* Fx = S->FxWorld.Get();
				int32 N = 0, Et = 0, Old = 0;
				TArray<FVector> Pos;
				Scan(Fx, N, Et, Old, &Pos);
				double MaxD = 0.0;
				const bool bSameN = Pos.Num() == S->PosKey0.Num();
				for (int32 k = 0; bSameN && k < Pos.Num(); ++k) { MaxD = FMath::Max(MaxD, (double)FVector::Dist(Pos[k], S->PosKey0[k])); }
				S->Check(Fx && N > 0 && Et == N && bSameN && MaxD < 0.5,
					FString::Printf(TEXT("%s ключ 1 (эталон 0x1208B40), та же фаза: плоских секций %d, V как в эталоне %d, сдвиг вершин %.2f"),
						*Tag, N, Et, MaxD));
				Shot(*S, Tag + TEXT("_key1"));
			}));
			S->Steps.Add(Wait(0.3f));
			S->Steps.Add(Do([S]() { if (AAsdaEffectActor* F = S->FxWorld.Get()) { F->CustomTimeDilation = 1.f; } }));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestFlatQuadUv(
	TEXT("asda.SelfTest.FlatQuadUv"),
	TEXT("Самотест V текстуры плоских квадов эффектов (эталон 0x1208B40): эффекты в мире и на персонаже, ключ asda.Fx.FlatQuadUvEtalon 0/1, "
	     "кадры до/после. `asda.SelfTest.FlatQuadUv <jeb…> [shot=<папка>] [quit]`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaQuadUvTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (!Net || !WS) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		TSharedRef<FState> SP = MakeShared<FState>();
		SP->W = W; SP->bQuit = KA.bQuit; SP->Net = Net; SP->WS = WS;
		for (const FString& P : KA.Pos)
		{
			if (P.StartsWith(TEXT("shot="), ESearchCase::IgnoreCase))
			{
				SP->ShotDir = FPaths::ConvertRelativePathToFull(P.Mid(5));
				IFileManager::Get().MakeDirectory(*SP->ShotDir, /*Tree*/true);
			}
			else { SP->Jebs.Add(P); }
		}
		SP->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		SP->CVars = MakeUnique<FAsdaCVarScope>(W, kCmd);
		Build(SP);
		SP->StepT0 = W->GetTimeSeconds();
		SP->H = FWorldDelegates::OnWorldPostActorTick.AddLambda([SP](UWorld* InW, ELevelTick, float) { Tick(SP, InW); });
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
