// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystem_Tab.cpp — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp, дословный перенос): контролы вкладки, подписи, блоки «поделиться»/свой, заметка.
//   Общее для частей — в AsdaSoulmateSubsystemInternal.h; шапка и реверс — в AsdaSoulmateSubsystem.cpp.
#include "UI/AsdaSoulmateSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "UI/AsdaNotice.h"   // ★ волна 1: фасад нотисов (Loc / Chat / MsgBox)
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"   // OP_SOULMATE_UPDATE_INFO / OP_SOULMATE_WND_APPLY_INFO_PACKET
#include "AsdaBytes.h"
#include "Misc/ScopeExit.h"   // ★ 26.10.2026: ON_SCOPE_EXIT — выход из защиты повторного входа панели 6500
#include "UI/AsdaWindowSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
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
// ★ волна 1-А: гейт показа 8200 — элитный магазин 6000 (`helper_getField2075(g_pShopWnd)`)   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
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

// ★★★ ДЕФЕКТ ОРИГИНАЛА D16 (цвет чисел вкладки соулмейта, жалоба владельца 02.09).
//   Цвета текстов вкладки заданы РАЗМЕТКОЙ (`gamemain_jui.ini`, блок `EXS … TextColor … EXE`),
//   код эталона их не трогает. Два ЧИСЛА заданы так, что читать их нельзя:
//     • `Soul_HeartTxt` 6585 — `TextColor = FF323232` (почти чёрный) поверх ТЁМНОГО сердца;
//     • `SOUL_TXT12`   6557 — `TextColor = FFF5124B` (розово-красный) на тёмной подложке.
//   Остальные числа вкладки в разметке уже белые (`SOUL_TXT03` 6521, `SOUL_TXT13` 6558).
//   Ключ ниже: 0 — цвета как в разметке оригинала, 1 (по умолчанию) — оба числа белые.
//   Правка рантаймовая: в ассет не запекается и переживает переимпорт JUI
//   (закон `tools-no-import-side-effects`).
static ASDA_DEV_CVAR(int32, CVarSoulmateWhiteNumbers,
	TEXT("asda.UI.Soulmate.WhiteNumbers"), 1,
	TEXT("Вкладка соулмейта: 0 — цвет чисел 6585/6557 как в разметке оригинала ")
	TEXT("(FF323232 / FFF5124B, нечитаемо); 1 — белый."));

// ★★★ НАШЕ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА (`fix-original-defects`), требование владельца 02.09.
//   Блок «настройка доступа к моему персонажу» (заголовок 6579 loc 12108; три имени 6540/6541/6542;
//   подписи «поделиться» 6580/6581/6582 loc 12110; галки 6543/6544/6545) в ОРИГИНАЛЕ скрыт разметкой
//   (`Visible="0"`, `soulmate_share_feature_client.md` §1/§3), а сервер три имени НИКОГДА не заполняет
//   (§8) — поэтому и при снятом `Visible="0"` игрок увидел бы пустые строки. Владелец хочет ПОКАЗАТЬ
//   переключатели в меню друга и оживить их. Имена берём из НАШЕГО списка персонажей (CharList),
//   отправку галок делает существующий `UAsdaNetSubsystem::SendSoulmateModes5221` (опкод 5221).
//   1 (по умолчанию) — показывать и оживить; 0 — вернуть поведение оригинала (блок скрыт).
static ASDA_DEV_CVAR(int32, CVarSoulmateShareBlock,
	TEXT("asda.Soulmate.ShareBlockVisible"), 1,
	TEXT("Меню друга, вкладка соулмейта: 1 (по умолчанию) — показать и оживить блок «поделиться ")
	TEXT("персонажем» (галки 6543..6545 → 5221, имена из своего списка персонажей); ")
	TEXT("0 — как в оригинале, блок скрыт."));

bool AsdaSoulNoteEnterSendOn();    // `asda.Soulmate.NoteEnterSend` — Enter в поле 6602 = «Отправить»
bool AsdaSoulNoteInputFocusOn();   // `asda.Soulmate.NoteInputFocus` — фокус полю 6602 при открытии / снятие при закрытии

