// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ ОЧЕРЕДИ ОКОН-СООБЩЕНИЙ ONEBUT 1500 / TWOBUT 1520 (26.09.2026) — путь игрока (-game), только офлайн.
//   Проверяет порт стека `CMsgBoxMgr+0x3340` (разбор — `Reports/msgqueue/msgqueue.md`, шапка раздела в
//   `AsdaWindowSubsystem.h`): несколько сообщений подряд и дедуп пары, подтверждение поверх сообщения, цепочка
//   «ответ → новое окно», ESC на TWOBUT, сброс сессии, загрузка мира, модалка смерти, режим эталона
//   `asda.MsgBox.QueueFixes 0`. Звуки 25/4/5 — ровно по одному, дельтами от начала шага.
//   Кнопки нажимаются делегатом `OnClicked` (путь игрока), ESC — `CloseTopmostOnEsc`.
//   Строки проверок — «САМОТЕСТ-ОЧЕРЕДЬ», итог — «САМОТЕСТ-ОЧЕРЕДЬ ИТОГ». Ключи: `delay=N`, `quit`.
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#if !UE_BUILD_SHIPPING   // ★ античит А2 (12.10.2026): самотесты и стенды в Shipping не входят
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaCraftSubsystem.h"        // RefuseDebugWhenOnline — общий офлайн-гейт самотестов
#include "Test/AsdaSelfTestRunGate.h"   // Р-2: RefuseDeferredStartWhenOnline — повторная проверка офлайна на отложенном старте   ★ R3: FAsdaSelfTestRunGate — узкий заголовок вместо AsdaOutgoingGate.h
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "UI/AsdaMinimapSubsystem.h"      // карта мира 1710: показ окна-сообщения её закрывает (miniMapWndClose)
#include "UI/AsdaAfterDeathSubsystem.h"   // редакция 1 (С-2): модалка «вошёл мёртвым» (0, 2370) боевым путём
#include "UI/AsdaSoulmateSkillSubsystem.h" // редакция 1 (С-2): зов друга (1, 38) боевым приёмником 5236
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"     // ★ 09.10.2026: дети корневой канвы — рамка окна для проверки раскладки
#include "Components/RichTextBlock.h"   // ★ 09.10.2026: тело окна после показа с тегами — URichTextBlock
#include "UI/AsdaMarkup.h"              // ★ 09.10.2026: FAsdaMarkup::StripTags — ожидаемый видимый текст
#include "UI/AsdaNotice.h"              // ★ 09.10.2026: AsdaNotice::LocRaw — строка 111018 с тегами
#include "UI/AsdaUiHelpers.h"           // ★ 09.10.2026: FormatPrintfArgs / WidgetAbsRect — подстановка и геометрия
#include "AsdaCVarSave.h"               // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaMsgQueueTest, Log, All);

namespace
{
	const FName kOneBut(TEXT("ONEBUT_MSGBOX"));
	const FName kTwoBut(TEXT("TWOBUT_MSGBOX"));

	// Текст, который СЕЙЧАС нарисован в окне (а не верх очереди) — для режима эталона, где они расходятся.
	FString ShownText(UAsdaWindowSubsystem* WM, FName Win)
	{
		UUserWidget* W = WM->FindWindow(Win);
		const TCHAR* Node = (Win == kOneBut) ? TEXT("ONEBUT_TEXT") : TEXT("TWOBUT_TEXT");
		// ★ 09.10.2026: узел после показа с тегами — URichTextBlock; видимый текст читает общая точка подсистемы.
		return W ? UAsdaWindowSubsystem::ReadMsgBoxShownText(W->GetWidgetFromName(FName(Node))) : FString();
	}

	// Нажать кнопку окна делегатом `OnClicked` (тот же путь, что клик игрока).
	bool Press(UAsdaWindowSubsystem* WM, FName Win, const TCHAR* Btn)
	{
		UUserWidget* W = WM->FindWindow(Win);
		UButton* B = W ? Cast<UButton>(W->GetWidgetFromName(FName(Btn))) : nullptr;
		if (!B) { return false; }
		B->OnClicked.Broadcast();
		return true;
	}

	struct FSfx { int32 S25 = 0, S4 = 0, S5 = 0; };
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdMsgBoxQueueSelfTest(
	TEXT("asda.MsgBox.SelfTest"),
	TEXT("Самотест очереди окон-сообщений ONEBUT/TWOBUT: подряд + дедуп, подтверждение поверх сообщения, цепочка, ESC, "
	     "сброс сессии, загрузка мира, смерть, режим эталона QueueFixes 0; звуки 25/4/5 по одному. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		bool bQuit = false; float Delay = 0.f;
		for (const FString& A : Args)
		{
			if (A.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(Args, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		// Только офлайн: при живом соединении сброс сессии самотеста оборвал бы настоящую игру.
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.MsgBox.SelfTest"), KitArgs)) { return; }

		struct FStat { int32 Ok = 0; int32 Fail = 0; };
		TSharedRef<FStat> Stat = MakeShared<FStat>();
		// ★ Р-2 (ревизия shopdbl2, 10.10.2026): офлайн проверяется повторно в момент ОТЛОЖЕННОГО старта — первым сработавшим
		//   шагом; при отказе все шаги прогона пропускаются (итог отказа пишет `RefuseDeferredStartWhenOnline`).
		// ★ 11.10.2026 (В3 S1): своя копия флага (FGate) заменена общим `FAsdaSelfTestRunGate` — поведение то же.
		TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.MsgBox.SelfTest"), bQuit);   // общий флаг отложенного старта
		auto At = [W, WM, Delay, Gate, bQuit](float T, TFunction<void()> Fn)
		{
			FTimerHandle H;
			TWeakObjectPtr<UWorld> WW(W);   // мир прогона (для отказа)
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(WM, [Gate, WW, bQuit, Fn = MoveTemp(Fn)]()
			{
				if (!Gate->Pass(WW.Get())) { return; }   // Р-2: офлайн повторно — на отложенном старте
				Fn();
			}), T + Delay, false);
		};
		auto Check = [Stat](bool bOk, const FString& What)
		{
			if (bOk) { ++Stat->Ok; } else { ++Stat->Fail; }
			UE_LOG(LogAsdaMsgQueueTest, Warning, TEXT("САМОТЕСТ-ОЧЕРЕДЬ %s %s"), bOk ? TEXT("✔") : TEXT("✘ ПРОВАЛ"), *What);
		};
		auto Sfx = [WM]() { FSfx S; S.S25 = WM->GetMsgBoxSfxCount(25); S.S4 = WM->GetMsgBoxSfxCount(4); S.S5 = WM->GetMsgBoxSfxCount(5); return S; };
		auto SfxIs = [Sfx](const FSfx& From, int32 D25, int32 D4, int32 D5)
		{
			const FSfx N = Sfx();
			return N.S25 - From.S25 == D25 && N.S4 - From.S4 == D4 && N.S5 - From.S5 == D5;
		};
		auto SfxStr = [Sfx](const FSfx& From)
		{
			const FSfx N = Sfx();
			return FString::Printf(TEXT("звуков: 25×%d, 4×%d, 5×%d"), N.S25 - From.S25, N.S4 - From.S4, N.S5 - From.S5);
		};

		// ── 1. Подряд: дедуп пары (0, −1), разные id живут вместе, «ОК» возвращает нижнее сообщение ──
		At(0.5f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), 1);
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: чистый старт"));
			FSfx S0 = Sfx();
			UUserWidget* A = WM->ShowGameMessage(TEXT("Очередь: A (0,-1)"));
			UUserWidget* B = WM->ShowGameMessage(TEXT("Очередь: B (0,-1)"));
			Check(A && !B && WM->GetMsgBoxQueueNum() == 1 && ShownText(WM, kOneBut) == TEXT("Очередь: A (0,-1)") && SfxIs(S0, 1, 0, 0),
				FString::Printf(TEXT("дедуп: второй показ (0,-1) при первом в очереди отброшен молча; записей %d, %s (ожидалось 25×1)"),
					WM->GetMsgBoxQueueNum(), *SfxStr(S0)));
			S0 = Sfx();
			UUserWidget* C = WM->ShowGameMessage(TEXT("Очередь: C (0,24)"), 2288, EAsdaMsgCategory::ItemRefused, nullptr, 0, 24);
			Check(C && WM->GetMsgBoxQueueNum() == 2 && ShownText(WM, kOneBut) == TEXT("Очередь: C (0,24)") && SfxIs(S0, 1, 0, 0),
				FString::Printf(TEXT("другой id (0,24) ложится сверху и сразу виден; записей %d, %s (ожидалось 25×1)"), WM->GetMsgBoxQueueNum(), *SfxStr(S0)));
			S0 = Sfx();
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			Check(WM->IsOpen(kOneBut) && WM->GetMsgBoxQueueNum() == 1 && ShownText(WM, kOneBut) == TEXT("Очередь: A (0,-1)") && SfxIs(S0, 1, 1, 1),
				FString::Printf(TEXT("«ОК» по C вернул A: %s (ожидалось 4×1, 25×1, 5×1)"), *SfxStr(S0)));
		});

		// ── 2. Подтверждение поверх сообщения; «Да» → колбэк → возврат сообщения; цепочка «ответ → новое окно» ──
		At(1.0f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			static int32 Answer = -1;
			Answer = -1;
			FSfx S0 = Sfx();
			UUserWidget* Q = WM->ShowGameConfirm(TEXT("Очередь: вопрос (1,47)"), [](bool bYes) { Answer = bYes ? 1 : 0; },
				0.f, 2288, 2289, 47, 1);
			Check(Q && WM->IsOpen(kOneBut) && WM->IsOpen(kTwoBut) && WM->GetTopModalWindow() == Q && WM->GetMsgBoxQueueNum() == 2 && SfxIs(S0, 1, 0, 0),
				FString::Printf(TEXT("TWOBUT поверх ONEBUT: оба открыты, верхняя модалка — вопрос, записей %d, %s (ожидалось 25×1)"),
					WM->GetMsgBoxQueueNum(), *SfxStr(S0)));
			UUserWidget* Q2 = WM->ShowGameConfirm(TEXT("Очередь: тот же вопрос (1,47)"), [](bool) {}, 0.f, 2288, 2289, 47, 1);
			Check(!Q2 && WM->GetMsgBoxQueueNum() == 2, TEXT("дубль подтверждения (1,47) отброшен (второй запрос дружбы при открытом первом)"));
			S0 = Sfx();
			Press(WM, kTwoBut, TEXT("TWOBUT_BUT00"));
			Check(Answer == 1 && !WM->IsOpen(kTwoBut) && WM->IsOpen(kOneBut) && ShownText(WM, kOneBut) == TEXT("Очередь: A (0,-1)")
				&& WM->GetMsgBoxQueueNum() == 1 && SfxIs(S0, 1, 1, 1),
				FString::Printf(TEXT("«Да»: колбэк «да», TWOBUT закрыт, A снова показан: %s (ожидалось 4×1, 25×1, 5×1)"), *SfxStr(S0)));
			// Цепочка: ответ ставит новое сообщение ДО закрытия (как эталон: действие → CGameUI_CloseWindow).
			WM->ShowGameConfirm(TEXT("Очередь: вопрос-цепочка"), [WM](bool)
			{
				WM->ShowGameMessage(TEXT("Очередь: D из колбэка"), 2288, EAsdaMsgCategory::ItemRefused, nullptr, 0, 50);
			});
			S0 = Sfx();
			Press(WM, kTwoBut, TEXT("TWOBUT_BUT01"));
			Check(!WM->IsOpen(kTwoBut) && ShownText(WM, kOneBut) == TEXT("Очередь: D из колбэка") && WM->GetMsgBoxQueueNum() == 2 && SfxIs(S0, 2, 1, 1),
				FString::Printf(TEXT("цепочка «Нет» → новое сообщение D: D сверху, %s (ожидалось 4×1, 25×2, 5×1 — как эталон)"), *SfxStr(S0)));
			S0 = Sfx();
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			Check(!WM->IsOpen(kOneBut) && WM->GetMsgBoxQueueNum() == 0 && SfxIs(S0, 1, 2, 2),
				FString::Printf(TEXT("«ОК» по D и по A: очередь пуста, окно закрыто, %s (ожидалось 25×1, 4×2, 5×2)"), *SfxStr(S0)));
		});

		// ── 3. ESC на TWOBUT = «Нет» 1524; ONEBUT ESC съедает без закрытия ──
		At(1.5f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			static int32 Answer = -1;
			Answer = -1;
			WM->ShowGameMessage(TEXT("Очередь: под вопросом"));
			WM->ShowGameConfirm(TEXT("Очередь: ESC-вопрос"), [](bool bYes) { Answer = bYes ? 1 : 0; });
			FSfx S0 = Sfx();
			const bool bEsc = WM->CloseTopmostOnEsc();
			Check(bEsc && Answer == 0 && !WM->IsOpen(kTwoBut) && WM->IsOpen(kOneBut) && SfxIs(S0, 1, 1, 1),
				FString::Printf(TEXT("ESC на TWOBUT = «Нет»: вопрос снят, сообщение вернулось, %s (ожидалось 4×1, 25×1, 5×1)"), *SfxStr(S0)));
			S0 = Sfx();
			WM->CloseTopmostOnEsc();
			Check(WM->IsOpen(kOneBut) && WM->GetMsgBoxQueueNum() == 1 && SfxIs(S0, 0, 0, 0),
				FString::Printf(TEXT("ESC на ONEBUT съеден, окно не закрыто (эталон `if (a3 != 1500)`), %s"), *SfxStr(S0)));
		});

		// ── 4. Сброс сессии (полный): очередь снята молча, колбэки не зовутся; смена карты очередь сохраняет ──
		At(2.0f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			static int32 Answer = -1;
			Answer = -1;
			WM->ShowGameConfirm(TEXT("Очередь: вопрос до сброса"), [](bool bYes) { Answer = bYes ? 1 : 0; });
			FSfx S0 = Sfx();
			WM->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			Check(WM->GetMsgBoxQueueNum() == 0 && !WM->IsOpen(kOneBut) && !WM->IsOpen(kTwoBut) && Answer == -1 && SfxIs(S0, 0, 0, 0),
				FString::Printf(TEXT("полный сброс (LeftWorld): очередь пуста, окна закрыты, колбэк не вызван, %s (ожидалось без звуков)"), *SfxStr(S0)));
			WM->ShowGameMessage(TEXT("Очередь: переживает смену карты"));
			S0 = Sfx();
			WM->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			WM->MapLoadCloseMsgBoxes();   // шаг загрузки `g_LoadProgress == 0`: CGameUI_CloseWindow(5001/5002/0)
			Check(WM->GetMsgBoxQueueNum() == 1 && !WM->IsOpen(kOneBut) && SfxIs(S0, 3, 0, 3),
				FString::Printf(TEXT("смена карты: запись осталась, окно на экране загрузки не рисуется, %s (ожидалось 25×3, 5×3 — три закрытия эталона)"),
					*SfxStr(S0)));
			WM->CloseAll();   // вход в мир (`RevealWorld`) сносит все окна — запись вернётся следующим тиком
		});
		At(2.3f, [WM, Check]()
		{
			Check(WM->IsOpen(kOneBut) && ShownText(WM, kOneBut) == TEXT("Очередь: переживает смену карты"),
				TEXT("после входа в мир (CloseAll) запись очереди снова на экране"));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 4 закончен"));
		});

		// ── 5. Смерть: запись (0, 2370) снимается при входе на карту молча; чужая запись под ней возвращается ──
		At(2.6f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			WM->ShowGameMessage(TEXT("Очередь: чужое сообщение"));
			WM->ShowGameMessage(TEXT("Очередь: модалка смерти 2370"), 2288, EAsdaMsgCategory::ItemRefused, nullptr, 800, 2370);
			FSfx S0 = Sfx();
			const bool bGone = WM->CloseDeathMsgBoxOnMapEntry();
			Check(bGone && WM->GetMsgBoxQueueNum() == 1 && ShownText(WM, kOneBut) == TEXT("Очередь: чужое сообщение") && SfxIs(S0, 0, 0, 0),
				FString::Printf(TEXT("вход на карту снял запись смерти молча, чужое сообщение на экране, %s (ожидалось без звуков)"), *SfxStr(S0)));
			S0 = Sfx();
			WM->CloseWindow(kOneBut);   // программное закрытие чужим кодом = снять свою запись, 5
			Check(!WM->IsOpen(kOneBut) && WM->GetMsgBoxQueueNum() == 0 && SfxIs(S0, 0, 0, 1),
				FString::Printf(TEXT("программное закрытие ONEBUT: запись снята, %s (ожидалось 5×1)"), *SfxStr(S0)));
		});

		// ── 6. Режим эталона `asda.MsgBox.QueueFixes 0`: A, B, C → видно C, затем A, «ОК» по A исполняет B ──
		At(3.0f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), 0);
			static TArray<FString> Done;
			Done.Reset();
			WM->ShowGameMessage(TEXT("Эталон: A"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("A")); }, 0, 101);
			WM->ShowGameMessage(TEXT("Эталон: B"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("B")); }, 0, 102);
			WM->ShowGameMessage(TEXT("Эталон: C"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("C")); }, 0, 103);
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			Check(Done.Num() == 1 && Done[0] == TEXT("C") && ShownText(WM, kOneBut) == TEXT("Эталон: A"),
				FString::Printf(TEXT("эталон: «ОК» по C, затем показана ПЕРВАЯ запись A (sub_D7E120), на экране «%s»"), *ShownText(WM, kOneBut)));
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			Check(Done.Num() == 2 && Done[1] == TEXT("B") && ShownText(WM, kOneBut) == TEXT("Эталон: A"),
				TEXT("эталон: «ОК» при видимом A исполнил B (снятие с конца) — дефект оригинала воспроизведён при QueueFixes 0"));
			Press(WM, kOneBut, TEXT("ONEBUT_BUT00"));
			AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), 1);
			Check(Done.Num() == 3 && !WM->IsOpen(kOneBut), TEXT("эталон: третий «ОК» снял A, очередь пуста; QueueFixes возвращён в 1"));
			// Тот же сценарий с исправлением: C, B, A — каждое видно и отвечено своё.
			Done.Reset();
			WM->ShowGameMessage(TEXT("Порт: A"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("A")); }, 0, 101);
			WM->ShowGameMessage(TEXT("Порт: B"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("B")); }, 0, 102);
			WM->ShowGameMessage(TEXT("Порт: C"), 2288, EAsdaMsgCategory::ItemRefused, []() { Done.Add(TEXT("C")); }, 0, 103);
			FSfx S0 = Sfx();
			TArray<FString> Seen;
			for (int32 i = 0; i < 3; ++i) { Seen.Add(ShownText(WM, kOneBut)); Press(WM, kOneBut, TEXT("ONEBUT_BUT00")); }
			Check(Done == TArray<FString>({ TEXT("C"), TEXT("B"), TEXT("A") }) && Seen == TArray<FString>({ TEXT("Порт: C"), TEXT("Порт: B"), TEXT("Порт: A") })
				&& !WM->IsOpen(kOneBut) && SfxIs(S0, 2, 3, 3),
				FString::Printf(TEXT("исправление: видно и отвечено C, B, A по порядку, %s (ожидалось 25×2, 4×3, 5×3)"), *SfxStr(S0)));
		});

		// ── 7. Показ окна-сообщения закрывает карту мира 1710 (`miniMapWndClose`, звук 6), как эталон ──
		At(3.3f, [WM, GI, Check]()
		{
			UAsdaMinimapSubsystem* Mm = GI->GetSubsystem<UAsdaMinimapSubsystem>();
			if (!Mm) { Check(false, TEXT("карта мира: подсистемы мини-карты нет")); return; }
			// Офлайн-прогон на alpen окно 1702 не строит (нет карты/павна), а роспись `MAXMAP_WIN` мини-карта ставит
			//   именно при его создании (`AsdaMinimapSubsystem.cpp`, шаг 15 `uiCloseAllWindows`). Ставим ту же роспись
			//   (закрыть карту мира, если открыта) — проверяется путь оконной подсистемы: показ → роспись → закрытие.
			TWeakObjectPtr<UAsdaMinimapSubsystem> WeakMm(Mm);
			WM->RegisterWindowCloser(FName(TEXT("MAXMAP_WIN")), FSimpleDelegate::CreateLambda([WeakMm]()
			{
				if (WeakMm.IsValid() && WeakMm->IsBigMapOpen()) { WeakMm->SetBigMapVisible(false); }
			}));
			Mm->SetBigMapVisible(true);
			const bool bWasOpen = Mm->IsBigMapOpen();
			WM->ShowGameMessage(TEXT("Очередь: закрывает карту мира"));
			Check(bWasOpen && !Mm->IsBigMapOpen() && WM->IsOpen(kOneBut),
				FString::Printf(TEXT("показ ONEBUT закрыл открытую карту мира 1710 (была открыта: %s)"), bWasOpen ? TEXT("да") : TEXT("нет")));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: конец"));
		});

		// ── 8. Редакция 1, С-1: окно ВИДНО во время действия (эталон прячет только полосу); ветка «закрытие → действие →
		//      закрытие» (0x4000/0x97B) — 4, затем возврат нижней записи дважды (25×2) и два 5 ──
		At(3.6f, [WM, Check, Sfx, SfxIs, SfxStr]()
		{
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 8"));
			WM->ShowGameMessage(TEXT("Очередь: под 0x4000"));
			static bool bOpenInAction = false, bOpenAfterFirstClose = true, bUnderShownInAction = false;
			WM->ShowGameConfirm(TEXT("Очередь: 0x4000"), [WM](bool bYes)
			{
				if (!bYes) { return; }
				bOpenInAction = WM->IsOpen(kTwoBut);   // до первого закрытия окно ещё на экране — как эталон
				WM->RepeatMsgBoxClose(1, TEXT("самотест: закрытие до действия (0x4000)"));
				bOpenAfterFirstClose = WM->IsOpen(kTwoBut);
				bUnderShownInAction = ShownText(WM, kOneBut) == TEXT("Очередь: под 0x4000");
			}, 0.f, 2288, 2289, 0x4000, 1);
			FSfx S0 = Sfx();
			Press(WM, kTwoBut, TEXT("TWOBUT_BUT00"));
			Check(bOpenInAction && !bOpenAfterFirstClose && bUnderShownInAction && WM->GetMsgBoxQueueNum() == 1 && SfxIs(S0, 2, 1, 2),
				FString::Printf(TEXT("0x4000: в действии окно видно (%s), первое закрытие его сняло и вернуло нижнее, %s (ожидалось 4×1, 25×2, 5×2)"),
					bOpenInAction ? TEXT("да") : TEXT("нет"), *SfxStr(S0)));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 8 закончен"));
		});

		// ── 9. Редакция 1, С-2: дубль не затирает владельца. Модалка смерти (0, 2370) дважды → воскрешение снимает её;
		//      зов друга (1, 38) дважды → смена карты снимает его молча, без показа посреди травела ──
		At(3.9f, [WM, GI, Check, Sfx, SfxIs, SfxStr]()
		{
			UAsdaAfterDeathSubsystem* Ad = GI->GetSubsystem<UAsdaAfterDeathSubsystem>();
			UAsdaSoulmateSkillSubsystem* Ss = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>();
			if (!Ad || !Ss) { Check(false, TEXT("С-2: нет подсистемы смерти или навыков Дружбы")); return; }
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 9"));
			Ad->DebugShowEntryDeadModal();
			Ad->DebugShowEntryDeadModal();   // второй 4053 в состоянии «вошёл мёртвым» — дубль (0, 2370)
			Check(WM->GetMsgBoxQueueNum() == 1 && Ad->DebugHasModal(), TEXT("С-2 смерть: второй показ (0, 2370) отброшен, указатель модалки цел"));
			FSfx S0 = Sfx();
			Ad->DebugCloseMenuOnRevive();    // воскрешение 4055/4056 → CloseMenu → CloseModal
			Check(!WM->IsMsgBoxQueued(0, 2370) && !WM->IsOpen(kOneBut) && !Ad->DebugHasModal() && SfxIs(S0, 0, 0, 0),
				FString::Printf(TEXT("С-2 смерть: воскрешение сняло модалку 2370 молча, у живого персонажа её нет, %s"), *SfxStr(S0)));

			// А-2 (редакция 2): воскрешение в промежуток, когда окна не нарисованы (после травела до возврата записи).
			Ad->DebugShowEntryDeadModal();
			WM->CloseAll();   // вход в мир снёс окна; запись (0, 2370) вернётся только следующим тиком
			const bool bHiddenBefore = !WM->IsWindowVisible(kOneBut) && WM->IsMsgBoxQueued(0, 2370);
			Ad->DebugCloseMenuOnRevive();
			Check(bHiddenBefore && !WM->IsMsgBoxQueued(0, 2370),
				TEXT("А-2: воскрешение при ненарисованных окнах сняло запись смерти (0, 2370) — исправление дефекта оригинала"));
			AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), 0);
			Ad->DebugShowEntryDeadModal();
			WM->CloseAll();
			Ad->DebugCloseMenuOnRevive();
			const bool bEtalonKeeps = WM->IsMsgBoxQueued(0, 2370);
			AsdaCVarSave::SetForTest(TEXT("asda.MsgBox.QueueFixes"), 1);
			Check(bEtalonKeeps, TEXT("А-2 при QueueFixes 0: как эталон — гейт видимости, запись (0, 2370) осталась"));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: А-2 закончен"));

			WM->ShowGameMessage(TEXT("Очередь: под зовом друга"));
			TArray<uint8> Body; Body.SetNumZeroed(42);
			Ss->DebugInjectMateRecall5236(Body);
			Ss->DebugInjectMateRecall5236(Body);   // второй зов при открытом первом — дубль (1, 38)
			Check(WM->GetMsgBoxQueueNum() == 2 && WM->IsMsgBoxQueued(1, 38) && Ss->DebugHasRecallBox(),
				FString::Printf(TEXT("С-2 зов: второй 5236 отброшен дублем, указатель окна цел, записей %d"), WM->GetMsgBoxQueueNum()));
			S0 = Sfx();
			Ss->ResetSessionState(EAsdaSessionResetReason::MapChanged);
			Check(!WM->IsMsgBoxQueued(1, 38) && !WM->IsOpen(kTwoBut) && WM->GetMsgBoxQueueNum() == 1 && !Ss->DebugHasRecallBox() && SfxIs(S0, 0, 0, 0),
				FString::Printf(TEXT("С-2 зов: смена карты сняла (1, 38) молча, запись под ним осталась в очереди, %s"), *SfxStr(S0)));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: конец шага 9"));
		});

		// ── 10. ★ 09.10.2026 (СРЕДН-1 ревизии autoshovel_fishbooks): теги `<FC=…>` в теле окна НЕ видны как текст.
		//      TWOBUT: строка 111018 диалога 79 (две подстановки в `<FC=FF99FF99>[…]</F>`); затем ONEBUT с той же строкой.
		//      Видимый текст = строка без тегов, сегмент окрашен 99FF99, блок строк в центре авторского rect. ──
		At(4.3f, [WM, GI, Check]()
		{
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 10 (теги)"));
			const FString Raw = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, 111018, true), { TEXT("Книга"), TEXT("Рыбак") });   // ★ 09.10: порядок оригинала — предмет, затем игрок
			const FString Want = FAsdaMarkup::StripTags(Raw);
			WM->ShowGameConfirm(Raw, [](bool) {}, 0.f, 2288, 2289, 79, 1);
			UUserWidget* Box = WM->FindWindow(kTwoBut);
			const UWidget* Node = Box ? Box->GetWidgetFromName(TEXT("TWOBUT_TEXT")) : nullptr;
			const URichTextBlock* R = Cast<URichTextBlock>(Node);
			const FString Shown = UAsdaWindowSubsystem::ReadMsgBoxShownText(Node);
			const FString Markup = R ? R->GetText().ToString() : FString();
			Check(Raw.Contains(TEXT("<FC=")) && R && !Shown.Contains(TEXT("<FC=")) && !Shown.Contains(TEXT("</F>")) && Shown == Want
				&& Markup.Contains(TEXT("c=\"99FF99\"")),
				FString::Printf(TEXT("TWOBUT 111018: «<FC=» не виден, текст «%s», цвет сегмента 99FF99 (%s)"), *Shown,
					Markup.Contains(TEXT("c=\"99FF99\"")) ? TEXT("есть") : TEXT("НЕТ")));
		});
		// Раскладка — после прохода Slate: центр блока строк = центр авторского rect TWOBUT_TEXT 300×90 @ (45,5) →
		//   (195, 50) в координатах окна 360×140 (bbox детей канвы; TextAlign 5), ширина ≤ 300, строк > 1. Кадр «после».
		At(4.6f, [WM, Check, W]()
		{
			UUserWidget* Box = WM->FindWindow(kTwoBut);
			UWidget* Node = Box ? Box->GetWidgetFromName(TEXT("TWOBUT_TEXT")) : nullptr;
			// Рамка окна = общий bbox видимых детей корневой канвы (по нему же окно центрирует `CenterModalContent`).
			UPanelWidget* Canvas = Node ? Node->GetParent() : nullptr;
			FVector2D TMn, TMx, BMn(FLT_MAX, FLT_MAX), BMx(-FLT_MAX, -FLT_MAX);
			bool bFrame = false;
			for (int32 i = 0; Canvas && i < Canvas->GetChildrenCount(); ++i)
			{
				UWidget* Ch = Canvas->GetChildAt(i);
				FVector2D Mn, Mx;
				if (Ch && Ch->IsVisible() && AsdaUi::WidgetAbsRect(Ch, Mn, Mx) && Mx.X > Mn.X && Mx.Y > Mn.Y)
				{
					BMn = FVector2D(FMath::Min(BMn.X, Mn.X), FMath::Min(BMn.Y, Mn.Y));
					BMx = FVector2D(FMath::Max(BMx.X, Mx.X), FMath::Max(BMx.Y, Mx.Y));
					bFrame = true;
				}
			}
			const bool bGeo = bFrame && AsdaUi::WidgetAbsRect(Node, TMn, TMx);
			const FGeometry G = Node ? Node->GetCachedGeometry() : FGeometry();
			const float Scale = (G.GetLocalSize().X > 1.f) ? G.GetAbsoluteSize().X / G.GetLocalSize().X : 1.f;   // абсолютные px на юнит окна
			const FVector2D C = bGeo ? ((TMn + TMx) * 0.5f - BMn) / Scale : FVector2D(-1.f, -1.f);   // центр текста в юнитах окна
			const FVector2D Sz = bGeo ? (TMx - TMn) / Scale : FVector2D::ZeroVector;
			const FVector2D Fr = bGeo ? (BMx - BMn) / Scale : FVector2D::ZeroVector;
			Check(bGeo && FMath::Abs(C.X - 195.f) <= 2.f && FMath::Abs(C.Y - 50.f) <= 2.f && Sz.X <= 301.f && Sz.Y > 14.f,
				FString::Printf(TEXT("TWOBUT 111018 раскладка: центр текста (%.1f, %.1f) при авторском (195, 50) в рамке %.0f×%.0f, размер %.0f×%.0f (перенос по 300, строк > 1)"),
					C.X, C.Y, Fr.X, Fr.Y, Sz.X, Sz.Y));
			GEngine->Exec(W, TEXT("asda.UI.Shot asda_msgbox_111018_after"));
		});
		At(5.0f, [WM, GI, Check]()
		{
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 10, ONEBUT"));
			const FString Raw = AsdaUi::FormatPrintfArgs(AsdaNotice::LocRaw(GI, 111018, true), { TEXT("Книга"), TEXT("Рыбак") });   // ★ 09.10: порядок оригинала — предмет, затем игрок
			WM->ShowGameMessage(Raw, 2288, EAsdaMsgCategory::ItemRefused, nullptr, 0, 111018);
			Check(!ShownText(WM, kOneBut).Contains(TEXT("<FC=")) && ShownText(WM, kOneBut) == FAsdaMarkup::StripTags(Raw),
				FString::Printf(TEXT("ONEBUT 111018: «<FC=» не виден, текст «%s»"), *ShownText(WM, kOneBut)));
			// Повторный показ без тегов в уже подменённом узле (ветка `Already`): текст обычный, экранирование снято.
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 10, повтор"));
			WM->ShowGameConfirm(TEXT("Очередь: без тегов & <не тег>"), [](bool) {});
			Check(ShownText(WM, kTwoBut) == TEXT("Очередь: без тегов & <не тег>"),
				FString::Printf(TEXT("повторный показ в rich-узле: «%s» (экранирование &/< снято)"), *ShownText(WM, kTwoBut)));
			WM->ClearMsgBoxQueue(TEXT("самотест очереди: шаг 10 закончен"));
		});

		At(5.6f, [Stat, bQuit, W]()
		{
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ MsgBox.QueueFixes — к исходному значению И приоритету SetBy (прежде шаги возвращали
			//   константу 1 с SetByConsole).
			AsdaCVarSave::RestoreAll(TEXT("asda.MsgBox.SelfTest"));
			AsdaSelfTestKit::Summary(LogAsdaMsgQueueTest, TEXT("САМОТЕСТ-ОЧЕРЕДЬ"), Stat->Ok, Stat->Fail, TEXT(""), true);   // ★ 11.10.2026 (В3 S1): единый итог
			AsdaSelfTestKit::Quit(W, bQuit);
		});
	}));
#endif // ASDA_DEV_CONSOLE

#endif // !UE_BUILD_SHIPPING
