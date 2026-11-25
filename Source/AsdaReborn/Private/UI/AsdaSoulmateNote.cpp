// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ СООБЩЕНИЕ ДРУГУ (записки соулмейту) — методы `UAsdaSoulmateSubsystem` (26.10.2026, агент smmessage).
//   Жалоба игроков: «Soulmate Message feature fails to open; no dialogue or window pops up … unable to compose or view
//   soulmate messages». Владелец: при приходе сообщения рисуется уведомление — значок с письмом.
//
// ═══════════════════════ ЧТО БЫЛО В ПОРТУ ДО ЭТОЙ ПРАВКИ (шаг 0, журналы LogDrop) ═════════════════
//   • Окно ввода 6605 `Soul_MsgWin` (кнопка 6583 «Сообщение» вкладки «Друг» окна 6400) открывалось, 6052 уходил — НО с charId
//     ДРУГА в @0 (журналы 19.10: «→6052 заметка partnerId=75215» у персонажа 75214 и наоборот). Сервер сверяет @0 со СВОИМ
//     charId и молча выбрасывает пакет — ни строки в `Joy1.dbo.SoulMateNote`, ни ответа 6053. Отправка не работала НИКОГДА.
//   • Приёма не было вовсе: 6053 / 6055 без обработчика («RECV op=6055 … хендлера нет» — 18 входов владельца, в БД с 02.09
//     лежит непрочитанная записка «123123» его персонажу charId 1), значка 9 (`mes_tag1`) никто не ставил, окна-свитки
//     6610/6620/6630 (`WBP_Soul_RMsgWin/2/3`, ассеты есть) не открывались, 6054 не уходил.
//   • Фокус поля 6602 ставился `SetKeyboardFocus` — его перебивал фокус, который Slate отдаёт нажатой кнопке 6583.
//
// ═══════════════════════ ЭТАЛОН (клиент `RE/etalon`, сервер `GameServer.c`, `DBManager.exe.c`) ═════
//   ОТПРАВКА. Кнопка 6583 (`SoulmateUI_HandleButtonCommand@0xE69F90` case 0x19B7, social.c:8270): слот Друга `gap_878[47]` ≤ 0 →
//     loc 6035 в чат (−11514, 10 с); иначе показать 6605, очистить и дать ФОКУС полю 6602 (`CControl_SetFocus(+60, 1)`).
//     Кнопка 6603 (case 0x19CB): длина 1..64 → `netSendChatGuildMsg@0xE6B070` (МИСНЕЙМ, network.c:29627): конверт B, payload 69:
//     @0 i32 СВОЙ charId (`*(g_pLocalSessionCtx+1052)`), @4 char[65] текст, апостроф → обратная кавычка (защита от SQL-инъекции
//     `UPSoulMateNoteInput`); затем спрятать 6605 и снять фокус. 6604 (0x19CC) — спрятать и снять фокус. Enter в поле 6602 —
//     событие 67587 с id 6602, оригинал его НЕ разбирает (social.c:7899 — только 9101).
//   СЕРВЕР. 6052 → `Soulmate_SendNoteReq@0x4992E0` (GameServer.c:61333): `sub_524390` шапка, `sub_523F00(…, 0x45, …, 1)`,
//     `CUser+372 == @0` (СВОЙ charId), текст не пуст, слот `+99001 > 0`, `!+99030` → DBManager 2238 (слот, charId Друга `+99014`,
//     текст). DBManager `sub_42E1E0` → `EXEC UPSoulMateNoteInput слот, charId_Друга, 'текст'` (БД: SoulMateNote idx/Smidx/
//     Userindex/Note varchar(65)/Edate; у одного получателя не больше ТРЁХ — иначе Flag 2) → 2239 [флаг, номер, текст] →
//     `sub_4994D0@0x4994D0` (a4=0): флаг 1 и Друг в игре — 6053 вид 1 Другу (@4 его userNum, @6 его charId, @10 вид, @11 номер,
//     @15 текст, @80..@99 время сервера Y/M/D/h/m), на другом сервере — через чат-сервер (0x17A5 → `sub_4994D0` a4=1 пересылает);
//     затем отправителю 6053 вид 3 (@6 свой charId). Флаг ≠ 1 — отправителю 6053 с флагом (0 — ошибка, 2 — больше трёх).
//     Друг вне игры — записка лежит в БД до входа.
//   ВХОД / ПОРТАЛ. CharMake и CharPortalMake (GameServer.c:124152 / 124348) → `sub_4998F0` (a3=0) → DBManager 2241 →
//     `sub_42E5F0` `EXEC UPSoulMateNote слот, свой charId` → до трёх записей → 2242 → 6055 (279 б = 3 × 93; пустые не шлются).
//   ПРИЁМ (клиент, диспетчер network.c:474-477 с `g_pSoulmateWnd`):
//     6053 `guildHandleEventNotify@0xE6C840` (МИСНЕЙМ, social.c:8379): вид 1 и @6 = свой charId → запись 93 б в очередь
//       (`deque_PushBack(this+511)`), `statusBarAddIcon(…, 9)` + `MsgNotify_RefreshIcons`, loc 12181 в чат (−11514, 10 с);
//       вид 3 и свой charId → 12178; вид 2 → 12179; вид 0 → 12180 (два последних — без проверки charId); прочие — молча.
//     6055 `partyMemberSyncIcons@0xE6CB70` (МИСНЕЙМ, social.c:8474): первая запись моя → снять из головы очереди до трёх
//       записей; каждая моя запись → в очередь + значок 9.
//     Значок 9 = `mes_tag1` (`MsgNotify_TypeToIcon@0xDF4450`); `statusBarAddIcon` повторы типа 9 НЕ отсеивает (значок на
//       каждую записку), пульсация 500 мс — общая; звука нет (звук 215 только у типа 1, `statusEffectAddIconAndNotify`).
//     Клик по значку (`uiNoticeButtonDispatch@0xDF4F10` case 9) → `itemQueuePopAndShowTooltip@0xE6AD60` (МИСНЕЙМ,
//       item_inventory.c:57184): очередь пуста — ничего; иначе снять первую запись, слоты [sd1]..[sd5] ← время, показать первое
//       НЕвидимое окно 6610 → 6620 → 6630 (все видны — 6630), время 6614/6624/6634 ← loc 12177, текст 6613/6623/6633 ← запись,
//       затем 6054 (`antiCheat_SendHeartbeat6054@0xE6B740`, МИСНЕЙМ: payload 4 = номер записки). Значок снимает
//       `buffSlotCompact` — у типа 9 ОДИН (ветка default). Сервер 6054 → `Soulmate_ReceiveNoteConfirm@0x4997D0` → 2240 →
//       `UPSoulMateNoteDel номер, свой charId` (записка удалена — «прочитано»).
//     Окна-свитки: заголовок 6611/6621/6631 ← loc 12176 (флаг 0x200000 — псевдожирный), закрытие 6612/6622/6632 → спрятать
//       своё окно (social.c:8240-8265). ESC их не закрывает (в эталонной ESC-таблице их нет), Enter — тоже.
//     Отдельного «списка» и «удаления» записок в клиенте НЕТ: список — это значки (до трёх у сервера), удаление — прочтение.
//   НУМПАД: нет (количеств нет). Связи с картами прежних Друзей нет: записки идут по ТЕКУЩЕМУ слоту пары (Smidx).
//
// ═══════════════════════ ТАБЛИЦА НОТИСОВ «событие → loc → канал» ═════════════════════════════════
//   6583 без Друга                 → 6035  «Без Друга ничего не выйдет.»        → чат −11514, 10 с (ChatTip — прежнее место)
//   6053 вид 1, мне                → 12181 «Друг получил твое послание.» / «Your Soulmate's message has arrived.» → чат −11514
//   6053 вид 3, мне (отправлено)   → 12178 «Сообщение Другу отправлено.»         → чат −11514, 10 с
//   6053 вид 2 (у Друга уже три)   → 12179 «Нельзя отправлять более трех сообщений.» → чат −11514, 10 с
//   6053 вид 0 (ошибка БД)         → 12180 «Не удалось отправить сообщение.»     → чат −11514, 10 с
//   окно-свиток: заголовок 12176 «Сообщение Другу», время 12177 «Отправка [sd1]-[sd2]-[sd3] [sd4]:[sd5]»
//   ⚠ RU 12181 переведена неверно по смыслу (её видит ПОЛУЧАТЕЛЬ: EN «пришло сообщение Друга»), EN 12177 без «:» между
//     часом и минутой — текст не менялся (решение по текстам — за владельцем, ведомость `gametext_overrides.json`).
//
// ═══════════════════════ КЛЮЧИ (0 = прежнее поведение порта) ═════════════════════════════════════
//   [ИСПРАВЛЕНИЕ ПОРТА] asda.Soulmate.NoteSelfCharId 1   — 6052 @0 = свой charId (0: charId Друга — сервер выбрасывает).
//   [ОРИГИНАЛ]          asda.Soulmate.NoteRecv 1         — приём 6053/6055, значок 9, окна 6610/6620/6630, 6054.
//   [ОРИГИНАЛ]          asda.Soulmate.NoteInputFocus 1   — фокус полю 6602 по образцу окна 1580 (0: прежний SetKeyboardFocus).
//   [ИСПРАВЛЕНИЕ]       asda.Soulmate.NoteEnterSend 1    — Enter в поле 6602 = «Отправить» 6603 (0: как оригинал — ничего).
//   [ОРИГИНАЛ]          asda.Soulmate.NoteIconOnePerClick 1 — клик снимает ОДИН значок 9 (0: прежний порт — все значки 9).
//   [ПОРТ] панель уведомлений пересоздаётся на каждой смене карты — значки 9 тик выставляет заново по очереди записок;
//          окна-свитки после смены карты возвращаются с тем же текстом (оригинал окон не пересоздаёт).
//   Самотест: `asda.Soulmate.NoteSelfTest [quit]` (AsdaSoulmateNoteSelfTest.cpp).
// ═══════════════════════════════════════════════════════════════════════════════════════════════
#include "UI/AsdaSoulmateSubsystem.h"
#include "AsdaDevConsole.h"
#include "AsdaBytes.h"
#include "AsdaNetSubsystem.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaHotbarSubsystem.h"
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaNoticeColors.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::FindLabel / SetTxt
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/TextBlock.h"
#include "Components/SlateWrapperTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSoulNote, Log, All);

