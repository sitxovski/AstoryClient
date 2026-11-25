// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSowelSubsystem — реализация системы самоцветов окна инвентаря.
//   Вся мотивировка, адреса эталона и раскладки пакетов — в шапке `AsdaSowelSubsystem.h`
//   и в отчёте `Reports/sowel_system_port.md`.

#include "UI/AsdaSowelSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)

#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"   // окно выбора гнезда 1530: делегат «OK» → наш 5024
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaAnimatedPlane.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "World/AsdaPlayerController.h"
#include "AsdaNetSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ 21.10.2026: гейт `CObject_GetField254(g_pInventoryWnd)` = барабан гачи 10050
#include "UI/AsdaWarehouseSubsystem.h"      // ★ 21.10.2026: гейт `CObject_GetField5581(g_pWarehouseWnd)` = удалённый склад (+0x5734)
#include "UI/AsdaSystemOptionSubsystem.h"   // ★ 21.10.2026: гейт `objStateAllowsRender` = `IsLocalPlayerBusy` (нотис 2314)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSowel, Log, All);

// ★ 21.10.2026 (ревизия): цвет строк «святости» — эталонный −7930543 = 0xFF86FD51 (item_inventory.c:41430-41488, тот же
//   цвет у общего «своего подбора» `UAsdaMessageSubsystem`). Прежде стоял 0xFF873611 (коричневый) — ошибка перевода числа.
static const FColor kHolyNoticeColor(0x86, 0xFD, 0x51, 0xFF);

static const FName kSowelWheelId(TEXT("INVEN_SOWELLWHEELWIN"));

// ── Флаги отката исправлений (закон `fix-original-defects`) ────────────────────────────────────
static ASDA_DEV_CVAR(int32, CVarSowelLegacyNoDetachConfirm,
	TEXT("asda.Sowel.LegacyNoDetachConfirm"), 0,
	TEXT("С1: 1 = не показывать своё окно подтверждения на 5023 result=2, слать 5022 повторно молча (как в эталоне)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarSowelLegacyNoPreCheck,
	TEXT("asda.Sowel.LegacyNoPreCheck"), 0,
	TEXT("С2: 1 = не проверять совместимость камня локально перед 5024 (как ветка `itemSowelApplySend`, которая её дублирует)."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026: канал отказов 5025 (см. `ApplySocketResult`). 1 = как оригинал (окно — только код 0, прочие — чат).
static ASDA_DEV_CVAR(int32, CVarSowelRefusalToChat,
	TEXT("asda.Sowel.RefusalToChat"), 1,
	TEXT("1 = отказ 5025 кодами 2/4…11/15 — строка в чат цветом −11514, окно-сообщение только у кода 0 (как оригинал); "
	     "0 = прежний порт: модальное окно на каждый отказ."),
	ECVF_Default);

// ── Мелкие читалки ─────────────────────────────────────────────────────────────────────────────
// ★ D4 (аудит `code_dedup_audit_2.md`): тела переехали в `AsdaBytes` (Public/AsdaBytes.h) — семь
//   одинаковых наборов читалок по подсистемам сведены в один. Здесь остались форварды.
static FORCEINLINE int32 SwRdI16(const uint8* P) { return AsdaBytes::RdI16(P); }
static FORCEINLINE int32 SwRdI32(const uint8* P) { return AsdaBytes::RdI32(P); }

// Байт сырой записи таблицы из hex-колонки `DataHex` (импортёр кладёт запись 1:1 с offset 0). −1 вне строки.
// ★ УТОЧНЕНИЕ ПОВЕДЕНИЯ: прежняя копия брала ниббл через `FParse::HexDigit`, а тот на НЕ-hex символе
//   молча отдаёт 0 — битая строка превращалась в «валидный» байт. Общая точка честно возвращает −1
//   («поля нет»), ровно как остальные шесть прежних копий.
static FORCEINLINE int32 SwHexByte(const FString& Hex, int32 Off) { return AsdaBytes::HexByteAt(Hex, Off); }

// ★ D5/D6 (аудит `code_dedup_audit_2.md`): разбор имени плана импортёра и позиция в координатах
//   корня окна переехали в `AsdaUi::ParsePlaneName` / `AsdaUi::CanvasRootPos` — по 4 и 2 копии
//   соответственно жили по подсистемам. Здесь их больше нет, зовём общие точки напрямую.

// ═══════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ════════════════════════════════════════════════

void UAsdaSowelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TrayIcon.SetNum(kTrayCount);   TrayText.SetNum(kTrayCount);   TrayIconRoot.SetNum(kTrayCount);
	SocketIcon.SetNum(kSocketCount); SocketText.SetNum(kSocketCount); SocketIconRoot.SetNum(kSocketCount);
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] подсистема поднята (ветка 430…437 / 441…446 / 495 / 499 окна 400)"));
}

void UAsdaSowelSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSowelSocketResult.RemoveAll(this);
			Net->OnSowelDetachResult.RemoveAll(this);
			Net->OnHolyItemResult.RemoveAll(this);
		}
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->OnSowelSelBoxAccepted.RemoveAll(this);
		}
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			WM->UnregisterWheelHandler(kSowelWheelId);
		}
	}
	ResetWidgets();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaSowelSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	ResetWidgets();
	bAwaitingResult = false;          // ждали результат вставки/снятия камня
	SentAtS = 0.0;
	PendingSocket.Clear();
	PendingDetach.Clear();
	PendingHoly.Clear();
	bDetachConfirmPending = false;
	DeferredHolyBody.Reset();
	bHolyArmed = false;
	TargetCell = -1;
	DragArm.Disarm();   // ★ 21.10.2026: взвод переноса вместо прежних защёлок PressTray/PressSocket
	Dbl.Reset();
	HolyAnimStartS = 0.0;
	Mouse.Reset();
	(void)Reason;
}

UAsdaInventorySubsystem* UAsdaSowelSubsystem::Inv() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

void UAsdaSowelSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.Sowel");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }

	// Подписка на 5025 / 5023 / 5032 — лениво (сеть поднимается позже подсистем UI).
	if (!bNetBound)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnSowelSocketResult.AddUObject(this, &UAsdaSowelSubsystem::OnSocketResult);
			Net->OnSowelDetachResult.AddUObject(this, &UAsdaSowelSubsystem::OnDetachResult);
			Net->OnHolyItemResult.AddUObject(this, &UAsdaSowelSubsystem::OnHolyResult);
			bNetBound = true;
			UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] подписки на 5025 / 5023 / 5032 установлены"));
		}
	}

	// ★ Обратный ход окна выбора гнезда (1530): оно живёт в подсистеме тултипа и знает только
	//   номер гнезда, а состояние камней — здесь. Подписка ленивая и РОВНО ОДНА (флаг): двойная
	//   подписка отправила бы 5024 дважды на один клик.
	if (!bTipBound)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->OnSowelSelBoxAccepted.AddUObject(this, &UAsdaSowelSubsystem::HandleSelBoxAccepted);
			bTipBound = true;
			UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] окно выбора гнезда (1530) подключено — «OK» шлёт 5024"));
		}
	}

	// Очереди ответов применяем ЗДЕСЬ (закон `no-reentrant-net-callbacks`).
	//   ★ Аудит №3 (B10): три одинаковых блока «под замком забрать всё разом и очистить» — общая точка.
	TArray<TArray<uint8>> S, D, H;
	PendingSocket.Drain(S);
	PendingDetach.Drain(D);
	PendingHoly.Drain(H);
	for (const TArray<uint8>& B : S) { ApplySocketResult(B); }
	for (const TArray<uint8>& B : D) { ApplyDetachResult(B); }
	for (const TArray<uint8>& B : H) { ApplyHolyResult(B); }

	// Предохранитель: анимация 498 не прислала «завершено» — применяем отложенный 5032 сами.
	if (DeferredHolyBody.Num() >= 63 && FPlatformTime::Seconds() - HolyAnimStartS > (double)HolyAnimMaxWaitS)
	{
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] анимация 498 не сообщила о завершении за %.1f с — разбираем 5032 по таймауту"), HolyAnimMaxWaitS);
		OnHolyEffectAnimFinished();
	}

	if (bAwaitingResult && FPlatformTime::Seconds() - SentAtS > (double)ResultTimeoutS)
	{
		bAwaitingResult = false;
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] ответ не пришёл за %.1f с — снимаем ожидание"), ResultTimeoutS);
	}

	UAsdaInventorySubsystem* I = Inv();
	if (!I || !I->IsWindowOpen())
	{
		if (bControlsReady) { ResetWidgets(); }
		return;
	}
	if (!EnsureWidgets()) { return; }
	LayoutListBar();   // ★ 21.10.2026: полоса 495 видна ровно тогда, когда видна правая панель
	TickPointer();
}

// ═══════════════════════════════ ВИДЖЕТЫ ═══════════════════════════════════════════════════════

bool UAsdaSowelSubsystem::EnsureWidgets()
{
	UAsdaInventorySubsystem* I = Inv();
	UUserWidget* W = I ? I->GetWindowWidget() : nullptr;
	if (!W) { return false; }
	if (W != HostWidget) { ResetWidgets(); HostWidget = W; }
	if (!bControlsReady) { ScanWidgets(); }
	return bControlsReady;
}

void UAsdaSowelSubsystem::ResetWidgets()
{
	// ★ 21.10.2026: перенос, начатый до сброса, гасим (иначе призрак остался бы висеть, а источник — тусклым);
	//   планы полосы 495 живут в дереве окна — прячем, но НЕ забываем: при переоткрытии ТОГО ЖЕ окна они
	//   переиспользуются (`ScanWidgets`), иначе каждое открытие инвентаря добавляло бы ещё четыре плана.
	EndStoneDragVisual();
	DragArm.Disarm();
	Dbl.Reset();
	if (ChromeHost.IsValid() && ListBar.IsBuilt()) { ListBar.Hide(); }
	bListBarShown = false;
	ListBarKey = FVector4f(-1.f, -1.f, -1.f, -1.f);
	HostWidget = nullptr;
	for (int32 i = 0; i < kTrayCount; ++i)   { TrayIcon[i] = nullptr;   TrayText[i] = nullptr; }
	for (int32 i = 0; i < kSocketCount; ++i) { SocketIcon[i] = nullptr; SocketText[i] = nullptr; }
	TraySel = nullptr; SocketSel = nullptr; AvailTxt = nullptr;
	Shade1 = Shade2 = Shade3 = nullptr;
	EmptyBackGrp = nullptr; EmptyBackPlanes.Reset();
	HolyEffect = nullptr; SoulStoneEffect = nullptr;
	bControlsReady = false;
}

