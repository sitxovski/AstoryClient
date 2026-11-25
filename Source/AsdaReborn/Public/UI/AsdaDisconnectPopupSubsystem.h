// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaDisconnectPopupSubsystem — ПОПАП ОБРЫВА СВЯЗИ `COMMONWIN_MSGBOX` (JUI IDNum **8000**).
//
// ═════════ ЧТО ЭТО ЗА ОКНО (эталон) ═══════════════════════════════════════════════════════════
//   Разбор контролов — `Docs/UI/Common/COMMONWIN_MSGBOX.md`. Общий модальный месседж-бокс:
//   иконка `COMMONWIN_ICON` 8001 (декор) + текст `COMMONWIN_TEXT` 8002 + ОДНА кнопка
//   `COMMONWIN_BUT` 8003 с подписью из нотиса 2288 «ОК». Остальные контролы окна (`*_Base`,
//   `*_LBase`, `*_RBase`, `*_BUT_Icon`) имеют `idnum = −1` и кодом не адресуются — это подложки
//   одно-/двухкнопочной раскладки одного и того же .JUI.
//
//   ЕДИНСТВЕННАЯ точка, которая обращается к 8000/8002/8003, — `Show_DisconnectPopup@0xEC9A80`
//   (`AsdaStory_game.c:509056`). Её порядок действий 1:1:
//     1) `netCloseSocketBySlot(g_pNetwork, …)` — СНАЧАЛА реально закрыть сокет;
//     2) если `g_GameState` ∈ {1, 8, 11, 12, 16, 500} (игровые состояния) — окно 8000 ищется
//        В УЖЕ ЗАГРУЖЕННОМ наборе (`FindControlByID`), догрузки .JUI нет;
//     3) если `g_GameState` ∈ {2, 3, 5, 6, 10} (логин/загрузка) — это ВООБЩЕ ДРУГОЙ путь:
//        `uiControl_setTextAndFlag(g_pGameApp, заголовок=нотис 9580, текст, 1)` +
//        `wndShowHideOrQuit(g_pGameApp, 1)`, то есть контролы ЛОГИН-ОКНА 110/112, а не 8000
//        (прямое замечание разбора: «это ДРУГОЕ, логин-окно»). Порт держит эту развилку;
//     4) иначе — `LoadJUI_ParseNode("./JUIData/CommonWin.JUI")` и те же 8000/8002/8003, но уже из
//        класса `commonwin` (второй .JUI, у него свои скины и подписи — см. два дампа
//        `Docs/UI/_inv/Common/COMMONWIN_MSGBOX.json` и `Docs/UI/_inv/commonwin/…json`);
//     5) текст: `a2 == −1` → строка последней ошибки приложения `sub_9236EA(g_pAppRoot)`,
//        иначе `UI_FormatLocalizedNotice(ctx, a2, 0, 1)`;
//     6) подпись кнопки — нотис 2288; колбэк — `CommonWin_CommonEventFunc@0xC6EAE0`;
//     7) `Cursor_SetByIndex(…, 0, 0)`, `SetVisible(8000, 1)`, `helper_setupFields`.
//
// ═════════ ★★★ ОТСТУПЛЕНИЕ ОТ ЭТАЛОНА, КОТОРОЕ ЗАКРЕПЛЕНО ЗАКОНОМ ПРОЕКТА ══════════════════════
//   `CommonWin_CommonEventFunc@0xC6EAE0` (`AsdaStory_game.c:292421`) по клику 8003 делает ровно:
//       if (a2 == 66561 && a1 == 8003) { Net_RecvUiTick(); PostQuitMessage(0); }
//   то есть ЗАКРЫВАЕТ КЛИЕНТ. Закон `original-quits-client-on-disconnect`: у оригинала сброса
//   состояния НЕТ по построению — вместе с процессом умирают все поля `CCharacter`. Порт
//   намеренно оставляет клиент живым («вход можно повторить», см. шапку
//   `UAsdaNetSubsystem::HandleConnectionClosed`) и обязан чистить зеркала сам — это уже сделано
//   в `UAsdaSessionResetSubsystem`. ⇒ КНОПКА ЗДЕСЬ НЕ ЗАВЕРШАЕТ ПРОЦЕСС, а только закрывает попап;
//   сброс сессии к этому моменту уже выполнен подписчиком `OnGameDisconnected`.
//   ★★★ 19.10.2026 (жалоба владельца): если попап закрыт в МИРЕ, ОК ведёт на экран входа (`OpenLevel /Game/Map/Login`) —
//   иначе игрок оставался в мире без связи и продолжал ходить. На сцене входа — только закрыть попап.
//   Ключ отката к буквальному поведению оригинала: `asda.Net.DisconnectPopupQuit 1`.
//
// ═════════ ТАБЛИЦА «ПРИЧИНА → НОТИС» (1:1, WndProc, ветка `msg == 1297 && lParam == 2`) ════════
//   `AsdaStory_game.c:23941-24085`. `g_nDisconnectReason` — это БАЙТ ТЕЛА 4101
//   (`g_nDisconnectReason = *pLoginStatusByte`, `:16095`), в порту он же `UAsdaNetSubsystem::
//   LastKickCode`; 255 = «обрыв без 4101».
//       0 → 1002 · 1 → 1098 · 2 → 1099 · 3 → 1100 · 5 → 8509 · 6 → 1106 · 7 → 1107 ·
//       8 → 9566 · 9 → 9567 · 10 → 12940 · 11 → 12941 · 12 → 14000 · 13 → 14103 ·
//       14 → 14150 · 15 → 7034 · 255 → 1108
//   ⚠ Причины **4** в switch НЕТ (`default: break`) — и это не пропуск разбора: 4 ставится при
//     неудаче коннекта к портал-серверу (`:31490-31493`), где попап показан ОТДЕЛЬНО с нотисом
//     1001 ещё ДО присваивания. Поэтому 4 здесь молчит — так же, как в оригинале.
//   ⚠ Причина 14 в оригинале показывает попап ДВАЖДЫ (`g_GameState = 100` → показ → `= 11` →
//     показ), потому что от состояния зависит, какую из двух веток выберет `Show_DisconnectPopup`.
//     У нас состояние берётся из живого признака соединения, второй показ был бы дублем окна —
//     см. `ReasonToNoticeId`.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaDisconnectPopupSubsystem.generated.h"

class UUserWidget;

UCLASS()
class ASDAREBORN_API UAsdaDisconnectPopupSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Порт `Show_DisconnectPopup@0xEC9A80`. `NoticeId` — код нотиса текста; −1 = «строка последней
	//   ошибки приложения» (`sub_9236EA`), у нас это последняя строка отказа сети.
	//   Возвращает виджет попапа либо nullptr, если показать нечем (ассет не загрузился).
	UFUNCTION(BlueprintCallable, Category="Asda|Разрыв")
	UUserWidget* ShowDisconnectPopup(int32 NoticeId);

	// ★ 16.10.2026 (EAC, тексты владельца): тот же попап 8000 с ГОТОВЫМ текстом (заголовок 991020/991000 + текст причины
	//   EAC, собирает UAsdaNetSubsystem). Строки 991xxx в таблице — одной фразой без переносов, а перенос по ширине у
	//   COMMONWIN_TEXT в канвасе не работает (закон `ui-autowrap-canvas-gotcha`), поэтому текст разбивается на строки
	//   по словам под ширину слота контрола (замер шрифтом самого контрола). Разметка окна не меняется.
	UUserWidget* ShowDisconnectPopupText(const FString& Message);

	// Таблица «причина 4101 → нотис» (см. шапку). −1 = у этой причины попапа нет (случай 4).
	UFUNCTION(BlueprintPure, Category="Asda|Разрыв")
	static int32 ReasonToNoticeId(int32 Reason);

	// Закрыть попап (та же ветка, что и клик по 8003 в порту — без завершения процесса).
	UFUNCTION(BlueprintCallable, Category="Asda|Разрыв")
	void ClosePopup();

	UFUNCTION(BlueprintPure, Category="Asda|Разрыв")
	bool IsOpen() const;

	// ★ 19.10.2026 (кик оператором 6019): текст СЛЕДУЮЩЕГО попапа обрыва вместо таблицы причин — эталон зовёт
	//   `Show_DisconnectPopup(2113)` сам, до закрытия сокета. Сбрасывается после показа. 0 — не задано.
	void SetNextNotice(int32 NoticeId) { NextNoticeOverride = NoticeId; }
	// Заданный текст следующего попапа (0 — нет). Самотест: после кика он не должен остаться висеть.
	int32 GetNextNotice() const { return NextNoticeOverride; }

	// Идентификатор окна в реестре `UAsdaWindowSubsystem` (он же имя окна .JUI).
	static const FName kWindowId;

private:
	// Подписчики сети: игровое соединение потеряно / кик на логине.
	void OnGameDisconnected();
	void OnLoginKick(int32 Code, bool bClosed);

	// Клик по 8003 `COMMONWIN_BUT`. Порт `CommonWin_CommonEventFunc@0xC6EAE0` с отступлением из шапки.
	void OnOkClicked();

	// Какой ассет попапа брать: в игре — `gamemain`-копия (окно уже в наборе), иначе — копия класса
	//   `commonwin` (эталон догружает `./JUIData/CommonWin.JUI`). Развилка пункта 2/4 шапки.
	const TCHAR* PickWidgetPath() const;

	FString Loc(int32 Id) const;

	// Общий показ попапа с готовым текстом. bWrapToSlot — разбить по словам под ширину слота COMMONWIN_TEXT
	//   (только для текстов EAC; нотисы оригинала несут свои переносы и идут как есть). LogId — номер нотиса для журнала.
	UUserWidget* ShowPopupWithText(FString Message, bool bWrapToSlot, int32 LogId);

	// Разбить абзацы текста на строки по словам так, чтобы каждая строка уместилась в WrapW (замер шрифтом Font).
	//   Слово длиннее строки остаётся целым. Без Slate (самотест/коммандлет) — текст как есть.
	static FString WrapWordsToWidth(const FString& Text, const struct FSlateFontInfo& Font, float WrapW);

	// Мостик клика (динамический делегат Slate ссылается на приёмник СЛАБО — держим ссылку).
	UPROPERTY(Transient) TObjectPtr<UObject> OkRelay = nullptr;

	FDelegateHandle HGameDisconnected;
	FDelegateHandle HLoginKick;
	int32 NextNoticeOverride = 0;   // ★ 19.10.2026: текст следующего попапа (кик 6019 → 2113), 0 — таблица причин
};
