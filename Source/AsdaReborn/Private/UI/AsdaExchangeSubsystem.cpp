// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна АУКЦИОНА `EXCHANGE_WIN` (JUI 13000), эталонный класс `CItemMediation`.
//   Весь разбор, адреса и раскладки пакетов — в шапке `AsdaExchangeSubsystem.h` и в отчёте
//   `Reports/npc_exchange_win_13000.md`.
#include "UI/AsdaExchangeSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт режимов и отложенное золото 9913

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "AsdaPacketWriter.h"   // ★ U6 (В1 дедупа №2): записи 9901/9905/9909 через общий писатель
#include "Npc/AsdaNpcDialogSubsystem.h"    // GetCurrentNpcId — NPC-аукционщик в шапке 99xx (+10)
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaJuiComboBox.h"
// имя предмета и подстановка аргументов нотиса   ★ R3: комментарий снятого включения "Text/AsdaTextSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemTooltipSubsystem.h"   // общая точка подсказки предмета
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // ★ общая точка AsdaCommunityUi::ShownVisibilityFor
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"   // ★ пачка 20, жалоба 10: склад 4000 открыт — рынок не открывается
#include "World/AsdaWorldSubsystem.h"      // GetSelfCharName — имя игрока в записи 9901

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "Test/AsdaSelfTestKit.h"   // ★ 11.10.2026 (В3 S1): общий каркас самотестов (аргументы, отказ, перехват, итог, выход)
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: общий кодек провода (гибрид RU+EN+AR)
#include "AsdaExchangeSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaExchangeSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaExchange);   // ★ R2: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// ★ Откат исправления дефекта D10 (см. `HandleListingPacket`): 1 — эталонное поведение, окно
//   «мои лоты» открывается даже пустым и даже при уровне < 24; 0 (по умолчанию) — не открывается.
ASDA_DEV_CVAR(int32, CVarExchangeLegacyLevelGate,
	TEXT("asda.Exchange.LegacyLevelGate"), 0,
	TEXT("Аукцион: 1 — открывать пустое окно «мои лоты» при уровне < 24, как в оригинале (дефект D10)."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] Жалоба 10: пока открыт рынок, склад/личная лавка/обмен недоступны (заявки обмена — авто-отказ «занят»
//   штатным 5284/3), и рынок не открывается поверх склада или лавки. В оригинале взаимного гейта нет.
static ASDA_DEV_CVAR(int32, CVarExchangeExclusiveWindows,
	TEXT("asda.Exchange.ExclusiveWindows"), 1,
	TEXT("Аукцион: 1 — пока открыт рынок, склад, личная лавка и обмен 2008 недоступны (и наоборот); 0 — как в оригинале."),
	ECVF_Default);

bool UAsdaExchangeSubsystem::ExclusiveWindowsOn()
{
	return CVarExchangeExclusiveWindows.GetValueOnGameThread() != 0;
}

bool UAsdaExchangeSubsystem::IsMarketWindowOpen(const UObject* Ctx)
{
	if (!ExclusiveWindowsOn() || !Ctx) { return false; }
	const UWorld* W = Ctx->GetWorld();
	const UGameInstance* GI = W ? W->GetGameInstance() : Cast<UGameInstance>(Ctx);
	if (!GI) { if (const UGameInstanceSubsystem* S = Cast<UGameInstanceSubsystem>(Ctx)) { GI = S->GetGameInstance(); } }
	const UAsdaExchangeSubsystem* Ex = GI ? GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr;
	return Ex && Ex->bOpen && Ex->Mode != EAsdaExchangeMode::None;
}

namespace {
	const TCHAR* kWindowWbp =
		TEXT("/Game/AsdaAssets/UI/Player_UI/Trade/WBP_EXCHANGE_WIN.WBP_EXCHANGE_WIN_C");

// ═══════════════════════ живая диагностика (консоль) ══════════════════════════════════════════
	UAsdaExchangeSubsystem* ExchangeFromWorld(UWorld* W)
	{
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr;
	}
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExchangeOpenCmd(
	TEXT("asda.Exchange.Open"),
	TEXT("Аукцион (13000): открыть режим 1..5 (1 инвентарь, 2 сумка, 3 мои лоты, 4/5 поиск)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaExchangeSubsystem* Ex = ExchangeFromWorld(W);
			if (!Ex) { UE_LOG(LogAsdaExchange, Warning, TEXT("аукцион: подсистема не найдена")); return; }
			const int32 M = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 5;
			if (M == 0)   // ★ 0 = закрыть (для живой проверки: гейт не даёт сменить режим на ходу)
			{
				Ex->CloseAll();
				UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: консоль закрыла окно"));
				return;
			}
			UE_LOG(LogAsdaExchange, Log, TEXT("аукцион: консоль просит режим %d — результат %s"),
				M, Ex->OpenMode(M) ? TEXT("ОТКРЫТ") : TEXT("ОТКАЗ"));
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorld GExchangeDumpCmd(
	TEXT("asda.Exchange.Widgets"),
	TEXT("Аукцион (13000): дамп живого состояния окна — панели, сетки, вкладки, клик-зоны."),
	FConsoleCommandWithWorldDelegate::CreateStatic(
		[](UWorld* W)
		{
			if (const UAsdaExchangeSubsystem* Ex = ExchangeFromWorld(W)) { Ex->DumpLive(); }
			else { UE_LOG(LogAsdaExchange, Warning, TEXT("аукцион: подсистема не найдена")); }
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★ ЖИВАЯ ПРОВЕРКА ВКЛАДОК И СТРОК ВЫДАЧИ (жалобы 2026-08-28 пп. 2 и 3). Мышью в headless-PIE
//   не кликнуть, поэтому категория переключается командой, а список выдачи наполняется
//   СИНТЕТИЧЕСКИМ пакетом 9904 через штатный приёмник (`DebugFeedPacket`) — второго источника
//   истины не появляется.
static FAutoConsoleCommandWithWorldAndArgs GExchangeCatCmd(
	TEXT("asda.Exchange.Cat"),
	TEXT("Аукцион (13000): выбрать вкладку категории. Аргументы: <набор 4|5|3> <индекс>."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			UAsdaExchangeSubsystem* Ex = ExchangeFromWorld(W);
			if (!Ex || Args.Num() < 2) { return; }
			Ex->DebugPickCategory(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]));
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GExchangeFakeLotCmd(
	TEXT("asda.Exchange.FakeLot"),
	TEXT("Аукцион (13000): подать синтетический 9904 с одним лотом. Аргументы: <код предмета> [цена]."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный лот 9904 — «Купить» отправил бы 9905 с фальшивой записью.
			if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Exchange.FakeLot"))) { return; }
			UAsdaExchangeSubsystem* Ex = ExchangeFromWorld(W);
			if (!Ex || Args.Num() < 1) { return; }
			Ex->DebugFeedFakeLot(FCString::Atoi(*Args[0]),
				Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 12345);
		}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
// ★★★ САМОПРОВЕРКА ВКЛАДОК ПУТЁМ ИГРОКА (2026-08-30). Живой PIE-дамп уже ОДИН РАЗ СОВРАЛ по
//   вкладке 0 (владелец видит пустое место там, где дамп показывал белую подпись на чёрной
//   плашке), поэтому проверка перенесена в ЗАПУСК `-game` — тот же путь, каким игру видит
//   владелец, — и снимает не «что я поставил», а ФАКТИЧЕСКУЮ ЭКРАННУЮ ГЕОМЕТРИЮ после отрисовки:
//   абсолютный прямоугольник (`GetCachedGeometry`), видимость, кисть и цвет. Если виджет нулевого
//   размера, уехал или перекрыт — это видно числами, а не рассуждением.
//   Запуск: UnrealEditor.exe <uproject> -game -ExecCmds="asda.Exchange.SelfTest 4"
//   ★ АРГУМЕНТЫ (2026-08-28): `asda.Exchange.SelfTest <режим> [уровень] [quit]`.
//     • `уровень` > 0 — подставить его в `Net->SelfLevel` ПЕРЕД открытием: без сервера поле
//       нулевое, а гейт «24» обязан игнорировать ноль, поэтому иначе проверить обе ветки
//       (уровень < 24 → только модалка, уровень ≥ 24 → окно) нечем;
//     • `quit` — выйти после снятия дампа, чтобы прогон завершался сам.
//   Каждый шаг ПЕЧАТАЕТ РЕШЕНИЕ ГЕЙТА и факт наличия модалки — доказательство числами, а не
//   рассуждением: «окна нет» и «окна нет, зато есть модалка» — разные исходы.
static FAutoConsoleCommandWithWorldAndArgs GExchangeSelfTestCmd(
	TEXT("asda.Exchange.SelfTest"),
	TEXT("Аукцион (13000): <режим> [уровень] [quit] — открыть режим и снять ЭКРАННУЮ геометрию (для -game)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* W)
		{
			// ★ С-Р1 (26.09.2026), офлайн-гейт: самотест дважды шлёт 9903/9907 и подменяет SelfLevel живой сессии без возврата.
			static const uint16 kGuardOps[] = { 9903, 9907 };
			const FAsdaSelfTestArgs KitArgs = FAsdaSelfTestArgs::Parse(Args);   // ★ 11.10.2026 (В3 S1): каркас — отказ с ИТОГ и quit, второй барьер на все опкоды
			if (AsdaSelfTestKit::RefuseStart(W, TEXT("asda.Exchange.SelfTest"), KitArgs, 0.f, MakeArrayView(kGuardOps))) { return; }
			UAsdaExchangeSubsystem* Ex = ExchangeFromWorld(W);
			if (!Ex || !W) { AsdaSelfTestKit::Refused(W, TEXT("asda.Exchange.SelfTest"), KitArgs.bQuit, TEXT("нет мира или подсистемы аукциона")); return; }
			// ★ 11.10.2026 (В3 S1): `quit` в любой позиции не принимается за режим/уровень (прежде `… 4 quit` давал уровень Atoi("quit")).
			const int32 M  = KitArgs.Int(0, 4);
			const int32 Lv = KitArgs.Int(1, 0);
			const bool bQuit = KitArgs.bQuit;

			UGameInstance* GI = W->GetGameInstance();
			// ★ 11.10.2026 (В3 S1, N1(г)): подменённый уровень (и прочее «своё») возвращается в конце прогона — RAII каркаса.
			TSharedRef<FAsdaNetSelfScope> SelfScope = MakeShared<FAsdaNetSelfScope>(GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr,
				TEXT("asda.Exchange.SelfTest"));
			if (Lv > 0)
			{
				if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
				{
					Net->SelfLevel = (uint8)FMath::Clamp(Lv, 1, 255);
					UE_LOG(LogAsdaExchange, Log, TEXT("самотест: подставлен уровень %d"), Lv);
				}
			}

			auto Step = [Ex, GI, M](const TCHAR* Tag)
			{
				const bool bOpened = Ex->OpenMode(M);
				const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
				UE_LOG(LogAsdaExchange, Log,
					TEXT("САМОТЕСТ %s: режим %d, уровень %d → OpenMode=%s, окно EXCHANGE_WIN=%s, модалка=%s"),
					Tag, M, Ex->DebugSelfLevel(), bOpened ? TEXT("ОТКРЫТ") : TEXT("ОТКАЗ"),
					(WM && WM->IsWindowVisible(kWindowId)) ? TEXT("ВИДНО") : TEXT("нет"),
					(WM && WM->IsAnyModalOpen()) ? TEXT("ЕСТЬ") : TEXT("нет"));
			};

			// ★ СРЕДН-1 (ревизия shop_offline_fix, 10.10.2026): офлайн повторно — на отложенном старте
			//   (первый сработавший таймер; при отказе пропускаются все три — 9903/9907 не уходят, уровень не подменяется дальше).
			TSharedRef<FAsdaSelfTestRunGate> Gate = MakeShared<FAsdaSelfTestRunGate>(TEXT("asda.Exchange.SelfTest"), bQuit);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(Ex, [Step, Gate, Ex]()
			{
				if (!Gate->Pass(Ex)) { return; }
				Step(TEXT("клик 1"));
			}), 2.0f, false);
			// ★ ПОВТОРНОЕ открытие того же режима: для режима 3 запрос 9907 обязан уйти СНОВА
			//   (раньше защёлка `Mode == MyLots` запирала пункт «Мои предметы» навсегда).
			FTimerHandle H1;
			W->GetTimerManager().SetTimer(H1, FTimerDelegate::CreateWeakLambda(Ex, [Step, Gate, Ex]()
			{
				if (!Gate->Pass(Ex)) { return; }
				Step(TEXT("клик 2"));
			}), 3.5f, false);
			FTimerHandle H2;
			W->GetTimerManager().SetTimer(H2, FTimerDelegate::CreateWeakLambda(Ex, [Ex, bQuit, Gate, SelfScope]()
			{
				if (!Gate->Pass(Ex)) { SelfScope->Restore(); return; }   // отказ уже закрыл процесс при `quit`
				Ex->DumpTabsOnScreen();
				Ex->DumpLive();
				SelfScope->Restore();
				// ★ 11.10.2026 (В3 S1): единый итог и выход через `quit` движка (прежде `RequestExit` — пакетный прогон обрывался на
				//   этом самотесте). Самотест — дамп геометрии без проверок: ok = дамп снят.
				AsdaSelfTestKit::Finish(Ex->GetWorld(), TEXT("asda.Exchange.SelfTest"), bQuit, LogAsdaExchange, TEXT("САМОТЕСТ-АУКЦИОН"), 1, 0);
			}), 5.0f, false);
		}));
#endif // ASDA_DEV_CONSOLE

void UAsdaExchangeSubsystem::DumpTabsOnScreen() const
{
	UE_LOG(LogAsdaExchange, Log, TEXT("=== ВКЛАДКИ: ЭКРАННАЯ ГЕОМЕТРИЯ (путь -game) ==="));
	if (!Widget) { UE_LOG(LogAsdaExchange, Log, TEXT("виджета нет")); return; }
	auto Dump = [this](const TCHAR* Idc, int32 N)
	{
		for (int32 I = 0; I < N; ++I)
		{
			for (const FString& Nm : { FString::Printf(TEXT("%s_OPT%d"), Idc, I),
			     I == 0 ? FString::Printf(TEXT("%s_Text"), Idc)
			            : FString::Printf(TEXT("%s_Text%d"), Idc, I + 1) })
			{
				UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *DumpNodeGeom(*Nm));
			}
		}
		// планы: имя содержит jui-индекс, поэтому ищем перебором дерева
		if (Widget->WidgetTree)
		{
			Widget->WidgetTree->ForEachWidget([this, Idc](UWidget* W)
			{
				FString P; int32 J = 0, Pl = 0;
				if (W && AsdaUi::ParsePlaneName(W->GetName(), P, J, Pl) && P == Idc)
				{
					UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *DumpNodeGeom(*W->GetName()));
				}
			});
		}
	};
	Dump(TEXT("EXCHANGER_RADIOBTN2"), 7);
	Dump(TEXT("EXCHANGER_RADIOBTN1"), 8);
	// ★ Гнёзда самоцветов первой строки выдачи — обязаны быть Collapsed (в оригинале строка их
	//   не несёт; отсюда были «лишние иконки» на скриншоте владельца).
	for (int32 C = 0; C <= kLotSowels; ++C)
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("%s"),
			*DumpNodeGeom(*FString::Printf(TEXT("EXCHANGER_BB1_%d"), C)));
	}
	UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *DumpNodeGeom(TEXT("EXCHANGER_SOWEL")));
}

void UAsdaExchangeSubsystem::DebugPickCategory(int32 Set, int32 Index)
{
	if (Set == 3) { MyCategory = Index; RefreshCategoryCaptions(); return; }
	OnCategoryPicked(Set, Index);
}

void UAsdaExchangeSubsystem::DebugFeedFakeLot(int32 ItemCode, int32 Price)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	TArray<uint8> Body; Body.SetNumZeroed(kRec9904);          // раскладка §7.4
	AsdaBytes::WrI32(Body.GetData() + 0,  1);                  // серийник лота
	Body[4] = 0;                                               // код результата — успех
	AsdaBytes::WrI32(Body.GetData() + 5,  ItemCode);
	AsdaBytes::WrI32(Body.GetData() + 9,  1);                  // количество
	Body[13] = 100;                                            // прочность
	Body[46] = 0; Body[47] = 1;                                // страница 0 из 1
	AsdaBytes::WrI32(Body.GetData() + 48, Price);
	const char* Seller = "TESTSELLER";
	FMemory::Memcpy(Body.GetData() + 52, Seller, 10);
	Net->DebugFeedPacket(9904, Body);
}

// Живая геометрия одного узла — «где он на самом деле и какого размера» (жалоба про иконку
//   «во всё окно»): слот в родителе + фактический экранный прямоугольник + кисть.
FString UAsdaExchangeSubsystem::DumpNodeGeom(const TCHAR* Name) const
{
	const UWidget* W = Widget ? Widget->GetWidgetFromName(FName(Name)) : nullptr;
	if (!W) { return FString::Printf(TEXT("  %-28s НЕТ УЗЛА"), Name); }
	FString S = FString::Printf(TEXT("  %-28s %s vis=%s"), Name, *W->GetClass()->GetName(),
		*StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)W->GetVisibility()));
	if (const UCanvasPanelSlot* Sl = Cast<UCanvasPanelSlot>(W->Slot))
	{
		const FMargin O = Sl->GetOffsets();
		S += FString::Printf(TEXT(" слот(%.0f,%.0f %.0fx%.0f) auto=%d z=%d"),
			O.Left, O.Top, O.Right, O.Bottom, (int32)Sl->GetAutoSize(), Sl->GetZOrder());
	}
	const FGeometry& G = W->GetCachedGeometry();
	S += FString::Printf(TEXT(" экран(%.0f,%.0f %.0fx%.0f)"),
		G.GetAbsolutePosition().X, G.GetAbsolutePosition().Y,
		G.GetLocalSize().X, G.GetLocalSize().Y);
	if (const UImage* Img = Cast<UImage>(W))
	{
		const FSlateBrush& B = Img->GetBrush();
		const UObject* Res = B.GetResourceObject();
		const FBox2f Uv = B.GetUVRegion();
		S += FString::Printf(TEXT(" кисть[%s %.0fx%.0f uv %.3f,%.3f-%.3f,%.3f]"),
			Res ? *Res->GetName() : TEXT("-"), (float)B.ImageSize.X, (float)B.ImageSize.Y,
			Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y);
	}
	if (const UTextBlock* Tb = Cast<UTextBlock>(W))
	{
		const FLinearColor C = Tb->GetColorAndOpacity().GetSpecifiedColor();
		S += FString::Printf(TEXT(" текст«%s» цвет %.3f %.3f %.3f"),
			*Tb->GetText().ToString(), C.R, C.G, C.B);
	}
	return S;
}

