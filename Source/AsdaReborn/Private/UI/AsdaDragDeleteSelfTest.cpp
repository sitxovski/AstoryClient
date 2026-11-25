// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «БРОСОК ПРЕДМЕТА ВНЕ ОКОН = ВОПРОС УДАЛЕНИЯ» ПУТЁМ ИГРОКА (26.10.2026, баг игроков «Dragging an item outside of
//   the inventory window fails to delete or drop it… leaves it unaffected or snaps it back»). [ОРИГИНАЛ]
//
//   ЧТО БЫЛО. `UAsdaInventorySubsystem::EndDrag` при броске мимо ячеек/корзины/панели молча возвращал предмет («эталон ничего не
//   делает» — ложное «нет»: ветку `a3 == nullptr` эталона `inventoryWin_OnUiCommand@0xD17A80` не смотрели). Исправление —
//   `DropOutsideWindows` (`AsdaInventorySubsystem_Use.cpp`, ключ `asda.Inven.DropOutsideDelete`).
//
//   ПУТЬ ИГРОКА (законы diagnostic-must-use-real-path, ui-check-game-path-geometry): перетаскивания и щелчки — НАСТОЯЩИЕ события
//   Slate (`UAsdaWindowSubsystem::SimulateDrag/SimulateClickAt`) по ЖИВОЙ геометрии ячеек, корзины 405, кнопок окон-сообщений и
//   слота панели быстрого доступа; клавиши окна количества — `ProcessKeyDownEvent/ProcessKeyCharEvent` (как WM_KEYDOWN/WM_CHAR).
//   Точка «в мире» — экранная точка вьюпорта, над которой общий хит-тест интерфейса не видит ни одного окна (тот же
//   `IsScreenPointOverAnyUiWindow`, что решает и у боевого кода, и у хотбара). 5006 уходит БОЕВЫМ отправителем под перехватом
//   (`AsdaSelfTestKit::ArmCapture`, в сеть ничего), ответ 5007 подаётся штатным приёмом (`DebugFeedPacket`) в раскладке сервера
//   (`CharItemDump@0x4D3410` → `ApplyDumpResult`: [результат][слот i16][..][контейнер +5][остаток i32 +6][деньги +12][вес +16]).
//   Оракул — правило эталона, записанное здесь НЕЗАВИСИМО от боевого кода:
//     бросок вне окон, расходник (контейнер 2): золото — ничего; стек торгуемого — окно количества → вопрос 2059 с числом;
//       иначе — вопрос 2059 (TWOBUT, пара очереди (1, 2059)); «Да» → ровно одна 5006 (контейнер, слот, количество);
//     бросок вне окон, сумка (контейнер 1): привязан (+50 ≠ 0) — окно 2004 (ONEBUT, пара (0, 2004)), 5006 нет; иначе вопрос 2059;
//     бросок над окном (своим вне ячеек или чужим) — ничего; корзина — вопрос 2005 (щелчок и бросок); вопросы — «без Enter»;
//     бросок слота панели быстрого доступа вне окон — слот снимается, предмет в сумке цел, 5006 нет.
//   Разделы: A контроль ключа 0 (дефект воспроизведён = шаг 0); B «Нет»; C «Да» → 5006 → 5007 → ячейка пуста; D стопка через окно
//   количества (3 из 5) → 5006 ×3 → остаток 2; E сумка → 2059 → 5006 контейнер 1; F привязанный предмет сумки → окно 2004;
//   G золото — ничего; H своё окно вне ячеек — ничего; I чужое окно — ничего; J корзина щелчком → 2005 «Нет»; K бросок на корзину →
//   2005 «Да» → 5006; L панель быстрого доступа → вне окон → слот снят, предмет цел.
//   Состояние (сумка, расходники, слот панели, ключи, перехват, окно) восстанавливается в конце.
//
// Консоль: `asda.SelfTest.DragDelete [delay=N] [quit]`. Только офлайн (`-AsdaNoNet`), на карте мира.
//   Строки проверок — «УДАЛЕНИЕ-ТЕСТ ok/FAIL», итог — «УДАЛЕНИЕ-ТЕСТ ИТОГ: ok=… fail=…».
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestSeq.h"   // ★ R5-fix-b 13.11.2026 (заключительный заход): шаги по событию «ввод доставлен», а не по часам
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaDragDelTest, Log, All);

