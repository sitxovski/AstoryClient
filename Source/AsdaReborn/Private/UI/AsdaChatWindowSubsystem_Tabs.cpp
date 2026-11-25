// Copyright (c) 2026 @sitxovski (Discord)
// AsdaChatWindowSubsystem_Tabs.cpp — ★ 23.10.2026 часть UAsdaChatWindowSubsystem, вынесенная из AsdaChatWindowSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaChatWindowSubsystem.plan.json).
//   Панель вкладок 2501 и динамические вкладки, консольные команды asda.ChatWin.*, этап 3 (меню, игнор-лист, лимит длины).
//   #include — те же, что у AsdaChatWindowSubsystem.cpp, в том же порядке; общее — AsdaChatWindowSubsystemInternal.h.
#include "UI/AsdaChatWindowSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "Text/AsdaLang.h"   // ★ N9 (дедуп №2, В2-A): FAsdaLang::MissingTag — единая заглушка «[#id]»
#include "AsdaSelf.h"   // ★ В2-A дедупа №2: общая точка «это я?» (класс/уровень/charId/userNum)
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: WireTextByteLen/TruncateToWireBytes (лимит ввода в байтах провода)
#include "AsdaDataTableCache.h"   // ★ Аудит №4 (B2): AsdaDt::Cached — одна ленивая загрузка DataTable на порт
#include "UI/AsdaUiHelpers.h"   // ★ D1/D2/D5: единые UI-хелперы (дедуп 2026-08-08)
#include "UI/AsdaChatSubsystem.h"
#include "UI/AsdaCharaInfoSubsystem.h"   // ★ эмоции слэш-командой (порт ChatInput_ParseCommand)
#include "UI/AsdaJuiSlot.h"
#include "Text/AsdaTextSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"   // ★ Аудит №3 (A8/B8): атласы JUI — только через менеджер
// UAsdaRichText::ApplyFormatted — теги <FS>/<FD>/<FC=..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
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
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "UI/AsdaNotice.h"                 // ★ Дедуп №2, В1-B (U10c): `AsdaNotice::Loc` вместо прямого `FAsdaLoginData::Loc`
#include "UI/AsdaPartySubsystem.h"         // ★ 12.10.2026 [ОРИГИНАЛ]: чат отряда — `Party_IsInParty` и `g_nPartyInviteTargetIdx` (= PartyIndex)
#include "UI/AsdaSoulmateSubsystem.h"      // ★ 12.10.2026 [ОРИГИНАЛ]: канал «Друг» — слот/пара/имя партнёра (`g_pSoulmateWnd`)
#include "AsdaChatWindowSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaChatWindowSubsystem*.cpp (LogAsdaChatWin, чистые хелперы)



