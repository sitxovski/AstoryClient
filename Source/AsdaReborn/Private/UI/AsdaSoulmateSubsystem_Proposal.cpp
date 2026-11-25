// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSoulmateSubsystem_Proposal.cpp — ★ R2 10.11.2026 (разрез AsdaSoulmateSubsystem.cpp, дословный перенос): предложение/связь: окно связи, предложение, OnCard, меню игрока, пара/разрыв, уход партнёра.
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
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaChatWindowSubsystem.h"
#include "World/AsdaWorldSubsystem.h"   // GetSelfCharName — общая точка «своё имя» (ветка 5203 код 2)
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaSoulAppleSubsystem.h"   // ★ ревизия 1-Б С-1: яблочная часть inventoryHidePanelsAndRefresh
// ★ 23.10.2026: панель занятости 79900 — под панелью Друга 6500   ★ R3: комментарий снятого включения "UI/AsdaMinimapSubsystem.h" (файл им не пользовался) — сохранён
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

// ★★★ ДЕФЕКТ ОРИГИНАЛА D15 (найден 31.08 при закрытии T10). `coupleSendProposalReq@0xE658E0`
//   и `coupleSendProposalReqTarget@0xE655B0` кладут в payload[25..152] пакета 5200 текст «о себе»
//   из `sub_929E82(g_pFindSoulmateWin)` = `sub_E658B0` = `CFindSoulmateWin + 516`. Но во ВСЁМ
//   бинаре этот буфер только ОБНУЛЯЕТСЯ (`memset(this + 129, 0, 0x80)` в конструкторе окна,
//   game.c:477172) — писателя у него нет. То, что игрок вводит в окне 8300, уходит на сервер
//   ОТДЕЛЬНЫМ пакетом 6080 (`netSecure_sendTextInput@0xE74220`, ветка 8304) и в 5200 не попадает,
//   поэтому получатель предложения видит ПУСТОЕ самопредставление (поле 8406 окна 8400).
//   Ключ ниже: 0 — воспроизвести оригинал (шлём нули), 1 (по умолчанию) — подставить тот же
//   текст, что ушёл в 6080. Размер поля и его смысл при этом не меняются.
static ASDA_DEV_CVAR(int32, CVarSoulmateSendIntro,
	TEXT("asda.Soul.SendIntroInPropose"), 1,
	TEXT("5200: 0 — как в оригинале (поле «о себе» уходит нулями, дефект D15); ")
	TEXT("1 — слать текст, введённый в окне 8300 и отправленный пакетом 6080."));

// ★★★ ЕДИНАЯ ТОЧКА ОТПРАВКИ 5200 (`coupleSendProposalReq@0xE658E0` / `…ReqTarget@0xE655B0`).
//   Обе эталонные ветки — по имени из списка/меню игрока и по имени из окна 9100 — собирают ОДИН payload
//   153 б: [0] режим окна `+1900` (1, а пока открыто 9100 — 2), [1..20] имя цели, [21..22] свой уровень
//   (`g_LocalPlayer+1082`), [23] пол (+1079), [24] класс (+1080), [25..152] текст «о себе» (D15 выше).
//   ★ ВОЛНА 1-А (06.10.2026): защёлка «предложение в полёте» СНЯТА — оба гейта эталона мертвы (см. `.h`, R21):
//   `+1888` никто в 1 не ставит, `+1968` ставится в 1 и тут же обнуляется `editBoxClearAndHide`.
//   [ОРИГИНАЛ] После отправки ОБЕ ветки зовут `editBoxClearAndHide` — окно 9100 скрывается, режим возвращается в 1,
//   поле очищается; поэтому `CloseLinkWindow` живёт здесь, а не у вызывающих (иначе 2002/8266 при открытом 9100
//   уходили бы с режимом 2 и оставляли окно висеть).
bool UAsdaSoulmateSubsystem::SendProposal5200(const FString& TargetName)
{
	if (TargetName.IsEmpty()) { return false; }   // `a2 || strlen(Str) != 0` / `strlen(EditText)`
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return false; }
	// D15: 0 — как в оригинале (пусто), 1 — тот самый текст, что лежит на сервере (приём 6080).
	const FString Msg = (CVarSoulmateSendIntro.GetValueOnGameThread() != 0)
		? (Net->SelfIntroduction.IsEmpty() ? IntroText : Net->SelfIntroduction)
		: FString();
	// Имя цели для нотисов 5201 кодов 4..7. [ОРИГИНАЛ] его кладёт ТОЛЬКО ветка по имени (`&win[3].field_8 + 1` = +1833);
	//   ветка 9100 пишет имя в `+1901` и `+1833` не трогает — тогда 5201 коды 4..7 печатали ЧУЖОЕ (прошлое) имя.
	//   ★ отступление: дефект оригинала НЕ воспроизводим — имя цели запоминаем у обеих веток (решение порта 31.08).
	Card.OutgoingName = TargetName;
	const uint8 ModeSent = LinkWinMode;
	const bool bOk = Net->SendSoulmateCreate5200(TargetName, (int32)ModeSent,
		(int32)Net->SelfLevel, (int32)Net->SelfGender, FMath::Max(Net->SelfClassId, 0), Msg);
	// `editBoxClearAndHide` у эталона безусловен (результат `CNetwork_Send` не проверяется).
	CloseLinkWindow();
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5200 «%s» режим=%u «о себе»=%d б: %s"),
		*TargetName, (uint32)ModeSent, Msg.Len(), bOk ? TEXT("ушёл") : TEXT("ОТКАЗ шлюза/связи"));
	return bOk;
}

// 8266 — предложить Дружбу выбранному кандидату: тот же 5200, что и по имени.
void UAsdaSoulmateSubsystem::ProposeToSelected()
{
	// R24: выбор за пределами списка ⇒ эталон идёт на `LABEL_12` → `partyInvite_CloseList`,
	//   то есть ЗАКРЫВАЕТ список выбора (`game.c:477636-477656`), а не молча выходит.
	if (SelectedRow >= 0 && !Candidates.IsValidIndex(SelectedRow)) { SelectRow(-1); return; }
	if (!Candidates.IsValidIndex(SelectedRow)) { return; }
	SendProposal5200(Candidates[SelectedRow].Name);
}

// 8264 — шепот выбранному кандидату («/w имя » в поле ввода чата).
void UAsdaSoulmateSubsystem::WhisperToSelected()
{
	// R24: та же ветка `LABEL_12` — при выборе за пределами списка он закрывается.
	if (SelectedRow >= 0 && !Candidates.IsValidIndex(SelectedRow)) { SelectRow(-1); return; }
	if (!Candidates.IsValidIndex(SelectedRow)) { return; }
	WhisperTo(Candidates[SelectedRow].Name);
}

