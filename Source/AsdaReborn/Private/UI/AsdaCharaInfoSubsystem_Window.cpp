// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem_Window.cpp — ★ R3-c 12.11.2026 (разрез AsdaCharaInfoSubsystem.cpp, дословный перенос): окно представления, статические тексты, подписи вкладок, высота окна, открыть/закрыть, вкладки.
//   Общее для частей — в AsdaCharaInfoSubsystem_Private.h; шапка и реверс — в AsdaCharaInfoSubsystem.cpp.
#include "UI/AsdaCharaInfoSubsystem.h"


#include "Components/PanelWidget.h"
#include "AsdaCharaInfoSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaCharaInfoSubsystem*.cpp

// ★★★ ОКНО «ОБО МНЕ» — `My_Introduction_Win`, IDNum 8300 (`gamemain_jui.ini:422364`, 300x175).
//   Открывается кнопкой 987 страницы «Информация» (эталон `UI_Window_OnControlMessage`, case 0x3DB →
//   `findSoulmateWinOpen@0xE72C20`). Полный разбор — `Reports/char_info_fix2.md` §4.
static const TCHAR* kIntroWbp =
	TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_My_Introduction_Win.WBP_My_Introduction_Win_C");
static const FName  kIntroWinId(TEXT("My_Introduction_Win"));

namespace CharInfoLoc {
// ── Строковые id (`RawAssets/Text/GameText.csv`), все взяты из эталонных вызовов ──
	static constexpr int32 kTabBasic     = 8050;    // «Информация»
	static constexpr int32 kTabData      = 8051;    // «Параметры»
	static constexpr int32 kTabElement   = 8052;    // «Характеристики»
	static constexpr int32 kFaction      = 10081;   // «Армия»
	static constexpr int32 kHonorPoints  = 10082;   // «Очки славы»
	static constexpr int32 kHonorGrade   = 10083;   // «Звание»

	static constexpr int32 kBarFirst     = 12029;   // 12029…12032 — подписи 12306…12309

	static constexpr int32 kEleHeadAtk   = 12047;   // «Характеристики персонажа»
	static constexpr int32 kEleHeadDef   = 12055;   // «Защита»
	static constexpr int32 kEleAtkFirst  = 12049;   // 12049…12054 — стихии атаки
	static constexpr int32 kEleDefFirst  = 12056;   // 12056…12061 — стихии защиты

	static constexpr int32 kFishing      = 111075;  // «Навык рыбалки»
	static constexpr int32 kCashLabel    = 12904;   // «Сейчас GXP»

	static constexpr int32 kOwnShop      = 107022;  // «Ты в своей лавке» — отказ открытия

	static constexpr int32 kEmoteHead0   = 12155;   // «- Обычные»
	static constexpr int32 kEmoteHead1   = 12156;   // «- Отряд»
	static constexpr int32 kEmoteHead2   = 12157;   // «- Навык»
	// Окно «Обо мне» 8300 (эталон `CFindSoulmateWin_InitControls`, `ui_core.c:73818-73880`)
	static constexpr int32 kIntroTitle   = 11750;   // 8301 заголовок «Обо мне»
	static constexpr int32 kIntroGuide   = 11751;   // 8303 «Информация о тебе появится только…»
	static constexpr int32 kIntroOk      = 2423;    // 8304 «ОК»
	static constexpr int32 kIntroCancel  = 2424;    // 8305 «Отмена»
}

// ★ Эталонная третья под-вкладка «Характеристики» (страница 984) в оригинале НЕДОСТИЖИМА: контрол 981
//   носит скин `TabBar_W109_2Btn` с ДВУМЯ элементами, а `CListBox_SetItemString@0x12F6570` начинается с
//   `if (index < размер списка)` — вызов с индексом 2 молча пропадает (отчёт §6, дефект Д1).
//   Рядом в той же разметке лежит родной трёхкнопочный вариант того же контрола (`TabBar_W72_3Btn`,
//   IDNum отключён = 4294967295).
//   ★★★ ПО УМОЛЧАНИЮ 0 — РОВНО КАК В ЭТАЛОНЕ (решение владельца, 2026-08-23). Данные страницы
//   ЖИВЫЕ (см. `Reports/char_info_window.md` §10: опции предметов 12…17 «Навык огня/воды/…»
//   имеют 2938 записей разброса в 774 группах, сервер считает шесть чисел и шлёт их в блоке 4060,
//   клиент по ним же выбирает значок доминирующей стихии в мини-статусе). Но САМА СТРАНИЦА 984 в
//   эталонной сборке недостижима, поэтому включать её без спроса нельзя (закон `no-invented-ui`).
//   1 = вернуть третью кнопку (исправление дефекта оригинала Д1) — по решению владельца.
static ASDA_DEV_CVAR(int32, CVarCharInfoEleTab,
	TEXT("asda.CharInfo.EleTab"), 1,   // ★ ВКЛЮЧЕНО по решению владельца 23.08.2026 (0 = вернуть эталон)
	TEXT("0 = как в эталоне (две под-вкладки, страница стихий недостижима); 1 = вернуть третью под-вкладку «Характеристики» (исправление дефекта Д1)."),
	ECVF_Default);

// ★ Эталон при открытии окна прячет строку кэша (990 `STATUS_Line01` невидим), строку зодиака
//   (945 гасится в конце init) и кнопку блога (989 гасится). 1 = повторяем эталон, 0 = показать всё
//   (для сверки «что там вообще нарисовано»).
// ★ «ЧЁРНАЯ БОРОДА» снизу окна. Разбор и доказательство — у `FitWindowHeight` в этом же файле.
//   1 = рамка окна ужимается по содержимому (исправление дефекта оригинала), 0 = эталонные 580 px.
static ASDA_DEV_CVAR(int32, CVarCharInfoFitHeight,
	TEXT("asda.CharInfo.FitHeight"), 1,
	TEXT("1 = высота окна 900 подгоняется под содержимое (нет пустой полосы снизу); 0 = как в разметке (228x580)."),
	ECVF_Default);

// Отступы подгонки: под последней иконкой лотка и под нижним краем содержимого окна.
static constexpr float kFitPanelPad = 10.f;
static constexpr float kFitWinPad   = 14.f;