// ═══════════ ПАНЕЛЬ ВКЛАДОК ЧАТА 2501 (CHAT_NEW_Radio_B) — ФИЛЬТР ЛЕНТЫ ═══════════════════════
//
// ЭТАЛОН. `uiChatOpt_InitItems @0xBA0920` (ui_core.c:11618) заводит **7 вкладок**, запись 22 байта
//   (`char[20]` имя + `WORD` маска каналов на +20), и кладёт их в `CChat.tabRecordList`:
//
//     # | loc  | имя              | маска | биты (слоты каналов)
//     0 | 5411 | Объединение      | 1023  | 0…9 — ВСЕ
//     1 | 5412 | Отряд            |  928  | 5, 7, 8, 9
//     2 | 5413 | Друг             |  896  | 7, 8, 9
//     3 | 5414 | Клан             |  960  | 6, 7, 8, 9
//     4 | 5415 | Поиск отряда     |  930  | 1, 5, 7, 8, 9
//     5 | 5416 | Торговля         |  900  | 2, 7, 8, 9
//     6 | 5417 | По всем каналам  |  903  | 0, 1, 2, 7, 8, 9
//   ★ Биты 7/8/9 (Шепот, Друг, Армия) стоят во ВСЕХ вкладках — личные сообщения видны всегда.
//
// ПЕРЕКЛЮЧЕНИЕ. `chatTabSelectFilter @0xBA2290` (chat.c:1805): `this[108] = индекс`, фокус на поле
//   ввода, затем 10 байт `this+498` заполняются `sub_92B976(this, tab, i)` для i = 0…9 — это
//   ПОБИТОВЫЙ разбор маски в массив «канал i показывать», который отдаётся листбоксу
//   (`sub_9387FC`), после чего лента скроллится в конец. ⇒ **ЛЕНТА ОДНА**, вкладка — это ФИЛЬТР
//   отображения, а не отдельный буфер. Порт делает ровно так: строки хранят `Slot`, а
//   `RebuildLines` показывает только те, чей бит стоит в маске активной вкладки.
//
// РИСОВАНИЕ. Контрол 2501 — скин `TalkBar_2`, `Type = "RadioButton"`, `Rect = "0 0 79 25"` —
//   то есть ОДНА вкладка 79×25, а сам контрол растягивается на ширину окна:
//   `uiControlCountFittingChildren(radio, 0, width − 10)` (chat.c:790). При ширине окна 474
//   влезает (474 − 10) / 79 = **5 вкладок из 7** — ровно как на скриншоте оригинала.
//   Состояния скина (текстура NE_PvP2.TGA):
//     SS0 обычная  UV (0.840,0.375)-(0.994,0.424) Diffuse FFFFFFFF CaptionColor FFFFF9AC
//     SS1 ВЫБРАНА  UV (0.840,0.430)-(0.994,0.479) Diffuse FFFF8000 CaptionColor FFFFFFFF
//     SS2/3 наведение/нажатие UV (0.840,0.484)-(0.994,0.533) Diffuse FFFFC088 CaptionColor FFFFFF80
//   ★ Стрелок листания в скине НЕТ (у RadioButton их рисует сам движок, отдельного скина в
//   JuiSkin.ini нет) ⇒ стрелки собраны из ТОГО ЖЕ скина TalkBar_2 узкими кнопками с подписями
//   «◄» / «►». Это помечено как отступление: данных о их внешнем виде в эталоне не сохранилось.
void UAsdaChatWindowSubsystem::BuildChatTabs()
{
	if (bTabsBuilt || !ChatWidget || !ChatWidget->WidgetTree) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(ChatWidget->WidgetTree->RootWidget);
	if (!Root) { return; }
	// Родной план вкладки из WBP больше не нужен — рисуем весь ряд сами.
	// ★ Перенесено в ассет (15.09, разрешение владельца): CHAT_NEW_Radio_B_4_0 / _OPT0 / _Text свёрнуты
	//   в самом WBP (шаг Tools/asda_ui_chat_markup_visibility.py), код их больше не гасит.

	// ★★★ ПЛИТКИ РЯДА СОЗДАЮТСЯ ПО ПОТРЕБНОСТИ (`EnsureTabWidgets`), а не фиксированным числом.
	//   Было два поколения этой ошибки: сперва ровно kTabsVisible = 5 (при растягивании окна показать
	//   шестую/седьмую было НЕЧЕМ), потом ровно kTabCount = 7 — но вкладки стали ДИНАМИЧЕСКИМИ
	//   (добавление/удаление через меню 2670), и их число заранее неизвестно. Сколько ПОКАЗАТЬ,
	//   решает эталонная формула `VisibleTabCount()`; сколько СОЗДАТЬ — она же.
	TabQuads.Reset(); TabCaps.Reset();
	EnsureTabWidgets(VisibleTabCount());
	TabArrowL = MakeTabQuad(ChatWidget, nullptr, TEXT("CHAT_NEW_TabArrowL"), 80);
	TabArrowR = MakeTabQuad(ChatWidget, nullptr, TEXT("CHAT_NEW_TabArrowR"), 80);
	TabArrowLCap = MakeTabCap(ChatWidget, nullptr, TEXT("CHAT_NEW_TabArrowL_Text"), 81);
	TabArrowRCap = MakeTabCap(ChatWidget, nullptr, TEXT("CHAT_NEW_TabArrowR_Text"), 81);
	if (TabArrowLCap) { TabArrowLCap->SetText(FText::FromString(TEXT("\u25C4"))); }
	if (TabArrowRCap) { TabArrowRCap->SetText(FText::FromString(TEXT("\u25BA"))); }

	bTabsBuilt = true;
	RefreshChatTabs();
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-вкладки] построено: вкладок %d, плиток %d, влезает сейчас %d (ширина окна %.0f), вкладка %d×%d, активная %d, маска 0x%03X"),
		ChatTabs.Num(), TabQuads.Num(), VisibleTabCount(), BaseWidth(), (int32)kTabW, (int32)kTabH, ActiveTab, TabMask(ActiveTab));
}

// ── Фабрики виджетов ряда вкладок (скин TalkBar_2, атлас NE_PvP2.TGA) ──────────────────────────
// ★ ОДНИ И ТЕ ЖЕ фабрики обслуживают ДВА списка одного и того же скина: ряд вкладок 2501 в окне
//   чата и столбец клон-ячеек списка вкладок 2597 в редакторе 2590 (эталон рисует их ОДНИМ
//   классом контрола — RadioButton TalkBar_2, и клонирует ячейки одной функцией
//   `uiListAppendClonedCell`). Host — окно-хозяин, Parent — панель (nullptr = корневой канвас).
UImage* UAsdaChatWindowSubsystem::MakeTabQuad(UUserWidget* Host, UPanelWidget* Parent, const TCHAR* Name, int32 Z)
{
	// ★ U23 (В1 дедупа №2): квад — общей точкой `AsdaUi::MakeAtlasQuad` (атлас вкладок `ne_pvp2`, курсор-рука).
	return AsdaUi::MakeAtlasQuad(Host, Parent, Name, FName(TEXT("ne_pvp2")),
		FBox2f(FVector2f(0.840f, 0.375f), FVector2f(0.994f, 0.424f)), FVector2D(kTabW, kTabH), Z, /*bHandCursor*/ true);
}

UTextBlock* UAsdaChatWindowSubsystem::MakeTabCap(UUserWidget* Host, UPanelWidget* Parent, const TCHAR* Name, int32 Z)
{
	if (!Host || !Host->WidgetTree) { return nullptr; }
	UPanelWidget* Root = Parent ? Parent : Cast<UPanelWidget>(Host->WidgetTree->RootWidget);
	if (!Root) { return nullptr; }
	UTextBlock* Tb = Host->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
	if (!Tb) { return nullptr; }
	Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Tb->SetAutoWrapText(false);
	Tb->SetJustification(ETextJustify::Center);
	FSlateFontInfo F = TipFont; F.Size = (int32)kFontPx; Tb->SetFont(F);
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Root->AddChild(Tb))) { CS->SetZOrder(Z); }
	return Tb;
}

