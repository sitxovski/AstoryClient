// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestLogSubsystem_Slots.cpp — ★ R3-c 12.11.2026 (разрез AsdaQuestLogSubsystem.cpp, дословный перенос): ячейки заданий: выбор, перенос, перетаскивание, отказ от задания, раскрытие страницы, отслеживание.
//   Общее для частей — в AsdaQuestLogSubsystem_Private.h; шапка и реверс — в AsdaQuestLogSubsystem.cpp.
#include "Quest/AsdaQuestLogSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaSystemMenuSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaTipPanel.h"   // ★ общая точка Resize9Slice (порт uiPanel9SliceLayout)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaNameplateComponent.h"
#include "World/AsdaWorldSubsystem.h"      // ★ имя своего персонажа — ключ сохранения списка отслеживаемых
#include "AsdaNetSubsystem.h"
#include "Npc/AsdaNpcDialogSubsystem.h"   // ★ 19.10.2026: награды завершения 5052 — `ApplyQuestCompletionRewards5052`

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» кнопок окна заданий (событие 66561)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: задания доски объявлений 20000…30000 — тип, награды, готовность, имя
#include "UI/AsdaNotice.h"                   // ★ 31.10.2026: нотис провала отказа от задания (5007) — канал эталонного 6956
#include "AsdaBytes.h"                       // ★ 31.10.2026: разбор тела 5007 (ячейка i16 по +1)
#include "Quest/AsdaQuestGuideTarget.h"      // ★ 31.10.2026 (баг 6): место цели строки гида без координат — общая точка с картой мира
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)
#include "AsdaQuestLogSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaQuestLogSubsystem*.cpp

// Сколько секунд ждать 5007 на отказ, прежде чем снять защёлку (сервер молча роняет пакет при сбое анти-реплея/MD5).
static constexpr double kAbandonAckTimeoutSec = 10.0;

// Прозрачность призрака и приглушённого источника — те же 40 %, что у предмета и навыка.
static constexpr float kSlotDragOpacity = 0.4f;

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЯЧЕЙКИ ЖУРНАЛА
// ════════════════════════════════════════════════════════════════════════════════════════════════
const FAsdaQuestSlot* UAsdaQuestLogSubsystem::SlotAt(int32 Idx) const
{
	const UAsdaQuestMarkerSubsystem* QM = Marks();
	if (!QM) { return nullptr; }
	const TArray<FAsdaQuestSlot>& S = QM->GetSlots();
	return S.IsValidIndex(Idx) ? &S[Idx] : nullptr;
}

// Порт `uiQuestSlotSelectToggle@0xC60130`: выбирается только ЗАНЯТАЯ ячейка (`questId >= 2001`),
//   играет звук 12, дальше перерисовка. Рамка 737 садится на выбранную ячейку.
void UAsdaQuestLogSubsystem::SelectSlot(int32 SlotIdx)
{
	if (SlotIdx < 0 || SlotIdx > 11) { return; }
	if (!IsSlotBusy(SlotAt(SlotIdx)))
	{
		UE_LOG(LogAsdaQuestLog, Verbose, TEXT("[журнал] ячейка %d пуста — выбор не меняем"), SlotIdx);
		return;
	}
	if (ActiveSlot == SlotIdx) { return; }
	ActiveSlot = SlotIdx;
	PlaySound(QSnd::Page);
	RefreshAll();
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] выбрана ячейка %d («%s»)"), SlotIdx, *TitleForSlot(SlotIdx));
}

