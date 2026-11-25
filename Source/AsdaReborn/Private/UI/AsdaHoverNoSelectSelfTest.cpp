// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ САМОТЕСТ «НАВЕДЕНИЕ НЕ ВЫБИРАЕТ, ПОДСКАЗКИ НЕ ЗАВИСАЮТ» (30.10.2026): `asda.SelfTest.HoverNoSelect [quit]`. [ПОРТ]
//   Баги игроков 29.10: (1) наведение на надетую вещь чужого игрока выделяло её без щелчка; (7) подсказка «Title menu»
//   зависала над миром. Путь игрока (законы `ui-check-game-path-geometry`, `diagnostic-must-use-real-path`): живая
//   геометрия после отрисовки, настоящие события мыши Slate (`SetCursorPos` + `ProcessMouseMoveEvent`, щелчок —
//   `UAsdaWindowSubsystem::SimulateClickAt`), пакеты — штатным приёмом `DebugFeedPacket`, офлайн (-AsdaNoNet).
//     H. Панель быстрого доступа: строка доп. меню (по возможности «Title menu», подсказка 110930) брошена в слот штатным
//        `DropRowToHotbar`; наведение → подсказка от владельца `Hotbar`; курсор ушёл в мир → подсказки нет; с полного слота
//        на пустой → подсказки нет; ключ `asda.Quick.TipHideFix 0` → подсказка остаётся над миром (прежний дефект
//        воспроизводится — проверка чувствительна).
//     O. Окно осмотра 4200: сосед рядом, пара 5479 + 7006 штатным приёмом; наведение на слот надетого → подсказка, курсор
//        выбора 4223 НЕ появляется; щелчок → курсор 4223 на слоте; наведение на другой слот курсор не двигает; уход —
//        подсказка гаснет; ключ `asda.OtherChara.SelectOnClick 0` → наведение снова ставит курсор (прежний порт).
//     W. Мир: наведение на соседа → контроллер видит наведение (`HoverActor`), цель НЕ меняется, окно осмотра не
//        открывается; щелчок → сосед становится целью.
//   Слоты панели, ключи и соседи возвращаются в конце. Итог: «НАВЕДЕНИЕ ИТОГ: ok=N fail=M». Карта мира (`--map world`).
#include "CoreMinimal.h"
#include "AsdaDevConsole.h"
#include "Test/AsdaSelfTestCtx.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников
#include "Test/AsdaSelfTestInput.h"   // ★ R5-c (12.11.2026): общая точка самотестов вместо своих копий помощников

#if ASDA_DEV_CONSOLE && !UE_BUILD_SHIPPING

#include "AsdaBytes.h"
#include "AsdaCVarSave.h"
#include "AsdaNetSubsystem.h"
#include "Test/AsdaSelfTestKit.h"
#include "Data/AsdaExtraMenuRow.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaExtraMenuSubsystem.h"
#include "UI/AsdaHelpTopics.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaMinimapSubsystem.h"
#include "UI/AsdaNotice.h"
#include "Login/AsdaLoginData.h"
#include "Components/EditableText.h"
#include "Components/Image.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaOtherCharaSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaWindowSubsystem.h"
#include "World/AsdaCoord.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SViewport.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaHoverNoSelTest, Log, All);

// Доступ к закрытому состоянию окна осмотра (объявлен `friend` в `AsdaOtherCharaSubsystem.h`).
struct FAsdaHoverNoSelectSelfTest
{
	// Курсор выбора слота 4223 (`OTHERCHARA_EQUIPSEL`).
	static const UWidget* EquipSel(const UAsdaOtherCharaSubsystem* O) { return O ? O->EquipSel.Get() : nullptr; }
	// Слот под курсором 4223 (эталонный `win[198]`; −1 — курсора нет).
	static int32 SelSlot(const UAsdaOtherCharaSubsystem* O) { return O ? O->HoverSlot : -9; }
	// Плитка первой вкладки чата 2501 и поле ввода 2609.
	static UWidget* ChatTab(const UAsdaChatWindowSubsystem* Ch) { return (Ch && Ch->TabQuads.Num() > 0) ? Ch->TabQuads[0].Get() : nullptr; }
	static UWidget* ChatInput(const UAsdaChatWindowSubsystem* Ch) { return Ch ? (UWidget*)Ch->InputEdit.Get() : nullptr; }
	// Узел-иконка слота надетого `OTHERCHARA_EQUIP<NN>_<план>_0` (группа `_grp` и прочие узлы с тем же началом — мимо).
	static UWidget* EquipCell(const UAsdaOtherCharaSubsystem* O, int32 Slot)
	{
		if (!O || !O->Win || !O->Win->WidgetTree) { return nullptr; }
		const FString Pfx = FString::Printf(TEXT("OTHERCHARA_EQUIP%02d_"), Slot);
		TArray<UWidget*> All;
		O->Win->WidgetTree->GetAllWidgets(All);
		for (UWidget* Wd : All)
		{
			const FString N = Wd ? Wd->GetName() : FString();
			if (!N.StartsWith(Pfx) || !N.EndsWith(TEXT("_0"))) { continue; }
			const FString Mid = N.Mid(Pfx.Len(), N.Len() - Pfx.Len() - 2);
			if (Mid.IsNumeric()) { return Wd; }
		}
		return nullptr;
	}
};

namespace AsdaHoverNoSelTest
{
	static const TCHAR* kCmd = TEXT("asda.SelfTest.HoverNoSelect");   // имя команды
	static const TCHAR* kPfx = TEXT("НАВЕДЕНИЕ");                    // префикс строк и итога
	constexpr int32 kPeerFirst = 7741;   // первый userNum соседей прогона (их kPeerCount подряд)
	constexpr int32 kPeerCount = 4;      // соседей — по одному с каждой стороны: на экран попадёт хотя бы один
	constexpr int32 kPairKey = 7777;     // ключ пары 5479 + 7006
	constexpr int32 kRecSize = 57;       // запись предмета экипировки
	constexpr int32 kMe = 1001, kMate = 2002, kThird = 3003;   // charId участников отряда прогона
	constexpr uint16 kMyUserNum = 321;                          // мой userNum на время раздела R
	constexpr int32 kParty = 42;                                // partyIndex
	constexpr int32 kTitleTipLoc = 110930;   // подсказка строки «Title menu» доп. меню (EN-нумерация DT_ExtraMenu)