// Довести число плиток ряда до N. Вкладки динамические (меню 2670 их добавляет/удаляет), поэтому
//   виджеты создаются по мере надобности и НЕ удаляются при уменьшении — лишние просто Collapsed.
void UAsdaChatWindowSubsystem::EnsureTabWidgets(int32 N)
{
	for (int32 i = TabQuads.Num(); i < N; ++i)
	{
		TabQuads.Add(MakeTabQuad(ChatWidget, nullptr, *FString::Printf(TEXT("CHAT_NEW_Tab%d"), i), 80));
		TabCaps.Add(MakeTabCap(ChatWidget, nullptr, *FString::Printf(TEXT("CHAT_NEW_Tab%d_Text"), i), 81));
	}
}

// ★★★ ЭТАЛОННАЯ ФОРМУЛА (chat.c:795 + ui_core.c:115677): считаем, сколько плиток по kTabW
//   помещается в «ширина окна − 10». `uiControlCountFittingChildren` накапливает ширины детей и
//   инкрементит счётчик, пока сумма НЕ ПРЕВЫСИЛА лимит ⇒ floor((W − 10) / kTabW).
//   При авторских 474 → (474 − 10) / 79 = 5 (как в оригинале). Растянули окно — влезает больше.
//   ★ Верхняя граница — ФАКТИЧЕСКОЕ число вкладок (список динамический), а не константа 7.
int32 UAsdaChatWindowSubsystem::VisibleTabCount() const
{
	const int32 N = (int32)FMath::FloorToFloat((BaseWidth() - 10.f) / kTabW);
	return FMath::Clamp(N, 1, FMath::Max(1, ChatTabs.Num()));
}

// ══════════════ ДИНАМИЧЕСКИЕ ВКЛАДКИ (аналог `CChat::tabRecordList` эталона) ═══════════════════
// ЭТАЛОН: `uiChatOpt_InitItems @0xBA0920` (ui_core.c:11618) заводит СЕМЬ записей по 22 байта
//   (`char[20]` имя + WORD маска каналов на +20) и кладёт их в `CChat.tabRecordList`; при наличии
//   `ChatOpt.bin` они перечитываются из файла (`CChat_LoadChatOpt`). То есть набор вкладок в
//   оригинале — ДАННЫЕ, а не константа: игрок правит его окном 2590 и меню 2670.
//   ★ ДО СЕГОДНЯШНЕГО ДНЯ порт держал их двумя `static const` таблицами — из-за этого пункты меню
//   2670 физически не могли ничего изменить (жалоба юзера 2026-08-06: «ни один пункт не работает»).
void UAsdaChatWindowSubsystem::InitDefaultTabs()
{
	// Эталонные дефолты: loc-id подписи + маска каналов (биты = слоты 0…9).
	static const int32 kLoc [kTabCountDefault] = { 5411, 5412, 5413, 5414, 5415, 5416, 5417 };
	static const int32 kMask[kTabCountDefault] = { 1023,  928,  896,  960,  930,  900,  903 };
	ChatTabs.Reset();
	for (int32 i = 0; i < kTabCountDefault; ++i)
	{
		FAsdaChatTab T;
		T.LocId = kLoc[i];        // подпись берётся из таблицы текстов (имя пустое = «дефолтная»)
		T.Mask  = kMask[i];
		ChatTabs.Add(MoveTemp(T));
	}
	ActiveTab = FMath::Clamp(ActiveTab, 0, ChatTabs.Num() - 1);
	TabScroll = 0;
}

// Маска каналов вкладки. Вне диапазона — 1023 (все каналы), как эталонное «Объединение».
int32 UAsdaChatWindowSubsystem::TabMask(int32 Tab) const
{
	return ChatTabs.IsValidIndex(Tab) ? ChatTabs[Tab].Mask : 1023;
}

// loc-id подписи вкладки; 0 = вкладка ПОЛЬЗОВАТЕЛЬСКАЯ, подпись лежит в самой записи.
int32 UAsdaChatWindowSubsystem::TabLocId(int32 Tab) const
{
	return ChatTabs.IsValidIndex(Tab) ? ChatTabs[Tab].LocId : 0;
}

// Готовая подпись вкладки: у эталонных — строка из таблицы текстов (с переименованиями порта),
//   у пользовательских — введённое игроком имя.
FString UAsdaChatWindowSubsystem::TabName(int32 Tab) const
{
	if (!ChatTabs.IsValidIndex(Tab)) { return FString(); }
	const FAsdaChatTab& T = ChatTabs[Tab];
	if (T.LocId > 0) { return UiChannelText(T.LocId); }
	return T.Name;
}