void UAsdaSowelSubsystem::ScanWidgets()
{
	if (!HostWidget || !HostWidget->WidgetTree) { return; }
	TArray<UWidget*> All;
	HostWidget->WidgetTree->GetAllWidgets(All);

	// Ширина ячейки сетки INVEN_SET0 — нужна для эталонного центрирования эффектов 498/10009.
	float Set0W = 32.f, Set0H = 32.f;

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			Tb->SetAutoWrapText(false);   // закон `ui-autowrap-canvas-gotcha`
			if (N == TEXT("Inven_AvailableSowelTxt")) { AvailTxt = Tb; continue; }
			for (int32 i = 0; i < kTrayCount; ++i)
			{
				if (N == FString::Printf(TEXT("INVEN_SOWTRAY%d"), i)) { TrayText[i] = Tb; Tb->SetText(FText::GetEmpty()); break; }
			}
			for (int32 i = 0; i < kSocketCount; ++i)
			{
				if (N == FString::Printf(TEXT("INVEN_SOWELL%d"), i)) { SocketText[i] = Tb; Tb->SetText(FText::GetEmpty()); break; }
			}
			continue;
		}

		if (N == TEXT("INVEN_SOWELLEMTYBACK_grp")) { EmptyBackGrp = W; continue; }

		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane)) { continue; }

		if (UAsdaAnimatedPlane* Anim = Cast<UAsdaAnimatedPlane>(W))
		{
			if (Idc == TEXT("INVEN_SOWELLSELECT"))   { TraySel = Anim; continue; }
			if (Idc == TEXT("INVEN_SOWELLSEL"))      { SocketSel = Anim; continue; }
			if (Idc == TEXT("INVEN_HOLYEFFECT"))     { HolyEffect = Anim; continue; }
			if (Idc == TEXT("INVEN_SOULSTONEEFFECT")){ SoulStoneEffect = Anim; continue; }
			continue;
		}
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }

		if (Idc == TEXT("INVEN_SOWELLSHADE"))  { Shade1 = Img; continue; }
		if (Idc == TEXT("INVEN_SOWELLSHADE1")) { Shade3 = Img; continue; }
		if (Idc == TEXT("INVEN_SOWELLSHADE2")) { Shade2 = Img; continue; }
		// 499 = сам план + три безымянных «шторки» (в JUI они его ДЕТИ, поэтому гаснут вместе с ним).
		if (Idc.StartsWith(TEXT("INVEN_SOWELLEMTYBACK"))) { EmptyBackPlanes.AddUnique(Img); continue; }

		bool bTaken = false;
		for (int32 i = 0; i < kTrayCount && !bTaken; ++i)
		{
			if (Idc == FString::Printf(TEXT("INVEN_SOWTRAY%d"), i))
			{
				TrayIcon[i] = Img;
				TrayIconRoot[i] = AsdaUi::CanvasRootPos(Img);
				bTaken = true;
			}
		}
		if (bTaken) { continue; }
		for (int32 i = 0; i < kSocketCount && !bTaken; ++i)
		{
			if (Idc == FString::Printf(TEXT("INVEN_SOWELL%d"), i))
			{
				SocketIcon[i] = Img;
				SocketIconRoot[i] = AsdaUi::CanvasRootPos(Img);
				bTaken = true;
			}
		}
		if (bTaken) { continue; }
		if (Idc == TEXT("INVEN_SET0"))
		{
			if (const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Img))
			{
				const FMargin O = CS->GetOffsets();
				Set0W = O.Right; Set0H = O.Bottom;
			}
		}
	}

	if (TraySel)   { TraySelParentRoot   = AsdaUi::CanvasRootPos(TraySel->GetParent()); }
	if (SocketSel) { SocketSelParentRoot = AsdaUi::CanvasRootPos(SocketSel->GetParent()); }

	// Эталон `InitControls`: `pInvenWnd[219] = (ширина 498 − ширина INVEN_SET0) / 2`, то же для 10009.
	auto CenterOff = [Set0W, Set0H](const UWidget* Eff) -> FVector2D
	{
		if (const UCanvasPanelSlot* CS = Eff ? AsdaUi::SlotOf(Eff) : nullptr)
		{
			const FMargin O = CS->GetOffsets();
			return FVector2D((O.Right - Set0W) * 0.5f, (O.Bottom - Set0H) * 0.5f);
		}
		return FVector2D::ZeroVector;
	};
	HolyCenterOff = CenterOff(HolyEffect);
	SoulCenterOff = CenterOff(SoulStoneEffect);

	// Заголовок 437 = нотис 12198 «Живой камень» (эталон item_inventory.c:35598-35603).
	if (AvailTxt) { AvailTxt->SetText(FText::FromString(LocRaw(kLocAvailTitle))); }

	// Эталон в init прячет 499 / 446 / 10040 / 10041 (item_inventory.c:35481-35484).
	SetEmptyBackVisible(false);
	SetVis(Shade1, false); SetVis(Shade2, false); SetVis(Shade3, false);
	SetVis(TraySel, false); SetVis(SocketSel, false);
	SetVis(HolyEffect, false); SetVis(SoulStoneEffect, false);

	// Колесо над списком камней = «колёсное окно» 495 эталона (`SetScrollPage(1,1)`, msgType 0x17).
	//   ★ 21.10.2026: отдельного обработчика под id «INVEN_SOWELLWHEELWIN» больше нет — его опрашивали ПОСЛЕДНИМ
	//   (такого окна нет в реестре ⇒ z = −1), и колесо уходило любому окну выше. Теперь колесо приходит из обработчика
	//   окна инвентаря (`UAsdaInventorySubsystem::OnWheel` → `HandleListWheel`) — на z самого окна.
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWheelHandler(kSowelWheelId);
	}

	// ★★★ 21.10.2026 — ПОЛОСА ПРОКРУТКИ СПИСКА (жалоба игроков «список камней не листается»).
	//   В .JUI у колёсного окна 495 `INVEN_SOWELLWHEELWIN` стоит `VertScrollPosType = "1"` — у оригинала это
	//   ВИДИМАЯ вертикальная полоса окна: `inventorySowelWin_BuildSowelList@0xD18120` ставит ей
	//   `uiControlSetScrollRange(495, 1, страниц)` (1, 1 при < 6 камней), `itemGrid_RedrawDefaultSlots` — (1, 0),
	//   `InitControls` — `uiControlSetScrollPage(495, 1, 1)` (item_inventory.c:35388), сброс страницы —
	//   `uiCtrl_SetChildScrollbarPos(495, 0)`. Импортёр полосы колёсных окон не создаёт (у оригинала её рисует
	//   движок окна), поэтому — тот же общий компонент `FAsdaScrollBarNomal`, что у сетки инвентаря 494, магазина,
	//   книги навыков. Собирается ОДИН раз на окно (тот же WBP при переоткрытии — те же планы).
	{
		UUserWidget* Host = HostWidget.Get();
		if (ChromeHost.Get() != Host)
		{
			// Новое окно (пересоздали WBP / сменился мир): прежние планы и призрак умерли вместе со старым деревом.
			ListBar = FAsdaScrollBarNomal();
			DragGhost = FAsdaDragGhost();
			ChromeHost = Host;
		}
		if (!ListBar.IsBuilt() && Host->WidgetTree)
		{
			if (UCanvasPanel* Root = Cast<UCanvasPanel>(Host->WidgetTree->RootWidget))
			{
				ListBar.Build(Host, Root, TEXT("INVEN_SOWELLWHEELWIN_Scr"), /*ZTrack*/ 20);
			}
		}
		if (ListBar.IsBuilt()) { ListBar.Hide(); }
		bListBarShown = false;
		ListBarKey = FVector4f(-1.f, -1.f, -1.f, -1.f);
	}

	bControlsReady = true;
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] контролы разобраны: список %d/%d, гнёзда %d/%d, шторки %d, 499-планов %d"),
		TrayIcon.FilterByPredicate([](const TObjectPtr<UImage>& P) { return P != nullptr; }).Num(), kTrayCount,
		SocketIcon.FilterByPredicate([](const TObjectPtr<UImage>& P) { return P != nullptr; }).Num(), kSocketCount,
		(Shade1 ? 1 : 0) + (Shade2 ? 1 : 0) + (Shade3 ? 1 : 0), EmptyBackPlanes.Num());
}

void UAsdaSowelSubsystem::SetVis(UWidget* W, bool bVisible)
{
	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	AsdaUi::SetShownDecor(W, bVisible);
}

// 499 в JUI — РОДИТЕЛЬ ещё трёх «шторок»; импортёр разложил их в одну группу и свернул детей
//   поодиночке. Показ/скрытие делаем группой И всеми планами сразу, иначе видна только одна шторка.
void UAsdaSowelSubsystem::SetEmptyBackVisible(bool bVisible)
{
	SetVis(EmptyBackGrp, bVisible);
	for (UImage* P : EmptyBackPlanes) { SetVis(P, bVisible); }
}

// ═══════════════════════════════ ВЫБОР ПРЕДМЕТА ════════════════════════════════════════════════

const FAsdaItemInstance* UAsdaSowelSubsystem::TargetItem() const
{
	const UAsdaInventorySubsystem* I = Inv();
	if (!I || TargetCell < 0) { return nullptr; }
	return I->GetCell(TargetContainer, TargetCell);
}

void UAsdaSowelSubsystem::OnItemSelected(int32 Container, int32 Cell)
{
	// Ветка камней живёт ТОЛЬКО для предметов контейнера 1 (сумка) и 3 (надетое) — так в эталоне:
	//   `inventorySowelWin_BuildSowelList` зовётся из ветки `g_InventoryBag` клика по ячейке и из
	//   `ItemTooltip_PopulateInfoPanel` для надетого слота. Для расходников её нет вовсе.
	if (Container != 1 && Container != 3) { OnSelectionCleared(); return; }
	const UAsdaInventorySubsystem* I = Inv();
	const FAsdaItemInstance* It = I ? I->GetCell(Container, Cell) : nullptr;
	if (!It || It->IsEmpty()) { OnSelectionCleared(); return; }

	TargetContainer = Container;
	TargetCell = Cell;
	SelStoneIndex = -1; SelStoneCol = -1; SelSocket = -1;
	Pick = EAsdaSowelPick::None;

	RebuildList();
	RedrawSockets();
}

void UAsdaSowelSubsystem::OnSelectionCleared()
{
	TargetContainer = 0; TargetCell = -1;
	SelStoneIndex = -1; SelStoneCol = -1; SelSocket = -1;
	Pick = EAsdaSowelPick::None;
	ClearBranch();
}

// Эталон `itemGrid_RedrawDefaultSlots@0xD18D50`: диапазон колеса 0, вектор очищается, шесть ячеек
//   получают «пустую» иконку слота, курсоры гаснут.
void UAsdaSowelSubsystem::ClearBranch()
{
	SowelList.Reset();
	ListPage = 0; CachedPage = -1;
	if (!bControlsReady) { return; }
	for (int32 i = 0; i < kTrayCount; ++i) { ClearIcon(TrayIcon[i]); }
	for (int32 i = 0; i < kSocketCount; ++i) { ClearIcon(SocketIcon[i]); }
	SetVis(TraySel, false);
	SetVis(SocketSel, false);
	SetVis(Shade1, false); SetVis(Shade2, false); SetVis(Shade3, false);
	SetEmptyBackVisible(false);
}

// ═══════════════════════════════ СОВМЕСТИМОСТЬ ═════════════════════════════════════════════════

bool UAsdaSowelSubsystem::GetSowelTargets(int32 SowelCode, int32 Out[3], int32& OutSpecialFlag, int32& OutOptionMask) const
{
	Out[0] = Out[1] = Out[2] = -1; OutSpecialFlag = 0; OutOptionMask = 0;
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	FAsdaSowellRow Row;
	if (!IT->GetSowell(SowelCode, Row)) { return false; }
	Out[0] = Row.SocketTargetType;    // +45
	Out[1] = Row.SocketTargetType2;   // +46
	// ★ Третьего байта-цели (+47) в DataTable НЕТ — эталон читает `sowelRecord->gap_2D[2]`.
	//   Берём его из сырой записи (`DataHex` хранит запись 1:1 с offset 0).
	const int32 B47 = SwHexByte(Row.DataHex, 47);
	Out[2] = (B47 >= 0) ? B47 : -1;
	OutSpecialFlag = Row.SpecialFlag; // +44
	OutOptionMask  = Row.OptionMask;  // +68 — 19-битная маска подтипов для `Item_IsSowelCompatible`
	return true;
}

// Эталон: `inventorySowelWin_BuildSowelList` — три байта-цели подряд, и на каждом одна и та же
//   лесенка проверок; `Item_IsSowelCompatible@0xD1E810` — бит `ItemSubType` в маске камня (0..18).
bool UAsdaSowelSubsystem::IsSowelCompatible(int32 SowelCode, int32 TargetItemCode, int32 TargetItemType) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	int32 T[3] = { -1, -1, -1 }, Special = 0, Mask = 0;
	if (!GetSowelTargets(SowelCode, T, Special, Mask)) { return false; }
	const FAsdaItemRecord* Rec = IT->GetItem(TargetItemCode);
	const int32 SubType = Rec ? Rec->ItemSubType : -1;

	auto MaskAllows = [Mask, SubType]() -> bool
	{
		for (int32 i = 0; i < 19; ++i) { if (((1 << i) & Mask) != 0 && SubType == i) { return true; } }
		return false;
	};

	for (int32 i = 0; i < 3; ++i)
	{
		const int32 Tt = T[i];
		if (Tt < 0) { continue; }
		if (Tt == TargetItemType)
		{
			// == 9 (оружие) особый: без `SpecialFlag` требуется совпадение по маске подтипов.
			if (Tt != kTargetNeedsMask || Special != 0) { return true; }
			// ★ 21.10.2026 (сверка 1:1 с `inventorySowelWin_BuildSowelList`): байт совпал с типом предмета, а маска
			//   не пустила — эталон на этом ЗАКАНЧИВАЕТ разбор камня (`if (compat) push;` и выход на LABEL_4),
			//   следующие байты-цели уже не смотрит. Прежде порт шёл к следующему байту (`continue`). По живым
			//   данным `sowell_list` разницы нет (второй/третий байт у таких камней = 15, «нет цели»), но правило — эталонное.
			return MaskAllows();
		}
		if (Tt == kTargetAny) { return true; }
		// ★ Эталон в третьем байте ветку 13 пишет БЕЗ проверки `!= 9` (там только `!= 8 && != 9`
		//   у первых двух) — воспроизводим лесенку 1:1, различия сохранены.
		if (Tt == kTargetExcept89 && TargetItemType != 8 && TargetItemType != 9) { return true; }
		if (Tt == kTargetExcept9  && TargetItemType != 9) { return true; }
	}
	return false;
}