namespace AsdaDragDelTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.DragDelete");        // имя команды (и ключ реестра ключей консоли)
	static const TCHAR* kKey = TEXT("asda.Inven.DropOutsideDelete");    // ключ исправления (1 — как оригинал)
	static const TCHAR* kTwoBut = TEXT("TWOBUT_MSGBOX");                // окно-вопрос 1520
	static const TCHAR* kOneBut = TEXT("ONEBUT_MSGBOX");                // окно-сообщение 1500

	// Перехваченная отправка: опкод и тело.
	struct FSent { int32 Op = 0; TArray<uint8> Body; };

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		TSharedPtr<AsdaSelfTestSeq::FSeq> Seq;      // очередь шагов `At` (★ R5-fix-b 13.11.2026 (заключительный заход))
		bool bQuit = false;                        // выйти после итога
		TArray<TArray<uint8>> SavedSpend;          // расходники до прогона (пусто — ячейка была пуста)
		TArray<TArray<uint8>> SavedBag;            // сумка до прогона
		FAsdaHotbarSlot SavedHb;                   // слот панели раздела L до прогона
		int32 HbAbs = -1;                          // абсолютный номер этого слота
		uint32 CapGen = 0;                         // поколение перехвата отправок
		TArray<FSent> Sent;                        // перехваченные отправки
		int32 Mark = 0;                            // число отправок на начало шага
		int32 Stack = 0, Weapon = 0;               // расходник-стопка (торгуемый, без flagWord176, не самоцвет) и оружие
		FVector2D WorldPt = FVector2D::ZeroVector; // точка «в мире» (ни одного окна под ней)
		FVector2D OtherPt = FVector2D::ZeroVector; // точка над ЧУЖИМ окном
		FName OtherZone = NAME_None;               // какое окно под ней
		FVector2D OwnPt = FVector2D::ZeroVector;   // точка над окном инвентаря вне ячеек/корзины

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaDragDelTest, Log,   TEXT("УДАЛЕНИЕ-ТЕСТ ok   %s"), *What); }
			else   { ++Fail; UE_LOG(LogAsdaDragDelTest, Error, TEXT("УДАЛЕНИЕ-ТЕСТ FAIL %s"), *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Ячейки набора.
	static constexpr int32 kCellOne = 0, kCellStack = 1, kCellGold = 2, kCellTrash = 3;   // расходники (контейнер 2)
	static constexpr int32 kBagFree = 0, kBagBound = 1;                                    // сумка (контейнер 1)

	// Шаг через Sec секунд от старта.
	// ★ R5-fix-b 13.11.2026 (заключительный заход): расписание — общая очередь `AsdaSelfTestSeq::At` (запись та же): шаг ждёт доставки заказанного
	//   клика/перетаскивания и тот же промежуток после предыдущего шага. Под нагрузкой (три игры, `rx_r5fix3`) мировой таймер
	//   проверял до отпускания перетаскивания (30 кадров удержания), следующий заказ затирал недоставленный — каскад провалов.
	using AsdaSelfTestSeq::At;

	// Центр живой геометрии узла (false — не отрисован / скрыт / нулевой размер).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия отличалась только тем, что `Mn`/`Mx` объявлялись без начального значения (на результат не влияет).
	// Точка внутри живого прямоугольника узла.
	static bool Inside(const UWidget* Wd, const FVector2D& P)
	{
		FVector2D Mn, Mx;
		return Wd && AsdaUi::IsShown(Wd) && AsdaUi::WidgetAbsRect(Wd, Mn, Mx) && P.X >= Mn.X && P.X <= Mx.X && P.Y >= Mn.Y && P.Y <= Mx.Y;
	}

	// Код / количество в ячейке (0 — пусто).
	static int32 CodeAt(const FCtxRef& C, int32 Cont, int32 Cell)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		const FAsdaItemInstance* I = Inv ? Inv->GetCell(Cont, Cell) : nullptr;
		return (I && !I->IsEmpty()) ? I->ItemCode : 0;
	}
	static int32 AmountAt(const FCtxRef& C, int32 Cont, int32 Cell)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		const FAsdaItemInstance* I = Inv ? Inv->GetCell(Cont, Cell) : nullptr;
		return (I && !I->IsEmpty()) ? I->Amount : 0;
	}

	// Положить запись боевой точкой (`ApplyExternalRecord`, как приёмники пакетов). `bBound` — байт +50 = 1 («привязан»).
	static void Put(const FCtxRef& C, int32 Cont, int32 Cell, int32 Code, int32 Qty, bool bBound = false)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!Inv) { return; }
		TArray<uint8> R = FAsdaItemRecordWriter::Cell(Code, Cont, Cell, Qty);
		if (bBound && R.IsValidIndex(50)) { R[50] = 1; }
		Inv->ApplyExternalRecord(R, UAsdaInventorySubsystem::EExternalRecordMode::AsIs);
	}

	// Окно-сообщение: показано ли с парой очереди (Kind, Id).
	static bool Queued(const FCtxRef& C, int32 Kind, int32 Id)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		return WM && WM->IsMsgBoxQueued(Kind, Id);
	}
	// Узел окна-сообщения.
	static UWidget* MsgNode(const FCtxRef& C, const TCHAR* Win, const TCHAR* Node)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UUserWidget* Wd = WM ? WM->FindWindow(FName(Win)) : nullptr;
		return Wd ? Wd->GetWidgetFromName(FName(Node)) : nullptr;
	}
	// Видимый текст тела TWOBUT.
	static FString TwoButText(const FCtxRef& C)
	{
		return UAsdaWindowSubsystem::ReadMsgBoxShownText(MsgNode(C, kTwoBut, TEXT("TWOBUT_TEXT")));
	}
	// Настоящий щелчок по кнопке окна-сообщения.
	static void ClickMsg(const FCtxRef& C, const TCHAR* Win, const TCHAR* Node, const TCHAR* Tag)
	{
		FVector2D P = FVector2D::ZeroVector;
		const bool bGeo = Center(MsgNode(C, Win, Node), P);
		C->Expect(bGeo, FString::Printf(TEXT("%s кнопка %s окна %s на экране"), Tag, Node, Win));
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
	}

	// Перетаскивание из видимой ячейки сетки в точку экрана.
	static void DragCell(const FCtxRef& C, int32 Cell, const FVector2D& To, const TCHAR* Tag)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		FVector2D From = FVector2D::ZeroVector;
		const bool bGeo = Inv && Center(Inv->DebugGetGridIcon(Cell), From);
		C->Expect(bGeo, FString::Printf(TEXT("%s ячейка %d отрисована, бросок в (%.0f,%.0f)"), Tag, Cell, To.X, To.Y));
		C->Mark = C->Sent.Num();
		if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag(From, To, /*HoldFrames*/ 30); }
	}

	// Отправки опкода с шага (последняя — в OutLast).
	static int32 SentSince(const FCtxRef& C, int32 Op, TArray<uint8>* OutLast = nullptr)
	{
		int32 N = 0;
		for (int32 i = C->Mark; i < C->Sent.Num(); ++i)
		{
			if (C->Sent[i].Op == Op) { ++N; if (OutLast) { *OutLast = C->Sent[i].Body; } }
		}
		return N;
	}

	// Ровно одна 5006 с шага: ссылка 11 б = [слот i16][второй i16][контейнер u8][количество i32]… Возвращает тело (пусто — нет/не та).
	static TArray<uint8> Expect5006(const FCtxRef& C, const TCHAR* Tag, int32 Cont, int32 Slot, int32 Qty)
	{
		TArray<uint8> B;
		const int32 N = SentSince(C, 5006, &B);
		if (N != 1 || B.Num() < 11)
		{
			C->Expect(false, FString::Printf(TEXT("%s ушла ровно одна 5006 (ушло %d, тело %d б)"), Tag, N, B.Num()));
			return {};
		}
		const int32 Off = B.Num() - 11;   // тело — ссылка 11 б (если перехват отдаёт с конвертом — она в хвосте)
		const uint8* P = B.GetData() + Off;
		const int32 GSlot = AsdaBytes::RdI16(P + 0), GCont = P[4], GQty = AsdaBytes::RdI32(P + 5);
		const bool b = GSlot == Slot && GCont == Cont && GQty == Qty;
		C->Expect(b, FString::Printf(TEXT("%s 5006: контейнер %d слот %d количество %d (ждали %d/%d/%d)"), Tag, GCont, GSlot, GQty, Cont, Slot, Qty));
		return b ? B : TArray<uint8>();
	}

	// Ответ 5007 штатным приёмом (успех; `Remain` — остаток стопки).
	static void Reply5007(const FCtxRef& C, int32 Cont, int32 Slot, int32 Remain)
	{
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		if (!Net || !Inv) { return; }
		TArray<uint8> B; B.SetNumZeroed(18);
		B[0] = 1;
		AsdaBytes::WrI16(B.GetData() + 1, Slot);
		B[5] = (uint8)Cont;
		AsdaBytes::WrI32(B.GetData() + 6, Remain);
		AsdaBytes::WrI32(B.GetData() + 12, Inv->GetGold());
		Net->DebugFeedPacket(5007, B);
		Inv->DebugPumpPendingPackets();
	}

	// «Ничего не произошло»: ни вопроса 2059/2005, ни окна 2004, ни 5006, ячейка та же.
	static void ExpectNothing(const FCtxRef& C, const TCHAR* Tag, int32 Cont, int32 Cell, int32 Code, int32 Qty)
	{
		const bool bNoBox = !Queued(C, 1, 2059) && !Queued(C, 1, 2005) && !Queued(C, 0, 2004);
		C->Expect(bNoBox, FString::Printf(TEXT("%s окна вопроса/сообщения нет"), Tag));
		C->Expect(SentSince(C, 5006) == 0, FString::Printf(TEXT("%s 5006 не ушла"), Tag));
		C->Expect(CodeAt(C, Cont, Cell) == Code && AmountAt(C, Cont, Cell) == Qty,
			FString::Printf(TEXT("%s предмет на месте: %d:%d = %d ×%d (ждали %d ×%d)"), Tag, Cont, Cell, CodeAt(C, Cont, Cell), AmountAt(C, Cont, Cell), Code, Qty));
	}

	// Вопрос показан: пара очереди, текст начинается со строки LocId, пометка «без Enter».
	static void ExpectAsk(const FCtxRef& C, const TCHAR* Tag, int32 LocId, const TCHAR* MustContain = nullptr)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		const FString Shown = TwoButText(C);
		const FString Want = AsdaNotice::Loc(C->GI(), LocId);
		C->Expect(Queued(C, 1, LocId), FString::Printf(TEXT("%s показан вопрос TWOBUT (1, %d)"), Tag, LocId));
		C->Expect(!Want.IsEmpty() && Shown.StartsWith(Want) && (!MustContain || Shown.Contains(MustContain)),
			FString::Printf(TEXT("%s текст «%s» (строка %d «%s»%s%s)"), Tag, *Shown.Replace(TEXT("\n"), TEXT(" / ")), LocId, *Want,
				MustContain ? TEXT(", содержит ") : TEXT(""), MustContain ? MustContain : TEXT("")));
		C->Expect(WM && WM->IsDisplayedMsgBoxNoEnter(), FString::Printf(TEXT("%s вопрос помечен «без Enter»"), Tag));
	}

	// Подобрать данные по таблице (оракул свойств — сама запись item_list, не боевой код).
	static bool PickData(const FCtxRef& C)
	{
		UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
		if (!IT) { return false; }
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (!C->Stack && R.ItemType == 11 && R.TradableFlag != 0 && R.FlagWord176 == 0 && R.ItemCode != UAsdaInventorySubsystem::kGoldItemCode
				&& !IT->IsSowelCode(R.ItemCode) && !IT->IsSpecialItemCode(R.ItemCode)) { C->Stack = R.ItemCode; }
			if (!C->Weapon && R.ItemType == 9 && R.FlagWord176 == 0) { C->Weapon = R.ItemCode; }
			if (C->Stack && C->Weapon) { break; }
		}
		return C->Stack > 0 && C->Weapon > 0;
	}

	// Точки броска: «в мире» (окна нет), над чужим окном, над своим окном вне ячеек/корзины/манекена.
	static bool PickPoints(const FCtxRef& C)
	{
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
		if (!WM || !Inv || !GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return false; }
		const FGeometry& G = Vp->GetCachedGeometry();
		const FVector2D Mn = G.GetAbsolutePosition(), Sz = G.GetAbsoluteSize();
		if (Sz.X < 100.f || Sz.Y < 100.f) { return false; }
		// Над ячейкой/корзиной/манекеном/слотом панели — не годится ни для какого раздела (это цели броска).
		auto OverTarget = [Inv, Hb](const FVector2D& P)
		{
			for (int32 i = 0; i < UAsdaInventorySubsystem::kGridCells; ++i) { if (Inside(Inv->DebugGetGridIcon(i), P)) { return true; } }   // ★ 29.10.2026: 60 ячеек
			for (int32 s = 0; s < 11; ++s) { if (Inside(Inv->DebugGetEquipSlotIcon(s), P)) { return true; } }
			if (Inside(Inv->DebugGetTrashButton(), P)) { return true; }
			if (Hb)
			{
				for (int32 c = 0; c < UAsdaHotbarSubsystem::kColsPerPage; ++c)
				{
					FVector2D A, B;
					if (Hb->GetSlotIconAbsRect(c + UAsdaHotbarSubsystem::kColsPerPage * Hb->GetCurrentPage(), A, B)
						&& P.X >= A.X - 4 && P.X <= B.X + 4 && P.Y >= A.Y - 4 && P.Y <= B.Y + 4) { return true; }
				}
			}
			return false;
		};
		bool bWorld = false, bOther = false, bOwn = false;
		const FVector2D Mid = Mn + Sz * 0.5f;
		float BestWorld = TNumericLimits<float>::Max();
		for (int32 iy = 1; iy < 40; ++iy)
		{
			for (int32 ix = 1; ix < 40; ++ix)
			{
				const FVector2D P = Mn + FVector2D(Sz.X * ix / 40.f, Sz.Y * iy / 40.f);
				FName Z = NAME_None;
				const bool bUi = WM->IsScreenPointOverAnyUiWindow(P, Z);
				if (!bUi)
				{
					// Ближе к центру — дальше от HUD по краям.
					const float D = FVector2D::DistSquared(P, Mid);
					if (D < BestWorld && !OverTarget(P)) { BestWorld = D; C->WorldPt = P; bWorld = true; }
					continue;
				}
				if (OverTarget(P)) { continue; }
				if (Z == FName(TEXT("INVEN_WIN")))
				{
					if (!bOwn) { C->OwnPt = P; bOwn = true; }
				}
				else if (!bOther)
				{
					C->OtherPt = P; C->OtherZone = Z; bOther = true;
				}
			}
		}
		C->Expect(bWorld, FString::Printf(TEXT("точка «в мире» (ни одного окна): (%.0f,%.0f)"), C->WorldPt.X, C->WorldPt.Y));
		C->Expect(bOwn, FString::Printf(TEXT("точка над окном инвентаря вне ячеек: (%.0f,%.0f)"), C->OwnPt.X, C->OwnPt.Y));
		C->Expect(bOther, FString::Printf(TEXT("точка над чужим окном «%s»: (%.0f,%.0f)"), *C->OtherZone.ToString(), C->OtherPt.X, C->OtherPt.Y));
		return bWorld;
	}

	static void Finish(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (WM && WM->IsMsgBoxQueued(1, 2059)) { WM->CloseConfirm(false); }
		if (WM && WM->IsMsgBoxQueued(1, 2005)) { WM->CloseConfirm(false); }
		if (Hb && C->HbAbs >= 0)
		{
			const FAsdaHotbarSlot& S = C->SavedHb;
			if (S.IsEmpty()) { if (!Hb->GetSlot(C->HbAbs).IsEmpty()) { Hb->ClearSlot(C->HbAbs); } }
			else { Hb->SetSlotContent(C->HbAbs, S.Type, S.SubType, S.Index, S.Count, S.Id); }
		}
		if (Inv)
		{
			for (int32 I = 0; I < C->SavedSpend.Num(); ++I)
			{
				if (C->SavedSpend[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedSpend[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(2, I); }
			}
			for (int32 I = 0; I < C->SavedBag.Num(); ++I)
			{
				if (C->SavedBag[I].Num() >= 57) { Inv->ApplyExternalRecord(C->SavedBag[I], UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
				else { Inv->ClearCell(1, I); }
			}
			if (Inv->IsWindowOpen()) { Inv->CloseWindow(); }
		}
		if (Net && C->CapGen) { AsdaSelfTestKit::DisarmCapture(Net, C->CapGen); C->CapGen = 0; }
		AsdaSelfTestKit::Finish(C->W.Get(), kCmd, C->bQuit, LogAsdaDragDelTest, TEXT("УДАЛЕНИЕ-ТЕСТ"), C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
		if (!Inv || !WM || !Net) { C->Expect(false, TEXT("подсистемы инвентаря/окон/сети есть")); Finish(C); return; }
		if (!PickData(C))
		{
			C->Expect(false, TEXT("данные: торгуемый расходник без flagWord176 и оружие в item_list"));
			Finish(C);
			return;
		}
		C->Expect(true, FString::Printf(TEXT("данные: расходник %d, оружие %d"), C->Stack, C->Weapon));
		TWeakPtr<FCtx> WC(C);
		C->CapGen = AsdaSelfTestKit::ArmCapture(Net, [WC](uint16 Op, const uint8* P, int32 Len, bool, TCHAR)
		{
			if (TSharedPtr<FCtx> X = WC.Pin()) { FSent S; S.Op = (int32)Op; if (P && Len > 0) { S.Body.Append(P, Len); } X->Sent.Add(MoveTemp(S)); }
		}, 0, kCmd);
		if (!C->CapGen) { C->Expect(false, TEXT("перехват отправок свободен")); Finish(C); return; }

		// Снимок сумки и расходников, затем свой набор.
		const int32 NSpend = FMath::Max(0, Inv->GetContainerSize(2)), NBag = FMath::Max(0, Inv->GetContainerSize(1));
		for (int32 I = 0; I < NSpend; ++I) { const FAsdaItemInstance* X = Inv->GetCell(2, I); C->SavedSpend.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < NBag; ++I)   { const FAsdaItemInstance* X = Inv->GetCell(1, I); C->SavedBag.Add(X && !X->IsEmpty() ? X->Raw : TArray<uint8>()); }
		for (int32 I = 0; I < NSpend; ++I) { Inv->ClearCell(2, I); }
		for (int32 I = 0; I < NBag; ++I)   { Inv->ClearCell(1, I); }
		Put(C, 2, kCellOne, C->Stack, 1);
		Put(C, 2, kCellStack, C->Stack, 5);
		Put(C, 2, kCellGold, UAsdaInventorySubsystem::kGoldItemCode, 1000);
		Put(C, 2, kCellTrash, C->Stack, 1);
		Put(C, 1, kBagFree, C->Weapon, 1);
		Put(C, 1, kBagBound, C->Weapon, 1, /*bBound*/ true);
		AsdaCVarSave::SetForTest(kKey, 0);
		AsdaCVarSave::SetForTest(TEXT("asda.Inven.LegacyTrashNoConfirm"), 0);
		if (!Inv->IsWindowOpen()) { Inv->OpenWindow(); }
		WM->BringWindowToFront(FName(TEXT("INVEN_WIN")));
		Inv->SetTab(0);   // вкладка расходников = контейнер 2

		At(C, 0.8f, [C]() { if (!PickPoints(C)) { C->Expect(false, TEXT("геометрия вьюпорта есть")); Finish(C); } });

		// ═══ A. КОНТРОЛЬ (шаг 0): ключ 0 — прежнее поведение, бросок в мир → предмет молча на месте ═══
		At(C, 1.2f, [C]() { DragCell(C, kCellOne, C->WorldPt, TEXT("A")); });
		At(C, 2.8f, [C]()
		{
			ExpectNothing(C, TEXT("A КОНТРОЛЬ ключ 0 (дефект воспроизведён):"), 2, kCellOne, C->Stack, 1);
			AsdaCVarSave::SetForTest(kKey, 1);
		});
		// ═══ B. Ключ 1: бросок в мир → вопрос 2059 «без Enter» → «Нет» → предмет на месте ═══
		At(C, 3.2f, [C]() { DragCell(C, kCellOne, C->WorldPt, TEXT("B")); });
		At(C, 4.8f, [C]()
		{
			ExpectAsk(C, TEXT("B"), 2059);
			C->Expect(SentSince(C, 5006) == 0, TEXT("B до ответа 5006 не ушла"));
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT01"), TEXT("B «Нет»:"));
		});
		At(C, 5.6f, [C]()
		{
			C->Expect(!Queued(C, 1, 2059), TEXT("B вопрос закрыт"));
			C->Expect(SentSince(C, 5006) == 0, TEXT("B «Нет»: 5006 не ушла"));
			C->Expect(CodeAt(C, 2, kCellOne) == C->Stack && AmountAt(C, 2, kCellOne) == 1, TEXT("B «Нет»: предмет на месте"));
		});
		// ═══ C. Бросок в мир → 2059 → «Да» → одна 5006 (2, ячейка, 1) → 5007 → ячейка пуста ═══
		At(C, 6.0f, [C]() { DragCell(C, kCellOne, C->WorldPt, TEXT("C")); });
		At(C, 7.6f, [C]()
		{
			ExpectAsk(C, TEXT("C"), 2059);
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("C «Да»:"));
		});
		At(C, 8.4f, [C]()
		{
			const TArray<uint8> B = Expect5006(C, TEXT("C «Да»:"), 2, kCellOne, 1);
			if (B.Num()) { Reply5007(C, 2, kCellOne, 0); }
			C->Expect(CodeAt(C, 2, kCellOne) == 0, TEXT("C после 5007: ячейка пуста (предмет уничтожен)"));
		});
		// ═══ D. Стопка ×5 → окно количества → «3» + Enter → вопрос 2059 «× 3 (из 5)» → «Да» → 5006 ×3 → остаток 2 ═══
		At(C, 8.8f, [C]() { DragCell(C, kCellStack, C->WorldPt, TEXT("D")); });
		At(C, 10.4f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(WM && WM->IsNumPadOpen(), TEXT("D стопка: открыто окно количества 1410"));
			C->Expect(!Queued(C, 1, 2059), TEXT("D вопроса до ввода количества нет"));
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication& App = FSlateApplication::Get();
				const FKey K3 = FInputKeyManager::Get().GetKeyFromCodes((uint32)'3', (uint32)'3');
				App.ProcessKeyDownEvent(FKeyEvent(K3, FModifierKeysState(), 0, false, (uint32)'3', (uint32)'3'));
				App.ProcessKeyCharEvent(FCharacterEvent(TEXT('3'), FModifierKeysState(), 0, false));
				App.ProcessKeyUpEvent(FKeyEvent(K3, FModifierKeysState(), 0, false, (uint32)'3', (uint32)'3'));
			}
		});
		At(C, 10.8f, [C]() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13)); } });
		At(C, 11.0f, [C]() { if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 13, 13)); } });
		At(C, 11.6f, [C]()
		{
			UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
			C->Expect(WM && !WM->IsNumPadOpen(), TEXT("D окно количества закрыто Enter"));
			ExpectAsk(C, TEXT("D"), 2059, TEXT("3"));
			C->Expect(SentSince(C, 5006) == 0, TEXT("D Enter вопроса не подтвердил: 5006 не ушла"));
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("D «Да»:"));
		});
		At(C, 12.4f, [C]()
		{
			const TArray<uint8> B = Expect5006(C, TEXT("D «Да»:"), 2, kCellStack, 3);
			if (B.Num()) { Reply5007(C, 2, kCellStack, 2); }
			C->Expect(CodeAt(C, 2, kCellStack) == C->Stack && AmountAt(C, 2, kCellStack) == 2, FString::Printf(TEXT("D после 5007: в стопке 2 (есть %d)"), AmountAt(C, 2, kCellStack)));
		});
		// ═══ G. Золото → в мир → ничего ═══
		At(C, 12.8f, [C]() { DragCell(C, kCellGold, C->WorldPt, TEXT("G")); });
		At(C, 14.4f, [C]() { ExpectNothing(C, TEXT("G золото:"), 2, kCellGold, UAsdaInventorySubsystem::kGoldItemCode, 1000); });
		// ═══ H. Своё окно вне ячеек → ничего ═══
		At(C, 14.8f, [C]() { DragCell(C, kCellTrash, C->OwnPt, TEXT("H")); });
		At(C, 16.4f, [C]() { ExpectNothing(C, TEXT("H своё окно вне ячеек:"), 2, kCellTrash, C->Stack, 1); });
		// ═══ I. Чужое окно → ничего ═══
		At(C, 16.8f, [C]() { DragCell(C, kCellTrash, C->OtherPt, TEXT("I")); });
		At(C, 18.4f, [C]() { ExpectNothing(C, *FString::Printf(TEXT("I чужое окно «%s»:"), *C->OtherZone.ToString()), 2, kCellTrash, C->Stack, 1); });
		// ═══ J. Корзина щелчком (шаг 0 корзины): выбор ячейки → 405 → вопрос 2005 «без Enter» → «Нет» ═══
		At(C, 18.8f, [C]()
		{
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = Inv && Center(Inv->DebugGetGridIcon(kCellTrash), P);
			C->Expect(bGeo, TEXT("J ячейка корзины отрисована"));
			C->Mark = C->Sent.Num();
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
		});
		At(C, 19.4f, [C]()
		{
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			const bool bGeo = Inv && Center(Inv->DebugGetTrashButton(), P);
			C->Expect(bGeo, TEXT("J кнопка-корзина 405 на экране"));
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
		});
		At(C, 20.2f, [C]()
		{
			ExpectAsk(C, TEXT("J корзина щелчком:"), 2005);
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT01"), TEXT("J «Нет»:"));
		});
		At(C, 21.0f, [C]() { ExpectNothing(C, TEXT("J «Нет»:"), 2, kCellTrash, C->Stack, 1); });
		// ═══ K. Бросок на корзину → 2005 → «Да» → 5006 ═══
		At(C, 21.4f, [C]()
		{
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			FVector2D T = FVector2D::ZeroVector;
			if (Inv && Center(Inv->DebugGetTrashButton(), T)) { DragCell(C, kCellTrash, T, TEXT("K")); }
			else { C->Expect(false, TEXT("K кнопка-корзина на экране")); }
		});
		At(C, 23.0f, [C]()
		{
			ExpectAsk(C, TEXT("K бросок на корзину:"), 2005);
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("K «Да»:"));
		});
		At(C, 23.8f, [C]()
		{
			const TArray<uint8> B = Expect5006(C, TEXT("K «Да»:"), 2, kCellTrash, 1);
			if (B.Num()) { Reply5007(C, 2, kCellTrash, 0); }
			C->Expect(CodeAt(C, 2, kCellTrash) == 0, TEXT("K после 5007: ячейка пуста"));
		});
		// ═══ E. Сумка: оружие → в мир → 2059 → «Да» → 5006 (1, ячейка, 1) ═══
		//   Выбор снаряжения сам раскрывает панель подробностей (окно 228 → 648, эталон `uiPanelExpand`) — прежняя точка «в мире»
		//   может оказаться под окном. Поэтому сначала щелчок по ячейке (как игрок), затем точки — заново по живой геометрии.
		At(C, 24.2f, [C]()
		{
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			if (!Inv) { return; }
			Inv->SetTab(1);
		});
		At(C, 24.5f, [C]()
		{
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			FVector2D P = FVector2D::ZeroVector;
			if (Inv && Center(Inv->DebugGetGridIcon(kBagFree), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, 2); }
		});
		At(C, 25.0f, [C]() { PickPoints(C); });
		At(C, 25.2f, [C]() { DragCell(C, kBagFree, C->WorldPt, TEXT("E")); });
		At(C, 26.4f, [C]()
		{
			ExpectAsk(C, TEXT("E сумка:"), 2059);
			C->Expect(!C->Sub<UAsdaWindowSubsystem>()->IsNumPadOpen(), TEXT("E сумка: окна количества нет"));
			ClickMsg(C, kTwoBut, TEXT("TWOBUT_BUT00"), TEXT("E «Да»:"));
		});
		At(C, 27.2f, [C]()
		{
			const TArray<uint8> B = Expect5006(C, TEXT("E «Да»:"), 1, kBagFree, 1);
			if (B.Num()) { Reply5007(C, 1, kBagFree, 0); }
			C->Expect(CodeAt(C, 1, kBagFree) == 0, TEXT("E после 5007: ячейка сумки пуста"));
		});
		// ═══ F. Привязанный предмет сумки → в мир → окно 2004, 5006 нет ═══
		At(C, 27.6f, [C]() { DragCell(C, kBagBound, C->WorldPt, TEXT("F")); });
		At(C, 29.2f, [C]()
		{
			C->Expect(Queued(C, 0, 2004), TEXT("F привязанный: показано окно ONEBUT (0, 2004)"));
			const FString T = UAsdaWindowSubsystem::ReadMsgBoxShownText(MsgNode(C, kOneBut, TEXT("ONEBUT_TEXT")));
			const FString Want = AsdaNotice::Loc(C->GI(), 2004);
			C->Expect(!Want.IsEmpty() && T.Contains(Want), FString::Printf(TEXT("F текст «%s» (строка 2004 «%s»)"), *T, *Want));
			C->Expect(!Queued(C, 1, 2059), TEXT("F вопроса удаления нет"));
			C->Expect(SentSince(C, 5006) == 0, TEXT("F 5006 не ушла"));
			ClickMsg(C, kOneBut, TEXT("ONEBUT_BUT00"), TEXT("F «ОК»:"));
		});
		At(C, 30.0f, [C]()
		{
			C->Expect(!Queued(C, 0, 2004), TEXT("F окно 2004 закрыто"));
			C->Expect(CodeAt(C, 1, kBagBound) == C->Weapon, TEXT("F привязанный предмет на месте"));
			if (UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>()) { Inv->SetTab(0); }
		});
		// ═══ L. Панель быстрого доступа: предмет в слот → слот перетащен в мир → слот снят, предмет цел, 5006 нет ═══
		At(C, 30.6f, [C]()
		{
			UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
			UAsdaInventorySubsystem* Inv = C->Sub<UAsdaInventorySubsystem>();
			if (!Hb || !Inv) { C->Expect(false, TEXT("L подсистема панели есть")); return; }
			C->HbAbs = UAsdaHotbarSubsystem::kColsPerPage * Hb->GetCurrentPage();   // колонка 0 текущей страницы
			C->SavedHb = Hb->GetSlot(C->HbAbs);
			C->Expect(Inv->AssignToHotbarSlot(2, kCellStack, C->HbAbs), FString::Printf(TEXT("L предмет 2:%d положен в слот панели %d"), kCellStack, C->HbAbs));
		});
		At(C, 31.2f, [C]()
		{
			UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
			FVector2D A, B;
			const bool bGeo = Hb && C->HbAbs >= 0 && Hb->GetSlotIconAbsRect(C->HbAbs, A, B);
			C->Expect(bGeo && !Hb->GetSlot(C->HbAbs).IsEmpty(), TEXT("L слот панели на экране и занят"));
			C->Mark = C->Sent.Num();
			if (bGeo) { C->Sub<UAsdaWindowSubsystem>()->SimulateDrag((A + B) * 0.5f, C->WorldPt, /*HoldFrames*/ 30); }
		});
		At(C, 32.8f, [C]()
		{
			UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
			C->Expect(Hb && Hb->GetSlot(C->HbAbs).IsEmpty(), TEXT("L слот панели снят броском вне окон (эталон `hotbarWnd_HandleSlotClick`, a2 == nullptr)"));
			ExpectNothing(C, TEXT("L панель → мир:"), 2, kCellStack, C->Stack, 2);
		});
		At(C, 33.4f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestDragDelete(
	TEXT("asda.SelfTest.DragDelete"),
	TEXT("Самотест броска предмета вне окон путём игрока: контроль ключа 0, вопрос 2059 «Нет»/«Да» → 5006 → 5007, стопка через окно "
	     "количества, сумка, привязанный (окно 2004), золото, своё и чужое окно, корзина щелчком и броском (2005), слот панели вне окон. "
	     "5006 под перехватом. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaDragDelTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен гейтам мира — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
		if (!KA.Has(TEXT("selfready")) && W && GEngine && AsdaSelfTestKit::NeedSelfSpawn(W))
		{
			GEngine->Exec(W, TEXT("Asda.Load.CurrentMap 3"));
			GEngine->Exec(W, TEXT("Asda.World.FakeSelfSpawn 256 256"));
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
		FTimerHandle H;
		W->GetTimerManager().SetTimer(H, FTimerDelegate::CreateLambda([C]()
		{
			if (C->W.IsValid() && AsdaSelfTestKit::RefuseDeferred(C->W.Get(), kCmd, C->bQuit)) { return; }
			Run(C);
		}), FMath::Max(0.01f, KA.Delay), false);
	}));

#endif // ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING
