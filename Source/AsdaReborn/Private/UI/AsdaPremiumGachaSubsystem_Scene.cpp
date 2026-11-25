// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPremiumGachaSubsystem_Scene.cpp — ★ R2 10.11.2026 (разрез AsdaPremiumGachaSubsystem.cpp, дословный перенос): сцена: открыть/закрыть, фазы, Tick, приз, подтверждение, сетка призов, подсказки.
//   Общее для частей — в AsdaPremiumGachaSubsystemInternal.h; шапка и реверс — в AsdaPremiumGachaSubsystem.cpp.
#include "UI/AsdaPremiumGachaSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaPremiumPostSubsystem.h"   // отложенный показ приза: сообщаем ящику итог выбора
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaGachaSceneActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"                          // TActorIterator — поиск актора сцены для диагностики
#include "Kismet/KismetRenderingLibrary.h"   // ★ 24.09: выгрузка буфера сцены гачи в PNG (asda.Gacha.DumpRt)
#include "Components/Image.h"
#include "Engine/World.h"
#include "AsdaNetSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Items/AsdaItemDbRows.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Resources/AsdaSoundSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaAnimatedPlane.h"      // 68058 `Apple_Make3` — кадровый анимплан (RestartAnim = `CControl_Show`)
#include "UI/AsdaItemTooltipSubsystem.h" // общая плашка подсказки + владелец показа
#include "UI/AsdaInventorySubsystem.h"   // ★ общая точка модели контейнеров: ApplyExternalRecord (6582)
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"          // ★ 07.11.2026: окно-сообщение ONEBUT_MSGBOX (`CItem_BuildTooltipText(…, 0, -1, …)`)
#include "UI/AsdaUiHelpers.h"
#include "Components/AudioComponent.h"
#include "HAL/IConsoleManager.h"   // ★ 08.11.2026: ключ asda.Gacha.IdleLoop2D
#include "UI/AsdaJuiComboBox.h"   // ★ ОБЩАЯ точка: выпадающий список скина Bk_ComboBox
#include "UI/AsdaJuiWindowMeta.h"   // мета окон: ручка перетаскивания живёт в данных
#include "Framework/Application/SlateApplication.h"   // курсор для зоны колеса над окном 68310
#include "GameFramework/PlayerController.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaCoord.h"   // ★ единый перевод UE→игра: тайл входа в сцену (шаг 9)
#include "UI/AsdaCraftSubsystem.h"         // ★ гейт входа: открыты ли окна ремесла 76000/77500 (волна В2)
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "UI/AsdaItemRecordWriter.h"   // ★ U17 (В1 дедупа №2): синтетическая запись предмета 57 б
#include "AsdaPremiumGachaSubsystemInternal.h"   // ★ R2 10.11.2026: общее для частей AsdaPremiumGachaSubsystem*.cpp

// ★ 08.11.2026 (пачка 19): полная остановка своего персонажа из клика UI (World/AsdaUiActionMoveHold.cpp, ключ `asda.Move.UiActionStop`).
void AsdaFullStopSelfForUiAction(class UGameInstance* GI, const TCHAR* Why);

namespace {
	// ★★★ ДЛИТЕЛЬНОСТИ ФАЗ — ЗАПАСНОЙ ВАРИАНТ, А НЕ ИСТОЧНИК ПРАВДЫ.
	//   В эталоне фаза кончается НЕ по таймеру, а по `Game_IsPausedOrMenu@0x99ED60`
	//   (`slot[13] >= slot[11]` — «время клипа догнало его длительность»), причём скорость ровно 1.0
	//   (`animSkeletonAdvanceTimeMtx@0x112AB50`: `time += dt`, без множителя). Поэтому длительность
	//   берётся ИЗ САМОГО КЛИПА (`AAsdaGachaSceneActor::GetClipLength`), а эти числа работают,
	//   только если 3D-актора нет (сцена без моделей). Значения — из заголовка `.jbb` (@0x30, мс).
	constexpr float kIdleLen    = 2.000f;   // anim_01 = 2000.0 мс

	constexpr float kDispenseLen= 2.333f;   // anim_03 = 2333.33 мс
	constexpr float kOpenLen    = 2.333f;   // anim_04 = 2333.33 мс

	// Цена по умолчанию, если `premium_gatya_data.bin` недоступна. 690 подтверждено трижды:
	// таблицей, байтами запроса к GP7 (`payload[104]=B2020000`) и живым логом `price=690`.
	constexpr int32 kFallbackPrice = 690;

	const FName kSndRoll  = TEXT("S_e_sm_ro_start_00");   // ← S_e_pr_gc_rol_00.wav
	const FName kSndClick = TEXT("S_e_button06");         // ← S_e_pr_gc_click_00.wav
	const FName kSndOpen  = TEXT("S_e_sm_ro_item_00");    // ← S_e_pr_gc_open_00.wav

	// Геометрия колёсного окна 68310 `Premium_Pick_View_ScBar` в базе 1024×768 (`gamemain_jui.ini`):
	//   abs (328, 204), размер 334×334. Полоса вешается по ПРАВОМУ краю окна
	//   (`uiControl_AttachVertScrollbar@0x12F3160` читает у скина `WheelWin_Nomal` ключ
	//   `ScrollBarFileNameVert = "Nomal_Scr"`), поэтому X = 328 + 334.
	constexpr float kWheelAbsX = 328.f;
	constexpr float kWheelAbsY = 204.f;
	constexpr float kWheelW    = 334.f;
	constexpr float kWheelH    = 334.f;
	// Ячейка 68311 `NoImage_icon` — abs (331, 207), 64×64 (`gamemain_jui.ini`, §2.5 отчёта реверса).
	//   Через неё привязываемся к ЖИВОЙ геометрии окна: и полоса, и зона колеса считаются как
	//   «позиция ячейки + разность абсолютов», то есть переживают перетаскивание окна.
	constexpr float kCell0AbsX = 331.f;
	constexpr float kCell0AbsY = 207.f;

	// ★ Волна 14: видимых рядов витрины = первый аргумент эталонного `uiControlSetScrollRange(68310, 5, …)`
	//   (ui_core.c:7351) = 25 ячеек / 5 записей в ряду.
	constexpr int32 kGridVisibleRows = kGridCellCount / kGridPageStep;

	// Локализация (все id подтверждены в `GameText.csv`, см. `Reports/gacha_ui_re.md` §5).
	// ★ 110621 — ФОРМАТ-СТРОКА printf (в данных: «…[%s]…»), эталон кладёт её в `sprintf`
	//   (`uiItemTooltipShow@0xB70A60`, ui_core.c:7729-7731). Подставлять ТОЛЬКО через
	//   `UAsdaTextSubsystem::FormatPrintfById`, а не через слоты `[ssN]` (см. `FillPrizeBox`).
	constexpr int32 kLocPrizeCongrats = 110621;  // «Поздравляем. Твой приз: [%s].»

	constexpr int32 kLocBattleNoShop  = 6813;    // «В бою нельзя пользоваться элитным магазином»

	constexpr int32 kLocPrizeLock     = 991202;  // ★ 07.11.2026 строка порта: «сначала забери приз» (ведомости gametext_overrides + loc/en)
	// ★ 07.11.2026: сколько ждать 6578, прежде чем снова разрешить закрыть сцену (закон «молчание сервера = тупик»): ответ мог потеряться
	constexpr double kRollReplyHoldSec = 15.0;
}

// ★★★ 05.11.2026 (баг игроков «щелчок по соседнему автомату во время розыгрыша засчитывается соседнему»). [ОРИГИНАЛ]
//   Эталон меняет выбранный автомат (`CGachaScene+648` = dword 162) ТОЛЬКО за гейтом — `GachaScene_OnControlMessage@0xB704C0`
//   (ui_core.c:7596-7620) → `uiTradeConfirmMoney@0xB70420` / `openItemGridIfReady@0xB70470` (ui_core.c:7517-7544):
//     • кнопка «Играть» 68061+i: ни один автомат не в клипе ≥ 2 (`sub_92839D(k)` = `sub_B6ACB0` = клип ребёнка, +620),
//       сам автомат i в клипе 1, и НЕТ запроса в полёте (`+644`, антидубль 6577);
//     • кнопка «Основные предметы» 68011+10i: ни один автомат не в клипе ≥ 2 и нет запроса в полёте;
//       витрина при этом закрывает диалог оплаты 68100 (`uiOpenItemGridWindow@0xB6EDE0` → `sub_92387F`).
//   Ответ 6578 номера автомата НЕ несёт — клип прокрутки запускается на ТЕКУЩЕМ выбранном (`thisSelf[162]`, ui_core.c:7442).
//   Порт менял выбор ДО гейта (`SelectSlot(i); RequestRoll();`, а `OpenPrizeGrid` не смотрел на запрос в полёте и не
//   закрывал диалог) — щелчок по соседу, пока ждём 6578 или крутится барабан, переносил прокрутку, шар и вскрытие на
//   соседний автомат: игрок видел приз «не с того автомата», а щёлкая по своему — «оба автомата заглохли».
//   1 — гейт эталона; 0 — прежнее поведение порта.
static ASDA_DEV_CVAR(int32, CVarGachaSlotSwitchGate,
	TEXT("asda.Gacha.SlotSwitchGate"), 1,
	TEXT("[ОРИГИНАЛ] Смена автомата премиум-гачи только при отсутствии запроса 6577 в полёте и при всех автоматах в покое ")
	TEXT("(uiTradeConfirmMoney/openItemGridIfReady); витрина закрывает диалог оплаты. 0 — прежнее поведение порта."),
	ECVF_Default);

// Читатель ключа выше (закон «имя ключа не в рабочем коде вне журналов»: только через аксессор).
static bool AsdaGachaSlotSwitchGateOn() { return CVarGachaSlotSwitchGate.GetValueOnGameThread() != 0; }

