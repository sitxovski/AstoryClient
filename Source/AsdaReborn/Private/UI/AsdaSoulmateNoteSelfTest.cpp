// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ ЗАПИСОК ДРУГУ (26.10.2026, агент smmessage): `asda.Soulmate.NoteSelfTest [quit]`. [ПОРТ]
//   Путь игрока: окно сообщества 6400 на вкладке «Друг» (`OpenOnTab(0)`), НАСТОЯЩИЙ щелчок по кнопке 6583 (`SimulateClickAt` —
//   события Slate), набор настоящими символьными событиями (`ProcessKeyCharEvent` — уходят ТОЛЬКО в поле с фокусом), Enter
//   настоящей клавишей (через препроцессор EnterConfirm), щелчок по «Отправить»/«Отмена» и по крестику окна-свитка. Приём —
//   `DebugFeedPacket` → `HandlePacket` → делегат. Значок 9 — `HandleNoticeIconClick` (тот же путь, что щелчок мыши после
//   хит-теста рамки). Исходящие перехватываются (`BeginSendCapture`) — в сеть не уходит ничего. Офлайн (-AsdaNoNet), карта мира.
//   Этапы:
//     1  6583 → окно 6605 видно, подписи 12174/12175, фокус в поле 6602;
//     2  набор + Enter → 6052: payload 69, @0 = СВОЙ charId, текст cp1251, апостроф → «`»; окно спрятано;
//     3  контроли ключей: NoteSelfCharId 0 → @0 = charId Друга (прежний порт); NoteEnterSend 0 → Enter не шлёт; «Отмена» прячет;
//     4  6053 вид 3/2/0 → нотисы 12178/12179/12180; вид 3 чужой — молча; вид 1 мне → очередь, 12181; вид 1 чужой — молча;
//     5  значок 9 на панели → клик → окно 6610: заголовок 12176, текст, время 12177; 6054 с номером; значок погас, очередь пуста;
//     6  ещё две записки → два значка; клик → 6620 (6610 занято), значок остался один; клик → 6630; четвёртая → перезапись 6630;
//        крестики 6612/6622 прячут свои окна;
//     7  вход в мир (сброс EnteringWorld): очередь и окна чисты; 6055 (2 моих + пустая) → 2 записки, 2 значка;
//     8  смена карты: панель уведомлений пересоздана — значки вернулись; повтор 6055 — очередь не удвоилась; окно-свиток,
//        снесённое вместе с миром, вернулось с тем же текстом;
//     9  перезаход (сброс LeftWorld): очередь, значки, окна — пусто;
//     10 ключ NoteRecv 0 — 6053 вид 1 не кладёт записку (прежний порт).
//   Итог: «ЗАПИСКИ-ДРУГУ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Login/AsdaLoginData.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Text/AsdaLang.h"           // язык сессии — сверка текстов 12181/12177 по языку
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::FindLabel
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UnrealClient.h"   // FScreenshotRequest — кадры `shots=<папка>`

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulNoteTest, Log, All);