void UAsdaExchangeSubsystem::DumpLive() const
{
	UE_LOG(LogAsdaExchange, Log, TEXT("=== АУКЦИОН 13000: живое состояние ==="));
	UE_LOG(LogAsdaExchange, Log, TEXT("режим %d, окно %s, контролы %s, блокировка %s, страница %d/%d"),
		(int32)Mode, bOpen ? TEXT("открыто") : TEXT("закрыто"),
		bControlsReady ? TEXT("связаны") : TEXT("НЕ связаны"),
		bAuctionBlocked ? TEXT("ДА") : TEXT("нет"), PageCur, PageLast);
	if (!Widget) { UE_LOG(LogAsdaExchange, Log, TEXT("виджета нет")); return; }

	auto VisOf = [this](const TCHAR* Name) -> FString
	{
		const UWidget* W = Widget->GetWidgetFromName(FName(Name));
		if (!W) { return TEXT("НЕТ УЗЛА"); }
		return StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)W->GetVisibility());
	};
	for (const TCHAR* N : { TEXT("EXCH_REGISTRATION_grp"), TEXT("EXCH_MYITEM_grp"),
	                        TEXT("EXCHANGER_WIN_grp"), TEXT("EXCHANGER_RADIOBTN1_OPT0"),
	                        TEXT("EXCHANGER_RADIOBTN2_OPT0"), TEXT("EXCH_OK"), TEXT("EXCHANGER_BUY"),
	                        TEXT("EXCHANGER_RESCAN"), TEXT("EXCHMY_DEL") })
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("  %-28s %s"), N, *VisOf(N));
	}
	UE_LOG(LogAsdaExchange, Log, TEXT("  сетки: выставление %d, строки лота %d, мои лоты %d, выдача %d, гнёзда %d"),
		GridSell ? GridSell->GetNumCells() : -1, GridReg ? GridReg->GetNumCells() : -1,
		GridMy ? GridMy->GetNumCells() : -1, GridLots ? GridLots->GetNumCells() : -1,
		GridSowel ? GridSowel->GetNumCells() : -1);
	UE_LOG(LogAsdaExchange, Log, TEXT("  годных ячеек инвентаря %d, «моих лотов» %d, лотов выдачи %d"),
		SellCells.Num(),
		[this]{ int32 N = 0; for (const FAsdaExchangeMyLot& L : MyLots) { if (!L.IsEmpty()) { ++N; } } return N; }(),
		[this]{ int32 N = 0; for (const FAsdaExchangeListing& L : Listings) { if (!L.IsEmpty()) { ++N; } } return N; }());

	auto DumpCaptions = [](const TCHAR* Title, const TArray<TObjectPtr<UTextBlock>>& T)
	{
		for (int32 I = 0; I < T.Num(); ++I)
		{
			if (!T[I]) { UE_LOG(LogAsdaExchange, Log, TEXT("  %s[%d] НЕТ УЗЛА"), Title, I); continue; }
			const FLinearColor C = T[I]->GetColorAndOpacity().GetSpecifiedColor();
			UE_LOG(LogAsdaExchange, Log, TEXT("  %s[%d] «%s» цвет %.3f %.3f %.3f"),
				Title, I, *T[I]->GetText().ToString(), C.R, C.G, C.B);
		}
	};
	DumpCaptions(TEXT("13408"), CatTexts1);
	DumpCaptions(TEXT("13409"), CatTexts2);
	DumpCaptions(TEXT("13260"), MyCatTexts);

	// ★★★ ПЛАШКИ ВКЛАДОК: у активной обязан стоять UV кадра 2 скина (V ≈ 0.484…0.570 у
	//   `TabBar_W72_*`), у остальных — кадр 0 (V ≈ 0.586…0.672). Именно это владелец видит как
	//   «чёрный фон + белая рамка».
	auto DumpPlanes = [](const TCHAR* Title, const TArray<TObjectPtr<UImage>>& P, int32 Active)
	{
		for (int32 I = 0; I < P.Num(); ++I)
		{
			if (!P[I]) { UE_LOG(LogAsdaExchange, Log, TEXT("  план %s[%d] НЕТ УЗЛА"), Title, I); continue; }
			const FSlateBrush& B = P[I]->GetBrush();
			const FLinearColor T = B.TintColor.GetSpecifiedColor();
			const FBox2f Uv = B.GetUVRegion();
			UE_LOG(LogAsdaExchange, Log,
				TEXT("  план %s[%d]%s uv %.3f,%.3f-%.3f,%.3f тинт %.2f %.2f %.2f"),
				Title, I, I == Active ? TEXT(" ← АКТИВНАЯ") : TEXT(""),
				Uv.Min.X, Uv.Min.Y, Uv.Max.X, Uv.Max.Y, T.R, T.G, T.B);
		}
	};
	DumpPlanes(TEXT("13408"), CatPlanes1, SearchCritSel1);
	DumpPlanes(TEXT("13409"), CatPlanes2, SearchCritSel2);
	DumpPlanes(TEXT("13260"), MyCatPlanes, MyCategory);

	// ★ Строки лота: цена из поля ввода, срок и комиссия — проверка жалобы №6.
	for (int32 R = 0; R < RegRows.Num(); ++R)
	{
		const FString Ed = (RegPriceEdits.IsValidIndex(R) && RegPriceEdits[R])
			? RegPriceEdits[R]->GetText().ToString() : FString(TEXT("НЕТ ПОЛЯ"));
		UE_LOG(LogAsdaExchange, Log,
			TEXT("  строка %d: код %d, поле «%s», цена %lld, дней %d (код срока %d, налог %d %%), комиссия %lld"),
			R, RegRows[R].Item.ItemCode, *Ed, RegRows[R].Price, RegRows[R].DueDays,
			DurationCodeForDays(RegRows[R].DueDays),
			SaleTaxPercentForDurationCode(DurationCodeForDays(RegRows[R].DueDays)),
			ListingFee(RegRows[R].Price));
	}
	{
		UGameInstance* GI = GetGameInstance();
		const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		UE_LOG(LogAsdaExchange, Log, TEXT("  NPC-аукционщик в шапке 99xx (+10): %d"),
			Net ? Net->GetExchangeNpcId() : -1);
	}
	// ★ Живая геометрия узлов, на которые жалуется владелец (пп. 1, 2, 3 от 2026-08-28).
	for (const TCHAR* N : { TEXT("EXCHANGER_SEARCH1"), TEXT("EXCHANGER_SEARCH1_466_0"),
	                        TEXT("EXCHANGER_SEARCH1_466_1"), TEXT("EXCHANGER_SEARCH1_Text"),
	                        TEXT("EXCH_DUE01"), TEXT("EXCH_DUE01_214_0"), TEXT("EXCH_DUE01_214_1"),
	                        TEXT("EXCHANGER_ITEM01_334_0"), TEXT("EXCHANGER_ITEMNAME01"),
	                        TEXT("EXCHANGER_ITEMPRI01"), TEXT("EXCHANGER_ITEMSEL01"),
	                        TEXT("EXCH_EDIT_PRICE01"), TEXT("EXCH_ITEMPRICE01"),
	                        TEXT("EXCH_FEETEXT"), TEXT("EXCH_TIMETEXT"), TEXT("EXCH_SELDEL_Text") })
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *DumpNodeGeom(N));
	}
	if (GridSell) { UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *GridSell->DumpHitZones(TEXT("сетка выставления"))); }
	if (GridLots) { UE_LOG(LogAsdaExchange, Log, TEXT("%s"), *GridLots->DumpHitZones(TEXT("строки выдачи"))); }
}

