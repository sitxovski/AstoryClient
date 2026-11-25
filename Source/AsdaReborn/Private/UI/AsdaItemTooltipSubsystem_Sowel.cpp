// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemTooltipSubsystem_Sowel.cpp — ★ R2 10.11.2026 (разрез AsdaItemTooltipSubsystem.cpp, дословный перенос): окно выбора камня совель, мост инвентаря.
//   Общее для частей — в AsdaItemTooltipSubsystemInternal.h; шапка и реверс — в AsdaItemTooltipSubsystem.cpp.
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaItemTipStandalone.h"   // ★ [ПОРТ] 29.10.2026: «отдельный комплект» просмотрщика предметов редактора
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaJuiWindowHandle.h"   // ★ волна 3 (15.09.2026): каркас окон JUI
#include "AsdaSelf.h"   // ★ волна 1: «это я?» / мой номер / мой уровень — общая точка ядра (E2, E4)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "UI/AsdaLocIds.h"   // Дд2/Дд3: нотисы кнопок ОБЩИХ окон (ONEBUT/TWOBUT/SHOUTING) — одно место
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)

#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemRecordWriter.h"   // ★ U17: синтетическая запись предмета (57 б) одной точкой
#include "UI/AsdaNpcShopSubsystem.h"         // потолок цены продажи сервера (`ServerSellUnitCap`) — одна точка
#include "UI/AsdaSowelSubsystem.h"           // источник события «нет свободных гнёзд» (SOWELL_SELBOX)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaUiHelpers.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Resources/AsdaTextureSubsystem.h"   // атлас иконки инфо-панели (423)

#include "Engine/GameInstance.h"
#include "Engine/DataTable.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/RichTextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"              // кнопки окна SOWELL_SELBOX (1532/1533)
#include "Components/ProgressBar.h"
#include "Components/CanvasPanelSlot.h"     // эталонные сдвиги контролов инфо-панели
#include "Components/TextWidgetTypes.h"     // UTextLayoutWidget — чтение Justification рефлексией
#include "UObject/UnrealType.h"             // FByteProperty
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"              // ★ Д3 круга 5: подбор кегля под ширину контрола панели
#include "HAL/IConsoleManager.h"
#include "UObject/Class.h"        // StaticEnum<EAsdaTipOwner> для диагностических строк
#include "Text/AsdaLang.h"         // [ПОРТ] 09.10.2026 (мультиязычность): формат параметра на языке сессии
#include "AsdaItemTooltipSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaItemTooltipSubsystem*.cpp

// Камень лотка окна 1530 под курсором (1…3, −1 — нет). Обнуляется ТОЛЬКО при показе и закрытии окна: тик подсистемы идёт и у
//   экземпляра без окна, и сброс в тике обнулял бы значение каждый кадр — подсказка показывалась бы заново и не гасла.
static int32 GSowelBoxHover = -1;

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026: лоток окна выбора гнезда 1530 принимает щелчки, когда верхняя модалка — само 1530 (см. `TickSowelSelBox`).
static ASDA_DEV_CVAR(int32, CVarSowelSelBoxOwnModal,
	TEXT("asda.Sowel.SelBoxOwnModal"), 1,
	TEXT("1 = щелчки по камням окна 1530 работают, пока оно верхняя модалка (как оригинал); 0 = прежний порт: гейт модалки глушит его же лоток."),
	ECVF_Default);

// ★★★ ОТКАТ ГЕЙТА ПРАВОЙ ИНФО-ПАНЕЛИ ПО КОНТЕЙНЕРУ.
//   1 — наполнять панель и с вкладки «Расходники» (порт делал так; эталон — НЕТ, см. `FillInfoPanel`
//   в мосте и `CInventoryUI_OnSlotClick@0xD01F50`, item_inventory.c:36266/36356).
static ASDA_DEV_CVAR(int32, CVarLegacyInfoPanelAnyTab,
	TEXT("asda.ItemTip.LegacyInfoPanelAnyTab"), 0,
	TEXT("1 — правую инфо-панель наполняет ЛЮБАЯ вкладка (как до правки); 0 — только «Снаряжение» и надетое"),
	ECVF_Default);

// ★ 21.10.2026: контрольный ключ самотеста `asda.SelfTest.Sowel` — вернуть дефект привязки (панель остаётся на
//   прежнем экземпляре окна инвентаря после его пересоздания ⇒ в живом окне видны заглушки дизайнера). Только для
//   доказательства «до/после»; в игре всегда 0.
static ASDA_DEV_CVAR(int32, CVarLegacyNoInfoRebind,
	TEXT("asda.ItemTip.LegacyNoInfoRebind"), 0,
	TEXT("1 — НЕ перепривязывать инфо-панель к пересозданному окну инвентаря (прежний дефект: заглушки «Option 1…», «Blah-blah…»); 0 — перепривязывать"),
	ECVF_Default);

// ★ [ОРИГИНАЛ] 07.11.2026: подсказка камня окна 1530. Плашка стоит на z 9100, а окно 1530 — в полосе модалок 20000+, и верх плашки
//   уходил под окно; у эталона `g_ItemTooltip` рисуется поверх всего. На время наведения плашка встаёт над окном (ModalZ > 0),
//   при уходе курсора и закрытии окна возвращается на свой z (ModalZ = 0).
static void AsdaTipOverModal(UGameInstance* GI, int32 ModalZ)
{
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const int32 Want = (ModalZ > 0) ? ModalZ + 1 : kTipZ;
	if (WM->GetWindowZOrder(kTipWindowId) != Want) { WM->SetWindowZOrder(kTipWindowId, Want); }
}

