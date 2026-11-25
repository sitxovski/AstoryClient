// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «СКЛАД + ЛИЧНАЯ ЛАВКА ВМЕСТЕ» (05.11.2026, баг игроков, сессия warehouse-shop-ui): `asda.SelfTest.WareStoreCombo [quit]`.
//   Жалоба: окно склада отображается неправильно, если открыть его вместе с окном личной лавки, и ошибка остаётся после
//   закрытия и повторного открытия. Шаги игрока: диалог кладовщика → Y (лавка 22000) → «Save Items» (склад 4000, режим 1) →
//   щелчок по предмету в лавке (выставить) → двойной щелчок по другому предмету склада (в корзину) → закрыть оба → открыть склад.
//   Путь игрока (закон `ui-check-game-path-geometry`): диалог NPC 47 «Кладовщик Кларк» (Альпен), хоткей 65 — боевой
//   `AAsdaPlayerController::RunCommandForCheck(65)`, ответ 6301 — штатный приём `DebugFeedPacket`, кнопка «Хранить» диалога —
//   `RunCommandForCheck(30011)` (та же ветка, что у кнопки), щелчки по ячейкам и крестикам — настоящие события мыши Slate
//   по живой геометрии, офлайн (-AsdaNoNet), исходящие пакеты — под перехватом.
//   Разделы:
//     A. КОНТРОЛЬ: склад ОДИН — открыть, снимок разметки окна 4000, закрыть крестиком, открыть — снимок совпадает;
//     B. ПУТЬ БАГА: диалог → Y → 6301 → «Хранить» → выставить в лавке → в корзину склада → закрыть оба → открыть склад —
//        снимок разметки = снимку раздела A, корзина пуста, режим 1, сетка = сумке;
//     C. обмен, магазин NPC (продажа/покупка), элитный почтовый ящик + гейт поля 5581 (свиток) с парами ключа `asda.Warehouse.GateRecallFlag`;
//     D. режим «Взять» + лавка: перенос между ячейками склада (5000) при лавке не уходит (107022), пары — ключ 0 и лавка закрыта.
//   ★ Исправление `asda.Warehouse.SelfStoreGate` (B2.10, D3) и контрольные пары с ключом 0 (B2.10к, D4).
//   Снимок — все узлы окна 4000: показан/скрыт, абсолютный прямоугольник, текст подписи, ресурс и прозрачность картинки.
//   Кадры — `Reports/warehouse_shop_combo/2026-11-05/frames/<язык>_<шаг>.png`. Сумка и окна возвращаются в конце.
//   Итог: «СКЛАД-ЛАВКА ИТОГ: ok=N fail=M». Карта Альпен (3), язык — `--lang ru|en`.
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaNetSubsystem.h"
#include "AsdaCVarSave.h"
#include "World/AsdaPlayerController.h"
#include "Test/AsdaSelfTestKit.h"
#include "Text/AsdaLang.h"
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaSelfStoreSubsystem.h"
#include "UI/AsdaWarehouseSubsystem.h"
#include "UI/AsdaShopDblClick.h"
#include "UI/AsdaTradeStateSubsystem.h"
#include "UI/AsdaExchangeP2PSubsystem.h"
#include "UI/AsdaPlayerMenuSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaPremiumPostSubsystem.h"
#include "World/AsdaWorldSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaWareStoreTest, Log, All);