// ───────────────────────────── открытие / закрытие ─────────────────────────────
bool UAsdaPremiumGachaSubsystem::OpenScene()
{
	if (bSceneOpen) { return true; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }

	// ═══════════ ШАГ 2 ЭТАЛОНА: ТРИ ВХОДНЫХ УСЛОВИЯ ═══════════
	//   `if (*(корень68000 + 292) != 1 && !tileInteract_isControlVisible(g_pItemMainWnd)
	//        && !isBoundControlVisible(g_pItemQtyMakeDlg))`
	//   (1) корень 68000 УЖЕ виден — сцена открыта, повторный вход запрещён;
	if (WM->IsWindowVisible(FName(kRoot.Id)))
	{
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] вход отклонён: окно 68000 уже видимо"));
		return false;
	}
	//   (2) `g_pItemMainWnd` (класс `CItemMainWnd`) и (3) `g_pItemQtyMakeDlg` (`CItemQtyMakeDlg`,
	//       тикается `upgradeAnimDlg_tick`, game_main.c:1531) — окна СОЗДАНИЯ СНАРЯЖЕНИЯ (ремесло): 76000
	//       `ITEM_making_WIN` и окно изготовления 77500 `ITEM_making_Mark_WIN`. Оба есть в порту (ремесло, волны В2/В4,
	//       25–26.09.2026); условия (2) и (3) — одна общая точка ремесла `UAsdaCraftSubsystem::IsAnyCraftWindowOpen`.
	//       Отказ молчаливый (как эталон). ★ Правка комментария 26.09.2026 (ревизия ремесла В7 МЕЛК-9, волна В8):
	//       прежний текст «окон в порту НЕТ» устарел.
	if (UAsdaCraftSubsystem::IsAnyCraftWindowOpen(GetGameInstance()))
	{
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] вход отклонён: открыто окно ремесла 76000/77500"));
		return false;
	}

	// ═══════════ ШАГ 5: `uiCloseAllWindows(g_pUIManager)` (world_map.c:4702) ═══════════
	//   ★★★ ИСПРАВЛЕНО 2026-08-19. Здесь стоял `WM->CloseAll()` — «снять с вьюпорта и выбросить из
	//     реестра ВСЁ». Это НЕ эталон и давало сразу две противоположные поломки:
	//       • подсистемы со своим кэшем виджетов переставали рисовать (тултипы во всей игре; список
	//         каналов мини-карты вместе с картой мира, редактором вкладок чата, палитрой, окном
	//         гнезда самоцвета и GM-панелью) — `Reports/minimap_channels_fix.md`;
	//       • ИНВЕНТАРЬ, наоборот, ОСТАВАЛСЯ НА ЭКРАНЕ: `CloseAll` не трогает его флаг `bWindowOpen`,
	//         и `UAsdaInventorySubsystem::Tick` → `EnsureWidget` создавал окно заново тем же кадром
	//         (живой лог владельца 2026-08-18 22:18:55, кадр [22] — «сцена открыта» и через 73 мс
	//         «окно INVEN_WIN открыто»). Разбор — `Reports/gacha_close_windows.md`.
	//     Эталонный `uiCloseAllWindows@0xD61960` — ЯВНЫЙ СПИСОК окон, у каждого из которых зовут
	//     ЕГО СОБСТВЕННОЕ закрытие (у инвентаря — `CInventoryWindow::Close`, мисномер
	//     `sowelWindowClose@0xCFF760`), поэтому флаги владельцев сбрасываются и окна не всплывают.
	//     В порту это `CloseAllPlayerWindows` — росписи участников подсистемы дают сами.
	WM->CloseAllPlayerWindows();
	//   Отдельное `uiWindowClose(g_pCharaInfoWin)` (world_map.c:4703-4704) — окно персонажа 900
	//   `CHARAINFO_WIN`. ПОДСИСТЕМЫ ЭТОГО ОКНА В ПОРТУ НЕТ (в реестре меню оно объявлено как цель
	//   кнопки 73401, но ни один `OpenWindowByPath` его не открывает). Как только появится — она
	//   расписывается в `RegisterWindowCloser`, и эта строка закроется сама, без правки здесь.
	//   Запись в реестре отложенного: `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`.

	// ★ Окна САМОЙ СЦЕНЫ в списке эталона не значатся и `CloseAllPlayerWindows` их не трогает,
	//   поэтому порядок «сначала закрыть, потом поднять свои» безопасен: `EnsureWindows` найдёт
	//   уже живой корень и переиспользует семёрку, а при первом входе — создаст.
	if (!EnsureWindows()) { return false; }

	// ═══════════ ШАГИ 6-9: персонаж «встаёт» перед входом в сцену ═══════════
	UWorld* World = GetWorld();
	AAsdaPlayerController* PC = World ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (PC)
	{
		// ★ 08.11.2026 [ОРИГИНАЛ] (пачка 19): шаги 6-7 эталона снимают и автопуть, и сам ход (выход из состояния 1) — порт ставил только
		//   клип покоя. Ключ `asda.Move.UiActionStop`.
		AsdaFullStopSelfForUiAction(GetGameInstance(), TEXT("вход в сцену гачи"));
		// Шаг 6 — `followTargetCancelReset(&g_bMapTransferActive)`: снять преследование цели.
		//   ★ Гейт-агент гасит ВВОД (клик-мув, WASD, раскладка) по `IsSceneOpen()`, но уже взведённый
		//     маршрут и авто-атака этим не снимаются — их снимает именно вход в сцену. Дубля нет.
		PC->CancelRoute();
		PC->ClearTarget();
		PC->bAutoAttack = false;
	}
	// Шаг 7 — `CharFx_AutoSabzil_State(LocalPlayer, …, 0, 0, 0)`: действие персонажа сбрасывается в 0.
	if (PC)
	{
		if (AAsdaPlayerCharacter* Pawn = Cast<AAsdaPlayerCharacter>(PC->GetPawn()))
		{
			if (!Pawn->IsIncapacitated())
			{
				// ★ 24.10.2026 (выход из сидения 1:1): `CharFx_AutoSabzil_State(…, 0)` переписывает 7/8/9 сразу — без «встаёт» и без 4024
				//   (прежде порт ставил хвост «встаёт» и по его концу код 0). Ключ `asda.Char.SitExitEtalon 0` — прежнее.
				Pawn->DropSitForEtalonState(/*bIdleClip*/ true, TEXT("гача: CharFx 0"));
				Pawn->SetAnimState(EAsdaPlayerState::Idle);
			}
		}
	}
	// Шаг 8 — `Net_SendActionPacket(...)` = опкод **4008 `OP_CHAR_STOP`**. Отправляет именно ГАЧА
	//   (world_map.c:4714); гейт-агент этого намеренно не делает, чтобы пакет не ушёл дважды.
	//   ★ `no-reentrant-net-callbacks`: `OpenScene` вызывается из клика UI (кнопка 73410) или из
	//     Blueprint — это НЕ обработчик пакета, поэтому отправка здесь синхронная и безопасная.
	//     Единственный сетевой вход в сцену — 6578/6582 — сцену не ОТКРЫВАЕТ.
	{
		const APawn* Self = PC ? PC->GetPawn() : nullptr;   // свой персонаж — чья позиция уходит в 4008
		// ★ 26.09.2026: раньше в 4008 и в тайл шли сырые `GetActorLocation().X/.Y` движка; эталон берёт
		//   `worldPos` персонажа — ИГРОВЫЕ координаты. Перевод — только общей точкой `AsdaCoord::UeToGame2D`.
		if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->SendStop4008AtActor(Self);   // гейт `bActionPending` — внутри, как в эталоне
		}
		// Шаг 9 — `g_nPlayerTileX = worldPosX / 100`, `g_nPlayerTileY = worldPosZ / 100` (world_map.c:4719-4720):
		//   тайл игрока на момент входа в сцену, по игровым X и Z.
		//   ⚠ Глобалов `g_nPlayerTileX/Y` в порту нет (их читает только логика перехода карт
		//     оригинала, которая портируется отдельно), поэтому значение держим у себя —
		//     оно понадобится, когда появится их потребитель.
		const FVector2D G = Self ? AsdaCoord::UeToGame2D(Self->GetActorLocation()) : FVector2D::ZeroVector;
		SceneEnterTileX = FMath::TruncToInt(G.X / AsdaCoord::kTileUnits);
		SceneEnterTileY = FMath::TruncToInt(G.Y / AsdaCoord::kTileUnits);
	}

	// Порядок ниже — построчный порт `premiumGacha_BeginRoll@0xB6DF80` (world_map.c:4671-4756).
	bRequestInFlight = false;                       // шаг 3: gap_10[628] = 0
	StartIdleLoop();                                // шаг 4: петля покоя (замена звука, см. шапку)
	WM->SetWindowVisible(FName(kRoot.Id), true);    // шаг 10
	WM->SetWindowVisible(FName(kExit.Id), true);
	// [ОРИГИНАЛ] world_map.c:4724 (0xB6E10D): `sub_929A2C(gap_10[556] = 68075, 1)` — кнопка дня получает AlwaysTop ЗДЕСЬ, в начале
	//   прокрута (до первого прокрута действует значение `.JUI`). Ревизия подъёма окон, МЕЛК-Р2 (07.10.2026).
	WM->SetWindowAlwaysTop(FName(kExit.Id), true);
	// ★★★ 68200 `Premium_Pick_Click_Win` — панель «Сейчас GXP / Деньги в подарок / Пополнить».
	//   ПОКАЗ НАЙДЕН В ЭТАЛОНЕ (прежняя формулировка «ссылок на показ в срезе нет» была ложным
	//   «нет», закон `false-negatives-method`): последний шаг `premiumGacha_BeginRoll`
	//   (world_map.c:4751) зовёт `inventoryBag_Open((int *)v20->gap_0)` — мисномер IDA, а внутри
	//   (item_inventory.c:5381-5385) `CControl_SetVisible(*(this + 109), 1)`, где dword 109 =
	//   (16 + 420)/4 — это КОНТРОЛ 68200, плюс `inventoryBag_RefreshGoldUi` (значения 68202/68204).
	//   Симметрично `inventoryBag_Close` = `uiGuildPanelClose` + `SetVisible(68200, 0)`.
	//   ⇒ панель открывается ВМЕСТЕ со сценой по оригиналу, помечать это как правку дефекта не нужно.
	WM->SetWindowVisible(FName(kClick.Id), true);
	RefreshClickWinTexts();                         // порт `inventoryBag_RefreshGoldUi@0xB6DE60`
	SetVis(HintPlane, false);                       // шаг 12: `SetVisible(68055, 0)` (world_map.c:4732)
	SetVis(BallBang, false);                        // шаг 15: вспышка погашена
	SetVis(Ball, false);                            // шар появится только на фазе AwaitClick
	SetVis(BallHit, false);                         // вместе с шаром гаснет и его клик-зона

	GridScrollRow = 0;
	GridSelectedCell = -1;
	GridMouse.Reset();
	TipHoverCell = -1;
	PopulatePrizeGrid();                            // витрина призов (окно 68300)
	EnsureSceneActor();                           // шаг 16: 3D-автоматы + камеры в render-target
	// ★★★ ШАГИ 11 и 13 `premiumGacha_BeginRoll@0xB6DF80` (world_map.c:4725-4736): подложка 68059 и
	//   контейнер 3D 68056 получают размер БЭКБУФЕРА. Без этого оба остаются 1024×768 из `.JUI` и
	//   вся сцена рисуется прямоугольником в левом верхнем углу экрана.
	ApplyFullScreenPanels();
	LayoutByProjection();                           // раскладка полос ДО первого кадра (эталон зовёт её тут же)
	bSceneOpen = true;                              // шаг 14: gap_10[629] = 1 ⇒ гейт истинен
	EnterPhase(EAsdaGachaPhase::Idle);
	RequestBalance();                               // шаг 18: 5466
	ResumePrizeIfAny();                             // ★ 07.11.2026: незабранный приз — автомат снова выдаёт шар

	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] сцена открыта, слот=%d"), SelectedSlot);
	return true;
}

