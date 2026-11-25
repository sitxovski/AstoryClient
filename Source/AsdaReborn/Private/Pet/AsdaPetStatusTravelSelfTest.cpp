// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ПЛАШКА ПИТОМЦА ПОСЛЕ ПЕРЕХОДА» `asda.Pet.SelfTestStatusTravel` (08.11.2026, пачка 19 баг 7: «после каждого телепорта
//   мини-окно питомца пропадает и появляется только через некоторое время»). [ПОРТ]
//   Путь игрока без сети: настоящий 6106 с призванным питомцем через приёмник → плашка 43200 → НАСТОЯЩИЙ травел UE (`OpenLevel`,
//   движок сносит виджеты, `PostLoadMap` рассылает «смену карты») → вход своего персонажа (`Asda.World.FakeSelfSpawn`, как 4003).
//   Каждый кадр снимается, видна ли плашка на экране: окно в реестре, во вьюпорте, видимо, геометрия не нулевая.
//   Меряется: от входа своего персонажа до показа (оригинал: опрос `inventoryAutoUpdateThrottled2s@0xDCC170` раз в 2000 мс, только когда
//   свой персонаж есть, `game_main.c:1464-1481`), затем плашка обязана держаться весь отрезок наблюдения без пропаданий и пересоздания.
//   Кадры `asda_petstatus_t<переход>_<a|b>.png` (копия в `out=`). Итог `ПЛАШКА-ПИТОМЦА ИТОГ: ok=N fail=M`.
// Консоль: `asda.Pet.SelfTestStatusTravel [travels=N] [out=<папка>] [quit]`. Только офлайн (`-AsdaNoNet`).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "Test/AsdaSelfTestKit.h"              // аргументы, отказ, итог, выход
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket, SelectedCharId
#include "AsdaBytes.h"                         // поля тела 6106
#include "Login/AsdaLoginData.h"               // EncodeCp1251 — кличка
#include "Pet/AsdaPetSubsystem.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"                      // FScreenshotRequest

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetStatusTest, Log, All);

namespace AsdaPetStatusTestLocal
{
	static const TCHAR* kCmd = TEXT("asda.Pet.SelfTestStatusTravel");
	static const TCHAR* kPfx = TEXT("ПЛАШКА-ПИТОМЦА");
	static const FName kStatusId(TEXT("Pet_Status_Win"));   // окно 43200 в реестре окон
	constexpr int32 kUid = 990099;             // uid питомца стенда
	constexpr int32 kType = 14;                // порода стенда
	constexpr int32 kTestCharId = 77099;       // свой charId, если офлайн его нет
	constexpr double kShowLimitSec = 2.1;      // потолок «вход своего → плашка»: опрос оригинала 2000 мс + кадр
	constexpr double kWatchSec = 8.0;          // отрезок наблюдения после показа
	constexpr double kWorldWaitSec = 30.0;     // потолок ожидания нового мира
	constexpr double kSpawnDelaySec = 0.4;     // после начала игры нового мира — вход своего (в живом журнале 4003 приходит через 0.3–0.5 с)

	// Шаг сценария.
	enum class EPhase : uint8 { First, Travel, WaitWorld, Spawn, WaitShown, Watch, Done };

	// Состояние прогона.
	struct FState : public TSharedFromThis<FState>
	{
		TWeakObjectPtr<UGameInstance> GI;      // игра (переживает травел)
		bool bQuit = false;                    // выйти по окончании
		int32 Ok = 0, Fail = 0;                // счётчики проверок
		int32 Travels = 3;                     // число переходов
		int32 Travel = 0;                      // номер текущего перехода (0 — до первого)
		FString OutDir;                        // папка для копий кадров
		FString PendingShot;                   // кадр, ждущий копии
		bool bCapture = false;                 // перехват (подмена charId) взведён этим прогоном
		EPhase Phase = EPhase::First;          // текущий шаг
		double T0 = 0.0;                       // начало шага
		double TOpen = 0.0;                    // момент вызова OpenLevel
		double TSpawn = 0.0;                   // момент входа своего персонажа
		TWeakObjectPtr<UWorld> OldWorld;       // мир до перехода
		TWeakObjectPtr<UUserWidget> Shown;     // виджет плашки в момент показа
		int32 Gone = 0;                        // кадров без плашки на отрезке наблюдения
		int32 Recreated = 0;                   // кадров с другим виджетом плашки на отрезке наблюдения
		bool bShotB = false;                   // второй кадр отрезка снят

