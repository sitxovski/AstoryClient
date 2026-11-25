// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ `asda.Login.SoulmateStatusSelfTest` (02.11.2026, волна jl_presm). [ПОРТ]
//   ЗАЧЕМ. Серверная правка jl_presm чинит `Joy1.dbo.UPPreSMConnect` (LoginManager): после неё 1025 приходит с кодом 3/4
//   («соулмейт в сети / нет») вместо 0, а вход одержимым при владельце в сети получает ОТКАЗ 1027 код 2 вместо входа с выбиванием
//   владельца. Проверяем, что клиент путём игрока: (а) верно разбирает 1025 коды 3/4 и не принимает код 0 после 1023 за провал
//   списка (а без 1023 — принимает, как раньше); (б) на 1027 код 2 показывает модалку с loc 131 (RU/EN), снимает признак
//   одержимости, взводит «соулмейт в сети»; (в) наведение на «Играть» даёт подсказку по режиму слота (эталон
//   `CCharSelectScene_onUiEvent@0xF06910`: 34 «Друг уже в игре» / 26 / 33).
//   ПУТЬ ИГРОКА: пакеты — через ШТАТНЫЙ `HandlePacket` (`DebugFeedPacket`), экран выбора — штатный `ShowCharSelectUI` (через
//   `RunCharSelectOfflineDemo`), наведение — делегат `OnHovered` самой кнопки CARASTART_BUT00 (его же зовёт Slate), текст —
//   из отрисованных виджетов WBP_ANI_WIN / MESSAGE_WIN. Только офлайн (`-AsdaNoNet`), карта логина.
//   Запуск: `python Tools/asda_selftest_batch.py --map login "asda.Login.SoulmateStatusSelfTest" [--lang en]`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "AsdaNetSubsystem.h"
#include "AsdaSelfTestBatch.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Login/AsdaLoginData.h"
#include "Login/AsdaLoginGameMode.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "TimerManager.h"
#include "UI/AsdaWindowSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulmateStatusTest, Log, All);

#if ASDA_DEV_CONSOLE   // дев-самотест: в Shipping не собирается

static const TCHAR* kSmsCmd = TEXT("asda.Login.SoulmateStatusSelfTest");   // имя команды (и префикс итога)

// Друг сцены логина: читает/ставит закрытые поля выбора слота (своих копий логики нет — подсказку считает боевой PlayHelperRec).
struct FAsdaSoulmateStatusSelfTest
{
	static int32& Selected(AAsdaLoginGameMode* GM) { return GM->SelectedSlot; }
	static int32& Mode(AAsdaLoginGameMode* GM, int32 S) { return GM->SlotMode[S]; }
	static UUserWidget* CharStart(AAsdaLoginGameMode* GM) { return GM->CharStartWidget; }
	static int32 PlayRec(const AAsdaLoginGameMode* GM) { return GM->PlayHelperRec(); }   // боевой выбор подсказки «Играть»
};

namespace AsdaSmsTest
{
	// Состояние прогона (один прогон за раз).
	struct FRun
	{
		TWeakObjectPtr<AAsdaLoginGameMode> GM;    // сцена логина
		TWeakObjectPtr<UAsdaNetSubsystem> Net;    // сеть (офлайн)
		bool bQuit = false;                       // выйти по концу
		int32 Ok = 0, Fail = 0;                   // счёт проверок
		int32 ListFailed = 0;                     // сколько раз пришло событие «список 1005 не придёт»
		FDelegateHandle ListFailedH;              // подписка на OnCharListFailed
	};
	static TSharedPtr<FRun> GRun;                 // текущий прогон

	// Учёт проверки.
	static void Check(bool bCond, const FString& What)
	{
		if (!GRun.IsValid()) { return; }
		(bCond ? GRun->Ok : GRun->Fail) += 1;
		if (bCond) { UE_LOG(LogAsdaSoulmateStatusTest, Display, TEXT("  OK   %s"), *What); }
		else       { UE_LOG(LogAsdaSoulmateStatusTest, Warning, TEXT("  FAIL %s"), *What); }
	}

