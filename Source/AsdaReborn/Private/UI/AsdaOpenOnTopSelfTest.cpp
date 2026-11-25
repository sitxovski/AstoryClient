// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ОТКРЫТИЕ ПОВЕРХ ВСЕХ» — `asda.UI.SelfTestOpenOnTop [delay=N] [limit=N] [quit]` (правило владельца 08.10.2026).
//   Отчёт: Reports/owner_1008/ui_bugs.md. Механизм — `UAsdaWindowSubsystem::IsShowRaiseEligible` / `FlushShowRaise`.
//   Разделы:
//     A. Каталог: все окна реестра + все окна .JUI с ассетом WBP (кроме логина). Для каждого: спрятать, поднять «покрышку»
//        (обычное окно), показать проверяемое ОБЩИМ путём (`SetVisibility`) → поднимаемое встаёт наверх своей полосы (выше
//        покрышки и всех видимых окон полосы); исключение — z не меняется. Строка таблицы на окно: «[ОТКР-ТАБЛ] …».
//     A-контроль: `asda.UI.OpenOnTop 0` на первых окнах каталога — показ НЕ поднимает (самотест различает поведение).
//     B. Путь игрока: открыть сумку, НАСТОЯЩИЙ щелчок (события Slate) по кнопке «Разобрать» 453 → окно 32000 выше сумки;
//        контроль `asda.UI.OpenOnTop 0` — 32000 под сумкой (дефект владельца 08.10 воспроизводится).
//     C. Пары: окно A открыто → щелчок по телу A (подъём тела взведён, как нажатие в `TickDrag`) → открытие B штатной функцией
//        подсистемы → после отпускания все окна, показанные открытием B, выше A и всех прежних окон; и обратный порядок.
//        A = сумка 2000 / разговор с NPC 4200; B = окна предметов, магазины, склад, лавка (6301 через `DebugFeedPacket`),
//        улучшение, создание, персонаж, навыки, сообщество.
//   Исходящие пакеты перехватываются (`UAsdaPetSubsystem::BeginSendCapture`) — в сеть не уходит ничего.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"            // реестр окон, подъём, очередь показа, синтетический клик
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaUiHelpers.h"                  // AsdaUi::WidgetAbsRect — живая геометрия кнопки
#include "UI/AsdaItemTooltipSubsystem.h"       // погасить общую подсказку
#include "UI/AsdaInventorySubsystem.h"         // сумка 2000
#include "UI/AsdaDisassemblySubsystem.h"       // разборка 32000
#include "UI/AsdaItemUpgradeSubsystem.h"       // улучшение снаряжения
#include "UI/AsdaHighRefineSubsystem.h"        // высшая очистка
#include "UI/AsdaItemOptionSubsystem.h"        // опции предмета
#include "UI/AsdaItemResetSubsystem.h"         // сброс предмета
#include "UI/AsdaWarehouseSubsystem.h"         // склад
#include "UI/AsdaRepairSubsystem.h"            // ремонт
#include "UI/AsdaEquipShopSubsystem.h"         // магазин снаряжения
#include "UI/AsdaWarShopSubsystem.h"           // военный магазин
#include "UI/AsdaExtractShopSubsystem.h"       // магазин извлечения
#include "UI/AsdaBoosterShopSubsystem.h"       // магазин бустеров
#include "UI/AsdaPremiumShopSubsystem.h"       // элитный магазин
#include "UI/AsdaHairShopSubsystem.h"          // парикмахерская
#include "UI/AsdaConstellationSubsystem.h"     // созвездия
#include "UI/AsdaCraftWindowSubsystem.h"       // создание снаряжения 76000
#include "UI/AsdaCharaInfoSubsystem.h"         // окно персонажа
#include "UI/AsdaSkillWindowSubsystem.h"       // окно навыков
#include "UI/AsdaCommunitySubsystem.h"         // сообщество 6400
#include "UI/AsdaSelfStoreSubsystem.h"         // личная лавка 22000 (6301)
#include "UI/AsdaSoulmateSubsystem.h"          // зонд поля ввода: окно 9100 с фокусом в строке имени
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"            // общий зонд поля: однострочное поле с рамкой
#include "Components/MultiLineEditableText.h"      // общий зонд поля: многострочное
#include "Components/MultiLineEditableTextBox.h"   // общий зонд поля: многострочное с рамкой
#include "Framework/Application/SlateApplication.h"
#include "UI/AsdaTradeStateSubsystem.h"        // общий признак «лавка открыта» (гейты окон) — сброс между парами
#include "AsdaNetSubsystem.h"                  // DebugFeedPacket — штатный путь пакета
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих
#include "World/AsdaWorldSubsystem.h"          // свой павн офлайн (HUD и окна строятся в мире)
#include "AsdaOutgoingGate.h"                  // офлайн-гейт отладочных команд
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "AssetRegistry/AssetRegistryModule.h" // поиск WBP_<id>
#include "AsdaUiWbpCatalog.h"   // ★ R5-fix-b 13.11.2026 (третий заход): каталог WBP окон (общая точка)
#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOpenTopTest, Log, All);

namespace AsdaOpenTopLocal
{
	// Одно окно каталога раздела A.
	struct FItem
	{
		FName Id;                     // id окна (суффикс WBP)
		FString Path;                 // путь WBP (пусто — окно уже в реестре)
		bool bOpened = false;         // открыто самотестом (закрыть)
		bool bWasShown = false;       // было показано до проверки (вернуть)
		ESlateVisibility VisWas = ESlateVisibility::SelfHitTestInvisible;   // прежняя видимость
		int32 ZWas = 0;               // z до проверки (вернуть окну, открытому не нами)
	};

	// Функция открытия окна подсистемы (раздел C).
	struct FOpener
	{
		FString Name;                                   // подпись в отчёте
		TFunction<void(UGameInstance*)> Open;           // штатный вход подсистемы
		TFunction<void(UGameInstance*)> Close;          // штатное закрытие (между парами — чистый лист)
	};

