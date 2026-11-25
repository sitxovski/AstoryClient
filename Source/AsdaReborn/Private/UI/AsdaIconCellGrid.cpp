// Copyright (c) 2026 @sitxovski (Discord)
// Реализация контроллера сетки иконок — см. AsdaIconCellGrid.h.
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaUiHelpers.h"   // ★ Аудит №4 (B1): AsdaUi::SlotOf/SlotRect — общая точка слота канваса
#include "AsdaDevConsole.h"     // ASDA_DEV_CVAR — ключ отката `asda.UI.CellGridSyncOverlay` (вне Shipping)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"

#include "Login/AsdaButtonRelay.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaIconGrid, Log, All);

// ★★★ 31.10.2026 [ИСПРАВЛЕНИЕ] — клик-зоны сетки на НОВОМ окне. Владельцы переиспользуют объект сетки между пересозданиями своего
//   окна (магазин бустеров `IconGrid`, премиум-магазин `PkgIconGrid`) и зовут `Setup`/`SetupNamed` с новым виджетом. Гвард
//   `bOverlayBuilt` оставался взведённым от прежнего (уничтоженного) окна, оверлей на новом не строился — клик по иконке молча не
//   работал (найдено полным обходом GC-якорей реле 31.10). 1 = новый владелец ⇒ прежние зоны и их реле забываются, оверлей
//   строится заново; 0 = прежнее поведение.
static ASDA_DEV_CVAR(int32, CVarAsdaCellGridRebuildOnNewOwner,
	TEXT("asda.UI.CellGridRebuildOnNewOwner"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) = Setup сетки ячеек с НОВЫМ окном строит клик-зоны заново (после пересоздания окна ")
	TEXT("магазина бустеров/премиум-магазина клик по иконке работает); 0 = как раньше (зоны остались на старом окне)."),
	ECVF_Default);

// Новый владелец сетки — забыть клик-зоны прежнего окна (разбор у ключа выше).
static void AsdaCellGridForgetOldOverlay(UUserWidget* OldOwner, UUserWidget* NewOwner, bool& bOverlayBuilt,
	TArray<TObjectPtr<UAsdaButtonRelay>>& Relays, TArray<TObjectPtr<UButton>>& HitButtons)
{
	if (!bOverlayBuilt || OldOwner == NewOwner || CVarAsdaCellGridRebuildOnNewOwner.GetValueOnGameThread() == 0) { return; }
	bOverlayBuilt = false;
	Relays.Reset();
	HitButtons.Reset();
	UE_LOG(LogAsdaIconGrid, Log, TEXT("[сетка] новый владелец «%s» (был «%s») — клик-зоны строятся заново"),
		NewOwner ? *NewOwner->GetName() : TEXT("нет"), OldOwner ? *OldOwner->GetName() : TEXT("нет"));
}

UWidget* UAsdaIconCellGrid::FindCell(UUserWidget* W, const FString& Prefix)
{
	if (!W || !W->WidgetTree) return nullptr;
	TArray<UWidget*> All;
	W->WidgetTree->GetAllWidgets(All);
	for (UWidget* Widget : All)
	{
		if (Widget && Widget->GetName().StartsWith(Prefix)) { return Widget; }
	}
	return nullptr;
}

void UAsdaIconCellGrid::Setup(UUserWidget* InOwner, const FString& InCellPrefix, int32 InNumCells, bool bMale,
	TFunction<int32(int32)> InCodeProvider, TFunction<void(int32)> InOnClick)
{
	AsdaCellGridForgetOldOverlay(Owner, InOwner, bOverlayBuilt, CellRelays, CellHitButtons);   // ★ 31.10.2026
	Owner = InOwner;
	CellPrefix = InCellPrefix;
	NumCells = InNumCells;
	IconSex = bMale ? AsdaUi::IconSexSelf : 0;   // ★ 08.11.2026: true = свой пол (эталон), см. шапку .h
	CodeProvider = MoveTemp(InCodeProvider);
	OnClick = MoveTemp(InOnClick);
	CellNames.Reset();
	BuildClickOverlay();
}

// Вход по ЯВНОМУ списку имён — для окон, где у JUI-ячеек нет `idc` (см. шапку .h).
void UAsdaIconCellGrid::SetupNamed(UUserWidget* InOwner, const TArray<FName>& InCellNames, bool bMale,
	TFunction<int32(int32)> InCodeProvider, TFunction<void(int32)> InOnClick)
{
	AsdaCellGridForgetOldOverlay(Owner, InOwner, bOverlayBuilt, CellRelays, CellHitButtons);   // ★ 31.10.2026
	Owner = InOwner;
	CellNames = InCellNames;
	CellPrefix.Reset();
	NumCells = InCellNames.Num();
	IconSex = bMale ? AsdaUi::IconSexSelf : 0;   // ★ 08.11.2026: true = свой пол (эталон), см. шапку .h
	bClearEmptyCells = true;   // страничная сетка ⇒ пустые ячейки гасим (см. Refresh)
	CodeProvider = MoveTemp(InCodeProvider);
	OnClick = MoveTemp(InOnClick);
	BuildClickOverlay();
}

// Одна точка «как зовут ячейку №Cell»: явное имя ищем ТОЧНО, схему префикса — по началу имени.
UWidget* UAsdaIconCellGrid::CellWidget(int32 Cell) const
{
	if (!Owner) { return nullptr; }
	if (CellNames.IsValidIndex(Cell))
	{
		return Owner->GetWidgetFromName(CellNames[Cell]);
	}
	return FindCell(Owner, FString::Printf(TEXT("%s%02d"), *CellPrefix, Cell));
}

void UAsdaIconCellGrid::BuildClickOverlay()
{
	if (bOverlayBuilt || !bWantClickOverlay || !Owner || !Owner->WidgetTree) return;

	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		UWidget* Box = CellWidget(Cell);
		UCanvasPanelSlot* BS = Box ? AsdaUi::SlotOf(Box) : nullptr;
		if (!BS) { continue; }

		// ★★★ КЛИК-ЗОНА ЖИВЁТ В КОРНЕВОМ КАНВАСЕ ОКНА И СТАВИТСЯ ПО АБСОЛЮТНОЙ ПОЗИЦИИ ИКОНКИ.
		//   ДЕФЕКТ (найден 2026-08-26 по жалобе «первые ячейки витрины магазина кладут в корзину»):
		//   раньше кнопка добавлялась В РОДИТЕЛЯ ИКОНКИ и получала ЛОКАЛЬНУЮ позицию её слота.
		//   Для плоской раскладки это верно, но в `.JUI` ячейки заданы ЦЕПОЧКОЙ ВЛОЖЕННОСТИ:
		//   `TRAY01` лежит внутри `TRAY00`, `TRAY02` внутри `TRAY01` и так далее (то же у корзины).
		//   При такой структуре «локальная позиция в своём родителе» у разных ячеек отсчитывается
		//   от РАЗНЫХ начал, и часть зон уезжала на чужую панель — клик по витрине попадал в корзину.
		//   Абсолютная позиция в корне от иерархии не зависит вовсе, поэтому берём её.
		UPanelWidget* HostCanvas = Cast<UPanelWidget>(Owner->WidgetTree->RootWidget);
		if (!HostCanvas) { HostCanvas = Box->GetParent(); }
		if (!HostCanvas) { continue; }
		// ★★★ ПОЗИЦИЯ И АНКОРЫ. Одной суммы `Left/Top` мало: окно может быть прижато к правому
		//   краю (у магазина бустеров `Booster_Shop_Part2_grp` анкор (1,0) и `Left = -667`), и
		//   тогда сумма отрицательна. Зона с анкором (0,0) уезжала за левый край, Slate её не
		//   рисовал (живая геометрия (0,0)-(0,0)) — клик по иконке не срабатывал ВООБЩЕ.
		FAnchors AbsAnchors(0.f, 0.f, 0.f, 0.f);
		FVector2D AbsPos = FVector2D::ZeroVector;
		AsdaUi::CanvasRootPlacement(Box, AbsAnchors, AbsPos);

		// Прозрачная кнопка ровно поверх ячейки (JUI Image не кликабелен) — только клик.
		// ★ Имя зоны: тег владельца (если задан) → префикс ячеек → историческое `CELL`.
		//   Уникальность обязательна: в одном окне может быть несколько именованных сеток.
		const FString HitTag = !OverlayTag.IsEmpty() ? OverlayTag
			: (CellPrefix.IsEmpty() ? FString(TEXT("CELL")) : CellPrefix);
		UButton* Btn = Owner->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),
			FName(*FString::Printf(TEXT("%s_HIT%02d"), *HitTag, Cell)));
		FButtonStyle St = Btn->GetStyle();
		St.Normal.TintColor = St.Hovered.TintColor = St.Pressed.TintColor = FSlateColor(FLinearColor(1, 1, 1, 0));
		Btn->SetStyle(St);
		if (UCanvasPanelSlot* NS = Cast<UCanvasPanelSlot>(HostCanvas->AddChild(Btn)))
		{
			// Анкор — левый верх корня, позиция — абсолютная: зона встаёт ровно поверх иконки,
			//   какой бы глубины ни была её собственная вложенность.
			NS->SetAnchors(AbsAnchors);
			NS->SetAlignment(FVector2D(0.f, 0.f));
			NS->SetPosition(AbsPos);
			// ★ Клик-зона во всю КЛЕТКУ (размер задаёт владелец через `SetCellSizePx`; по умолчанию
			//   64×64 — размер `ICONBOX` инвентаря/витрины), а не в 10px-плейсхолдер Log_NoImage_Icon.
			NS->SetSize(FVector2D(CellSizePx, CellSizeYPx));
			NS->SetZOrder(50);
			UE_LOG(LogAsdaIconGrid, Verbose,
				TEXT("[сетка] ячейка %d «%s»: зона в корне (%.0f,%.0f), размер %.0fx%.0f"),
				Cell, *Box->GetName(), AbsPos.X, AbsPos.Y, CellSizePx, CellSizeYPx);
		}
		// Реле несёт индекс ячейки; клик → колбэк владельца.
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = Cell;
		R->Callback = [this](int32 CellIdx) { if (OnClick) { OnClick(CellIdx); } };
		Btn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		// ★ Наведение/уход — общая точка для подсказок (см. `SetHoverHandlers`).
		R->HoverCallback   = [this, Cell]() { if (OnHover)   { OnHover(Cell); } };
		R->UnhoverCallback = [this]()       { if (OnUnhover) { OnUnhover(); } };
		Btn->OnHovered.AddDynamic(R, &UAsdaButtonRelay::OnHovered);
		Btn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
		AsdaUi::AnchorRelay(CellRelays, R, Btn);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
		CellHitButtons.Add(Btn);
	}
	bOverlayBuilt = true;
}

