// Copyright (c) 2026 @sitxovski (Discord)
// Реализация единого выпадающего списка JUI. Разбор данных скина — в шапке заголовка.
#include "UI/AsdaJuiComboBox.h"
#include "UI/AsdaUiHelpers.h"   // ★ R1: прямое включение (раньше приходило транзитивно)

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaCombo, Log, All);

namespace
{
	// ── ЦВЕТА И UV ИЗ `JuiSkin.ini` (см. шапку заголовка). Ни одного подобранного значения. ──
	const TCHAR* kBodySs0  = TEXT("FF213739");   // тело: обычное
	const TCHAR* kBodySs1  = TEXT("FF225777");   // тело: наведение
	const TCHAR* kBodySs2  = TEXT("FF153549");   // тело: нажато/раскрыто
	const TCHAR* kCapSs0   = TEXT("FFFFFFFF");   // подпись тела: обычная
	const TCHAR* kCapSs12  = TEXT("FFE9D6BC");   // подпись тела: наведение/нажато
	// ★★★ ЦВЕТ ПУНКТОВ — БЕЛЫЙ, И ЭТО ИЗ ЭТАЛОНА, А НЕ ИЗ `CaptionColor` СКИНА (правка 2026-08-28
	//   по жалобе владельца «текст элементов должен быть БЕЛЫМ, а не желтоватым»; касается ВСЕХ
	//   выпадающих списков порта). Цепочка доказательства:
	//     1. Пункт кладётся вызовом `CClass_DrawText(list, text, −1, 0)` — напр. все три списка
	//        аукциона (`uiItemDlg_fillAttributeLists`, `ui_core.c:52945-52999`), третий аргумент
	//        ВСЮДУ −1 = 0xFFFFFFFF.
	//     2. `CClass_DrawText@0x12F50F0` (`entity_char.c:66101`) пробрасывает его в
	//        `uiChatLogAddLine(this, Str, a3, a4, …)`.
	//     3. `uiChatLogAddLine@0x12EA470` (`ui_core.c:111678-111684`): `a3` → dword-индекс 670
	//        (байт +2680) записи пункта, `this->m_defaultColor` → 680 (+2720), `a4` → 690 (+2760).
	//     4. Рендер списка (`ui_core.c:113203-113222`) для НЕвыбранной строки зовёт
	//        `textDrawString(str, rect, flags, v86 /*+2680*/, v88 /*+2720*/, …)`, а у
	//        `textDrawString@0x12C2470` (`font_text.c:5947-5975`) ЦВЕТ ТЕКСТА — четвёртый
	//        параметр `a4` (единственный, который уходит в движковый DrawText). Значит цвет
	//        строки = +2680 = переданный при вставке = **0xFFFFFFFF**.
	//   `DarkGrey_List CaptionColor FFE9D6BC` — подпись САМОГО контрола-списка, а не его строк;
	//   прежняя редакция взяла её и дала «желтоватый» текст.
	const TCHAR* kRowText  = TEXT("FFFFFFFF");   // строка списка: цвет вставки (−1)
	// ★ ВЫБРАННАЯ строка: эталон рисует её ДРУГОЙ веткой (`ui_core.c:113226-113230`):
	//   `selectedTextColor = (item[+2760] & 0xFFFFFF) | 0xFF000000`, где +2760 — четвёртый
	//   аргумент вставки, а он у всех наших списков 0 ⇒ **ЧЁРНЫЙ** текст. Под ним при этом лежит
	//   плита `~item[+2760]` = 0xFFFFFFFF (белая) — то есть чёрное по белому, и пункт ЧИТАЕТСЯ.
	//   Именно из-за этой пары прежняя редакция и увела ВЕСЬ список в бежевый.
	const TCHAR* kRowTextSel = TEXT("FF000000");

	const FBox2f kUvArrowNormal(FVector2f(0.785f, 0.520f), FVector2f(0.824f, 0.559f));
	const FBox2f kUvArrowPress (FVector2f(0.834f, 0.520f), FVector2f(0.873f, 0.559f));
	const FBox2f kUvListBg     (FVector2f(0.285f, 0.816f), FVector2f(0.371f, 0.840f));