// ═══════════════════════ жизненный цикл ══════════════════════════════════════════════════════
void UAsdaExchangeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegRows.SetNum(kRegRows);
	// ★ Размер массивов задаёт ЛИМИТ СЕРВЕРА, а не число строк панели: 9908 шлёт до 10 записей
	//   на 8 строк «моих лотов», 9904 — до 8 записей на 8 строк выдачи (§9.4 отчёта).
	MyLots.SetNum(kMyMaxRecs);
	Listings.SetNum(kLotMaxRecs);

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			HDispatch = Net->OnExchangePacket.AddLambda(
				[this](int32 Opcode, int32 Kind, int32 Sub, const TArray<uint8>& Body)
				{
					// Эталонный диспетчер `npcShop_DispatchResult@0xD43270` роутит по (a3, a4).
					HandleDispatch(Kind, Sub, Body);
					// Логируем опкод отдельно: он нужен для выбора размера записи.
					UE_LOG(LogAsdaExchange, Verbose, TEXT("аукцион: опкод %d, тело %d б"),
						Opcode, Body.Num());
					LastOpcode = Opcode;
				});
		}
	}
}

void UAsdaExchangeSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (HDispatch.IsValid()) { Net->OnExchangePacket.Remove(HDispatch); }
		}
	}
	HDispatch.Reset();
	ResetBindings();
	Super::Deinitialize();
}