// ★ 2026-08-29, ТРЕБОВАНИЕ ВЛАДЕЛЬЦА: кнопку 988 «Мой блог» (строка 989) ПОКАЗЫВАЕМ, хотя эталон
//   её прячет. Блог в оригинале рабочий (памятка `character-blog-system`), просто скрыт.
static ASDA_DEV_CVAR(int32, CVarCharInfoShowBlog,
	TEXT("asda.CharInfo.ShowBlogButton"), 1,
	TEXT("1 (по умолчанию) = показывать кнопку 988 «Мой блог» в окне 900. 0 = как в эталоне (скрыта)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarCharInfoHideDead,
	TEXT("asda.CharInfo.HideDeadRows"), 1,
	TEXT("1 = прятать строки, которые прячет эталон (кэш 990, зодиак 945, ремесло-2 943/12322). ")
	TEXT("★ Строка блога 989 выведена из-под этого ключа: у неё свой — asda.CharInfo.ShowBlogButton."),
	ECVF_Default);

// ★★★ ЦВЕТ ПОДПИСЕЙ ПОД-ВКЛАДОК — ЭТО НАШЕ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА (требование владельца
//   23.08.2026: «текст вкладок "Информация", "Параметры" и "Стихии" сделай чёрного цвета»).
//   ЭТАЛОН: цвет подписи RadioButton-а живёт в СКИНЕ по кадрам (`RE/docs/JuiSkin.ini`,
//   `TabBar_W72_3Btn` / `TabBar_W109_2Btn`, поле `CaptionColor` в блоках `SS`):
//     кадр 0 (покой)      FF808080   кадр 1 (наведение) FFAAAAAA
//     кадр 2 (нажата/выбрана) FFFFFFFF   кадр 3 FFFFFFFF   + сдвиг подписи `CaptionPoint` 0,-1 → 0,2.
//   Чтобы состояния ПРОДОЛЖАЛИ работать, переносим ту же ЛЕСТНИЦУ яркости в чёрное семейство,
//   сохранив шаги скина (+0x2A на наведение, +0x55 на нажатие): 000000 → 2A2A2A → 555555.
//   0 = вернуть цвета скина как есть (эталон).
static ASDA_DEV_CVAR(int32, CVarCharInfoTabCaptionBlack,
	TEXT("asda.CharInfo.TabCaptionBlack"), 1,
	TEXT("1 = подписи под-вкладок 981 чёрные (требование владельца; состояния наведения/нажатия сохранены); 0 = цвета скина (эталон)."),
	ECVF_Default);

// ══════════════════════════ ★★★ ОКНО «ОБО МНЕ» (8300) ══════════════════════════════════════════
// Порт эталонной связки:
//   открыть  — `findSoulmateWinOpen@0xE72C20` (`ui_core.c:74116`): текст поля = сохранённая строка,
//              фокус в поле, окно наверх, показать;
//   «ОК»     — `uiListPagerHandleCommand` case 8304 (`ui_core.c:74025`): взять текст поля,
//              `netSecure_sendTextInput` → пакет 6080, затем скрыть окно;
//   «Отмена»/крестик — case 8305/8399: просто скрыть и снять фокус (`uiHideCtrlClearFocus@0xE72CC0`).
// Ответ сервера (тот же 6080, 128 б) кладётся в `UAsdaNetSubsystem::SelfIntroduction`, откуда поле
//   и берёт текст при следующем открытии.
bool UAsdaCharaInfoSubsystem::EnsureIntroWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(IntroWin)) { return true; }

	IntroWin = nullptr; IntroEdit = nullptr; IntroOkBtn = nullptr;
	IntroCancelBtn = nullptr; IntroCloseBtn = nullptr; IntroGuide = nullptr; IntroTitle = nullptr;

	// Окно поверх 900 (у эталона `AlwaysTop = "1"` в разметке 8300).
	IntroWin = WM->OpenWindowByPath(kIntroWinId, kIntroWbp, WindowZ + 2, ESlateVisibility::SelfHitTestInvisible);
	if (!IntroWin)
	{
		UE_LOG(LogAsdaCharInfo, Error, TEXT("[обо мне] WBP_My_Introduction_Win не загрузился"));
		return false;
	}
	// ★ Потомок окна 900: иначе клик по окну статов поднимал его НАД «О себе» —
	//   BringWindowToFront не перепрыгивает только СВОИХ потомков (эталон: AlwaysTop="1" у 8300).
	WM->SetWindowOwner(kIntroWinId, kCharInfoWinId);
	IntroEdit      = Cast<UEditableText>(IntroWin->GetWidgetFromName(TEXT("My_Intro_EditBox")));
	IntroOkBtn     = Cast<UButton>(IntroWin->GetWidgetFromName(TEXT("My_Intro_OKBtn")));
	IntroCancelBtn = Cast<UButton>(IntroWin->GetWidgetFromName(TEXT("My_Intro_CancelBtn")));
	IntroCloseBtn  = Cast<UButton>(IntroWin->GetWidgetFromName(TEXT("My_Intro_ClsBtn")));
	IntroGuide     = Cast<UTextBlock>(IntroWin->GetWidgetFromName(TEXT("My_Intro_Guide")));
	IntroTitle     = Cast<UTextBlock>(IntroWin->GetWidgetFromName(TEXT("My_Intro_Move")));

	// Подписи — эталонные нотисы (`ui_core.c:73828-73877`).
	SetText(IntroTitle.Get(), Loc(CharInfoLoc::kIntroTitle));
	SetText(IntroGuide.Get(), Loc(CharInfoLoc::kIntroGuide));
	// Подписи кнопок кладёт `CWidget_SetTextAll4` — в порту это сосед `<Кнопка>_Text`.
	SetText(Cast<UTextBlock>(IntroWin->GetWidgetFromName(TEXT("My_Intro_OKBtn_Text"))),     Loc(CharInfoLoc::kIntroOk));
	SetText(Cast<UTextBlock>(IntroWin->GetWidgetFromName(TEXT("My_Intro_CancelBtn_Text"))), Loc(CharInfoLoc::kIntroCancel));
	// ★ Ограничение эталона: `LimitText = "128"` в разметке 8302 + обрезка до 128 байт в отправителе.
	if (IntroEdit) { IntroEdit->SetJustification(ETextJustify::Left); }
	// ★ [ПОРТ] 08.10.2026: жёсткий стоп набора (аналог EM_LIMITTEXT, которого у UEditableText нет) — в БАЙТАХ провода. Раньше
	//   лимита не было вовсе: длинный текст молча резался в отправителе (а арабский/смешанный — вдвое раньше, чем видел игрок).
	//   ★ [ОРИГИНАЛ] 08.10.2026 (пакет 2, МЕЛК-4): 128 б, как `LimitText = "128"` и эталонный отправитель (поле 128 б без
	//   обязательного нуля; сервер допускает 128 — разбор у `UAsdaSoulmateSubsystem::SendIntroText`).
	if (IntroEdit)
	{
		UAsdaTextLimiter* Lim = NewObject<UAsdaTextLimiter>(this);
		Lim->Callback = [this](const FText& NewText)
		{
			if (!IntroEdit) { return; }
			const FString Str = NewText.ToString();
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(Str, 128, 128);
			if (Cut != Str) { IntroEdit->SetText(FText::FromString(Cut)); }
		};
		IntroEdit->OnTextChanged.AddDynamic(Lim, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(IntroRelays, Lim, IntroEdit);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	auto BindIntro = [this](UButton* B, TFunction<void(int32)> Cb)
	{
		if (!B) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = MoveTemp(Cb);
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(IntroRelays, R, B);
	};
	BindIntro(IntroOkBtn.Get(),     [this](int32) { SubmitIntroText(); });
	BindIntro(IntroCancelBtn.Get(), [this](int32) { CloseIntroWindow(); });
	BindIntro(IntroCloseBtn.Get(),  [this](int32) { CloseIntroWindow(); });

	AsdaUi::BindButtonCaptionStates(IntroWin, IntroRelays);
	AsdaUi::DisableButtonFocusRings(IntroWin);
	// Зона курсора и роспись в «закрыть всё» — как у любого окна порта.
	WM->RegisterWindowCloser(kIntroWinId, FSimpleDelegate::CreateUObject(
		this, &UAsdaCharaInfoSubsystem::CloseIntroWindow));
	// ★ 08.10.2026 [ОРИГИНАЛ] (отчёт window_default_pos.md §6 п.2): окно 8300 — дефолт .JUI «ц50/ц72» (362,465 при 1024×768),
	//   оригинал не центрирует. Откат: `asda.UI.JuiDefaultPos 0` — прежний центр.
	if (!UAsdaWindowSubsystem::UseOriginalDefaultPos()) { WM->CenterWindowOnScreen(IntroWin); }
	WM->SetWindowVisible(kIntroWinId, false);
	return true;
}

void UAsdaCharaInfoSubsystem::OpenIntroWindow()
{
	if (!EnsureIntroWidget()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaNetSubsystem*    Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!WM) { return; }
	// Эталон: `CControl_SetText_12F9850(pIntroEditBox, nameBuf)` — поле получает СОХРАНЁННЫЙ текст.
	if (IntroEdit) { IntroEdit->SetText(FText::FromString(Net ? Net->SelfIntroduction : FString())); }
	WM->SetWindowVisible(kIntroWinId, true);
	WM->BringWindowToFront(kIntroWinId);
	if (IntroEdit) { IntroEdit->SetKeyboardFocus(); }   // эталон: `CControl_SetFocus(pIntroEditBox, 1)`
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[обо мне] окно 8300 открыто, текст «%s»"),
		Net ? *Net->SelfIntroduction : TEXT(""));
}

void UAsdaCharaInfoSubsystem::CloseIntroWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM) { WM->SetWindowVisible(kIntroWinId, false); }
}