// ═══════════════════════════════ СПИСОК ════════════════════════════════════════════════════════

void UAsdaSowelSubsystem::RebuildList()
{
	SowelList.Reset();
	ListPage = 0; CachedPage = -1;
	SelStoneIndex = -1; SelStoneCol = -1;

	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* I = Inv();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* Target = TargetItem();
	if (!I || !IT || !Target || Target->IsEmpty()) { ClearBranch(); return; }

	const FAsdaItemRecord* TRec = IT->GetItem(Target->ItemCode);
	if (!TRec) { ClearBranch(); return; }

	// ★ Эталон: у предмета с `durationOrEnchant != 0` список НЕ строится вовсе — шесть ячеек
	//   просто гасятся (item_inventory.c:36313-36321).
	if (TRec->DurationOrEnchant != 0)
	{
		if (bControlsReady) { for (int32 i = 0; i < kTrayCount; ++i) { ClearIcon(TrayIcon[i]); } }
		RedrawSockets();
		return;
	}
	// ★ Удочка камней не принимает — эталон обнуляет запись камня до фильтра.
	const bool bRod = (TRec->ItemSubType == kRodSubType && TRec->ItemVariant == kRodVariant);

	const int32 SpendSize = I->GetContainerSize(2);
	int32 Ordinal = 0;
	for (int32 Cell = 0; Cell < SpendSize; ++Cell)
	{
		const FAsdaItemInstance* It = I->GetCell(2, Cell);
		if (!It || It->IsEmpty()) { continue; }
		if (!IT->IsSowelCode(It->ItemCode)) { continue; }
		if (bRod) { continue; }
		if (!IsSowelCompatible(It->ItemCode, TRec->ItemCode, TRec->ItemType)) { continue; }
		FAsdaSowelListEntry E;
		E.Code = It->ItemCode;
		E.SpendCell = Cell;
		E.Ordinal = ++Ordinal;
		FAsdaSowellRow Row;
		if (IT->GetSowell(It->ItemCode, Row)) { E.IconPath = Row.IconPath; }
		SowelList.Add(E);
	}

	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] список пересобран: предмет код=%d тип=%d подтип=%d гнёзд=%d → подходящих камней %d"),
		TRec->ItemCode, TRec->ItemType, TRec->ItemSubType, TRec->EnchantOrSocket + 1, SowelList.Num());

	SetListPage(0);
	RedrawSockets();
}

int32 UAsdaSowelSubsystem::GetListPageCount() const
{
	if (SowelList.Num() <= 0) { return 0; }
	// Эталон: `count / 6`, и «+1», если остаток > 0.
	int32 Pages = SowelList.Num() / kTrayCount;
	if (SowelList.Num() % kTrayCount > 0) { ++Pages; }
	return FMath::Max(1, Pages);
}

void UAsdaSowelSubsystem::SetListPage(int32 Page)
{
	const int32 Pages = GetListPageCount();
	ListPage = FMath::Clamp(Page, 0, FMath::Max(0, Pages - 1));
	// Эталон `uiSowelGridSetPage`: перерисовывает только при СМЕНЕ страницы и гасит курсор списка.
	if (CachedPage == ListPage) { return; }
	CachedPage = ListPage;
	RedrawList();
	SetVis(TraySel, false);
}

void UAsdaSowelSubsystem::ScrollListBy(int32 Delta)
{
	if (Delta == 0) { return; }
	SetListPage(ListPage + Delta);
}

void UAsdaSowelSubsystem::RedrawList()
{
	if (!bControlsReady) { return; }
	const int32 Start = ListPage * kTrayCount;
	for (int32 Col = 0; Col < kTrayCount; ++Col)
	{
		const int32 Idx = Start + Col;
		if (SowelList.IsValidIndex(Idx) && SowelList[Idx].Code > 0)
		{
			if (!ApplyIcon(TrayIcon[Col], SowelList[Idx].Code)) { ClearIcon(TrayIcon[Col]); }
		}
		else
		{
			ClearIcon(TrayIcon[Col]);
		}
		if (TrayText[Col]) { TrayText[Col]->SetText(FText::GetEmpty()); }   // счётчика у камней нет
	}
}

bool UAsdaSowelSubsystem::SelectStoneByColumn(int32 Column)
{
	if (Column < 0 || Column >= kTrayCount) { return false; }
	const int32 Idx = Column + kTrayCount * ListPage;
	// Эталон: выбор проходит, только если в этой позиции ЕСТЬ камень (`*stl_vectorAt(...) > 0`).
	if (!SowelList.IsValidIndex(Idx) || SowelList[Idx].Code <= 0) { return false; }
	Pick = EAsdaSowelPick::Stone;      // m_nSelKind = 2
	SelStoneIndex = Idx;
	SelStoneCol = Column;
	SelSocket = -1;
	PlaceTrayCursor();
	SetVis(SocketSel, false);          // `uiHideControlIfVisible` — курсор ГНЕЗДА гаснет
	return true;
}

// Эталон `uiSelSlot_PositionCursorControl@0xD02AB0`: курсор 436 в (x − 2, y) ячейки списка.
void UAsdaSowelSubsystem::PlaceTrayCursor()
{
	if (!TraySel || SelStoneCol < 0 || SelStoneCol >= kTrayCount) { SetVis(TraySel, false); return; }
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(TraySel))
	{
		const FVector2D P = TrayIconRoot[SelStoneCol] - TraySelParentRoot;
		FMargin O = CS->GetOffsets();
		O.Left = P.X - 2.f; O.Top = P.Y;
		CS->SetOffsets(O);
	}
	TraySel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	TraySel->RestartAnim();
}

// ═══════════════════════════════ ГНЁЗДА ════════════════════════════════════════════════════════

int32 UAsdaSowelSubsystem::GetSocketCode(int32 SocketIndex) const
{
	if (SocketIndex < 0 || SocketIndex >= kSocketCount) { return -1; }
	const FAsdaItemInstance* It = TargetItem();
	if (!It || It->Raw.Num() < kSocketOff0 + kSocketStride * kSocketCount) { return -1; }
	return SwRdI16(It->Raw.GetData() + kSocketOff0 + kSocketStride * SocketIndex);
}

int32 UAsdaSowelSubsystem::GetSocketCount() const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* It = TargetItem();
	if (!IT || !It || It->IsEmpty()) { return 0; }
	const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
	return Rec ? (Rec->EnchantOrSocket + 1) : 0;
}

// Часть `ItemTooltip_PopulateInfoPanel@0xD02C00` (item_inventory.c:37324-37387), воспроизведена 1:1.
void UAsdaSowelSubsystem::RedrawSockets()
{
	if (!bControlsReady) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemInstance* It = TargetItem();
	const FAsdaItemRecord* Rec = (IT && It && !It->IsEmpty()) ? IT->GetItem(It->ItemCode) : nullptr;
	if (!Rec)
	{
		for (int32 i = 0; i < kSocketCount; ++i) { ClearIcon(SocketIcon[i]); }
		SetVis(Shade1, false); SetVis(Shade2, false); SetVis(Shade3, false);
		SetEmptyBackVisible(false);
		SetVis(SocketSel, false);
		return;
	}

	const int32 Count = Rec->EnchantOrSocket + 1;
	const bool bRod = (Rec->ItemSubType == kRodSubType && Rec->ItemVariant == kRodVariant);

	if (Count >= 1 && Count <= 4)
	{
		for (int32 i = 0; i < kSocketCount; ++i)
		{
			const int32 Code = GetSocketCode(i);
			if (Code <= 0) { ClearIcon(SocketIcon[i]); }
			else if (!ApplyIcon(SocketIcon[i], Code)) { ClearIcon(SocketIcon[i]); }
		}
	}
	else
	{
		for (int32 i = 0; i < kSocketCount; ++i) { ClearIcon(SocketIcon[i]); }
	}
	ApplyShades(Count, bRod);
	PlaceSocketCursor();
}

void UAsdaSowelSubsystem::ApplyShades(int32 SocketCountAvailable, bool bIsRod)
{
	// Порядок эталона: сперва 499 показан, три шторки скрыты; затем switch правит.
	bool bEmptyBack = true, bS1 = false, bS2 = false, bS3 = false;
	switch (SocketCountAvailable)
	{
	case 1: bS1 = true;  bS2 = true;  bS3 = true;  bEmptyBack = false; break;   // свободно гнездо 0
	case 2: bS1 = false; bS2 = true;  bS3 = true;  bEmptyBack = false; break;   // 0 и 1
	case 3: bS1 = false; bS2 = false; bS3 = true;  bEmptyBack = false; break;   // 0,1,2 (заперто 3)
	case 4: bS1 = false; bS2 = false; bS3 = false; bEmptyBack = false; break;   // все четыре
	default: break;                                                             // 0 или ≥5 → только 499
	}
	if (bIsRod) { bEmptyBack = true; }   // эталон: удочке принудительно показывает 499
	SetVis(Shade1, bS1);
	SetVis(Shade2, bS2);
	SetVis(Shade3, bS3);
	SetEmptyBackVisible(bEmptyBack);
}

bool UAsdaSowelSubsystem::SelectSocket(int32 SocketIndex)
{
	if (SocketIndex < 0 || SocketIndex >= kSocketCount) { return false; }
	// Эталон `CInventoryUI_OnSlotClick`, ветка 0x1B9…0x1BC: выбор проходит ТОЛЬКО если в гнезде
	//   действительно лежит камень (`statValue > 0`).
	if (GetSocketCode(SocketIndex) <= 0) { return false; }
	SelSocket = SocketIndex;
	Pick = EAsdaSowelPick::Socket;     // m_nSelKind = 3
	PlaceSocketCursor();
	SetVis(TraySel, false);            // `uiHideControlIfVisible_2B30` — курсор СПИСКА гаснет
	return true;
}

// Эталон `uiTargetSlotShow@0xD029E0`: курсор 445 ставится на выбранное гнездо.
void UAsdaSowelSubsystem::PlaceSocketCursor()
{
	if (!SocketSel || SelSocket < 0 || SelSocket >= kSocketCount || Pick != EAsdaSowelPick::Socket)
	{
		SetVis(SocketSel, false);
		return;
	}
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(SocketSel))
	{
		const FVector2D P = SocketIconRoot[SelSocket] - SocketSelParentRoot;
		FMargin O = CS->GetOffsets();
		O.Left = P.X; O.Top = P.Y;
		CS->SetOffsets(O);
	}
	SocketSel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SocketSel->RestartAnim();
}

// ═══════════════════════════════ ВЫБОР ГНЕЗДА ДЛЯ ВСТАВКИ ══════════════════════════════════════
//   ЭТАЛОН, `itemSowelSocketRequest@0xD090B0`:
//     • `SpecialFlag == 0` → гнездо ВСЕГДА 0; в пакет идёт код камня, СТОЯЩЕГО в гнезде 0
//       (замена: сервер вернёт старый камень в контейнер 2);
//     • `SpecialFlag != 0` → `slotFindFirstEmpty(EnchantOrSocket, запись, &idx)` ищет первое
//       ПУСТОЕ гнездо среди 1..EnchantOrSocket; нашли → шлём с ним и «старый код = 0»;
//       не нашли → эталон открывает окно камней и НИЧЕГО не шлёт.
bool UAsdaSowelSubsystem::ResolveTargetSocket(int32 SowelCode, int32& OutSocket) const
{
	OutSocket = 0;
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return false; }
	FAsdaSowellRow Row;
	if (!IT->GetSowell(SowelCode, Row)) { return false; }
	if (Row.SpecialFlag == 0) { OutSocket = 0; return true; }

	const FAsdaItemInstance* It = TargetItem();
	const FAsdaItemRecord* Rec = (It && !It->IsEmpty()) ? IT->GetItem(It->ItemCode) : nullptr;
	if (!Rec) { return false; }
	for (int32 i = 1; i < Rec->EnchantOrSocket + 1; ++i)
	{
		if (GetSocketCode(i) <= 0) { OutSocket = i; return true; }
	}
	return false;
}

// ═══════════════════════════════ ДЕЙСТВИЯ ══════════════════════════════════════════════════════