	// Плиты подсветки строки — из рендера списка (`ui_core.c:112969`), см. шапку заголовка.
	constexpr uint32 kPlateHover    = 0x96F0F0F0u;
	constexpr uint32 kPlateSelected = 0xFFFFFFFFu;

	// `Bk_ComboBox` ES0 `CaptionPoint = "5 1"` — отступ подписи от левого-верха ТЕЛА.
	constexpr float kCapOffX = 5.f;
	constexpr float kCapOffY = 1.f;

	// Z-порядок внутри канваса списка: фон 0, плита строки 1, текст 2 (эталон рисует квад ДО текста).
	constexpr int32 kZListBg = 0;
	constexpr int32 kZPlate  = 1;
	constexpr int32 kZText   = 2;
	// Список лежит выше своих соседей ВНУТРИ окна; слой «поверх ВСЕХ окон» даёт `PushTopmost`.
	constexpr int32 kZList   = 900;

	UAsdaTextureSubsystem* TexMgr()
	{
		return GEngine ? GEngine->GetEngineSubsystem<UAsdaTextureSubsystem>() : nullptr;
	}
}

bool UAsdaJuiComboBox::Setup(UUserWidget* InHost, const FString& InIdc, FName InOwnerWindowId, bool bInOpenUp,
	const FString& InPlaneIdc)
{
	Reset();
	Host = InHost;
	Idc = InIdc;
	// Планы и подпись обычно названы тем же idc; исключение — контролы с ПУСТЫМ `ID` в .JUI,
	//   у которых импортёр берёт имя скина (см. комментарий к объявлению `InPlaneIdc`).
	PlaneIdc = InPlaneIdc.IsEmpty() ? InIdc : InPlaneIdc;
	OwnerWindowId = InOwnerWindowId;
	ClaimId = FName(*(InIdc + TEXT("_ComboList")));
	bOpenUp = bInOpenUp;
	if (!Host || !Host->WidgetTree) { return false; }

	// 1. Движковый комбобокс: источник прямоугольника и родителя, дальше — гасится навсегда.
	Engine = Cast<UComboBoxString>(Host->GetWidgetFromName(FName(*Idc)));
	if (!Engine)
	{
		UE_LOG(LogAsdaCombo, Warning, TEXT("[комбобокс] в окне «%s» нет контрола «%s»"),
			*Host->GetName(), *Idc);
		return false;
	}
	if (!AsdaUi::SlotRect(Engine, BodyPos, BodySize))
	{
		UE_LOG(LogAsdaCombo, Warning, TEXT("[комбобокс] «%s» лежит не в канвасе — раскладку не снять"), *Idc);
		return false;
	}
	Engine->ClearOptions();
	Engine->SetVisibility(ESlateVisibility::Collapsed);

	UPanelWidget* ListHost = Engine->GetParent();
	if (!ListHost)
	{
		UE_LOG(LogAsdaCombo, Warning, TEXT("[комбобокс] «%s» без родителя-панели"), *Idc);
		return false;
	}

	// 2. Планы скина: `<idc>_<juiIdx>_0` тело, `<idc>_<juiIdx>_1` стрелка (закон `jui-importer-widget-naming`).
	// ★★★ РЕВИЗИЯ 2026-09-17 (С2): СРАВНИВАТЬ НАДО ДВА ВИДА КЛЮЧА, А НЕ ОДИН.
	//   `AsdaUi::ParsePlaneName` снимает С КОНЦА ДВА числовых хвоста и отдаёт `NodeIdc` БЕЗ
	//   jui-индекса: у `Bk_ComboBox_245_0` это `Bk_ComboBox`, а `JuiIdx` = 245. Вызыватель же для
	//   контрола с пустым `ID` передаёт сюда имя планов ВМЕСТЕ с индексом — `Bk_ComboBox_245`
	//   (так это описано в шапке `Setup` и так его считает `UAsdaSystemOptionSubsystem::PlaneIdcOf`),
	//   поэтому прежнее `NodeIdc != PlaneIdc` НЕ СОВПАДАЛО НИКОГДА: `BodyPlane`/`ArrowPlane`
	//   оставались null, тинт корпуса и кадр стрелки не менялись ни в одном комбобоксе с пустым `ID`.
	//   Для контрола с заполненным `ID` планы зовутся `<idc>_<jui>_<plane>` и там верен ПЕРВЫЙ вид
	//   ключа (`NodeIdc == PlaneIdc`), поэтому проверяем оба и ничего не ломаем у рабочих окон.
	Host->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (!W) { return; }
		FString NodeIdc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(W->GetName(), NodeIdc, JuiIdx, Plane)) { return; }
		const bool bMatch = (NodeIdc == PlaneIdc)                                        // `ID` есть: `<idc>_<jui>_<plane>`
			|| (FString::Printf(TEXT("%s_%d"), *NodeIdc, JuiIdx) == PlaneIdc);           // `ID` пуст: `<скин>_<jui>_<plane>`
		if (!bMatch) { return; }
		if (Plane == 0) { BodyPlane  = Cast<UImage>(W); }
		if (Plane == 1) { ArrowPlane = Cast<UImage>(W); }
	});

	// 3. ★ Геометрия по-движковому: стрелка СВОИМ размером к правому краю, тянется только тело.
	//    Импортёр масштабирует оба плана пропорционально ширине скина (110) и сплющивает стрелку.
	if (BodyPlane)  { AsdaUi::SetSlotRect(BodyPlane, BodyPos, FVector2D(FMath::Max(BodySize.X - kArrowW, 1.f), BodySize.Y)); }
	if (ArrowPlane) { AsdaUi::SetSlotRect(ArrowPlane, FVector2D(BodyPos.X + BodySize.X - kArrowW, BodyPos.Y), FVector2D(kArrowW, kArrowW)); }

	// 4. Подпись выбранного пункта — та самая, куда импортёр положил дизайнерскую рыбу `CaptionText`.
	Label = Cast<UTextBlock>(Host->GetWidgetFromName(FName(*(Idc + TEXT("_Text")))));
	if (!Label && PlaneIdc != Idc)
	{
		// Контрол с пустым `ID` в .JUI: подпись импортёр назвал по скину — `<скин>_<jui>_txt`.
		Label = Cast<UTextBlock>(Host->GetWidgetFromName(FName(*(PlaneIdc + TEXT("_txt")))));
	}
	if (Label)
	{
		Label->SetAutoWrapText(false);                       // закон `ui-autowrap-canvas-gotcha`
		Label->SetJustification(ETextJustify::Left);         // `CaptionAlign = "4"` = по левому краю
		Label->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Label->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(kCapSs0)));
		Label->SetText(FText::GetEmpty());                   // ★ рыба «ABC123» уходит здесь же
		AsdaUi::SetSlotRect(Label, FVector2D(BodyPos.X + kCapOffX, BodyPos.Y + kCapOffY),
			FVector2D(FMath::Max(BodySize.X - kArrowW - kCapOffX, 8.f), BodySize.Y));
	}

	// 5. Раскрывающаяся панель. Живёт в ТОЙ ЖЕ панели, что и сам контрол, — тогда она едет вместе
	//    с окном и с группой, и её координаты те же, что у прямоугольника контрола.
	List = Host->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),
		FName(*(Idc + TEXT("_List"))));
	if (!List) { return false; }
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(ListHost->AddChild(List)))
	{
		CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		CS->SetAlignment(FVector2D(0.f, 0.f));
		CS->SetZOrder(kZList);
	}
	List->SetVisibility(ESlateVisibility::Collapsed);

	// Фон списка — плита из атласа N_02 с UV-вырезкой скина `DarkGrey_List`.
	//   ★ Атлас берётся у менеджера текстур (кэш + refCount, закон `resource-subsystems`).
	if (UTexture2D* Tex = TexMgr() ? TexMgr()->LoadUITexture(FName(TEXT("n_02"))) : nullptr)
	{
		ListBg = Host->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*(Idc + TEXT("_ListBg"))));
		if (ListBg)
		{
			FSlateBrush Br;
			Br.SetResourceObject(Tex);
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(BodySize.X, kRowH);
			Br.SetUVRegion(kUvListBg);
			ListBg->SetBrush(Br);
			ListBg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* BS = List->AddChildToCanvas(ListBg))
			{
				BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
				BS->SetOffsets(FMargin(0.f));
				BS->SetZOrder(kZListBg);
			}
		}
	}

	bReady = true;
	ApplyBodyState();
	UE_LOG(LogAsdaCombo, Log,
		TEXT("[комбобокс] «%s» собран по скину Bk_ComboBox: тело %.0f×%.0f, стрелка %.0f×%.0f, ")
		TEXT("список раскрывается %s"),
		*Idc, BodySize.X - kArrowW, BodySize.Y, kArrowW, kArrowW, bOpenUp ? TEXT("вверх") : TEXT("вниз"));
	return true;
}