	// Весь видимый текст окна (TextBlock + RichTextBlock), через пробел.
	static FString WidgetText(UUserWidget* W)
	{
		FString Out;
		if (!W || !W->WidgetTree) { return Out; }
		W->WidgetTree->ForEachWidget([&Out](UWidget* C)
		{
			if (const URichTextBlock* R = Cast<URichTextBlock>(C)) { Out += R->GetText().ToString() + TEXT(" "); }
			else if (const UTextBlock* T = Cast<UTextBlock>(C))   { Out += T->GetText().ToString() + TEXT(" "); }
		});
		return Out;
	}

	// Окно менеджера окон по имени.
	static UUserWidget* Window(const TCHAR* Id)
	{
		UAsdaNetSubsystem* N = GRun.IsValid() ? GRun->Net.Get() : nullptr;
		UGameInstance* GI = N ? N->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		return WM ? WM->FindWindow(FName(Id)) : nullptr;
	}

	// Сброс транзакции 1005 — те же поля, что чистит `SendCharListRequest` (офлайн он не шлёт и до сброса не доходит).
	static void ResetTxn(UAsdaNetSubsystem* N)
	{
		N->SoulmateBranchCode = -1;
		N->bSoulmateOnline = false;
		N->bSoulmateInfoSecondParagraph = false;
	}

	// Тело 1023 (184 б, как `sub_415A40(1023, 184, …)`): 1 запись — Charidx 10, имя, пол 1, класс 1, ур. 20.
	static TArray<uint8> Body1023()
	{
		TArray<uint8> B; B.SetNumZeroed(184);
		B[0] = 1;
		B[1] = 10;
		const char* Name = "jlpMate";
		for (int32 i = 0; Name[i]; ++i) { B[2 + i] = (uint8)Name[i]; }
		B[1 + 22] = 1; B[1 + 23] = 1; B[1 + 25] = 20;
		return B;
	}

	// Тело 1025 (5 б): код, подкод, в игре, канал, портал.
	static TArray<uint8> Body1025(uint8 Code, uint8 Sub, uint8 InGame, uint8 Ch, uint8 Por)
	{
		return TArray<uint8>({ Code, Sub, InGame, Ch, Por });
	}

	// Ожидаемое ключевое слово текста по языку сессии.
	static const TCHAR* Kw(const TCHAR* Ru, const TCHAR* En) { return FAsdaLang::IsRu() ? Ru : En; }

	// Наведение на «Играть» через делегат самой кнопки → текст помощника.
	static FString HoverPlay(AAsdaLoginGameMode* GM)
	{
		UUserWidget* CS = FAsdaSoulmateStatusSelfTest::CharStart(GM);
		UButton* B = CS ? Cast<UButton>(CS->GetWidgetFromName(TEXT("CARASTART_BUT00"))) : nullptr;
		if (!B) { return FString(TEXT("<нет CARASTART_BUT00>")); }
		B->OnHovered.Broadcast();
		const FString T = WidgetText(Window(TEXT("WBP_ANI_WIN")));
		B->OnUnhovered.Broadcast();
		return T;
	}