// ═══════════════════════════════ ОКНО 9100 (предложение по имени) ══════════════════════════════
// ★★★ ВОЛНА 1-А (06.10.2026): ОКНО 9100 СВЕРЕНО С ЭТАЛОНОМ ЦЕЛИКОМ. Вход — NPC 30023 «Статуя души» (106).
//   [ОРИГИНАЛ] Подписи ставит `CSoulmateWnd_InitControls` при постройке (social.c:6176-6197, 6232-6240):
//   9104 ← loc 2288 «ОК», 9103 ← loc 2289 «Отмена», 9129 ← loc 12018, 9130 ← loc 12019. Прежний порт их не ставил —
//   на экране оставался текст разметки. Кнопки: 9103 → `editBoxClearAndHide`, 9104 → `coupleSendProposalReqTarget`;
//   Enter в поле 9101 (событие 67587, social.c:7899-7902) → тот же `coupleSendProposalReqTarget`.
void UAsdaSoulmateSubsystem::OpenLinkWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Дедуп №2, В1-B (U22): каркас `FAsdaJuiWindowHandle::EnsureLazy` (шаги и порядок прежние; закрыватель общих путей
	//   регистрируется каркасом после привязки — роспись только хранится, поэтому место в теле ничего не меняет).
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kLinkWindowId, kLinkWbp, kWinZ).EnsureLazy(*WM, LinkWnd, /*bReady*/true,
		ESlateVisibility::SelfHitTestInvisible,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulmate, Error, TEXT("[соулмейт] WBP_SOULMATE_LINK_WIN не загрузился")); },
		[this](UAsdaWindowSubsystem& Wm)
		{
			// ОК (9104 SOULMATE_LINK_BTN1) → `coupleSendProposalReqTarget`; Отмена (9103 SOULMATE_LINK_BTN0) → скрыть.
			if (UButton* Ok = Cast<UButton>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN1"))))
			{
				UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
				R->Callback = [this](int32) { SubmitLinkWindow(); };
				Ok->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
				AsdaUi::AnchorRelay(PopupRelays, R, Ok);   // ★★★ GC-якорь (см. `Relays`)
			}
			if (UButton* Cancel = Cast<UButton>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_LINK_BTN0"))))
			{
				UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
				R->Callback = [this](int32) { CloseLinkWindow(); };
				Cancel->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
				AsdaUi::AnchorRelay(PopupRelays, R, Cancel);   // ★★★ GC-якорь (см. `Relays`)
			}
			// Enter в поле 9101 = «ОК». Только `OnEnter`: потеря фокуса и Esc подтверждением НЕ являются
			//   (грабля диалога 1580, см. `UAsdaTextLimiter::CommitCallback`).
			if (UEditableText* E = Cast<UEditableText>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))))
			{
				UAsdaTextLimiter* C = NewObject<UAsdaTextLimiter>(this);
				C->CommitCallback = [this](const FText&, ETextCommit::Type Method)
				{
					if (Method == ETextCommit::OnEnter) { SubmitLinkWindow(); }
				};
				E->OnTextCommitted.AddDynamic(C, &UAsdaTextLimiter::OnTextCommitted);
				AsdaUi::AnchorRelay(PopupRelays, C, E);   // ★★★ GC-якорь (см. `Relays`)
			}
			// Статические подписи `CSoulmateWnd_InitControls` (теги срезаны, как у прочих подписей окна).
			auto SetTxt = [this](const TCHAR* Idc, const FString& S)
			{
				if (UTextBlock* T = Cast<UTextBlock>(LinkWnd->GetWidgetFromName(FName(Idc)))) { T->SetText(FText::FromString(S)); }
				else { UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] окно 9100: текстового узла «%s» нет"), Idc); }
			};
			SetTxt(TEXT("SOULMATE_LINK_BTN1_Text"), Loc(2288));    // 9104 «ОК»
			SetTxt(TEXT("SOULMATE_LINK_BTN0_Text"), Loc(2289));    // 9103 «Отмена»
			SetTxt(TEXT("SOULMATE_LINK_TXT0"),      Loc(12018));   // 9129 «Поздравляем!»
			SetTxt(TEXT("SOULMATE_LINK_TXT1"),      Loc(12019));   // 9130 «Введи имя Друга»
			// [ОРИГИНАЛ] Ревизия 1-А §8: окно показывается НЕ поднятым (`editBoxShowAndFocus` без `CClass_MapInsert`), и системный
			//   чат 2570 может его накрыть; клик по ТЕЛУ окна поднимает его (`uiWindowMgrDispatchMouse` → перестановка в конец
			//   списка). Ручек перетаскивания у 9100 нет, поэтому подъём по телу включаем явно.
			Wm.SetRaiseOnBodyClick(kLinkWindowId, true);
			// ★ 08.10.2026 [ОРИГИНАЛ] (window_default_pos.md §6 п.9): `CSoulmateWnd_InitControls@0xE5E440` (social.c:6646-6657)
			//   ставит окно 9100 по центру экрана и на 105 px выше: Origin = ((W>>1) − w/2, (H>>1) − h/2 − 105) — (382,199) при
			//   1024×768; якоря у окна в .JUI нет, позиция абсолютна. Порт стоял в Origin разметки (363,203) на любом экране.
			//   Откат: `asda.UI.JuiDefaultPos 0`.
			if (UAsdaWindowSubsystem::UseOriginalDefaultPos())
			{
				Wm.PlaceWindowKeepingSavedOffset(kLinkWindowId, FName(TEXT("SOULMATE_LINK_WIN")), nullptr, [](const FVector2D& V, const FVector2D& F)
				{
					return FVector2D((FMath::TruncToInt32(V.X) >> 1) - FMath::TruncToInt32(F.X) / 2,
						(FMath::TruncToInt32(V.Y) >> 1) - FMath::TruncToInt32(F.Y) / 2 - 105);
				});
			}
		},
		FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseLinkWindow(); }));
	if (Ens == EAsdaJuiEnsure::LoadFailed) { return; }
	// [ОРИГИНАЛ] `editBoxShowAndFocus@0xE69B40`: гейт `!*(this+492)` (`+1968`) — у эталона всегда пройден (см. `.h`, R21);
	//   режим `+1900 = 2`; блок входящего предложения `+1608` сбрасывается `sub_E5DC20`; поле 9101 — пустая строка и
	//   фокус; фокус ввода чата снимается; окно показывается.
	//   ★ отступление: сброс блока `+1608` НЕ воспроизводим. Это данные ВХОДЯЩЕГО предложения (5201 код 0), по которым
	//   значок 5055 открывает окно 8400: после сброса «Принять» уходил бы с charId −1 (сервер такой 5202 отбрасывает), а
	//   «Отказать» не уходил бы вовсе (эхо [31] = 0 ≠ режиму). Чужое предложение молча становилось бы неотвечаемым.
	UEditableText* NameEdit = Cast<UEditableText>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT")));
	if (NameEdit) { NameEdit->SetText(FText::GetEmpty()); }
	WM->SetWindowVisible(kLinkWindowId, true);
	// `CControl_SetFocus(v3[205], 1)` — фокус ставим ПОСЛЕ показа (скрытому виджету Slate фокус не отдаёт);
	//   фокус ввода чата при этом уходит сам (`uiPanel_SetFocusChildren(g_pChatWnd, 0)`).
	if (NameEdit) { NameEdit->SetKeyboardFocus(); }
	// ★★★ РЕЖИМ ОКНА = 2, пока 9100 открыто (`editBoxShowAndFocus@0xE69B40`: `*(this+1900) = 2`).
	//   Он уходит в payload[0] пакета 5200 и возвращается в 5201 байтом [31]; кнопка 8409
	//   сверяет их (`Soulmate_SendBondAccept@0xE66790`).
	LinkWinMode = 2;
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно предложения по имени 9100 открыто (режим=2)"));
}