void UAsdaJuiComboBox::Reset()
{
	// Слой «поверх всего» держится заявкой — снять его обязаны мы, иначе зона останется висеть.
	if (bTopmostPushed)
	{
		if (UWorld* W = Host ? Host->GetWorld() : nullptr)
		{
			if (UGameInstance* GI = W->GetGameInstance())
			{
				if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>()) { WM->PopTopmost(ClaimId); }
			}
		}
		bTopmostPushed = false;
	}
	Host = nullptr; Engine = nullptr; BodyPlane = nullptr; ArrowPlane = nullptr;
	Label = nullptr; List = nullptr; ListBg = nullptr;
	RowBgs.Reset(); Rows.Reset(); ItemLabels.Reset();
	bReady = false; bOpen = false; SelectedIndex = INDEX_NONE; HoverRow = INDEX_NONE;
}

void UAsdaJuiComboBox::SetItems(const TArray<FString>& Labels)
{
	ItemLabels = Labels;
	SelectedIndex = ItemLabels.Num() > 0 ? 0 : INDEX_NONE;
	HoverRow = INDEX_NONE;
	RebuildRows();
	SetOpen(false);
	SetSelectedIndex(SelectedIndex);
}

void UAsdaJuiComboBox::RebuildRows()
{
	if (!bReady || !List || !Host || !Host->WidgetTree) { return; }
	// Строк ровно столько, сколько пунктов: список — не пул, а точная копия состава (эталон
	//   пересобирает его целиком в `uiCtrlReleaseChildAndResize` + `CClass_DrawText` по пунктам).
	for (UImage* B : RowBgs)     { if (B) { B->RemoveFromParent(); } }
	for (UTextBlock* T : Rows)   { if (T) { T->RemoveFromParent(); } }
	RowBgs.Reset(); Rows.Reset();

	UTexture2D* White = TexMgr()
		? TexMgr()->LoadTextureCached(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"))
		: nullptr;
	const FSlateFontInfo Font = Label ? Label->GetFont() : FSlateFontInfo();

	for (int32 i = 0; i < ItemLabels.Num(); ++i)
	{
		UImage* Bg = Host->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),
			FName(*FString::Printf(TEXT("%s_RowBg%d"), *Idc, i)));
		if (Bg)
		{
			FSlateBrush Br;
			if (White) { Br.SetResourceObject(White); }
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(BodySize.X, kRowH);
			Bg->SetBrush(Br);
			Bg->SetVisibility(ESlateVisibility::Collapsed);   // мышь ловит СТРОКА, не плита
			if (UCanvasPanelSlot* BS = List->AddChildToCanvas(Bg))
			{
				BS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				BS->SetAlignment(FVector2D(0.f, 0.f));
				BS->SetOffsets(FMargin(0.f, kRowH * i, BodySize.X, kRowH));
				BS->SetZOrder(kZPlate);
			}
		}
		RowBgs.Add(Bg);

		UTextBlock* Row = Host->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Row%d"), *Idc, i)));
		if (!Row) { continue; }
		Row->SetAutoWrapText(false);
		Row->SetFont(Font);
		Row->SetText(FText::FromString(ItemLabels[i]));
		Row->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(kRowText)));
		Row->SetVisibility(ESlateVisibility::Visible);       // строка ловит мышь (хит-тест по геометрии)
		Row->SetCursor(EMouseCursor::Hand);
		if (UCanvasPanelSlot* RS = List->AddChildToCanvas(Row))
		{
			RS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			RS->SetAlignment(FVector2D(0.f, 0.f));
			RS->SetOffsets(FMargin(kCapOffX, kRowH * i + kCapOffY, BodySize.X - kCapOffX * 2.f, kRowH));
			RS->SetZOrder(kZText);
		}
		Rows.Add(Row);
	}
	ApplyListGeometry();
}