void UAsdaQuestLogSubsystem::MoveSlot(int32 SrcSlot, int32 DstSlot)
{
	if (SrcSlot < 0 || SrcSlot > 11 || DstSlot < 0 || DstSlot > 11 || SrcSlot == DstSlot) { return; }
	const FAsdaQuestSlot* S = SlotAt(SrcSlot);
	if (!IsSlotBusy(S)) { return; }
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }
	// Эталон берёт номер ячейки из ПОЛЯ записи (`слот+5`), а не из индекса массива.
	const int32 SrcField = (S->SlotIndex >= 0 && S->SlotIndex <= 11) ? S->SlotIndex : SrcSlot;
	if (GAsdaQuestLogSlotMove != 0)
	{
		// Ответы на отказ и перенос находят ячейку по её номеру — пока один не пришёл, второй не начинаем.
		if (IsMovePending() || (bAbandonPending && FPlatformTime::Seconds() - AbandonSentAt < kAbandonAckTimeoutSec))
		{
			UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] перенос %d → %d: ждём ответа сервера на прошлую операцию — не шлём"), SrcSlot, DstSlot);
			return;
		}
		if (!Net->SendQuestSlotMove5000(SrcField, DstSlot)) { return; }
		bMovePending = true;
		MoveSentAt = FPlatformTime::Seconds();
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] перенос задания: ячейка %d → %d (5000, типы 4→4), ждём 5001"), SrcField, DstSlot);
		return;
	}
	Net->SendQuestSlotMove5000(SrcField, DstSlot);
	// Эталон здесь ещё и МЕНЯЛ МЕСТАМИ флаги отслеживания у двух ячеек (`questLogSlotClick`) —
	//   ему приходилось, потому что пометка у него привязана к НОМЕРУ ЯЧЕЙКИ. У нас пометка
	//   привязана к САМОМУ ЗАДАНИЮ (записи трекера ключуются `QuestNo`), поэтому при переносе
	//   задания в другую ячейку она едет вместе с ним и трогать ничего не надо.
	ActiveSlot = DstSlot;
	PlaySound(QSnd::Slot);
	bDirty = true;
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] перенос задания: ячейка %d → %d (5000, типы 4→4)"), SrcField, DstSlot);
}

bool UAsdaQuestLogSubsystem::IsMovePending() const
{
	if (bMovePending && FPlatformTime::Seconds() - MoveSentAt >= kAbandonAckTimeoutSec)
	{
		UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] перенос: ответа 5001 нет %.0f с — ожидание снято"), kAbandonAckTimeoutSec);
		bMovePending = false;
	}
	return bMovePending;
}

// ★ 06.11.2026 Ответ 5001 на перенос — обоснование у объявления в .h и в блоке `asda.QuestLog.SlotMove`.
//   Отказ сервера эхом кладёт запрос (+6 тип 4), а провал БД (код 100) тела не несёт (тип 0) — оба снимают ожидание журнала.
bool UAsdaQuestLogSubsystem::ConsumeItemMove5001(const TArray<uint8>& Body24)
{
	if (GAsdaQuestLogSlotMove == 0 || Body24.Num() < 24) { return false; }
	const uint8* P = Body24.GetData();
	const int32 Result  = P[0];
	const int32 SubKind = P[1];
	const int32 SrcSlot = AsdaBytes::RdI16(P + 2);
	const int32 SrcType = P[6];
	const int32 DstSlot = AsdaBytes::RdI16(P + 13);
	if (Result != 1)
	{
		if (bMovePending && (SrcType == 4 || SrcType == 0))
		{
			bMovePending = false;
			UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] 5001 перенос ОТКЛОНЁН сервером: код %d — журнал не меняем"), Result);
		}
		return false;
	}
	if (SrcType != 4) { return false; }
	bMovePending = false;
	const bool bSwap = SubKind == 1;
	UAsdaQuestMarkerSubsystem* QM = Marks();
	if (QM && QM->ApplyQuestSlotMove(SrcSlot, DstSlot, bSwap))
	{
		// Выбор едет вместе с заданием.
		if (ActiveSlot == SrcSlot) { ActiveSlot = DstSlot; }
		else if (bSwap && ActiveSlot == DstSlot) { ActiveSlot = SrcSlot; }
		bDirty = true;
	}
	return true;
}

// Окно журнала под курсором и не перекрыто другим окном (тот же хит-тест, что решает у боевого кода и хотбара).
bool UAsdaQuestLogSubsystem::IsCursorOnQuestWindow() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	FName Zone = NAME_None;
	return WM && WM->IsCursorOverAnyUiWindow(Zone) && Zone == kWindowId;
}

int32 UAsdaQuestLogSubsystem::SlotUnderCursor() const
{
	if (!IsCursorOnQuestWindow()) { return -1; }
	for (int32 i = 0; i < SlotIcons.Num(); ++i)
	{
		if (SlotIcons[i] && AsdaUi::IsCursorOverWidget(SlotIcons[i].Get())) { return i; }
	}
	return -1;
}

