// Copyright (c) 2026 @sitxovski (Discord)
// ★★★ СПРАВОЧНЫЕ ПОДСКАЗКИ КНОПОК (30.10.2026) — порт тем `UI_BuildHelpTooltipText@0xEB11A0` (ui_core.c:83219). [ОРИГИНАЛ]
//   У оригинала одна функция строит подсказку «по теме» (число 0…35): заголовок и тело — строки таблицы нотисов,
//   плашка — общая `g_ItemTooltip`. Темы вызывают оконные процедуры HUD на событии 8 (наведение): панель быстрого доступа
//   (0 — пустой слот оружия, 1 — кнопки листания страниц), мини-карта, отряд, чат и др. Здесь — ОДНА точка «тема → тексты»
//   для новых мест (закон `port-shared-points`); прежние локальные таблицы соседей (поиск отряда — темы 32…35, панель
//   Друга — 16/17/23, системное меню — 30) не трогаем.
//   Гашение — владельцем показа (`EAsdaTipOwner`, закон `ui-shared-tip-ownership`).
#pragma once
#include "CoreMinimal.h"

class UGameInstance;
enum class EAsdaTipOwner : uint8;

namespace AsdaHelpTopics
{
	// Тексты темы `Topic` на языке сессии: заголовок и тело (строки тела через «\n», как `sprintf("%s\n%s")` эталона).
	//   false — тема в порт не перенесена (эталон по неизвестной теме не показывает ничего).
	ASDAREBORN_API bool Build(const UGameInstance* GI, int32 Topic, FString& OutTitle, FString& OutBody);

	// Показать тему на общей плашке у курсора от имени владельца `Owner` (`UAsdaMiniStateSubsystem::ShowSharedTip`;
	//   позиция — эталонная `Tooltip_LayoutNearCursor`, +32/+32 с прижатием к экрану). false — не показана.
	ASDAREBORN_API bool Show(UGameInstance* GI, int32 Topic, EAsdaTipOwner Owner);

	// Показать подсказку по паре строк (заголовок, тело) — эталонные `tutorialPopupLayoutSmall` / `CControl_SetState(…, 8, id)`,
	//   где тема не нужна. Пустые id (≤ 0) пропускаются. false — показывать нечего.
	ASDAREBORN_API bool ShowText(UGameInstance* GI, int32 TitleLoc, int32 BodyLoc, EAsdaTipOwner Owner);
}

// ★ НАВЕДЕНИЕ НА КНОПКИ ОКНА СО СПРАВКОЙ — общая точка для окон, которые опрашивают курсор каждый кадр (мини-карта, отряд, чат).
//   Эталон: смена контрола под курсором = событие 0xA прежнему (`CControl_KillFocus(&g_ItemTooltip)`) и 8 новому (показ);
//   9 — плашка за курсором (её ведёт `UAsdaItemTooltipSubsystem::Tick`). Гейт события 8 `!helper_getField1(g_pUIManager)` —
//   пока предмет «в руке» (`FAsdaDragGhost::IsAnyActive`), новой справки нет.
struct ASDAREBORN_API FAsdaHelpTipHover
{
	int32 ShownKey = -1;   // ключ контрола, чья подсказка сейчас показана этим окном (−1 — ничего)
	int32 HoverKey = -1;   // ключ контрола под курсором в прошлом кадре (−1 — ничего)

	// Кадровое обновление. `Key` — контрол под курсором (−1 — ничего), `TitleLoc`/`BodyLoc` или `Topic` — его подсказка.
	//   Подсказка показывается только на СМЕНЕ контрола (как событие 8), гаснет на уходе (0xA).
	void UpdateTopic(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, int32 Topic);
	void UpdateText(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, int32 TitleLoc, int32 BodyLoc);
	// Общий шаг с произвольным показом (`Show` строит плашку сам, true — показана). `bDragGate` — гейт эталона
	//   `!helper_getField1` (у панели отряда его нет: `partyWnd_onCommand` событие 8 показывает и при захвате).
	void Update(UGameInstance* GI, EAsdaTipOwner Owner, int32 Key, TFunctionRef<bool()> Show, bool bDragGate = true);
	// Погасить свою подсказку и забыть наведение (окно закрылось, ранний выход тика, сброс сессии).
	void Hide(UGameInstance* GI, EAsdaTipOwner Owner);
};