void UAsdaPremiumGachaSubsystem::CloseScene()
{
	if (!bSceneOpen) { return; }
	StashPrizeForReopen();   // ★ 07.11.2026: сюда доходит и вынужденное закрытие (смерть, смена карты) — шар вернётся при открытии
	StopIdleLoop();   // порт `sub_9349C7(g_pSoundMgr, "S_e_pr_gc_idle_00.wav")` (ui_core.c:7188)
	GridScrollBar.Hide();
	GridMouse.Reset();
	// Эталон при закрытии сцены гасит общую плашку: `CControl_KillFocus(&g_ItemTooltip)`
	//   (`uiGuildPanelClose@0xB6E920`, ui_core.c:7196). ★ Гасим ТОЛЬКО свой показ — закон
	//   `ui-shared-tip-ownership` (безусловный Hide снёс бы чужую подсказку).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::PremiumGacha);
	}
	TipHoverCell = -1;
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WM)
	{
		// Порядок — порт `uiGuildPanelClose@0xB6E920` (ui_core.c:7183-7207).
		for (const FWin* W : { &kExit, &kRoot, &kClick, &kSelect, &kView, &kMsg, &kSel2 })
		{
			WM->SetWindowVisible(FName(W->Id), false);
		}
		// ★★★ ПАРНОЕ ВОССТАНОВЛЕНИЕ HUD — `uiExpandPartyPanel(g_pUIManager)` (ui_core.c:7195),
		//   предпоследняя содержательная строка `uiGuildPanelClose@0xB6E920`. Возвращает ровно три
		//   вещи: флаг «окна убраны» = 0, разблокировку хотбара, разворот и разрешение трея меню.
		//   ★ Окна, закрытые на входе (инвентарь, книга навыков, мини-карта), НЕ переоткрываются:
		//     снимок `uiCaptureOpenWindowsState` вход в сцену гачи не делает (world_map.c:4700-4702),
		//     в отличие от входа в магазин NPC (ui_core.c:61117) и продажи (item_inventory.c:19254).
		//     Асимметрия эталонная, а не упущение порта.
		WM->RestoreAfterSceneExit();
	}
	if (SceneActor)
	{
		// Порт `gachaSceneClearAll` (= containerClearAndDestroyAll(g_pGachaSceneCtrl)): при закрытии
		// сцены 3D-объекты сносятся целиком, а не прячутся — иначе капчер продолжает рисовать.
		SceneActor->Destroy();
		SceneActor = nullptr;
	}
	bSceneOpen = false;
	bRequestInFlight = false;
	PrizeItemCode = -1;
	SetSceneVariantBase(-1);   // эталонное `gap_254[288]` — сцена закрыта, варианты выбирать не для чего
	// ★ `bApplyPending` здесь НЕ сбрасывается. Ответ 6582 — это УЖЕ СОСТОЯВШИЙСЯ обмен на сервере:
	//   предмет в ячейке заменён, и модель инвентаря обязана это увидеть, даже если игрок закрыл
	//   сцену между отправкой 6581 и приходом ответа. Прежний сброс молча терял такой обмен —
	//   вторая половина того же дефекта, что и гейт `bSceneOpen` в `Tick` (см. там комментарий).
	//   Тело применится следующим же кадром: блок стоит выше гейта и от сцены не зависит.
	PrizeRecord.Reset();
	// Кисть контейнера держала пересозданный RT — при следующем открытии её собираем заново.
	SceneImage = nullptr;
	FullScreenApplied = FVector2D::ZeroVector;
	EnterPhase(EAsdaGachaPhase::Closed);
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] сцена закрыта"));
}

// ★ 07.11.2026: окно приза 68450 показано (фаза Prize бывает и без него — после окна вариантов 68500; такую сцену не держим).
bool UAsdaPremiumGachaSubsystem::IsPrizeBoxShown() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsWindowVisible(FName(kMsg.Id));
}

// ★ 07.11.2026: приз ещё не забран (описание — у объявления).
bool UAsdaPremiumGachaSubsystem::IsPrizePending() const
{
	if (!bSceneOpen) { return false; }
	if (bRequestInFlight && FPlatformTime::Seconds() - RollSentAt < kRollReplyHoldSec) { return true; }
	switch (Phase)
	{
	case EAsdaGachaPhase::Rolling:
	case EAsdaGachaPhase::Dispensing:
	case EAsdaGachaPhase::AwaitClick:
	case EAsdaGachaPhase::Opening:
		return true;
	case EAsdaGachaPhase::Prize:
		return IsPrizeBoxShown();
	default:
		return false;
	}
}

// ★ 07.11.2026: отказ закрыть сцену с модальным окном ONEBUT_MSGBOX (общая точка; пара (0, −1) сама отбрасывает дубль при повторном щелчке).
bool UAsdaPremiumGachaSubsystem::RefuseCloseForPrize()
{
	if (!AsdaGachaPrizeCloseLockOn() || !IsPrizePending()) { return false; }
	AsdaNotice::MsgBoxLoc(GetGameInstance(), EAsdaMsgCategory::Trade, kLocPrizeLock);
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] закрытие сцены запрещено: приз не забран (фаза %d, запрос в полёте %d)"),
		(int32)Phase, (int32)bRequestInFlight);
	return true;
}

// ★ 07.11.2026: закрытие по действию игрока — кнопки 68076/68209 и кнопка меню 73410.
void UAsdaPremiumGachaSubsystem::RequestCloseScene()
{
	if (RefuseCloseForPrize()) { return; }
	CloseScene();
}

// ★ 07.11.2026: запомнить приз, если сцену закрывают, а он не забран (смерть, смена карты, разрыв). Запись приза при закрытии стирается.
void UAsdaPremiumGachaSubsystem::StashPrizeForReopen()
{
	const bool bUnclaimed = Phase == EAsdaGachaPhase::Rolling || Phase == EAsdaGachaPhase::Dispensing || Phase == EAsdaGachaPhase::AwaitClick
		|| Phase == EAsdaGachaPhase::Opening || (Phase == EAsdaGachaPhase::Prize && IsPrizeBoxShown());
	if (!AsdaGachaPrizeCloseLockOn() || !bUnclaimed || PrizeItemCode <= 0) { return; }
	bResumePrize = true;
	ResumePrizeSlot = SelectedSlot;
	ResumePrizeCode = PrizeItemCode;
	ResumePrizeRecord = PrizeRecord;
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] сцена закрыта с незабранным призом %d (автомат %d, фаза %d) — шар вернётся при открытии"),
		PrizeItemCode, SelectedSlot + 1, (int32)Phase);
}

// ★ 07.11.2026: новое открытие сцены с незабранным призом — выбранный автомат снова отыгрывает выдачу (клип 3) и показывает шар.
void UAsdaPremiumGachaSubsystem::ResumePrizeIfAny()
{
	if (!bResumePrize) { return; }
	bResumePrize = false;
	SelectSlot(ResumePrizeSlot);
	PrizeItemCode = ResumePrizeCode;
	PrizeRecord = MoveTemp(ResumePrizeRecord);
	ResumePrizeRecord.Reset();
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] незабранный приз %d: автомат %d снова выдаёт шар"), PrizeItemCode, SelectedSlot + 1);
	EnterPhase(EAsdaGachaPhase::Dispensing);
}

void UAsdaPremiumGachaSubsystem::ToggleScene()
{
	if (bSceneOpen) { RequestCloseScene(); } else { OpenScene(); }   // ★ 07.11.2026: запрет до забора приза
}

// ───────────────────────────── фазы ─────────────────────────────
float UAsdaPremiumGachaSubsystem::ClipLen(int32 ClipSlot, float Fallback) const
{
	// ★ Длительность фазы = длительность КЛИПА выбранного автомата (эталон сравнивает
	//   время клипа с его собственной длительностью, `Game_IsPausedOrMenu@0x99ED60`).
	//   Запасное число нужно только если 3D-актора нет вовсе.
	if (SceneActor)
	{
		const float Len = SceneActor->GetClipLength(SelectedSlot, ClipSlot);
		if (Len > 0.f)
		{
			return Len;
		}
	}
	return Fallback;
}

float UAsdaPremiumGachaSubsystem::RollingLen() const
{
	return ClipLen(2, 2.000f);
}