// Покадровый опрос (образец — лоток эмоций `UAsdaCharaInfoSubsystem`): кнопки ячеек `QUEST_INVENn_HIT` ловят только щелчок.
void UAsdaQuestLogSubsystem::TickSlotDrag()
{
	if (GAsdaQuestLogSlotMove == 0) { return; }
	SlotMouse.Update();
	// Под модальным окном мышь ниже не отдаётся (`AsdaUi::IsModalDispatchBlocked`): захват снимается без действия.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		CancelSlotDrag();
		SlotMouse.Reset();
		return;
	}
	if (SlotMouse.bLmbDown)
	{
		const int32 Slot = SlotUnderCursor();
		const bool bBusyNet = IsMovePending() || (bAbandonPending && FPlatformTime::Seconds() - AbandonSentAt < kAbandonAckTimeoutSec);
		if (Slot >= 0 && IsSlotBusy(SlotAt(Slot)) && !bBusyNet) { SlotDragArm.Arm(Slot, -1, SlotMouse.Cursor); }
	}
	if (SlotMouse.bLmb && SlotDragArm.ShouldStart(SlotMouse.Cursor)) { BeginSlotDrag(SlotDragArm.KeyA); }
	if (SlotDragSrc >= 0) { SlotDragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr); }
	if (SlotMouse.bLmbUp)
	{
		if (SlotDragSrc >= 0) { EndSlotDrag(); }
		SlotDragArm.Disarm();
	}
}

void UAsdaQuestLogSubsystem::BeginSlotDrag(int32 SlotIdx)
{
	SlotDragArm.MarkStarted();
	SlotDbl.Reset();   // ★ 10.11.2026: захват обрывает серию щелчков (контракт `FAsdaDblClick`)
	UImage* Src = SlotIcons.IsValidIndex(SlotIdx) ? SlotIcons[SlotIdx].Get() : nullptr;
	if (!Src || !IsSlotBusy(SlotAt(SlotIdx)) || !Win || !Win->WidgetTree) { return; }
	SelectSlot(SlotIdx);   // эталон: выбор приходит раньше захвата, а `CQuest_Send_SelectSlot` переносит выбранную ячейку
	SlotDragSrc = SlotIdx;
	if (UCanvasPanel* Root = Cast<UCanvasPanel>(Win->WidgetTree->RootWidget))
	{
		SlotDragGhost.Begin(Win, Root, Src->GetBrush(), FVector2D(32.f, 32.f), kSlotDragOpacity, FVector2D(-16.f, -16.f),
			TEXT("QUEST_DragGhost"), GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
	}
	Src->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, kSlotDragOpacity));
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] захват ячейки %d"), SlotIdx);
}

void UAsdaQuestLogSubsystem::CancelSlotDrag()
{
	SlotDragArm.Disarm();
	if (SlotDragSrc < 0) { return; }
	SlotDragGhost.Cancel();
	if (UImage* Src = SlotIcons.IsValidIndex(SlotDragSrc) ? SlotIcons[SlotDragSrc].Get() : nullptr) { Src->SetColorAndOpacity(FLinearColor::White); }
	SlotDragSrc = -1;
}

void UAsdaQuestLogSubsystem::EndSlotDrag()
{
	const int32 Src = SlotDragSrc;
	CancelSlotDrag();   // призрак снимаем до хит-теста: иначе он сам оказался бы под курсором
	const int32 Dst = SlotUnderCursor();
	if (Dst >= 0)
	{
		if (Dst != Src) { MoveSlot(Src, Dst); }
		return;
	}
	if (BtnTrash && IsCursorOnQuestWindow() && AsdaUi::IsCursorOverWidget(BtnTrash.Get())) { OnTrashClicked(); }   // эталон: цель 713
}

void UAsdaQuestLogSubsystem::OnTrashClicked() { if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 713)) { return; } AbandonSelected(); }

