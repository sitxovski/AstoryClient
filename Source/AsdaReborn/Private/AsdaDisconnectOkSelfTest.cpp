// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ОК ПОПАПА ОБРЫВА УВОДИТ ИЗ МИРА» (19.10.2026): `asda.SelfTest.DisconnectOk [quit]`.
//   Жалоба владельца: «сервер дисконнектит игрока, он нажимает ОК, окно закрывается и он продолжает ходить».
//   Исправление — `UAsdaDisconnectPopupSubsystem::OnOkClicked`: в мире ОК ведёт на экран входа (шапка .h подсистемы).
//   Реальный путь (закон diagnostic-must-use-real-path), без сети:
//     1) в МИРЕ: показ попапа 8000 той же функцией, что при живом обрыве (`ShowDisconnectPopup(1108)`); окно модальное —
//        ввод мира снят; клик по 8003 — рассылкой `OnClicked` самой кнопки; попап закрыт; через кадры текущий уровень —
//        сцена входа (переход состоялся, в мире без связи игрок не остаётся);
//     2) на СЦЕНЕ ВХОДА: тот же показ и клик — попап закрыт, уровень не меняется (повторного перехода нет).
//   Запускать на карте мира (`--map world` пакета), только офлайн (-AsdaNoNet). Итог: «ОБРЫВ-ОК ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Test/AsdaSelfTestKit.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDiscOkTest, Log, All);

namespace AsdaDisconnectOkSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.DisconnectOk");

	// Состояние прогона (переживает смену уровня — живёт на тикере ядра).
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры (переживает переход)
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		int32 Phase = 0;                    // 0 — ждём сцену входа после ОК в мире; 1 — пауза на сцене входа
		double PhaseStart = 0.0;            // начало фазы
		FString WorldMap;                   // имя уровня мира до ОК
	};
	static TSharedPtr<FRun> GRun;

	// Записать результат проверки.
	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaDiscOkTest, Log, TEXT("ОБРЫВ-ОК ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaDiscOkTest, Error, TEXT("ОБРЫВ-ОК FAIL %s"), *What); }
	}

	// Текущий мир экземпляра игры.
	static UWorld* WorldOf(const FRun& R)
	{
		return R.GI.IsValid() ? R.GI->GetWorld() : nullptr;
	}

	// Показать попап реальным путём и нажать ОК его кнопкой. Tag — префикс строк журнала.
	static void ShowAndClickOk(FRun& R, const TCHAR* Tag)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaDisconnectPopupSubsystem* Pop = GI ? GI->GetSubsystem<UAsdaDisconnectPopupSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Pop || !WM) { Expect(R, false, FString::Printf(TEXT("%s: нет подсистемы попапа или окон"), Tag)); return; }
		UUserWidget* Box = Pop->ShowDisconnectPopup(1108);
		Expect(R, Box && Pop->IsOpen() && WM->IsAnyModalOpen(),
			FString::Printf(TEXT("%s: попап 8000 показан, модальный — ввод мира снят"), Tag));
		UButton* B = Box ? Cast<UButton>(Box->GetWidgetFromName(TEXT("COMMONWIN_BUT"))) : nullptr;
		Expect(R, B && B->OnClicked.IsBound(), FString::Printf(TEXT("%s: кнопка ОК 8003 привязана"), Tag));
		if (B) { B->OnClicked.Broadcast(); }   // тот же путь, что щелчок игрока
		Expect(R, !Pop->IsOpen(), FString::Printf(TEXT("%s: после ОК попап закрыт"), Tag));
	}

	// Ожидание перехода и вторая часть (на тиках ядра — переживают смену уровня).
	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UWorld* W = WorldOf(R);
		const double Now = FPlatformTime::Seconds();
		auto Done = [&R]()
		{
			AsdaSelfTestKit::Finish(WorldOf(R), kCmd, R.bQuit, LogAsdaDiscOkTest, TEXT("ОБРЫВ-ОК"), R.Ok, R.Fail);
			GRun.Reset();
			return false;
		};
		if (R.Phase == 0)
		{
			if (W && AsdaWorldKind::IsLoginScene(W) && W->HasBegunPlay())
			{
				Expect(R, true, FString::Printf(TEXT("1: ОК в мире → переход на сцену входа за %.1f с (%s → %s)"),
					Now - R.PhaseStart, *R.WorldMap, *W->GetMapName()));
				R.Phase = 1; R.PhaseStart = Now;
				return true;
			}
			if (Now - R.PhaseStart > 60.0)
			{
				Expect(R, false, FString::Printf(TEXT("1: за 60 с переход на сцену входа не состоялся (уровень %s)"),
					W ? *W->GetMapName() : TEXT("—")));
				return Done();
			}
			return true;
		}
		if (Now - R.PhaseStart < 3.0) { return true; }   // сцене входа дать подняться
		const FString MapBefore = W ? W->GetMapName() : FString();
		ShowAndClickOk(R, TEXT("2"));
		UWorld* W2 = WorldOf(R);
		// Запрошенный переход (`OpenLevel`) ложится в TravelURL контекста мира — после ОК он обязан остаться пустым.
		const FWorldContext* Ctx = (W2 && GEngine) ? GEngine->GetWorldContextFromWorld(W2) : nullptr;
		const bool bNoTravel = Ctx && Ctx->TravelURL.IsEmpty();
		Expect(R, W2 && AsdaWorldKind::IsLoginScene(W2) && W2->GetMapName() == MapBefore && bNoTravel,
			TEXT("2: на сцене входа ОК только закрывает попап — нового перехода нет"));
		return Done();
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }
		if (AsdaWorldKind::IsLoginScene(W)) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать в мире, не на сцене входа")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.bQuit = Args.bQuit; R.WorldMap = W->GetMapName();
		if (IConsoleVariable* Q = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Net.DisconnectPopupQuit")))
		{
			Expect(R, Q->GetInt() == 0, TEXT("0: ключ asda.Net.DisconnectPopupQuit = 0 (иначе ОК закрыл бы процесс)"));
		}
		ShowAndClickOk(R, TEXT("1"));
		R.Phase = 0;
		R.PhaseStart = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.2f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDisconnectOk(
	TEXT("asda.SelfTest.DisconnectOk"),
	TEXT("Самотест: ОК попапа обрыва связи 8000 в мире ведёт на экран входа (игрок не остаётся в мире без связи); на сцене "
	     "входа ОК только закрывает попап. Запускать в мире, только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaDisconnectOkSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