void UAsdaPremiumGachaSubsystem::EnterPhase(EAsdaGachaPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
	// Клип автомата по фазе (слоты 1..4; 0 в оригинале не загружается — дефект оригинала).
	if (SceneActor)
	{
		switch (Phase)
		{
		case EAsdaGachaPhase::Idle:       SceneActor->PlayClipAll(1, /*loop*/ true);  break;
		// ★ Клип прокрутки ИГРАЕТСЯ ОДИН РАЗ: в эталоне петли нет — по концу клипа 2
		//   `gachaScene_UpdateChildAnims` переключает на клип 3 (ui_core.c:6641-6645).
		case EAsdaGachaPhase::Rolling:    SceneActor->PlayClip(SelectedSlot, 2, false); break;
		case EAsdaGachaPhase::Dispensing: SceneActor->PlayClip(SelectedSlot, 3, false); break;
		case EAsdaGachaPhase::Opening:    SceneActor->PlayClip(SelectedSlot, 4, false); break;
		default: break;
		}
	}
	switch (Phase)
	{
	case EAsdaGachaPhase::Rolling:
		// `GachaScene_OnServerResult`, ветка `payload[0]==1`: клип 2 + звук прокрута (ui_core.c:7442).
		PlayGachaSound(kSndRoll);
		break;
	case EAsdaGachaPhase::Dispensing:
		// `gachaScene_UpdateChildAnims`: «клип 2 доиграл → клип 3» + щелчок (ui_core.c:6642).
		PlayGachaSound(kSndClick);
		break;
	case EAsdaGachaPhase::AwaitClick:
		// ★★★ КЛИП 3 ДОИГРАЛ (`gachaScene_UpdateChildAnims@0xB6B0B0`, ui_core.c:6632-6636):
		//     g_bGachaRollActive = 0;
		//     CControl_SetVisible(g_pPauseMenuCtrl /* = ctrl 68055 */, 1);   ← ПОДСКАЗКА «нажми на шар»
		//     helper_setByte295(dword_2BDE140 /* = ctrl 68057 */, 0);        ← шару вернули мышь
		//   Именно этих двух строк в порту не было: подсказка не показывалась (симптом «нет текста»),
		//   а кликать по шару эталонный гейт `ui_core.c:7686` не разрешает, пока 68055 не видна.
		SetVis(Ball, true);            // зона 68057 — по ней кликают
		SetVis(BallHit, true);         // NoSelect = 0: мышь шару вернули
		SetVis(HintPlane, true);       // 68055 «нажми на шар»
		// Подсказка — ПЛАН, мышь она не ловит: её нижний край на 8 px заходит на верх зоны шара, и
		//   hit-testable спрайт отъедал бы у него клики (закон `ui-button-hover-click-rule`).
		if (HintPlane) { HintPlane->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		break;
	case EAsdaGachaPhase::Opening:
		// Клик по шару (`GachaScene_OnControlMessage`, msg 0xB, ui_core.c:7686-7699):
		//   гасим 68055, ставим клип 4, звук click + open, показываем вспышку 68058 и ЗАПУСКАЕМ
		//   её кадровую анимацию (`CControl_Show` = перезапуск прогона, `Apple_Make3` 16/1000 мс).
		SetVis(Ball, false);
		SetVis(BallHit, false);
		SetVis(HintPlane, false);      // `CControl_SetVisible(*(&this->gap_4[8]) = 68055, 0)`
		SetVis(BallBang, true);        // анимплан 68058 Apple_Make3, 16 кадров / 1000 мс
		if (UAsdaAnimatedPlane* Bang = Cast<UAsdaAnimatedPlane>(BallBang)) { Bang->RestartAnim(); }
		// Клик по шару: сначала click, затем open — ровно в этом порядке (ui_core.c:7692-7693).
		PlayGachaSound(kSndClick);
		PlayGachaSound(kSndOpen);
		break;
	case EAsdaGachaPhase::Prize:
		// Эталон гасит вспышку по её СОБСТВЕННОМУ событию 24 («прогон кадров закончен»,
		//   `GachaScene_OnControlMessage` msg 0x18, a2 == 68058 → `SetVisible(0)` + `CloseHide`);
		//   у нас прогон 1000 мс короче клипа 4 (2333 мс), поэтому к этому моменту он уже завершён.
		SetVis(BallBang, false);
		ShowPrizeBox();
		break;
	default:
		break;
	}
	UE_LOG(LogAsdaGacha, Verbose, TEXT("[премиум-гача] фаза → %d"), (int32)Phase);
}

void UAsdaPremiumGachaSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PremiumGacha");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	// ★★★ ОКНО 68500 ЖИВЁТ И ПРИ ЗАКРЫТОЙ СЦЕНЕ. На маршруте «из элитного ящика»
	//   (`OnPrizeTakenFromPost`) игрок в сцену гачи вообще не заходил, поэтому разбор мыши для его
	//   выпадающих списков обязан стоять ДО гейта `bSceneOpen` — иначе списки не раскрывались бы.
	TickVariantWindow();

	// ★★★ ПРИМЕНЕНИЕ 6582 ТОЖЕ ОБЯЗАНО СТОЯТЬ ДО ГЕЙТА `bSceneOpen` — И ПО ТОЙ ЖЕ ПРИЧИНЕ.
	//   Эталонный `uiGachaResultApply@0xB72440` — обработчик ПАКЕТА, а не метод сцены: он
	//   зарегистрирован в диспетчере (`network.c:926, case 0x19B6`) и вызывается независимо от
	//   того, показана сцена гачи или нет. На маршруте «из элитного ящика» игрок в сцену не
	//   заходил (`bSceneOpen == false`), поэтому прежний порядок «сначала `if (!bSceneOpen) return;`,
	//   потом `bApplyPending`» ГЛУШИЛ УСПЕШНЫЙ ОБМЕН ВАРИАНТА: сервер предмет менял, а модель
	//   инвентаря в клиенте оставалась со старым кодом — ровно баг владельца «мой выбор не несёт
	//   смысла» (доказательство — живая сессия, `Reports/elite_mailbox_fix2.md` §1.4).
	//   ★ Отложенность (закон `no-reentrant-net-callbacks`) сохранена: колбэк 6582 по-прежнему
	//     только кладёт тело в `PendingApplyBody`, а применяем мы его здесь, вне разбора пакета.
	if (bApplyPending)
	{
		bApplyPending = false;
		const TArray<uint8> Body = MoveTemp(PendingApplyBody);
		PendingApplyBody.Reset();
		ApplyPrizeResult(Body);
	}

	if (!bSceneOpen) { return; }

	// ═══════════ АВТО-ЗАКРЫТИЕ СЦЕНЫ С НОТИСОМ 6813 ═══════════
	//   Эталон: `PetWindow_RenderToTarget@0xB6D5B0` (npc_mob_pet.c:85-92) вызывается КАЖДЫЙ КАДР,
	//   пока гейт `sub_922D5D(g_pGachaScene)` истинен, и первым делом проверяет
	//   `objStateAllowsRender(LocalPlayer)` (мисномер: @0xA2A2F0 возвращает TRUE, когда номер
	//   состояния персонажа лежит в «боевом/занятом» наборе 1, 7, 8, 9, 33…47, 51, 60…67, либо
	//   состояние 0 с под-состоянием 10/20). В этом случае:
	//       uiGuildPanelClose(g_pGachaScene);                       // закрыть сцену
	//       CChat_AddTokenizedLines(..., loc 6813, -6881324, …);    // «В бою нельзя пользоваться
	//                                                               //  элитным магазином»
	//   ⚠ ЧЕСТНО О ПОКРЫТИИ. Порт не моделирует ВЕСЬ эталонный набор номеров состояний персонажа
	//     (`EAsdaPlayerState` = None/Idle/Move/Sit/Dead), поэтому «занят» здесь собирается из того,
	//     что порт реально знает: авто-атака, боевая стойка и недееспособность (смерть). Состояния
	//     33…47 / 51 / 60…67 (спец-действия) в порту пока не существуют — добавятся вместе с ними.
	if (UWorld* W = GetWorld())
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(W->GetFirstPlayerController()))
		{
			const AAsdaPlayerCharacter* Pawn = Cast<AAsdaPlayerCharacter>(PC->GetPawn());
			const bool bBusy = PC->bAutoAttack || PC->bInCombatStance || PC->bCombatStarted
				|| (Pawn && Pawn->IsIncapacitated());
			// ★ 07.11.2026: пока приз не забран, бой сцену не закрывает; смерть закрывает всегда (шар вернётся при открытии).
			const bool bHoldForPrize = AsdaGachaPrizeCloseLockOn() && IsPrizePending() && !(Pawn && Pawn->IsIncapacitated());
			if (bBusy && !bHoldForPrize)
			{
				CloseScene();
				if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
				{
					Msg->PostLoc(EAsdaMsgCategory::System, kLocBattleNoShop);
				}
				UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] сцена закрыта автоматически: игрок в бою (нотис %d)"), kLocBattleNoShop);
				return;
			}
		}
	}

	// (применение 6582 перенесено ВЫШЕ гейта `bSceneOpen` — см. комментарий в начале `Tick`)

	PhaseTime += DeltaTime;
	TickGridScrollBar();   // полоса 68310 живёт, только пока витрина 68300 показана
	TickTooltips();        // msg 8/0xA эталона: подсказка по ячейке витрины и по иконке приза

	// ★ Размер кадра в оригинале = размер бэкбуфера, а он меняется вместе с разрешением. Держим
	//   RT, подложку 68059 и контейнер 68056 в согласии с вьюпортом КАЖДЫЙ кадр (обе проверки
	//   выходят сразу, если размер не изменился).
	if (SceneActor && SceneActor->SyncRenderTargetToViewport())
	{
		ApplySceneBrush();   // RT и материал пересозданы — кисть контейнера переподставить
	}
	ApplyFullScreenPanels();

	LayoutByProjection();          // экранные позиции полос и шара — из 3D, каждый кадр

	switch (Phase)
	{
	case EAsdaGachaPhase::Idle:
		// Эталон: «клип не 3 и не 4 доиграл → снова клип 1» (ui_core.c:6646-6647) = петля покоя.
		if (PhaseTime >= ClipLen(1, kIdleLen)) { PhaseTime = 0.f; }
		break;
	case EAsdaGachaPhase::Rolling:
		// ★★★ КЛИП ПРОКРУТКИ ОТЫГРЫВАЕТСЯ ЦЕЛИКОМ, и ТОЛЬКО по его концу — выдача.
		//   Эталон `gachaScene_UpdateChildAnims@0xB6B0B0` (ui_core.c:6641-6645):
		//       if ( v7[155] == 2 )  { ctrlSetActiveChildByIndex(scene, v7[118], 3);
		//                              CSoundFlag_PlaySound2D(g_pSoundMgr, "S_e_pr_gc_click_00.wav", 0); }
		//   то есть переход 2→3 случается ПО КОНЦУ КЛИПА, а не по пакету.
		if (PhaseTime >= RollingLen()) { EnterPhase(EAsdaGachaPhase::Dispensing); }
		break;
	case EAsdaGachaPhase::Dispensing:
		if (PhaseTime >= ClipLen(3, kDispenseLen)) { EnterPhase(EAsdaGachaPhase::AwaitClick); }
		break;
	case EAsdaGachaPhase::Opening:
		if (PhaseTime >= ClipLen(4, kOpenLen)) { EnterPhase(EAsdaGachaPhase::Prize); }
		break;
	default:
		break;
	}
}

// ───────────────────────────── действия игрока ─────────────────────────────
void UAsdaPremiumGachaSubsystem::SelectSlot(int32 Slot)
{
	SelectedSlot = FMath::Clamp(Slot, 0, 4);
}

