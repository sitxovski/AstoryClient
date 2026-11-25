// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ШЛЕЙФ ТРАНСПОРТА ЗА СПИНОЙ» (08.11.2026): `asda.Mount.FxDirTest [out=<папка>] [quit]`.
//   Баг: полосы шлейфа квадроцикла идут впереди транспорта. Разбор — у ключа `asda.Fx.CharWorldStrict` (AsdaEffectActor.cpp).
//   Для каждого уникального эффекта езды (все записи special_itemlist подтипа 15 с шлейфом в DT_RideEffect, в том числе Fable 559–637) транспорт
//   садится тем же путём, что 5453, эффект спавнится единой точкой шлейфа (привязка 6) и через 0.5 с меряется центр его геометрии в осях хода:
//   ключ 1 — четыре направления, ключ 0 — одно. Смещение вдоль хода не должно зависеть от направления, а полосы с разворотом обязаны уйти назад (kFlipFx).
//   `frame=<код,код>` — кадры этих транспортов, плюс езда: персонаж едет в четыре стороны, эффект рождается на ходу. `fx=<имя,имя>` — другие эффекты привязки 6
//   на пешем персонаже, кадры с ключом 1 и 0. Кадры пишутся в `out=`. Эффект без собранного BP — строка ПРОПУСК, не провал.
//   Итог: «ШЛЕЙФ-НАПРАВЛЕНИЕ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "AsdaDataTableCache.h"
#include "AsdaEffectActor.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaWorldSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMountFxDir, Log, All);

namespace AsdaMountFxDir
{
	const TCHAR* const kCmd = TEXT("asda.Mount.FxDirTest");       // имя команды
	const TCHAR* const kPrefix = TEXT("ШЛЕЙФ-НАПРАВЛЕНИЕ");       // префикс строк
	const TCHAR* const kKey = TEXT("asda.Fx.CharWorldStrict");    // ключ правки
	const TCHAR* const kRideDT = TEXT("/Game/AsdaAssets/Data/DT_RideEffect.DT_RideEffect");   // таблица эффектов езды
	const float kRideSpeed = 500.f;                               // скорость езды в замере, см/с
	const float kSpreadMax = 60.f;                                // допуск разброса: частицы случайны, у 0037 разброс 36
	const TCHAR* const kFlipFx[] = { TEXT("itm1_n_96_vehicle_0012_e"), TEXT("itm1_n_96_vehicle_0013_e"), TEXT("itm1_n_96_vehicle_0014_e"), TEXT("itm1_n_96_vehicle_0022_e") };   // эффекты с полосами-квадами не биллбордами
	const FVector2D kDirs[4] = { FVector2D(1, 0), FVector2D(0, 1), FVector2D(-1, 0), FVector2D(0, -1) };   // направления хода (игра)

