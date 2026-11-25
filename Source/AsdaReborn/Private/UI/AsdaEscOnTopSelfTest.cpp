// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «ESC ЗАКРЫВАЕТ ВЕРХНЕЕ ОКНО» — `asda.UI.SelfTestEscOnTop [delay=N] [quit]` (10.10.2026, отчёт
//   `Reports/owner_1010/esc_ranks.md`). Эталон — `uiContainerHandleKey@0x12AF390` (ui_core.c:104068): ESC уходит
//   ПЕРВОМУ видимому корневому окну с признаком `CControl+0x228` (сверху вниз), событие 21, одно окно за нажатие.
//   Проверяется штатный проход порта `UAsdaWindowSubsystem::CloseTopmostOnEsc` (его зовёт настоящая клавиша в
//   `UAsdaSystemOptionSubsystem::PollHotkeys`; в -RenderOffscreen клавишу не доставить — см. шапку `asda.UI.EscClose`).
//   Разделы:
//     A. Пары «окно X поверх сумки 400 → ESC закрывает X, сумка остаётся; второй ESC закрывает сумку»:
//        настройки 69500, ESC-меню 69400, магазин бустеров 15300, парикмахерская 15000, меню вкладок чата 2670,
//        контроль — ремонт 57300 (роспись была и до 10.10).
//     B. Обратный порядок: настройки открыты, сумка поверх → первый ESC закрывает сумку, настройки остаются.
//     C. Слои внутри окна: парикмахерская с открытой панелью покупки 15168 → ESC закрывает только панель.
//     D. Сцена премиум-гачи 68000: в покое (клип 1) ESC закрывает сцену; не в покое — съедает и сцену не закрывает.
//        ★ 07.11.2026 [ИСПРАВЛЕНИЕ, решение владельца]: при `asda.Gacha.PrizeCloseLock` 1 ESC с незабранным призом ещё и показывает
//        окно ONEBUT_MSGBOX; разделы D2 и D3 проверяют окно и закрывают его кнопкой, иначе модалка заперла бы следующие разделы.
//   ★ 10.10.2026 (отчёт `Reports/owner_1010/barber_minors.md`, М-3/М-4 ревизии `esc_ranks_REVIEW.md`):
//     D3. Гача во время прокрутки: ответ 6578 со статусом 1 (`SimulateRollResult`, тот же путь, что сеть) → фаза `Rolling`
//         → ESC съеден, сцена открыта (ui_core.c:7664-7670).
//     C2. Парикмахерская по умолчанию (`LegacyConfirmPanel 0`): «Купить» → общая модалка 1559 (эталон показывает её
//         модально, признака ESC нет) → ESC ничего не закрывает и не съедается, ESC-меню заперто; «Отмена» 1562 → ESC
//         закрывает окно 15000.
//     E. Магазин бустеров 15300: панель покупки 15515 + окно «Вид» 15600 → 1-й ESC закрывает «Вид», 2-й — панель,
//        3-й — магазин (shop_trade.c:1703-1706 + ui_core.c:10518).
//     F. Поиск соулмейта 8200 и окно «о себе» 8300 с фокусом в поле 8302. Окно 8300 стоит в полосе AlwaysTop, для игрока оно выше 8200. Ключ `asda.UI.EscExtraWindows` 1:
//        ESC снимает фокус, ESC закрывает 8300, ESC закрывает 8200. Ключ 0 (оригинал): ESC закрывает 8200 и снимает фокус (`cFindSoulmateWinHide@0xE72BD0`), 8300 остаётся.
//     G. Сообщество 6400 с под-окном 6640 `OldSoulmate_Win` → ESC закрывает 6400 и скрывает 6640 (`communityUI_Close`).
//     K. Контроль (самотест различает поведение): роспись окна 69500 снята → ESC закрывает сумку ПОД настройками
//        (дефект ревизии `overlaps_fixes2_REVIEW.md` воспроизводится), роспись возвращается.
//   Исходящие пакеты перехватываются (`UAsdaPetSubsystem::BeginSendCapture`) — в сеть не уходит ничего.
#include "CoreMinimal.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if !UE_BUILD_SHIPPING

#include "UI/AsdaWindowSubsystem.h"            // проход ESC, реестр окон
#include "AsdaCVarSave.h"                   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключи консоли самотеста — запомнить и вернуть (значение + SetBy)
#include "UI/AsdaInventorySubsystem.h"         // сумка 400 (окно INVEN_WIN)
#include "UI/AsdaExchangeP2PSubsystem.h"       // ★ R5-fix-b 13.11.2026 (третий заход): сброс оставшегося окна обмена 2008
#include "UI/AsdaTradeStateSubsystem.h"        // ★ R5-fix-b 13.11.2026 (третий заход): признак «окно обмена 2008»
#include "UI/AsdaSystemOptionSubsystem.h"      // настройки 69500 и ESC-меню 69400
#include "UI/AsdaBoosterShopSubsystem.h"       // магазин бустеров 15300
#include "UI/AsdaHairShopSubsystem.h"          // парикмахерская 15000
#include "UI/AsdaChatWindowSubsystem.h"        // меню вкладок чата 2670
#include "UI/AsdaRepairSubsystem.h"            // ремонт 57300 (контроль)
#include "UI/AsdaPremiumGachaSubsystem.h"      // сцена премиум-гачи 68000
#include "UI/AsdaPurchaseBoxSubsystem.h"       // общая модалка покупки 1559 (раздел C2)
#include "UI/AsdaSoulmateSubsystem.h"          // поиск соулмейта 8200 и окно «о себе» 8300 (раздел F)
#include "UI/AsdaCommunitySubsystem.h"         // сообщество 6400 и под-окно 6640 (раздел G)
#include "Pet/AsdaPetSubsystem.h"              // перехват исходящих
#include "World/AsdaWorldSubsystem.h"          // свой павн офлайн (HUD и окна строятся в мире)
#include "World/AsdaPlayerCharacter.h"         // ★ 25.10.2026: пересборка модели при возврате причёски (изоляция в пакете)
// офлайн-гейт отладочных команд   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Blueprint/UserWidget.h"            // ★ 07.11.2026: окно-сообщение гачи (поиск кнопки ОК)
#include "Components/Button.h"               // ★ 07.11.2026: кнопка ОК окна-сообщения
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaEscTopTest, Log, All);

namespace AsdaEscTopLocal
{
	static const FName kInven(TEXT("INVEN_WIN"));   // сумка 400 — нижнее окно пар

	// Одна пара раздела A: как открыть окно X, как узнать, что оно показано, и как закрыть его между парами.
	struct FCase
	{
		FString Name;                                       // подпись в отчёте
		FName ZWin;                                         // окно, чей z сравнивается с сумкой (для слоя — окно-владелец)
		TFunction<void(UGameInstance*)> Open;               // штатный вход подсистемы
		TFunction<bool(UGameInstance*)> IsShown;            // окно X показано
		TFunction<void(UGameInstance*)> Close;              // штатное закрытие (чистый лист между парами)
	};