// Разложить ряд вкладок и стрелок по текущему прокруту + подсветить активную.
void UAsdaChatWindowSubsystem::RefreshChatTabs()
{
	if (!bTabsBuilt) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	const float L = WinL, T = WinT;
	// ★★★ СКОЛЬКО ПЛИТОК ПОКАЗЫВАЕМ СЕЙЧАС — по эталонной формуле от ТЕКУЩЕЙ ширины окна.
	//   Растянули окно вправо — освободившееся место занимают следующие вкладки; сузили — уходят.
	const int32 Vis = VisibleTabCount();
	// Прокрут держим в допустимых пределах: расширили окно на последней странице — ряд «подтянется».
	TabScroll = FMath::Clamp(TabScroll, 0, FMath::Max(0, ChatTabs.Num() - Vis));
	EnsureTabWidgets(Vis);   // вкладку могли ДОБАВИТЬ — плитки создаются по потребности

	auto Place = [](UWidget* W, float X, float Y, float Wd, float Ht)
	{
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
		{
			AsdaJui::SetChatSlot(CS, X, Y, Wd, Ht);
		}
	};
	for (int32 i = 0; i < TabQuads.Num(); ++i)
	{
		const int32 Tab = TabScroll + i;
		const bool bShow = (i < Vis) && ChatTabs.IsValidIndex(Tab);
		const float X = L + 9.f + kTabW * (float)i;
		if (UImage* Q = TabQuads[i])
		{
			Q->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			Place(Q, X, T + 11.f, kTabW, kTabH);
			if (bShow)
			{
				// ★ ТРИ СОСТОЯНИЯ СКИНА TalkBar_2 (текстура NE_PvP2.TGA):
				//   SS1 ВЫБРАНА  UV (0.840,0.430)-(0.994,0.479)  Diffuse FFFF8000  caption FFFFFFFF
				//   SS2 НАВЕДЕНИЕ UV (0.840,0.484)-(0.994,0.533)  Diffuse FFFFC088  caption FFFFFF80
				//   SS0 ОБЫЧНАЯ   UV (0.840,0.375)-(0.994,0.424)  Diffuse FFFFFFFF  caption FFFFF9AC
				FSlateBrush Br = Q->GetBrush();
				const bool bSel = (Tab == ActiveTab);
				const bool bHov = (Tab == TabHoverIdx) && !bSel;
				Br.SetUVRegion(bSel ? FBox2f(FVector2f(0.840f, 0.430f), FVector2f(0.994f, 0.479f))
				             : (bHov ? FBox2f(FVector2f(0.840f, 0.484f), FVector2f(0.994f, 0.533f))
				                     : FBox2f(FVector2f(0.840f, 0.375f), FVector2f(0.994f, 0.424f))));
				Br.TintColor = FSlateColor(AsdaSrgbHexToLinear(
					bSel ? TEXT("FFFF8000") : (bHov ? TEXT("FFFFC088") : TEXT("FFFFFFFF"))));
				Q->SetBrush(Br);
			}
		}
		if (UTextBlock* Cp = TabCaps[i])
		{
			Cp->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			Place(Cp, X, T + 11.f + 5.f, kTabW, 14.f);
			if (bShow)
			{
				// ★ Подпись вкладки-фильтра — через UiChannelText: вкладка № 6 (loc 5417) показывается
				//   как «Все каналы» (требование юзера 2026-08-06), а не «По всем каналам».
				const FString N = TabName(Tab);
				Cp->SetText(FText::FromString(N));
				Cp->SetColorAndOpacity(FSlateColor(AsdaSrgbHexToLinear(
					(Tab == ActiveTab) ? TEXT("FFFFFFFF")
					: ((Tab == TabHoverIdx) ? TEXT("FFFFFF80") : TEXT("FFFFF9AC")))));
			}
		}
	}
	// ★★★ СТРЕЛКИ ЛИСТАНИЯ — ТОЛЬКО ПОКА ВЛЕЗАЮТ НЕ ВСЕ ВКЛАДКИ (требование юзера 2026-08-06:
	//   «в случае если все кнопки отображены — кнопки влево/вправо для вкладок нужно скрыть»).
	//   Раньше условие было КОНСТАНТНЫМ (7 > 5) и стрелки висели всегда. Теперь считается от
	//   фактического Vis, поэтому при достаточно широком окне они исчезают, а при сужении — возвращаются.
	const bool bNeedArrows = (Vis < ChatTabs.Num());
	const float AX = L + 9.f + kTabW * (float)Vis;
	auto ShowArrow = [&](UImage* Q, UTextBlock* Cp, float X, bool bEnabled, int32 ArrowId)
	{
		if (Q)
		{
			Q->SetVisibility(bNeedArrows ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			Place(Q, X, T + 11.f, kTabArrowW, kTabH);
			FSlateBrush Br = Q->GetBrush();
			Br.SetUVRegion(FBox2f(FVector2f(0.840f, 0.484f), FVector2f(0.994f, 0.533f)));
			// Наведённая стрелка — ярче (тот же кадр SS2 скина), недоступная — приглушена.
			const bool bHovA = bEnabled && (ArrowHoverIdx == ArrowId);
			Br.TintColor = FSlateColor(AsdaSrgbHexToLinear(
				!bEnabled ? TEXT("80FFC088") : (bHovA ? TEXT("FFFFE0B0") : TEXT("FFFFC088"))));
			Q->SetBrush(Br);
		}
		if (Cp)
		{
			Cp->SetVisibility(bNeedArrows ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
			Place(Cp, X, T + 11.f + 5.f, kTabArrowW, 14.f);
			Cp->SetColorAndOpacity(FSlateColor(AsdaSrgbHexToLinear(bEnabled ? TEXT("FFFFFF80") : TEXT("60FFFF80"))));
		}
	};
	ShowArrow(TabArrowL, TabArrowLCap, AX, TabScroll > 0, 0);
	ShowArrow(TabArrowR, TabArrowRCap, AX + kTabArrowW + 2.f, TabScroll + Vis < ChatTabs.Num(), 1);
	UE_LOG(LogAsdaChatWin, Verbose,
		TEXT("[чат-вкладки] ширина окна %.0f → влезает %d из %d, первая видимая %d, стрелки %s"),
		BaseWidth(), Vis, ChatTabs.Num(), TabScroll, bNeedArrows ? TEXT("показаны") : TEXT("СКРЫТЫ (влезли все)"));
}

// Переключение вкладки = смена фильтра ленты (chatTabSelectFilter) + перерисовка ряда.
void UAsdaChatWindowSubsystem::SelectChatTab(int32 Tab)
{
	if (!ChatTabs.IsValidIndex(Tab)) { return; }
	ActiveTab = Tab;
	RefreshChatTabs();
	RebuildLines();                 // фильтр по маске применяется здесь
	UGameInstance* GI = GetGameInstance();
	UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-вкладки] активна %d «%s», маска 0x%03X"),
		Tab, *TabName(Tab), TabMask(Tab));
	// Эталон после смены вкладки ставит фокус в поле ввода и скроллит ленту в конец.
	if (LinesBox && !bScrollFrozen) { LinesBox->ScrollToEnd(); }
}