		// Записать проверку в журнал и счётчик.
		void Expect(bool b, const FString& What)
		{
			b ? ++Ok : ++Fail;
			UE_LOG(LogAsdaPetStatusTest, Warning, TEXT("%s %s %s"), kPfx, b ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		}
		// Перейти к шагу.
		void Begin(EPhase P) { Phase = P; T0 = FPlatformTime::Seconds(); }
		// Текущий мир игры.
		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		// Подсистема игры по классу.
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		// Выполнить консольную команду в текущем мире.
		void Exec(const TCHAR* C) const { if (UWorld* Wd = W()) { if (GEngine) { GEngine->Exec(Wd, C); } } }
	};

	// Виджет плашки, если она СЕЙЧАС на экране: в реестре, во вьюпорте, видима, с ненулевой геометрией; иначе nullptr.
	static UUserWidget* VisibleStatus(const FState& S)
	{
		const UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>();
		const UAsdaPetCageSubsystem* C = S.Sub<UAsdaPetCageSubsystem>();
		if (!WM || !C || !C->IsStatusPanelShown() || !WM->IsWindowVisible(kStatusId)) { return nullptr; }
		UUserWidget* U = WM->FindWindow(kStatusId);
		if (!U || !U->IsInViewport() || !U->IsVisible() || U->GetWorld() != S.W()) { return nullptr; }
		const FVector2D Sz = U->GetCachedGeometry().GetLocalSize();
		return (Sz.X > 1.0 && Sz.Y > 1.0) ? U : nullptr;
	}

	// Разбор, какое из условий показа не выполнено (для строки провала).
	static FString WhyHidden(const FState& S)
	{
		const UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>();
		const UAsdaPetCageSubsystem* C = S.Sub<UAsdaPetCageSubsystem>();
		const UAsdaPetSubsystem* P = S.Sub<UAsdaPetSubsystem>();
		UUserWidget* U = WM ? WM->FindWindow(kStatusId) : nullptr;
		return FString::Printf(TEXT("признак показа %d, призванный %d, окно видимо по реестру %d, виджет %d, во вьюпорте %d, видим %d (%d), мир свой %d, размер %s"),
			C && C->IsStatusPanelShown() ? 1 : 0, P && P->GetActivePet() ? 1 : 0, WM && WM->IsWindowVisible(kStatusId) ? 1 : 0, U ? 1 : 0,
			U && U->IsInViewport() ? 1 : 0, U && U->IsVisible() ? 1 : 0, U ? (int32)U->GetVisibility() : -1, U && U->GetWorld() == S.W() ? 1 : 0,
			U ? *U->GetCachedGeometry().GetLocalSize().ToString() : TEXT("—"));
	}

	// Настоящее тело 6106: charId, флаг 0, одна запись 46 б; `bActive` — питомец призван (+45 = 1).
	static void Feed6106(const FState& S, bool bActive = true)
	{
		UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
		if (!Net) { return; }
		TArray<uint8> B; B.SetNumZeroed(5 + 46);
		AsdaBytes::WrI32(B.GetData(), Net->SelectedCharId);
		uint8* R = B.GetData() + 5;
		AsdaBytes::WrI32(R, kUid);
		uint8* P = R + 4;
		AsdaBytes::WrI16(P + 0, kType);
		FAsdaLoginData::EncodeCp1251(TEXT("Bunny"), P + 2, 16);
		P[18] = 90; P[19] = 3; P[20] = 5;   // сытость, уровень, предел
		R[45] = bActive ? 1 : 0;
		Net->DebugFeedPacket(6106, B);
	}

	// Запросить кадр и запомнить, что его надо скопировать.
	static void Shot(FState& S, const FString& Name)
	{
		S.PendingShot = Name;
		FScreenshotRequest::RequestScreenshot(Name, /*bShowUI*/ true, /*bAddUniqueSuffix*/ false);
	}