// ══════════════════ ОКНО ВЫБОРА КАМНЯ `SOWELL_SELBOX` (1530) ═══════════════════════════════════
// Разбор эталона — в шапке .h (раздел 5). Здесь только реализация.
//   Соответствие «поле эталона → контрол» восстановлено по ПОРЯДКУ полей `CItemTooltipUI`
//   в `uiWindowBindMsgBoxes` (ui_core.c:55692-55740): pSowelTrBack01(1535) лежит по смещению
//   0x54, дальше подряд TrBack00(1536), BTray002(1540), BTray012(1541), BTray00(1542),
//   BTray01(1543), BTray02(1544), BCursor2(1537), BCursor(1538) — ровно те `gap_50[4…36]`,
//   которыми оперируют `sowelSlot_DrawStones` и `uiCtrlOnDragCommand`.
static const TCHAR* kSowelSelBoxWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_SOWELL_SELBOX.WBP_SOWELL_SELBOX_C");
static const FName  kSowelSelBoxId(TEXT("SOWELL_SELBOX"));

// ★★★ [ИСПРАВЛЕНИЕ] 09.11.2026 (баг игрока alpha-7.4.0: «при замене совела появляется окно выбора — и игра перестаёт отвечать»,
//   на видео — не работают щелчки и WASD). Окно 1530 — МОДАЛЬНОЕ (`bModal`), а показ/скрытие шли прямым `SetVisibility`
//   мимо менеджера окон: `RefreshModalInput` не вызывался. При первой привязке окно регистрировалось модальным (ввод мира
//   отдан модалке: UIOnly, вьюпорт игнорирует мышь) и тут же сворачивалось БЕЗ пересчёта — режим ввода оставался
//   «только интерфейс»; то же после «OK»/«Отмены»/Esc. Итог: после окна 1530 мир не принимал ни щелчков, ни WASD.
//   Теперь видимость окна меняет `UAsdaWindowSubsystem::SetWindowVisible` — он пересчитывает режим ввода (у эталона модальность
//   снимает сам менеджер окон, `sub_92E450`). Ключ `asda.Sowel.SelBoxInputRefresh` (0 — прежний порт).
static ASDA_DEV_CVAR(int32, CVarSowelSelBoxInputRefresh,
	TEXT("asda.Sowel.SelBoxInputRefresh"), 1,
	TEXT("1 = показ и скрытие окна выбора камня 1530 идут через менеджер окон и пересчитывают режим ввода (после окна мир снова принимает мышь и WASD); 0 = прежний порт (ввод мира оставался запертым)."),
	ECVF_Default);

// Показ/скрытие окна 1530 (см. ключ выше).
static void AsdaSetSelBoxVisible(UGameInstance* GI, UUserWidget* Box, bool bShow)
{
	if (!Box) { return; }
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Запись окна в менеджере могла быть уже снята (`CloseAll` при смене карты/разрыве) — тогда менеджер виджет не тронет:
	//   гасим/показываем его напрямую, иначе мёртвый виджет считался бы открытым окном (ревизия 09.11).
	if (WM && CVarSowelSelBoxInputRefresh.GetValueOnGameThread() != 0 && WM->IsOpen(kSowelSelBoxId))
	{
		WM->SetWindowVisible(kSowelSelBoxId, bShow, ESlateVisibility::SelfHitTestInvisible);
		return;
	}
	AsdaUi::SetShownDecor(Box, bShow);
}
static constexpr int32 kLocSowelSelBoxText = 2015;   // подпись 1531 (`InitControls`, ui_core.c:55700)
// * Дд2 (аудит `code_dedup_audit_2.md`): переименованы ПО СМЫСЛУ — имена `kLocBtnOk`/`kLocBtnCancel`
//   носило ещё и окно снасти, но с ДРУГИМИ значениями (108510/108511). Это — кнопки ИМЕННО лотка
//   самоцветов `SOWELL_SELBOX`: 1532 «ОК» и 1533 «Отмена». Общие значения — из `AsdaLoc`.
static constexpr int32 kLocSowelSelBoxOk     = AsdaLoc::kBtnOk;         // 2288, контрол 1532
static constexpr int32 kLocSowelSelBoxCancel = AsdaLoc::kBtnCancelAlt;  // 2378, контрол 1533

// План/подложка ищутся по ПРЕФИКСУ: импортёр JUI даёт точное имя только интерактиву и Text
//   (закон `jui-importer-widget-naming`), планам — `{idc}_{ctrlIdx}_{planeJ}`.
// ★ D11 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::FindNodeByPrefix` — третий режим
//   поиска встал рядом с двумя уже сведёнными (точное имя и contains-фолбэк), чтобы следующее окно
//   с планами не написало четвёртую копию обхода `WidgetTree`.
static FORCEINLINE UWidget* FindByPrefix(UUserWidget* Win, const TCHAR* Prefix)
{
	return AsdaUi::FindNodeByPrefix(Win, Prefix);
}