// Порт `editBoxClearAndHide@0xE69C20`: режим 1, `+1968 = 0`, поле 9101 — пустая строка без фокуса, окно скрыто.
//   ★ отступление (то же, что в `OpenLinkWindow`): сброс блока входящего предложения `+1608` не воспроизводим.
void UAsdaSoulmateSubsystem::CloseLinkWindow()
{
	if (LinkWnd)
	{
		if (UEditableText* E = Cast<UEditableText>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))))
		{
			E->SetText(FText::GetEmpty());
		}
	}
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kLinkWindowId, false);
	}
	LinkWinMode = 1;   // `editBoxClearAndHide@0xE69C20`: `*(this+1900) = 1`
}

// Порт `coupleSendProposalReqTarget@0xE655B0` (9104 «ОК» и Enter в 9101).
//   [ОРИГИНАЛ] Гейты: `+1888`/`+1968` (мертвы, R21) → `soulmateUI_IsTargetInRange` — при любом отказе МОЛЧА, окно остаётся.
//   Имя берётся из поля КАК ЕСТЬ (`strcpy(self+1901, GetEditText(...))`, без обрезки пробелов).
bool UAsdaSoulmateSubsystem::SubmitLinkWindow()
{
	if (LinkWinMode == 0) { return false; }   // `if (!*(self + 1900)) return 0;` (режим 1/2 — всегда пройден)
	FString Name;
	if (LinkWnd)
	{
		if (UEditableText* E = Cast<UEditableText>(LinkWnd->GetWidgetFromName(TEXT("SOULMATE_NAME_INPUT"))))
		{
			Name = E->GetText().ToString();
		}
	}
	if (Name.IsEmpty() || !IsLinkTargetInRange(Name))
	{
		UE_LOG(LogAsdaSoulmate, Log,
			TEXT("[соулмейт] 9104: «%s» — цель не рядом/не найдена/это я (гейт `soulmateUI_IsTargetInRange`, молча)"), *Name);
		return false;
	}
	return SendProposal5200(Name);   // внутри — `editBoxClearAndHide` (окно скрывается, режим 1)
}

// Порт `soulmateUI_IsTargetInRange@0xE69CF0` (AsdaStory_game.c:474196-474241), дословно:
//   цель = `CharManager_FindByName(имя)` (strcmp — РЕГИСТР ВАЖЕН; CharManager хранит и своего персонажа);
//   отказ, если её userIdx == мой или её имя == моё; тайлы обеих сторон — `helper_scaleIfNonzero(X)` и
//   `ratioIfFieldSet(Z)` (мир → тайл, `AsdaCoord::GameToTile`); пройдено при `| |мойX| − |еёX| | ≤ 10` и так же по Z.
bool UAsdaSoulmateSubsystem::IsLinkTargetInRange(const FString& Name) const
{
	UGameInstance* GI = GetGameInstance();
	const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!WS || !Net || Name.IsEmpty()) { return false; }
	// Своё имя: `FindByName` нашёл бы своего персонажа, и эталон отказал бы по совпадению userIdx/имени.
	if (Name.Equals(WS->GetSelfCharName(), ESearchCase::CaseSensitive)) { return false; }
	// ★ Ревизия 1-А МЕЛК-2: поиск — ТОЧНОЕ сравнение по всему реестру соседей, как `CharManager_FindByName` (strcmp).
	//   Прежний `FindPlayerActorByName` сравнивал без учёта регистра и при соседях «Abc» и «abc» мог вернуть не того.
	AActor* Target = nullptr;
	int32 TargetIdx = -1;
	WS->ForEachPlayerActorWithCharId([&](int32 /*CharId*/, AActor* A, bool bSelf)
	{
		if (Target || bSelf || !::IsValid(A)) { return; }
		const int32 Idx = WS->FindUserIdxByActor(A);
		FString N; int32 Cls = 0, Lv = 0, Clan = -1;
		if (Idx >= 0 && WS->GetNetPlayerInfo(Idx, N, Cls, Lv, Clan) && N.Equals(Name, ESearchCase::CaseSensitive))
		{
			Target = A; TargetIdx = Idx;
		}
	});
	const AActor* Self = WS->LocalPlayerActor.Get();
	if (!::IsValid(Target) || !::IsValid(Self)) { return false; }
	if (AsdaSelf::IsSelfUserIdx(Net, TargetIdx)) { return false; }   // ★ U5b (В2-A): общая точка «это я?» (при ключе 1 — то же условие)
	const FIntPoint T = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Target->GetActorLocation()));
	const FIntPoint M = AsdaCoord::GameToTile(AsdaCoord::UeToGame2D(Self->GetActorLocation()));
	const int32 DiffX = FMath::Abs(M.X) - FMath::Abs(T.X);   // `abs(localDx) − abs(targetDx)`
	const int32 DiffZ = FMath::Abs(M.Y) - FMath::Abs(T.Y);
	return FMath::Abs(DiffX) <= 10 && FMath::Abs(DiffZ) <= 10;
}