// Индекс вкладки под курсором (−1 = мимо).
int32 UAsdaChatWindowSubsystem::TabHitTest(const FVector2D& Cur) const
{
	for (int32 i = 0; i < TabQuads.Num(); ++i)
	{
		if (IsUnderCursor(TabQuads[i], Cur)) { return TabScroll + i; }
	}
	return -1;
}

void UAsdaChatWindowSubsystem::ScrollChatTabs(int32 Delta)
{
	const int32 New = FMath::Clamp(TabScroll + Delta, 0, FMath::Max(0, ChatTabs.Num() - VisibleTabCount()));
	if (New == TabScroll) { return; }
	TabScroll = New;
	RefreshChatTabs();
	UE_LOG(LogAsdaChatWin, Log, TEXT("[чат-вкладки] прокрут ряда → первая видимая %d"), TabScroll);
}

// ★ РАНТАЙМ-ДОКАЗАТЕЛЬСТВО переноса и выравнивания (asda.ChatWin.DumpTip).
//   Печатает по каждому узлу справки то, что реально стоит на живом виджете, а не то, что мы
//   «хотели поставить»: класс, autowrap, ширину переноса, ширину слота и фактическую ширину
//   отрисованного текста (desired size). Если ширина текста больше ширины слота — перенос не
//   применён, и это видно числом.
void UAsdaChatWindowSubsystem::DumpQueTip() const
{
	UE_LOG(LogAsdaChatWin, Log, TEXT("=========== ПОДПИСИ ОКНА СПРАВКИ 2580 ==========="));
	if (QueTipTexts.Num() == 0)
	{
		UE_LOG(LogAsdaChatWin, Log, TEXT("  подписи не созданы (окно не построено)"));
		return;
	}
	for (const TObjectPtr<UTextBlock>& W : QueTipTexts)
	{
		if (!W) { continue; }
		float SlotW = -1.f, SlotH = -1.f;
		if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
		{
			const FMargin O = CS->GetOffsets();
			SlotW = O.Right; SlotH = O.Bottom;
		}
		const FVector2D Desired = W->GetDesiredSize();
		const FVector2D Cached  = W->GetCachedGeometry().GetLocalSize();
		const bool bOverflow = (Desired.X > SlotW + 1.f);
		// Текст одной строкой (переводы → «|») — чтобы по логу было видно, ЧТО стоит в узле (loc или заготовка JUI).
		FString Shown = W->GetText().ToString();
		Shown.ReplaceInline(TEXT("\n"), TEXT("|"));
		UE_LOG(LogAsdaChatWin, Log,
			TEXT("  %-32s %-16s вид=%d autowrap=%s wrapAt=%.1f кегль=%d | слот %.1f×%.1f | текст %.1f×%.1f | геометрия %.1f×%.1f | %s | «%s»"),
			*W->GetName(), *W->GetClass()->GetName(), (int32)W->GetVisibility(),
			W->GetAutoWrapText() ? TEXT("ДА") : TEXT("нет"), W->GetWrapTextAt(), (int32)W->GetFont().Size,
			SlotW, SlotH, Desired.X, Desired.Y, Cached.X, Cached.Y,
			bOverflow ? TEXT("★ ТЕКСТ ШИРЕ СЛОТА — ПЕРЕНОС НЕ ПРИМЕНЁН") : TEXT("умещается — перенос работает"),
			*Shown);
	}
	UE_LOG(LogAsdaChatWin, Log, TEXT("================================================="));
}

void UAsdaChatWindowSubsystem::DebugToggleExpand() { OnUDbtnClicked(); }
void UAsdaChatWindowSubsystem::DebugSubmit(const FString& Text) { SubmitInput(Text); }

