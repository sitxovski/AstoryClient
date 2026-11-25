// Copyright (c) 2026 @sitxovski (Discord)
// AsdaHotbarSubsystem_Pointer.cpp — ★ R2 10.11.2026 (разрез AsdaHotbarSubsystem.cpp, дословный перенос): курсор/перетаскивание/подсказка, иконки-уведомления.
//   Общее для частей — в AsdaHotbarSubsystemInternal.h; шапка и реверс — в AsdaHotbarSubsystem.cpp.
#include "UI/AsdaHotbarSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaSoulmateSubsystem.h"    // ★ 06.11.2026: пара и «Друг в игре» — из карточки (`asda.Soul.SlotTruth`)
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ 06.11.2026: перерисовка лотков действий при смене пары
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaItemCountStyle.h"   // ★ 26.10.2026: обводка числа стопки — общая точка (ключ asda.UI.ItemCountOutline)
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaBytes.h"   // D4: единые читалки сырой записи (little-endian, DataHex, ASCII-поле)
#include "UI/AsdaUiHelpers.h"   // ★ D2/D4: единые UI-хелперы (дедуп 2026-08-08)

#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaMiniStateSubsystem.h"        // один общий тултип на HUD (эталон держит один g_ItemTooltip)
#include "UI/AsdaItemTooltipSubsystem.h"      // владелец общей плашки (порт g_ItemTooltip)
// ★ ТОЛЬКО ЗАГОЛОВОК, поведение не меняется: ниже (мост к окну навыков) уже используется
//   `UAsdaSkillWindowSubsystem`, но #include отсутствовал — тип «доезжал» через unity-сборку.
//   Как только этот файл попадает в отдельную единицу трансляции, компиляция падала.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"     // сводная сверка шторок отката (`asda.Cooldown.Dump`)
#include "UI/AsdaBoosterPanelSubsystem.h"  // ★ гейт `CObject_GetField254(g_pInventoryWnd)` = ОКНО ГАЧИ 10050
#include "UI/AsdaChatWindowSubsystem.h"    // ★ гейт `Input_IsTextInputFocused(g_pChatInputWnd)`
#include "UI/AsdaSkillDatabase.h"          // ★ Аудит №3 (A13): имя иконки навыка — только отсюда
#include "UI/AsdaAnimatedPlane.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — заголовки подсказок по id
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaKeybindConfig.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Engine.h"   // ★ 17.10.2026 GEngine->Exec(quit) в самотесте иконок эмоций
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ObjectKey.h"       // ★ 26.10.2026: TObjectKey — ревизия списка навыков по экземпляру панели
#include "UI/AsdaCraftSubsystem.h"   // ★ волна В3 ремесла: единый предикат «окно ремесла открыто» (`IsAnyCraftWindowOpen`)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: окно обмена 2008 открыто — гейт слотов и значков заявки 4/5
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "World/AsdaFishingSubsystem.h"     // ★ 08.10.2026 (рыбалка В4): гейт «идёт рыбалка» в `IsActivationBlocked`
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaHelpTopics.h"             // ★ 30.10.2026: справочные темы кнопок листания и пустого слота оружия
#include "UI/AsdaDeathInputLock.h"          // ★ 05.11.2026: меню смерти запирает слоты (эталон CUIManager+0x44, ui_core.c:67351)
#include "AsdaHotbarSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaHotbarSubsystem*.cpp

// ★★★ [ИСПРАВЛЕНИЕ 30.10.2026] Баг игроков «подсказка Title menu зависает над миром». Причина: у слота пункта доп. меню
//   (тип 5) плашку показывает мост доп. меню ОТ ИМЕНИ ХОТБАРА (`EAsdaTipOwner::Hotbar`), а хотбар записывал себе владельца
//   `Item` и при уходе курсора гасил `Item` — общая плашка такой запрос отвергает (закон `ui-shared-tip-ownership`), и
//   подсказка ехала за курсором по миру, пока её не перебивал чужой показ. Заодно — эталонное событие 0xA
//   (`hotbarOnUiEvent@0xDF28E0` case 0xA → `CControl_KillFocus(&g_ItemTooltip)`): уход с полного слота на пустой и уход
//   курсора на чужое окно поверх панели гасят подсказку (порт держал её до выхода курсора с панели).
static ASDA_DEV_CVAR(int32, CVarQuickTipHideFix,
	TEXT("asda.Quick.TipHideFix"),
	1,
	TEXT("1 = подсказка слота панели быстрого доступа гаснет, как только курсор ушёл со слота (владелец плашки пункта "
	     "доп. меню — хотбар; уход на пустой слот и на чужое окно поверх панели гасят её, как событие 0xA оригинала); "
	     "0 = прежнее (подсказка пункта доп. меню зависала и ехала за курсором)."),
	ECVF_Default);
// ★★★ [ОРИГИНАЛ 30.10.2026] Справочные подсказки панели (`hotbarOnUiEvent@0xDF28E0` case 8): кнопки листания страниц
//   73327/73328 — тема 1 «Кнопка 'Быстрый выбор'», пустой слот оружия (колонка 12, контрол 73023) — тема 0 «Слот оружия»
//   (`uiSlotHoverTooltip@0xDED810`). Тексты — `AsdaHelpTopics`; гашение — как у слотов (событие 0xA, владелец Hotbar).
static ASDA_DEV_CVAR(int32, CVarQuickHelpTips,
	TEXT("asda.Quick.HelpTips"),
	1,
	TEXT("1 = как в оригинале: подсказки кнопок листания страниц панели быстрого доступа и пустого слота оружия; "
	     "0 = прежний порт (подсказок нет)."),
	ECVF_Default);

