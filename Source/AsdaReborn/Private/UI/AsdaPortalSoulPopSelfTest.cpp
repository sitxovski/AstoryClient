// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «ПОРТАЛ С ПАНЕЛЬЮ ДРУГА» (26.10.2026, волна silaris): `asda.Window.PortalSoulPopSelfTest [quit]`. [ИСПРАВЛЕНИЕ]
//   Жалоба Player17 (75214) и Player52 (75230), alpha-3.0.2…3.0.5: «игра зависает на любом портале/телепорте». Журналы LogDrop:
//   после травела на `L_Loading` строка «[панель Друга] окно 6500 потеряно с миром — поднимаю заново», затем «поп-карточка 6500: …»
//   тысячами в секунду и ничего больше — игровой поток в бесконечном цикле. Причина: реестр окон держит запись 6500 с виджетом,
//   умершим вместе с прежним миром (реестр чистит только `CloseAll` при входе в мир), `OpenWindow` возвращал его, `IsWindowLive`
//   на нём ложно — `TryShowPop` ↔ `RefreshPopCard` ↔ `ApplyPopVisibility` гоняли «поднять заново» без конца.
//   Всё — штатным путём: 5110/5220 через `UAsdaNetSubsystem::DebugFeedPacket`, смена карты — `UAsdaLoadManagerSubsystem::BeginMapLoad`
//   (тот же вызов, что отложенная загрузка после 6004), шаги — по тикеру ядра (таймеры мира умирают вместе с миром).
//   Этапы:  1 панель показана на карте мира;  2 травел на L_Loading: игра не повисла (тикер идёт), окно 6500 в реестре ЖИВОЕ,
//           пересозданий не больше 2;  3 через 2 с на L_Loading кадры идут, окно по-прежнему живое.
//   ★ 26.10.2026 (вторая линия, ключ asda.Soulmate.PopGuard 1): на L_Loading панель не поднимается (этапы 2/3 ждут «не поднята,
//           желание сохранено»), этап 4 — на целевой карте панель вернулась на новом живом виджете. PopGuard 0 — этапы как выше.
//   Доказательство причины вторым методом: прогон с `--pre "asda.Window.ReopenOrphan 0"` повисает (таймаут пакета) — прежний код.
//   Итог: «ПОРТАЛ-ДРУГ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaWorldKind.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPortalSoulPopTest, Log, All);

namespace AsdaPortalSoulPopTest
{
	static const TCHAR* kCmd = TEXT("asda.Window.PortalSoulPopSelfTest");   // имя команды
	static constexpr int32  kMe = 1001;          // свой charId на прогон
	static constexpr uint16 kMyUserNum = 321;    // свой userNum на прогон
	static constexpr int32  kMateU = 777;        // userNum Друга
	static constexpr int32  kMateCid = 900777;   // charId Друга
	static constexpr int32  kSlot = 5;           // слот пары
	static constexpr int32  kTargetMap = 3;      // карта перехода (alpen — как телепорт Player17 22:18:02)
	static const FName kPopId(TEXT("SOULMATE_POP_WIN"));   // id окна 6500 в реестре

	// Контекст прогона (живёт между шагами тикера, переживает смену мира).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                     // счёт
		bool bQuit = false;                         // выйти после итога
		TWeakObjectPtr<UGameInstance> GI;           // игра (общая для всех миров прогона)
		TUniquePtr<FAsdaNetSelfScope> NetScope;     // свои charId/userNum — вернуть в конце
		TWeakObjectPtr<UUserWidget> PopBefore;      // виджет 6500 до травела
		double T0 = 0.0;                            // начало ожидания текущего шага
		uint64 Frame0 = 0;                          // кадр начала ожидания
		int32 Stage = 0;                            // текущий шаг
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaPortalSoulPopTest, Log, TEXT("ПОРТАЛ-ДРУГ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaPortalSoulPopTest, Error, TEXT("ПОРТАЛ-ДРУГ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	template <class T> static T* Sub(const FCtxRef& C) { UGameInstance* GI = C->GI.Get(); return GI ? GI->GetSubsystem<T>() : nullptr; }
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& B) { if (UAsdaNetSubsystem* N = Sub<UAsdaNetSubsystem>(C)) { N->DebugFeedPacket(Op, B); } }