// Положить текст в поле (консольная проверка): в игре его набирает игрок с клавиатуры.
void UAsdaCharaInfoSubsystem::SetIntroFieldText(const FString& Text)
{
	if (!EnsureIntroWidget()) { return; }
	if (IntroEdit) { IntroEdit->SetText(FText::FromString(Text)); }
}

// ★★★ ДАМП ЖИВЫХ ВИДЖЕТОВ ОКНА — из C++, а НЕ из python.
//   ГРАБЛЯ (живой прогон 23.08.2026): при запуске редактора через `-ExecCmds=py …` в python-модуле
//   `unreal` НЕТ атрибута `WidgetBlueprintLibrary`, и все виджет-дампы зонда молча превращались в
//   «окна нет» — ровно то ложное «нет», от которого предостерегает закон `false-negatives-method`.
//   Здесь то же самое печатает сам движок: имя, КЛАСС узла (после разбора тегов строка обязана
//   стать `URichTextBlock`), видимость, текст, а для подписей под-вкладок — ещё и цвет.
FString UAsdaCharaInfoSubsystem::DumpWidgets() const
{
	if (!Win) { return TEXT("[характеристики] окна нет"); }
	FString S = TEXT("[характеристики] живые виджеты окна 900:\n");
	auto Row = [&S, this](const TCHAR* Name)
	{
		UWidget* W = Win->GetWidgetFromName(FName(Name));
		if (!W) { S += FString::Printf(TEXT("  %-26s НЕТ\n"), Name); return; }
		FString Txt;
		if (URichTextBlock* R = Cast<URichTextBlock>(W))      { Txt = R->GetText().ToString(); }
		else if (UTextBlock* T = Cast<UTextBlock>(W))         { Txt = T->GetText().ToString(); }
		FString Col;
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			const FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
			Col = FString::Printf(TEXT(" цвет=(%.3f %.3f %.3f %.3f)"), C.R, C.G, C.B, C.A);
		}
		S += FString::Printf(TEXT("  %-26s класс=%-16s vis=%d текст=«%s»%s\n"),
			Name, *W->GetClass()->GetName(), (int32)W->GetVisibility(), *Txt, *Col);
	};
	// Каркас и страница «Информация» — то же, что раньше печатал python-зонд.
	for (const TCHAR* N : { TEXT("STATUS_NAME"), TEXT("STATUS_LEVEL"), TEXT("STATUS_JOB"),
	                        TEXT("Status_FrontTitle"), TEXT("Status_2ndTitle"),
	                        TEXT("STATUS_B00"), TEXT("STATUS_B01"), TEXT("STATUS_B02"), TEXT("STATUS_B03"),
	                        TEXT("STATUS_B05"), TEXT("STATUS_B06"), TEXT("STATUS_B08"), TEXT("STATUS_B09"),
	                        TEXT("STATUS_THP"), TEXT("STATUS_TMP"), TEXT("STATUS_TEXP"), TEXT("STATUS_TWEIGHT"),
	                        TEXT("CHARAINFO_CAMPWAR_T1"), TEXT("CHARAINFO_CAMPWAR_T2"), TEXT("CHARAINFO_CAMPWAR_T3"),
	                        TEXT("STATUS_STTT01"), TEXT("STATUS_STTT06"), TEXT("STATUS_STTT12"),
	                        TEXT("STATUS_ST1"), TEXT("STATUS_ST2"), TEXT("STATUS_ST3"), TEXT("STATUS_ST4"),
	                        TEXT("STATUS_ST5"), TEXT("STATUS_SB8"), TEXT("STATUS_AT10"), TEXT("STATUS_AT22"),
	                        TEXT("IMOTION_B00"), TEXT("IMOTION_B01"), TEXT("IMOTION_B02") }) { Row(N); }
	// Пункт 1 владельца: строки статов с тегами обязаны быть URichTextBlock.
	for (const TCHAR* N : { TEXT("STATUS_ST6"), TEXT("STATUS_ST7"), TEXT("STATUS_ST8"),
	                        TEXT("STATUS_ST9"), TEXT("STATUS_ST10"), TEXT("STATUS_ST11"),
	                        TEXT("CHARAINFO_CAMPWART1_S") }) { Row(N); }
	// Пункт 6 владельца: «Навык рыбалки» — подпись и значение.
	Row(TEXT("STATUS_fishing1")); Row(TEXT("STATUS_fishing2"));
	// Ур. ремесла рядом — для сравнения «0 против пустоты».
	Row(TEXT("STATUS_ST12"));
	// Пункт 2 владельца: подписи под-вкладок и их цвет.
	for (int32 i = 0; i < StatTabCaptions.Num(); ++i)
	{
		Row(*FString::Printf(TEXT("CHARAINFO_STMENU_Cap%d"), i));
	}
	S += FString::Printf(TEXT("  выбрана под-вкладка %d (кадр 2), прочие кадр 0\n"), CurStatPage);
	return S;
}

FString UAsdaCharaInfoSubsystem::DumpIntroState() const
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaNetSubsystem*   Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return FString::Printf(
		TEXT("[обо мне] окно=%s видно=%s поле=%s кнопки(ОК/Отмена/крестик)=%d/%d/%d текст поля=«%s» с сервера=«%s»"),
		IntroWin ? TEXT("есть") : TEXT("НЕТ"),
		(WM && WM->IsWindowVisible(kIntroWinId)) ? TEXT("да") : TEXT("нет"),
		IntroEdit ? TEXT("есть") : TEXT("НЕТ"),
		IntroOkBtn ? 1 : 0, IntroCancelBtn ? 1 : 0, IntroCloseBtn ? 1 : 0,
		IntroEdit ? *IntroEdit->GetText().ToString() : TEXT(""),
		Net ? *Net->SelfIntroduction : TEXT(""));
}

void UAsdaCharaInfoSubsystem::SubmitIntroText()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const FString Text = IntroEdit ? IntroEdit->GetText().ToString() : FString();
	if (Net) { Net->SendSoulmateIntroduction6080(Text); }
	CloseIntroWindow();   // эталон скрывает окно СРАЗУ, ответа не ждёт
}

