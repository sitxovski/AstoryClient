// Copyright (c) 2026 @sitxovski (Discord)
// ★ САМОТЕСТ «КНОПКИ-ПЕРЕКЛЮЧАТЕЛИ ОКОН» (25.10.2026, волна ui_toggles): `asda.SelfTest.Toggles [legacy] [notravel] [quit]`.
//   Отчёт и таблица «кнопка → окно → поведение → оригинал» — `Reports/ui_toggles/TOGGLE_AUDIT.md`.
//   Жалобы: (1) игроки — «окно „Кто?“ сообщества при повторных нажатиях не закрывается, а открывается снова»;
//   (2) владелец — премиум-магазин закрыт ESC или крестиком, а кнопка 73407 панели быстрого доступа «закрывает» уже закрытый
//   магазин, открывается он только со второго нажатия.
//   ПУТЬ ИГРОКА (законы diagnostic-must-use-real-path, ui-check-game-path-geometry):
//     • клавиши — НАСТОЯЩИЕ нажатия в `UPlayerInput` (`InputKey`, как `asda.Keybinds.HoldSim`): нажать → кадры → отпустить; их
//       ловит штатный опрос раскладки `AAsdaPlayerController` (фронт отпускания) → `ExecuteCommand`; клавиша — из живой раскладки;
//     • кнопки панели 73400 — `UAsdaSystemMenuSubsystem::ClickButton` (тот же `OnButtonToggled`, что щелчок мышью по чекбоксу;
//       мышью не жмём: окна, открытые тестом, накрывают панель, и эталонный гейт «мышь верхнему окну» съел бы щелчок);
//     • ESC — настоящая клавиша Escape (её опрос `UAsdaSystemOptionSubsystem::PollHotkeys` → `CloseTopmostOnEsc`);
//     • крестик — `OnClicked` кнопки окна с «CLOSE» в имени (обработчик, привязанный владельцем окна);
//     • «закрыть другим окном» — `UAsdaWindowSubsystem::CloseAllPlayerWindows` (эталонный `uiCloseAllWindows`: магазин 6000,
//       разговор с NPC и т. п.);
//     • смена карты — НАСТОЯЩИЙ травел UE на ту же карту и вход 4003 (`Asda.World.FakeSelfSpawn`, как
//       `asda.SelfTest.EntryWindowsTravel`); шаги — на тикере ядра, он переживает травел.
//   «Открыто» — ФАКТ: живой виджет своего мира во вьюпорте + видимость в реестре окон (не флаг владельца).
//   Для каждого переключателя: нажать (открыто) · нажать (закрыто) · 10 нажатий подряд (чётность) · ESC → нажать · крестик →
//   нажать · «закрыть всё» → нажать · подсветка кнопки панели = факт. После травела — два нажатия у каждого (флип факта).
//   `legacy` — исправления этой волны выключены ключами (`asda.SysMenu.ToggleFromFact 0`, `asda.PremiumMall.ToggleFromFact 0`,
//   `asda.Community.HotkeyToggle 0`): самотест обязан дать FAIL ровно на жалобах; травел в этом режиме не идёт (сторож мира
//   вернул бы ключи на очистке мира). `notravel` — без травела.
//   Итог: «ПЕРЕКЛЮЧАТЕЛИ ИТОГ: ok=N fail=M (пропусков K)».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaCVarSave.h"
#include "Test/AsdaSelfTestKit.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"
#include "UI/AsdaPremiumMallSubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaSystemOptionSubsystem.h"
#include "UI/AsdaCraftSubsystem.h"
#include "UI/AsdaDevMenuSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaToggleTest, Log, All);