bool UAsdaItemTooltipSubsystem::BindSowelSelBox()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★★★ ГВАРД ПО `IsWindowLive`, А НЕ ПО NULL (регрессия 2026-08-18, разбор —
	//   `Reports/minimap_channels_fix.md`). Эталонный `uiCloseAllWindows` (порт —
	//   `UAsdaWindowSubsystem::CloseAllPlayerWindows()`; дерево виджетов сносит `CloseAll()` —
	//   вход в мир `UAsdaWorldSubsystem::RevealWorld`) снимает окно
	//   1530 с реестра и с вьюпорта, оставляя указатель ненулевым: по старой проверке привязка
	//   считалась действительной, и окно выбора гнезда самоцвета больше не показывалось.
	//   Все производные указатели (лотки, курсоры, подписи) собираются ниже заново, поэтому
	//   отдельной чистки не нужно — достаточно НЕ выйти по раннему return.
	if (bSowelWinBound && WM->IsWindowLive(SowelSelBox)) { return true; }
	bSowelWinBound = false;

	// В эталоне окно — из семейства msgbox’ов (1520/1530/1545) и помечается «поверх всего»
	//   (`helper_setByteField552(pSowelSelBox, 1)`), поэтому берём модальную z-полосу порта.
	// ★ 09.11.2026: регистрируем СРАЗУ свёрнутым — иначе менеджер на миг отдавал ввод мира модалке и сбрасывал зажатые клавиши.
	SowelSelBox = WM->OpenWindowByPath(kSowelSelBoxId, kSowelSelBoxWbp, /*ZOrder*/ 0,
	                                   CVarSowelSelBoxInputRefresh.GetValueOnGameThread() != 0 ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible,
	                                   /*bModal*/ true);
	if (!SowelSelBox || !SowelSelBox->WidgetTree)
	{
		UE_LOG(LogAsdaItemTip, Warning, TEXT("[камни] окно SOWELL_SELBOX не загрузилось (%s)"), kSowelSelBoxWbp);
		SowelSelBox = nullptr;
		return false;
	}
	AsdaSetSelBoxVisible(GetGameInstance(), SowelSelBox, false);

	SowelTrBack2 = FindByPrefix(SowelSelBox, TEXT("SOWELL_TRBACK01_grp"));   // 1535 (два камня)
	SowelTrBack3 = FindByPrefix(SowelSelBox, TEXT("SOWELL_TRBACK00_grp"));   // 1536 (три камня)
	SowelCursor2 = FindByPrefix(SowelSelBox, TEXT("SOWELL_BCURSOR2_"));      // 1537
	SowelCursor3 = FindByPrefix(SowelSelBox, TEXT("SOWELL_BCURSOR_"));       // 1538

	SowelTray2.Reset(); SowelTray3.Reset(); SowelTrayBase2.Reset(); SowelTrayBase3.Reset();
	SowelTray2.Add(Cast<UImage>(FindByPrefix(SowelSelBox, TEXT("SOWELL_BTRAY002_"))));   // 1540
	SowelTray2.Add(Cast<UImage>(FindByPrefix(SowelSelBox, TEXT("SOWELL_BTRAY012_"))));   // 1541
	SowelTray3.Add(Cast<UImage>(FindByPrefix(SowelSelBox, TEXT("SOWELL_BTRAY00_"))));    // 1542
	SowelTray3.Add(Cast<UImage>(FindByPrefix(SowelSelBox, TEXT("SOWELL_BTRAY01_"))));    // 1543
	SowelTray3.Add(Cast<UImage>(FindByPrefix(SowelSelBox, TEXT("SOWELL_BTRAY02_"))));    // 1544
	SowelTrayBase2.Add(FindByPrefix(SowelSelBox, TEXT("Sowel_Btray002_Base_")));
	SowelTrayBase2.Add(FindByPrefix(SowelSelBox, TEXT("Sowel_Btray012_Base_")));
	SowelTrayBase3.Add(FindByPrefix(SowelSelBox, TEXT("Sowel_Btray00_Base_")));
	SowelTrayBase3.Add(FindByPrefix(SowelSelBox, TEXT("Sowel_Btray01_Base_")));
	SowelTrayBase3.Add(FindByPrefix(SowelSelBox, TEXT("Sowel_Btray02_Base_")));

	// Подписи — только из таблицы текста (закон `ui-button-hover-click-rule`).
	if (UTextBlock* T = Cast<UTextBlock>(SowelSelBox->GetWidgetFromName(TEXT("SOWELL_TEXT"))))
	{
		T->SetText(FText::FromString(LocRaw(kLocSowelSelBoxText)));
	}
	if (UTextBlock* T = Cast<UTextBlock>(SowelSelBox->GetWidgetFromName(TEXT("SOWELL_BUT00_Text"))))
	{
		T->SetText(FText::FromString(LocRaw(kLocSowelSelBoxOk)));
	}
	if (UTextBlock* T = Cast<UTextBlock>(SowelSelBox->GetWidgetFromName(TEXT("SOWELL_BUT01_Text"))))
	{
		T->SetText(FText::FromString(LocRaw(kLocSowelSelBoxCancel)));
	}
	if (UButton* B = Cast<UButton>(SowelSelBox->GetWidgetFromName(TEXT("SOWELL_BUT00"))))
	{
		B->OnClicked.RemoveAll(this);
		B->OnClicked.AddDynamic(this, &UAsdaItemTooltipSubsystem::OnSowelSelBoxOk);
	}
	if (UButton* B = Cast<UButton>(SowelSelBox->GetWidgetFromName(TEXT("SOWELL_BUT01"))))
	{
		B->OnClicked.RemoveAll(this);
		B->OnClicked.AddDynamic(this, &UAsdaItemTooltipSubsystem::OnSowelSelBoxCancel);
	}
	// ★ У ИКОНОК ЛОТКА КНОПОК НЕТ. В .JUI 1540…1544 — контролы скина `NoImage_icon`, импортёр делает
	//   из них план + пустой Text, а `UButton` — только из PushButton-скинов. Клик по ним ловим
	//   покадровым хит-тестом (`TickSowelSelBox`), как это уже делает инвентарь для своих ячеек.
	bSowelWinBound = true;
	UE_LOG(LogAsdaItemTip, Log,
		TEXT("[камни] SOWELL_SELBOX привязан: лоток2=%s лоток3=%s иконки2=%d иконки3=%d курсоры=%s/%s"),
		SowelTrBack2 ? TEXT("да") : TEXT("НЕТ"), SowelTrBack3 ? TEXT("да") : TEXT("НЕТ"),
		SowelTray2.Num(), SowelTray3.Num(),
		SowelCursor2 ? TEXT("да") : TEXT("НЕТ"), SowelCursor3 ? TEXT("да") : TEXT("НЕТ"));
	return true;
}