	// Проверки после показа экрана выбора.
	static void Body()
	{
		AAsdaLoginGameMode* GM = GRun->GM.Get();
		UAsdaNetSubsystem* N = GRun->Net.Get();
		if (!GM || !N) { Check(false, TEXT("сцена логина/сеть пропали")); return; }
		Check(FAsdaSoulmateStatusSelfTest::CharStart(GM) != nullptr, TEXT("экран выбора персонажа открыт (CARASTART)"));
		Check(Window(TEXT("WBP_ANI_WIN")) != nullptr, TEXT("окно помощника WBP_ANI_WIN есть"));

		// (а) 1025 после 1023: коды 3 / 4 / 0.
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(3, 1, 1, 2, 7));
		Check(N->SoulmateBranchCode == 3 && N->bSoulmateOnline && N->SoulmateChannel == 2 && N->SoulmatePortalId == 7,
			FString::Printf(TEXT("1025 код 3: «в сети» (код=%d в сети=%d канал=%d портал=%d)"), N->SoulmateBranchCode, N->bSoulmateOnline ? 1 : 0, N->SoulmateChannel, N->SoulmatePortalId));
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(4, 1, 0, 0, 0));
		Check(N->SoulmateBranchCode == 4 && !N->bSoulmateOnline, FString::Printf(TEXT("1025 код 4: «не в сети» (в сети=%d)"), N->bSoulmateOnline ? 1 : 0));
		const int32 F0 = GRun->ListFailed;
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(0, 1, 0, 0, 0));
		Check(GRun->ListFailed == F0 && !N->bSoulmateOnline, FString::Printf(TEXT("1025 код 0 ПОСЛЕ 1023 (сбой UPPreSMConnect): провала списка нет (событий %d), «не в сети»"), GRun->ListFailed - F0));
		ResetTxn(N); N->DebugFeedPacket(1025, Body1025(0, 0, 0, 0, 0));
		Check(GRun->ListFailed == F0 + 1, FString::Printf(TEXT("1025 код 0 БЕЗ 1023 (сбой до 1006): событие провала списка, как раньше (событий %d)"), GRun->ListFailed - F0));

		// (в) подсказка «Играть» по режиму слота — до отказа: соулмейт «не в сети».
		int32& Sel = FAsdaSoulmateStatusSelfTest::Selected(GM);
		Sel = 0;
		int32& M0 = FAsdaSoulmateStatusSelfTest::Mode(GM, 0);
		const int32 SavedMode = M0;
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(4, 1, 0, 0, 0));
		M0 = 2;
		FString T = HoverPlay(GM);
		Check(FAsdaSoulmateStatusSelfTest::PlayRec(GM) == 26 && T.Contains(Kw(TEXT("Фантазиума"), TEXT("world of Asda Story"))),
			FString::Printf(TEXT("«Играть», персонаж соулмейта, тот НЕ в сети → подсказка 26: «%s»"), *T.Left(90)));
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(3, 1, 1, 0, 0));
		T = HoverPlay(GM);
		Check(FAsdaSoulmateStatusSelfTest::PlayRec(GM) == 34 && T.Contains(Kw(TEXT("уже в игре"), TEXT("connected to the game"))),
			FString::Printf(TEXT("«Играть», персонаж соулмейта, тот В СЕТИ (1025 код 3) → подсказка 34: «%s»"), *T.Left(90)));
		M0 = 0;
		T = HoverPlay(GM);
		Check(FAsdaSoulmateStatusSelfTest::PlayRec(GM) == 26, FString::Printf(TEXT("«Играть», свой персонаж → подсказка 26: «%s»"), *T.Left(60)));
		M0 = 1;
		T = HoverPlay(GM);
		Check(FAsdaSoulmateStatusSelfTest::PlayRec(GM) == 33 && T.Contains(Kw(TEXT("призраком"), TEXT("as a Soulmate"))),
			FString::Printf(TEXT("«Играть», режим «душа» → подсказка 33: «%s»"), *T.Left(90)));

		// (б) отказ 1027 код 2 при входе одержимым: соулмейт «не в сети» до отказа, признак одержимости был запрошен.
		M0 = 2;
		ResetTxn(N); N->DebugFeedPacket(1023, Body1023()); N->DebugFeedPacket(1025, Body1025(4, 1, 0, 0, 0));
		N->SetSelfSoulState(2);                                  // как если бы вход одержимым уже взвёл признак (страховка ветки отказа)
		TArray<uint8> R1027; R1027.SetNumZeroed(651); R1027[0] = 2;
		N->DebugFeedPacket(1027, R1027);
		UUserWidget* Msg = Window(TEXT("MESSAGE_WIN"));
		const FString MsgText = WidgetText(Msg);
		const FString Want131 = FAsdaLoginData::Loc(131);
		Check(Msg != nullptr && !Want131.IsEmpty() && MsgText.Contains(Want131) && MsgText.Contains(Kw(TEXT("вошел в игру"), TEXT("joined the game"))),
			FString::Printf(TEXT("1027 код 2 → модалка MESSAGE_WIN с loc 131: «%s»"), *MsgText.Left(90)));
		Check(N->bSoulmateOnline, TEXT("1027 код 2 → флаг «соулмейт в сети» взведён (эталон +3075039 = 1)"));
		Check(N->SelfSoulState == 0, FString::Printf(TEXT("1027 код 2 → признак одержимости снят (SelfSoulState=%u)"), (uint32)N->SelfSoulState));
		T = HoverPlay(GM);
		Check(FAsdaSoulmateStatusSelfTest::PlayRec(GM) == 34 && T.Contains(Kw(TEXT("уже в игре"), TEXT("connected to the game"))),
			FString::Printf(TEXT("после отказа «Играть» на персонаже соулмейта → подсказка 34: «%s»"), *T.Left(90)));
		if (UGameInstance* GI = N->GetGameInstance())
		{
			if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->CloseWindow(FName(TEXT("MESSAGE_WIN"))); }
		}
		Check(Window(TEXT("MESSAGE_WIN")) == nullptr, TEXT("модалка отказа закрывается (экран не заперт)"));

		// Возврат состояния.
		M0 = SavedMode;
		ResetTxn(N);
	}

	// Конец прогона.
	static void End(UWorld* W)
	{
		if (!GRun.IsValid()) { return; }
		if (UAsdaNetSubsystem* N = GRun->Net.Get()) { N->OnCharListFailed.Remove(GRun->ListFailedH); }
		const bool bQuit = GRun->bQuit;
		const FString Tail = FString::Printf(TEXT(" язык=%s"), FAsdaLang::IsRu() ? TEXT("ru") : TEXT("en"));
		AsdaSelfTestKit::Finish(W, kSmsCmd, bQuit, LogAsdaSoulmateStatusTest, kSmsCmd, GRun->Ok, GRun->Fail, *Tail);
		GRun.Reset();
	}

	// Старт: офлайн-гейт → экран выбора (штатный) → через 2 с проверки → конец.
	static void Start(const TArray<FString>& Args, UWorld* W)
	{
		const FAsdaSelfTestArgs A = FAsdaSelfTestArgs::Parse(Args);
		if (AsdaSelfTestKit::RefuseStart(W, kSmsCmd, A)) { return; }
		AAsdaLoginGameMode* GM = W ? Cast<AAsdaLoginGameMode>(W->GetAuthGameMode()) : nullptr;
		UAsdaNetSubsystem* N = AsdaSelfTestKit::NetOf(W);
		if (!GM || !N) { AsdaSelfTestKit::Refused(W, kSmsCmd, A.bQuit, TEXT("нет сцены логина или сети (нужна --map login)")); return; }
		GRun = MakeShared<FRun>();
		GRun->GM = GM; GRun->Net = N; GRun->bQuit = A.bQuit;
		GRun->ListFailedH = N->OnCharListFailed.AddLambda([]() { if (GRun.IsValid()) { ++GRun->ListFailed; } });
		GM->RunCharSelectOfflineDemo();
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, [WW]()
		{
			if (GRun.IsValid()) { Body(); }
			End(WW.Get());
		}, 2.0f + A.Delay, false);
		UE_LOG(LogAsdaSoulmateStatusTest, Display, TEXT("%s: экран выбора показан, проверки через %.1f с"), kSmsCmd, 2.0f + A.Delay);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulmateStatusSelfTestCmd(
	kSmsCmd,
	TEXT("[САМОТЕСТ] Статус соулмейта на выборе персонажа: 1025 коды 3/4/0, отказ 1027 код 2 (loc 131), подсказка «Играть» 34/26/33. [delay=N] [quit]. Только офлайн, карта логина."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AsdaSmsTest::Start));

#endif // ASDA_DEV_CONSOLE