// ★★★ [ИСПРАВЛЕНИЕ] 29.10.2026 — разбор у объявления `SyncClickOverlay` в .h. 0 = прежнее поведение (зоны стоят там,
//   где их поставил `BuildClickOverlay`, и не следуют за ячейками, которые владелец передвинул своим кодом).
static ASDA_DEV_CVAR(int32, CVarAsdaCellGridSyncOverlay,
	TEXT("asda.UI.CellGridSyncOverlay"), 1,
	TEXT("1 = клик-зоны сетки ячеек переставляются на текущее место ячеек по запросу владельца (окно наград 540 и др.); ")
	TEXT("0 = как раньше (зоны на месте первой постройки)."),
	ECVF_Default);

void UAsdaIconCellGrid::SyncClickOverlay(bool bFollowCellVisibility)
{
	if (!bOverlayBuilt || !Owner || CVarAsdaCellGridSyncOverlay.GetValueOnGameThread() == 0) { return; }
	// Зона и реле добавляются парой (`BuildClickOverlay`), номер ячейки несёт реле: ячейки без слота там пропущены,
	//   поэтому индекс массива зон с номером ячейки совпадать не обязан.
	for (int32 i = 0; i < CellHitButtons.Num() && i < CellRelays.Num(); ++i)
	{
		UButton* Btn = CellHitButtons[i].Get();
		const UAsdaButtonRelay* R = CellRelays[i].Get();
		UCanvasPanelSlot* NS = Btn ? Cast<UCanvasPanelSlot>(Btn->Slot) : nullptr;
		UWidget* Box = R ? CellWidget(R->Index) : nullptr;
		if (!NS || !Box) { continue; }
		FAnchors AbsAnchors(0.f, 0.f, 0.f, 0.f);
		FVector2D AbsPos = FVector2D::ZeroVector;
		AsdaUi::CanvasRootPlacement(Box, AbsAnchors, AbsPos);   // та же формула, что при постройке зоны
		NS->SetAnchors(AbsAnchors);
		NS->SetPosition(AbsPos);
		if (bFollowCellVisibility)
		{
			// Видима ли ячейка с учётом ВСЕЙ цепочки родителей до корня окна (погашенная панель гасит и строку).
			bool bShown = true;
			for (const UWidget* W = Box; W; W = W->GetParent())
			{
				const ESlateVisibility V = W->GetVisibility();
				if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { bShown = false; break; }
			}
			AsdaUi::SetShownHit(Btn, bShown);
		}
		UE_LOG(LogAsdaIconGrid, Verbose, TEXT("[сетка] ячейка %d «%s»: зона переставлена в (%.0f,%.0f)%s"),
			R->Index, *Box->GetName(), AbsPos.X, AbsPos.Y,
			(bFollowCellVisibility && Btn->GetVisibility() == ESlateVisibility::Collapsed) ? TEXT(", погашена (ячейка не видна)") : TEXT(""));
	}
}