// ── Тип уведомления → имя иконки 64×64 (эталон `MsgNotify_TypeToIcon@0xDF4450`) ──
static const TCHAR* NoticeTypeToIcon(int32 Type)
{
	switch (Type)
	{
	case 0: case 8: return TEXT("act_invite");        // приглашение в пати
	case 1:         return TEXT("mes_couple");        // соулмейт
	case 2:         return TEXT("mes_friend");        // друг
	case 3:         return TEXT("mes_present");       // подарок
	case 4:         return TEXT("act_itemchange");    // обмен предметами
	case 5:         return TEXT("act_itemchange2");
	// ★ ДОБИТА ТАБЛИЦА ЭТАЛОНА (`MsgNotify_TypeToIcon@0xDF4450`, разбор — `Reports/
	//   elite_mailbox_re.md` §1.4). Раньше строки 6/9…15 отсутствовали, и `AddNoticeIcon` по ним
	//   молча выходил с предупреждением. Ни одна строка не выдумана — все текстуры лежат в
	//   `Content/AsdaAssets/Textures/JUI` (проверено наличием ассетов).
	//   ⚠ Типа 7 в таблице эталона НЕТ, тип 16 есть в `statusBarAddIcon`, но ветки в таблице нет —
	//   так и оставляем (закон `never-invent`): для них по-прежнему вернётся nullptr.
	case 6:         return TEXT("mes_clan");
	case 9:         return TEXT("mes_tag1");
	case 10:        return TEXT("mes_event_gift");
	case 11:        return TEXT("mes_whisper");
	case 12:        return TEXT("mes_jobchange");
	case 13:        return TEXT("mes_clan");
	case 14:        return TEXT("mes_dual_64");
	// ★★★ 15 — ЭЛИТНЫЙ ПОЧТОВЫЙ ЯЩИК (окно 69000). `statusBarAddIcon(g_pQuickSlotBar, 15)` зовут
	//   `netParseLocalSlotSync@0xEF0990` (непустой 11005 при закрытом окне ящика) и
	//   `netParseItemAcquireNotice@0xEF0720` (6226 type 101).
	case 15:        return TEXT("mes_premium_postbox");
	default:        return nullptr;
	}
}

// ══════════════════════════════ ВВОД (события 5/6/7/8/9/0xA/0xD) ═══════════════════════════════
// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget` — одна точка
//   хит-теста на все подсистемы. Метод оставлен тонким форвардом.
bool UAsdaHotbarSubsystem::IsCursorOver(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

int32 UAsdaHotbarSubsystem::HitTestSlot(bool& bOutTRow, int32& OutColumn) const
{
	for (int32 C = 0; C < MainUi.Num(); ++C)
	{
		if (IsCursorOver(MainUi[C].Icon)) { bOutTRow = false; OutColumn = C; return AbsSlot(C, false); }
	}
	if (bSecondRowVisible)
	{
		for (int32 C = 0; C < TUi.Num(); ++C)
		{
			if (IsCursorOver(TUi[C].Icon)) { bOutTRow = true; OutColumn = C; return AbsSlot(C, true); }
		}
	}
	bOutTRow = false; OutColumn = -1;
	return -1;
}

// Эталон, событие 5: захват слота-источника (только если слот НЕ пуст), + опция «нужен Shift».
void UAsdaHotbarSubsystem::BeginDrag(int32 AbsIndex)
{
	if (!Slots.IsValidIndex(AbsIndex) || Slots[AbsIndex].IsEmpty()) { return; }
	DragSrcSlot = AbsIndex;
	bDragActive = true;
	HideSlotTooltip();
	// Эталон `uiTabRefreshSlotText@0xDEF490`: сохранить строку счётчика, спрятать COOL/CEND,
	//   поставить иконке «призрачный» режим заливки (FillMode 5), очистить текст.
	TArray<FAsdaHotbarSlotUi>& Row = bDragSrcTRow ? TUi : MainUi;
	if (Row.IsValidIndex(DragSrcColumn))
	{
		FAsdaHotbarSlotUi& U = Row[DragSrcColumn];
		if (U.StackText)
		{
			DragSavedStackText = U.StackText->GetText().ToString();
			U.StackText->SetText(FText::GetEmpty());
		}
		SetVis(U.Cool.Plane, false);
		SetVis(U.Flash, false);
		// ★★★ СБРОС КЭША ПОКАЗА. Без него `UpdateCooldownVisual` на следующем кадре считает
		//   `bChanged == false` (слот/состояние/срок не менялись) и шторку обратно НЕ поднимает —
		//   слот, за который просто дёрнули мышью, теряет свою перезарядку до конца отката, тогда
		//   как соседние её сохраняют. Это самостоятельный источник впечатления «откат встал не на
		//   тот навык». Обнуляем — следующий кадр перезапустит шторку с корректным остатком.
		//   ⚠ Сам план (`U.Cool.Plane`) сбрасывать НЕЛЬЗЯ — он родной контрол окна: чистим только кэш.
		U.Cool.ShownKey = -1; U.Cool.ShownState = 0; U.Cool.ShownEndS = 0.0; U.FlashEndS = 0.0;
		// Исходный слот приглушаем (эталон гасит его совсем через FillMode 5 «рисовать у курсора»).
		if (U.Icon) { U.Icon->SetColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, DragSourceOpacity)); }

		// ★ ПРИЗРАК: летящая за курсором копия ТОЙ ЖЕ иконки с alpha 40 %. В оригинале это делает
		//   движок (плоскость с FillMode 5 рисуется у курсора); у нас — явный UImage в корневом канвасе.
		//   ★ Аудит №3 (B4): создание/раскладка/показ — в общей точке `FAsdaDragGhost`.
		//   Бренд = тот же браш, что сейчас показан в слоте (предмет/эмоция/навык — без разницы);
		//   размер = размер иконки в ячейке (32×32).
		if (U.Icon && QuickWidget && QuickWidget->WidgetTree)
		{
			if (UCanvasPanel* Root = Cast<UCanvasPanel>(QuickWidget->WidgetTree->RootWidget))
			{
				DragGhost.Begin(QuickWidget, Root, U.Icon->GetBrush(),
					U.IconBaseSize.X > 0.f ? U.IconBaseSize : FVector2D(32.f, 32.f),
					DragGhostOpacity, DragGhostCursorOffset, TEXT("QUICK_DragGhost"),
					GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
			}
		}
	}
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] захват слота %d для перетаскивания (призрак alpha %.0f%%)"),
		AbsIndex, DragGhostOpacity * 100.f);
}

// Двигать призрак к курсору. Координаты курсора → пространство КАНВАСА окна с учётом DPI и позиции
//   окна игры на десктопе (закон ui-window-drag-hittest).
//   ★ D2 (дедуп UI 2026-08-08): перевод вынесен в AsdaUi::ScreenToCanvasLocal (та же точка, что зовёт
//   UAsdaChatWindowSubsystem::ScreenToWindowBase). Раньше формула была скопирована сюда дословно.
//   ★ Аудит №3 (B4): и само тело слежения теперь общее (`FAsdaDragGhost::Tick`) — до правки оно
//   существовало ТРЕМЯ экземплярами, два из которых совпадали ПОБАЙТОВО.
void UAsdaHotbarSubsystem::UpdateDragGhost()
{
	if (!bDragActive || !QuickWidget || !QuickWidget->WidgetTree) { return; }
	DragGhost.Tick(GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr);
}