// ═══════════════════════════ СТАТИЧЕСКИЕ ПОДПИСИ ════════════════════════════════════════════════
// Порт `CUICharInfo_InitControls@0xB91E60`: все нотисы и все «спрятать» из конца функции.
void UAsdaCharaInfoSubsystem::ApplyStaticTexts()
{
	// Строки левой колонки: Имя / Звание / Уровень / Профессия (12300…12303 ← 12023…12026)
	for (int32 i = 0; i < 4; ++i) { SetText(RowLabels.IsValidIndex(i) ? RowLabels[i].Get() : nullptr,
		Loc(CharInfoLoc::kRowFirst + i)); }
	// Подписи полос: ОЗ / ОМ / Опыт / Вес (12306…12309 ← 12029…12032)
	for (int32 i = 0; i < 4; ++i) { SetText(BarLabels.IsValidIndex(i) ? BarLabels[i].Get() : nullptr,
		Loc(CharInfoLoc::kBarFirst + i)); }
	// Подписи статов 12310…12322 ← 12033…12045
	for (int32 i = 0; i < 13; ++i) { SetText(StatLabels.IsValidIndex(i) ? StatLabels[i].Get() : nullptr,
		Loc(CharInfoLoc::kStatFirst + i)); }
	// ★ Эталон в КОНЦЕ init переписывает подпись 12321 нотисом 5501 («Ур. ремесла» вместо «Ремесло»).
	SetText(StatLabels.IsValidIndex(11) ? StatLabels[11].Get() : nullptr, Loc(CharInfoLoc::kCraftLabel));

	// Фракция
	SetText(FactionLabel.Get(), Loc(CharInfoLoc::kFaction));
	SetText(HonorLabel.Get(),   Loc(CharInfoLoc::kHonorPoints));
	SetText(GradeLabel.Get(),   Loc(CharInfoLoc::kHonorGrade));

	// Стихии
	SetText(EleHeadAtk.Get(), Loc(CharInfoLoc::kEleHeadAtk));
	SetText(EleHeadDef.Get(), Loc(CharInfoLoc::kEleHeadDef));
	for (int32 i = 0; i < 6; ++i)
	{
		SetText(EleAtkLabels.IsValidIndex(i) ? EleAtkLabels[i].Get() : nullptr, Loc(CharInfoLoc::kEleAtkFirst + i));
		SetText(EleDefLabels.IsValidIndex(i) ? EleDefLabels[i].Get() : nullptr, Loc(CharInfoLoc::kEleDefFirst + i));
	}

	// Рыбалка (эталон делает её видимой явно)
	SetText(FishLabel.Get(), Loc(CharInfoLoc::kFishing));
	if (FishRow)   { FishRow->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (FishLabel) { FishLabel->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	if (UWidget* FV = FishFmt.Node ? FishFmt.Node.Get() : FishValue.Get()) { FV->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }

	// Кэш / премиум-баланс
	SetText(CashLabel.Get(), Loc(CharInfoLoc::kCashLabel));

	// Заголовки лотков эмоций (12155/12156/12157)
	static const int32 kHeads[3] = { CharInfoLoc::kEmoteHead0, CharInfoLoc::kEmoteHead1, CharInfoLoc::kEmoteHead2 };
	for (int32 g = 0; g < 3; ++g)
	{
		if (EmotionHeads.IsValidIndex(g) && EmotionHeads[g])
		{
			SetText(EmotionHeads[g].Get(), Loc(kHeads[g]));
			EmotionHeads[g]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	// ── ★ То, что эталон ПРЯЧЕТ (дефекты Д3…Д6) ────────────────────────────────────────────────
	const bool bHideDead = (CVarCharInfoHideDead.GetValueOnGameThread() != 0);
	auto Vis = [](UWidget* W, bool bShow)
	{
		AsdaUi::SetShownDecor(W, bShow);
	};
	Vis(CashRow.Get(), !bHideDead);        // 990 STATUS_Line01 — Visible="0" и ни одной ссылки в эталоне
	Vis(ConsRow.Get(), !bHideDead);        // 945 STATUS_B04 — SetVisible(0) в конце init
	// ★ 2026-08-29, ТРЕБОВАНИЕ ВЛАДЕЛЬЦА: строку блога 989 (кнопка 988 «Мой блог», подпись loc
	//   11752) ПОКАЗЫВАЕМ, хотя эталон её прячет (`SetVisible(0)` в init) — блог технически
	//   рабочий (см. памятку `character-blog-system`), просто скрыт оригиналом.
	//   Ключ отката к эталону — `asda.CharInfo.ShowBlogButton 0`.
	Vis(BlogRow.Get(), CVarCharInfoShowBlog.GetValueOnGameThread() != 0);
	Vis(StatLabels.IsValidIndex(12) ? StatLabels[12].Get() : nullptr, !bHideDead);  // 12322 «Алхимия»
	Vis(StatFmt.IsValidIndex(12) ? StatFmt[12].Node.Get() : nullptr, !bHideDead);  // 943  ST13
	// 944/12323 («навык обработки предметов») мертвы всегда: единственный писатель `g_pStatValueLabel`
	//   в оригинале НИКОГДА не присваивается (отчёт §6, дефект Д2) — оставляем скрытыми.
	Vis(StatLabels.IsValidIndex(13) ? StatLabels[13].Get() : nullptr, false);
	Vis(StatFmt.IsValidIndex(13) ? StatFmt[13].Node.Get() : nullptr, false);
	// Премиум-баланс: эталон гасит обе строки сразу после инициализации.
	Vis(PreCashLabel.Get(), false);
	Vis(PreCashValue.Get(), false);
	// Кнопки «пополнить»/«проверить кэш» в разметке Visible="0".
	Vis(MacCharButton.Get(), false);
	Vis(CashCheckButton.Get(), false);
	// Подписи слотов эмоций эталон не заполняет — гасим, чтобы не мешали иконкам.
	for (FAsdaEmotionSlotUi& S : EmotionSlots) { Vis(S.Label.Get(), false); }

	// ── ★★★ Переключатель под-вкладок: два или три варианта ────────────────────────────────────
	//   В разметке лежат ДВА одноимённых контрола 981: рабочий двухкнопочный (`TabBar_W109_2Btn`,
	//   планы шириной 109) и отключённый трёхкнопочный (`TabBar_W72_3Btn`, планы шириной 72).
	//   Импортёр выдал планы обоих и ЧЕТЫРЕ+1 кнопку с дублирующимися именами, причём геометрия
	//   кнопок трёхкнопочного варианта досталась от двухкнопочного. Поэтому кнопки НЕ доверяем:
	//   берём ровно `Want` штук и кладём их РОВНО НА ПЛАНЫ выбранного варианта.
	const bool bThree = (CVarCharInfoEleTab.GetValueOnGameThread() != 0);
	auto SortByX = [](TArray<TObjectPtr<UImage>>& Arr)
	{
		Arr.Sort([](const UImage& A, const UImage& B)
		{
			FVector2D PA, SA, PB, SB;
			AsdaUi::SlotRect(&A, PA, SA); AsdaUi::SlotRect(&B, PB, SB);
			return PA.X < PB.X;
		});
	};
	SortByX(StatTab2Planes);
	SortByX(StatTab3Planes);
	for (UImage* Im : StatTab2Planes) { Vis(Im, !bThree); }
	for (UImage* Im : StatTab3Planes) { Vis(Im, bThree); }
	const TArray<TObjectPtr<UImage>>& ActivePlanes = bThree ? StatTab3Planes : StatTab2Planes;
	StatTabPageOf.Reset();
	for (int32 i = 0; i < StatTabButtons.Num(); ++i)
	{
		UButton* B = StatTabButtons[i].Get();
		if (!B) { continue; }
		if (!ActivePlanes.IsValidIndex(i))
		{
			B->SetVisibility(ESlateVisibility::Collapsed);
			StatTabPageOf.Add(-1);
			continue;
		}
		FVector2D P, S;
		if (AsdaUi::SlotRect(ActivePlanes[i].Get(), P, S)) { AsdaUi::SetSlotRect(B, P, S); }
		B->SetVisibility(ESlateVisibility::Visible);
		StatTabPageOf.Add(i);
	}
	// ★★★ ПОДПИСИ ПОД-ВКЛАДОК. У RadioButton-контрола 981 подпись каждого элемента живёт в СКИНЕ
	//   (блоки `SS` записи `TabBar_W*`), а код эталона переписывает её `CListBox_SetItemString`
	//   (нотисы 8050/8051/8052). Импортёр слил все элементы в ОДИН `..._Text`, поэтому здесь
	//   раскладываем по одной подписи НА КНОПКУ — клоном того же самого `..._Text` (закон
	//   `no-invented-ui`: берём УЖЕ импортированный элемент окна, а не рисуем свой).
	BuildStatTabCaptions(bThree);
	// ★ Обводку снимаем ПОСЛЕ создания клонов подписей и подъёма ранее свёрнутых страниц (§4 отчёта).
	ApplyOutlinePolicy();
	UE_LOG(LogAsdaCharInfo, Log,
		TEXT("[характеристики] переключатель под-вкладок: %s (asda.CharInfo.EleTab=%d)"),
		bThree ? TEXT("ТРИ кнопки — исправление дефекта Д1") : TEXT("две кнопки — как в эталоне"),
		bThree ? 1 : 0);
}

// ★★★ ПОДПИСИ ПОД-ВКЛАДОК СТРАНИЦЫ «СТАТУС» (981).
// Эталон кладёт их `CListBox_SetItemString(контрол, индекс, нотис)` — 8050 «Информация»,
//   8051 «Параметры», 8052 «Характеристики». В .JUI подпись элемента RadioButton живёт в СКИНЕ
//   (`TabBar_W109_2Btn` / `TabBar_W72_3Btn`, блоки `SS`), а импортёр слил их в ОДИН `..._Text`
//   на всю полосу. Раскладываем по одной подписи на кнопку КЛОНОМ этого же `..._Text`:
//   у клона ровно тот же шрифт, цвет и обводка, что у оригинала, потому что это он и есть.
void UAsdaCharaInfoSubsystem::BuildStatTabCaptions(bool bThreeTabs)
{
	StatTabCaptions.Reset();
	// ★★★ СНАЧАЛА УБРАТЬ ПРЕЖНИЕ КЛОНЫ. `ApplyStaticTexts` (а с ней и этот метод) зовётся ДО ЧЕТЫРЁХ
	//   раз за жизнь окна — при создании и при первом показе каждой из свёрнутых страниц. Клон берёт
	//   имя `CHARAINFO_STMENU_Cap<N>`, но `AsdaUi::CloneWidgetSubtree` при занятом имени делает
	//   ПРОИЗВОДНОЕ уникальное (`..._1`, `..._2`), а старый узел остаётся в дереве ВИДИМЫМ.
	//   Итог до правки (виден в живом дампе 23.08.2026): до четырёх наложенных копий каждой подписи,
	//   причём `GetWidgetFromName` возвращал САМУЮ СТАРУЮ — с цветом кадра на момент её создания.
	//   Убираем из родителя и освобождаем имя, иначе кадр «выбрана» ложится не на ту копию.
	if (Win && Win->WidgetTree)
	{
		TArray<UWidget*> Stale;
		Win->WidgetTree->ForEachWidget([&Stale](UWidget* W)
		{
			if (W && W->GetName().StartsWith(TEXT("CHARAINFO_STMENU_Cap"))) { Stale.Add(W); }
		});
		for (UWidget* W : Stale)
		{
			W->RemoveFromParent();
			W->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		}
		if (Stale.Num() > 0)
		{
			UE_LOG(LogAsdaCharInfo, Verbose, TEXT("[характеристики] снято прежних клонов подписей: %d"), Stale.Num());
		}
	}
	UTextBlock* Src = bThreeTabs ? StatTab3Text.Get() : StatTab2Text.Get();
	if (!Src) { Src = StatTab2Text.Get() ? StatTab2Text.Get() : StatTab3Text.Get(); }
	if (!Src || !Win)
	{
		UE_LOG(LogAsdaCharInfo, Warning, TEXT("[характеристики] нет образца подписи под-вкладок — надписи не выведены"));
		return;
	}
	UPanelWidget* Parent = Src->GetParent();
	if (!Parent) { return; }
	// Оба исходных текста-полосы прячем: они тянутся на всю ширину контрола.
	if (StatTab2Text) { StatTab2Text->SetVisibility(ESlateVisibility::Collapsed); }
	if (StatTab3Text) { StatTab3Text->SetVisibility(ESlateVisibility::Collapsed); }

	static const int32 kTabLoc[3] = { CharInfoLoc::kTabBasic, CharInfoLoc::kTabData, CharInfoLoc::kTabElement };
	const TArray<TObjectPtr<UImage>>& Planes = bThreeTabs ? StatTab3Planes : StatTab2Planes;
	const int32 Want = FMath::Min(bThreeTabs ? 3 : 2, Planes.Num());
	int32 Made = 0;
	for (int32 Page = 0; Page < Want; ++Page)
	{
		FVector2D P, S;
		if (!AsdaUi::SlotRect(Planes[Page].Get(), P, S)) { continue; }
		const FString Name = FString::Printf(TEXT("CHARAINFO_STMENU_Cap%d"), Page);
		UWidget* Clone = AsdaUi::CloneWidgetSubtree(Win, Src, Parent,
			[&Name](const UWidget*) { return Name; });
		UTextBlock* Cap = Cast<UTextBlock>(Clone);
		if (!Cap) { continue; }
		Cap->SetAutoWrapText(false);
		Cap->SetJustification(ETextJustify::Center);
		const FString CapText = Loc(kTabLoc[Page]);
		Cap->SetText(FText::FromString(CapText));
		// ★ Автоподбор кегля: у трёхкнопочного варианта элемент 72 px, а русские подписи длиннее
		//   корейских оригиналов — без подбора «Характеристики» вылезает за рамку окна.
		//   Уменьшаем ТОЛЬКО пока не влезет, минимум 6 px (ниже текст нечитаем).
		{
			FSlateFontInfo F = Cap->GetFont();
			const float Limit = FMath::Max(8.f, (float)S.X - 4.f);
			for (int32 Px = F.Size; Px >= 6; --Px)
			{
				F.Size = Px;
				if (AsdaUi::MeasureTextWidth(CapText, F, Limit) <= Limit) { break; }
			}
			Cap->SetFont(F);
		}
		// Подпись ложится РОВНО на элемент переключателя (эталон центрует её в его прямоугольнике).
		AsdaUi::SetSlotRect(Cap, FVector2D(P.X, P.Y + 3.f), FVector2D(S.X, S.Y));
		Cap->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		StatTabCaptions.Add(Cap);

		// ★★★ СОСТОЯНИЯ ПОДПИСИ (наведение / нажатие). Общий механизм `AsdaUi::BindButtonCaptionStates`
		//   сюда НЕ достаёт: он ищет соседа `<Кнопка>_Text`, а наши подписи — КЛОНЫ с именем
		//   `CHARAINFO_STMENU_Cap<N>`, и цвета кадров у RadioButton-а лежат не в стиле кнопки, а в
		//   скине (`TabBar_W*`, `CaptionColor` кадров 0/1/2). Поэтому вешаем мостик здесь, теми же
		//   четырьмя событиями и тем же правилом «отпустили → кадр 1, если курсор ещё над кнопкой»
		//   (`CPushButton_OnReleased@0x12F7600` ставит `+1976 = 1`).
		if (StatTabButtons.IsValidIndex(Page))
		{
			if (UButton* Btn = StatTabButtons[Page].Get())
			{
				TWeakObjectPtr<UTextBlock> WeakCap(Cap);
				TWeakObjectPtr<UButton>    WeakBtn(Btn);
				const int32 PageIdx = Page;
				auto SetFrame = [this, WeakCap, PageIdx](int32 Frame)
				{
					if (UTextBlock* T = WeakCap.Get()) { T->SetColorAndOpacity(FSlateColor(StatTabFrameColor(PageIdx, Frame))); }
				};
				// ★ 31.10.2026 [ИСПРАВЛЕНИЕ] (`asda.UI.RelayAnchorPrune`): подписи вкладок пересобираются (`ApplyStaticTexts` — при
				//   создании окна и при раскрытии страниц), а кнопка вкладки та же — прежнее реле подписи снимаем с неё и из якоря, иначе
				//   каждое наведение/нажатие красило подпись N раз (N подписок), а реле копились.
				static const FName kStatTabCapRole(TEXT("StatTabCaption"));
				if (AsdaUi::RelayAnchorPruneOn())
				{
					for (UObject* O : Btn->OnHovered.GetAllObjects())
					{
						UAsdaButtonRelay* Old = Cast<UAsdaButtonRelay>(O);
						if (!Old || Old->Role != kStatTabCapRole) { continue; }
						Btn->OnHovered.RemoveAll(Old); Btn->OnUnhovered.RemoveAll(Old);
						Btn->OnPressed.RemoveAll(Old); Btn->OnReleased.RemoveAll(Old);
						Relays.Remove(Old);
					}
				}
				UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
				R->Role = kStatTabCapRole;
				R->Index = Page;
				R->HoverCallback   = [this, SetFrame, PageIdx]() { SetFrame(CurStatPage == PageIdx ? 2 : 1); };
				R->UnhoverCallback = [this, SetFrame, PageIdx]() { SetFrame(CurStatPage == PageIdx ? 2 : 0); };
				R->PressCallback   = [this, SetFrame, WeakBtn, PageIdx](int32, bool bPressed)
				{
					if (bPressed) { SetFrame(2); return; }
					SetFrame((CurStatPage == PageIdx) ? 2 : ((WeakBtn.IsValid() && WeakBtn->IsHovered()) ? 1 : 0));
				};
				Btn->OnHovered.AddDynamic(R,   &UAsdaButtonRelay::OnHovered);
				Btn->OnUnhovered.AddDynamic(R, &UAsdaButtonRelay::OnUnhovered);
				Btn->OnPressed.AddDynamic(R,   &UAsdaButtonRelay::OnPressed);
				Btn->OnReleased.AddDynamic(R,  &UAsdaButtonRelay::OnReleased);
				AsdaUi::AnchorRelay(Relays, R, Btn);
			}
		}
		++Made;
	}
	// ★★★ БАЗОВЫЕ ЦВЕТА ПОДПИСЕЙ — ИЗ АССЕТА, и снять их надо ИМЕННО ЗДЕСЬ: клон только что взял
	//   цвет у образца `CHARAINFO_STMENU_Text` (его кладёт шаг `Tools/asda_ui_tabbar_captions.py`),
	//   а первая же покраска состоянием этот цвет затрёт. Кэш пересоздаётся вместе с подписями.
	StatTabIdleColors.Reset();
	AsdaUi::CaptureTabCaptionIdle(StatTabCaptions, StatTabIdleColors);
	RefreshTabCaptionColors();
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] подписи под-вкладок выведены: %d из %d"), Made, Want);
}

// ★★★ ОБВОДКА ТЕКСТА ВО ВСЁМ ОКНЕ 900 — СНЯТА (требование владельца 2026-08-23:
//   «у всего окна характеристик персонажа убрать обводки»).
//   ПОЧЕМУ ЭТО ЗАКОННО. Импортёр JUI запекает `OutlineSize = 1` всем текстам, чей скин несёт
//   `TextOutLine = "1"`, а прочтение «TextOutLine ⇒ обводка» ОТМЕНЕНО (разбор — в шапке
//   `AsdaUi::SetTextOutline`): поле 925 выбирает флаги вывода, обводку даёт поле 926
//   (`CClass_SetField926(control, 0x800000)`), и НИ ОДНОГО такого вызова по контролам окна 900
//   в срезе эталона нет. То есть снятие обводки — это ПРИВЕДЕНИЕ К ЭТАЛОНУ, а не отступление.
//   ★ Обход — ОБЩАЯ точка `AsdaUi::ApplyTextOutlineTree` (её же зовут журнал заданий 700, попап
//   наград 540 и окно разговора); своей копии не заводим (закон `port-shared-points`).
//   ★ ДВУСТОРОННОСТЬ ВАЖНА: функция ставит `DefaultPx` ВСЕМ `UTextBlock` дерева, поэтому
//   переимпорт JUI не может вернуть обводку — новые узлы придут и тут же будут очищены.
//   Зовём не только при создании окна, но и после каждой `ApplyStaticTexts`: `RevealSubtree`
//   поднимает ранее свёрнутые страницы, а `BuildStatTabCaptions` создаёт НОВЫЕ клоны подписей.
void UAsdaCharaInfoSubsystem::ApplyOutlinePolicy()
{
	if (!Win) { return; }
	// ★★★ ТЕЛО УБРАНО 2026-08-27: обводка теперь приведена к данным оригинала В САМИХ АССЕТАХ
	//   (шаг `Tools/asda_ui_text_style926.py` по таблице `Tools/data/text_style926.tsv`).
	//   Окну 900 (`CHARAINFO_WIN`) обводка по оригиналу НЕ ПОЛОЖЕНА НИ ОДНОМУ контролу, и
	//   импортёр её больше не ставит — правило «скиновый `TextOutLine=1` ⇒ обводка» отключено
	//   (оно читало поле 925 «флаги переноса» вместо поля 926 «стиль»).
	//   Снимать её в рантайме больше нечего: по всему интерфейсу обводка осталась у 630 текстов
	//   из 7311 — ровно у тех, кому её ставит оригинал.
	//   Функция сохранена как точка вызова (её зовут из нескольких мест) и намеренно пуста.
}

// ★★★ ПОДГОНКА ВЫСОТЫ ОКНА ПОД СОДЕРЖИМОЕ — лечение «чёрной бороды» снизу (пункт владельца 7).
//
// ★ ДОКАЗАТЕЛЬСТВО, ЧТО ЭТО ДЕФЕКТ ОРИГИНАЛА, А НЕ НАША РЕГРЕССИЯ (числа — из `gamemain_jui.ini`,
//   дамп `Reports/_charinfo_jui_dump.txt`):
//     900  `CHARAINFO_WIN`          abs (145, 44)  размер 228×580  ⇒ низ окна y = 624;
//     1100 `CHARAINFO_IMOTIONBACK`  abs (150,142)  размер 218×300;
//     2000000027 `CHARAINFO_IMOTIONBase` (`Win_Tab`) abs (150,124) размер 218×399 ⇒ низ y = 523;
//     последний ряд лотка «Навык» `IMOTION_TRAY02_06…11` — y = 378, высота 32 ⇒ низ y = 410.
//   То есть ниже последней иконки в оригинале 113 px пустой подложки и ещё 101 px голого корпуса.
//   ПЕРЕРАЗМЕТКИ НЕТ: `uiWindowSwitchTabMode@0xE76510` (`ui_core.c:74320`) при смене вкладки делает
//   РОВНО три вещи — `SetVisible(pStatBack)`, `SetVisible(pMotionBack)`, `SetSelectedIndex(pTabCtrl)`;
//   ни одного `SetSize`/`uiPanel9SliceLayout` во всём блоке `CCharaInfoWin` (74183…74342) нет.
//   Механизм `uiWindow_relayoutByState@0xD941A0` (высоты 192…512), который мы портировали для окна
//   награды 540, к окну 900 НЕ ПРИМЕНЯЕТСЯ — его единственный вызывающий это окно 540.
//   ⇒ у оригинала «борода» ЕСТЬ, и на вкладке статов тоже: 580 px рассчитаны на блок премиум-кэша
//   (990 `STATUS_Line01`, самый нижний видимый узел разметки — `STATUS_MAC_CHAR_Ribbon` y 592…615),
//   а сам этот блок эталон при инициализации ГАСИТ (`CUICharInfo_InitControls`, см. §6 отчёта по
//   окну). Высота осталась от выключенной ветки.
//
// ★ ЛЕЧИМ по закону `fix-original-defects`: рамка окна (и подложка страницы эмоций) ужимается по
//   фактическому содержимому. КЛЮЧ ОТКАТА: `asda.CharInfo.FitHeight 0` — эталонные 580 px.
//   Обе рамки — 9-slice, и пересборка идёт ОБЩЕЙ точкой `UAsdaTipPanel::Resize9Slice`
//   (её же зовут тултип, мини-статус, чат и попап наград) — своей копии не заводим.
void UAsdaCharaInfoSubsystem::FitWindowHeight()
{
	if (!Win || WinFramePlanes.Num() != 9) { return; }
	if (CVarCharInfoFitHeight.GetValueOnGameThread() == 0) { return; }
	UCanvasPanel* Root = Cast<UCanvasPanel>(Win->GetRootWidget());
	if (!Root) { return; }

	// Прямоугольник рамки берём у ВЕРХНЕГО ЛЕВОГО плана — он никогда не двигается.
	FVector2D FrameP, FrameS;
	if (!AsdaUi::SlotRect(WinFramePlanes[0].Get(), FrameP, FrameS)) { return; }
	const float FrameLeft = FrameP.X, FrameTop = FrameP.Y;
	float FrameW = 0.f;
	{
		FVector2D P2, S2;
		if (AsdaUi::SlotRect(WinFramePlanes[2].Get(), P2, S2)) { FrameW = (P2.X + S2.X) - FrameLeft; }
	}
	if (FrameW <= 0.f) { return; }

	// ── Обход дерева: самый нижний край ВИДИМОГО содержимого в координатах корневого канваса ──
	//   Свёрнутые ветки пропускаем целиком (их дети на экране не занимают места), сами планы
	//   подгоняемых рамок — тоже (иначе они «подпирали» бы сами себя).
	auto IsSkipped = [](const FString& N)
	{
		return N.StartsWith(TEXT("CHARAINFO_WIN_0_"))            // планы рамки окна
			|| N.StartsWith(TEXT("CHARAINFO_IMOTIONBase_"))      // планы подложки страницы эмоций
			|| N == TEXT("CHARAINFO_WIN_MOVE")                   // невидимая ручка перетаскивания
			|| N.StartsWith(TEXT("EMOTE_DragGhost"));            // «призрак» летит где угодно
	};
	float Bottom = FrameTop;
	FString BottomName;
	TFunction<void(UWidget*, FVector2D)> Walk = [&](UWidget* W, FVector2D Base)
	{
		if (!W) { return; }
		const ESlateVisibility V = W->GetVisibility();
		if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden) { return; }
		FVector2D P(0.f, 0.f), S(0.f, 0.f);
		const bool bHasSlot = AsdaUi::SlotRect(W, P, S);
		const FVector2D Abs = bHasSlot ? (Base + P) : Base;
		// ★★★ В СЧЁТ ИДЁТ ТОЛЬКО ТО, ЧТО РЕАЛЬНО РИСУЕТСЯ. Иначе «низом» становится пустая
		//   ОБЁРТКА: страницы 903/1100 объявлены в разметке высотой 300 px, хотя их содержимое
		//   кончается много выше (именно они и держали высоту в первом заходе — доказано строкой
		//   «его держит CHARAINFO_STBACK_grp / CHARAINFO_IMOTIONBACK» в живом логе). Поэтому:
		//     • контейнеры (`UPanelWidget`) собственный прямоугольник НЕ дают — только детей;
		//     • пустой текст не даёт (у 1100 это `UTextBlock` без текста на всю страницу);
		//     • картинка без браша не даёт (узлы `..._HIT` импортёра — чистый хит-тест, скин `NoImage`).
		bool bDraws = bHasSlot && !IsSkipped(W->GetName());
		if (bDraws && Cast<UPanelWidget>(W)) { bDraws = false; }
		if (bDraws)
		{
			if (const UTextBlock* T = Cast<UTextBlock>(W)) { bDraws = !T->GetText().IsEmpty(); }
			else if (const UImage* Im2 = Cast<UImage>(W))  { bDraws = (Im2->GetBrush().GetResourceObject() != nullptr); }
		}
		if (bDraws && (float)(Abs.Y + S.Y) > Bottom)
		{
			Bottom = (float)(Abs.Y + S.Y);
			BottomName = W->GetName();
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i) { Walk(Panel->GetChildAt(i), Abs); }
		}
	};

	// ШАГ 1. На вкладке эмоций сперва ужимаем ПОДЛОЖКУ страницы: иначе она сама и есть «низ».
	UPanelWidget* MotionBasePanel = Cast<UPanelWidget>(MotionBase.Get());
	if (CurTab == 1 && MotionBasePlanes.Num() == 9 && MotionBasePanel)
	{
		FVector2D BaseP, BaseS;
		if (AsdaUi::SlotRect(MotionBasePlanes[0].Get(), BaseP, BaseS))
		{
			// Низ содержимого САМОЙ подложки (лотки и заголовки) в её локальных координатах.
			float Save = Bottom;
			Bottom = 0.f;
			for (int32 i = 0; i < MotionBasePanel->GetChildrenCount(); ++i) { Walk(MotionBasePanel->GetChildAt(i), FVector2D::ZeroVector); }
			const float PanelH = FMath::Max(Bottom + kFitPanelPad, 32.f);
			Bottom = Save;
			float PanelW = 0.f;
			FVector2D P2, S2;
			if (AsdaUi::SlotRect(MotionBasePlanes[2].Get(), P2, S2)) { PanelW = (P2.X + S2.X) - BaseP.X; }
			if (PanelW > 0.f)
			{
				UAsdaTipPanel::Resize9Slice(MotionBasePlanes, FVector2D(PanelW, PanelH), BaseP);
			}
		}
	}

	// ШАГ 2. Низ ВСЕГО содержимого окна и новая высота рамки.
	Walk(Root, FVector2D::ZeroVector);
	const float NewH = FMath::Max(Bottom + kFitWinPad - FrameTop, 64.f);
	UAsdaTipPanel::Resize9Slice(WinFramePlanes, FVector2D(FrameW, NewH), FVector2D(FrameLeft, FrameTop));
	UE_LOG(LogAsdaCharInfo, Log,
		TEXT("[характеристики] высота окна подогнана под содержимое: вкладка %d, низ %.0f (его держит «%s», верх рамки %.0f), рамка %.0fx%.0f (в разметке 228x580)"),
		CurTab, Bottom, *BottomName, FrameTop, FrameW, NewH);
}

// ★★★ Рекурсивно поднять поддерево (см. комментарий у объявления).
// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь была своя копия правила видимости
//   («`Cast<UButton>(Root) ? Visible : SelfHitTestInvisible`»). Правило одно на весь порт —
//   `AsdaCommunityUi::ShownVisibilityFor` (`UI/AsdaCommunityFriendSubsystem.h`), откат
//   `asda.UI.InteractiveHitTest 0`.
//   ⚠ ПОВЕДЕНИЕ НЕ МЕНЯЕТСЯ: в разметке `CHARAINFO_WIN` (900, 241 узел) нет контролов со скином
//   `EditBox`/`CheckButton`/`ComboBox` (`Text` 126, `StaticWindow` 93, `ResizeWindow` 9,
//   `PushButton` 6, `ProgressBar` 4, `RadioButton` 3), а поднимаются отсюда только страницы
//   «Параметры»/«Характеристики» и панель эмоций этого же окна.
//   ⚠ Условие «поднимаем только СХЛОПНУТОЕ» сохранено: уже показанный узел не переставляется,
//   иначе слетели бы точечные состояния, выставленные страницами после показа.
void UAsdaCharaInfoSubsystem::RevealSubtree(UWidget* Root)
{
	if (!Root) { return; }
	if (Root->GetVisibility() == ESlateVisibility::Collapsed || Root->GetVisibility() == ESlateVisibility::Hidden)
	{
		Root->SetVisibility(AsdaCommunityUi::ShownVisibilityFor(Root));
	}
	if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i) { RevealSubtree(Panel->GetChildAt(i)); }
	}
}