// ★ [ИСПРАВЛЕНИЕ] 25.10.2026: «эта причина начинает новую сессию и вещи прошлой уходят» — определено рядом с ключом
//   в `AsdaInventorySubsystem.cpp` (общая точка инвентаря, панели, склада и аукциона).
bool AsdaSessionResetClearsItems(EAsdaSessionResetReason Reason);

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaExchangeSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ⚠ `CloseAll()` здесь НЕ зовём: он идёт через `CancelByType`, а тот печатает игроку нотисы
	//   «аукцион закрыт». Игрок ничего не закрывал — сессия оборвалась. Закрываем молча.
	Mode = EAsdaExchangeMode::None;
	bOpen = false;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->CloseWindow(kWindowId);
		}
	}
	Widget = nullptr;
	ResetBindings();
	// Флаг «аукцион закрыт до конца сессии» (К-7): его пересчитывает список 9908, которого
	// после разрыва не будет — без сброса аукцион остался бы недоступен и новому персонажу.
	bAuctionBlocked = false;
	DeferredSaleGold = 0;   // ★ 29.09.2026: отложенное золото 9913 — данные сессии
	LastOpcode = 0;
	PageCur = 0; PageLast = 0;
	PrevCritSel1 = -1; PrevCritSel2 = -1;
	ScrollRowSell = 0; ScrollRowMy = 0; ScrollRowLot = 0;
	SellCells.Reset();
	PendingNumPadGrid = -1;
	MyCategory = 0;
	bRegAll = false; bMyAll = false; bLotAll = false;   // ★ пачка 20: большие галочки «всё» — состояние окна сессии
	Mouse.Reset();
	if (AsdaIsFullSessionReset(Reason))
	{
		UE_LOG(LogAsdaExchange, Log, TEXT("[аукцион] сброс сессии (%s): окно закрыто, блокировка снята"),
			AsdaSessionResetReasonName(Reason));
	}
	// ★ [ИСПРАВЛЕНИЕ] 25.10.2026 — перезаход другой учёткой (`Reports/inventory_relog_leak/REPORT.md`): строки
	//   выставления (`RegRows` — копии вещей СУМКИ с номером ячейки-источника), «мои лоты» (9908) и выдача поиска (9904)
	//   переживали выход на логин. Новый персонаж видел в панели выставления вещи прошлого (старые иконки), а `SendRegister`
	//   ушёл бы с ЧУЖИМИ номерами ячеек; по старым «моим лотам» пересчитывалась блокировка аукциона. У эталона поля
	//   класса рождаются с процессом (при выходе клиент закрывается). Длина массивов фиксированная — `Reset()+SetNum(N)`,
	//   как в их собственных точках перезаписи (`DropCheckedRegRows`, приём 9908/9904). Причины и ключ — общие с инвентарём.
	if (AsdaSessionResetClearsItems(Reason))
	{
		RegRows.Reset();  RegRows.SetNum(kRegRows);
		MyLots.Reset();   MyLots.SetNum(kMyMaxRecs);
		Listings.Reset(); Listings.SetNum(kLotMaxRecs);
	}
}