	// Скопировать прошлый кадр в папку отчёта.
	static void CopyShot(FState& S)
	{
		if (S.PendingShot.IsEmpty() || S.OutDir.IsEmpty()) { return; }
		const FString Src = FPaths::ScreenShotDir() / (S.PendingShot + TEXT(".png"));
		IFileManager::Get().MakeDirectory(*S.OutDir, true);
		const bool bCopied = IFileManager::Get().Copy(*(S.OutDir / (S.PendingShot + TEXT(".png"))), *Src) == COPY_OK;
		UE_LOG(LogAsdaPetStatusTest, Log, TEXT("%s кадр %s: %s"), kPfx, *S.PendingShot, bCopied ? TEXT("скопирован") : TEXT("НЕ скопирован"));
		S.PendingShot.Reset();
	}

	// Завершение: снять перехват, вернуть charId, итог.
	static void Finish(FState& S)
	{
		CopyShot(S);
		// Не оставлять призванного питомца стенда следующим самотестам: тот же приёмник 6106 снимает признак «призван».
		if (UAsdaPetSubsystem* P = S.Sub<UAsdaPetSubsystem>())
		{
			UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>();
			if (Net && Net->SelectedCharId == 0) { P->BeginSendCapture([](int32, const TArray<uint8>&, bool, TCHAR) {}, kTestCharId); S.bCapture = true; }
			Feed6106(S, /*bActive*/ false);
		}
		if (S.bCapture) { if (UAsdaPetSubsystem* P = S.Sub<UAsdaPetSubsystem>()) { P->EndSendCapture(); } }
		AsdaSelfTestKit::Finish(S.W(), kCmd, S.bQuit, LogAsdaPetStatusTest, kPfx, S.Ok, S.Fail, TEXT(""), true);
		S.Phase = EPhase::Done;
	}

