// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystem_Search.cpp — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp, дословный перенос): окно поиска: контролы, интро, фильтры, строки, результаты поиска.
//   Общее для частей — в AsdaSoulmateSubsystemInternal.h; шапка и реверс — в AsdaSoulmateSubsystem.cpp.
#include "UI/AsdaSoulmateSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"   // OP_SOULMATE_UPDATE_INFO / OP_SOULMATE_WND_APPLY_INFO_PACKET
#include "AsdaBytes.h"
#include "Misc/ScopeExit.h"   // ★ 26.10.2026: ON_SCOPE_EXIT — выход из защиты повторного входа панели 6500
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
// GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)   ★ R3: комментарий снятого включения "World/AsdaWorldSubsystem.h" (файл им не пользовался) — сохранён
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ ревизия 1-Б С-1: яблочная часть inventoryHidePanelsAndRefresh
#include "UI/AsdaMinimapSubsystem.h"     // ★ 23.10.2026: панель занятости 79900 — под панелью Друга 6500
#include "UI/AsdaSoulmateSkillSubsystem.h"
#include "UI/AsdaSoulRouletteSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "UI/AsdaConstellationSubsystem.h"   // SignName / MatchIndex / SelfSign — влечение знаков
#include "UI/AsdaPartySubsystem.h"           // 8265 «Вступить в отряд» — гейты живут ТАМ
#include "UI/AsdaUiHelpers.h"                // ParsePlaneName / ApplyTabState / FormatPrintfArgs
// ★ ОБЩАЯ ТОЧКА работы с узлами окна 6400 (`namespace AsdaCommunityUi`): `Find`/`FindLabel`/
//   `SetTxt`/`SetButtonCaption`/`SetVisByPrefix`/`ShownVisibilityFor`. Заводить здесь свои копии
//   поиска по `<idc>`/`<idc>_grp`/`<idc>_Text` нельзя — закон `port-shared-points`.
#include "UI/AsdaCommunityFriendSubsystem.h"
#include "UI/AsdaCommunitySubsystem.h"   // ★ 24.10.2026 (sm-offline): окно 6400 открыто и активна вкладка «Друг» — гейт автозапроса 2244
#include "Text/AsdaLang.h"               // ★ 24.10.2026 (sm-offline): формат «Последнего выхода» 6522 по языку
// DT_smcharexp — таблица опыта Дружбы   ★ R3: комментарий снятого включения "Items/AsdaItemTableSubsystem.h" (файл им не пользовался) — сохранён
#include "Items/AsdaItemDbRows.h"            // FAsdaSmcharexpRow
#include "World/AsdaCoord.h"                  // ★ волна 1-А: мир → тайл (`helper_scaleIfNonzero`/`ratioIfFieldSet`) для 9104
#include "UI/AsdaPremiumShopSubsystem.h"     // ★ волна 1-А: гейт показа 8200 — элитный магазин 6000 (`helper_getField2075(g_pShopWnd)`)
#include "UI/AsdaCraftSubsystem.h"           // ★ волна 1-А: гейт показа 8200 — окна ремесла (`g_pItemMainWnd`/`g_pItemQtyMakeDlg`)
#include "Engine/DataTable.h"
#include "Containers/Ticker.h"   // ★ 23.10.2026: отложенная (на кадр) пересборка отметки Couple после сброса сессии
#include "Components/Button.h"
#include "Components/CheckBox.h"   // UCheckBox — галки блока «доступ к персонажу» 6543..6546
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/MultiLineEditableText.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Application/SlateApplication.h"   // ★ 10.10.2026: снятие фокуса с поля 8302 (`cFindSoulmateWinHide`)
#include "Engine/GameInstance.h"                    // ★ 10.10.2026: таймер следующего кадра (фокус поля 8302)
#include "TimerManager.h"                           // FTimerManager::SetTimerForNextTick
#include "AsdaSelf.h"                      // ★ Дедуп №2, В1-B (U5a): «это я?» — `AsdaSelf::IsSelfUserIdx`
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окон 6500/8200/8300/8400/9100
#include "AsdaSoulmateSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaSoulmateSubsystem*.cpp

// ★★★ ДЕФЕКТ ОРИГИНАЛА D10 (найден аудитом 31.08). `soulmateParseSearchResult@0xE73E00`
//   двигает курсор так: шаг `+= 33` стоит в самом `for`, а `+= pRecord[31]` (длина
//   самопредставления) — ВНУТРИ ветки, прошедшей фильтр пола (game.c:478194-478230).
//   Значит запись, ОТСЕЯННУЮ фильтром, эталон перешагивает на 33 байта вместо 33 + хвост, и
//   весь остаток списка разъезжается. Ключ ниже: 0 — воспроизвести оригинал, 1 (по умолчанию) —
//   шагать по заявленной длине. Дефолт «исправлено» выбран потому, что в оригинале фильтр
//   дублируется сервером (он же уходит в 5483), и ветка почти никогда не срабатывает, а при
//   срабатывании ломает список целиком.
static ASDA_DEV_CVAR(int32, CVarSoulmateSearchStride,
	TEXT("asda.Soul.Search.FixStride"), 1,
	TEXT("5484: 0 — как в оригинале (отсеянная фильтром запись шагает на 33 б без хвоста, ")
	TEXT("список разъезжается); 1 — шагать 33 + rec[31] всегда."));

// ★★★ ДЕФЕКТ ОРИГИНАЛА D17 (С-3 ревизии расширения окон, 07.10.2026): «мёртвые» колонки строки окна 8200.
//   В разметке строки два контрола БЕЗ `NoSelect` и без своего обработчика: контейнер сердечек (ID −1, 36×35) и
//   ячейки созвездия/влечения 8275/8276. Дети разбираются раньше панели (`uiContainerHandleMouse@0x12DCC50`), они
//   СЪЕДАЮТ нажатие, а `partyInvite_OnListSelect` их ID (вне 8208..8250) не узнаёт — щелчок по сердечку или знаку
//   зодиака строку НЕ выбирает. Это огрех разметки, а не замысел (подсветка наведения при этом горит на всей строке).
//   После расширения окна (8200 +28) «мёртвой» стала бы и вся добавленная ширина колонки сердечек. Решение по
//   требованию правки (выбор по всей ширине строки): 1 (по умолчанию) — строка выбирается щелчком в любом месте;
//   0 — как в оригинале (нажатие в колонке сердечек и в колонках созвездия/влечения строку не выбирает).
static ASDA_DEV_CVAR(int32, CVarSoulmateRowClickFullWidth,
	TEXT("asda.Soul.RowClickFullWidth"), 1,
	TEXT("Окно 8200, выбор строки щелчком: 1 — по всей ширине строки (исправлен дефект оригинала D17); ")
	TEXT("0 — как в оригинале: колонка сердечек и колонки созвездия/влечения нажатие съедают."));

// ═══════════════════════════════ ОКНО 8200 (поиск кандидатов) ═════════════════════════════════
//   Структурный разбор `CFindSoulmateWin_InitControls@0xE71240`: 7 строк (шаг IDNum 7 от 8208),
//   радио пола 8203/8204/8205, «Найти» 8207, пагинация 8258..8261, счётчики 8262/8263,
//   действия 8264 шепот / 8265 отряд / 8266 предложить Дружбу.
// ★★★ ВОЛНА 1-А (06.10.2026): ПОТОК ОКНА 8200 ПРИВЕДЁН К ЭТАЛОНУ.
//   [ОРИГИНАЛ] `netSendMaskedPacket_op5483@0xE737D0` шлёт запрос и ОЧИЩАЕТ список (`sub_E73430`: вектор пуст,
//   cur = total = 1); `soulmateParseSearchResult@0xE73E00` (5484) только ДОПИСЫВАЕТ записи (до 7) и пишет cur/total —
//   окно не трогает; `findSoulmateWin_Show@0xE741B0` (5485, тело 0 б, сервер шлёт его последним —
//   `Send_SoulSearch@0x54DFD0`) показывает окно (`uiFindSoulmateWinOnShow`) и перерисовывает строки.
//   Прежний порт открывал окно сам и обнулял список на КАЖДОМ 5484 — при длинных «о себе» сервер режет страницу на
//   два пакета 5484 (порог 650 б), и первая половина списка терялась.
// Порт `uiFindSoulmateWinOnShow@0xE72A70`: гейты, показ, счётчики страниц, снятие выбора (`partyInvite_CloseList`).
void UAsdaSoulmateSubsystem::OpenSearchWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// `if (!this->initialized) CFindSoulmateWin_InitControls(this)` — виджет строим СКРЫТЫМ, показ решают гейты ниже.
	// ★ Дедуп №2, В1-B (U22): каркас `FAsdaJuiWindowHandle::EnsureLazy` (шаги и порядок прежние).
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kFindWindowId, kFindWbp, kWinZ).EnsureLazy(*WM, FindWnd, /*bReady*/true,
		ESlateVisibility::Collapsed,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulmate, Error, TEXT("[соулмейт] WBP_Find_Soulmate_Win не загрузился")); },
		[this](UAsdaWindowSubsystem&) { BindSearchControls(); },
		FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseSearchWindow(); }));
	if (Ens == EAsdaJuiEnsure::LoadFailed) { return; }
	// [ОРИГИНАЛ] Гейты показа — МОЛЧА (game.c:477775-477781): `helper_getField2075(g_pShopWnd)` — открыт элитный
	//   магазин 6000; `tileInteract_isControlVisible(g_pItemMainWnd)` / `isBoundControlVisible(g_pItemQtyMakeDlg)` —
	//   окна ремесла 76000/77500 (общая точка `UAsdaCraftSubsystem::IsAnyCraftWindowOpen`, та же, что у сообщества 6400).
	const UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>();
	if ((Ps && Ps->IsOpen()) || UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно 8200 не показано: открыт элитный магазин 6000 или окно ремесла (гейт эталона, молча)"));
		return;
	}
	// `CClass_MapInsert(g_pWindowMgr, pWin)` + `CControl_SetVisible(pWin, 1)` — окно поднимается НАВЕРХ и показывается.
	WM->SetWindowVisible(kFindWindowId, true);
	WM->BringWindowToFront(kFindWindowId);
	// Счётчики 8262/8263 и `partyInvite_CloseList` (выбор −1) — их рисует общая перерисовка ниже.
	SelectedRow = -1;
	RefreshSearchRows();
}