UButton* UAsdaIconCellGrid::GetHitButton(int32 Cell) const
{
	for (int32 i = 0; i < CellHitButtons.Num() && i < CellRelays.Num(); ++i)
	{
		if (CellRelays[i] && CellRelays[i]->Index == Cell) { return CellHitButtons[i].Get(); }
	}
	return nullptr;
}

void UAsdaIconCellGrid::SetHitZonesVisible(bool bVisible)
{
	for (const TObjectPtr<UButton>& B : CellHitButtons)
	{
		AsdaUi::SetShownHit(B, bVisible);
	}
}

// ★★★ Номер ячейки под курсором по ЖИВОЙ геометрии клик-зоны (обоснование — в заголовке).
int32 UAsdaIconCellGrid::HitTestCellUnderCursor() const
{
	for (int32 i = 0; i < CellHitButtons.Num(); ++i)
	{
		const UButton* B = CellHitButtons[i].Get();
		// Погашенная зона мышь не ловит — пропускаем её и здесь, иначе хит-тест «видел» бы то,
		//   чего игрок нажать не может.
		if (!B || B->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		if (AsdaUi::IsCursorOverWidget(B)) { return i; }
	}
	return -1;
}

FString UAsdaIconCellGrid::DumpHitZonesLive(const TCHAR* Title, int32 Count) const
{
	FString S = FString::Printf(TEXT("  --- ЖИВЫЕ клик-зоны «%s» ---\n"), Title);
	for (int32 i = 0; i < CellHitButtons.Num() && i < Count; ++i)
	{
		UButton* B = CellHitButtons[i].Get();
		if (!B) { S += FString::Printf(TEXT("    зона %02d: НЕТ КНОПКИ\n"), i); continue; }
		FVector2D Mn, Mx;
		const bool bOk = AsdaUi::WidgetAbsRect(B, Mn, Mx);
		S += FString::Printf(TEXT("    зона %02d живая: %s(%.0f,%.0f)-(%.0f,%.0f) вид=%d\n"),
			i, bOk ? TEXT("") : TEXT("НЕТ ГЕОМЕТРИИ "), Mn.X, Mn.Y, Mx.X, Mx.Y, (int32)B->GetVisibility());
	}
	return S;
}

FString UAsdaIconCellGrid::DumpHitZones(const TCHAR* Title) const
{
	FString S = FString::Printf(TEXT("  --- клик-зоны «%s» (в корне окна) ---\n"), Title);
	for (int32 i = 0; i < CellHitButtons.Num(); ++i)
	{
		UButton* B = CellHitButtons[i].Get();
		if (!B) { continue; }
		const UCanvasPanelSlot* CS = AsdaUi::SlotOf(B);
		const FVector2D P = AsdaUi::CanvasRootPos(B);
		S += FString::Printf(TEXT("    зона %02d: в корне (%.0f,%.0f), локально (%.0f,%.0f), размер %.0fx%.0f, родитель «%s»\n"),
			i, P.X, P.Y,
			CS ? CS->GetPosition().X : -1.f, CS ? CS->GetPosition().Y : -1.f,
			CS ? CS->GetSize().X : -1.f, CS ? CS->GetSize().Y : -1.f,
			B->GetParent() ? *B->GetParent()->GetName() : TEXT("?"));
	}
	return S;
}

// ★★★ ОБЩАЯ ТОЧКА: иконка предмета в одном `UImage` (см. подробное «зачем» в заголовке).
//   Всё тело — бывший внутренний код `Refresh`; комментарии оригинала сохранены.
bool UAsdaIconCellGrid::ApplyItemIcon(UObject* WorldCtx, UImage* Box, int32 Code, int32 InIconSex,
	float IconW, float IconH, bool bClearIfEmpty)
{
	if (!Box) { return false; }
	if (Code <= 0)
	{
		if (bClearIfEmpty)
		{
			FSlateBrush Empty;
			Empty.SetResourceObject(nullptr);
			Empty.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
			Box->SetBrush(Empty);
		}
		return false;
	}
	UGameInstance* GI = nullptr;
	if (const UUserWidget* UW = Cast<UUserWidget>(WorldCtx)) { GI = UW->GetGameInstance(); }
	else if (WorldCtx) { if (UWorld* W = WorldCtx->GetWorld()) { GI = W->GetGameInstance(); } }
	// Иконка = ЯЧЕЙКА АТЛАСА (эталон uiInventory_DrawItemOptionIcons). ★ Волна 1 (14.09.2026, В1): атлас,
	//   UV и заворот по модулю — общая точка `AsdaUi::ApplyItemIcon` (размер кисти = размер клетки).
	//   Нет данных/атласа → false, базовый слот остаётся — как было.
	if (!AsdaUi::ApplyItemIcon(GI, Box, Code, FVector2D::ZeroVector, InIconSex, /*bWrap*/ true)) { return false; }
	if (IconW > 0.f && IconH > 0.f)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(Box)) { CS->SetSize(FVector2D(IconW, IconH)); }
	}
	return true;
}
void UAsdaIconCellGrid::Refresh()
{
	if (!Owner || !CodeProvider) return;
	UAsdaItemTableSubsystem* Items = Owner->GetGameInstance() ? Owner->GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	UAsdaTextureSubsystem* TexMgr = GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	if (!Items || !TexMgr) return;

	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		UImage* Box = Cast<UImage>(CellWidget(Cell));
		if (!Box) { continue; }
		const int32 Code = CodeProvider(Cell);
		if (Code <= 0)
		{
			// ★ Пустая ячейка: у окон, которые ПЕРЕЛИСТЫВАЮТСЯ страницами, её надо гасить — эталон
			//   `uiRefreshItemGridPage@0xB6EE70` перед заполнением страницы затирает все 25 ячеек
			//   пустой текстурой 32×32, иначе на неполной последней странице остаются иконки
			//   предыдущей. Окнам без страниц (сумка, char-create) гасить нечего, и там сохраняется
			//   прежнее поведение «базовый слот-фон остаётся» — поэтому флаг, а не общее правило.
			if (bClearEmptyCells)
			{
				FSlateBrush Empty;
				Empty.SetResourceObject(nullptr);
				Empty.TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
				Box->SetBrush(Empty);
			}
			continue;
		}
		// Рисование иконки — ОБЩАЯ ТОЧКА `ApplyItemIcon` (её же зовёт панель покупки бустер-шопа).
		const float IW = (IconSizePx  > 0.f) ? IconSizePx  : CellSizePx;
		const float IH = (IconSizeYPx > 0.f) ? IconSizeYPx : CellSizeYPx;
		ApplyItemIcon(Owner, Box, Code, IconSex, IW, IH, /*bClearIfEmpty*/false);
	}
}