	// Один замер: эффект, код, ключ, направление, снимать ли кадр.
	struct FJob { FString Fx; int32 Code = 0; int32 Key = 1; int32 Dir = 0; bool bShot = false; bool bMove = false; };

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UWorld> W; bool bQuit = false; int32 Ok = 0, Fail = 0; FString Out; int32 Idx = 0; int32 Phase = 0;
		TArray<FJob> Jobs; TWeakObjectPtr<AAsdaEffectActor> E; FTimerHandle H; int32 Mounted = 0;
		TMap<FString, TArray<float>> Along;   // эффект → смещения вдоль хода при ключе 1
		TMap<FString, float> Old;             // эффект → смещение при ключе 0
		float Size = 0.f;
		FVector Home = FVector::ZeroVector;   // точка старта езды
		FTimerHandle Mv;                      // таймер езды
	};

	// Ось хода в осях UE: игра (x, z) → UE (−z, x).
	static FVector Fwd(const FVector2D& D) { return FVector(-D.Y, D.X, 0.f); }

	static void Measure(const TSharedRef<FRun>& R, AAsdaPlayerCharacter* Ch)
	{
		const FJob& J = R->Jobs[R->Idx];
		AAsdaEffectActor* E = R->E.Get();
		if (!E) { ++R->Fail; UE_LOG(LogAsdaMountFxDir, Error, TEXT("%s FAIL %s: эффект не создан"), kPrefix, *J.Fx); return; }
		const FBox B = E->GetComponentsBoundingBox(true);
		const FVector Off = (B.IsValid ? B.GetCenter() : E->GetActorLocation()) - Ch->GetActorLocation();
		const float Along = FVector::DotProduct(Off, Fwd(kDirs[J.Dir]));
		const float Size = B.IsValid ? (float)B.GetSize().Size() : 0.f;
		if (!J.bMove && J.Code != 0)
		{
			if (J.Key) { R->Along.FindOrAdd(J.Fx).Add(Along); R->Size = FMath::Max(R->Size, Size); }
			else       { R->Old.Add(J.Fx, Along); }
		}
		UE_LOG(LogAsdaMountFxDir, Log, TEXT("%s замер %s код %d ключ %d%s ход (%.0f,%.0f): вдоль хода %.0f, размер %.0f"), kPrefix, *J.Fx, J.Code, J.Key, J.bMove ? TEXT(" езда") : TEXT(""), kDirs[J.Dir].X, kDirs[J.Dir].Y, Along, Size);
	}

	static void Summary(const TSharedRef<FRun>& R)
	{
		for (const TPair<FString, TArray<float>>& P : R->Along)
		{
			float Mn = 1e9f, Mx = -1e9f, Sum = 0.f;
			for (const float V : P.Value) { Mn = FMath::Min(Mn, V); Mx = FMath::Max(Mx, V); Sum += V; }
			const float Avg = Sum / FMath::Max(1, P.Value.Num());
			const float* O = R->Old.Find(P.Key);
			bool bSame = (Mx - Mn) < kSpreadMax;
			for (const TCHAR* const Flip : kFlipFx)
			{
				if (P.Key == Flip && (!O || *O - Avg < 15.f || Avg > -10.f)) { bSame = false; }   // разворот снят: полосы обязаны сместиться назад
			}
			if (bSame) { ++R->Ok; } else { ++R->Fail; }
			UE_LOG(LogAsdaMountFxDir, Log, TEXT("%s %s %s: вдоль хода ключ 1 среднее %.0f (разброс %.0f), ключ 0 %.0f — %s"), kPrefix, bSame ? TEXT("ok  ") : TEXT("FAIL"), *P.Key, Avg, Mx - Mn,
				O ? *O : 0.f, Avg < -10.f ? TEXT("за спиной") : (Avg > 10.f ? TEXT("ВПЕРЕДИ") : TEXT("на месте")));
		}
	}

	// ★ По значению: последнюю ссылку держит лямбда таймера, и `ClearTimer` в конце прогона её уничтожает. По ссылке свод читал бы уже
	//   освобождённый прогон (так было: «ИТОГ: ok=0 fail=0» при 155 замерах).
	static void Step(TSharedRef<FRun> R)
	{
		UWorld* W = R->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		AAsdaPlayerCharacter* Ch = WS ? Cast<AAsdaPlayerCharacter>(WS->LocalPlayerActor.Get()) : nullptr;
		if (!Ch) { return; }
		if (R->Idx >= R->Jobs.Num())
		{
			W->GetTimerManager().ClearTimer(R->H);
			Ch->ClearMount();
			AsdaCVarSave::SetForTest(kKey, 1);
			Summary(R);
			AsdaSelfTestKit::Finish(W, kCmd, R->bQuit, LogAsdaMountFxDir, kPrefix, R->Ok, R->Fail);
			return;
		}
		const FJob& J = R->Jobs[R->Idx];
		const int32 ShotPhase = J.Code ? 3 : 6;   // эффекты на пешем снимаются позже: у них задержки старта
		if (R->Phase == 0)
		{
			AsdaCVarSave::SetForTest(kKey, J.Key);
			if (J.Code == 0) { if (R->Mounted != 0) { Ch->ClearMount(); R->Mounted = 0; } }
			else if (R->Mounted != J.Code)
			{
				FString M, A; float Hh = 0.f;
				const FAsdaRideEffectRow* Row = AsdaDt::RowByInt<FAsdaRideEffectRow>(kRideDT, J.Code, kCmd);
				Ch->ClearMount();
				if (Row && WS->LookupMount(J.Code, M, A, Hh)) { Ch->SetMount(M, A, Hh, Row->EffectName, FString(), FString(), FString()); }
				R->Mounted = J.Code;
			}
			Ch->FaceGameDir(kDirs[J.Dir]);
			if (J.bMove)
			{
				R->Home = Ch->GetActorLocation();
				TWeakObjectPtr<AAsdaPlayerCharacter> WC(Ch);
				const FVector Step = Fwd(kDirs[J.Dir]) * (kRideSpeed * 0.02f);
				W->GetTimerManager().SetTimer(R->Mv, FTimerDelegate::CreateLambda([WC, Step]() { if (WC.IsValid()) { WC->AddActorWorldOffset(Step); } }), 0.02f, true);
			}
		}
		else if (R->Phase == 1)
		{
			const FVector Start = Ch->GetActorLocation();
			R->E = AAsdaEffectActor::SpawnOnCarrier(W, J.Fx, Start, Ch, Ch->BaseSkel, NAME_None, EAsdaFxAttachMode::CharWorld);
		}
		else if (R->Phase == ShotPhase)
		{
			Measure(R, Ch);
			if (J.bShot && !R->Out.IsEmpty())
			{
				const FString Who = J.Code ? FString::FromInt(J.Code) : J.Fx;
				FScreenshotRequest::RequestScreenshot(R->Out / FString::Printf(TEXT("%s_%s_%s%d_dir%d.png"), FAsdaLang::Code(FAsdaLang::Current()), *Who, J.bMove ? TEXT("ride") : TEXT("key"), J.Key, J.Dir), /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
			}
		}
		else if (R->Phase == ShotPhase + 1)
		{
			if (AAsdaEffectActor* E = R->E.Get()) { E->Destroy(); }
			if (J.bMove) { W->GetTimerManager().ClearTimer(R->Mv); Ch->SetActorLocation(R->Home); }
			R->Phase = -1; ++R->Idx;
		}
		++R->Phase;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaMountFxDirTest(
	TEXT("asda.Mount.FxDirTest"),
	TEXT("Самотест шлейфов транспорта: положение эффекта вдоль хода во все стороны, ключ asda.Fx.CharWorldStrict 0/1. `frame=<код,код>` `fx=<имя,имя>` `out=<папка>` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaMountFxDir;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		FString Out, FrameArg = TEXT("163"), FxArg; bool bReady = false;
		for (const FString& A : KA.Pos)
		{
			if (A.StartsWith(TEXT("out=")))   { Out = A.Mid(4); }
			if (A.StartsWith(TEXT("frame="))) { FrameArg = A.Mid(6); }
			if (A.StartsWith(TEXT("fx=")))    { FxArg = A.Mid(3); }
			if (A == TEXT("selfready"))       { bReady = true; }
		}
		if (!bReady && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready frame=%s fx=%s out=%s"), kCmd, *FrameArg, *FxArg, *Out);
			if (KA.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IT) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет таблицы предметов")); return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->W = W; R->bQuit = KA.bQuit; R->Out = Out;
		TSet<FString> Seen; int32 NumMounts = 0, NoFx = 0, NoBp = 0;
		TArray<FString> FrameParts, FxParts;
		FrameArg.ParseIntoArray(FrameParts, TEXT(","));
		FxArg.ParseIntoArray(FxParts, TEXT(","));
		TSet<int32> FrameCodes;
		for (const FString& P : FrameParts) { FrameCodes.Add(FCString::Atoi(*P)); }
		for (int32 Code = 1; Code <= 700; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(Code, Sp) || Sp.SubType != 15) { continue; }
			++NumMounts;
			const FAsdaRideEffectRow* Row = AsdaDt::RowByInt<FAsdaRideEffectRow>(kRideDT, Code, kCmd);
			if (!Row || Row->EffectName.IsEmpty()) { ++NoFx; UE_LOG(LogAsdaMountFxDir, Log, TEXT("%s код %d: шлейфа нет"), kPrefix, Code); continue; }
			UE_LOG(LogAsdaMountFxDir, Log, TEXT("%s код %d: шлейф %s"), kPrefix, Code, *Row->EffectName);
			if (!AAsdaEffectActor::ResolveJebClass(Row->EffectName)) { ++NoBp; UE_LOG(LogAsdaMountFxDir, Warning, TEXT("%s ПРОПУСК код %d: BP %s не собран"), kPrefix, Code, *Row->EffectName); continue; }
			const bool bFrame = FrameCodes.Contains(Code);
			if (Seen.Contains(Row->EffectName) && !bFrame) { continue; }
			if (!Seen.Contains(Row->EffectName) || bFrame)
			{
				for (int32 D = 0; D < 4; ++D) { FJob J; J.Fx = Row->EffectName; J.Code = Code; J.Key = 1; J.Dir = D; J.bShot = bFrame; R->Jobs.Add(J); }
				FJob J0; J0.Fx = Row->EffectName; J0.Code = Code; J0.Key = 0; J0.Dir = 0; J0.bShot = bFrame; R->Jobs.Add(J0);
				for (int32 D = 0; bFrame && D < 4; ++D) { FJob J; J.Fx = Row->EffectName; J.Code = Code; J.Key = 1; J.Dir = D; J.bShot = true; J.bMove = true; R->Jobs.Add(J); }
			}
			Seen.Add(Row->EffectName);
		}
		for (const FString& Fx : FxParts)
		{
			if (!AAsdaEffectActor::ResolveJebClass(Fx)) { UE_LOG(LogAsdaMountFxDir, Warning, TEXT("%s ПРОПУСК %s: BP не собран"), kPrefix, *Fx); continue; }
			for (int32 K = 1; K >= 0; --K) { FJob J; J.Fx = Fx; J.Key = K; J.Dir = 0; J.bShot = true; R->Jobs.Add(J); }
		}
		if (!Out.IsEmpty()) { IFileManager::Get().MakeDirectory(*Out, /*Tree*/ true); }
		UE_LOG(LogAsdaMountFxDir, Log, TEXT("%s транспортов %d, без шлейфа %d, без собранного BP %d, уникальных эффектов %d, замеров %d"), kPrefix, NumMounts, NoFx, NoBp, Seen.Num(), R->Jobs.Num());
		W->GetTimerManager().SetTimer(R->H, FTimerDelegate::CreateLambda([R]() { Step(R); }), 0.15f, true);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