	// Контекст прогона.
	struct FCtx
	{
		int32 Ok = 0, Fail = 0, Skip = 0;      // пройдено / провалено / пропущено
		TWeakObjectPtr<UWorld> W;              // мир прогона
		bool bQuit = false;                    // выйти после прогона
		uint32 CapGen = 0;                     // перехват исходящих взведён
		int32 Sent = 0;                        // перехвачено отправок
		int8 DayTopWas = -1;                   // ★ В3 S1: AlwaysTop кнопки дня гачи до прогона (−1 — не снят)
		TArray<FCase> Cases;                   // пары раздела A
		int32 Idx = 0;                         // текущая пара
		TArray<FString> Table;                 // строки итоговой таблицы
		int32 HairLegacyWas = 0;               // `asda.HairShop.LegacyConfirmPanel` до раздела C
		// ★ 25.10.2026 (изоляция в пакете): причёска своего персонажа до разделов C/C2. «Купить» карточки 0 ПРИМЕРЯЕТ причёску
		//   (`PreviewHair`), а откат парикмахерской (`RevertPreview`) при офлайн-причёске −1 молчит (`ApplySelfHair(-1)` не применяется) —
		//   после прогона у персонажа оставалась причёска карточки 0, и следующий самотест (`asda.UI.SelfTestModalLock`, раздел M)
		//   видел карточку 0 «уже купленной» (кнопка «Купить» 01 выключена — щелчок не открывал 1559). Возврат — в `StageFinish`.
		bool bHairSaved = false;               // снимок причёски снят
		int32 HairStyleWas = -1;               // стиль до прогона (`SelfParts.HairStyle`)
		int32 HairColorWas = -1;               // цвет до прогона (`SelfParts.HairColor`)