// [ОРИГИНАЛ] `minimapDrawTargetMarker@0xB56BB0`: розовая точка Друга (miniCouplePos) на мини-карте и большой карте.
static ASDA_DEV_CVAR(int32, CVarSoulmateMapMark,
	TEXT("asda.Soulmate.MapMark"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = отметка Друга miniCouplePos на мини-карте и карте мира (пара есть, Друг в игре, тот же канал,\n")
	TEXT("та же карта; тайл — 5110/5111). 0 = прежний порт: отметки нет."),
	ECVF_Default);
// [ОРИГИНАЛ]+[ПОРТ] своя одержимость: эталон при блоке +26 == 2 гасит галки (`CChat_SetIconState(…, 0)`, 6546 снята); у порта
//   блок «поделиться» открыт (наше отступление) — без замка вселившийся мог бы переписать Share ХОЗЯИНА тела (5221 пишет его Userindex).
static ASDA_DEV_CVAR(int32, CVarSoulmateShareLockPossessed,
	TEXT("asda.Soulmate.ShareLockPossessed"), 1,
	TEXT("1 (по умолчанию) = пока я одержим (вселён в тело Друга), галки 6543..6546 погашены и 5221 не уходит.\n")
	TEXT("0 = прежний порт: галки кликаются и под одержимостью."),
	ECVF_Default);

// [ОРИГИНАЛ] имена строк «поделиться» — из своей 5220 (@39/@59/@79; DBManager кладёт их по слотам Charidx 10/11/12 с P78).
static ASDA_DEV_CVAR(int32, CVarSoulmateShareNames5220,
	TEXT("asda.Soulmate.ShareNamesFrom5220"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = имена трёх строк «поделиться» из своей 5220 (до неё — из списка персонажей).\n")
	TEXT("0 = прежний порт: только из списка персонажей."),
	ECVF_Default);
// [ОРИГИНАЛ] 5227 — Друг сменил карту/канал: строка 6523 и свой блок (+22 карта, +23 канал).
static ASDA_DEV_CVAR(int32, CVarSoulmateRecv5227,
	TEXT("asda.Soulmate.Recv5227"), 1,
	TEXT("1 (по умолчанию) [ОРИГИНАЛ] = 5227 обновляет карту/канал Друга и поле 6523 «Локация». 0 = прежний порт: 5227 не разбирается."),
	ECVF_Default);

UButton* UAsdaSoulmateSubsystem::FindBtn(const TCHAR* Idc) const
{
	return CommWnd ? Cast<UButton>(CommWnd->GetWidgetFromName(FName(Idc))) : nullptr;
}

// Галка блока «доступ к персонажу». Импортёр называет интерактивный `UCheckBox` РОВНО по idc
//   (закон `jui-importer-widget-naming`), поэтому поиск точный, без цепочки `<idc>_grp`.
UCheckBox* UAsdaSoulmateSubsystem::FindCheck(const TCHAR* Idc) const
{
	return CommWnd ? Cast<UCheckBox>(CommWnd->GetWidgetFromName(FName(Idc))) : nullptr;
}

UTextBlock* UAsdaSoulmateSubsystem::FindTxt(const TCHAR* Idc) const
{
	return CommWnd ? Cast<UTextBlock>(CommWnd->GetWidgetFromName(FName(Idc))) : nullptr;
}

void UAsdaSoulmateSubsystem::BindTabControls(UUserWidget* CommunityWindow)
{
	CommWnd = CommunityWindow;
	if (!CommWnd) { return; }

	// ★★★ Реле ПРОШЛОГО дерева окна держать нельзя: они ведут в уничтоженные виджеты (окно 6400
	//   пересоздаётся при каждой смене карты — см. `IsBoundTo`). Чистим перед новой привязкой,
	//   иначе массив рос бы на каждое пересоздание. `PopupRelays` не трогаем — это ДРУГИЕ окна.
	Relays.Reset();

	// Тексты карточки — назначение снято с `CSoulmateWnd_RefreshInfo@0xE63410` (подписи отчёта §3.1 врут).
	TxtStatus   = FindTxt(TEXT("SOUL_TXT01"));   // 6548 — статус, всегда loc 8053
	TxtName     = FindTxt(TEXT("SOUL_TXT02"));   // 6520 — имя партнёра (rec[63..])
	TxtLevel    = FindTxt(TEXT("SOUL_TXT03"));   // 6521 — «%d <12215>» (rec[29])
	TxtGrade    = FindTxt(TEXT("SOUL_TXT10"));   // 6556 — класс/подкласс (rec[41]?:rec[40])
	TxtJob      = FindTxt(TEXT("SOUL_TXT05"));   // 6523 — профессия со ступенью (rec[38]/rec[39])
	TxtFriendLv = FindTxt(TEXT("SOUL_TXT12"));   // 6557 — уровень Дружбы (rec[50])
	// ★★★ ОПРОВЕРЖЕНИЕ D9 (31.08). Прежний комментарий утверждал, что 6558 `SOUL_TXT13` и полоса
	//   6559 `SOUL_GAUGE01` в эталоне заполняются пустой строкой в InitControls и больше НИКОГДА
	//   не трогаются. Это неверно: их пишет `uiProgressBar_RefreshPercent@0xE62E20`
	//   (game.c:471345-471358) — `CProgressBar_SetValue(v16[11] /*6559*/, pct)` и
	//   `CControl_SetText(v16[10] /*6558*/, "%.1lf %%")`; `this+11` = +0x2C = m_pGauge01,
	//   `this+10` = +0x28 = m_pTxt13 (разметка структуры `CSoulmateWnd`, game.c:1495/1511).
	//   Не заполняется в эталоне только 6522 `SOUL_TXT04` (`this+6`, читателей нет) — его и нет.
	TxtPercent  = FindTxt(TEXT("SOUL_TXT13"));   // 6558 — «%.1lf %%» доли опыта Дружбы
	// ★ 24.10.2026 (sm-offline) [ИСПРАВЛЕНИЕ]: 6522 «Последний выход» — пишем время выхода Друга вне игры (эталон поле не пишет).
	TxtLogout   = FindTxt(TEXT("SOUL_TXT04"));
	BindBarFill(CommWnd, TEXT("SOUL_GAUGE01"), TabGauge01);   // 6559 — та же доля полосой

	// Кнопки-действия (`SoulmateUI_HandleButtonCommand`).
	auto Bind = [this](const TCHAR* Idc, int32 Tag, TFunction<void()> Fn)
	{
		if (UButton* B = FindBtn(Idc))
		{
			AsdaUi::BindButton(this, B, Tag, [Fn](int32) { Fn(); }, Relays);   // ★ U20: реле клика — общая точка   // ★★★ GC-якорь: динамический делегат держит реле СЛАБО
		}
		else
		{
			UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] кнопка «%s» не найдена"), Idc);
		}
	};
	Bind(TEXT("Soul_MessageBtn"),  6583, [this]() { ActionMessage(); });
	Bind(TEXT("Soul_OldSoul_Btn"), 6584, [this]() { ActionOldSoul(); });
	Bind(TEXT("Soul_CutoffBtn"),   6587, [this]() { ActionCutoff();  });
	Bind(TEXT("Soul_Find_Btn"),    6588, [this]() { ActionFindNew(); });
	Bind(TEXT("Soul_Msg_OkBtn"),   6603, [this]() { SendNote();      });
	Bind(TEXT("Soul_Msg_ClBtn"),   6604, [this]() { HideNotePanel(); });
	// ★ 24.10.2026 (sm-offline) [ОРИГИНАЛ]: 6642 — закрыть окно прежних Друзей 6640 (case 0x19F2: `SetVisible(this+78, 0)`).
	Bind(TEXT("OldSoulmate_CloseBtn"), 6642, [this]() { CloseOldSoulGrid(); });
	// ★ [ПОРТ] Ревизия arabic_text_REVIEW С4 (08.10.2026): жёсткий стоп набора поля заметки 6602 — `LimitText = "64"`
	//   разметки (`gamemain_jui.ini:186677`, EM_LIMITTEXT оригинала по байтам cp1251) в БАЙТАХ ПРОВОДА 6052 (`char[65]`, 64 + NUL):
	//   арабский — маркер + cp1256, смешанный — UTF-8. Без стопа гейт `SendNote` молча отбрасывал заметку длиннее поля.
	if (UEditableText* NoteEdit = Cast<UEditableText>(CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input"))))
	{
		UAsdaTextLimiter* L = NewObject<UAsdaTextLimiter>(this);
		const TWeakObjectPtr<UEditableText> WeakEdit = NoteEdit;
		L->Callback = [WeakEdit](const FText& NewText)
		{
			UEditableText* Fld = WeakEdit.Get();
			if (!Fld) { return; }
			const FString S = NewText.ToString();
			const FString Cut = FAsdaLoginData::TruncateToWireLimit(S.Left(64), 64, 64);   // по границе символа
			if (Cut != S) { Fld->SetText(FText::FromString(Cut)); }
		};
		NoteEdit->OnTextChanged.AddDynamic(L, &UAsdaTextLimiter::OnTextChanged);
		AsdaUi::AnchorRelay(Relays, L, NoteEdit);   // ★★★ GC-якорь: динамический делегат держит объект СЛАБО
		// ★ 26.10.2026 (smmessage) [ИСПРАВЛЕНИЕ] Enter в поле 6602 = «Отправить» 6603. У оригинала Enter в поле записки — событие
		//   67587 с id 6602, его `SoulmateUI_HandleEvent` не разбирает (social.c:7899 — только 9101); правило EnterConfirm
		//   (`Reports/enter_confirm/AUDIT.md` §3.1, COMMUNITY_WIN: `Soul_Msg_OkBtn` — «записка, обратимо») относит кнопку к
		//   безвредным, а п.1 правила отдаёт Enter в поле самому полю — как у поля 9101 окна 9100 и поля 1583 окна 1580.
		//   Только по ENTER и только пока окно записки видно (снятие фокуса/прочие commit — не отправка). Ключ
		//   `asda.Soulmate.NoteEnterSend 0` — как у оригинала (Enter в поле ничего не делает).
		UAsdaTextLimiter* C = NewObject<UAsdaTextLimiter>(this);
		const TWeakObjectPtr<UEditableText> WeakField = NoteEdit;
		C->CommitCallback = [this, WeakField](const FText&, ETextCommit::Type Method)
		{
			if (Method != ETextCommit::OnEnter || !AsdaSoulNoteEnterSendOn()) { return; }
			if (!WeakField.IsValid() || !IsNotePanelShown()) { return; }
			UE_LOG(LogAsdaSoulmate, Log, TEXT("[записка] Enter в поле 6602 → «Отправить» 6603"));
			SendNote();
		};
		NoteEdit->OnTextCommitted.AddDynamic(C, &UAsdaTextLimiter::OnTextCommitted);
		AsdaUi::AnchorRelay(Relays, C, NoteEdit);   // GC-якорь   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}
	// ★★★ ГАЛКИ РЕЖИМОВ 6543..6546 (SOUL_CHECK00..03) → 5221.
	//   ИСПРАВЛЕН ТИП ВИДЖЕТА. Скин `Box_Check` в `JuiSkin.ini` имеет `Type = "CheckButton"`, а
	//   импортёр делает из `CheckButton` `UCheckBox` (закон `jui-checkbutton-vs-radiobutton`).
	//   Прежний код искал их `FindBtn` (`Cast<UButton>`) и вешал `OnClicked` — для `UCheckBox` каст
	//   давал nullptr, и привязка МОЛЧА не срабатывала (не замечали: блок share был скрыт, а галка
	//   6546 «окно сведений» так и не кликалась). Теперь берём `UCheckBox` и слушаем
	//   `OnCheckStateChanged` (мостик `UAsdaButtonRelay::OnCheckChanged`, как у панели меню 10 тогглов).
	for (int32 i = 0; i < 4; ++i)
	{
		const FString Idc = FString::Printf(TEXT("SOUL_CHECK%02d"), i);
		if (UCheckBox* Cb = FindCheck(*Idc))
		{
			UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
			R->Index = i;
			R->CheckCallback = [this](int32 Which, bool bChecked) { OnShareCheckChanged(Which, bChecked); };
			Cb->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
			AsdaUi::AnchorRelay(Relays, R, Cb);   // ★★★ GC-якорь: динамический делегат держит реле СЛАБО
		}
	}
	// ★ Признак привязки взводим ДО наполнения: `ApplyStaticCaptions`/`ApplyAlwaysHidden` сами
	//   защищены гейтом `IsBoundTo` (чтобы сетевые обработчики не рисовали в пересозданное дерево),
	//   и при обратном порядке они молча ничего бы не сделали.
	BoundWnd  = CommWnd;   // ★ на ЭТОМ виджете стоит привязка — по нему решает `IsBoundTo`
	bTabBound = true;
	// Порт статической части `CSoulmateWnd_InitControls`: подписи, пустые поля, «0» на сердце,
	//   спрятанное окно заметки и спрятанный блок доступа к персонажу.
	ApplyStaticCaptions();
	ApplyAlwaysHidden();
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] контролы вкладки 0 привязаны"));
}

// ★★★ ПРИВЯЗАНА ЛИ ВКЛАДКА К ЭТОМУ КОНКРЕТНОМУ ВИДЖЕТУ ОКНА (разбор — в объявлении).
//   ⚠ Дополнительных проверок «а нашёлся ли такой-то узел» тут быть не должно: если узла в
//   разметке нет, он не появится НИКОГДА, и такая проверка гнала бы `BindTabControls` на каждый
//   показ вкладки — реле кнопок копились бы, и одно нажатие давало бы несколько действий.
bool UAsdaSoulmateSubsystem::IsBoundTo(const UUserWidget* W) const
{
	if (!W || !bTabBound) { return false; }
	return BoundWnd.Get() == W;
}

// ═════════════ СТАТИЧЕСКОЕ НАПОЛНЕНИЕ ВКЛАДКИ (порт `CSoulmateWnd_InitControls@0xE5E440`) ═════
// Соответствие «контрол → loc» снято ДОСЛОВНО с эталона (`RE/etalon/social.c`, строки указаны
//   у каждой группы). Ни один текст-заглушка дизайнера не должен доживать до экрана.
void UAsdaSoulmateSubsystem::ApplyStaticCaptions()
{
	// ★ Рисуем только в ЖИВОЕ привязанное дерево: после пересоздания окна 6400 старые указатели
	//   ведут в уничтоженные виджеты (разбор — в объявлении `IsBoundTo`).
	if (!IsBoundTo(CommWnd)) { return; }

	// ── ЛЕВЫЙ БЛОК «Информация о Друге» (social.c:6242-6251 и 6367-6376) ───────────────────────
	//   ⚠ У подписей 6550..6554 в эталоне ДВА прохода: сперва 8017..8022, затем ПОВЕРХ 12099,
	//     12100, 12101, 12102, 12109. Выигрывает второй — его и ставим (первый мёртв).
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT00"), Loc(12098));  // 6574 заголовок блока
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB00"),  Loc(12099));  // 6550 подпись «состояние»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB01"),  Loc(12100));  // 6551 подпись «имя»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB02"),  Loc(12101));  // 6552 подпись «уровень»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB03"),  Loc(12102));  // 6553 подпись «профессия»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB04"),  Loc(12109));  // 6554 подпись «локация»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB07"),  Loc(12103));  // 6575 «Last Logout Time»

	// ── ПРАВЫЙ БЛОК «Сведения о Друге» (social.c:6252-6291) ───────────────────────────────────
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT11"),      Loc(12105));  // 6576 заголовок блока
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_LEVEL_TEXT"), Loc(12106));  // 6577 «Soulmaye Level»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_EXP_TEXT"),   Loc(12107));  // 6578 «опыт Дружбы»
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_SB05"),       Loc(12138));  // 6555 подпись галки 6546

	// ── БЛОК «доступ к персонажу» (social.c:6267-6286). В РАЗМЕТКЕ ОН СКРЫТ (`Visible="0"` у
	//   6579 и 6540/6541/6542), но эталон подписи всё равно ставит — повторяем 1:1, а видимость
	//   возвращает `ApplyAlwaysHidden`.
	//   ★★★ НАШЕ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (требование владельца 03.09.2026). В ОРИГИНАЛЕ три подписи
	//     SHARE несут loc 12110 = «Распределение добычи». Владелец потребовал заменить надпись на
	//     «Делиться». Существующий loc 12110 НЕ переписываем (строка «Распределение добычи» может
	//     использоваться в других окнах/данных) — завели ОТДЕЛЬНУЮ строку таблицы текста
	//     `990130` = «Делиться» (проектный диапазон 9xxxxx, `GameText.csv`) и подставляем её.
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT06"), Loc(12108));  // 6579 заголовок блока
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SHARE_00"),   Loc(990130)); // 6580 «Делиться» (наш loc)
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SHARE_01"),   Loc(990130)); // 6581 «Делиться» (наш loc)
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SHARE_02"),   Loc(990130)); // 6582 «Делиться» (наш loc)
	// 6540/6541/6542 `SOUL_TXT07..09` эталон заливает ПУСТОЙ строкой (цикл social.c:6068-6077).
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT07"), FString());
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT08"), FString());
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT09"), FString());

	// ── ЛОТОК НАВЫКОВ ДРУЖБЫ (social.c:6377-6381) ─────────────────────────────────────────────
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("Soulmate_Skill_Title"), Loc(12188));   // 6586

	// ── КНОПКИ ВКЛАДКИ (social.c:6382-6391 и 6623-6638) ───────────────────────────────────────
	//   Подпись кнопки импортёр кладёт в узел `<idc>_Text`; `SetButtonCaption` знает эту цепочку.
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_MessageBtn"),  Loc(12170));  // 6583
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_OldSoul_Btn"), Loc(12171));  // 6584
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_CutoffBtn"),   Loc(8045));   // 6587
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_Find_Btn"),    Loc(8046));   // 6588

	// ── ОКНО ЗАМЕТКИ 6605 (social.c:6397-6413) ────────────────────────────────────────────────
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("Soul_Msg_TitleTxt"), Loc(12174));           // 6600
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("Soul_Msg_Txt01"),    Loc(12175));           // 6601
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_Msg_OkBtn"), Loc(2288));     // 6603
	AsdaCommunityUi::SetButtonCaption(CommWnd, TEXT("Soul_Msg_ClBtn"), Loc(2289));     // 6604

	// ── ПОЛЯ ДАННЫХ: до первого пакета эталон держит в них ПУСТУЮ строку (`&Src`) ─────────────
	//   social.c:6023-6063. 6522 `SOUL_TXT04` не заполняется вообще НИКОГДА (читателей у `this+6`
	//   в бинаре нет) — дефект оригинала, подпись 6575 остаётся без значения. ★ 24.10.2026 [ИСПРАВЛЕНИЕ]: порт пишет его у Друга
	//   вне игры (время последнего выхода, P79) — `FillOfflineFriendFields`, ключ `asda.Soulmate.OfflineFriendInfo`.
	static const TCHAR* const kDataFields[] = {
		TEXT("SOUL_TXT01"),   // 6548 статус
		TEXT("SOUL_TXT02"),   // 6520 имя
		TEXT("SOUL_TXT03"),   // 6521 уровень
		TEXT("SOUL_TXT04"),   // 6522 время последнего выхода (эталон не пишет)
		TEXT("SOUL_TXT05"),   // 6523 профессия
		TEXT("SOUL_TXT10"),   // 6556 звание
		TEXT("SOUL_TXT12"),   // 6557 уровень Дружбы
		TEXT("SOUL_TXT13"),   // 6558 доля опыта Дружбы
	};
	if (!bCardReceived)
	{
		for (const TCHAR* Idc : kDataFields) { AsdaCommunityUi::SetTxt(CommWnd, Idc, FString()); }
		if (TabGauge01.IsValid())
		{
			UAsdaMiniStateSubsystem::ClipFill(TabGauge01, 0.f);   // 6559 — полоса пуста до пакета
		}
	}

	// ── СЕРДЦЕ 6585 (social.c:6392-6396) ──────────────────────────────────────────────────────
	//   ★★★ ДЕФЕКТ ОРИГИНАЛА: `m_pHeartTxt` во ВСЁМ бинаре имеет РОВНО ОДНОГО писателя — эту
	//   строку, «0». Ни один обработчик пакета и ни одна ветка обновления карточки в него не
	//   пишет, то есть индикатор мёртв и всегда показывает ноль (подсказка у него живая:
	//   loc 12186/12187 «Очки Дружбы»). Число «53» на экране — заглушка дизайнера из разметки,
	//   а не данные. Данных для этого поля в протоколе нет, поэтому воспроизводим эталон: «0».
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("Soul_HeartTxt"), TEXT("0"));

	// ── ПСЕВДОЖИРНЫЙ (эталонный флаг вывода 926 = 0x200000, `font_text.c:6832`) ────────────────
	//   Его эталон ставит ровно двум контролам вкладки: сердцу 6585 и заголовку окна заметки 6600
	//   (в разметке у 6600 даже сохранилась пометка дизайнера «(볼드)» = «(bold)»).
	//   ★ 2026-09-24: делаем ИМЕННО псевдо-жирный (второй проход глифа по горизонтали), а не
	//   Bold-гарнитуру — она шире эталона на 7 % (разбор — `AsdaUi::ApplyPseudoBold`).
	auto MakeBold = [this](const TCHAR* Idc)
	{
		AsdaUi::ApplyPseudoBold(AsdaCommunityUi::FindLabel(CommWnd, Idc));
	};
	MakeBold(TEXT("Soul_HeartTxt"));
	MakeBold(TEXT("Soul_Msg_TitleTxt"));

	// ── ЦВЕТ ЧИСЕЛ (дефект оригинала D16, см. `CVarSoulmateWhiteNumbers`) ──────────────────────
	static const TCHAR* const kDarkNumbers[] = { TEXT("Soul_HeartTxt"), TEXT("SOUL_TXT12") };
	if (CVarSoulmateWhiteNumbers.GetValueOnGameThread() != 0)
	{
		for (const TCHAR* Idc : kDarkNumbers)
		{
			if (UTextBlock* T = AsdaCommunityUi::FindLabel(CommWnd, Idc))
			{
				T->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			}
		}
	}
}