void UAsdaExchangeSubsystem::Tick(float DeltaTime)
{
	if (!bOpen || !Widget) { return; }

	// ОДИН опрос мыши на окно — фронты нельзя опрашивать дважды за кадр.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись бы
	//   в клик, которого игрок не делал.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		// У трёх полос прокрутки свой латч захвата бегунка (`bThumbGrab`); кадр «кнопка отпущена»
		//   снимает и его, и картинку нажатой стрелки — иначе бегунок поехал бы после нотиса.
		int32 Dummy = 0;
		if (ScrollSell.IsBuilt()) { ScrollSell.Tick(false, false, GetSellMaxScrollRow(), Dummy); }
		if (ScrollMy.IsBuilt())   { ScrollMy.Tick(false, false, GetMyMaxScrollRow(),   Dummy); }
		if (ScrollLot.IsBuilt())  { ScrollLot.Tick(false, false, GetLotMaxScrollRow(),  Dummy); }
		Mouse.Reset();
		return;
	}

	// Раскрытые списки-комбобоксы разбирают тот же кадр мыши.
	for (const TObjectPtr<UAsdaJuiComboBox>& C : RegDueCombos) { if (C) { C->Tick(Mouse); } }
	if (CmbKind)       { CmbKind->Tick(Mouse); }
	if (CmbClass)      { CmbClass->Tick(Mouse); }
	if (CmbLevel)      { CmbLevel->Tick(Mouse); }

	// ★★★ P3: три полосы прокрутки — тем же кадром мыши, общей точкой `FAsdaScrollBarNomal`.
	LayoutScrollBars();
	TickScrollBars();
}