void UAsdaQuestLogSubsystem::AbandonSelected()
{
	const FAsdaQuestSlot* S = SlotAt(ActiveSlot);
	if (!IsSlotBusy(S))
	{
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отказ: ячейка не выбрана или пуста"));
		return;
	}
	// FIX-ORIG D10: у эталона здесь свёрнутый Hex-Rays тест `id <= 2001 && id > 7000`, который
	//   читается как «тождественно ложно». На деле это беззнаковая проверка «ВНЕ [2001…7000]»
	//   — пишем её честно.
	// ★ 28.10.2026: обработчик 713 эталона (quest.c:1712-1726) отказ от задания ДОСКИ (номер 20000…30000) тоже принимает —
	//   с именем записи доски в тултипе 6174; сам отправитель 5006 (quest.c:2897: `id <= 7000 || id > 2001` — истинно
	//   всегда) номера не фильтрует. Прежний фильтр «только [2001…7000]» отрезал задания доски — снят для записей доски.
	const bool bBoardQuest = (S->QuestNo >= 20000 && S->QuestNo <= 30000) && GetGameInstance()
		&& GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>()
		&& GetGameInstance()->GetSubsystem<UAsdaBoardQuestSubsystem>()->FindQuest(S->QuestNo);
	if (!IsEpisodeQuestId(S->QuestId) && !bBoardQuest)
	{
		UE_LOG(LogAsdaQuestLog, Log,
			TEXT("[журнал] отказ: задание %d вне диапазона эпизодов [2001…7000] — эталон такие не отдаёт"),
			S->QuestId);
		return;
	}
	// ★ 06.11.2026: ждём ответа на перенос — номер ячейки в отказе ещё мог бы измениться.
	if (GAsdaQuestLogSlotMove != 0 && IsMovePending())
	{
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отказ: ждём ответа 5001 на перенос — не начинаем"));
		return;
	}
	// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] эталонная защёлка `acceptPending != 1` (quest.c:2900): пока сервер не ответил на прошлый
	//   отказ, новый не шлём — иначе второй 5006 приходит в уже пустую ячейку и сервер отвечает «нельзя удалить» (6956).
	//   Защёлку при молчании сервера снимаем через `kAbandonAckTimeoutSec` (у эталона она висела вечно).
	if (GAsdaQuestLogAbandonAck != 0 && bAbandonPending)
	{
		const double Age = FPlatformTime::Seconds() - AbandonSentAt;
		if (Age < kAbandonAckTimeoutSec)
		{
			UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отказ: ждём ответа 5007 на отказ от ячейки %d (%.1f с) — повторно не шлём"),
				AbandonSlotIdx, Age);
			return;
		}
		UE_LOG(LogAsdaQuestLog, Warning, TEXT("[журнал] отказ: ответа 5007 на ячейку %d нет %.1f с — защёлка снята"), AbandonSlotIdx, Age);
		bAbandonPending = false;
	}
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	const int32 SlotField = (S->SlotIndex >= 0 && S->SlotIndex <= 11) ? S->SlotIndex : ActiveSlot;
	const int32 QuestNo = S->QuestNo;
	const int32 SlotIdx = ActiveSlot;
	const FString Title = TitleForSlot(ActiveSlot);
	// Эталон ставит в очередь тултип-подтверждение с нотисом 6174 и кодом «ОК» 6174/6175;
	//   в порту единая точка подтверждения — игровой двухкнопочный msgbox.
	FString Msg = Loc(QLoc::kAbandonConfirm);
	if (!Title.IsEmpty()) { Msg = FString::Printf(TEXT("%s\n[%s]"), *Msg, *Title); }
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Msg, [this, SlotField, QuestNo, SlotIdx](bool bOk)
	{
		if (!bOk) { return; }
		UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (!Net) { return; }
		if (GAsdaQuestLogAbandonAck != 0)
		{
			// Второй вопрос мог быть поднят до ответа на первый — защёлку проверяет и сам отправитель (как эталон).
			if (bAbandonPending && FPlatformTime::Seconds() - AbandonSentAt < kAbandonAckTimeoutSec)
			{
				UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отказ подтверждён, но прошлый ещё без ответа 5007 — не шлём"));
				return;
			}
			const bool bWasTracked = IsSlotTracked(SlotIdx);
			if (!Net->SendQuestAbandon5006(SlotField)) { return; }
			bAbandonPending    = true;   // эталон `acceptPending = 1`
			AbandonSlotIdx     = SlotIdx; // эталон `acceptSlotIndex = activeSlot`
			AbandonServerSlot  = SlotField;
			AbandonQuestNo     = QuestNo;
			bAbandonWasTracked = bWasTracked;
			AbandonSentAt      = FPlatformTime::Seconds();
		}
		else
		{
			Net->SendQuestAbandon5006(SlotField);
		}
		// Эталон локально: снимает отслеживание, гасит чекбокс 743, взводит `acceptPending`.
		//   Обновлённый журнал приедет пакетом 5047 — оптимистично слот НЕ чистим.
		//   ★ Снимаем ПО НОМЕРУ ЗАДАНИЯ: `SlotField` — номер ячейки СЕРВЕРА, а запись трекера
		//   ключуется квестом (иначе при несовпадении индексов запись осталась бы висеть).
		if (UAsdaQuestMarkerSubsystem* QM = Marks()) { QM->TrackerRemoveEntry(QuestNo); }
		SaveTrackedToSettings();
		if (TrackBox) { TrackBox->SetIsChecked(false); }
		UE_LOG(LogAsdaQuestLog, Log,
			TEXT("[журнал] отказ от задания подтверждён: ячейка %d, квест %d → 5006 (под-тип 4)"),
			SlotField, QuestNo);
	}, 0.f, 2288, 2289, /*очередь: эталон (7, 6174) quest.c:1717*/6174, 7);
}

// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] Ответ 5007 на отказ от задания — обоснование у объявления в .h и в блоке `asda.QuestLog.AbandonAck`.
//   Чей пакет: тип +5 == 4 ставит только `sendCharQuestDump` (ответ на отказ) — такой 5007 всегда наш; отказ проверки в
//   `CharItemDump` (LABEL_50) тип не несёт — он наш, только если ждём ответа на отказ, а инвентарь своего уничтожения не ждёт.
bool UAsdaQuestLogSubsystem::ConsumeItemDump5007(const TArray<uint8>& Body18, bool bInventoryDumpPending, double InventoryDumpSentAt)
{
	if (GAsdaQuestLogAbandonAck == 0 || Body18.Num() < 6) { return false; }
	const uint8* P = Body18.GetData();
	const int32 Result     = P[0];
	const int32 ServerSlot = AsdaBytes::RdI16(P + 1);
	const int32 Kind       = P[5];
	// ★ Ожидания живы только `kAbandonAckTimeoutSec` (ревизия 31.10, А1): сервер молча роняет 5006 при сбое анти-реплея/MD5,
	//   и вечный флаг уводил бы чужой провал без типа не тому приёмнику (игрок снова видел бы 6956 вместо 6192 или наоборот).
	//   Время инвентаря — `ActionSentAtS` (ставится вместе с `bDumpPending`; более поздняя операция его лишь омолаживает).
	const double Now = FPlatformTime::Seconds();
	const bool bAbandonLive = bAbandonPending && Now - AbandonSentAt < kAbandonAckTimeoutSec;
	const bool bDumpLive    = bInventoryDumpPending && Now - InventoryDumpSentAt < kAbandonAckTimeoutSec;
	// А2: живы оба и тело без типа (отказ проверки `CharItemDump` до БД) — чей это провал, по пакету не различить: сервер
	//   тип не кладёт, а порядок ответов зависит от того, чья проверка прошла (прошедшая уходит в БД и отвечает позже). Такой
	//   провал отдаётся инвентарю (прежнее поведение, нотис 6956); провал отказа в БД приходит с типом 4 и всегда наш.
	const bool bOurs = (Kind == 4) || (Result != 1 && bAbandonLive && !bDumpLive);
	if (!bOurs) { return false; }

	const int32 PreferIdx = bAbandonPending ? AbandonSlotIdx : -1;
	const bool bWasTracked = bAbandonPending && bAbandonWasTracked;
	const int32 QuestNo = AbandonQuestNo;
	bAbandonPending = false;            // эталон `acceptPending = 0` (при успехе); при провале эталон защёлку не снимал — дефект
	AbandonSlotIdx = -1;                // эталон `acceptSlotIndex = −1`

	if (Result == 1)
	{
		// `uiCraftSlotConfirmRemove` → `uiCraftSlotRemoveAndReset`: снять ячейку, сбросить выбор и панель наград, метки NPC.
		UAsdaQuestMarkerSubsystem* QM = Marks();
		const int32 Cleared = QM ? QM->ApplyAbandonAck(ServerSlot, PreferIdx) : -1;
		if (Cleared >= 0 && ActiveSlot == Cleared) { ActiveSlot = -1; }
		bDirty = true;
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] 5007 отказ от задания ПРИНЯТ: ячейка сервера %d → снята ячейка журнала %d"),
			ServerSlot, Cleared);
		return true;
	}

	// Провал: эталон показывал строку уничтожения ПРЕДМЕТА 6956 — для задания это ложь; берём строку журнала заданий.
	const int32 LocId = (Result == 2) ? QLoc::kAbandonBusy : QLoc::kAbandonFailed;
	if (Result == 0 || Result == 2) { AsdaNotice::Chat(GetGameInstance(), EAsdaMsgCategory::ItemRefused, LocId); }
	// Эталон снял отслеживание ещё при отправке — задание осталось у игрока, возвращаем ему отслеживание.
	if (bWasTracked && PreferIdx >= 0)
	{
		const FAsdaQuestSlot* S = SlotAt(PreferIdx);
		if (S && IsSlotBusy(S) && S->QuestNo == QuestNo && !IsSlotTracked(PreferIdx)) { SetSlotTracked(PreferIdx, true); }
	}
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] 5007 отказ от задания ОТКЛОНЁН сервером: результат %d, ячейка сервера %d, тип %d → нотис %d"),
		Result, ServerSlot, Kind, (Result == 0 || Result == 2) ? LocId : 0);
	return true;
}