// ═══════════════════════════ ОТКРЫТИЕ / ЗАКРЫТИЕ ════════════════════════════════════════════════
void UAsdaCharaInfoSubsystem::OpenWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }

	// ── ГЕЙТЫ `uiWindow_OpenIfAllowed@0xE76240` ────────────────────────────────────────────────
	//   Эталон отказывает при: заблокированном инвентаре, открытых парикмахерской / магазине NPC /
	//   складе / окне тайла / диалоге количества — МОЛЧА; при открытой ЛИЧНОЙ ЛАВКЕ — с нотисом
	//   107022. В порту из этого списка представлен общий признак «игровые окна убраны сценой»
	//   (полноэкранные сцены гачи/шкатулки) — остальные окна ещё не портированы, строка занесена
	//   в PORT_PENDING_ON_SYSTEMS.md вместе с записью журнала заданий.
	if (WM->ArePlayerWindowsHidden())
	{
		UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] открытие отклонено: активна полноэкранная сцена"));
		return;
	}
	// ★ Волна В3 ремесла (ревизия В0–В2 С-1): из гейтов `uiWindow_OpenIfAllowed@0xE76240` (ui_core.c:74270)
	//   подключены окна ремесла (`tileInteract_isControlVisible(g_pItemMainWnd)` / `isBoundControlVisible(
	//   g_pItemQtyMakeDlg)`) — МОЛЧА.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GI))
	{
		UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] открытие отклонено молча: открыто окно ремесла 76000/77500 (ui_core.c:74270)"));
		return;
	}
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `uiWindow_OpenIfAllowed@0xE76240` (ui_core.c:74272-74275) — после молчаливых гейтов
	//   `if (sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled))` → 107022 в чат (−11514, 10 с); ни 5466, ни окна.
	if (UAsdaTradeStateSubsystem::IsSelfStoreOpen(GI))
	{
		UAsdaTradeStateSubsystem::ChatStoreGate(GI, CharInfoLoc::kOwnShop, TEXT("uiWindow_OpenIfAllowed"));
		return;
	}
	if (!EnsureWidget()) { return; }

	// Эталон при КАЖДОМ открытии шлёт запрос премиум-баланса (5466).
	//   ★ 22.10.2026: кроме служебного переоткрытия после травела (`ReopenTravelKeptWindows`) — у оригинала окно при смене
	//   карты не закрывалось и повторного «открытия» (а значит и 5466) не было.
	if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
	{
		if (!WM->IsReopeningTravelKept()) { Net->SendPremiumBalanceQuery5466(Net->SelectedCharId); }
	}

	RefreshAll();
	WM->SetWindowVisible(kCharInfoWinId, true);
	WM->BringWindowToFront(kCharInfoWinId);
	if (UAsdaSoundSubsystem* Snd = GI->GetSubsystem<UAsdaSoundSubsystem>())
	{
		Snd->PlaySound2D(FName(TEXT("S_e_button06")));
	}
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] окно 900 открыто на вкладке %d (страница %d)"),
		CurTab, CurStatPage);
}