void UAsdaHotbarSubsystem::CancelDragVisual()
{
	// Эталон `uiTabRefreshFillMode@0xDEF670` / `uiTabSetFillModeAndText@0xDEF720`.
	TArray<FAsdaHotbarSlotUi>& Row = bDragSrcTRow ? TUi : MainUi;
	if (Row.IsValidIndex(DragSrcColumn))
	{
		FAsdaHotbarSlotUi& U = Row[DragSrcColumn];
		if (U.Icon) { U.Icon->SetColorAndOpacity(FLinearColor::White); }
		if (U.StackText && !DragSavedStackText.IsEmpty()) { U.StackText->SetText(FText::FromString(DragSavedStackText)); }
	}
	DragGhost.Cancel();         // призрак убрать (★ Аудит №3 (B4): гашение — в общей точке)
	DragSavedStackText.Reset();
	DragSrcSlot = -1;
	DragSrcColumn = -1;
	bDragActive = false;
}

// Эталон, событие 7 → `hotbarWnd_HandleSlotClick@0xDEEEC0`; цель == nullptr → снятие содержимого.
void UAsdaHotbarSubsystem::EndDrag(int32 AbsTarget, bool bTargetTRow, int32 TargetColumn)
{
	const int32 Src = DragSrcSlot;
	CancelDragVisual();
	if (Src < 0) { return; }
	if (AbsTarget < 0)
	{
		// ★★★ ТОЧНОЕ УСЛОВИЕ ЭТАЛОНА. `hotbarWnd_HandleSlotClick(this, a2)` чистит слот ТОЛЬКО при
		//   `a2 == nullptr`, то есть когда под курсором НЕТ НИ ОДНОГО контрола интерфейса. Если там
		//   лежит любой другой контрол (окно инвентаря, чат, фон самой панели) — идёт `LABEL_3`:
		//   `uiTabRefreshFillMode(this)`, слот остаётся на месте. Раньше мы чистили слот при любом
		//   промахе мимо ячейки панели, и предмет пропадал из панели при броске НА ИНВЕНТАРЬ —
		//   а это ровно тот жест, которым игрок «возвращает» предмет в сумку.
		//   Аналог `a2 != nullptr` у нас — общий хит-тест интерфейса `IsCursorOverAnyUiWindow`
		//   (он же учитывает зоны всплывающих слоёв).
		//   ⚠ ПОРЯДОК ВАЖЕН: «призрак» перетаскивания — UImage в канвасе окна панели, висящий прямо
		//   под курсором. `CancelDragVisual()` выше уже спрятал его, иначе хит-тест интерфейса был бы
		//   истинным всегда и слот не снимался бы НИКОГДА.
		UGameInstance* GI = GetGameInstance();
		UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		FName OverZone = NAME_None;
		if (WM && WM->IsCursorOverAnyUiWindow(OverZone))
		{
			UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] слот %d отпущен над интерфейсом «%s» — эталон здесь "
				"НИЧЕГО не делает (снятие = бросок в пустоту)"), Src, *OverZone.ToString());
			RefreshPage();         // эталонный LABEL_3: uiTabRefreshFillMode
			return;
		}
		ClearSlot(Src);            // перетащили НАРУЖУ = снять (ПКМ-обработчика у хотбара нет)
		return;
	}
	if (AbsTarget == Src) { RefreshPage(); return; }
	SwapSlots(Src, AbsTarget);
}

void UAsdaHotbarSubsystem::ShowSlotTooltip(int32 AbsIndex)
{
	if (!Slots.IsValidIndex(AbsIndex)) { return; }
	const FAsdaHotbarSlot& S = Slots[AbsIndex];
	UGameInstance* GI = GetGameInstance();
	UAsdaMiniStateSubsystem* Mini = GI ? GI->GetSubsystem<UAsdaMiniStateSubsystem>() : nullptr;
	if (!Mini) { return; }

	FString Title, Body;
	// ★ Кто станет владельцем ОБЩЕЙ плашки: у предмета показ уходит в инвентарь → владелец `Item`,
	//   у навыка/эмоции/пункта меню плашку берём сами → владелец `Hotbar`. Гасить надо ТОГО ЖЕ
	//   владельца, иначе подсказка зависнет на экране (см. шапку `EAsdaTipOwner`).
	switch ((EAsdaHotbarSlotType)S.Type)
	{
	case EAsdaHotbarSlotType::Item:
		if (Inventory.ShowTooltip)
		{
			Inventory.ShowTooltip((int32)S.SubType, S.Index);
			bTipShown = true; TipOwner = EAsdaTipOwner::Item; return;
		}
		// Инвентаря нет — показываем то, что ЕСТЬ в данных: имя предмета из БД по кэшированному коду.
		if (UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>())
		{
			Title = IT->GetItemName(S.Id);
		}
		// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Предмет %d» = 990463, «Ячейка %d контейнера %d, в стеке: %d» = 990464.
		if (Title.IsEmpty()) { Title = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990463), { FString::FromInt(S.Id) }); }
		Body = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990464), { FString::FromInt(S.Index), FString::FromInt((int32)S.SubType), FString::FromInt(S.Count) });
		break;
	case EAsdaHotbarSlotType::Emotion:
		// Эталон case 2: `CControl_SetState(&g_ItemTooltip, 3, code, 0, 0, -1, 0)` → одна СТРОКА из
		//   записи `EmotionTip.BIN`. Заголовка у неё нет — весь текст идёт телом.
		if (!ResolveEmotionTip(S.Index, Body))
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] подсказка эмоции %d: записи в emotiontip нет ")
				TEXT("(эталон в этом случае подсказку не показывает вовсе)"), S.Index);
			return;                                    // эталон: `if (rec) tooltipShowAtCursor(...)`
		}
		break;
	case EAsdaHotbarSlotType::Skill:
	case EAsdaHotbarSlotType::SkillAuto:
		// ★★★ ИСПРАВЛЕНО 2026-08-11. Раньше здесь звался `Skills.ShowTooltip`, который показывал
		//   плашку ОТ ИМЕНИ ОКНА НАВЫКОВ (владелец `SkillWin`) и работал только при ОТКРЫТОМ окне —
		//   а хотбар записывал себе владельца `Item`. В итоге: при закрытой книге навыков подсказки
		//   не было вовсе, при открытой — её гасил тик окна на следующем кадре
		//   (закон ui-shared-tip-ownership). Теперь мост только СТРОИТ текст, а показывает и гасит
		//   его хотбар от своего имени (`EAsdaTipOwner::Hotbar`) — ровно как эталонный
		//   `skillShowTooltipForId@0xE54120` / `SkillTooltip_BuildSoulmateTooltip@0xEAF450`,
		//   которые ищут запись ПО КОДУ и о сетке окна ничего не знают.
		if (!Skills.BuildTooltip || !Skills.BuildTooltip(S.Id, S.Type == (uint8)EAsdaHotbarSlotType::SkillAuto, Title, Body))
		{
			UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] подсказка навыка %d (тип слота %d): мост книги ")
				TEXT("навыков %s — показываю только код"), S.Id, (int32)S.Type,
				Skills.BuildTooltip ? TEXT("записи не нашёл") : TEXT("не зарегистрирован"));
			Title = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990465), { FString::FromInt(S.Id) });   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Навык %d»
		}
		break;
	case EAsdaHotbarSlotType::ExtraMenu:
		// Эталон case 5: `tutorialShowItemNoticePopup@0xECD760` — заголовок = строка `rec+4` записи
		//   доп-меню, тело = нотис по id `rec+112`. ⚠ САМОГО ОКНА доп-меню (`g_pExtraMenuWnd`,
		//   контрол 74600) в порту ещё нет, а вместе с ним нет и его таблицы записей — брать текст
		//   неоткуда. Показываем индекс и пишем это в лог, чтобы дыра была видна, а не «замазана».
		if (Misc.ShowExtraMenuTooltip)
		{
			Misc.ShowExtraMenuTooltip((int32)S.SubType, S.Index);
			// ★ 30.10.2026: мост показывает плашку от имени `Hotbar` — им же и гасить (`asda.Quick.TipHideFix`).
			bTipShown = true;
			TipOwner = (CVarQuickTipHideFix.GetValueOnGameThread() != 0) ? EAsdaTipOwner::Hotbar : EAsdaTipOwner::Item;
			return;
		}
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] подсказка пункта доп-меню %d: окно доп-меню ")
			TEXT("(74600) в порту не портировано — таблицы записей нет"), S.Index);
		Title = UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990466), { FString::FromInt(S.Index) });   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Пункт меню %d»
		break;
	default:
		return;
	}
	Mini->ShowSharedTip(Title, Body, EAsdaTipOwner::Hotbar);
	bTipShown = true;
	TipOwner  = EAsdaTipOwner::Hotbar;
}