	// Запись 5110 (251 б): слот, Друг, HP/MP, уровень, класс, уровень Дружбы, имя (разметка — как у самотеста панели Друга).
	static TArray<uint8> Body5110()
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], kSlot); AsdaBytes::WrI32(&B[4], kMateCid); AsdaBytes::WrI16(&B[8], kMateU);
		AsdaBytes::WrI32(&B[13], 1000); AsdaBytes::WrI32(&B[17], 500);
		AsdaBytes::WrI16(&B[21], 200);  AsdaBytes::WrI16(&B[23], 50);
		B[29] = 42; B[38] = 3; B[40] = 2; B[50] = 3;
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[63], 19);
		return B;
	}
	// Своя запись 5220 (101 б): Друг «в игре» (@25) и серверный признак панели (@33).
	static TArray<uint8> Body5220(uint8 Login, uint8 Opt, int32 Slot)
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI16(&B[4], kMyUserNum);
		AsdaBytes::WrI32(&B[6], Slot); AsdaBytes::WrI32(&B[19], kMateCid); AsdaBytes::WrI16(&B[23], kMateU);
		B[25] = Login; B[33] = Opt; B[36] = 1; B[37] = 0; B[38] = 1;
		return B;
	}
	// ★ 26.10.2026 (вторая линия, smpop_guard): ключ `asda.Soulmate.PopGuard` 1 — на L_Loading панель НЕ поднимается вовсе
	//   (тик и показ вне мира выключены), её возвращает тик на целевой карте. 0 — прежнее ожидание «пересоздана на L_Loading».
	static bool PopGuardOn()
	{
		const IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.Soulmate.PopGuard"));
		return V && V->GetInt() != 0;
	}
	// Окно 6500 в реестре живо в ТЕКУЩЕМ мире.
	static bool PopLive(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UUserWidget* W = WM ? WM->FindWindow(kPopId) : nullptr;
		return WM && W && WM->IsWindowLive(W);
	}

	static void Finish(const FCtxRef& C)
	{
		Feed(C, 5220, Body5220(0, 0, -1));   // «слота нет» — снять пару штатным приёмом
		if (UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C)) { Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		C->NetScope.Reset();
		UGameInstance* GI = C->GI.Get();
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, C->bQuit, LogAsdaPortalSoulPopTest, TEXT("ПОРТАЛ-ДРУГ"), C->Ok, C->Fail);
	}

	// Шаг тикера: true — тикать дальше.
	static bool Step(const FCtxRef& C)
	{
		UGameInstance* GI = C->GI.Get();
		UWorld* W = GI ? GI->GetWorld() : nullptr;
		if (!GI) { return false; }
		const double Now = FPlatformTime::Seconds();
		switch (C->Stage)
		{
		case 0:   // ждём 0,3 с после 5110/5220 — панель показана
			if (Now - C->T0 < 0.3) { return true; }
			{
				UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
				C->PopBefore = WM ? WM->FindWindow(kPopId) : nullptr;
				C->Expect(Sm && Sm->IsPopCardShown() && PopLive(C), TEXT("1: 5110 + своя 5220 — панель Друга 6500 показана на карте мира"));
				UAsdaLoadManagerSubsystem* LM = Sub<UAsdaLoadManagerSubsystem>(C);
				if (!LM || !Sm || !Sm->IsPopCardShown()) { Finish(C); return false; }
				UE_LOG(LogAsdaPortalSoulPopTest, Log, TEXT("ПОРТАЛ-ДРУГ шаг 2: BeginMapLoad(%d) — травел на L_Loading с поднятой панелью"), kTargetMap);
				LM->BeginMapLoad(kTargetMap);
				C->Stage = 1; C->T0 = Now; C->Frame0 = GFrameCounter;
			}
			return true;
		case 1:   // ждём L_Loading с начатой игрой
			if (W && AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay())
			{
				C->Stage = 2; C->T0 = Now; C->Frame0 = GFrameCounter;
				return true;
			}
			if (Now - C->T0 > 30.0) { C->Expect(false, TEXT("2: мир L_Loading не начался за 30 с")); Finish(C); return false; }
			return true;
		case 2:   // 10 кадров на L_Loading: тик панели уже отработал
			if (GFrameCounter - C->Frame0 < 10) { return true; }
			{
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
				UUserWidget* Now6500 = WM ? WM->FindWindow(kPopId) : nullptr;
				C->Expect(true, FString::Printf(TEXT("2: на L_Loading прошло %llu кадров — игровой поток не повис"), (unsigned long long)(GFrameCounter - C->Frame0)));
				if (PopGuardOn())
				{
					UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
					C->Expect(!PopLive(C) && Sm && Sm->IsPopWanted(),
						TEXT("2: защита панели — на L_Loading окно 6500 не поднимается, желание «панель на экране» сохранено"));
				}
				else
				{
					C->Expect(Now6500 && PopLive(C), TEXT("2: окно 6500 в реестре живое в мире L_Loading (мёртвая запись прежнего мира снята)"));
					C->Expect(Now6500 && Now6500 != C->PopBefore.Get(), TEXT("2: виджет 6500 создан заново (не виджет прежнего мира)"));
				}
				C->Stage = 3; C->T0 = Now; C->Frame0 = GFrameCounter;
			}
			return true;
		case 3:   // ещё 2 с на экране загрузки (или уже на целевой карте): кадры идут, окно живо
			if (Now - C->T0 < 2.0) { return true; }
			C->Expect(GFrameCounter - C->Frame0 > 20, FString::Printf(TEXT("3: за 2 с прошло %llu кадров (зависания нет)"), (unsigned long long)(GFrameCounter - C->Frame0)));
			if (!PopGuardOn())
			{
				C->Expect(!W || !AsdaWorldKind::IsLoadingMap(W) || PopLive(C), TEXT("3: на L_Loading окно 6500 по-прежнему живое"));
				Finish(C);
				return false;
			}
			C->Stage = 4; C->T0 = Now;
			return true;
		case 4:   // ★ защита панели: ждём целевую карту — тик возвращает панель на новом виджете
			if (W && !AsdaWorldKind::IsLoadingMap(W) && W->HasBegunPlay())
			{
				C->Stage = 5; C->T0 = Now;
				return true;
			}
			if (Now - C->T0 > 60.0) { C->Expect(false, TEXT("4: целевая карта не началась за 60 с")); Finish(C); return false; }
			return true;
		case 5:   // 1 с на целевой карте
			if (Now - C->T0 < 1.0) { return true; }
			{
				UAsdaSoulmateSubsystem* Sm = Sub<UAsdaSoulmateSubsystem>(C);
				UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
				UUserWidget* Now6500 = WM ? WM->FindWindow(kPopId) : nullptr;
				C->Expect(Sm && PopLive(C) && Sm->IsPopCardShown() && Now6500 != C->PopBefore.Get(),
					FString::Printf(TEXT("4: на целевой карте %s панель 6500 вернулась на новом живом виджете"), W ? *W->GetName() : TEXT("?")));
			}
			Finish(C);
			return false;
		default:
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* Sm = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Net || !Sm || !GI->GetSubsystem<UAsdaLoadManagerSubsystem>()) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->GI = GI; C->bQuit = KA.bQuit;
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		Sm->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Feed(C, 5110, Body5110());
		Feed(C, 5220, Body5220(1, 1, kSlot));
		C->T0 = FPlatformTime::Seconds();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([C](float) { return Step(C); }), 0.f);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaPortalSoulPopSelfTest(
	TEXT("asda.Window.PortalSoulPopSelfTest"),
	TEXT("Самотест перехода карты с поднятой панелью Друга 6500 (зависание Player17/Player52 26.10): 5110/5220 → BeginMapLoad → "
	     "L_Loading — игра не виснет, окно 6500 живое. Офлайн, на карте мира. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaPortalSoulPopTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