// ═══════════════════════════════ ОКНО 8400 (ответ на предложение) ═════════════════════════════
void UAsdaSoulmateSubsystem::ShowProposalWindow()
{
	if (!Proposal.bPending) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// ★ Дедуп №2, В1-B (U22): каркас `FAsdaJuiWindowHandle::EnsureLazy` (шаги и порядок прежние; закрыватель общих путей
	//   регистрируется каркасом после привязки — роспись только хранится, поэтому место в теле ничего не меняет).
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kReqAnsrWindowId, kReqAnsrWbp, kWinZ).EnsureLazy(*WM, ReqWnd, /*bReady*/true,
		ESlateVisibility::SelfHitTestInvisible,
		[]() {},
		[]() { UE_LOG(LogAsdaSoulmate, Error, TEXT("[соулмейт] WBP_SMate_ReqAnsr_Win не загрузился")); },
		[this](UAsdaWindowSubsystem&)
		{
			// 8408 Принять → 5202 [0]=1 ; 8409 Отказать → 5202 [0]=0 ; 8407 Шепот → «/w имя » в чат.
			auto BindReq = [this](const TCHAR* Idc, TFunction<void()> Fn)
			{
				if (UButton* B = Cast<UButton>(ReqWnd->GetWidgetFromName(FName(Idc))))
				{
					UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
					R->Callback = [Fn](int32) { Fn(); };
					B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
					AsdaUi::AnchorRelay(PopupRelays, R, B);   // ★★★ GC-якорь (см. `Relays`)
				}
			};
			BindReq(TEXT("SMate_ReqAnsr_OKBtn"), [this]()   // 8408 «Принять»
			{
				if (UGameInstance* G = GetGameInstance())
					if (UAsdaNetSubsystem* Net = G->GetSubsystem<UAsdaNetSubsystem>())
						Net->SendSoulmateResponse5202(/*bAccept=*/true, Proposal.InitCharId, Proposal.InitUserNum,
							Proposal.B7, Proposal.B8, Proposal.B9, Proposal.Channel, Proposal.InitName, Proposal.SkillType);
				Proposal.bPending = false;
				// ⚠ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D11 (помечено): эталонные ветки 8408/8409
				//   (`SoulmateUI_HandleButtonCommand`, game.c:474299-474320) прячут только окно
				//   (`SetVisible(win+226, 0)`) и значок с панели быстрых слотов НЕ снимают — он
				//   висит до перезахода. Снимаем; вернуть оригинал — убрать этот вызов.
				ClearProposalIcon();
				CloseProposalWindow();
			});
			BindReq(TEXT("SMate_ReqAnsr_CanBtn"), [this]()  // 8409 «Отказать»
			{
				// ★★★ ГЕЙТ ЭТАЛОНА ВОССТАНОВЛЕН (аудит §7 п.5 закрыт).
				//   `Soulmate_SendBondAccept@0xE66790` шлёт 5202 ТОЛЬКО при `*(this+1900) == *(this+1639)`.
				//   Оба поля теперь известны: `+1900` — режим окна предложения (1 обычный, 2 — открыто
				//   9100; ставится в `editBoxShowAndFocus`/`editBoxClearAndHide`), `+1639` — байт [31]
				//   тела 5201 (блок 164 б лежит с +1608, 1608 + 31 = 1639). Сервер зовёт это поле
				//   `iSoulMateSkillType` (`RequestUser_SoulMateCreate@0x4924F0`: лог
				//   «if( sClientReq.iSoulMateSkillType <= 0 )», после чего принудительно ставит 1) —
				//   то есть это ЭХО режима отправителя, а не «тип навыка».
				//   У кнопки 8408 («Принять», `Soulmate_SendBondPropose@0xE669F0`) такого гейта НЕТ.
				if (LinkWinMode != Proposal.SkillType)
				{
					UE_LOG(LogAsdaSoulmate, Log,
						TEXT("[соулмейт] 8409: гейт эталона не пройден (режим окна %u != эхо 5201[31] %u) — 5202 не шлём"),
						(uint32)LinkWinMode, (uint32)Proposal.SkillType);
					CloseProposalWindow();
					return;
				}
				if (UGameInstance* G = GetGameInstance())
					if (UAsdaNetSubsystem* Net = G->GetSubsystem<UAsdaNetSubsystem>())
						Net->SendSoulmateResponse5202(/*bAccept=*/false, Proposal.InitCharId, Proposal.InitUserNum,
							Proposal.B7, Proposal.B8, Proposal.B9, 0, Proposal.InitName, Proposal.SkillType);
				Proposal.bPending = false;
				ClearProposalIcon();
				CloseProposalWindow();
			});
			BindReq(TEXT("SMate_ReqAnsr_WhisBtn"), [this]() { WhisperTo(Proposal.InitName); });  // 8407 «Шепот»
		},
		FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseProposalWindow(); }));
	if (Ens == EAsdaJuiEnsure::LoadFailed) { return; }
	// Заполнить: имя подтверждено (body+11). Уровень/пол/профессия/самопредставление ждут точной
	//   раскладки 164-байтного тела 5201 (не выдумываем) — оставляем прочерк.
	auto SetTxt = [this](const TCHAR* Idc, const FString& S)
	{
		if (UTextBlock* T = Cast<UTextBlock>(ReqWnd->GetWidgetFromName(FName(Idc)))) { T->SetText(FText::FromString(S)); }
	};
	// Порт `Soulmate_ShowPartnerInfo@0xE66570` — состав и источники строк 1:1:
	//   8401 ← loc 20270 (слот-строка = имя) · 8402 ← loc 20271 (слот-строка = имя)
	//   8403 ← loc 20272 (слот-ЧИСЛО = уровень) · 8404 ← loc (20272 + пол) · 8405 ← loc (12300 + класс)
	//   8406 ← самопредставление как есть.
	SetTxt(TEXT("SMate_ReqAnsr_Title"), LocSlotStr(20270, Proposal.InitName));
	SetTxt(TEXT("SMate_ReqAnsr_Name"),  LocSlotStr(20271, Proposal.InitName));
	SetTxt(TEXT("SMate_ReqAnsr_Level"), LocSlotNum(20272, Proposal.Level));
	SetTxt(TEXT("SMate_ReqAnsr_Sex"),   Loc(20272 + (int32)Proposal.Sex));
	SetTxt(TEXT("SMate_ReqAnsr_Job"),   Loc(12300 + (int32)Proposal.Job));
	SetTxt(TEXT("SMate_ReqAnsr_Intro"), Proposal.Intro);
	WM->SetWindowVisible(kReqAnsrWindowId, true);
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] окно предложения 8400 от «%s» показано"), *Proposal.InitName);
}

// Клик по значку уведомления (эталон `uiNoticeButtonDispatch` case 1 → `Soulmate_ShowPartnerInfo`).
void UAsdaSoulmateSubsystem::OnNoticeIcon(int32 NoticeType)
{
	// ★ 26.10.2026 (smmessage) [ОРИГИНАЛ]: case 9 → `itemQueuePopAndShowTooltip(g_pSoulmateWnd)` — открыть записку Друга.
	//   Значок снимает хотбар (один на клик), наш счётчик выставленных значков уменьшаем здесь же.
	if (NoticeType == kNoticeTypeNote)
	{
		NoteIconsShown = FMath::Max(0, NoteIconsShown - 1);
		bNoteIconsDirty = true;
		OpenNextNote();
		return;
	}
	if (NoticeType != kNoticeTypeSoulmate) { return; }
	ShowProposalWindow();
}

void UAsdaSoulmateSubsystem::CloseProposalWindow()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kReqAnsrWindowId, false);
	}
}

// ═══════════════════════════════ ПРИЁМ СЕТИ ═══════════════════════════════════════════════════
void UAsdaSoulmateSubsystem::OnCard(int32 Opcode, const TArray<uint8>& Body)
{
	if (Body.Num() <= 0) { return; }
	// ★ 23.10.2026 (панель Друга 6500): тем же каналом приходят своя запись 5220 и эффекты Друга 5112.
	if (Opcode == 5220) { OnSelfSoulInfo5220(Body); return; }
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] «эхо себя»: эффекты (5112) и место (5227) Друга — только когда запись хранит настоящего Друга.
	if (Opcode == 5112 || Opcode == 5227 || Opcode == (int32)OP_SOULMATE_WND_APPLY_INFO_PACKET)
	{
		if (!IsPartnerFeedTrusted())
		{
			++SelfEchoDrops;
			UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←%d отброшен: %s"), Opcode,
				bSelfEchoSeen ? TEXT("сервер шлёт эхо моих данных (Друг на деле вне игры)") : TEXT("записи настоящего Друга (5110) ещё нет"));
			return;
		}
	}
	if (Opcode == 5112) { OnPartnerBuffs5112(Body); return; }
	// ★ 23.10.2026 (sm-info): 5227 — Друг сменил карту/канал (тот же канал делегата).
	if (Opcode == 5227) { OnPartnerMoved5227(Body); return; }
	// ★ 24.10.2026 (sm-offline): 2243 — прежние Друзья (окно 6640) и строки P79 активной пары (сведения о Друге вне игры).
	if (Opcode == (int32)OP_UI_REFRESH_SLOT_GRID3X3) { OnOldSoulGrid2243(Body); return; }
	// ★ 26.10.2026 (smmessage): записки Другу — 6053 доставка/итог, 6055 непрочитанные (тот же канал делегата).
	if (Opcode == (int32)OP_GUILD_HANDLE_EVENT_NOTIFY) { OnNote6053(Body); return; }
	if (Opcode == (int32)OP_PARTY_MEMBER_SYNC_ICONS)   { OnNotes6055(Body); return; }

	if (Opcode == (int32)OP_SOULMATE_UPDATE_INFO)
	{
		// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] «эхо себя»: 5110 со СВОИМ charId в rec[4] — сервер вернул мне мою же «Друг вошёл».
		if (IsSelfEcho5110(Body))
		{
			++SelfEchoDrops;
			UE_LOG(LogAsdaSoulmate, Warning, TEXT("[соулмейт] ←5110 ЭХО: charId «Друга» %d = мой — запись не применяю, Друг вне игры"),
				AsdaBytes::RdI32(&Body[4]));
			EnterSelfEchoOffline(TEXT("5110 — эхо своих данных"));
			return;
		}
		if (bSelfEchoSeen)
		{
			// Пришла запись НАСТОЯЩЕГО Друга (он вошёл — сервер переписал номер Друга) — эхо кончилось.
			bSelfEchoSeen = false;
			UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←5110 настоящего Друга (charId %d) — признак эха снят"),
				Body.Num() >= 8 ? AsdaBytes::RdI32(&Body[4]) : -1);
		}
		// 5110 `soulmateUpdateInfo`: memcpy(this+1357, Src, 0xFB) — запись ЦЕЛИКОМ, rec[n] = body[n].
		const int32 N = FMath::Min(Body.Num(), FAsdaSoulmateRecord::kSize);
		FMemory::Memcpy(Rec.B, Body.GetData(), N);
	}
	else
	{
		// 5111 `soulmateWnd_ApplyInfoPacket`: КАЖДОЕ присваивание идёт в gap_54D[13+n] ← body[n].
		//   ⚠ Копируется НЕ весь блок, а ровно перечисленные поля — rec[38],[39],[41] эталон
		//     не трогает (они остаются от последнего 5110). Повторяем список 1:1.
		auto Cp = [this, &Body](int32 RecOfs, int32 BodyOfs, int32 Len)
		{
			if (BodyOfs + Len <= Body.Num() && RecOfs + Len <= FAsdaSoulmateRecord::kSize)
			{
				FMemory::Memcpy(&Rec.B[RecOfs], Body.GetData() + BodyOfs, Len);
			}
		};
		// ★★★ R18 ЗАКРЫТ: признак «уровень Дружбы сменился». Эталон (game.c:472536-472537)
		//   сравнивает СТАРЫЙ rec[50] с новым body[37] ДО присваивания и при расхождении ставит
		//   `*((DWORD*)this + 493) = 1`. Читает его `uiProgressBar_RefreshPercent`
		//   (game.c:471330-471337): обнуляет долю опыта РОВНО НА ОДИН показ и снимает признак —
		//   иначе при смене уровня полоса на кадр показывала долю от старого порога.
		if (Body.IsValidIndex(37) && Rec.B[50] != Body[37]) { bFriendLevelDirty = true; }
		Cp(13, 0, 4);    Cp(17, 4, 4);    Cp(21, 8, 2);    Cp(23, 10, 2);
		Cp(25, 12, 2);   Cp(27, 14, 2);   Cp(29, 16, 1);   Cp(30, 17, 4);
		Cp(34, 21, 4);   Cp(40, 27, 1);   Cp(42, 29, 4);   Cp(46, 33, 4);
		Cp(50, 37, 1);   Cp(51, 38, 2);   Cp(53, 40, 2);   Cp(55, 42, 2);
		Cp(57, 44, 2);   Cp(59, 46, 2);   Cp(61, 48, 2);
		// ★★★ СНЯТО (аудит 31.08): порт копировал сюда ещё и ИМЯ (rec[63] ← body[50…]).
		//   В `soulmateWnd_ApplyInfoPacket@0xE65060` (game.c:472502-472523) присваиваний
		//   РОВНО ДЕВЯТНАДЦАТЬ — они перечислены выше, и НИ ОДНОГО в gap_54D[63] среди них нет.
		//   Имя партнёра в 5111 не приходит: оно остаётся от последнего 5110. Копировать хвост
		//   тела в поле имени — выдумка, которая при коротком 5111 писала бы мусор.
	}

	// Признак «пара есть» эталон ставит безусловно (gap_8B4[6] = 1) в обоих обработчиках.
	Card.bHasSoulmate  = true;
	// ★ 24.10.2026 (sm-offline): Друг в игре — сведения «вне игры» устарели (после его выхода спросим заново), отложенный запрос не нужен.
	bOffStale = true;
	bOffLeaveCheck = false;   // ★ 24.10.2026: Друг снова в игре — проверка свежести ответа после прошлого 5226 не нужна
	CancelOfflineReqTimer();
	// С этого момента карточку можно рисовать и при показе вкладки: данные пришли с сервера.
	bCardReceived      = true;
	Card.SlotIndex     = Rec.Slot();
	Card.PartnerName   = RecName();
	// ★ 23.10.2026: `gap_8B4[0..3]` ← rec[4] (charId Друга), `gap_8B4[4]` ← rec[8] (userIdx) — оба обработчика (social.c:7311-7312, 7409-7410).
	MateCharIdLive  = Rec.PartnerCharId();
	MateUserIdxLive = Rec.PartnerUserNum();
	// ★ 23.10.2026 (sm-info): свой блок из записи — оба обработчика (social.c:7307-7318 / 7412-7420): +4..11 опыт Дружбы ← rec[42..49],
	//   +12 уровень Дружбы ← rec[50], +22 карта Друга ← rec[38] (миснейм iSoulMateSkillType), +23 канал Друга ← rec[10].
	FriendExpBlk      = Rec.FriendExp();
	FriendLevelBlk    = Rec.B[50];
	PartnerMapBlk     = Rec.B[38];
	PartnerChannelBlk = Rec.B[10];
	bBlkLevelKnown    = true;

	// ★★★ T8 ЗАКРЫТ — СТРОКА ЧАТА ПОСЛЕ КАЖДОГО 5110 (`soulmateUpdateInfo@0xE64C30`,
	//   game.c:472487-472490): `sprintf(Buffer, loc(110103), -28418, win + 1420)` и затем
	//   `CChat_AddTokenizedLines(chat, Buffer, -28418, 0, 1)` — то есть loc-строка САМА является
	//   printf-форматом с ДВУМЯ аргументами. Формат разобран по таблице порта
	//   (`RawAssets/Text/GameText.csv`, ключ 110103): «<FC=%08x>Появляется Друг [%s].</F>» —
	//   первый аргумент это цвет в разметке, второй — имя партнёра. Ветка идёт внутри
	//   `if (gap_8B4[6])`, а этот признак строкой выше поставлен в 1 ⇒ для 5110 она безусловна.
	//   −28418 = 0xFFFF90BE (255,144,190). Раньше строка терялась целиком.
	if (Opcode == (int32)OP_SOULMATE_UPDATE_INFO)
	{
		// ⚠ ТОЛЬКО `LocRaw`: `GetTextById` СРЕЗАЕТ inline-теги `<FC=…>`, а здесь тег и есть
		//   первый аргумент формата (грабля `ministate-win-port`).
		FString Fmt = AsdaUi::LocRaw(GetGameInstance(), 110103, /*bDebugFallback=*/false);
		if (!Fmt.IsEmpty())
		{
			// `%08x` от int −28418 у C-`sprintf` даёт «ffff90be»; `AsdaUi::FormatPrintfArgs`
			//   знает только `%s`/`%d`, поэтому цвет подставляем сами, а имя — общей точкой.
			// ★ 23.10.2026 [ИСПРАВЛЕНИЕ порта]: −28418 = 0xFFFF90FE (было «ffff90be»/0xBE — опечатка; ср. 5226).
			Fmt.ReplaceInline(TEXT("%08x"), TEXT("ffff90fe"), ESearchCase::IgnoreCase);
			const FString Line = AsdaUi::FormatPrintfArgs(Fmt, { Card.PartnerName });
			if (UAsdaMessageSubsystem* Msg =
				GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				FAsdaMsgParams P;
				P.Category  = EAsdaMsgCategory::Social;
				P.Text      = Line;
				P.bUseColor = true;
				P.Color     = FColor(0xFF, 0x90, 0xFE, 0xFF);   // −28418 = 0xFFFF90FE
				P.TtlMs     = 0.f;                              // эталон передаёт TTL 0
				P.SrcOpcode = 5110;
				Msg->Post(P);
			}
		}
	}
	RefreshCard();
	// ★ 23.10.2026 [ОРИГИНАЛ]: 5110 копирует и 28 эффектов Друга (rec+83) и зовёт `buffUiRefreshIcons` (social.c:7375).
	if (Opcode == (int32)OP_SOULMATE_UPDATE_INFO) { RefreshPopBuffs(); }
	// Состав лотка навыков Дружбы зависит от уровня Дружбы — обновляем после каждой записи.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaSoulmateSkillSubsystem* Sk = GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>())
		{
			Sk->RefreshTray();
		}
	}
	// ★ 23.10.2026 (sm-info): отметка Друга на картах — тайл/карта из записи.
	PushCoupleMark(false);
	UE_LOG(LogAsdaSoulmate, Log,
		TEXT("[соулмейт] запись (%d): слот=%d партнёр='%s' ур=%u карта=%u канал=%u тайл=(%d,%d) др.ур=%u"),
		Opcode, Card.SlotIndex, *Card.PartnerName, (uint32)Rec.Level(),
		(uint32)Rec.MapId(), (uint32)Rec.Channel(), Rec.TileX(), Rec.TileZ(), (uint32)Rec.FriendLevel());
}

void UAsdaSoulmateSubsystem::OnPlayerMenuResult(int32 Opcode, const TArray<uint8>& Body)
{
	if (Opcode != 5201) { return; }
	if (Body.Num() < 1) { return; }
	const int32 Code = Body[0];
	if (Code == 0)
	{
		// ★★★ 1:1 с `coupleRecvProposalNotice` case 0: сохранить блок 164 б и повесить ЗНАЧОК на
		//   панель быстрых слотов. Окно 8400 эталон здесь НЕ открывает — оно поднимается по клику
		//   значка (`uiNoticeButtonDispatch` case 1 → `Soulmate_ShowPartnerInfo`).
		ParseProposal5201(Body);
		NotifyProposalIcon();
	}
	// ★★★ ИСПРАВЛЕНО (аудит 31.08). Прежний комментарий «коды 1..7 обрабатывает
	//   UAsdaPlayerMenuSubsystem» — НЕВЕРЕН: там (`AsdaPlayerMenuSubsystem.cpp:1180-1184`)
	//   ветка 5201 только снимает свою защёлку и пишет в журнал «окна соулмейта не портированы».
	//   Значит СЕМЬ строк эталона просто пропадали. Порт `coupleRecvProposalNotice@0xE65BB0`
	//   (game.c:472761-472807), дословно:
	//     1 → 20163 · 2 → 20164 · 3 → 20165  — имя из ТЕЛА (+11);
	//     4 → 20166 · 5 → 20163 · 6 → 20164 · 7 → 20165 — имя ЦЕЛИ МОЕГО предложения
	//       (эталон держит его в `&win[3].field_8 + 1`; у нас это `Card.OutgoingName`).
	//   Канал у всех семи — ЧАТ, цвет −8847556, TTL 10000.
	else if (Code >= 1 && Code <= 7)
	{
		static const int32 kLoc[8] = { 0, 20163, 20164, 20165, 20166, 20163, 20164, 20165 };
		const bool bFromBody = (Code <= 3);
		FString Who;
		if (bFromBody && Body.Num() >= 12)
		{
			Who = FAsdaLoginData::DecodeCp1251(Body.GetData() + 11, FMath::Min(20, Body.Num() - 11));
		}
		else if (!bFromBody)
		{
			Who = Card.OutgoingName;
		}
		ChatSocial(LocSlotStr(kLoc[Code], Who));
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5201 код %d → loc %d, имя '%s' (%s)"),
			Code, kLoc[Code], *Who, bFromBody ? TEXT("из тела +11") : TEXT("цель моего предложения"));
	}
	// ★★★ Хвост `coupleRecvProposalNotice@0xE65BB0` (game.c:472805-472807) — БЕЗУСЛОВНЫЙ, общий
	//   для всех восьми веток: `*(DWORD*)&pCoupleWnd[3].gap_C[52] = 0`. Смещение 52 при известном
	//   `gap_C[64]` = +1900 даёт +1888 = `this+472` — первый гейт отправки 5200 (R21).
	//   ★ Волна 1-А: `+1888` в `CSoulmateWnd` в 1 не ставит НИКТО (оба отправителя и этот хвост пишут 0), то есть
	//   гейт у эталона всегда открыт — порт защёлку не держит (см. `.h`), и сбрасывать здесь нечего.
}

// Порт `statusEffectAddIconAndNotify@0xDF33B0` (вызов с кодом 5055): значок типа 1 на панели
//   быстрых слотов + звук 215 + строка 5055 в чат. Гейт: значок типа 1 уже висит ⇒ выходим,
//   ничего не делая (иначе повторные предложения задублируют звук и строку).
void UAsdaSoulmateSubsystem::NotifyProposalIcon()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (Hb && Hb->HasNoticeIcon(kNoticeTypeSoulmate))
	{
		UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] значок предложения уже висит — повтор подавлен (эталон)"));
		return;
	}
	if (Hb) { Hb->AddNoticeIcon(kNoticeTypeSoulmate); }
	if (UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
	{
		Snd->PlaySound2DById(215);
	}
	ChatSocial(Loc(5055));   // «Тебе предлагают стать Другом.»
}

// Ответ дан (принял/отказал) — значок больше не нужен.
void UAsdaSoulmateSubsystem::ClearProposalIcon()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr)
	{
		Hb->RemoveNoticeIcon(kNoticeTypeSoulmate);
	}
}

void UAsdaSoulmateSubsystem::ParseProposal5201(const TArray<uint8>& Body)
{
	// Тело 5201 (код 0) делит раскладку с payload 5202: [1..4] i32 charId, [5..6] i16 userNum,
	//   [7][8][9], [10] канал, [11..30] имя, [31] тип навыка. Имя@+11 подтверждено отчётом §5.4a.
	FAsdaSoulmateProposal P;
	P.bPending = true;
	if (Body.Num() >= 5)  { P.InitCharId  = AsdaBytes::RdI32(Body.GetData() + 1); }
	if (Body.Num() >= 7)  { P.InitUserNum = AsdaBytes::RdI16(Body.GetData() + 5); }
	if (Body.Num() >= 10) { P.B7 = Body[7]; P.B8 = Body[8]; P.B9 = Body[9]; }
	if (Body.Num() >= 11) { P.Channel = Body[10]; }
	if (Body.Num() >= 31)
	{
		P.InitName = FAsdaLoginData::DecodeCp1251(Body.GetData() + 11, FMath::Min(20, Body.Num() - 11));
	}
	if (Body.Num() >= 32) { P.SkillType = Body[31]; }
	// ★★★ Хвост доказан `Soulmate_ShowPartnerInfo@0xE66570`: блок 5201 лежит в объекте с +1608,
	//   поэтому объектные смещения 1640/1642/1643/1644 = байты тела 32/34/35/36.
	if (Body.Num() >= 34) { P.Level = AsdaBytes::RdI16(Body.GetData() + 32); }   // i16 уровень
	if (Body.Num() >= 35) { P.Sex   = Body[34]; }                                // пол    → loc 20272+пол
	if (Body.Num() >= 36) { P.Job   = Body[35]; }                                // класс  → loc 12300+класс
	if (Body.Num() >  36)                                                        // самопредставление 128 б
	{
		P.Intro = FAsdaLoginData::DecodeCp1251(Body.GetData() + 36, FMath::Min(128, Body.Num() - 36));
	}
	Proposal = P;
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] предложение от «%s» charId=%d userNum=%d канал=%u"),
		*P.InitName, P.InitCharId, P.InitUserNum, (uint32)P.Channel);
}

// ★★★ ПОЛНЫЙ ПОРТ `Recv_SoulmateBond@0xE65FE0` (game.c:472811-472919). Веток ЧЕТЫРЕ (0..3),
//   а не две: раньше всё, что не 1, печаталось строкой 8008 — это было неверно для 0/2/3.
//   Раскладка тела: [0] код · [1..4] i32 индекс пары · [5..8] i32 userIdx ИНИЦИАТОРА ·
//   [9..] имя. Ветки 0 и 1 выводят текст НОТИСОМ-ОКНОМ (`CItem_BuildTooltipText(…, 0, −1, …, 1)`),
//   ветки 2 и 3 — строкой ЧАТА цветом −8847556.
void UAsdaSoulmateSubsystem::OnBond(int32 Subcode, int32 PairIdx, int32 InitiatorUidx, FString PartnerName)
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bIAmInitiator = Net && AsdaSelf::IsSelfUserIdx(Net, InitiatorUidx);

	switch (Subcode)
	{
	case 0:
		// Отказ: инициатор — я ⇒ loc 8008, иначе loc 8009. Слот `[ss1]` эталон здесь НЕ ставит.
		//   ★ T6: канал — НОТИС-ОКНО типа 0 (`CItem_BuildTooltipText(…, 0, −1, …, 1)`),
		//   теперь он у порта есть (`ShowNotice0` → `ONEBUT_MSGBOX` 1500). Строкой чата больше не идёт.
		ShowNotice0(Loc(bIAmInitiator ? 8008 : 8009));
		break;

	case 1:
		// Связь создана. Эталон: сохранить индекс пары (+1) и имя (+9), затем нотис-окно
		//   loc 20167 (инициатор — я) либо 20168, с именем в слоте `[ss1]`; следом эффект
		//   `eff_1_char_99_soulmatec-a_01.jeb` и звук `S_c_SMMate01.wav` — И НА ПАРТНЁРЕ, И НА СЕБЕ.
		Card.bHasSoulmate = true;
		Card.PartnerName  = PartnerName;
		// ⚠ ОТСТУПЛЕНИЕ, ПОМЕЧЕНО. Эталон в этой ветке СВОЙ слот (`gap_878[47]`) не трогает
		//   вовсе: он пишет индекс пары только партнёру (`*(int*)(pMateChar + 2215)`), а свой
		//   приходит следом пакетом 5110. Держим страховку на случай задержки 5110 — иначе до
		//   его прихода кнопка 6587 будет отказывать по гейту «слот <= 0».
		if (Card.SlotIndex <= 0) { Card.SlotIndex = PairIdx; }
		Proposal.bPending = false;
		CloseProposalWindow();
		RefreshCard();
		// ★ T6: тот же нотис-окно типа 0, что и в ветке 0.
		ShowNotice0(LocSlotStr(bIAmInitiator ? 20167 : 20168, PartnerName));
		// ⚠ Церемония (эффект + 3D-звук на обоих) — чужая подсистема эффектов/звука,
		//   см. «ТРЕБУЕТСЯ ОТ ОРКЕСТРАТОРА»: своей точки спавна эффекта здесь заводить нельзя.
		break;

	case 2:
		// Эталон печатает строку ТОЛЬКО когда инициатор — Я, и подставляет в слот СВОЁ имя.
	{
		// Своё имя — общая точка `UAsdaWorldSubsystem::GetSelfCharName()` (эталон берёт
		//   `CClass_GetField142C(&g_InventoryBag)->name`); второй копии имени заводить нельзя.
		const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (bIAmInitiator) { ChatSocial(LocSlotStr(20164, WS ? WS->GetSelfCharName() : FString())); }
	}
		break;

	case 3:
		// Зеркало ветки 2: строка идёт, только если инициатор НЕ я; имя берётся из тела (+9).
		if (!bIAmInitiator) { ChatSocial(LocSlotStr(20164, PartnerName)); }
		break;

	default:
		break;
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5203 код=%d пара=%d инициатор=%d(%s) имя='%s'"),
		Subcode, PairIdx, InitiatorUidx, bIAmInitiator ? TEXT("я") : TEXT("не я"), *PartnerName);
}