// Порт `cFindSoulmateWinHide@0xE72BD0` (ui_core.c:74101): `SetVisible(*this, 0)` + `SetFocus(this+116, 0)` — спрятать окно
//   8200 и снять фокус с поля 8302 `My_Intro_EditBox` (поле окна «о себе» 8300; само окно 8300 эталон здесь не прячет).
//   ★ 10.10.2026 [ОРИГИНАЛ] (barber_minors, М-1 ревизии `esc_ranks_REVIEW.md`): раньше снятия фокуса не было — после ESC
//   по 8200 каретка оставалась в поле «о себе», и клавиши уходили в него, а не в игру.
void UAsdaSoulmateSubsystem::CloseSearchWindow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kFindWindowId, false);
	}
	// `CControl_SetFocus(поле, 0)` = снять фокус, только если он на этом поле (у UMG прямого «SetFocus(0)» нет —
	//   фокус возвращается окну игры, как при потере фокуса щелчком мимо).
	UWidget* Edit = IntroWnd ? IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox")) : nullptr;
	if (Edit && Edit->HasKeyboardFocus() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 8200 закрыто — фокус снят с поля 8302 (cFindSoulmateWinHide)"));
	}
}

// ★ 10.10.2026 (barber_minors): у поля 8302 `My_Intro_EditBox` клавиатурный фокус (самотест `asda.UI.SelfTestEscOnTop`).
bool UAsdaSoulmateSubsystem::IsIntroEditFocused() const
{
	const UWidget* Edit = IntroWnd ? IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox")) : nullptr;
	return Edit && Edit->HasKeyboardFocus();
}

// `sub_938F68` → `sub_E74A50`: `*(BYTE*)(*this + 292)` — байт видимости корневого контрола окна 8200.
bool UAsdaSoulmateSubsystem::IsSearchWindowShown() const
{
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && FindWnd && WM->IsWindowVisible(kFindWindowId);
}

// [ОРИГИНАЛ] `uiOpenWindowDispatch@0xECD570` case 1 (AsdaStory_game.c:510454-510458).
void UAsdaSoulmateSubsystem::ToggleSearchFromExtraMenu()
{
	if (IsSearchWindowShown())
	{
		CloseSearchWindow();
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] доп-меню «Меню Друга»: окно 8200 было видно → скрыто"));
		return;
	}
	SendSearch();
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] доп-меню «Меню Друга»: окно 8200 скрыто → 5483 (окно поднимет 5485)"));
}

// Порт `findSoulmateWin_Show@0xE741B0` — приём 5485 (тело сервер шлёт нулевым, эталон его не читает).
void UAsdaSoulmateSubsystem::OnSearchShow()
{
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←5485 показ окна 8200: записей %d, страница %d/%d"),
		Candidates.Num(), SearchPage, SearchLast);
	// `if (вектор пуст) soulmateInviteClearList` + `uiFindSoulmateWinOnShow` + `soulmateWin_RefreshList` —
	//   пустые строки при пустом векторе рисует та же перерисовка, что и заполненные (`RefreshSearchRows`).
	OpenSearchWindow();
	if (FindWnd) { RefreshSearchRows(); }
}