bool UAsdaSowelSubsystem::RequestSocketInsert()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaInventorySubsystem* I = Inv();
	if (!Net || !IT || !I) { return false; }
	// ★ 21.10.2026: первый гейт эталона `itemSowelSocketRequest@0xD090B0` — «персонаж занят» → нотис 2314 в чат.
	if (RefuseIfLocalPlayerBusy()) { return false; }
	if (bAwaitingResult) { UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] ждём предыдущий ответ — пропуск")); return false; }
	if (Pick != EAsdaSowelPick::Stone || !SowelList.IsValidIndex(SelStoneIndex)) { return false; }

	const FAsdaItemInstance* Target = TargetItem();
	if (!Target || Target->IsEmpty()) { return false; }
	const FAsdaSowelListEntry& E = SowelList[SelStoneIndex];
	const FAsdaItemInstance* Stone = I->GetCell(2, E.SpendCell);
	if (!Stone || Stone->IsEmpty()) { return false; }
	const FAsdaItemRecord* TRec = IT->GetItem(Target->ItemCode);
	if (!TRec) { return false; }

	// Локальная проверка совместимости (эталон делает её ДО отправки и печатает нотис 20068).
	if (CVarSowelLegacyNoPreCheck.GetValueOnGameThread() == 0 &&
		!IsSowelCompatible(Stone->ItemCode, TRec->ItemCode, TRec->ItemType))
	{
		const FString Txt = LocSlots(kLocIncompatible,
			{ SowelName(Stone->ItemCode), IT->GetItemName(TRec->ItemCode) });
		ChatNotice(Txt, AsdaNoticeColor::Gold);   // ★ 21.10.2026: эталон item_inventory.c:38492 — −11514
		return false;
	}

	int32 Socket = 0;
	if (!ResolveTargetSocket(Stone->ItemCode, Socket))
	{
		// Свободных гнёзд нет: эталон открывает окно выбора `SOWELL_SELBOX` (1530) и НЕ шлёт пакет.
		//   Пакет уйдёт позже — когда игрок ткнёт заменяемое гнездо и нажмёт «OK»
		//   (обратный ход: `HandleSelBoxAccepted` → `RequestSocketInsertAt`).
		UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] свободных гнёзд нет — эталон показывает окно камней, пакет не шлём"));
		OnNeedSowelStonesWindow.Broadcast(Target->Raw, TRec->EnchantOrSocket);
		return false;
	}
	return SendSocketInsert(Socket);
}

// ★ Вход из окна выбора гнезда (1530). Проверки те же, что и у авто-пути, — их делает
//   `SendSocketInsert`; здесь только валидируем номер гнезда, пришедший из UI.
bool UAsdaSowelSubsystem::RequestSocketInsertAt(int32 SocketIndex)
{
	if (SocketIndex < 0 || SocketIndex >= kSocketCount)
	{
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] окно выбора вернуло гнездо %d — вне диапазона 0..%d"),
			SocketIndex, kSocketCount - 1);
		return false;
	}
	return SendSocketInsert(SocketIndex);
}

// Общее тело отправки 5024. Состояние (какой камень выбран, в каком контейнере носитель)
//   живёт здесь, поэтому оба входа собирают пакет в одном месте.
bool UAsdaSowelSubsystem::SendSocketInsert(int32 Socket)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaInventorySubsystem* I = Inv();
	if (!Net || !I) { return false; }
	if (bAwaitingResult) { UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] ждём предыдущий ответ — пропуск")); return false; }
	if (!SowelList.IsValidIndex(SelStoneIndex)) { return false; }

	const FAsdaItemInstance* Target = TargetItem();
	if (!Target || Target->IsEmpty()) { return false; }
	const FAsdaItemInstance* Stone = I->GetCell(2, SowelList[SelStoneIndex].SpendCell);
	if (!Stone || Stone->IsEmpty()) { return false; }

	// Код камня, стоящего в этом гнезде сейчас: эталон читает `запись + 16 + 2*гнездо`.
	//   Для авто-пути это ненулевое значение бывает только у гнезда 0 (единственная замена),
	//   для пути через окно 1530 — у любого выбранного гнезда, оно там всегда занято.
	const int32 OldCode = FMath::Max(0, GetSocketCode(Socket));

	const bool bOk = Net->SendSowelSocket5024(
		(uint8)TargetContainer, Target->SlotIndex, (uint8)Socket,
		OldCode, Stone->ItemCode, 2, Stone->SlotIndex);
	if (bOk) { bAwaitingResult = true; SentAtS = FPlatformTime::Seconds(); }
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] 5024: гнездо %d, старый камень %d, новый %d — %s"),
		Socket, OldCode, Stone->ItemCode, bOk ? TEXT("отправлено") : TEXT("ОТКАЗ отправки"));
	return bOk;
}

// «OK» окна `SOWELL_SELBOX` (1530): подсистема тултипа знает только номер гнезда.
void UAsdaSowelSubsystem::HandleSelBoxAccepted(int32 SocketIndex)
{
	RequestSocketInsertAt(SocketIndex);
}

bool UAsdaSowelSubsystem::RequestSocketDetach()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	// ★ 21.10.2026: первый гейт эталона `invItemDropSendPacket@0xD06BA0` (и `netSendItemActionRequest@0xD1CE30`) —
	//   «персонаж занят» (`objStateAllowsRender`) → нотис 2314 в системный чат, пакет не шлётся.
	if (RefuseIfLocalPlayerBusy()) { return false; }
	if (bAwaitingResult) { return false; }
	if (Pick != EAsdaSowelPick::Socket || SelSocket < 0) { return false; }
	// ★★★ 21.10.2026 — ГНЕЗДО 0 ВЫНИМАЕТСЯ (жалоба игроков «нельзя вынуть главный камень»). Здесь стоял запрет
	//   «эталон гнездо 0 вынимать не даёт (`if (invCtx[200])`)» — ЛОЖНОЕ чтение: в `invItemDropSendPacket@0xD06BA0`
	//   эта развилка выбирает лишь, КАК отправить: гнездо ≠ 0 — встроенной копией, гнездо 0 —
	//   `netSendItemActionRequest@0xD1CE30`, которая шлёт тот же 5022 (OP_CHAR_CELLOB_ITEM_DETACH, `push 0x139E`) с
	//   байтом гнезда `*(this+800)` = 0. Второй метод — дизасм эталонной сборки (0xD06C74 `cmp [eax+0x320],0` /
	//   `jne 0xD06CAF`; ветка нуля 0xD06CA5 `call 0x92245C` = `jmp 0xD1CE30`). Сервер `CharCellobItemDetach@0x4DBC30`
	//   гнездо 0 принимает. Общее условие обеих веток эталона — в гнезде ЕСТЬ камень (`*(i16*)(запись+16+2·гнездо) > 0`).
	if (GetSocketCode(SelSocket) <= 0) { return false; }
	const FAsdaItemInstance* Target = TargetItem();
	if (!Target || Target->IsEmpty()) { return false; }

	const bool bOk = Net->SendSowelDetach5022((uint8)TargetContainer, Target->SlotIndex, (uint8)SelSocket);
	if (bOk) { bAwaitingResult = true; SentAtS = FPlatformTime::Seconds(); }
	return bOk;
}

// ═══════════════════════════════ «СВЯТОСТЬ» (5031) ═════════════════════════════════════════════

bool UAsdaSowelSubsystem::OnSpendCellUse(int32 Cell)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* I = Inv();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!I || !IT) { return false; }
	const FAsdaItemInstance* It = I->GetCell(2, Cell);
	if (!It || It->IsEmpty()) { return false; }

	// (а) Клик по КАМНЮ при взведённом режиме → 5031.
	if (IT->IsSowelCode(It->ItemCode))
	{
		if (!bHolyArmed) { return false; }
		return RequestHolyOnSowel(Cell);
	}

	const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
	if (!Rec) { return false; }

	// (б) «Святой» предмет. Эталон `CInventory_OnSlotUse` (item_inventory.c:37905-37927):
	//     `durationOrEnchant <= 0` И подтип ∈ [0x2C..0x35] ∪ {8, 9, 12} → переключить режим.
	//     ★ Сервер дополнительно требует `ItemType == 11` (`CharSpendHolyItemUse`: `itemTemplate+26 == 11`)
	//       — клиент оригинала эту проверку не делает и позволяет взвести режим «вхолостую».
	const bool bHolyShape = Rec->DurationOrEnchant <= 0 &&
		((Rec->ItemSubType >= 0x2C && Rec->ItemSubType <= 0x35) ||
		  Rec->ItemSubType == 9 || Rec->ItemSubType == 8 || Rec->ItemSubType == 12);
	if (bHolyShape)
	{
		if (bHolyArmed)
		{
			CancelHolyMode(false);
		}
		else
		{
			bHolyArmed = true;
			HolyCell = Cell;
			SetCursorMode(1);   // курсор «bottle» (эталон `registry_FindEntryByName("bottle")`)
			UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] режим «святости» взведён: предмет код=%d тип=%d подтип=%d ячейка=%d"),
				It->ItemCode, Rec->ItemType, Rec->ItemSubType, Cell);
			if (Rec->ItemType != 11)
			{
				UE_LOG(LogAsdaSowel, Warning,
					TEXT("[самоцветы] у предмета ItemType=%d, а сервер ждёт 11 — 5031 вернёт отказ (дефект оригинала: клиент тип не проверяет)"),
					Rec->ItemType);
			}
		}
		return true;
	}

	// (в) Взведён режим, но кликнули НЕ по камню → снять режим + нотис 2016 (эталон, ветка
	//     `equipItemCode < g_SowelCodeRangeLo`).
	if (bHolyArmed) { CancelHolyMode(true); return true; }
	return false;
}

bool UAsdaSowelSubsystem::RequestHolyOnSowel(int32 SowelCell)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaInventorySubsystem* I = Inv();
	if (!Net || !I || !bHolyArmed || HolyCell < 0) { return false; }
	if (bAwaitingResult) { return false; }
	const FAsdaItemInstance* Holy  = I->GetCell(2, HolyCell);
	const FAsdaItemInstance* Stone = I->GetCell(2, SowelCell);
	if (!Holy || Holy->IsEmpty() || !Stone || Stone->IsEmpty()) { return false; }

	// Эталон перед отправкой возвращает курсор в «nomal» и запоминает ячейку камня (для эффекта 498).
	SetCursorMode(0);
	HolySowelCell = SowelCell;
	const bool bOk = Net->SendHolyItemUse5031(2, Holy->SlotIndex, 2, Stone->SlotIndex);
	if (bOk) { bAwaitingResult = true; SentAtS = FPlatformTime::Seconds(); }
	return bOk;
}

void UAsdaSowelSubsystem::CancelHolyMode(bool bNotice)
{
	if (!bHolyArmed) { return; }
	bHolyArmed = false;
	HolyCell = -1;
	SetCursorMode(0);
	if (bNotice) { MsgBox1(LocRaw(kLocNotSowel)); }
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] режим «святости» снят%s"), bNotice ? TEXT(" (нотис 2016)") : TEXT(""));
}

// ═══════════════════════════════ АНИМАЦИИ 498 / 10009 ══════════════════════════════════════════

// Эталон `sowelWndOnSocketResultPacket`: ячейка камня приводится к КОЛОНКЕ видимой сетки
//   (`slot − 6 * страница`), эффект ставится в её позицию минус смещение центрирования.
void UAsdaSowelSubsystem::ShowHolyEffectOverCell(int32 SpendCell)
{
	if (!bControlsReady || !HolyEffect) { return; }
	UAsdaInventorySubsystem* I = Inv();
	if (!I) { return; }
	const int32 Col = SpendCell - UAsdaInventorySubsystem::kGridCols * I->GetScrollRow();
	if (Col < 0 || Col >= UAsdaInventorySubsystem::kGridCells) { return; }
	const UWidget* Cell = AsdaUi::FindNodeByName(HostWidget, TEXT(""), *FString::Printf(TEXT("INVEN_SET%d_"), Col));
	if (!Cell) { return; }
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(HolyEffect))
	{
		const FVector2D P = AsdaUi::CanvasRootPos(Cell) - AsdaUi::CanvasRootPos(HolyEffect->GetParent()) - HolyCenterOff;
		FMargin O = CS->GetOffsets();
		O.Left = P.X; O.Top = P.Y;
		CS->SetOffsets(O);
	}
	HolyEffect->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	HolyEffect->RestartAnim();
	SetCursorMode(2);      // курсор «hammer» — так делает эталон
	PlayUiSound(HolyEffectSoundId);
}

void UAsdaSowelSubsystem::ShowSoulStoneEffect(int32 SpendCell)
{
	if (!bControlsReady || !SoulStoneEffect) { return; }
	UAsdaInventorySubsystem* I = Inv();
	if (!I) { return; }
	const int32 Col = SpendCell - UAsdaInventorySubsystem::kGridCols * I->GetScrollRow();
	if (Col < 0 || Col >= UAsdaInventorySubsystem::kGridCells) { return; }
	const UWidget* Cell = AsdaUi::FindNodeByName(HostWidget, TEXT(""), *FString::Printf(TEXT("INVEN_SET%d_"), Col));
	if (!Cell) { return; }
	if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(SoulStoneEffect))
	{
		const FVector2D P = AsdaUi::CanvasRootPos(Cell) - AsdaUi::CanvasRootPos(SoulStoneEffect->GetParent()) - SoulCenterOff;
		FMargin O = CS->GetOffsets();
		O.Left = P.X; O.Top = P.Y;
		CS->SetOffsets(O);
	}
	SoulStoneEffect->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	SoulStoneEffect->RestartAnim();
}