int32 UAsdaPremiumGachaSubsystem::GetSlotPrice(int32 Slot) const
{
	// Цена берётся из таблицы `DT_premium_gatya_data` — импорта `./Data/premium_gatya_data.bin`.
	// Формат разобран по КОДУ чтения (`PremiumGatya_LoadData@0xB6F720`): заголовок 20 б, счётчик
	// в @4, магия @12 "PMD", записи по 8 б {slot i32, price i32}; см. Tools/asda_decode_premium_gatya_data.py.
	// ⚠ В файле ДВЕ записи ({0,690} и {1,690}), а счётчик = 1 — дефект данных оригинала, из-за
	//   которого оригинал видит только первый автомат. В таблицу импортированы обе.
	// ★ С 2026-08-18 в таблице ПЯТЬ строк: слоты 2..4 добавлены нами копией цены слота 0 (690)
	//   — расширение сверх оригинала, подробности в комментарии `EnsureSceneActor`.
	static const TCHAR* kTablePath = TEXT("/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya_data.DT_premium_gatya_data");
	if (!PriceTable)
	{
		// ★ R5-fix2-c (13.11.2026): таблица — у подсистемы предметов (манифест item-БД, имя `premium_gatya_data`: тот же путь ассета, что
		//   `kTablePath`). Был свой `LoadObject` того же пути.
		const UAsdaItemTableSubsystem* ItemDb = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		PriceTable = ItemDb ? ItemDb->GetDataTable(TEXT("premium_gatya_data")) : nullptr;
		if (!PriceTable)
		{
			UE_LOG(LogAsdaGacha, Warning,
				TEXT("[премиум-гача] нет %s — цена берётся запасным значением %d"), kTablePath, kFallbackPrice);
		}
	}
	if (PriceTable)
	{
		// Строки таблицы названы по номеру записи файла; ищем по полю Slot, а не по имени строки,
		// потому что слот — это данные, а не порядок.
		static const FString Ctx(TEXT("GetSlotPrice"));
		for (const TPair<FName, uint8*>& It : PriceTable->GetRowMap())
		{
			if (const FAsdaPremiumGatyaDataRow* Row = reinterpret_cast<const FAsdaPremiumGatyaDataRow*>(It.Value))
			{
				if (Row->Slot == Slot) { return Row->Price; }
			}
		}
		// ★ Строки для этого автомата в таблице НЕТ. Эталон в этом случае возвращает 0
		//   (`list_GetSelectedEntryCode@0xB6F2C0`: индекс вне `[0, count)` → `return 0`), а НЕ
		//   запасную цену: запасная цена маскировала бы «сервер такого автомата не знает».
		//   Ровно та же строка обязана быть и в серверной `PrimiumGaChaPriceTable.txt` — иначе
		//   `Recv_UserPrimiumGaCha@0x4F6F10` отобьёт 6577 со статусом 0 (см. `OnRollResult`).
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] в DT_premium_gatya_data нет строки для автомата %d — цена 0 ")
			TEXT("(эталон `list_GetSelectedEntryCode` вне диапазона тоже даёт 0)"), Slot);
		return 0;
	}
	return kFallbackPrice;
}

// Порт `uiTradeConfirmMoney@0xB70420` вместе с гейтом вызывающего `GachaScene_OnControlMessage` (ui_core.c:7610-7619).
//   Описание и причина — у ключа `asda.Gacha.SlotSwitchGate` в начале файла.
void UAsdaPremiumGachaSubsystem::OnSpinButton(int32 Slot)
{
	if (!AsdaGachaSlotSwitchGateOn())
	{
		SelectSlot(Slot);   // прежнее поведение порта: выбор менялся до гейта `RequestRoll`
		RequestRoll();
		return;
	}
	// Порт держит клип одного (выбранного) автомата фазой: `Idle` = все автоматы в клипе 1 (петля покоя `PlayClipAll`),
	//   любая другая фаза = выбранный автомат в клипе 2/3/4, то есть `sub_92839D(k) >= 2` для него.
	if (!bSceneOpen || bRequestInFlight || Phase != EAsdaGachaPhase::Idle)
	{
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] «Играть» автомата %d отклонено гейтом эталона: запрос 6577 %s, фаза %d (выбран автомат %d)"),
			Slot + 1, bRequestInFlight ? TEXT("в полёте") : TEXT("не ждём"), (int32)Phase, SelectedSlot + 1);
		return;
	}
	SelectSlot(Slot);   // `*(this + 162) = a2`
	RequestRoll();      // `uiShowMoneyConfirmDialog`
}

void UAsdaPremiumGachaSubsystem::RequestRoll()
{
	if (!bSceneOpen || bRequestInFlight) { return; }
	if (Phase != EAsdaGachaPhase::Idle) { return; }
	ShowConfirmDialog();
}

void UAsdaPremiumGachaSubsystem::ShowConfirmDialog()
{
	// Порт `uiShowMoneyConfirmDialog@0xB6F130`: 68103 = всего, 68105 = цена, 68107 = остаток.
	//   ★ «Всего» — СУММА ДВУХ кошельков (ui_core.c:7374:
	//     `totalMoney = Field142C->gap_43B[17] + sub_929A63(g_pGameClient)`), а не один баланс.
	const int32 Price = GetSlotPrice(SelectedSlot);
	const int32 Total = Balance + GiftBalance;
	auto Fmt = [](int32 V) { return FText::AsNumber(V); };
	if (TotalText) { TotalText->SetText(Fmt(Total)); }
	if (PriceText) { PriceText->SetText(Fmt(Price)); }
	if (RestText)  { RestText->SetText(Fmt(Total - Price)); }
	// ★ Эталон при показе диалога ГАСИТ мышь у шара: `helper_setByte295(dword_2BDE140 = ctrl 68057, 1)`
	//   (ui_core.c:7372) — байт +295 контрола это `NoSelect` (разбор атрибута: ui_core.c:104983).
	//   Пока висит диалог оплаты, по шару не кликают.
	SetVis(BallHit, false);

	if (UAsdaWindowSubsystem* WM = GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>())
	{
		WM->SetWindowVisible(FName(kSelect.Id), true);
	}
}

void UAsdaPremiumGachaSubsystem::HideConfirmDialog()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kSelect.Id), false);   // `sub_92387F@0xB6F320`
	}
}

void UAsdaPremiumGachaSubsystem::HideGridWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kView.Id), false);     // `sub_91F36A@0xB6EE40`
	}
}

// Порт `uiItemTooltipShow@0xB70A60` (ui_core.c:7710-7734). ★ Гейт показа — КОД В ЗАПИСИ ПРИЗА
//   (`*(this + 180) != -1`), а не `gap_254[288]`: без записи мессадж-бокс не показывается вовсе.
void UAsdaPremiumGachaSubsystem::ShowPrizeBox()
{
	if (PrizeItemCode == -1)
	{
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] 68450 не показан: записи приза нет (эталонный гейт `*(this+180) != -1`)"));
		return;
	}
	FillPrizeBox();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kMsg.Id), true);
	}
}

// Порт `sub_92373F` → `sub_B70B80@0xB70B80` (ui_core.c:125096): `SetVisible(ctrl 68450, 0)`.
//   Разбор и доказательство соответствия тонка — в объявлении (заголовок).
void UAsdaPremiumGachaSubsystem::HidePrizeBox()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kMsg.Id), false);
	}
}

// Порт `uiItemTooltipShow@…` (ветка «клип 4 доиграл»): `SetVisible(68450)`,
//   `itemTooltipDrawRecordTextures(68453, код)` и текст loc 110621 «Поздравляем. Твой приз: [%s].»
//   в 68451, где `%s` — ИМЯ ПРЕДМЕТА из item-таблицы (`sprintf(fmt, itemName)`).
void UAsdaPremiumGachaSubsystem::FillPrizeBox()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTableSubsystem* Items = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;

	// Иконка 68453 — та же ячейка атласа, что и везде в порту (item-таблица → атлас JUI).
	if (PrizeIcon && Items && PrizeItemCode > 0)
	{
		// ★ Волна 1 (14.09.2026, В1 + исправление E3): общая точка иконки. Раньше клетка шла БЕЗ заворота;
		//   эталон рисует приз через `itemTooltipDrawRecordTextures` → `CControl_DrawTextureRect` →
		//   `planeFrame_setUV` + D3DTADDRESS_WRAP (разбор — у `AsdaUi::ApplyItemIcon`), поэтому клетка за краем
		//   узкого атласа повторяет атлас, а не пустеет. Кисть = размер клетки, слот 64×64 — как было.
		//   Откат — `asda.UI.ItemIconWrap 0`.
		if (AsdaUi::ApplyItemIcon(GI, PrizeIcon, PrizeItemCode, FVector2D::ZeroVector,
			AsdaUi::IconSexSelf, /*bWrap*/ AsdaUi::IsItemIconWrapFixEnabled()))
		{
			if (UCanvasPanelSlot* CS = AsdaUi::SlotOf(PrizeIcon)) { CS->SetSize(FVector2D(64.f, 64.f)); }
		}
	}

	// ═══ Текст 68451 ═══
	// ★★★ ПРИЧИНА «В ОКНЕ ПРИЗА НАПИСАНО [%s]» (найдена сверкой эталона со строкой данных, а не гаданием).
	//   Строка loc **110621** в `error list.bin` — это ФОРМАТ-СТРОКА printf:
	//     `110621  ...ǰ ̱⸦ ؼ [%s] ȹ߽ϴ!`   ⇒ «… [%s] …»
	//   Эталон подставляет имя ИМЕННО через `sprintf` (ui_core.c:7726-7732):
	//     TooltipDataById = itemGetTooltipDataById(v6[180]);      // запись имени предмета
	//     sprintf(Buffer, "%s", TooltipDataById);
	//     v3 = UI_FormatLocalizedNotice(g_pLocNoticeCtx, 110621, 0, 1);
	//     sprintf(Str, v3, Buffer);                                // ← ФОРМАТ = loc-строка
	//     CControl_SetText(v6[136] /* = ctrl 68451 */, Str);
	//   А порт звал `UAsdaTextSubsystem::FormatById`, который умеет ТОЛЬКО слоты `[ssN]`/`[sdN]`
	//   (эталонный `CObject_SetSlotString` + `UI_FormatLocalizedNotice`). Спецификатор `%s` он не
	//   трогает вовсе, поэтому строка уходила игроку как есть — «Твой приз: [%s].».
	//   ⇒ Зовём НОВУЮ общую точку `FormatPrintfById` (второй, printf-способ подстановки).
	//   ★ Имя берётся так же, как в `itemGetTooltipDataById@0xB74B30`: item_list → special_itemlist →
	//     sowell_list (все три таблицы) — это ровно то, что делает `GetItemName` подсистемы предметов.
	if (PrizeText)
	{
		const FString ItemName = (Items && PrizeItemCode > 0) ? Items->GetItemName(PrizeItemCode) : FString();
		UAsdaTextSubsystem* Txt = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		FString Msg = Txt ? Txt->FormatPrintfById(kLocPrizeCongrats, { ItemName }) : FString();
		if (Msg.IsEmpty()) { Msg = ItemName; }
		PrizeText->SetText(FText::FromString(Msg));
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] 68450: приз %d, имя «%s»%s, текст 110621 → «%s»"),
			PrizeItemCode, *ItemName,
			ItemName.IsEmpty() ? TEXT(" (★ ИМЕНИ НЕТ НИ В ОДНОЙ ТАБЛИЦЕ ПРЕДМЕТОВ)") : TEXT(""),
			*Msg);
	}
}