void UAsdaExchangeSubsystem::ResetBindings()
{
	bControlsReady = false;
	GrpRegistration.Reset(); GrpMyItem.Reset(); GrpExchanger.Reset();
	GrpRadio1.Reset(); GrpRadio2.Reset(); AlwaysHidden.Reset();
	SellIcons.Reset(); SellLabels.Reset();
	RegIcons.Reset(); RegFeeTexts.Reset(); RegChecks.Reset(); RegPriceEdits.Reset();
	RegDueCombos.Reset(); RegArrUp.Reset(); RegArrDn.Reset();
	MyIcons.Reset(); MyNames.Reset(); MyPrices.Reset(); MyTimes.Reset(); MyChecks.Reset();
	LotIcons.Reset(); LotNames.Reset(); LotPrices.Reset(); LotSellers.Reset(); LotChecks.Reset();
	ChkRegAll = nullptr; ChkMyAll = nullptr; ChkLotAll = nullptr;   // ★ пачка 20: большие галочки «всё»
	LotSowelIcons.Reset();
	CatBtns1.Reset(); CatBtns2.Reset(); MyCatBtns.Reset(); MyCatTexts.Reset();
	CatTexts1.Reset(); CatTexts2.Reset();
	// ★ Кэш базовых цветов подписей снимается ОДИН раз; при пересборке окна его надо обнулить,
	//   иначе «базовым» окажется уже проставленный цвет активной вкладки.
	CatIdle1.Reset(); CatIdle2.Reset(); MyCatIdle.Reset();
	// ★ Кисти плашек вкладок кэшируются по ЖИВЫМ виджетам — при пересборке дерева сброс обязателен.
	CatPlanes1.Reset(); CatPlanes2.Reset(); MyCatPlanes.Reset();
	CatPlaneIdle1.Reset(); CatPlaneIdle2.Reset(); MyCatPlaneIdle.Reset();
	CatBtnIdle1.Reset(); CatBtnIdle2.Reset(); MyCatBtnIdle.Reset();
	GrpFilter1.Reset(); GrpFilter2.Reset(); GrpFilter3.Reset();
	Relays.Reset();
	GridSell = nullptr; GridReg = nullptr; GridMy = nullptr; GridLots = nullptr;
	GridSowel = nullptr;
	ScrollSell = FAsdaScrollBarNomal();
	ScrollMy   = FAsdaScrollBarNomal();
	ScrollLot  = FAsdaScrollBarNomal();
	ScrollRowSell = ScrollRowMy = ScrollRowLot = 0;
	CmbKind = CmbClass = CmbLevel = nullptr;
	TxtFilter1 = TxtFilter2 = TxtFilter3 = nullptr;
	SellSelFrame = nullptr;
	BtnTitleDel = BtnSelDel = BtnRegOk = BtnRegCancel = nullptr;
	BtnMyDel = BtnMyClose = BtnBuy = BtnExchCancel = BtnRescan = nullptr;
	BtnPagePrev = BtnPageNext = BtnPageFirst = BtnPageLast = nullptr;
	TxtSubTitle = TxtMoney = TxtTotalFee = TxtMyAmount = TxtMyRegGold = nullptr;
	TxtBuyPrice = TxtExchGold = TxtPage1 = TxtPage2 = nullptr;
	Mouse.Reset();
}