void UAsdaHotbarSubsystem::HideSlotTooltip()
{
	if (!bTipShown) { return; }
	bTipShown = false;
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	// ★ Гасим ИМЕННО того владельца, от чьего имени показывали (см. ShowSlotTooltip).
	if (TipOwner == EAsdaTipOwner::Item)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GI->GetSubsystem<UAsdaItemTooltipSubsystem>())
		{
			Tip->HideTooltipFor(EAsdaTipOwner::Item);
		}
	}
	else if (UAsdaMiniStateSubsystem* Mini = GI->GetSubsystem<UAsdaMiniStateSubsystem>())
	{
		Mini->HideSharedTip(EAsdaTipOwner::Hotbar);
	}
	TipOwner = EAsdaTipOwner::None;
}

// Таблица «ряд → колонка → слот → команда → клавиша». Номер команды = номер строки
//   `system/Shortcutkey.ini` (1-based): `BAR SLOT 1..12` = 6..17, `NEW BAR SLOT 1..12` = 18..29,
//   `PAGE 1..5` = 1..5. Хардкода клавиш нет: всё читается из живого `UAsdaKeybindConfig`.
FString UAsdaHotbarSubsystem::DumpKeyMap() const
{
	FString S = TEXT("═══ КЛАВИШИ ПАНЕЛИ БЫСТРОГО ДОСТУПА ═══\n");
	const UWorld* Wld = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const AAsdaPlayerController* PC = Wld ? Cast<AAsdaPlayerController>(Wld->GetFirstPlayerController()) : nullptr;
	if (!PC || !PC->Keys) { return S + TEXT("  контроллер/раскладка ещё не подняты\n"); }
	S += FString::Printf(TEXT("  раскладка: %s (%s)\n"), *PC->Keys->SourcePath,
		PC->Keys->Source == EAsdaKeybindSource::ShortcutkeyIni ? TEXT("Shortcutkey.ini")
			: (PC->Keys->Source == EAsdaKeybindSource::Profile ? TEXT("профиль") : TEXT("хардкод-дефолты")));
	for (int32 P = 0; P < 5; ++P)
	{
		FAsdaKeyBind B; PC->Keys->GetBind(P + 1, B);
		S += FString::Printf(TEXT("  страница %d  cmd %2d  %-22s  клавиша: %s\n"),
			P, P + 1, *UAsdaKeybindConfig::GetCmdName(P + 1), *UAsdaKeybindConfig::FormatBind(B));
	}
	auto Row = [&](bool bT, int32 FirstCmd, const TCHAR* Tag)
	{
		for (int32 C = 0; C < 12; ++C)
		{
			FAsdaKeyBind B; PC->Keys->GetBind(FirstCmd + C, B);
			S += FString::Printf(TEXT("  %-4s колонка %2d  слот %2d  cmd %2d  %-22s  клавиша: %s\n"),
				Tag, C, AbsSlot(C, bT), FirstCmd + C, *UAsdaKeybindConfig::GetCmdName(FirstCmd + C),
				*UAsdaKeybindConfig::FormatBind(B));
		}
	};
	Row(false, 6,  TEXT("осн"));
	Row(true,  18, TEXT("T"));
	S += TEXT("  ⚠ колонка 12 (слот оружия) команды не имеет — так и в оригинале (по 12 команд на ряд)\n");
	return S;
}