	// Контекст многоэтапного прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0, Skip = 0;      // пройдено / провалено / пропущено
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти после прогона
		int32 Limit = 0;                       // >0 — только первые N окон раздела A
		TArray<FItem> Items;                   // каталог A
		int32 Idx = 0;                         // индекс в каталоге
		bool bCtrl = false;                    // A-контроль (ключ 0)
		int32 CtrlN = 0, CtrlNotRaised = 0;    // A-контроль: проверено / не поднято
		FName Cover;                           // окно-покрышка
		int32 ZBeforeShow = 0;                 // z проверяемого до показа
		uint32 CapGen = 0;                     // перехват исходящих взведён
		int32 Sent = 0;                        // перехвачено отправок
		int32 NRaised = 0, NExcl = 0, NNoShow = 0;   // статистика A
		TArray<FString> Table;                 // строки таблицы A
		// Раздел B.
		int32 BStep = 0;                       // 0 — основной, 1 — контроль (ключ 0)
		// Раздел C.
		TArray<FOpener> As, Bs;                // окна-«открыватели» A и открываемые B
		int32 PairA = 0, PairB = 0;            // текущая пара
		bool bReverse = false;                 // обратный порядок (B открыто первым)
		TSet<FName> VisBefore;                 // видимые окна до второго открытия
		FName FirstWin;                        // главное окно первого открытия (по нему щёлкнули)
		TArray<FString> PairTable;             // строки таблицы C
		TSharedRef<bool> SkillJobSet = MakeShared<bool>(false);   // ★ R5-fix-b 13.11.2026 (третий заход): подмену класса окна навыков поставил самотест

		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaOpenTopTest, Log, TEXT("ОТКРЫТИЕ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaOpenTopTest, Error, TEXT("ОТКРЫТИЕ FAIL %s"), *What); }
		}
	};

	using FStage = void (*)(TSharedRef<FCtx>);

	// Запустить этап не раньше Sec секунд И Frames кадров (первое открытие окна грузит ассеты — кадр длинный).
	static void After(TSharedRef<FCtx> C, float Sec, FStage Stage, int32 Frames = 4)
	{
		struct FPoll
		{
			// Опрос раз в кадр до выполнения обоих условий.
			static void Tick(TSharedRef<FCtx> C, double TEnd, uint64 FEnd, FStage Stage)
			{
				UWorld* W = C->W.Get();
				if (!W) { return; }
				if (FPlatformTime::Seconds() >= TEnd && GFrameCounter >= FEnd) { Stage(C); return; }
				W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { Tick(C, TEnd, FEnd, Stage); }));
			}
		};
		UWorld* W = C->W.Get();
		if (!W) { return; }
		const double TEnd = FPlatformTime::Seconds() + Sec;
		const uint64 FEnd = GFrameCounter + (uint64)FMath::Max(Frames, 1);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { FPoll::Tick(C, TEnd, FEnd, Stage); }));
	}

	// Подсистема окон мира прогона.
	static UAsdaWindowSubsystem* WMOf(const TSharedRef<FCtx>& C)
	{
		UWorld* W = C->W.Get();
		return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	}
	// GameInstance мира прогона.
	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Установить консольную переменную (int).
	static void SetCv(const TCHAR* Name, int32 V)
	{
		AsdaCVarSave::SetForTest(Name, V);
	}

	// Видимость окна (true — показано).
	static bool IsShown(UAsdaWindowSubsystem* WM, FName Id) { return WM && WM->IsWindowVisible(Id); }

	// Полоса окна для сравнения: 1 — AlwaysTop [8600…8699], 0 — обычный слой (< 8600), −1 — прочие полосы (не сравниваем).
	static int32 BandOf(UAsdaWindowSubsystem* WM, FName Id)
	{
		const int32 Z = WM->GetWindowZOrder(Id);
		if (Z >= UAsdaWindowSubsystem::kAlwaysTopZ && Z <= UAsdaWindowSubsystem::kAlwaysTopZMax) { return 1; }
		if (Z < UAsdaWindowSubsystem::kAlwaysTopZ) { return 0; }
		return -1;
	}

	// Окно Id — верх своей полосы среди видимых «настоящих» окон (поднимаемых при показе), кроме своих потомков и Ignore.
	//   OutAbove — окна выше.
	static bool IsTopOfBand(UAsdaWindowSubsystem* WM, FName Id, FString& OutAbove, const TSet<FName>& Ignore = TSet<FName>())
	{
		const int32 Z = WM->GetWindowZOrder(Id);
		const int32 Band = BandOf(WM, Id);
		bool bTop = true;
		for (const FName O : WM->GetRegisteredWindowIds())
		{
			if (O == Id || Ignore.Contains(O) || !IsShown(WM, O)) { continue; }
			if (WM->IsWindowOwnedBy(O, Id)) { continue; }                 // потомок законно выше владельца
			if (!WM->IsShowRaiseEligible(O)) { continue; }                // HUD/плашки/модалки/свои полосы — не соперники
			if (BandOf(WM, O) != Band) { continue; }
			const int32 Zo = WM->GetWindowZOrder(O);
			if (Zo > Z) { bTop = false; OutAbove += FString::Printf(TEXT(" %s(%d)"), *O.ToString(), Zo); }
		}
		return bTop;
	}

	// Модалка от прошлой проверки гасит разбор у окон ниже — закрыть.
	static void CloseStrayModals(UAsdaWindowSubsystem* WM)
	{
		if (!WM->IsAnyModalOpen()) { return; }
		for (const FName Id : WM->GetRegisteredWindowIds())
		{
			if (WM->GetWindowZOrder(Id) >= UAsdaWindowSubsystem::kModalZ && IsShown(WM, Id)) { WM->CloseWindowSilently(Id); }
		}
		WM->ClearMsgBoxQueue(TEXT("самотест открытия поверх: модалка от прошлой проверки"));
	}

	static void Finish(TSharedRef<FCtx> C);
	static void StageItemShow(TSharedRef<FCtx> C);
	static void StageItemCheck(TSharedRef<FCtx> C);
	static void StageB(TSharedRef<FCtx> C);
	static void StageC(TSharedRef<FCtx> C);

	// ═══════════ A. КАТАЛОГ ═══════════

	// Следующее окно каталога (или контроль / раздел B).
	static void NextItem(TSharedRef<FCtx> C);

	// A-1: подготовить окно (открыть скрытым или запомнить и спрятать), поднять покрышку.
	static void StageItemPrep(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		CloseStrayModals(WM);
		FItem& It = C->Items[C->Idx];
		It.bOpened = false;
		UUserWidget* Wd = WM->FindWindow(It.Id);
		if (Wd)
		{
			It.bWasShown = IsShown(WM, It.Id);
			It.VisWas = Wd->GetVisibility();
			It.ZWas = WM->GetWindowZOrder(It.Id);
		}
		else
		{
			if (It.Path.IsEmpty()) { ++C->Skip; NextItem(C); return; }
			Wd = WM->OpenWindowByPath(It.Id, It.Path, 750, ESlateVisibility::Collapsed, false);
			if (!Wd) { ++C->Skip; C->Table.Add(FString::Printf(TEXT("%s | — | не открылось"), *It.Id.ToString())); NextItem(C); return; }
			It.bOpened = true;
			It.bWasShown = false;
			It.ZWas = WM->GetWindowZOrder(It.Id);
		}
		Wd->SetVisibility(ESlateVisibility::Collapsed);
		// Покрышка — видимое обычное окно наверху обычного слоя (у AlwaysTop проверяется верх своей полосы).
		if (It.Id != C->Cover)
		{
			if (UUserWidget* Cw = WM->FindWindow(C->Cover)) { Cw->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
			WM->BringWindowToFront(C->Cover);
		}
		WM->FlushShowRaise(TEXT("самотест: покрышка"));
		After(C, 0.02f, &StageItemShow, 2);
	}

	// A-2: показать общим путём (`SetVisibility`) — очередь подъёма разберёт начало тика Slate.
	static void StageItemShow(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = C->Items[C->Idx];
		C->ZBeforeShow = WM->GetWindowZOrder(It.Id);
		if (UUserWidget* Wd = WM->FindWindow(It.Id)) { Wd->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		After(C, 0.02f, &StageItemCheck, 3);
	}

	// A-3: проверка и откат окна.
	static void StageItemCheck(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		FItem& It = C->Items[C->Idx];
		const bool bShownNow = IsShown(WM, It.Id);
		FString Why;
		const bool bElig = WM->IsShowRaiseEligible(It.Id, &Why);
		const int32 ZNow = WM->GetWindowZOrder(It.Id);
		FString Above;
		const bool bTop = bShownNow && IsTopOfBand(WM, It.Id, Above);
		const FString Src = It.bOpened ? TEXT("каталог .JUI (z 750)") : TEXT("реестр (открыто подсистемой)");
		if (!bShownNow)
		{
			// Окно прячет само себя (подсистема держит своё состояние видимости) — показ не состоялся.
			++C->NNoShow; ++C->Skip;
			C->Table.Add(FString::Printf(TEXT("%s | %s | z %d | показ не удержался (окно прячет подсистема) | —"), *It.Id.ToString(), *Src, C->ZBeforeShow));
		}
		else if (C->bCtrl)
		{
			++C->CtrlN;
			if (!bTop || ZNow == C->ZBeforeShow) { ++C->CtrlNotRaised; }
		}
		else if (bElig)
		{
			++C->NRaised;
			C->Expect(bTop, FString::Printf(TEXT("«%s» (%s): показ → верх полосы, z %d → %d%s"), *It.Id.ToString(), *Src, C->ZBeforeShow, ZNow,
				bTop ? TEXT("") : *FString::Printf(TEXT(", выше:%s"), *Above)));
			C->Table.Add(FString::Printf(TEXT("%s | %s | z %d → %d | поднимается | %s"), *It.Id.ToString(), *Src, C->ZBeforeShow, ZNow, bTop ? TEXT("ok") : TEXT("FAIL")));
		}
		else
		{
			++C->NExcl;
			C->Expect(ZNow == C->ZBeforeShow, FString::Printf(TEXT("«%s» (%s): исключение (%s), z %d → %d"), *It.Id.ToString(), *Src, *Why, C->ZBeforeShow, ZNow));
			C->Table.Add(FString::Printf(TEXT("%s | %s | z %d → %d | исключение: %s | %s"), *It.Id.ToString(), *Src, C->ZBeforeShow, ZNow, *Why,
				ZNow == C->ZBeforeShow ? TEXT("ok") : TEXT("FAIL")));
		}
		// Откат.
		if (It.bOpened) { WM->CloseWindowSilently(It.Id); }
		else
		{
			if (WM->FindWindow(It.Id) && WM->GetWindowZOrder(It.Id) != It.ZWas) { WM->SetWindowZOrder(It.Id, It.ZWas); }
			if (UUserWidget* Wd = WM->FindWindow(It.Id)) { Wd->SetVisibility(It.bWasShown ? It.VisWas : ESlateVisibility::Collapsed); }
		}
		WM->FlushShowRaise(TEXT("самотест: откат"));
		NextItem(C);
	}

	static void NextItem(TSharedRef<FCtx> C)
	{
		++C->Idx;
		const int32 N = C->bCtrl ? FMath::Min(C->Items.Num(), 25) : C->Items.Num();
		if (C->Idx < N) { After(C, 0.01f, &StageItemPrep, 1); return; }
		if (!C->bCtrl)
		{
			// A-контроль: ключ 0 — показ поднимать не должен.
			C->bCtrl = true;
			C->Idx = 0;
			SetCv(TEXT("asda.UI.OpenOnTop"), 0);
			After(C, 0.05f, &StageItemPrep, 2);
			return;
		}
		SetCv(TEXT("asda.UI.OpenOnTop"), 1);
		C->Expect(C->CtrlNotRaised > 0, FString::Printf(TEXT("A-контроль (asda.UI.OpenOnTop 0): из %d показанных НЕ поднялось %d — самотест различает поведение"),
			C->CtrlN, C->CtrlNotRaised));
		UE_LOG(LogAsdaOpenTopTest, Log, TEXT("ОТКРЫТИЕ инфо: раздел A — поднимаемых %d, исключений %d, показ не удержался %d"), C->NRaised, C->NExcl, C->NNoShow);
		if (UUserWidget* Cw = WMOf(C)->FindWindow(C->Cover)) { WMOf(C)->CloseWindowSilently(C->Cover); }
		C->BStep = 0;
		After(C, 0.2f, &StageB, 6);
	}

	// ═══════════ B. ПУТЬ ИГРОКА: сумка → кнопка «Разобрать» 453 → окно 32000 ═══════════

	static void StageBClick(TSharedRef<FCtx> C);
	static void StageBCheck(TSharedRef<FCtx> C);

	// B-1: закрыть всё, открыть сумку.
	static void StageB(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UGameInstance* GI = GIOf(C);
		if (!WM || !GI) { Finish(C); return; }
		CloseStrayModals(WM);
		WM->CloseAllPlayerWindows();
		SetCv(TEXT("asda.UI.OpenOnTop"), C->BStep == 0 ? 1 : 0);
		if (UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>()) { Inv->OpenWindow(); }
		After(C, 0.4f, &StageBClick, 10);
	}

	// B-2: настоящий щелчок по кнопке 453 (события мыши Slate).
	static void StageBClick(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		UUserWidget* Inv = WM->FindWindow(FName(TEXT("INVEN_WIN")));
		UWidget* Btn = Inv ? Inv->GetWidgetFromName(TEXT("Inven_Disassembly_Btn")) : nullptr;
		FVector2D Mn, Mx;
		if (!Btn || !AsdaUi::WidgetAbsRect(Btn, Mn, Mx))
		{
			C->Expect(false, TEXT("B: кнопка Inven_Disassembly_Btn не найдена / нет геометрии"));
			After(C, 0.1f, &StageC);
			return;
		}
		WM->SimulateClickAt((Mn + Mx) * 0.5, 2);
		After(C, 0.5f, &StageBCheck, 12);
	}

	// B-3: 32000 выше сумки (контроль — под сумкой).
	static void StageBCheck(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		const FName Dis(TEXT("Disassembly_Win")), Inv(TEXT("INVEN_WIN"));
		const bool bDis = IsShown(WM, Dis), bInv = IsShown(WM, Inv);
		const int32 Zd = WM->GetWindowZOrder(Dis), Zi = WM->GetWindowZOrder(Inv);
		if (C->BStep == 0)
		{
			FString Above;
			C->Expect(bDis && bInv && Zd > Zi && IsTopOfBand(WM, Dis, Above),
				FString::Printf(TEXT("B: щелчок «Разобрать» в сумке → 32000 открыто=%d z %d, сумка открыта=%d z %d — 32000 ВЫШЕ сумки и верх слоя%s"),
					bDis ? 1 : 0, Zd, bInv ? 1 : 0, Zi, *Above));
			C->PairTable.Add(FString::Printf(TEXT("сумка → «Разобрать» 453 (настоящий щелчок) | Disassembly_Win z %d > INVEN_WIN z %d | %s"),
				Zd, Zi, (bDis && Zd > Zi) ? TEXT("ok") : TEXT("FAIL")));
			C->BStep = 1;
			After(C, 0.1f, &StageB);
			return;
		}
		C->Expect(bDis && bInv && Zd < Zi, FString::Printf(TEXT("B-контроль (asda.UI.OpenOnTop 0): 32000 z %d ПОД сумкой z %d — дефект владельца воспроизводится"), Zd, Zi));
		SetCv(TEXT("asda.UI.OpenOnTop"), 1);
		After(C, 0.1f, &StageC);
	}

	// ═══════════ C. ПАРЫ ═══════════

	static void StageCSecond(TSharedRef<FCtx> C);
	static void StageCCheck(TSharedRef<FCtx> C);
	static void NextPair(TSharedRef<FCtx> C);

	// Видимые поднимаемые окна (снимок).
	static TSet<FName> VisibleEligible(UAsdaWindowSubsystem* WM)
	{
		TSet<FName> S;
		for (const FName Id : WM->GetRegisteredWindowIds()) { if (IsShown(WM, Id) && WM->IsShowRaiseEligible(Id)) { S.Add(Id); } }
		return S;
	}

	// Верхнее из окон набора (главное окно открытия).
	static FName TopOf(UAsdaWindowSubsystem* WM, const TSet<FName>& S)
	{
		FName Best; int32 Bz = MIN_int32;
		for (const FName Id : S) { const int32 Z = WM->GetWindowZOrder(Id); if (Z > Bz) { Bz = Z; Best = Id; } }
		return Best;
	}

	static const FOpener& FirstOpener(const TSharedRef<FCtx>& C) { return C->bReverse ? C->Bs[C->PairB] : C->As[C->PairA]; }
	static const FOpener& SecondOpener(const TSharedRef<FCtx>& C) { return C->bReverse ? C->As[C->PairA] : C->Bs[C->PairB]; }

	// C-1: закрыть всё, открыть первое окно пары.
	static void StageC(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UGameInstance* GI = GIOf(C);
		if (!WM || !GI) { Finish(C); return; }
		if (C->PairA >= C->As.Num()) { Finish(C); return; }
		CloseStrayModals(WM);
		// Чистый лист: штатные закрытия всех окон пар (закрытие окна предмета/магазина может вернуть сумку/персонажа — их последними),
		//   затем общее закрытие окон игрока.
		for (int32 i = C->Bs.Num() - 1; i >= 0; --i) { if (C->Bs[i].Close) { C->Bs[i].Close(GI); } }
		for (const FOpener& O : C->As) { if (O.Close) { O.Close(GI); } }
		for (const FOpener& O : C->Bs) { if (O.Name == TEXT("персонаж") && O.Close) { O.Close(GI); } }
		if (C->As.Num() > 0 && C->As[0].Close) { C->As[0].Close(GI); }   // сумка
		WM->CloseAllPlayerWindows();
		CloseStrayModals(WM);
		WM->FlushShowRaise(TEXT("самотест: закрыто всё"));
		C->VisBefore = VisibleEligible(WM);
		FirstOpener(C).Open(GI);
		After(C, 0.25f, &StageCSecond, 8);
	}

	// C-2: щелчок по телу первого окна (подъём тела взведён) + открытие второго штатной функцией.
	static void StageCSecond(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UGameInstance* GI = GIOf(C);
		if (!WM || !GI) { Finish(C); return; }
		CloseStrayModals(WM);
		TSet<FName> Now = VisibleEligible(WM);
		TSet<FName> New = Now.Difference(C->VisBefore);
		C->FirstWin = TopOf(WM, New);
		if (C->FirstWin.IsNone())
		{
			++C->Skip;
			C->PairTable.Add(FString::Printf(TEXT("%s → %s | первое окно «%s» офлайн не открылось | пропуск"),
				*FirstOpener(C).Name, *SecondOpener(C).Name, *FirstOpener(C).Name));
			NextPair(C);
			return;
		}
		C->VisBefore = Now;
		WM->DebugSetPendingBodyRaise(C->FirstWin);   // нажатие по телу первого окна (как `TickDrag` на фронте нажатия)
		SecondOpener(C).Open(GI);                    // обработчик щелчка открывает второе окно
		After(C, 0.25f, &StageCCheck, 8);            // отпускание: подъём тела, затем очередь показа
	}

	// C-3: все окна, показанные вторым открытием, выше всех прежних видимых окон полосы.
	static void StageCCheck(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		const TSet<FName> Now = VisibleEligible(WM);
		const TSet<FName> New = Now.Difference(C->VisBefore);
		const FString Pair = FString::Printf(TEXT("%s → %s"), *FirstOpener(C).Name, *SecondOpener(C).Name);
		if (New.Num() == 0)
		{
			++C->Skip;
			C->PairTable.Add(FString::Printf(TEXT("%s | второе окно офлайн не открылось (или уже было открыто) | пропуск"), *Pair));
			NextPair(C);
			return;
		}
		// Порядок новых окон (сверху вниз) — для разбора спутников (окно + сумка вместе).
		TArray<FName> NewSorted = New.Array();
		NewSorted.Sort([WM](const FName& A, const FName& B) { return WM->GetWindowZOrder(A) > WM->GetWindowZOrder(B); });
		FString NewS;
		for (const FName Id : NewSorted) { NewS += FString::Printf(TEXT(" %s(%d)"), *Id.ToString(), WM->GetWindowZOrder(Id)); }
		bool bOk = true;
		FString Bad;
		for (const FName Id : New)
		{
			const int32 Band = BandOf(WM, Id);
			for (const FName O : Now)
			{
				if (New.Contains(O) || BandOf(WM, O) != Band || WM->IsWindowOwnedBy(O, Id)) { continue; }
				if (WM->GetWindowZOrder(O) > WM->GetWindowZOrder(Id)) { bOk = false; Bad += FString::Printf(TEXT(" %s(%d)>%s(%d)"), *O.ToString(), WM->GetWindowZOrder(O), *Id.ToString(), WM->GetWindowZOrder(Id)); }
			}
		}
		const bool bFirstStill = IsShown(WM, C->FirstWin);
		C->Expect(bOk, FString::Printf(TEXT("пара %s: щелчок по «%s» → новые окна%s выше прежних%s"), *Pair, *C->FirstWin.ToString(), *NewS,
			bOk ? TEXT("") : *FString::Printf(TEXT(" — НАРУШЕНО:%s"), *Bad)));
		C->PairTable.Add(FString::Printf(TEXT("%s | первое %s(%d)%s | новые:%s | %s"), *Pair, *C->FirstWin.ToString(), WM->GetWindowZOrder(C->FirstWin),
			bFirstStill ? TEXT("") : TEXT(" закрыто вторым"), *NewS, bOk ? TEXT("ok") : TEXT("FAIL")));
		NextPair(C);
	}

	static void NextPair(TSharedRef<FCtx> C)
	{
		if (!C->bReverse) { C->bReverse = true; }
		else
		{
			C->bReverse = false;
			if (++C->PairB >= C->Bs.Num()) { C->PairB = 0; ++C->PairA; }
		}
		After(C, 0.05f, &StageC, 2);
	}

	// Подсистема по типу.
	template <class T> static T* Sub(UGameInstance* GI) { return GI ? GI->GetSubsystem<T>() : nullptr; }

	// Списки пар раздела C.
	static void BuildOpeners(TSharedRef<FCtx> C)
	{
		C->As.Add({ TEXT("сумка"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaInventorySubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaInventorySubsystem>(GI)) { S->CloseWindow(); } } });
		C->As.Add({ TEXT("разговор с NPC"), [](UGameInstance* GI)
		{
			// [САМОТЕСТ] офлайн NPC нет — показ окна разговора 4200 тем же реестром (z 120, как `kDlgZ`) и подъём, как у эталона
			//   (`CClass_MapInsert(g_pWindowMgr, this[4244])`).
			UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI);
			if (!WM) { return; }
			const FName Id(TEXT("NPC_LINE_NPC"));
			if (!WM->FindWindow(Id)) { WM->OpenWindowByPath(Id, TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_NPC_LINE_NPC"), 120, ESlateVisibility::SelfHitTestInvisible); }
			WM->SetWindowVisible(Id, true, ESlateVisibility::SelfHitTestInvisible);
			WM->BringWindowToFront(Id);
		}, [](UGameInstance* GI) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI)) { WM->SetWindowVisible(FName(TEXT("NPC_LINE_NPC")), false); } } });
		C->Bs.Add({ TEXT("разборка 32000"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaDisassemblySubsystem>(GI)) { S->Toggle(TEXT("самотест открытия поверх")); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaDisassemblySubsystem>(GI)) { if (S->IsOpen()) { S->CloseWindow(); } } } });   // ★ 08.10.2026 (пакет 2, МЕЛК-3): закрывать только ОТКРЫТОЕ, как эталонный `uiCloseAllWindows` (ui_core.c:54362, `sub_933CA7 == 1`) — закрытие скрытого ставило [223] и гейт 1 с держал закрытие 78200 в следующем цикле
		C->Bs.Add({ TEXT("улучшение"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemUpgradeSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemUpgradeSubsystem>(GI)) { S->HideWindow(); } } });
		C->Bs.Add({ TEXT("высшая очистка"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaHighRefineSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaHighRefineSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("опции предмета"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemOptionSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemOptionSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("сброс предмета"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemResetSubsystem>(GI)) { S->OpenWindow(0); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaItemResetSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("склад"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaWarehouseSubsystem>(GI)) { S->OpenWindow(0); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaWarehouseSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("ремонт"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaRepairSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaRepairSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("магазин снаряжения"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaEquipShopSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaEquipShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("военный магазин"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaWarShopSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaWarShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("магазин извлечения"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaExtractShopSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaExtractShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("магазин бустеров"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaBoosterShopSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaBoosterShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("элитный магазин"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaPremiumShopSubsystem>(GI)) { S->OpenWindow(false); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaPremiumShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("парикмахерская"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaHairShopSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaHairShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("созвездия"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaConstellationSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaConstellationSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Bs.Add({ TEXT("создание 76000"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCraftWindowSubsystem>(GI)) { S->GateAndToggle(TEXT("самотест открытия поверх")); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCraftWindowSubsystem>(GI)) { S->HideAndCleanup(); } } });
		C->Bs.Add({ TEXT("лавка 22000 (6301)"), [](UGameInstance* GI)
		{
			// Штатный путь пакета: ответ сервера 6301 код 1 → `sellWindow_Open`.
			if (UAsdaNetSubsystem* Net = Sub<UAsdaNetSubsystem>(GI)) { Net->DebugFeedPacket(6301, TArray<uint8>{ 1 }); }
			if (auto* S = Sub<UAsdaSelfStoreSubsystem>(GI)) { S->PumpPending(); }
		}, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaSelfStoreSubsystem>(GI)) { S->ResetSessionState(EAsdaSessionResetReason::MapChanged); } if (auto* T = Sub<UAsdaTradeStateSubsystem>(GI)) { T->ResetSessionState(EAsdaSessionResetReason::MapChanged); } } });
		C->Bs.Add({ TEXT("персонаж"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCharaInfoSubsystem>(GI)) { S->OpenWindow(); } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCharaInfoSubsystem>(GI)) { S->CloseWindow(); } } });
		// ★ R5-fix-b 13.11.2026 (третий заход): окно навыков без класса не открывается (гейт эталона `skillwin_OpenSkillWindow`: job ≤ 0 → 9421).
		//   Офлайн класс оставлял только предыдущий самотест пачки — в одиночном прогоне (и в редакторе) 4 пары «навыки» уходили
		//   в пропуск. Класса нет — подмена класса 1 (`SetJobForTest`, как у `UI.SelfTestCloseGear`); снимает её сам самотест.
		const TSharedRef<bool> JobSet = C->SkillJobSet;
		C->Bs.Add({ TEXT("навыки"), [JobSet](UGameInstance* GI) { if (auto* S = Sub<UAsdaSkillWindowSubsystem>(GI)) { if (S->GetSelfJob() <= 0) { S->SetJobForTest(1, -1); *JobSet = true; } S->OpenWindow(); } },
			[JobSet](UGameInstance* GI) { if (auto* S = Sub<UAsdaSkillWindowSubsystem>(GI)) { S->CloseWindow(); if (*JobSet) { S->SetJobForTest(-1, -1); *JobSet = false; } } } });
		C->Bs.Add({ TEXT("сообщество 6400"), [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCommunitySubsystem>(GI)) { S->OpenOnTab(0); /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */ } }, [](UGameInstance* GI) { if (auto* S = Sub<UAsdaCommunitySubsystem>(GI)) { S->CloseWindow(); } } });
	}

	static void Finish(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UGameInstance* GI = GIOf(C);
		if (WM) { CloseStrayModals(WM); WM->CloseAllPlayerWindows(); }
		if (*C->SkillJobSet && GI)   // ★ R5-fix-b 13.11.2026 (третий заход): подмена класса окна навыков снимается и при досрочном конце
		{
			if (UAsdaSkillWindowSubsystem* S = GI->GetSubsystem<UAsdaSkillWindowSubsystem>()) { S->SetJobForTest(-1, -1); }
			*C->SkillJobSet = false;
		}
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestOpenOnTop"));   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ UI.OpenOnTop — значение + SetBy (прежде — CvOpen с SetByConsole)
		if (C->CapGen && GI) { if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		for (const FString& R : C->Table) { UE_LOG(LogAsdaOpenTopTest, Log, TEXT("[ОТКР-ТАБЛ] %s"), *R); }
		for (const FString& R : C->PairTable) { UE_LOG(LogAsdaOpenTopTest, Log, TEXT("[ОТКР-ПАРЫ] %s"), *R); }
		UE_LOG(LogAsdaOpenTopTest, Log, TEXT("ОТКРЫТИЕ ПОВЕРХ ИТОГ: ok=%d fail=%d пропуск=%d, перехвачено отправок %d"), C->Ok, C->Fail, C->Skip, C->Sent);
		if (C->bQuit && GEngine && C->W.Get()) { GEngine->Exec(C->W.Get(), TEXT("quit")); }
	}

	// Каталог + покрышка, затем раздел A.
	static void StageCatalog(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = WMOf(C);
		if (!WM) { Finish(C); return; }
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>()) { Tip->HideTooltip(); }
		TMap<FName, FString> Wbp;
		AsdaUiWbpCatalog::Collect(Wbp);   // ★ R5-fix-b 13.11.2026 (третий заход): общая точка трёх копий; класс `WBP_<id>_C` собранной игры — под id `<id>`
		TSet<FName> Seen;
		for (const FName Id : WM->GetRegisteredWindowIds()) { FItem It; It.Id = Id; C->Items.Add(It); Seen.Add(Id); }
		TArray<FName> Keys; Wbp.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const FName K : Keys) { if (!Seen.Contains(K)) { FItem It; It.Id = K; It.Path = Wbp[K]; C->Items.Add(It); } }
		// Покрышка — обычное окно каталога, поднимаемое при показе и не AlwaysTop.
		for (const FName K : Keys)
		{
			if (WM->FindWindow(K)) { continue; }
			if (!WM->OpenWindowByPath(K, Wbp[K], 760, ESlateVisibility::SelfHitTestInvisible, false)) { continue; }
			if (WM->IsShowRaiseEligible(K) && !WM->IsWindowAlwaysTop(K)) { C->Cover = K; break; }
			WM->CloseWindowSilently(K);
		}
		if (C->Cover.IsNone()) { C->Expect(false, TEXT("нет покрышки")); Finish(C); return; }
		C->Items.RemoveAll([C](const FItem& It) { return It.Id == C->Cover; });
		if (C->Limit > 0 && C->Items.Num() > C->Limit) { C->Items.SetNum(C->Limit); }
		UE_LOG(LogAsdaOpenTopTest, Log, TEXT("ОТКРЫТИЕ инфо: каталог %d окон (реестр + .JUI с WBP), покрышка «%s»"), C->Items.Num(), *C->Cover.ToString());
		BuildOpeners(C);
		C->Idx = 0;
		After(C, 0.1f, &StageItemPrep);
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!WMOf(C)) { UE_LOG(LogAsdaOpenTopTest, Error, TEXT("ОТКРЫТИЕ: нет подсистемы окон")); return; }
		SetCv(TEXT("asda.UI.OpenOnTop"), 1);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): исходное значение и SetBy запоминает `AsdaCVarSave` (прежде — поле CvOpen)
		if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>())
		{
			TWeakPtr<FCtx> WC = C;
			C->CapGen = Pet->BeginSendCapture([WC](int32, const TArray<uint8>&, bool, TCHAR) { if (TSharedPtr<FCtx> S = WC.Pin()) { ++S->Sent; } }, 0) ? 1u : 0u;
		}
		UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		if (WS && !::IsValid(WS->LocalPlayerActor.Get()) && GEngine)
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			WS->DebugSpawnSelfAt(256, 256, TEXT("СамТест"));
		}
		After(C, 2.5f, &StageCatalog, 30);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestOpenOnTop(
	TEXT("asda.UI.SelfTestOpenOnTop"),
	TEXT("asda.UI.SelfTestOpenOnTop [delay=N] [limit=N] [quit] — ОФЛАЙН-самотест правила владельца 08.10.2026 «каждое открываемое окно ")
	TEXT("поверх всех»: каталог всех окон (показ → верх полосы, исключения), настоящий щелчок «Разобрать» в сумке, пары окон в обе стороны, ")
	TEXT("контрольные прогоны ключом asda.UI.OpenOnTop 0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestOpenOnTop"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestOpenOnTop"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		int32 Limit = 0;     // limit=N — только первые N окон раздела A
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
			else if (Arg.StartsWith(TEXT("limit="))) { Limit = FCString::Atoi(*Arg.Mid(6)); }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit, Limit]()
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.UI.SelfTestOpenOnTop"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaOpenTopLocal::FCtx> C = MakeShared<AsdaOpenTopLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit; C->Limit = Limit;
			AsdaOpenTopLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

// Дамп решения «поднимается ли окно при показе» по всем окнам реестра.
static FAutoConsoleCommandWithWorld GAsdaUiDumpShowRaiseList(
	TEXT("asda.UI.DumpShowRaiseList"),
	TEXT("asda.UI.DumpShowRaiseList — для каждого окна реестра: z, видимость и решение «поднимается при показе» с причиной"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UAsdaWindowSubsystem* WM = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM) { return; }
		TArray<FName> Ids = WM->GetRegisteredWindowIds();
		Ids.Sort([WM](const FName& A, const FName& B) { return WM->GetWindowZOrder(A) < WM->GetWindowZOrder(B); });
		for (const FName Id : Ids)
		{
			FString Why;
			const bool b = WM->IsShowRaiseEligible(Id, &Why);
			UE_LOG(LogAsdaOpenTopTest, Log, TEXT("[показ-список] %-28s z=%5d вид=%d → %s | %s"),
				*Id.ToString(), WM->GetWindowZOrder(Id), WM->IsWindowVisible(Id) ? 1 : 0, b ? TEXT("ПОДНИМАЕТСЯ") : TEXT("нет"), *Why);
		}
	}));

// ★ ЗОНД (08.10.2026): окно с фокусом в строке ввода, открытое и поднятое очередью показа, — строка и фокус живы?
//   `asda.UI.ProbeEditRaise` — 9100 `OpenLinkWindow` (поле пустое + фокус), кадр (очередь показа поднимает окно), запись текста,
//   кадр, чтение текста и фокуса. Печать «[зонд-поле]».
static FAutoConsoleCommandWithWorld GAsdaUiProbeEditRaise(
	TEXT("asda.UI.ProbeEditRaise"),
	TEXT("asda.UI.ProbeEditRaise — зонд: окно 9100 с фокусом в поле имени, подъём очередью показа, запись/чтение текста и фокуса"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!Soul || !WM) { return; }
		Soul->OpenLinkWindow();
		auto Dump = [Soul, WM](const TCHAR* Tag)
		{
			UUserWidget* L = Soul->GetLinkWindowForCheck();
			UEditableText* E = L ? Cast<UEditableText>(L->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))) : nullptr;
			const TSharedPtr<SWidget> F = FSlateApplication::Get().GetKeyboardFocusedWidget();
			UE_LOG(LogAsdaOpenTopTest, Log, TEXT("[зонд-поле] %s: z %d, текст «%s», фокус у поля %d, фокус: %s"), Tag,
				WM->GetWindowZOrder(TEXT("SOULMATE_LINK_WIN")), E ? *E->GetText().ToString() : TEXT("?"), E ? (E->HasKeyboardFocus() ? 1 : 0) : -1,
				F.IsValid() ? *F->GetTypeAsString() : TEXT("нет"));
		};
		Dump(TEXT("после открытия"));
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([Soul, Dump, W]()
		{
			Dump(TEXT("кадр спустя (подъём очередью)"));
			UUserWidget* L = Soul->GetLinkWindowForCheck();
			if (UEditableText* E = L ? Cast<UEditableText>(L->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))) : nullptr) { E->SetText(FText::FromString(TEXT("ДругТест"))); }
			Dump(TEXT("сразу после записи"));
			FTimerHandle H2;
			W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([Dump, Soul, W]()
			{
				Dump(TEXT("через 0.5 с"));
				// Щелчок по «ОК» 9104 настоящими событиями Slate — обработчик читает поле (`coupleSendProposalReqTarget`).
				UUserWidget* L = Soul->GetLinkWindowForCheck();
				UWidget* Ok = L ? L->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN1")) : nullptr;
				FVector2D Mn, Mx;
				UAsdaWindowSubsystem* WM2 = W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>();
				if (Ok && WM2 && AsdaUi::WidgetAbsRect(Ok, Mn, Mx)) { WM2->SimulateClickAt((Mn + Mx) * 0.5, 3); }
				FTimerHandle H3;
				W->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateLambda([Dump]() { Dump(TEXT("после щелчка «ОК»")); }), 0.5f, false);
			}), 0.5f, false);
		}), 1.5f, false);   // после разбора очереди показа: окно уже поднято, поле с фокусом — как в самотесте 1-А H2/H3
	}));