namespace AsdaSoulNoteLocal
{
	static ASDA_DEV_CVAR(int32, CVarNoteSelfCharId,
		TEXT("asda.Soulmate.NoteSelfCharId"), 1,
		TEXT("Записка Другу 6052: 1 (по умолчанию) [ИСПРАВЛЕНИЕ ПОРТА] — @0 = СВОЙ charId, как оригинал (`netSendChatGuildMsg`).\n")
		TEXT("0 = прежний порт: @0 = charId Друга — сервер (`Soulmate_SendNoteReq`) молча выбрасывает записку."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarNoteRecv,
		TEXT("asda.Soulmate.NoteRecv"), 1,
		TEXT("Записки Другу: 1 (по умолчанию) [ОРИГИНАЛ] — приём 6053/6055, значок письма (тип 9), окна 6610/6620/6630, 6054.\n")
		TEXT("0 = прежний порт: 6053/6055 только в журнал, значка и окон нет."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarNoteInputFocus,
		TEXT("asda.Soulmate.NoteInputFocus"), 1,
		TEXT("Поле записки 6602: 1 (по умолчанию) [ОРИГИНАЛ] — фокус полю при открытии 6605 по образцу окна 1580 (SetUserFocus +\n")
		TEXT("страховка следующего кадра). 0 = прежний порт (SetKeyboardFocus, фокус перебивает нажатая кнопка)."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarNoteEnterSend,
		TEXT("asda.Soulmate.NoteEnterSend"), 1,
		TEXT("Поле записки 6602: 1 (по умолчанию) [ИСПРАВЛЕНИЕ] — Enter в поле = «Отправить» 6603 (правило EnterConfirm: записка\n")
		TEXT("обратима, Enter в поле — правило поля). 0 = как оригинал: Enter в поле ничего не делает."),
		ECVF_Default);
	static ASDA_DEV_CVAR(int32, CVarNoteIconOnePerClick,
		TEXT("asda.Soulmate.NoteIconOnePerClick"), 1,
		TEXT("Значок записки (тип 9): 1 (по умолчанию) [ОРИГИНАЛ] — клик снимает ОДИН значок (`buffSlotCompact`, ветка default).\n")
		TEXT("0 = прежний порт: клик снимает все значки типа 9."),
		ECVF_Default);

	// Окна-свитки 6610 / 6620 / 6630 — id в реестре окон и WBP (`CSoulmateWnd_InitControls`, social.c:6426-6446).
	static const FName kNoteWindowId[UAsdaSoulmateSubsystem::kNoteWinCount] = {
		FName(TEXT("Soul_RMsgWin")), FName(TEXT("Soul_RMsgWin2")), FName(TEXT("Soul_RMsgWin3")) };
	static const TCHAR* const kNoteWbp[UAsdaSoulmateSubsystem::kNoteWinCount] = {
		TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_Soul_RMsgWin.WBP_Soul_RMsgWin_C"),
		TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_Soul_RMsgWin2.WBP_Soul_RMsgWin2_C"),
		TEXT("/Game/AsdaAssets/UI/Player_UI/Soulmate/WBP_Soul_RMsgWin3.WBP_Soul_RMsgWin3_C") };
	static constexpr int32 kNoteWinZ = 420;           // как у прочих окон соулмейта (`kWinZ`) — поверх окна сообщества
	static constexpr int32 kRec93 = 93;               // запись очереди / 6055
	static constexpr int32 kBody6053 = 100;           // тело 6053 (`guildRec[25]`)
	static constexpr int32 kBody6055 = 3 * kRec93;    // тело 6055 (279 б)
	// loc записок.
	static constexpr int32 kLocArrived = 12181, kLocSent = 12178, kLocTooMany = 12179, kLocFailed = 12180;
	static constexpr int32 kLocTitle = 12176, kLocTime = 12177;
	static const FName kHotbarWindowId(TEXT("QUICK_NEW_WIN"));   // панель быстрых слотов (значки уведомлений)
}
using namespace AsdaSoulNoteLocal;

// Аксессоры ключей (имена ключей в рабочем коде других файлов не встречаются — правило Shipping-проверки дев-строк).
bool AsdaSoulNoteSelfCharIdOn()      { return CVarNoteSelfCharId.GetValueOnGameThread() != 0; }
bool AsdaSoulNoteEnterSendOn()       { return CVarNoteEnterSend.GetValueOnGameThread() != 0; }
bool AsdaSoulNoteIconOnePerClickOn() { return CVarNoteIconOnePerClick.GetValueOnGameThread() != 0; }
bool AsdaSoulNoteInputFocusOn()      { return CVarNoteInputFocus.GetValueOnGameThread() != 0; }
static bool AsdaSoulNoteRecvOn()     { return CVarNoteRecv.GetValueOnGameThread() != 0; }

// ═══════════════════════════════ ОКНА-СВИТКИ ════════════════════════════════════════════════════

UUserWidget* UAsdaSoulmateSubsystem::NoteWndAt(int32 I) const
{
	switch (I) { case 0: return NoteWnd0; case 1: return NoteWnd1; case 2: return NoteWnd2; default: return nullptr; }
}

TObjectPtr<UUserWidget>& UAsdaSoulmateSubsystem::NoteWndRef(int32 I)
{
	switch (I) { case 1: return NoteWnd1; case 2: return NoteWnd2; default: return NoteWnd0; }
}

// Импортёр называет узлы второго/третьего окна с суффиксом номера: `Soul_RMsgTxt2`, `Soul_RMsg_CloseBtn3` (idc JUI).
FString UAsdaSoulmateSubsystem::NoteNodeName(const TCHAR* Base, int32 I)
{
	return (I <= 0) ? FString(Base) : FString::Printf(TEXT("%s%d"), Base, I + 1);
}

bool UAsdaSoulmateSubsystem::EnsureNoteWindow(int32 I)
{
	if (I < 0 || I >= kNoteWinCount) { return false; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// Каркас «открыть при первом показе»: в `.JUI` у окна `Visible="0"` — строим скрытым, показ решает `FillAndShowNoteWindow`.
	const EAsdaJuiEnsure Ens = FAsdaJuiWindowHandle(kNoteWindowId[I], kNoteWbp[I], kNoteWinZ).EnsureLazy(*WM, NoteWndRef(I), /*bReady*/true,
		ESlateVisibility::Collapsed,
		[]() {},
		[I]() { UE_LOG(LogAsdaSoulNote, Error, TEXT("[записка] WBP окна-свитка %d (%s) не загрузился"), I, kNoteWbp[I]); },
		[this, I](UAsdaWindowSubsystem&)
		{
			UUserWidget* W = NoteWndAt(I);
			if (!W) { return; }
			// [ОРИГИНАЛ] social.c:6434-6436: заголовок 6611+10i ← loc 12176, флаг вывода 0x200000 (псевдожирный, `font_text.c:6832`).
			const FString Title = NoteNodeName(TEXT("Soul_RMsgTitle_Move"), I);
			AsdaCommunityUi::SetTxt(W, Title, AsdaNotice::Loc(GetGameInstance(), kLocTitle));
			AsdaUi::ApplyPseudoBold(AsdaCommunityUi::FindLabel(W, Title));
			// Тексты 6613/6614 эталон в InitControls не пишет — гасим заглушки разметки до первого показа.
			AsdaCommunityUi::SetTxt(W, NoteNodeName(TEXT("Soul_RMsgTxt"), I), FString());
			AsdaCommunityUi::SetTxt(W, NoteNodeName(TEXT("Soul_RMsgTimeTxt"), I), FString());
			// [ОРИГИНАЛ] 6612/6622/6632 → `SetVisible(окно i, 0)` (social.c:8240-8265).
			const FString Close = NoteNodeName(TEXT("Soul_RMsg_CloseBtn"), I);
			if (UButton* B = Cast<UButton>(W->GetWidgetFromName(FName(*Close))))
			{
				AsdaUi::BindButton(this, B, 6612 + 10 * I, [this, I](int32) { CloseNoteWindow(I); }, NoteRelays);
			}
			else
			{
				UE_LOG(LogAsdaSoulNote, Warning, TEXT("[записка] окно-свиток %d: кнопки «%s» нет"), I, *Close);
			}
		});
	return FAsdaJuiWindowHandle::Succeeded(Ens);
}

void UAsdaSoulmateSubsystem::FillAndShowNoteWindow(int32 I, const FAsdaSoulNote& N)
{
	if (!EnsureNoteWindow(I)) { return; }
	UUserWidget* W = NoteWndAt(I);
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!W || !WM) { return; }
	// [ОРИГИНАЛ] `CClass_SetSlotPair(ctx, 0..4, время)` + loc 12177 → 6614+10i; текст записки → 6613+10i.
	FString Time;
	if (UAsdaTextSubsystem* Txt = GI->GetSubsystem<UAsdaTextSubsystem>())
	{
		for (int32 k = 0; k < 5; ++k) { Txt->NoticeSetInt(k, N.Stamp[k]); }
		Time = Txt->FormatNotice(kLocTime);
	}
	AsdaCommunityUi::SetTxt(W, NoteNodeName(TEXT("Soul_RMsgTimeTxt"), I), Time);
	AsdaCommunityUi::SetTxt(W, NoteNodeName(TEXT("Soul_RMsgTxt"), I), N.Text);
	NoteShown[I] = N;
	bNoteWinWanted[I] = true;
	WM->SetWindowVisible(kNoteWindowId[I], true);
	WM->BringWindowToFront(kNoteWindowId[I]);
	UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] окно-свиток %d (%s) показано: №%d «%s», %s"),
		I, *kNoteWindowId[I].ToString(), N.NoteId, *N.Text, *Time);
}

void UAsdaSoulmateSubsystem::CloseNoteWindow(int32 Index)
{
	if (Index < 0 || Index >= kNoteWinCount) { return; }
	bNoteWinWanted[Index] = false;
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		if (NoteWndAt(Index)) { WM->SetWindowVisible(kNoteWindowId[Index], false); }
	}
}