void UAsdaCharaInfoSubsystem::CloseWindow()
{
	HideOwnTooltip();
	// Окно закрылось прямо во время перетаскивания — снимаем захват и призрак (эталон msg 6:
	//   `this[55] = 0` + `sub_91F61C(g_pUIManager, 1)`, БЕЗ укладки и БЕЗ запуска эмоции).
	CancelEmotionDrag();
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kCharInfoWinId, false);
	}
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(TEXT("S_e_button07")));
	}
	UE_LOG(LogAsdaCharInfo, Log, TEXT("[характеристики] окно 900 закрыто"));
}

// Порт `uiWindowToggleTab@0xE76160`.
void UAsdaCharaInfoSubsystem::ToggleTab(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, 1);
	if (IsWindowOpen())
	{
		if (Tab == CurTab) { CloseWindow(); return; }
		HideOwnTooltip();
		SwitchTabMode(Tab);
		return;
	}
	// ★ ДЕФЕКТ ОРИГИНАЛА Д9 (отчёт §6): эталон переключает вкладку ДО проверки гейтов открытия, и при
	//   отказе вкладка остаётся сменённой. Здесь переключаем ПОСЛЕ успешного открытия — исправление.
	const int32 Was = CurTab;
	CurTab = Tab;
	OpenWindow();
	if (!IsWindowOpen()) { CurTab = Was; return; }
	SwitchTabMode(Tab);
}