// ── Разворот/сворот правой страницы-свитка 732 (кнопки 739 / 735) ────────────────────────────────
void UAsdaQuestLogSubsystem::OnExpandClicked()   { if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 739)) { return; } SetPageExpanded(true); }
void UAsdaQuestLogSubsystem::OnCollapseClicked() { if (AsdaQuestEpisodeFishingGate(GetGameInstance(), 735)) { return; } SetPageExpanded(false); }

void UAsdaQuestLogSubsystem::OnSlotClicked(int32 SlotIdx)
{
	SelectSlot(SlotIdx);   // событие 0xB — выбор ячейки (как было)
	if (!SlotDbl.Click(SlotIdx)) { return; }
	// ★ 10.11.2026 [ОРИГИНАЛ] событие 0xD → `uiQuestSlotSelectToggle@0xC60130`: только ЗАНЯТАЯ ячейка (`questId >= 2001`);
	//   звук 12 и разворот/сворот страницы разговора (`this+151`: 1 → `sub_92F459` сворот, иначе `uiToggleStatePanelOn`).
	if (SlotIdx < 0 || SlotIdx > 11 || !IsSlotBusy(SlotAt(SlotIdx))) { return; }
	SetPageExpanded(!bPageExpanded);
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] двойной щелчок по ячейке %d — страница разговора %s"), SlotIdx,
		bPageExpanded ? TEXT("развёрнута") : TEXT("свёрнута"));
}

void UAsdaQuestLogSubsystem::SetPageExpanded(bool bExpanded)
{
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `uiToggleStatePanelOn@0xC5DF10` ставит байт 93 первой строкой — при КАЖДОМ вызове.
	if (bExpanded) { AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::QuestTalkPanel); }
	if (bPageExpanded == bExpanded) { return; }
	bPageExpanded = bExpanded;
	PlaySound(QSnd::Page);
	ApplyPageExpanded();
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] правая страница %s"), bExpanded ? TEXT("развёрнута") : TEXT("свёрнута"));
}

void UAsdaQuestLogSubsystem::ApplyPageExpanded()
{
	// Группа-контейнер видимой должна оставаться SelfHitTestInvisible: прозрачный CanvasPanel с
	//   Visible начал бы съедать клики по миру (грабля оконной подсистемы).
	if (RightPage)   { RightPage->SetVisibility(bPageExpanded ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (BtnExpand)   { BtnExpand->SetVisibility(bPageExpanded ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
	if (BtnCollapse) { BtnCollapse->SetVisibility(bPageExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }

	// ★★★ РАМКА ОКНА ПРИ РАЗВОРОТЕ. Эталон `uiToggleStatePanelOn@0xC5DF10` (ui_core.c:33236)
	//   делает РОВНО ДВА действия помимо переключения стрелок:
	//       sub_930E85(pMainWindow, 653, -1);   // ширина окна = 653, высота не трогается
	//       uiPanel9SliceLayout(pMainWindow);   // пересборка девяти планов рамки под новый размер
	//   Свёрнутая ширина — из `.JUI` (`QUEST_WIN Size = "338 636"`).
	//   ⚠ БЕЗ ЭТОГО у развёрнутого окна не сомкнут ПРАВЫЙ ВЕРХНИЙ УГОЛ: страница-свиток доходит
	//   до x ≈ 654, а рамка оставалась шириной 338 — между концом полосы заголовка и краем
	//   страницы зияла прямоугольная дыра (замечание владельца 2026-08-22).
	//   Пересборку делает ОБЩАЯ точка `UAsdaTipPanel::Resize9Slice` (порт `uiPanel9SliceLayout`);
	//   своей арифметики девяти частей не заводим.
	// Узлы, привязанные к правому краю окна, едут вместе с ним (обоснование — в `SetupControls`).
	const float DeltaW = bPageExpanded ? (kExpandedW - kCollapsedW) : 0.f;
	for (int32 i = 0; i < RightEdgeNodes.Num(); ++i)
	{
		// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.QuestLog.RightEdgeKeepsDrag`, разбор — у ключа): X «от базы» с сохранением сдвига
		//   окна игроком/постановщиком. Минус — помощник кладёт `Left = база − dX`.
		if (GAsdaQuestRightEdgeKeepsDrag != 0)
		{
			AsdaUi::ShiftSlotXFromBase(GQuestRightEdgeBase, RightEdgeNodes[i], -DeltaW);
			continue;
		}
		if (UCanvasPanelSlot* CS = RightEdgeNodes[i] ? AsdaUi::SlotOf(RightEdgeNodes[i]) : nullptr)
		{
			FMargin M = CS->GetOffsets();
			M.Left = RightEdgeBaseX[i] + DeltaW;
			CS->SetOffsets(M);
		}
	}

	if (FrameNine.Num() == 9 && FrameNine[0] && FrameNine[6])
	{
		const UCanvasPanelSlot* S0 = AsdaUi::SlotOf(FrameNine[0]);
		const UCanvasPanelSlot* S6 = AsdaUi::SlotOf(FrameNine[6]);
		if (S0 && S6)
		{
			// Начало рамки и её высоту берём ЖИВЫЕ (окно можно перетаскивать), меняем только ширину.
			const FMargin M0 = S0->GetOffsets();
			const FMargin M6 = S6->GetOffsets();
			const FVector2D Origin(M0.Left, M0.Top);
			const float Height = (M6.Top + M6.Bottom) - M0.Top;
			UAsdaTipPanel::Resize9Slice(FrameNine, FVector2D(bPageExpanded ? kExpandedW : kCollapsedW, Height), Origin);
		}
	}
}

// ── Чекбокс 743 «отслеживать задание» ───────────────────────────────────────────────────────────
// ★ Отслеживается ЗАДАНИЕ, а не ячейка: спрашиваем единственного владельца состояния — трекер.
bool UAsdaQuestLogSubsystem::IsSlotTracked(int32 SlotIdx) const
{
	const FAsdaQuestSlot* S = SlotAt(SlotIdx);
	if (!IsSlotBusy(S)) { return false; }
	const UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	return QM && QM->IsTrackedQuest(S->QuestNo);
}

// ★★★ ЛЕЧЕНИЕ Е1 — «у заданий без координат галочку вообще нельзя поставить».
//   Один и тот же построитель отвечает и на вопрос «положить запись», и на вопрос «а можно ли»:
//   `bSilent = true` считает всё то же самое и НИЧЕГО не пишет (эталонный аргумент `a3`).
bool UAsdaQuestLogSubsystem::CanTrackSlot(int32 SlotIdx)
{
	if (!IsSlotBusy(SlotAt(SlotIdx))) { return false; }
	return RebuildTrackerEntry(SlotIdx, /*bSilent=*/true);
}

// Ячейки журнала, чьи задания сейчас в трекере (вычисляется, не хранится).
TArray<int32> UAsdaQuestLogSubsystem::TrackedSlotIndices() const
{
	TArray<int32> Out;
	for (int32 i = 0; i < 12; ++i) { if (IsSlotTracked(i)) { Out.Add(i); } }
	return Out;
}

void UAsdaQuestLogSubsystem::OnTrackChanged(bool bIsChecked) { SetTrackSelected(bIsChecked); }

void UAsdaQuestLogSubsystem::SetTrackSelected(bool bTrack)
{
	// Чекбокс 743 всегда относится к ВЫБРАННОЙ ячейке — вся логика в общей точке ниже.
	SetSlotTracked(ActiveSlot, bTrack);
}

bool UAsdaQuestLogSubsystem::SetSlotTracked(int32 Slot, bool bTrack)
{
	// Тело — порт эталонного обработчика событий 67073 (галку поставили) / 67074 (сняли).
	if (Slot < 0 || !IsSlotBusy(SlotAt(Slot)))
	{
		if (TrackBox) { TrackBox->SetIsChecked(false); }
		return false;
	}
	if (!bTrack)
	{
		// Событие 67074: `questTracker_RemoveEntry(questNo)`, флаг слота в 0.
		if (UAsdaQuestMarkerSubsystem* QM = Marks())
		{
			if (const FAsdaQuestSlot* S = SlotAt(Slot)) { QM->TrackerRemoveEntry(S->QuestNo); }
		}
		SaveTrackedToSettings();
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отслеживание ячейки %d снято"), Slot);
		return true;
	}
	// FIX-ORIG D5: эталон вёл ОТДЕЛЬНЫЙ счётчик `g_nQuestEpisodeCount`, который в
	//   `questTrackerRefreshEpisodeVisibility` увеличивался БЕЗ проверки лимита и перерастал 6 —
	//   после этого чекбокс переставал работать навсегда («нотис 7008»), хотя в трекере строк
	//   было меньше шести. Считаем РЕАЛЬНО занятые записи, отдельного счётчика нет.
	const int32 MaxTracked = UAsdaQuestMarkerSubsystem::TrackerLimit();   // ★ ЕДИНАЯ точка лимита
	const int32 NowTracked = Marks() ? Marks()->GetTrackEntries().Num() : 0;
	if (!IsSlotTracked(Slot) && NowTracked + 1 > MaxTracked)
	{
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			// ★ ЧИСЛО В НОТИСЕ. Строка 7008 в таблицах игры написана под ЭТАЛОННЫЙ лимит
			//   («…до 6 заданий»), а лимит порта другой — поэтому в готовой локализованной строке
			//   подменяется само число. Своей выдуманной фразы не пишем: берётся ТА ЖЕ строка
			//   игры через общую точку `UAsdaTextSubsystem`, меняется только цифра. При откате
			//   `Asda.Quest.TrackerLimit 6` подмена не срабатывает (число совпало) и нотис
			//   остаётся дословно эталонным.
			FString Notice = Loc(QLoc::kTrackLimit);
			if (MaxTracked != UAsdaQuestMarkerSubsystem::kTrackerEtalonMaxEntries)
			{
				const FString Was = FString::FromInt(UAsdaQuestMarkerSubsystem::kTrackerEtalonMaxEntries);
				const FString Now = FString::FromInt(MaxTracked);
				if (Notice.Contains(Was)) { Notice.ReplaceInline(*Was, *Now); }
				else { Notice.Reset(); }
			}
			if (!Notice.IsEmpty()) { Msg->PostText(EAsdaMsgCategory::Quest, Notice); }
			else
			{
				// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран (строки 7008 нет / в ней нет эталонного числа) → «[#7008]».
				Msg->PostText(EAsdaMsgCategory::Quest, FAsdaLang::MissingTag(QLoc::kTrackLimit));
			}
		}
		if (TrackBox) { TrackBox->SetIsChecked(false); }
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] отслеживание отклонено: уже %d из %d"), NowTracked, MaxTracked);
		return false;
	}

	// ★ Эталон (событие 67073) НЕ добавляет запись сам: он взводит флаг слота и зовёт
	//   `QuestReward_UpdateProgressList`, а тот уже наполняет запись трекера строками целей.
	//   Если построитель вернул 0 — эталон СНИМАЕТ флаг обратно и гасит галку.
	if (!RebuildTrackerEntry(Slot))
	{
		// Построитель строк не дал ни одной цели — записи в трекере не появилось, значит и
		//   «помечено» нигде не осталось: отдельного флага, который надо было бы откатывать, нет.
		if (TrackBox) { TrackBox->SetIsChecked(false); }
		// ★★★ Е1: этот путь остаётся ЗАПАСНЫМ. В норме до него не доходит — галка у такого
		//   задания уже недоступна (`RefreshAll` → `CanTrackSlot`), и клик по ней не проходит.
		//   Сюда попадают только не-мышиные пути (консоль, восстановление из ini) и случай, когда
		//   задание перестало быть отслеживаемым между перерисовками. Приводим вид к факту.
		if (Slot == ActiveSlot) { ApplyTrackBoxEnabled(false); }
		// ★ НОТИСА ЗДЕСЬ НЕТ — и это эталонное поведение, а не забывчивость: обработчик 67073
		//   при `QuestReward_UpdateProgressList != 1` просто снимает флаг и делает `return 0`.
		//   Нотис 7008 эталон выдаёт ТОЛЬКО в двух случаях — исчерпан лимит и не выбрана ячейка.
		UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] ячейка %d: строк целей нет — отслеживание отменено"), Slot);
		return false;
	}
	SaveTrackedToSettings();
	UE_LOG(LogAsdaQuestLog, Log, TEXT("[журнал] ячейка %d поставлена на отслеживание (%d из %d)"),
		Slot, Marks() ? Marks()->GetTrackEntries().Num() : 0, MaxTracked);
	return true;
}