// ═════════════ УЗЛЫ, СКРЫТЫЕ РАЗМЕТКОЙ ═══════════════════════════════════════════════════════
// Список снят с дампа `Reports/soulmate_win6400_jui_dump.md` (все `vis=0` внутри 6402), из
//   него исключены `SOUL_TRAYSEL` 6538 и `SOUL_WHELL` 6539 — ими владеет
//   `UAsdaSoulmateSkillSubsystem` (лоток и рулетка), двум подсистемам один узел не отдаём.
// ★ ТОЧНАЯ ВИДИМОСТЬ УЗЛА РАЗМЕТКИ И ЕГО ПОДДЕРЕВА. `AsdaCommunityUi::SetVisByPrefix` здесь не
//   годится для `SOUL_TXT06`: под тот же префикс попадают контейнеры `SOUL_TXT06_B1*`/`SOUL_TXT06_B2*`,
//   у которых в разметке `Visible="1"` (в оригинале пустая серая коробка блока НАРИСОВАНА).
//   Правило принадлежности: имя равно idc, либо остаток — `_grp`, `_Text`, `_HIT` или начинается с
//   ЦИФРЫ (`<idc>_<обход>_<план>`, `<idc>_<обход>_HIT`, закон `jui-importer-widget-naming`).
//   Буквенный остаток (`_B1`, `_B2`) — ЧУЖОЙ узел. ПОКАЗ — через `ShownVisibilityFor` (интерактиву
//   ловящая мышь `Visible`, декору `SelfHitTestInvisible`, закон `selfhittest-vs-hittest-invisible`);
//   гашение — `Collapsed`.
void UAsdaSoulmateSubsystem::SetBlockNodeVisible(const TCHAR* Idc, bool bVisible)
{
	if (!CommWnd || !CommWnd->WidgetTree) { return; }
	const FString Name(Idc);
	const FString Pref = Name + TEXT("_");
	CommWnd->WidgetTree->ForEachWidget([&Name, &Pref, bVisible](UWidget* W)
	{
		if (!W) { return; }
		const FString N = W->GetName();
		bool bMine = N.Equals(Name, ESearchCase::IgnoreCase);
		if (!bMine && N.StartsWith(Pref, ESearchCase::IgnoreCase))
		{
			const FString Rest = N.RightChop(Pref.Len());
			bMine = !Rest.IsEmpty()
				&& (FChar::IsDigit(Rest[0])
					|| Rest.Equals(TEXT("grp"),  ESearchCase::IgnoreCase)
					|| Rest.Equals(TEXT("Text"), ESearchCase::IgnoreCase)
					|| Rest.Equals(TEXT("HIT"),  ESearchCase::IgnoreCase));
		}
		if (bMine)
		{
			W->SetVisibility(bVisible ? AsdaCommunityUi::ShownVisibilityFor(W)
			                          : ESlateVisibility::Collapsed);
		}
	});
}