void UAsdaHotbarSubsystem::TickPointer()
{
	if (!bControlsReady || !FSlateApplication::IsInitialized()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ⚠ МОДАЛКА ИЗ ЭТОГО УСЛОВИЯ УБРАНА и проверяется НИЖЕ, под `Mouse.Update()` — см. гейт там.
	//   Чужой слой поверх панели по-прежнему выходит ДО опроса: там латч не трогается сознательно.
	if (WM->IsPointerBlockedByTopmost(kQuickWindowId))
	{
		// ★ 30.10.2026 (`asda.Quick.TipHideFix`): курсор ушёл на чужое окно поверх панели — у эталона наведение уходит
		//   верхнему контролу, а слот получает событие 0xA (`CControl_KillFocus(&g_ItemTooltip)`). Латч — как прежде.
		if (CVarQuickTipHideFix.GetValueOnGameThread() != 0 && HoverSlot >= 0 && !bDragActive)
		{
			HoverSlot = -1;
			HideSlotTooltip();
		}
		return;
	}
	// Эталон `hotbarOnUiEvent`: `if (!this->bPanelHidden) return 1;` — при заблокированной панели
	//   НИ ОДНО событие не обрабатывается.
	if (!bPanelActive) { return; }

	FSlateApplication& Sl = FSlateApplication::Get();
	// ★ Аудит №3 (B3): опрос ЛКМ и фронты — общая точка. Вызов стоит РОВНО там, где стоял прежний
	//   `GetPressedMouseButtons()`: все ранние выходы выше латч не трогают (как и раньше).
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ★ ПОЧЕМУ ПРОВЕРКА МОДАЛКИ ПЕРЕЕХАЛА СЮДА, ПОД ОПРОС. Раньше она стояла одним условием с
	//     `IsPointerBlockedByTopmost` ВЫШЕ `Mouse.Update()` и защёлки НЕ сбрасывала: латч замирал
	//     в доносном состоянии, и на возврате из-под нотиса выстреливал ЛОЖНЫЙ фронт ОТПУСКАНИЯ —
	//     то есть ПРИМЕНЕНИЕ слота (зелье/навык), которого игрок не заказывал. `Reset()` кладёт
	//     латч в «кнопка отпущена», поэтому удержанная кнопка даст потом ЧЕСТНЫЙ фронт нажатия.
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		// Начатый захват снимаем ТОЛЬКО визуально: `EndDrag` выполнил бы укладку, а это отмена.
		if (bDragActive) { CancelDragVisual(); RefreshPage(); }
		DragArm.Disarm();
		if (HoverSlot >= 0) { HoverSlot = -1; HideSlotTooltip(); }   // висящую плашку слота ГАСИМ
		Mouse.Reset();
		return;
	}
	const bool bShift = Sl.GetModifierKeys().IsShiftDown();

	// ★ Иконки уведомлений (`P_ICONSEL00..06`) висят НАД панелью (Origin «-16 -115»), поэтому
	//   их клик разбирается ДО хит-теста слотов: попав в рамку, дальше кадр не идёт (эталон
	//   маршрутизирует команду одному контролу — тому, кто её послал).
	if (TickNoticeClick()) { return; }

	bool bTRow = false; int32 Col = -1;
	const int32 Under = HitTestSlot(bTRow, Col);

	// ★ 30.10.2026 [ОРИГИНАЛ] (`asda.Quick.HelpTips`): справка кнопок листания 73327/73328 (`hotbarOnUiEvent@0xDF28E0`
	//   case 8: `a3 ∈ 0x11E6F…0x11E70` → `UI_BuildHelpTooltipText(&g_ItemTooltip, 1)`). Кнопка — не слот, поэтому в
	//   `HoverSlot` она живёт под номером ВНЕ диапазона слотов (`kHoverPageUp`/`kHoverPageDown`): «что-то под курсором»
	//   (`HoverSlot >= 0`) и гашение на уходе работают для неё той же веткой, что для слотов.
	constexpr int32 kHoverPageUp = 1000, kHoverPageDown = 1001;
	// Показ справочной темы от имени панели (`EAsdaTipOwner::Hotbar` — гасит её `HideSlotTooltip`). Гейт эталона
	//   `if (helper_getField1(g_pUIManager)) return` — пока чужой предмет «в руке», подсказки нет.
	auto ShowHelpTip = [this, GI](int32 Topic)
	{
		if (FAsdaDragGhost::IsAnyActive()) { return; }
		if (AsdaHelpTopics::Show(GI, Topic, EAsdaTipOwner::Hotbar)) { bTipShown = true; TipOwner = EAsdaTipOwner::Hotbar; }
	};
	int32 Hov = Under;
	if (Hov < 0 && CVarQuickHelpTips.GetValueOnGameThread() != 0)
	{
		if (IsCursorOver(BtnPageUp))        { Hov = kHoverPageUp; }
		else if (IsCursorOver(BtnPageDown)) { Hov = kHoverPageDown; }
	}

	// ── события 8/9/0xA: наведение → тултип (пока НЕ тащим — иначе призрак мешал бы тултипу) ──
	if (!bDragActive)
	{
		if (Hov >= 0 && Hov != HoverSlot)
		{
			// ★ 30.10.2026 (`asda.Quick.TipHideFix`): событие 0xA прежнего слота — гасим ДО показа нового. Без этого уход
			//   с полного слота на пустой оставлял чужую подсказку висеть над пустым слотом.
			if (HoverSlot >= 0 && CVarQuickTipHideFix.GetValueOnGameThread() != 0) { HideSlotTooltip(); }
			HoverSlot = Hov;
			if (Hov == kHoverPageUp || Hov == kHoverPageDown) { ShowHelpTip(1); }
			else if (!Slots[Under].IsEmpty()) { ShowSlotTooltip(Under); }
			// ★ [ОРИГИНАЛ] `uiSlotHoverTooltip@0xDED810` default-ветка: пустой слот оружия основного ряда (контрол 73023 =
			//   колонка 12) → `UI_BuildHelpTooltipText(&g_ItemTooltip, 0)` «Слот оружия». Обучающий шаг 4 той же ветки
			//   (`inventorySaveAndConfirm(g_pTutorialWindow, 4)`) не переносится: окна обучения в порту нет.
			else if (!bTRow && Col == kWeaponCol && CVarQuickHelpTips.GetValueOnGameThread() != 0) { ShowHelpTip(0); }
		}
		else if (Hov < 0 && HoverSlot >= 0)
		{
			HoverSlot = -1;
			HideSlotTooltip();
		}
	}
	else
	{
		// ── событие 9 при активном драге: тянуть призрак за курсором каждый кадр ──
		UpdateDragGhost();
	}

	// ── событие 5 (нажатие) / 7 (отпускание) ──
	if (Mouse.bLmbDown)
	{
		// Опция g_bHotbarRequireShift: перетаскивание только с зажатым Shift.
		// ★★★ ЗАХВАТ ЗДЕСЬ БОЛЬШЕ НЕ НАЧИНАЕТСЯ — только ВЗВОДИТСЯ (общая точка `FAsdaDragArm`).
		//   Гейт взвода повторяет гейт `BeginDrag` (`IsValidIndex && !IsEmpty`), чтобы ветка
		//   отпускания срабатывала ровно на тех же слотах, что и раньше.
		//   `bDragSrcTRow`/`DragSrcColumn` пишем ЗДЕСЬ, как и прежде: к моменту прохода порога
		//   курсор может уже уйти на соседний слот, а источник должен остаться тем, где нажали.
		if (Under >= 0 && (!bDragRequiresShift || bShift)
			&& Slots.IsValidIndex(Under) && !Slots[Under].IsEmpty())
		{
			bDragSrcTRow = bTRow;
			DragSrcColumn = Col;
			DragArm.Arm(Under, -1, Mouse.Cursor);
		}
		else
		{
			DragArm.Disarm();
		}
	}

	// ── «ЗАЖАЛИ И СДВИНУЛИ» ⇒ вот теперь захват (порог живёт в общей точке, не здесь) ──
	if (Mouse.bLmb && DragArm.ShouldStart(Mouse.Cursor))
	{
		BeginDrag(DragArm.KeyA);
		DragArm.MarkStarted();
	}

	if (Mouse.bLmbUp)
	{
		if (bDragActive)
		{
			const int32 SrcWas = DragSrcSlot;
			if (Under == SrcWas)
			{
				// ★★★ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА ПО ПРЯМОМУ ТРЕБОВАНИЮ ЮЗЕРА.
				//   В оригинале применение из панели быстрого доступа висит на ДВОЙНОМ левом клике:
				//   `hotbarOnUiEvent@0xDF28E0` (`ui_core.c:68897`) в `case 0xBu` (одиночное нажатие)
				//   лишь запоминает слот под курсором (`gap_52C[28]`), а `Hotbar_SlotActivate`
				//   вызывается только из `case 0xDu` (WM_LBUTTONDBLCLK).
				//   Юзер потребовал: двойной клик — везде (сумка, снаряжение, склад…), КРОМЕ панели
				//   быстрого доступа, где применение остаётся ОДИНОЧНЫМ. Здесь ровно это и сделано:
				//   отпускание ЛКМ над тем же слотом = применить.
				CancelDragVisual();
				RefreshPage();
				ActivateSlot(SrcWas);
			}
			else
			{
				EndDrag(Under, bTRow, Col);
			}
		}
		// ★★★ ПОРОГ НЕ ПРОЙДЕН ⇒ это был ОБЫЧНЫЙ КЛИК по слоту, а не перетаскивание.
		//   Применение из панели — ОДИНОЧНОЕ по прямому требованию юзера (см. ветку выше), поэтому
		//   здесь тот же `ActivateSlot`. Визуал восстанавливать не надо: `BeginDrag` не вызывался,
		//   иконка и текст стопки слота не трогались.
		else if (DragArm.IsArmed() && Under == DragArm.KeyA)
		{
			ActivateSlot(DragArm.KeyA);
		}
		DragArm.Disarm();              // кнопку отпустили — взвод в любом случае снят
	}
	// ★ Аудит №3 (B3): запись латча делает `Mouse.Update()` в начале — отдельной строки здесь больше
	//   нет. Порядок кадра сохранён: между опросом и обработкой латч никто не читал.
}