void UAsdaPremiumGachaSubsystem::ConfirmRoll()
{
	HideConfirmDialog();
	if (!bSceneOpen || bRequestInFlight) { return; }

	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net) { return; }

	// Эталон 68108: звук покоя + `premiumGatya_CheckGoldAndSend@0xB70350` (ui_core.c:7437).
	StartIdleLoop();

	// Эталонный гейт `premiumGatya_CheckGoldAndSend@0xB70350` (ui_core.c:7499): сравнивается СУММА
	//   двух кошельков, а не один баланс.
	const int32 Price = GetSlotPrice(SelectedSlot);
	if (Balance + GiftBalance < Price)
	{
		// `premiumGatya_CheckGoldAndSend`: не хватило премиум-валюты → нотис loc 110624.
		PostRollRefusal(kLocNoMoney);
		UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] не хватает средств: баланс=%d цена=%d"), Balance, Price);
		return;
	}

	// ⚠ charId (i32), НЕ userNum (i16): сервер сверяет поле с `CUser+372`.
	// charId берём оттуда же, откуда все прочие отправители порта — из блока выбора
	// персонажа (тело 1017), поле `SelectedCharId`. Это НЕ сессионный userNum.
	const int32 CharId = Net->SelectedCharId;
	bRequestInFlight = true;                       // gap_10[628] — гейт от двойной отправки
	RollSentAt = FPlatformTime::Seconds();         // ★ 07.11.2026: от этого момента считается удержание сцены до ответа
	Net->SendPremiumGachaRoll6577(CharId, (uint8)SelectedSlot);
	// ★★★ ПО КЛИКУ КЛИП НЕ МЕНЯЕТСЯ. Эталонный обработчик кнопки
	//   `premiumGatya_CheckGoldAndSend@0xB70350` → `premiumGatya_SendRequest` только шлёт 6577 и
	//   глушит idle-звук; автомат продолжает крутить клип покоя. Клип прокрутки
	//   запускает ТОЛЬКО ответ 6578 (`GachaScene_OnServerResult@0xB6FEE0`, case 1:
	//   `gachaSceneSetActiveChild(thisSelf[162], 2)`). Раньше порт входил в `Rolling` здесь, а
	//   выходил по приходу пакета — и прокрутка жила время сетевого round-trip
	//   (десятки мс). Именно это и было «анимация прокрутки ручки слишком короткая».
}

void UAsdaPremiumGachaSubsystem::CancelRoll()
{
	HideConfirmDialog();
}

// Клик по «золотому шару» 68057 — эталон `GachaScene_OnControlMessage@0xB704C0`, msg **0xB**
//   (у 68057 нет `idc`/PushButton, поэтому он получает СЫРОЕ мышиное сообщение, а не 0x10401):
//     if (a2 == 68057 && *(BYTE *)(this->gap_4[8] + 292))      // 0xC = ctrl 68055, +292 = Visible
//     {
//         if (this->gap_254[124] != -1) CItemManager_NotifyItemInfo(g_pInventoryWnd, &gap_254[124]);
//         CControl_SetVisible(this->gap_4[8], 0);              // спрятать подсказку 68055
//         ctrlSetActiveChildByIndex(g_pGachaSceneCtrl, this->gap_254[52], 4);   // клип ВСКРЫТИЯ
//         PlaySound2D("S_e_pr_gc_click_00.wav"); PlaySound2D("S_e_pr_gc_open_00.wav");
//         SetVisible(68058,1); CControl_Show(68058); SetField74_75(68058, -13, -1);
//     }
//   ⇒ ГЕЙТ КЛИКА — ВИДИМОСТЬ ПОДСКАЗКИ 68055, а не что-либо ещё. Воспроизводим его буквально
//   (фаза `AwaitClick` — наша модель того же состояния; проверяем оба, как в эталоне).
void UAsdaPremiumGachaSubsystem::ClickBall()
{
	if (Phase != EAsdaGachaPhase::AwaitClick) { return; }
	const bool bHintShown = AsdaUi::IsShown(HintPlane);   // ★ волна В6 ремесла (МЕЛК-8): общая точка видимости узла
	if (HintPlane && !bHintShown)
	{
		UE_LOG(LogAsdaGacha, Verbose, TEXT("[премиум-гача] клик по шару отклонён: подсказка 68055 скрыта"));
		return;
	}
	EnterPhase(EAsdaGachaPhase::Opening);
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] клик по шару 68057 → клип вскрытия"));
}

// ═══════════ ★★★ КНОПКА 68452 «Нажми» (`Premium_Pick_mssageBOX_ok`, текст loc 2288) ═══════════
//   ЭТАЛОН ЦЕЛИКОМ — `GachaScene_OnControlMessage@0xB704C0`, событие 0x10401 (ui_core.c:7645-7658):
//
//     if ( a2 == 68452 )
//     {
//       if ( *(_DWORD *)&this->gap_254[288] == -1 )          // ← ВАРИАНТ ЕЩЁ НЕ ВЫБИРАЮТ
//       {
//         g_bGachaRollActive = 1;
//         sub_92373F(this);                                   // = SetVisible(ctrl 68450, 0)
//         ctrlSetActiveChildByIndex(g_pGachaSceneCtrl, this->gap_254[52], 1);   // клип 1 = ПОКОЙ
//         CSoundFlag_PlaySound2D(g_pSoundMgr, "S_e_pr_gc_idle_00.wav", 1);      // петля покоя
//       }
//       else
//       {
//         uiItemInfoPopupOpen(this);                          // внутри — тот же sub_92373F + окно 68500
//       }
//     }
//
// ★★★ ЧТО БЫЛО СЛОМАНО В ПОРТУ (симптом владельца «кнопка ничего не делает, даже модалка не закрывается»):
//   1. Порт НИ В ОДНОЙ ветке не звал `sub_92373F` ⇒ мессадж-бокс 68450 не закрывался НИКОГДА.
//      Тонк `0x92373F` разобран по бинарю: `jmp 0xB70B80`, тело — одна строка `SetVisible(dword 133, 0)`,
//      dword 133 = контрол 68450. Это ЕДИНСТВЕННОЕ, что кнопка делает в обеих ветках гарантированно.
//   2. Порт вместо эталонной развилки сразу лез в окно вариантов и слал **6581**. Развилка эталона
//      идёт по `gap_254[288]`, а это поле сразу после прокрута РАВНО −1 (его ставит ответ 6578,
//      ui_core.c:7429; второй и последний писатель — успех 6582, ui_core.c:7950). ⇒ в оригинале
//      сразу после розыгрыша кнопка НЕ шлёт 6581 и НЕ открывает 68500.
//      Живой лог владельца это подтверждает: на каждый клик уходило `→6581 … слот=-1 контейнер=0`
//      и приходило `6582 статус=10` (отказ сервера) — то есть порт слал пакет, которого в этом
//      состоянии в оригинале нет вовсе.
//   3. Гейт `Phase != Prize` дополнительно глушил кнопку, если фаза успевала смениться.
void UAsdaPremiumGachaSubsystem::TakePrize()
{
	// [ИСПРАВЛЕНИЕ] 10.11.2026: окно 68500 одно на оба маршрута — висящий выбор «до получения» из ящика
	//   отменяется (заказ остаётся в ящике) и возвращает `VariantBaseCode` сцены до развилки ниже.
	if (bPreTakeChoice) { AbortVariantChoice(); }
	// `sub_92373F` — ОБЩИЙ ПЕРВЫЙ ШАГ обеих веток эталона: мессадж-бокс закрывается всегда.
	HidePrizeBox();

	if (VariantBaseCode == -1)
	{
		// Ветка «вариант не выбирают»: g_bGachaRollActive = 1, клип 1 (покой), петля покоя.
		StartIdleLoop();
		EnterPhase(EAsdaGachaPhase::Idle);
		UE_LOG(LogAsdaGacha, Log,
			TEXT("[премиум-гача] 68452 «Нажми»: gap_254[288] = −1 ⇒ эталонная ветка «покой» — ")
			TEXT("окно 68450 закрыто, автомат в клипе 1 (приз %d выдаёт сервер ответом 6578, не эта кнопка)"),
			PrizeItemCode);
		return;
	}

	// Ветка `uiItemInfoPopupOpen@0xB732F0` — окно выбора варианта 68500 по коду `gap_254[288]`.
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68452 «Нажми»: gap_254[288] = %d ⇒ окно 68500"), VariantBaseCode);
	OpenVariantWindow(VariantBaseCode);
}

// ═══════════════════════ ОКНО 68300: ВИТРИНА ПРИЗОВ ═══════════════════════
// Кнопка 68011+10i: эталон `GachaScene_OnControlMessage` → `openItemGridIfReady(this, i)` —
//   витрина открывается ТОЛЬКО если ни один автомат не «занят» (`sub_92839D(j) < 2` для всех j).
void UAsdaPremiumGachaSubsystem::OpenPrizeGrid(int32 Slot)
{
	if (!bSceneOpen) { return; }
	if (Phase != EAsdaGachaPhase::Idle) { return; }   // порт условия «ни один слот не занят»
	// ★ 05.11.2026 [ОРИГИНАЛ] `openItemGridIfReady@0xB70470`: при запросе 6577 в полёте (`+644`) выбор не меняется и витрина
	//   не открывается; `uiOpenItemGridWindow@0xB6EDE0` первым шагом прячет диалог оплаты 68100 (`sub_92387F`). Без этого
	//   «диалог автомата A → витрина B → Играть» уводил 6577 на автомат B. Ключ `asda.Gacha.SlotSwitchGate`.
	if (AsdaGachaSlotSwitchGateOn())
	{
		if (bRequestInFlight)
		{
			UE_LOG(LogAsdaGacha, Log,
				TEXT("[премиум-гача] витрина автомата %d отклонена гейтом эталона: запрос 6577 в полёте (выбран автомат %d)"),
				Slot + 1, SelectedSlot + 1);
			return;
		}
		HideConfirmDialog();
	}
	SelectSlot(Slot);
	// ★ [ПОРТ→ОРИГИНАЛ] исправление дефекта оригинала (волна 15, МЕЛК-3 ревизии волны 14). Эталон ряд витрины
	//   при открытии НЕ сбрасывает: `openItemGridIfReady@0xB70470` → `uiOpenItemGridWindow@0xB6EDE0`
	//   (ui_core.c:7299) зовёт только `sub_92387F` → `sub_B6F320` (скрыть контрол) и `uiRefreshItemGridPage`,
	//   а ряд (dword 179 = `gap_254[120]`) пишет ЕДИНСТВЕННОЕ место — сообщение 0x17 от полосы 68310
	//   (ui_core.c:7564). `uiControlSetScrollRange(68310, 5, рядов)` зажимает лишь позицию самого контрола, не
	//   dword 179. Итог у оригинала: прокрутили автомат A, открыли автомат B с меньшим числом рядов — сетка
	//   рисуется с чужого ряда (`5·v10[179]`, ui_core.c:7330) и может оказаться пустой при зажатом бегунке.
	//   Порт начинает витрину с ряда 0 — как видит её игрок при первом открытии. Разбор — GACHA_FLOW.md.
	GridScrollRow = 0;
	GridSelectedCell = -1;
	GridMouse.Reset();
	PopulatePrizeGrid();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(FName(kView.Id), true);
	}
	RefreshPrizeGridPage();
	LayoutGridScrollBar();
}