	// Контекст прогона (живёт между таймерами).
	struct FCtx
	{
		int32 Ok = 0, Fail = 0;                    // пройдено / провалено
		TWeakObjectPtr<UWorld> W;                  // мир прогона
		bool bQuit = false;                        // выйти после итога
		// H — панель быстрого доступа
		int32 SlotA = -1, SlotB = -1;              // слот с пунктом доп. меню и пустой соседний
		FAsdaHotbarSlot SavedA, SavedB;            // их содержимое до прогона
		bool bSlotsSaved = false;                  // содержимое запомнено (вернуть в конце)
		int32 SlotW = -1;                          // слот оружия текущей страницы (колонка 12)
		FAsdaHotbarSlot SavedW;                    // его содержимое до прогона
		bool bWeaponSaved = false;                 // запомнено (вернуть в конце)
		// R — панель отряда
		FAsdaNetSelfScope NetScope;                // свои номера (charId, userNum) — вернуть после раздела R
		int32 StateId = -1;                        // id состояния из stateinfotip для 5108
		FString StateTitle;                        // его заголовок (ожидаемый текст подсказки)
		int32 MateRow = -1;                        // строка панели с соотрядником Member1
		FVector2D IconPt = FVector2D::ZeroVector;  // центр его первой иконки состояния
		// O / W — сосед и окно осмотра
		TWeakObjectPtr<AActor> Peer;               // сосед на экране
		int32 PeerNum = -1;                        // его userNum
		FVector2D PeerAbs = FVector2D::ZeroVector; // его голова на экране (абсолютные координаты Slate)
		FVector2D WorldPt = FVector2D::ZeroVector; // точка «мир без интерфейса»
		bool bWorldPt = false;                     // такая точка найдена

		void Expect(bool b, const FString& What)
		{
			if (b) { ++Ok;   UE_LOG(LogAsdaHoverNoSelTest, Log,   TEXT("%s ok   %s"), kPfx, *What); }
			else   { ++Fail; UE_LOG(LogAsdaHoverNoSelTest, Error, TEXT("%s FAIL %s"), kPfx, *What); }
		}
		UGameInstance* GI() const { return W.IsValid() ? W->GetGameInstance() : nullptr; }
		template <class T> T* Sub() const { UGameInstance* G = GI(); return G ? G->GetSubsystem<T>() : nullptr; }
	};
	using FCtxRef = TSharedRef<FCtx>;

