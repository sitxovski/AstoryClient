// Copyright (c) 2026 @sitxovski (Discord)
// AsdaChatWindowSubsystem_Mouse.cpp — ★ 23.10.2026 часть UAsdaChatWindowSubsystem, вынесенная из AsdaChatWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaChatWindowSubsystem.plan.json).
//   Мышь (TickPointer, ресайз), тумблер заморозки автопрокрутки 2607, окно справки 2580, комбобокс канала 2608, прокрутка и колесо.
//   #include — те же, что у AsdaChatWindowSubsystem.cpp, в том же порядке; общее — AsdaChatWindowSubsystemInternal.h.
#include "UI/AsdaChatWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: WireTextByteLen/TruncateToWireBytes (лимит ввода в байтах провода)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "UI/AsdaUiHelpers.h"   // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "UI/AsdaTipPanel.h"     // ★ 30.10.2026: EAsdaTipOwner::ChatWin — владелец подсказок окна чата

// ★ 30.10.2026 [ОРИГИНАЛ] подсказки вкладок 2501 и поля ввода 2609 при наведении (`TickPointer`). 0 = прежний порт.
static ASDA_DEV_CVAR(int32, CVarChatHelpTips,
	TEXT("asda.Chat.HelpTips"),
	1,
	TEXT("1 = как в оригинале: при наведении на вкладку чата и на поле ввода — подсказка; 0 = прежний порт (подсказок нет)."),
	ECVF_Default);
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ эмоции слэш-командой (порт ChatInput_ParseCommand)
#include "UI/AsdaJuiSlot.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атласы JUI — только через менеджер
#include "UI/AsdaMarkup.h"   // UAsdaRichText::ApplyFormatted — теги <FS>/<FD>/<FC=..>
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/TextWidgetTypes.h"   // UTextLayoutWidget — общий предок UTextBlock и URichTextBlock
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/RichTextBlock.h"
#include "Components/EditableText.h"
#include "Components/CheckBox.h"   // 2607 CHAT_NEW_Hbtn — CheckButton скина Chat_new_ck3
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"   // замер высоты строки для точной центровки подписи 2608
#include "Styling/CoreStyle.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaNotice.h"                 // ★ Дедуп №2, В1-B (U10c): `AsdaNotice::Loc` вместо прямого `FAsdaLoginData::Loc`
#include "UI/AsdaPartySubsystem.h"         // ★ 12.10.2026 [ОРИГИНАЛ]: чат отряда — `Party_IsInParty` и `g_nPartyInviteTargetIdx` (= PartyIndex)
#include "UI/AsdaSoulmateSubsystem.h"      // ★ 12.10.2026 [ОРИГИНАЛ]: канал «Друг» — слот/пара/имя партнёра (`g_pSoulmateWnd`)
#include "AsdaChatWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaChatWindowSubsystem*.cpp (LogAsdaChatWin, чистые хелперы)


// ★★★ ЗАПИСЬ РАЗМЕРА ОКНА 2500 В ini ИГРЫ (решение юзера 2026-08-07 «все окна … сохраняют»).
//   Отдельной функцией, потому что точек фиксации размера три и они обязаны писать ОДНО И ТО ЖЕ:
//   отпускание верхней кромки, отпускание правой кромки и кнопка 2605. Механизм общий —
//   `UAsdaWindowSubsystem::SaveWindowSize` (секция `[AsdaWindowLayout]`), своего файла у чата нет.
//   ★ В ЭТАЛОНЕ размеры окна чата не сохраняются вовсе: `ChatOpt.bin` (chat.c:1397) хранит только
//   вкладки/каналы/цвета. Это осознанное отступление, «к эталону» не возвращать.
static void AsdaPersistChatSize(UGameInstance* GI, float BaseW, float BaseH, bool bBottomFixed)
{
	if (!GI) { return; }
	if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SaveWindowSize(FName(TEXT("CHAT_NEW")), FVector2D(BaseW, BaseH), bBottomFixed);
	}
}

// ══════════════════════════════ МЫШЬ ══════════════════════════════════════════════════════════

// ★★★ ЭКРАННЫЙ КУРСОР → ЛОКАЛЬНЫЕ (БАЗОВЫЕ) КООРДИНАТЫ КОРНЕВОГО КАНВАСА ОКНА.
//   Это ЕДИНСТВЕННЫЙ правильный перевод для «поставить виджет в точку курсора»:
//     Local = (Abs − ГеометрияКанваса.AbsolutePosition) / ГеометрияКанваса.Scale
//   Учитывает и DPI-масштаб, и ПОЛОЖЕНИЕ ОКНА ИГРЫ на рабочем столе. Прежний вариант
//   `Cur / GetViewportScale()` терял второе слагаемое и в оконном режиме уводил всплывающее
//   меню от курсора на «позиция окна / масштаб» (живой тест 2026-08-06, окно 2670).
//   ★ Хит-тесты (`IsUnderCursor`) переводить НЕ НУЖНО: они сравнивают в АБСОЛЮТНОМ пространстве
//   через `FGeometry::IsUnderLocation`, там обе стороны уже в одних координатах.
//   ★ Перетаскивание (`TickMenuDrag`) считает РАЗНОСТЬ координат — ей достаточно масштаба.
FVector2D UAsdaChatWindowSubsystem::ScreenToWindowBase(const FVector2D& ScreenPos) const
{
	// ★ D2 (дедуп UI 2026-08-08): единая реализация в AsdaUi::ScreenToCanvasLocal (её же зовёт хотбар).
	UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	return AsdaUi::ScreenToCanvasLocal(ChatWidget, ScreenPos, W);
}

bool UAsdaChatWindowSubsystem::IsUnderCursor(const UWidget* W, const FVector2D& CursorAbs) const
{
	if (!W) { return false; }
	const FGeometry& G = W->GetCachedGeometry();
	if (G.GetLocalSize().X <= 0.f || G.GetLocalSize().Y <= 0.f) { return false; }
	return G.IsUnderLocation(CursorAbs) && !AsdaUi::IsCoveredByHigherWindow(W, CursorAbs);   // ★ 06.11.2026: тело окна поверх чата
}

UWidget* UAsdaChatWindowSubsystem::FindNodeByName(const TCHAR* Exact, const TCHAR* Needle) const
{
	// ★ D5 (дедуп UI 2026-08-08): единая реализация в AsdaUi::FindNodeByName.
	return AsdaUi::FindNodeByName(ChatWidget, Exact, Needle);
}

// ★ Перенесено в ассет (15.09, разрешение владельца): здесь было определение
//   UAsdaChatWindowSubsystem::NormalizeControlSlot (рантайм-перевод SPAN-анкора в POINT). В ассете
//   все слоты окна — POINT, функция удалена вместе с объявлением.

bool UAsdaChatWindowSubsystem::GetWindowScreenRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!ChatWidget || !ChatWidget->WidgetTree) { return false; }
	bool bAny = false;
	TArray<UWidget*> All;
	ChatWidget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W || W->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		// ★ ТОЛЬКО узлы, которые окно и образуют: подложка, рамка, контейнер ленты.
		//   Объединение ВСЕХ узлов брать НЕЛЬЗЯ — один растянутый контрол раздует зону на весь
		//   экран и заблокирует игру (случай SCROLL_DOWN на окне 2570, отчёт Д9.2).
		//   Подсказка 2580 (справка) исключена намеренно: её rect уходит вверх до y=371.
		const FString N = W->GetName();
		if (!N.StartsWith(TEXT("CHAT_NEW_back"))
		 && !N.StartsWith(TEXT("back2"))
		 && !N.StartsWith(TEXT("lCHAT_NEW_ine0"))
		 && !N.StartsWith(TEXT("CHAT_NEW_C_List"))
		 // ★ Добавлено 2026-08-05: строка ввода (группа SizeWin, 465×158) — часть корпуса окна,
		 //   без неё нижние 30 px окна пропускали клик в мир. Габарит ограничен, зону не раздувает.
		 && !N.StartsWith(TEXT("SizeWin"))
		 // Окно справки 2580 (276×310) — только когда открыто: цикл выше пропускает Collapsed.
		 && !N.StartsWith(TEXT("CHAT_NEW_Que_btn_tooltip_grp"))) { continue; }
		// ★ Аудит №4 (A9/B1): прямоугольник — через общую точку `AsdaUi::WidgetAbsRect`.
		//   Накопитель и СПИСОК несущих узлов (фильтр по имени выше) остаются здесь — это знание
		//   про конкретное окно, а не про геометрию.
		FVector2D P, PMax;
		if (!AsdaUi::WidgetAbsRect(W, P, PMax)) { continue; }
		const FVector2D Sz = PMax - P;
		if (!bAny) { OutMin = P; OutMax = P + Sz; bAny = true; }
		else
		{
			OutMin.X = FMath::Min(OutMin.X, P.X); OutMin.Y = FMath::Min(OutMin.Y, P.Y);
			OutMax.X = FMath::Max(OutMax.X, P.X + Sz.X); OutMax.Y = FMath::Max(OutMax.Y, P.Y + Sz.Y);
		}
	}
	return bAny;
}

bool UAsdaChatWindowSubsystem::GetComboListScreenRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!bComboOpen || !ComboList) { return false; }
	// ★ Аудит №4 (A9/B1): через общую точку `AsdaUi::WidgetAbsRect` — она возвращает false ровно
	//   в том случае, ради которого ниже написан запасной расчёт (кэш геометрии ещё нулевой).
	if (AsdaUi::WidgetAbsRect(ComboList, OutMin, OutMax)) { return true; }
	// ★ В КАДР РАСКРЫТИЯ кэш геометрии списка ещё нулевой (он был Collapsed, Slate его не считал),
	//   и зона не работала бы ровно на том кадре, когда она нужнее всего. Считаем от кнопки 2608:
	//   список стоит ВПЛОТНУЮ НАД ней, ширина kComboW, высота kComboRowH × число ВИДИМЫХ пунктов
	//   (не kComboItems: пункт «Армия» вне битвы армий скрыт, и зона была бы на строку выше плиты).
	const UWidget* Btn = ChatWidget ? ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_0")) : nullptr;
	FVector2D BP, BMax;
	if (!AsdaUi::WidgetAbsRect(Btn, BP, BMax)) { return false; }   // ★ A9/B1: общая точка
	const FVector2D BSz = BMax - BP;
	if (BSz.Y <= 0.f) { return false; }
	const float S = BSz.Y / kComboH;                       // масштаб экрана относительно базовых px
	OutMin = FVector2D(BP.X, BP.Y - kComboRowH * (float)FMath::Max(1, ComboVisibleCount) * S);
	OutMax = FVector2D(BP.X + kComboW * S, BP.Y);
	return true;
}

bool UAsdaChatWindowSubsystem::IsCursorOverWindow() const
{
	if (!ChatWidget || !FSlateApplication::IsInitialized()) { return false; }
	FVector2D Mn, Mx;
	if (!GetWindowScreenRect(Mn, Mx)) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	return (Cur.X >= Mn.X && Cur.X <= Mx.X && Cur.Y >= Mn.Y && Cur.Y <= Mx.Y);
}

// ЕДИНЫЙ хит-тест мыши по фактической геометрии — не зависит ни от класса виджета, ни от
//   доставки событий Slate (грабли №1 и №2). Фиксируем узел, НА КОТОРОМ началось нажатие,
//   иначе протаскивание мимо ложно срабатывало бы на соседнем узле.
// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] ключ щелчка мимо списка каналов; разбор — у проверки в `TickPointer`.
static ASDA_DEV_CVAR(int32, CVarChatComboClickAway,
	TEXT("asda.Chat.ComboClickAway"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 (по умолчанию) — щелчок по вкладке или стрелке гасит раскрытый список каналов; 0 — прежнее: список остаётся."),
	ECVF_Default);

void UAsdaChatWindowSubsystem::TickPointer(float DeltaTime)
{
	if (!FSlateApplication::IsInitialized()) { return; }
	// ★ Аудит №3 (B3): опрос ОБЕИХ кнопок, фронты и курсор — общая точка, ровно на месте прежних
	//   трёх строк опроса.
	// ★ ПКМ. Карта событий CControl: триады симметричны — ЛКМ 11/13/12, ПКМ 14/16/15.
	//   `case 0xF` в `uiChatWindowMsgProc` = ПКМ КЛИК-ОТПУСКАНИЕ (state[1] == 0), именно оно
	//   открывает оба контекстных меню (ui_core.c:12505-12509). Ловим фронт отпускания.
	Mouse.Update();
	const FVector2D Cur = Mouse.Cursor;
	const bool bDown = Mouse.bLmb;
	UWorld* WdT = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	const float UiScale = FMath::Max(0.01f, WdT ? UWidgetLayoutLibrary::GetViewportScale(WdT) : 1.f);

	// ★★★ HOVER НИКА В ЛЕНТЕ 2521 — КАЖДЫЙ КАДР (в эталоне разбор разметки и запись
	//   g_szParsedNameBuf живут ВНУТРИ функции рендера списка `uiListWidgetRender @0x12ECC50`,
	//   а не в обработчике мыши). Отсюда же берётся ник для ПКМ-меню 80000.
	TickRibbonHover(Cur);
	// Состояния скина кнопок открытого меню (SS0 обычная / SS1 наведение / SS2 нажатие).
	RefreshMenuHover(Cur, bDown);

	// ★★★ МОДАЛЬНОЕ ОКНО ЗАБИРАЕТ МЫШЬ ЦЕЛИКОМ (эталон `uiIsAnyModalWindowOpen @0xB79520`).
	//   Пока открыт диалог ввода 1580 или подтверждения 1520, чат не должен ни ловить клики, ни
	//   перехватывать фокус (иначе клик по «ОК» диалога попутно уходил бы в поле ввода 2609 и
	//   отбирал у диалога клавиатуру). Оконный менеджер уже держит прозрачный блокер под модалкой —
	//   здесь просто не разбираем указатель.
	if (UGameInstance* GIm = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WMm = GIm->GetSubsystem<UAsdaWindowSubsystem>())
		{
			//   ★★★ П8-9, ВОЛНА 9: условие переехало в ОБЩУЮ ТОЧКУ `AsdaUi::IsModalDispatchBlocked`
			//   (ключ отката `asda.UI.ModalGatePolling`), а к нему добавлен СБРОС ЗАЩЁЛОК.
			//   ⚠ ЗАЧЕМ СБРОС. Прежний голый `return` оставлял взведёнными `PressedNode`,
			//   `bTipDragging`, `bResizingTop` и `bResizingRight`: нажатие ДО нотиса и отпускание
			//   ПОСЛЕ его закрытия сходились в действие, которого игрок не делал — кромка окна
			//   «допрыгивала» до нового положения курсора. Для окна модалка и есть «кнопку
			//   отпустили», поэтому отдаём те же ветки, что и штатное отпускание, включая запись
			//   размера и смещения в ini (иначе изменённый размер не сохранился бы).
			if (AsdaUi::IsModalDispatchBlocked(GIm))
			{
				if (bTipDragging)
				{
					bTipDragging = false;
					WMm->SetSavedWindowOffset(FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_QueTip")), QueTipOffset);
				}
				if (bResizingTop || bResizingRight)
				{
					bResizingTop = false; bResizingRight = false;
					AsdaPersistChatSize(GetGameInstance(), BaseWidth(), BaseHeight(), bLayoutToggled);
				}
				PressedNode = nullptr;
				Mouse.Reset();
				HelpTip.Hide(GIm, EAsdaTipOwner::ChatWin);   // ★ 30.10.2026: под модалкой у чата «ухода» мыши
				return;
			}
		}
	}

	// ★ ЭТАП 4: редактор вкладок 2590 и палитра 2700 — ОТДЕЛЬНЫЕ окна поверх чата, клик в них
	//   разбирается ПЕРВЫМ и дальше по цепочке не идёт (они модальны по смыслу, как и меню).
	if (TabEditorWidget && TabEditorWidget->GetVisibility() != ESlateVisibility::Collapsed)
	{
		HelpTip.Hide(GetGameInstance(), EAsdaTipOwner::ChatWin);   // ★ 30.10.2026: мышь у редактора вкладок
		TickTabEditorPointer(Cur, Mouse.bLmbDown);
		return;
	}

	if (Mouse.bLmbDown)
	{
		PressedNode = nullptr;
		// ★ 07.11.2026 [ИСПРАВЛЕНИЕ] щелчок мимо раскрытого списка каналов гасит его и там, где цепочка ниже до ветки «клик мимо» не доходит
		//   (вкладки, стрелки, крестик справки). Клик продолжает свой путь. Кнопка 2608 здесь исключена: её щелчок — переключатель.
		if (bComboOpen && CVarChatComboClickAway.GetValueOnGameThread() != 0 && !(ComboList && IsUnderCursor(ComboList, Cur))
			&& !IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_0")), Cur)
			&& !IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_1")), Cur))
		{
			SetChannelListOpen(false);
		}
		// ★★★ ОТКРЫТОЕ КОНТЕКСТНОЕ МЕНЮ ЗАБИРАЕТ КЛИК ПЕРВЫМ И БЕЗУСЛОВНО — оно нарисовано
		//   поверх всего (z = kMenuZ) и геометрически может накрыть что угодно, включая вкладки,
		//   ленту и строку ввода. Тот же принцип, что у раскрытого списка каналов ниже.
		const bool bMenuTookClick = HandleMenuMouseDown(Cur);
		if (bMenuTookClick)
		{
			// клик поглощён меню — дальше по цепочке не идём
		}
		else
		// ★★★ РАСКРЫТЫЙ СПИСОК КАНАЛОВ — ПЕРВЫМ И БЕЗУСЛОВНО (живой тест 2026-08-05).
		//   Он всплывает ВВЕРХ от строки ввода (базовые y 526…726, x 28…128) и геометрически
		//   НАКЛАДЫВАЕТСЯ на ряд вкладок 2501 (y ≈ верх окна + 11…36, x от 10). Ветки вкладок
		//   стоят в этой цепочке первыми (дополнение 11), поэтому часть пунктов списка съедалась
		//   вкладкой под ними. Всплывающий слой обязан забирать мышь раньше всего остального —
		//   ровно тот же принцип, что и у IsPointerBlockedByTopmost для соседних окон.
		if (bComboOpen && ComboList && IsUnderCursor(ComboList, Cur))
		{
			PressedNode = ComboList;
			int32 Picked = -1;
			for (int32 i = 0; i < ComboRows.Num(); ++i)
			{
				if (IsUnderCursor(ComboRows[i], Cur)) { Picked = i; break; }
			}
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-каналы] клик по списку в (%.0f,%.0f) → пункт %d"),
				Cur.X, Cur.Y, Picked);
			if (Picked >= 0) { SelectChannelIndex(Picked); }
			SetChannelListOpen(false);
		}
		// ★ Порядок проверок = сверху вниз по z: мелкие кнопки раньше большой зоны drag'а.
		// ★★★ ЖИВОЙ ТЕСТ: «вкладки и стрелки не кликабельны». Причина — этих веток
		//   ЗДЕСЬ НЕ БЫЛО ВОВСЕ: вставка шла по якорю «else if (bQueTipVisible … QueTipXSrc)»,
		//   а в файле это ПЕРВЫЙ `if` без `else` ⇒ замена молча не сработала, и мышь
		//   до вкладок не доходила. Вкладки и стрелки стоят ПЕРВЫМИ — они выше всего остального
		//   в ряду и перекрываются с зоной растягивания верхней кромки.
		else if (bTabsBuilt && IsUnderCursor(TabArrowL, Cur))
		{
			PressedNode = TabArrowL;
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат] стрелка ◄ нажата"));
			ScrollChatTabs(-1);
		}
		else if (bTabsBuilt && IsUnderCursor(TabArrowR, Cur))
		{
			PressedNode = TabArrowR;
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат] стрелка ► нажата"));
			ScrollChatTabs(+1);
		}
		else if (bTabsBuilt && TabHitTest(Cur) >= 0)
		{
			const int32 Hit = TabHitTest(Cur);
			PressedNode = TabQuads.IsValidIndex(Hit - TabScroll) ? TabQuads[Hit - TabScroll].Get() : nullptr;
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат] вкладка %d нажата"), Hit);
			SelectChatTab(Hit);
		}
		else if (bQueTipVisible && IsUnderCursor(QueTipXSrc, Cur))
		{
			// 2589 — chatWnd_OnCommand case 0xA1D: uiWnd_SetVisibleAndRegister(self, id−2556, 0).
			PressedNode = QueTipXSrc;
			SetQueTipVisible(false);
		}
		else if (IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_0")), Cur)
		      || IsUnderCursor(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_1")), Cur))
		{
			// Клик по кнопке 2608 — раскрыть/свернуть список.
			SetChannelListOpen(!bComboOpen);
		}
		else if (bComboOpen)
		{
			SetChannelListOpen(false);   // клик мимо — список закрывается
		}
		else if (bQueTipVisible && IsUnderCursor(QueTipMoveSrc, Cur))
		{
			// ★ Перетаскивание окна справки — отступление по требованию юзера.
			PressedNode = QueTipMoveSrc;
			bTipDragging = true;
			TipDragStartMouse = Cur;
			TipDragStartOffset = QueTipOffset;
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] НАЧАТО перетаскивание окна справки 2580"));
		}
		else if (IsUnderCursor(QueBtnSrc, Cur))
		{
			// 2537 — chatWnd_OnCommand case 0x9E9: uiWnd_SetVisibleAndRegister(self, id, 1).
			PressedNode = QueBtnSrc;
			SetQueTipVisible(true);
		}
		else if (IsUnderCursor(UDbtnSrc, Cur))
		{
			PressedNode = UDbtnSrc;
			OnUDbtnClicked();
		}
		else if (IsUnderCursor(HbtnSrc, Cur))
		{
			// ★★★ 2607 = ТУМБЛЕР ЗАМОРОЗКИ АВТОПРОКРУТКИ (скин `Chat_new_ck3`, Type="CheckButton";
			//   `checkBox_OnToggle @0x12FDF70` поднимает 0x10601 при ВКЛЮЧЕНИИ и 0x10602 при
			//   ВЫКЛЮЧЕНИИ — полное доказательство в шапке поля `bScrollFrozen`).
			//   Оконная процедура чата пишет по этим событиям CChat+497 = 1 / 0
			//   (ui_core.c:12304 и ui_core.c:12574) — то есть ровно наш тоггл.
			//   ★ Раньше здесь стояло «пока удерживается» — из-за чего кнопка на вид не работала:
			//   флаг падал на том же отпускании, которым игрок её нажал.
			PressedNode = HbtnSrc;
			SetScrollFrozen(!bScrollFrozen);
		}
		else if (IsUnderCursor(HomeBtnSrc, Cur))
		{
			PressedNode = HomeBtnSrc;
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] НАЖАТ узел CHAT_NEW_HomeBtn (%s) в (%.0f,%.0f)"),
				*HomeBtnSrc->GetClass()->GetName(), Cur.X, Cur.Y);
			OnHomeBtnClicked();
		}
		else if (IsUnderCursor(ResizeTopSrc, Cur))
		{
			// ★ Захват ВЕРХНЕЙ КРОМКИ. Низ окна закреплён, поэтому тянуть можно только вверх/вниз,
			//   и это единственный способ менять размер помимо эталонной кнопки 2605.
			UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
			PressedNode = ResizeTopSrc;
			bResizingTop = true;
			bLayoutToggled = true;            // с этого момента низ окна закреплён на 750 (как в эталоне)
			CurH = BaseHeight();
			DragStartH = CurH;
			DragStartMouseY = Cur.Y / Scale;
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-окно] НАЧАТО растягивание верхней кромки (высота %.0f, пределы %.0f…%.0f)"),
				CurH, kHCollapsed, kHExpanded);
		}
		else if (IsUnderCursor(ResizeRightSrc, Cur))
		{
			// ★ Захват ПРАВОЙ КРОМКИ — горизонталь. ЛЕВЫЙ край окна (якорь kWinX = 1) НЕ ДВИГАЕТСЯ:
			//   окно стоит в левом нижнем углу, свободное место — справа, поэтому растём ВПРАВО.
			//   Отдельная зона, а не «угол»: так вертикальный ресайз остаётся ровно тем, чем был,
			//   и не приходится выдумывать угловой контрол, которого в JUI нет.
			UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
			const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
			PressedNode = ResizeRightSrc;
			bResizingRight = true;
			CurW = BaseWidth();
			ResizeStartW = CurW;
			ResizeStartX = Cur.X / Scale;
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-окно] НАЧАТО растягивание правой кромки (ширина %.0f, пределы %.0f…%.0f)"),
				CurW, kWinMinW, kWinMaxW);
		}
		else if (LinesBox && IsUnderCursor(LinesBox, Cur))
		{
			// ★ ЛЕВОЕ НАЖАТИЕ ПО ЛЕНТЕ 2521 ГАСИТ МЕНЮ 80000 — `case 0xB` (ui_core.c:12478)
			//   вызывает `sub_93393C(pThis)` = CControl_SetVisible(this[425], 0). Это ВТОРОЙ
			//   (и последний) способ закрыть меню помимо трёх его кнопок: клик МИМО окна меню
			//   его НЕ закрывает — проверено, `sub_93393C` вызывается ровно в 4 местах.
			//   ★ 07.11.2026: порт гасит меню и щелчком мимо (`asda.UI.ClickAwayMenu`); эта ветка работает при ключе 0.
			//   ★ Проверка стоит ПОСЛЕДНЕЙ в цепочке: кнопка 2528 (HomeBtn) лежит ВНУТРИ
			//   прямоугольника ленты, и если поставить ленту раньше — 2528 перестанет нажиматься.
			PressedNode = LinesBox;
			HideNickMenu();
		}
	}

	// Перетаскивание открытого меню за его зону 80009 / 2675 (JUI EVT 6 → RET 20003).
	if (bDown) { TickMenuDrag(Cur, UiScale); }

	// ★ НАВЕДЕНИЕ НА ВКЛАДКИ И СТРЕЛКИ — состояния SS2/SS3 скина TalkBar_2.
	if (bTabsBuilt)
	{
		const int32 NewHover = TabHitTest(Cur);
		const int32 NewArrow = IsUnderCursor(TabArrowL, Cur) ? 0 : (IsUnderCursor(TabArrowR, Cur) ? 1 : -1);
		if (NewHover != TabHoverIdx || NewArrow != ArrowHoverIdx)
		{
			TabHoverIdx = NewHover; ArrowHoverIdx = NewArrow;
			RefreshChatTabs();
		}
	}

	// ★ 30.10.2026 [ОРИГИНАЛ] (`asda.Chat.HelpTips`): подсказки при наведении — `uiChatWindowMsgProc` (ui_core.c:12439-12474)
	//   событие 8: вкладка 2501 (`CHAT_NEW_Radio_B`) → `tutorialPopupLayoutSmall(5361, 5362)`, поле ввода 2609 (`CHAT_NEW_Edit2`) →
	//   (5363, 5364); 9 — плашка за курсором; 0xA — гашение. Ветки 201/209/215/256/257/258 эталона стоят в окне `CHAT_BACKWIN`
	//   (214), которое оригинал держит невидимым (chat.c:256) — на экране их нет и у нас. Гейта «предмет в руке» у чата нет.
	{
		UGameInstance* GIh = GetGameInstance();
		const UAsdaWindowSubsystem* WMh = GIh ? GIh->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
		int32 Key = -1, TitleLoc = 0, BodyLoc = 0;
		if (CVarChatHelpTips.GetValueOnGameThread() != 0 && WMh && !WMh->IsPointerBlockedByTopmost(kZoneId) && !bComboOpen)
		{
			// `IsUnderCursor` смотрит только геометрию — свёрнутое поле ввода (до Enter) подсказку давать не должно.
			if (bTabsBuilt && TabHitTest(Cur) >= 0)                                       { Key = 2501; TitleLoc = 5361; BodyLoc = 5362; }
			else if (InputEdit && AsdaUi::IsShown(InputEdit) && IsUnderCursor(InputEdit, Cur)) { Key = 2609; TitleLoc = 5363; BodyLoc = 5364; }
		}
		HelpTip.Update(GIh, EAsdaTipOwner::ChatWin, Key,
			[GIh, TitleLoc, BodyLoc]() { return AsdaHelpTopics::ShowText(GIh, TitleLoc, BodyLoc, EAsdaTipOwner::ChatWin); },
			/*bDragGate*/ false);
	}

	// Подсветка строки под курсором в раскрытом списке каналов (наведение = FFFFFFFF).
	if (bComboOpen)
	{
		int32 Hover = -1;
		for (int32 i = 0; i < ComboRows.Num(); ++i)
		{
			if (IsUnderCursor(ComboRows[i], Cur)) { Hover = i; break; }
		}
		if (Hover != ComboHover) { ComboHover = Hover; RefreshChannelCombo(Hover); }
	}

	if (bDown && bTipDragging)
	{
		UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
		ApplyQueTipOffset(TipDragStartOffset + (Cur - TipDragStartMouse) / Scale);
	}

	if (bDown && bResizingTop)
	{
		UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
		// Курсор вверх (Y уменьшается) → высота растёт: низ на месте, верх едет вверх.
		const float Want = FMath::Clamp(DragStartH + (DragStartMouseY - Cur.Y / Scale), kHCollapsed, kHExpanded);
		if (!FMath::IsNearlyEqual(Want, CurH, 0.5f))
		{
			CurH = Want;
			bExpanded = (CurH > (kHCollapsed + kHExpanded) * 0.5f);   // чтобы 2605 продолжал быть осмысленным
			RefreshLayout();
			if (LinesBox && !bScrollFrozen) { LinesBox->ScrollToEnd(); }
		}
	}

	if (bDown && bResizingRight)
	{
		UWorld* Wd = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		const float Scale = FMath::Max(0.01f, Wd ? UWidgetLayoutLibrary::GetViewportScale(Wd) : 1.f);
		// Курсор вправо (X растёт) → ширина растёт: левый край на месте, правый едет вправо.
		const float Want = FMath::Clamp(ResizeStartW + (Cur.X / Scale - ResizeStartX), kWinMinW, kWinMaxW);
		if (!FMath::IsNearlyEqual(Want, CurW, 0.5f))
		{
			CurW = Want;
			RefreshLayout();
			// ★ ПЕРЕНОС СТРОК ЛЕНТЫ пересчитывается по НОВОЙ ширине — иначе строки остались бы
			//   разорванными по старой (главная ловушка горизонтального ресайза).
			ApplyLinesWidth();
			if (LinesBox && !bScrollFrozen) { LinesBox->ScrollToEnd(); }
		}
	}

	if (Mouse.bLmbUp)
	{
		if (bTipDragging)
		{
			bTipDragging = false;
			// ★ Справка 2580 — тоже ОКНО, которое пользователь двигает, поэтому её позиция едет в
			//   тот же ini через внешний вход общего механизма (`SetSavedWindowOffset`): двигает её
			//   не `TickDrag`, а собственный код чата, значит и записать сдвиг обязан он сам.
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
				{
					WM->SetSavedWindowOffset(FName(TEXT("CHAT_NEW")), FName(TEXT("CHAT_NEW_QueTip")), QueTipOffset);
				}
			}
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] окно справки 2580 перемещено на (%.0f,%.0f) базовых px"),
				QueTipOffset.X, QueTipOffset.Y);
		}
		if (bResizingTop)
		{
			bResizingTop = false;
			AsdaPersistChatSize(GetGameInstance(), BaseWidth(), BaseHeight(), bLayoutToggled);
			UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] растягивание закончено: высота %.0f, окно y %.0f…%.0f"),
				BaseHeight(), BaseTop(), BaseTop() + BaseHeight());
		}
		if (bResizingRight)
		{
			bResizingRight = false;
			AsdaPersistChatSize(GetGameInstance(), BaseWidth(), BaseHeight(), bLayoutToggled);
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-окно] растягивание вправо закончено: ширина %.0f, окно x %.0f…%.0f"),
				BaseWidth(), kWinX, kWinX + BaseWidth());
		}
		// ★ ЗДЕСЬ БЫЛ СБРОС ЗАМОРОЗКИ ПО ОТПУСКАНИЮ МЫШИ — он и делал кнопку 2607 «нерабочей».
		//   Событие 0x10602 (ui_core.c:12574) — это СНЯТИЕ ГАЛОЧКИ чекбокса, а не отпускание
		//   кнопки мыши; оно приходит только со ВТОРОГО клика по 2607 и обрабатывается в тоггле.
		// ★ КОМАНДА ПУНКТА МЕНЮ — на ОТПУСКАНИИ над той же кнопкой (в эталоне пункты разбираются
		//   в `case 0x10401` = WM_COMMAND, то есть по завершённому клику PushButton'а).
		HandleMenuMouseUp(Cur);
		// ★ ВЫБОР СТРОКИ ЛЕНТЫ — `uiList_OnMouseUpSelect @0x12EB9F0` (ui_core.c:112098) пишет
		//   индекс в CListBox+0x51C ПО ОТПУСКАНИЮ ЛКМ. Выделенная строка получает плиту цвета
		//   `~item[+2760]` (у обычных строк ⇒ СПЛОШНУЮ БЕЛУЮ), а её текст красится в
		//   `item[+2760] | 0xFF000000` (⇒ ЧЁРНЫЙ). ★ ЭТО ОРИГИНАЛ, А НЕ БАГ КОНТРАСТА.
		if (PressedNode == LinesBox && LinesBox && IsUnderCursor(LinesBox, Cur))
		{
			int32 Hit = -1;
			for (int32 i = 0; i < LinesBox->GetChildrenCount(); ++i)
			{
				if (IsUnderCursor(LinesBox->GetChildAt(i), Cur)) { Hit = i; break; }
			}
			if (Hit != RibbonSelIdx)
			{
				const int32 Old = RibbonSelIdx;
				RibbonSelIdx = Hit;
				RepaintRibbonLine(Old);
				RepaintRibbonLine(RibbonSelIdx);
				UE_LOG(LogAsdaChatWin, Verbose, TEXT("[чат-лента] выбрана строка %d (было %d)"), RibbonSelIdx, Old);
			}
		}
		PressedNode = nullptr;
	}
	// ★ Аудит №3 (B3): здесь стояла запись латча ЛКМ — её делает `Mouse.Update()` в начале функции.
	//   Порядок сохранён: блок ПКМ ниже латч ЛКМ не читал и не читает.

	// ═══════════ ПКМ: событие 15 (клик-отпускание) — ЕДИНСТВЕННЫЕ ДВЕ ВЕТКИ ЭТАЛОНА ═══════════
	//   `ui_core.c:12505-12509`: ctrlId 2501 → окно 2670 в точке курсора;
	//                            ctrlId 2521 + непустой g_szParsedNameBuf → меню 80000 (кX+32, кY−81).
	//   Больше ПКМ-обработчиков у окна чата НЕТ: ни по фону 2500, ни по полю ввода 2609, ни по
	//   скроллбару 2536, ни по журналу 2570/2572, ни по кнопке-замку 2607, ни по комбобоксу 2608
	//   (проверено сплошным просмотром всех `case` в `uiChatWindowMsgProc`).
	if (Mouse.bRmbUp)
	{
		// ★★★ ЖИВОЙ ТЕСТ 2026-08-06: «меню 2670 открывается слишком далеко от курсора».
		//   ПРИЧИНА: `Cur / UiScale` — НЕПОЛНЫЙ перевод. `GetCursorPos()` даёт координаты В
		//   ЭКРАННОМ (десктопном) пространстве, а слот меню живёт в ЛОКАЛЬНОМ пространстве
		//   корневого канваса. Деление на DPI-масштаб учитывает только МАСШТАБ и молча
		//   предполагает, что окно игры прибито к (0,0) рабочего стола. В ОКОННОМ режиме (а
		//   упакованная сборка у юзера именно такая) окно смещено — и меню уезжало ровно на
		//   «позиция окна / масштаб». Полный перевод — через геометрию самого канваса.
		const FVector2D CurBase = ScreenToWindowBase(Cur);
		if (LinesBox && IsUnderCursor(LinesBox, Cur) && !RibbonHoverNick.IsEmpty())
		{
			ShowNickMenu(RibbonHoverNick, CurBase + FVector2D(kNickMenuDX, kNickMenuDY));
		}
		else if (bTabsBuilt && (TabHitTest(Cur) >= 0
		                     || IsUnderCursor(TabArrowL, Cur) || IsUnderCursor(TabArrowR, Cur)))
		{
			// ★★★ ЗАПОМИНАЕМ ВКЛАДКУ, ПО КОТОРОЙ ВЫЗВАНО МЕНЮ (Д27 п.5, требование юзера).
			//   ЭТАЛОН этого не делает: правый клик по списку вкладку НЕ выбирает
			//   (`uiList_OnMouseUpSelect @0x12EB9F0` начинается с `if (!a2)`, где a2 = номер
			//   кнопки мыши ⇒ пишется только по ЛЕВОМУ отпусканию), и пункты 2673/2674 берут
			//   `self[108]` = АКТИВНУЮ вкладку (chat.c:2110/2119). Здесь — осознанное отступление.
			//   По стрелкам прокрутки вкладки нет ⇒ −1 ⇒ цель останется активной (как в эталоне).
			ShowTabMenu(CurBase, TabHitTest(Cur));
		}
		else if (LinesBox && IsUnderCursor(LinesBox, Cur))
		{
			UE_LOG(LogAsdaChatWin, Verbose,
				TEXT("[чат-меню] ПКМ по ленте: ник в строке не распознан (нет пары «[…]») — эталон меню не открывает"));
		}
	}
	// ★ Аудит №3 (B3): запись латча ПКМ делает `Mouse.Update()` в начале функции.
}

// ★★★ ПЕРЕТАСКИВАНИЕ УБРАНО (требование юзера 2026-08-05). Здесь была `ApplyWindowOffset`,
//   которая двигала окно за зону 2534. Вместе с ней сняты `WinOffset`, `DragStart*` и клампы
//   по краям экрана — мёртвого кода не осталось. Заодно снят и повод для бага, который до этого
//   пришлось чинить: она меняла КОРНЕВОЙ слот WBP (полноэкранный от AddToViewport) на 474×178,
//   и все дети, анкеренные к низу родителя с оффсетом T = Y − 768, пересчитывались от 178 —
//   содержимое окна уезжало на −10 px по Y. Теперь корневой слот не трогает вообще ничто.

// CHAT_NEW_UDbtn (2605) — chatWnd_OnCommand case 0xA2D (chat.c:2085):
//     uiToggleExpandLayout(self, *(self + 496) == 0)
//   `uiToggleExpandLayout @0xBB6E40` (ui_core.c:13529): развернуть → Origin.y = −170 и высота
//   340, свернуть → Origin.y = 0 и высота 170. Верх окна = Origin.y − 188 + 768, то есть
//   410 / 580, а НИЗ в обоих случаях 750. Всё остальное эталон пересчитывает сам из режимов
//   Align* — у нас это делает ApplyWindowLayout по тем же правилам.
// ★ ОСТАЛОСЬ ПО ЭТАЛОНУ (ЭТАП 3): вместе с окном чата тогл двигает на те же −170 окно
//   системных сообщений A (CChat+48 = sysMsgAWin, ID 2557) — оно в порт ещё не заведено.
// ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (требование юзера 2026-08-06) — НЕ «ЧИНИТЬ» ОБРАТНО.
//   Эталонная кнопка знает ровно два размера по ВЫСОТЕ (170 ↔ 340) и ширину не трогает.
//   Требование: «для текущей сессии запоминать размер окна и по горизонту, и по вертикали;
//   нажатие — сворачивать в дефолт, повторное — восстановить в сохранённые размеры».
//   РЕАЛИЗАЦИЯ (без лишних флагов — состояние выводится из самой геометрии):
//     • первое нажатие ВООБЩЕ (окно ещё на авторских 474×178) — эталонное РАЗВОРАЧИВАНИЕ в 474×340
//       и оно же становится «запомненным» размером ⇒ пара 170↔340 оригинала сохраняется полностью;
//     • окно НЕ в дефолте (474 × 170) — запоминаем текущие W/H и схлопываем в дефолт;
//     • окно в дефолте — возвращаем запомненные W/H.
//   Растянул мышью в свёрнутом состоянии — окно перестало быть дефолтным, значит следующее
//   нажатие запомнит именно этот размер: ровно то, что просил юзер, без отдельного флага.
void UAsdaChatWindowSubsystem::OnUDbtnClicked()
{
	const bool bAtDefault = FMath::IsNearlyEqual(BaseWidth(),  kWinMinW,    0.5f)
	                     && FMath::IsNearlyEqual(BaseHeight(), kHCollapsed, 0.5f);
	const TCHAR* What = nullptr;
	// ★ Условие «ширина ещё авторская» обязательно: горизонтальное растягивание НЕ ставит
	//   bLayoutToggled (иначе окно прыгнуло бы по вертикали 178→170 прямо под курсором), поэтому
	//   без него растянутое-но-не-тогленное окно потеряло бы свою ширину при первом нажатии.
	if (!bLayoutToggled && FMath::IsNearlyEqual(BaseWidth(), kWinMinW, 0.5f))
	{
		// Ни тогла, ни растягивания ещё не было (окно 474×178 из JUI) — эталонная первая реакция.
		SavedW = kWinMinW; SavedH = kHExpanded; bHasSavedSize = true;
		CurW = kWinMinW;   CurH = kHExpanded;
		What = TEXT("РАЗВЁРНУТО (первое нажатие = эталонный uiToggleExpandLayout)");
	}
	else if (!bAtDefault)
	{
		SavedW = BaseWidth(); SavedH = BaseHeight(); bHasSavedSize = true;
		CurW = kWinMinW;      CurH = kHCollapsed;
		What = TEXT("СВЁРНУТО в дефолт (размер запомнен на сессию)");
	}
	else
	{
		CurW = bHasSavedSize ? FMath::Clamp(SavedW, kWinMinW,    kWinMaxW)   : kWinMinW;
		CurH = bHasSavedSize ? FMath::Clamp(SavedH, kHCollapsed, kHExpanded) : kHExpanded;
		What = TEXT("ВОССТАНОВЛЕНО из запомненного размера");
	}
	bLayoutToggled = true;      // после первого тогла высота уже 170/340, а не 178 из JUI
	bExpanded = (CurH > (kHCollapsed + kHExpanded) * 0.5f);
	// ★ Итоговый размер — в ini. Пара «свернуть/развернуть» (`SavedW/SavedH`) при этом остаётся
	//   СЕССИОННОЙ: в файл едет то, как окно выглядит СЕЙЧАС, поэтому следующий запуск открывает
	//   чат ровно в том виде, в каком его оставили, а кнопка продолжает работать как раньше.
	AsdaPersistChatSize(GetGameInstance(), BaseWidth(), BaseHeight(), bLayoutToggled);
	RefreshLayout();
	// Ширина изменилась ⇒ пересчёт переноса строк ленты обязателен (иначе строки останутся
	//   порезанными по прежней ширине — та же ловушка, что и у растягивания мышью).
	ApplyLinesWidth();
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] UDbtn (2605): %s — rect (%.0f,%.0f,%.0fx%.0f); запомнено %.0fx%.0f (%s) ")
		TEXT("★ отступление: в эталоне кнопка меняет ТОЛЬКО высоту 170↔340 и ничего не помнит"),
		What, WinL, WinT, WinW, WinH, SavedW, SavedH, bHasSavedSize ? TEXT("есть") : TEXT("нет"));
	if (LinesBox && !bScrollFrozen) { LinesBox->ScrollToEnd(); }
}

// ══════════ CHAT_NEW_Hbtn (2607) — ТУМБЛЕР ЗАМОРОЗКИ АВТОПРОКРУТКИ (флаг CChat+497) ═══════════
// Доказательство «это тумблер, а не hold» — в шапке поля `bScrollFrozen`.
// ★ Эталон при СНЯТИИ флага ленту в конец НЕ гонит: 0x10602 пишет только `CChat+497 = 0`
//   (ui_core.c:12574), а прыжок к концу произойдёт сам при следующей строке или смене вкладки.
//   Воспроизведено ровно так — «догоняющего» скролла здесь нет намеренно.
void UAsdaChatWindowSubsystem::SetScrollFrozen(bool bOn)
{
	if (bScrollFrozen == bOn) { RefreshScrollLockVisual(); return; }
	bScrollFrozen = bOn;
	RefreshScrollLockVisual();
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] Hbtn (2607): автопрокрутка %s (CChat+497 = %d). Строки в ленту приходят ")
		TEXT("как обычно — заморожен ТОЛЬКО прыжок к последней строке (chat.c:3375 и chat.c:1828)"),
		bScrollFrozen ? TEXT("ЗАМОРОЖЕНА") : TEXT("включена"), bScrollFrozen ? 1 : 0);
}

// Кадр скина: SS0/SS1 (UV 0.742…0.785) — выключено, SS2/SS3 (UV 0.789…0.832) — включено.
//   Импортёр положил их в Unchecked*/Checked* стиля `UCheckBox` (asda_jui_generic_umg.py:361),
//   поэтому кадр ставится штатным SetIsChecked, а не подменой кисти.
void UAsdaChatWindowSubsystem::RefreshScrollLockVisual()
{
	if (!HbtnSrc) { return; }
	if (UCheckBox* Cb = Cast<UCheckBox>(HbtnSrc))
	{
		Cb->SetIsChecked(bScrollFrozen);
		Cb->SetRenderOpacity(1.f);
		Cb->SetIsEnabled(true);   // ★ Д26: выключенный виджет Slate рисуется обесцвеченным
		return;
	}
	// Контрол внезапно не чекбокс (перегенерация окна другим типом) — состояние показываем
	//   яркостью и ГРОМКО пишем в лог, а не молчим.
	HbtnSrc->SetRenderOpacity(bScrollFrozen ? 1.f : 0.55f);
	UE_LOG(LogAsdaChatWin, Warning,
		TEXT("[чат-окно] 2607 CHAT_NEW_Hbtn — не UCheckBox, а %s: кадр «заморожено» показать нечем, ")
		TEXT("состояние отрисовано яркостью. Скин Chat_new_ck3 имеет Type=\"CheckButton\" — проверить импорт окна."),
		*HbtnSrc->GetClass()->GetName());
}

// CHAT_NEW_Que_btn_tooltip (2580) — окно справки кнопки 2537.
void UAsdaChatWindowSubsystem::SetQueTipVisible(bool bVisible)
{
	bQueTipVisible = bVisible;
	if (QueTipSrc)
	{
		QueTipSrc->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] окно справки 2580 %s"),
		bVisible ? TEXT("ОТКРЫТО (кнопка 2537)") : TEXT("закрыто"));
}

// ═══════════ ОКНО СПРАВКИ 2580 «Изменение режима чата» — ПОЛНЫЙ РАЗБОР (2026-08-05) ═══════════
//
// Заполнение — `CChat_BuildQueBtnUI @0xB9FFA0` (chat.c:1229-1282):
//   2581←loc 5400 (+ `CClass_SetField926(0x200000)` = ЖИРНЫЙ), 2582←5401, 2583←5402, 2584←5403,
//   2585←5404, 2586←5405, 2588←5406. (2587 в эталоне не заполняется.)
//
// ★★★ ЦВЕТ ТЕКСТА — ЭТО НАША СТАРАЯ ГРАБЛЯ, ПОВТОРИЛАСЬ. У КАЖДОГО узла в JUI ДВА цвета:
//   • `CaptionInfo = "0 0 ff400040 5"` — цвет ПОДПИСИ контрола (caption). У всех семи он один и
//     тот же тёмно-фиолетовый ff400040 — очевидный «дефолт редактора», подписи здесь пустые.
//   • блок `EXS … TextColor = "…" TextAlign = "…" … EXE` — цвет и выравнивание САМОГО ТЕКСТА,
//     который и рисуется. Именно он у каждого узла СВОЙ.
//   Ровно на этом мы уже спотыкались в окне смерти (death_ui_layout.md: «импортёр красил в
//   CaptionInfo ff400040 — цвет caption, а не текста»). Берём TextColor:
//
//     узел  loc   TextColor   TextAlign   что это
//     2581  5400  FFC0C0C0    5 (центр)   заголовок «Изменение режима чата»
//     2582  5401  FFFF8040    0 (слева)   «Для быстрой смены режимов чата набери '/', …»
//     2583  5402  FFC0C0C0    0           колонка команд  /all /party /soulmate …
//     2584  5403  FF80FF00    0           колонка алиасов /A /P /S /C /J /B /N /Y /R /W
//     2585  5404  FF80FF00    0           колонка-разделитель «-»
//     2586  5405  FFFFFF80    0           «Чтобы передать игроку личное сообщение, набери '/W имя'»
//     2588  5406  FFFFFFFF    4 (v-центр) «Чтобы ответить на шепот, нажми 'R' и введи сообщение»
//   Маска TextAlign: 0x1 = по центру по горизонтали, 0x2 = вправо, 0x4 = по центру по вертикали,
//   0x8 = вниз (парсер CaptionInfo/EXS, ui_core.c:105012). sRGB→linear делает разметка (<FC=…>).
//
// ★★★ ПЕРЕВОДЫ СТРОК: строки 5402/5403 содержат ЛИТЕРАЛЬНЫЕ «\n» (`/all\n/party\n/soulmate\n…` —
//   десять команд), одной строкой в колонке шириной 80 px они не помещаются. В настоящие переводы строки
//   их разворачивает НЕ этот код: текст приходит через `GetTextById` → `FAsdaText::Loc` →
//   `FAsdaMarkup::StripTags` (AsdaMarkup.cpp:251), который обе формы «\n»/«\N» уже развернул.
//   ★ 15.09 (поправка ревизии; прежний комментарий «без местного разворота команды шли одной строкой» был
//   неверен): цикл ниже — эквивалент эталонного `CControl_SetText` для текста, пришедшего НЕ через
//   `StripTags`; на нынешнем пути он ничего не меняет (разбор у цикла).
//
// Кегль — общий портовый FONT_PX = 8 (`<FS08>`), как во всём UI; перенос — `SetWrapTextAt(ширина слота
//   узла в ассете)` прямо на UTextBlock (без `ApplyFormatted`, см. ниже).
void UAsdaChatWindowSubsystem::BuildQueTipTexts()
{
	if (bQueTipTextsBuilt || !QueTipSrc || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Host = Cast<UCanvasPanel>(QueTipSrc);
	if (!Host) { return; }
	// Ничто не должно вылезать за рамку окна справки.
	Host->SetClipping(EWidgetClipping::ClipToBounds);

	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	// Игровой шрифт берём из Default-строки того же стиля, которым порт рисует весь текст.
	// ★ Аудит №4 (B2/A29): путь — общая константа `kAsdaRichStyleDT` (AsdaMarkup.h), загрузка —
	//   общая точка `AsdaDt::Cached`. До сведения ту же таблицу грузила ещё и разметка.
	if (const FRichTextStyleRow* Row =
			AsdaDt::Row<FRichTextStyleRow>(kAsdaRichStyleDT, FName(TEXT("Default")), TEXT("ChatTipFont")))
	{
		TipFont = Row->TextStyle.Font;
	}

	// ★★★ ТЕКСТ ПИШЕТСЯ В САМИ КОНТРОЛЫ ОКНА, КАК У ЭТАЛОНА (15.09, разрешение владельца на окно чата).
	//   `CChat_BuildQueBtnUI` (chat.c:1238-1280) находит 2581…2586 и 2588 по IDNum и зовёт
	//   `CControl_SetText(loc 5400…5406)` — отдельной отрисовки поверх у оригинала нет; корейский
	//   `EXS TextContent` из JUI (gamemain_jui.ini:93213-93403) этим вызовом заменяется и на экран не выходит.
	//   В ассете эти контролы — узлы `CHAT_NEW_Que_btn_tooltip_TXTnn` (имя по общему правилу
	//   `asda_jui_parse.anchored_names`: якорь 2580 + номер в порядке документа): 2581…2586 создал шаг
	//   `asda_ui_missing_text_nodes`, 2588 (TextContent пуст) — шаг `asda_ui_chat_markup_visibility`.
	//   ГЕОМЕТРИЯ — ИЗ АССЕТА (формулы импортёра по Origin/Size/Align JUI), здесь её нет. До 15.09 код
	//   строил параллельные `CHAT_NEW_Que_tip_2581…2588` в тех же прямоугольниках — текст шёл дважды.
	//   Кодом остаётся оформление (выравнивание/перенос по EXS, цвет EXS TextColor, жирный 2581) и текст.
	//   Порядок документа JUI ≠ порядку IDNum: TXT01 = 2583, TXT02 = 2582, TXT03 = 2586 (сверено по дампу).
	struct FTip { int32 Id; int32 LocId; const TCHAR* Node; bool bBold; const TCHAR* Color; int32 Align; };
	static const FTip Tips[] =
	{
		{ 2581, 5400, TEXT("CHAT_NEW_Que_btn_tooltip_TXT00"), true,  TEXT("FFC0C0C0"), 5 },
		{ 2582, 5401, TEXT("CHAT_NEW_Que_btn_tooltip_TXT02"), false, TEXT("FFFF8040"), 0 },
		{ 2583, 5402, TEXT("CHAT_NEW_Que_btn_tooltip_TXT01"), false, TEXT("FFC0C0C0"), 0 },
		{ 2584, 5403, TEXT("CHAT_NEW_Que_btn_tooltip_TXT04"), false, TEXT("FF80FF00"), 0 },
		{ 2585, 5404, TEXT("CHAT_NEW_Que_btn_tooltip_TXT05"), false, TEXT("FF80FF00"), 0 },
		{ 2586, 5405, TEXT("CHAT_NEW_Que_btn_tooltip_TXT03"), false, TEXT("FFFFFF80"), 0 },
		{ 2588, 5406, TEXT("CHAT_NEW_Que_btn_tooltip_TXT06"), false, TEXT("FFFFFFFF"), 4 },
	};
	// ★★★ БЕЗ `ApplyFormatted` (живой тест 2026-08-05, второй круг). Тот подменяет UTextBlock на
	//   URichTextBlock и создаёт НОВЫЙ виджет: и перенос, и выравнивание, которые мы ставили на
	//   исходном, уходили вместе со старым виджетом — на экране строки 2582/2586 оставались
	//   одной длинной строкой и обрезались рамкой окна, а заголовок 2581 не центрировался.
	//   Разметка здесь не нужна вовсе: у каждой подписи ОДИН цвет, ОДИН кегль и опционально
	//   жирное начертание. Поэтому оставляем обычный UTextBlock и правим его напрямую —
	//   ни один параметр после этого потеряться не может.
	QueTipTexts.Reset();
	for (const FTip& T : Tips)
	{
		UTextBlock* Tb = Cast<UTextBlock>(ChatWidget->GetWidgetFromName(FName(T.Node)));
		if (!Tb)
		{
			// Эталон на этом месте показывает MessageBox «CHAT_NEW_Que_btn_tooltip_txtN UI Point NULL».
			UE_LOG(LogAsdaChatWin, Error,
				TEXT("[чат-окно] справка 2580: нет узла %s (контрол %d) в WBP_CHAT_NEW — прогнать шаг пост-импорта asda_ui_chat_markup_visibility"),
				T.Node, T.Id);
			continue;
		}
		// TextAlign: 0x1 = по центру, 0x2 = вправо, иначе влево. Вертикаль (0x4) UTextBlock не держит —
		//   её учла геометрия слота в ассете (импортёр: +(h − 14)/2, строка 14 px).
		Tb->SetJustification((T.Align & 0x2) ? ETextJustify::Right
		                   : ((T.Align & 0x1) ? ETextJustify::Center : ETextJustify::Left));
		// Ширина переноса = ширина слота узла в ассете (= авторская ширина контрола в JUI).
		// ★ 15.09 (хвост ревизии): нет слота канваса или ширина ≤ 0 — поведение прежнее (WrapTextAt 0 при
		//   AutoWrapText: перенос только по выделенной виджету геометрии, ширина из ассета не применена),
		//   но теперь НЕ МОЛЧА — предупреждение с именем узла (разметка узла в ассете разошлась с шагом).
		const UCanvasPanelSlot* CS = AsdaUi::SlotOf(Tb);
		const float WrapW = CS ? (float)CS->GetOffsets().Right : 0.f;
		if (WrapW <= 0.f)
		{
			UE_LOG(LogAsdaChatWin, Warning,
				TEXT("[чат-окно] справка 2580: узел %s (контрол %d) — слот канваса %s, ширина %.1f: ширина переноса из ассета не применена (WrapTextAt 0)"),
				T.Node, T.Id, CS ? TEXT("есть") : TEXT("НЕТ"), WrapW);
		}
		Tb->SetAutoWrapText(true);
		Tb->SetWrapTextAt(WrapW);
		// Шрифт — тот же, что у всего порта (Default-строка DT_AsdaRichStyle), кегль 8.
		//   ★ 2026-09-24: у заголовка 2581 эталонный флаг `CClass_SetField926(0x200000)` — это
		//   ПСЕВДО-жирный (второй проход глифа ±1 px), а НЕ Bold-гарнитура: жирного начертания в
		//   оригинале нет вовсе, атлас собран весом 400 (разбор — `AsdaUi::ApplyPseudoBold`).
		//   Гарнитуру не трогаем, ставим второй проход ПОСЛЕ цвета (он берётся из заливки).
		FSlateFontInfo F = TipFont;
		F.Size = (int32)kFontPx;
		Tb->SetFont(F);
		// ★ ЗАКОН: цвета JuiSkin/JUI заданы в sRGB, UE ждёт linear.
		Tb->SetColorAndOpacity(FSlateColor(AsdaSrgbHexToLinear(T.Color)));
		if (T.bBold) { AsdaUi::ApplyPseudoBold(Tb); }
		FString Body = Txt ? Txt->GetTextById(T.LocId) : FString();
		// ★ Разворот «\n»/«\N» — ПОРТ `CControl_SetText` (`RE/etalon/ui_core.c:110523-110558`), которым эталон
		//   кладёт loc 5400…5406 в контролы: один проход слева направо; «\» + 'n' (110) или 'N' (78) → байт 10,
		//   второй символ удаляется, проход идёт дальше со следующего символа; «\» последним символом — конец.
		//   Это эквивалент `CControl_SetText` для текста, пришедшего НЕ через `StripTags`. Сейчас строка приходит
		//   через `GetTextById` → `FAsdaText::Loc` → `FAsdaMarkup::StripTags` (AsdaMarkup.cpp:251), который обе
		//   формы уже развернул, поэтому на этом пути цикл ничего не меняет (в 5400…5406 только «\n», «\t»/«\r»
		//   нет — GameText.csv). До 15.09 здесь была замена только «\n».
		for (int32 i = 0; i + 1 < Body.Len(); ++i)
		{
			if (Body[i] == TEXT('\\') && (Body[i + 1] == TEXT('n') || Body[i + 1] == TEXT('N')))
			{
				Body[i] = TEXT('\n');
				Body.RemoveAt(i + 1);
			}
		}
		Tb->SetText(FText::FromString(Body));
		// Показ — как у эталона: явный `CControl_SetVisible(2585, 1)` есть только у 2585 (chat.c:1265);
		//   остальные видимы по JUI `Visible = "1"`, это исходная видимость узлов в ассете.
		if (T.Id == 2585)
		{
			Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		QueTipTexts.Add(Tb);
	}
	// ★ ЗОНА ПЕРЕТАСКИВАНИЯ окна справки — ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА по требованию юзера
	//   (в оригинале 2580 не двигается: у него нет ни «ручки» вроде 2534, ни обработчика).
	//   Шапка окна шириной 250 (276 − 26, правее сидит крестик 2589), высотой 24; кисть
	//   прозрачная — ничего не рисует, только ловит мышь (как ручка растягивания чата).
	if (UBorder* Grip = ChatWidget->WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), FName(TEXT("CHAT_NEW_QueTip_Move"))))
	{
		Grip->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		Grip->SetPadding(FMargin(0.f));
		Grip->SetVisibility(ESlateVisibility::Visible);
		Grip->SetCursor(EMouseCursor::CardinalCross);
		if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(Grip))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetOffsets(FMargin(0.f, 0.f, kTipHeaderW, kTipHeaderH));
			CS->SetZOrder(20);
		}
		QueTipMoveSrc = Grip;
	}

	bQueTipTextsBuilt = true;
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] справка 2580: loc 5400-5406 записаны в %d из 7 узлов ассета TXT00…06, цвета из EXS TextColor, «\\n» развёрнуты, кегль %d, шапка-перетаскивание %.0f×%.0f"),
		QueTipTexts.Num(), (int32)kFontPx, kTipHeaderW, kTipHeaderH);
}

// Перетаскивание окна справки 2580 (отступление по требованию юзера). Клампим так, чтобы окно
//   целиком оставалось в базовом экране 1024×768; зона блокировки клика считается по ЖИВОЙ
//   геометрии группы, поэтому едет за окном сама.
void UAsdaChatWindowSubsystem::ApplyQueTipOffset(FVector2D NewOffset)
{
	const float StartX = kTipX + AsdaJui::kBaseW * kTipStartShiftFrac;
	NewOffset.X = FMath::Clamp(NewOffset.X, -StartX, AsdaJui::kBaseW - StartX - kTipW);
	NewOffset.Y = FMath::Clamp(NewOffset.Y, -kTipY, AsdaJui::kBaseH - kTipY - kTipH);
	QueTipOffset = NewOffset;
	RefreshLayout();
}

// ═══════════ КОМБОБОКС ВЫБОРА КАНАЛА 2608 (CHAT_NEW_SSbtn) ═══════════════════════════════════
// ★★★ Эталон ПОКАЗЫВАЕТ его — `chatInitColorPalette @0xB9E110` (chat.c:714-733):
//     chatSetupWin = FindControlByID(2608);
//     CControl_SetVisible(chatSetupWin, 1);            // ← вот это прошлый разбор пропустил
//     uiCtrlReleaseChildAndResize(chatSetupWin);       // очистить список
//     список = loc {5332, 5331, 5336, 5334, 5333, 5335} + loc 1465 «Прочие»   // 7 пунктов
//     CControl_SetScrollPos(chatSetupWin, 7); CControl_SetSelectedItem(chatSetupWin, …);
// Выбор пункта — `uiChatWindowMsgProc` (ui_core.c:12588), msgCode 0x11201, ctrlId 2608:
//     if (wparam < 6) uiQuestInput_UpdatePromptByState(pThis, channelOrder[wparam]);
//     channelOrder[6] = { 4, 3, 8, 6, 5, 7 }   // индекс пункта → номер канала
//   то есть ОБЩИЙ→4, ГЛОБАЛЬНЫЙ→3, ДРУГ→8, КЛАН→6, ОТРЯД→5, ШЕПОТ→7. Седьмой пункт «Прочие»
//   (индекс 6) действия НЕ вызывает — условие wparam < 6 его отсекает.
// ★★★ ВИД 1:1 ПО СКИНАМ (живой тест 2026-08-05: «список самосделанный, текст чёрный»).
//
// Скин кнопки — `Bk_ComboBox` (JuiSkin.ini):
//     Type = "ComboBox";  ListBoxName = "DarkGrey_List"
//     ES=0 (тело)   Rect = "0 0 89 20"   CaptionPoint = "5 4"
//                   CaptionColor: SS0 FFFFFFFF (обычное) / SS1 FFE9D6BC (наведение) / SS2 FFE9D6BC
//     ES=1 (стрелка)Rect = "90 0 110 20" ⇒ РОВНО 20×20, UV квадратный
//                   (0.785,0.520)-(0.824,0.559) на N_02.tga; при нажатии UV сдвигается на
//                   (0.834,0.520)-(0.873,0.559) — второй кадр стрелки.
//   ★ ПОЧЕМУ СТРЕЛКА БЫЛА СЖАТА: полная ширина скина 110, а контрол 2608 в JUI — 100. Импортёр
//   масштабирует ВСЕ планы пропорционально (100/110 = 0.909), поэтому стрелка приезжала
//   18.2×20 вместо 20×20 — при квадратной UV это видно сразу («сплюснутая»). Правильно (и так
//   делает движок для ComboBox): стрелка кладётся СВОИМ размером к ПРАВОМУ краю контрола, а
//   растягивается только тело. ⇒ тело (28,726,80×20), стрелка (108,726,20×20).
//
// Скин раскрытого списка — `DarkGrey_List` (JuiSkin.ini):
//     Type = "ListBox";  ScrollBarFileNameVert = "Nomal_Scr_Ot50"
//     ES=0 SS=0  Rect = "0 0 50 20"  (⇒ высота строки 20)
//                ImageFileName = "N_02.tga",  Diffuse = FFFFFFFF,
//                UV = (0.285,0.816)-(0.371,0.840)      ← фон списка
//                CaptionColor = "FFE9D6BC"             ← цвет текста пунктов
//   Планов выделения/наведения в скине НЕТ (единственный ES, единственный SS) ⇒ подсветку
//   строки берём из ЕДИНСТВЕННОЙ пары цветов, которая в этой группе скинов есть: обычный пункт
//   FFE9D6BC, наведённый/выбранный FFFFFFFF (это ровно пара SS0/SS1 подписи Bk_ComboBox).
//   Ничего сверх данных скина не рисуем.
//
// ПУТЬ РЕАЛИЗАЦИИ. `UComboBoxString` отпадает: раскрывающуюся часть он строит через
//   SMenuAnchor + SListView движковым стилем (белая плашка, чёрный текст), и кисти меню
//   (`WidgetStyle.MenuBorderBrush`) читаются на КОНСТРУИРОВАНИИ SComboBox — после AddToViewport
//   их уже не подменить. Поэтому виджет `CHAT_NEW_SSbtn` гасим и собираем список из тех же
//   примитивов, из которых игра его и рисует: плита с UV-вырезкой из атласа + строки текста.
void UAsdaChatWindowSubsystem::BuildChannelCombo()
{
	if (bChannelComboBuilt || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;

	// ★ СОСТАВ СПИСКА. Эталонные семь пунктов (chat.c:719-731) + «широкие» каналы —
	//   ОТСТУПЛЕНИЕ по требованию юзера. В оригинале широкие режимы доступны ТОЛЬКО
	//   слэш-командами (алиасы A/J/B, chat.c:1616/1625/1634) — в комбобоксе их нет вовсе.
	//   Подписи берём из приглашений loc 5317/5318.
	//   Плюс КАНАЛ АРМИИ (фракции) — состояние 9, loc 5337 «[Армия]», цвет слота 9.
	//   ★ Пункт «Прочие» (loc 1465) УБРАН по требованию юзера — он и в эталоне
	//   ничего не делает (условие wparam < 6 его отсекает).
	//   ★★★ ПУНКТ «По всем каналам» (слот 0, loc 5316) УБРАН — РЕШЕНИЕ ЮЗЕРА 2026-08-05.
	//   Серверный патч на широкие каналы будет сделан ТОЛЬКО под «Поиск отряда» (1) и
	//   «Торговлю» (2), поэтому канал 0 из списка ОТПРАВКИ исключён. Это заодно ПРИБЛИЖАЕТ
	//   нас к эталону (там слота 0 в комбобоксе тоже не было).
	//   ⚠ НЕ ПУТАТЬ с ВКЛАДКОЙ-ФИЛЬТРОМ № 6 «По всем каналам» (маска 903) в панели вкладок
	//   2501 — она ОСТАЁТСЯ: это фильтр ленты (какие слоты показывать), а не канал отправки.
	//   Слот 0 по-прежнему достижим слэш-командой «/A» (эталон chat.c:1616) — см. ParseSlashCommand.
	ComboLoc   = { 5332, 5331, 5336, 5334, 5333, 5335, 5317, 5318, 5337 };
	ComboSlots = {    4,    3,    8,    6,    5,    7,    1,    2,    9 };
	// ★ ГЕОМЕТРИЯ списка считается от kComboItems, а состав — от этих таблиц. Раньше при правке
	//   состава легко было забыть константу (список раскрывался бы с пустой полосой или обрезал
	//   последний пункт). Держим сцепку жёстко и шумно.
	checkf(ComboLoc.Num() == ComboSlots.Num() && ComboSlots.Num() == kComboItems,
		TEXT("Комбобокс 2608: ComboLoc=%d, ComboSlots=%d, kComboItems=%d — обновите kComboItems"),
		ComboLoc.Num(), ComboSlots.Num(), kComboItems);

	// Движковый комбобокс не рисуем вовсе (см. шапку) — он остаётся в дереве для редактора.
	// ★ Перенесено в ассет (15.09, разрешение владельца): CHAT_NEW_SSbtn свёрнут в самом WBP
	//   (JUI Visible=0 → Collapsed), код его больше не гасит.

	// Подпись выбранного канала. ЭТАЛОН рисует её от CaptionPoint (5,4) угла контрола, цвет SS0
	//   = FFFFFFFF, общим кеглем. ★ ОТСТУПЛЕНИЕ (требование юзера 2026-08-06): кегль на 1 меньше
	//   (kComboLabelFontPx) и подпись центрируется по ТЕЛУ комбобокса по обеим осям —
	//   фактическая геометрия ставится в RefreshChannelCombo (там же, где ApplyFormatted, иначе
	//   подменённый виджет унёс бы раскладку с собой).
	if (UTextBlock* Lb = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(TEXT("CHAT_NEW_SSbtn_Label"))))
	{
		Lb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Lb->SetAutoWrapText(false);
		Lb->SetJustification(ETextJustify::Center);
		FSlateFontInfo F = TipFont;
		F.Size = (int32)kComboLabelFontPx;
		Lb->SetFont(F);
		Lb->SetColorAndOpacity(FSlateColor(AsdaSrgbHexToLinear(TEXT("FFFFFFFF"))));
		// ★★★ ПОДПИСЬ ЖИВЁТ В ТОЙ ЖЕ ГРУППЕ, ЧТО И ТЕЛО КОМБОБОКСА (`SizeWin_grp`).
		//   Тело `CHAT_NEW_SSbtn_29_0` — ЛОКАЛЬНЫЙ ребёнок этой группы, а группу `BuildLayoutTable`
		//   кладёт в `T + H − 170`, то есть её Y зависит от высоты окна. Подпись раньше висела на
		//   КОРНЕВОМ канвасе по абсолютному `kComboY` — совпадало только при нетронутой раскладке;
		//   после кнопки 2605 / растягивания тело уезжало, а подпись оставалась ⇒ «съезжала»
		//   (живой тест 2026-08-06). В одной группе они двигаются ВМЕСТЕ и без пересчёта.
		UPanelWidget* ComboHost = Cast<UPanelWidget>(ChatWidget->GetWidgetFromName(TEXT("SizeWin_grp")));
		UPanelWidget* LabelHost = ComboHost ? ComboHost : Cast<UPanelWidget>(Root);
		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(LabelHost->AddChild(Lb)))
		{
			ComboLabel = Lb;                 // ApplyComboLabelSlot смотрит на родителя подписи
			ApplyComboLabelSlot(CS);
			CS->SetZOrder(70);
		}
		ComboLabel = Lb;
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-каналы] подпись 2608 положена в «%s» (тело комбобокса живёт там же)"),
			ComboHost ? TEXT("SizeWin_grp") : TEXT("корневой канвас — группы SizeWin_grp нет"));
	}

	// Раскрывающаяся панель. Открывается ВВЕРХ: снизу места нет (низ комбобокса 746, семь строк
	//   по 20 = 140 ⇒ 886 > 768 базовой высоты экрана), поэтому список встаёт НАД кнопкой.
	UCanvasPanel* List = ChatWidget->WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), FName(TEXT("CHAT_NEW_SSbtn_List")));
	if (!List) { return; }
	if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(List))
	{
		AsdaJui::SetChatSlot(CS, kComboX, kComboY - kComboRowH * (float)kComboItems, kComboW, kComboRowH * (float)kComboItems);
		CS->SetZOrder(kComboListZ);
	}
	List->SetVisibility(ESlateVisibility::Collapsed);
	ComboList = List;

	// Фон списка — плита из атласа N_02 с UV-вырезкой скина DarkGrey_List.
	// ★ Аудит №3 (A8/B8): атлас JUI берётся у менеджера текстур (кэш + refCount, закон
	//   `resource-subsystems`) — прямой `LoadObject` шёл мимо и грузил атлас заново.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	if (UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(TEXT("n_02"))) : nullptr)
	{
		if (UImage* Bg = ChatWidget->WidgetTree->ConstructWidget<UImage>(
				UImage::StaticClass(), FName(TEXT("CHAT_NEW_SSbtn_List_bg"))))
		{
			FSlateBrush Br;
			Br.SetResourceObject(Tex);
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(kComboW, kComboRowH * (float)kComboItems);
			Br.SetUVRegion(FBox2f(FVector2f(0.285f, 0.816f), FVector2f(0.371f, 0.840f)));
			Bg->SetBrush(Br);
			Bg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* BS = List->AddChildToCanvas(Bg))
			{
				BS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
				BS->SetOffsets(FMargin(0.f));
				BS->SetZOrder(0);
			}
		}
	}

	// ★★★ ПОДСВЕТКА ПУНКТА — ПЛИТА ПОД ТЕКСТОМ, ЭТО ДВИЖОК, А НЕ СКИН (живой тест 2026-08-05:
	//   «нет эффектов hover и click»). Прошлый вывод «планов выделения в скине DarkGrey_List нет»
	//   ВЕРЕН (там единственный ES=0/SS=0), но неполон: подсветку рисует не скин, а РЕНДЕР СПИСКА.
	//   `ui_core.c:112969` (тело отрисовки строк CListBox):
	//
	//       if ( (i == *(this + 327) || i == *(this + 326)) && !*((_BYTE *)this + 1320) )
	//       {
	//         if ( i == *(this + 326) )                       // +326 = индекс ПОД КУРСОРОМ
	//           SetRenderState(dev, 60, -1762594576);         // 60 = D3DRS_TEXTUREFACTOR
	//         if ( i == *(this + 327) )                       // +327 = ВЫБРАННЫЙ индекс
	//           SetRenderState(dev, 60, ~*(_DWORD *)(item + 2760));
	//         … MakeVertex4(...) — КВАД НА ПРЯМОУГОЛЬНИК СТРОКИ …
	//       }
	//
	//   Роли полей подтверждаются соседним кодом: ctor ставит оба в −1 (ui_core.c:111211-111212),
	//   `uiList_RemoveItemAt` правит оба при удалении (111717-111720), hover пишется по движению
	//   мыши через хит-тест (`*(this + 326) = uiListHitTestItemY(...)`, 112089), выбранный — по
	//   клику (112109). Флаг +1320 — «подсветку не рисовать».
	//
	//   ЦИФРЫ (и это ровно то, чего у нас не было):
	//     • HOVER = **−1762594576 = 0x96F0F0F0** ⇒ ARGB: альфа 0x96 (150/255 ≈ 59 %), цвет F0F0F0
	//       (почти белый) — полупрозрачная светлая плита под строкой;
	//     • ВЫБРАННЫЙ/НАЖАТЫЙ = **побитовая инверсия** второго цвета элемента (поле +2760).
	//       Оно заполняется 4-м аргументом `uiChatLogAddLine` (ui_core.c:111664: `if (a4) …+690 = a4`),
	//       а список каналов заводится вызовом `CClass_DrawText(chatSetupWin, текст, −1, 0)`
	//       (chat.c:728/731) — то есть a4 = **0**, поле остаётся нулём, и `~0 = 0xFFFFFFFF`
	//       ⇒ СПЛОШНАЯ БЕЛАЯ плита на выбранной строке. Обе константы вынесены рядом.
	//   Порядок: плита z=1 (под текстом), текст z=2 — квад рисуется до текста строки.
	ComboRows.Reset();
	ComboRowBgs.Reset();
	// ★ Аудит №3 (A8): белый пиксель — НЕ процедурная текстура, а обычный ассет движка
	//   (`/Engine/EngineResources/WhiteSquareTexture`), поэтому менеджер подходит: `LoadTextureCached`
	//   принимает любой объектный путь UE и даёт тот же кэш и refCount, что и атласам JUI.
	UTexture2D* WhiteTex = TexMgr ? TexMgr->LoadTextureCached(
		TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")) : nullptr;
	for (int32 i = 0; i < kComboItems; ++i)
	{
		// Плита подсветки строки — ровно прямоугольник строки (kComboW × kComboRowH).
		UImage* Bg = ChatWidget->WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), FName(*FString::Printf(TEXT("CHAT_NEW_SSbtn_ItemBg%d"), i)));
		if (Bg)
		{
			FSlateBrush Br;
			if (WhiteTex) { Br.SetResourceObject(WhiteTex); }
			Br.DrawAs = ESlateBrushDrawType::Image;
			Br.ImageSize = FVector2D(kComboW, kComboRowH);
			Bg->SetBrush(Br);
			Bg->SetVisibility(ESlateVisibility::Collapsed);   // мышь ловит СТРОКА, не плита
			if (UCanvasPanelSlot* BS = List->AddChildToCanvas(Bg))
			{
				BS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
				BS->SetAlignment(FVector2D(0.f, 0.f));
				BS->SetOffsets(FMargin(0.f, kComboRowH * i, kComboW, kComboRowH));
				BS->SetZOrder(1);
			}
		}
		ComboRowBgs.Add(Bg);

		UTextBlock* Row = ChatWidget->WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), FName(*FString::Printf(TEXT("CHAT_NEW_SSbtn_Item%d"), i)));
		if (!Row) { continue; }
		Row->SetAutoWrapText(false);
		Row->SetVisibility(ESlateVisibility::Visible);   // строка ловит мышь (хит-тест по геометрии)
		Row->SetCursor(EMouseCursor::Hand);
		if (UCanvasPanelSlot* RS = List->AddChildToCanvas(Row))
		{
			RS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			RS->SetAlignment(FVector2D(0.f, 0.f));
			RS->SetOffsets(FMargin(5.f, kComboRowH * i + 4.f, kComboW - 10.f, kComboRowH));
			RS->SetZOrder(2);
		}
		ComboRows.Add(Row);
	}
	RefreshChannelCombo(-1);
	SetChannelListOpen(false);

	bChannelComboBuilt = true;
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-окно] комбобокс 2608 собран по скинам: тело %.0f×%.0f, стрелка %.0f×%.0f (native), ")
		TEXT("список %.0f×%.0f вверх, 7 пунктов, текст FFE9D6BC/наведение FFFFFFFF"),
		kComboW - kComboArrowW, kComboH, kComboArrowW, kComboH, kComboW, kComboRowH * 7.f);
}