void UAsdaJuiComboBox::ApplyListGeometry()
{
	if (!List) { return; }
	const float H = kRowH * (float)FMath::Max(1, ItemLabels.Num());
	const float Y = bOpenUp ? (BodyPos.Y - H) : (BodyPos.Y + BodySize.Y);
	AsdaUi::SetSlotRect(List, FVector2D(BodyPos.X, Y), FVector2D(BodySize.X, H));
	if (ListBg)
	{
		FSlateBrush Br = ListBg->GetBrush();
		Br.ImageSize = FVector2D(BodySize.X, H);
		ListBg->SetBrush(Br);
	}
}

void UAsdaJuiComboBox::SetSelectedIndex(int32 Idx)
{
	SelectedIndex = ItemLabels.IsValidIndex(Idx) ? Idx : INDEX_NONE;
	if (Label)
	{
		Label->SetText(SelectedIndex != INDEX_NONE ? FText::FromString(ItemLabels[SelectedIndex]) : FText::GetEmpty());
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		// ★★★ ЦВЕТ ПУНКТА — ДВЕ ВЕТКИ ЭТАЛОНА (`ui_core.c:113203-113230`, разбор — у констант):
		//   обычная строка = цвет вставки (−1 ⇒ БЕЛЫЙ), ВЫБРАННАЯ = `(a4 & 0xFFFFFF) | 0xFF000000`
		//   (a4 = 0 ⇒ ЧЁРНЫЙ) поверх белой плиты `~a4`. Раньше обе ветки красились в бежевый
		//   `FFE9D6BC` — это подпись контрола-списка, а не его строк (жалоба владельца 2026-08-28).
		if (Rows[i])
		{
			Rows[i]->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(
				i == SelectedIndex ? kRowTextSel : kRowText)));
		}
		if (RowBgs.IsValidIndex(i) && RowBgs[i])
		{
			if (i == HoverRow)
			{
				RowBgs[i]->SetBrushTintColor(FSlateColor(AsdaUi::SrgbU32ToLinear(kPlateHover)));
				RowBgs[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else if (i == SelectedIndex)
			{
				RowBgs[i]->SetBrushTintColor(FSlateColor(AsdaUi::SrgbU32ToLinear(kPlateSelected)));
				RowBgs[i]->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				RowBgs[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void UAsdaJuiComboBox::SetOpen(bool bInOpen)
{
	if (!bReady) { return; }
	bOpen = bInOpen && ItemLabels.Num() > 0;
	if (List)
	{
		ApplyListGeometry();
		List->SetVisibility(bOpen ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (!bOpen) { HoverRow = INDEX_NONE; }
	ApplyBodyState();
	SetSelectedIndex(SelectedIndex);
	PushOrPopTopmost();
}

void UAsdaJuiComboBox::ApplyBodyState()
{
	const TCHAR* BodyHex = bOpen ? kBodySs2 : kBodySs0;
	const TCHAR* CapHex  = bOpen ? kCapSs12 : kCapSs0;
	if (BodyPlane)  { BodyPlane->SetBrushTintColor(FSlateColor(AsdaUi::SrgbHexToLinear(BodyHex))); }
	if (Label)      { Label->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(CapHex))); }
	if (ArrowPlane)
	{
		FSlateBrush Br = ArrowPlane->GetBrush();
		Br.SetUVRegion(bOpen ? kUvArrowPress : kUvArrowNormal);   // второй кадр стрелки SS2
		ArrowPlane->SetBrush(Br);
	}
}

void UAsdaJuiComboBox::PushOrPopTopmost()
{
	UWorld* W = Host ? Host->GetWorld() : nullptr;
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	if (bOpen && !bTopmostPushed)
	{
		// Раскрытый список геометрически вылезает за корпус окна ⇒ (1) корневой виджет окна
		//   поднимается на зарезервированный слой, (2) прямоугольник списка регистрируется зоной
		//   интерфейса, иначе клик по нему провалился бы в мир (см. `PushTopmost` в реестре окон).
		FAsdaUiZoneRect Rect;
		Rect.BindUObject(this, &UAsdaJuiComboBox::GetListScreenRect);
		WM->PushTopmost(ClaimId, OwnerWindowId, Rect);
		bTopmostPushed = true;
	}
	else if (!bOpen && bTopmostPushed)
	{
		WM->PopTopmost(ClaimId);
		bTopmostPushed = false;
	}
}

bool UAsdaJuiComboBox::GetListScreenRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bOpen || !List) { return false; }
	return AsdaUi::WidgetAbsRect(List, OutMin, OutMax);
}

void UAsdaJuiComboBox::Tick(const FAsdaMouseFrame& Mouse)
{
	// ★★★ ОБОРОНА ОБЩЕЙ ТОЧКИ (2026-08-28, жалоба «кнопка вызова списка не игровая»).
	//   Движковый `UComboBoxString` рисуется СВОИМ стилем — белая плашка с чёрным текстом — и
	//   лежит ПОВЕРХ планов скина `Bk_ComboBox`. `Setup` его гасит, но окно может зажечь его
	//   обратно своим проходом «показать интерактив» (так и было у аукциона: живой дамп дерева
	//   показывал `EXCHANGER_SEARCH1 ComboBoxString … VISIBLE`). Держим его погашенным здесь —
	//   тогда ни одно окно порта эту ошибку повторить не сможет.
	if (Engine && Engine->GetVisibility() != ESlateVisibility::Collapsed)
	{
		Engine->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (!bReady) { return; }

	// ── Раскрытый список: наведение и выбор пункта ──
	if (bOpen)
	{
		int32 NewHover = INDEX_NONE;
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			if (Rows[i] && AsdaUi::IsCursorOverWidget(Rows[i])) { NewHover = i; break; }
		}
		if (NewHover != HoverRow)
		{
			HoverRow = NewHover;
			SetSelectedIndex(SelectedIndex);   // перекраска строк (подсветка = функция HoverRow)
		}
		if (Mouse.bLmbDown)
		{
			if (HoverRow != INDEX_NONE)
			{
				const int32 Picked = HoverRow;
				SetOpen(false);
				SetSelectedIndex(Picked);
				OnPicked.ExecuteIfBound(Picked);
			}
			else
			{
				// ★★★ ЗАКРЫТИЕ ПО ПОВТОРНОМУ КЛИКУ (правка 2026-08-29, жалоба «список не
				//   закрывается вторым нажатием»). Раньше здесь стояло условие «курсор НЕ над
				//   телом и НЕ над стрелкой», поэтому клик по САМОМУ комбобоксу не делал ничего
				//   и список оставался раскрытым.
				//   ЭТАЛОН — `uiControlToggleDropdownPopup@0x12F...` (`ui_core.c:115070`), ветка
				//   `a2 == 1` (клик по самому контролу) ДОСЛОВНО:
				//       if ( !popup || CControl_IsVisible(popup) ) { if (popup) SetVisible(popup, 0); }
				//       else { …разместить…; SetVisible(popup, 1); }
				//   то есть строгий ПЕРЕКЛЮЧАТЕЛЬ: раскрыт -> свернуть. А ветка `a2 != 1`
				//   (клик мимо, по другому контролу) там же прячет список безусловно.
				//   ⇒ в раскрытом состоянии ЛЮБОЙ клик не по строке списка его закрывает:
				//   по телу/стрелке — это переключатель, мимо — это ветка `a2 != 1`.
				SetOpen(false);
			}
		}
		return;
	}

	// ── Свёрнутое состояние: наведение на тело/стрелку и раскрытие по клику ──
	const bool bOver = AsdaUi::IsCursorOverWidget(BodyPlane) || AsdaUi::IsCursorOverWidget(ArrowPlane);
	if (BodyPlane)
	{
		BodyPlane->SetBrushTintColor(FSlateColor(AsdaUi::SrgbHexToLinear(bOver ? kBodySs1 : kBodySs0)));
	}
	if (Label)
	{
		Label->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(bOver ? kCapSs12 : kCapSs0)));
	}
	if (bOver && Mouse.bLmbDown) { SetOpen(true); }
}