namespace AsdaSoulNoteTest
{
	static const TCHAR* kCmd = TEXT("asda.Soulmate.NoteSelfTest");   // имя команды
	static constexpr int32  kMe = 1001;          // свой charId на прогон
	static constexpr uint16 kMyUserNum = 321;    // свой userNum
	static constexpr int32  kMateU = 777;        // userNum Друга
	static constexpr int32  kMateCid = 900777;   // charId Друга
	static constexpr int32  kSlot = 5;           // слот пары (Smidx)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                  // счёт
		TWeakObjectPtr<UWorld> W;                // мир прогона
		bool bQuit = false;                      // выйти после итога
		bool bCap = false;                       // перехват исходящих взведён
		TUniquePtr<FAsdaNetSelfScope> NetScope;  // свои charId/userNum — вернуть в конце
		TArray<TArray<uint8>> Sent6052;          // перехваченные тела 6052
		TArray<TArray<uint8>> Sent6054;          // перехваченные тела 6054
		int32 Seq = 0;                           // след нотисов до шага
		FString ShotDir;                         // `shots=<папка>` — куда класть кадры (пусто — без кадров)
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaSoulNoteTest, Log, TEXT("ЗАПИСКИ-ДРУГУ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaSoulNoteTest, Error, TEXT("ЗАПИСКИ-ДРУГУ FAIL %s"), *What); }
		}
	};
	using FCtxRef = TSharedRef<FCtx>;

	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	using AsdaSelfTestCtx::Sub;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	static UAsdaSoulmateSubsystem* Sm(const FCtxRef& C) { return Sub<UAsdaSoulmateSubsystem>(C); }
	static UAsdaHotbarSubsystem* Hb(const FCtxRef& C) { return Sub<UAsdaHotbarSubsystem>(C); }
	using AsdaSelfTestCtx::Feed;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия отличалась только тем, что подсистема сети бралась через `Sub<UAsdaNetSubsystem>` — то же, что `AsdaTestNet::Get`.
	using AsdaSelfTestCtx::ByStage::After;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Кадр экрана с интерфейсом в папку `shots=` (снимается в конце кадра — следующий шаг ставить таймером).
	static void Shot(const FCtxRef& C, const TCHAR* Name)
	{
		if (C->ShotDir.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(C->ShotDir, FString(Name) + TEXT(".png"));
		UE_LOG(LogAsdaSoulNoteTest, Log, TEXT("ЗАПИСКИ-ДРУГУ кадр → %s"), *Path);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/true, /*bAddFilenameSuffix*/false);
	}
	static FString Loc(const FCtxRef& C, int32 Id) { return AsdaNotice::Loc(GIOf(C), Id); }
	static UUserWidget* Wnd(const FCtxRef& C) { UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C); return Cm ? Cm->GetWindowWidget() : nullptr; }
	static FString TxtOf(UUserWidget* W, const FString& Idc)
	{
		const UTextBlock* T = W ? AsdaCommunityUi::FindLabel(W, Idc) : nullptr;
		return T ? T->GetText().ToString() : FString(TEXT("<нет узла>"));
	}
	// Настоящий щелчок мыши по центру узла (события Slate через менеджер окон).
	static bool ClickNode(const FCtxRef& C, UUserWidget* Host, const TCHAR* Name)
	{
		UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C);
		UWidget* N = Host ? Host->GetWidgetFromName(FName(Name)) : nullptr;
		FVector2D Mn, Mx;
		if (!WM || !N || !AsdaUi::WidgetAbsRect(N, Mn, Mx))
		{
			C->Expect(false, FString::Printf(TEXT("у «%s» нет живой геометрии — щёлкнуть нельзя"), Name));
			return false;
		}
		WM->SimulateClickAt((Mn + Mx) * 0.5f, 3);
		return true;
	}
	static void TypeChars(const FString& S)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		for (int32 i = 0; i < S.Len(); ++i) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(S[i], FModifierKeysState(), 0, false)); }
	}
	static void PressEnter()
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13));
		FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13));
	}
	static FString FieldText(const FCtxRef& C)
	{
		UUserWidget* W = Wnd(C);
		const UEditableText* E = W ? Cast<UEditableText>(W->GetWidgetFromName(TEXT("Soul_Msg_Input"))) : nullptr;
		return E ? E->GetText().ToString() : FString(TEXT("<нет поля>"));
	}

	// 5220 своя запись: слот, Друг в игре, Друг.
	static TArray<uint8> Body5220()
	{
		TArray<uint8> B; B.SetNumZeroed(101);
		AsdaBytes::WrI32(&B[0], kMe); AsdaBytes::WrI16(&B[4], kMyUserNum);
		AsdaBytes::WrI32(&B[6], kSlot); AsdaBytes::WrI32(&B[19], kMateCid); AsdaBytes::WrI16(&B[23], kMateU);
		B[25] = 1;
		return B;
	}
	// 5110 запись пары (251 б): слот, Друг, имя — нужна контролю ключа NoteSelfCharId 0 (charId Друга из записи).
	static TArray<uint8> Body5110()
	{
		TArray<uint8> B; B.SetNumZeroed(251);
		AsdaBytes::WrI32(&B[0], kSlot); AsdaBytes::WrI32(&B[4], kMateCid); AsdaBytes::WrI16(&B[8], kMateU);
		B[29] = 30; B[50] = 1;
		FAsdaLoginData::EncodeCp1251(TEXT("Mate"), &B[63], 19);
		return B;
	}
	// 6053 (100 б): @6 charId, @10 вид, @11 номер, @15 текст, @80..@99 время.
	static TArray<uint8> Body6053(int32 CharId, int8 Kind, int32 NoteId, const FString& Text)
	{
		TArray<uint8> B; B.SetNumZeroed(100);
		AsdaBytes::WrI16(&B[4], kMyUserNum);
		AsdaBytes::WrI32(&B[6], CharId); B[10] = (uint8)Kind; AsdaBytes::WrI32(&B[11], NoteId);
		FAsdaLoginData::EncodeCp1251(Text, &B[15], 64);
		const int32 T[5] = { 2026, 10, 26, 14, 5 };
		for (int32 k = 0; k < 5; ++k) { AsdaBytes::WrI32(&B[80 + 4 * k], T[k]); }
		return B;
	}
	// Запись 93 б (6055).
	static void Rec93(TArray<uint8>& B, int32 J, int32 CharId, int32 NoteId, const FString& Text, int32 Day)
	{
		uint8* P = &B[93 * J];
		AsdaBytes::WrI32(P + 0, CharId); AsdaBytes::WrI32(P + 4, NoteId);
		FAsdaLoginData::EncodeCp1251(Text, P + 8, 64);
		const int32 T[5] = { 2026, 10, Day, 9, 30 };
		for (int32 k = 0; k < 5; ++k) { AsdaBytes::WrI32(P + 73 + 4 * k, T[k]); }
	}
	static TArray<uint8> Body6055AB()
	{
		TArray<uint8> B; B.SetNumZeroed(279 + 7);   // + паддинг, как у живого сервера (тело 284…298 б)
		Rec93(B, 0, kMe, 90, TEXT("A1"), 24);
		Rec93(B, 1, kMe, 91, TEXT("B2"), 25);
		AsdaBytes::WrI32(&B[93 * 2], -1);            // пустая запись (сервер её не заполняет)
		return B;
	}
	// Ожидаемое время 12177 — подстановка [sd1]..[sd5] (слоты без цвета = «%d»).
	static FString WantTime(const FCtxRef& C, int32 Y, int32 Mo, int32 D, int32 H, int32 Mi)
	{
		return Loc(C, 12177).Replace(TEXT("[sd1]"), *FString::FromInt(Y)).Replace(TEXT("[sd2]"), *FString::FromInt(Mo))
			.Replace(TEXT("[sd3]"), *FString::FromInt(D)).Replace(TEXT("[sd4]"), *FString::FromInt(H)).Replace(TEXT("[sd5]"), *FString::FromInt(Mi));
	}
	static FString NoteTxt(const FCtxRef& C, int32 I) { return TxtOf(Sm(C)->GetNoteWindowForCheck(I), I == 0 ? FString(TEXT("Soul_RMsgTxt")) : FString::Printf(TEXT("Soul_RMsgTxt%d"), I + 1)); }
	static FString NoteTime(const FCtxRef& C, int32 I) { return TxtOf(Sm(C)->GetNoteWindowForCheck(I), I == 0 ? FString(TEXT("Soul_RMsgTimeTxt")) : FString::Printf(TEXT("Soul_RMsgTimeTxt%d"), I + 1)); }
	static FString NoteTitle(const FCtxRef& C, int32 I) { return TxtOf(Sm(C)->GetNoteWindowForCheck(I), I == 0 ? FString(TEXT("Soul_RMsgTitle_Move")) : FString::Printf(TEXT("Soul_RMsgTitle_Move%d"), I + 1)); }

	static void S1(FCtxRef C); static void S2(FCtxRef C); static void S2b(FCtxRef C); static void S3(FCtxRef C);
	static void S3b(FCtxRef C); static void S3c(FCtxRef C); static void S3d(FCtxRef C); static void S3e(FCtxRef C);
	static void S4(FCtxRef C); static void S5(FCtxRef C); static void S5b(FCtxRef C); static void S5c(FCtxRef C); static void S6(FCtxRef C); static void S6b(FCtxRef C);
	static void S6c(FCtxRef C); static void S7(FCtxRef C); static void S8(FCtxRef C); static void S8b(FCtxRef C);
	static void S8c(FCtxRef C); static void S9(FCtxRef C); static void Finish(FCtxRef C);

	// ═══ 1: кнопка 6583 «Сообщение» — настоящий щелчок ═══
	static void S1(FCtxRef C)
	{
		C->Expect(Wnd(C) != nullptr && Sm(C) && Hb(C), TEXT("подготовка: окно сообщества 6400 открыто на вкладке «Друг», панель уведомлений есть"));
		if (!Wnd(C) || !Sm(C) || !Hb(C)) { Finish(C); return; }
		C->Expect(Sm(C)->GetFriendSlot() == kSlot, FString::Printf(TEXT("подготовка: слот Друга %d (5220)"), Sm(C)->GetFriendSlot()));
		C->Expect(!Sm(C)->IsNotePanelShown(), TEXT("1: до щелчка окно записки 6605 скрыто"));
		ClickNode(C, Wnd(C), TEXT("Soul_MessageBtn"));
		After(C, 0.5f, &S2);
	}
	static void S2(FCtxRef C)
	{
		C->Expect(Sm(C)->IsNotePanelShown(), TEXT("1: щелчок 6583 — окно записки 6605 показано"));
		C->Expect(TxtOf(Wnd(C), TEXT("Soul_Msg_TitleTxt")) == Loc(C, 12174),
			FString::Printf(TEXT("1: заголовок 6600 «%s» (loc 12174)"), *TxtOf(Wnd(C), TEXT("Soul_Msg_TitleTxt"))));
		C->Expect(TxtOf(Wnd(C), TEXT("Soul_Msg_Txt01")) == Loc(C, 12175),
			FString::Printf(TEXT("1: подпись 6601 «%s» (loc 12175)"), *TxtOf(Wnd(C), TEXT("Soul_Msg_Txt01"))));
		C->Expect(Sm(C)->IsNoteInputFocused(), TEXT("1: фокус клавиатуры в поле 6602 (как `CControl_SetFocus(+60, 1)`)"));
		TypeChars(TEXT("Hi Друг 'x"));
		Shot(C, TEXT("1_note_input"));
		After(C, 0.3f, &S2b);
	}
	static void S2b(FCtxRef C)
	{
		C->Expect(FieldText(C) == TEXT("Hi Друг 'x"), FString::Printf(TEXT("2: набор с клавиатуры попал в поле: «%s»"), *FieldText(C)));
		PressEnter();
		After(C, 0.3f, &S3);
	}
	// ═══ 2: Enter → 6052 ═══
	static void S3(FCtxRef C)
	{
		C->Expect(C->Sent6052.Num() == 1, FString::Printf(TEXT("2: Enter в поле — ушёл ровно один 6052 (ушло %d)"), C->Sent6052.Num()));
		if (C->Sent6052.Num() >= 1)
		{
			const TArray<uint8>& B = C->Sent6052[0];
			C->Expect(B.Num() == 69, FString::Printf(TEXT("2: payload 6052 = 69 б (%d)"), B.Num()));
			if (B.Num() >= 69)
			{
				C->Expect(AsdaBytes::RdI32(B.GetData()) == kMe, FString::Printf(TEXT("2: 6052 @0 = СВОЙ charId %d (%d)"), kMe, AsdaBytes::RdI32(B.GetData())));
				const FString T = FAsdaLoginData::DecodeCp1251(B.GetData() + 4, 65);
				C->Expect(T == TEXT("Hi Друг `x"), FString::Printf(TEXT("2: текст cp1251, апостроф → «`»: «%s»"), *T));
			}
		}
		C->Expect(!Sm(C)->IsNotePanelShown(), TEXT("2: после отправки окно 6605 спрятано"));
		// ── 3а: ключ NoteSelfCharId 0 — прежний порт (charId Друга) ──
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteSelfCharId"), 0);
		ClickNode(C, Wnd(C), TEXT("Soul_MessageBtn"));
		After(C, 0.4f, &S3b);
	}
	static void S3b(FCtxRef C)
	{
		TypeChars(TEXT("a"));
		ClickNode(C, Wnd(C), TEXT("Soul_Msg_OkBtn"));
		After(C, 0.4f, &S3c);
	}
	static void S3c(FCtxRef C)
	{
		const bool bGot = C->Sent6052.Num() == 2 && C->Sent6052[1].Num() >= 4;
		C->Expect(bGot && AsdaBytes::RdI32(C->Sent6052[1].GetData()) == kMateCid,
			TEXT("3: «Отправить» 6603 щелчком при ключе NoteSelfCharId 0 — @0 = charId Друга (прежний порт, контроль ключа)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteSelfCharId"), 1);
		// ── 3б: ключ NoteEnterSend 0 — Enter в поле ничего не шлёт (как оригинал) ──
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteEnterSend"), 0);
		ClickNode(C, Wnd(C), TEXT("Soul_MessageBtn"));
		After(C, 0.4f, &S3d);
	}
	static void S3d(FCtxRef C)
	{
		C->Expect(Sm(C)->IsNoteInputFocused(), TEXT("3: повторное открытие — фокус снова в поле 6602"));
		TypeChars(TEXT("b"));
		PressEnter();
		After(C, 0.3f, &S3e);
	}
	static void S3e(FCtxRef C)
	{
		C->Expect(C->Sent6052.Num() == 2 && Sm(C)->IsNotePanelShown(), TEXT("3: ключ NoteEnterSend 0 — Enter не отправил, окно на месте (оригинал)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteEnterSend"), 1);
		ClickNode(C, Wnd(C), TEXT("Soul_Msg_ClBtn"));
		After(C, 0.4f, &S4);
	}
	// ═══ 4: 6053 — итоги отправки и записка мне ═══
	static void S4(FCtxRef C)
	{
		C->Expect(!Sm(C)->IsNotePanelShown() && C->Sent6052.Num() == 2, TEXT("3: «Отмена» 6604 — окно спрятано, ничего не ушло"));
		// Окно сообщества дальше не нужно — закрываем, чтобы кадры показали панель уведомлений и окна-свитки без перекрытия.
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		struct { int8 Kind; int32 Cid; int32 Loc; const TCHAR* What; } const K[] = {
			{ 3, kMe, 12178, TEXT("вид 3 мне → 12178 «отправлено»") }, { 2, kMe, 12179, TEXT("вид 2 → 12179 «больше трёх»") },
			{ 0, kMe, 12180, TEXT("вид 0 → 12180 «не удалось»") } };
		for (const auto& E : K)
		{
			const int32 Was = Sm(C)->GetNoteNoticeSeq();
			Feed(C, 6053, Body6053(E.Cid, E.Kind, 0, FString()));
			C->Expect(Sm(C)->GetNoteNoticeSeq() == Was + 1 && Sm(C)->GetNoteLastNoticeLoc() == E.Loc, FString::Printf(TEXT("4: 6053 %s"), E.What));
		}
		{
			const int32 Was = Sm(C)->GetNoteNoticeSeq();
			Feed(C, 6053, Body6053(kMateCid, 3, 0, FString()));
			C->Expect(Sm(C)->GetNoteNoticeSeq() == Was, TEXT("4: 6053 вид 3 чужого charId — молча (как оригинал)"));
		}
		C->Seq = Sm(C)->GetNoteNoticeSeq();
		Feed(C, 6053, Body6053(kMe, 1, 77, TEXT("Привет, Друг!")));
		C->Expect(Sm(C)->GetNoteQueueNum() == 1 && Sm(C)->GetNoteLastNoticeLoc() == 12181 && Sm(C)->GetNoteNoticeSeq() == C->Seq + 1,
			TEXT("4: 6053 вид 1 мне — записка в очереди, нотис 12181"));
		Feed(C, 6053, Body6053(kMateCid, 1, 78, TEXT("чужая")));
		C->Expect(Sm(C)->GetNoteQueueNum() == 1, TEXT("4: 6053 вид 1 чужому charId — не моя, в очередь не легла"));
		After(C, 0.3f, &S5);
	}
	// ═══ 5: значок письма → окно 6610 → прочитано ═══
	static void S5(FCtxRef C)
	{
		C->Expect(Hb(C)->HasNoticeIcon(9) && Sm(C)->GetNoteIconsShown() == 1, TEXT("5: на панели уведомлений значок письма (тип 9, mes_tag1)"));
		UE_LOG(LogAsdaSoulNoteTest, Log, TEXT("ЗАПИСКИ-ДРУГУ значки: %s"), *Hb(C)->DumpNoticeIcons());
		Shot(C, TEXT("2_letter_icon"));
		After(C, 0.4f, &S5b);
	}
	static void S5b(FCtxRef C)
	{
		const int32 Conf = C->Sent6054.Num();
		C->Expect(Hb(C)->HandleNoticeIconClick(9), TEXT("5: клик по значку 9 принят"));
		C->Expect(Sm(C)->IsNoteWindowShown(0), TEXT("5: открыто окно-свиток 6610"));
		C->Expect(NoteTitle(C, 0) == Loc(C, 12176), FString::Printf(TEXT("5: заголовок 6611 «%s» (loc 12176)"), *NoteTitle(C, 0)));
		C->Expect(NoteTxt(C, 0) == TEXT("Привет, Друг!"), FString::Printf(TEXT("5: текст 6613 «%s»"), *NoteTxt(C, 0)));
		C->Expect(NoteTime(C, 0) == WantTime(C, 2026, 10, 26, 14, 5), FString::Printf(TEXT("5: время 6614 «%s» (ждём «%s»)"), *NoteTime(C, 0), *WantTime(C, 2026, 10, 26, 14, 5)));
		C->Expect(C->Sent6054.Num() == Conf + 1 && C->Sent6054.Last().Num() == 4 && AsdaBytes::RdI32(C->Sent6054.Last().GetData()) == 77,
			TEXT("5: ушёл 6054 (payload 4) с номером записки 77 — «прочитано»"));
		C->Expect(!Hb(C)->HasNoticeIcon(9) && Sm(C)->GetNoteQueueNum() == 0, TEXT("5: значок погас, очередь пуста"));
		C->Expect(!Sm(C)->OpenNextNote(), TEXT("5: повторный клик при пустой очереди — ничего (как оригинал)"));
		// ★ 26.10.2026 правка переводов (координатор): RU 12181 — получателю «пришло», EN 12177 — «Send:» и «час:минута».
		if (FAsdaLang::IsRu())
		{
			C->Expect(Loc(C, 12181) == TEXT("Пришло сообщение от Друга."), FString::Printf(TEXT("5: RU 12181 «%s» (ждём «Пришло сообщение от Друга.»)"), *Loc(C, 12181)));
			C->Expect(NoteTime(C, 0) == TEXT("Отправка 2026-10-26 14:5"), FString::Printf(TEXT("5: RU время на экране «%s»"), *NoteTime(C, 0)));
		}
		else
		{
			C->Expect(Loc(C, 12181) == TEXT("Your Soulmate's message has arrived."), FString::Printf(TEXT("5: EN 12181 «%s»"), *Loc(C, 12181)));
			C->Expect(NoteTime(C, 0) == TEXT("Send: 2026-10-26 14:5"), FString::Printf(TEXT("5: EN время на экране «%s» (ждём «Send: 2026-10-26 14:5»)"), *NoteTime(C, 0)));
		}
		Shot(C, TEXT("3_note_window"));
		After(C, 0.4f, &S5c);
	}
	static void S5c(FCtxRef C)
	{
		Feed(C, 6053, Body6053(kMe, 1, 78, TEXT("Второе")));
		Feed(C, 6053, Body6053(kMe, 1, 79, TEXT("Третье")));
		After(C, 0.3f, &S6);
	}
	// ═══ 6: три окна-свитка ═══
	static void S6(FCtxRef C)
	{
		C->Expect(Sm(C)->GetNoteIconsShown() == 2 && Hb(C)->HasNoticeIcon(9), TEXT("6: две записки — два значка 9"));
		Hb(C)->HandleNoticeIconClick(9);
		C->Expect(Sm(C)->IsNoteWindowShown(0) && Sm(C)->IsNoteWindowShown(1) && NoteTxt(C, 1) == TEXT("Второе"),
			FString::Printf(TEXT("6: 6610 занято — записка открыта в 6620 («%s»)"), *NoteTxt(C, 1)));
		C->Expect(Hb(C)->HasNoticeIcon(9) && Sm(C)->GetNoteIconsShown() == 1, TEXT("6: клик снял ОДИН значок — второй висит"));
		Hb(C)->HandleNoticeIconClick(9);
		C->Expect(Sm(C)->IsNoteWindowShown(2) && NoteTxt(C, 2) == TEXT("Третье") && !Hb(C)->HasNoticeIcon(9),
			TEXT("6: третья — в 6630, значков не осталось"));
		Feed(C, 6053, Body6053(kMe, 1, 80, TEXT("Четвёртое")));
		After(C, 0.3f, &S6b);
	}
	static void S6b(FCtxRef C)
	{
		Hb(C)->HandleNoticeIconClick(9);
		C->Expect(Sm(C)->IsNoteWindowShown(2) && NoteTxt(C, 2) == TEXT("Четвёртое") && NoteTxt(C, 0) == TEXT("Привет, Друг!"),
			TEXT("6: все три окна видны — четвёртая перезаписала 6630 (как оригинал)"));
		// Три окна стоят в одной точке (у всех `AlignXPos/AlignYPos = 16 51 -340 / 8 51 -340` в `.JUI`) — верхнее 6630 (показано
		//   последним); щелчок по его крестику 6632 обязан спрятать ТОЛЬКО его.
		ClickNode(C, Sm(C)->GetNoteWindowForCheck(2), TEXT("Soul_RMsg_CloseBtn3"));
		After(C, 0.4f, &S6c);
	}
	static void S6c(FCtxRef C)
	{
		C->Expect(!Sm(C)->IsNoteWindowShown(2) && Sm(C)->IsNoteWindowShown(0) && Sm(C)->IsNoteWindowShown(1),
			TEXT("6: крестик 6632 щелчком — спрятано только 6630"));
		Sm(C)->CloseNoteWindow(1);
		Sm(C)->CloseNoteWindow(0);
		// ═══ 7: вход в мир — 6055 ═══
		Sm(C)->ResetSessionState(EAsdaSessionResetReason::EnteringWorld);
		C->Expect(Sm(C)->GetNoteQueueNum() == 0 && !Sm(C)->IsNoteWindowShown(0) && !Sm(C)->IsNoteWindowShown(1) && !Sm(C)->IsNoteWindowShown(2),
			TEXT("7: сброс «вход в мир» — очередь и окна чисты"));
		Feed(C, 6055, Body6055AB());
		C->Expect(Sm(C)->GetNoteQueueNum() == 2, FString::Printf(TEXT("7: 6055 (2 моих + пустая, тело с паддингом) — в очереди %d"), Sm(C)->GetNoteQueueNum()));
		After(C, 0.3f, &S7);
	}
	static void S7(FCtxRef C)
	{
		C->Expect(Sm(C)->GetNoteIconsShown() == 2 && Hb(C)->HasNoticeIcon(9), TEXT("7: непрочитанные при входе — два значка письма"));
		// ═══ 8: смена карты ═══
		Hb(C)->HandleNoticeIconClick(9);   // окно 6610 с «A1» — проверим его возврат
		C->Expect(Sm(C)->IsNoteWindowShown(0) && NoteTxt(C, 0) == TEXT("A1") && NoteTime(C, 0) == WantTime(C, 2026, 10, 24, 9, 30),
			FString::Printf(TEXT("8: записка из 6055 открыта: «%s», «%s»"), *NoteTxt(C, 0), *NoteTime(C, 0)));
		Hb(C)->ClearNoticeIcons();         // панель уведомлений пересоздаётся вместе с миром
		if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(C)) { WM->CloseWindowSilently(FName(TEXT("Soul_RMsgWin"))); }
		Sm(C)->ResetSessionState(EAsdaSessionResetReason::MapChanged);
		C->Expect(Sm(C)->GetNoteQueueNum() == 1, TEXT("8: смена карты — очередь пережила (оригинал её не чистит)"));
		After(C, 0.3f, &S8);
	}
	static void S8(FCtxRef C)
	{
		C->Expect(Sm(C)->GetNoteIconsShown() == 1 && Hb(C)->HasNoticeIcon(9), TEXT("8: значок письма вернулся на новой панели"));
		C->Expect(Sm(C)->IsNoteWindowShown(0) && NoteTxt(C, 0) == TEXT("A1"), TEXT("8: окно-свиток 6610 вернулось с тем же текстом"));
		// Сервер в CharPortalMake присылает непрочитанные заново (A1 уже подтверждена, но 6054 сервер не получил — офлайн): обе.
		Feed(C, 6055, Body6055AB());
		C->Expect(Sm(C)->GetNoteQueueNum() == 2, FString::Printf(TEXT("8: повтор 6055 — голова снята, пачка легла: в очереди %d (не 3)"), Sm(C)->GetNoteQueueNum()));
		After(C, 0.3f, &S8b);
	}
	static void S8b(FCtxRef C)
	{
		C->Expect(Sm(C)->GetNoteIconsShown() == 2, FString::Printf(TEXT("8: значков 9 — %d (по одному на записку)"), Sm(C)->GetNoteIconsShown()));
		// ═══ 9: перезаход ═══
		Sm(C)->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Hb(C)->ClearNoticeIcons();
		After(C, 0.3f, &S8c);
	}
	static void S8c(FCtxRef C)
	{
		C->Expect(Sm(C)->GetNoteQueueNum() == 0 && Sm(C)->GetNoteIconsShown() == 0 && !Hb(C)->HasNoticeIcon(9) && !Sm(C)->IsNoteWindowShown(0),
			TEXT("9: перезаход (LeftWorld) — очередь, значки и окна пусты"));
		// ═══ 10: ключ NoteRecv 0 ═══
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteRecv"), 0);
		Feed(C, 6053, Body6053(kMe, 1, 99, TEXT("x")));
		C->Expect(Sm(C)->GetNoteQueueNum() == 0, TEXT("10: ключ NoteRecv 0 — 6053 не разобран (прежний порт)"));
		AsdaCVarSave::SetForTest(TEXT("asda.Soulmate.NoteRecv"), 1);
		After(C, 0.1f, &S9);
	}
	static void S9(FCtxRef C) { Finish(C); }

	static void Finish(FCtxRef C)
	{
		UWorld* W = C->W.Get();
		if (UAsdaSoulmateSubsystem* S = Sm(C)) { S->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaHotbarSubsystem* H = Hb(C)) { H->ClearNoticeIcons(); }
		if (UAsdaCommunitySubsystem* Cm = Sub<UAsdaCommunitySubsystem>(C)) { Cm->CloseWindow(); }
		if (C->bCap) { if (UAsdaPetSubsystem* Pet = Sub<UAsdaPetSubsystem>(C)) { Pet->EndSendCapture(); } }
		C->NetScope.Reset();
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaSoulNoteTest, TEXT("ЗАПИСКИ-ДРУГУ"), C->Ok, C->Fail);
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(A);
		// Свой персонаж нужен окну мира и панели уведомлений — офлайн его нет: штатный фейковый спавн и повтор через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Extra;   // `shots=<папка>` переживает повторный запуск после спавна
			for (const FString& P : KA.Pos) { if (P.StartsWith(TEXT("shots="), ESearchCase::IgnoreCase)) { Extra += TEXT(" ") + P; } }
			const FString Again = FString::Printf(TEXT("%s selfready%s%s"), kCmd, *Extra, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UAsdaSoulmateSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaCommunitySubsystem* Cm = GI ? GI->GetSubsystem<UAsdaCommunitySubsystem>() : nullptr;
		UAsdaPetSubsystem* Pet = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!Net || !S || !Cm || !Pet) { AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет подсистем")); return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W; C->bQuit = KA.bQuit;
		for (const FString& P : KA.Pos) { if (P.StartsWith(TEXT("shots="), ESearchCase::IgnoreCase)) { C->ShotDir = P.Mid(6); } }
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		C->NetScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelectedCharId = kMe; Net->SelfUserNum = kMyUserNum; Net->bSelfUserNumKnown = true;
		for (const TCHAR* K : { TEXT("asda.Soulmate.NoteSelfCharId"), TEXT("asda.Soulmate.NoteRecv"), TEXT("asda.Soulmate.NoteInputFocus"),
		                        TEXT("asda.Soulmate.NoteEnterSend"), TEXT("asda.Soulmate.NoteIconOnePerClick"), TEXT("asda.Soulmate.PopFrom5220"),
		                        TEXT("asda.UI.EnterConfirm") })
		{
			AsdaCVarSave::SetForTest(K, 1);
		}
		TWeakPtr<FCtx> WC(C);
		C->bCap = Pet->BeginSendCapture([WC](int32 Op, const TArray<uint8>& Body, bool, TCHAR)
		{
			TSharedPtr<FCtx> P = WC.Pin();
			if (!P) { return; }
			if (Op == 6052) { P->Sent6052.Add(Body); }
			if (Op == 6054) { P->Sent6054.Add(Body); }
		}, 0);
		C->Expect(C->bCap, TEXT("подготовка: перехват исходящих взведён (в сеть ничего не уходит)"));
		S->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		Cm->CloseWindow();
		Feed(C, 5220, Body5220());
		Feed(C, 5110, Body5110());
		Cm->OpenOnTab(0);
		After(C, 1.0f, &S1);
	}
}

// Консольная команда самотеста.
static FAutoConsoleCommandWithWorldAndArgs GAsdaSoulmateNoteSelfTest(
	TEXT("asda.Soulmate.NoteSelfTest"),
	TEXT("Самотест записок Другу: кнопка 6583 → окно 6605 с фокусом → набор → Enter/«Отправить» → 6052 (свой charId); 6053 итоги и ")
	TEXT("записка мне → значок письма 9 → окна 6610/6620/6630 → 6054; 6055 при входе; смена карты; перезаход; ключи отката. Офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W) { AsdaSoulNoteTest::Run(A, W); }));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