// ★★★ ЦЕНТРОВКА ПОДПИСИ ВЫБРАННОГО КАНАЛА ПО ТЕЛУ КОМБОБОКСА — ОДНА ТОЧКА ИСТИНЫ.
//   Раньше центровка считалась «по замеру высоты строки» и жила только в RefreshChannelCombo —
//   любой другой путь обновления подписи её терял. Теперь геометрию ставит ЭТА функция, и её
//   зовут ВСЕ пути (постройка и каждое обновление).
//
//   Тело комбобокса 2608 = (kComboX, kComboY, kComboW − kComboArrowW, kComboH) = (28,726,80×20):
//   стрелка ES=1 скина Bk_ComboBox занимает правые 20 px и в область подписи не входит.
//
//   ★ ПРИЁМ: слот AUTO-SIZE + выравнивание (0.5, 0.5) в ЦЕНТР тела. Тогда слот всегда равен
//   desired-size самого текста, а центр текста совпадает с центром области ПО ОБЕИМ ОСЯМ —
//   без замеров шрифта и независимо от кегля, языка и длины строки.
//   ★ ЗАКОН jui-align-anchors СОБЛЮДЁН: анкор остаётся POINT-схемой импортёра (fx=0 левый край,
//   fy=1 нижний край), меняются только Alignment и AutoSize — то есть на других разрешениях
//   подпись ведёт себя ровно как остальные узлы чат-колонки.
void UAsdaChatWindowSubsystem::ApplyComboLabelSlot(UCanvasPanelSlot* CS) const
{
	if (!CS) { return; }
	// Родитель подписи решает, в каких координатах считать центр: ЛОКАЛЬНЫХ (внутри `SizeWin_grp`,
	//   где живёт и тело комбобокса) или экранных (фолбэк, если группы в окне нет).
	UWidget* Lb = ComboLabel.Get();
	const bool bInGroup = Lb && Lb->GetParent()
	                   && Lb->GetParent()->GetName().Equals(TEXT("SizeWin_grp"), ESearchCase::CaseSensitive);
	if (bInGroup)
	{
		// Локальные координаты тела в группе — те же числа, что кладёт BuildLayoutTable
		//   (`CHAT_NEW_SSbtn_29_0`: X = kComboX − 1, Y = kSizeWinComboLocalY, W = тело, H = kComboH).
		const float CX = (kComboX - 1.f) + (kComboW - kComboArrowW) * 0.5f;
		const float CY = kSizeWinComboLocalY + kComboH * 0.5f;
		CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		CS->SetAlignment(FVector2D(0.5f, 0.5f));
		CS->SetAutoSize(true);
		CS->SetOffsets(FMargin(CX, CY, 0.f, 0.f));
		return;
	}
	const float CX = kComboX + (kComboW - kComboArrowW) * 0.5f;   // центр тела по X
	const float CY = ComboBodyAbsY() + kComboH * 0.5f;            // центр тела по Y (по ЖИВОЙ раскладке)
	CS->SetAnchors(FAnchors(AsdaJui::kFxLeft, AsdaJui::kFyBottom, AsdaJui::kFxLeft, AsdaJui::kFyBottom));
	CS->SetAlignment(FVector2D(0.5f, 0.5f));
	CS->SetAutoSize(true);
	CS->SetOffsets(FMargin(CX - AsdaJui::kFxLeft * AsdaJui::kBaseW,
	                       CY - AsdaJui::kFyBottom * AsdaJui::kBaseH, 0.f, 0.f));
}

// ★★★ ЕДИНАЯ ТОЧКА ГЕОМЕТРИИ КОМБОБОКСА 2608: подпись + раскрытый список.
//   Зовётся ИЗ ВСЕХ путей: постройка (BuildChannelCombo), каждое обновление подписи
//   (RefreshChannelCombo) и КАЖДАЯ смена раскладки окна (ApplyWindowLayout — а через неё кнопка
//   2605, растягивание обеих кромок, восстановление размера, загрузка настроек, travel).
//   Именно отсутствие последнего пути и давало «подпись съезжает» на живом тесте 2026-08-06.
void UAsdaChatWindowSubsystem::ApplyComboGeometry()
{
	if (UTextBlock* Lb = Cast<UTextBlock>(ComboLabel.Get()))
	{
		if (UCanvasPanelSlot* LS = AsdaUi::SlotOf(Lb))
		{
			ApplyComboLabelSlot(LS);
			LS->SetZOrder(70);
		}
	}
	if (ComboList)
	{
		if (UCanvasPanelSlot* LS = AsdaUi::SlotOf(ComboList))
		{
			const float ListH = kComboRowH * (float)FMath::Max(1, ComboVisibleCount);
			AsdaJui::SetChatSlot(LS, kComboX, ComboBodyAbsY() - ListH, kComboW, ListH);
		}
	}
}

// ★ ЖИВОЙ абсолютный Y тела комбобокса 2608 — ровно та же формула, что в `BuildLayoutTable`:
//   группа `SizeWin_grp` стоит в `T + H − kSizeWinDY`, тело внутри неё — в `kSizeWinComboLocalY`.
//   Нужен раскрывающемуся списку: он живёт на корневом канвасе (выше верхней кромки группы) и в
//   неё не помещается, поэтому его Y приходится считать.
float UAsdaChatWindowSubsystem::ComboBodyAbsY() const
{
	return BaseTop() + BaseHeight() - kSizeWinDY + kSizeWinComboLocalY;
}