// ★ ЗОНД ОБЩИЙ (08.10.2026, ревизия ui_bugs_REVIEW М-1): любое открытое окно реестра и его строка ввода.
//   `asda.UI.ProbeEditRaiseAt <ИдОкна> <ИмяПоля> [ДругоеОкно]` — фокус в поле → подъём ДРУГОГО окна и затем этого (как
//   щелчок по телу / `BringWindowToFront` из кода) → программная запись «ПробаПоле» → фокус уходит на вьюпорт (потеря
//   фокуса, как при щелчке мимо) → чтение. Печать «[зонд-поле-общий] … ЦЕЛ / ПОТЕРЯН». Поля: UEditableText(Box), многострочные.
static FText AsdaProbeGetText(UWidget* F)
{
	if (UEditableText* A = Cast<UEditableText>(F)) { return A->GetText(); }
	if (UEditableTextBox* B = Cast<UEditableTextBox>(F)) { return B->GetText(); }
	if (UMultiLineEditableText* C = Cast<UMultiLineEditableText>(F)) { return C->GetText(); }
	if (UMultiLineEditableTextBox* D = Cast<UMultiLineEditableTextBox>(F)) { return D->GetText(); }
	return FText::FromString(TEXT("<не поле>"));
}
// Запись в поле любого из четырёх типов (тот же `SetText`, что зовёт код подсистем).
static void AsdaProbeSetText(UWidget* F, const FText& T)
{
	if (UEditableText* A = Cast<UEditableText>(F)) { A->SetText(T); }
	else if (UEditableTextBox* B = Cast<UEditableTextBox>(F)) { B->SetText(T); }
	else if (UMultiLineEditableText* C = Cast<UMultiLineEditableText>(F)) { C->SetText(T); }
	else if (UMultiLineEditableTextBox* D = Cast<UMultiLineEditableTextBox>(F)) { D->SetText(T); }
}
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiProbeEditRaiseAt(
	TEXT("asda.UI.ProbeEditRaiseAt"),
	TEXT("asda.UI.ProbeEditRaiseAt <ИдОкна> <ИмяПоля> [ДругоеОкно] — зонд: фокус в поле, подъём другого окна и этого, запись, потеря фокуса, чтение"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM || A.Num() < 2) { UE_LOG(LogAsdaOpenTopTest, Warning, TEXT("[зонд-поле-общий] использование: <ИдОкна> <ИмяПоля> [ДругоеОкно]")); return; }
		const FName Win(*A[0]);
		const FName Field(*A[1]);
		const FName Other = A.Num() > 2 ? FName(*A[2]) : FName(TEXT("INVEN_WIN"));
		UUserWidget* L = WM->FindWindow(Win);
		UWidget* F = L ? L->GetWidgetFromName(Field) : nullptr;
		if (!F) { UE_LOG(LogAsdaOpenTopTest, Warning, TEXT("[зонд-поле-общий] «%s».«%s»: окна/поля нет — ПРОПУСК"), *A[0], *A[1]); return; }
		F->SetKeyboardFocus();
		TWeakObjectPtr<UWidget> WF = F;
		FTimerHandle H1;
		W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateLambda([WM, Win, Other, WF, W]()
		{
			if (!WF.IsValid()) { return; }
			const bool bFocBefore = WF->HasKeyboardFocus();
			if (WM->IsOpen(Other)) { WM->BringWindowToFront(Other); }
			WM->BringWindowToFront(Win);                           // подъём окна, у поля которого фокус
			AsdaProbeSetText(WF.Get(), FText::FromString(TEXT("ПробаПоле")));
			FTimerHandle H2;
			W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateLambda([WM, Win, WF, W, bFocBefore]()
			{
				if (!WF.IsValid()) { return; }
				const bool bFocNow = WF->HasKeyboardFocus();
				FSlateApplication::Get().SetAllUserFocusToGameViewport();   // потеря фокуса полем (щелчок мимо)
				FTimerHandle H3;
				W->GetTimerManager().SetTimer(H3, FTimerDelegate::CreateLambda([WM, Win, WF, bFocBefore, bFocNow]()
				{
					if (!WF.IsValid()) { return; }
					const FString T = AsdaProbeGetText(WF.Get()).ToString();
					const bool bOk = T == TEXT("ПробаПоле");
					UE_LOG(LogAsdaOpenTopTest, Log, TEXT("[зонд-поле-общий] «%s».«%s»: фокус до подъёма %d, после %d; после потери фокуса «%s» → %s (asda.UI.ZOrderInPlace=%d)"),
						*Win.ToString(), *WF->GetName(), bFocBefore ? 1 : 0, bFocNow ? 1 : 0, *T, bOk ? TEXT("ЦЕЛ") : TEXT("ПОТЕРЯН"),
						IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.ZOrderInPlace"))->GetInt());
				}), 0.3f, false);
			}), 0.3f, false);
		}), 0.3f, false);
	}));

// [САМОТЕСТ] `asda.UI.DebugFillLayoutAll <x> <y>` — всем ключам перетаскивания меты записать сдвиг (x, y) в раскладку ini ПРОГОНА
//   (отдельная папка настроек `-saveddirsuffix`). Только офлайн. Прогон «все ключи за экраном» правила владельца 08.10.
static FAutoConsoleCommandWithWorldAndArgs GAsdaUiDebugFillLayoutAll(
	TEXT("asda.UI.DebugFillLayoutAll"),
	TEXT("asda.UI.DebugFillLayoutAll <x> <y> — ОФЛАЙН: всем ключам перетаскивания меты .JUI записать сдвиг (x,y) в ini прогона"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.DebugFillLayoutAll"), KitArgs)) { return; }
		UAsdaWindowSubsystem* WM = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (!WM || A.Num() < 2) { return; }
		WM->DebugFillAllLayouts(FVector2D(FCString::Atof(*A[0]), FCString::Atof(*A[1])));
	}));

#endif // !UE_BUILD_SHIPPING