// ══════════════════════════════ ИКОНКИ УВЕДОМЛЕНИЙ ═════════════════════════════════════════════
bool UAsdaHotbarSubsystem::AddNoticeIcon(int32 NoticeType)
{
	const TCHAR* IconName = NoticeTypeToIcon(NoticeType);
	if (!IconName)
	{
		UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] уведомление типа %d: иконки в таблице MsgNotify_TypeToIcon нет"), NoticeType);
		return false;
	}
	// ★★★ ДЕДУПЛИКАЦИЯ — НЕ ДЛЯ ВСЕХ ТИПОВ (найдено аудитом 31.08.2026).
	//   Эталон `statusBarAddIcon@0xDF3F80` отсеивает повторы ТОЛЬКО для типов 11, 13, 15, 16
	//   (у каждого свой отдельный цикл проверки). Остальные — в том числе тип 1 (соулмейт),
	//   тип 2 (приглашение в приятели) и тип 3 — вешают ОТДЕЛЬНЫЙ значок на КАЖДОЕ событие.
	//   Прежняя безусловная дедупликация давала дефект: при двух и более приглашениях
	//   ответ на первое снимал единственный значок, и остальные терялись навсегда.
	{
		const bool bDedup = (NoticeType == 11 || NoticeType == 13 || NoticeType == 15 || NoticeType == 16);
		if (bDedup)
		{
			for (FAsdaHotbarNoticeUi& N : NoticeUi) { if (N.NoticeType == NoticeType) { return false; } }
		}
	}
	for (int32 I = 0; I < NoticeUi.Num(); ++I)
	{
		FAsdaHotbarNoticeUi& N = NoticeUi[I];
		if (N.NoticeType >= 0 || !N.Icon) { continue; }
		N.NoticeType = NoticeType;
		FAsdaHotbarSlot Fake;
		Fake.IconAtlas = IconName; Fake.IconX = 0; Fake.IconY = 0; Fake.IconSize = 64;
		ApplyIconToImage(N.Icon, Fake);
		SetVis(N.Group, true);   // ★ без раскрытия свёрнутой группы иконку не видно (см. FAsdaHotbarNoticeUi::Group)
		SetVis(N.Icon, true);
		if (N.SelFrame) { N.SelFrame->bLoop = true; N.SelFrame->RestartAnim(); SetVis(N.SelFrame, true); }
		// ★ ПУЛЬСАЦИЯ. Стартовое состояние — ровно то, что делает `statusBarAddIcon@0xDF3F80`:
		//   `SetColor4(…, 0xFF)` + `objStartTimedAction(icon, 255 → 0, 500 мс)`, фаза = 1,
		//   таймер 500 мс. Дальше её ведёт `TickNoticeBlink` (порт `uiControlPairToggleBlink`).
		N.BlinkPhase = 1;
		N.BlinkFrom = 255.f; N.BlinkTo = 0.f; N.BlinkDur = 0.5f; N.BlinkElapsed = 0.f;
		N.BlinkTimer = 0.f;
		if (N.Icon)     { N.Icon->SetRenderOpacity(1.f); }
		if (N.SelFrame) { N.SelFrame->SetRenderOpacity(1.f); }
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] уведомление типа %d → иконка %s (слот %d)"), NoticeType, IconName, I);
		return true;
	}
	// ★ Различаем ДВЕ разные причины: «панель ещё не поднята» (в мире её нет — например, в PIE на
	//   логине) и «все 7 слотов реально заняты». Раньше обе давали одно и то же сообщение, и
	//   диагностика уведомлений вводила в заблуждение.
	if (NoticeUi.Num() == 0)
	{
		UE_LOG(LogAsdaQuick, Warning,
			TEXT("[хотбар] уведомление типа %d: панель `QUICK_NEW_WIN` ещё не поднята (контролы не разобраны) — показывать не на чем"),
			NoticeType);
		return false;
	}
	UE_LOG(LogAsdaQuick, Warning, TEXT("[хотбар] уведомление типа %d: все 7 слотов уведомлений заняты"), NoticeType);
	return false;
}