void UAsdaSoulmateSubsystem::BindSearchControls()
{
	if (!FindWnd) { return; }
	auto Bind = [this](const TCHAR* Idc, TFunction<void()> Fn)
	{
		if (UButton* B = Cast<UButton>(FindWnd->GetWidgetFromName(FName(Idc))))
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Callback = [Fn](int32) { Fn(); };
			B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
			AsdaUi::AnchorRelay(PopupRelays, R, B);   // ★★★ GC-якорь (см. `Relays`)
		}
		else
		{
			UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] окно 8200: кнопка «%s» не найдена"), Idc);
		}
	};
	auto SetTxt = [this](const TCHAR* Idc, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(FName(Idc))))
		{
			T->SetText(FText::FromString(S));
		}
	};
	auto SetCaption = [this](const TCHAR* Idc, const FString& S)
	{
		// Подпись НЕ-Text контрола импортёр кладёт в отдельный узел `{idc}_Text`
		//   (закон `jui-importer-widget-naming`); писать в сам контрол бессмысленно.
		if (UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(FName(*(FString(Idc) + TEXT("_Text"))))))
		{
			T->SetText(FText::FromString(S));
		}
	};

	// ── ★★★ R23 ЗАКРЫТ: ВСЕ подписи окна 8200 из `CFindSoulmateWin_InitControls@0xE71240` ──
	//   (game.c:477220-477505). Ни одной строки не выдумано — все loc-ID сняты дословно.
	SetTxt(TEXT("Find_SMate_Move"),          Loc(11700));   // 8201 заголовок окна
	SetTxt(TEXT("Find_SMate_All"),           Loc(11701));   // 8203 подпись позиции 0 контрола 8202
	SetTxt(TEXT("Find_SMate_Female"),        Loc(11702));   // 8204 подпись позиции 1
	SetTxt(TEXT("Find_SMate_Male"),          Loc(11703));   // 8205 подпись позиции 2
	SetTxt(TEXT("Find_SMate_Guide01"),       Loc(11704));   // 8206 пояснение
	SetCaption(TEXT("Find_SMate_FindBtn"),   Loc(11705));   // 8207 «Поиск»
	SetCaption(TEXT("Find_SMate_WhisBtn"),   Loc(12096));   // 8264 «Шепот»
	SetCaption(TEXT("Find_SMate_PtyBtn"),    Loc(11708));   // 8265 «Вступить в отряд»
	SetCaption(TEXT("Find_SMate_SMBtn"),     Loc(6801));    // 8266 «Предложить Дружбу»
	SetCaption(TEXT("Find_SMate_CanBtn"),    Loc(2424));    // 8267 «Отмена»
	SetTxt(TEXT("Find_SMate_MyIntroTitle"),  Loc(11706));   // 8268 «Обо мне»
	SetCaption(TEXT("Find_SMate_EditBtn"),   Loc(11707));   // 8270 «Изменить информацию»
	// ⚠ Подписи окна 8300 ставятся при ЕГО открытии: 8300 `My_Introduction_Win` — ОТДЕЛЬНОЕ
	//   верхнеуровневое окно разметки (`gamemain_jui.ini:422365`), а не поддерево 8200.
	// Заголовки восьми колонок 8351..8358 = `Find_SMate_List_D1..D8` ← loc 110804..110811
	//   (тот же цикл `colIdx + 8351` / `colIdx + 110804`, что в эталоне; ID сняты по разметке).
	for (int32 Col = 0; Col < 8; ++Col)
	{
		SetTxt(*FString::Printf(TEXT("Find_SMate_List_D%d"), Col + 1), Loc(110804 + Col));
	}

	// ★ ИСПРАВЛЕНО: 8207 «Найти» в эталоне НЕ сбрасывает страницу — он перезапрашивает ТЕКУЩУЮ
	//   (`case 8207: goto LABEL_9` → `win[122] = win[121]`, game.c:477614-477630).
	// ★ Волна 1-А: страница ЗАПРОСА `+488` отделена от текущей `+484` (как в эталоне, `uiListPagerHandleCommand`).
	Bind(TEXT("Find_SMate_FindBtn"),  [this]() { SearchReqPage = SearchPage; SendSearch(); });   // 8207: req = cur
	Bind(TEXT("Find_SMate_FirstBtn"), [this]() { SearchReqPage = 1; SendSearch(); });            // 8258: req = 1
	// ★ Ревизия 1-А МЕЛК-4: границы — как у эталона, ПО ОДНОЙ стороне (`uiListPagerHandleCommand@0xE72600`):
	//   8259 `if (--cur < 1) cur = 1`, 8260 `if (++cur > total) cur = total`; затем LABEL_9 `req = cur` и 5483.
	Bind(TEXT("Find_SMate_PreBtn"),   [this]() { if (--SearchPage < 1) { SearchPage = 1; } SearchReqPage = SearchPage; SendSearch(); });            // 8259
	Bind(TEXT("Find_SMate_NextBtn"),  [this]() { if (++SearchPage > SearchLast) { SearchPage = SearchLast; } SearchReqPage = SearchPage; SendSearch(); });   // 8260
	Bind(TEXT("Find_SMate_EndBtn"),   [this]() { SearchReqPage = SearchLast; SendSearch(); });   // 8261: req = total
	Bind(TEXT("Find_SMate_WhisBtn"),  [this]() { WhisperToSelected(); });            // 8264
	Bind(TEXT("Find_SMate_PtyBtn"),   [this]() { InviteSelectedToParty(); });        // 8265 (R23)
	Bind(TEXT("Find_SMate_SMBtn"),    [this]() { ProposeToSelected(); });            // 8266
	Bind(TEXT("Find_SMate_CanBtn"),   [this]() { CloseSearchWindow(); });            // 8267 (R23)
	Bind(TEXT("Find_SMate_CloseBtn"), [this]() { CloseSearchWindow(); });            // 8299 (R23)
	Bind(TEXT("Find_SMate_EditBtn"),  [this]() { OpenIntroWindow(); });              // 8270 (R23)

	// ══ ★★★ T5 ЗАКРЫТ — ТРЁХПОЗИЦИОННЫЙ ПЕРЕКЛЮЧАТЕЛЬ ПОЛА 8202 ══════════════════════════════
	//   ЧТО БЫЛО. Порт вешал клик на 8203/8204/8205 через `Cast<UButton>` — а это Text-контролы
	//   с `NoSelect = "1"` (`gamemain_jui.ini:244599/244624/244649`), импортёр делает из них
	//   `UTextBlock`. `Cast<UButton>` возвращал nullptr, привязка молча не происходила, и фильтр
	//   пола НЕ РАБОТАЛ ВООБЩЕ (в журнал шли три предупреждения «кнопка не найдена»).
	//   КАК ПРАВИЛЬНО. Кликабельный контрол один — 8202 `Find_SMate_GenderSel_Btn`, скин
	//   `FindSMate_3Btn` типа `RadioButton` с ТРЕМЯ планами (`RE/docs/JuiSkin.ini:28586`,
	//   `ES 0/1/2` — Rect 0/80/160 по X). Импортёр рисует планы картинками
	//   `Find_SMate_GenderSel_Btn_<обход>_<план>` и кладёт НАД каждым по клик-кнопке
	//   `Find_SMate_GenderSel_Btn_OPT<план>` (`asda_jui_generic_umg.py`, ветка `RadioButton`).
	//   `CFindSoulmateWin_InitControls` ставит `SetSelectedIndex(0)`, а подписи 8203/8204/8205
	//   получают loc 11701/11702/11703 — то есть индекс позиции = значение фильтра.
	//   Отбор эталона (`soulmateParseSearchResult`, game.c:478207):
	//   `(f != 1 || rec[23] == 1) && (f != 2 || rec[23] == 2)` ⇒ фильтр РАВЕН байту пола записи.
	//   ⚠ НАХОДКА ПО ДАННЫМ: в `RawAssets/Text/GameText.csv` 11702 = «М», 11703 = «Ж», тогда как
	//     `ID` контролов и корейский `TextContent` разметки говорят обратное (8204 = 여 «женский»,
	//     8205 = 남 «мужской»). Это дефект РУССКОЙ таблицы текста, а не кода: и подпись позиции,
	//     и колонка «Пол» строки берут ОДНУ И ТУ ЖЕ строку `loc(11701 + пол)`, поэтому список
	//     остаётся самосогласованным. Правка таблицы — вне этого файла (см. отчёт).
	SexBtns.Reset();  SexPlanes.Reset();  SexCaptions.Reset();
	static const TCHAR* const kSexCaptionIdc[3] =
		{ TEXT("Find_SMate_All"), TEXT("Find_SMate_Female"), TEXT("Find_SMate_Male") };
	for (int32 i = 0; i < 3; ++i)
	{
		UButton* B = Cast<UButton>(FindWnd->GetWidgetFromName(
			FName(*FString::Printf(TEXT("Find_SMate_GenderSel_Btn_OPT%d"), i))));
		SexBtns.Add(B);
		SexPlanes.Add(PlaneOf(FindWnd, TEXT("Find_SMate_GenderSel_Btn"), i));
		SexCaptions.Add(Cast<UTextBlock>(FindWnd->GetWidgetFromName(FName(kSexCaptionIdc[i]))));
		if (!B)
		{
			UE_LOG(LogAsdaSoulmate, Warning,
				TEXT("[соулмейт] окно 8200: позиция %d контрола 8202 не найдена"), i);
			continue;
		}
		AsdaUi::BindButton(this, B, i, [this](int32 Which) { SetSexFilter(Which); }, PopupRelays);   // ★ U20: реле клика — общая точка   // ★★★ GC-якорь (см. `Relays`)
	}
	// ═══ ★★★ ВЫБОР СТРОКИ — С-3 ревизии расширения окон (07.10.2026) ═══════════════════════════════════
	//   БЫЛО: клик вешался на `UButton` с именем `Find_SMate_ListBase01..07` — таких кнопок в окне НЕТ
	//   (панель строки 8208 — скин `Box_WhiteOt20`, импортёр делает из неё картинку-план), `Cast` молча
	//   давал nullptr, и строки щелчком не выбирались вообще. Рамкой выбора служила синяя подсветка 8214.
	//   [ОРИГИНАЛ] (реверс `ui_core.c:73913-73950`, `social.c:8563-8585`, `ui_core.c:74151-74170`):
	//     • нажатие ЛКМ (событие 0xB) на панели строки k (контролы 8208..8250, k = (id−8208)/7) —
	//       `partyInvite_OnListSelect`: та же строка → `partyInvite_CloseList` (рамка 8257 скрыта, выбор −1);
	//       иначе ЗЕЛЁНАЯ рамка 8257 `Find_SMate_ListSel` встаёт к строке (`SetField74_75(x+9, y−1)` от
	//       панели строки = ровно авторское положение рамки у строки 1) и показывается; размер списка
	//       НЕ проверяется — выбирается и пустая строка (кнопки 8264/8266 тогда закрывают выбор, R24);
	//     • курсор вошёл в контрол строки (событие 8) — `uiSelectTabPage_2D10(…, 1)`: все семь СИНИХ подсветок
	//       8214+7·k гаснут, загорается подсветка этой строки; ушёл (0xA) — гаснут все;
	//     • сети нет: выбор — чисто клиентский индекс `+480`.
	//   Кнопки нет и в оригинале, поэтому клик и наведение ловит покадровый опрос `TickSearchRows` по
	//   ЖИВОЙ геометрии панели строки — тот же способ, что у строк окна 8500 (`TickRowHover`).
	//   Рамку 8257 (восемь планов девятисреза) собираем здесь; её авторская раскладка — у строки 1.
	SelFramePlanes.Reset();
	SelFrameBase.Reset();
	HoverRow = -1;
	// ★★ С-2 ревизии `widen_fixes2_REVIEW.md` (07.10.2026): кэши колонок 8275/8276 — от ПРЕЖНЕГО дерева окна.
	//   БЫЛО: при пересоздании 8200 (вход в мир / портал → `RevealWorld` → `CloseAll`) они оставались по 7 элементов,
	//   `EnsureRowSpecialLabels` выходил рано — подписи созвездия/влечения не достраивались в новом дереве, текст уходил
	//   в старое (имя знака и совместимость пустые), а «мёртвые» ячейки мерились по планам без геометрии (клик не съедался).
	//   СТАЛО: сброс здесь, вместе с рамкой 8257; достроит `RefreshSearchRows` → `EnsureRowSpecialLabels` в новом дереве.
	RowConstTxt.Reset();
	RowMatchTxt.Reset();
	RowConstPlane.Reset();
	RowMatchPlane.Reset();
	FindWnd->WidgetTree->ForEachWidget([this](UWidget* W)
	{
		// `Find_SMate_ListSel_` (за «ListSel» — подчёркивание) ≠ подсветки строк `Find_SMate_ListSel0N_`.
		if (W && W->GetName().StartsWith(TEXT("Find_SMate_ListSel_")))
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(W))
			{
				SelFramePlanes.Add(W);
				SelFrameBase.Add(W, CS->GetOffsets());
			}
		}
	});
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно 8200: планов рамки выбора 8257 найдено %d (ожидается 8)"),
		SelFramePlanes.Num());
	// Отрисовать выбранную позицию БЕЗ запроса: эталон в InitControls зовёт только
	//   `SetSelectedIndex(0)`, а список запрашивает уже показ окна (`findSoulmateWin_Show`).
	ApplySexFilterVisual();

	// 8269 `Find_SMate_MyIntroTxt` — СОХРАНЁННЫЙ сервером текст «о себе» (приём 6080,
	//   `uiSetNameText@0xE749B0`); в порту он уже лежит в `UAsdaNetSubsystem::SelfIntroduction`.
	UGameInstance* GI = GetGameInstance();
	if (const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		IntroText = Net->SelfIntroduction;
	}
	SetTxt(TEXT("Find_SMate_MyIntroTxt"), IntroText);
}