// Последняя строка истории ленты (самотест чата отряда): текст, цвет, слот, ник.
bool UAsdaChatWindowSubsystem::DebugLastLogLine(FString& OutRaw, FColor& OutColor, int32& OutSlot, FString& OutNick) const
{
	if (LogHistory.Num() == 0) { return false; }
	const FChatLogLine& L = LogHistory.Last();
	OutRaw = L.Raw; OutColor = L.Color; OutSlot = L.Slot; OutNick = L.Nick;
	return true;
}

// Вкладки, чья маска пропускает слот (то же условие, что у показа строки в AddChatLineRaw/RebuildLines).
TArray<int32> UAsdaChatWindowSubsystem::DebugTabsShowingSlot(int32 Slot) const
{
	TArray<int32> Out;
	for (int32 T = 0; T < ChatTabs.Num(); ++T)
	{
		if (Slot < 0 || ((TabMask(T) >> Slot) & 1) != 0) { Out.Add(T); }
	}
	return Out;
}
void UAsdaChatWindowSubsystem::DebugParse(const FString& Text)
{
	FString T = Text;
	const bool bCmd = ParseSlashCommand(T);
	UE_LOG(LogAsdaChatWin, Log,
		TEXT("[чат-ввод] разбор «%s»: команда=%s, режим=%d, адресат=«%s», остаток=«%s»"),
		*Text, bCmd ? TEXT("ДА") : TEXT("нет"), InputState, *WhisperTarget, *T);
}
void UAsdaChatWindowSubsystem::DebugToggleHelp(bool bShow) { SetQueTipVisible(bShow); }

// ══════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ════════════════════════════════════════════

