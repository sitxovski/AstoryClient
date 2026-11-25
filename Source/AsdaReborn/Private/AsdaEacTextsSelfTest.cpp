// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ТЕКСТОВ EAC ИГРОКУ (16.10.2026, agent-eac-texts): `asda.SelfTest.EacTexts [quit]`.
//   Тексты 991xxx утверждены владельцем (`Reports/anticheat/eac/PLAYER_TEXTS.md`) и внесены штатным путём: RU —
//   `Tools/data/gametext_overrides.json` → DT_GameText, EN — `Tools/data/loc/en/GameText.json` → DT_Loc_GameText_en.
//   Проверяет РЕАЛЬНЫМ путём показа (закон diagnostic-must-use-real-path), без сети:
//     1) все 30 строк находятся в таблицах на языке сессии (`AsdaNotice::Loc` — та же точка, что у показа): RU —
//        с кириллицей, EN — без неё; каждая строка печатается в журнал (дамп); 991034 и 991040 — НЕ заведены;
//     2) сборка текста окна (`UAsdaNetSubsystem::BuildEacPlayerText` по ключам `AsdaEacLoc::ForFatal`): заголовок + причина,
//        деталь EAC последней строкой (угловые скобки погашены), без строки причины — пусто (запасной нотис оригинала);
//     3) мягкое состояние: событие `UAsdaAntiCheatSubsystem::OnSoftNotice` → ровно одна строка в журнале системного чата
//        (через подписку UAsdaNetSubsystem); не-мягкий ключ строки не даёт;
//     4) попап разрыва 8000 с текстом EAC (`ShowDisconnectPopupText`): текст разбит на строки, каждая — в ширину слота
//        COMMONWIN_TEXT (замер шрифтом контрола), слова не потеряны; попап закрывается.
//   Итог: «EAC-ТЕКСТЫ ИТОГ: ok=N fail=M». Только офлайн (-AsdaNoNet), язык — `-AsdaLang=ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaAntiCheatSubsystem.h"
#include "AsdaEacTypes.h"
#include "Text/AsdaLang.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaDisconnectPopupSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEacTexts, Log, All);

namespace AsdaEacTextsSelfTest
{
	// Имя команды (строка итога, отказ, возврат ключей).
	static const TCHAR* kCmd = TEXT("asda.SelfTest.EacTexts");

	// Есть ли в строке кириллица (признак RU-текста).
	using AsdaSelfTestInput::HasCyrillic;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.

	// Все заведённые ключи EAC: 991000–991015 (нарушения), 991020–991026 (KICK), 991031–991033, 991036–991039.
	static TArray<int32> AllKeys()
	{
		TArray<int32> K;
		for (int32 Id = 991000; Id <= 991015; ++Id) { K.Add(Id); }
		for (int32 Id = 991020; Id <= 991026; ++Id) { K.Add(Id); }
		for (int32 Id = 991031; Id <= 991033; ++Id) { K.Add(Id); }
		for (int32 Id = 991036; Id <= 991039; ++Id) { K.Add(Id); }
		return K;
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!GI) { AsdaSelfTestKit::Refused(W, kCmd, Args.bQuit, TEXT("нет GameInstance")); return; }