// ═══════════════════════════════ ОКНО 8300 «Обо мне» ══════════════════════════════════════════
// Разбор — `CFindSoulmateWin_InitControls@0xE71240` (8300..8305, game.c:477452-477505) и
//   `uiListPagerHandleCommand@0xE72600` (8270 → показать, 8304 → 6080 + скрыть, 8305/8399 → скрыть).
//   ⚠ 8300 `My_Introduction_Win` — ОТДЕЛЬНОЕ верхнеуровневое окно разметки, свой WBP.
void UAsdaSoulmateSubsystem::OpenIntroWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Дедуп №2, В1-B (U22): каркас `FAsdaJuiWindowHandle::EnsureLazy` (шаги и порядок прежние).
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kIntroWindowId, kIntroWbp, kWinZ + 10).EnsureLazy(*WM, IntroWnd, /*bReady*/true,
		ESlateVisibility::SelfHitTestInvisible,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulmate, Error, TEXT("[соулмейт] WBP_My_Introduction_Win не загрузился")); },
		[this](UAsdaWindowSubsystem&)
		{
			auto Bind = [this](const TCHAR* Idc, TFunction<void()> Fn)
			{
				if (UButton* B = Cast<UButton>(IntroWnd->GetWidgetFromName(FName(Idc))))
				{
					UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
					R->Callback = [Fn](int32) { Fn(); };
					B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
					AsdaUi::AnchorRelay(PopupRelays, R, B);   // ★★★ GC-якорь (см. `Relays`)
				}
			};
			Bind(TEXT("My_Intro_OKBtn"),     [this]() { SendIntroText();    });   // 8304
			Bind(TEXT("My_Intro_CancelBtn"), [this]() { CloseIntroWindow(); });   // 8305
			Bind(TEXT("My_Intro_ClsBtn"),    [this]() { CloseIntroWindow(); });   // 8399
			auto SetTxt = [this](const TCHAR* Idc, const FString& S)
			{
				if (UTextBlock* T = Cast<UTextBlock>(IntroWnd->GetWidgetFromName(FName(Idc))))
				{
					T->SetText(FText::FromString(S));
				}
			};
			auto SetCap = [this](const TCHAR* Idc, const FString& S)
			{
				if (UTextBlock* T = Cast<UTextBlock>(
					IntroWnd->GetWidgetFromName(FName(*(FString(Idc) + TEXT("_Text"))))))
				{
					T->SetText(FText::FromString(S));
				}
			};
			SetTxt(TEXT("My_Intro_Move"),      Loc(11750));   // 8301 заголовок
			SetTxt(TEXT("My_Intro_Guide"),     Loc(11751));   // 8303 пояснение
			SetCap(TEXT("My_Intro_OKBtn"),     Loc(2423));    // 8304 «ОК»
			SetCap(TEXT("My_Intro_CancelBtn"), Loc(2424));    // 8305 «Отмена»
		},
		FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseIntroWindow(); }));
	if (Ens == EAsdaJuiEnsure::LoadFailed) { return; }
	// Поле ввода 8302 — многострочное (`sub_930E6C(ctrl, 1)` + `wndToggleStyleFlag(…, 1)` эталона).
	// ★ 10.10.2026 [ПОРТ] (barber_minors): эталон `findSoulmateWinOpen@0xE72C20` ставит фокус (`CControl_SetFocus(поле, 1)`)
	//   ДО показа окна — у `CControl` это работает. В UMG фокус на скрытом виджете не встаёт (Slate его отбрасывает), и
	//   каретки в поле 8302 после открытия не было (найдено самотестом `asda.UI.SelfTestEscOnTop`, раздел F). Поэтому
	//   сначала окно показывается, затем фокус — итог тот же, что у эталона.
	UWidget* Edit = nullptr;
	if (UMultiLineEditableText* E =
		Cast<UMultiLineEditableText>(IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox"))))
	{
		E->SetText(FText::FromString(IntroText));
		Edit = E;
	}
	else if (UEditableText* E1 = Cast<UEditableText>(IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox"))))
	{
		E1->SetText(FText::FromString(IntroText));
		Edit = E1;
	}
	WM->SetWindowVisible(kIntroWindowId, true);
	if (Edit)
	{
		Edit->SetKeyboardFocus();
		// Только что показанный виджет Slate находит в дереве окна лишь со следующего кадра — тогда фокус ставится повторно.
		if (!Edit->HasKeyboardFocus())
		{
			TWeakObjectPtr<UWidget> WeakEdit(Edit);
			TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
			GI->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [WeakEdit, WeakWM]()
			{
				// ★ 10.10.2026 (МЕЛК-4 ревизии barber_minors): условие — видимость ОКНА 8300, а не только своя видимость поля
				//   (окно могли закрыть в тот же кадр; `IsVisible` поля смотрит лишь его собственный флаг).
				//   ★ 10.10.2026 (modal_lock2, МЕЛК-5 ревизии modal_lock): и модалки нет — поднявшаяся в тот же кадр модалка
				//   (флаг `+0x200`, `IsAnyModalOpen`) держит клавиатуру у себя, поле её не отнимает.
				UWidget* E = WeakEdit.Get();
				const UAsdaWindowSubsystem* M = WeakWM.Get();
				if (E && M && M->IsWindowVisible(kIntroWindowId) && E->IsVisible() && !M->IsAnyModalOpen()) { E->SetKeyboardFocus(); }
			}));
		}
	}
}

// Порт `uiHideCtrlClearFocus@0xE72CC0` (8305/8399, и после 8304): `SetVisible(8300, 0)` + `SetFocus(8302, 0)`.
//   ★ 10.10.2026 [ОРИГИНАЛ] (barber_minors): снятия фокуса с поля 8302 раньше не было (та же половина, что у 8200).
void UAsdaSoulmateSubsystem::CloseIntroWindow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kIntroWindowId, false);
	}
	if (IsIntroEditFocused() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

// 8304 `My_Intro_OKBtn`: эталон читает текст поля 8302, шлёт его пакетом 6080
//   (`netSecure_sendTextInput@0xE74220`: замена ' → ` и обрез до 128 байт) и прячет окно.
void UAsdaSoulmateSubsystem::SendIntroText()
{
	if (!IntroWnd) { return; }
	FString Text;
	if (UMultiLineEditableText* E =
		Cast<UMultiLineEditableText>(IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox"))))
	{
		Text = E->GetText().ToString();
	}
	else if (UEditableText* E1 = Cast<UEditableText>(IntroWnd->GetWidgetFromName(TEXT("My_Intro_EditBox"))))
	{
		Text = E1->GetText().ToString();
	}
	// Обрез до 128 делает и эталон (`if (textLen > 128) textLen = 128`); замену ' → ` —
	//   сам отправитель 6080 (общая точка `SendSoulmateIntroduction6080`).
	//   [ПОРТ] С4 08.10.2026: обрезка в БАЙТАХ ПРОВОДА 6080, по границе символа — как у окна персонажа
	//   (`AsdaCharaInfoSubsystem.cpp`, тот же `TruncateToWireLimit`).
	//   ★ [ОРИГИНАЛ] 08.10.2026 (пакет 2, МЕЛК-4): поле 128 б БЕЗ обязательного нуля, как эталон (`netSecure_sendTextInput`,
	//   network.c:30443-30448: `if (textLen > 128) textLen = 128`, memcpy в поле 128) — русский текст до 128 символов.
	//   Сервер это допускает: GameServer `Recv_SoulmateIntroduction@0x498960` пересылает 0x80 б как есть (2262), DBManager
	//   `sub_4303B0` пишет `'%s'` в `UPCharIntroUp` с параметром `@Intro VARCHAR(128)` (столбец `CharIntroduce.Intro
	//   varchar(128)`) — хвост за 128-м байтом параметр режет молча; кадр 2262 кладётся в буфер 140 б.
	if (Text.Len() > 128) { Text.LeftInline(128); }
	Text = FAsdaLoginData::TruncateToWireLimit(Text, 128, 128);
	IntroText = Text;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendSoulmateIntroduction6080(Text);
	}
	// 8269 обновится ответом сервера (6080 S→C → `SelfIntroduction`); показываем сразу, чтобы
	//   игрок видел результат, — при расхождении сервер перепишет поле своим текстом.
	if (FindWnd)
	{
		if (UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(TEXT("Find_SMate_MyIntroTxt"))))
		{
			T->SetText(FText::FromString(IntroText));
		}
	}
	CloseIntroWindow();
}

// 8265 `Find_SMate_PtyBtn` — порт `party_inviteSelectedEntry@0xE734C0` (game.c:477958-478010).
//   ⚠ ВСЕ гейты эталона («я в отряде», «я главный», «уже приглашаю», нотисы 1045/1056/1089)
//   живут в отряде и уже портированы в `UAsdaPartySubsystem::OpenInviteWindow` — второй копии
//   заводить нельзя (закон `port-shared-points`). Здесь остаётся ровно то, что делает эталонная
//   ветка до вызова: проверка выбора и передача userNum + имени выбранной строки.
void UAsdaSoulmateSubsystem::InviteSelectedToParty()
{
	if (SelectedRow < 0) { return; }
	if (!Candidates.IsValidIndex(SelectedRow))
	{
		// R24: выбор за пределами списка ⇒ эталон ЗАКРЫВАЕТ список (`partyInvite_CloseList`).
		SelectRow(-1);
		return;
	}
	const FAsdaSoulmateCandidate& C = Candidates[SelectedRow];
	UGameInstance* GI = GetGameInstance();
	if (UAsdaPartySubsystem* Party = GI ? GI->GetSubsystem<UAsdaPartySubsystem>() : nullptr)
	{
		Party->OpenInviteWindow(C.UserNum, C.Name);
	}
}

// 5483 (конверт B, payload 2) — `netSendMaskedPacket_op5483@0xE737D0`: [0] = фильтр пола `+496`,
//   [1] = страница ЗАПРОСА `+488` (порядок в проводе держит сам отправитель `SendSoulmateSearch5483`).
//   [ОРИГИНАЛ] Сразу после отправки — `sub_93BA42` → `sub_E73430`: вектор кандидатов очищается, cur = total = 1
//   (окно не перерисовывается — строки сменит 5485). Отправка без проверки успеха — как у эталона.
void UAsdaSoulmateSubsystem::SendSearch()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->SendSoulmateSearch5483((uint8)FMath::Clamp(SearchReqPage, 0, 255), (uint8)SexFilter);
		}
	}
	Candidates.Reset();
	SearchPage = 1;
	SearchLast = 1;
}