void UAsdaSowelSubsystem::HideEffects()
{
	SetVis(HolyEffect, false);
	SetVis(SoulStoneEffect, false);
}

// ═══════════════════════════════ ВВОД ══════════════════════════════════════════════════════════

// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget`.
bool UAsdaSowelSubsystem::IsCursorOver(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

int32 UAsdaSowelSubsystem::HitTestTray() const
{
	for (int32 i = 0; i < kTrayCount; ++i) { if (IsCursorOver(TrayIcon[i])) { return i; } }
	return -1;
}

int32 UAsdaSowelSubsystem::HitTestSocket() const
{
	for (int32 i = 0; i < kSocketCount; ++i) { if (IsCursorOver(SocketIcon[i])) { return i; } }
	return -1;
}

// Эталонные события процедуры инвентаря (`CGameWindow_OnCommand`, ui_core.c:47600-48010):
//   0xB «нажатие» → `CInventoryUI_OnSlotClick` (430…435: `m_nSelKind = 2`, только если в позиции есть камень;
//        441…444: `m_nSelKind = 3`, только если в гнезде есть камень) — результат кладётся в `self[202]`;
//   7   «отпускание» → при `self[202]`: гейты гачи 13106 / склада 10309, затем `inventoryWin_OnUiCommand`
//        (`m_nSelKind == 2` + цель 441…444 → `itemSowelSocketRequest`; `m_nSelKind == 3` + цель 430…435 или
//        ячейка сетки 10010…10039 → `invItemDropSendPacket`) — item_inventory.c:41745-41757;
//   0xD «двойной щелчок» (WM_LBUTTONDBLCLK) → при `self[202]` те же гейты, затем `CInventory_OnSlotUse`:
//        `m_nSelKind == 2` → `itemSowelSocketRequest` (вставка/замена по правилу гнезда), `m_nSelKind == 3` →
//        `invItemDropSendPacket` (извлечение ИМЕННО этого гнезда) — item_inventory.c:37796-37816.
//   ★★★ 21.10.2026 (жалоба игроков «нельзя вынуть/сменить камень, двойной клик не работает»): прежний опрос знал
//   только нажатие/отпускание — двойного щелчка не было вовсе, переноса не было видно (камень «не брался в руку»).
//   Теперь: взвод переноса общей точкой `FAsdaDragArm` (порог порта), летящая иконка `FAsdaDragGhost` и тусклый
//   источник — РОВНО как у ячеек инвентаря (`UAsdaInventorySubsystem::BeginDrag`, те же прозрачности и смещение);
//   отпускание без переноса над тем же узлом = щелчок, второй щелчок подряд (`FAsdaDblClick`, порог Windows) = 0xD.
//   ⚠ Гейты гачи/склада эталон проверяет на КАЖДОМ принятом отпускании (и печатает нотис даже на простом щелчке);
//     здесь — только там, где отпускание действительно ведёт к пакету (бросок на цель, двойной щелчок): нотис
//     «нельзя» на простом выборе камня игроку ничего не говорит. Отступление записано в отчёт волны.
void UAsdaSowelSubsystem::TickPointer()
{
	UAsdaInventorySubsystem* I = Inv();
	if (!I || !I->IsPanelExpanded())
	{
		// Панель свернули посреди переноса — перенос отменяется (бросать камень некуда, призрак не должен висеть).
		if (DragGhost.IsActive() || DragArm.IsPressed()) { EndStoneDragVisual(); DragArm.Disarm(); }
		return;
	}
	if (!FSlateApplication::IsInitialized()) { return; }
	// ★ Аудит №3 (B3): опрос ЛКМ и фронты — общая точка, ровно на месте прежнего опроса.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Взвод сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия сошлись
	//   бы в «бросок» камня, которого игрок не делал (вставка/извлечение уходят пакетом).
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		EndStoneDragVisual();
		DragArm.Disarm();
		Dbl.Reset();
		Mouse.Reset();
		return;
	}

	// Полоса колёсного окна 495 — свои планы, свой ввод (кнопки ↑/↓, дорожка, бегунок). Нажатие на неё
	//   ячеек не выбирает и перенос не взводит.
	if (TickListBar()) { return; }

	const int32 OverTray = HitTestTray();
	const int32 OverSocket = HitTestSocket();

	// ★★★ Волна 9 заточки (С-1): контролы 430…444 — процедура инвентаря (`CGameWindow_OnCommand`, группа 4);
	//   общая блокировка `uiCheckRecentActivity` глушит у неё выбор (0xB), бросок (7) и двойной щелчок (0xD) —
	//   ui_core.c:47932/:47784/:47990. Нажатие не принимается, отпускание ничего не делает; взвод снимаем, чтобы
	//   нажатие ДО блокировки и отпускание ПОСЛЕ неё не сложились в бросок, которого эталон не принял бы.
	if (AsdaUi::CheckRecentActivity(GetGameInstance()))
	{
		if (Mouse.bLmbDown || Mouse.bLmbUp) { EndStoneDragVisual(); DragArm.Disarm(); Dbl.Reset(); }
		return;
	}

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;

	if (Mouse.bLmbDown)
	{
		EndStoneDragVisual();
		DragArm.Disarm();
		// 0xB: выбор проходит ТОЛЬКО по непустой позиции/гнезду — это и есть `self[202]` эталона.
		if (OverTray >= 0 && SelectStoneByColumn(OverTray))
		{
			DragArm.Arm(/*KeyA вид*/1, /*KeyB колонка*/OverTray, Mouse.Cursor);
		}
		else if (OverSocket >= 0 && SelectSocket(OverSocket))
		{
			DragArm.Arm(/*KeyA вид*/2, /*KeyB гнездо*/OverSocket, Mouse.Cursor);
		}
		else
		{
			// ★ 21.10.2026 (ревизия, МЕЛОЧЬ-1): ЛЮБОЕ нажатие, не принятое камнем/гнездом (пустая позиция, фон, сетка,
			//   другое окно, мир), рвёт пару щелчков. Эталонное 0xD — это WM_LBUTTONDBLCLK: Windows выдаёт его, только если
			//   ОБА нажатия пришли подряд в одну точку; любое нажатие между ними — уже новое первое. Прежде пару рвало только
			//   нажатие по пустой позиции, и «камень → щелчок мимо → тот же камень» за < 0,5 с слал 5024.
			Dbl.Reset();
		}
	}
	else if (Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		// Порог пройден при зажатой ЛКМ — камень «взят в руку».
		BeginStoneDrag(DragArm.KeyA, DragArm.KeyB);
		DragArm.MarkStarted();
		Dbl.Reset();
	}

	if (DragGhost.IsActive()) { DragGhost.Tick(World); }

	if (Mouse.bLmbUp)
	{
		const int32 Kind = DragArm.KeyA;
		const int32 Idx  = DragArm.KeyB;
		const bool bDragged = DragArm.bStarted;
		EndStoneDragVisual();
		DragArm.Disarm();
		if (Kind <= 0) { return; }   // нажатие не было принято (`self[202] == 0`) — эталон отпускание не разбирает

		if (bDragged)
		{
			// ── 7: бросок ──
			if (Kind == 1 && Pick == EAsdaSowelPick::Stone && OverSocket >= 0)
			{
				// Бросок камня на ЛЮБОЕ гнездо: номер гнезда всё равно выберет эталонное правило.
				if (PassInventoryActionGates()) { RequestSocketInsert(); }
			}
			else if (Kind == 2 && Pick == EAsdaSowelPick::Socket && Idx == SelSocket
			         && (OverTray >= 0 || I->IsCursorOverGridCell()))
			{
				// Бросок камня ИЗ ГНЕЗДА на список камней или на ячейку сетки инвентаря.
				if (PassInventoryActionGates()) { RequestSocketDetach(); }
			}
			return;
		}

		// ── щелчок: отпустили над тем же узлом, порог не пройден ──
		const bool bSameNode = (Kind == 1) ? (OverTray == Idx) : (OverSocket == Idx);
		if (!bSameNode) { Dbl.Reset(); return; }
		if (!Dbl.Click(Kind, Idx)) { return; }   // первый щелчок — только выбор (0xB уже выполнен на нажатии)
		// ── 0xD: двойной щелчок ──
		if (!PassInventoryActionGates()) { return; }
		if (Kind == 1 && Pick == EAsdaSowelPick::Stone)
		{
			UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] двойной щелчок по камню списка (колонка %d) → вставка по правилу гнезда"), Idx);
			RequestSocketInsert();
		}
		else if (Kind == 2 && Pick == EAsdaSowelPick::Socket && Idx == SelSocket)
		{
			UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] двойной щелчок по гнезду %d → извлечение"), Idx);
			RequestSocketDetach();
		}
	}
	// ★ Аудит №3 (B3): запись латча делает `Mouse.Update()` в начале функции.
}

// ★ 21.10.2026: вид переноса — общая точка порта и ТЕ ЖЕ параметры, что у ячеек инвентаря (берём их из подсистемы
//   инвентаря, чтобы правка в редакторе меняла оба окна разом): призрак 0.4, источник 0.4, смещение (−16,−16).
//   В оригинале летящую иконку рисует движок (слой переноса), своего контрола у неё нет.
void UAsdaSowelSubsystem::BeginStoneDrag(int32 Kind, int32 Idx)
{
	UImage* Src = (Kind == 1) ? (TrayIcon.IsValidIndex(Idx) ? TrayIcon[Idx].Get() : nullptr)
	                          : (SocketIcon.IsValidIndex(Idx) ? SocketIcon[Idx].Get() : nullptr);
	UUserWidget* Host = HostWidget.Get();
	UCanvasPanel* Root = (Host && Host->WidgetTree) ? Cast<UCanvasPanel>(Host->WidgetTree->RootWidget) : nullptr;
	if (!Src || !Root) { return; }
	const UAsdaInventorySubsystem* I = Inv();
	const float GhostA  = I ? I->DragGhostOpacity  : 0.4f;
	const float SourceA = I ? I->DragSourceOpacity : 0.4f;
	const FVector2D Off = I ? I->DragGhostCursorOffset : FVector2D(-16.f, -16.f);
	// Подсказки камня на время переноса прячем (у инвентаря `BeginDrag` делает то же — `HideCellTooltip`).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::InfoSocket);
		Tip->HideTooltipFor(EAsdaTipOwner::Item);
	}
	DragSrcIcon = Src;
	Src->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, SourceA));
	DragGhost.Begin(Host, Root, Src->GetBrush(), FVector2D(IconDrawSize, IconDrawSize), GhostA, Off,
		TEXT("INVEN_SowelDragGhost"), GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] перенос: камень взят %s %d"), Kind == 1 ? TEXT("из списка, колонка") : TEXT("из гнезда"), Idx);
}

void UAsdaSowelSubsystem::EndStoneDragVisual()
{
	if (UImage* Src = DragSrcIcon.Get()) { Src->SetColorAndOpacity(FLinearColor::White); }
	DragSrcIcon = nullptr;
	if (DragGhost.IsActive()) { DragGhost.Cancel(); }
}

bool UAsdaSowelSubsystem::PassInventoryActionGates() const
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	// LABEL_233 (ui_core.c:47784…): `CObject_GetField254(g_pInventoryWnd)` — открыт барабан гачи 10050 → 13106.
	if (const UAsdaBoosterPanelSubsystem* Bo = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (Bo->IsWindowOpen()) { ChatNotice(LocRaw(13106), AsdaNoticeColor::Gold); return false; }
	}
	// LABEL_235: `CObject_GetField5581(g_pWarehouseWnd)` — активен удалённый склад (+0x5734) → 10309.
	if (const UAsdaWarehouseSubsystem* Wh = GI->GetSubsystem<UAsdaWarehouseSubsystem>())
	{
		if (Wh->IsRecallActive()) { ChatNotice(LocRaw(10309), AsdaNoticeColor::Gold); return false; }
	}
	return true;
}

bool UAsdaSowelSubsystem::RefuseIfLocalPlayerBusy() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaSystemOptionSubsystem* SO = GI ? GI->GetSubsystem<UAsdaSystemOptionSubsystem>() : nullptr;
	if (!SO || !SO->IsLocalPlayerBusy()) { return false; }
	ChatNotice(LocRaw(2314), AsdaNoticeColor::Gold);   // эталон: `CChat_AddTokenizedLines(…, -11514, 10000, 0)`
	UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] персонаж занят — нотис 2314, пакет не шлём"));
	return true;
}

