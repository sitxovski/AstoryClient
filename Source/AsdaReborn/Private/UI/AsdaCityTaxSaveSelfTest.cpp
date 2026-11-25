// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «НАЛОГ ГОРОДА СОХРАНЯЕТСЯ — ПОВТОР СЦЕНАРИЯ ИГРОКА» (08.11.2026, пачка 18, баг 4): `asda.SelfTest.CityTaxSave [quit]`.
//   Жалоба: Player58 (charId 75210, Свет, глава клана Vanderich — хозяина Силариса) «ставка налога не сохраняется».
//   Живой журнал клиента (LogDrop Player5, 02.11): 21:21:20 →6744 25% ←6745 код 1 (111912 «изменена… начнёт действовать в 00:00»);
//   окно, открытое повторно, показывает ДЕЙСТВУЮЩУЮ ставку 0% (её приносит 6743 — так и в оригинале, `CUniconWar_UpdateFactionInfoPanel`),
//   повторное «Нажми» → ←6745 код 3 (111919 «изменение на 25% будет произведено в 00:00»). Сервер: 03.11 00:00:05 Силарис Tax:25.
//   Самотест повторяет этот путь штатными входами (кнопка NPC 30076, полоса, «Нажми», «Да» очереди окон, приём 6743/6745 через
//   `DebugFeedPacket`, исходящие под перехватом) и снимает кадры в `Reports/batch18/city_tax/<ru|en>_<шаг>.png`.
//   Только офлайн, на карте Альпии (слот 0). Итог: «НАЛОГ-ГОРОДА ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaTownWarSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "Text/AsdaLang.h"
#include "World/AsdaLoadManagerSubsystem.h"   // номер карты (офлайн до спавна ещё −1)
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCityTaxTest, Log, All);

namespace AsdaCityTaxSaveSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.CityTaxSave");   // имя команды
	static constexpr int32 kSelfCharId = 75210;                     // charId Player58 (живой журнал)
	static constexpr uint8 kSelfIdx = 10;                           // его индекс персонажа (6743 +29)
	static constexpr int32 kRate = 25;                              // ставка из журнала

	// Состояние прогона.
	struct FRun
	{
		TWeakObjectPtr<UGameInstance> GI;   // экземпляр игры
		bool bQuit = false;                 // выйти после итога
		int32 Ok = 0, Fail = 0;             // счёт проверок
		int32 Step = 0;                     // шаг сценария
		double StepAt = 0.0;                // момент начала шага
		uint32 CapGen = 0;                  // поколение перехвата исходящих
		int32 Sent6744 = 0;                 // сколько 6744 ушло
		int32 Last6744Rate = -1;            // ставка последнего 6744
		int32 SavedCharId = 0;              // свой charId до прогона
		int32 SavedRawSlot = 0;             // свой индекс до прогона
		int32 SavedMapId = -1;              // номер карты до прогона
		UAsdaNetSubsystem::FAsdaCityOwner SavedCity[3];   // записи 6743 до прогона
		bool bSavedCityReceived = false;    // 6743 до прогона
		FString Lang, ShotDir;              // язык и папка кадров
	};
	static TSharedPtr<FRun> GRun;

	static void Expect(FRun& R, bool b, const FString& What)
	{
		if (b) { ++R.Ok; UE_LOG(LogAsdaCityTaxTest, Log, TEXT("НАЛОГ-ГОРОДА ok   %s"), *What); }
		else { ++R.Fail; UE_LOG(LogAsdaCityTaxTest, Error, TEXT("НАЛОГ-ГОРОДА FAIL %s"), *What); }
	}

	// Кадр с интерфейсом (окно города и окна-сообщения).
	static void Shot(FRun& R, const TCHAR* Name)
	{
		const FString File = FPaths::Combine(R.ShotDir, FString::Printf(TEXT("%s_%s.png"), *R.Lang, Name));
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI=*/true, /*bAddFilenameSuffix=*/false);
		UE_LOG(LogAsdaCityTaxTest, Log, TEXT("НАЛОГ-ГОРОДА кадр: %s"), *File);
	}

	// Тело 6743: город слот 0 (Альпия — карта прогона) за Светом с налогом Tax, клан Vanderich, глава Player58 = я.
	static TArray<uint8> Make6743(int32 Tax)
	{
		TArray<uint8> B; B.SetNumZeroed(274);
		B[0] = 1;
		for (int32 I = 0; I < 3; ++I)
		{
			uint8* P = B.GetData() + 1 + 91 * I;
			AsdaBytes::WrI16(P, I == 0 ? 0 : 3);
			if (I != 0) { continue; }
			P[2] = (uint8)Tax;
			AsdaBytes::WrI16(P + 6, 1);
			FMemory::Memcpy(P + 8, "Vanderich", 9);
			AsdaBytes::WrI32(P + 25, kSelfCharId);
			P[29] = kSelfIdx;
			FMemory::Memcpy(P + 30, "Player58", 9);
		}
		return B;
	}
	static TArray<uint8> Make6745(uint8 Code, uint8 Rate) { TArray<uint8> B; B.Add(Code); B.Add(Rate); return B; }

	static void Finish(FRun& R)
	{
		UGameInstance* GI = R.GI.Get();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (UAsdaTownWarSubsystem* T = GI ? GI->GetSubsystem<UAsdaTownWarSubsystem>() : nullptr) { T->CloseWindow(); }
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr) { WM->ClearMsgBoxQueue(TEXT("самотест налога: конец")); }
		if (Net)
		{
			AsdaSelfTestKit::DisarmCapture(Net, R.CapGen);
			Net->SelectedCharId = R.SavedCharId;
			Net->LastSelectedRawSlot = R.SavedRawSlot;
			for (int32 I = 0; I < 3; ++I) { Net->CityOwner[I] = R.SavedCity[I]; }
			Net->bCityOwnerReceived = R.bSavedCityReceived;
		}
		if (UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr) { LM->CurrentMapId = R.SavedMapId; }
		AsdaSelfTestKit::Finish(GI ? GI->GetWorld() : nullptr, kCmd, R.bQuit, LogAsdaCityTaxTest, TEXT("НАЛОГ-ГОРОДА"), R.Ok, R.Fail);
		GRun.Reset();
	}

	static bool OnTick(float)
	{
		if (!GRun.IsValid()) { return false; }
		FRun& R = *GRun;
		UGameInstance* GI = R.GI.Get();
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaTownWarSubsystem* T = GI ? GI->GetSubsystem<UAsdaTownWarSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		UAsdaNpcDialogSubsystem* Npc = GI ? GI->GetSubsystem<UAsdaNpcDialogSubsystem>() : nullptr;
		if (!Net || !T || !WM || !Npc) { Expect(R, false, TEXT("подсистемы сети/города/окон/NPC")); Finish(R); return false; }
		const double Now = FPlatformTime::Seconds();
		if (Now - R.StepAt < 1.5) { return true; }   // кадр предыдущего шага успел сняться
		R.StepAt = Now;
		const FString Pct = FString::Printf(TEXT("%d%%"), kRate);
		switch (R.Step++)
		{
		case 0:   // открыть окно кнопкой NPC 30076: действующая ставка 0%
			Net->DebugFeedPacket(6743, Make6743(0));
			T->ClearTrace();
			Npc->RunCommandForCheck(30076, -1);
			Expect(R, T->IsOpen() && T->GetText(79315) == TEXT("0%"), FString::Printf(TEXT("окно 79300 открыто, ставка «%s» (ждём 0%%)"), *T->GetText(79315)));
			Shot(R, TEXT("1_window_0pct"));
			return true;
		case 1:   // полоса на 25%, «Нажми» главой → TWOBUT (1, 86) 111921
			T->SetScrollPos(kRate);
			T->DebugResetThrottle();
			T->OnOkClicked();
			Expect(R, WM->IsMsgBoxQueued(1, 86) && T->GetLastNoticeText().Contains(FString::FromInt(kRate)),
				FString::Printf(TEXT("«Нажми» главой: подтверждение 111921 «%s»"), *T->GetLastNoticeText()));
			Shot(R, TEXT("2_confirm_25pct"));
			return true;
		case 2:   // «Да» → 6744 (25%) → ответ 6745 код 1 → ONEBUT 111912
			WM->AnswerMsgBox(/*bTwoBut*/ true, /*bYes*/ true);
			Expect(R, R.Sent6744 == 1 && R.Last6744Rate == kRate, FString::Printf(TEXT("ушёл 6744: %d шт., ставка %d"), R.Sent6744, R.Last6744Rate));
			T->ClearTrace();
			Net->DebugFeedPacket(6745, Make6745(1, kRate));   // приём ставит ответ в очередь; окно — на тике подсистемы
			return true;
		case 3:   // ответ 6745 код 1 разобран → ONEBUT 111912
			Expect(R, T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111912 && T->GetLastNoticeText().Contains(Pct),
				FString::Printf(TEXT("6745 код 1: ONEBUT 111912 «%s»"), *T->GetLastNoticeText()));
			Shot(R, TEXT("3_accepted_code1"));
			return true;
		case 4:   // закрыть и открыть снова: до 00:00 окно показывает ДЕЙСТВУЮЩУЮ ставку (6743 не менялся) — как оригинал
			WM->ClearMsgBoxQueue(TEXT("самотест налога"));
			T->CloseWindow();
			Npc->RunCommandForCheck(30076, -1);
			Expect(R, T->IsOpen() && T->GetText(79315) == TEXT("0%"), FString::Printf(TEXT("повторное окно до 00:00: «%s» (действующая 0%%)"), *T->GetText(79315)));
			Shot(R, TEXT("4_reopen_still_0pct"));
			return true;
		case 5:   // повторная попытка → 6745 код 3 → ONEBUT 111919 «…будет произведено в 00:00»
			T->SetScrollPos(kRate);
			T->DebugResetThrottle();
			T->OnOkClicked();
			WM->AnswerMsgBox(true, true);
			T->ClearTrace();
			Net->DebugFeedPacket(6745, Make6745(3, kRate));
			return true;
		case 6:   // ответ 6745 код 3 разобран → ONEBUT 111919
			Expect(R, T->GetNoticeTrace().Num() == 1 && T->GetNoticeTrace()[0] == 111919 && T->GetLastNoticeText().Contains(Pct),
				FString::Printf(TEXT("6745 код 3: ONEBUT 111919 «%s»"), *T->GetLastNoticeText()));
			Shot(R, TEXT("5_pending_code3"));
			return true;
		case 7:   // 00:00: сервер шлёт 6743 с новой ставкой — окно показывает 25%
			WM->ClearMsgBoxQueue(TEXT("самотест налога"));
			T->CloseWindow();
			Net->DebugFeedPacket(6743, Make6743(kRate));
			Npc->RunCommandForCheck(30076, -1);
			Expect(R, T->IsOpen() && T->GetText(79315) == Pct, FString::Printf(TEXT("после 00:00 (6743 налог 25): окно «%s»"), *T->GetText(79315)));
			Shot(R, TEXT("6_after_midnight_25pct"));
			return true;
		default:
			Finish(R);
			return false;
		}
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет подсистемы сети")); return; }
		UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
		const int32 MapWas = LM ? LM->CurrentMapId : -1;
		// Офлайн номер карты выставляется только спавном своего персонажа; на уровне alpen ставим его сами (как `asda.SelfTest.CityWarFlags`).
		if (LM && W->GetMapName().Contains(TEXT("alpen"))) { LM->CurrentMapId = 3; }
		if (AsdaUi::WarCitySlot(GI) != 0) { if (LM) { LM->CurrentMapId = MapWas; } AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("запускать на Альпии (слот города 0)")); return; }
		if (GRun.IsValid()) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("самотест уже идёт")); return; }
		GRun = MakeShared<FRun>();
		FRun& R = *GRun;
		R.GI = GI; R.bQuit = Args.bQuit;
		R.Lang = FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en");
		R.ShotDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../Reports/batch18/city_tax")));
		R.SavedCharId = Net->SelectedCharId;
		R.SavedRawSlot = Net->LastSelectedRawSlot;
		R.SavedMapId = MapWas;
		for (int32 I = 0; I < 3; ++I) { R.SavedCity[I] = Net->CityOwner[I]; }
		R.bSavedCityReceived = Net->bCityOwnerReceived;
		Net->SelectedCharId = kSelfCharId;
		Net->LastSelectedRawSlot = kSelfIdx;
		TWeakPtr<FRun> Weak = GRun;
		R.CapGen = AsdaSelfTestKit::ArmCapture(Net, [Weak](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			TSharedPtr<FRun> S = Weak.Pin();
			if (S && Op == 6744 && Len >= 5) { ++S->Sent6744; S->Last6744Rate = P[4]; }
		}, 0, kCmd);
		if (R.CapGen == 0) { GRun.Reset(); AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("перехват отправок занят")); return; }
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест налога: старт")); }
		R.StepAt = 0.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&OnTick), 0.1f);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestCityTaxSave(
	TEXT("asda.SelfTest.CityTaxSave"),
	TEXT("Самотест: повтор сценария Player58 — окно налога 79300 главой клана-хозяина, 6744 25%, ответы 6745 код 1 (111912) и "
	     "код 3 (111919), окно до и после 00:00; кадры в Reports/batch18/city_tax. Только офлайн, на Альпии. [quit]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCityTaxSaveSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