// Порт `uiWindowSwitchTabMode@0xE76510`.
void UAsdaCharaInfoSubsystem::SwitchTabMode(int32 Tab)
{
	Tab = FMath::Clamp(Tab, 0, 1);
	CurTab = Tab;
	CancelEmotionDrag();   // ушли со вкладки эмоций с зажатой иконкой — захват отменяется (эталон msg 6)
	AsdaUi::SetShownDecor(StatBack, Tab == 0);
	AsdaUi::SetShownDecor(MotionBack, Tab == 1);
	AsdaUi::SetShownDecor(MotionBase, Tab == 1);
	for (int32 g = 0; g < EmotionGroups.Num(); ++g)
	{
		if (EmotionGroups[g])
		{
			AsdaUi::SetShownDecor(EmotionGroups[g], Tab == 1);
		}
	}
	if (Tab == 1)
	{
		if (!bRevealedEmotion) { RevealSubtree(MotionBack.Get()); bRevealedEmotion = true; ApplyStaticTexts(); }
		RefreshEmotionTab();
		FitWindowHeight();   // страница эмоций ниже страницы статов — рамка ужимается под неё
	}
	else { SelectStatPage(CurStatPage); }   // внутри тоже зовёт FitWindowHeight
	if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2D(FName(TEXT("S_e_button06")));
	}
}