bool UAsdaHotbarSubsystem::HasNoticeIcon(int32 NoticeType) const
{
	for (const FAsdaHotbarNoticeUi& N : NoticeUi) { if (N.NoticeType == NoticeType) { return true; } }
	return false;
}

void UAsdaHotbarSubsystem::RemoveNoticeIcon(int32 NoticeType)
{
	bool bRemoved = false;
	for (FAsdaHotbarNoticeUi& N : NoticeUi)
	{
		if (N.NoticeType != NoticeType) { continue; }
		N.NoticeType = -1;
		bRemoved = true;
	}
	if (bRemoved) { CompactNoticeIcons(); }
}

// ★ Порт `buffSlotCompact` + `MsgNotify_RefreshIcons`: после снятия уведомления оставшиеся
//   СДВИГАЮТСЯ ВЛЕВО, а не оставляют дыру. Без этого `AddNoticeIcon` («первый слот с типом −1»)
//   заполнял бы дыру, и порядок иконок расходился бы с порядком событий.
void UAsdaHotbarSubsystem::CompactNoticeIcons()
{
	// Собрать оставшиеся типы в порядке слотов и переразложить их подряд с начала.
	TArray<int32> Types;
	for (const FAsdaHotbarNoticeUi& N : NoticeUi) { if (N.NoticeType >= 0) { Types.Add(N.NoticeType); } }
	for (int32 I = 0; I < NoticeUi.Num(); ++I)
	{
		FAsdaHotbarNoticeUi& N = NoticeUi[I];
		const int32 T = Types.IsValidIndex(I) ? Types[I] : -1;
		N.NoticeType = T;
		if (T < 0)
		{
			SetVis(N.Group, false);
			SetVis(N.Icon, false);
			SetVis(N.SelFrame, false);
			continue;
		}
		if (const TCHAR* IconName = NoticeTypeToIcon(T))
		{
			FAsdaHotbarSlot Fake;
			Fake.IconAtlas = IconName; Fake.IconX = 0; Fake.IconY = 0; Fake.IconSize = 64;
			ApplyIconToImage(N.Icon, Fake);
		}
		SetVis(N.Group, true);
		SetVis(N.Icon, true);
		if (N.SelFrame) { N.SelFrame->bLoop = true; N.SelFrame->RestartAnim(); SetVis(N.SelFrame, true); }
	}
}

// ★ Полный дамп слотов уведомлений — см. описание у команды `asda.Quick.NoticeDump`.
FString UAsdaHotbarSubsystem::DumpNoticeIcons()
{
	// Панель квик-бара поднимается ЛЕНИВО (только в мире); для диагностики поднимаем её здесь —
	//   иначе дамп всегда отвечал бы «панель не поднята» и проверить видимость было бы нечем.
	EnsureWidget();
	auto VisName = [](const UWidget* Wg) -> const TCHAR*
	{
		if (!Wg) { return TEXT("НЕТ ВИДЖЕТА"); }
		switch (Wg->GetVisibility())
		{
		case ESlateVisibility::Visible:               return TEXT("Visible");
		case ESlateVisibility::Collapsed:             return TEXT("COLLAPSED");
		case ESlateVisibility::Hidden:                return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:      return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible:  return TEXT("SelfHitTestInvisible");
		default:                                      return TEXT("?");
		}
	};
	FString S = TEXT("=== иконки уведомлений квик-бара (P_ICON00_N…06_N) ===\n");
	if (NoticeUi.Num() == 0)
	{
		return S + TEXT("панель `QUICK_NEW_WIN` не поднята: слотов 0 (в мире она поднимается лениво)");
	}
	for (int32 I = 0; I < NoticeUi.Num(); ++I)
	{
		const FAsdaHotbarNoticeUi& N = NoticeUi[I];
		// ДВА разных прямоугольника, и путать их нельзя:
		//   «макет» — слот канваса, координаты ЛОКАЛЬНЫ для группы-родителя (у иконки это всегда
		//             (0,0)-(64,64): смещение −21/−53 несёт сама группа);
		//   «экран» — абсолютная геометрия последнего кадра; она и отвечает на вопрос «а не за
		//             экраном ли». У свёрнутого виджета её нет (нулевой размер) — это тоже ответ.
		FVector2D P(ForceInitToZero), Sz(ForceInitToZero);
		const bool bRect = N.Icon && GetSlotRect(N.Icon, P, Sz);
		FString Screen = TEXT("нет (виджет не отрисован)");
		if (N.Icon)
		{
			const FGeometry& G = N.Icon->GetCachedGeometry();
			const FVector2D A = FVector2D(G.GetAbsolutePosition());
			const FVector2D L = FVector2D(G.GetLocalSize()) * (double)G.Scale;
			if (L.X > 0.0 && L.Y > 0.0)
			{
				Screen = FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), A.X, A.Y, A.X + L.X, A.Y + L.Y);
			}
		}
		const TCHAR* Tex = (N.NoticeType >= 0) ? NoticeTypeToIcon(N.NoticeType) : nullptr;
		S += FString::Printf(
			TEXT("слот %d: тип=%d%s%s | группа=%s иконка=%s рамка=%s | альфа=%.2f (фаза %d) | ")
			TEXT("макет=%s | экран=%s\n"),
			I, N.NoticeType,
			Tex ? TEXT(" текстура=") : TEXT(""), Tex ? Tex : TEXT(""),
			VisName(N.Group), VisName(N.Icon), VisName(N.SelFrame),
			N.Icon ? N.Icon->GetRenderOpacity() : 0.f, N.BlinkPhase,
			bRect ? *FString::Printf(TEXT("(%.0f,%.0f)-(%.0f,%.0f)"), P.X, P.Y, P.X + Sz.X, P.Y + Sz.Y)
			      : TEXT("нет слота канваса"),
			*Screen);
	}
	return S;
}

// Порт `hotbarClearType4Slots@0xDF...` + `hotbarRebuildSlots`: слоты навыка соулмейта (Type 4)
//   снимаются целиком, затем кэш пересобирается. Общая точка — своей копии раскладки панели
//   владельцы подсистем НЕ заводят.
int32 UAsdaHotbarSubsystem::ClearSoulmateSkillSlots()
{
	int32 nCleared = 0;
	for (int32 i = 0; i < kSlotCount; ++i)
	{
		if (GetSlot(i).Type == (uint8)EAsdaHotbarSlotType::SkillAuto)
		{
			ClearSlot(i);
			++nCleared;
		}
	}
	if (nCleared > 0) { RebuildSlots(); }
	UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] снято слотов навыка соулмейта (тип 4): %d"), nCleared);
	return nCleared;
}