// `DrawTextureRect(ctrl, TexturePath_Format(sowelRecord->iconPath), 0, 0, 32, 32)` —
//   у самоцвета атлас = `IconPath` целиком, берётся левый-верхний квадрат 32×32.
bool UAsdaItemTooltipSubsystem::ApplyStoneIcon(UImage* Plane, int32 SowelCode)
{
	if (!Plane || !Items() || SowelCode <= 0) { return false; }
	// ★ Волна 1 (14.09.2026, В1 + исправление E3): общая точка иконки. Раньше здесь клетка шла БЕЗ
	//   заворота; эталон рисует через тот же `planeFrame_setUV` + D3DTADDRESS_WRAP (разбор — у
	//   `AsdaUi::ApplyItemIcon`). У самоцвета клетка (0,0) — в пределах атласа заворот ничего не меняет.
	//   Откат — `asda.UI.ItemIconWrap 0`.
	if (!AsdaUi::ApplyItemIcon(GetGameInstance(), Plane, SowelCode, FVector2D(32.f, 32.f),
		/*bMale*/ true, /*bWrap*/ AsdaUi::IsItemIconWrapFixEnabled())) { return false; }
	Plane->SetVisibility(ESlateVisibility::Visible);   // иконка ловит клик (эталон: контрол кликабелен)
	return true;
}