bool UAsdaSoulmateSubsystem::IsNoteWindowShown(int32 Index) const
{
	const UUserWidget* W = NoteWndAt(Index);
	const UGameInstance* GI = GetGameInstance();
	const UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return W && WM && WM->IsWindowLive(W) && WM->IsWindowVisible(kNoteWindowId[Index]);
}

UUserWidget* UAsdaSoulmateSubsystem::GetNoteWindowForCheck(int32 Index) const { return NoteWndAt(Index); }

// ═══════════════════════════════ ОКНО ВВОДА 6605 ═══════════════════════════════════════════════

bool UAsdaSoulmateSubsystem::IsNotePanelShown() const
{
	if (!CommWnd || !IsBoundTo(CommWnd) || !CommWnd->IsInViewport() || CommWnd->GetVisibility() == ESlateVisibility::Collapsed
		|| CommWnd->GetVisibility() == ESlateVisibility::Hidden)
	{
		return false;
	}
	const UWidget* G = AsdaUi::FindNodeExact(CommWnd, TEXT("Soul_MsgWin_grp"));
	return G && G->GetVisibility() != ESlateVisibility::Collapsed && G->GetVisibility() != ESlateVisibility::Hidden;
}

bool UAsdaSoulmateSubsystem::IsNoteInputFocused() const
{
	const UWidget* E = CommWnd ? CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input")) : nullptr;
	return E && E->HasKeyboardFocus();
}