void UAsdaPremiumGachaSubsystem::CloseGridWindow()
{
	GridScrollBar.Hide();
	GridMouse.Reset();
	HideGridWindow();   // `sub_91F36A@0xB6EE40`
}

void UAsdaPremiumGachaSubsystem::SetGridScrollRow(int32 Row)
{
	const int32 Clamped = FMath::Clamp(Row, 0, GridMaxScrollRow());
	if (Clamped == GridScrollRow) { return; }
	GridScrollRow = Clamped;     // эталонное поле `CGachaScene` dword 179 (`gap_254[120] = a6`)
	GridSelectedCell = -1;       // страница сменилась — подсветка выбора снимается
	RefreshPrizeGridPage();
	LayoutGridScrollBar();
}

// Число строк прокрутки: `uiControlSetScrollRange(68310, 5, ceil(count / 5))` — шаг 5 записей.
//   ★ Волна 14 [ПОРТ→ОРИГИНАЛ]: максимум позиции у эталона = a3 − a2 = рядов − 5 (`uiScrollbarSetRange`: +612 = a3 − a2,
//   `uiScrollbarSetPos` клампит к нему), то есть последняя позиция показывает ПОСЛЕДНИЕ 5 рядов. Прежнее
//   «рядов − 1» давало прокрутку почти до пустой сетки и бегунок 1/рядов вместо 5/рядов.
int32 UAsdaPremiumGachaSubsystem::GridRowCount() const
{
	return FMath::DivideAndRoundUp(Prizes.Num(), kGridPageStep);
}

int32 UAsdaPremiumGachaSubsystem::GridMaxScrollRow() const
{
	return FMath::Max(0, GridRowCount() - kGridVisibleRows);
}

// Порт `uiRefreshItemGridPage@0xB6EE70`: иконки 25 ячеек + оверлей выбора 68356.
void UAsdaPremiumGachaSubsystem::RefreshPrizeGridPage()
{
	if (PrizeGrid) { PrizeGrid->Refresh(); }

	// Оверлей выбора 68356 `Slot_AniSelect` (4 кадра / 300 мс) ставим на выбранную ячейку —
	//   геометрию берём у самой ячейки, чтобы не выдумывать координаты (закон `no-invented-ui`).
	if (!GridSelectMark) { return; }
	if (!ViewWin || GridSelectedCell < 0 || GridSelectedCell >= kGridCellCount)
	{
		SetVis(GridSelectMark, false);
		return;
	}
	UWidget* Cell = ViewWin->GetWidgetFromName(FName(kGridCells[GridSelectedCell]));
	UCanvasPanelSlot* CellSlot = Cell ? AsdaUi::SlotOf(Cell) : nullptr;
	UCanvasPanelSlot* MarkSlot = AsdaUi::SlotOf(GridSelectMark);
	if (!CellSlot || !MarkSlot) { SetVis(GridSelectMark, false); return; }
	// Ячейки лежат в группах-строках, а маркер — в общей канве вкладки: позиция ячейки переводится в координаты
	//   родителя маркера. ★ Дедуп №2, В3-S10 (остаток U19): перевод — общая точка `AsdaUi::PlaceFrameOver` (было своё
	//   `CanvasRootPos(ячейка) − CanvasRootPos(родитель маркера)` — то же число, равенство формул доказано
	//   `asda.UI.FrameFormulaCheck` в В2-B); видимость задаёт `SetVis` ниже, кадры не перезапускались и не перезапускаются.
	// ⚠ [ПОРТ] Размер 64×64 ставит порт (прежнее `SetOffsets(…, 64, 64)` — оставлено): у эталона маркер 68356 только
	//   ищется (`world_map.c:4635`, `gap_10[368]`), ни позиции, ни показа в коде оригинала нет (`Docs/UI/Premium/
	//   Premium_Pick_View_Win.md`) — постановка маркера на ячейку и его размер — решение порта, не эталон (отчёт `wave3_S10.md`).
	AsdaUi::PlaceFrameOver(GridSelectMark, Cell, /*bRestartAnim*/ false);
	MarkSlot->SetSize(FVector2D(64.f, 64.f));
	SetVis(GridSelectMark, true);
}

// Полоса 68310. Контрола полосы в .JUI нет: движок оригинала подвешивает `CScrollBar` по ключу
//   скина `WheelWin_Nomal` → `ScrollBarFileNameVert = "Nomal_Scr"` (`uiControl_AttachVertScrollbar
//   @0x12F3160`). У порта это общий компонент `FAsdaScrollBarNomal` — тот же, что у сумки и книги
//   навыков (закон `port-shared-points`).
// ★★★ ПРИЧИНА «КНОПКИ ПРОКРУТКИ В ЛЕВОМ ВЕРХНЕМ УГЛУ ЭКРАНА» (найдена сверкой двух источников,
//   а не подбором координат). Общая полоса создаёт свои четыре плана с АНКОРОМ (0,0) и ставит их
//   `SetOffsets` (`AsdaScrollBarNomal.cpp:55-56`), то есть её `Origin` отсчитывается от ЛЕВОГО
//   ВЕРХА канвы-хозяина. А хозяином была КОРНЕВАЯ канва окна 68300, все прямые дети которой у
//   импортёра сидят на анкоре 0.5/0.5 (дамп WBP: `Premium_Pick_View_Win_0_0 … анкор [0.500,0.500]`),
//   и `Origin` считался в ТОЙ схеме: `abs − (512, 384)` = (150, −180). Смешение двух схем и дало
//   полосу в углу экрана, да ещё выше его верхней границы.
//   ЧИНИМ ПО ОБРАЗЦУ ДВУХ ДРУГИХ ОКОН ПОРТА (сумка и книга навыков): вешаем полосу в ТУ ЖЕ
//   канву-группу, где лежат ряды ячеек, и считаем `Origin` от ЖИВОЙ ячейки 68311 — тогда полоса
//   не зависит ни от анкоров, ни от перетаскивания окна (`ui-window-drag-hittest`).
void UAsdaPremiumGachaSubsystem::BuildGridScrollBar()
{
	if (GridScrollBar.IsBuilt() || !ViewWin) { return; }
	// Хозяин — канва внутренней рамки `Win_Tab_grp` (в `.JUI` это контрол `Win_Tab`, abs 323,199,
	//   356×346): именно в ней лежат и ряды ячеек, и оверлей выбора 68356, и туда же оригинал
	//   подвешивает полосу колёсного окна. Берём по ЖИВОМУ дереву — через родителя 68356, а если
	//   его нет, через деда ячейки 68311 (ячейка → группа-строка → группа-рамка).
	//   ★ Формула `Origin` ниже НЕ зависит от того, какая именно канва стала хозяином: она считает
	//     смещение от ЖИВОЙ ячейки внутри ЭТОЙ ЖЕ канвы. Хозяин выбран ради читаемости иерархии.
	UWidget* Cell0 = ViewWin->GetWidgetFromName(FName(kGridCells[0]));
	UCanvasPanel* Host = GridSelectMark ? Cast<UCanvasPanel>(GridSelectMark->GetParent()) : nullptr;
	if (!Host && Cell0 && Cell0->GetParent()) { Host = Cast<UCanvasPanel>(Cell0->GetParent()->GetParent()); }
	if (!Host)
	{
		// Иерархия окна изменилась — предупреждаем ГРОМКО: в корневой канве у детей анкор 0.5/0.5,
		//   и полоса (анкор 0/0) снова уедет в угол экрана. Это ровно тот дефект, что чинится здесь.
		Host = Cast<UCanvasPanel>(ViewWin->GetRootWidget());
		UE_LOG(LogAsdaGacha, Warning,
			TEXT("[премиум-гача] 68300: канва рамки не найдена (нет ни 68356, ни деда ячейки) — ")
			TEXT("полоса встала в корневую канву, проверьте раскладку"));
	}
	if (!Host) { return; }
	GridScrollHost = Host;
	GridScrollBar.Build(ViewWin, Host, TEXT("PREMIUM_VIEW_Scr"), /*ZTrack*/ 30);
	GridScrollBar.Hide();
}

void UAsdaPremiumGachaSubsystem::LayoutGridScrollBar()
{
	if (!GridScrollBar.IsBuilt() || !ViewWin || !GridScrollHost) { return; }
	const int32 MaxRow = GridMaxScrollRow();

	// Полоса стоит по ПРАВОМУ краю колёсного окна 68310 (`uiControl_AttachVertScrollbar`), а его
	//   геометрия из `gamemain_jui.ini`: abs (328, 204), 334×334. Позицию берём не константой, а
	//   через живую ячейку 68311 (abs 331, 207): её смещение внутри канвы-хозяина известно точно,
	//   а разность абсолютов — это ДАННЫЕ `.JUI`, не подбор.
	UWidget* Cell0 = ViewWin->GetWidgetFromName(FName(kGridCells[0]));
	if (!Cell0) { return; }
	const FVector2D CellLocal = AsdaUi::CanvasRootPos(Cell0) - AsdaUi::CanvasRootPos(GridScrollHost);
	const FVector2D Origin = CellLocal
		+ FVector2D(kWheelAbsX + kWheelW - kCell0AbsX, kWheelAbsY - kCell0AbsY);
	// ★ Волна 14 (МЕЛК-1): доля видимого — целыми аргументами эталона `(5, ceil(count / 5))`: 5 видимых рядов
	//   из всех; длину бегунка считает `EtalonThumbLen` (float-арифметика `uiScrollbarSetRange`).
	GridScrollBar.Layout(Origin, kWheelH, /*Scale*/ 1.f, kGridVisibleRows, GridRowCount(), MaxRow, GridScrollRow);
}

// Прямоугольник колёсного окна 68310 в ЭКРАННЫХ координатах Slate — нужен колесу мыши.
//   Считается от живой ячейки 68311 теми же дельтами `.JUI`, что и позиция полосы.
bool UAsdaPremiumGachaSubsystem::GetWheelRect(FVector2D& OutMin, FVector2D& OutMax) const
{
	if (!ViewWin) { return false; }
	UWidget* Cell0 = ViewWin->GetWidgetFromName(FName(kGridCells[0]));
	FVector2D CMin, CMax;
	if (!Cell0 || !AsdaUi::WidgetAbsRect(Cell0, CMin, CMax)) { return false; }
	// Масштаб экрана берём по самой ячейке: её авторский размер 64×64 (`gamemain_jui.ini`).
	const float S = FMath::Max((float)(CMax.X - CMin.X) / 64.f, KINDA_SMALL_NUMBER);
	OutMin = CMin + FVector2D(kWheelAbsX - kCell0AbsX, kWheelAbsY - kCell0AbsY) * S;
	OutMax = OutMin + FVector2D(kWheelW, kWheelH) * S;
	return true;
}