	// Шаг через Sec секунд от старта.
	using AsdaSelfTestCtx::At;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestCtx.h`) — копия была дословной.

	// Центр живого прямоугольника узла (false — не отрисован / скрыт).
	using AsdaSelfTestInput::Center;   // ★ R5-c (12.11.2026): общая точка (`Test/AsdaSelfTestInput.h`) — копия была дословной.
	// Курсор в точку + событие движения (наведение без щелчка) — как у игрока.
	static void Hover(const FVector2D& P)
	{
		if (!FSlateApplication::IsInitialized()) { return; }
		FSlateApplication& App = FSlateApplication::Get();
		App.SetCursorPos(P);
		const FPointerEvent Mv(0, P, P, TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState());
		App.ProcessMouseMoveEvent(Mv);
	}
	// Геометрия вьюпорта игры (false — нет).
	static bool ViewportGeo(FGeometry& Out)
	{
		if (!GEngine || !GEngine->GameViewport) { return false; }
		const TSharedPtr<SViewport> Vp = GEngine->GameViewport->GetGameViewportWidget();
		if (!Vp.IsValid()) { return false; }
		Out = Vp->GetCachedGeometry();
		return Out.GetLocalSize().X > 10.f && Out.GetLocalSize().Y > 10.f;
	}
	// Подсказка показана и принадлежит владельцу `O`.
	static bool TipOf(const FCtxRef& C, EAsdaTipOwner O)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T && T->IsTooltipShown() && T->GetTipOwner() == O;
	}
	// Строка состояния подсказки для журнала.
	static FString TipState(const FCtxRef& C)
	{
		const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		return T ? FString::Printf(TEXT("показана=%d владелец=%d"), T->IsTooltipShown() ? 1 : 0, (int32)T->GetTipOwner()) : TEXT("нет подсистемы");
	}
	// Узел по точному имени в мире прогона (кнопки панели быстрого доступа не открыты наружу — ищем их, как игрок «видит»).
	static UWidget* FindLiveWidget(const FCtxRef& C, const TCHAR* Name)
	{
		for (TObjectIterator<UWidget> It; It; ++It)
		{
			if (It->GetFName() == FName(Name) && It->GetWorld() == C->W.Get() && It->GetCachedWidget().IsValid()) { return *It; }
		}
		return nullptr;
	}
	// Все тексты, видимые на общей плашке (заголовок + тело), одной строкой.
	static FString TipText(const FCtxRef& C)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		UUserWidget* Wd = P ? P->GetWidget() : nullptr;
		FString Out;
		if (!Wd || !Wd->WidgetTree) { return Out; }
		TArray<UWidget*> All;
		Wd->WidgetTree->GetAllWidgets(All);
		for (UWidget* X : All)
		{
			if (!X || !AsdaUi::IsShown(X)) { continue; }
			if (const UTextBlock* Tb = Cast<UTextBlock>(X)) { Out += Tb->GetText().ToString() + TEXT(" | "); }
			else if (const URichTextBlock* Rb = Cast<URichTextBlock>(X)) { Out += Rb->GetText().ToString() + TEXT(" | "); }
		}
		// Разметка rich-текста хранит «&», «<», «>», «"» сущностями — на экране они раскодированы.
		Out.ReplaceInline(TEXT("&lt;"), TEXT("<"));
		Out.ReplaceInline(TEXT("&gt;"), TEXT(">"));
		Out.ReplaceInline(TEXT("&quot;"), TEXT("\""));
		Out.ReplaceInline(TEXT("&amp;"), TEXT("&"));
		return Out;
	}
	// Плашка стоит у курсора по правилу оригинала: `Tooltip_LayoutNearCursor` (+32/+32) и `uiClampPopupToScreen` (отступ 5 от
	//   краёв; у нижнего края — над ним, X = курсор + 5). Ожидание считается НЕЗАВИСИМО от `UAsdaTipPanel::LayoutAtCursor`.
	static bool TipNearCursor(const FCtxRef& C, FString& OutWhy)
	{
		UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
		UAsdaTipPanel* P = T ? T->GetMainPanel() : nullptr;
		if (!P) { OutWhy = TEXT("нет плашки"); return false; }
		FVector2D Cur = FVector2D::ZeroVector;
		if (!AsdaUi::CursorInViewportUnits(C->W.Get(), Cur)) { OutWhy = TEXT("курсор вне вьюпорта"); return false; }
		const FVector2D Scr = AsdaUi::ViewportSizeUnits(C->W.Get());
		const FVector2D Sz = P->GetLastSize();
		FVector2D E(Cur.X + 32.0, Cur.Y + 32.0);
		if (E.X + Sz.X > Scr.X) { E.X = Scr.X - Sz.X - 5.0; }
		if (E.Y + Sz.Y + 5.0 > Scr.Y)
		{
			E.Y = Scr.Y - Sz.Y - 5.0;
			E.X = Cur.X + 5.0;
			if (E.X + Sz.X > Scr.X) { E.X = Scr.X - Sz.X - 5.0; }
		}
		const FVector2D Pos = P->GetLastViewportPos();
		OutWhy = FString::Printf(TEXT("курсор (%.0f,%.0f), плашка %.0fx%.0f в (%.0f,%.0f), ожидалось (%.0f,%.0f), экран %.0fx%.0f"),
			Cur.X, Cur.Y, Sz.X, Sz.Y, Pos.X, Pos.Y, E.X, E.Y, Scr.X, Scr.Y);
		return FVector2D::Distance(Pos, E) <= 1.5;
	}
	// Голова соседа на экране (абсолютные координаты Slate); false — за кадром.
	static bool PeerOnScreen(const FCtxRef& C, const AActor* Peer, FVector2D& OutAbs)
	{
		UWorld* W = C->W.Get();
		APlayerController* PC = W ? UGameplayStatics::GetPlayerController(W, 0) : nullptr;
		FGeometry G;
		FVector2D Px(0.f, 0.f);
		if (!Peer || !PC || !ViewportGeo(G)) { return false; }
		if (!UGameplayStatics::ProjectWorldToScreen(PC, Peer->GetActorLocation() + FVector(0.f, 0.f, 60.f), Px)) { return false; }
		const FVector2D Local = Px / FMath::Max(0.01f, G.Scale);
		const FVector2D Sz = G.GetLocalSize();
		if (Local.X < Sz.X * 0.15f || Local.X > Sz.X * 0.85f || Local.Y < Sz.Y * 0.12f || Local.Y > Sz.Y * 0.7f) { return false; }
		OutAbs = G.LocalToAbsolute(Local);
		FName Zone;
		const UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		return !(WM && WM->IsScreenPointOverAnyUiWindow(OutAbs, Zone));
	}
	// Вернуть слоты панели, соседей и цель; итог.
	static void Finish(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>(); Hb && C->bSlotsSaved)
		{
			auto Put = [Hb](int32 Abs, const FAsdaHotbarSlot& S)
			{
				if (S.IsEmpty()) { Hb->ClearSlot(Abs); }
				else { Hb->SetSlotContent(Abs, S.Type, S.SubType, S.Index, S.Count, S.Id); }
			};
			Put(C->SlotA, C->SavedA);
			Put(C->SlotB, C->SavedB);
		}
		if (UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>(); Hb && C->bWeaponSaved)
		{
			if (C->SavedW.IsEmpty()) { Hb->ClearSlot(C->SlotW); }
			else { Hb->SetSlotContent(C->SlotW, C->SavedW.Type, C->SavedW.SubType, C->SavedW.Index, C->SavedW.Count, C->SavedW.Id); }
		}
		if (UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>(); OC && OC->IsOpen()) { OC->CloseWindow(); }
		if (AAsdaPlayerController* PC = W ? Cast<AAsdaPlayerController>(W->GetFirstPlayerController()) : nullptr) { PC->ClearTarget(); }
		if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltip(); }
		// Соседи прогона уходят штатно (4011) — следующему самотесту пакета не достаются.
		if (UAsdaNetSubsystem* Net = AsdaSelfTestKit::NetOf(W))
		{
			for (int32 i = 0; i < kPeerCount; ++i)
			{
				TArray<uint8> B; B.SetNumZeroed(2);
				AsdaBytes::WrI16(B.GetData(), kPeerFirst + i);
				Net->DebugFeedPacket(4011, B);
			}
		}
		AsdaSelfTestKit::Finish(W, kCmd, C->bQuit, LogAsdaHoverNoSelTest, kPfx, C->Ok, C->Fail);
	}

	static void Run(const FCtxRef& C)
	{
		UWorld* W = C->W.Get();
		UAsdaHotbarSubsystem* Hb = C->Sub<UAsdaHotbarSubsystem>();
		UAsdaExtraMenuSubsystem* Etc = C->Sub<UAsdaExtraMenuSubsystem>();
		UAsdaWindowSubsystem* WM = C->Sub<UAsdaWindowSubsystem>();
		if (!W || !Hb || !Etc || !WM) { C->Expect(false, TEXT("подсистемы панели/доп. меню/окон есть")); Finish(C); return; }

		// ═══ H. Пункт доп. меню на панели быстрого доступа ═══
		At(C, 0.2f, [C]()
		{
			UAsdaHotbarSubsystem* H = C->Sub<UAsdaHotbarSubsystem>();
			UAsdaExtraMenuSubsystem* E = C->Sub<UAsdaExtraMenuSubsystem>();
			int32 Row = 0;
			for (int32 i = 0; E && i < 64; ++i)
			{
				const FAsdaExtraMenuRow* R = E->FindByIndex(i);
				if (!R) { break; }
				if (R->DescLocId == kTitleTipLoc) { Row = i; break; }
			}
			const FAsdaExtraMenuRow* R = E ? E->FindByIndex(Row) : nullptr;
			const int32 Page = H->GetCurrentPage();
			C->SlotA = Page * UAsdaHotbarSubsystem::kColsPerPage + 2;
			C->SlotB = Page * UAsdaHotbarSubsystem::kColsPerPage + 3;
			C->SavedA = H->GetSlot(C->SlotA);
			C->SavedB = H->GetSlot(C->SlotB);
			C->bSlotsSaved = true;
			H->ClearSlot(C->SlotB);
			const bool bDrop = E && E->DropRowToHotbar(Row, C->SlotA);
			C->Expect(bDrop && H->GetSlot(C->SlotA).Type == (uint8)EAsdaHotbarSlotType::ExtraMenu && H->GetSlot(C->SlotB).IsEmpty(),
				FString::Printf(TEXT("H0 строка доп. меню %d «%s» (подсказка %d) брошена в слот %d, слот %d пуст"),
					Row, R ? *R->Name : TEXT("?"), R ? R->DescLocId : -1, C->SlotA, C->SlotB));
			// Точка «мир без интерфейса» — для ухода курсора с панели.
			FGeometry G;
			if (ViewportGeo(G))
			{
				const FVector2D Fr[] = { {0.5f, 0.22f}, {0.3f, 0.3f}, {0.7f, 0.3f}, {0.5f, 0.4f}, {0.2f, 0.5f}, {0.8f, 0.5f} };
				for (const FVector2D& F : Fr)
				{
					const FVector2D P = G.LocalToAbsolute(G.GetLocalSize() * F);
					FName Zone;
					if (!C->Sub<UAsdaWindowSubsystem>()->IsScreenPointOverAnyUiWindow(P, Zone))
					{
						C->WorldPt = P; C->bWorldPt = true; break;
					}
				}
			}
			C->Expect(C->bWorldPt, FString::Printf(TEXT("H0 точка мира без интерфейса (%.0f,%.0f)"), C->WorldPt.X, C->WorldPt.Y));
		});
		// Центр иконки слота панели (живой прямоугольник; false — слот не на экране).
		auto SlotCenter = [](const FCtxRef& Cx, int32 Abs, FVector2D& Out) -> bool
		{
			FVector2D Mn, Mx;
			UAsdaHotbarSubsystem* H = Cx->Sub<UAsdaHotbarSubsystem>();
			if (!H || !H->GetSlotIconAbsRect(Abs, Mn, Mx)) { return false; }
			Out = (Mn + Mx) * 0.5f;
			return true;
		};
		At(C, 0.8f, [C, SlotCenter]()
		{
			FVector2D P;
			const bool bGeo = SlotCenter(C, C->SlotA, P);
			C->Expect(bGeo, FString::Printf(TEXT("H1 слот %d на экране (%.0f,%.0f)"), C->SlotA, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 1.2f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar),
				FString::Printf(TEXT("H2 наведение на пункт доп. меню → подсказка от владельца Hotbar (%s)"), *TipState(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 1.6f, [C, SlotCenter]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(),
				FString::Printf(TEXT("H3 курсор ушёл в мир → подсказка погашена (%s)"), *TipState(C)));
			FVector2D P;
			if (SlotCenter(C, C->SlotA, P)) { Hover(P); }
		});
		At(C, 2.0f, [C, SlotCenter]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar), FString::Printf(TEXT("H4 повторное наведение → подсказка (%s)"), *TipState(C)));
			FVector2D P;
			const bool bGeo = SlotCenter(C, C->SlotB, P);
			C->Expect(bGeo, FString::Printf(TEXT("H4 пустой слот %d на экране"), C->SlotB));
			if (bGeo) { Hover(P); }
		});
		At(C, 2.4f, [C, SlotCenter]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(),
				FString::Printf(TEXT("H5 с полного слота на пустой → подсказки нет (%s)"), *TipState(C)));
			// Ключ 0 — прежнее поведение: подсказка должна остаться висеть (проверка чувствительна к дефекту).
			AsdaCVarSave::SetForTest(TEXT("asda.Quick.TipHideFix"), 0);
			FVector2D P;
			if (SlotCenter(C, C->SlotA, P)) { Hover(P); }
		});
		At(C, 2.8f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar), FString::Printf(TEXT("H6 ключ 0: наведение → подсказка (%s)"), *TipState(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 3.2f, [C]()
		{
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar),
				FString::Printf(TEXT("H7 ключ 0: курсор в мире, подсказка ВИСИТ — прежний дефект воспроизведён (%s)"), *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Quick.TipHideFix"), 1);
			if (UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>()) { T->HideTooltipFor(EAsdaTipOwner::Hotbar); }
		});

		// ═══ P. Справка кнопок листания (тема 1) и пустого слота оружия (тема 0) ═══
		At(C, 3.4f, [C]()
		{
			UAsdaHotbarSubsystem* H = C->Sub<UAsdaHotbarSubsystem>();
			C->SlotW = H->GetCurrentPage() * UAsdaHotbarSubsystem::kColsPerPage + UAsdaHotbarSubsystem::kWeaponCol;
			C->SavedW = H->GetSlot(C->SlotW);
			C->bWeaponSaved = true;
			H->ClearSlot(C->SlotW);
			FVector2D P;
			const bool bGeo = Center(FindLiveWidget(C, TEXT("QUICK_NEW_UPBUT")), P);
			C->Expect(bGeo, FString::Printf(TEXT("P0 кнопка листания 73327 на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 3.8f, [C]()
		{
			FString T1, B1, Why;
			AsdaHelpTopics::Build(C->GI(), 1, T1, B1);
			const FString On = TipText(C);
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar) && !T1.IsEmpty() && !B1.IsEmpty() && On.Contains(T1) && On.Contains(B1),
				FString::Printf(TEXT("P1 наведение на 73327 → справка «%s» / «%s» (%s; на плашке: %s)"), *T1, *B1, *TipState(C), *On));
			const bool bNear = TipNearCursor(C, Why);
			C->Expect(bNear, FString::Printf(TEXT("P1 плашка у курсора по правилу оригинала: %s"), *Why));
			FVector2D P;
			if (Center(FindLiveWidget(C, TEXT("QUICK_NEW_DOWNBUT")), P)) { Hover(P); }
		});
		At(C, 4.2f, [C]()
		{
			FString T1, B1;
			AsdaHelpTopics::Build(C->GI(), 1, T1, B1);
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar) && TipText(C).Contains(T1),
				FString::Printf(TEXT("P2 наведение на 73328 → та же справка (%s)"), *TipState(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 4.6f, [C]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(), FString::Printf(TEXT("P3 уход с кнопки → справка погашена (%s)"), *TipState(C)));
			FVector2D Mn, Mx;
			UAsdaHotbarSubsystem* H = C->Sub<UAsdaHotbarSubsystem>();
			const bool bGeo = H && H->GetSlotIconAbsRect(C->SlotW, Mn, Mx);
			C->Expect(bGeo && H->GetSlot(C->SlotW).IsEmpty(), FString::Printf(TEXT("P3 пустой слот оружия %d на экране"), C->SlotW));
			if (bGeo) { Hover((Mn + Mx) * 0.5f); }
		});
		At(C, 5.0f, [C]()
		{
			FString T0, B0;
			AsdaHelpTopics::Build(C->GI(), 0, T0, B0);
			C->Expect(TipOf(C, EAsdaTipOwner::Hotbar) && !T0.IsEmpty() && TipText(C).Contains(T0),
				FString::Printf(TEXT("P4 наведение на пустой слот оружия → справка «%s» (%s; на плашке: %s)"), *T0, *TipState(C), *TipText(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 5.4f, [C]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(), FString::Printf(TEXT("P5 уход со слота оружия → справка погашена (%s)"), *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Quick.HelpTips"), 0);
			FVector2D P;
			if (Center(FindLiveWidget(C, TEXT("QUICK_NEW_UPBUT")), P)) { Hover(P); }
		});
		At(C, 5.8f, [C]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(), FString::Printf(TEXT("P6 ключ asda.Quick.HelpTips 0 → справки нет, как прежний порт (%s)"), *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Quick.HelpTips"), 1);
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});

		// ═══ M. Справка кнопок мини-карты (темы 14, 13/25, 12/26, 15) ═══
		auto TopicOn = [](const FCtxRef& Cx, int32 Topic, EAsdaTipOwner Owner, const TCHAR* Tag)
		{
			FString T, B;
			AsdaHelpTopics::Build(Cx->GI(), Topic, T, B);
			const FString On = TipText(Cx);
			Cx->Expect(TipOf(Cx, Owner) && (!T.IsEmpty() || !B.IsEmpty()) && (T.IsEmpty() || On.Contains(T.TrimStartAndEnd())) && (B.IsEmpty() || On.Contains(B.Left(12))),
				FString::Printf(TEXT("%s → тема %d «%s» / «%s» (%s; на плашке: %s)"), Tag, Topic, *T, *B, *TipState(Cx), *On));
		};
		At(C, 6.2f, [C]()
		{
			FVector2D P;
			const bool bGeo = Center(FindLiveWidget(C, TEXT("MINIMAP_CLOSE")), P);
			C->Expect(bGeo, FString::Printf(TEXT("M0 кнопка 1704 мини-карты на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 6.6f, [C, TopicOn]()
		{
			TopicOn(C, 14, EAsdaTipOwner::Minimap, TEXT("M1 наведение на 1704"));
			FVector2D P;
			if (Center(FindLiveWidget(C, TEXT("MINIMAP_ZOOM")), P)) { Hover(P); }
		});
		At(C, 7.0f, [C, TopicOn]()
		{
			const UAsdaMinimapSubsystem* Mm = C->Sub<UAsdaMinimapSubsystem>();
			TopicOn(C, (Mm && Mm->IsZoomed()) ? 25 : 13, EAsdaTipOwner::Minimap, TEXT("M2 наведение на 1706 (по флагу зума)"));
			FVector2D P;
			if (Center(FindLiveWidget(C, TEXT("MINIMAP_UPDOWN")), P)) { Hover(P); }
		});
		At(C, 7.4f, [C, TopicOn]()
		{
			const UAsdaMinimapSubsystem* Mm = C->Sub<UAsdaMinimapSubsystem>();
			TopicOn(C, (Mm && Mm->IsExpanded()) ? 26 : 12, EAsdaTipOwner::Minimap, TEXT("M3 наведение на 1708 (по развороту окна)"));
			FVector2D P;
			if (Center(FindLiveWidget(C, TEXT("MINIMAP_MAXBUT")), P)) { Hover(P); }
		});
		At(C, 7.8f, [C, TopicOn]()
		{
			TopicOn(C, 15, EAsdaTipOwner::Minimap, TEXT("M4 наведение на 1707"));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 8.2f, [C]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(), FString::Printf(TEXT("M5 уход с мини-карты → справка погашена (%s)"), *TipState(C)));
		});

		// ═══ R. Панель отряда: темы 22/19/20, шестерёнка 2279, подсказка иконки состояния ═══
		At(C, 8.6f, [C]()
		{
			UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
			UAsdaEffectsSubsystem* Fx = C->Sub<UAsdaEffectsSubsystem>();
			if (!Net || !Fx) { C->Expect(false, TEXT("R0 подсистемы сети/эффектов")); return; }
			C->NetScope.Capture(Net, kCmd);
			Net->SelectedCharId = kMe; Net->SetSelfUserNumForDebug(kMyUserNum);
			// Отряд из трёх, я глава: 5093 код 1 + 5107 (как `asda.SelfTest.PartyHud`).
			TArray<uint8> B; B.SetNumZeroed(15);
			B[0] = 1; AsdaBytes::WrI16(&B[1], kMyUserNum); AsdaBytes::WrI32(&B[3], kMe);
			AsdaBytes::WrI16(&B[7], 322); AsdaBytes::WrI32(&B[9], kMate); AsdaBytes::WrI16(&B[13], kParty);
			Net->DebugFeedPacket(5093, B);
			TArray<uint8> R; R.SetNumZeroed(144);
			const int32 Ids[6] = { kMe, kMate, kThird, -1, -1, -1 };
			for (int32 i = 0; i < 6; ++i)
			{
				AsdaBytes::WrI32(&R[4 * i], Ids[i]);
				if (Ids[i] != -1) { FAsdaLoginData::EncodeCp1251(FString::Printf(TEXT("Member%d"), i), &R[24 + 20 * i], 19); }
			}
			Net->DebugFeedPacket(5107, R);
			// 5108 соотрядника Member1: запись 0 — «дыра» (вид 0), запись 1 — состояние из stateinfotip (вид 1).
			FString Nm, Atl, Txt; int32 Cell = 0;
			for (int32 Id = 1; Id < 4000 && C->StateId < 0; ++Id) { if (Fx->GetStateInfo(Id, Nm, Atl, Cell, Txt) && !Nm.IsEmpty()) { C->StateId = Id; C->StateTitle = Nm; } }
			TArray<uint8> S; S.SetNumZeroed(4 + 6 * 28);
			AsdaBytes::WrI32(&S[0], kMate);
			S[4 + 6 * 1] = 1; AsdaBytes::WrI32(&S[4 + 6 * 1 + 2], C->StateId);
			Net->DebugFeedPacket(5108, S);
			C->Expect(C->StateId > 0, FString::Printf(TEXT("R0 отряд из трёх (5093+5107), 5108 соотрядника: состояние %d «%s» во второй записи"), C->StateId, *C->StateTitle));
		});
		At(C, 9.2f, [C]()
		{
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("PARTY_WIN")));
			for (int32 R = 0; Win && R < 6 && C->MateRow < 0; ++R)
			{
				const UTextBlock* T = Cast<UTextBlock>(AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("PARTY_NAME%02d"), R)));
				if (T && T->GetText().ToString() == TEXT("Member1")) { C->MateRow = R; }
			}
			FVector2D P = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Win && C->MateRow >= 0 && Center(AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("PARTY_NAME%02d"), C->MateRow)), P);
			C->Expect(bGeo, FString::Printf(TEXT("R1 строка соотрядника Member1 (%d) на экране (%.0f,%.0f)"), C->MateRow, P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 9.6f, [C, TopicOn]()
		{
			TopicOn(C, 22, EAsdaTipOwner::PartyHud, TEXT("R2 наведение на имя соотрядника"));
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("PARTY_WIN")));
			FVector2D P;
			if (Win && Center(Win->GetWidgetFromName(TEXT("PARTY_EXITBUT")), P)) { Hover(P); }
			else { C->Expect(false, TEXT("R2 кнопка 2260 PARTY_EXITBUT на экране")); }
		});
		At(C, 10.0f, [C, TopicOn]()
		{
			TopicOn(C, 19, EAsdaTipOwner::PartyHud, TEXT("R3 наведение на кнопку 2260"));
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("PARTY_WIN")));
			FVector2D P;
			if (Win && Center(Win->GetWidgetFromName(TEXT("PARTY_MINIBUT")), P)) { Hover(P); }
			else { C->Expect(false, TEXT("R3 кнопка 2279 PARTY_MINIBUT на экране")); }
		});
		At(C, 10.4f, [C, TopicOn]()
		{
			TopicOn(C, 20, EAsdaTipOwner::PartyHud, TEXT("R4 наведение на кнопку 2279"));
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("PARTY_WIN")));
			const UWidget* Gear = Win ? AsdaUi::FindNodeByPrefix(Win, TEXT("Party_Gear_")) : nullptr;
			const UWidget* Ani = Win ? AsdaUi::FindNodeByPrefix(Win, TEXT("Party_AniGear_")) : nullptr;
			C->Expect(Gear && Ani && !AsdaUi::IsShown(Gear) && AsdaUi::IsShown(Ani),
				FString::Printf(TEXT("R4 шестерёнка 2280 скрыта, анимированная 2281 показана (%s / %s)"),
					Gear ? *Gear->GetName() : TEXT("нет"), Ani ? *Ani->GetName() : TEXT("нет")));
			FVector2D P = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Win && Center(AsdaUi::FindNodeByPrefix(Win, *FString::Printf(TEXT("PARTY_STATE_ICON%d00_"), C->MateRow)), P);
			C->IconPt = P;
			C->Expect(bGeo, FString::Printf(TEXT("R4 иконка состояния соотрядника на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 10.8f, [C]()
		{
			UUserWidget* Win = C->Sub<UAsdaWindowSubsystem>()->FindWindow(FName(TEXT("PARTY_WIN")));
			const UWidget* Gear = Win ? AsdaUi::FindNodeByPrefix(Win, TEXT("Party_Gear_")) : nullptr;
			const UWidget* Ani = Win ? AsdaUi::FindNodeByPrefix(Win, TEXT("Party_AniGear_")) : nullptr;
			C->Expect(Gear && Ani && AsdaUi::IsShown(Gear) && !AsdaUi::IsShown(Ani), TEXT("R5 уход с 2279 → шестерёнки вернулись"));
			C->Expect(TipOf(C, EAsdaTipOwner::PartyHud) && TipText(C).Contains(C->StateTitle),
				FString::Printf(TEXT("R5 наведение на иконку состояния → подсказка «%s» (%s; на плашке: %s)"), *C->StateTitle, *TipState(C), *TipText(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Party.StateTipFix"), 0);
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 11.2f, [C]() { Hover(C->IconPt); });
		At(C, 11.6f, [C]()
		{
			C->Expect(!TipOf(C, EAsdaTipOwner::PartyHud),
				FString::Printf(TEXT("R6 ключ asda.Party.StateTipFix 0: вид из «дырявой» записи — подсказки нет, дефект оригинала воспроизведён (%s)"), *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.Party.StateTipFix"), 1);
			if (C->bWorldPt) { Hover(C->WorldPt); }
			// Роспуск 5103 и свои номера — обратно.
			if (UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>())
			{
				TArray<uint8> B; B.SetNumZeroed(2); AsdaBytes::WrI16(&B[0], kParty);
				Net->DebugFeedPacket(5103, B);
			}
			C->NetScope.Restore();
		});

		// ═══ C. Окно чата: вкладка 2501 и поле ввода 2609 ═══
		At(C, 12.0f, [C]()
		{
			UAsdaChatWindowSubsystem* Ch = C->Sub<UAsdaChatWindowSubsystem>();
			FVector2D P = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Ch && Center(FAsdaHoverNoSelectSelfTest::ChatTab(Ch), P);
			C->Expect(bGeo, FString::Printf(TEXT("C0 вкладка чата 2501 на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 12.4f, [C]()
		{
			const FString T = AsdaNotice::Loc(C->GI(), 5361), B = AsdaNotice::Loc(C->GI(), 5362);
			C->Expect(TipOf(C, EAsdaTipOwner::ChatWin) && TipText(C).Contains(T) && TipText(C).Contains(B.Left(12)),
				FString::Printf(TEXT("C1 наведение на вкладку → «%s» / «%s» (%s)"), *T, *B, *TipState(C)));
			if (UAsdaChatWindowSubsystem* Ch = C->Sub<UAsdaChatWindowSubsystem>()) { Ch->OpenInput(0); }
		});
		At(C, 12.8f, [C]()
		{
			UAsdaChatWindowSubsystem* Ch = C->Sub<UAsdaChatWindowSubsystem>();
			FVector2D P = FVector2D::ZeroVector;   // ★ R5-fix-b 13.11.2026: ноль — C4701 (на ветке без присвоения значение уходит в текст проверки)
			const bool bGeo = Ch && Center(FAsdaHoverNoSelectSelfTest::ChatInput(Ch), P);
			C->Expect(bGeo, FString::Printf(TEXT("C2 поле ввода 2609 на экране (%.0f,%.0f)"), P.X, P.Y));
			if (bGeo) { Hover(P); }
		});
		At(C, 13.2f, [C]()
		{
			const FString T = AsdaNotice::Loc(C->GI(), 5363), B = AsdaNotice::Loc(C->GI(), 5364);
			C->Expect(TipOf(C, EAsdaTipOwner::ChatWin) && TipText(C).Contains(T) && TipText(C).Contains(B.Left(12)),
				FString::Printf(TEXT("C3 наведение на поле ввода → «%s» / «%s» (%s)"), *T, *B, *TipState(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 13.6f, [C]()
		{
			const UAsdaItemTooltipSubsystem* T = C->Sub<UAsdaItemTooltipSubsystem>();
			C->Expect(T && !T->IsTooltipShown(), FString::Printf(TEXT("C4 уход с чата → подсказка погашена (%s)"), *TipState(C)));
			if (UAsdaChatWindowSubsystem* Ch = C->Sub<UAsdaChatWindowSubsystem>()) { Ch->CloseInput(); }
		});

		// ═══ O. Окно осмотра соседа ═══
		At(C, 15.2f, [C]()
		{
			UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>();
			UAsdaNetSubsystem* Net = C->Sub<UAsdaNetSubsystem>();
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			UAsdaItemTableSubsystem* IT = C->Sub<UAsdaItemTableSubsystem>();
			const AActor* Me = (WS && ::IsValid(WS->LocalPlayerActor.Get())) ? WS->LocalPlayerActor.Get() : nullptr;
			if (!WS || !Net || !OC || !IT || !Me) { C->Expect(false, TEXT("O0 мир/сеть/окно осмотра/таблица предметов/свой персонаж есть")); return; }
			// Соседи по четырём сторонам — на экран попадёт хотя бы один.
			const FIntPoint MyTile = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Me->GetActorLocation()));
			const FIntPoint Off[kPeerCount] = { {0, 3}, {3, 0}, {0, -3}, {-3, 0} };
			for (int32 i = 0; i < kPeerCount; ++i)
			{
				WS->DebugSpawnFakePeer(kPeerFirst + i, FString::Printf(TEXT("СоседНаведение%d"), i),
					AsdaCoord::TileCenterGame(MyTile.X + Off[i].X, MyTile.Y + Off[i].Y));
			}
			// Два предмета экипировки из item_list (категория не «прочее» — у тех гнёзд нет).
			int32 Codes[2] = { 0, 0 };
			int32 N = 0;
			for (int32 Code = 1; Code < 80000 && N < 2; ++Code)
			{
				if (IT->GetItem(Code) && IT->GetItemCategory(Code) != 1) { Codes[N++] = Code; }
			}
			C->Expect(N == 2, FString::Printf(TEXT("O0 предметы экипировки для слотов: %d, %d"), Codes[0], Codes[1]));
			C->PeerNum = kPeerFirst;
			const bool bReq = OC->BeginRequest(C->PeerNum);
			// 5479 (121 б): ключ пары @116. 7006 (631 б): ключ @0, записи слотов по 57 б с @4.
			TArray<uint8> B1; B1.SetNumZeroed(121);
			AsdaBytes::WrI32(B1.GetData() + 116, kPairKey);
			TArray<uint8> B2; B2.SetNumZeroed(631);
			AsdaBytes::WrI32(B2.GetData(), kPairKey);
			AsdaBytes::WrI32(B2.GetData() + 4 + kRecSize * 1, Codes[0]);
			AsdaBytes::WrI32(B2.GetData() + 4 + kRecSize * 2, Codes[1]);
			Net->DebugFeedPacket(5479, B1);
			Net->DebugFeedPacket(7006, B2);
			C->Expect(bReq && OC->IsOpen(), FString::Printf(TEXT("O0 запрос к соседу %d и пара 5479+7006 → окно осмотра открыто (%d)"),
				C->PeerNum, OC->IsOpen() ? 1 : 0));
		});
		At(C, 15.8f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			FVector2D P;
			const bool bGeo = Center(FAsdaHoverNoSelectSelfTest::EquipCell(OC, 1), P);
			C->Expect(bGeo && !AsdaUi::IsShown(FAsdaHoverNoSelectSelfTest::EquipSel(OC)) && FAsdaHoverNoSelectSelfTest::SelSlot(OC) == -1,
				FString::Printf(TEXT("O1 слот 1 на экране (%.0f,%.0f), курсор 4223 скрыт, слот курсора %d"),
					P.X, P.Y, FAsdaHoverNoSelectSelfTest::SelSlot(OC)));
			if (bGeo) { Hover(P); }
		});
		At(C, 16.2f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			C->Expect(TipOf(C, EAsdaTipOwner::OtherChara),
				FString::Printf(TEXT("O2 наведение на надетую вещь → подсказка предмета (%s)"), *TipState(C)));
			C->Expect(!AsdaUi::IsShown(FAsdaHoverNoSelectSelfTest::EquipSel(OC)) && FAsdaHoverNoSelectSelfTest::SelSlot(OC) == -1,
				FString::Printf(TEXT("O2 наведение НЕ выделяет вещь: курсор 4223 %s, слот курсора %d"),
					AsdaUi::IsShown(FAsdaHoverNoSelectSelfTest::EquipSel(OC)) ? TEXT("ПОКАЗАН") : TEXT("скрыт"), FAsdaHoverNoSelectSelfTest::SelSlot(OC)));
			FVector2D P;
			if (Center(FAsdaHoverNoSelectSelfTest::EquipCell(OC, 1), P)) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(P, /*HoldFrames*/ 2); }
		});
		At(C, 16.8f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			FVector2D PC = FVector2D::ZeroVector, PS = FVector2D::ZeroVector;
			const bool bCell = Center(FAsdaHoverNoSelectSelfTest::EquipCell(OC, 1), PC);
			const bool bSel = Center(FAsdaHoverNoSelectSelfTest::EquipSel(OC), PS);
			C->Expect(FAsdaHoverNoSelectSelfTest::SelSlot(OC) == 1 && bCell && bSel && FVector2D::Distance(PC, PS) < 6.0,
				FString::Printf(TEXT("O3 щелчок → курсор 4223 на слоте 1 (слот курсора %d, ячейка (%.0f,%.0f), курсор (%.0f,%.0f))"),
					FAsdaHoverNoSelectSelfTest::SelSlot(OC), PC.X, PC.Y, PS.X, PS.Y));
			FVector2D P2;
			if (Center(FAsdaHoverNoSelectSelfTest::EquipCell(OC, 2), P2)) { Hover(P2); }
		});
		At(C, 17.2f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			C->Expect(FAsdaHoverNoSelectSelfTest::SelSlot(OC) == 1 && TipOf(C, EAsdaTipOwner::OtherChara),
				FString::Printf(TEXT("O4 наведение на слот 2 → подсказка, курсор 4223 остался на слоте %d (%s)"),
					FAsdaHoverNoSelectSelfTest::SelSlot(OC), *TipState(C)));
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});
		At(C, 17.6f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			C->Expect(!TipOf(C, EAsdaTipOwner::OtherChara), FString::Printf(TEXT("O5 уход курсора → подсказка окна погашена (%s)"), *TipState(C)));
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.SelectOnClick"), 0);
			FVector2D P2;
			if (Center(FAsdaHoverNoSelectSelfTest::EquipCell(OC, 2), P2)) { Hover(P2); }
		});
		At(C, 18.0f, [C]()
		{
			UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			C->Expect(FAsdaHoverNoSelectSelfTest::SelSlot(OC) == 2 && AsdaUi::IsShown(FAsdaHoverNoSelectSelfTest::EquipSel(OC)),
				FString::Printf(TEXT("O6 ключ 0: наведение ставит курсор 4223 (прежний порт воспроизведён), слот курсора %d"),
					FAsdaHoverNoSelectSelfTest::SelSlot(OC)));
			AsdaCVarSave::SetForTest(TEXT("asda.OtherChara.SelectOnClick"), 1);
			if (OC) { OC->CloseWindow(); }
			if (C->bWorldPt) { Hover(C->WorldPt); }
		});

		// ═══ W. Наведение на соседа в мире ═══
		At(C, 18.4f, [C]()
		{
			UAsdaWorldSubsystem* WS = C->Sub<UAsdaWorldSubsystem>();
			AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
			if (PC) { PC->ClearTarget(); }
			for (int32 i = 0; WS && i < kPeerCount; ++i)
			{
				AActor* A = WS->GetPlayerActorByUserNum(kPeerFirst + i);
				FVector2D P;
				if (A && PeerOnScreen(C, A, P)) { C->Peer = A; C->PeerAbs = P; break; }
			}
			C->Expect(C->Peer.IsValid() && PC, FString::Printf(TEXT("W0 сосед на экране (%.0f,%.0f)"), C->PeerAbs.X, C->PeerAbs.Y));
			if (C->Peer.IsValid()) { Hover(C->PeerAbs); }
		});
		At(C, 19.0f, [C]()
		{
			AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
			const UAsdaOtherCharaSubsystem* OC = C->Sub<UAsdaOtherCharaSubsystem>();
			AActor* Peer = C->Peer.Get();
			C->Expect(PC && Peer && PC->HoverActor.Get() == Peer,
				FString::Printf(TEXT("W1 наведение на соседа замечено (HoverActor %s)"),
					PC && PC->HoverActor.IsValid() ? *PC->HoverActor->GetName() : TEXT("нет")));
			C->Expect(PC && Peer && PC->TargetActor.Get() != Peer && !(OC && OC->IsOpen()),
				FString::Printf(TEXT("W1 наведение НЕ выбирает соседа целью и не открывает осмотр (цель %s, осмотр %d)"),
					PC && PC->TargetActor.IsValid() ? *PC->TargetActor->GetName() : TEXT("нет"), OC && OC->IsOpen() ? 1 : 0));
			if (Peer) { C->Sub<UAsdaWindowSubsystem>()->SimulateClickAt(C->PeerAbs, /*HoldFrames*/ 2); }
		});
		At(C, 19.6f, [C]()
		{
			AAsdaPlayerController* PC = C->W.IsValid() ? Cast<AAsdaPlayerController>(C->W->GetFirstPlayerController()) : nullptr;
			AActor* Peer = C->Peer.Get();
			C->Expect(PC && Peer && PC->TargetActor.Get() == Peer,
				FString::Printf(TEXT("W2 щелчок по соседу → он цель (цель %s)"),
					PC && PC->TargetActor.IsValid() ? *PC->TargetActor->GetName() : TEXT("нет")));
		});
		At(C, 20.0f, [C]() { Finish(C); });
	}
}

static FAutoConsoleCommandWithWorldAndArgs GAsdaSelfTestHoverNoSelect(
	TEXT("asda.SelfTest.HoverNoSelect"),
	TEXT("Самотест «наведение не выбирает, подсказки не зависают»: пункт доп. меню на панели быстрого доступа (подсказка гаснет при уходе "
	     "в мир и на пустой слот; ключ asda.Quick.TipHideFix), окно осмотра 4200 (наведение — подсказка, щелчок — курсор 4223; ключ "
	     "asda.OtherChara.SelectOnClick), наведение на соседа в мире цель не меняет, щелчок — меняет. Офлайн, карта мира. `delay=N` `quit`."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* W)
	{
		using namespace AsdaHoverNoSelTest;
		const FAsdaSelfTestArgs KA = FAsdaSelfTestArgs::Parse(Args);
		// Свой персонаж нужен миру и панели — офлайн его нет: штатный фейковый спавн и повторный запуск через 2 с.
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