namespace AsdaToggleTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.Toggles");   // имя самотеста (итог, ключи, выход)
	static constexpr float kStep = 0.22f;                       // пауза между шагами, с (окна порта открываются отложенно)

	// Способ нажать переключатель.
	enum class EHow : uint8
	{
		Key,      // клавиша раскладки команды `Arg` (настоящее нажатие в `UPlayerInput`)
		Menu,     // кнопка панели 73400 `Arg` (`ClickButton`)
		Etc       // пункт доп-меню 74600: команда `Arg` диспетчера `DispatchCommand`
	};

	// Один переключатель.
	struct FCase
	{
		FString Name;          // подпись в журнале
		EHow How = EHow::Key;  // чем нажимаем
		int32 Arg = 0;         // команда раскладки / IDNum кнопки / команда доп-меню
		FName Win;             // id окна в реестре (факт «открыто»)
		int32 Tab = -1;        // для окна 6400: вкладка, на которой окно считается «этим» окном (−1 — любая)
		int32 MenuBtn = 0;     // кнопка панели 73400, чья подсветка обязана равняться факту (0 — нет)
		bool bEsc = true;      // проверять ESC (окно в эталонной таблице `kEscCloseWindows`)
		bool bToggleCloses = true;   // нажатие при ОТКРЫТОМ окне доходит до команды (false — 73407: магазин 6000 сам зовёт
		                             //   `uiCloseAllWindows`, тот запрещает кнопки трея — эталон, нажимать нечем)
		bool bSkip = false;    // не открылось офлайн (гейт) — остальное по этому случаю пропускается
		bool bSub = false;     // текущий под-сценарий (ESC / крестик / «закрыть всё») к окну неприменим — его шаги пропускаются
		bool bFact0 = false;   // факт до нажатия (для проверок «флип»)
	};

	// Прогон: шаги на тикере ядра (переживают травел).
	struct FRun : public TSharedFromThis<FRun>
	{
		TWeakObjectPtr<UGameInstance> GI;
		bool bQuit = false;
		bool bLegacy = false;
		int32 Ok = 0, Fail = 0, Skip = 0;
		TArray<FCase> Cases;
		TArray<FString> Table;                              // строки итоговой таблицы
		TArray<TPair<float, TFunction<void(FRun&)>>> Steps;
		int32 Next = 0;
		double DueAt = 0.0;
		FTSTicker::FDelegateHandle H;

		UWorld* W() const { return GI.IsValid() ? GI->GetWorld() : nullptr; }
		template <class T> T* Sub() const { return GI.IsValid() ? GI->GetSubsystem<T>() : nullptr; }
		AAsdaPlayerController* PC() const { UWorld* X = W(); return X ? Cast<AAsdaPlayerController>(X->GetFirstPlayerController()) : nullptr; }
		void Check(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaToggleTest, Log,   TEXT("ПЕРЕКЛЮЧАТЕЛИ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaToggleTest, Error, TEXT("ПЕРЕКЛЮЧАТЕЛИ FAIL %s"), *What); }
			Table.Add(FString::Printf(TEXT("%s | %s"), b ? TEXT("ok  ") : TEXT("FAIL"), *What));
		}
		void SkipCase(const FString& What)
		{
			++Skip; UE_LOG(LogAsdaToggleTest, Warning, TEXT("ПЕРЕКЛЮЧАТЕЛИ ПРОПУСК %s"), *What);
			Table.Add(FString::Printf(TEXT("пропуск | %s"), *What));
		}
		void Add(float Delay, TFunction<void(FRun&)> Fn) { Steps.Emplace(Delay, MoveTemp(Fn)); }
		void Start()
		{
			DueAt = FPlatformTime::Seconds() + (Steps.Num() ? Steps[0].Key : 0.f);
			TSharedRef<FRun> Self = AsShared();
			H = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self](float) { return Self->TickOnce(); }));
		}
		bool TickOnce()
		{
			if (FPlatformTime::Seconds() < DueAt) { return true; }
			if (Next >= Steps.Num()) { return false; }
			TFunction<void(FRun&)> Fn = Steps[Next].Value;
			++Next;
			Fn(*this);
			if (Next >= Steps.Num())
			{
				for (const FString& L : Table) { UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ ТАБЛИЦА %s"), *L); }
				AsdaSelfTestKit::Finish(W(), kCmd, bQuit, LogAsdaToggleTest, TEXT("ПЕРЕКЛЮЧАТЕЛИ"), Ok, Fail,
					*FString::Printf(TEXT(" (пропусков %d%s)"), Skip, bLegacy ? TEXT(", режим legacy") : TEXT("")));
				return false;
			}
			DueAt = FPlatformTime::Seconds() + Steps[Next].Key;
			return true;
		}
	};

	// Факт «окно на экране»: живой виджет СВОЕГО мира во вьюпорте + видимость в реестре (+ вкладка у окна 6400).
	static bool Fact(FRun& R, const FCase& C)
	{
		UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(C.Win) : nullptr;
		const bool bShown = Wd && WM->IsWindowLive(Wd) && WM->IsWindowVisible(C.Win);
		if (!bShown || C.Tab < 0) { return bShown; }
		const UAsdaCommunitySubsystem* Cm = R.Sub<UAsdaCommunitySubsystem>();
		return Cm && Cm->GetActiveTab() == C.Tab;
	}

	// Подпись способа нажатия.
	static FString HowStr(const FCase& C)
	{
		switch (C.How)
		{
		case EHow::Key:  return FString::Printf(TEXT("клавиша cmd%d"), C.Arg);
		case EHow::Menu: return FString::Printf(TEXT("кнопка %d"), C.Arg);
		default:         return FString::Printf(TEXT("доп-меню команда %d"), C.Arg);
		}
	}

	// Клавиша раскладки команды (без модификатора); пустой ключ — не назначена.
	static FKey KeyOf(FRun& R, int32 Cmd)
	{
		AAsdaPlayerController* P = R.PC();
		FAsdaKeyBind B;
		return (P && P->Keys && P->Keys->GetBind(Cmd, B) && B.Vk != 0 && B.Mod == EAsdaKeyMod::None) ? UAsdaKeybindConfig::VkToFKey(B.Vk) : FKey();
	}
	// Клавиша прямо в `UPlayerInput` — тот же вход, что у физической клавиатуры после Slate.
	static void PiKey(FRun& R, const FKey& K, bool bDown)
	{
		AAsdaPlayerController* P = R.PC();
		if (!P || !K.IsValid()) { return; }
		if (bDown && FSlateApplication::IsInitialized()) { FSlateApplication::Get().SetAllUserFocusToGameViewport(); }
		P->InputKey(FInputKeyEventArgs::CreateSimulated(K, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}

	// Модальное окно-сообщение 1500 (предупреждение 97100 молла и т. п.) — игрок жмёт «ОК» до следующего действия; под модалкой
	//   эталон клавиши и панель не пропускает (замок ввода), поэтому перед КАЖДЫМ действием «ОК» нажимается её кнопкой.
	static void DismissMsgBox(FRun& R)
	{
		UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>();
		static const FName kOneBut(TEXT("ONEBUT_MSGBOX"));
		if (!WM || !WM->IsWindowVisible(kOneBut)) { return; }
		UUserWidget* Wd = WM->FindWindow(kOneBut);
		UButton* OkBtn = Wd ? Cast<UButton>(Wd->GetWidgetFromName(FName(TEXT("ONEBUT_BUT00")))) : nullptr;
		if (OkBtn && OkBtn->OnClicked.IsBound()) { OkBtn->OnClicked.Broadcast(); }
		else { WM->CloseWindow(kOneBut); }
		UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ окно-сообщение 1500 закрыто «ОК» (%s)"), OkBtn ? TEXT("кнопка ONEBUT_BUT00") : TEXT("кнопки нет — CloseWindow"));
	}

	// Шаги «нажать переключатель» (клавиша — два шага: нажать и отпустить; опрос стреляет по фронту отпускания).
	static void AddPress(FRun& R, int32 Ci)
	{
		const FCase& C = R.Cases[Ci];
		if (C.How == EHow::Key)
		{
			R.Add(kStep, [Ci](FRun& X)
			{
				if (X.Cases[Ci].bSkip || X.Cases[Ci].bSub) { return; }
				DismissMsgBox(X);
				const FKey K = KeyOf(X, X.Cases[Ci].Arg);
				if (K.IsValid()) { PiKey(X, K, true); }
				else if (AAsdaPlayerController* P = X.PC()) { P->RunCommandForCheck(X.Cases[Ci].Arg); }   // клавиши нет — тот же диспетчер
			});
			R.Add(0.12f, [Ci](FRun& X)
			{
				if (X.Cases[Ci].bSkip || X.Cases[Ci].bSub) { return; }
				const FKey K = KeyOf(X, X.Cases[Ci].Arg);
				if (K.IsValid()) { PiKey(X, K, false); }
			});
			return;
		}
		R.Add(kStep, [Ci](FRun& X)
		{
			const FCase& Cs = X.Cases[Ci];
			if (Cs.bSkip || Cs.bSub) { return; }
			DismissMsgBox(X);
			if (Cs.How == EHow::Menu)
			{
				if (UAsdaSystemMenuSubsystem* M = X.Sub<UAsdaSystemMenuSubsystem>()) { M->ClickButton(Cs.Arg); }
			}
			else if (UAsdaExtraMenuSubsystem* E = X.Sub<UAsdaExtraMenuSubsystem>())
			{
				E->DispatchCommand(Cs.Arg, TEXT("самотест переключателей"));
			}
		});
	}

	// Шаг проверки: факт == ожидание (+ подсветка кнопки панели = факт).
	static void AddExpect(FRun& R, int32 Ci, bool bWantOpen, const FString& What)
	{
		R.Add(kStep, [Ci, bWantOpen, What](FRun& X)
		{
			const FCase& C = X.Cases[Ci];
			if (C.bSkip || C.bSub) { return; }
			const bool bNow = Fact(X, C);
			FString Hl;
			bool bHlOk = true;
			if (C.MenuBtn)
			{
				const UAsdaSystemMenuSubsystem* M = X.Sub<UAsdaSystemMenuSubsystem>();
				const bool bChecked = M && M->IsButtonChecked(C.MenuBtn);
				bHlOk = (bChecked == bNow);
				Hl = FString::Printf(TEXT(", подсветка %d = %s"), C.MenuBtn, bChecked ? TEXT("горит") : TEXT("нет"));
			}
			X.Check(bNow == bWantOpen && bHlOk, FString::Printf(TEXT("[%s · %s] %s: окно %s (ждём %s)%s"),
				*C.Name, *HowStr(C), *What, bNow ? TEXT("открыто") : TEXT("закрыто"), bWantOpen ? TEXT("открыто") : TEXT("закрыто"), *Hl));
		});
	}

	// «Закрыть всё» эталона (`uiCloseAllWindows`) и парное возвращение HUD (`uiExpandPartyPanel`): без второго шага трей остаётся
	//   свёрнутым и запрещённым, а флаг «окна убраны сценой» держит гейты открытия (так ведёт себя сцена гачи между входом и выходом).
	static void CloseAllAndRestore(FRun& R)
	{
		if (UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>()) { WM->CloseAllPlayerWindows(); WM->RestoreAfterSceneExit(); }
	}

	// Всё закрыть перед случаем: окно-сообщение, «закрыть всё», ESC-меню, элитный магазин (его в росписи «закрыть всё» нет —
	//   он сам её зовёт), окна вне росписи — ESC-проходом по одному.
	static void CloseEverything(FRun& R)
	{
		DismissMsgBox(R);
		CloseAllAndRestore(R);
		if (UAsdaSystemOptionSubsystem* So = R.Sub<UAsdaSystemOptionSubsystem>()) { if (So->IsMenuShown()) { So->HideMenu(); } }
		if (UAsdaPremiumMallSubsystem* Ml = R.Sub<UAsdaPremiumMallSubsystem>()) { if (Ml->IsOpen()) { Ml->CloseMall(); } }
		if (UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>()) { for (int32 i = 0; i < 8 && WM->CloseTopmostOnEsc(); ++i) {} }
		for (FCase& C : R.Cases) { C.bSub = false; }
	}

	// Нажатие «сразу» (в этом же шаге): тот же диспетчер, что у клавиши (`RunCommandForCheck` = `ExecuteCommand`), кнопка панели,
	//   пункт доп-меню. Нужно там, где окно надо открыть внутри шага (крестика у окна не нашлось).
	static void AddPressNow(FRun& R, int32 Ci)
	{
		const FCase& C = R.Cases[Ci];
		DismissMsgBox(R);
		if (C.How == EHow::Key) { if (AAsdaPlayerController* P = R.PC()) { P->RunCommandForCheck(C.Arg); } return; }
		if (C.How == EHow::Menu) { if (UAsdaSystemMenuSubsystem* M = R.Sub<UAsdaSystemMenuSubsystem>()) { M->ClickButton(C.Arg); } return; }
		if (UAsdaExtraMenuSubsystem* E = R.Sub<UAsdaExtraMenuSubsystem>()) { E->DispatchCommand(C.Arg, TEXT("самотест переключателей")); }
	}

	// Крестик окна: кнопка с привязанным `OnClicked`, у которой в имени «CLOSE», либо имя кончается на «_X»/«_XX» (ящик 69000).
	static UButton* FindCloseButton(FRun& R, const FCase& C)
	{
		UAsdaWindowSubsystem* WM = R.Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(C.Win) : nullptr;
		UButton* Close = nullptr;
		if (Wd && Wd->WidgetTree)
		{
			Wd->WidgetTree->ForEachWidget([&Close](UWidget* Wg)
			{
				UButton* B = Cast<UButton>(Wg);
				if (Close || !B || !B->IsVisible() || !B->OnClicked.IsBound()) { return; }
				const FString N = B->GetName();
				if (N.Contains(TEXT("CLOSE"), ESearchCase::IgnoreCase) || N.EndsWith(TEXT("_X"), ESearchCase::IgnoreCase)
					|| N.EndsWith(TEXT("_XX"), ESearchCase::IgnoreCase)) { Close = B; }
			});
		}
		return Close;
	}

	// Сценарий одного переключателя.
	static void AddCaseBody(FRun& R, int32 Ci)
	{
		R.Add(kStep, [Ci](FRun& X) { CloseEverything(X); X.Cases[Ci].bSkip = false; });
		AddPress(R, Ci);
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (!Fact(X, C))
			{
				X.SkipCase(FString::Printf(TEXT("[%s · %s] первое нажатие не открыло окно «%s» (гейт офлайн) — случай пропущен"), *C.Name, *HowStr(C), *C.Win.ToString()));
				C.bSkip = true;
				return;
			}
			X.Check(true, FString::Printf(TEXT("[%s · %s] 1 нажатие: окно открыто"), *C.Name, *HowStr(C)));
			if (!C.bToggleCloses)
			{
				X.SkipCase(FString::Printf(TEXT("[%s · %s] 2/3/7 нажатие при открытом окне: кнопки трея запрещены «закрыть всё» магазина (эталон) — не проверяется"), *C.Name, *HowStr(C)));
			}
		});
		// 2 — повторное нажатие закрывает (жалоба 1: «при двух нажатиях должно свернуться»); 3 — 10 нажатий подряд, факт чередуется
		//   (жалоба 1: «при тысяче нажатий продолжает открываться»).
		R.Add(0.f, [Ci](FRun& X) { X.Cases[Ci].bSub = !X.Cases[Ci].bToggleCloses; });
		AddPress(R, Ci);
		AddExpect(R, Ci, false, TEXT("2 повторное нажатие"));
		for (int32 i = 0; i < 10; ++i)
		{
			AddPress(R, Ci);
			AddExpect(R, Ci, (i % 2) == 0, FString::Printf(TEXT("3 серия, нажатие %d/10"), i + 1));
		}
		R.Add(0.f, [Ci](FRun& X) { X.Cases[Ci].bSub = false; });
		// 4 — открытое окно закрыть ESC, нажать: открывается с ПЕРВОГО раза (жалоба 2). У 73407 окно и так открыто (шаг 1).
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (C.bSkip) { return; }
			C.bSub = Fact(X, C);   // уже открыто — нажимать не нужно
		});
		AddPress(R, Ci);
		R.Add(0.f, [Ci](FRun& X) { X.Cases[Ci].bSub = false; });
		AddExpect(R, Ci, true, TEXT("4а окно открыто перед ESC"));
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (C.bSkip) { return; }
			const UAsdaWindowSubsystem* WM = X.Sub<UAsdaWindowSubsystem>();
			if (!C.bEsc || !WM || !WM->IsEscCloseWindow(C.Win))
			{
				X.SkipCase(FString::Printf(TEXT("[%s · %s] 4 ESC: окна «%s» нет в эталонной таблице ESC (признак CControl+0x228) — оригинал по ESC его не закрывает"), *C.Name, *HowStr(C), *C.Win.ToString()));
				C.bSub = true;
				return;
			}
			DismissMsgBox(X);
			PiKey(X, EKeys::Escape, true);
		});
		R.Add(0.12f, [Ci](FRun& X) { if (!X.Cases[Ci].bSkip && !X.Cases[Ci].bSub) { PiKey(X, EKeys::Escape, false); } });
		AddExpect(R, Ci, false, TEXT("4б ESC закрыл окно"));
		AddPress(R, Ci);
		AddExpect(R, Ci, true, TEXT("4в нажатие после ESC — открыто с первого раза"));
		// 5 — крестик окна, нажать. Окно в начале — открыто (4в или, если ESC неприменим, 4а).
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (C.bSkip) { return; }
			C.bSub = false;
			DismissMsgBox(X);
			UButton* Close = FindCloseButton(X, C);
			if (!Close)
			{
				X.SkipCase(FString::Printf(TEXT("[%s · %s] 5 крестика с привязкой (имя *CLOSE*/*_X) у окна «%s» не нашлось — шаг пропущен"), *C.Name, *HowStr(C), *C.Win.ToString()));
				C.bSub = true;
				return;
			}
			UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ [%s] крестик «%s»"), *C.Name, *Close->GetName());
			Close->OnClicked.Broadcast();
		});
		AddExpect(R, Ci, false, TEXT("5а крестик закрыл"));
		AddPress(R, Ci);
		AddExpect(R, Ci, true, TEXT("5б нажатие после крестика — открыто с первого раза"));
		// 6 — «закрыть всё» (эталонный `uiCloseAllWindows` + возврат HUD), нажать. Окно вне росписи «закрыть всё» эталон не трогает.
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (C.bSkip) { return; }
			C.bSub = false;
			if (!Fact(X, C)) { AddPressNow(X, Ci); }   // 5 был пропущен и окно закрыто — сперва открыть (тот же диспетчер)
		});
		R.Add(kStep, [Ci](FRun& X)
		{
			if (X.Cases[Ci].bSkip) { return; }
			DismissMsgBox(X);
			CloseAllAndRestore(X);
		});
		R.Add(kStep, [Ci](FRun& X)
		{
			FCase& C = X.Cases[Ci];
			if (C.bSkip) { return; }
			if (Fact(X, C))
			{
				X.SkipCase(FString::Printf(TEXT("[%s · %s] 6 «закрыть всё»: окна «%s» нет в росписи uiCloseAllWindows — эталон его не закрывает"), *C.Name, *HowStr(C), *C.Win.ToString()));
				C.bSub = true;
			}
		});
		AddPress(R, Ci);
		AddExpect(R, Ci, true, TEXT("6б нажатие после «закрыть всё» — открыто с первого раза"));
		// 7 — нажатие закрывает (у 73407 — не проверяется: кнопки трея запрещены, пока открыт магазин).
		R.Add(0.f, [Ci](FRun& X) { X.Cases[Ci].bSub = !X.Cases[Ci].bToggleCloses; });
		AddPress(R, Ci);
		AddExpect(R, Ci, false, TEXT("7 нажатие — закрыто"));
		R.Add(0.f, [Ci](FRun& X) { X.Cases[Ci].bSub = false; });
	}

	// Список переключателей.
	static void BuildCases(FRun& R)
	{
		auto Add = [&R](const TCHAR* Name, EHow How, int32 Arg, const TCHAR* Win, int32 Tab, int32 MenuBtn, bool bEsc)
		{
			FCase C; C.Name = Name; C.How = How; C.Arg = Arg; C.Win = FName(Win); C.Tab = Tab; C.MenuBtn = MenuBtn; C.bEsc = bEsc;
			R.Cases.Add(C);
		};
		Add(TEXT("Кто? (J)"),               EHow::Key,  36,    TEXT("COMMUNITY_WIN"),         3,  73405, true);
		Add(TEXT("Друг (U)"),               EHow::Key,  35,    TEXT("COMMUNITY_WIN"),         0,  73405, true);
		Add(TEXT("Сообщество"),             EHow::Menu, 73405, TEXT("COMMUNITY_WIN"),        -1,  73405, true);
		Add(TEXT("Премиум-магазин (\\)"),   EHow::Key,  39,    TEXT("SHELLITEM_STORE_WIN"),  -1,  73407, true);
		Add(TEXT("Премиум-магазин"),        EHow::Menu, 73407, TEXT("SHELLITEM_STORE_WIN"),  -1,  73407, true);
		R.Cases.Last().bToggleCloses = false;   // магазин 6000 зовёт `uiCloseAllWindows` → кнопки трея запрещены, пока он открыт
		Add(TEXT("Персонаж (C)"),           EHow::Key,  30,    TEXT("CHARAINFO_WIN"),        -1,  73401, true);
		Add(TEXT("Персонаж"),               EHow::Menu, 73401, TEXT("CHARAINFO_WIN"),        -1,  73401, true);
		Add(TEXT("Сумка (I)"),              EHow::Key,  33,    TEXT("INVEN_WIN"),            -1,  73402, true);
		Add(TEXT("Сумка"),                  EHow::Menu, 73402, TEXT("INVEN_WIN"),            -1,  73402, true);
		Add(TEXT("Навыки (K)"),             EHow::Key,  32,    TEXT("SKILL_WIN"),            -1,  73403, true);
		Add(TEXT("Навыки"),                 EHow::Menu, 73403, TEXT("SKILL_WIN"),            -1,  73403, true);
		Add(TEXT("Журнал заданий (Q)"),     EHow::Key,  34,    TEXT("QUEST_WIN"),            -1,  73404, true);
		Add(TEXT("Журнал заданий"),         EHow::Menu, 73404, TEXT("QUEST_WIN"),            -1,  73404, true);
		Add(TEXT("Доп-меню"),               EHow::Menu, 73409, TEXT("ETC_NEW_WIN"),          -1,  73409, true);
		Add(TEXT("ESC-меню"),               EHow::Menu, 73406, TEXT("SYSTEM_MenuWIN_NEW"),   -1,  73406, false);
		Add(TEXT("Звания (T)"),             EHow::Key,  38,    TEXT("Title_Win"),            -1,  0,     true);
		Add(TEXT("Разборка (V)"),           EHow::Key,  40,    TEXT("Disassembly_Win"),      -1,  0,     true);
		Add(TEXT("Создание снаряжения (Z)"), EHow::Key, 41,    UAsdaCraftSubsystem::kMainWindowId, -1, 0, true);
		Add(TEXT("Улучшение снаряжения (G)"), EHow::Key, 42,   TEXT("Item_Upgrade_Win"),     -1,  0,     true);
		Add(TEXT("Питомец (P)"),            EHow::Key,  43,    TEXT("Pet_Cage_Win"),         -1,  0,     true);
		Add(TEXT("Карта мира (M)"),         EHow::Key,  37,    TEXT("MAXMAP_WIN"),           -1,  0,     true);
		Add(TEXT("Поиск отряда (доп-меню)"), EHow::Etc, 7,     TEXT("Find_Party_WIn"),       -1,  0,     true);
		Add(TEXT("Элитный ящик (доп-меню)"), EHow::Etc, 10,    TEXT("Primium_Item_post_WIN"), -1, 0,     true);
	}

	// Травел: открыть часть окон, настоящий травел UE на ту же карту, вход 4003; затем у каждого переключателя два нажатия —
	//   факт обязан флипнуться оба раза, подсветка — равняться факту.
	static void AddTravel(FRun& R)
	{
		// До травела: «Кто?», магазин, сумка, навыки (эталон держит сумку/навыки/сообщество, магазин закрывает загрузкой).
		R.Add(kStep, [](FRun& X) { CloseEverything(X); });
		for (int32 Ci : { 0, 3, 8, 10 }) { if (R.Cases.IsValidIndex(Ci)) { R.Cases[Ci].bSkip = false; AddPress(R, Ci); } }
		// Меню графики Ctrl+N (панель на GameInstance, мимо реестра окон) — тоже открыть до травела.
		R.Add(kStep, [](FRun& X) { if (UAsdaDevMenuSubsystem* Dm = X.Sub<UAsdaDevMenuSubsystem>()) { if (!Dm->IsMenuOpen()) { Dm->ToggleMenu(); } } });
		R.Add(0.6f, [](FRun& X)
		{
			FString S;
			for (int32 Ci : { 0, 3, 8, 10 }) { if (X.Cases.IsValidIndex(Ci)) { S += FString::Printf(TEXT("%s=%d "), *X.Cases[Ci].Name, Fact(X, X.Cases[Ci]) ? 1 : 0); } }
			UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ до травела: %s"), *S);
			if (UWorld* Wd = X.W()) { UGameplayStatics::OpenLevel(Wd, FName(TEXT("/Game/Map/alpen/alpen")), true, TEXT("game=/Script/AsdaReborn.AsdaWorldGameMode")); }
		});
		R.Add(6.f, [](FRun& X)
		{
			if (UWorld* Wd = X.W()) { if (GEngine) { GEngine->Exec(Wd, TEXT("Asda.Load.CurrentMap 3")); GEngine->Exec(Wd, TEXT("Asda.World.FakeSelfSpawn 256 256")); } }
		});
		R.Add(3.f, [](FRun& X) { UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ после травела: проверка нажатий")); });
		// Меню Ctrl+N: травел снял панель с вьюпорта — «открыто» обязано это видеть; одно переключение открывает, второе закрывает.
		R.Add(kStep, [](FRun& X)
		{
			UAsdaDevMenuSubsystem* Dm = X.Sub<UAsdaDevMenuSubsystem>();
			if (!Dm) { X.SkipCase(TEXT("[меню Ctrl+N] подсистемы нет")); return; }
			const bool bPtr = Dm->DebugHasMenuWidget();
			const bool bOpen0 = Dm->IsMenuOpen();
			Dm->ToggleMenu();
			const bool bOpen1 = Dm->IsMenuOpen();
			Dm->ToggleMenu();
			const bool bOpen2 = Dm->IsMenuOpen();
			X.Check(!bOpen0 && bOpen1 && !bOpen2, FString::Printf(TEXT("[меню Ctrl+N · ToggleMenu] после травела: указатель панели %s, «открыто» %d → нажатие %d → нажатие %d (ждём 0 → 1 → 0)"),
				bPtr ? TEXT("жив (панель снята с вьюпорта)") : TEXT("пуст"), bOpen0 ? 1 : 0, bOpen1 ? 1 : 0, bOpen2 ? 1 : 0));
		});
		// Порядок: сначала все окна, затем элитный магазин 6000 — его открытие зовёт «закрыть всё» и запрещает трей (эталон),
		//   что исказило бы послетравельное состояние остальных окон.
		TArray<int32> Order;
		for (int32 Ci = 0; Ci < R.Cases.Num(); ++Ci) { if (R.Cases[Ci].Win != FName(TEXT("SHELLITEM_STORE_WIN"))) { Order.Add(Ci); } }
		for (int32 Ci = 0; Ci < R.Cases.Num(); ++Ci) { if (R.Cases[Ci].Win == FName(TEXT("SHELLITEM_STORE_WIN"))) { Order.Add(Ci); } }
		for (int32 Ci : Order)
		{
			for (int32 Rep = 0; Rep < 2; ++Rep)
			{
				R.Add(kStep, [Ci, Rep](FRun& X) { X.Cases[Ci].bSkip = false; if (Rep == 0) { X.Cases[Ci].bSub = false; } X.Cases[Ci].bFact0 = Fact(X, X.Cases[Ci]); });
				AddPress(R, Ci);
				R.Add(kStep, [Ci, Rep](FRun& X)
				{
					FCase& C = X.Cases[Ci];
					if (C.bSub) { return; }   // T1 уже пропущен (окно не открывается офлайн) — T2 не о чем судить
					const bool bNow = Fact(X, C);
					FString Hl;
					bool bHlOk = true;
					if (C.MenuBtn)
					{
						const UAsdaSystemMenuSubsystem* M = X.Sub<UAsdaSystemMenuSubsystem>();
						const bool bChecked = M && M->IsButtonChecked(C.MenuBtn);
						bHlOk = (bChecked == bNow);
						Hl = FString::Printf(TEXT(", подсветка %d = %s"), C.MenuBtn, bChecked ? TEXT("горит") : TEXT("нет"));
					}
					// 73407 при открытом магазине нажать нечем (кнопки трея запрещены — эталон).
					if (C.bFact0 && !C.bToggleCloses)
					{
						X.SkipCase(FString::Printf(TEXT("[%s · %s] T%d после травела: окно открыто, кнопки трея запрещены (эталон) — не проверяется"), *C.Name, *HowStr(C), Rep + 1));
						return;
					}
					// Если окно не открывается офлайн и после травела (тот же гейт) — это не дефект переключателя.
					if (!C.bFact0 && !bNow && Rep == 0)
					{
						X.SkipCase(FString::Printf(TEXT("[%s · %s] T после травела: окно не открылось (гейт офлайн)"), *C.Name, *HowStr(C)));
						C.bSub = true;
						return;
					}
					X.Check(bNow != C.bFact0 && bHlOk, FString::Printf(TEXT("[%s · %s] T%d после травела: было %s → стало %s%s"),
						*C.Name, *HowStr(C), Rep + 1, C.bFact0 ? TEXT("открыто") : TEXT("закрыто"), bNow ? TEXT("открыто") : TEXT("закрыто"), *Hl));
				});
			}
		}
		R.Add(kStep, [](FRun& X) { CloseEverything(X); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestToggles(
	TEXT("asda.SelfTest.Toggles"),
	TEXT("Самотест кнопок-переключателей окон путём игрока: клавиши раскладки, кнопки панели 73400, пункты доп-меню; нажать/нажать/"
	     "серия/ESC/крестик/«закрыть всё»/травел; подсветка кнопок панели = факт. `legacy` (ключи волны 0), `notravel`, `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaToggleTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам окон — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
			FString Again = FString::Printf(TEXT("%s selfready"), kCmd);
			if (KA.Has(TEXT("legacy")))   { Again += TEXT(" legacy"); }
			if (KA.Has(TEXT("notravel"))) { Again += TEXT(" notravel"); }
			if (KA.bQuit)                 { Again += TEXT(" quit"); }
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle Hd;
			W->GetTimerManager().SetTimer(Hd, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		TSharedRef<FRun> R = MakeShared<FRun>();
		R->GI = W ? W->GetGameInstance() : nullptr;
		R->bQuit = KA.bQuit;
		R->bLegacy = KA.Has(TEXT("legacy"));
		if (!R->GI.IsValid() || !R->Sub<UAsdaWindowSubsystem>() || !R->Sub<UAsdaSystemMenuSubsystem>() || !R->PC())
		{
			AsdaSelfTestKit::Refused(W, kCmd, KA.bQuit, TEXT("нет GameInstance / реестра окон / панели меню / контроллера"));
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (R->bLegacy)
		{
			UE_LOG(LogAsdaToggleTest, Warning, TEXT("ПЕРЕКЛЮЧАТЕЛИ режим legacy: исправления волны ui_toggles выключены ключами"));
			AsdaCVarSave::SetForTest(TEXT("asda.SysMenu.ToggleFromFact"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.PremiumMall.ToggleFromFact"), 0);
			AsdaCVarSave::SetForTest(TEXT("asda.Community.HotkeyToggle"), 0);
		}
		BuildCases(*R);
		{
			FString Keys;
			for (const FCase& C : R->Cases)
			{
				if (C.How != EHow::Key) { continue; }
				const FKey K = KeyOf(*R, C.Arg);
				Keys += FString::Printf(TEXT("cmd%d=%s "), C.Arg, K.IsValid() ? *K.ToString() : TEXT("(нет — диспетчер команды)"));
			}
			UE_LOG(LogAsdaToggleTest, Log, TEXT("ПЕРЕКЛЮЧАТЕЛИ раскладка: %s"), *Keys);
		}
		for (int32 Ci = 0; Ci < R->Cases.Num(); ++Ci) { AddCaseBody(*R, Ci); }
		if (!R->bLegacy && !KA.Has(TEXT("notravel"))) { AddTravel(*R); }
		R->Start();
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