// Порт `uiTabSelectVisible@0xB95A40` (гасит 982/983/984, показывает выбранную).
void UAsdaCharaInfoSubsystem::SelectStatPage(int32 Page)
{
	const int32 MaxPage = (CVarCharInfoEleTab.GetValueOnGameThread() != 0) ? 2 : 1;
	Page = FMath::Clamp(Page, 0, MaxPage);
	CurStatPage = Page;
	for (int32 i = 0; i < StatPages.Num(); ++i)
	{
		if (StatPages[i])
		{
			AsdaUi::SetShownDecor(StatPages[i], i == Page);
		}
	}
	// ★ Импортёр протащил `Visible="0"` родителя рекурсией на всё поддерево страниц 983/984 — при
	//   первом показе поднимаем детей сами, иначе страница остаётся пустой.
	if (Page == 1)
	{
		if (!bRevealedData)    { RevealSubtree(StatPages.IsValidIndex(1) ? StatPages[1].Get() : nullptr); bRevealedData = true; ApplyStaticTexts(); }
		RefreshDataPage();
	}
	if (Page == 2)
	{
		if (!bRevealedElement) { RevealSubtree(StatPages.IsValidIndex(2) ? StatPages[2].Get() : nullptr); bRevealedElement = true; ApplyStaticTexts(); }
		RefreshElementPage();
	}
	// Подпись ВЫБРАННОЙ под-вкладки — кадр 2 скина, остальные — кадр 0 (эталонное поведение
	//   RadioButton-а: выбранный элемент рисуется кадром 2/3, невыбранные — кадром 0).
	RefreshTabCaptionColors();
	FitWindowHeight();   // страницы разной высоты — рамка следует за содержимым (см. FitWindowHeight)
}

// ★ Цвет подписи под-вкладки по кадру скина. Разбор и обоснование отступления — у
//   `CVarCharInfoTabCaptionBlack` в шапке файла.
FLinearColor UAsdaCharaInfoSubsystem::TabCaptionColor(int32 Frame)
{
	// Цвета скина `TabBar_W72_3Btn` / `TabBar_W109_2Btn` (`CaptionColor` кадров 0/1/2).
	static const FColor kSkin[3]  = { FColor(0x80, 0x80, 0x80), FColor(0xAA, 0xAA, 0xAA), FColor(0xFF, 0xFF, 0xFF) };
	// Та же лестница, перенесённая в чёрное семейство с сохранением шагов скина (+0x2A / +0x55).
	static const FColor kBlack[3] = { FColor(0x00, 0x00, 0x00), FColor(0x2A, 0x2A, 0x2A), FColor(0x55, 0x55, 0x55) };
	const int32 F = FMath::Clamp(Frame, 0, 2);
	const FColor C = (CVarCharInfoTabCaptionBlack.GetValueOnGameThread() != 0) ? kBlack[F] : kSkin[F];
	// Цвета UI — sRGB, в UMG кладём линейные (закон ui-srgb-gamma-rule).
	return FLinearColor::FromSRGBColor(C);
}

// ★ Цвет подписи под-вкладки по кадру состояния. ДВА кадра из трёх отданы ОБЩЕЙ ТОЧКЕ
//   (закон `port-shared-points`), потому что это и есть «какая вкладка активна»:
//     кадр 0 (покой)   → `AsdaUi::TabCaptionInactiveColor` — базовый цвет ИЗ АССЕТА, снятый при
//                        сборке подписей (для этого окна он тёмный `ff4c4c4c`, и общая точка
//                        отдаёт его без изменений — приглушает она только почти-белые подписи);
//     кадр 2 (выбрана) → `AsdaUi::TabCaptionActiveColor` (ЧЁРНЫЙ на светлом скине этого окна);
//     кадр 1 (наведение) → лестница скина (`TabCaptionColor`) — это НЕ состояние вкладки,
//                          а отклик на курсор, общей точки для него нет.
FLinearColor UAsdaCharaInfoSubsystem::StatTabFrameColor(int32 Page, int32 Frame) const
{
	const FLinearColor Idle = StatTabIdleColors.IsValidIndex(Page) ? StatTabIdleColors[Page]
	                                                               : TabCaptionColor(0);
	if (Frame >= 2) { return AsdaUi::TabCaptionActiveColor(Idle); }
	if (Frame == 1) { return TabCaptionColor(1); }
	return AsdaUi::TabCaptionInactiveColor(Idle);
}

// Перекрасить все подписи под-вкладок по текущему выбору — ОБЩАЯ ТОЧКА порта.
void UAsdaCharaInfoSubsystem::RefreshTabCaptionColors()
{
	AsdaUi::ApplyTabCaptionState(StatTabCaptions, CurStatPage, StatTabIdleColors);
}