void UAsdaExchangeSubsystem::EnsureWidget()
{
	if (Widget) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// Порт `CItemMediation_BuildUI@0xD40100`: окно создаётся ЛЕНИВО, при первом открытии.
	Widget = WM->OpenWindowByPath(kWindowId, kWindowWbp, /*ZOrder*/ 0);
	if (!Widget)
	{
		UE_LOG(LogAsdaExchange, Error, TEXT("аукцион: не удалось открыть окно %s"), kWindowWbp);
		return;
	}
	ResetBindings();
	SetupControls();
	RegisterZone();
	WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateUObject(this,
		&UAsdaExchangeSubsystem::CloseAll));
}

void UAsdaExchangeSubsystem::RegisterZone()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — планы корня `EXCHANGE_WIN_0_*` (скин `Win_Mg`).
	WM->RegisterUiZone(kWindowId, FAsdaUiZoneRect::CreateWeakLambda(this,
		[this](FVector2D& Mn, FVector2D& Mx) -> bool
		{
			if (!bOpen || !Widget) { return false; }
			return AsdaUi::FramePlanesAbsRect(Widget, TEXT("EXCHANGE_WIN_0_%d"), 9, Mn, Mx);
		}));
}

void UAsdaExchangeSubsystem::CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const
{
	Out.Reset();
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		// Сам контрол, его группа-канвас, его подписи `_Text`, `_Text2`… и его кнопки `_OPT<i>`.
		//   ★★★ ПРЕЖНЯЯ ВЕРСИЯ ЛОВИЛА ТОЛЬКО `_Text` И ПЛАНЫ: кнопки набора категорий
		//   `EXCHANGER_RADIOBTN1_OPT0…7` в группу НЕ ПОПАДАЛИ, поэтому при показе набора 13409
		//   его семь кнопок оставались `Collapsed` (вкладки режима 4 не нажимались вовсе),
		//   а при гашении набора 13408 его восемь кнопок оставались ЖИВЫМИ и невидимо
		//   перехватывали клики. Жалоба владельца «вкладки не переключаются».
		if (N == Idc || N == Idc + TEXT("_grp")) { Out.Add(W); continue; }
		// `_Text`, `_Text2`… и `_OPT0`… — ТОЛЬКО с чисто цифровым хвостом: `EXCHANGER_RADIOBTN2_Text_1`
		//   это остаток ВТОРОГО, дублирующего объявления контрола в данных, и он не наш.
		auto TailIsIndex = [&N](const FString& Pre) -> bool
		{
			if (!N.StartsWith(Pre)) { return false; }
			const FString Tail = N.RightChop(Pre.Len());
			if (Tail.IsEmpty()) { return true; }
			for (TCHAR C : Tail) { if (!FChar::IsDigit(C)) { return false; } }
			return true;
		};
		if (TailIsIndex(Idc + TEXT("_Text")) || TailIsIndex(Idc + TEXT("_OPT"))) { Out.Add(W); continue; }
		FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
		if (AsdaUi::ParsePlaneName(N, ParsedIdc, JuiIdx, Plane) && ParsedIdc == Idc) { Out.Add(W); }
	}
}