static UAsdaChatWindowSubsystem* AsdaChatWin(UWorld* W)
{
	return (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UAsdaChatWindowSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinLine(
	TEXT("asda.ChatWin.Line"),
	TEXT("Строка в окно чата: asda.ChatWin.Line <категория 0..9|100|101> <текст> — проверка слота/цвета"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 2) { return; }
		const int32 Cat = FCString::Atoi(*Args[0]);
		FString Text;
		for (int32 i = 1; i < Args.Num(); ++i) { Text += (i > 1 ? TEXT(" ") : TEXT("")) + Args[i]; }
		C->AddChatLineRaw(Cat, Text);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ Прогон дубля оповещения БЕЗ сервера и без маршрутизатора: проверяет ровно ту функцию,
//   которую зовёт приёмник EAsdaMsgTarget::ChatRibbon. Цвет — sRGB-шестнадцатеричный (RRGGBB),
//   слот — −1 (все вкладки, эталон 3113) или 0..9 (4 = эталон анонса редкой добычи).
//   Разметку внутри текста писать МОЖНО и НУЖНО проверить: «<FC=FFB0FF20>Меч</F>» должен остаться
//   зелёным, а остальная строка — базового цвета.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatAnnounce(
	TEXT("asda.Chat.Announce"),
	TEXT("Дубль оповещения в ленту: asda.Chat.Announce <текст> [RRGGBB] [слот=-1] — прогон AddAnnounceLine"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1)
		{
			UE_LOG(LogAsdaChatWin, Warning,
				TEXT("[чат-окно] использование: asda.Chat.Announce <текст> [RRGGBB] [слот]"));
			return;
		}
		// Хвостовые аргументы «цвет» и «слот» распознаём по форме: 6 hex-цифр и целое число.
		TArray<FString> A = Args;
		int32  Slot  = INDEX_NONE;
		FColor Color = AsdaNoticeColor::Gold;   // золотой FFD306 — цвет успеха заточки
		auto IsInt = [](const FString& S)
		{
			if (S.IsEmpty()) { return false; }
			int32 i = (S[0] == TEXT('-')) ? 1 : 0;
			if (i >= S.Len()) { return false; }
			for (; i < S.Len(); ++i) { if (!FChar::IsDigit(S[i])) { return false; } }
			return true;
		};
		auto IsHex6 = [](const FString& S)
		{
			if (S.Len() != 6) { return false; }
			for (int32 i = 0; i < 6; ++i) { if (!FChar::IsHexDigit(S[i])) { return false; } }
			return true;
		};
		if (A.Num() >= 2 && IsInt(A.Last())) { Slot = FCString::Atoi(*A.Last()); A.Pop(); }
		if (A.Num() >= 2 && IsHex6(A.Last())) { Color = FColor::FromHex(A.Last()); Color.A = 255; A.Pop(); }
		FString Text;
		for (int32 i = 0; i < A.Num(); ++i) { Text += (i > 0 ? TEXT(" ") : TEXT("")) + A[i]; }
		C->AddAnnounceLine(Text, Color, Slot);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinFlood(
	TEXT("asda.ChatWin.Flood"),
	TEXT("N строк подряд: asda.ChatWin.Flood <N> [категория] — проверка лимита 500 и автоскролла"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1) { return; }
		const int32 N = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 2000);
		const int32 Cat = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : -1;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 UseCat = (Cat >= 0) ? Cat : (i % 10 < 7 ? i % 10 : 0);
			C->AddChatLineRaw(UseCat, FString::Printf(TEXT("тестовая строка %d (категория %d)"), i + 1, UseCat));
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ ДИАГНОСТИКА ЗАЛИПАНИЯ ФЛАГА НАБОРА (дефект 2026-08-30). Открываем строку ввода ТЕМ ЖЕ
//   путём, что и кнопка «Шёпот» окна поиска отряда / пункт контекстного меню ника, — своего
//   обходного пути не заводим (закон `diagnostic-must-use-real-path`).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinInputOpen(
	TEXT("asda.ChatWin.InputOpen"),
	TEXT("Открыть строку ввода чата тем же путём, что кнопка «Шёпот» (аргумент — режим 0..9, по умолчанию текущий)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			C->OpenInput(Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : -1);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinInputState(
	TEXT("asda.ChatWin.InputState"),
	TEXT("Флаг «идёт набор» против ФАКТИЧЕСКОГО фокуса Slate у поля 2609 (доказательство по залипанию)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugInputState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDump(
	TEXT("asda.ChatWin.Dump"),
	TEXT("Состояние окна чата: строки по слотам каналов, автоскролл, смещение"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpCombo(
	TEXT("asda.ChatWin.DumpCombo"),
	TEXT("Список каналов 2608 + слои «поверх всего» + кулдаун-полоса 2618: живая геометрия и индексы"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugCombo());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ═══════════ ЭТАП 3: КОНТЕКСТНЫЕ МЕНЮ, ИГНОР-ЛИСТ, ЛИМИТ ДЛИНЫ ════════════════════════════════
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpMenu(
	TEXT("asda.ChatWin.DumpMenu"),
	TEXT("Контекстные меню 80000/2670, ник под курсором, игнор-лист и действующий лимит длины сообщения"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugContextMenus());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinNickMenu(
	TEXT("asda.ChatWin.NickMenu"),
	TEXT("Открыть меню 80000 по нику без мыши: asda.ChatWin.NickMenu <ник>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1) { return; }
		FString N;
		for (int32 i = 0; i < Args.Num(); ++i) { N += (i ? TEXT(" ") : TEXT("")) + Args[i]; }
		C->DebugShowNickMenu(N);
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЭТАП 4: диагностика и тест-обёртки вкладок без мыши.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpTabs(
	TEXT("asda.ChatWin.DumpTabs"),
	TEXT("Чат: список вкладок, их маски каналов, цвета каналов и состояние редактора 2590."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugTabs()); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinTabAdd(
	TEXT("asda.ChatWin.TabAdd"),
	TEXT("Чат: добавить вкладку с указанным именем (эталон chatAddFilterEntry, маска 0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugAddTab(FString::Join(A, TEXT(" "))); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinTabRename(
	TEXT("asda.ChatWin.TabRename"),
	TEXT("Чат: переименовать АКТИВНУЮ вкладку (эталон chatMacro_setShortcutText)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& A, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugRenameTab(FString::Join(A, TEXT(" "))); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinTabRemove(
	TEXT("asda.ChatWin.TabRemove"),
	TEXT("Чат: удалить АКТИВНУЮ вкладку (последнюю удалить нельзя — эталон `tabCount > 1`)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugRemoveTab(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinTabEditor(
	TEXT("asda.ChatWin.TabEditor"),
	TEXT("Чат: открыть редактор вкладок 2590 (эталон uiRefreshSkillSlotList(this,1))."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugShowTabEditor(); }
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА 2590 (закон full-structural-analysis-always): headless-дамп
//   дерева WBP невозможен — свойство `WidgetTree` в UE-Python ЗАЩИЩЁННОЕ (проверено 2026-08-06,
//   `_probe_chat_tab_editor_widget.py` падает на `get_editor_property("widget_tree")`), поэтому
//   единственный способ увидеть КАЖДЫЙ контрол импортированного окна — рантайм-дамп.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpTabEditor(
	TEXT("asda.ChatWin.DumpTabEditor"),
	TEXT("Чат: полный разбор окна редактора вкладок 2590 — каждый узел (имя/класс/видимость/слот)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugTabEditorWidgets());
		}
	}));
#endif // ASDA_DEV_CONSOLE

// ★★★ Д28: ЦВЕТА КАНАЛОВ — ДАМП И ПРЯМАЯ ЗАПИСЬ (проверка рендер-пути без палитры и редактора).
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinChanColors(
	TEXT("asda.ChatWin.ChanColors"),
	TEXT("Чат: таблица цветов каналов (живая / в редакторе / заводская) + сколько строк ленты каждым слотом."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			UE_LOG(LogAsdaChatWin, Log, TEXT("%s"), *C->DebugChanColors());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinSetChanColor(
	TEXT("asda.ChatWin.SetChanColor"),
	TEXT("Чат: задать цвет канала напрямую — asda.ChatWin.SetChanColor <слот 0..9> <RRGGBB>. ")
	TEXT("Слот 4 = «Общий чат» (5085), слот 0 = «[Все] Общий чат» (6561)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C) { return; }
		if (Args.Num() < 2)
		{
			UE_LOG(LogAsdaChatWin, Warning, TEXT("нужно: asda.ChatWin.SetChanColor <слот 0..9> <RRGGBB>"));
			return;
		}
		const int32 Slot = FCString::Atoi(*Args[0]);
		const uint32 Hex = (uint32)FCString::Strtoi(*Args[1], nullptr, 16);
		C->SetChanColorDirect(Slot, FColor((uint8)((Hex >> 16) & 0xFF), (uint8)((Hex >> 8) & 0xFF),
		                                   (uint8)(Hex & 0xFF), 255));
	}));
#endif // ASDA_DEV_CONSOLE

// ★ ЗАМОК АВТОПРОКРУТКИ 2607 (флаг CChat+497) — тумблер, проверяется без мыши.
#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinFreeze(
	TEXT("asda.ChatWin.Freeze"),
	TEXT("Чат: заморозка автопрокрутки (кнопка 2607, флаг CChat+497). Без аргумента — переключить, ")
	TEXT("«1»/«0» — задать явно."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C) { return; }
		const bool bWant = (Args.Num() >= 1) ? (FCString::Atoi(*Args[0]) != 0) : !C->IsScrollFrozen();
		C->SetScrollFrozen(bWant);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinTabMenu(
	TEXT("asda.ChatWin.TabMenu"),
	TEXT("Открыть меню панели вкладок 2670 без мыши (в центре экрана)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugShowTabMenu(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinIgnore(
	TEXT("asda.ChatWin.Ignore"),
	TEXT("Тоггл игнор-листа: asda.ChatWin.Ignore <ник> (то же, что пункт 80002 «Блокировать чат»)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1) { return; }
		FString N;
		for (int32 i = 0; i < Args.Num(); ++i) { N += (i ? TEXT(" ") : TEXT("")) + Args[i]; }
		C->ToggleChatIgnore(N);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpWidgets(
	TEXT("asda.ChatWin.DumpWidgets"),
	TEXT("Полный структурный дамп окна чата: узлы, анкоры SPAN|POINT, сверка схем, зоны интерфейса"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DumpWidgets(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinToggle(
	TEXT("asda.ChatWin.Toggle"),
	TEXT("Свернуть/развернуть окно чата — то же, что кнопка 2605 (uiToggleExpandLayout 170 <-> 340)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DebugToggleExpand(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinDumpTip(
	TEXT("asda.ChatWin.DumpTip"),
	TEXT("Подписи окна справки 2580: класс, autowrap, ширина переноса, ширина слота и фактическая ширина текста"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W)) { C->DumpQueTip(); }
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinHelp(
	TEXT("asda.ChatWin.Help"),
	TEXT("Показать/скрыть окно справки 2580 — то же, что кнопки 2537 и 2589"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		if (UAsdaChatWindowSubsystem* C = AsdaChatWin(W))
		{
			C->DebugToggleHelp(Args.Num() >= 1 ? (FCString::Atoi(*Args[0]) != 0) : true);
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinSay(
	TEXT("asda.ChatWin.Say"),
	TEXT("Отправка в канал: asda.ChatWin.Say <режим 0..8> <текст> — тот же путь, что и Enter в поле"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 2) { return; }
		C->SetInputState(FCString::Atoi(*Args[0]));
		FString T;
		for (int32 i = 1; i < Args.Num(); ++i) { T += (i > 1 ? TEXT(" ") : TEXT("")) + Args[i]; }
		C->DebugSubmit(T);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinParse(
	TEXT("asda.ChatWin.Parse"),
	TEXT("Проверка разбора слэш-команды: asda.ChatWin.Parse /p привет — печатает режим и остаток"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: «проверка разбора» по-настоящему выполняет слэш-эмоцию (4024) и меняет режим ввода/цель шёпота.
		static const uint16 kGuardOps[] = { 4024 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.ChatWin.Parse"), 5.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1) { return; }
		FString T;
		for (int32 i = 0; i < Args.Num(); ++i) { T += (i > 0 ? TEXT(" ") : TEXT("")) + Args[i]; }
		C->DebugParse(T);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GAsdaChatWinCooldown(
	TEXT("asda.ChatWin.Cooldown"),
	// ★★★ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА: номер канала — чтобы проверить НЕЗАВИСИМОСТЬ трёх таймеров.
	//   `asda.ChatWin.Cooldown 30 2` = кулдаун только «Торговли»; переключился на «Поиск отряда»
	//   (`asda.ChatWin.Say /f …` или комбобокс) — полосы нет, отправка проходит.
	TEXT("Кулдаун-полоса в поле ввода: asda.ChatWin.Cooldown <секунды> [канал 0..2] [причина]. ")
	TEXT("Без канала — общий кулдаун (антифлуд/мат), виден в любом режиме"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaChatWindowSubsystem* C = AsdaChatWin(W);
		if (!C || Args.Num() < 1) { return; }
		// Второй аргумент — ЧИСЛО 0..2 ⇒ номер канала; иначе это начало причины (обратная совместимость).
		int32 Channel = INDEX_NONE;
		int32 First = 1;
		if (Args.Num() > 1 && Args[1].IsNumeric())
		{
			const int32 N = FCString::Atoi(*Args[1]);
			if (N >= 0 && N <= 2) { Channel = N; First = 2; }
		}
		FString R;
		for (int32 i = First; i < Args.Num(); ++i) { R += (R.IsEmpty() ? FString() : TEXT(" ")) + Args[i]; }
		if (R.IsEmpty()) { R = (Channel >= 0) ? TEXT("кулдаун канала") : TEXT("тест"); }
		C->StartCooldown(FCString::Atof(*Args[0]), R, Channel);
	}));
#endif // ASDA_DEV_CONSOLE