		// Одна проверка: печать и счёт.
		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok; UE_LOG(LogAsdaEscTopTest, Log, TEXT("ESC-ВЕРХ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaEscTopTest, Error, TEXT("ESC-ВЕРХ FAIL %s"), *What); }
		}
		// Пропуск (офлайн окно не открылось) — не провал, но в отчёт.
		void SkipCase(const FString& What)
		{
			++Skip; UE_LOG(LogAsdaEscTopTest, Warning, TEXT("ESC-ВЕРХ ПРОПУСК %s"), *What);
			Table.Add(What + TEXT(" | пропуск"));
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
				if (!W) { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestEscOnTop")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
				if (FPlatformTime::Seconds() >= TEnd && GFrameCounter >= FEnd) { Stage(C); return; }
				W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { Tick(C, TEnd, FEnd, Stage); }));
			}
		};
		UWorld* W = C->W.Get();
		if (!W) { AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestEscOnTop")); return; }   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернуть и на этом выходе
		const double TEnd = FPlatformTime::Seconds() + Sec;
		const uint64 FEnd = GFrameCounter + (uint64)FMath::Max(Frames, 1);
		W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([C, TEnd, FEnd, Stage]() { FPoll::Tick(C, TEnd, FEnd, Stage); }));
	}

	// GameInstance мира прогона.
	using AsdaSelfTestCtx::GIOf;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.
	// Подсистема GameInstance (nullptr — нет).
	template<typename T> static T* Sub(UGameInstance* GI) { return GI ? GI->GetSubsystem<T>() : nullptr; }
	// Подсистема окон мира прогона.
	static UAsdaWindowSubsystem* WMOf(const TSharedRef<FCtx>& C) { return Sub<UAsdaWindowSubsystem>(GIOf(C)); }
	// Сумка показана.
	static bool InvenShown(const TSharedRef<FCtx>& C) { UAsdaWindowSubsystem* WM = WMOf(C); return WM && WM->IsWindowVisible(kInven); }
	// Закрыть сумку штатно (если открыта).
	static void CloseInven(const TSharedRef<FCtx>& C)
	{
		if (InvenShown(C)) { if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(GIOf(C))) { Inv->CloseWindow(); } }
	}
	// Открыть сумку штатно.
	static void OpenInven(const TSharedRef<FCtx>& C)
	{
		if (!InvenShown(C)) { if (UAsdaInventorySubsystem* Inv = Sub<UAsdaInventorySubsystem>(GIOf(C))) { Inv->OpenWindow(); } }
	}

	// Этапы прогона (объявлены заранее: цепочка A → B → C → D → K → итог идёт вперёд по файлу и назад к началу).
	static void StagePairOpen(TSharedRef<FCtx> C);     // A1: сумка, затем окно X
	static void StagePairOpenX(TSharedRef<FCtx> C);    // A1': окно X
	static void StagePairEsc(TSharedRef<FCtx> C);      // A2: два ESC
	static void StagePairNext(TSharedRef<FCtx> C);     // A3: следующая пара
	static void StageReverse1(TSharedRef<FCtx> C);     // B1: настройки
	static void StageReverse1Inv(TSharedRef<FCtx> C);  // B1': сумка поверх
	static void StageReverse2(TSharedRef<FCtx> C);     // B2: ESC
	static void StageLayer1(TSharedRef<FCtx> C);       // C1: парикмахерская
	static void StageLayer1Buy(TSharedRef<FCtx> C);    // C1': панель покупки
	static void StageLayer2(TSharedRef<FCtx> C);       // C2: ESC
	static void StageGacha1(TSharedRef<FCtx> C);       // D1: сцена гачи
	static void StageGacha2(TSharedRef<FCtx> C);       // D2: ESC
	static void StageRoll1(TSharedRef<FCtx> C);        // D3: сцена гачи для прокрутки
	static void StageRoll2(TSharedRef<FCtx> C);        // D3': ответ 6578 → прокрутка
	static void StageRoll3(TSharedRef<FCtx> C);        // D3'': ESC во время прокрутки
	static void StageHairModal1(TSharedRef<FCtx> C);   // C2: парикмахерская
	static void StageHairModal2(TSharedRef<FCtx> C);   // C2': «Купить» → модалка 1559
	static void StageHairModal3(TSharedRef<FCtx> C);   // C2'': ESC при модалке, «Отмена», ESC
	static void StageBoost1(TSharedRef<FCtx> C);       // E1: магазин бустеров
	static void StageBoost2(TSharedRef<FCtx> C);       // E1': панель покупки + «Вид»
	static void StageBoost3(TSharedRef<FCtx> C);       // E2: три ESC
	static void StageFind1(TSharedRef<FCtx> C);        // F1: поиск 8200
	static void StageFind2(TSharedRef<FCtx> C);        // F1': окно «о себе» 8300 (кнопка 8270, фокус в поле 8302)
	static void StageFind3(TSharedRef<FCtx> C);        // F2: ESC, ключ `asda.UI.EscExtraWindows` 1
	static void StageFindOld1(TSharedRef<FCtx> C);     // F3: ключ 0, снова поиск 8200
	static void StageFindOld2(TSharedRef<FCtx> C);     // F3': снова окно 8300
	static void StageFindOld3(TSharedRef<FCtx> C);     // F4: ESC как в оригинале
	static void StageComm1(TSharedRef<FCtx> C);        // G1: сообщество 6400
	static void StageComm2(TSharedRef<FCtx> C);        // G1': под-окно 6640
	static void StageComm3(TSharedRef<FCtx> C);        // G2: ESC
	static void StageCtrl1(TSharedRef<FCtx> C);        // K1: сумка
	static void StageCtrl1Opt(TSharedRef<FCtx> C);     // K1': настройки поверх
	static void StageCtrl2(TSharedRef<FCtx> C);        // K2: ESC без росписи 69500
	static void StageFinish(TSharedRef<FCtx> C);       // итог

	// ── Раздел A, шаг 3: пара разобрана — следующая ──
	static void StagePairNext(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		if (C->Cases.IsValidIndex(C->Idx) && GI) { C->Cases[C->Idx].Close(GI); }
		CloseInven(C);
		++C->Idx;
		After(C, 0.3f, &StagePairOpen);
	}

	// ── Раздел A, шаг 2: X показано поверх сумки → ESC закрывает X, сумка на месте; второй ESC — сумку ──
	static void StagePairEsc(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaWindowSubsystem* WM = WMOf(C);
		FCase& K = C->Cases[C->Idx];
		if (!WM || !GI || !K.IsShown(GI) || !InvenShown(C))
		{
			C->SkipCase(FString::Printf(TEXT("%s поверх сумки: офлайн окно %s"), *K.Name,
				InvenShown(C) ? TEXT("не открылось") : TEXT("сумка не открылась")));
			StagePairNext(C);
			return;
		}
		const int32 ZX = WM->GetWindowZOrder(K.ZWin), ZI = WM->GetWindowZOrder(kInven);
		C->Expect(ZX > ZI, FString::Printf(TEXT("%s: окно выше сумки до ESC (z %d > %d)"), *K.Name, ZX, ZI));
		const bool bEaten1 = WM->CloseTopmostOnEsc();
		const bool bXGone = !K.IsShown(GI), bInvStay = InvenShown(C);
		C->Expect(bEaten1 && bXGone && bInvStay,
			FString::Printf(TEXT("%s поверх сумки: 1-й ESC съеден=%d, окно закрыто=%d, сумка осталась=%d"), *K.Name, (int32)bEaten1, (int32)bXGone, (int32)bInvStay));
		const bool bEaten2 = WM->CloseTopmostOnEsc();
		const bool bInvGone = !InvenShown(C);
		C->Expect(bEaten2 && bInvGone, FString::Printf(TEXT("%s: 2-й ESC закрыл сумку (съеден=%d, сумка закрыта=%d)"), *K.Name, (int32)bEaten2, (int32)bInvGone));
		C->Table.Add(FString::Printf(TEXT("%s поверх сумки | 1-й ESC: окно %s, сумка %s | 2-й ESC: сумка %s"), *K.Name,
			bXGone ? TEXT("закрыто") : TEXT("ОСТАЛОСЬ"), bInvStay ? TEXT("на месте") : TEXT("ЗАКРЫТА"), bInvGone ? TEXT("закрыта") : TEXT("ОСТАЛАСЬ")));
		StagePairNext(C);
	}

	// ── Раздел A, шаг 1: сумка, затем окно X ──
	static void StagePairOpen(TSharedRef<FCtx> C)
	{
		if (!C->Cases.IsValidIndex(C->Idx)) { After(C, 0.2f, &StageReverse1); return; }
		OpenInven(C);
		After(C, 0.5f, &StagePairOpenX, 6);
	}
	static void StagePairOpenX(TSharedRef<FCtx> C)
	{
		if (UGameInstance* GI = GIOf(C)) { C->Cases[C->Idx].Open(GI); }
		After(C, 0.8f, &StagePairEsc, 8);
	}

	// ── Раздел B: настройки открыты, сумка поверх → 1-й ESC закрывает сумку, настройки остаются ──
	static void StageReverse2(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaSystemOptionSubsystem* SO = Sub<UAsdaSystemOptionSubsystem>(GI);
		if (!WM || !SO || !SO->IsOptionsShown() || !InvenShown(C))
		{
			C->SkipCase(TEXT("сумка поверх настроек: окна не открылись"));
		}
		else
		{
			WM->BringWindowToFront(kInven);   // сумка — верхняя (как щелчок по её телу)
			const int32 ZI = WM->GetWindowZOrder(kInven), ZO = WM->GetWindowZOrder(FName(TEXT("SYSTEM_Option_NEW")));
			C->Expect(ZI > ZO, FString::Printf(TEXT("сумка выше настроек до ESC (z %d > %d)"), ZI, ZO));
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bInvGone = !InvenShown(C), bOptStay = SO->IsOptionsShown();
			C->Expect(bEaten && bInvGone && bOptStay,
				FString::Printf(TEXT("сумка поверх настроек: 1-й ESC закрыл сумку=%d, настройки остались=%d"), (int32)bInvGone, (int32)bOptStay));
			const bool bEaten2 = WM->CloseTopmostOnEsc();
			C->Expect(bEaten2 && !SO->IsOptionsShown(), TEXT("сумка поверх настроек: 2-й ESC закрыл настройки"));
			C->Table.Add(FString::Printf(TEXT("сумка поверх настроек | 1-й ESC: сумка %s, настройки %s | 2-й ESC: настройки %s"),
				bInvGone ? TEXT("закрыта") : TEXT("ОСТАЛАСЬ"), bOptStay ? TEXT("на месте") : TEXT("ЗАКРЫТЫ"),
				SO->IsOptionsShown() ? TEXT("ОСТАЛИСЬ") : TEXT("закрыты")));
		}
		if (SO && SO->IsOptionsShown()) { SO->CloseOptions(false); }
		CloseInven(C);
		After(C, 0.3f, &StageLayer1);
	}

	// ── Раздел C: парикмахерская с панелью покупки — ESC снимает только панель ──
	static void StageLayer2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GIOf(C));
		if (!WM || !H || !H->IsSessionOpen() || !H->IsPurchasePanelShown())
		{
			C->SkipCase(TEXT("парикмахерская + панель 15168: офлайн окно/панель не открылись"));
		}
		else
		{
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bPanelGone = !H->IsPurchasePanelShown(), bWinStay = H->IsSessionOpen();
			C->Expect(bEaten && bPanelGone && bWinStay,
				FString::Printf(TEXT("парикмахерская: 1-й ESC закрыл панель=%d, окно осталось=%d"), (int32)bPanelGone, (int32)bWinStay));
			const bool bEaten2 = WM->CloseTopmostOnEsc();
			C->Expect(bEaten2 && !H->IsSessionOpen(), TEXT("парикмахерская: 2-й ESC закрыл окно 15000"));
			C->Table.Add(FString::Printf(TEXT("парикмахерская + панель 15168 | 1-й ESC: панель %s, окно %s | 2-й ESC: окно %s"),
				bPanelGone ? TEXT("закрыта") : TEXT("ОСТАЛАСЬ"), bWinStay ? TEXT("на месте") : TEXT("ЗАКРЫТО"),
				H->IsSessionOpen() ? TEXT("ОСТАЛОСЬ") : TEXT("закрыто")));
		}
		if (H && H->IsSessionOpen()) { H->CloseWindow(); }
		if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.HairShop.LegacyConfirmPanel")))
		{
			AsdaCVarSave::SetForTest(TEXT("asda.HairShop.LegacyConfirmPanel"), C->HairLegacyWas);   // ключ — как был до раздела
		}
		After(C, 0.3f, &StageGacha1);
	}
	static void StageLayer1(TSharedRef<FCtx> C)
	{
		UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GIOf(C));
		// Панель 15168 — путь оригинала (`asda.HairShop.LegacyConfirmPanel 1`); по умолчанию порт сразу открывает модалку 1559
		//   (решение владельца). Ключ на время раздела включается и возвращается в `StageLayer2`.
		if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.HairShop.LegacyConfirmPanel")))
		{
			C->HairLegacyWas = Cv->GetInt();
			AsdaCVarSave::SetForTest(TEXT("asda.HairShop.LegacyConfirmPanel"), 1);
		}
		// ★ 25.10.2026: снимок причёски до первой примерки (возврат — `StageFinish`).
		if (const UAsdaWorldSubsystem* WSh = Sub<UAsdaWorldSubsystem>(GIOf(C)); WSh && !C->bHairSaved)
		{
			C->bHairSaved = true;
			C->HairStyleWas = WSh->SelfParts.HairStyle;
			C->HairColorWas = WSh->SelfParts.HairColor;
		}
		if (H) { H->OpenWindow(); }
		After(C, 0.8f, &StageLayer1Buy, 8);
	}
	static void StageLayer1Buy(TSharedRef<FCtx> C)
	{
		// «Купить» первой карточки — штатный вход панели 15168 (`uiHandleMenuButtonCommand`).
		if (UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GIOf(C))) { if (H->IsSessionOpen()) { H->OnCardBuyClicked(0); } }
		After(C, 0.5f, &StageLayer2);
	}

	// ★ 07.11.2026: проверить окно-сообщение после ESC при незабранном призе и закрыть его кнопкой ОК (путь игрока).
	//   `bPending` — приз считался незабранным до нажатия ESC; `Where` — подпись случая в журнале.
	static void ExpectPrizeBoxAndDismiss(TSharedRef<FCtx> C, UAsdaWindowSubsystem* WM, bool bPending, const TCHAR* Where)
	{
		const FName BoxName(TEXT("ONEBUT_MSGBOX"));
		const bool bShown = WM->IsWindowVisible(BoxName);
		C->Expect(bShown == bPending, FString::Printf(TEXT("%s: окно «сначала забери приз» показано=%d (ждём %d)"), Where, (int32)bShown, (int32)bPending));
		if (!bShown) { return; }
		UUserWidget* Box = WM->FindWindow(BoxName);
		if (UButton* Ok = Box ? Cast<UButton>(Box->GetWidgetFromName(TEXT("ONEBUT_BUT00"))) : nullptr) { Ok->OnClicked.Broadcast(); }
		C->Expect(!WM->IsWindowVisible(BoxName), FString::Printf(TEXT("%s: кнопка ОК закрыла окно"), Where));
	}

	// ── Раздел D: сцена премиум-гачи ──
	static void StageGacha2(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaPremiumGachaSubsystem* G = Sub<UAsdaPremiumGachaSubsystem>(GIOf(C));
		if (!WM || !G || !G->IsSceneOpen())
		{
			C->SkipCase(TEXT("сцена гачи 68000: офлайн не открылась"));
		}
		else
		{
			const EAsdaGachaPhase Ph = G->GetPhase();
			const bool bPending = G->IsPrizePending();
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bOpen = G->IsSceneOpen();
			ExpectPrizeBoxAndDismiss(C, WM, bPending, TEXT("гача D2"));
			if (Ph == EAsdaGachaPhase::Idle)
			{
				C->Expect(bEaten && !bOpen, FString::Printf(TEXT("гача в покое: ESC закрыл сцену (съеден=%d, открыта=%d)"), (int32)bEaten, (int32)bOpen));
				C->Table.Add(FString::Printf(TEXT("сцена гачи в покое | 1-й ESC: сцена %s"), bOpen ? TEXT("ОСТАЛАСЬ") : TEXT("закрыта")));
			}
			else
			{
				C->Expect(bEaten && bOpen, FString::Printf(TEXT("гача не в покое (фаза %d): ESC съеден, сцена открыта=%d"), (int32)Ph, (int32)bOpen));
				C->Table.Add(FString::Printf(TEXT("сцена гачи не в покое (фаза %d) | ESC съеден, сцена %s"), (int32)Ph, bOpen ? TEXT("открыта") : TEXT("ЗАКРЫТА")));
			}
		}
		if (G && G->IsSceneOpen()) { G->CloseScene(); }
		After(C, 0.5f, &StageRoll1);
	}
	static void StageGacha1(TSharedRef<FCtx> C)
	{
		if (UAsdaPremiumGachaSubsystem* G = Sub<UAsdaPremiumGachaSubsystem>(GIOf(C))) { G->OpenScene(); }
		After(C, 1.5f, &StageGacha2, 20);
	}


	// ── ★ Раздел D3: гача во время прокрутки — ESC съеден, сцена открыта (ui_core.c:7664-7670) ──
	static void StageRoll3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaPremiumGachaSubsystem* G = Sub<UAsdaPremiumGachaSubsystem>(GIOf(C));
		if (!WM || !G || !G->IsSceneOpen())
		{
			C->SkipCase(TEXT("гача во время прокрутки: офлайн сцена не открылась"));
		}
		else
		{
			const EAsdaGachaPhase Ph = G->GetPhase();
			C->Expect(Ph != EAsdaGachaPhase::Idle && Ph != EAsdaGachaPhase::Closed,
				FString::Printf(TEXT("гача: ответ 6578 (статус 1) увёл автомат из покоя (фаза %d)"), (int32)Ph));
			const bool bPending = G->IsPrizePending();
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bOpen = G->IsSceneOpen();
			ExpectPrizeBoxAndDismiss(C, WM, bPending, TEXT("гача D3"));
			C->Expect(bEaten && bOpen, FString::Printf(TEXT("гача во время прокрутки (фаза %d): ESC съеден=%d, сцена открыта=%d"),
				(int32)Ph, (int32)bEaten, (int32)bOpen));
			C->Table.Add(FString::Printf(TEXT("сцена гачи во время прокрутки (фаза %d) | 1-й ESC: съеден %s, сцена %s"), (int32)Ph,
				bEaten ? TEXT("да") : TEXT("НЕТ"), bOpen ? TEXT("открыта") : TEXT("ЗАКРЫТА")));
		}
		if (G && G->IsSceneOpen()) { G->CloseScene(); }
		After(C, 0.5f, &StageHairModal1);
	}
	static void StageRoll2(TSharedRef<FCtx> C)
	{
		// Ответ сервера 6578 со статусом 1 тем же путём, что сеть (`OnRollResult`): автомат уходит в клип прокрутки 2.
		UAsdaPremiumGachaSubsystem* G = Sub<UAsdaPremiumGachaSubsystem>(GIOf(C));
		if (G && G->IsSceneOpen()) { G->SimulateRollResult(1, 0); }
		After(C, 0.2f, &StageRoll3, 2);
	}
	static void StageRoll1(TSharedRef<FCtx> C)
	{
		if (UAsdaPremiumGachaSubsystem* G = Sub<UAsdaPremiumGachaSubsystem>(GIOf(C))) { G->OpenScene(); }
		After(C, 1.5f, &StageRoll2, 20);
	}

	// ── ★ Раздел C2: парикмахерская по умолчанию — «Купить» открывает общую модалку 1559 ──
	//   Эталон: `upgradeUiFillConfirmPanel@0xD7BD90` показывает 1559 модально (`helper_setupFields(…, 1)`), признака ESC
	//   у неё нет ⇒ `uiContainerHandleKey` смотрит только модальный контрол: ничего не закрыто, нажатие не съедено,
	//   ESC-меню заперто флагом `CWindowMgr+0x200`. После «Отмены» ESC закрывает 15000.
	static void StageHairModal3(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GI);
		UAsdaPurchaseBoxSubsystem* Box = Sub<UAsdaPurchaseBoxSubsystem>(GI);
		if (!WM || !H || !Box || !H->IsSessionOpen() || !Box->IsOpen())
		{
			C->SkipCase(TEXT("парикмахерская + модалка 1559: офлайн окно/модалка не открылись"));
		}
		else
		{
			// Настоящий путь ESC (закон `diagnostic-must-use-real-path`): сперва проход окон, затем ГЕЙТ ESC-меню 69400 самой
			//   подсистемы настроек (`IsEscMenuGateBlocked`), а не признак окна — снятие части 3 гейта уронит тест (МЕЛК-1 ревизии).
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bBoxStay = Box->IsOpen(), bWinStay = H->IsSessionOpen();
			FString Why;
			UAsdaSystemOptionSubsystem* SysOpt = Sub<UAsdaSystemOptionSubsystem>(GI);
			const bool bGate = SysOpt && SysOpt->IsEscMenuGateBlocked(Why);
			C->Expect(!bEaten && bBoxStay && bWinStay && bGate,
				FString::Printf(TEXT("парикмахерская + модалка 1559: 1-й ESC съеден=%d (ждём 0), модалка осталась=%d, окно осталось=%d, ESC-меню заперто=%d (%s)"),
					(int32)bEaten, (int32)bBoxStay, (int32)bWinStay, (int32)bGate, *Why));
			Box->OnCancelClicked();   // «Отмена» 1562 — штатный выход из модалки
			FString Why2;
			const bool bGate2 = SysOpt && SysOpt->IsEscMenuGateBlocked(Why2);   // контрольная пара: гейт отперт по видимости
			const bool bEaten2 = WM->CloseTopmostOnEsc();
			const bool bWinGone = !H->IsSessionOpen();
			C->Expect(!Box->IsOpen() && !bGate2 && bEaten2 && bWinGone,
				FString::Printf(TEXT("парикмахерская: после «Отмены» 1559 ESC-меню отперто=%d, 2-й ESC закрыл окно 15000 (съеден=%d, окно закрыто=%d)"),
					(int32)!bGate2, (int32)bEaten2, (int32)bWinGone));
			C->Table.Add(FString::Printf(TEXT("парикмахерская + модалка 1559 | 1-й ESC: съеден %s, модалка %s, окно %s, меню %s | «Отмена» + 2-й ESC: окно %s"),
				bEaten ? TEXT("ДА") : TEXT("нет"), bBoxStay ? TEXT("на месте") : TEXT("ЗАКРЫТА"), bWinStay ? TEXT("на месте") : TEXT("ЗАКРЫТО"),
				bGate ? TEXT("заперто") : TEXT("НЕ ЗАПЕРТО"), bWinGone ? TEXT("закрыто") : TEXT("ОСТАЛОСЬ")));
		}
		if (Box && Box->IsOpen()) { Box->Close(); }
		if (H && H->IsSessionOpen()) { H->CloseWindow(); }
		After(C, 0.3f, &StageBoost1);
	}
	static void StageHairModal2(TSharedRef<FCtx> C)
	{
		// «Купить» первой карточки при `LegacyConfirmPanel 0` (умолчание порта, решение владельца) — сразу модалка 1559.
		if (UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GIOf(C))) { if (H->IsSessionOpen()) { H->OnCardBuyClicked(0); } }
		After(C, 0.6f, &StageHairModal3, 6);
	}
	static void StageHairModal1(TSharedRef<FCtx> C)
	{
		if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.HairShop.LegacyConfirmPanel")))
		{
			C->HairLegacyWas = Cv->GetInt();
			AsdaCVarSave::SetForTest(TEXT("asda.HairShop.LegacyConfirmPanel"), 0);   // путь по умолчанию; значение возвращается в `StageBoost1`
		}
		if (UAsdaHairShopSubsystem* H = Sub<UAsdaHairShopSubsystem>(GIOf(C))) { H->OpenWindow(); }
		After(C, 0.8f, &StageHairModal2, 8);
	}

	// ── ★ Раздел E: магазин бустеров — «Вид» 15600, затем панель 15515, затем окно (одно нажатие — один слой) ──
	static void StageBoost3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaBoosterShopSubsystem* S = Sub<UAsdaBoosterShopSubsystem>(GIOf(C));
		const FName PrevId(TEXT("BOOSTER_PREV_WIN"));
		if (!WM || !S || !S->IsOpen() || !S->IsPurPanelShown() || !WM->IsWindowVisible(PrevId))
		{
			C->SkipCase(FString::Printf(TEXT("бустеры: офлайн не открылись слои (магазин %d, панель %d, «Вид» %d)"),
				(int32)(S && S->IsOpen()), (int32)(S && S->IsPurPanelShown()), (int32)(WM && WM->IsWindowVisible(PrevId))));
		}
		else
		{
			const bool bE1 = WM->CloseTopmostOnEsc();
			const bool bPrev1 = WM->IsWindowVisible(PrevId), bPan1 = S->IsPurPanelShown(), bShop1 = S->IsOpen();
			C->Expect(bE1 && !bPrev1 && bPan1 && bShop1,
				FString::Printf(TEXT("бустеры: 1-й ESC закрыл «Вид»=%d, панель осталась=%d, магазин остался=%d"), (int32)!bPrev1, (int32)bPan1, (int32)bShop1));
			const bool bE2 = WM->CloseTopmostOnEsc();
			const bool bPan2 = S->IsPurPanelShown(), bShop2 = S->IsOpen();
			C->Expect(bE2 && !bPan2 && bShop2,
				FString::Printf(TEXT("бустеры: 2-й ESC закрыл панель 15515=%d, магазин остался=%d"), (int32)!bPan2, (int32)bShop2));
			const bool bE3 = WM->CloseTopmostOnEsc();
			const bool bShop3 = S->IsOpen();
			C->Expect(bE3 && !bShop3, FString::Printf(TEXT("бустеры: 3-й ESC закрыл магазин 15300=%d"), (int32)!bShop3));
			C->Table.Add(FString::Printf(TEXT("бустеры: «Вид» + панель | 1-й ESC: «Вид» %s, панель %s | 2-й: панель %s, магазин %s | 3-й: магазин %s"),
				bPrev1 ? TEXT("ОСТАЛСЯ") : TEXT("закрыт"), bPan1 ? TEXT("на месте") : TEXT("ЗАКРЫТА"), bPan2 ? TEXT("ОСТАЛАСЬ") : TEXT("закрыта"),
				bShop2 ? TEXT("на месте") : TEXT("ЗАКРЫТ"), bShop3 ? TEXT("ОСТАЛСЯ") : TEXT("закрыт")));
		}
		if (S && S->IsOpen()) { S->CloseWindow(); }
		After(C, 0.3f, &StageFind1);
	}
	static void StageBoost2(TSharedRef<FCtx> C)
	{
		// Штатные входы мыши: «Купить» ячейки 0 (панель 15515, режим 1), затем щелчок по иконке (окно «Вид» 15600).
		if (UAsdaBoosterShopSubsystem* S = Sub<UAsdaBoosterShopSubsystem>(GIOf(C)))
		{
			if (S->IsOpen()) { S->OnCellBuyClicked(0); S->OnCellIconClicked(0); }
		}
		After(C, 0.8f, &StageBoost3, 8);
	}
	static void StageBoost1(TSharedRef<FCtx> C)
	{
		if (IConsoleVariable* Cv = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.HairShop.LegacyConfirmPanel")))
		{
			AsdaCVarSave::SetForTest(TEXT("asda.HairShop.LegacyConfirmPanel"), C->HairLegacyWas);   // ключ раздела C2 — как был
		}
		if (UAsdaBoosterShopSubsystem* S = Sub<UAsdaBoosterShopSubsystem>(GIOf(C))) { S->OpenWindow(); }
		After(C, 0.8f, &StageBoost2, 8);
	}

	// ── ★ Раздел F: поиск соулмейта 8200 — ESC закрывает окно и снимает фокус с поля 8302 (`cFindSoulmateWinHide`) ──
	static void StageFind3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C));
		const FName IntroId(TEXT("My_Introduction_Win"));
		if (!WM || !So || !So->IsSearchWindowShown() || !WM->IsWindowVisible(IntroId) || !So->IsIntroEditFocused())
		{
			C->SkipCase(FString::Printf(TEXT("поиск 8200 + поле 8302: офлайн не открылись (8200 %d, 8300 %d, фокус в 8302 %d)"),
				(int32)(So && So->IsSearchWindowShown()), (int32)(WM && WM->IsWindowVisible(IntroId)), (int32)(So && So->IsIntroEditFocused())));
		}
		else
		{
			// Щелчок по телу 8200 поднимает его только внутри обычной полосы: окно 8300 стоит в полосе AlwaysTop (`.JUI` AlwaysTop="1", z 8600), для игрока оно выше.
			WM->BringWindowToFront(FName(TEXT("Find_Soulmate_Win")));
			C->Expect(So->IsIntroEditFocused(), TEXT("поиск 8200: после подъёма 8200 фокус всё ещё в поле 8302"));
			C->Expect(WM->GetWindowZOrder(IntroId) > WM->GetWindowZOrder(FName(TEXT("Find_Soulmate_Win"))), FString::Printf(TEXT("поиск 8200: верхнее окно 8300 (z %d) выше 8200 (z %d)"),
				WM->GetWindowZOrder(IntroId), WM->GetWindowZOrder(FName(TEXT("Find_Soulmate_Win")))));
			// Ключ 1: сначала снимается фокус поля, текст цел; затем закрывается то, что видно сверху: 8300, потом 8200.
			const bool bE0 = WM->CloseTopmostOnEsc();
			const bool bFocusOff = !So->IsIntroEditFocused();
			C->Expect(bE0 && bFocusOff && So->IsSearchWindowShown() && WM->IsWindowVisible(IntroId),
				FString::Printf(TEXT("поиск 8200, фокус в 8302: ESC снял фокус=%d, оба окна на месте"), (int32)bFocusOff));
			const bool bE1 = WM->CloseTopmostOnEsc();
			const bool bIntroGone = !WM->IsWindowVisible(IntroId), bSearchStay = So->IsSearchWindowShown();
			C->Expect(bE1 && bIntroGone && bSearchStay, FString::Printf(TEXT("поиск 8200 без фокуса: 1-й ESC закрыл верхнее 8300=%d, 8200 осталось=%d"), (int32)bIntroGone, (int32)bSearchStay));
			const bool bE2 = WM->CloseTopmostOnEsc();
			const bool bSearchGone = !So->IsSearchWindowShown();
			C->Expect(bE2 && bSearchGone, FString::Printf(TEXT("поиск 8200 без фокуса: 2-й ESC закрыл 8200=%d"), (int32)bSearchGone));
			C->Table.Add(FString::Printf(TEXT("поиск 8200 под «о себе» 8300 (фокус в 8302) | ESC: фокус %s; ESC: 8300 %s; ESC: 8200 %s"),
				bFocusOff ? TEXT("снят") : TEXT("ОСТАЛСЯ"), bIntroGone ? TEXT("закрыто") : TEXT("ОСТАЛОСЬ"), bSearchGone ? TEXT("закрыто") : TEXT("ОСТАЛОСЬ")));
		}
		if (So) { So->CloseSearchWindow(); So->CloseIntroWindow(); }
		After(C, 0.3f, &StageFindOld1);
	}
	// Ключ 0: поведение оригинала. Проход ищет только окна с признаком ESC: у 8300 его нет, закрывается 8200, поле теряет фокус, 8300 остаётся (`cFindSoulmateWinHide`).
	static void StageFindOld3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C));
		const FName IntroId(TEXT("My_Introduction_Win"));
		if (!WM || !So || !So->IsSearchWindowShown() || !WM->IsWindowVisible(IntroId) || !So->IsIntroEditFocused())
		{
			C->SkipCase(FString::Printf(TEXT("ключ 0, поиск 8200 + поле 8302: офлайн не открылись (8200 %d, 8300 %d, фокус в 8302 %d)"),
				(int32)(So && So->IsSearchWindowShown()), (int32)(WM && WM->IsWindowVisible(IntroId)), (int32)(So && So->IsIntroEditFocused())));
		}
		else
		{
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bGone = !So->IsSearchWindowShown(), bFocusOff = !So->IsIntroEditFocused(), bIntroStay = WM->IsWindowVisible(IntroId);
			C->Expect(bEaten && bGone && bFocusOff && bIntroStay,
				FString::Printf(TEXT("ключ 0, поиск 8200: 1-й ESC закрыл окно=%d, фокус снят с поля 8302=%d, окно 8300 осталось=%d"),
					(int32)bGone, (int32)bFocusOff, (int32)bIntroStay));
		}
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.EscExtraWindows"))) { K->Set(1, ECVF_SetByConsole); }
		if (So) { So->CloseSearchWindow(); So->CloseIntroWindow(); }
		After(C, 0.3f, &StageComm1);
	}
	static void StageFindOld2(TSharedRef<FCtx> C)
	{
		if (UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C))) { So->OpenIntroWindow(); }
		After(C, 0.6f, &StageFindOld3, 6);
	}
	static void StageFindOld1(TSharedRef<FCtx> C)
	{
		if (IConsoleVariable* K = IConsoleManager::Get().FindConsoleVariable(TEXT("asda.UI.EscExtraWindows"))) { K->Set(0, ECVF_SetByConsole); }
		if (UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C))) { So->OpenSearchWindow(); }
		After(C, 0.6f, &StageFindOld2, 6);
	}
	static void StageFind2(TSharedRef<FCtx> C)
	{
		// Путь игрока: в окне поиска 8200 кнопка 8270 открывает «о себе» 8300 — фокус в поле 8302 (`findSoulmateWinOpen`).
		if (UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C))) { So->OpenIntroWindow(); }
		After(C, 0.6f, &StageFind3, 6);
	}
	static void StageFind1(TSharedRef<FCtx> C)
	{
		if (UAsdaSoulmateSubsystem* So = Sub<UAsdaSoulmateSubsystem>(GIOf(C))) { So->OpenSearchWindow(); }
		After(C, 0.6f, &StageFind2, 6);
	}

	// ── ★ Раздел G: сообщество 6400 — ESC закрывает окно и скрывает под-окно 6640 (`communityUI_Close`) ──
	static void StageComm3(TSharedRef<FCtx> C)
	{
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaCommunitySubsystem* Co = Sub<UAsdaCommunitySubsystem>(GIOf(C));
		if (!WM || !Co || !Co->IsWindowOpen() || !Co->IsOldSoulmateWinShown())
		{
			C->SkipCase(FString::Printf(TEXT("сообщество 6400 + 6640: офлайн не открылись (6400 %d, 6640 %d)"),
				(int32)(Co && Co->IsWindowOpen()), (int32)(Co && Co->IsOldSoulmateWinShown())));
		}
		else
		{
			WM->BringWindowToFront(FName(TEXT("COMMUNITY_WIN")));
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bGone = !Co->IsWindowOpen(), bOldOff = !Co->IsOldSoulmateWinShown();
			C->Expect(bEaten && bGone && bOldOff,
				FString::Printf(TEXT("сообщество: 1-й ESC закрыл 6400=%d и скрыл 6640=%d"), (int32)bGone, (int32)bOldOff));
			C->Table.Add(FString::Printf(TEXT("сообщество 6400 с под-окном 6640 | 1-й ESC: 6400 %s, 6640 %s"),
				bGone ? TEXT("закрыто") : TEXT("ОСТАЛОСЬ"), bOldOff ? TEXT("скрыто") : TEXT("ОСТАЛОСЬ")));
		}
		if (Co && Co->IsWindowOpen()) { Co->CloseWindow(); }
		After(C, 0.3f, &StageCtrl1);
	}
	static void StageComm2(TSharedRef<FCtx> C)
	{
		if (UAsdaCommunitySubsystem* Co = Sub<UAsdaCommunitySubsystem>(GIOf(C))) { if (Co->IsWindowOpen()) { Co->DebugShowOldSoulmateWin(); } }
		After(C, 0.3f, &StageComm3, 4);
	}
	static void StageComm1(TSharedRef<FCtx> C)
	{
		if (UAsdaCommunitySubsystem* Co = Sub<UAsdaCommunitySubsystem>(GIOf(C))) { Co->OpenOnTab(0); /* ★ R5-fix6-c (13.11.2026): вкладку 0 ставит сам тест штатным путём (хоткей 35), а не берёт оставшуюся от соседа пачки */ }
		After(C, 0.8f, &StageComm2, 8);
	}

	// ── Контроль K: роспись 69500 снята → ESC закрывает сумку под настройками (дефект до 10.10) ──
	static void StageCtrl2(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		UAsdaWindowSubsystem* WM = WMOf(C);
		UAsdaSystemOptionSubsystem* SO = Sub<UAsdaSystemOptionSubsystem>(GI);
		const FName OptId(TEXT("SYSTEM_Option_NEW"));
		if (!WM || !SO || !SO->IsOptionsShown() || !InvenShown(C))
		{
			C->SkipCase(TEXT("контроль: окна не открылись"));
		}
		else
		{
			WM->BringWindowToFront(OptId);
			WM->UnregisterEscCloser(OptId);
			const bool bEaten = WM->CloseTopmostOnEsc();
			const bool bInvGone = !InvenShown(C), bOptStay = SO->IsOptionsShown();
			C->Expect(bEaten && bInvGone && bOptStay,
				FString::Printf(TEXT("КОНТРОЛЬ без росписи 69500: ESC закрыл сумку ПОД настройками=%d, настройки остались=%d (дефект воспроизведён)"),
					(int32)bInvGone, (int32)bOptStay));
			WM->RegisterEscCloser(OptId, FSimpleDelegate::CreateUObject(SO, &UAsdaSystemOptionSubsystem::EscCloseOptions));
			C->Table.Add(FString::Printf(TEXT("КОНТРОЛЬ: роспись 69500 снята | 1-й ESC: сумка %s, настройки %s"),
				bInvGone ? TEXT("закрыта (как до 10.10)") : TEXT("на месте"), bOptStay ? TEXT("остались") : TEXT("закрыты")));
		}
		if (SO && SO->IsOptionsShown()) { SO->CloseOptions(false); }
		CloseInven(C);
		After(C, 0.3f, &StageFinish);
	}
	static void StageCtrl1(TSharedRef<FCtx> C)
	{
		OpenInven(C);
		After(C, 0.5f, &StageCtrl1Opt, 6);
	}
	static void StageCtrl1Opt(TSharedRef<FCtx> C)
	{
		if (UAsdaSystemOptionSubsystem* SO = Sub<UAsdaSystemOptionSubsystem>(GIOf(C))) { SO->ShowOptions(); }
		After(C, 0.8f, &StageCtrl2, 8);
	}

	// ── Раздел B, шаг 1: настройки, затем сумка ──
	static void StageReverse1(TSharedRef<FCtx> C)
	{
		if (UAsdaSystemOptionSubsystem* SO = Sub<UAsdaSystemOptionSubsystem>(GIOf(C))) { SO->ShowOptions(); }
		After(C, 0.5f, &StageReverse1Inv, 6);
	}
	static void StageReverse1Inv(TSharedRef<FCtx> C)
	{
		OpenInven(C);
		After(C, 0.8f, &StageReverse2, 8);
	}

	// ── Итог ──
	static void StageFinish(TSharedRef<FCtx> C)
	{
		UGameInstance* GI = GIOf(C);
		if (C->CapGen && GI) { if (UAsdaPetSubsystem* Pet = GI->GetSubsystem<UAsdaPetSubsystem>()) { Pet->EndSendCapture(); } }
		UE_LOG(LogAsdaEscTopTest, Log, TEXT("ESC-ВЕРХ ТАБЛИЦА (%d строк):"), C->Table.Num());
		for (const FString& L : C->Table) { UE_LOG(LogAsdaEscTopTest, Log, TEXT("[ESC-ТАБЛ] %s"), *L); }
		// ★ ИСПРАВЛЕНИЕ 11.10.2026 (В0, N1(д)/U8): ключ HairShop.LegacyConfirmPanel — к исходному значению И приоритету SetBy (разделы C/C2 возвращают
		//   прежнее значение, но с SetByConsole).
		AsdaCVarSave::RestoreAll(TEXT("asda.UI.SelfTestEscOnTop"));
		// ★ 25.10.2026 (изоляция в пакете): причёска своего — как до разделов C/C2 (примерка карточки 0 при офлайн-причёске −1 не
		//   откатывается самой парикмахерской). Прямо в снимок и пересборка модели — `ApplySelfHair` стиль −1 не принимает.
		if (UAsdaWorldSubsystem* WSh = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr; WSh && C->bHairSaved
			&& (WSh->SelfParts.HairStyle != C->HairStyleWas || WSh->SelfParts.HairColor != C->HairColorWas))
		{
			UE_LOG(LogAsdaEscTopTest, Log, TEXT("ESC-ВЕРХ уборка: причёска своего %d/%d → %d/%d (как до прогона)"),
				WSh->SelfParts.HairStyle, WSh->SelfParts.HairColor, C->HairStyleWas, C->HairColorWas);
			WSh->SelfParts.HairStyle = C->HairStyleWas;
			WSh->SelfParts.HairColor = C->HairColorWas;
			if (AAsdaPlayerCharacter* Me = Cast<AAsdaPlayerCharacter>(WSh->LocalPlayerActor.Get())) { Me->InitAppearance(WSh->SelfSex, WSh->VisualSelfParts()); }
		}
		// ★ В3 S1 (N1(г)): AlwaysTop кнопки дня гачи — как до прогона.
		if (UAsdaWindowSubsystem* WM9 = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr; WM9 && C->DayTopWas >= 0)
		{
			WM9->SetWindowAlwaysTop(FName(TEXT("Premium_PickBAR_daybtn_X_B")), C->DayTopWas != 0);
		}
		UE_LOG(LogAsdaEscTopTest, Log, TEXT("ESC-ВЕРХ ИТОГ: ok=%d fail=%d пропуск=%d, перехвачено отправок %d"), C->Ok, C->Fail, C->Skip, C->Sent);
		if (C->bQuit && GEngine && C->W.Get()) { GEngine->Exec(C->W.Get(), TEXT("quit")); }
	}

	// Пары раздела A.
	static void BuildCases(TSharedRef<FCtx> C)
	{
		C->Cases.Add({ TEXT("настройки 69500"), FName(TEXT("SYSTEM_Option_NEW")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaSystemOptionSubsystem>(GI)) { S->ShowOptions(); } },
			[](UGameInstance* GI) { auto* S = Sub<UAsdaSystemOptionSubsystem>(GI); return S && S->IsOptionsShown(); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaSystemOptionSubsystem>(GI)) { if (S->IsOptionsShown()) { S->CloseOptions(false); } } } });
		C->Cases.Add({ TEXT("ESC-меню 69400"), FName(TEXT("SYSTEM_MenuWIN_NEW")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaSystemOptionSubsystem>(GI)) { S->ShowMenu(); } },
			[](UGameInstance* GI) { auto* S = Sub<UAsdaSystemOptionSubsystem>(GI); return S && S->IsMenuShown(); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaSystemOptionSubsystem>(GI)) { if (S->IsMenuShown()) { S->HideMenu(); } } } });
		C->Cases.Add({ TEXT("магазин бустеров 15300"), FName(TEXT("BOOSTER_SHOP_WIN")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaBoosterShopSubsystem>(GI)) { S->OpenWindow(); } },
			[](UGameInstance* GI) { auto* S = Sub<UAsdaBoosterShopSubsystem>(GI); return S && S->IsOpen(); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaBoosterShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Cases.Add({ TEXT("парикмахерская 15000"), FName(TEXT("HAIRSHOP_WIN")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaHairShopSubsystem>(GI)) { S->OpenWindow(); } },
			[](UGameInstance* GI) { auto* S = Sub<UAsdaHairShopSubsystem>(GI); return S && S->IsSessionOpen(); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaHairShopSubsystem>(GI)) { S->CloseWindow(); } } });
		C->Cases.Add({ TEXT("меню вкладок чата 2670"), FName(TEXT("CHAT_NEW")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaChatWindowSubsystem>(GI)) { S->DebugShowTabMenu(); } },
			[](UGameInstance* GI) { auto* S = Sub<UAsdaChatWindowSubsystem>(GI); return S && S->IsTabMenuShown(); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaChatWindowSubsystem>(GI)) { if (S->IsTabMenuShown()) { if (UAsdaWindowSubsystem* WM = Sub<UAsdaWindowSubsystem>(GI)) { WM->CloseTopmostOnEsc(); } } } } });
		C->Cases.Add({ TEXT("КОНТРОЛЬ ремонт 57300"), FName(TEXT("INVEN_Repair_Win")),
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaRepairSubsystem>(GI)) { S->OpenWindow(); } },
			[](UGameInstance* GI) { auto* WM = Sub<UAsdaWindowSubsystem>(GI); return WM && WM->IsWindowVisible(FName(TEXT("INVEN_Repair_Win"))); },
			[](UGameInstance* GI) { if (auto* S = Sub<UAsdaRepairSubsystem>(GI)) { S->CloseWindow(); } } });
	}

	static void Run(TSharedRef<FCtx> C)
	{
		UWorld* W = C->W.Get();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		if (!WMOf(C)) { UE_LOG(LogAsdaEscTopTest, Error, TEXT("ESC-ВЕРХ: нет подсистемы окон")); return; }
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
		BuildCases(C);
		After(C, 2.5f, &StagePairOpen, 30);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaUiSelfTestEscOnTop(
	TEXT("asda.UI.SelfTestEscOnTop"),
	TEXT("asda.UI.SelfTestEscOnTop [delay=N] [quit] — ОФЛАЙН-самотест прохода ESC (эталон uiContainerHandleKey@0x12AF390): ")
	TEXT("окно X поверх сумки → ESC закрывает X (настройки 69500, ESC-меню 69400, бустеры 15300, парикмахерская 15000, меню чата 2670, ")
	TEXT("контроль 57300), обратный порядок, слой панели 15168, сцена гачи 68000 в покое и во время прокрутки, модалка 1559 ")
	TEXT("парикмахерской, бустеры «Вид» 15600 → панель 15515 → окно, поиск 8200 (фокус 8302), сообщество 6400 (6640), ")
	TEXT("контроль без росписи 69500"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		AsdaCVarSave::ArmWorldGuard(W, TEXT("asda.UI.SelfTestEscOnTop"));   // ★ С1 (ревизия wave0_REVIEW.md, 11.10.2026): мир ушёл до итога — ключи вернёт сторож
		const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(A);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
		if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.UI.SelfTestEscOnTop"), KitArgs)) { return; }
		if (!W) { return; }
		float Delay = 0.f;   // задержка запуска, с
		bool bQuit = false;  // выйти после прогона
		for (const FString& Arg : A)
		{
			if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase)) { bQuit = true; }
		}
		Delay = AsdaSelfTestKit::DelayArgOr(A, Delay);   // ★ R5-fix2-c (13.11.2026): `delay=N` — общая точка (была ветка цикла аргументов)
		TWeakObjectPtr<UWorld> WW = W;
		auto Go = [WW, bQuit]()
		{
			if (!WW.Get()) { return; }
			if (AsdaSelfTestKit::RefuseDeferred(WW.Get(), TEXT("asda.UI.SelfTestEscOnTop"), bQuit)) { return; }   // Р-2: офлайн повторно — на отложенном старте
			TSharedRef<AsdaEscTopLocal::FCtx> C = MakeShared<AsdaEscTopLocal::FCtx>();
			C->W = WW; C->bQuit = bQuit;
			// ★ R5-fix-b 13.11.2026 (третий заход): окно обмена 2008, оставленное прежним самотестом пачки, молча запрещает открыть сумку
			//   (`uiOpenInventoryWindow`, ui_core.c:46618) — все случаи «поверх сумки» ушли бы в пропуск. Остался признак —
			//   штатный сброс модели обмена (тот же, что у ухода из мира) до первого открытия сумки.
			if (UGameInstance* GI0 = WW->GetGameInstance())
			{
				if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI0))
				{
					if (UAsdaExchangeP2PSubsystem* X = GI0->GetSubsystem<UAsdaExchangeP2PSubsystem>()) { X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
					UE_LOG(LogAsdaEscTopTest, Log, TEXT("ESC-ПОВЕРХ инфо: осталось окно обмена 2008 от прежнего самотеста — сброшено до старта"));
				}
			}
			// ★ 11.10.2026 (В3 S1, N1(г)): прокрут гачи (раздел D) ставит кнопке дня AlwaysTop (premiumGacha_BeginRoll,
			//   world_map.c:4724) — флаг живёт до конца процесса. Запомнить, чтобы вернуть в конце (иначе в пакете
			//   `asda.UI.SelfTestRaiseClick` F9 «вне прокрута — без AlwaysTop» падал).
			if (const UAsdaWindowSubsystem* WM0 = WW->GetGameInstance() ? WW->GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				C->DayTopWas = WM0->IsWindowAlwaysTop(FName(TEXT("Premium_PickBAR_daybtn_X_B"))) ? 1 : 0;
			}
			AsdaEscTopLocal::Run(C);
		};
		if (Delay <= 0.f) { Go(); return; }
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda(Go), Delay, false);
	}));

#endif // !UE_BUILD_SHIPPING