void UAsdaSoulmateSubsystem::ApplyAlwaysHidden()
{
	if (!IsBoundTo(CommWnd)) { return; }

	// Окно ввода заметки 6605: `CObject_SetField166/SetFlag668(m_pMsgWin, 0)` в InitControls
	//   (social.c:6400-6401) и `sub_92F599` при каждом уходе с вкладки.
	SetBlockNodeVisible(TEXT("Soul_MsgWin"), false);

	// ★★★ БЛОК «ДОСТУП К ПЕРСОНАЖУ» (6579 / 6540..6542 / SHARE_00..02 / SOUL_CHECK00..02 / Soul_Hline1).
	//   В ОРИГИНАЛЕ он скрыт разметкой (`Visible="0"`; дети скрытого родителя в оригинале не рисуются —
	//   `CControl_RenderChildren@0x12DF9C0` рекурсирует только в видимых детей). НАШЕ ОСОЗНАННОЕ
	//   ОТСТУПЛЕНИЕ (требование владельца, `fix-original-defects`): при ключе
	//   `asda.Soulmate.ShareBlockVisible`!=0 показываем и наполняем блок (`RefreshShareBlock`),
	//   иначе гасим его целиком, как оригинал. У импортёра иерархия расплющена (закон
	//   `jui-uneven-node-layout`) — галки/подписи лежат соседями, поэтому гасим/показываем КАЖДЫЙ
	//   узел блока явно, а не рассчитываем на гашение родителя.
	if (CVarSoulmateShareBlock.GetValueOnGameThread() != 0)
	{
		RefreshShareBlock();
	}
	else
	{
		static const TCHAR* const kBlockNodes[] = {
			TEXT("SOUL_TXT06"),   // 6579 заголовок
			TEXT("SOUL_TXT07"), TEXT("SOUL_TXT08"), TEXT("SOUL_TXT09"),   // 6540/6541/6542 имена
			TEXT("SHARE_00"),   TEXT("SHARE_01"),   TEXT("SHARE_02"),     // 6580/6581/6582 «поделиться»
			TEXT("SOUL_CHECK00"), TEXT("SOUL_CHECK01"), TEXT("SOUL_CHECK02"),   // 6543/6544/6545 галки
			TEXT("Soul_Hline1"),
		};
		for (const TCHAR* Idc : kBlockNodes) { SetBlockNodeVisible(Idc, false); }
	}

	// Планы-состояния: «в сети» у индикатора 6548 и «зажжённое» сердце. Оба в разметке скрыты,
	//   переключателей у них в эталоне нет.
	SetBlockNodeVisible(TEXT("Soul_ConnectOn"), false);
	SetBlockNodeVisible(TEXT("Soul_Heart2"),   false);
}