// ★ 21.10.2026: колесо над списком (зовёт обработчик окна инвентаря). Курсор над ячейками 430…435 или над полосой
//   495 — колесо СЪЕДЕНО контролом (эталон: сообщение 0x17 колёсного окна), листаем, если страниц больше одной;
//   «от себя» (+1) — к меньшей странице, как у сетки инвентаря.
bool UAsdaSowelSubsystem::HandleListWheel(int32 Notches)
{
	if (!bControlsReady || Notches == 0) { return false; }
	const UAsdaInventorySubsystem* I = Inv();
	if (!I || !I->IsWindowOpen() || !I->IsPanelExpanded()) { return false; }
	const UWidget* Grp = AsdaUi::FindNodeByName(HostWidget, TEXT("INVEN_SOWTRAY_IconBase_grp"), TEXT("INVEN_SOWTRAY_IconBase"));
	if (!IsCursorOver(Grp) && !(bListBarShown && ListBar.IsCursorOver())) { return false; }
	if (SowelList.Num() > kTrayCount) { ScrollListBy(-Notches); }
	return true;
}

// ★ 21.10.2026: раскладка полосы 495. Начало — правый-верх rect'а колёсного окна (= ячейка 430 + ширина 98 в
//   пикселях .JUI), высота — его 65 px; масштаб — по живому шагу ячеек (окно таскается и масштабируется вместе с
//   канвасом, константы держим в пикселях .JUI). Диапазон — эталонный `SetScrollRange(1, страниц)`.
void UAsdaSowelSubsystem::LayoutListBar()
{
	if (!ListBar.IsBuilt()) { return; }
	const UAsdaInventorySubsystem* I = Inv();
	const UImage* Cell0 = TrayIcon.IsValidIndex(0) ? TrayIcon[0].Get() : nullptr;
	const bool bShow = bControlsReady && I && I->IsWindowOpen() && I->IsPanelExpanded() && Cell0;
	if (!bShow)
	{
		if (bListBarShown) { ListBar.Hide(); bListBarShown = false; ListBarKey = FVector4f(-1.f, -1.f, -1.f, -1.f); }
		return;
	}
	const FVector2D Base = AsdaUi::CanvasRootPos(Cell0);
	float Step = kTrayJuiStep;
	if (TrayIcon.IsValidIndex(1) && TrayIcon[1])
	{
		const float D = (float)(AsdaUi::CanvasRootPos(TrayIcon[1]) - Base).X;
		if (D > 1.f) { Step = D; }
	}
	const float S = Step / kTrayJuiStep;
	const int32 Pages = GetListPageCount();          // 0 — списка нет (эталон: `SetScrollRange(495, 1, 0)`)
	const int32 MaxRow = FMath::Max(0, Pages - 1);
	const FVector4f Key((float)Base.X, (float)Base.Y, (float)Pages, (float)ListPage);
	if (bListBarShown && Key == ListBarKey) { return; }
	ListBarKey = Key;
	bListBarShown = true;
	ListBar.Layout(Base + FVector2D(kWheelJuiW * S, 0.f), kWheelJuiH * S, S, /*RangeA2*/1, /*RangeA3*/Pages, MaxRow, ListPage);
}

bool UAsdaSowelSubsystem::TickListBar()
{
	if (!ListBar.IsBuilt() || !bListBarShown) { return false; }
	const bool bDownOnBar = Mouse.bLmbDown && ListBar.IsCursorOver();
	const int32 MaxRow = FMath::Max(0, GetListPageCount() - 1);
	int32 Value = 0;
	switch (ListBar.Tick(Mouse.bLmb, Mouse.bLmbDown, MaxRow, Value))
	{
	case EAsdaScrollAction::ScrollBy: ScrollListBy(Value); break;   // стрелка / «страница» = 1 страница (`SetScrollPage(495, 1, 1)`)
	case EAsdaScrollAction::SetRow:   SetListPage(Value);  break;   // бегунок
	default: break;
	}
	if (bDownOnBar) { EndStoneDragVisual(); DragArm.Disarm(); Dbl.Reset(); }
	// Пока бегунок схвачен, ячейки под курсором не разбираем (иначе отпускание над ними стало бы щелчком).
	return bDownOnBar || ListBar.bThumbGrab;
}

// ═══════════════════════════════ СЕТЬ: ПРИЁМ ═══════════════════════════════════════════════════

// ★ Аудит №3 (B10): замок и `Add` — внутри общей `TAsdaNetQueue`.
void UAsdaSowelSubsystem::OnSocketResult(const TArray<uint8>& B) { PendingSocket.Push(B); }
void UAsdaSowelSubsystem::OnDetachResult(const TArray<uint8>& B) { PendingDetach.Push(B); }
void UAsdaSowelSubsystem::OnHolyResult  (const TArray<uint8>& B) { PendingHoly.Push(B); }

void UAsdaSowelSubsystem::SimulateSocketResult(const TArray<uint8>& B) { ApplySocketResult(B); }
void UAsdaSowelSubsystem::SimulateDetachResult(const TArray<uint8>& B) { ApplyDetachResult(B); }
void UAsdaSowelSubsystem::SimulateHolyResult  (const TArray<uint8>& B) { ApplyHolyResult(B); }

// ★ 5025 — эталон `netRecvItemActionResult@0xD0FB10` + `invApplySlotSwapResult@0xD0FF10`.
void UAsdaSowelSubsystem::ApplySocketResult(const TArray<uint8>& B)
{
	bAwaitingResult = false;
	if (B.Num() < 117) { return; }
	UAsdaInventorySubsystem* I = Inv();
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const uint8 Res = B[0];

	// Коды отказа эталона (нотисы 1:1). Комментарий у каждого — расшифровка серверной ветки.
	static const TMap<uint8, int32> kFail = {
		{ 0,  2311 },   // общая ошибка / рассинхрон гнезда
		{ 2,  1215 },
		{ 4,  1216 },
		{ 5,  2045 },
		{ 6,  1217 },
		{ 7,  1218 },
		{ 8,  1219 },
		{ 9,  8013 },
		{ 10, 1220 },
		{ 11, 1221 },
		{ 15, 111303 },  // сервер: код камня в пакете не совпал с содержимым слота
	};
	if (Res == 3)
	{
		// Сервер: прочность камня кончилась — в @1 приходит ПРЕЖНЯЯ запись предмета.
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] 5025 result=3: прочность камня исчерпана, гнездо не изменено"));
		if (I) { TArray<uint8> R; R.Append(B.GetData() + 1, 57); I->ApplyExternalRecord(R); }
		RedrawSockets();
		return;
	}
	if (Res != 1)
	{
		const int32* Loc = kFail.Find(Res);
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] 5025 отказ, код %d (нотис %d)"), (int32)Res, Loc ? *Loc : -1);
		// ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игроков «клиент зависает, когда вставляешь Sowel выше своего уровня»): КАНАЛ
		//   отказа 1:1 с эталоном `netRecvItemActionResult@0xD0FB10` (network.c:17526-17578): окно-сообщение
		//   (`CItem_BuildTooltipText(…, 0, −1, …)`) — ТОЛЬКО у кода 0 (2311); коды 2/4…11/15 — СТРОКА В ЧАТ
		//   (`CChat_AddTokenizedLines(…, −11514, 10000, 0)`). Сервер отвечает кодом 10 на камень выше уровня персонажа
		//   (`CharCellobItemMove@0x4DC390` → `sub_4DE250` → `sub_4DE530@0x4DE530`: строка камня +32 > уровень). Прежде порт
		//   показывал на любой отказ МОДАЛЬНОЕ окно 1500: модальный гейт запирал ввод всего интерфейса и мира, а окно
		//   стояло под правой панелью инвентаря/окном 1530 — игрок видел «замёрзший» клиент. Ключ `asda.Sowel.RefusalToChat`
		//   (0 = прежнее поведение порта — окно на каждый отказ).
		if (Loc)
		{
			if (Res == 0 || CVarSowelRefusalToChat.GetValueOnGameThread() == 0) { MsgBox1(LocRaw(*Loc)); }
			else
			{
				// Строки 111303 (код 15) в таблицах игры нет ни в RU, ни в EN: у оригинала `UI_FormatLocalizedNotice` даёт
				//   пустую строку и в чате не появляется ничего — отладочное «[#id]» игроку не показываем, только журнал.
				const FString Txt = AsdaNotice::LocRaw(GetGameInstance(), *Loc, /*bDebugFallback*/ false);
				if (!Txt.IsEmpty()) { ChatNotice(Txt, AsdaNoticeColor::Gold); }
				else { UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] 5025 код %d: строки %d в таблицах нет — в чат ничего (как оригинал)"), (int32)Res, *Loc); }
			}
		}
		RedrawSockets();
		return;
	}

	// Успех. @1 — новая запись ПРЕДМЕТА (её +4 контейнер, +5 слот), @58 — вынутый камень,
	//   @115 — слот контейнера 2, который надо очистить, если запись @58 пуста.
	TArray<uint8> ItemRec; ItemRec.Append(B.GetData() + 1, 57);
	TArray<uint8> StoneRec; StoneRec.Append(B.GetData() + 58, 57);
	const int32 ClearedSlot = SwRdI16(B.GetData() + 115);
	const int32 StoneCode = SwRdI32(StoneRec.GetData());
	// ★ 21.10.2026 (ревизия, нотисы): [ss1] 20121 — имя ВСТАВЛЕННОГО камня. Эталон `invApplySlotSwapResult@0xD0FF10`
	//   (item_inventory.c:40365-40398) берёт его из ячейки контейнера 2 ДО обновления: при пустой записи @58 — ячейка
	//   @115, иначе — ячейка @63 (слот возвращённого камня, туда сервер кладёт старый на место вставленного).
	FString InsertedName;
	if (I)
	{
		const FAsdaItemInstance* Ins = I->GetCell(2, (StoneCode > 0) ? SwRdI16(B.GetData() + 63) : ClearedSlot);
		if (Ins && !Ins->IsEmpty()) { InsertedName = SowelName(Ins->ItemCode); }
	}

	if (I)
	{
		I->ApplyExternalRecord(ItemRec);
		if (StoneCode > 0)
		{
			I->ApplyExternalRecord(StoneRec);   // возвращённый камень лёг обратно в контейнер 2
		}
		else
		{
			// Камень израсходован — эталон `memset(g_InventoryArray2 + 57*slot, 0, 0x39)`.
			const TArray<uint8> Empty = FAsdaItemRecordWriter::ClearCell(2, ClearedSlot);   // amount = −1 ⇒ очистить (★ U17)
			I->ApplyExternalRecord(Empty);
		}
	}

	// Нотис 20121 «[ss1] помещается в предмет [ss2].»: слот 0 — вставленный камень (снят ДО обновления, см. выше),
	//   слот 1 — предмет @1; цвет −11514. ★ 21.10.2026: прежде строка шла через `%s`-подстановку — маркеров `[ssN]`
	//   она не знает, и в чат уходило «помещается в предмет .» без имён; в [ss1] стоял ВОЗВРАЩЁННЫЙ камень.
	if (IT)
	{
		const int32 ItemCode = SwRdI32(ItemRec.GetData());
		ChatNotice(LocSlots(kLocSocketDone, { InsertedName, IT->GetItemName(ItemCode) }), AsdaNoticeColor::Gold);
	}
	PlayUiSound(InsertSoundId);
	OnSowelGlowDirty.Broadcast();     // `itemSetupSowelGlow(pLocalPlayer)`

	Pick = EAsdaSowelPick::None;
	SelStoneIndex = -1; SelStoneCol = -1; SelSocket = -1;
	SetVis(TraySel, false); SetVis(SocketSel, false);
	RebuildList();
	RedrawSockets();
}