// Позиция трёхпозиционного контрола 8202. Эталон меняет её штатным `SetSelectedIndex` контрола
//   (`uiFindSoulmateWinOnCommand` case `0x10501`: `*(this + 496) = a5`), рисование выбранной
//   позиции берёт на себя сам контрол. У нас позиции — три плана + три кнопки-опции, поэтому
//   вид переключаем общей точкой `AsdaUi::ApplyTabState` (плашка = кадр 2 скина, подпись и стиль
//   кнопки — оттуда же). Ничего не выдумано: кадры берутся из `FindSMate_3Btn`.
// Только ВИД выбранной позиции — без сети (эталонный `SetSelectedIndex` в InitControls).
void UAsdaSoulmateSubsystem::ApplySexFilterVisual()
{
	AsdaUi::ApplyTabState(SexPlanes, SexCaptions, SexBtns, SexFilter,
		SexIdlePlanes, SexIdleStyles, SexIdleCaptions);
}

void UAsdaSoulmateSubsystem::SetSexFilter(int32 Mode)
{
	SexFilter = FMath::Clamp(Mode, 0, 2);
	ApplySexFilterVisual();
	// ⚠ ОТСТУПЛЕНИЕ, ПОМЕЧЕНО. Эталон по смене позиции 8202 запрос НЕ шлёт: он лишь кладёт
	//   индекс в `win+496` (`uiFindSoulmateWinOnCommand` case `0x10501`), а список запрашивает
	//   кнопка 8207 «Поиск». Порт шлёт сразу, чтобы на экране не остался список прошлого
	//   фильтра; вернуть оригинал — убрать две строки ниже.
	SearchReqPage = 1;
	SendSearch();
}

void UAsdaSoulmateSubsystem::SetPage(int32 Page)
{
	// Эталон зажимает страницу теми же границами (8259: `if (--cur < 1) cur = 1`;
	//   8260: `if (++cur > total) cur = total`) и ШЛЁТ ЗАПРОС ВСЕГДА, даже если страница
	//   не изменилась. Раннего выхода `if (P == SearchPage) return;` у него нет — снят.
	// ★ Волна 1-А: затем `LABEL_9: +488 = +484` — запрос уходит с ТЕКУЩЕЙ страницей.
	SearchPage = FMath::Clamp(Page, 1, FMath::Max(1, SearchLast));
	SearchReqPage = SearchPage;
	SendSearch();
}

void UAsdaSoulmateSubsystem::SelectRow(int32 Row)
{
	// [ОРИГИНАЛ] `partyInvite_OnListSelect`: индекс строки берётся из ID контрола (0..6) без сверки с размером
	//   списка — пустая строка тоже выбирается. −1 (и любой индекс вне 0..6) — `partyInvite_CloseList`.
	SelectedRow = (Row >= 0 && Row < 7) ? Row : -1;
	ApplySelFrame();
}

// Панель строки 8208+7·k: картинка-план `Find_SMate_ListBase0N_<обход>_0` внутри `Find_SMate_ListBase0N_grp`.
//   ⚠ Ищем ВНУТРИ группы строки и только `UImage`: префикс `Find_SMate_ListBase01_` совпал бы и с самой
//   группой `Find_SMate_ListBase01_grp` (закон `jui-importer-node-names`).
UWidget* UAsdaSoulmateSubsystem::RowPlate(int32 Row) const
{
	if (!FindWnd) { return nullptr; }
	const UPanelWidget* Grp = Cast<UPanelWidget>(FindWnd->GetWidgetFromName(
		FName(*FString::Printf(TEXT("Find_SMate_ListBase%02d_grp"), Row + 1))));
	if (!Grp) { return nullptr; }
	const FString Pref = FString::Printf(TEXT("Find_SMate_ListBase%02d_"), Row + 1);
	for (int32 i = 0; i < Grp->GetChildrenCount(); ++i)
	{
		UWidget* C = Grp->GetChildAt(i);
		if (C && C->IsA<UImage>() && C->GetName().StartsWith(Pref)) { return C; }
	}
	return nullptr;
}

// ★ [ОРИГИНАЛ] Дети строки разбираются РАНЬШЕ панели (`uiContainerHandleMouse@0x12DCC50`: рекурсия по детям,
//   первый, кто съел нажатие, возвращает 1). Нажатие съедают контролы строки БЕЗ `NoSelect`:
//   контейнер сердечек (ID −1, 36×35 справа от «о себе», в порту `Black_grp*` — родитель `…_MARK1`) и
//   ячейки созвездия/влечения 8275/8276. Их ID вне 8208..8250 ⇒ `partyInvite_OnListSelect` ничего не делает,
//   строка НЕ выбирается. Тексты пола/уровня/имени/профессии/«о себе» — `NoSelect=1`, их нажатие идёт панели.
bool UAsdaSoulmateSubsystem::IsRowClickSwallowed(int32 Row, const FVector2D& Cursor) const
{
	// D17: по умолчанию «мёртвых» колонок нет — дефект оригинала исправлен (ключ `asda.Soul.RowClickFullWidth`).
	if (CVarSoulmateRowClickFullWidth.GetValueOnGameThread() != 0) { return false; }
	auto Inside = [&Cursor](const UWidget* W)
	{
		FVector2D Mn, Mx;
		return W && AsdaUi::WidgetAbsRect(W, Mn, Mx)
			&& Cursor.X >= Mn.X && Cursor.X <= Mx.X && Cursor.Y >= Mn.Y && Cursor.Y <= Mx.Y;
	};
	// Контейнер сердечек: родитель картинки `Find_SMate_ListInt0N_MARK1_<обход>_0`.
	if (const UWidget* Mark = AsdaUi::FindNodeByPrefix(FindWnd,
		*FString::Printf(TEXT("Find_SMate_ListInt%02d_MARK1_"), Row + 1)))
	{
		if (Inside(Mark->GetParent())) { return true; }
	}
	// Ячейки 8275/8276 — по ПЛАНАМ (прямоугольник контрола эталона, М-4 07.10.2026). Подписи над ними сдвинуты
	//   выравниванием текста на 0.5 px, и между двумя ячейками оставалась щель, где строка выбиралась.
	//   Плана нет (разметка не распознана) — прежний путь по подписи.
	auto Cell = [](const TArray<TObjectPtr<UWidget>>& Planes, const TArray<TObjectPtr<UTextBlock>>& Labels, int32 R) -> const UWidget*
	{
		if (Planes.IsValidIndex(R) && Planes[R]) { return Planes[R]; }
		return Labels.IsValidIndex(R) ? Labels[R].Get() : nullptr;
	};
	if (Inside(Cell(RowConstPlane, RowConstTxt, Row))) { return true; }
	if (Inside(Cell(RowMatchPlane, RowMatchTxt, Row))) { return true; }
	return false;
}