void UAsdaExchangeSubsystem::CollectRadioSet(const FString& Idc, int32 ExpectedButtons,
	TArray<TObjectPtr<UWidget>>& Out) const
{
	CollectByIdc(Idc, Out);

	// ★★★ В ДАННЫХ КОНТРОЛ `EXCHANGER_RADIOBTN2` ОБЪЯВЛЕН ДВАЖДЫ: `TabBar_W77_5Btn` (5 планов,
	//   IDNum −1, `gamemain_jui.ini:233959`) и `TabBar_W72_7Btn` (7 планов, IDNum 13409,
	//   `:234001`). В `WidgetTree` имя уникально, поэтому кнопок — один набор (7), а ПЛАНОВ —
	//   ДВА набора с разными jui-индексами (474 и 476, живой дамп дерева). Показать оба нельзя:
	//   пять широких плашек 77 px легли бы поверх семи узких 72 px.
	//   ⇒ Оставляем набор планов, число которых совпадает с числом кнопок.
	TMap<int32, int32> PlanesPerJuiIdx;
	for (const TObjectPtr<UWidget>& W : Out)
	{
		FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
		if (W && AsdaUi::ParsePlaneName(W->GetName(), ParsedIdc, JuiIdx, Plane) && ParsedIdc == Idc)
		{
			PlanesPerJuiIdx.FindOrAdd(JuiIdx)++;
		}
	}
	if (PlanesPerJuiIdx.Num() <= 1) { return; }

	int32 KeepIdx = -1;
	for (const TPair<int32, int32>& It : PlanesPerJuiIdx)
	{
		if (It.Value == ExpectedButtons) { KeepIdx = It.Key; break; }
	}
	if (KeepIdx < 0) { return; }   // совпадения нет — лучше ничего не выбрасывать

	Out.RemoveAll([&Idc, KeepIdx](const TObjectPtr<UWidget>& W)
	{
		FString ParsedIdc; int32 JuiIdx = 0, Plane = 0;
		return W && AsdaUi::ParsePlaneName(W->GetName(), ParsedIdc, JuiIdx, Plane)
			&& ParsedIdc == Idc && JuiIdx != KeepIdx;
	});
	UE_LOG(LogAsdaExchange, Log,
		TEXT("аукцион: набор %s — оставлен план-индекс %d (наборов планов было %d, кнопок %d)"),
		*Idc, KeepIdx, PlanesPerJuiIdx.Num(), ExpectedButtons);
}

void UAsdaExchangeSubsystem::SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible)
{
	// ★★★ ДЕФЕКТ ПОРТА, НАЙДЕННЫЙ 2026-08-27 (жалоба «кнопки не нажимаются»).
	//   Здесь стоял `ESlateVisibility::HitTestInvisible`, а группы панелей аукциона — это
	//   КАНВАС-КОНТЕЙНЕРЫ (`EXCH_REGISTRATION_grp`, `EXCH_MYITEM_grp`, `EXCHANGER_WIN_grp`,
	//   живой дамп `Tools/_probe_exchange_tree.py`). В UMG `HitTestInvisible` — это
	//   «виджет И ВСЕ ЕГО ДЕТИ не участвуют в хит-тесте», в отличие от `SelfHitTestInvisible`,
	//   где мышь не ловит только сам виджет. Одна эта строка выключала мышь у ВСЕГО окна:
	//   вкладок, кнопок «Поиск/Купить/Отмена/Зарегистрировать», галочек, полей цены и ячеек.
	//   ⇒ Контейнерам и планам — `SelfHitTestInvisible`; интерактиву — `Visible`;
	//   подписям-`TextBlock` — `HitTestInvisible` (закон `ui-button-hover-click-rule`).
	// ★★★ СВЕДЕНО (`fix_helpers_dedup.md`) к общей точке `AsdaCommunityUi::ShownVisibilityFor`.
	//   Прежняя копия давала: ComboBox→Collapsed, Button/CheckBox/EditableText→Visible,
	//   TextBlock→HitTestInvisible, прочее→SelfHitTestInvisible. Общая точка совпадает по существу:
	//   движковый комбобокс так же `Collapsed`; тот же интерактив `Visible`; декорации
	//   `SelfHitTestInvisible` (для листового `UTextBlock` это тождественно прежнему
	//   `HitTestInvisible` — детей у него нет). Дополнительно общая точка правильно относит к
	//   интерактиву прокрутку/ползунки/списки (если в группе появятся) — прежняя копия их гасила.
	for (const TObjectPtr<UWidget>& W : Group)
	{
		if (!W) { continue; }
		W->SetVisibility(bVisible ? AsdaCommunityUi::ShownVisibilityFor(W) : ESlateVisibility::Collapsed);
	}
}