// Перерисовать подпись и строки списка (HoverIdx = строка под курсором, −1 = нет).
void UAsdaChatWindowSubsystem::RefreshChannelCombo(int32 HoverIdx)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	// ★ ПО ТРЕБОВАНИЮ ЮЗЕРА: у названий каналов снимаем квадратные скобки
	//   (в loc 5316..5323/5325/5337 они есть: «[Общий] » → «Общий»). Отступление.
	auto Strip = [](FString S)
	{
		S.TrimStartAndEndInline();
		S.ReplaceInline(TEXT("["), TEXT(""), ESearchCase::CaseSensitive);
		S.ReplaceInline(TEXT("]"), TEXT(""), ESearchCase::CaseSensitive);
		S.TrimStartAndEndInline();
		return S;
	};
	// ★ Названия каналов идут ЧЕРЕЗ UiChannelText — там живёт переименование «По всем каналам»
	//   → «Все каналы» (требование юзера 2026-08-06), одинаковое для всех мест вывода.
	auto LocName = [&](int32 i)
	{
		if (!ComboLoc.IsValidIndex(i)) { return FString(); }
		return Strip(UiChannelText(ComboLoc[i]));
	};
	// ★★★ ЖИВОЙ ТЕСТ 2026-08-06: «после ВЫБОРА канала подпись теряет центрирование».
	//   ПРИЧИНА — `UAsdaRichText::ApplyFormatted`, которым подпись обновлялась. При ПЕРВОМ вызове он
	//   заменяет `UTextBlock` на `URichTextBlock` (Д19.4), при КАЖДОМ следующем — идёт по ветке
	//   «уже rich» и там БЕЗУСЛОВНО делает `SetAutoWrapText(true)` + `SetWrapTextAt(ширина слота)`
	//   и `SetAutoSize(false)`, то есть переопределяет раскладку виджета ПОСЛЕ того, как мы её
	//   выставили. Плюс сам текст рисуется inline-виджетом декоратора, у которого своя метрика,
	//   поэтому выставленная «по замеру» высота слота переставала совпадать со строкой.
	//   ЛЕЧЕНИЕ (а не подпорка): подпись канала — ПРОСТОЙ ТЕКСТ (одна строка, один цвет, один
	//   кегль), разметка ей не нужна вовсе ⇒ ApplyFormatted здесь БОЛЬШЕ НЕ ИСПОЛЬЗУЕТСЯ.
	//   Обычный `UTextBlock` + `SetText` не подменяет виджет, слот не теряется, кегль 7 держится.
	if (UTextBlock* Lb = Cast<UTextBlock>(ComboLabel.Get()))
	{
		// Подпись на САМОЙ КНОПКЕ — ВСЕГДА БЕЛАЯ, независимо от канала:
		//   скин Bk_ComboBox задаёт CaptionColor SS0 = FFFFFFFF (наведение/нажатие — FFE9D6BC).
		//   Это эталон, менять не стали.
		Lb->SetText(FText::FromString(LocName(ComboIndex)));
		Lb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		Lb->SetAutoWrapText(false);
		Lb->SetJustification(ETextJustify::Center);
		// ★ Кегль на 1 меньше общего (требование юзера) — ставим КАЖДЫЙ раз: смена канала не
		//   должна его сбрасывать.
		FSlateFontInfo F = Lb->GetFont();
		if (F.Size != (int32)kComboLabelFontPx) { F.Size = (int32)kComboLabelFontPx; Lb->SetFont(F); }
		// Геометрия — ТОЛЬКО через единую точку (ApplyComboGeometry в конце функции).
	}
	// Счётчик ВИДИМЫХ пунктов: по нему раскладываются строки и считается высота плиты
	//   (см. разбор пустой последней строки ниже).
	int32 Vis = 0;
	for (int32 i = 0; i < ComboRows.Num(); ++i)
	{
		if (!ComboRows[i]) { continue; }
		// ★★ ЦВЕТ ПУНКТА = ЦВЕТ ЕГО КАНАЛА (требование юзера 2026-08-05).
		//   Источник — ТА ЖЕ таблица kChannels, что и у строк ленты (SlotColor), — копии нет.
		//   Номер слота = channelOrder[i] (он же номер режима ввода); седьмой пункт
		//   «Прочие» канала не имеет — остаётся белым.
		//   ★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА: в оригинале все семь пунктов белые —
		//   `CClass_DrawText(chatSetupWin, текст, -1, 0)` (chat.c:728/731) кладёт цвет −₁ = 0xFFFFFFFF
		//   в поле строки +2680 (`uiChatLogAddLine` ui_core.c:111661). Красим по просьбе юзера.
		const int32 Slot = ComboSlots.IsValidIndex(i) ? ComboSlots[i] : -1;
		// ★ ЖИВАЯ таблица каналов (ChanColor): перекрасили канал в палитре 2700 — пункт списка
		//   меняет цвет вместе с лентой и префиксом. Заводская SlotColor осталась дефолтом.
		const FColor Ch = (Slot >= 0) ? ChanColor(Slot) : FColor(255, 255, 255, 255);
		// Подсветка: отдельного плана выделения в скине DarkGrey_List НЕТ, поэтому
		//   сохраняем ЦВЕТ КАНАЛА, а состояние показываем яркостью: наведённый/
		//   выбранный — полная альфа FF, остальные — BF (75 %).
		const bool bHot = (i == HoverIdx) || (i == ComboIndex);
		const FString Col = FString::Printf(TEXT("%s%02X%02X%02X"),
			bHot ? TEXT("FF") : TEXT("BF"), Ch.R, Ch.G, Ch.B);
		// ★ КАНАЛ АРМИИ ДОСТУПЕН ТОЛЬКО В БИТВЕ АРМИЙ (требование юзера,
		//   симметрично эталону: в битве ЗАПРЕЩЁН «Общий», loc 5327). Признак битвы —
		//   тот же флаг `bChannelPrefixMode` (CChat+0x6A0), который включает `campWarWnd_ToggleVisible`
		//   и пакеты входа в зону осады (network.c:43115/43119) — когда кампвар придёт,
		//   пункт появится САМ, без правок.
		if (Slot == 9 && !bChannelPrefixMode)
		{
			ComboRows[i]->SetVisibility(ESlateVisibility::Collapsed);
			if (ComboRowBgs.IsValidIndex(i) && ComboRowBgs[i])
			{
				ComboRowBgs[i]->SetVisibility(ESlateVisibility::Collapsed);
			}
			continue;
		}
		// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «самый последний элемент списка — пустое место».
		//   ПРИЧИНА (разобрана, а не отрезана): это НЕ эталонный пункт «Прочие» (loc 1465) —
		//   его мы убрали ещё раньше по требованию юзера. Пустая строка — ПОСЛЕДСТВИЕ
		//   ФИКСИРОВАННОЙ РАСКЛАДКИ: и высота плиты (`kComboRowH × kComboItems`), и Y каждой
		//   строки считались по ИНДЕКСУ В ТАБЛИЦЕ, а последний пункт — «Армия» (слот 9) —
		//   вне битвы армий `Collapsed`. Плита оставалась высотой на ВСЕ пункты, и снизу
		//   зияли ровно 20 px пустоты. (Эталону такая беда не грозит: там список наполняется
		//   через `uiCtrlReleaseChildAndResize` + `CControl_SetScrollPos(chatSetupWin, 7)` —
		//   контрол ЗНАЕТ фактическое число элементов, chat.c:718-732.)
		//   ⇒ Раскладываем ВИДИМЫЕ строки подряд, а высоту плиты пересчитываем по их числу.
		ComboRows[i]->SetVisibility(ESlateVisibility::Visible);
		// Плита подсветки: hover 0x96F0F0F0, выбранный ~0 = 0xFFFFFFFF (см. разбор в BuildChannelCombo).
		if (ComboRowBgs.IsValidIndex(i) && ComboRowBgs[i])
		{
			UImage* Bg = ComboRowBgs[i];
			if (UCanvasPanelSlot* BS = AsdaUi::SlotOf(Bg))
			{
				BS->SetOffsets(FMargin(0.f, kComboRowH * (float)Vis, kComboW, kComboRowH));
			}
			const bool bSel = (i == ComboIndex);
			const bool bHov = (i == HoverIdx);
			if (bSel || bHov)
			{
				// Приоритет ВЫБРАННОГО: в эталоне обе ветки `if` идут подряд, и вторая (выбранный)
				//   перезаписывает TEXTUREFACTOR, выставленный первой (наведение).
				const FColor C = bSel ? kComboSelColor : kComboHoverColor;
				// ★ TEXTUREFACTOR оригинала — цвет УМНОЖИТЕЛЬ, не sRGB-текстура: гамма-коррекцию
				//   здесь применять НЕЛЬЗЯ (иначе полупрозрачная плита уедет в белый).
				Bg->SetColorAndOpacity(FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f));
				Bg->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				Bg->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (UWidget* New = UAsdaRichText::ApplyFormatted(ComboRows[i],
				FString::Printf(TEXT("<FS%02d><FC=%s>%s"), (int32)kFontPx, *Col, *LocName(i))))
		{
			ComboRows[i] = New;
			New->SetVisibility(ESlateVisibility::Visible);   // строка ловит мышь (хит-тест по геометрии)
			New->SetCursor(EMouseCursor::Hand);
		}
		// ★ Позицию ставим ПОСЛЕ ApplyFormatted: он может ПОДМЕНИТЬ виджет (у нового — свой слот),
		//   и раскладка, выставленная до вызова, потерялась бы вместе со старым виджетом.
		if (UCanvasPanelSlot* RS = AsdaUi::SlotOf(ComboRows[i]))
		{
			RS->SetOffsets(FMargin(5.f, kComboRowH * (float)Vis + 4.f, kComboW - 10.f, kComboRowH));
			RS->SetZOrder(2);
		}
		++Vis;
	}
	// Высота раскрытой плиты и её фона — по ФАКТИЧЕСКОМУ числу видимых пунктов.
	ComboVisibleCount = Vis;
	const float ListH = kComboRowH * (float)FMath::Max(1, Vis);
	// ★ Геометрия подписи и плиты списка — ЕДИНОЙ функцией (она же зовётся из ApplyWindowLayout).
	ApplyComboGeometry();
	if (ComboList)
	{
		if (UImage* Bg = Cast<UImage>(ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_List_bg"))))
		{
			FSlateBrush Br = Bg->GetBrush();
			Br.ImageSize = FVector2D(kComboW, ListH);
			Bg->SetBrush(Br);
		}
	}
	UE_LOG(LogAsdaChatWin, Verbose,
		TEXT("[чат-каналы] список: пунктов в таблице %d, видимых %d, высота плиты %.0f (пустых строк нет)"),
		ComboSlots.Num(), Vis, ListH);
}

// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «список каналов уходит ПОД окно системного журнала».
//   Причина найдена и она принципиальная: список — ребёнок корневого виджета чата, журнал 2570 —
//   ДРУГОЙ корневой виджет. `UCanvasPanelSlot::SetZOrder(9000)` управляет порядком ТОЛЬКО внутри
//   своего родителя-канваса и на спор двух корневых виджетов не влияет НИКАК; решает только
//   z-order слота `AddToViewport`. Чат стоял на 689, журнал на 690 ⇒ журнал всегда сверху.
//   Чиним ОБЩИМ механизмом оконного менеджера (он же понадобится справке «?» и меню 2670/80000):
//   на время раскрытия список объявляется «слоем поверх всего» — корневой виджет чата уезжает на
//   z = 9000+, а прямоугольник списка регистрируется отдельной зоной интерфейса, чтобы клик по
//   нему не проваливался ни в мир, ни в журнал.
void UAsdaChatWindowSubsystem::SetChannelListOpen(bool bOpen)
{
	bComboOpen = bOpen;
	if (ComboList)
	{
		ComboList->SetVisibility(bOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>())
		{
			if (bOpen)
			{
				FAsdaUiZoneRect D;
				D.BindWeakLambda(this, [this](FVector2D& Mn, FVector2D& Mx) -> bool
				{
					return GetComboListScreenRect(Mn, Mx);
				});
				WM->PushTopmost(kComboClaimId, kZoneId, MoveTemp(D));
			}
			else
			{
				WM->PopTopmost(kComboClaimId);
			}
			FVector2D Mn, Mx;
			const bool bR = GetComboListScreenRect(Mn, Mx);
			UE_LOG(LogAsdaChatWin, Log,
				TEXT("[чат-каналы] список %s; слой поверх всего=%s; rect=%s(%.0f,%.0f)-(%.0f,%.0f)"),
				bOpen ? TEXT("РАСКРЫТ") : TEXT("свёрнут"),
				WM->IsTopmostActive(kComboClaimId) ? TEXT("ДА") : TEXT("нет"),
				bR ? TEXT("") : TEXT("НЕТ "), Mn.X, Mn.Y, Mx.X, Mx.Y);
		}
	}
	// Второй кадр стрелки (UV скина SS2) в раскрытом состоянии — план из WBP, подменяем только UV.
	if (UImage* Arrow = Cast<UImage>(ChatWidget ? ChatWidget->GetWidgetFromName(TEXT("CHAT_NEW_SSbtn_29_1")) : nullptr))
	{
		FSlateBrush Br = Arrow->GetBrush();
		Br.SetUVRegion(bOpen ? FBox2f(FVector2f(0.834f, 0.520f), FVector2f(0.873f, 0.559f))
		                     : FBox2f(FVector2f(0.785f, 0.520f), FVector2f(0.824f, 0.559f)));
		Arrow->SetBrush(Br);
	}
}

// Выбор пункта: `uiChatWindowMsgProc` (ui_core.c:12588) — if (wparam < 6)
//   uiQuestInput_UpdatePromptByState(this, channelOrder[wparam]); channelOrder = {4,3,8,6,5,7}.
void UAsdaChatWindowSubsystem::SelectChannelIndex(int32 Idx)
{
	// ★ 30.10.2026: эталон `Chat_SwitchChannel` (chat.c:4702) гасит плашку подсказки (`CControl_KillFocus(&g_ItemTooltip)`).
	HelpTip.Hide(GetGameInstance(), EAsdaTipOwner::ChatWin);
	// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «Торговля и Поиск отряда не выбираются, остальные работают».
	//   Причина — ЗДЕСЬ. Стояла эталонная отсечка `Idx > 6` (в оригинале обработчик 2608 делает
	//   `if (wparam < 6)` на СЕМЬ пунктов), а список у нас РАСШИРЕН до десяти: индексы
	//   6 «По всем каналам» (слот 0), 7 «Поиск отряда» (слот 1), 8 «Торговля» (слот 2),
	//   9 «Армия» (слот 9). Пункт 6 проскакивал (6 не > 6), а 7/8/9 молча отбрасывались —
	//   ровно то, что видел юзер. Отсечка теперь по ФАКТИЧЕСКОЙ длине таблицы ComboSlots:
	//   добавление пункта больше не требует правки границы.
	if (!ComboSlots.IsValidIndex(Idx))
	{
		UE_LOG(LogAsdaChatWin, Warning,
			TEXT("[чат-каналы] выбран пункт %d, но в таблице каналов их %d — игнор"), Idx, ComboSlots.Num());
		return;
	}
	ComboIndex = Idx;
	// ★★★ ЖИВОЙ ТЕСТ 2026-08-05: «всё уходит в общий». Причина была ЗДЕСЬ: выбор
	//   пункта менял ТОЛЬКО `SelectedChannel`, а `SendCurrentChannel` маршрутизирует по
	//   `InputState`, который оставался 4 («Общий», опкод 5084). Теперь пункт сразу
	//   ставит РЕЖИМ ВВОДА — ровно как `uiQuestInput_UpdatePromptByState(this, channelOrder[wparam])`.
	if (Idx < ComboSlots.Num() && ComboSlots[Idx] >= 0)
	{
		SetInputState(ComboSlots[Idx]);
	}
	else
	{
		// Пункт «Прочие» (loc 1465): эталон действия не выполняет (условие wparam < 6).
		UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] выбран пункт «Прочие» — эталон действия не выполняет"));
	}
	RefreshChannelCombo(-1);
}

// ══════════════════════════════ ПРОКРУТКА ═════════════════════════════════════════════════════

float UAsdaChatWindowSubsystem::MeasureLineHeight() const
{
	if (!LinesBox || LinesBox->GetChildrenCount() == 0) { return 12.f; }
	if (UWidget* First = LinesBox->GetChildAt(0))
	{
		const FVector2D D = First->GetDesiredSize();
		if (D.Y > 1.f) { return (float)D.Y; }
	}
	return 12.f;
}

void UAsdaChatWindowSubsystem::ScrollByLines(int32 Lines)
{
	if (!LinesBox) { return; }
	const float H = MeasureLineHeight();
	const float Want = FMath::Clamp(LinesBox->GetScrollOffset() + Lines * H,
		0.f, LinesBox->GetScrollOffsetOfEnd());
	LinesBox->SetScrollOffset(Want);
}

// ══════════ ★★★ КОЛЕСО НАД ОКНОМ ЧАТА 2500: лента 2521 и ряд вкладок 2501 ═══════════════════════
// РАЗБОР ЭТАЛОНА (что доказано кодом, а что нет — см. заголовок).
//   ДОКАЗАНО: колесо в оригинале ПОПАДАЕТ в интерфейс. `WM_MOUSEWHEEL` (game_main.c:5864) кладёт
//   его в очередь обоих менеджеров окон записью типа 7 (`CWindowMgr_QueueMouseWheel @0x12F01C0`,
//   ui_core.c:113636). Насос кадра `CControl_DispatchMessage @0x12B2440` (ui_core.c:105755) снимает
//   запись через `input_ProcessMoveCommand @0x12F0290` (input.c:5236, `case 7`), кладёт wParam в
//   `CWindowMgr+0x138` и зовёт ВИРТУАЛЬНЫЙ СЛОТ 7 менеджера. Зум камеры идёт ПОСЛЕ и только если
//   курсор не над UI — то есть приоритет колеса у интерфейса ПЕРВЫЙ.
//   НЕ ДОКАЗАНО: что именно делает слот 7 у конкретного списка — для этого нужна таблица
//   `off_1505DD4`, которой в разобранном срезе нет; «шаг прокрутки колесом» в данных .JUI и в
//   `chat.c` тоже не встречается (★ `uiListSetScrollOffset(list,2)` — это межстрочный интервал
//   2 px, а НЕ шаг листания, см. ui_core.c:111621).
// ⇒ ПОЭТОМУ: сам приём колеса интерфейсом воспроизводим 1:1, а ШАГ берём свой и равный шагу
//   штатных кнопок-стрелок (лента — kScrollStep строк, списки — 1 позиция). Помечено как
//   осознанное отступление.
bool UAsdaChatWindowSubsystem::OnWheelChatWindow(int32 Notches)
{
	if (!ChatWidget || Notches == 0) { return false; }
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FVector2D Cur = FSlateApplication::Get().GetCursorPos();
	// Чужой всплывающий слой поверх нас (список каналов, меню) — колесо не наше.
	{
		UGameInstance* GI = GetGameInstance();
		if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
		{
			if (WM->IsPointerBlockedByTopmost(kZoneId)) { return false; }
		}
	}

	// ── Лента 2521: вверх колеса = к более старым строкам (уменьшение смещения) ──
	if (LinesBox && IsUnderCursor(LinesBox, Cur))
	{
		const float Before = LinesBox->GetScrollOffset();
		ScrollByLines(-Notches * kScrollStep);
		UE_LOG(LogAsdaChatWin, Verbose, TEXT("[колесо] лента 2521: %+d щелч. → смещение %.1f → %.1f"),
			Notches, Before, LinesBox->GetScrollOffset());
		return true;
	}

	// ── Ряд вкладок 2501: колесо листает ленту вкладок влево/вправо (шаг стрелок = 1) ──
	for (const TObjectPtr<UImage>& Q : TabQuads)
	{
		if (Q && Q->GetVisibility() != ESlateVisibility::Collapsed && IsUnderCursor(Q, Cur))
		{
			ScrollChatTabs(-Notches);
			UE_LOG(LogAsdaChatWin, Verbose, TEXT("[колесо] ряд вкладок 2501: %+d щелч. → первая видимая %d"),
				Notches, TabScroll);
			return true;
		}
	}
	return false;
}

// CHAT_NEW_HomeBtn (2528) — эталон chatWnd_OnCommand case 0x9E0 (chat.c:2081):
//     scrollCtrl = helper_getFieldByFlag(self[3], 1);
//     Field153   = CClass_GetField153(scrollCtrl);
//     uiScrollbarSetPos(scrollCtrl, Field153);
//   то есть «поставить позицию скролла в максимум» = прокрутить ленту В КОНЕЦ (к новейшей строке).
void UAsdaChatWindowSubsystem::OnHomeBtnClicked()
{
	if (!LinesBox) { return; }
	LinesBox->ScrollToEnd();
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-окно] HomeBtn: лента прокручена в конец"));
}