void UAsdaHotbarSubsystem::ClearNoticeIcons()
{
	for (FAsdaHotbarNoticeUi& N : NoticeUi)
	{
		N.NoticeType = -1;
		SetVis(N.Group, false);
		SetVis(N.Icon, false);
		SetVis(N.SelFrame, false);
	}
}

// ═══ Пульсация alpha (порт `uiControlPairToggleBlink@0xDF65B0`, таймеры 18…24 по 500 мс) ═══
void UAsdaHotbarSubsystem::TickNoticeBlink(float DeltaTime)
{
	if (DeltaTime <= 0.f) { return; }
	for (FAsdaHotbarNoticeUi& N : NoticeUi)
	{
		if (N.NoticeType < 0 || !N.Icon) { continue; }
		// Текущий прогон `objStartTimedAction` (линейная интерполяция, как у эталона).
		N.BlinkElapsed += DeltaTime;
		const float A = (N.BlinkDur > 0.f)
			? FMath::Lerp(N.BlinkFrom, N.BlinkTo, FMath::Clamp(N.BlinkElapsed / N.BlinkDur, 0.f, 1.f))
			: N.BlinkTo;
		const float Op = FMath::Clamp(A / 255.f, 0.f, 1.f);
		N.Icon->SetRenderOpacity(Op);
		if (N.SelFrame) { N.SelFrame->SetRenderOpacity(Op); }

		// Тик таймера 500 мс — переключение фазы.
		N.BlinkTimer += DeltaTime;
		while (N.BlinkTimer >= 0.5f)
		{
			N.BlinkTimer -= 0.5f;
			if (N.BlinkPhase == 1) { N.BlinkFrom = 0.f;   N.BlinkTo = 255.f; N.BlinkDur = 0.5f; }
			else                   { N.BlinkFrom = 255.f; N.BlinkTo = 128.f; N.BlinkDur = 1.0f; }
			N.BlinkElapsed = 0.f;
			N.BlinkPhase ^= 1;
		}
	}
}

// ═══ Клик по иконке уведомления (порт `uiNoticeButtonHandler@0xDF4DB0`) ═══
//   Эталон принимает команду 0x11E90…0x11E96 = контролы 73360…73366, то есть кликабельна ИМЕННО
//   РАМКА `P_ICONSEL`, а не сама иконка (у них один и тот же прямоугольник 64×64, поэтому для
//   игрока разницы нет). Порядок эталона: сперва `uiNoticeButtonDispatch` (открыть окно), затем
//   `buffSlotCompact(this, тип)` + `MsgNotify_RefreshIcons` — то есть снятие ПОСЛЕ обработки.
//   ★ Хит-тест — общая точка `AsdaUi::IsCursorOverWidget` (закон `port-shared-points`), фронт ЛКМ —
//   уже существующий кадровый `Mouse`; отдельных прозрачных кнопок над рамками не заводим.
bool UAsdaHotbarSubsystem::TickNoticeClick()
{
	if (!Mouse.bLmbDown) { return false; }
	for (int32 I = 0; I < NoticeUi.Num(); ++I)
	{
		FAsdaHotbarNoticeUi& N = NoticeUi[I];
		if (N.NoticeType < 0) { continue; }
		const UWidget* Hit = N.SelFrame ? (const UWidget*)N.SelFrame : (const UWidget*)N.Icon;
		if (!Hit || !AsdaUi::IsCursorOverWidget(Hit)) { continue; }
		const int32 Type = N.NoticeType;
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] клик по иконке уведомления слот=%d тип=%d"), I, Type);
		return HandleNoticeIconClick(Type);
	}
	return false;
}

bool UAsdaHotbarSubsystem::HandleNoticeIconClick(int32 Type)
{
	// Тело клика после хит-теста рамки (вынесено 29.09.2026, волна О2 — путь самотестов тот же, что у мыши).
	if (!HasNoticeIcon(Type)) { return false; }
	// ★ 29.09.2026 (волна 0 торговли): [ОРИГИНАЛ] `uiNoticeButtonHandler@0xDF4DB0` (ui_core.c:69361-69369) — значок
	//   заявки обмена (тип 4 обычные / 5 личные вещи) при ОТКРЫТОМ окне обмена 2008 (`CExchange+0x2C`): 5042 в чат
	//   (−75279 = 0xFFFED9F1, 10 с), окно-вопрос не открывается и значок НЕ снимается. [ПОРТ] Значки 4/5 ставит модель обмена
	//   (волна О1), признак — общая точка `UAsdaTradeStateSubsystem`.
	if ((Type == 4 || Type == 5) && UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GetGameInstance()))
	{
		AsdaNotice::ChatColored(GetGameInstance(), EAsdaMsgCategory::System, AsdaNotice::LocRaw(GetGameInstance(), 5042),
			AsdaNoticeColor::Pink, 10000.f);
		UE_LOG(LogAsdaQuick, Log, TEXT("[хотбар] значок обмена тип %d: окно обмена 2008 открыто — нотис 5042, значок остаётся (ui_core.c:69361)"), Type);
		return true;
	}
	OnNoticeIconClicked.Broadcast(Type);   // `uiNoticeButtonDispatch@0xDF4F10`
	// ★ 26.10.2026 (агент smmessage) [ОРИГИНАЛ]: `buffSlotCompact@0xDF5280` снимает ВСЕ значки только у типов 0/8, 1, 3, 4/5 (7), 14;
	//   у прочих (ветка default) — ПЕРВЫЙ значок этого типа. Для записок Другу (тип 9) это важно: значок висит на КАЖДУЮ записку,
	//   клик открывает одну — остальные значки остаются. Ключ `asda.Soulmate.NoteIconOnePerClick 0` — прежний порт (снять все).
	//   Прочие типы не тронуты (у них поведение порта прежнее).
	extern bool AsdaSoulNoteIconOnePerClickOn();
	if (Type == 9 && AsdaSoulNoteIconOnePerClickOn())
	{
		for (FAsdaHotbarNoticeUi& N : NoticeUi)
		{
			if (N.NoticeType == Type) { N.NoticeType = -1; break; }
		}
		CompactNoticeIcons();
		return true;
	}
	RemoveNoticeIcon(Type);                // `buffSlotCompact` + `MsgNotify_RefreshIcons`
	return true;
}
