// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ 06.11.2026 САМОТЕСТ «ЧАТ МЁРТВОГО» — `asda.SelfTest.DeathChat [quit]`.
//   Баг 06.11: мёртвый не может печатать в чат (регресс замка ввода `asda.Death.InputLock`, 05.11; прежний самотест `DeathLock`
//   ввод символов не проверял). ПУТЬ ИГРОКА, офлайн: настоящий 4053, клавиши и символы — события Slate
//   (`ProcessKeyDownEvent` / `ProcessKeyCharEvent` / `ProcessKeyUpEvent`), клик в поле — `ProcessMouseButtonDownEvent` по геометрии
//   поля 2609, исходящие — перехват (5084 общий чат, 5088 шёпот).
//   Пути: Enter · R (ответ на шёпот) · Ctrl+W (шёпот) · клик в поле. При открытом поле буквы и цифры не срабатывают как горячие.
//   После закрытия поля замок на месте: клавиша инвентаря мёртвому молчит. Итог: «СМЕРТЬ-ЧАТ ИТОГ: ok=N fail=M».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaAfterDeathSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Components/EditableText.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDeathChatTest, Log, All);

namespace AsdaDeathChatSelfTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.DeathChat");   // имя команды (итог, отказ, ключи)
	static constexpr uint16 kOpNormal = 5084;                     // C→S общий чат (`SendChatShout5084`, режим 4)
	static constexpr uint16 kOpWhisper = 5088;                    // C→S шёпот
	static constexpr float kKeyGap = 0.12f;                       // пауза между событиями клавиши (кадры PollKeys)

	// Перехваченный исходящий пакет.
	struct FSent { uint16 Op = 0; TArray<uint8> Body; };

	// Состояние прогона (шаги идут таймерами: фокус Slate и опрос клавиш живут по кадрам).
	struct FState
	{
		TWeakObjectPtr<UWorld> W;
		FAsdaSelfTestArgs Args;
		TUniquePtr<FAsdaNetSelfScope> SelfScope;
		TUniquePtr<FAsdaCVarScope> Cv;
		TArray<FSent> Sent;
		uint32 CapGen = 0;
		int32 Ok = 0, Fail = 0;
		int32 Uidx = 7;
		int32 N5086 = 0, N5088 = 0;
		TArray<TPair<float, TFunction<void(FState&)>>> Steps;
		int32 Next = 0;

		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		UAsdaChatWindowSubsystem* Chat() const { return Sub<UAsdaChatWindowSubsystem>(); }
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaDeathChatTest, Log, TEXT("СМЕРТЬ-ЧАТ ok   %s"), *What); }
			else { ++Fail; UE_LOG(LogAsdaDeathChatTest, Error, TEXT("СМЕРТЬ-ЧАТ FAIL %s"), *What); }
		}
		int32 Count(uint16 Op) const { int32 N = 0; for (const FSent& S : Sent) { if (S.Op == Op) { ++N; } } return N; }
		void Then(float Delay, TFunction<void(FState&)> F) { Steps.Emplace(Delay, MoveTemp(F)); }
		// Текст поля 2609 из дампа «латч против факта» (`DebugInputState`, тот же, что консоль asda.ChatWin.InputState).
		FString FieldText() const
		{
			const UAsdaChatWindowSubsystem* C = Chat();
			FString L, R, T;
			if (C && C->DebugInputState().Split(TEXT("текст«"), &L, &R)) { R.Split(TEXT("»"), &T, nullptr); }
			return T;
		}
		bool FieldFocused() const { const UAsdaChatWindowSubsystem* C = Chat(); return C && C->DebugInputState().Contains(TEXT("фокус=1")); }
		bool Typing() const { const UAsdaChatWindowSubsystem* C = Chat(); return C && C->IsInputActive(); }
	};

	// Клавиша, как её строит Slate из WM_KEYDOWN + WM_CHAR + WM_KEYUP. Vk — виртуальный код, Ch — символ (0 — без WM_CHAR).
	static void KeyDown(const FKey& K, uint32 Vk, TCHAR Ch)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(K, FModifierKeysState(), 0, false, (uint32)Ch, Vk));
		if (Ch) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Ch, FModifierKeysState(), 0, false)); }
	}
	static void KeyUp(const FKey& K, uint32 Vk, TCHAR Ch)
	{
		if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(K, FModifierKeysState(), 0, false, (uint32)Ch, Vk)); }
	}
	// Шаги «нажать → отпустить».
	static void AddTap(FState& S, const FKey& K, uint32 Vk, TCHAR Ch)
	{
		S.Then(kKeyGap, [K, Vk, Ch](FState&) { KeyDown(K, Vk, Ch); });
		S.Then(kKeyGap, [K, Vk, Ch](FState&) { KeyUp(K, Vk, Ch); });
	}
	// Набрать слово буквами a–z / цифрами настоящими событиями.
	static void AddType(FState& S, const FString& Word)
	{
		for (const TCHAR C : Word)
		{
			const TCHAR Up = FChar::ToUpper(C);
			const FKey K(FName(*FString::Chr(Up)));
			const FKey Key = FChar::IsDigit(C) ? FKey(FName(*(FString(TEXT("")) + (C == TEXT('0') ? TEXT("Zero") : C == TEXT('1') ? TEXT("One") : TEXT("Two"))))) : K;
			AddTap(S, Key, (uint32)Up, C);
		}
	}
	// Клик левой кнопкой по центру поля ввода (настоящая геометрия виджета).
	static void ClickAt(UWidget* Ed, const FVector2D& Rel, bool bDown)
	{
		if (!Ed || !FSlateApplication::IsInitialized()) { return; }
		const FGeometry G = Ed->GetCachedGeometry();
		const FVector2D At = G.LocalToAbsolute(G.GetLocalSize() * Rel);
		FSlateApplication& App = FSlateApplication::Get();
		TSet<FKey> Btn;
		if (bDown) { Btn.Add(EKeys::LeftMouseButton); }
		const FPointerEvent E(0, At, At, Btn, EKeys::LeftMouseButton, 0.f, FModifierKeysState());
		if (bDown)
		{
			App.ProcessMouseMoveEvent(FPointerEvent(0, At, At, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
			App.ProcessMouseButtonDownEvent(nullptr, E);
		}
		else { App.ProcessMouseButtonUpEvent(E); }
		UE_LOG(LogAsdaDeathChatTest, Log, TEXT("СМЕРТЬ-ЧАТ клик %s по полю 2609 в (%.0f, %.0f), размер (%.0f×%.0f)"), bDown ? TEXT("вниз") : TEXT("вверх"),
			At.X, At.Y, G.GetLocalSize().X, G.GetLocalSize().Y);
	}

	static void ClickField(FState& T, bool bDown)
	{
		UAsdaChatWindowSubsystem* C = T.Chat();
		const FObjectProperty* P = C ? CastField<FObjectProperty>(C->GetClass()->FindPropertyByName(TEXT("InputEdit"))) : nullptr;
		ClickAt(P ? Cast<UEditableText>(P->GetObjectPropertyValue_InContainer(C)) : nullptr, FVector2D(0.5, 0.5), bDown);
	}
	// Клик в верхнюю кромку меню смерти — мимо кнопок опций, но в само окно.
	static void ClickDeathMenu(FState& T, bool bDown)
	{
		UAsdaWindowSubsystem* WM = T.Sub<UAsdaWindowSubsystem>();
		ClickAt(WM ? WM->FindWindow(FName(TEXT("After_Death_Win"))) : nullptr, FVector2D(0.5, 0.04), bDown);
	}

	static void Finish(FState& S)
	{
		if (UAsdaChatWindowSubsystem* C = S.Chat()) { if (C->IsInputActive()) { C->CloseInput(); } }
		if (UAsdaNetSubsystem* Net = S.Sub<UAsdaNetSubsystem>())
		{
			if (S.CapGen) { AsdaSelfTestKit::DisarmCapture(Net, S.CapGen); S.CapGen = 0; }
		}
		if (UAsdaAfterDeathSubsystem* Ad = S.Sub<UAsdaAfterDeathSubsystem>()) { Ad->DebugCloseMenuOnRevive(); }
		if (UAsdaWindowSubsystem* WM = S.Sub<UAsdaWindowSubsystem>()) { WM->ClearMsgBoxQueue(TEXT("самотест чата мёртвого: конец")); }
		if (S.SelfScope) { S.SelfScope->Restore(); }
		if (S.Cv) { S.Cv->Restore(); }
		AsdaSelfTestKit::Finish(S.W.Get(), kCmd, S.Args.bQuit, LogAsdaDeathChatTest, TEXT("СМЕРТЬ-ЧАТ"), S.Ok, S.Fail);
	}

	static void Step(TSharedRef<FState> S)
	{
		UWorld* W = S->W.Get();
		if (!W) { return; }
		if (S->Next >= S->Steps.Num()) { Finish(*S); return; }
		const float Delay = S->Steps[S->Next].Key;
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S]()
		{
			if (!S->W.IsValid()) { return; }
			TFunction<void(FState&)> F = S->Steps[S->Next].Value;
			++S->Next;
			F(*S);
			Step(S);
		}), FMath::Max(0.01f, Delay), false);
	}

	// Общая концовка пути: поле открыто и в фокусе → набор → Enter → пакет `Op` ушёл, поле закрылось.
	static void AddTypeAndSend(FState& S, const TCHAR* Path, uint16 Op, const FString& Word)
	{
		const FString P(Path);
		S.Then(0.3f, [P](FState& T)
		{
			T.Expect(T.Typing(), FString::Printf(TEXT("%s: строка чата открыта"), *P));
			T.Expect(T.FieldFocused(), FString::Printf(TEXT("%s: фокус клавиатуры в поле 2609"), *P));
			T.N5086 = T.Count(kOpNormal); T.N5088 = T.Count(kOpWhisper);
			if (UAsdaChatWindowSubsystem* C = T.Chat()) { C->DebugResetAntiFlood(false); }
		});
		AddType(S, Word);
		S.Then(0.2f, [P, Word](FState& T)
		{
			T.Expect(T.FieldText().EndsWith(Word), FString::Printf(TEXT("%s: символы вводятся — в поле «%s»"), *P, *T.FieldText()));
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			T.Expect(Inv && !Inv->IsWindowOpen(), FString::Printf(TEXT("%s: буквы в поле не сработали как горячие (инвентарь закрыт)"), *P));
		});
		AddTap(S, EKeys::Enter, 13, TEXT('\r'));
		S.Then(0.3f, [P, Op](FState& T)
		{
			const int32 Was = (Op == kOpNormal) ? T.N5086 : T.N5088;
			T.Expect(T.Count(Op) == Was + 1, FString::Printf(TEXT("%s: Enter отправил ровно один %d (%d → %d)"), *P, (int32)Op, Was, T.Count(Op)));
			T.Expect(!T.Typing(), FString::Printf(TEXT("%s: после отправки поле закрыто"), *P));
			T.Expect(!T.FieldFocused(), FString::Printf(TEXT("%s: после отправки фокус вернулся миру"), *P));
		});
	}

	static void Build(FState& S)
	{
		// ════════ 0. Живой: отправка; затем смерть при ОТКРЫТОЙ строке настоящим 4053 ════════
		S.Then(0.05f, [](FState& T)
		{
			UAsdaNetSubsystem* Net = T.Sub<UAsdaNetSubsystem>();
			T.SelfScope = MakeUnique<FAsdaNetSelfScope>(Net, kCmd);
			T.Cv = MakeUnique<FAsdaCVarScope>(T.W.Get(), kCmd);
			T.Cv->Set(TEXT("asda.Death.InputLock"), 1);
			if (!Net->bSelfUserNumKnown) { Net->SelfUserNum = 7; Net->bSelfUserNumKnown = true; }
			T.Uidx = (int32)Net->SelfUserNum;
			Net->SelfLevel = 50;
			if (Net->EnterMapId < 0 || Net->EnterMapId == 19 || Net->EnterMapId == 23) { Net->EnterMapId = 3; }
			T.CapGen = AsdaSelfTestKit::ArmCapture(Net, [&T](uint16 Op, const uint8* B, int32 N, bool, TCHAR)
			{
				FSent X; X.Op = Op; if (B && N > 0) { X.Body.Append(B, N); } T.Sent.Add(MoveTemp(X));
			}, 0, kCmd);
			T.Expect(T.CapGen != 0, TEXT("перехват исходящих взведён"));
			if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		});
		AddTap(S, EKeys::Enter, 13, TEXT('\r'));
		AddTypeAndSend(S, TEXT("0 живой"), kOpNormal, TEXT("al"));
		AddTap(S, EKeys::Enter, 13, TEXT('\r'));
		S.Then(0.3f, [](FState& T)
		{
			T.Expect(T.Typing() && T.FieldFocused(), TEXT("0 живой: второй Enter снова открыл строку"));
			UAsdaNetSubsystem* Net = T.Sub<UAsdaNetSubsystem>();
			TArray<uint8> B; B.SetNumZeroed(4); AsdaBytes::WrI16(&B[0], (int16)T.Uidx); AsdaBytes::WrI16(&B[2], -1);
			Net->DebugFeedPacket(4053, B);   // фокус намеренно не трогаем: путь игрока
		});
		S.Then(1.0f, [](FState& T)
		{
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			T.Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("0: меню смерти открыто, персонаж мёртв"));
		});
		AddTypeAndSend(S, TEXT("0 смерть при открытой строке"), kOpNormal, TEXT("ab"));

		// ════════ 1. Enter ════════
		AddTap(S, EKeys::Enter, 13, TEXT('\r'));
		AddTypeAndSend(S, TEXT("1 Enter"), kOpNormal, TEXT("hi1r"));

		// ════════ 2. R — ответ на шёпот ════════
		S.Then(0.1f, [](FState& T) { if (UAsdaChatWindowSubsystem* C = T.Chat()) { C->NoteWhisperFrom(TEXT("Pal")); } });
		AddTap(S, EKeys::R, 'R', TEXT('r'));
		AddTypeAndSend(S, TEXT("2 R"), kOpWhisper, TEXT("ok"));

		// ════════ 3. Ctrl+W — шёпот (адресат из прошлого шага) ════════
		S.Then(kKeyGap, [](FState&) { KeyDown(EKeys::LeftControl, 17, 0); });
		S.Then(kKeyGap, [](FState&) { KeyDown(EKeys::W, 'W', 0); });
		S.Then(kKeyGap, [](FState&) { KeyUp(EKeys::W, 'W', 0); });
		S.Then(kKeyGap, [](FState&) { KeyUp(EKeys::LeftControl, 17, 0); });
		S.Then(0.25f, [](FState& T)
		{
			UAsdaChatWindowSubsystem* C = T.Chat();
			T.Expect(T.Typing() && T.FieldFocused() && C && C->GetInputState() == 7,
				FString::Printf(TEXT("3 Ctrl+W: поле открыто в режиме шёпота (режим %d, фокус %d)"), C ? C->GetInputState() : -1, T.FieldFocused() ? 1 : 0));
		});
		AddTypeAndSend(S, TEXT("3 Ctrl+W"), kOpWhisper, TEXT("wh"));

		// ════════ 4. Клик в поле чата ════════
		S.Then(0.2f, [](FState& T) { if (UAsdaChatWindowSubsystem* C = T.Chat()) { C->SetInputState(4); } ClickField(T, true); });
		S.Then(kKeyGap, [](FState& T) { ClickField(T, false); });
		AddTypeAndSend(S, TEXT("4 клик"), kOpNormal, TEXT("yo"));

		// ════════ 4б. Клик по меню смерти, затем Enter: окно не отбирает клавиатуру у чата ════════
		S.Then(0.2f, [](FState& T) { ClickDeathMenu(T, true); });
		S.Then(kKeyGap, [](FState& T) { ClickDeathMenu(T, false); });
		AddTap(S, EKeys::Enter, 13, TEXT('\r'));
		AddTypeAndSend(S, TEXT("4б клик по меню"), kOpNormal, TEXT("me"));

		// ════════ 5. Замок на месте: без открытого поля клавиша инвентаря мёртвому молчит ════════
		S.Then(0.1f, [](FState&) { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); } });
		AddTap(S, EKeys::I, 'I', TEXT('i'));
		S.Then(0.3f, [](FState& T)
		{
			UAsdaInventorySubsystem* Inv = T.Sub<UAsdaInventorySubsystem>();
			UAsdaAfterDeathSubsystem* Ad = T.Sub<UAsdaAfterDeathSubsystem>();
			T.Expect(Inv && !Inv->IsWindowOpen() && !T.Typing(), TEXT("5: поле закрыто — клавиша инвентаря мёртвому не срабатывает (замок цел)"));
			T.Expect(Ad && Ad->IsDeathMenuOpen(), TEXT("5: меню смерти на месте"));
		});
	}

	static void Run(const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs Args = FAsdaSelfTestArgs::Parse(A);
		const uint16 Extra[] = { kOpNormal, kOpWhisper };
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, Args, 0.f, Extra)) { return; }
		if (!W) { return; }
		const bool bSpawn = AsdaSelfTestKit::NeedSelfSpawn(W) && GEngine;
		if (bSpawn)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 115 396"));
		}
		TSharedRef<FState> S = MakeShared<FState>();
		S->W = W;
		S->Args = Args;
		TWeakObjectPtr<UWorld> WW(W);
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([S, WW]()
		{
			UWorld* Wd = WW.Get();
			if (!Wd) { return; }
			if (!S->Sub<UAsdaNetSubsystem>() || !S->Chat() || !S->Sub<UAsdaAfterDeathSubsystem>())
			{
				AsdaSelfTestKit::Refused(Wd, kCmd, S->Args.bQuit, TEXT("нет подсистем — нужна карта мира"));
				return;
			}
			Build(*S);
			Step(S);
		}), bSpawn ? 2.f : 0.05f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDeathChat(
	TEXT("asda.SelfTest.DeathChat"),
	TEXT("Самотест чата мёртвого: при открытом меню смерти Enter, R, Ctrl+W и клик открывают поле, символы вводятся настоящими событиями "
	     "Slate, Enter отправляет (5084/5088), буквы в поле не срабатывают как горячие; без поля замок цел. Только офлайн. `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaDeathChatSelfTest::Run(A, W);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