// ★★★ ПОКАЗ И НАПОЛНЕНИЕ БЛОКА «ПОДЕЛИТЬСЯ ПЕРСОНАЖЕМ» (НАШЕ отступление, ключ
//   `asda.Soulmate.ShareBlockVisible`). Заголовок 6579 (loc 12108) и подписи «Делиться» 6580..6582
//   (наш loc 990130) уже поставлены `ApplyStaticCaptions`; здесь — видимость, данные строк и сдвиг:
//     · ИМЕНА трёх персонажей (6540/6541/6542) берём из НАШЕГО списка персонажей
//       (`UAsdaNetSubsystem::CharList`, 3 слота — тот же источник, что экран выбора персонажа).
//       Сервер эти имена в оригинале НЕ шлёт (`soulmate_share_feature_client.md` §8), поэтому ждать
//       их от 5220 нельзя. Пустой слот (нет персонажа) — строку и её галку ПРЯЧЕМ (как оригинал,
//       который прячет галку при пустом имени, §4), а не выводим «Аяя».
//     · СОСТОЯНИЕ галок (6543/6544/6545) — из `Card.Modes[0..2]`. Приёма 4 байт опций из 5220 в
//       порту нет (делегат `OnSoulmateState` разбирает только charId/soulIdx — см. отчёт, «требуется
//       от других»), поэтому до первого клика галки сняты (0) — это и есть эталонное «иначе 0».
void UAsdaSoulmateSubsystem::RefreshShareBlock()
{
	if (!IsBoundTo(CommWnd)) { return; }

	// Заголовок и горизонтальная линия блока — видимы всегда, пока блок включён.
	SetBlockNodeVisible(TEXT("SOUL_TXT06"), true);
	SetBlockNodeVisible(TEXT("Soul_Hline1"), true);

	// Список моих персонажей (читаем — правка `AsdaNetSubsystem` не требуется).
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	static const TCHAR* const kRowName[3]  = { TEXT("SOUL_TXT07"),  TEXT("SOUL_TXT08"),  TEXT("SOUL_TXT09")  };
	static const TCHAR* const kRowShare[3] = { TEXT("SHARE_00"),    TEXT("SHARE_01"),    TEXT("SHARE_02")    };
	static const TCHAR* const kRowChk[3]   = { TEXT("SOUL_CHECK00"),TEXT("SOUL_CHECK01"),TEXT("SOUL_CHECK02")};

	for (int32 i = 0; i < 3; ++i)
	{
		FString Name;
		bool bHasChar = false;
		// ★ 23.10.2026 (sm-info) [ОРИГИНАЛ]: имя строки — из своей 5220 (`strlen(v12+33/53/73)`, itemTooltip_RefreshControls);
		//   сервер их ЗАПОЛНЯЕТ (DBManager `UPSoulMateShare`, с P78 — строго по слотам 10/11/12; прежний вывод отчёта
		//   «сервер имена не шлёт» — ложное «нет»). До первой 5220 — прежний путь (список персонажей).
		if (CVarSoulmateShareNames5220.GetValueOnGameThread() != 0 && ShareRowNameFrom5220(i, Name))
		{
			bHasChar = !Name.IsEmpty();
		}
		else if (Net && Net->CharList.IsValidIndex(i) && !Net->CharList[i].IsEmpty())
		{
			Name = Net->CharList[i].Name;
			bHasChar = !Name.IsEmpty();
		}

		// Имя персонажа в строку 6540/6541/6542.
		AsdaCommunityUi::SetTxt(CommWnd, kRowName[i], Name);

		// Пустой слот прячем целиком (строка + подпись + галка), непустой — показываем.
		SetBlockNodeVisible(kRowName[i],  bHasChar);
		SetBlockNodeVisible(kRowShare[i], bHasChar);
		SetBlockNodeVisible(kRowChk[i],   bHasChar);

		// ★ 08.10.2026: положение подписи «Делиться» и галки — В АССЕТЕ (шаг `asda_ui_widen_windows`), рантайм-сдвиг снят.

		// Начальное состояние галки из `Card.Modes` (SetIsChecked не шлёт OnCheckStateChanged —
		//   лишней отправки 5221 и рекурсии не будет).
		if (UCheckBox* Cb = FindCheck(kRowChk[i]))
		{
			Cb->SetIsChecked(Card.Modes[i] != 0);
			// ★ 23.10.2026: своя одержимость — галка погашена (эталон `CChat_SetIconState(check, 0)` при блоке +26 == 2).
			Cb->SetIsEnabled(!IsShareLocked());
		}
	}
}

// ★ 23.10.2026 (sm-info): имя строки «поделиться» i из своей 5220 (@39 + 20·i, cp1251 до NUL). false — 5220 не было.
bool UAsdaSoulmateSubsystem::ShareRowNameFrom5220(int32 I, FString& OutName) const
{
	if (I < 0 || I > 2 || SelfInfo5220.Num() < 101) { return false; }
	OutName = FAsdaLoginData::DecodeCp1251(&SelfInfo5220[39 + 20 * I], 20);
	return true;
}

// ★ 23.10.2026 (sm-info): галки «поделиться» заблокированы — я одержим (свой блок +26 == 2 или 1017 режим 2).
bool UAsdaSoulmateSubsystem::IsShareLocked() const
{
	if (CVarSoulmateShareLockPossessed.GetValueOnGameThread() == 0) { return false; }
	if (SelfConnectType == 2) { return true; }
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net && Net->bSelfPossessed;
}

// ★ 23.10.2026 (sm-info): уровень Дружбы — свой блок +12 (`CCharacter+0x8B3`): пишут 5220 @18 и 5110/5111 rec[50].
int32 UAsdaSoulmateSubsystem::GetFriendLevel() const
{
	if (CVarSoulmateOfflineInfo.GetValueOnGameThread() != 0 && bBlkLevelKnown) { return (int32)FriendLevelBlk; }
	return (int32)Rec.FriendLevel();
}

// ★ 23.10.2026 (sm-info) [ОРИГИНАЛ] 6523 «Локация» — `CSoulmateWnd_RefreshInfo` (social.c:6996-7012) и
//   `tradeWnd_updateNoticeAndTarget` (shop_trade.c:21688-21708): карта ≥ 101 → пусто; канал 6 → «<loc 13927> <карта>»;
//   иначе «  <канал+1> <loc 1187> <карта>». Имя карты — общая таблица `Map_info` (тот же источник, что у мини-карты/журнала).
FString UAsdaSoulmateSubsystem::LocationText(int32 Map, int32 Chan, bool b5227) const
{
	if (Map < 0 || Map >= 101) { return FString(); }
	FString MapName;
	if (UAsdaTextSubsystem* T = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
	{
		MapName = T->GetText(TEXT("Map_info"), Map, TEXT("name"));
	}
	// ★ 23.10.2026 (ревизия, п.2): канал 6 — у 5227 формат " %s %s" (shop_trade.c:21700, ведущий пробел), у RefreshInfo "%s %s" (social.c:7002).
	if (Chan == 6) { return FString::Printf(TEXT("%s%s %s"), b5227 ? TEXT(" ") : TEXT(""), *Loc(13927), *MapName); }
	return FString::Printf(TEXT("  %d %s %s"), Chan + 1, *Loc(1187), *MapName);
}

// ═══════════ ★ 23.10.2026 (sm-info) ПОРТ `itemTooltip_RefreshControls@0xE62710` — ВКЛАДКА ПО СВОЕМУ БЛОКУ ═══════════
//   Дизасм 0xE62710…: v12 = CCharacter+2215 (свой блок 95 б), v13 = CSoulmateWnd:
//     *(int*)v12 <= 0                 → `uiPanelClearAllFields` (все тексты пусты, галки 6543..6546 спрятаны, полоса 0);
//     !v12[19] (Друг вне игры) || v12[26]==2 (я одержим) → 6548 = (одержим ? "" : loc 8054), 6520/6521/6556/6523 = "";
//     строки 6540..6542 ← имена +33/+53/+73 (пусто — галка спрятана); 6557 ← «%d <12215> » из +0x8B3 (0 — пусто);
//     6558/6559 ← доля опыта из +0x8AB; галки ← +30..32, 6546 видна ← +27; одержим — галки погашены; лоток навыков.
void UAsdaSoulmateSubsystem::RefreshSelfBlockControls(const TCHAR* Why)
{
	if (CVarSoulmateOfflineInfo.GetValueOnGameThread() == 0) { return; }
	UGameInstance* GI = GetGameInstance();
	// Лоток навыков Дружбы (`skillHotbarRefreshSlots`) — от уровня Дружбы, живёт своей подсистемой.
	if (UAsdaSoulmateSkillSubsystem* Sk = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr) { Sk->RefreshTray(); }
	if (!IsBoundTo(CommWnd)) { return; }
	// Своего блока ещё не было ни из одного пакета — вкладка в виде «только построена» (как прежде).
	if (SelfInfo5220.Num() < 101 && !bCardReceived && !bBlkLevelKnown) { return; }
	static const TCHAR* const kInfo[] = { TEXT("SOUL_TXT02"), TEXT("SOUL_TXT03"), TEXT("SOUL_TXT10"), TEXT("SOUL_TXT05") };
	if (Card.SlotIndex <= 0)
	{
		// `uiPanelClearAllFields@0xE62C20`.
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT01"), FString());
		for (const TCHAR* Idc : kInfo) { AsdaCommunityUi::SetTxt(CommWnd, Idc, FString()); }
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT07"), FString());
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT08"), FString());
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT09"), FString());
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT12"), FString());
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT13"), FString());
		// ★ 24.10.2026 (sm-offline): 6522 эталон не трогает (он его и не пишет); порт его пишет — значит, и гасит.
		if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0) { AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT04"), FString()); }
		if (TabGauge01.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(TabGauge01, 0.f); }
		SetBlockNodeVisible(TEXT("SOUL_CHECK00"), false);
		SetBlockNodeVisible(TEXT("SOUL_CHECK01"), false);
		SetBlockNodeVisible(TEXT("SOUL_CHECK02"), false);
		SetBlockNodeVisible(TEXT("SOUL_CHECK03"), false);
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] вкладка по своему блоку (%s): пары нет — поля очищены"), Why);
		return;
	}
	const bool bPossessed = (SelfConnectType == 2);
	const bool bOffline = !Card.bHasSoulmate || bPossessed;
	if (bOffline)
	{
		AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT01"), bPossessed ? FString() : Loc(8054));   // «Вне игры»
		// ★ 24.10.2026 (sm-offline) [ИСПРАВЛЕНИЕ]: сведения о Друге вне игры из ответа 2243 (P79); нет их / я одержим — пусто, как эталон.
		const bool bFilled = !bPossessed && FillOfflineFriendFields();
		if (!bFilled)
		{
			for (const TCHAR* Idc : kInfo) { AsdaCommunityUi::SetTxt(CommWnd, Idc, FString()); }
			if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0) { AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT04"), FString()); }
		}
	}
	// Строки и галки «поделиться» (наш показ блока — ключ ShareBlockVisible; имена и состояние — из блока).
	if (CVarSoulmateShareBlock.GetValueOnGameThread() != 0) { RefreshShareBlock(); }
	// 6557 — уровень Дружбы из своего блока.
	const int32 Lv = GetFriendLevel();
	AsdaCommunityUi::SetTxt(CommWnd, TEXT("SOUL_TXT12"), Lv ? FString::Printf(TEXT("%d %s "), Lv, *Loc(12215)) : FString());
	// 6558/6559 — доля опыта Дружбы (`uiProgressBar_RefreshPercent`, аргумент — QWORD своего блока).
	const float Pct = FriendExpPercent();
	if (TabGauge01.IsValid()) { UAsdaMiniStateSubsystem::ClipFill(TabGauge01, Pct / 100.f); }
	if (TxtPercent && Pct >= 0.f) { TxtPercent->SetText(FText::FromString(FString::Printf(TEXT("%.1f %%"), Pct))); }
	// 6546 — видна, состояние из блока (+27); одержим — снята и погашена.
	SetBlockNodeVisible(TEXT("SOUL_CHECK03"), true);
	if (UCheckBox* Cb = FindCheck(TEXT("SOUL_CHECK03")))
	{
		Cb->SetIsChecked(!bPossessed && Card.Modes[3] != 0);
		Cb->SetIsEnabled(!IsShareLocked());
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] вкладка по своему блоку (%s): слот=%d Друг %s%s, ур. Дружбы=%d, доля=%.1f%%"),
		Why, Card.SlotIndex, bOffline ? TEXT("ВНЕ ИГРЫ") : TEXT("в игре"), bPossessed ? TEXT(" (я одержим)") : TEXT(""), Lv, Pct);
}

// ═══════════ ★ 23.10.2026 (sm-info) 5227 — ДРУГ СМЕНИЛ КАРТУ/КАНАЛ (`tradeWnd_updateNoticeAndTarget@0xE67B60`, миснейм) ═══
//   Сервер `sub_4949C0@0x4949C0`: тело 2 б — [0] канал (CUser+99024 → блок +23), [1] карта (+99023 → блок +22).
//   Эталон: `gap_8BE[0] = [0]`, `+0x8BD = [1]`; 6523 ← «Локация» (карта ≥ 101 — пусто); если карта Друга == моя — снова
//   включает кольцо соулмейта себе и Другу (`sub_A38020`); у порта кольца держит кэш SoulState по 5220 и спавн — новых
//   данных 5227 для них не несёт. ⚠ Фильтр отметки на карте у эталона — rec[38] записи 5110 (5227 его НЕ меняет).
void UAsdaSoulmateSubsystem::OnPartnerMoved5227(const TArray<uint8>& Body)
{
	if (CVarSoulmateRecv5227.GetValueOnGameThread() == 0 || Body.Num() < 2) { return; }
	PartnerChannelBlk = Body[0];
	PartnerMapBlk     = Body[1];
	if (IsBoundTo(CommWnd) && CVarSoulmateLocationText.GetValueOnGameThread() != 0 && TxtJob)
	{
		TxtJob->SetText(FText::FromString(LocationText(PartnerMapBlk, PartnerChannelBlk, /*b5227*/true)));
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←5227 Друг сменил место: карта=%d канал=%d"), PartnerMapBlk, PartnerChannelBlk);
	PushCoupleMark(false);
}

// ═══════════ ★ 23.10.2026 (sm-info) ОТМЕТКА ДРУГА НА КАРТАХ — `minimapDrawTargetMarker@0xB56BB0` ═══════════
//   Эталон (world_map.c:3381-3418): режим 1 (большая карта) — rec[38] == показанная карта; 2/3 (мини-карта) — rec[38] ==
//   g_nCurMapId (фильтр по карте делает общая мини-карта по `MapId` отметки); затем гейт: свой слот (`gap_878[47]`) > 0,
//   Друг в игре (`gap_8B4[6]` == 1), канал Друга (`gap_8BE[0]`) == свой канал (`sub_92B66A(&g_ServerSelectWnd)`);
//   позиция — тайл rec[25]/rec[27] (5110/5111), текстура `miniCouplePos` (CMap+520), квад 16×16 по центру.
void UAsdaSoulmateSubsystem::PushCoupleMark(bool bDeferred)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	if (MM)
	{
		const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
		const bool bOn = CVarSoulmateMapMark.GetValueOnGameThread() != 0;
		const bool bShow = bOn && bCardReceived && Card.SlotIndex > 0 && Card.bHasSoulmate
			&& Net && PartnerChannelBlk >= 0 && PartnerChannelBlk == Net->CurrentChannel;
		if (bShow)
		{
			FAsdaMapMark K;
			K.TileX = Rec.TileX();
			K.TileY = Rec.TileZ();
			K.MapId = (int32)Rec.MapId();   // всегда явная карта (эталон сравнивает rec[38])
			K.Kind  = EAsdaMapMark::Couple;
			MM->SetMarks(EAsdaMapMark::Couple, { K });
		}
		else
		{
			MM->ClearMarks(EAsdaMapMark::Couple);
		}
	}
	if (bDeferred && !bCouplePushQueued)
	{
		bCouplePushQueued = true;
		TWeakObjectPtr<UAsdaSoulmateSubsystem> Weak(this);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
		{
			if (UAsdaSoulmateSubsystem* Self = Weak.Get()) { Self->bCouplePushQueued = false; Self->PushCoupleMark(false); }
			return false;   // один раз
		}), 0.f);
	}
}

bool UAsdaSoulmateSubsystem::GetCoupleMarkForCheck(int32& OutTileX, int32& OutTileZ, int32& OutMap) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaMinimapSubsystem* MM = GI ? GI->GetSubsystem<UAsdaMinimapSubsystem>() : nullptr;
	const TArray<FAsdaMapMark>* M = MM ? MM->FindMarks(EAsdaMapMark::Couple) : nullptr;
	if (!M || M->Num() != 1) { return false; }
	OutTileX = (*M)[0].TileX; OutTileZ = (*M)[0].TileY; OutMap = (*M)[0].MapId;
	return true;
}

// 6605 `Soul_MsgWin` — единственная точка показа/гашения окна заметки.
//   ★★★ ПОЧЕМУ ОКНО «ОТКРЫВАЛОСЬ САМО». Порт искал узел по имени `Soul_MsgWin`, а импортёр
//   называет группу поддерева `<idc>_grp` (закон `jui-importer-node-names`): узла `Soul_MsgWin`
//   в ассете НЕТ, `GetWidgetFromName` возвращал nullptr, и гашение молча не срабатывало ни разу.
//   Показать окно при этом успевал контейнер вкладки, который поднимает ВСЁ поддерево.
void UAsdaSoulmateSubsystem::ShowNotePanel(bool bShow)
{
	if (!IsBoundTo(CommWnd)) { return; }
	AsdaCommunityUi::SetVisByPrefix(CommWnd, TEXT("Soul_MsgWin"), bShow);
	if (!bShow)
	{
		// Эталон снимает фокус с поля ввода 6602 (`sub_E6AD20`: `SetFocus(this+60, 0)`).
		if (UEditableText* E = Cast<UEditableText>(CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input"))))
		{
			E->SetText(FText::GetEmpty());
			// ★ 26.10.2026 (smmessage) [ОРИГИНАЛ] `CControl_SetFocus(+60, 0)` (case 0x19CB/0x19CC): фокус с поля СНЯТЬ — иначе
			//   клавиатура оставалась у свёрнутого поля, и клавиши не доходили до игры (как у `cFindSoulmateWinHide`, поле 8302).
			//   Ключ — общий с фокусом при открытии (`asda.Soulmate.NoteInputFocus`).
			if (AsdaSoulNoteInputFocusOn() && E->HasKeyboardFocus() && FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().SetAllUserFocusToGameViewport();
			}
		}
	}
}