// Перенос зелёной рамки 8257 к выбранной строке — порт `partyInvite_OnListSelect` / `partyInvite_CloseList`.
//   Смещение = (панель строки k) − (панель строки 1) в координатах родителя рамки; планы ставим от АВТОРСКОЙ
//   раскладки (`SelFrameBase`), поэтому повторный вызов ничего не накапливает.
void UAsdaSoulmateSubsystem::ApplySelFrame()
{
	if (SelFramePlanes.Num() == 0) { return; }
	auto HideAll = [this]()
	{
		for (UWidget* W : SelFramePlanes) { if (W) { W->SetVisibility(ESlateVisibility::Collapsed); } }
	};
	if (SelectedRow < 0) { HideAll(); return; }
	const UWidget* Ref = RowPlate(0);
	const UWidget* Cur = RowPlate(SelectedRow);
	const UPanelWidget* Parent = SelFramePlanes[0] ? SelFramePlanes[0]->GetParent() : nullptr;
	if (!Ref || !Cur || !Parent)
	{
		UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] окно 8200: нет панели строки %d или родителя рамки 8257 — рамку прячу"),
			SelectedRow + 1);
		HideAll();
		return;
	}
	// Живая геометрия (окно видно — клик пришёл по ней же); перевод в локальные юниты родителя рамки.
	const FGeometry PG = Parent->GetCachedGeometry();
	const FGeometry RG = Ref->GetCachedGeometry();
	const FGeometry CG = Cur->GetCachedGeometry();
	if (PG.GetLocalSize().X <= 0.f || RG.GetLocalSize().X <= 0.f || CG.GetLocalSize().X <= 0.f)
	{
		UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] окно 8200: у строки %d ещё нет геометрии — рамку прячу"),
			SelectedRow + 1);
		HideAll();
		return;
	}
	const FVector2D D = FVector2D(PG.AbsoluteToLocal(CG.GetAbsolutePosition()))
	                  - FVector2D(PG.AbsoluteToLocal(RG.GetAbsolutePosition()));
	for (UWidget* W : SelFramePlanes)
	{
		if (!W) { continue; }
		const FMargin* B = SelFrameBase.Find(W);
		UCanvasPanelSlot* CS = AsdaUi::SlotOf(W);
		if (!B || !CS) { continue; }
		// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U15): сдвиг от базы по правилу анкоров (`AsdaUi::ShiftedOffsets`); было `Left/Top += d`.
		CS->SetOffsets(AsdaUi::ShiftedOffsets(*B, CS->GetAnchors(), D));
		W->SetVisibility(ESlateVisibility::HitTestInvisible);   // рамка мышь не ловит (NoSelect=1)
	}
}

bool UAsdaSoulmateSubsystem::IsTickable() const
{
	if (IsTemplate()) { return false; }
	// ★ 23.10.2026: панель Друга 6500 — опрос мыши, подсказка и возврат после смены карты.
	if (bPopWanted || bPopTipShown) { return true; }
	// ★ 26.10.2026 (smmessage): записки — значки 9 по очереди и возврат окон-свитков после смены карты.
	if (NoteQueue.Num() > 0 || NoteIconsShown > 0 || bNoteWinWanted[0] || bNoteWinWanted[1] || bNoteWinWanted[2]) { return true; }
	return FindWnd != nullptr && IsSearchWindowShown();
}

void UAsdaSoulmateSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.Soulmate");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	TickPopCard();   // ★ 23.10.2026: панель Друга 6500 (свой ранний выход внутри)
	TickNotes();     // ★ 26.10.2026: записки Другу (свой ранний выход внутри)
	if (!FindWnd) { return; }
	if (!IsSearchWindowShown())
	{
		// Окно спрятано — за мышью не следим: без сброса удержанная кнопка не дала бы фронта при показе.
		RowMouse.Reset();
		return;
	}
	// ★ М-3: при скрытом окне тик не идёт (`IsTickable`). Пропуск кадров = окно было скрыто → кадр мыши
	//   сбрасываем, как раньше делала ветка «спрятано» выше: удержанная до показа кнопка даст фронт.
	if (RowPollFrame + 1 != GFrameCounter) { RowMouse.Reset(); }
	RowPollFrame = GFrameCounter;
	TickSearchRows();
}

void UAsdaSoulmateSubsystem::TickSearchRows()
{
	RowMouse.Update();
	UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// Мышь достаётся окну 8200, только если в этой точке оно ВЕРХНЕЕ (порядок z и модальный гейт —
	//   `FindWindowCatchingAt`, тот же разбор, что у подъёма/перетаскивания). Иначе строка под чужим окном.
	const bool bOurs = WM && WM->FindWindowCatchingAt(RowMouse.Cursor) == kFindWindowId;
	int32 Over = -1;
	if (bOurs)
	{
		for (int32 Row = 0; Row < 7; ++Row)
		{
			if (AsdaUi::IsCursorOverWidget(RowPlate(Row))) { Over = Row; break; }
		}
	}
	// Наведение — `uiSelectTabPage_2D10`: гасим все семь подсветок 8214+7·k и зажигаем ту, что под курсором.
	if (Over != HoverRow)
	{
		HoverRow = Over;
		for (int32 Row = 0; Row < 7; ++Row)
		{
			if (UWidget* Hl = AsdaUi::FindNodeByPrefix(FindWnd, *FString::Printf(TEXT("Find_SMate_ListSel%02d_"), Row + 1)))
			{
				Hl->SetVisibility((Row == HoverRow) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			}
		}
	}
	// Нажатие ЛКМ — `partyInvite_OnListSelect`: та же строка снимает выбор, другая — переносит рамку.
	if (RowMouse.bLmbDown && Over >= 0 && !IsRowClickSwallowed(Over, RowMouse.Cursor))
	{
		const int32 Was = SelectedRow;
		SelectRow((SelectedRow == Over) ? -1 : Over);
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно 8200: нажатие на строку %d — выбор %d → %d"),
			Over + 1, Was, SelectedRow);
	}
}