// ★ 5023 — эталон `netRecvSowelActionResult@0xD0F7D0`.
void UAsdaSowelSubsystem::ApplyDetachResult(const TArray<uint8>& B)
{
	bAwaitingResult = false;
	if (B.Num() < 115) { return; }
	UAsdaInventorySubsystem* I = Inv();
	const uint8 Res = B[0];

	if (Res == 1)
	{
		// @1 новая запись предмета, @58 запись вынутого камня (её +5 — слот контейнера 2).
		TArray<uint8> ItemRec;  ItemRec.Append(B.GetData() + 1, 57);
		TArray<uint8> StoneRec; StoneRec.Append(B.GetData() + 58, 57);
		// ★★ 21.10.2026 (ревизия, ВАЖНО-1): число нотиса 20120 «прочность предмета снизилась на [sd1]» — УБЫЛЬ ПРОЧНОСТИ.
		//   Эталон `netRecvSowelActionResult@0xD0F7D0` (network.c:17466-17473) считает её ДО `OnRecv_ItemUpdate`:
		//   `старая запись[+13] − новая запись[+13]` (новая +13 = байт @14 тела; контейнер — @5, ячейка — i16 @6;
		//   для надетого (3) старая — из надетого). Прежде порт печатал сам байт @14 — ПРОЧНОСТЬ ПОСЛЕ извлечения.
		int32 DurLoss = 0;
		{
			const int32 Cont = (int32)B[5];
			const int32 Slot = SwRdI16(B.GetData() + 6);
			const FAsdaItemInstance* Old = I ? I->GetCell(Cont, Slot) : nullptr;
			if (Old && Old->Raw.Num() > 13) { DurLoss = (int32)Old->Raw[13] - (int32)B[14]; }
		}
		if (I) { I->ApplyExternalRecord(ItemRec); I->ApplyExternalRecord(StoneRec); }
		// ★ Эталон эту строку только СОБИРАЕТ (`sprintf(Buffer, …)`), но в чат НЕ выводит: дизасм 0xD0F900 `push 0x4E98` (20120) →
		//   0xD0F90A формат → 0xD0F91C `sprintf`, и ни одного вызова чата (`0x91F63A`) в этой ветке нет — у оригинала нотиса нет.
		//   Сервер `CharCellobItemDetach` прочность на успехе не меняет (убыль = 0). [ИСПРАВЛЕНИЕ] порта: строка показывается,
		//   только если прочность действительно убыла (> 0); при нуле — молча, как оригинал.
		if (DurLoss > 0)
		{
			// Строка 20120 несёт маркер `[sd1]` (не `%d`): эталон кладёт число в целый слот 0 (`CClass_SetSlotPair(ctx,0,v8,0)`)
			//   и форматирует `UI_FormatLocalizedNotice`. Прежний `FormatPrintf` по `%d` его не находил, а маркер
			//   превращался в «0» — отсюда «снизилась на 0» при любой убыли.
			UAsdaTextSubsystem::FNoticeSlots Slots;
			Slots.Num[0] = DurLoss;
			ChatNotice(UAsdaTextSubsystem::FormatNoticeText(AsdaNotice::LocTagged(GetGameInstance(), kLocDetachDone), Slots), AsdaNoticeColor::Olive);
		}
		UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] 5023 успех: убыль прочности %d%s"), DurLoss, DurLoss > 0 ? TEXT(" — нотис 20120") : TEXT(" — нотиса нет (как у оригинала)"));
		PlayUiSound(DetachSoundId);
		OnSowelGlowDirty.Broadcast();
		Pick = EAsdaSowelPick::None; SelSocket = -1;
		SetVis(SocketSel, false);
		RebuildList();
		RedrawSockets();
		return;
	}
	if (Res == 2)
	{
		// Сервер требует ПОДТВЕРЖДЕНИЯ (двухкнопочный msgbox, нотис 2002); «OK» повторяет 5022.
		bDetachConfirmPending = true;
		UGameInstance* GI = GetGameInstance();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		if (CVarSowelLegacyNoDetachConfirm.GetValueOnGameThread() != 0 || !WM)
		{
			bDetachConfirmPending = false;
			RequestSocketDetach();
			return;
		}
		AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(LocRaw(kLocDetachConfirm), [this](bool bYes)
		{
			bDetachConfirmPending = false;
			if (bYes) { RequestSocketDetach(); }
		}, 0.f, 2288, 2289, /*очередь: эталон (1, 2002) network.c:17488*/2002, 1);
		return;
	}
	// ★ 21.10.2026 (ревизия, МЕЛОЧЬ-4): цвет — эталонный −11514 (дизасм 0xD0F9B8/0xD0F9E4 `push 0xFFFFD306`), не оливковый.
	if (Res == 4) { ChatNotice(LocRaw(kLocBagFull), AsdaNoticeColor::Gold); }
	else if (Res == 5) { ChatNotice(LocRaw(kLocSoulmateOnly), AsdaNoticeColor::Gold); }
	else { UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] 5023 код %d — текста под него в эталоне нет"), (int32)Res); }
	RedrawSockets();
}

// ★ 5032, ПЕРВАЯ ПОЛОВИНА — эталон `sowelWndOnSocketResultPacket@0xD15EE0`.
//   Только при result 1/7 включает анимацию 498 и ЖДЁТ её конца; во всех прочих ветках сразу
//   передаёт управление разбору `sowelWndHandleSocketResult`.
void UAsdaSowelSubsystem::ApplyHolyResult(const TArray<uint8>& B)
{
	bAwaitingResult = false;
	if (B.Num() < 63) { return; }
	const uint8 Res = B[0];
	if ((Res == 1 || Res == 7) && HolyEffect && HolySowelCell >= 0)
	{
		DeferredHolyBody = B;
		if (!bHolyAnimBound)
		{
			HolyEffect->OnAnimFinished.AddDynamic(this, &UAsdaSowelSubsystem::OnHolyEffectAnimFinished);
			bHolyAnimBound = true;
		}
		// Эталон зовёт `CControl_Show` — одиночный прогон, по концу которого прилетает событие 0x18.
		HolyEffect->bLoop = false;
		HolyEffect->MaxLoops = 1;
		HolyAnimStartS = FPlatformTime::Seconds();
		ShowHolyEffectOverCell(HolySowelCell);
		UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] 5032 result=%d: играем анимацию 498, разбор — по её концу"), (int32)Res);
		return;
	}
	FinishHolyResult(B);
}

void UAsdaSowelSubsystem::OnHolyEffectAnimFinished()
{
	if (DeferredHolyBody.Num() < 63) { return; }
	const TArray<uint8> B = MoveTemp(DeferredHolyBody);
	DeferredHolyBody.Reset();
	SetVis(HolyEffect, false);
	FinishHolyResult(B);
}

// ★ 5032, ВТОРАЯ ПОЛОВИНА — эталон `sowelWndHandleSocketResult@0xD16070` (switch по gap_1B0[0]).
void UAsdaSowelSubsystem::FinishHolyResult(const TArray<uint8>& B)
{
	if (B.Num() < 63) { return; }
	UAsdaInventorySubsystem* I = Inv();
	const uint8 Res = B[0];
	const int32 ConsumedSlot = SwRdI16(B.GetData() + 4);
	TArray<uint8> NewStone; NewStone.Append(B.GetData() + 6, 57);
	const int32 NewStoneCode = SwRdI32(NewStone.GetData());
	const int32 NewStoneSlot = SwRdI16(NewStone.GetData() + 5);

	switch (Res)
	{
	case 0:
	{
		// «Ничего не вышло»: эталон печатает 20140 с именем КАМНЯ из ячейки, по которой ударили,
		//   а если там не камень — нотис 2013.
		const FAsdaItemInstance* Stone = (I && HolySowelCell >= 0) ? I->GetCell(2, HolySowelCell) : nullptr;
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (Stone && IT && IT->IsSowelCode(Stone->ItemCode))
		{
			ChatNotice(LocSlots(kLocHolyNothing, { SowelName(Stone->ItemCode) }), kHolyNoticeColor);
		}
		else
		{
			ChatNotice(LocRaw(kLocHolyNoSowel), kHolyNoticeColor);
		}
		break;
	}
	case 1:
	case 7:
	{
		// Успех: израсходованный «святой» предмет исчезает, камень заменяется новой записью.
		if (I)
		{
			const TArray<uint8> Empty = FAsdaItemRecordWriter::ClearCell(2, ConsumedSlot);   // amount = −1 ⇒ очистить (★ U17)
			I->ApplyExternalRecord(Empty);
			if (NewStoneCode > 0) { I->ApplyExternalRecord(NewStone); }
		}
		if (Res == 1 && NewStoneCode > 0)
		{
			ChatNotice(LocSlots(kLocHolySuccess, { SowelName(NewStoneCode) }), kHolyNoticeColor);
		}
		else if (Res == 7)
		{
			MsgBox1(LocRaw(2017));
		}
		else
		{
			MsgBox1(LocRaw(2007));   // эталон: запись пуста ⇒ нотис 2007
		}
		PlayUiSound(HolySoundId);
		UE_LOG(LogAsdaSowel, Log, TEXT("[самоцветы] 5032 успех: израсходован слот %d, новый камень код=%d слот=%d"),
			ConsumedSlot, NewStoneCode, NewStoneSlot);
		break;
	}
	case 2:
	{
		// Эталон печатает 20142 с именем ПРЕДМЕТА из ячейки «святого» предмета.
		UGameInstance* GI = GetGameInstance();
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		const FAsdaItemInstance* Holy = (I && HolyCell >= 0) ? I->GetCell(2, HolyCell) : nullptr;
		if (IT && Holy) { ChatNotice(LocSlots(kLocHolyItemName, { IT->GetItemName(Holy->ItemCode) }), kHolyNoticeColor); }
		break;
	}
	case 3: MsgBox1(LocRaw(2009)); break;
	case 4: MsgBox1(LocRaw(2010)); break;
	case 5: MsgBox1(LocRaw(2011)); break;
	case 6: MsgBox1(LocRaw(2012)); break;
	case 8: MsgBox1(LocRaw(2019)); break;
	default:
		UE_LOG(LogAsdaSowel, Warning, TEXT("[самоцветы] 5032 код %d — ветки под него в эталоне нет"), (int32)Res);
		break;
	}

	// Эталон в конце ВСЕГДА снимает режим и возвращает курсор «nomal».
	bHolyArmed = false; HolyCell = -1;
	if (Res != 1 && Res != 7) { SetCursorMode(0); }
	RebuildList();
	RedrawSockets();
}

// ═══════════════════════════════ ПОМОЩНИКИ ═════════════════════════════════════════════════════

// ★ D10 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::LocRaw`. Фолбэк ЭТОГО окна —
//   диагностический «[#N]» (до 11.10.2026 — «[loc N]», N9) (bDebugFallback = true), он и сохранён 1:1.
// ★ 21.10.2026 (ревизия): строки самоцветов несут маркеры `[ssN]`/`[sdN]` (20068/20121/20140…20142, 20120), а не `%s`.
//   Эталон пишет аргументы в слоты `g_pLocNoticeCtx` (`CObject_SetSlotString(ctx, i, …, 0)`) и форматирует
//   `UI_FormatLocalizedNotice` — порт этой пары: `UAsdaTextSubsystem::FormatNoticeText` со своими слотами (закон
//   [[loc-string-placeholders]]). Слоты строк — по порядку, без цвета (эталон везде передаёт цвет 0).
FString UAsdaSowelSubsystem::LocSlots(int32 Id, std::initializer_list<FString> Strs) const
{
	UAsdaTextSubsystem::FNoticeSlots Slots;
	int32 i = 0;
	for (const FString& S : Strs) { if (i < 5) { Slots.Str[i++] = S; } }
	return UAsdaTextSubsystem::FormatNoticeText(AsdaNotice::LocTagged(GetGameInstance(), Id), Slots);
}

FString UAsdaSowelSubsystem::LocRaw(int32 Id) const
{
	// ★ Волна 1 (14.09.2026, В2): общая точка `AsdaNotice::LocRaw` (поведение то же).
	return AsdaNotice::LocRaw(GetGameInstance(), Id, /*bDebugFallback*/true);
}

// ★ D8 (аудит `code_dedup_audit_2.md`): подстановка `%s`/`%d` подряд переехала в
//   `AsdaUi::FormatPrintfArgs` — ЭТА копия была ВЕРНОЙ (схлопывала «%%» в один «%», как C-`sprintf`
//   эталона), она и стала общей. Метод оставлен тонким форвардом: он публичный API окна.
FString UAsdaSowelSubsystem::FormatPrintf(const FString& Fmt, const TArray<FString>& Args)
{
	return AsdaUi::FormatPrintfArgs(Fmt, Args);
}

FString UAsdaSowelSubsystem::SowelName(int32 Code) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSowellRow Row;
	if (IT && IT->GetSowell(Code, Row) && !Row.Name.IsEmpty()) { return Row.Name; }
	return FString::Printf(TEXT("%d"), Code);
}

void UAsdaSowelSubsystem::ChatNotice(const FString& Text, FColor Color) const
{
	// ★ Волна 1 (14.09.2026, В2, E5): раньше строка шла прямо в `UAsdaChatSubsystem::AddSystemMessage` мимо
	//   маршрутизатора (единственный такой обход в UI). Теперь — через `UAsdaMessageSubsystem::Post` с ЯВНЫМИ
	//   приёмником `SystemChat` (журнал + буфер A), цветом обработчика и TTL 10 000 мс: на экране та же строка
	//   того же цвета в том же буфере, но проходит счётчики/`asda.Msg.Verbose`. Категория `System` выбрана
	//   потому, что у неё в маршруте НЕТ звука: `ItemRefused` добавил бы «error.wav» к успешным нотисам
	//   самоцветов, которого у эталона и у прежней копии не было.
	LastNoticeText = Text; LastNoticeColor = Color;   // ★ 21.10.2026: для самотеста `asda.SelfTest.Sowel`
	AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, Text, Color, 10000.f);
}