void UAsdaSoulmateSubsystem::OnBreak(int32 Subcode, FString Name)
{
	// §5.4: 0 отказ разрывать (20221), 1 разрыв (20222), 2 партнёр вне игры (20223), 3 принудительно (8033).
	switch (Subcode)
	{
	// Имя партнёра идёт в слот `[ss1]` (эталон подставляет его во все четыре строки).
	case 0: ChatSocial(LocSlotStr(20221, Name)); break;
	// ★ Ревизия 1-Б С-1: во всех трёх ветках разрыва эталон после `gap_8B4[6] = 0` зовёт `inventoryHidePanelsAndRefresh`
	//   (game.c:473334/473343/473364) — карточка 6500 гаснет, панель яблока сбрасывается (пары больше нет).
	case 1: ChatSocial(LocSlotStr(20222, Name)); ClearBond(); HidePanelsAndRefresh(TEXT("5207 код 1 — разрыв")); break;
	case 2:
		// «Партнёр вне игры»: связь НЕ рвётся, эталон лишь гасит признак и обновляет панели.
		ChatSocial(LocSlotStr(20223, Name));
		Card.bHasSoulmate = false;
		// ★ 23.10.2026 (sm-info) [ОРИГИНАЛ]: ветка 2 зовёт `itemTooltip_RefreshControls` («Вне игры»), не RefreshInfo.
		if (CVarSoulmateOfflineInfo.GetValueOnGameThread() == 0) { RefreshCard(); }
		HidePanelsAndRefresh(TEXT("5207 код 2 — партнёр вне игры"));
		break;
	case 3:
		// Принудительный разрыв: полная очистка И сброс лотка навыков Дружбы + слотов хотбара.
		// ★ ИСПРАВЛЕНО: эталон в ЭТОЙ ветке `CObject_SetSlotString` НЕ зовёт — строка 8033
		//   печатается как есть (`soulmateWnd_handleResponsePacket@0xE67590`, game.c:473362-473365),
		//   в отличие от веток 0/1/2, где имя берётся из тела (+5). Порт подставлял имя всегда.
		ChatSocial(Loc(8033));
		ClearBond();
		// ★ ИСПРАВЛЕНО: раньше лоток чистился ДВАЖДЫ — здесь по именам `SOUL_TRAY00..11` и ещё
		//   раз в `Sk->ResetTrayAndHotbar()`. Теперь `ResetSkillTrayAndHotbar()` делегирует
		//   лоток владельцу (подсистеме навыков) и сама снимает только слоты хотбара типа 4,
		//   как `hotbarClearType4Slots` + `hotbarRebuildSlots` эталона (game.c:473377-473378).
		HidePanelsAndRefresh(TEXT("5207 код 3 — принудительный разрыв"));
		ResetSkillTrayAndHotbar();
		break;
	default: break;
	}
	// ★★★ R22: конец обработчика 5207 тоже снимает `m_nField473 = 0` (эталон делает это в обеих
	//   точках — 5205 и 5207, иначе кнопка 6587 осталась бы заблокированной навсегда).
	bBreakReqInFlight = false;
	bBreakAskPending  = false;
	// ★ 23.10.2026 (sm-info) [ОРИГИНАЛ]: ветки 1/2/3 после `gap_8B4[6] = 0` зовут `itemTooltip_RefreshControls`
	//   (social.c:7774/7783/7804): 1/3 — слот −1 → поля пусты; 2 — «Вне игры». Отметка на картах снимается.
	if (Subcode >= 1 && Subcode <= 3)
	{
		RefreshSelfBlockControls(TEXT("5207"));
		PushCoupleMark(false);
	}
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] 5207 разрыв: код=%d партнёр='%s'"), Subcode, *Name);
}

// ★★★ Ревизия 1-Б С-1: 5226 (0x146A) `tradeWnd_resetAndNotify@0xE67A70` (★ МИСНЕЙМ: к торговле отношения нет — объект
//   `g_pSoulmateWnd`, game.c:473387-473405). Тело пакета эталон НЕ читает. Порядок 1:1:
//     gap_8B4[6] = 0 → `itemTooltip_RefreshControls` (перерисовка карточки) → `inventoryHidePanelsAndRefresh` →
//     `skillHotbarClearAutoSlots` (гасит навыки Дружбы 30..44) → `hotbarRebuildSlots` →
//     loc 110104 «<FC=%08x>Друг [%s] уходит.</F>» (sprintf: цвет −28418 = 0xFFFF90FE, имя `this + 1420` = запись +63)
//     в журнал `CChat_AddTokenizedLines(…, −28418, 0, 1)` и в плашку 221 `CControl_SetTooltipText(…, −11514, 10000)`.
void UAsdaSoulmateSubsystem::OnPartnerLeft5226()
{
	UGameInstance* GI = GetGameInstance();
	Card.bHasSoulmate = false;
	// ★ 23.10.2026 (sm-info) [ОРИГИНАЛ]: эталон тут зовёт `itemTooltip_RefreshControls` (6548 «Вне игры», имя/уровень/
	//   класс/локация пусты), а НЕ `CSoulmateWnd_RefreshInfo` — порт печатал «В игре» и старые данные Друга.
	// ★ 24.10.2026 (sm-offline): Друг только что вышел — прежние сведения «вне игры» устарели (новое время выхода, персонаж, место).
	bOffStale = true;
	// ★ 24.10.2026 (ревизия, (б)): ответ на запрос после 5226 сверим со временем 5226 — БД может записать выход позже нашего запроса.
	if (CVarSoulmateOfflineLeaveRetry.GetValueOnGameThread() != 0)
	{
		bOffLeaveCheck = true;
		bOffLeaveRetryUsed = false;
		OffLeaveUtc = FDateTime::UtcNow();
	}
	if (CVarSoulmateOfflineInfo.GetValueOnGameThread() != 0) { RefreshSelfBlockControls(TEXT("5226")); }
	else if (bCardReceived) { RefreshCard(); }
	// Вкладка на экране — спросить сведения через `OfflineFriendAfterLeaveSec` (запись выхода в БД отстаёт от 5226), не чаще предела.
	if (CVarSoulmateOfflineFriendInfo.GetValueOnGameThread() != 0)
	{
		RequestOfflineFriendInfo(TEXT("5226"), FMath::Max(0.f, CVarSoulmateOfflineAfterLeave.GetValueOnGameThread()));
	}
	PushCoupleMark(false);   // Друга нет в игре — розовой точки нет
	HidePanelsAndRefresh(TEXT("5226 — Друг уходит"));
	if (UAsdaSoulmateSkillSubsystem* Sk = GI ? GI->GetSubsystem<UAsdaSoulmateSkillSubsystem>() : nullptr) { Sk->ClearTrayLit(); }
	if (UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr) { Hb->RebuildSlots(); }
	FString Line = AsdaNotice::LocTagged(GI, 110104);
	Line.ReplaceInline(TEXT("%08x"), TEXT("ffff90fe"), ESearchCase::CaseSensitive);   // sprintf(v1, −28418, …)
	Line.ReplaceInline(TEXT("%s"), *RecName(), ESearchCase::CaseSensitive);
	AsdaNotice::ChatColored(GI, EAsdaMsgCategory::Social, Line, FColor(0xFF, 0x90, 0xFE, 0xFF), 0.f);
	AsdaNotice::Plank(GI, EAsdaMsgCategory::Social, Line, AsdaNoticeColor::Gold, 10000.f, 5226);
	UE_LOG(LogAsdaSoulmate, Log, TEXT("[соулмейт] ←5226 Друг «%s» уходит: пара снята, панели погашены"), *RecName());
}