namespace AsdaWareStoreComboTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.WareStoreCombo");   // имя команды
	static const TCHAR* kPfx = TEXT("СКЛАД-ЛАВКА");                    // префикс строк и итога
	static const FName kWareId(TEXT("WARE_WIN"));                       // окно склада 4000
	static const FName kSellId(TEXT("StreetStall_Open_Win"));           // окно подготовки лавки 22000
	static const FName kNumPadId(TEXT("NUMPAD_WIN"));                    // нумпад 1410 (id окна в реестре — как `UAsdaWindowSubsystem::kNumPadWindowId`)

	static constexpr int32 kKeeperNpc = 47;      // «Кладовщик Кларк», Alpen.npc idx 16 (кнопки 30011/30012)
	static constexpr int32 kShopNpc   = 104;     // «Бен» (Rein): магазин 30001/30002 и склад 30011 у одного NPC
	static constexpr int32 kPeer      = 901;     // фейковый сосед — партнёр обмена
	static constexpr int32 kSpend     = 2;       // контейнер «Расходники» (60 ячеек) — его показывают обе сетки
	static constexpr int32 kSpendN    = 60;      // ячеек контейнера 2
	// Предметы сумки (контейнер 2, ячейки 0…3): штучные — без нумпада, чтобы щелчок давал результат сразу.
	static constexpr int32 kItemSell  = 20592;   // ячейка 0 — выставляется в лавке (стопка 1)
	static constexpr int32 kItemKeep  = 20571;   // ячейка 1 — кладётся в корзину склада (стопка 1)
	static constexpr int32 kItemC     = 20572;   // ячейка 2 — фон (стопка 3)
	static constexpr int32 kItemD     = 20573;   // ячейка 3 — фон (стопка 2)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;      // очередь шагов `At` (★ R5-fix-b 13.11.2026 (заключительный заход))
		bool bQuit = false;                        // выйти после итога
		FString FrameDir;                          // папка кадров
		TArray<TArray<uint8>> Saved;               // контейнер 2, ячейки 0…59: сырые записи до прогона (пусто — была пуста)
		TArray<uint8> Saved5;                      // склад (контейнер 5), ячейка 0 до прогона (пусто — была пуста)
		TSharedPtr<FAsdaNetSelfScope> Self;        // свой персонаж (уровень) — вернуть в конце
		uint32 CapGen = 0;                         // поколение перехватчика исходящих
		TSharedRef<TArray<uint16>> Sent = MakeShared<TArray<uint16>>();   // перехваченные опкоды
		TArray<FString> Base;                      // снимок разметки окна 4000 раздела A (эталон «склад один»)

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaWareStoreTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaWareStoreTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
		int32 SentCount(uint16 Op) const { int32 N = 0; for (uint16 O : *Sent) { if (O == Op) { ++N; } } return N; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	// ★ R5-fix-b 13.11.2026 (заключительный заход): расписание — общая очередь `AsdaSelfTestSeq::At` (запись та же): шаг ждёт доставки заказанного
	//   клика/перетаскивания и тот же промежуток после предыдущего шага. Под нагрузкой (три игры, `rx_r5fix3`) мировой таймер
	//   проверял до отпускания перетаскивания (30 кадров удержания), следующий заказ затирал недоставленный — каскад провалов.
	using AsdaSelfTestSeq::At;
	// Центр живой геометрии узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Курсор в точку + событие движения.
	using AsdaSelfTestInput::Hover;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Увести курсор в нейтральную точку (левый верх вьюпорта), чтобы наведение не меняло снимок.
	static void Park()
	{
		if (!GEngine || !GEngine->GameViewport || !GEngine->GameViewport->GetGameViewportWidget().IsValid()) { return; }
		const FGeometry G = GEngine->GameViewport->GetGameViewportWidget()->GetCachedGeometry();
		Hover(G.LocalToAbsolute(FVector2D(2.f, 2.f)));
	}
	// Настоящий щелчок Slate.
	static void Click(const FCtxRef& C, const FVector2D& P) { if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { Hover(P); WM->SimulateClickAt(P, /*HoldFrames*/ 2); } }
	// Узел окна по началу имени.
	static UWidget* Node(const FCtxRef& C, FName WinId, const TCHAR* Pfx)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(WinId) : nullptr;
		return Win ? AsdaUi::FindNodeByPrefix(Win, Pfx) : nullptr;
	}
	// Щелчок по узлу окна (false — узла нет на экране).
	static bool ClickNode(const FCtxRef& C, FName WinId, const TCHAR* Pfx)
	{
		FVector2D P;
		if (!Center(Node(C, WinId, Pfx), P)) { return false; }
		Click(C, P);
		return true;
	}
	// Двойной щелчок по узлу окна.
	static bool DblClickNode(const FCtxRef& C, FName WinId, const TCHAR* Pfx)
	{
		FVector2D P;
		if (!Center(Node(C, WinId, Pfx), P)) { return false; }
		Hover(P);
		AsdaSimDoubleClickAt(C->Sub<UAsdaWindowSubsystem>(), P, false);
		return true;
	}
	// Кадр.
	static void Shot(const FCtxRef& C, const TCHAR* Tag)
	{
		const FString File = C->FrameDir / FString::Printf(TEXT("%s_%s.png"), FAsdaLang::Code(FAsdaLang::Current()), Tag);
		UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s: КАДР → %s"), kPfx, *File);
		FScreenshotRequest::RequestScreenshot(File, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	}
	// Положить запись в ячейку контейнера 2 (боевая точка приёма записи); Code 0 — очистить.
	static void Put(const FCtxRef& C, int32 Cell, int32 Code, int32 Qty)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		if (!I) { return; }
		I->ApplyExternalRecord(Code > 0 ? FAsdaItemRecordWriter::Cell(Code, kSpend, Cell, Qty) : FAsdaItemRecordWriter::ClearCell(kSpend, Cell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Узел виден на экране: он и ВСЕ его предки не скрыты (своя видимость узла под свёрнутым родителем ничего не значит).
	static bool ShownDeep(const UWidget* X)
	{
		for (const UWidget* P = X; P; P = P->GetParent()) { if (!AsdaUi::IsShown(P)) { return false; } }
		return X != nullptr;
	}
	// ПКМ по соседу → пункт меню «Обмен» 2004 (боевая цепочка меню). true — меню показано.
	static bool TradeMenu(const FCtxRef& C)
	{
		UAsdaPlayerMenuSubsystem* PM = C->Sub<UAsdaPlayerMenuSubsystem>();
		if (!PM) { return false; }
		PM->NotifyRightPress(kPeer);
		const bool bShown = PM->NotifyRightRelease(kPeer, false);
		PM->ClickByNodeName(TEXT("TRANTOOL_BUT04"));
		if (PM->IsMenuOpen()) { PM->CloseMenu(); }
		return bShown;
	}
	// Штатный приём пакета обмена + разбор очереди.
	static void FeedX(const FCtxRef& C, int32 Op, const TArray<uint8>& Body)
	{
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(Op, Body); }
		if (UAsdaExchangeP2PSubsystem* X = C->Sub<UAsdaExchangeP2PSubsystem>()) { X->Tick(0.f); }
	}
	// Тело 5283 (24 б): [0] код 1, [1..2] userNum инициатора, [3..22] имя, [23] тип 0.
	static TArray<uint8> Body5283()
	{
		TArray<uint8> B; B.SetNumZeroed(24);
		B[0] = 1; B[1] = kPeer & 0xFF; B[2] = (kPeer >> 8) & 0xFF; B[3] = 'P'; B[4] = 'e'; B[5] = 'e'; B[6] = 'r';
		return B;
	}
	// Тело 5285 (50 б): [0] код 1, [1] тип 0, [2..5] окно 3, [6..7]/[8..27] сторона партнёра, [28..29]/[30..49] своя сторона 500.
	static TArray<uint8> Body5285()
	{
		TArray<uint8> B; B.SetNumZeroed(50);
		B[0] = 1; B[2] = 3; B[6] = kPeer & 0xFF; B[7] = (kPeer >> 8) & 0xFF; B[8] = 'P';
		B[28] = 500 & 0xFF; B[29] = (500 >> 8) & 0xFF; B[30] = 'I';
		return B;
	}
	// Положить запись в ячейку склада (контейнер 5) боевой точкой приёма записи; Code 0 — вернуть запись до прогона (или очистить).
	static void Put5(const FCtxRef& C, int32 Cell, int32 Code, int32 Qty)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		if (!I) { return; }
		if (Code <= 0 && C->Saved5.Num() > 0) { I->ApplyExternalRecord(C->Saved5, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); return; }
		I->ApplyExternalRecord(Code > 0 ? FAsdaItemRecordWriter::Cell(Code, 5, Cell, Qty) : FAsdaItemRecordWriter::ClearCell(5, Cell),
			UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}
	// Перетаскивание ЛКМ путём игрока (настоящие события Slate) от узла окна склада к узлу окна склада.
	static bool DragNode(const FCtxRef& C, const TCHAR* FromPfx, const TCHAR* ToPfx)
	{
		FVector2D A, B;
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!WM || !Center(Node(C, kWareId, FromPfx), A) || !Center(Node(C, kWareId, ToPfx), B)) { return false; }
		Hover(A);
		WM->SimulateDrag(A, B, /*HoldFrames*/ 6);
		return true;
	}
	// Снимок разметки окна 4000: каждый узел — показан ли, прямоугольник, текст / ресурс и прозрачность картинки.
	static TArray<FString> Snapshot(const FCtxRef& C)
	{
		TArray<FString> Out;
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Win = WM ? WM->FindWindow(kWareId) : nullptr;
		if (!Win || !Win->WidgetTree) { Out.Add(TEXT("<окна 4000 нет>")); return Out; }
		TArray<UWidget*> All;
		Win->WidgetTree->GetAllWidgets(All);
		for (const UWidget* X : All)
		{
			if (!X) { continue; }
			if (X->GetName().StartsWith(TEXT("WARE_DragGhost"))) { continue; }   // призрак перетаскивания живёт только после первого броска, скрыт
			const bool bShown = ShownDeep(X);
			FString Line = FString::Printf(TEXT("%s показан=%d"), *X->GetName(), bShown ? 1 : 0);
			if (bShown)
			{
				FVector2D Mn, Mx;
				if (AsdaUi::WidgetAbsRect(X, Mn, Mx)) { Line += FString::Printf(TEXT(" [%.0f,%.0f-%.0f,%.0f]"), Mn.X, Mn.Y, Mx.X, Mx.Y); }
				if (const UTextBlock* T = Cast<UTextBlock>(X)) { Line += FString::Printf(TEXT(" текст=«%s»"), *T->GetText().ToString()); }
				if (const UImage* Im = Cast<UImage>(X))
				{
					const UObject* Res = Im->GetBrush().GetResourceObject();
					Line += FString::Printf(TEXT(" рис=%s α=%.2f"), Res ? *Res->GetName() : TEXT("-"), Im->GetColorAndOpacity().A);
				}
			}
			Out.Add(MoveTemp(Line));
		}
		return Out;
	}
	// Сравнить снимок с эталоном раздела A; в журнал — первые 25 расхождений.
	static bool SameAsBase(const FCtxRef& C, const TArray<FString>& Now, const TCHAR* Tag)
	{
		int32 Diff = 0;
		const int32 N = FMath::Max(Now.Num(), C->Base.Num());
		for (int32 i = 0; i < N; ++i)
		{
			const FString A = C->Base.IsValidIndex(i) ? C->Base[i] : TEXT("<нет>");
			const FString B = Now.IsValidIndex(i) ? Now[i] : TEXT("<нет>");
			if (A == B) { continue; }
			if (++Diff <= 25) { UE_LOG(LogAsdaWareStoreTest, Warning, TEXT("%s [%s] РАЗНИЦА #%d:\n   было  %s\n   стало %s"), kPfx, Tag, Diff, *A, *B); }
		}
		UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s [%s] узлов %d, расхождений с контролем %d"), kPfx, Tag, Now.Num(), Diff);
		return Diff == 0 && Now.Num() > 10;
	}
	// Подать пакет штатным приёмом и разобрать очередь лавки.
	static void Feed(const FCtxRef& C, int32 Op, const TArray<uint8>& Body)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>();
		if (!Net || !S) { return; }
		Net->DebugFeedPacket(Op, Body);
		S->PumpPending();
	}
	// 6306 код 1 (лавка закрыта сервером), записей нет.
	static void Feed6306(const FCtxRef& C) { TArray<uint8> B; B.SetNumZeroed(571); B[0] = 1; Feed(C, 6306, B); }
	// Хоткей 65 (Y) боевой точкой контроллера → 6300 → ответ 6301 код 1.
	static void PressY(const FCtxRef& C)
	{
		AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
		if (PC) { PC->RunCommandForCheck(65); }
		TArray<uint8> B; B.Add(1);
		Feed(C, 6301, B);
	}
	// Разговор с кладовщиком и кнопка «Хранить» (30011).
	static void OpenKeeperKeep(const FCtxRef& C)
	{
		UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
		if (!D) { return; }
		if (!D->IsDialogOpen() || D->GetCurrentNpcId() != kKeeperNpc) { D->OpenForNpcId(kKeeperNpc); }
		D->RunCommandForCheck(30011, 0);
	}
	// Закрыть всё, что мог открыть шаг (без проверок).
	static void CloseAll(const FCtxRef& C)
	{
		if (UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>()) { if (Wh->IsOpen()) { Wh->CloseWindow(); } }
		if (UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>()) { if (S->IsSellWindowOpen()) { Feed6306(C); } }
		if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { if (WM->IsNumPadOpen()) { WM->CloseNumPad(false); } WM->ClearMsgBoxQueue(TEXT("самотест склад+лавка")); }
		if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { if (D->IsDialogOpen()) { D->Close(); } }
	}
	// Сумка раздела: 4 предмета в ячейках 0…3, остальное пусто.
	static void FillBag(const FCtxRef& C)
	{
		for (int32 i = 0; i < kSpendN; ++i) { Put(C, i, 0, 0); }
		Put(C, 0, kItemSell, 1);
		Put(C, 1, kItemKeep, 1);
		Put(C, 2, kItemC, 3);
		Put(C, 3, kItemD, 2);
	}

	static void Finish(const FCtxRef& C)
	{
		CloseAll(C);
		if (UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>())
		{
			for (int32 i = 0; i < kSpendN && i < C->Saved.Num(); ++i)
			{
				if (C->Saved[i].Num() > 0) { I->ApplyExternalRecord(C->Saved[i], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Put(C, i, 0, 0); }
			}
		}
		if (UAsdaExchangeP2PSubsystem* X = C->Sub<UAsdaExchangeP2PSubsystem>()) { X->ResetSessionState(EAsdaSessionResetReason::LeftWorld); }
		if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); }
		if (C->Self.IsValid()) { C->Self->Restore(); }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaWareStoreTest, kPfx, C->Ok, C->Fail);
	}

	// Одно сочетание раздела C: открыть «второе» окно Open2 при открытом складе (или склад после него), закрыть оба, открыть
	//   склад один — снимок = контролю. Open2/Close2 — путь игрока чужого окна.
	struct FCombo
	{
		const TCHAR* Name;                         // подпись сочетания
		TFunction<void(const FCtxRef&)> Open2;     // открыть второе окно
		TFunction<void(const FCtxRef&)> Close2;    // закрыть второе окно
	};

	// Проба «склад принимает щелчки»: разговор → «Хранить» → двойной щелчок → предмет в корзине (Tag — место в сценарии). Возвращает время конца.
	static float Probe(const FCtxRef& C, float T, const TCHAR* Tag)
	{
		const FString S(Tag);
		At(C, T + 0.0f, [C]() { OpenKeeperKeep(C); Park(); });
		At(C, T + 0.6f, [C]() { DblClickNode(C, kWareId, TEXT("WAREKEEP_TRAY01_")); });
		At(C, T + 1.4f, [C, S]()
		{
			const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
			const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			const FAsdaItemInstance* It = C->Sub<UAsdaInventorySubsystem>()->GetCell(kSpend, 1);
			C->Expect(Wh->DumpState().Contains(TEXT("корзина[0]")), FString::Printf(TEXT("%s проба: двойной щелчок кладёт в корзину (модалка=%d, ячейка 1 код=%d кол=%d) %s"),
				*S, WM->IsAnyModalOpen() ? 1 : 0, It ? It->ItemCode : -1, It ? It->Amount : -1, *Wh->DumpState()));
			CloseAll(C);
			FillBag(C);
		});
		return T + 1.8f;
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* I = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!I || !Net || !C->Sub<UAsdaWarehouseSubsystem>() || !C->Sub<UAsdaSelfStoreSubsystem>())
		{
			C->Expect(false, TEXT("подсистемы на месте"));
			Finish(C);
			return;
		}
		C->Self = MakeShared<FAsdaNetSelfScope>(Net, kCmd);
		Net->SelfLevel = 30;   // лавка требует уровень ≥ 10 (107080)
		C->Saved.SetNum(kSpendN);
		for (int32 i = 0; i < kSpendN; ++i)
		{
			const FAsdaItemInstance* It = I->GetCell(kSpend, i);
			C->Saved[i] = (It && !It->IsEmpty()) ? It->Raw : TArray<uint8>();
		}
		if (const FAsdaItemInstance* It5 = I->GetCell(5, 0)) { if (!It5->IsEmpty()) { C->Saved5 = It5->Raw; } }
		TSharedRef<TArray<uint16>> Sent = C->Sent;
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [Sent](uint16 Op, const uint8*, int32, bool, TCHAR) { Sent->Add(Op); }, 0, kCmd);
		CloseAll(C);
		FillBag(C);

		// ═══ A. КОНТРОЛЬ: склад один ═══
		At(C, 0.3f, [C]() { OpenKeeperKeep(C); });
		At(C, 1.2f, [C]()
		{
			const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
			C->Expect(Wh && Wh->IsOpen(), TEXT("A1 диалог кладовщика → «Хранить» 30011 → склад 4000 открыт"));
			Park();
		});
		At(C, 1.6f, [C]()
		{
			C->Base = Snapshot(C);
			UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s A: контрольный снимок — узлов %d\n%s"), kPfx, C->Base.Num(),
				*C->Sub<UAsdaWarehouseSubsystem>()->DumpState());
			Shot(C, TEXT("A1_ware_alone"));
		});
		At(C, 2.2f, [C]() { C->Expect(ClickNode(C, kWareId, TEXT("WARE_CLOBUT")), TEXT("A2 крестик 4050 склада на экране — щелчок")); });
		At(C, 2.8f, [C]()
		{
			const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
			C->Expect(Wh && !Wh->IsOpen(), TEXT("A3 склад закрыт крестиком"));
			OpenKeeperKeep(C);
		});
		At(C, 3.6f, [C]() { Park(); });
		At(C, 4.0f, [C]()
		{
			C->Expect(SameAsBase(C, Snapshot(C), TEXT("A")), TEXT("A4 (контроль) повторное открытие склада одного — разметка = первой"));
			Shot(C, TEXT("A4_ware_alone_reopen"));
		});
		At(C, 4.3f, [C]() { CloseAll(C); });

		// ═══ B. ПУТЬ БАГА — четыре варианта, после каждого склад открывается заново и сверяется с контролем A ═══
		//   1 — как в жалобе: диалог → Y → «Хранить» → щелчок по штучному в лавке → двойной по штучному в складе → крестики;
		//   2 — стопки через нумпад (лавка: режим 7, склад: режим 4), кнопка «OK» лавки (6302 молча — идёт разговор, эталон),
		//       «Положить» 4002 → 5008 → ответ живого сервера при открытой подготовке лавки: 5009 код 0 (гейт `sub_52BFF0`,
		//       причина 4 «участвует в лавке» — `+0x145` ставит Ready 6300), лавка — «Отмена» 22501, склад — ESC;
		//   3 — обратный порядок: склад первым, лавка второй, закрыть лавку первой;
		//   4 — разговор закрыт крестиком диалога при обоих открытых окнах (уход от NPC), склад — снова через диалог.
		float T = 4.6f;
		for (int32 V = 1; V <= 4; ++V)
		{
			const FString Tag = FString::Printf(TEXT("B%d"), V);
			At(C, T + 0.0f, [C, V, Tag]()
			{
				C->Sent->Reset();
				UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
				if (D) { D->OpenForNpcId(kKeeperNpc); }
				C->Expect(D && D->IsDialogOpen(), Tag + TEXT(".1 диалог кладовщика открыт"));
				if (V == 3) { OpenKeeperKeep(C); } else { PressY(C); }
			});
			At(C, T + 0.6f, [C, V, Tag]()
			{
				if (V == 3) { PressY(C); } else { OpenKeeperKeep(C); }
				const UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>();
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				C->Expect(C->SentCount(6300) == 1 && S && S->IsSellWindowOpen() && Wh && Wh->IsOpen(),
					FString::Printf(TEXT("%s.2 при разговоре: Y → 6300 (%d) → 6301 → лавка 22000 и «Хранить» → склад 4000 — оба открыты"), *Tag, C->SentCount(6300)));
				Park();
			});
			At(C, T + 1.2f, [C, V, Tag]()
			{
				Shot(C, *(Tag + TEXT("_both_open")));
				const TCHAR* SellCell = (V == 2) ? TEXT("Stall_Open_Inven02_") : TEXT("Stall_Open_Inven00_");
				C->Expect(ClickNode(C, kSellId, SellCell), Tag + TEXT(".3 ячейка сетки лавки — щелчок (выставить)"));
			});
			At(C, T + 1.8f, [C, V, Tag]()
			{
				if (V == 2)
				{
					UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
					C->Expect(WM && WM->IsNumPadOpen(), Tag + TEXT(".3н стопка 3 → нумпад режим 7"));
					ClickNode(C, kNumPadId, TEXT("NUMPAD_NO01"));   // цифра 2
				}
			});
			At(C, T + 2.3f, [C, V, Tag]()
			{
				if (V == 2) { ClickNode(C, kNumPadId, TEXT("NUMPAD_OK")); }
			});
			At(C, T + 2.8f, [C, V, Tag]()
			{
				const UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>();
				C->Expect(S && S->GetSellCount() == 1, FString::Printf(TEXT("%s.4 предмет выставлен в лавку (строк %d)"), *Tag, S ? S->GetSellCount() : -1));
				const TCHAR* WareCell = (V == 2) ? TEXT("WAREKEEP_TRAY03_") : TEXT("WAREKEEP_TRAY01_");
				C->Expect(DblClickNode(C, kWareId, WareCell), Tag + TEXT(".5 ячейка сетки склада — двойной щелчок (в корзину)"));
			});
			At(C, T + 3.4f, [C, V, Tag]()
			{
				if (V == 2)
				{
					UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
					C->Expect(WM && WM->IsNumPadOpen(), Tag + TEXT(".5н стопка 2 → нумпад режим 4"));
					ClickNode(C, kNumPadId, TEXT("NUMPAD_NO00"));   // цифра 1
				}
			});
			At(C, T + 3.9f, [C, V, Tag]()
			{
				if (V == 2) { ClickNode(C, kNumPadId, TEXT("NUMPAD_OK")); }
			});
			At(C, T + 4.4f, [C, V, Tag]()
			{
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s %s: после корзины\n%s"), kPfx, *Tag, Wh ? *Wh->DumpState() : TEXT("-"));
				C->Expect(Wh && Wh->DumpState().Contains(TEXT("корзина[0]")), Tag + TEXT(".6 предмет в корзине склада"));
				Park();
				Shot(C, *(Tag + TEXT("_sell_and_basket")));
				if (V == 2)
				{
					C->Sent->Reset();
					C->Expect(ClickNode(C, kSellId, TEXT("Stall_Open_OKBtn")), Tag + TEXT(".7 «OK» лавки — щелчок"));
				}
			});
			At(C, T + 5.0f, [C, V, Tag]()
			{
				if (V != 2) { return; }
				C->Expect(C->SentCount(6302) == 0, FString::Printf(TEXT("%s.8 «OK» при разговоре с NPC — 6302 не уходит (эталон, g_bInventoryLocked): %d"), *Tag, C->SentCount(6302)));
				C->Expect(ClickNode(C, kWareId, TEXT("WAREKEEP_BUT")), Tag + TEXT(".9 «Положить» 4002 — щелчок"));
			});
			At(C, T + 5.6f, [C, V, Tag]()
			{
				if (V != 2) { return; }
				// ★ Исправление `asda.Warehouse.SelfStoreGate 1`: 5008 не уходит, в чат — 107022 «Ты в своей лавке».
				const UAsdaTradeStateSubsystem* TS = C->Sub<UAsdaTradeStateSubsystem>();
				C->Expect(C->SentCount(5008) == 0 && TS && TS->GetLastStoreGateLoc() == 107022 && TS->GetLastStoreGateWho().Contains(TEXT("4002")),
					FString::Printf(TEXT("%s.10 «Положить» при открытой лавке → 5008 НЕ уходит (%d), в чат 107022 (последний гейт loc %d «%s»)"), *Tag,
						C->SentCount(5008), TS ? TS->GetLastStoreGateLoc() : -1, TS ? *TS->GetLastStoreGateWho() : TEXT("-")));
				// Контрольная пара: ключ 0 — как в оригинале, 5008 уходит.
				AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.SelfStoreGate"), 0);
				C->Expect(ClickNode(C, kWareId, TEXT("WAREKEEP_BUT")), Tag + TEXT(".10к (ключ 0) «Положить» — щелчок"));
			});
			At(C, T + 6.2f, [C, V, Tag]()
			{
				if (V != 2) { return; }
				C->Expect(C->SentCount(5008) == 1, FString::Printf(TEXT("%s.10к (ключ 0, оригинал) «Положить» → 5008 уходит (%d)"), *Tag, C->SentCount(5008)));
				TArray<uint8> R; R.SetNumZeroed(113); R[0] = 0;   // ответ живого сервера: отказ гейта `sub_52BFF0` (причина 4, участвует в лавке)
				if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { Net->DebugFeedPacket(5009, R); }
				AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.SelfStoreGate"), 1);
			});
			At(C, T + 6.8f, [C, V, Tag]()
			{
				// Закрытие. 1 — крестики (склад, затем лавка); 2 — «Отмена» лавки, склад ESC; 3 — лавка первой, затем склад;
				//   4 — крестик ДИАЛОГА (уход от NPC) при обоих открытых.
				C->Sent->Reset();
				UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
				if (V == 1) { ClickNode(C, kWareId, TEXT("WARE_CLOBUT")); }
				if (V == 2) { ClickNode(C, kSellId, TEXT("Stall_Open_CancelBtn")); }
				if (V == 3) { ClickNode(C, kSellId, TEXT("Stall_Open_ClsBtn")); }
				if (V == 4) { if (UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>()) { D->Close(); } }
				(void)WM;
			});
			At(C, T + 7.4f, [C, V, Tag]()
			{
				UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
				if (V == 1) { ClickNode(C, kSellId, TEXT("Stall_Open_ClsBtn")); }
				// Вариант 2: ESC — склада 4000 в эталонной ESC-таблице НЕТ (`kEscCloseWindows`), ESC его не закрывает и в оригинале;
				//   поэтому после ESC — крестик склада.
				if (V == 2) { Feed6306(C); if (WM) { WM->CloseTopmostOnEsc(); } ClickNode(C, kWareId, TEXT("WARE_CLOBUT")); }
				if (V == 3) { Feed6306(C); ClickNode(C, kWareId, TEXT("WARE_CLOBUT")); }
			});
			At(C, T + 8.0f, [C, V, Tag]()
			{
				UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>();
				if (V == 1 || V == 4) { if (S && S->IsSellWindowOpen()) { Feed6306(C); } }
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				C->Expect(S && !S->IsSellWindowOpen() && Wh && !Wh->IsOpen(),
					FString::Printf(TEXT("%s.11 оба окна закрыты (лавка=%d склад=%d, 6305 ушло %d)"), *Tag,
						S && S->IsSellWindowOpen() ? 1 : 0, Wh && Wh->IsOpen() ? 1 : 0, C->SentCount(6305)));
				Park();
				Shot(C, *(Tag + TEXT("_both_closed")));
			});
			At(C, T + 8.6f, [C]() { OpenKeeperKeep(C); Park(); });
			At(C, T + 9.2f, [C, V, Tag]()
			{
				Shot(C, *(Tag + TEXT("_ware_reopen")));
				UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s %s: склад заново\n%s"), kPfx, *Tag, *C->Sub<UAsdaWarehouseSubsystem>()->DumpState());
				C->Expect(SameAsBase(C, Snapshot(C), *Tag), Tag + TEXT(".12 склад открыт заново — разметка = контролю A (склад один)"));
			});
			At(C, T + 9.6f, [C]() { CloseAll(C); FillBag(C); });
			T += 10.2f;
		}
		// ═══ D. РЕЖИМ «ВЗЯТЬ» + ЛАВКА: перенос между ячейками склада (5000) — сервер отвергает при лавке (5001 код 18) ═══
		At(C, T + 0.0f, [C]()
		{
			C->Sent->Reset();
			Put5(C, 0, kItemKeep, 1);   // предмет в ячейке 0 склада (контейнер 5) — его и тащим
			UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
			if (D) { D->OpenForNpcId(kKeeperNpc); }
			PressY(C);
			if (D) { D->RunCommandForCheck(30012, 0); }   // «Взять вещи»
			const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
			const UAsdaSelfStoreSubsystem* S = C->Sub<UAsdaSelfStoreSubsystem>();
			C->Expect(Wh && Wh->IsOpen() && S && S->IsSellWindowOpen(), TEXT("D1 диалог → Y → «Взять» 30012: лавка и склад (режим 0) открыты"));
			// Ответ живого сервера на запрос страницы 4050 — конец выдачи 4052: без него флаг «запрос в полёте» держал бы склад (эталон +22328).
			if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>()) { TArray<uint8> E; E.SetNumZeroed(4); Net->DebugFeedPacket(4052, E); }
			Park();
		});
		At(C, T + 0.8f, [C]()
		{
			Shot(C, TEXT("D1_take_and_shop"));
			C->Expect(DragNode(C, TEXT("WAREKEEP_TRAY00_"), TEXT("WAREKEEP_TRAY01_")), TEXT("D2 перетаскивание ячейка 0 → ячейка 1 склада (лавка открыта)"));
		});
		At(C, T + 1.8f, [C]()
		{
			const UAsdaTradeStateSubsystem* TS = C->Sub<UAsdaTradeStateSubsystem>();
			C->Expect(C->SentCount(5000) == 0 && TS && TS->GetLastStoreGateLoc() == 107022 && TS->GetLastStoreGateWho().Contains(TEXT("5000")),
				FString::Printf(TEXT("D3 перенос при открытой лавке → 5000 НЕ уходит (%d), в чат 107022 («%s»)"), C->SentCount(5000), TS ? *TS->GetLastStoreGateWho() : TEXT("-")));
			AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.SelfStoreGate"), 0);
			DragNode(C, TEXT("WAREKEEP_TRAY00_"), TEXT("WAREKEEP_TRAY01_"));
		});
		At(C, T + 2.8f, [C]()
		{
			C->Expect(C->SentCount(5000) == 1, FString::Printf(TEXT("D4 (ключ 0, оригинал) тот же перенос → 5000 уходит (%d)"), C->SentCount(5000)));
			AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.SelfStoreGate"), 1);
			C->Sent->Reset();
			C->Expect(ClickNode(C, kSellId, TEXT("Stall_Open_ClsBtn")), TEXT("D5 крестик лавки"));
		});
		At(C, T + 3.4f, [C]()
		{
			Feed6306(C);
			DragNode(C, TEXT("WAREKEEP_TRAY00_"), TEXT("WAREKEEP_TRAY01_"));
		});
		At(C, T + 4.4f, [C]()
		{
			C->Expect(C->SentCount(5000) == 1, FString::Printf(TEXT("D6 (контроль) лавка закрыта, ключ 1 → перенос 5000 уходит (%d)"), C->SentCount(5000)));
			CloseAll(C);
			Put5(C, 0, 0, 0);
		});
		T += 5.0f;
		T = Probe(C, T, TEXT("P1 после раздела D"));
		// ═══ C. СОЧЕТАНИЯ С ПРОЧИМИ ОКНАМИ ТОРГОВЛИ ═══
		//   C1–C3 гейт поля 5581 (свиток возврата, окно 4000 закрыто) у обмена и ремесла, пары — ключ 0;
		//   C4 склад кладовщика → обмен: меню 5043, «Принять» 5044 (разговор); C5 окно обмена 2008 → разговор с NPC не открывается;
		//   C6/C7 магазин NPC (продажа/покупка) + склад; C8/C9 элитный почтовый ящик + склад в обоих порядках.
		//   Клановый склад не проверяется: такой системы нет (окна нет в `.JUI`, таблиц в Joy1, кода в клиенте и сервере).
		At(C, T + 0.0f, [C]()
		{
			UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
			Net->SelfUserNum = 500; Net->bSelfUserNumKnown = true;
			if (UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>()) { WS->DebugSpawnFakePeer(kPeer, TEXT("Партнёр"), FVector2D(8260.f, 35160.f)); }
			if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>()) { Hb->DumpNoticeIcons(); }
			CloseAll(C);
			FillBag(C);
			C->Sent->Reset();
			UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
			Wh->OpenRecallPanel();
			C->Expect(Wh->IsRecallActive() && !Wh->IsOpen(), TEXT("C0 свиток возврата: панель 5500, окно 4000 закрыто"));
			const bool bMenu = TradeMenu(C);
			C->Expect(bMenu && C->SentCount(5282) == 0 && C->Sub<UAsdaExchangeP2PSubsystem>()->GetLastNoticeLoc() == 10307,
				FString::Printf(TEXT("C1 обмен при свитке (окно 4000 закрыто) → 10307, заявки нет (5282=%d, нотис %d)"), C->SentCount(5282), C->Sub<UAsdaExchangeP2PSubsystem>()->GetLastNoticeLoc()));
			AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.GateRecallFlag"), 0);
			TradeMenu(C);
			C->Expect(C->SentCount(5282) == 1, FString::Printf(TEXT("C2 (ключ 0, прежний порт) тот же обмен → 5282 уходит (%d)"), C->SentCount(5282)));
			C->Sub<UAsdaExchangeP2PSubsystem>()->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			UAsdaCraftWindowSubsystem* Cw = C->Sub<UAsdaCraftWindowSubsystem>();
			Cw->GateAndToggle(TEXT("самотест склад+лавка, ключ 0"));
			const bool bOld = Cw->IsOpen();
			if (bOld) { Cw->HideAndCleanup(); }
			AsdaCVarSave::SetForTest(TEXT("asda.Warehouse.GateRecallFlag"), 1);
			Cw->GateAndToggle(TEXT("самотест склад+лавка, ключ 1"));
			const bool bNew = Cw->IsOpen();
			if (bNew) { Cw->HideAndCleanup(); }
			C->Expect(bOld && !bNew, FString::Printf(TEXT("C3 ремесло при свитке: ключ 1 молча не открывается ↔ ключ 0 открывается (ключ 1=%d ключ 0=%d)"), bNew ? 1 : 0, bOld ? 1 : 0));
			Wh->CloseRecallPanel();
		});
		T = Probe(C, T + 0.6f, TEXT("P2 после свитка")) - 0.6f;
		At(C, T + 0.6f, [C]()
		{
			C->Sent->Reset();
			OpenKeeperKeep(C);
			UAsdaExchangeP2PSubsystem* X = C->Sub<UAsdaExchangeP2PSubsystem>();
			TradeMenu(C);
			C->Expect(C->SentCount(5282) == 0 && X->GetLastNoticeLoc() == 5043, FString::Printf(TEXT("C4а склад кладовщика → меню обмена: 5043 (разговор), заявки нет (5282=%d, нотис %d)"), C->SentCount(5282), X->GetLastNoticeLoc()));
			FeedX(C, 5283, Body5283());
			if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>()) { Hb->HandleNoticeIconClick(4); }
			if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->AnswerMsgBox(true, true); }
			C->Expect(X->GetLastNoticeLoc() == 5044 && !X->IsWindowShown(), FString::Printf(TEXT("C4б входящий обмен при складе: «Принять» → 5044, окна нет (нотис %d)"), X->GetLastNoticeLoc()));
			X->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
			CloseAll(C);
		});
		T = Probe(C, T + 1.2f, TEXT("P3 после отказов обмена")) - 1.2f;
		At(C, T + 1.2f, [C]()
		{
			UAsdaExchangeP2PSubsystem* X = C->Sub<UAsdaExchangeP2PSubsystem>();
			FeedX(C, 5283, Body5283());
			if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>()) { Hb->HandleNoticeIconClick(4); }
			if (UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>()) { WM->AnswerMsgBox(true, true); }
			FeedX(C, 5285, Body5285());
			C->Expect(X->IsWindowShown(), TEXT("C5а окно обмена 2008 открыто"));
			Park();
		});
		At(C, T + 1.8f, [C]()
		{
			Shot(C, TEXT("C5_exchange"));
			UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
			D->OpenForNpcId(kKeeperNpc);
			C->Expect(!D->IsDialogOpen() && !C->Sub<UAsdaWarehouseSubsystem>()->IsOpen(), TEXT("C5б при окне обмена разговор с кладовщиком не открывается, склада нет"));
			C->Sub<UAsdaExchangeP2PSubsystem>()->CloseByCancelButton(TEXT("самотест склад+лавка"));
			C->Sub<UAsdaExchangeP2PSubsystem>()->ResetSessionState(EAsdaSessionResetReason::LeftWorld);
		});
		At(C, T + 2.4f, [C]() { OpenKeeperKeep(C); Park(); });
		At(C, T + 3.0f, [C]()
		{
			C->Expect(SameAsBase(C, Snapshot(C), TEXT("C5")), TEXT("C5в после обмена склад открыт — разметка = контролю A"));
		});
		At(C, T + 3.4f, [C]() { CloseAll(C); FillBag(C); });
		T += 3.8f;
		T = Probe(C, T, TEXT("P4 после окна обмена"));
		// C6/C7 магазин NPC: у Бена (NPC 104) есть и магазин, и склад. 30002 — продажа, 30001 — покупка.
		for (int32 V = 6; V <= 7; ++V)
		{
			const FString Tag = FString::Printf(TEXT("C%d"), V);
			const int32 Cmd = (V == 6) ? 30002 : 30001;
			At(C, T + 0.0f, [C, Cmd, Tag]()
			{
				UAsdaNpcDialogSubsystem* D = C->Sub<UAsdaNpcDialogSubsystem>();
				D->OpenForNpcId(kShopNpc);
				D->RunCommandForCheck(Cmd, 0);
				const UAsdaNpcShopSubsystem* Sh = C->Sub<UAsdaNpcShopSubsystem>();
				C->Expect(D->IsDialogOpen() && Sh && Sh->IsOpen(), FString::Printf(TEXT("%s.1 Бен: команда %d → магазин NPC открыт (тип %d)"), *Tag, Cmd, Sh ? Sh->GetShopType() : -1));
				D->RunCommandForCheck(30011, 0);
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				C->Expect(Wh->IsOpen() && Sh->IsOpen(), Tag + TEXT(".2 «Хранить» при магазине: склад и магазин открыты"));
				Park();
			});
			At(C, T + 0.6f, [C, Tag]()
			{
				Shot(C, *(Tag + TEXT("_shop_and_ware")));
				C->Expect(DblClickNode(C, kWareId, TEXT("WAREKEEP_TRAY01_")), Tag + TEXT(".3 ячейка склада — двойной щелчок"));
			});
			At(C, T + 1.2f, [C, Tag]()
			{
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				C->Expect(Wh->DumpState().Contains(TEXT("корзина[0]")), Tag + TEXT(".4 предмет в корзине склада при открытом магазине"));
				UAsdaNpcShopSubsystem* Sh = C->Sub<UAsdaNpcShopSubsystem>();
				FVector2D P;
				if (Center(Sh->DebugGridIcon(0), P)) { Click(C, P); }
			});
			At(C, T + 1.8f, [C, Tag]()
			{
				C->Expect(ClickNode(C, kWareId, TEXT("WARE_CLOBUT")), Tag + TEXT(".5 крестик склада"));
				C->Sub<UAsdaNpcShopSubsystem>()->CloseShop();
			});
			At(C, T + 2.4f, [C]() { C->Sub<UAsdaNpcDialogSubsystem>()->RunCommandForCheck(30011, 0); Park(); });
			At(C, T + 3.0f, [C, Tag]()
			{
				C->Expect(SameAsBase(C, Snapshot(C), *Tag), Tag + TEXT(".6 склад после магазина открыт заново — разметка = контролю A"));
			});
			At(C, T + 3.4f, [C]() { CloseAll(C); FillBag(C); });
			T += 3.8f;
		}
		// C8/C9 элитный почтовый ящик 69000: 8 — склад первым, 9 — ящик первым.
		for (int32 V = 8; V <= 9; ++V)
		{
			const FString Tag = FString::Printf(TEXT("C%d"), V);
			At(C, T + 0.0f, [C, V, Tag]()
			{
				UAsdaPremiumPostSubsystem* Po = C->Sub<UAsdaPremiumPostSubsystem>();
				if (V == 9) { Po->ToggleWindow(); }
				OpenKeeperKeep(C);
				if (V == 8) { Po->ToggleWindow(); }
				const UAsdaWarehouseSubsystem* Wh = C->Sub<UAsdaWarehouseSubsystem>();
				UE_LOG(LogAsdaWareStoreTest, Log, TEXT("%s %s: ящик виден=%d склад=%d разговор=%d"), kPfx, *Tag, Po->IsWindowVisible() ? 1 : 0, Wh->IsOpen() ? 1 : 0, C->Sub<UAsdaNpcDialogSubsystem>()->IsDialogOpen() ? 1 : 0);
				C->Expect(Wh->IsOpen(), Tag + TEXT(".1 склад открыт рядом с элитным ящиком"));
				Park();
			});
			At(C, T + 0.6f, [C, Tag]()
			{
				Shot(C, *(Tag + TEXT("_post_and_ware")));
				C->Expect(DblClickNode(C, kWareId, TEXT("WAREKEEP_TRAY01_")), Tag + TEXT(".2 ячейка склада — двойной щелчок"));
			});
			At(C, T + 1.2f, [C, Tag]()
			{
				C->Expect(C->Sub<UAsdaWarehouseSubsystem>()->DumpState().Contains(TEXT("корзина[0]")), Tag + TEXT(".3 предмет в корзине склада"));
				C->Expect(ClickNode(C, kWareId, TEXT("WARE_CLOBUT")), Tag + TEXT(".4 крестик склада"));
				UAsdaPremiumPostSubsystem* Po = C->Sub<UAsdaPremiumPostSubsystem>();
				if (Po->IsWindowVisible()) { Po->CloseWindow(); }
			});
			At(C, T + 1.8f, [C]() { OpenKeeperKeep(C); Park(); });
			At(C, T + 2.4f, [C, Tag]()
			{
				C->Expect(SameAsBase(C, Snapshot(C), *Tag), Tag + TEXT(".5 склад после ящика открыт заново — разметка = контролю A"));
			});
			At(C, T + 2.8f, [C]() { CloseAll(C); FillBag(C); });
			T += 3.2f;
		}
		At(C, T + 0.2f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestWareStoreCombo(
	TEXT("asda.SelfTest.WareStoreCombo"),
	TEXT("Самотест бага игроков 05.11.2026: склад кладовщика вместе с личной лавкой (Y) — после закрытия обоих склад открывается "
	     "с той же разметкой, что и один. Путь игрока, офлайн, Альпен. Кадры — Reports/warehouse_shop_combo/2026-11-05/frames. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaWareStoreComboTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 82 351"));
			const FString Again = FString::Printf(TEXT("%s selfready%s"), kCmd, KA.bQuit ? TEXT(" quit") : TEXT(""));
			TWeakObjectPtr<UWorld> WW(W);
			FTimerHandle H;
			W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([WW, Again]() { if (WW.IsValid() && GEngine) { GEngine->Exec(WW.Get(), *Again); } }), 2.f, false);
			return;
		}
		AsdaCVarSave::ArmWorldGuard(W, kCmd);
		if (AsdaSelfTestKit::RefuseStart(W, kCmd, KA)) { return; }
		if (!W) { return; }
		FCtxRef C = MakeShared<FCtx>();
		C->W = W;
		C->bQuit = KA.bQuit;
		C->FrameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../Reports/warehouse_shop_combo/2026-11-05/frames"));
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