// ★ D2 (аудит `code_dedup_audit_2.md`): полотно сборки окна 1500 переехало в
//   `UAsdaWindowSubsystem::ShowGameMessage`.
//   ★ ИСПРАВЛЕН ДЕФЕКТ ПОРТА (закон `fix-original-defects`): эта копия НЕ ставила подпись кнопки
//   `ONEBUT_BUT00_Text` и НЕ вешала обработчик на саму кнопку — модальное окно самоцветов нечем
//   было закрыть. Через общую точку и подпись (loc 2288 «ОК»), и закрытие приходят сами.
void UAsdaSowelSubsystem::MsgBox1(const FString& Text) const
{
	// ★ Волна 1 (14.09.2026, В2): единый маршрут `AsdaNotice::MsgBox` — окно 1500, без оконной подсистемы —
	//   строка в ленту той же категорией. Категория/подпись кнопки — прежние.
	AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Text);
}

void UAsdaSowelSubsystem::PlayUiSound(int32 SoundId) const
{
	if (SoundId <= 0) { return; }
	UGameInstance* GI = GetGameInstance();
	if (UAsdaSoundSubsystem* S = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		S->PlaySound2DById(SoundId, false);
	}
}

// Режимы курсора эталона: 0 «nomal», 1 «bottle» (взведён «святой» предмет), 2 «hammer» (сработало).
void UAsdaSowelSubsystem::SetCursorMode(int32 Mode) const
{
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* Pc = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr;
	if (!Pc) { return; }
	switch (Mode)
	{
	case 1:  Pc->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Bottle); break;
	case 2:  Pc->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Hammer); break;
	default: Pc->SetGameCursor(AAsdaPlayerController::EAsdaCursor::Normal); break;
	}
	// Пока курсор задан режимом камней, наведение на мир его не перебивает.
	Pc->bUiCursorOverride = (Mode != 0);
}

bool UAsdaSowelSubsystem::ApplyIcon(UImage* Img, int32 ItemCode) const
{
	// ★ `GetItemIcon` уже умеет ТРИ пространства кодов, включая `sowell_list` (см. [[inventory-port]]).
	// ★ Волна 1 (14.09.2026, В1): клетка атласа — общая точка `AsdaUi::ApplyItemIcon` (заворот, кисть
	//   IconDrawSize — как было). Видимость ставит окно само: общая точка её не трогает.
	if (!AsdaUi::ApplyItemIcon(GetGameInstance(), Img, ItemCode, FVector2D(IconDrawSize, IconDrawSize))) { return false; }
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	return true;
}

void UAsdaSowelSubsystem::ClearIcon(UImage* Img) const
{
	if (!Img) { return; }
	FSlateBrush Br = Img->GetBrush();
	Br.SetResourceObject(nullptr);
	Br.DrawAs = ESlateBrushDrawType::NoDrawType;
	Img->SetBrush(Br);
}

// ═══════════════════════════════ ДИАГНОСТИКА ═══════════════════════════════════════════════════

FString UAsdaSowelSubsystem::DumpControls() const
{
	FString S;
	S += TEXT("=== Контролы ветки самоцветов окна INVEN_WIN 400 ===\n");
	auto Line = [&S](const TCHAR* Idc, int32 IdNum, const UWidget* W)
	{
		S += FString::Printf(TEXT("  %-24s idnum=%-6d %s\n"), Idc, IdNum,
			W ? *FString::Printf(TEXT("%s [%s] vis=%d"), *W->GetName(), *W->GetClass()->GetName(), (int32)W->GetVisibility())
			  : TEXT("НЕ НАЙДЕН"));
	};
	for (int32 i = 0; i < kTrayCount; ++i)   { Line(*FString::Printf(TEXT("INVEN_SOWTRAY%d"), i), kTrayFirstId + i, TrayIcon[i]); }
	Line(TEXT("INVEN_SOWELLSELECT"), kTraySelId, TraySel);
	Line(TEXT("Inven_AvailableSowelTxt"), kAvailTxtId, AvailTxt);
	for (int32 i = 0; i < kSocketCount; ++i) { Line(*FString::Printf(TEXT("INVEN_SOWELL%d"), i), kSocketFirstId + i, SocketIcon[i]); }
	Line(TEXT("INVEN_SOWELLSEL"),      kSocketSelId, SocketSel);
	Line(TEXT("INVEN_SOWELLSHADE"),    kShade1Id, Shade1);
	Line(TEXT("INVEN_SOWELLSHADE1"),   kShade3Id, Shade3);
	Line(TEXT("INVEN_SOWELLSHADE2"),   kShade2Id, Shade2);
	Line(TEXT("INVEN_SOWELLEMTYBACK"), kEmptyBackId, EmptyBackGrp);
	S += FString::Printf(TEXT("  (у 499 дочерних планов-шторок: %d)\n"), EmptyBackPlanes.Num());
	Line(TEXT("INVEN_HOLYEFFECT"),       kHolyEffectId, HolyEffect);
	Line(TEXT("INVEN_SOULSTONEEFFECT"),  kSoulStoneId,  SoulStoneEffect);
	S += FString::Printf(TEXT("  центрирование эффектов: 498 (%.0f, %.0f), 10009 (%.0f, %.0f)\n"),
		HolyCenterOff.X, HolyCenterOff.Y, SoulCenterOff.X, SoulCenterOff.Y);
	return S;
}

FString UAsdaSowelSubsystem::DumpState() const
{
	FString S;
	S += FString::Printf(TEXT("=== Состояние самоцветов ===\n  предмет: контейнер=%d ячейка=%d гнёзд=%d\n"),
		TargetContainer, TargetCell, GetSocketCount());
	for (int32 i = 0; i < kSocketCount; ++i)
	{
		const int32 C = GetSocketCode(i);
		S += FString::Printf(TEXT("    гнездо %d = %d%s\n"), i, C, (C > 0) ? *FString::Printf(TEXT(" (%s)"), *SowelName(C)) : TEXT(""));
	}
	S += FString::Printf(TEXT("  список: %d камней, страница %d/%d, выбран idx=%d (кол.%d), гнездо=%d, режим=%d\n"),
		SowelList.Num(), ListPage, GetListPageCount(), SelStoneIndex, SelStoneCol, SelSocket, (int32)Pick);
	for (int32 i = 0; i < SowelList.Num(); ++i)
	{
		S += FString::Printf(TEXT("    [%d] код=%d ячейка2=%d «%s» иконка=%s\n"),
			i, SowelList[i].Code, SowelList[i].SpendCell, *SowelName(SowelList[i].Code), *SowelList[i].IconPath);
	}
	S += FString::Printf(TEXT("  святость: взведена=%d ячейка=%d последний камень=%d, ждём ответа=%d\n"),
		bHolyArmed ? 1 : 0, HolyCell, HolySowelCell, bAwaitingResult ? 1 : 0);
	return S;
}

// ═══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════
//   Проверка ветки БЕЗ сервера: `asda.Sowel.Dump/State/Select/Socket/Insert/Detach/Page/Holy/Sim*`.

static UAsdaSowelSubsystem* SwGet(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaSowelSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwDump(
	TEXT("asda.Sowel.Dump"), TEXT("Дамп КАЖДОГО контрола ветки самоцветов окна инвентаря."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { UE_LOG(LogAsdaSowel, Log, TEXT("\n%s"), *S->DumpControls()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwState(
	TEXT("asda.Sowel.State"), TEXT("Состояние: список камней, выбор, гнёзда, режим «святости»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { UE_LOG(LogAsdaSowel, Log, TEXT("\n%s"), *S->DumpState()); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwTarget(
	TEXT("asda.Sowel.Target"), TEXT("asda.Sowel.Target <контейнер 1|3> <ячейка> — выбрать предмет-носитель."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSowelSubsystem* S = SwGet(W);
		if (!S || A.Num() < 2) { return; }
		S->OnItemSelected(FCString::Atoi(*A[0]), FCString::Atoi(*A[1]));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwRebuild(
	TEXT("asda.Sowel.Rebuild"), TEXT("Пересобрать список доступных камней для выбранного предмета."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->RebuildList(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwPage(
	TEXT("asda.Sowel.Page"), TEXT("asda.Sowel.Page <N> — страница списка камней (6 ячеек на страницу)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->SetListPage(A.Num() ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwSelect(
	TEXT("asda.Sowel.Select"), TEXT("asda.Sowel.Select <колонка 0..5> — выбрать камень в списке."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->SelectStoneByColumn(A.Num() ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwSocket(
	TEXT("asda.Sowel.Socket"), TEXT("asda.Sowel.Socket <0..3> — выбрать ГНЕЗДО (только если в нём есть камень)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->SelectSocket(A.Num() ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwInsert(
	TEXT("asda.Sowel.Insert"), TEXT("Отправить 5024 — вставить выбранный камень (гнездо выбирает правило эталона)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->RequestSocketInsert(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwDetach(
	TEXT("asda.Sowel.Detach"), TEXT("Отправить 5022 — вынуть камень из выбранного гнезда (любое 0..3 с камнем, в том числе главное 0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->RequestSocketDetach(); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwHoly(
	TEXT("asda.Sowel.Holy"), TEXT("asda.Sowel.Holy <ячейка контейнера 2> — двойной клик по предмету/камню (режим «святости»)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->OnSpendCellUse(A.Num() ? FCString::Atoi(*A[0]) : 0); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwHolyCancel(
	TEXT("asda.Sowel.HolyCancel"), TEXT("Снять взведённый режим «святости»."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* W)
	{ if (UAsdaSowelSubsystem* S = SwGet(W)) { S->CancelHolyMode(false); } }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwEffect(
	TEXT("asda.Sowel.Effect"), TEXT("asda.Sowel.Effect <holy|soul> <ячейка> — показать анимацию 498 / 10009 над ячейкой."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{
		UAsdaSowelSubsystem* S = SwGet(W);
		if (!S || A.Num() < 1) { return; }
		const int32 Cell = (A.Num() > 1) ? FCString::Atoi(*A[1]) : 0;
		if (A[0].Equals(TEXT("soul"), ESearchCase::IgnoreCase)) { S->ShowSoulStoneEffect(Cell); }
		else if (A[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)) { S->HideEffects(); }
		else { S->ShowHolyEffectOverCell(Cell); }
	}));
#endif // ASDA_DEV_CONSOLE

// Эмуляция ответов сервера: `asda.Sowel.SimSocket <код>`, `SimDetach <код>`, `SimHoly <код>`.
//   Тело заполняется нулями, кроме кода результата — этого хватает, чтобы прогнать ветки нотисов.
static void SwSim(UWorld* W, int32 Which, int32 Code)
{
	// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный ответ сервера. При живом соединении код 2 у 5023 вызвал бы НАСТОЯЩИЙ
	//   5022 по реально выбранному гнезду, а любой код снимает замок `bAwaitingResult` (повторная 5024/5031) и подделывает сумку.
	if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, Which == 0 ? TEXT("asda.Sowel.SimSocket") : (Which == 1 ? TEXT("asda.Sowel.SimDetach") : TEXT("asda.Sowel.SimHoly")))) { return; }
	UAsdaSowelSubsystem* S = SwGet(W);
	if (!S) { return; }
	TArray<uint8> B;
	B.SetNumZeroed(Which == 0 ? 117 : (Which == 1 ? 115 : 63));
	B[0] = (uint8)Code;
	if (Which == 0) { S->SimulateSocketResult(B); }
	else if (Which == 1) { S->SimulateDetachResult(B); }
	else { S->SimulateHolyResult(B); }
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwSimSocket(
	TEXT("asda.Sowel.SimSocket"), TEXT("asda.Sowel.SimSocket <код> — эмуляция 5025 без сервера."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ SwSim(W, 0, A.Num() ? FCString::Atoi(*A[0]) : 1); }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwSimDetach(
	TEXT("asda.Sowel.SimDetach"), TEXT("asda.Sowel.SimDetach <код> — эмуляция 5023 без сервера."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ SwSim(W, 1, A.Num() ? FCString::Atoi(*A[0]) : 1); }));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GSwSimHoly(
	TEXT("asda.Sowel.SimHoly"), TEXT("asda.Sowel.SimHoly <код> — эмуляция 5032 без сервера."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& A, UWorld* W)
	{ SwSim(W, 2, A.Num() ? FCString::Atoi(*A[0]) : 1); }));
#endif // ASDA_DEV_CONSOLE