// Порт `soulmateParseSearchResult@0xE73E00`. Запись переменной длины: 33 + rec[31].
//   Раскладка (§5.6): rec[0] уровень · rec[1] профессия · rec[3..22] имя · rec[23] пол
//   · rec[24..25] i16 userNum (−1 = конец) · rec[26..29] i32 charId (−1 = конец)
//   · rec[30] число участников · rec[31] длина хвоста · rec[32] признак созвездия
//   · rec[33..] самопредставление.
void UAsdaSoulmateSubsystem::OnSearch(const TArray<uint8>& Body)
{
	// ★★★ ИСПРАВЛЕН СДВИГ НА 9 БАЙТ (аудит 31.08 — было КРИТИЧНО).
	//   Диспетчер эталона зовёт `soulmateParseSearchResult(g_pFindSoulmateWin, pktBuf)` с
	//   ЦЕЛЫМ пакетом (game.c:15192), тогда как всем прочим обработчикам соулмейта он передаёт
	//   `&pktBuf[9]`. Поэтому эталонные смещения +9/+10/+11 — это ПАКЕТНЫЕ, а в теле (которое
	//   `UAsdaNetSubsystem` отдаёт уже без 9-байтового заголовка) им отвечают 0/1/2.
	//   Порт читал body[9]/body[10] и записи с body[11] — весь разбор ехал на 9 байт.
	//   Порядок полей тоже уточнён (game.c:478191-478194):
	//     `win[123] = pkt[9]`  = ВСЕГО СТРАНИЦ (totalPages, потолок для 8260/8261)
	//     `win[121] = pkt[10]` = ТЕКУЩАЯ СТРАНИЦА (curPage, её печатает 8262)
	//   — что подтверждает `uiListPagerHandleCommand@0xE72600` (game.c:477618-477633).
	if (Body.Num() < 2) { return; }
	SearchLast = FMath::Max(1, (int32)Body[0]);
	SearchPage = FMath::Max(1, (int32)Body[1]);

	UGameInstance* GI = GetGameInstance();
	// ★ ВОЛНА 1-А (06.10.2026): список НЕ очищается — эталон ДОПИСЫВАЕТ (`sub_937FB4` = push_back, пока размер < 7).
	//   Очищает его отправка 5483 (`SendSearch`). Сервер режет страницу на несколько 5484 при теле > 650 б
	//   (`Send_SoulSearch@0x54DFD0`), и прежний `Candidates.Reset()` здесь терял все записи, кроме последнего пакета.
	int32 Ofs = 2;
	// Записи, которые не войдут (вектор полон), эталон ВСЁ РАВНО разбирает и перешагивает — порт тоже (лимит — в push).
	while (Ofs + 33 <= Body.Num())
	{
		const uint8* R = Body.GetData() + Ofs;
		const int32 UserNum = AsdaBytes::RdI16(R + 24);
		const int32 CharId  = AsdaBytes::RdI32(R + 26);
		if (UserNum == -1 || CharId == -1) { break; }        // маркер конца списка

		const int32 TailLen = (int32)R[31];
		// ★★★ ЛЕЧЕНИЕ ДЕФЕКТА D7: в эталоне `memcpy(v11, …, pRecord[31])` копирует до 255 байт в
		//   буфер char[203] и затирает стек. Здесь длина зажимается и по буферу (203), и по телу.
		const int32 SafeTail = FMath::Clamp(FMath::Min(TailLen, 203), 0, FMath::Max(0, Body.Num() - (Ofs + 33)));
		if (SafeTail != TailLen)
		{
			UE_LOG(LogAsdaSoulmate, Warning,
				TEXT("[соулмейт] 5484: длина самопредставления %d обрезана до %d (защита от D7)"),
				TailLen, SafeTail);
		}

		// Фильтр пола окна (эталон: поле +496; 0 = без фильтра).
		const uint8 Sex = R[23];
		if (SexFilter == 0 || (int32)Sex == SexFilter)
		{
			FAsdaSoulmateCandidate C;
			C.UserNum   = UserNum;
			C.CharId    = CharId;
			C.Members   = R[30];
			C.Sex       = Sex;
			C.SexText   = Loc(11701 + (int32)Sex);                    // loc 11701 + пол
			C.LevelText = FString::Printf(TEXT("%d"), (int32)R[0]);   // «%d» от rec[0]
			C.Name      = FAsdaLoginData::DecodeCp1251(R + 3, 20);
			C.JobText   = Loc(12300 + (int32)R[1]);                   // loc 12300 + профессия
			C.ConstFlag = (int32)R[32];
			// ★★★ T9: колонки 7 и 8 строки. Эталон `soulmateWin_RefreshList@0xE72EE0`
			//   (game.c:477884-477895) считает их прямо в отрисовке:
			//     `cfgFindRecordByKey65(g_LoginCtxBuf, entry+328)` → имя знака в колонку 8275;
			//     `uiFormatPartyRelationNotice(g_LoginCtxBuf, buf, entry+328)` → строка в 8276
			//     И ВОЗВРАТ 0..2 = номер одной из трёх картинок 8311..8313.
			//   Обе функции уже портированы в `UAsdaConstellationSubsystem` (`SignName` /
			//   `MatchIndex`); свой знак берётся оттуда же (`SelfSign`).
			if (const UAsdaConstellationSubsystem* Con =
				GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr)
			{
				C.ConstName = Con->SignName((uint8)C.ConstFlag);
				C.MatchIcon = Con->MatchIndex(Con->SelfSign(), (uint8)C.ConstFlag, C.MatchText);
			}
			if (SafeTail > 0) { C.Intro = FAsdaLoginData::DecodeCp1251(R + 33, SafeTail); }
			// `if (stl_vectorSize(win + 125) < 7) sub_937FB4(win + 125, entry)` — лимит 7 на ВЕСЬ список, а не на пакет.
			if (Candidates.Num() < 7) { Candidates.Add(MoveTemp(C)); }
			Ofs += 33 + TailLen;
		}
		else
		{
			// ★ ДЕФЕКТ D10 (см. `asda.Soul.Search.FixStride` в шапке файла).
			Ofs += (CVarSoulmateSearchStride.GetValueOnGameThread() != 0) ? (33 + TailLen) : 33;
		}
	}

	// [ОРИГИНАЛ] 5484 окно НЕ перерисовывает и выбор не снимает — это делает 5485 (`findSoulmateWin_Show`).
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5484: кандидатов=%d (дописано) страница %d/%d фильтр=%d — ждём 5485"),
		Candidates.Num(), SearchPage, SearchLast, SexFilter);
}

// ★★★ T9 — ДВЕ КОЛОНКИ, КОТОРЫХ У ИМПОРТЁРА НЕТ ВОВСЕ.
//
//   ЧТО В ДАННЫХ. Контролы 8275 `pList_Constellation` (2*row + 8275) и 8276 `pList_ConsteMatch`
//   (2*row + 8276) в `gamemain_jui.ini:241561/241585` имеют ПУСТОЙ `ID`, ПУСТОЙ `CaptionText`
//   И ПУСТОЙ `EXS TextContent` (скин `Black`, 120×17, `TextAlign = 5`, `TextColor = FFBEBEBE`).
//   Импортёр называет узлы так: интерактив и Text-контрол — по `ID`; безымянный контрол с
//   НЕПУСТЫМ `TextContent` — по правилу `{ЯКОРЬ}_TXT{NN}` (`asda_jui_parse.missing_text_nodes`);
//   а у этих двух пусто ВСЁ, поэтому текстового узла не создаётся ни одной из веток. В ассете от
//   них остались только планы-картинки `Black_<обход>_0`.
//
//   ПОЧЕМУ НЕ ПЕРЕГЕНЕРАЦИЯ. Правка импортёра = перегенерация UI, а она запрещена без разрешения
//   владельца (закон `no-ui-regeneration-without-permission`). Достраиваем узлы В РАНТАЙМЕ — тем
//   же приёмом, что `UAsdaCommunityFriendSubsystem::EnsureLabel` (закон `tools-no-import-side-effects`:
//   правка переживает любую перегенерацию JUI).
//
//   КАК НАХОДИМ ПЛАН, НЕ ОПИРАЯСЬ НА ИНДЕКС ОБХОДА. Индекс в имени `Black_<обход>_0` сдвигается
//   от любой правки разметки, поэтому берём ВСЕ планы со скином `Black` размера 120×17 в порядке
//   обхода дерева (он совпадает с порядком документа `.JUI`): их ровно 14 = 7 строк × 2 колонки,
//   и `2*row` — созвездие, `2*row + 1` — совместимость. Если счёт не сошёлся, НИЧЕГО не строим и
//   пишем в журнал: гадать, какой план чей, нельзя.
void UAsdaSoulmateSubsystem::EnsureRowSpecialLabels()
{
	if (!FindWnd || !FindWnd->WidgetTree) { return; }
	// Уже собрано — и именно в ТЕКУЩЕМ дереве окна (С-2, 07.10.2026: кэш от пересозданного окна не годится).
	if (RowConstTxt.Num() == 7 && RowMatchTxt.Num() == 7 && RowConstPlane.Num() == 7 && RowMatchPlane.Num() == 7
		&& (!RowConstPlane[0] || RowConstPlane[0]->IsIn(FindWnd))) { return; }

	// 1) Собрать планы-кандидаты в порядке обхода.
	struct FCand { int32 JuiIdx = 0; UImage* Img = nullptr; };
	TArray<FCand> Planes;
	FindWnd->WidgetTree->ForEachWidget([&Planes](UWidget* W)
	{
		UImage* Img = Cast<UImage>(W);
		if (!Img) { return; }
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(Img->GetName(), Idc, JuiIdx, Plane)) { return; }
		if (Plane != 0 || !Idc.Equals(TEXT("Black"), ESearchCase::IgnoreCase)) { return; }
		FVector2D Pos, Size;
		if (!UAsdaMiniStateSubsystem::GetSlotRect(Img, Pos, Size)) { return; }
		// Размер из разметки: 120×17 у обеих колонок; безымянная подложка иконок — 36×35.
		if (FMath::Abs(Size.X - 120.f) > 2.f || FMath::Abs(Size.Y - 17.f) > 2.f) { return; }
		Planes.Add({ JuiIdx, Img });
	});
	Planes.Sort([](const FCand& A, const FCand& B) { return A.JuiIdx < B.JuiIdx; });
	if (Planes.Num() != 14)
	{
		UE_LOG(LogAsdaSoulmate, Warning,
			TEXT("[соулмейт] окно 8200: планов колонок созвездия найдено %d вместо 14 — ")
			TEXT("узлы 8275/8276 не достраиваю (гадать нельзя)"), Planes.Num());
		RowConstTxt.Init(nullptr, 7);
		RowMatchTxt.Init(nullptr, 7);
		RowConstPlane.Init(nullptr, 7);
		RowMatchPlane.Init(nullptr, 7);
		return;
	}

	// 2) Достроить по подписи над каждым планом.
	const FLinearColor Col = AsdaUi::SrgbHexToLinear(TEXT("FFBEBEBE"));   // EXS TextColor разметки
	RowConstTxt.Init(nullptr, 7);
	RowMatchTxt.Init(nullptr, 7);
	RowConstPlane.Init(nullptr, 7);
	RowMatchPlane.Init(nullptr, 7);
	for (int32 Row = 0; Row < 7; ++Row)
	{
		for (int32 Which = 0; Which < 2; ++Which)
		{
			(Which ? RowMatchPlane : RowConstPlane)[Row] = Planes[2 * Row + Which].Img;   // М-4: прямоугольник ячейки
			// ⚠ UE 5.8: форматная строка `Printf` обязана быть ЛИТЕРАЛОМ (проверка на этапе
			//   компиляции), поэтому выбор делаем МЕЖДУ готовыми строками, а не между форматами.
			const FString Name = Which
				? FString::Printf(TEXT("Find_SMate_ListMatch%02d"), Row + 1)
				: FString::Printf(TEXT("Find_SMate_ListConst%02d"), Row + 1);
			UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(FName(*Name)));
			if (!T)
			{
				UImage* Plane = Planes[2 * Row + Which].Img;
				UPanelWidget* Parent = Plane ? Plane->GetParent() : nullptr;
				UCanvasPanelSlot* PS = Plane ? AsdaUi::SlotOf(Plane) : nullptr;
				if (!Parent || !PS) { continue; }
				T = FindWnd->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(*Name));
				// Кегль берём у соседней колонки той же строки — свой шрифт не выдумываем.
				if (UTextBlock* Donor = Cast<UTextBlock>(FindWnd->GetWidgetFromName(
					FName(*FString::Printf(TEXT("Find_SMate_ListJob%02d"), Row + 1)))))
				{
					T->SetFont(Donor->GetFont());
				}
				T->SetColorAndOpacity(FSlateColor(Col));
				// Подписи не ловят мышь — иначе строка перестанет выделяться кликом
				//   (закон `selfhittest-vs-hittest-invisible`).
				T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				UCanvasPanelSlot* NS = Cast<UCanvasPanelSlot>(Parent->AddChild(T));
				if (!NS) { continue; }
				AsdaUi::CopyCanvasGeometry(PS, NS);
				// `CControl_SetField924(ctrl, 5)` эталона (game.c:477320/477331) = TextAlign 5 =
				//   центр по горизонтали + центр по вертикали; та же маска, что в `EXS TextAlign`.
				AsdaUi::ApplyJuiTextAlign(T, /*AlignMask=*/0x1 | 0x4, /*JuiH=*/17.f);
			}
			(Which ? RowMatchTxt : RowConstTxt)[Row] = T;
		}
	}
	UE_LOG(LogAsdaSoulmate, Log,
		TEXT("[соулмейт] окно 8200: колонки созвездия и влечения достроены для 7 строк"));
}