// [ОРИГИНАЛ] `CControl_SetFocus(+60, 1)` (social.c:8284). Образец — общая точка окон 1410/1580 (`AsdaFocusModalField`,
//   AsdaWindowSubsystem_Dialogs.cpp): `SetUserFocus(PC)` отменяет отложенный запрос фокуса, страховка следующего кадра — если фокус
//   увела нажатая кнопка 6583 или подъём окна по щелчку (тело 6400 не модальное — проверяем видимость окна записки, а не модалку).
void UAsdaSoulmateSubsystem::FocusNoteInput()
{
	UEditableText* E = CommWnd ? Cast<UEditableText>(CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input"))) : nullptr;
	if (!E) { return; }
	if (CVarNoteInputFocus.GetValueOnGameThread() == 0) { E->SetKeyboardFocus(); return; }
	UGameInstance* GI = GetGameInstance();
	UWorld* W = GI ? GI->GetWorld() : nullptr;
	APlayerController* PC = (GI && W) ? GI->GetFirstLocalPlayerController(W) : nullptr;
	if (PC) { E->SetUserFocus(PC); } else { E->SetKeyboardFocus(); }
	if (!W) { return; }
	TWeakObjectPtr<UEditableText> WeakE = E;
	W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, WeakE]()
	{
		UEditableText* F = WeakE.Get();
		if (!F || !CommWnd || CommWnd->GetWidgetFromName(TEXT("Soul_Msg_Input")) != F || !IsNotePanelShown()) { return; }
		bool bOtherField = false;   // фокус уже у другого поля ввода (игрок сам щёлкнул) — не трогаем
		if (FSlateApplication::IsInitialized())
		{
			const TSharedPtr<SWidget> Cur = FSlateApplication::Get().GetKeyboardFocusedWidget();
			bOtherField = Cur.IsValid() && Cur != F->GetCachedWidget() && Cur->GetTypeAsString().Contains(TEXT("EditableText"));
		}
		if (!F->HasKeyboardFocus() && !bOtherField)
		{
			UGameInstance* G = GetGameInstance();
			APlayerController* P = G ? G->GetFirstLocalPlayerController(G->GetWorld()) : nullptr;
			if (P) { F->SetUserFocus(P); } else { F->SetKeyboardFocus(); }
			UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка 6602] фокус клавиатуры возвращён полю следующим кадром"));
		}
		UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка 6602] фокус клавиатуры после открытия: поле — %s"), F->HasKeyboardFocus() ? TEXT("да") : TEXT("НЕТ"));
	}));
}

// ═══════════════════════════════ ПРИЁМ ══════════════════════════════════════════════════════════

FAsdaSoulNote UAsdaSoulmateSubsystem::ParseNote93(const uint8* P)
{
	FAsdaSoulNote N;
	if (!P) { return N; }
	N.CharId = AsdaBytes::RdI32(P + 0);
	N.NoteId = AsdaBytes::RdI32(P + 4);
	// Поле хранится в БД (`SoulMateNote.Note` varchar(65)) — пара cp1251, как у отправки (`FAsdaPacketWriter::Cp1251`).
	N.Text = FAsdaLoginData::DecodeCp1251(P + 8, 65);
	for (int32 k = 0; k < 5; ++k) { N.Stamp[k] = AsdaBytes::RdI32(P + 73 + 4 * k); }
	return N;
}

void UAsdaSoulmateSubsystem::NoteChat(int32 LocId)
{
	UGameInstance* GI = GetGameInstance();
	const FString Text = AsdaNotice::Loc(GI, LocId);
	++NoteNoticeSeq;
	NoteLastNoticeLoc = LocId;
	if (Text.IsEmpty()) { UE_LOG(LogAsdaSoulNote, Warning, TEXT("[записка] loc %d пуст"), LocId); return; }
	if (UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		// `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)` — тот же канал, что ChatTip (6035 кнопки 6583).
		FAsdaMsgParams P;
		P.Category  = EAsdaMsgCategory::Social;
		P.Text      = Text;
		P.bUseColor = true;
		P.Color     = AsdaNoticeColor::Gold;   // −11514 = 0xFFFFD306
		P.TtlMs     = 10000.f;
		P.SrcOpcode = 6053;
		Msg->Post(P);
	}
}

void UAsdaSoulmateSubsystem::PushNote(const FAsdaSoulNote& N, const TCHAR* Why)
{
	NoteQueue.Add(N);          // `deque_PushBack(this + 511, запись)`
	bNoteIconsDirty = true;    // `statusBarAddIcon(…, 9)` — тиком (панель могла ещё не подняться)
	UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] в очередь (%s): №%d «%s» %04d-%02d-%02d %02d:%02d, в очереди %d"),
		Why, N.NoteId, *N.Text, N.Stamp[0], N.Stamp[1], N.Stamp[2], N.Stamp[3], N.Stamp[4], NoteQueue.Num());
}

void UAsdaSoulmateSubsystem::OnNote6053(const TArray<uint8>& Body)
{
	if (!AsdaSoulNoteRecvOn())
	{
		UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6053 (%d б) не разбираем — ключ приёма записок выключен"), Body.Num());
		return;
	}
	if (Body.Num() < kBody6053)
	{
		UE_LOG(LogAsdaSoulNote, Warning, TEXT("[записка] ←6053 короткое тело %d б (ждали %d) — хвост нулями"), Body.Num(), kBody6053);
	}
	uint8 R[kBody6053] = {};
	FMemory::Memcpy(R, Body.GetData(), FMath::Min(Body.Num(), kBody6053));
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Self = Net ? Net->SelectedCharId : -1;
	const int32 Cid = AsdaBytes::RdI32(R + 6);
	const int8 Kind = (int8)R[10];   // `SBYTE2(guildRec[2])` — знаковый байт @10
	UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6053 вид %d charId=%d (свой %d) №%d"), Kind, Cid, Self, AsdaBytes::RdI32(R + 11));
	if (Kind == 1)
	{
		if (Cid != Self) { return; }   // эталон: только записка МНЕ
		// Запись 93 б собирается из тела: @0 ← @6, @4 ← @11, @8 ← @15 (strcpy), @73..@92 ← @80..@99.
		FAsdaSoulNote N;
		N.CharId = Cid;
		N.NoteId = AsdaBytes::RdI32(R + 11);
		N.Text = FAsdaLoginData::DecodeCp1251(R + 15, 65);
		for (int32 k = 0; k < 5; ++k) { N.Stamp[k] = AsdaBytes::RdI32(R + 80 + 4 * k); }
		PushNote(N, TEXT("6053 вид 1"));
		NoteChat(kLocArrived);
	}
	else if (Kind == 3)
	{
		if (Cid == Self) { NoteChat(kLocSent); }
	}
	else if (Kind != 0)
	{
		if (Kind == 2) { NoteChat(kLocTooMany); }
		else { UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6053 вид %d — у оригинала ветки нет, молча"), Kind); }
	}
	else
	{
		NoteChat(kLocFailed);
	}
}

void UAsdaSoulmateSubsystem::OnNotes6055(const TArray<uint8>& Body)
{
	if (!AsdaSoulNoteRecvOn())
	{
		UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6055 (%d б) не разбираем — ключ приёма записок выключен"), Body.Num());
		return;
	}
	if (Body.Num() < kBody6055)
	{
		UE_LOG(LogAsdaSoulNote, Warning, TEXT("[записка] ←6055 короткое тело %d б (ждали %d) — хвост нулями"), Body.Num(), kBody6055);
	}
	uint8 R[kBody6055] = {};
	FMemory::Memcpy(R, Body.GetData(), FMath::Min(Body.Num(), kBody6055));
	const UGameInstance* GI = GetGameInstance();
	const UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Self = Net ? Net->SelectedCharId : -1;
	FAsdaSoulNote Recs[3];
	for (int32 j = 0; j < 3; ++j) { Recs[j] = ParseNote93(R + kRec93 * j); }
	// [ОРИГИНАЛ] первая запись моя → «если очередь не пуста — снять голову» трижды (повторная пачка после портала).
	if (Recs[0].CharId == Self)
	{
		int32 Dropped = 0;
		for (int32 i = 0; i < 3; ++i) { if (NoteQueue.Num() > 0) { NoteQueue.RemoveAt(0); ++Dropped; } }
		if (Dropped > 0) { UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6055: снято из головы очереди %d прежних"), Dropped); }
	}
	int32 Mine = 0;
	for (int32 j = 0; j < 3; ++j)
	{
		if (Recs[j].CharId == Self) { PushNote(Recs[j], TEXT("6055")); ++Mine; }
	}
	UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] ←6055 непрочитанных моих %d (свой charId %d), в очереди %d"), Mine, Self, NoteQueue.Num());
}

// ═══════════════════════════════ ЧТЕНИЕ ═════════════════════════════════════════════════════════

bool UAsdaSoulmateSubsystem::OpenNextNote()
{
	if (!AsdaSoulNoteRecvOn()) { return false; }
	if (NoteQueue.Num() == 0)
	{
		UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] значок 9: очередь пуста — ничего (как оригинал)"));
		return false;
	}
	const FAsdaSoulNote N = NoteQueue[0];
	NoteQueue.RemoveAt(0);
	// [ОРИГИНАЛ] первое невидимое окно: 6610 → 6620 → 6630; все видны — 6630 (перезапись).
	const int32 I = !bNoteWinWanted[0] ? 0 : (!bNoteWinWanted[1] ? 1 : 2);
	FillAndShowNoteWindow(I, N);
	// 6054 — номер записки: сервер удаляет её из БД (`UPSoulMateNoteDel`) — «прочитано».
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->SendSoulmateNoteConfirm6054(N.NoteId);
		++NoteConfirmSent;
		NoteLastConfirmId = N.NoteId;
	}
	bNoteIconsDirty = true;
	return true;
}

// ═══════════════════════════════ ТИК ════════════════════════════════════════════════════════════

void UAsdaSoulmateSubsystem::TickNotes()
{
	if (!AsdaSoulNoteRecvOn()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	// Панель уведомлений ещё не поднята (экран загрузки, логин) — ждём молча: и значки, и окна вернутся на ней.
	if (!WM || !Hb || !WM->IsOpen(kHotbarWindowId)) { return; }

	// ── значки 9: столько, сколько записок в очереди (по одному на записку, как `statusBarAddIcon` без отсева типа 9) ──
	if (NoteIconsShown > 0 && !Hb->HasNoticeIcon(kNoticeTypeNote))
	{
		// Панель пересоздана (смена карты) или значки сняты чужим путём — наших больше нет.
		NoteIconsShown = 0;
		bNoteIconsDirty = true;
	}
	if (bNoteIconsDirty)
	{
		bNoteIconsDirty = false;
		while (NoteIconsShown < NoteQueue.Num())
		{
			if (!Hb->AddNoticeIcon(kNoticeTypeNote)) { break; }   // все 7 слотов заняты — как оригинал: записка без значка
			++NoteIconsShown;
		}
	}

	// ── окна-свитки: оригинал их не пересоздаёт — после смены карты вернуть на новом виджете ──
	for (int32 i = 0; i < kNoteWinCount; ++i)
	{
		if (!bNoteWinWanted[i]) { continue; }
		const UUserWidget* W = NoteWndAt(i);
		if (W && WM->IsWindowLive(W)) { continue; }
		UE_LOG(LogAsdaSoulNote, Log, TEXT("[записка] окно-свиток %d вернулось после смены карты"), i);
		FillAndShowNoteWindow(i, NoteShown[i]);
	}
}