	// Один кадр сценария; false — конец.
	static bool Step(const TSharedRef<FState>& SR)
	{
		FState& S = *SR;
		const double Now = FPlatformTime::Seconds();
		const double Dt = Now - S.T0;
		UWorld* W = S.W();
		switch (S.Phase)
		{
		case EPhase::First:
			// До первого перехода: плашка обязана появиться по 6106 (призванный есть).
			if (!VisibleStatus(S) && Dt < 2.5) { return true; }
			S.Expect(VisibleStatus(S) != nullptr, FString::Printf(TEXT("до перехода: 6106 с призванным → плашка на экране через %.2f с"), Dt));
			Shot(S, TEXT("asda_petstatus_t0"));
			S.Begin(EPhase::Travel);
			return true;

		case EPhase::Travel:
			if (Dt < 0.6) { return true; }
			CopyShot(S);
			++S.Travel;
			S.OldWorld = W;
			S.TOpen = Now;
			UGameplayStatics::OpenLevel(W, FName(TEXT("/Game/Map/alpen/alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode"));
			S.Begin(EPhase::WaitWorld);
			return true;

		case EPhase::WaitWorld:
			if (!W || W == S.OldWorld.Get() || !W->HasBegunPlay())
			{
				if (Dt > kWorldWaitSec) { S.Expect(false, FString::Printf(TEXT("переход %d: новый мир не поднялся за %.0f с"), S.Travel, kWorldWaitSec)); Finish(S); return false; }
				return true;
			}
			S.Begin(EPhase::Spawn);
			return true;

		case EPhase::Spawn:
			if (Dt < kSpawnDelaySec) { return true; }
			// Вход своего персонажа в новый мир — тот же путь, что 4003 в игре.
			S.Exec(TEXT("Asda.Load.CurrentMap 3"));
			S.Exec(TEXT("Asda.World.FakeSelfSpawn 256 256"));
			S.TSpawn = Now;
			S.Begin(EPhase::WaitShown);
			return true;

		case EPhase::WaitShown:
			{
				UUserWidget* U = VisibleStatus(S);
				if (!U && Dt < kShowLimitSec + 3.0) { return true; }
				const double Lag = Now - S.TSpawn;
				S.Expect(U != nullptr && Lag <= kShowLimitSec, FString::Printf(
					TEXT("переход %d: плашка на экране через %.2f с после входа своего персонажа (потолок оригинала %.1f с), от вызова перехода %.2f с"),
					S.Travel, Lag, kShowLimitSec, Now - S.TOpen));
				if (!U) { UE_LOG(LogAsdaPetStatusTest, Warning, TEXT("%s состояние: %s"), kPfx, *WhyHidden(S)); Finish(S); return false; }
				S.Shown = U;
				S.Gone = 0;
				S.Recreated = 0;
				S.bShotB = false;
				Shot(S, FString::Printf(TEXT("asda_petstatus_t%d_a"), S.Travel));
				S.Begin(EPhase::Watch);
				return true;
			}

		case EPhase::Watch:
			{
				if (Dt > 0.3) { CopyShot(S); }
				UUserWidget* U = VisibleStatus(S);
				if (!U) { if (S.Gone++ == 0) { UE_LOG(LogAsdaPetStatusTest, Warning, TEXT("%s пропала через %.2f с: %s"), kPfx, Dt, *WhyHidden(S)); } }
				else if (U != S.Shown.Get()) { ++S.Recreated; }
				if (!S.bShotB && Dt > kWatchSec - 1.0) { Shot(S, FString::Printf(TEXT("asda_petstatus_t%d_b"), S.Travel)); S.bShotB = true; }
				if (Dt < kWatchSec) { return true; }
				S.Expect(S.Gone == 0 && S.Recreated == 0, FString::Printf(
					TEXT("переход %d: за %.0f с наблюдения кадров без плашки %d, с пересозданной плашкой %d"), S.Travel, kWatchSec, S.Gone, S.Recreated));
				if (S.Travel >= S.Travels) { Finish(S); return false; }
				S.Begin(EPhase::Travel);
				return true;
			}

		default:
			return false;
		}
	}

	// Старт после входа своего персонажа в первый мир.
	static void Start(UWorld* W, const FAsdaSelfTestArgs& A, int32 Travels, const FString& Out)
	{
		TSharedRef<FState> S = MakeShared<FState>();
		S->GI = W->GetGameInstance();
		S->bQuit = A.bQuit;
		S->Travels = Travels;
		S->OutDir = Out;
		UAsdaNetSubsystem* Net = S->Sub<UAsdaNetSubsystem>();
		UAsdaPetSubsystem* Pets = S->Sub<UAsdaPetSubsystem>();
		if (!Net || !Pets || !S->Sub<UAsdaPetCageSubsystem>() || !S->Sub<UAsdaWindowSubsystem>())
		{
			AsdaSelfTestKit::Refused(W, kCmd, A.bQuit, TEXT("нет сети / питомцев / окон"));
			return;
		}
		// Подмена charId (офлайн он 0, а приёмник отбрасывает 6106 «не свой»). Сброс «смена карты» снимает подмену — список при этом
		//   сохраняется (`UAsdaPetSubsystem::ResetSessionState`), второй 6106 после перехода не нужен, как и на живом сервере.
		S->bCapture = Pets->BeginSendCapture([](int32, const TArray<uint8>&, bool, TCHAR) {}, Net->SelectedCharId == 0 ? kTestCharId : 0);
		Feed6106(*S);
		S->Begin(EPhase::First);
		UE_LOG(LogAsdaPetStatusTest, Log, TEXT("%s старт: переходов %d, карта перехода alpen"), kPfx, Travels);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float) { return Step(S); }));
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPetStatusTravelSelfTest(
	TEXT("asda.Pet.SelfTestStatusTravel"),
	TEXT("Самотест плашки питомца 43200 после НАСТОЯЩЕЙ смены карты (08.11.2026): показ после входа своего персонажа не позже опроса ")
	TEXT("оригинала (2 с), затем без пропаданий и пересоздания. `travels=N` (3), `out=<папка>` для кадров, `quit`. Только офлайн."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaPetStatusTestLocal;
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		int32 Travels = 3;
		FString Out;
		for (const FString& P : A.Pos)
		{
			if (P.StartsWith(TEXT("travels="))) { Travels = FMath::Clamp(FCString::Atoi(*P.Mid(8)), 1, 5); }
			if (P.StartsWith(TEXT("out=")))     { Out = P.Mid(4); }
		}
		if (!W) { AsdaSelfTestKit::Refused(nullptr, kCmd, A.bQuit, TEXT("нет мира")); return; }
		if (!A.Has(TEXT("selfready")) && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready travels=%d"), kCmd, Travels);
			if (!Out.IsEmpty()) { Again += TEXT(" out=") + Out; }
			if (A.bQuit) { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, A)) { return; }
		Start(W, A, Travels, Out);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