// Курсор выбора: эталон `CControl_SetField74_75(cursor, pos(иконки))` + `SetVisible(cursor, 1)`.
//   Позиция берётся у ВЫБРАННОЙ иконки, обе живут в одном канвасе — копируем Left/Top слота.
void UAsdaItemTooltipSubsystem::PlaceStoneCursor()
{
	UWidget* Cur = bSowelWinTwo ? SowelCursor2.Get() : SowelCursor3.Get();
	const TArray<TObjectPtr<UImage>>& Tray = bSowelWinTwo ? SowelTray2 : SowelTray3;
	if (!Cur) { return; }
	const int32 I = SowelWinSel - 1;   // эталон нумерует гнёзда с 1
	if (!Tray.IsValidIndex(I) || !Tray[I])
	{
		Cur->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	UCanvasPanelSlot* CS = AsdaUi::SlotOf(Cur);
	const UCanvasPanelSlot* IS = AsdaUi::SlotOf(Tray[I]);
	if (CS && IS)
	{
		FMargin O = CS->GetOffsets();
		O.Left = IS->GetOffsets().Left;
		O.Top  = IS->GetOffsets().Top;
		CS->SetOffsets(O);
	}
	Cur->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UAsdaItemTooltipSubsystem::SelectSowelStone(int32 TrayIndex)
{
	if (!SowelSelBox) { return; }
	const int32 Max = bSowelWinTwo ? 2 : 3;
	if (TrayIndex < 1 || TrayIndex > Max) { return; }
	SowelWinSel = TrayIndex;                       // `gap_32D8[40] = 1|2|3`
	PlaceStoneCursor();
	// Эталон сразу показывает тултип камня: `CControl_SetState(&g_ItemTooltip, 1, sowelCode, …)`,
	//   но ТОЛЬКО если код попал в диапазон `g_SowelCodeRangeLo..Hi` (ui_core.c:58174/58209).
	const int32 Off = 16 + 2 * SowelWinSel;        // `gap_32D8[2*idx + 60]` = запись + 16 + 2*idx
	int32 Code = 0;
	if (SowelWinRec.IsValidIndex(Off + 1))
	{
		Code = (int16)((uint16)SowelWinRec[Off] | ((uint16)SowelWinRec[Off + 1] << 8));
	}
	UAsdaItemTableSubsystem* IT = Items();
	if (IT && IT->IsSowelCode(Code)) { ShowItemTooltipByCode(Code, EAsdaTipOwner::Item); }
}

void UAsdaItemTooltipSubsystem::ShowSowelStonesWindow(const TArray<uint8>& Rec57, int32 SlotMode)
{
	// `if (!pSowelData) return;` — эталон без записи не делает ничего.
	if (Rec57.Num() < 57) { return; }
	if (!BindSowelSelBox()) { return; }

	SowelWinRec = Rec57;          // `memcpy(&gap_32D8[44], pSowelData, 0x39)`
	GSowelBoxHover = -1;          // новое окно: наведения ещё нет
	SowelWinSel = -1;             // `*(gap_32D8 + 40) = -1`
	bSowelWinTwo = (SlotMode <= 2);

	auto Rec16 = [this](int32 Off) -> int32
	{
		return SowelWinRec.IsValidIndex(Off + 1)
			? (int32)(int16)((uint16)SowelWinRec[Off] | ((uint16)SowelWinRec[Off + 1] << 8)) : 0;
	};
	auto ShowAll = [](const TArray<TObjectPtr<UWidget>>& Ws, bool bShow)
	{
		for (const TObjectPtr<UWidget>& W : Ws)
		{
			AsdaUi::SetShownDecor(W, bShow);
		}
	};

	// ★ В .JUI обе группы лотков помечены `Visible = 0`, поэтому импортёр свернул и их, и все
	//   подложки/иконки внутри. Показывать надо ВСЮ ветку выбранного лотка (та же грабля, что
	//   с подложкой 499 в подсистеме камней).
	AsdaUi::SetShownDecor(SowelTrBack2, bSowelWinTwo);
	AsdaUi::SetShownDecor(SowelTrBack3, !bSowelWinTwo);
	ShowAll(SowelTrayBase2, bSowelWinTwo);
	ShowAll(SowelTrayBase3, !bSowelWinTwo);
	if (SowelCursor2) { SowelCursor2->SetVisibility(ESlateVisibility::Collapsed); }
	if (SowelCursor3) { SowelCursor3->SetVisibility(ESlateVisibility::Collapsed); }

	// `slotMode <= 2` → пара камней (запись +18 и +20); иначе тройка (+18, +20, +22).
	//   Гнездо 0 (+16) в этом окне НЕ показывается ни в одной ветке.
	const TArray<TObjectPtr<UImage>>& Tray = bSowelWinTwo ? SowelTray2 : SowelTray3;
	const int32 N = bSowelWinTwo ? 2 : 3;
	for (int32 i = 0; i < N; ++i)
	{
		if (!Tray.IsValidIndex(i) || !Tray[i]) { continue; }
		if (!ApplyStoneIcon(Tray[i], Rec16(18 + 2 * i)))
		{
			// Эталон при `!sowelRecord` просто НЕ рисует — план остаётся пустым (подложка видна).
			Tray[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	// ★ Д8 (отступление). Эталон центрирует 1530 ОДИН раз в инициализации и считает центр по
	//   размеру ДРУГОГО окна — однокнопочного msgbox’а (`pOneBtnMsgBox`, ui_core.c:55953-55959),
	//   поэтому в оригинале коробка стоит чуть мимо центра. Однокнопочного msgbox’а как отдельного
	//   окна в порту нет, воспроизводить чужой размер не по чему — центрируем по собственному.
	//   Флага отката нет: «мимо центра на неизвестную дельту» невоспроизводимо.
	AsdaSetSelBoxVisible(GetGameInstance(), SowelSelBox, true);
	UE_LOG(LogAsdaItemTip, Log,
		TEXT("[камни] SOWELL_SELBOX показан: режим %d (лоток на %d), камни %d/%d/%d"),
		SlotMode, N, Rec16(18), Rec16(20), bSowelWinTwo ? 0 : Rec16(22));
}

void UAsdaItemTooltipSubsystem::HideSowelStonesWindow()
{
	// Эталон `ui_HideControlsAndRefresh@0xD6E0E0` (ui_core.c:56279): гасит оба лотка, оба курсора
	//   и само окно, затем зовёт перерисовку менеджера окон.
	if (SowelTrBack2) { SowelTrBack2->SetVisibility(ESlateVisibility::Collapsed); }
	if (SowelTrBack3) { SowelTrBack3->SetVisibility(ESlateVisibility::Collapsed); }
	if (SowelCursor2) { SowelCursor2->SetVisibility(ESlateVisibility::Collapsed); }
	if (SowelCursor3) { SowelCursor3->SetVisibility(ESlateVisibility::Collapsed); }
	AsdaSetSelBoxVisible(GetGameInstance(), SowelSelBox, false);
	SowelWinSel = -1;
	if (GSowelBoxHover > 0) { AsdaTipOverModal(GetGameInstance(), 0); }
	GSowelBoxHover = -1;          // окно закрыто: наведения больше нет
	HideTooltipFor(EAsdaTipOwner::Item);
}

bool UAsdaItemTooltipSubsystem::IsSowelStonesWindowShown() const
{
	return AsdaUi::IsShown(SowelSelBox);   // ★ волна В6 (МЕЛК-8): общая точка видимости узла
}

// Звук кнопки окна 1530 — общей точкой звуков окон-сообщений (`UAsdaWindowSubsystem::PlayMsgBoxSfx`), id 4.
void UAsdaItemTooltipSubsystem::PlaySowelSelBoxSfx(const TCHAR* Why)
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->PlayMsgBoxSfx(4, Why);
	}
}

void UAsdaItemTooltipSubsystem::OnSowelSelBoxOk()
{
	// Эталон (ui_core.c:57886): звук 4 → `net_sendItemUseRequest(…, запись, гнездо)` → скрыть окно.
	//   Пакет собирает владелец состояния камней (см. шапку .h) — отдаём ему номер гнезда.
	// ★ Волна В6 ремесла (ревизия В5 СРЕДН-2): звук 4 — ОБЩЕЙ точкой звуков окон-сообщений, первой строкой, как эталон.
	PlaySowelSelBoxSfx(TEXT("SOWELL_SELBOX «ОК» 1532 (ui_core.c:57887)"));
	const int32 Socket = SowelWinSel;
	if (Socket < 1)
	{
		UE_LOG(LogAsdaItemTip, Log, TEXT("[камни] OK без выбранного гнезда — эталон шлёт гнездо −1, пакет не собирается"));
		HideSowelStonesWindow();
		return;
	}
	OnSowelSelBoxAccepted.Broadcast(Socket);
	HideSowelStonesWindow();
}

void UAsdaItemTooltipSubsystem::OnSowelSelBoxCancel()
{
	// Эталон (ui_core.c:57891): `net_sendItemUseRequest(g_pInventoryWnd, 0, -1)` — с нулевым
	//   указателем записи вся отправка пропускается (`if (itemSlotPtr)`), то есть НИЧЕГО не шлётся.
	// ★ Волна В6 (СРЕДН-2): звук 4 (ui_core.c:57892). ESC окна приходит сюда же — у эталона ESC 1530 зовёт
	//   `OnSendItemUpgrade(mgr, 1533)`, то есть тот же звук.
	PlaySowelSelBoxSfx(TEXT("SOWELL_SELBOX «Отмена» 1533 (ui_core.c:57892)"));
	HideSowelStonesWindow();
}

// Курсор внутри прямоугольника виджета.
// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget` — одна точка
//   хит-теста на все подсистемы. Метод оставлен тонким форвардом.
bool UAsdaItemTooltipSubsystem::IsCursorOverWidget(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

// Покадровый опрос лотка: наведение показывает тултип камня, нажатие ЛКМ выбирает гнездо —
//   в эталоне то же самое делает `uiCtrlOnDragCommand` по сообщениям контрола.
void UAsdaItemTooltipSubsystem::TickSowelSelBox()
{
	// ★ Аудит №3 (B3): опрос мыши и латч — общей структурой `FAsdaMouseFrame`. Это ОДИННАДЦАТЫЙ латч
	//   того же вида (в самом аудите его не было — нашёлся при сведении остальных десяти). Ручной
	//   сброс при закрытом окне сохранён дословно: без него удержанная с прошлого кадра кнопка не
	//   дала бы фронта и первый клик по лотку терялся бы.
	if (!IsSowelStonesWindowShown()) { SowelMouse.Reset(); return; }
	if (!FSlateApplication::IsInitialized()) { return; }
	SowelMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлку кадра сбрасываем: иначе нажатие ДО нотиса и отпускание ПОСЛЕ его закрытия
	//   сошлись бы в выбор камня в лотке, которого игрок не делал. Своих защёлок у лотка нет —
	//   выбор происходит прямо на фронте нажатия.
	// ★★★ [ИСПРАВЛЕНИЕ] 05.11.2026 (баг игроков «зависает при замене Sowel»): окно 1530 САМО модальное (z-полоса модалок,
	//   эталон `helper_setByteField552(pSowelSelBox, 1)`), и гейт «открыта модалка» глушил опрос ЕГО ЖЕ лотка: щелчки по
	//   камням окна были мертвы, гнездо не выбиралось, «OK» закрывал окно без 5024, а мир и прочие окна под модалкой заперты —
	//   игрок видел «зависший» клиент. У эталона захваченный модальный контрол получает мышь сам (ui_core.c:104149-104153),
	//   поэтому лоток глушим только под ЧУЖОЙ модалкой поверх 1530. Ключ `asda.Sowel.SelBoxOwnModal` (0 = прежнее).
	const UAsdaWindowSubsystem* SelWM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bSelBoxIsTopModal = CVarSowelSelBoxOwnModal.GetValueOnGameThread() != 0
		&& SelWM && SowelSelBox && SelWM->GetTopModalWindow() == SowelSelBox;
	if (!bSelBoxIsTopModal && AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		SowelMouse.Reset();
		if (GSowelBoxHover > 0) { HideTooltipFor(EAsdaTipOwner::Item); AsdaTipOverModal(GetGameInstance(), 0); }
		GSowelBoxHover = -1;
		return;
	}

	const TArray<TObjectPtr<UImage>>& Tray = bSowelWinTwo ? SowelTray2 : SowelTray3;
	const int32 N = bSowelWinTwo ? 2 : 3;
	// ★★★ [ОРИГИНАЛ] 07.11.2026: наведение на камень окна — `uiControlSetStateByMsg@0xD78710` (ui_core.c:58217, сообщение 8 на
	//   1540…1544, вызов :58506): подсказка камня гнезда (запись +18/+20/+22); сообщение 0xA (курсор ушёл) — `CControl_KillFocus`.
	//   Прежде порт показывал её только по щелчку. Ключ `asda.Sowel.HoverTip`.
	if (CVarSowelHoverTip.GetValueOnGameThread() != 0)
	{
		int32 Hover = -1;
		for (int32 i = 0; i < N; ++i)
		{
			if (Tray.IsValidIndex(i) && AsdaUi::IsShown(Tray[i]) && IsCursorOverWidget(Tray[i])) { Hover = i + 1; break; }
		}
		if (Hover != GSowelBoxHover)
		{
			if (GSowelBoxHover > 0) { HideTooltipFor(EAsdaTipOwner::Item); AsdaTipOverModal(GetGameInstance(), 0); }
			GSowelBoxHover = Hover;
			const int32 Off = 16 + 2 * Hover;   // `gap_32D8[2*idx + 60]` = запись + 16 + 2*idx (как в `SelectSowelStone`)
			if (Hover > 0 && SowelWinRec.IsValidIndex(Off + 1))
			{
				const int32 Code = (int16)((uint16)SowelWinRec[Off] | ((uint16)SowelWinRec[Off + 1] << 8));
				UAsdaItemTableSubsystem* HoverIT = Items();
				if (HoverIT && HoverIT->IsSowelCode(Code))
				{
					ShowItemTooltipByCode(Code, EAsdaTipOwner::Item);
					if (SelWM) { AsdaTipOverModal(GetGameInstance(), SelWM->GetWindowZOrder(kSowelSelBoxId)); }
				}
			}
		}
	}
	if (!SowelMouse.bLmbDown) { return; }

	for (int32 i = 0; i < N; ++i)
	{
		if (Tray.IsValidIndex(i) && IsCursorOverWidget(Tray[i]))
		{
			SelectSowelStone(i + 1);
			return;
		}
	}
}

// ═══════════════════ мост в инвентарь ═══════════════════
bool UAsdaItemTooltipSubsystem::RebindInfoPanelIfWindowChanged()
{
	if (!bBridgeBound) { return false; }   // первая привязка — `TryBindInventoryBridge`
	if (CVarLegacyNoInfoRebind.GetValueOnGameThread() != 0) { return false; }   // контроль самотеста: прежний дефект
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	UUserWidget* Live = Inv ? Inv->GetWindowWidget() : nullptr;
	if (!Live || Live == InvenWindow.Get()) { return false; }
	UE_LOG(LogAsdaItemTip, Log, TEXT("[инфо-панель] окно инвентаря пересоздано (%s → %s) — перепривязка"),
		InvenWindow ? *InvenWindow->GetName() : TEXT("нет"), *Live->GetName());
	BindInfoPanel(Live);
	return true;
}

void UAsdaItemTooltipSubsystem::TryBindInventoryBridge()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	if (!Inv) { return; }

	FAsdaItemTooltipBridge B;
	TWeakObjectPtr<UAsdaItemTooltipSubsystem> Self(this);
	TWeakObjectPtr<UAsdaInventorySubsystem>   WInv(Inv);

	B.ShowItemTooltip = [Self, WInv](int32 Container, int32 Cell, FVector2D /*ScreenPos*/)
	{
		if (!Self.IsValid() || !WInv.IsValid()) { return; }
		const FAsdaItemInstance* It = WInv->GetCell(Container, Cell);
		if (!It || It->IsEmpty()) { return; }
		// ★★★ Д5 (КРУГ 5, доработка). ЦЕПОЧКА «КУРСОР → ЯЧЕЙКА → КОД» — В ЛОГ, ОДНОЙ СТРОКОЙ.
		//   Жалоба «навёл на одно, а описание другого» проверяется только так: инвентарь считает
		//   ячейку своим `HitTest`, мы — НЕЗАВИСИМО по планам `INVEN_SET{N}_{idx}_0`, и если два
		//   счёта разошлись, это печатается как Warning (класс бага уже был: курсор выбора 409
		//   жил в другом канвасе и рисовался не в той ячейке).
		{
			const int32 VisN = Self->HitTestGridCellVisual();
			const int32 Page = WInv->GetScrollRow();
			const int32 AbsFromVisual = (VisN >= 0) ? (VisN + UAsdaInventorySubsystem::kGridCols * Page) : -1;   // ★ 29.10.2026: 12 колонок
			const FVector2D Cur = FSlateApplication::IsInitialized()
				? FSlateApplication::Get().GetCursorPos() : FVector2D::ZeroVector;
			UE_LOG(LogAsdaItemTip, Log,
				TEXT("[наведение] курсор (%.0f,%.0f) → вкладка %d стр.%d · контейнер %d ячейка %d · код=%d кол-во=%d | контроль: план INVEN_SET%d ⇒ ячейка %d"),
				Cur.X, Cur.Y, WInv->GetTab(), Page, Container, Cell, It->ItemCode, It->Amount,
				VisN, AbsFromVisual);
			if (Container != 3 && VisN >= 0 && AbsFromVisual != Cell)
			{
				UE_LOG(LogAsdaItemTip, Warning,
					TEXT("[наведение] ★ РАСХОЖДЕНИЕ ПОПАДАНИЯ: инвентарь дал ячейку %d, а план под курсором — %d (стр. %d). Подсказка строится НЕ для той ячейки."),
					Cell, AbsFromVisual, Page);
			}
		}
		// ★ Для НАДЕТОГО предмета эталон гасит сравнение (`equipSlotUpdateTooltip`: suppressCompare = 1).
		// ★★★ Container = эталонный `slotSource`: 1 «Снаряжение» (`g_InventoryBag`),
		//   2 «Расходники» (`g_InventoryArray2`), 3 надетое. От него зависит ПАЙПЛАЙН тултипа —
		//   см. развилку в `ShowItemTooltip`.
		Self->ShowItemTooltip(*It, /*bSuppressCompare=*/Container == 3, EAsdaTipOwner::Item, Container);
	};
	// ★ Гасим ТОЛЬКО свой тултип: инвентарь опрашивает курсор каждый кадр, и безусловный Hide
	//   отсюда сносил бы подсказку, которую в этот момент держит другая подсистема.
	B.HideItemTooltip = [Self]() { if (Self.IsValid()) { Self->HideTooltipFor(EAsdaTipOwner::Item); } };
	B.FillInfoPanel = [Self, WInv](int32 Container, int32 Cell)
	{
		if (!Self.IsValid() || !WInv.IsValid()) { return; }
		// ★★★ ДЕФЕКТ 1: ПРАВУЮ ИНФО-ПАНЕЛЬ НАПОЛНЯЕТ НЕ ЛЮБАЯ ВКЛАДКА.
		//   `CInventoryUI_OnSlotClick@0xD01F50` (item_inventory.c:36266) на клике по ячейке сетки
		//   расходится по текущей вкладке (`CInventoryWindow` + `gap_1B0[84]`):
		//     • вкладка «Снаряжение» (флаг != 0, массив `g_InventoryBag`, 30 слотов) — ветка
		//       36268-36343, и ТОЛЬКО она зовёт `ItemTooltip_PopulateInfoPanel(this, 1, -1)`
		//       (строки 36278 и 36335);
		//     • вкладка «Расходники» (флаг == 0, массив `g_InventoryArray2`, 60 слотов) — ветка
		//       36356-36394: выставляет `m_nSelEquipSlot`/`m_nSelEquipCol`, курсор выбора,
		//       снимок записи — и ВСЁ. Ни `PopulateInfoPanel`, ни `uiWindowResetAllFields` там
		//       НЕТ, то есть панель ОСТАЁТСЯ с прежним содержимым.
		//   Единственный другой вход в панель — надетый слот (`slotSource == 3`, ui_core.c:33972).
		//   Порт звал `FillInfoPanel` для ЛЮБОГО контейнера, поэтому клик по расходнику затирал
		//   витрину последнего выбранного снаряжения. Гейт возвращает эталонное поведение.
		//   ⚠ Ветку `special_itemlist` (П15) это НЕ отменяет: эталон её ДЕЙСТВИТЕЛЬНО заполняет,
		//   но только когда спец-предмет лежит в контейнере 1 (`g_InventoryBag`).
		if (Container != 1 && Container != 3
			&& CVarLegacyInfoPanelAnyTab.GetValueOnGameThread() == 0)
		{
			UE_LOG(LogAsdaItemTip, Verbose,
				TEXT("[инфо-панель] контейнер %d (вкладка «Расходники») — эталон панель не трогает, оставляем прежнюю"),
				Container);
			return;
		}
		// ★ 21.10.2026: окно инвентаря могли пересоздать в этом же кадре — пишем в ЖИВОЕ окно, не в прежнее.
		Self->RebindInfoPanelIfWindowChanged();
		const FAsdaItemInstance* It = WInv->GetCell(Container, Cell);
		// ★ Д1 (КРУГ 6): клик по ПУСТОЙ ячейке (и по пустому надетому слоту) — панель гасим и
		//   сворачиваем. Эталон здесь тоже сбрасывает поля (item_inventory.c:36345-36353), но окно
		//   оставляет раскрытым; свёртка — требование пользователя.
		if (!It || It->IsEmpty()) { Self->ClearInfoPanel(); return; }
		Self->FillInfoPanel(*It, Container == 3 ? 3 : 1);
		// ★ Д1 (КРУГ 6): источник запоминаем ПОСЛЕ наполнения — внутри есть ветки с `ClearInfoPanel`.
		Self->NoteInfoPanelSource(Container, Cell);
	};
	// ★ 21.10.2026: сперва — к живому окну (оно могло смениться в этом же кадре), потом очистка.
	B.ClearInfoPanel = [Self]() { if (Self.IsValid()) { Self->RebindInfoPanelIfWindowChanged(); Self->ClearInfoPanel(); } };
	// ★ Д1 (КРУГ 6): пропажа показанного предмета по пакету. Подписка ОДНА на всё время жизни
	//   подсистемы (`TryBindInventoryBridge` зовётся каждый кадр, пока окно не создано).
	if (!CellWatchHandle.IsValid())
	{
		CellWatchHandle = Inv->OnCellChanged.AddUObject(this, &UAsdaItemTooltipSubsystem::OnInventoryCellChanged);
		WatchedInv = Inv;
	}
	// Спец-подсказка служебного контрола (эталон `tutorialPopupLayoutSmall`: заголовок + тело).
	B.ShowHintTooltip = [Self](int32 TitleLoc, int32 BodyLoc)
	{
		if (!Self.IsValid()) { return; }
		FAsdaTipMetrics M;
		M.WidthAdd   = 27.f;    // «маленькая» всплывашка эталона
		M.ExtraLines = 0;
		Self->ShowTextTip(EAsdaTipOwner::Item, Self->LocRaw(TitleLoc), Self->LocRaw(BodyLoc), M);
	};
	Inv->SetTooltipBridge(B);

	// Инфо-панель живёт в самом окне инвентаря — привязываемся, как только окно создано.
	if (UUserWidget* W = Inv->GetWindowWidget())
	{
		BindInfoPanel(W);
		bBridgeBound = true;
		UE_LOG(LogAsdaItemTip, Log, TEXT("[тултип] мост в инвентарь зарегистрирован (тултип + инфо-панель)"));
	}
}