// ★★★ КОЛЕСО МЫШИ НАД ВИТРИНОЙ. Эталон: колёсное окно 68310 шлёт сцене сообщение **0x17**, и
//   `GachaScene_OnControlMessage@0xB704C0` (ui_core.c:7561-7567) делает
//   `this->gap_254[120] = a6; uiRefreshItemGridPage(this);` — то есть ставит НОВУЮ ПОЗИЦИЮ СКРОЛЛА
//   и перерисовывает страницу. У порта колесо над интерфейсом раздаёт общий реестр
//   `UAsdaWindowSubsystem::RegisterWheelHandler` (тот же канал, что у сумки, книги навыков и чата),
//   поэтому своего опроса аксиса здесь нет (закон `port-shared-points`).
bool UAsdaPremiumGachaSubsystem::OnGridWheel(int32 Notches)
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!bSceneOpen || !WM || !WM->IsWindowVisible(FName(kView.Id))) { return false; }
	FVector2D Mn, Mx;
	if (!GetWheelRect(Mn, Mx)) { return false; }
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FVector2D C = FSlateApplication::Get().GetCursorPos();
	if (C.X < Mn.X || C.X > Mx.X || C.Y < Mn.Y || C.Y > Mx.Y) { return false; }
	SetGridScrollRow(GridScrollRow - Notches);   // щелчок «на себя» = вниз по списку
	return true;                                  // колесо съедено интерфейсом
}

void UAsdaPremiumGachaSubsystem::TickGridScrollBar()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	const bool bShown = WM && WM->IsWindowVisible(FName(kView.Id));
	if (!GridScrollBar.IsBuilt() || !bShown)
	{
		GridScrollBar.bThumbGrab = false;
		GridScrollBar.PressedPlane = -1;
		if (GridScrollBar.IsBuilt() && !bShown) { GridScrollBar.Hide(); }
		return;
	}
	LayoutGridScrollBar();
	GridMouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ⚠ Защёлки сбрасываем: и кадр мыши, и латч захвата бегунка (`bThumbGrab`) — иначе схваченная
	//   полоса поехала бы за курсором, как только нотис закроется.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		int32 Dummy = 0;
		GridScrollBar.Tick(false, false, GridMaxScrollRow(), Dummy);
		GridMouse.Reset();
		return;
	}
	int32 Value = 0;
	switch (GridScrollBar.Tick(GridMouse.bLmb, GridMouse.bLmbDown, GridMaxScrollRow(), Value))
	{
	case EAsdaScrollAction::ScrollBy: SetGridScrollRow(GridScrollRow + Value); break;
	case EAsdaScrollAction::SetRow:   SetGridScrollRow(Value);                 break;
	default: break;
	}
}

// ═══════════════════ ПОДСКАЗКИ ПО ЯЧЕЙКАМ ВИТРИНЫ И ПО ИКОНКЕ ПРИЗА ═══════════════════
// ЭТАЛОН (`GachaScene_OnControlMessage@0xB704C0`, ui_core.c:7673-7683):
//     case 8:  cItemBar_BuildSlotTooltip(this, a2);        // курсор вошёл в контрол — СОБРАТЬ
//     case 9:  Tooltip_LayoutNearCursor(&g_ItemTooltip);   // курсор двигается — переложить
//     case 0xA: CControl_KillFocus(&g_ItemTooltip);        // курсор ушёл — ПОГАСИТЬ
//   `cItemBar_BuildSlotTooltip@0xB6F3C0` принимает idnum контрола и ищет его среди 25 ячеек
//   витрины (68311…68335) либо сравнивает с 68453 (иконка приза в мессадж-боксе).
// ПОРТ. Плашка в порту ОДНА на всю игру (`UAsdaTipPanel` у `UAsdaItemTooltipSubsystem`), поэтому
//   ★ ЗАКОН `ui-shared-tip-ownership`: показываем со СВОИМ владельцем `PremiumGacha`, гасим только
//   через `HideTooltipFor(владелец)` — безусловный `Hide()` из нашего тика съедал бы чужой показ
//   (и наоборот). Укладка у курсора — внутри общей точки (`ShowItemTooltip` → `LayoutAtCursor`,
//   смещение +32/+32 от курсора, то есть плашка курсор не перекрывает).
void UAsdaPremiumGachaSubsystem::TickTooltips()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!Tip || !WM || !ViewWin) { return; }

	int32 Hit = -1;        // −1 нет, 0…24 ячейка витрины, 25 иконка приза 68453
	int32 HitCode = 0;

	// Ячейки витрины — только пока окно 68300 показано.
	if (WM->IsWindowVisible(FName(kView.Id)))
	{
		for (int32 Cell = 0; Cell < kGridCellCount && Hit < 0; ++Cell)
		{
			const int32 Idx = GridScrollRow * kGridPageStep + Cell;
			const int32 Code = Prizes.IsValidIndex(Idx) ? Prizes[Idx] : 0;
			if (Code <= 0) { continue; }               // пустая ячейка подсказки не имеет
			UWidget* W = ViewWin->GetWidgetFromName(FName(kGridCells[Cell]));
			if (W && AsdaUi::IsCursorOverWidget(W)) { Hit = Cell; HitCode = Code; }
		}
	}
	// Иконка приза 68453 в мессадж-боксе 68450 (`a2 == 68453` → код приза `v10[134]`).
	if (Hit < 0 && PrizeIcon && PrizeItemCode > 0 && WM->IsWindowVisible(FName(kMsg.Id))
		&& AsdaUi::IsCursorOverWidget(PrizeIcon))
	{
		Hit = kGridCellCount;
		HitCode = PrizeItemCode;
	}

	if (Hit < 0)
	{
		if (TipHoverCell >= 0) { Tip->HideTooltipFor(EAsdaTipOwner::PremiumGacha); TipHoverCell = -1; }
		return;
	}
	if (Hit == TipHoverCell) { return; }   // тот же контрол — эталон msg 8 повторно не шлёт
	TipHoverCell = Hit;
	Tip->ShowGachaSlotTooltip(HitCode, EAsdaTipOwner::PremiumGacha);
	// ★ ДИАГНОСТИКА ДЛЯ ЖИВОГО ЗАПУСКА. Владелец сообщал «тултипов в витрине нет», а причина была
	//   НЕ здесь: общая плашка `ITEM_TIPWIN` живёт в реестре окон, и шаг 5 `premiumGacha_BeginRoll`
	//   (`WM->CloseAll()`) выбрасывал её из вьюпорта — `UAsdaTipPanel::Bind` считал привязку
	//   действительной и молча показывал плашку «в никуда». Починено самолечением привязки
	//   (`AsdaTipPanel.cpp::Bind` + `AsdaItemTooltipSubsystem::GetMainPanel`). Строка ниже
	//   подтверждает на следующем запуске, что показ реально дошёл до видимой плашки.
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] подсказка: %s, код %d, плашка на экране=%s"),
		(Hit == kGridCellCount) ? TEXT("иконка приза 68453") : TEXT("ячейка витрины"),
		HitCode, Tip->IsTooltipShown() ? TEXT("да") : TEXT("НЕТ"));
}

// ═══════════════════════ ОКНО 68200: БАЛАНС И ПОПОЛНЕНИЕ ═══════════════════════
// ★★★ ПОРТ `inventoryBag_RefreshGoldUi@0xB6DE60` (item_inventory.c:5398-5419) — ЗАКРЫТ ОТКРЫТЫЙ
//   ВОПРОС `gacha_ui_re.md` §8 п.5 («писателя 68202/68204 в срезе эталона нет»). Это было ЛОЖНОЕ
//   «нет» (закон `false-negatives-method`): писатель есть, просто под мисномером IDA и адресуется
//   не именами, а dword-индексами структуры сцены:
//       v8[112] ← String_FormatThousands(Field142C->gap_43B[17])   // (16 + 432)/4 = 112 = ctrl 68202
//       v8[114] ← String_FormatThousands(sub_929A63(g_pGameClient))// (16 + 440)/4 = 114 = ctrl 68204
//   Зовёт его `inventoryBag_Open@0xB6DDD0`, который ТУТ ЖЕ делает `SetVisible(*(this+109) = 68200, 1)`,
//   а его самого зовёт последний шаг `premiumGacha_BeginRoll` (world_map.c:4751,
//   `inventoryBag_Open((int *)v20->gap_0)` — аргумент = сама сцена). ⇒ окно 68200 в оригинале
//   ПОКАЗЫВАЕТСЯ вместе со сценой, и это НЕ наше исправление дефекта, а эталонное поведение.
//   ⇒ ДВЕ строки — норма оригинала: 68202 = деньги персонажа (их перезаписывает ответ 5467),
//     68204 = второй кошелёк `get_oBC0D5` (loc 12959 «Деньги в подарок»). Скрывать вторую строку
//     нельзя — её показывают и подпись, и данные. Значение = `GiftBalance`, которое в оригинале
//     всегда 0 (писателя `*(g_pGameClient + 0x2BCFD4)` в срезе НЕТ, в канале 5466/5467 поля нет),
//     поэтому печатается ноль — источник не выдуман.
void UAsdaPremiumGachaSubsystem::RefreshClickWinTexts()
{
	if (ClickCashText)     { ClickCashText->SetText(FText::FromString(AsdaUi::FormatThousands(Balance))); }
	if (ClickGiftCashText) { ClickGiftCashText->SetText(FText::FromString(AsdaUi::FormatThousands(GiftBalance))); }
}

void UAsdaPremiumGachaSubsystem::RefreshBalanceButton()
{
	RequestBalance();   // 5466, как кнопка 68206 в эталоне
}

// 68205 `Premium_Pick_Click_Win_char` → `UI_OpenDonatePage(g_pShopWnd)`.
//   ★ Страницы доната в порту нет (веб-витрина оригинала не воспроизводится), поэтому по нажатию
//     печатаем ЭТАЛОННЫЙ нотис 110624 — тот самый текст, которым оригинал и просит пополнить счёт.
//     Это отступление помечено; появится страница — сюда встанет её открытие.
void UAsdaPremiumGachaSubsystem::OpenDonatePage()
{
	if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
	{
		Msg->PostLoc(EAsdaMsgCategory::Trade, kLocNoMoney);
	}
	UE_LOG(LogAsdaGacha, Log, TEXT("[премиум-гача] 68205: страницы пополнения нет — выдан нотис %d"), kLocNoMoney);
}