		int32 Ok = 0, Fail = 0;
		auto Expect = [&Ok, &Fail](bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaEacTexts, Log, TEXT("EAC-ТЕКСТЫ ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaEacTexts, Error, TEXT("EAC-ТЕКСТЫ FAIL %s"), *What); }
		};
		const FString Lang = FAsdaLang::Code(FAsdaLang::Current());
		const bool bEn = Lang.Equals(TEXT("en"), ESearchCase::IgnoreCase);
		UE_LOG(LogAsdaEacTexts, Log, TEXT("EAC-ТЕКСТЫ язык сессии: %s"), *Lang);

		// ── 1. Строки в таблицах на языке сессии (дамп) ──
		for (const int32 Id : AllKeys())
		{
			const FString S = AsdaNotice::Loc(GI, Id);
			Expect(!S.IsEmpty() && (bEn ? !HasCyrillic(S) : HasCyrillic(S)),
				FString::Printf(TEXT("1: loc %d [%s] «%s»"), Id, *Lang, *S));
		}
		Expect(AsdaNotice::Loc(GI, AsdaEacLoc::kServerDisabledLog).IsEmpty(), TEXT("1: 991034 не заведён (игроку не показывается)"));
		Expect(AsdaNotice::Loc(GI, AsdaEacLoc::kRefreshRejected).IsEmpty(), TEXT("1: 991040 не заведён (показывается 991037)"));

		// ── 2. Сборка текста окна по ключам ForFatal ──
		{
			FAsdaEacFatal F; F.Source = EAsdaEacFatalSource::SidecarKick; F.OurReason = 1; F.LocId = AsdaEacLoc::ForKick(1);
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			const FString T = UAsdaNetSubsystem::BuildEacPlayerText(GI, D.TitleId, D.BodyId, D.bDetails, TEXT("ignored"));
			Expect(T == AsdaNotice::Loc(GI, 991020) + TEXT("\n") + AsdaNotice::Loc(GI, 991021),
				FString::Printf(TEXT("2: KICK 1 → «%s»"), *T.Replace(TEXT("\n"), TEXT(" / "))));
		}
		{
			FAsdaEacFatal F; F.Source = EAsdaEacFatalSource::SidecarKick; F.OurReason = 2; F.LocId = AsdaEacLoc::ForKick(2);
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			const FString T = UAsdaNetSubsystem::BuildEacPlayerText(GI, D.TitleId, D.BodyId, D.bDetails, TEXT("  ClientViolation <x>  "));
			Expect(T == AsdaNotice::Loc(GI, 991020) + TEXT("\n") + AsdaNotice::Loc(GI, 991022) + TEXT("\nClientViolation ‹x›"),
				FString::Printf(TEXT("2: KICK 2 → деталь EAC последней строкой «%s»"), *T.Replace(TEXT("\n"), TEXT(" / "))));
		}
		{
			FAsdaEacFatal F; F.Source = EAsdaEacFatalSource::IntegrityViolation; F.ViolationType = 14; F.LocId = AsdaEacLoc::ForViolation(14);
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			const FString T = UAsdaNetSubsystem::BuildEacPlayerText(GI, D.TitleId, D.BodyId, D.bDetails, TEXT("vm"));
			Expect(T == AsdaNotice::Loc(GI, 991000) + TEXT("\n") + AsdaNotice::Loc(GI, 991014),
				FString::Printf(TEXT("2: нарушение тип 14 → «%s»"), *T.Replace(TEXT("\n"), TEXT(" / "))));
		}
		{
			FAsdaEacFatal F; F.Source = EAsdaEacFatalSource::SessionFailed; F.LocId = AsdaEacLoc::kRefreshRejected; F.FailKind = EAsdaEacFailKind::Auth;
			const AsdaEacLoc::FFatalDisplay D = AsdaEacLoc::ForFatal(F);
			const FString T = UAsdaNetSubsystem::BuildEacPlayerText(GI, D.TitleId, D.BodyId, D.bDetails, TEXT("служебное"));
			Expect(T == AsdaNotice::Loc(GI, 991037), FString::Printf(TEXT("2: 401 /refresh → «%s»"), *T));
		}
		Expect(UAsdaNetSubsystem::BuildEacPlayerText(GI, 991020, 0, false, FString()).IsEmpty(),
			TEXT("2: только заголовок без причины → пусто (показ нотиса оригинала)"));

		// ── 3. Мягкое состояние → одна строка в журнале системного чата ──
		UAsdaAntiCheatSubsystem* Eac = GI->GetSubsystem<UAsdaAntiCheatSubsystem>();
		UAsdaChatSubsystem* Chat = GI->GetSubsystem<UAsdaChatSubsystem>();
		if (!Eac || !Chat)
		{
			Expect(false, TEXT("3: нет подсистемы EAC или чата"));
		}
		else
		{
			for (const int32 Id : { AsdaEacLoc::kSoftRegBadToken, AsdaEacLoc::kSoftRegInternal, AsdaEacLoc::kAuthUnreachable, AsdaEacLoc::kEosFailed })
			{
				const FString S = AsdaNotice::Loc(GI, Id);
				const int32 Before = Chat->DebugCountSystemLines(S);
				Eac->OnSoftNotice.Broadcast(Id);
				const int32 After = Chat->DebugCountSystemLines(S);
				Expect(After == Before + 1, FString::Printf(TEXT("3: мягкое %d → системный чат +1 строка (%d → %d) «%s»"), Id, Before, After, *S));
			}
			const FString Hard = AsdaNotice::Loc(GI, 991032);
			const int32 Before = Chat->DebugCountSystemLines(Hard);
			Eac->OnSoftNotice.Broadcast(991032);
			Expect(Chat->DebugCountSystemLines(Hard) == Before, TEXT("3: не-мягкий ключ 991032 в чат не идёт"));
		}

		// ── 4. Попап разрыва с текстом EAC: перенос по словам в ширину слота ──
		if (UAsdaDisconnectPopupSubsystem* Pop = GI->GetSubsystem<UAsdaDisconnectPopupSubsystem>())
		{
			const FString Src = UAsdaNetSubsystem::BuildEacPlayerText(GI, 991020, 991021, false, FString());
			UUserWidget* Box = Pop->ShowDisconnectPopupText(Src);
			UTextBlock* Tb = Box ? Cast<UTextBlock>(Box->GetWidgetFromName(TEXT("COMMONWIN_TEXT"))) : nullptr;
			const UCanvasPanelSlot* CS = Tb ? Cast<UCanvasPanelSlot>(Tb->Slot) : nullptr;
			if (!Tb || !CS || !FSlateApplication::IsInitialized())
			{
				Expect(false, TEXT("4: попап 8000 / COMMONWIN_TEXT / слот не найден"));
			}
			else
			{
				const FString Shown = Tb->GetText().ToString();
				const float WrapW = (float)CS->GetSize().X;
				const TSharedRef<FSlateFontMeasure> FM = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
				TArray<FString> Lines; Shown.ParseIntoArray(Lines, TEXT("\n"), false);
				bool bFit = true;
				for (const FString& L : Lines)
				{
					const bool bOneWord = !L.Contains(TEXT(" "));
					if (!bOneWord && FM->Measure(L, Tb->GetFont()).X > WrapW) { bFit = false; }
				}
				TArray<FString> W1, W2; Src.ParseIntoArrayWS(W1); Shown.ParseIntoArrayWS(W2);
				Expect(Lines.Num() >= 3 && bFit && W1 == W2,
					FString::Printf(TEXT("4: попап 8000: %d строк в ширину %.0f, слова целы: «%s»"), Lines.Num(), WrapW, *Shown.Replace(TEXT("\n"), TEXT(" / "))));
			}
			Pop->ClosePopup();
			Expect(!Pop->IsOpen(), TEXT("4: попап закрыт"));
		}
		else
		{
			Expect(false, TEXT("4: нет подсистемы попапа разрыва"));
		}

		AsdaSelfTestKit::Finish(W, kCmd, Args.bQuit, LogAsdaEacTexts, TEXT("EAC-ТЕКСТЫ"), Ok, Fail);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestEacTexts(
	TEXT("asda.SelfTest.EacTexts"),
	TEXT("Самотест текстов EAC игроку (991xxx): строки в таблицах на языке сессии (дамп), сборка текста окна, мягкое → системный "
	     "чат один раз, попап 8000 с переносом по ширине. Только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaEacTextsSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
