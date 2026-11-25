// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ ОБЩАЯ ТОЧКА «СОСТОЯНИЕ ТОРГОВЛИ» — признаки открытых окон обмена и личной лавки (29.09.2026, волна 0 торговли).
//
// ЗАЧЕМ. Эталон держит три признака, которые читают десятки чужих гейтов:
//   • `CExchange+0x2C` (глобал 0x2BF965C, МИСНЕЙМ `g_bPartyInviteFlag`) — «ОКНО ОБМЕНА `TRAN_WIN` 2008 ОТКРЫТО».
//     Писатели: `uiWindow_OpenWithGuards` → 1 (`mov [eax+0x2C],1` @0xC75A72, ui_core.c:35205), `uiInventoryDialogReset` → 0
//     (ui_core.c:35233), `invItemListDialogInit` → 0 (item_inventory.c:23582), `gameLeaveToCharSelect` → 0 (@0xED2E46).
//     Читателей 20 функций (скан .text: 23 `cmp` + 1 запись) — таблица `Reports/trade/A_exchange_client.md` §9.
//     ★ Прежний вывод «флаг мёртв» (реестр №37①/№41а) — ложное «нет»: запись идёт через `this`, а не по абсолютному адресу.
//   • `CSellWindow+0x300` (`m_bDlgVisible`, геттер `sub_934E86`) — «окно подготовки лавки 22000 открыто».
//   • `CShopDlg+0x168` (геттер `sub_929DC4(g_bSomeFeatureEnabled)`, МИСНЕЙМ) — «витрина лавки 23000 открыта».
//   Условие 107022 `communityUI_Open` (social.c:3780) = `sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled)`.
//
// ПОЧЕМУ ОТДЕЛЬНАЯ ПОДСИСТЕМА. Окон 2008/22000/23000 в порту ещё нет (волны О1…О4 и Л1…Л5 плана
//   `Reports/trade/trade_re_report.md` §5), а читатели уже есть — и до 29.09 двое из них (ход клавишами 9568,
//   ремесло 5590) подставляли вместо «окно открыто» флаг «заявка в полёте» (ПД-2), а сообщество блокировалось
//   аукционом вместо лавки (ПД-1). Читатели обязаны спрашивать ОДНУ точку; пишут её будущие подсистемы окон.
//   [ПОРТ] Пока окон нет, все признаки = false (писателей нет), и гейты-читатели пропускают — как у эталона без окна.
//
// СБРОС. [ПОРТ] Эталон при разрыве закрывает клиент (признаки умирают с процессом); выход в выбор персонажа пишет 0
//   (`gameLeaveToCharSelect`). Порт сбрасывает все три признака при ЛЮБОМ сбросе сессии, включая смену карты: игровое
//   соединение пересоздаётся, а сервер при разрыве сам снимает обмен и лавку (`DisConnectUser` S:111411-111420).
//
// Отладка (не Shipping): `asda.Trade.State` — печать признаков; `asda.Trade.SelfTestGates` — самотест читателей
//   (`AsdaTradeGatesSelfTest.cpp`).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс признаков вместе с сессией
#include "AsdaTradeStateSubsystem.generated.h"

UCLASS()
class ASDAREBORN_API UAsdaTradeStateSubsystem : public UGameInstanceSubsystem, public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── IAsdaSessionResettable: все признаки гаснут при любом сбросе (см. шапку, раздел «СБРОС»).
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ═══ Статические чтения для гейтов (контекст — мир, GameInstance или любая его подсистема; нет GI → false) ═══

	// [ОРИГИНАЛ] `CExchange+0x2C` (МИСНЕЙМ `g_bPartyInviteFlag`): окно обмена `TRAN_WIN` 2008 открыто.
	//   ★ НЕ «заявка в полёте»: у заявки свой признак `CExchange+0x14` (гейт 5054) — `UAsdaExchangeP2PSubsystem::IsRequestActive`.
	//   ★ Писатель (Т0-1, волна О1): ТОЛЬКО `UAsdaExchangeP2PSubsystem` (`uiWindow_OpenWithGuards` → 1, сбросы → 0) и самотесты.
	static bool IsExchangeWindowOpen(const UObject* Ctx);
	// [ОРИГИНАЛ] `CSellWindow+0x300` (`sub_934E86`): окно подготовки личной лавки 22000 открыто.
	static bool IsSellWindowOpen(const UObject* Ctx);
	// [ОРИГИНАЛ] `CShopDlg+0x168` (`sub_929DC4`): витрина личной лавки 23000 открыта (у хозяина и у посетителя).
	static bool IsShopDlgOpen(const UObject* Ctx);
	// [ОРИГИНАЛ] Условие 107022 эталона: `sub_934E86(g_pSellWindow) || sub_929DC4(g_bSomeFeatureEnabled)` — «лавка открыта».
	static bool IsSelfStoreOpen(const UObject* Ctx);

	// ★ Волна Л5 торговли (30.09.2026, `Reports/trade/wave_L45.md`): [ОРИГИНАЛ] общий канал отказов читателей «лавка открыта» —
	//   `UI_FormatLocalizedNotice(g_pLocNoticeCtx, loc, 0, 1)` → `CChat_AddTokenizedLines(g_pChatWnd, текст, −11514, 10000, 0)`: строка
	//   системного чата цветом 0xFFFFD306 на 10 с (одинаково у всех читателей B §6: 107022, 107011, 107012, 13106). `Who` — эталонная
	//   функция-читатель (для лога и самотеста). Возврат: строка выведена.
	static bool ChatStoreGate(const UObject* Ctx, int32 LocId, const TCHAR* Who);
	// [ИСПРАВЛЕНИЕ] К-17 (волна Л5): текст отказа рюкзака при открытой витрине 23000. Оригинал (`uiOpenInventoryWindow`, ui_core.c:46616 →
	//   LABEL_10) пишет 13106 «Ничего не выйдет, пока идет работа с бустером.» — текст бустера; ключ `asda.Trade.StoreFixK17` 1 → 107022
	//   (строка всех прочих читателей «лавка открыта»), 0 → 13106 (оригинал).
	static int32 InventoryShowcaseRefusalLoc();
	// Самотест Л5: сколько отказов прошло через `ChatStoreGate`, последний loc и читатель.
	int32 GetStoreGateSeq() const { return StoreGateSeq; }
	int32 GetLastStoreGateLoc() const { return LastStoreGateLoc; }
	const FString& GetLastStoreGateWho() const { return LastStoreGateWho; }

	// ═══ Запись — ТОЛЬКО владельцы окон (будущие подсистемы 2008/22000/23000) и самотест ═══

	// Окно обмена 2008 показано (1) / скрыто (0). `Why` — источник для лога (эталонная функция-писатель).
	void SetExchangeWindowOpen(bool bOpen, const TCHAR* Why);
	// Окно подготовки лавки 22000 показано / скрыто (`sellWindow_Open` / закрытие).
	void SetSellWindowOpen(bool bOpen, const TCHAR* Why);
	// Витрина лавки 23000 показана / скрыта.
	void SetShopDlgOpen(bool bOpen, const TCHAR* Why);

	// [ОРИГИНАЛ] Хвост `uiCloseAllWindowsOnLeave@0xD62790` (ui_core.c:54726-54730): при открытом окне обмена —
	//   `CExchange_SendWindowCancel` + `uiInventoryDialogReset` (признак → 0).
	//   [ПОРТ] С волны О1 (29.09.2026) — форвард в `UAsdaExchangeP2PSubsystem::OnCloseAllWindowsOnLeave`: 5286(windowIndex)
	//   вместо 5284/0 (дефект Д1 не повторяется) + `uiInventoryDialogReset`. Возврат: было ли окно открыто.
	bool OnCloseAllWindowsOnLeave(const TCHAR* Why);

	// Сводка признаков одной строкой (лог / `asda.Trade.State`).
	FString DebugString() const;

private:
	// Экземпляр подсистемы из контекста (мир / GI / подсистема GI); nullptr — нет GameInstance.
	static const UAsdaTradeStateSubsystem* Get(const UObject* Ctx);

	bool bExchangeWindowOpen = false;   // `CExchange+0x2C` — окно обмена 2008 открыто
	bool bSellWindowOpen     = false;   // `CSellWindow+0x300` — окно подготовки лавки 22000 открыто
	bool bShopDlgOpen        = false;   // `CShopDlg+0x168` — витрина лавки 23000 открыта
	int32 StoreGateSeq       = 0;       // число отказов «лавка открыта», прошедших через `ChatStoreGate` (самотест Л5)
	int32 LastStoreGateLoc   = 0;       // loc последнего такого отказа
	FString LastStoreGateWho;           // эталонная функция-читатель последнего отказа
};