void UAsdaSoulmateSubsystem::SetRowSpecialText(int32 Row, bool bMatch, const FString& S)
{
	const TArray<TObjectPtr<UTextBlock>>& Arr = bMatch ? RowMatchTxt : RowConstTxt;
	if (!Arr.IsValidIndex(Row) || !Arr[Row]) { return; }
	Arr[Row]->SetText(FText::FromString(S));
}

// Порт `soulmateWin_RefreshList@0xE72EE0`: в строке СЕМЬ текстовых колонок (пол, уровень, имя,
//   профессия, представление, созвездие, влечение) плюс три картинки, из которых горит одна.
//   Идентификаторы контролов (шаг 7 от 8208, 2 от 8275/8276, 3 от 8311) сняты структурным
//   разбором `CFindSoulmateWin_InitControls@0xE71240` (game.c:477300-477345).
void UAsdaSoulmateSubsystem::RefreshSearchRows()
{
	if (!FindWnd) { return; }
	EnsureRowSpecialLabels();
	// ★ Имена виджетов — idc из JUI с порядковым суффиксом 01..07 (закон jui-importer-widget-naming),
	//   а НЕ IDNum: импортёр называет интерактивные узлы по idc.
	auto Node = [this](const TCHAR* Prefix, int32 Row) -> UWidget*
	{
		return FindWnd->GetWidgetFromName(*FString::Printf(TEXT("%s%02d"), Prefix, Row + 1));
	};
	auto SetTxt = [&Node](const TCHAR* Prefix, int32 Row, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Node(Prefix, Row))) { T->SetText(FText::FromString(S)); }
	};

	for (int32 Row = 0; Row < 7; ++Row)
	{
		const bool bHas = Candidates.IsValidIndex(Row);
		const FAsdaSoulmateCandidate* C = bHas ? &Candidates[Row] : nullptr;
		SetTxt(TEXT("Find_SMate_ListSex"),  Row, bHas ? C->SexText   : FString());
		SetTxt(TEXT("Find_SMate_ListLv"),   Row, bHas ? C->LevelText : FString());
		SetTxt(TEXT("Find_SMate_ListName"), Row, bHas ? C->Name      : FString());
		SetTxt(TEXT("Find_SMate_ListJob"),  Row, bHas ? C->JobText   : FString());
		SetTxt(TEXT("Find_SMate_ListInt"),  Row, bHas ? C->Intro     : FString());
		// ★★★ T9: колонки 7 (8275 `pList_Constellation`) и 8 (8276 `pList_ConsteMatch`).
		SetRowSpecialText(Row, /*bMatch=*/false, bHas ? C->ConstName : FString());
		SetRowSpecialText(Row, /*bMatch=*/true,  bHas ? C->MatchText : FString());
		// ★★★ T9: три картинки `Find_SMate_ListInt<NN>_MARK1..3` (IDNum 3*row + img + 8311).
		//   Эталон гасит ВСЕ ТРИ и зажигает РОВНО ОДНУ — по индексу, возвращённому
		//   `uiFormatPartyRelationNotice` (game.c:477893-477895). ⚠ ДЕФЕКТ ОРИГИНАЛА D16:
		//   отрицательный возврат эталон не проверяет и адресовал бы `aRowSex[44*i + 32 - 4]`,
		//   то есть ЧУЖОЕ поле структуры. У нас −1 («таблица влечения не собрана») означает
		//   «не зажигать ни одной» — этого состояния в оригинале не бывает, порчи памяти нет.
		{
			const int32 Icon = bHas ? C->MatchIcon : -1;
			for (int32 Img = 0; Img < 3; ++Img)
			{
				const FString Idc = FString::Printf(TEXT("Find_SMate_ListInt%02d_MARK%d"), Row + 1, Img + 1);
				if (UWidget* W = AsdaUi::FindNodeByPrefix(FindWnd, *(Idc + TEXT("_"))))
				{
					W->SetVisibility((Img == Icon) ? ESlateVisibility::HitTestInvisible
					                               : ESlateVisibility::Collapsed);
				}
			}
		}
		// Синяя подсветка 8214+7·k — строка ПОД КУРСОРОМ (`uiSelectTabPage_2D10`, ведёт `TickSearchRows`).
		//   ★ С-3 (07.10.2026): прежде ею рисовался ВЫБОР — это было отступление; выбор теперь показывает
		//   зелёная рамка 8257, как в оригинале (`ApplySelFrame` ниже).
		//   ⚠ Имя узла: у 8214 `Find_SMate_ListSel01` импортёр СНЯЛ точное имя (не Text и не интерактив),
		//   остался только план `Find_SMate_ListSel01_<обход>_0` — ищем по префиксу.
		if (UWidget* Sel = AsdaUi::FindNodeByPrefix(FindWnd,
			*FString::Printf(TEXT("Find_SMate_ListSel%02d_"), Row + 1)))
		{
			Sel->SetVisibility((Row == HoverRow) ? ESlateVisibility::HitTestInvisible
			                                     : ESlateVisibility::Collapsed);
		}
	}
	// Зелёная рамка выбора 8257 — к `SelectedRow` (её снимает `partyInvite_CloseList` при показе окна).
	ApplySelFrame();

	// Счётчики страниц (idc из JUI: 8262 / 8263).
	if (UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(TEXT("Find_SMate_CurrPage"))))
	{
		T->SetText(FText::FromString(FString::FromInt(SearchPage)));
	}
	if (UTextBlock* T = Cast<UTextBlock>(FindWnd->GetWidgetFromName(TEXT("Find_SMate_LastPage"))))
	{
		T->SetText(FText::FromString(FString::FromInt(SearchLast)));
	}
}
