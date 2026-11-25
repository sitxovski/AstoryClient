// Copyright (c) 2026 @sitxovski (Discord)
// ★ 23.10.2026 РАЗНОС ФАЙЛА (слово владельца «огромные файлы разделить … с гарантией 100 %»): методы UAsdaNpcDialogSubsystem
//   разнесены по группам в AsdaNpcDialogSubsystem_{Script,Quest,Popups,Debug}.cpp; общее (LogAsdaNpcDlg, чистые константы и хелперы) —
//   в AsdaNpcDialogSubsystemInternal.h. Строки перенесены БАЙТ В БАЙТ:
//   `python Tools/asda_split_cpp.py verify --plan Reports/split_2026-10-23/AsdaNpcDialogSubsystem.plan.json`.
//   Здесь осталось ядро: жизненный цикл, окно, данные, открытие, наполнение, нажатия, условия и действия шага.
// UAsdaNpcDialogSubsystem — реализация. Обоснование каждого шага и границы захода — в .h.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaNoticeColors.h"   // ★ U12 (В1 дедупа №2): эталонные цвета нотисов одной точкой
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"          // строка списка 2408/2410: ровно 20 px по эталону
#include "Components/ButtonSlot.h"       // выравнивание подписи внутри строки-кнопки
#include "Components/ContentWidget.h"    // обход строки списка (SizeBox → Button → TextBlock)
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"

#include "AsdaDataTableCache.h"
#include "World/AsdaEntityText.h"   // ★ волна 3 п.14: имя NPC для заголовка диалога — одна точка
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaGameText.h"        // FAsdaTextRow — строка DT_Talk (поля npc / line / text)
#include "Npc/AsdaNpcActor.h"
#include "Quest/AsdaQuestTypes.h"
#include "Resources/AsdaTextureSubsystem.h"
#include "Text/AsdaTextSubsystem.h"
#include "Login/AsdaText.h"                 // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): FAsdaText::Loc — подписи отказов по id
#include "UI/AsdaMessageSubsystem.h"
#include "UI/AsdaNotice.h"            // ★ 27.09 (clan_btn_6099_fix): MsgBoxLoc — окна нехватки/отказа обмена, как у эталона
#include "UI/AsdaCommunityFriendSubsystem.h"   // AsdaCommunityUi::ShownVisibilityFor — общая точка видимости узла
#include "UI/AsdaBoosterPanelSubsystem.h"   // ★ гейт 13101: «открыто окно бустера» (эталонное поле +0x3F8)
#include "UI/AsdaTradeStateSubsystem.h"   // ★ 29.09.2026: гейт 1 `NPC_OpenWindow` — окно обмена 2008 открыто (`CExchange+0x2C`)
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaMarkup.h"   // ★ 09.11.2026: UAsdaRichText — заголовок с префиксом [MISSING]
#include "Npc/AsdaNpcMissing.h"   // ★ 09.11.2026: красный префикс [MISSING]
#include "Components/RichTextBlock.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaNpcShopSubsystem.h"
#include "UI/AsdaEquipShopSubsystem.h"   // 30020 — магазин снаряжения, окно 6800
#include "UI/AsdaBoosterShopSubsystem.h" // 30034 — магазин бустеров, окно 15300
#include "Quest/AsdaQuestLogSubsystem.h"   // ★ волна В6 ремесла: смерть закрывает журнал квестов (`uiWnd_Close`, entity_char.c:16898)
#include "UI/AsdaHairShopSubsystem.h"    // 30029 — салон причёсок, окно 15000
#include "UI/AsdaPremiumShopSubsystem.h" // 30030 — элитный магазин 6000 (волна В5); хвост смерти
#include "UI/AsdaClanSubsystem.h"       // 30015/30016 — создание и роспуск клана
#include "UI/AsdaSoulmateSubsystem.h"   // ★ волна 1-А: 30023/30024/30025 — соулмейт (окно 9100, разрыв 5204)
#include "UI/AsdaExchangeSubsystem.h"   // 30017/30018/30019/30021/30028 — аукцион, окно 13000
#include "UI/AsdaWarehouseSubsystem.h"   // 30011/30012 — склад кладовщика, окно 4000 (+ свиток 5500)
#include "UI/AsdaConstellationSubsystem.h" // 30073 — выбор знака зодиака, окно 74500
#include "UI/AsdaRepairSubsystem.h"      // 30069 — окно ремонта 57300
#include "UI/AsdaWarShopSubsystem.h"
#include "UI/AsdaExtractShopSubsystem.h"   // 30035 — передача параметров, окно 16000
#include "UI/AsdaItemResetSubsystem.h"     // 30046/30047 — сброс улучшений, окно 20200 (2 режима)
#include "UI/AsdaItemOptionSubsystem.h"    // 30060 — смена параметров предмета, окно 50300     // 30070 — военный магазин 67200   // ★ магазин NPC: коды 30001…30004 / 30026 (окно 1300)
#include "UI/AsdaMarkup.h"          // UAsdaRichText::ApplyFormatted — теги <FC=…> в баннере 599
#include "UI/AsdaTipPanel.h"             // общая точка пересборки 9-slice (порт uiPanel9SliceLayout) + EAsdaTipOwner
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: развилка NPC типа 5 → доска объявлений 75000 (NPC_SpecialAction_Type5)
#include "UI/AsdaItemTooltipSubsystem.h" // ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "Data/AsdaEffectMobTableRows.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"              // ★ ДЕДУП Д4: чтение little-endian из тела пакета — одна точка на порт
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaInventorySubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaCraftSubsystem.h"             // ★ ключ К-9 `asda.Craft.FixDeathRestoreOrder` (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "UI/AsdaFactionSelectSubsystem.h"        // ★ 30.10.2026: 30056 — выбор армии 44000; 6700 при разговоре с NPC войны
#include "Npc/AsdaDonationSubsystem.h"         // ★ волна 2-Б: 30051 — пожертвование (нумпад режима 6 → 6132)
#include "TimerManager.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "HAL/IConsoleManager.h"            // ★ 27.09 itemsfix: asda.EventExchange.ServerGuard
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "UI/AsdaTutorialEvents.h"   // ★ 10.11.2026 обучение: флаги шагов гида 1950 (TUTORIAL.md §2.3)

DEFINE_LOG_CATEGORY(LogAsdaNpcDlg);   // ★ 23.10.2026 разнос: было DEFINE_LOG_CATEGORY_STATIC; объявление — AsdaNpcDialogSubsystemInternal.h (один экземпляр на все части)

// ── адреса ассетов и таблиц ─────────────────────────────────────────────────────────────────────
static const TCHAR* kDlgWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Quest/WBP_NPC_LINE_NPC.WBP_NPC_LINE_NPC_C");
static const FName  kDlgWinId(TEXT("NPC_LINE_NPC"));

static const TCHAR* kDtNpc    = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcQuestNpc.DT_NpcQuestNpc");
static const TCHAR* kDtHeader = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrHeader.DT_NpcNsrHeader");
static const TCHAR* kDtButton = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrButton.DT_NpcNsrButton");

// ── эталонные константы ─────────────────────────────────────────────────────────────────────────
// `npcCraftSlotClick_Handle@0xD9C470`: стартовая страница = запись сценария с этим opcode.
static constexpr int32 kNsrOpcodeStartPage = 121;
// `npcDialogRenderPage@0xD9C7B0`: у этих трёх npcId к тексту страницы приписывается нотис 111929.
static constexpr int32 kNoticeAppendNpc[3] = { 46, 50, 53 };
static constexpr int32 kLocNoticeAppend = 111929;
// Цвет строки реплики — `uiChatLogAddLine(..., -16777216, ...)` = 0xFF000000 (чёрный).
static constexpr uint32 kTalkTextArgb = 0xFF000000u;
// Эталон вычитает 12 из ширины тела реплики перед разбивкой на строки.
static constexpr float kTalkWrapMargin = 12.f;

// ★★ [ОРИГИНАЛ] 30.10.2026 (баг игроков «Sowel Shop Lua: Don't forget to load <FC=FFFF2222> sowel </F>!»):
//   эталон кладёт строки реплики в список 574 через `uiChatLogAddLine` (npc_mob_pet.c:3423/3466), а тот рисует
//   разметкой `<F…>` (`textParseColorTag`, font_text.c:7924); перенос `textTokenizer_SplitStringIntoRows(…, 1)`
//   теги при измерении пропускает. Порт клал строку в обычный UTextBlock — теги были видны буквами
//   (NSR `lua_50`, `cave_44`, приписка 111929 у NPC 46/50/53). 1 = строка с тегами через общую точку
//   `UAsdaRichText::ApplyFormatted`, 0 = прежний вывод текстом.
static ASDA_DEV_CVAR(int32, CVarTalkMarkup,
	TEXT("asda.NpcDlg.TalkMarkup"), 1,
	TEXT("1 = теги цвета <FC=..>..</F> в реплике NPC рисуются цветом (как оригинал); 0 = выводятся текстом (прежний порт)."),
	ECVF_Default);

// Локализованные строки, на которые ссылается диспетчер и приём 5044.
static constexpr int32 kCmdClanCreateCode = 30015;  // единственный код, который знает uiShowItemTooltipByCode
static constexpr int32 kLocClanCreateCond   = 12457;
// ★ Гейт 4 эталона (`CObject_GetField254(g_pInventoryWnd)`, поле `+0x3F8`): «открыто окно бустера».
//   Текст 13101 в `GameText.csv` — «Нельзя разговаривать … во время применения бустера»; он и
//   доказал, что поле означает бустер, а не сумку (разбор — `Reports/npc_visual_fix.md` §4.5).
static constexpr int32 kLocNoTalkWhileBooster = 13101;

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ЖИЗНЕННЫЙ ЦИКЛ
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★ Волна 1-А: отметки «встреча с NPC была» — пусто на старте запуска (`gameObj_ctor@0xD8E300`: 200 × 0).
	MetNpc.Init(false, 256);
	// ★ Приём ответа сервера на 5043 идёт ОТЛОЖЕННО, через делегат сетевой подсистемы — своей
	//   разборки пакета у окна нет (закон `no-reentrant-net-callbacks`).
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		QuestStepResultHandle = Net->OnQuestStepResult.AddUObject(this, &UAsdaNpcDialogSubsystem::HandleQuestStepResult);
		QuestTurnInResultHandle = Net->OnQuestTurnInResult.AddUObject(this, &UAsdaNpcDialogSubsystem::HandleQuestTurnInResult);
		// ★ Волна В4 ремесла (ревизия В3 МЕЛК-1): смерть своего персонажа (`CCharacter_SetAction`, entity_char.c:16900)
		//   безусловно зовёт `UI_resetAllWindowsAndLeave_5045` — выход из разговора и восстановление снимка окон.
		//   Колбэк сети только ставит флаг, действие — в `Tick`.
		DeathHandle = Net->OnCharDeath.AddWeakLambda(this, [this](int32 UserIdx, int32 /*KillerIdx*/)
		{
			const UAsdaNetSubsystem* N = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
			if (N && N->IsSelfUserIdx(UserIdx)) { bLeaveOnDeathPending = true; }
		});
	}
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] подсистема поднята: окно 570 `NPC_LINE NPC` + облачко 2406 + подсказка 599, ")
		TEXT("данные — DT_NpcNsrHeader/Button/Page, DT_NpcSnq/SnqIct/EpisodeTable/WarpList/CallMob/EventItem"));
}

void UAsdaNpcDialogSubsystem::Deinitialize()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWindowCloser(kDlgWinId);
		WM->UnregisterWindowCloser(kMqWinId);
		WM->UnregisterWindowCloser(kQiWinId);
		WM->UnregisterWindowCloser(kRwWinId);   // Э6: попап наград 540
		WM->UnregisterWindowCloser(kAcWinId);   // окно приёма задания 750
	}
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnQuestStepResult.Remove(QuestStepResultHandle);
		Net->OnQuestTurnInResult.Remove(QuestTurnInResultHandle);
		Net->OnCharDeath.Remove(DeathHandle);
	}
	QuestStepResultHandle.Reset();
	QuestTurnInResultHandle.Reset();
	Relays.Reset();
	RwRelays.Reset(); AcRelays.Reset(); MqRelays.Reset(); QiRelays.Reset();   // ★ 31.10.2026: якоря кнопок окон 540/750/2400/599
	Win = nullptr;
	bReady = false;
	// Третья и четвёртая эталонные точки сброса бита 0 в `g_pEntityList+0x1048`:
	//   `charLeaveWorldCleanup@0xF8F7EE` (выход из мира) и разбор мира в
	//   `MainLoop_Tick_SoundUpdate@0x96435E` (смена карты). В порту обе сводятся к разбору
	//   подсистемы вместе с игровым инстансом — снимаем флаг здесь.
	bEntityListDialogOpen = false;
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaNpcDialogSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Диалог ведётся с NPC КОНКРЕТНОЙ карты, а `bAcceptInFlight`/`bCompleteInFlight` снимаются
	// только ответом сервера: после разрыва приём и сдача задания были бы заперты навсегда.
	// ★ Волна В3: снимок открытых окон снимается БЕЗ восстановления ДО `Close()` — иначе `Close()` переоткрыл бы
	//   окна посреди травела/разрыва (порядок сброса подсистем не гарантирован, поэтому явно здесь).
	//   ★ Волна В5 (ревизия В4 МЕЛК-1): это защита порта (1) ключа `asda.UI.WindowSnapshotGuards`; при 0 — как оригинал:
	//   флаги живут до восстановления (у эталона их обнуляют только конструктор и само восстановление).
	UAsdaWindowSubsystem* WMr = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (WMr && UAsdaWindowSubsystem::AreSnapshotGuardsEnabled())
	{
		WMr->ClearWindowSnapshot(TEXT("сброс сессии диалога NPC"));
	}
	Close();
	HideMultiQuest();
	HideQuestAcceptWindow();
	HideRewardPopup();
	HideQuestInfo();
	bDialogOpen = false;
	bEntityListDialogOpen = false;
	CurrentNpcId = -1;
	CurrentNpcRenderId = -1;
	CurrentTalkId = -1;
	CurrentPageIndex = -1;
	MenuCount = 0;
	PreStepIndex = -1; NextStepIndex = -1;
	SelIndex = -1;
	bMenuButtonPending = false;
	bRewardPopupMode = false;
	RewardPanelMode = 0;
	bScriptClickSessionFlag = false;
	bTalkPagePending = false;
	bAcceptInFlight = false;
	bCompleteInFlight = false;
	SavedTalkId = -1; SavedStep = -1; SavedSub = -1;
	ShopTabState = -1;
	GiveList.Reset(); TakeList.Reset();
	RewardBasic.Reset(); RewardSelect.Reset();
	RewardSelIndex = -1;
	RwTipHover = -1; RwTipTicks = 0;
	MqBaseGeom.Reset();
	MqSelCtrl = -1; MqSelIndex = -1;
	(void)Reason;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОКНО
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::ExpandBranch(UWidget* W)
{
	// ★ Импортёр JUI кладёт `Visible = "0"` как `Collapsed` и САМОМУ узлу, и его union-ГРУППЕ, и
	//   всем потомкам. Поэтому «показать» = пройти вверх по цепочке родителей до корня окна.
	for (UWidget* P = W; P; P = P->GetParent())
	{
		if (P->GetVisibility() == ESlateVisibility::Collapsed || P->GetVisibility() == ESlateVisibility::Hidden)
		{
			P->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

void UAsdaNpcDialogSubsystem::SetVis(UWidget* W, bool bVisible)
{
	if (!W) { return; }
	if (bVisible)
	{
		ExpandBranch(W);
		// Интерактиву нужен хит-тест, декорациям — нет (иначе подпись съедает клик по кнопке;
		//   закон `ui-button-hover-click-rule`).
		// ★★★ ДЕДУПЛИКАЦИЯ 2026-09-02. Здесь была своя копия правила
		//   («`W->IsA<UButton>() ? Visible : SelfHitTestInvisible`»). Правило одно на весь порт —
		//   `AsdaCommunityUi::ShownVisibilityFor`, откат `asda.UI.InteractiveHitTest 0`.
		//   ⚠ ЧТО МЕНЯЕТСЯ: в разметке окна `NPC_LINE NPC` (570, 27 узлов) полей ввода, галочек
		//   и комбобоксов НЕТ вовсе (`PushButton` 11, `StaticWindow` 10, `Text` 4, `ListBox` 1,
		//   `ResizeWindow` 1), поэтому у ассетных узлов поведение прежнее. Выигрывают ленты
		//   окна «Основные задания» `MqList00`/`MqList01` (2408/2410) — это `UScrollBox`,
		//   создаваемые кодом: раньше `SetVis` (строки 3113/3115) отдавал им
		//   `SelfHitTestInvisible`, и прокрутка колесом/перетаскиванием не работала.
		W->SetVisibility(AsdaCommunityUi::ShownVisibilityFor(W));
	}
	else
	{
		W->SetVisibility(ESlateVisibility::Collapsed);
	}
}

#if !UE_BUILD_SHIPPING
// ★ 31.10.2026 самотест GC-якорей (`asda.SelfTest.RelayAnchors`) — разбор у объявления.
TArray<UButton*> UAsdaNpcDialogSubsystem::DebugRecreateTalkKeepPopups()
{
	TArray<UButton*> Out;
	if (!EnsureWindow()) { return Out; }
	EnsureRewardWindow(); EnsureQuestAcceptWindow(); EnsureMultiQuestWindow(); EnsureQuestInfoWindow();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(kDlgWinId);   // окно разговора 570 больше не живо, попапы живы
	}
	EnsureWindow();   // штатный путь: окно не живо ⇒ `Relays.Reset()` + разметка заново
	Out = { RwEndBut.Get(), AcOkBut.Get(), MqClose.Get(), QiClose.Get() };
	return Out;
}
#endif

bool UAsdaNpcDialogSubsystem::EnsureWindow()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (bReady && WM->IsWindowLive(Win)) { return true; }

	// Окно пересоздаётся ⇒ ВСЕ указатели на его дерево мертвы (та же грабля, что у сцены гачи).
	NameText = nullptr; Portrait = nullptr; TalkLines = nullptr;
	TalkBoxGrp = nullptr; BubbleGrp = nullptr; FuncBoxGrp = nullptr;
	CloseBtn = nullptr; PreGrp = nullptr; NextGrp = nullptr; PreText = nullptr; NextText = nullptr;
	QaButtons.Reset(); QaTexts.Reset(); ShopButtons.Reset(); ShopTexts.Reset();
	Relays.Reset();

	Win = WM->OpenWindowByPath(kDlgWinId, kDlgWbp, kDlgZ, ESlateVisibility::SelfHitTestInvisible);
	if (!Win)
	{
		UE_LOG(LogAsdaNpcDlg, Error, TEXT("[NPC-диалог] WBP_NPC_LINE_NPC не загрузился (%s)"), kDlgWbp);
		return false;
	}
	WM->SetWindowVisible(kDlgWinId, false);   // корень 570 в .JUI объявлен `Visible="0"`
	SetupControls();
	// ★ Роспись в `CloseAllPlayerWindows` (порт эталонного списка `uiCloseAllWindows@0xD61960`):
	//   окно диалога закрывается своим собственным методом, со сбросом своего флага `bDialogOpen`.
	WM->RegisterWindowCloser(kDlgWinId, FSimpleDelegate::CreateUObject(this, &UAsdaNpcDialogSubsystem::CloseFromCloseAll));
	bReady = true;
	return true;
}

void UAsdaNpcDialogSubsystem::SetupControls()
{
	if (!Win) { return; }

	// ★ Контролы ищутся ПО `idc`, а не по IDNum (закон `jui-importer-widget-naming`): точное имя
	//   импортёр даёт только интерактиву и Text-контролам, планам — имя с двумя числовыми хвостами.
	auto Node = [&](const TCHAR* Exact, const TCHAR* Needle) { return AsdaUi::FindNodeByName(Win, Exact, Needle); };

	NameText   = Cast<UTextBlock>(Node(TEXT("NPC_TNAME"), TEXT("NPC_TNAME")));
	Portrait   = Cast<UImage>(AsdaUi::FindNodeByPrefix(Win, TEXT("NPC_POSI_")));
	TalkBoxGrp = Node(TEXT("NPC_TALKBOX_grp"), TEXT("NPC_TALKBOX_grp"));
	BubbleGrp  = Node(TEXT("NPC_Talkbox2_grp"), TEXT("NPC_Talkbox2_grp"));
	FuncBoxGrp = Node(TEXT("NPC_Function_Box_grp"), TEXT("NPC_Function_Box_grp"));
	CloseBtn   = Cast<UButton>(Node(TEXT("NPC_TALKCLOSE"), TEXT("NPC_TALKCLOSE")));
	PreGrp     = Node(TEXT("NPC_PRE_BUT_grp"), TEXT("NPC_PRE_BUT_grp"));
	NextGrp    = Node(TEXT("NPC_NEXT_BUT_grp"), TEXT("NPC_NEXT_BUT_grp"));

	auto Bind = [&](UButton* B, int32 CtrlId)
	{
		if (!B) { return; }
		AsdaUi::BindButton(this, B, CtrlId, [this](int32 Id) { OnDialogButton(Id); }, Relays);   // ★ U20: реле клика — общая точка
	};

	Bind(CloseBtn, 575);
	if (UButton* Pre  = Cast<UButton>(Node(TEXT("NPC_PRE_BUT"),  TEXT("NPC_PRE_BUT"))))  { Bind(Pre, 576); }
	if (UButton* Next = Cast<UButton>(Node(TEXT("NPC_NEXT_BUT"), TEXT("NPC_NEXT_BUT")))) { Bind(Next, 577); }

	for (int32 i = 0; i < 3; ++i)   // 578…580 — кнопки ШАГОВ страницы
	{
		const FString N = FString::Printf(TEXT("NPC_QA_BUT%02d"), i);
		UButton* B = Cast<UButton>(Node(*N, *N));
		QaButtons.Add(B);
		QaTexts.Add(Cast<UTextBlock>(Node(*(N + TEXT("_Text")), *(N + TEXT("_Text")))));
		Bind(B, 578 + i);
	}
	for (int32 i = 0; i < 5; ++i)   // 581…585 — кнопки МЕНЮ сценария
	{
		const FString N = FString::Printf(TEXT("NPC_SHOP_BUT%02d"), i);
		UButton* B = Cast<UButton>(Node(*N, *N));
		ShopButtons.Add(B);
		ShopTexts.Add(Cast<UTextBlock>(Node(*(N + TEXT("_Text")), *(N + TEXT("_Text")))));
		Bind(B, 581 + i);
	}

	// ── ★ D2: кнопки 579 и 580 в `.JUI` стоят В ОДНОЙ ТОЧКЕ (обе Origin 18,59, 150×32), поэтому при
	//   исправленном D1 (см. FillStepButtons) они бы наложились. Разводим по Y на высоту кнопки.
	//   ⚠ В данных ЭТОЙ сборки stepCount не превышает 2 (проверено на всех 2607 страницах), то есть
	//     третья кнопка шага не включается никогда — исправление сделано «на вырост» и вреда не несёт.
	if (QaButtons.Num() > 2 && QaButtons[1] && QaButtons[2])
	{
		UCanvasPanelSlot* S1 = AsdaUi::SlotOf(QaButtons[1]);
		UCanvasPanelSlot* S2 = AsdaUi::SlotOf(QaButtons[2]);
		if (S1 && S2)
		{
			const FVector2D P1 = S1->GetPosition();
			const FVector2D Sz = S1->GetSize();
			S2->SetPosition(FVector2D(P1.X, P1.Y + Sz.Y));
			if (UCanvasPanelSlot* T2 = QaTexts.IsValidIndex(2) ? AsdaUi::SlotOf(QaTexts[2]) : nullptr)
			{
				if (UCanvasPanelSlot* T1 = QaTexts.IsValidIndex(1) ? AsdaUi::SlotOf(QaTexts[1]) : nullptr)
				{
					T2->SetPosition(FVector2D(T1->GetPosition().X, T1->GetPosition().Y + Sz.Y));
				}
			}
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] FIX-ORIG D2: NPC_QA_BUT02 сдвинута под NPC_QA_BUT01 (были в одной точке %.0f,%.0f)"),
				P1.X, P1.Y);
		}
	}

	// ── тело реплики: в `.JUI` контрол 574 `NPC_TTALK` — ListBox (скин `Npc_List`), импортёр
	//   создал от него только ХИТ-ЗОНУ `NPC_TTALK_12_HIT`. Строки складываем в UScrollBox,
	//   положенный в ТУ ЖЕ группу и на ТОТ ЖЕ прямоугольник — тогда он едет вместе с окном.
	if (UWidget* Hit = AsdaUi::FindNodeByPrefix(Win, TEXT("NPC_TTALK_")))
	{
		UCanvasPanel* Host = Cast<UCanvasPanel>(Hit->GetParent());
		UCanvasPanelSlot* HS = AsdaUi::SlotOf(Hit);
		if (Host && HS && Win->WidgetTree)
		{
			TalkLines = Win->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("NPC_TTALK_Lines"));
			if (TalkLines)
			{
				TalkLines->SetOrientation(Orient_Vertical);
				TalkLines->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
				TalkLines->SetScrollBarVisibility(ESlateVisibility::Collapsed);
				TalkLines->SetVisibility(ESlateVisibility::Visible);
				if (UCanvasPanelSlot* CS = Host->AddChildToCanvas(TalkLines))
				{
					AsdaUi::CopyCanvasGeometry(HS, CS);   // дедуп Д6: «встань на место контрола-места»
					CS->SetZOrder(5);
				}
				TalkWrapWidth = FMath::Max(40.f, (float)HS->GetSize().X - kTalkWrapMargin);
			}
		}
	}

	// ★★★ ПОДПИСИ 615/616 «Назад»/«Далее» — БОЛЬШЕ НЕ СТРОЯТСЯ КОДОМ (2026-08-27).
	//   ЧТО БЫЛО ЗДЕСЬ. Лямбда `MakeArrowCaption` создавала два `UTextBlock`
	//   (`NPC_PRE_BUT_Caption` / `NPC_NEXT_BUT_Caption`) в группах кнопок 576/577, ставила им
	//   анкор 0..1 + `FMargin(0, 6, 0, 0)`, чёрный цвет и текст «Назад»/«Далее» строкой в коде.
	//   Основанием был дамп WBP, где узлов не было: у контролов 615/616 в `.JUI` пустые `ID` и
	//   `CaptionText`, а текст лежит в блоке `EXS TextContent` — импортёр такую пару не видел.
	//   ПОЧЕМУ УБРАНО. Этот дефект импортёра ЗАКРЫТ (класс S1a): шаг
	//   `Tools/asda_ui_missing_text_nodes.py` и сам импортёр теперь создают такие узлы по общему
	//   правилу `{ЯКОРЬ}_TXT{NN}`. В `WBP_NPC_LINE_NPC` они уже есть — `NPC_PRE_BUT_TXT00`
	//   («Назад») и `NPC_NEXT_BUT_TXT00` («Далее»), геометрия и цвет прямо из данных
	//   (`gamemain_jui.ini:52880` и далее: 615 `Origin "-6 9" Size "100 15"`,
	//   616 `Origin "-9 8" Size "100 15"`, оба `TextAlign "5"`, `TextColor "FF000000"`).
	//   Код же клал ВТОРУЮ подпись поверх первой, да ещё с другой геометрией — то есть двоил текст.
	//   ГДЕ ЖИВЁТ ТЕПЕРЬ. В самом ассете: и узлы, и текст, и цвет. Здесь остаётся только взять их.
	//   ⚠ ПОДПИСЬ ШАГА СЮДА ПО-ПРЕЖНЕМУ НЕ ИДЁТ. У шага со состоянием 2/3/4 в данных лежит своя
	//   подпись («Accept», «End»…), но эталон её на стрелках НЕ показывает — там всегда
	//   статический текст из `.JUI`. Менять это без основания нельзя (в списке дефектов D1…D9
	//   такого пункта нет).
	PreText  = Cast<UTextBlock>(AsdaUi::FindNodeByName(Win, TEXT("NPC_PRE_BUT_TXT00"),  nullptr));
	NextText = Cast<UTextBlock>(AsdaUi::FindNodeByName(Win, TEXT("NPC_NEXT_BUT_TXT00"), nullptr));
	if (!PreText || !NextText)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] подписи стрелок 615/616 не найдены в ассете (пред=%d след=%d) — "
			     "прогони Tools/asda_ui_missing_text_nodes.py"),
			PreText ? 1 : 0, NextText ? 1 : 0);
	}

	// ★★★ ОБВОДКА ТЕКСТА В ОКНЕ 570 — СНЯТА (замечание владельца 2026-08-20, разбор
	//   `Reports/npc_visual_fix.md` §1). Обводку в оригинале даёт ТОЛЬКО поле 926 контрола
	//   (`CClass_SetField926(ctrl, 0x800000)`, отрисовка — font_text.c:6952); поле 925 `TextOutLine`
	//   из скина — это флаги ВЫВОДА (перенос/обрезка), а не контур (ui_core.c:110741-110746).
	//   Для контролов окна 570 эталон 926 НЕ ставит НИ РАЗУ (просмотрены все 254 вызова
	//   `CClass_SetField926` в срезе; строитель окна — ui_core.c:59337…59700) ⇒ обводки нет ни у
	//   имени 573, ни у списка реплики 574, ни у подписей кнопок.
	//   ⚠ Снимаем В РАНТАЙМЕ, а не в ассете: обводку запекает импортёр JUI
	//   (`Tools/asda_jui_generic_umg.py:261`), и после любого переимпорта она вернулась бы
	//   (грабля `tools-no-import-side-effects`). Прецедент — облачко чата.
	//   ★ ПОРЯДОК ВАЖЕН: имя 573 чистится ЗДЕСЬ, до первой реплики, потому что строки реплики и
	//   строки облачка 2406 берут шрифт именно у него (`SetFont(NameText->GetFont())`) — иначе
	//   обводка приезжает к ним «зайцем» вместе со шрифтом.
	// ★★★ УБРАНО 2026-08-27: это ПОСТОЯННЫЕ узлы окна 570, и обводка у них снята уже В АССЕТЕ
	//   (шаг `Tools/asda_ui_text_style926.py`; окну `NPC_LINE NPC` обводка по данным оригинала
	//   не положена ни одному контролу). Прежняя оговорка «снимаем в рантайме, иначе переимпорт
	//   вернёт» больше не действует: ошибочное правило импортёра отключено, а правка ассета
	//   оформлена идемпотентным шагом пост-импорта и переживает перегенерацию.
	//   ★ Наследование шрифта строками реплики и облачка (`SetFont(NameText->GetFont())`) теперь
	//   тоже безопасно: у `NameText` в ассете обводки нет, значит «зайцем» она не приедет.

	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] контролы разобраны: имя=%d портрет=%d тело=%d закрыть=%d шаги=%d/%d меню=%d/%d стрелки=%d/%d"),
		NameText ? 1 : 0, Portrait ? 1 : 0, TalkLines ? 1 : 0, CloseBtn ? 1 : 0,
		QaButtons.Num(), QaTexts.Num(), ShopButtons.Num(), ShopTexts.Num(), PreGrp ? 1 : 0, NextGrp ? 1 : 0);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ДАННЫЕ
// ════════════════════════════════════════════════════════════════════════════════════════════════
const FAsdaNpcQuestNpcRow* UAsdaNpcDialogSubsystem::FindNpcRow(int32 NpcId) const
{
	return AsdaDt::RowByInt<FAsdaNpcQuestNpcRow>(kDtNpc, NpcId, TEXT("NpcDialog"));
}

const FAsdaNsrHeaderRow* UAsdaNpcDialogSubsystem::FindHeader(const FString& NsrName) const
{
	return AsdaDt::Row<FAsdaNsrHeaderRow>(kDtHeader, FName(*NsrName), TEXT("NpcDialog"));
}

const FAsdaNsrPageRow* UAsdaNpcDialogSubsystem::FindPage(const FString& NsrName, int32 TalkId) const
{
	return AsdaDt::Row<FAsdaNsrPageRow>(kDtPage, FName(*FString::Printf(TEXT("%s_%d"), *NsrName, TalkId)),
		TEXT("NpcDialog"));
}

const FAsdaNsrButtonRow* UAsdaNpcDialogSubsystem::FindButton(const FString& NsrName, int32 Index) const
{
	return AsdaDt::Row<FAsdaNsrButtonRow>(kDtButton, FName(*FString::Printf(TEXT("%s_%d"), *NsrName, Index)),
		TEXT("NpcDialog"));
}

// Порт `npcCraftSlotClick_Handle@0xD9C470`: `for (j = 0; j < 64; ++j) if (nsrCmd(j)[0] == 121) …`.
//   Записи сценария лежат в `DT_NpcQuestScript` (импортёр отбрасывает те, что НИКОГДА не
//   срабатывают; opcode 121 в число отброшенных не входит — он в списке «полезных»).
int32 UAsdaNpcDialogSubsystem::FindStartTalkId(const FString& NsrName) const
{
	const UDataTable* Dt = AsdaDt::Cached(kDtScript);
	if (!Dt) { return -1; }
	int32 Best = -1;
	int32 BestRec = MAX_int32;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaNpcQuestScriptRow* R = reinterpret_cast<const FAsdaNpcQuestScriptRow*>(It.Value);
		if (!R || R->Opcode != kNsrOpcodeStartPage || R->NsrName != NsrName) { continue; }
		// Эталон берёт ПЕРВУЮ подошедшую запись в порядке индекса слота — воспроизводим порядок.
		if (R->RecIndex < BestRec) { BestRec = R->RecIndex; Best = R->PageId; }
	}
	return Best;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ОТКРЫТИЕ (порт NPC_OpenWindow + npcCraftSlotClick_Handle + regionWindow_OpenAndPathMove)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Удержание таблиц диалога на сессию (15.09.2026, dt_gc_hold) — обоснование в объявлении. Пути — константы этого файла.
int32 UAsdaNpcDialogSubsystem::HoldGameTables()
{
	static const FName Group(TEXT("NpcDialog"));
	const TCHAR* const Paths[] = { kDtNpc, kDtScript, kDtHeader, kDtButton, kDtPage, kDtTalk,
		kDtWarpList, kDtNpcSnq, kDtSnqIct, kDtCallMob, kDtEventItem, kDtEventItemServer };
	const int32 N = HeldTables.Hold(Group, Paths);
	// Окно приёма 750 берёт награды общей точкой журнала (`UAsdaQuestLogSubsystem::BuildRewardLines`) — её таблицы держит журнал.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (const UAsdaQuestLogSubsystem* QLog = GI->GetSubsystem<UAsdaQuestLogSubsystem>()) { QLog->HoldGameTables(); }
	}
	return N;
}

bool UAsdaNpcDialogSubsystem::OpenForNpc(AAsdaNpcActor* Npc)
{
	if (!Npc) { return false; }
	// ★ 28.10.2026 ДОСКА ОБЪЯВЛЕНИЙ: у NPC типа 5 эталон зовёт `NPC_SpecialAction_Type5` ВМЕСТО `uiPanelSelectTab(npc, 1, 0)` +
	//   `NPC_OpenWindow` (input.c:336, entity_char.c:18316/18945, ui_core.c:1124) — ни жеста разговора, ни окна 570, ни его гейтов.
	//   Развилка здесь — до `SetEntityAnimState(1)` (жест — часть ветки обычного разговора).
	if (UAsdaBoardQuestSubsystem::IsEnabled())
	{
		if (const FAsdaNpcQuestNpcRow* Rec = FindNpcRow(Npc->NpcId); Rec && Rec->NpcType == 5)
		{
			UGameInstance* GI = GetGameInstance();
			UAsdaBoardQuestSubsystem* Board = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
			return Board && Board->OpenForNpc(Npc->NpcId);
		}
	}
	CurrentNpcRenderId = Npc->NetRenderId;
	CurrentNpcActor = Npc;
	// ═══ `uiPanelSelectTab(npcEntity, 1, 0)` ═══ (ui_core.c:1131 и entity_char.c:18321 — ОБА пути
	//   разговора зовут её ПЕРЕД `NPC_OpenWindow`). Это смена анимационного состояния сущности NPC на
	//   1 = «разговор»: собеседник проигрывает свой жест вместо idle. Разбор таблицы состояние→клип
	//   и все гейты — в `AAsdaNpcActor::SetEntityAnimState`.
	Npc->SetEntityAnimState(1);
	return OpenForNpcId(Npc->NpcId);
}

bool UAsdaNpcDialogSubsystem::OpenForNpcId(int32 NpcId)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return false; }
	HoldGameTables();   // ★ 15.09 (dt_gc_hold): таблицы — сильно у подсистемы на сессию

	// ═══ ★ 28.10.2026 ВЕТКА ТИПА-ФУНКЦИИ 5 — ДОСКА ОБЪЯВЛЕНИЙ, ДО ГЕЙТОВ ОКНА 570 ═══
	//   Эталон (input.c:336, entity_char.c:18316/18945, ui_core.c:1124): `if (npc[182] == 5) NPC_SpecialAction_Type5(g_pQuestCraftWnd,
	//   npcId); else NPC_OpenWindow(…)` — гейты `NPC_OpenWindow`/`regionWindow_OpenAndPathMove` (обмен, витрина, разборка, бустер,
	//   свиток склада, «окно уже открыто») к доске НЕ применяются. Окно — `UAsdaBoardQuestSubsystem` (спецификация
	//   `Docs/Port/systems/BOARD_QUEST_FLOW.md`). Ключ `asda.Board.Enabled 0` — прежнее поведение (отказ с логом ниже).
	if (UAsdaBoardQuestSubsystem::IsEnabled())
	{
		if (const FAsdaNpcQuestNpcRow* Rec5 = FindNpcRow(NpcId); Rec5 && Rec5->NpcType == 5)
		{
			UAsdaBoardQuestSubsystem* Board = GI->GetSubsystem<UAsdaBoardQuestSubsystem>();
			return Board && Board->OpenForNpc(NpcId);
		}
	}

	// ═══ ГЕЙТЫ `NPC_OpenWindow@0xD99D80` (npc_mob_pet.c:2470-2496, дизасм 0xD99D8F…0xD99E96) ═══
	//   ★★★ ВСЕ ШЕСТЬ РАЗОБРАНЫ 2026-08-20 (`Reports/npc_visual_fix.md` §4.5). Прежняя запись
	//   «поля чужих объектов не разобраны» БЫЛА ВЕРНА ЛИШЬ ОТЧАСТИ, и один из гейтов был понят
	//   НЕВЕРНО. Порядок эталона и текущее состояние:
	//
	//   1. `g_bPartyInviteFlag == 1` (0x2BF965C) — молча. ★★★ ПОПРАВКА 29.09.2026 (волна 0 торговли,
	//      `Reports/trade/trade_re_report.md` §0 п.3): прежний вывод «ветка мёртвая» — ЛОЖНОЕ «НЕТ». Байт-скан искал
	//      абсолютный адрес, а писатель пишет через объект: 0x2BF965C = `CExchange(0x2BF9630)+0x2C`, и
	//      `uiWindow_OpenWithGuards` ставит `mov [eax+0x2C],1` @0xC75A72 — это признак «ОКНО обмена 2008 открыто».
	//      Реализовано НИЖЕ через общую точку `UAsdaTradeStateSubsystem::IsExchangeWindowOpen` (пока окна 2008 нет — false).
	//   2. `g_pPartyTargetObj` (0x2BE779C) → нотис 12920. ★ Имя-миснеймер: этот адрес =
	//      `g_pShopWnd(0x2BE5730) + 0x206C` = поле 2075, «открыт ЭЛИТНЫЙ МАГАЗИН (Shell Item
	//      Store)» (писатели `uiInventoryOpenWindow@0xC3DCB0` → 1, `uiInventoryResetAndLeave@0xC3DE00`
	//      → 0; текст 12920 — «Открыт элитный магазин, разговор невозможен»). Системы в порту нет.
	//   3. `CObject_GetField166(g_pHairShopWnd)` = `+0x298` → нотис 13009: «сессия парикмахерской
	//      открыта» (`set_oA6@0xCEF110` из `uiHairShopWindowOnShow@0xCEEE90` → 1). Системы нет.
	//   4. `CObject_GetField254(g_pInventoryWnd)` = `+0x3F8` → нотис 13101. ★★★ ЭТО НЕ «ОТКРЫТ
	//      ИНВЕНТАРЬ», как считалось раньше, а «ОТКРЫТО ОКНО БУСТЕРА/БАРАБАНА» (JUI 10050):
	//      писатель `set_oFE@0xD1F7D0` зовут `itemBoosterWnd_Open@0xD1F4A0` (→1) и
	//      `boosterApplyResult@0xD1F830` = `tagBoosterShop_CloseWindow` (→0). Текст нотиса это
	//      подтверждает дословно: «Нельзя разговаривать … во время применения бустера».
	//      ⇒ Владелец гейта — `UAsdaBoosterPanelSubsystem`, и он В ПОРТУ ЕСТЬ. РЕАЛИЗОВАН НИЖЕ.
	//      (Тот же гейт порт уже применяет в хотбаре — `hotbar_input_fix.md`, правка 2026-08-15.)
	//   5. `CObject_GetField5581(g_pWarehouseWnd)` = `+0x5734` = `CWarehouseWnd::isOpen` → нотис
	//      10306 (`uiWindowOpen_EA20@0xE8EA20` → 1, `uiHidePanelAndChild@0xE8EAB0` → 0). Системы нет.
	//   6. «окно разговора уже открыто» (`cmp [this+0x400C], 0`) — молча. Реализовано.
	//   Чат-параметры всех нотисов эталона одинаковы: цвет −11514, время 10000.
	//   Оставшиеся 2/3/5 ждут свои подсистемы — строка 37 реестра переписана готовыми условиями.
	// Гейт 1 (★ 29.09.2026): окно обмена 2008 открыто → МОЛЧА, разговор не открывается (npc_mob_pet.c:2477).
	if (UAsdaTradeStateSubsystem::IsExchangeWindowOpen(GI))
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] npcId=%d: открыто окно обмена 2008 — разговор молча не открыт (NPC_OpenWindow, npc_mob_pet.c:2477)"), NpcId);
		return false;
	}
	// ★ Волна Л5 торговли (30.09.2026): [ОРИГИНАЛ] `regionWindow_OpenAndPathMove@0xD99290` (ui_core.c:61106-61114) — последний шаг
	//   открытия: «остановка» идёт `fxEmitter_SetSpawnPosGuarded`/`mapClick_BeginPathMove` (оба при открытой лавке молча ничего не
	//   делают), а окно разговора показывается только при `!sub_929DC4(g_bSomeFeatureEnabled)`. Витрина 23000 открыта ⇒ МОЛЧА: ни
	//   окна, ни `this[4099] = 1`, ни «закрыть все окна» (витрина остаётся). [ПОРТ] Проверка поднята к гейтам входа: все шаги между
	//   ними и `regionWindow_OpenAndPathMove` у эталона без видимых следов (поиск записи NPC и стартовой страницы) — итог тот же.
	if (UAsdaTradeStateSubsystem::IsShopDlgOpen(GI))
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] npcId=%d: открыта витрина личной лавки 23000 — разговор молча не открыт (regionWindow_OpenAndPathMove, ui_core.c:61112)"), NpcId);
		return false;
	}
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: первые строки `regionWindow_OpenAndPathMove@0xD99290` — `uiSelectFirstValidTab(книга)`
	//   (раздел книги и «?») и гейт `if (sub_B79200(книга)) return` — пока открыта полоса книги 69200, разговор молча не открывается.
	if (!AsdaTutorial::OnNpcTalkOpening(GI))
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] npcId=%d: открыта книга обучения 69200 — разговор молча не открыт (regionWindow_OpenAndPathMove)"), NpcId);
		return false;
	}
	// ★ Волна 2-А (06.10.2026): [ОРИГИНАЛ] `regionWindow_OpenAndPathMove@0xD99290` (ui_core.c:61095) — всё открытие стоит под
	//   `if (!g_pRecvUiHandler3[0] || *(BYTE*)(g_pRecvUiHandler3[0] + 292) != 1)`: окно разборки 32000 ВИДНО ⇒ МОЛЧА, разговора нет.
	if (const UAsdaDisassemblySubsystem* Ds = GI->GetSubsystem<UAsdaDisassemblySubsystem>())
	{
		if (Ds->IsOpen())
		{
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] npcId=%d: открыто окно разборки 32000 — разговор молча не открыт (regionWindow_OpenAndPathMove, ui_core.c:61095)"), NpcId);
			return false;
		}
	}
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: сразу за гейтом окна разборки — `byte_2C715EB = 1` (ui_core.c:61097), байт 83 гида.
	AsdaTutorial::Mark(GI, EAsdaTutorialFlag::NpcTalkOpen);
	if (const UAsdaBoosterPanelSubsystem* Booster = GI->GetSubsystem<UAsdaBoosterPanelSubsystem>())
	{
		if (Booster->IsWindowOpen())   // эталонное `CObject_GetField254(...) != 0`
		{
			if (UAsdaMessageSubsystem* M = GI->GetSubsystem<UAsdaMessageSubsystem>())
			{
				M->PostLoc(EAsdaMsgCategory::Quest, kLocNoTalkWhileBooster);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка («[#id]»)
			}
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] npcId=%d: открыт барабан бустера (окно 10050) — разговор запрещён, нотис %d (эталон)"),
				NpcId, kLocNoTalkWhileBooster);
			return false;
		}
	}
	// ★ Гейт 5 (реализован 2026-08-27 вместе с окном склада): `CWarehouseWnd::isOpen` (+0x5734).
	//   ★ ВАЖНОЕ УТОЧНЕНИЕ разбора: признак взводит НЕ открытие склада у кладовщика, а активный
	//   СВИТОК ВОЗВРАТА СКЛАДА (`uiWindowOpen_EA20@0xE8EA20` → 1). Текст нотиса 10306 это и
	//   говорит дословно: «нельзя разговаривать с NPC, пока используется свиток возврата склада».
	//   Поэтому спрашиваем именно `IsRecallActive()`, а не `IsOpen()`: окно, открытое кнопкой
	//   самого кладовщика, разговору с ним мешать не должно.
	if (const UAsdaWarehouseSubsystem* Ware = GI->GetSubsystem<UAsdaWarehouseSubsystem>())
	{
		if (Ware->IsRecallActive())
		{
			const_cast<UAsdaWarehouseSubsystem*>(Ware)->NoticeRecallBlocked(10306);
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] npcId=%d: активен свиток возврата склада — разговор запрещён, нотис 10306 (эталон)"),
				NpcId);
			return false;
		}
	}
	if (bDialogOpen)
	{
		UE_LOG(LogAsdaNpcDlg, Verbose, TEXT("[NPC-диалог] окно уже открыто — повторное открытие игнорировано (эталон)"));
		return false;
	}

	const FAsdaNpcQuestNpcRow* NpcRec = FindNpcRow(NpcId);
	if (!NpcRec)
	{
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] npcId=%d нет в DT_NpcQuestNpc — окно не открываем"), NpcId);
		return false;
	}
	// ═══ ★ ВЕТКА ТИПА-ФУНКЦИИ 5 (`if (targetNode[182] == 5) NPC_SpecialAction_Type5(...)`) ═══
	//   ui_core.c:1123 и entity_char.c:18314. У этого типа окно РАЗГОВОРА (570) НЕ открывается вовсе:
	//   `NPC_SpecialAction_Type5@0xEE95B0` наполняет и показывает СОВСЕМ ДРУГОЕ окно — `g_pQuestCraftWnd`
	//   (список входов, строки берутся `insDungeon_GetSlotRecord`, заголовок — нотис 3597), то есть
	//   доску рейдов/подземелий. В данных таких NPC ровно два (id 9 `andres` и id 10 `10npc`, у обоих
	//   модель `mdel_raidboard-a_00`).
	//   ★ 28.10.2026: окно доски ЕСТЬ (`UAsdaBoardQuestSubsystem`, развилка — в начале функции, до гейтов). Сюда тип 5 доходит
	//   только при выключенной доске (откат) — тогда честный отказ, как было, а НЕ подмена окном 570.
	//   ⚠ Прежние слова «доска рейдов/подземелий» и «insDungeon» — миснеймы IDA: это доска «ограниченных заданий» 20000…20056.
	if (NpcRec->NpcType == 5)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] npcId=%d «%s»: тип-функции 5 — эталон открывает НЕ окно 570, а доску объявлений ")
			TEXT("75000 (NPC_SpecialAction_Type5); доска выключена ключом — окно не открываем"),
			NpcId, *NpcRec->ModelName);
		return false;
	}

	const FAsdaNsrHeaderRow* Hdr = FindHeader(NpcRec->NsrName);
	if (!Hdr)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] у npcId=%d сценарий «%s» отсутствует в DT_NpcNsrHeader — окно не открываем"),
			NpcId, *NpcRec->NsrName);
		return false;
	}
	const int32 StartTalk = FindStartTalkId(NpcRec->NsrName);
	// ★★★ ВОЛНА 1-А (06.10.2026): ВТОРАЯ ВЕТКА СТАРТА ЭТАЛОНА. [ОРИГИНАЛ] `npcCraftSlotClick_Handle@0xD9C470`
	//   (AsdaStory_game.c:397116-397133): `if (this+0x498 != 1 || npc_list[i]+124 != 0)` → `uiCraftSlotClickHandler`
	//   с `field2_object = 1` — ПЕРЕБОР ЗАПИСЕЙ СЦЕНАРИЯ (порт — `RunScriptRecordScan`); и только иначе — страница opcode 121.
	//   `npc_list+124` (= +0x7C) — ТИП NPC (`NpcType`, импортёр `asda_import_npc_quest_data.py`): у 74 NPC он не 0,
	//   среди них «Статуя души» 106 (тип 3) — единственный вход кнопки 30023, и у её сценария записи 121 НЕТ ВОВСЕ.
	//   Порт знал только ветку 121 и такие NPC не открывал.
	// ★★★ РЕВИЗИЯ 1-А §7 (06.10.2026): `this+0x498` — «ПЕРВАЯ ВСТРЕЧА». Его ставит `toggle_SelectOrClear@0xD94A30` из
	//   `NPC_OpenWindow` (AsdaStory_game.c:396043): байт `this+0x4B0+npcId` равен 0 → `+0x498 = 1`, `+0x4A0 = npcId`;
	//   иначе `+0x498 = 0`, `+0x4A0 = −1`. Байт пишет ТОЛЬКО уход от NPC (`UI_resetAllWindowsAndLeave_5045@0xD999AA`,
	//   вместе с 5045, у NPC типа 0) — см. `Close()`. Итог 1:1: страница 121 — только при ПЕРВОЙ встрече с NPC типа 0
	//   за запуск клиента; повторные встречи и NPC ненулевого типа — всегда перебор записей сценария.
	//   [ОРИГИНАЛ] toggle зовётся до загрузки сценария; порт ставит флаг здесь же, до проверок страницы.
	bFirstMeet       = !IsNpcMet(NpcId);
	FirstMeetNpcId   = bFirstMeet ? NpcId : -1;
	FirstMeetNpcType = NpcRec->NpcType;
	const bool bSweepOnOpen = !bFirstMeet || (NpcRec->NpcType != 0);
	if (!bSweepOnOpen && StartTalk < 0)
	{
		// [ПОРТ] Окно не открылось — флаг первой встречи снимаем сразу: в оригинале он дожил бы до ближайшего ухода от
		//   NPC (5045 по НЕоткрытому NPC). В данных этой сборки у всех NPC типа 0 запись 121 есть — ветка недостижима.
		bFirstMeet = false; FirstMeetNpcId = -1;
		// Эталон в этом случае просто выходит из `npcCraftSlotClick_Handle`, не показав окна.
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог] у сценария «%s» нет записи с opcode %d (стартовая страница) — эталон окно не показывает"),
			*NpcRec->NsrName, kNsrOpcodeStartPage);
		return false;
	}

	if (!EnsureWindow()) { return false; }

	CurrentNpcId = NpcId;
	CurrentNsr = NpcRec->NsrName;
	MenuCount = Hdr->MenuCount;

	// ═══ `regionWindow_OpenAndPathMove@0xD99290`, шаг 5: ОСТАНОВИТЬ ИГРОКА ═══
	//   Эталон гасит движение path-move'ом «в собственную позицию». Стоп-ПАКЕТ (4008) в этот момент
	//   уже отправлен вызывающей стороной (веткой клика или тиком прибытия), поэтому здесь только
	//   ЛОКАЛЬНЫЙ сброс маршрута — второй 4008 был бы дублем пакета из колбэка
	//   (закон `no-reentrant-net-callbacks`).
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()))
	{
		PC->CancelRoute();
		PC->ClearPendingNpcTalk(TEXT("окно диалога открыто"));
	}

	// ═══ шаг 8: `uiCloseAllWindows(g_pUIManager)` ═══
	//   ⚠ Парность обязательна: `CloseAllPlayerWindows` ещё и БЛОКИРУЕТ хотбар и сворачивает трей
	//   главного меню (порт эталонного списка). Обратное действие — `RestoreAfterSceneExit` —
	//   зовётся в `Close()`; без него хотбар остался бы заблокированным навсегда.
	UAsdaWindowSubsystem* WM = GI->GetSubsystem<UAsdaWindowSubsystem>();
	// ★ Волна В3 ремесла (25.09.2026): эталонный шаг 8 — `uiCaptureOpenWindowsState(this)` ПЕРЕД `uiCloseAllWindows`
	//   (ui_core.c:61117): снимок открытых окон, которые вернёт уход от NPC (`Close()` ниже). Общая точка —
	//   `UAsdaWindowSubsystem::CaptureOpenWindows` (ключ `asda.UI.WindowSnapshot`).
	if (WM) { WM->CaptureOpenWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("вход в диалог NPC")); }
	if (WM) { WM->CloseAllPlayerWindows(); }

	bDialogOpen = true;   // `this[4099] = 1`
	// ═══ `sub_91EB81(g_pEntityList, 1)` (`regionWindow_OpenAndPathMove@0xD99290`, ui_core.c:61123) ═══
	//   Переходник на `sub_D99580@0xD99580`: `*(dword*)(g_pEntityList + 0x1048) |= 1`. Это ЕДИНСТВЕННОЕ
	//   место, где бит ставится, и снимают его закрыватели диалога — значит бит 0 = «диалог NPC
	//   открыт» (парное снятие — в `Close()`, там же полный список эталонных точек сброса).
	bEntityListDialogOpen = true;
	// ═══ ★ 30.10.2026 (выбор армии): `if (itemIsCategory133or179_184(npcId)) Clan_Send_Op6700(g_pCampWarClan, 0)` ═══
	//   (`regionWindow_OpenAndPathMove`, AsdaStory_game.c:395827-395828 — следом за `npcExchangeUiResetFull`). Разговор с
	//   NPC войны 133/179/180/184 запрашивает сведения 6700 → 6701: из них окно выбора армии 44000 (кнопка 30056) берёт
	//   численность армий. Вся логика — `UAsdaFactionSelectSubsystem::OnNpcTalkOpened`.
	if (UAsdaFactionSelectSubsystem* Faction = GI->GetSubsystem<UAsdaFactionSelectSubsystem>())
	{
		Faction->OnNpcTalkOpened(NpcId);
	}

	// ═══ шаги 9 и 13: показать 573 (имя), 570 (корень), 572 (пузырь) ═══
	if (WM)
	{
		WM->SetWindowVisible(kDlgWinId, true);
		WM->BringWindowToFront(kDlgWinId);   // `CClass_MapInsert(g_pWindowMgr, this[4244])`
	}
	// ★ Риск №1 плана: корень 570 в `.JUI` объявлен `Visible="0"`, и импортёр протащил `Collapsed`
	//   на ВСЁ поддерево. После показа окна разворачиваем нужные группы ЯВНО.
	SetVis(TalkBoxGrp, true);    // 571 `NPC_TALKBOX` — в эталоне виден «сам собой» (дефект D8)
	SetVis(BubbleGrp, true);     // 572 `NPC_Talkbox2` — эталон показывает явно, шаг 13
	SetVis(FuncBoxGrp, true);    // `NPC_Function_Box` — в нём живёт `NPC_TNAME` (573)
	if (Portrait) { SetVis(Portrait, true); }

	FillPortrait(*NpcRec);

	// ═══ стартовая страница (`npcCraftSlotClick_Handle`) ═══
	if (bSweepOnOpen)
	{
		// ★ Волна 1-А: перебор записей сценария (`uiCraftSlotClickHandler`, ветка `field2_object`). Эталон при пустом
		//   переборе всё равно показывает окно со СТАРОЙ страницей (от прошлого разговора, хоть с другим NPC); [ПОРТ] такое
		//   окно игроку ничего не даст, поэтому закрываем разговор и пишем причину. Для персонажа уровня ≥ 1 не ожидается:
		//   у каждого сценария есть запись 122 или запись только с условием «уровень 1..150» (сверка `NpcQuestScript.csv`);
		//   офлайн-персонаж уровня 0 так закрывает 17 NPC ненулевого типа (ревизия 1-А, МЕЛК-1).
		RunScriptRecordScan();
		if (CurrentTalkId < 0)
		{
			UE_LOG(LogAsdaNpcDlg, Warning,
				TEXT("[NPC-диалог] npcId=%d «%s»: перебор записей сценария при открытии ничего не выбрал — разговор закрыт"),
				NpcId, *CurrentNsr);
			Close();
			return false;
		}
	}
	else
	{
		RenderPage(StartTalk);
	}

	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] открыт: npcId=%d «%s» сценарий «%s» заголовок «%s» страниц=%d кнопок меню=%d старт talkId=%d"),
		NpcId, *NpcRec->ModelName, *CurrentNsr, *Hdr->Title, Hdr->EntryCount, MenuCount, CurrentTalkId);
	return true;
}

// Порт `UI_resetAllWindowsAndLeave_5045@0xD995C0` в части, относящейся к самому диалогу.
void UAsdaNpcDialogSubsystem::Close()
{
	if (!bDialogOpen) { return; }   // условие «открыт» живёт внутри закрывателя (контракт реестра)
	// ★ 10.11.2026 [ОРИГИНАЛ] обучение: `UI_resetAllWindowsAndLeave_5045` (ui_core.c:61166) — байт 84 гида («ушёл от NPC»).
	AsdaTutorial::Mark(GetGameInstance(), EAsdaTutorialFlag::NpcTalkLeave);
	bDialogOpen = false;
	bHiddenForChild = false;        // разговор окончен — окно 570 больше не «спрятано под дочерним окном»
	TGuardValue<bool> InCloseGuard(bInClose, true);   // закрытия дочерних окон ниже не зовут 5045 повторно
	CurrentTalkId = -1;             // `this[7] = -1`
	CurrentPageIndex = -1;          // `this[14] = -1`
	PreStepIndex = NextStepIndex = -1;
	QaSlot[0] = QaSlot[1] = QaSlot[2] = -1;   // `this[4204…4206] = -1`
	MenuCount = 0;
	// Э3…Э5: то же, что гасит `UI_resetAllWindowsAndLeave_5045` — выбор, режим попапа, оба списка.
	SelIndex = -1;
	bMenuButtonPending = false;
	bRewardPopupMode = false;                 // `this[437] = 0`
	// `UI_resetAllWindowsAndLeave_5045@0xD999DC` гасит флаг сессии `[this+0x49C]`; одноразовый
	//   `[this+0x4A4]` к этому моменту либо уже сгорел в `FillPageText`, либо не пригодился.
	bScriptClickSessionFlag = false;
	// ★★★ РЕВИЗИЯ 1-А §7: «ПЕРВАЯ ВСТРЕЧА» — `UI_resetAllWindowsAndLeave_5045@0xD995C0` (AsdaStory_game.c:395927-395944):
	//   `if (+0x498 == 1 && npc_list[+0x4AC].type == 0) { 5045 { u16 +0x4A0 }; byte[+0x4B0 + +0x4A0] = 1; }`,
	//   затем `+0x4AC = −1; +0x498 = 0; +0x4A0 = −1`. [ПРОТОКОЛ] 5045 — конверт A (MD5 по 2 б, size 18); сервер
	//   `case 0x13B5` → `sub_45C260@0x45C260` (GameServer.c:19899) под гейтом «души», пишет запись 2084 в лог-сервер.
	//   Отметку ставим ПОСЛЕ отправки безусловно — эталон результат `CNetwork_Send` не проверяет.
	if (bFirstMeet && FirstMeetNpcType == 0 && FirstMeetNpcId >= 0)
	{
		UAsdaNetSubsystem* NetFm = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		const bool bSent = NetFm && NetFm->SendNpcFirstMeet5045(FirstMeetNpcId);
		if (MetNpc.IsValidIndex(FirstMeetNpcId)) { MetNpc[FirstMeetNpcId] = true; }
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] первая встреча с npcId=%d окончена → 5045 %s, дальше — перебор записей"),
			FirstMeetNpcId, bSent ? TEXT("ушёл") : TEXT("НЕ ушёл (нет связи/шлюз)"));
	}
	bFirstMeet = false;
	FirstMeetNpcId = -1;
	FirstMeetNpcType = 0;
	bTalkPagePending = false;
	SavedTalkId = SavedStep = SavedSub = -1;
	ShopTabState = -1;
	GiveList.Reset(); TakeList.Reset();
	HideMultiQuest();
	HideQuestInfo();
	HideRewardPopup();   // Э6: попап наград 540 живёт только вместе с окном диалога

	// ★★★ ЗАКРЫВАЕМ ОКНА, ОТКРЫТЫЕ ИЗ ЭТОГО ДИАЛОГА (требование владельца 2026-08-26):
	//   «когда разговор с NPC заканчивается — кнопкой или отходом от него — его окна надо закрывать
	//   (торговля, ремонт и т.п.)». Все они открываются кнопками меню диалога и без него смысла
	//   не имеют: продолжать торговать, отойдя от торговца, нельзя.
	//   Каждое окно закрывает СЕБЯ САМО своей же точкой закрытия — второй логики закрытия не заводим
	//   (закон `port-shared-points`), поэтому здесь только перечисление.
	if (UGameInstance* GIw = GetGameInstance())
	{
		int32 Closed = 0;
		if (UAsdaNpcShopSubsystem* Sh = GIw->GetSubsystem<UAsdaNpcShopSubsystem>())
		{ if (Sh->IsOpen()) { Sh->CloseShop(); ++Closed; } }
		if (UAsdaRepairSubsystem* Rp = GIw->GetSubsystem<UAsdaRepairSubsystem>())
		{ if (Rp->IsOpen()) { Rp->CloseWindow(); ++Closed; } }
		if (UAsdaWarShopSubsystem* Ws = GIw->GetSubsystem<UAsdaWarShopSubsystem>())
		{ if (Ws->IsOpen()) { Ws->CloseWindow(); ++Closed; } }
		if (UAsdaEquipShopSubsystem* Eq = GIw->GetSubsystem<UAsdaEquipShopSubsystem>())
		{ if (Eq->IsOpen()) { Eq->CloseWindow(); ++Closed; } }
		// ★ Волна В5: элитный магазин 6000 открывается и командой разговора 30030 (разговор под ним спрятан) — по правилу
		//   владельца 26.08 закрывается вместе с разговором. Его хвост 5045 внутри `Close()` не повторяется (`bInClose`).
		if (UAsdaPremiumShopSubsystem* Ps = GIw->GetSubsystem<UAsdaPremiumShopSubsystem>())
		{ if (Ps->IsOpen()) { Ps->CloseWindow(); ++Closed; } }
		// ★ Склад кладовщика (окно 4000). Эталон гасит его тем же хвостом
		//   `UI_resetAllWindowsAndLeave_5045`; у подсистемы для этого есть своя точка, которая
		//   вдобавок ВОЗВРАЩАЕТ ВЛАДЕЛЬЦАМ содержимое корзины переноса (иначе предметы «зависли»
		//   бы в корзине закрытого окна) — поэтому зовём именно её, а не общий `CloseWindow`.
		if (UAsdaWarehouseSubsystem* Wh = GIw->GetSubsystem<UAsdaWarehouseSubsystem>())
		{ if (Wh->IsOpen()) { Wh->ResetAllOnNpcLeave(); ++Closed; } }
		// ★ Волна В4 ремесла (ревизия В3 С-3): окна, открытые из разговора, который теперь продолжается под ними, —
		//   передача параметров 16000 (эталонный 5045 сам зовёт `sub_9209D1(g_pExtractShopWin)`, ui_core.c:61166) и
		//   сброс улучшений 20200 (правило владельца 26.08: окно, открытое кнопкой диалога, закрывается с разговором).
		if (UAsdaExtractShopSubsystem* Ex = GIw->GetSubsystem<UAsdaExtractShopSubsystem>())
		{ if (Ex->IsOpen()) { Ex->CloseWindow(); ++Closed; } }
		if (UAsdaItemResetSubsystem* Ir = GIw->GetSubsystem<UAsdaItemResetSubsystem>())
		{ if (Ir->IsOpen()) { Ir->CloseWindow(); ++Closed; } }
		// ★ Волна В6 ремесла (W5-7): магазин бустеров 15300 (вход — кнопка разговора 30034). Эталонный 5045 закрывает его
		//   сам (`boosterShopCloseWindow(g_bModalNoticeActive)`, ui_core.c:61165) — здесь его игровой точкой.
		if (UAsdaBoosterShopSubsystem* Bs = GIw->GetSubsystem<UAsdaBoosterShopSubsystem>())
		{ if (Bs->IsOpen()) { Bs->CloseWindow(); ++Closed; } }
		if (Closed > 0)
		{
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] разговор окончен — закрыто окон торговца: %d"), Closed);
		}
	}

	// ★ Волна 2-Б (06.10.2026, ревизия М2): голова эталонного 5045 — `if (sub_9260C5(g_pNumPadDialog)) uiDialogClose(g_pNumPadDialog)`
	//   (ui_core.c:61170) — закрывает ЛЮБОЙ открытый нумпад БЕЗ колбэка режима (пожертвование 6, доклад об охоте 9 — окно 56450
	//   переживает разговор, — магазин и прочие). Свой признак «нумпад открыт» пожертвование снимает своей точкой.
	if (UAsdaDonationSubsystem* Don = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaDonationSubsystem>() : nullptr)
	{
		Don->CloseNumPadOnNpcLeave();
	}
	if (UAsdaWindowSubsystem* WmNp = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WmNp->DismissNumPad(TEXT("уход от NPC (5045)"));
	}
	ResetPanel();                   // `npcDialogPanelReset`
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kDlgWinId, false);
		// ★ Волна В3 ремесла: `uiCloseAllToggleWindows((int *)v11->gap_0)` (ui_core.c:61245) — переоткрыть окна из
		//   снимка входа в разговор (и из снимка окна состава ремесла, если он ещё жив) их ИГРОВЫМИ функциями
		//   открытия; порядок эталона: восстановление окон, затем `uiExpandPartyPanel`.
		//   ⚠ Если разговор закрывает чужое «закрыть всё» (`CloseAllPlayerWindows` — диалог в его росписи), окна НЕ
		//   переоткрываются (часть их закрывателей уже отработала): снимок разговора снимается без восстановления.
		//   ★ Защита порта (ревизия В3 МЕЛК-2, ключ `asda.UI.WindowSnapshotGuards`): при 0 — как оригинал, восстановление всегда.
		if (WM->IsClosingAllPlayerWindows() && UAsdaWindowSubsystem::AreSnapshotGuardsEnabled())
		{
			WM->DropCapturedWindows(UAsdaWindowSubsystem::EWindowSnapshotOwner::NpcDialog, TEXT("диалог закрыт внутри CloseAllPlayerWindows"));
		}
		else
		{
			WM->ReopenCapturedWindows(TEXT("уход от NPC (UI_resetAllWindowsAndLeave_5045)"));
		}
		// Парное к `CloseAllPlayerWindows` из `OpenForNpcId`: вернуть хотбар и трей главного меню
		//   (порт `uiExpandPartyPanel@0xD61C30`).
		WM->RestoreAfterSceneExit();
	}
	// ★ ИСПРАВЛЕНИЕ D5: закрытие окна снимает и отложенную цель «подбежать и заговорить»,
	//   иначе она осталась бы висеть и окно открылось бы снова само, стоит игроку задеть AABB.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()))
		{
			PC->ClearPendingNpcTalk(TEXT("диалог закрыт"));
		}
	}
	// ═══ `sub_9394A4(g_pEntityList, 1)` (`dialogOnConfirmClose@0xDA3BDD`) ═══
	//   ★ РАЗОБРАНО 2026-08-20: переходник (`E9` @0x9394A4, проверено по бинарю) на
	//   `sub_9663E0@0x9663E0` (entity_char.c:66145), тело: `*(dword*)(this + 0x1048) &= ~a2`.
	//   Парный сеттер `sub_91EB81 → sub_D99580@0xD99580` (`|= a2`) зовётся РОВНО из одного места —
	//   `regionWindow_OpenAndPathMove@0xD99290` (ui_core.c:61123), то есть при ОТКРЫТИИ окна
	//   диалога. ⇒ бит 0 в `g_pEntityList+0x1048` = глобальный флаг «диалог NPC открыт».
	//   Никакого обхода сущностей тут нет — это одно поле списка, поэтому в порту это одно булево.
	bEntityListDialogOpen = false;
	// ═══ `objSetFlag0x1000ById(g_pEntityList, this[436])` (`dialogOnConfirmClose@0xDA3BB0`) ═══
	//   Флаг 0x1000 у сущности NPC = «доиграть разговорную петлю (клип 3) и уйти через прощальный
	//   жест (клип 4) в idle». Разбор всей цепочки — в `AAsdaNpcActor::TickEntityAnimState`.
	if (AAsdaNpcActor* Talker = CurrentNpcActor.Get()) { Talker->RequestEntityAnimFinish(); }
	CurrentNpcActor = nullptr;
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] закрыт (npcId=%d)"), CurrentNpcId);
	CurrentNpcId = -1;
	CurrentNpcRenderId = -1;
	CurrentNsr.Reset();
}

// ★ Волна В4 ремесла (ревизия В3 С-1/С-3). Порт `dialogOnConfirmClose(g_pNpcWnd, 1)@0xDA3BB0` (ui_core.c:61645-61653):
//   `id5C = −1` (цель игрока — у порта целью диалога не служит), `sub_9394A4(g_pEntityList, 1)` (флаг «диалог открыт»
//   списка сущностей), `objSetFlag0x1000ById` (NPC доигрывает разговорную петлю) и `SetVisible(окно 570, 0)`. Признак
//   разговора [4099], снимок окон и свёрнутый HUD НЕ трогаются — их снимет выход из разговора (`ResetAllWindowsAndLeave`).
void UAsdaNpcDialogSubsystem::HideForChildWindow(const TCHAR* Who)
{
	if (!bDialogOpen) { return; }
	bHiddenForChild = true;
	bEntityListDialogOpen = false;
	if (AAsdaNpcActor* Talker = CurrentNpcActor.Get()) { Talker->RequestEntityAnimFinish(); }
	// ⚠ ОТСТУПЛЕНИЕ ПОРТА (ревизия В4 МЕЛК-11, записано в реестре В5): эталонный `dialogOnConfirmClose` прячет ТОЛЬКО окно
	//   570; порт дополнительно прячет свои окна выбора задания (MultiQuest) и подсказки (QuestInfo) — у порта они
	//   отдельные окна разговора и без спрятанного 570 висели бы поверх дочернего окна.
	HideMultiQuest();
	HideQuestInfo();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kDlgWinId, false);
	}
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] окно 570 спрятано под дочерним окном %s (dialogOnConfirmClose) — разговор продолжается, снимок окон жив"), Who);
}

void UAsdaNpcDialogSubsystem::CloseFromCloseAll()
{
	if (bDialogOpen && bHiddenForChild)
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] «закрыть всё» пропускает разговор, спрятанный под дочерним окном (у эталона uiCloseAllWindows его не закрывает)"));
		return;
	}
	Close();
}

void UAsdaNpcDialogSubsystem::CloseShopsAfterDeath()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	int32 Closed = 0;
	// `if (sub_93B448(g_pShopWnd)) shopWin_Close(g_pShopWnd)` — магазин снаряжения 6800.
	if (UAsdaEquipShopSubsystem* Eq = GI->GetSubsystem<UAsdaEquipShopSubsystem>()) { if (Eq->IsOpen()) { Eq->CloseWindow(); ++Closed; } }
	// `if (helper_getField2075(g_pShopWnd)) uiInventoryResetAndLeave(g_pShopWnd)` — элитный магазин 6000.
	if (UAsdaPremiumShopSubsystem* Ps = GI->GetSubsystem<UAsdaPremiumShopSubsystem>()) { if (Ps->IsOpen()) { Ps->CloseWindow(); ++Closed; } }
	// `if (CObject_GetField166(g_pHairShopWnd)) uiLeaveAndResetAllWindows(g_pHairShopWnd)` — парикмахерская 15000.
	if (UAsdaHairShopSubsystem* Hs = GI->GetSubsystem<UAsdaHairShopSubsystem>()) { if (Hs->IsSessionOpen()) { Hs->CloseWindow(); ++Closed; } }
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] смерть: хвост после 5045 — закрыто магазинов %d (6800/6000/парикмахерская)"), Closed);
}

// ★ Волна В4 ремесла. Порт `UI_resetAllWindowsAndLeave_5045@0xD995C0` для вызывающих, которые зовут его безусловно
//   (закрытия магазинов 6800/6000 и окна 16000, смерть). Разговор идёт — полный `Close()` (он и восстанавливает снимок);
//   разговора нет — эталонный хвост 5045 без диалога: `uiCloseAllToggleWindows` + `uiExpandPartyPanel` (ui_core.c:61245).
//   ⚠ НЕПОЛНЫЙ 5045 (ревизия В4 МЕЛК-12, строка реестра В5): голова эталонного 5045 без разговора ещё закрывает магазин NPC
//   (`g_pRecvUiHandler2`), нумпад, обмен предметами, магазин бустеров, передачу параметров и аукцион (`g_pItemShopWnd`,
//   ui_core.c:61162-61181) — здесь этого нет (кроме магазина бустеров 15300 — он с волны В6 закрывается и здесь, и в `Close()`); пометка «как у эталона» для трекера 1800 не доказана (эталон показывает
//   его из отрисовки мини-карты).
void UAsdaNpcDialogSubsystem::ResetAllWindowsAndLeave(const TCHAR* Why)
{
	// Дочернее окно закрывает САМ разговор (`Close()` → «окна торговца» → закрытие магазина → сюда): восстановление снимка
	//   сделает `Close()` в своём порядке — второй проход здесь дал бы переоткрытие окон посреди выхода из разговора.
	if (bInClose) { return; }
	if (bDialogOpen)
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] выход из разговора (5045): %s"), Why);
		Close();
		return;
	}
	// ★ Волна В6 ремесла (W5-7): голова 5045 без разговора тоже закрывает магазин бустеров 15300 (ui_core.c:61165).
	if (UAsdaBoosterShopSubsystem* Bs = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr)
	{
		if (Bs->IsOpen()) { Bs->CloseWindow(); }
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		const int32 N = WM->ReopenCapturedWindows(Why);
		WM->RestoreAfterSceneExit();
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5045 без разговора (%s): восстановлено окон %d, HUD возвращён"), Why, N);
	}
}

// Порт `npcDialogPanelReset@0xD9C5D0`.
void UAsdaNpcDialogSubsystem::ResetPanel()
{
	if (NameText) { NameText->SetText(FText::GetEmpty()); SetVis(NameText, false); }
	// ★ 09.11.2026: заголовок мог стать rich-контролом (префикс [MISSING]) — его тоже скрыть.
	if (UWidget* Cur = Win ? AsdaUi::FindNodeByName(Win, TEXT("NPC_TNAME"), TEXT("NPC_TNAME")) : nullptr) { if (Cur != NameText) { SetVis(Cur, false); } }
	if (TalkLines) { TalkLines->ClearChildren(); }
	SetVis(PreGrp, false);
	SetVis(NextGrp, false);
	for (int32 i = 0; i < QaButtons.Num(); ++i)
	{
		if (QaTexts.IsValidIndex(i) && QaTexts[i]) { QaTexts[i]->SetText(FText::GetEmpty()); SetVis(QaTexts[i], false); }
		SetVis(QaButtons[i], false);
	}
	for (int32 i = 0; i < ShopButtons.Num(); ++i)
	{
		if (ShopTexts.IsValidIndex(i) && ShopTexts[i]) { ShopTexts[i]->SetText(FText::GetEmpty()); SetVis(ShopTexts[i], false); }
		SetVis(ShopButtons[i], false);
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  НАПОЛНЕНИЕ
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::FillPortrait(const FAsdaNpcQuestNpcRow& NpcRec)
{
	if (!Portrait) { return; }
	if (NpcRec.ModelName.IsEmpty()) { return; }
	// Эталон: `GetCurrentDir() + "\Data\NPC\Image\" + npc_list[+0x46] + ".TEX"`. В порту те же имена
	//   лежат ассетами в `Textures/NPC/Portrait/` (158 штук на 204 записи npc_list — у части NPC
	//   портрета нет и в оригинале: тогда контрол остаётся с прежней текстурой).
	// ⚠ `UAsdaTextureSubsystem` — ДВИЖКОВАЯ подсистема (`UEngineSubsystem`), а не GameInstance:
	//   её же так берут сборка персонажа и логин-экран.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): общий короткий доступ к менеджеру текстур
	if (!TexMgr) { return; }
	const FString Path = AsdaAssetPaths::NpcPortraitObject(NpcRec.ModelName);   // ★ волна 2 п. 7: правило в реестре путей
	if (UTexture2D* Tex = TexMgr->LoadTextureCached(Path))
	{
		// ★★★ ПОЧЕМУ ПОРТРЕТА НЕ БЫЛО ВИДНО (найдено 2026-08-21).
		//   Контрол `NPC_POSI` носит скин `NoImage_icon`, а у того план режет АТЛАС `N_01.tga`
		//   лоскутом `UV = 0.994 0.000 … 1.000 0.004` (6×2 пикселя пустоты). Импортёр честно
		//   запёк этот `UVRegion` в кисть. `UImage::SetBrushFromTexture` меняет ТОЛЬКО
		//   ресурс кисти и `UVRegion` НЕ ТРОГАЕТ (комментарий, стоявший здесь раньше, утверждал
		//   обратное — это была ошибка) ⇒ портрет 256×512 сэмплировался тем же лоскутом
		//   0,6 % × 0,4 % и на экране выглядел пустым местом.
		//   Портрет — ОТДЕЛЬНАЯ текстура, а не кусок атласа (эталон
		//   `\Data\NPC\Image\<npc_list+0x46>.TEX`, `npcCraftSlotClick_Handle@0xD9C470` +
		//   `uiFont_ReloadAtlas@0xD9A1A0`), поэтому регион снимаем ЯВНО.
		FSlateBrush Br = Portrait->GetBrush();
		Br.SetResourceObject(Tex);
		Br.DrawAs = ESlateBrushDrawType::Image;
		Br.TintColor = FSlateColor(FLinearColor::White);           // скин мог принести тинт плана
		//   ★★★ ПРОВЕРЕНО 2026-08-27 (класс С3 аудита `Reports/audit_runtime_style.md`
		//   предлагал «не писать UV плейсхолдеров в импортёре» и убрать этот сброс;
		//   ПРЕДЛОЖЕНИЕ ОТКЛОНЕНО). Скин `NoImage_icon` носят **2961** контрол во всех 4 `.JUI`
		//   (это все ячейки-иконки), и у него `PlaneAlwaysInvisible = 0` — то есть оригинал ЕГО
		//   РИСУЕТ, просто лоскутом ПУСТОГО угла атласа. Перестань импортёр писать этот UV —
		//   каждый из 2961 контрола, которому рантайм картинку НЕ подставляет, растянул бы на
		//   себя ВЕСЬ атлас `N_01`. Здесь же случай обратный и осознанный: мы кладём в кисть
		//   ДРУГУЮ текстуру (портрет NPC из `Data\NPC\Image\*.TEX`), поэтому чужой UV снимаем.
		Br.SetUVRegion(FBox2f(ForceInit));                         // bIsValid = false → весь квад
		Br.SetImageSize(FVector2D(Tex->GetSizeX(), Tex->GetSizeY()));
		Portrait->SetBrush(Br);
		SetVis(Portrait, true);
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] портрет «%s» %dx%d"),
			*NpcRec.ModelName, Tex->GetSizeX(), Tex->GetSizeY());
	}
	else
	{
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] портрет «%s» не найден (%s)"), *NpcRec.ModelName, *Path);
		SetVis(Portrait, false);
	}
}

// Порт `npcDialogRenderPage@0xD9C7B0`.
void UAsdaNpcDialogSubsystem::RenderPage(int32 TalkId)
{
	// 1) `if (talkId)` — ноль означает ВЫХОД из диалога (`npcDialogPanelReset` +
	//    `UI_resetAllWindowsAndLeave_5045`). Именно так закрываются страницы-«Конец»:
	//    у их шага результат-talkId в данных равен нулю.
	if (TalkId == 0)
	{
		Close();
		return;
	}
	if (!bDialogOpen) { return; }

	ResetPanel();
	QaSlot[0] = QaSlot[1] = QaSlot[2] = -1;   // `self[4204…4208] = -1`
	PreStepIndex = NextStepIndex = -1;

	// 4) заголовок = строка блока NSR +8 (НЕ имя из npc_list.BIN).
	const FAsdaNsrHeaderRow* Hdr = FindHeader(CurrentNsr);
	if (NameText)
	{
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `NpcNsrHeader`, ключ `<nsr>.Title` (имя строки = имя сценария).
		FString Title = Hdr ? FAsdaLang::ResolveRow(AsdaDt::Cached(kDtHeader), FName(*CurrentNsr), TEXT("Title"), Hdr->Title) : FString();
		if (Title.IsEmpty())
		{
			// У сценария заголовка нет — берём имя NPC из общей точки (`DT_NpcNames`).
			//   ★ Волна 3 п.14: `AsdaEntityText::NpcName` (раньше — `UAsdaWorldSubsystem::GetNpcName`, то же тело).
			Title = AsdaEntityText::NpcName(CurrentNpcId);
		}
		// ★ 09.11.2026 [ПОРТ, решение владельца] «пропавший» NPC (список AsdaNpcLists::kMissingPrefix, ключ asda.Npc.MissingPrefix; разбор —
		//   Public/Npc/AsdaNpcMissing.h): красный «[MISSING]» перед заголовком. Два цвета — rich-путь `UAsdaRichText::ApplyFormatted`
		//   (единая точка разметки порта); NameText остаётся источником шрифта реплик (его читают строки облачка).
		UWidget* Cur = AsdaUi::FindNodeByName(Win, TEXT("NPC_TNAME"), TEXT("NPC_TNAME"));
		const bool bPrefix = AsdaNpcMissing::HasPrefix(GetGameInstance(), CurrentNpcId);
		if (bPrefix || (Cur && Cur != NameText))
		{
			const FString Text = bPrefix ? AsdaNpcMissing::PrefixMarkup(Title, NameText->GetColorAndOpacity().GetSpecifiedColor()) : Title;
			UWidget* Applied = UAsdaRichText::ApplyFormatted(Cur ? Cur : NameText.Get(), Text);
			if (URichTextBlock* RT = Cast<URichTextBlock>(Applied))
			{
				RT->SetAutoWrapText(false);
				RT->SetDefaultFont(NameText->GetFont());
				RT->SetJustification(AsdaUi::ReadJustification(NameText));
			}
			SetVis(Applied, true);
		}
		else
		{
			NameText->SetText(FText::FromString(Title));
			SetVis(NameText, true);
		}
	}

	// 5) поиск страницы по talkId.
	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, TalkId);
	if (!Page)
	{
		// Эталон в этом случае просто ничего не рисует (цикл не нашёл записи) — окно остаётся как есть.
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] страница talkId=%d у сценария «%s» не найдена"),
			TalkId, *CurrentNsr);
		return;
	}
	CurrentTalkId = TalkId;
	CurrentPageIndex = Page->PageIndex;

	// ★ [ПОРТ] 09.10.2026 (мультиязычность): реплика и подписи шагов — с КОПИИ страницы на языке сессии (оверлей `NpcNsrPage`, ключи
	//   `<nsr>_<talkId>.Text` и `<nsr>_<talkId>.Steps.<i>.Caption`). Русская сессия — прежний путь без копии.
	if (AsdaLangRows::Active())
	{
		FAsdaNsrPageRow LocPage = *Page;
		AsdaLangRows::Localize(AsdaDt::Cached(kDtPage), FName(*FString::Printf(TEXT("%s_%d"), *CurrentNsr, TalkId)), LocPage);
		FillPageText(LocPage);
		FillMenuButtons();
		FillStepButtons(LocPage);
	}
	else
	{
		FillPageText(*Page);
		FillMenuButtons();          // `uiQuestList_populateSteps`
		FillStepButtons(*Page);     // `listPanel_refreshSlotStates`
	}

	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] страница talkId=%d (#%d) «%s…» шагов=%d"),
		TalkId, Page->PageIndex, *Page->Text.Left(40).Replace(TEXT("\r\n"), TEXT(" ")), Page->StepCount);
}

void UAsdaNpcDialogSubsystem::FillPageText(const FAsdaNsrPageRow& Page)
{
	if (!TalkLines || !Win || !Win->WidgetTree) { return; }
	TalkLines->ClearChildren();

	// ═══ ОДНОРАЗОВЫЙ ФЛАГ `[this+0x4A4]` (npc_mob_pet.c:3400) ═══
	//   Если страницу открыла запись сценария с опкодом 122, эталон берёт текст НЕ из `.NSR`, а
	//   случайной репликой из `Talk/<nsr>.bin`, и приписку нотиса 111929 в этой ветке НЕ делает.
	//   Флаг ЧИТАЕТСЯ И ГАСИТСЯ здесь же — ровно как `*(_DWORD *)&npcDialog->gap_268[572] = 0`.
	FString Text;
	if (bTalkPagePending)
	{
		bTalkPagePending = false;
		Text = PickEpisodeTalkLine();
	}
	else
	{
		Text = Page.Text;

		// ★ Приписка нотиса 111929 у трёх конкретных NPC (эталон сравнивает блок NSR +0 — то есть
		//   npcId владельца сценария — с 46 / 50 / 53 и делает `sprintf("%s\n %s", текст, нотис)`).
		for (int32 Id : kNoticeAppendNpc)
		{
			if (Id != CurrentNpcId) { continue; }
			if (UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr)
			{
				// ★ 30.10.2026 [ОРИГИНАЛ]: эталон приклеивает `UI_FormatLocalizedNotice(111929)` С ТЕГАМИ (красный `<FC=FFFF0000>`);
				//   срезанная строка — прежний вывод (ключ `asda.NpcDlg.TalkMarkup 0`).
				const FString Notice = (CVarTalkMarkup.GetValueOnGameThread() != 0) ? TS->GetTextByIdRaw(kLocNoticeAppend) : TS->GetTextById(kLocNoticeAppend);
				if (!Notice.IsEmpty()) { Text += TEXT("\n ") + Notice; }
			}
			break;
		}
	}

	// Подстановка токена `[NAME]` именем персонажа (эталон делает это построчно; результат тот же).
	if (Text.Contains(TEXT("[NAME]")))
	{
		FString Self;
		if (UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr)
		{
			Self = WS->GetSelfCharName();
		}
		Text.ReplaceInline(TEXT("[NAME]"), *Self, ESearchCase::CaseSensitive);
	}

	// Разбивка на строки: сначала по авторским переводам строки, затем перенос по ширине контрола
	//   минус 12 (эталон `textTokenizer_SplitStringIntoRows(..., dialogBoxWidth - 12, 1)`).
	//   ★ AutoWrap здесь БЕЗОПАСЕН: строки лежат в UScrollBox, а не в CanvasPanel
	//     (закон `ui-autowrap-canvas-gotcha` запрещает автоперенос именно в канвасе).
	TArray<FString> Rows;
	Text.Replace(TEXT("\r\n"), TEXT("\n")).ParseIntoArray(Rows, TEXT("\n"), /*CullEmpty*/false);
	const FLinearColor Col = AsdaUi::SrgbU32ToLinear(kTalkTextArgb);
	for (const FString& Row : Rows)
	{
		UTextBlock* Tb = Win->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		if (!Tb) { break; }
		if (NameText) { Tb->SetFont(NameText->GetFont()); }
		AsdaUi::SetTextOutline(Tb, 0);   // §1 отчёта: у списка реплики 574 обводки в эталоне нет
		Tb->SetColorAndOpacity(FSlateColor(Col));
		Tb->SetAutoWrapText(true);
		Tb->SetWrapTextAt(TalkWrapWidth);
		Tb->SetText(FText::FromString(Row));
		Tb->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		TalkLines->AddChild(Tb);
		// ★ 30.10.2026 [ОРИГИНАЛ]: строка с разметкой — через общую точку формата (шапка `CVarTalkMarkup`).
		//   Стиль строки (шрифт заголовка, без обводки, чёрный 574) передаётся rich-контролу, перенос — тот же.
		//   `ApplyFormatted` ставит rich в конец ScrollBox — строка только что добавлена последней, порядок цел.
		if (CVarTalkMarkup.GetValueOnGameThread() != 0 && Row.Contains(TEXT("<")))
		{
			const FTextBlockStyle St = FTextBlockStyle()
				.SetFont(Tb->GetFont())
				.SetColorAndOpacity(FSlateColor(Col))
				.SetShadowOffset(FVector2D::ZeroVector);
			FAsdaRichApplyOpts Opts;
			Opts.BaseStyle = &St;
			Opts.bWrap     = true;
			Opts.WrapAt    = TalkWrapWidth;
			if (UWidget* Rich = UAsdaRichText::ApplyFormatted(Tb, Row, &Opts))
			{
				Rich->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
	}
	TalkLines->ScrollToStart();
}

// Порт `uiQuestList_populateSteps@0xDA2600` в части кнопок меню: показываются первые `MenuCount`
//   штук, и только если `MenuCount < 6` (эталонное условие `(unsigned)v4 < 6`).
void UAsdaNpcDialogSubsystem::FillMenuButtons()
{
	if (MenuCount < 0 || MenuCount >= 6) { return; }
	for (int32 i = 0; i < MenuCount && i < ShopButtons.Num(); ++i)
	{
		const FAsdaNsrButtonRow* B = FindButton(CurrentNsr, i);
		if (!B) { continue; }
		// ★ Подпись ставит КОД, а не импортёр: в WBP лежит плейсхолдер «Текст».
		if (ShopTexts.IsValidIndex(i) && ShopTexts[i])
		{
			// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `NpcNsrButton`, ключ `<nsr>_<кнопка>.Caption` (имя строки — как в `FindButton`).
			ShopTexts[i]->SetText(FText::FromString(FAsdaLang::ResolveRow(AsdaDt::Cached(kDtButton),
				FName(*FString::Printf(TEXT("%s_%d"), *CurrentNsr, i)), TEXT("Caption"), B->Caption)));
			SetVis(ShopTexts[i], true);
		}
		SetVis(ShopButtons[i], true);
	}
}

// Порт `listPanel_refreshSlotStates@0xDA2810`.
void UAsdaNpcDialogSubsystem::FillStepButtons(const FAsdaNsrPageRow& Page)
{
	// Эталон отказывается рисовать шаги, если их больше трёх.
	if (Page.StepCount > 3) { return; }

	for (int32 i = 0; i < Page.Steps.Num() && i < Page.StepCount; ++i)
	{
		const FAsdaNsrStep& Step = Page.Steps[i];
		int32 Found = 0;
		for (const FAsdaNsrStepSub& S : Step.Subs)
		{
			if (S.State == 2 || S.State == 3 || S.State == 4) { Found = S.State; }
		}
		if (Found == 3)
		{
			SetVis(PreGrp, true);
			SetVis(PreText, true);        // текст статический, ставится в `SetupControls`
			PreStepIndex = i;             // `this[12] = i`
		}
		else if (Found == 2 || Found == 4)
		{
			SetVis(NextGrp, true);
			SetVis(NextText, true);
			NextStepIndex = i;            // `this[13] = i`
		}
		else
		{
			// ★ ИСПРАВЛЕНИЕ D1. ЭТАЛОН (quest.c:8168-8180) кладёт подпись шага №2 в кнопку
			//   `NPC_QA_BUT01` (579) — ту же, что и шаг №1, — и `NPC_QA_BUT02` (580) не получает
			//   НИ ТЕКСТА, НИ ВИДИМОСТИ никогда, хотя её клики обрабатываются. Здесь i-й шаг идёт
			//   в свою кнопку `NPC_QA_BUT0{i}` (пара к исправлению D2 в `SetupControls`).
			//   ⚠ В данных этой сборки stepCount ≤ 2, поэтому расхождение с оригиналом на реальном
			//     контенте НЕ проявляется — исправление держим ради корректности.
			if (QaButtons.IsValidIndex(i))
			{
				if (QaTexts.IsValidIndex(i) && QaTexts[i])
				{
					QaTexts[i]->SetText(FText::FromString(Step.Caption));
					SetVis(QaTexts[i], true);
				}
				SetVis(QaButtons[i], true);
				QaSlot[i] = i;   // `slotArrayAddFirstFree(this, i)` — какой ШАГ сидит на кнопке
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  НАЖАТИЯ (порт uiNpcOnTabButton@0xD92120 + craftWnd_onButtonClick@0xDA0CA0)
// ════════════════════════════════════════════════════════════════════════════════════════════════
void UAsdaNpcDialogSubsystem::OnDialogButton(int32 CtrlId)
{
	// ★★★ ИСПРАВЛЕНО 2026-08-28 — «крестик подсказки робота Минопио не закрывает окно 599».
	//   БЫЛО: `if (!bDialogOpen) { return; }` — гейта с таким смыслом в эталоне НЕТ.
	//   `craftWnd_onButtonClick@0xDA0CA0` (`RE/etalon/item_enhance.c:3139`) проверяет РОВНО ОДИН
	//   признак — `this[437]` (режим попапа наград); признак «диалог открыт» (`this[4099]`) в
	//   обработчике нажатий не участвует ни в одной ветке, и ветка 586 у него стоит СРАЗУ после
	//   `uiNpcOnTabButton`, безо всяких условий:
	//       if (a3 > 0x241) { if (a3 == 586) npcExchangeUiReset(this); else if (a3 == 2411) … }
	//   ПОЧЕМУ ЭТО ЛОМАЛО РОВНО КРЕСТИК 586. Окно `QUEST_INFO_WIN` (599) живёт САМО ПО СЕБЕ, поверх
	//   мира, и подсказка «Минопио» поднимается ТОЛЬКО при ЗАКРЫТОМ окне разговора: гейт (5)
	//   `UAsdaQuestMarkerSubsystem::MaybeShowAwaitHint` («открыт разговор с NPC» → выход) —
	//   `AsdaQuestMarkerSubsystem.cpp:1695`. То есть в момент, когда крестик виден, `bDialogOpen`
	//   гарантированно `false`, и нажатие умирало на первой строке, не дойдя до `HideQuestInfo()`.
	//   Заодно молча съедался и 2411 (`MULTYQUEST_CLOSE`), если облачко переживало закрытие диалога.
	//   ПОЧЕМУ СНЯТЬ ГЕЙТ БЕЗОПАСНО: собственные кнопки окна разговора (575…585, 567) сидят ВНУТРИ
	//   окна 570, а оно при закрытом диалоге `Collapsed` (`SetWindowVisible(kDlgWinId, false)` в
	//   `Close()`), мышь до них физически не доходит — ровно как в оригинале, где гейта тоже нет.
	UE_LOG(LogAsdaNpcDlg, Verbose, TEXT("[NPC-диалог] нажатие ctrlId=%d (диалог открыт=%d, попап наград=%d)"),
		CtrlId, bDialogOpen ? 1 : 0, bRewardPopupMode ? 1 : 0);

	// ═══ ГЕЙТ ЭТАЛОНА `this[437]` ═══ пока показан попап наград, окно принимает ТОЛЬКО кнопку 567
	//   («ОК» под наградой). Все остальные нажатия игнорируются молча — 1:1.
	if (bRewardPopupMode)
	{
		if (CtrlId == 567)
		{
			// ★★★ ИСПРАВЛЕНО 2026-08-22 (`Reports/quest_turnin_notice_fix.md` §1.2).
			//   БЫЛО: `RunStepAction(SavedStep)` — то есть ПОВТОРНЫЙ ПРОГОН шага, который при
			//   `EntryProgressState == 2` снова СТРОИТ ПОПАП и никакого пакета не шлёт. В живом
			//   логе владельца (22.08, 08:52:15 и 08:52:16) это видно буквально: два нажатия 567
			//   подряд и два «попап 540 показан», ни одного `→5041`. Отсюда «квесты не сдаются».
			//   Эталон `craftWnd_onButtonClick@0xDA0CA0` в режиме попапа делает РОВНО ДВЕ вещи:
			//       csoundPlayName2DStore(g_pSoundMgr, 4, 0);  uiCraftSlotClickSendReq(this);
			if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
			{
				Snd->PlaySound2DById(4);
			}
			ConfirmRewardPopup();
		}
		return;
	}

	// 575 `NPC_TALKCLOSE` — выход из диалога.
	if (CtrlId == 575) { Close(); return; }

	// 586 `QUEST_INFO_x` — погасить подсказку (`npcExchangeUiReset@0xDA3DF0`).
	if (CtrlId == 586)
	{
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог] QUEST_INFO_x (586): крестик подсказки 599 — гашу баннер (было видно=%d)"),
			bQiActive ? 1 : 0);
		HideQuestInfo();
		return;
	}

	// 2411 `MULTYQUEST_CLOSE` — спрятать облачко (эталон только `SetVisible(2406, 0)`).
	if (CtrlId == 2411) { HideMultiQuest(); return; }

	// ═══ ВЕТКА `uiNpcOnTabButton` (581…585 меню и 578…580 шаги) ═══
	if (CtrlId >= 578 && CtrlId <= 585)
	{
		if (CtrlId >= 581)
		{
			// Кнопка МЕНЮ: эталон ставит одноразовый флаг `this[24073] = 1` и кладёт в `this[4]`
			//   НОМЕР КНОПКИ, после чего `craftWnd_onButtonClick` сразу зовёт обработчик и ГАСИТ флаг.
			bMenuButtonPending = true;
			SelIndex = CtrlId - 581;
			RunMenuButton(SelIndex);
			bMenuButtonPending = false;
			return;
		}
		// Кнопка ШАГА: `this[4] = this[4204 + (ctrl − 578)]` — номер ШАГА страницы, а не кнопки.
		SelIndex = QaSlot[CtrlId - 578];
	}
	else if (CtrlId == 576) { SelIndex = PreStepIndex; }    // `this[4] = this[12]`
	else if (CtrlId == 577) { SelIndex = NextStepIndex; }   // `this[4] = this[13]`
	else { return; }

	// Общий хвост эталона: развилка по `craftSlotFindFirstStateIndex`.
	const int32 First = CraftSlotFindFirstStateIndex(SelIndex);
	if (First == 4)
	{
		// Состояние под-записи 5 → облачко выбора квеста.
		ShowMultiQuest();
		return;
	}
	if (First == 1)
	{
		// ★ РАЗОБРАНО 2026-08-20. `sub_933A04` — переходник (`E9` @0x933A04, проверено по бинарю)
		//   на `sub_D94C60@0xD94C60` (entity_char.c:68410). Его тело ЦЕЛИКОМ (дизасм 0xD94C60):
		//       [this+0x08] = 1;  [this+0x1C] = -1;  [this+0x49C] = 1;
		//       call uiCraftSlotClickHandler@0xD83D90;
		//   То есть это НЕ отдельное действие, а ТОТ ЖЕ клик, но в режиме перебора ЗАПИСЕЙ
		//   СЦЕНАРИЯ: `+0x08` (`field2_object`) включает в `uiCraftSlotClickHandler`
		//   (ui_core.c:59111-59139) ветку `else if (field2_object)` — обход 64 записей `.NSR`
		//   (в порту это `DT_NpcQuestScript`, блок `.NSR` +0x30, 64×144).
		//   `+0x1C = -1` — инвалидация ключа поиска индекса квеста; он читается ТОЛЬКО в ветке
		//     `field2_object == 0`, куда после этой записи управление уже не попадает, поэтому
		//     переносить нечего (в порту индекс страницы и так берётся по `CurrentTalkId`).
		//   `+0x49C = 1` — флаг сессии: читателей во ВСЁМ бинаре нет (сплошной скан `.text`),
		//     пишется здесь и сбрасывается в `UI_resetAllWindowsAndLeave_5045@0xD999DC`.
		//     Держим ради 1:1 — см. `bScriptClickSessionFlag`.
		bScriptClickSessionFlag = true;
		RunScriptRecordScan();
		return;
	}
	RunStepAction(SelIndex);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  УСЛОВИЯ И СОСТОЯНИЯ ШАГА
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★ Своего вычислителя условий у окна НЕТ — он один на порт и живёт в подсистеме квестовых меток
//   (`EvalNsrCond`). Здесь только выбор «раскладка под-записи» + «владелец: кнопка или шаг»
//   (у эталона они расходятся ровно в одной категории — классе, см. `EvalNsrCond`).
bool UAsdaNpcDialogSubsystem::EvalCond(const FAsdaNsrCond& C, bool bStepOwner, FString* OutWhy) const
{
	UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	if (!QM) { return false; }
	return QM->EvalNsrCond(C, EAsdaCondLayout::SubRecord,
		bStepOwner ? EAsdaCondOwner::Step : EAsdaCondOwner::MenuButton, OutWhy);
}

// Порт `craftSlotFindFirstStateIndex@0xD94AB0`: 2→1, 3→2, 4→3, 5→4, иначе 0 (ПЕРВОЕ совпадение).
int32 UAsdaNpcDialogSubsystem::CraftSlotFindFirstStateIndex(int32 StepIndex) const
{
	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, CurrentTalkId);
	if (!Page || !Page->Steps.IsValidIndex(StepIndex)) { return 0; }
	for (const FAsdaNsrStepSub& S : Page->Steps[StepIndex].Subs)
	{
		switch (S.State)
		{
		case 2: return 1;
		case 3: return 2;
		case 4: return 3;
		case 5: return 4;
		default: break;
		}
	}
	return 0;
}

// Порт `questNpc_IsTargetAvailable@0xD894C0` (P = блок цели):
//   `(u16 P+24)==0 && (u16 P+90)==0 && (i32 P+136)>=0` → 1;
//   иначе `(u16 P+24)==1 && ((i32 P+52)>0 || (i32 P+60)>0) && (i32 P+136)>0`.
bool UAsdaNpcDialogSubsystem::IsTargetAvailable(const FAsdaNsrStepObj& O)
{
	if (O.Kind24 == 0 && O.Flag90 == 0 && O.Need136 >= 0) { return true; }
	return O.Kind24 == 1 && (O.A52 > 0 || O.A60 > 0) && O.Need136 > 0;
}

// Порт `quest_GetEntryProgressState@0xD892D0`.
int32 UAsdaNpcDialogSubsystem::GetEntryProgressState(const FAsdaNsrStep& Step) const
{
	for (const FAsdaNsrStepSub& S : Step.Subs)
	{
		if (S.AcceptQuestId > 0) { return 1; }               // под-запись+1028 — шаг ВЫДАЁТ квест
		for (int32 j = 0; j < S.ObjActive.Num(); ++j)
		{
			if (S.ObjActive[j] <= 0) { continue; }
			const bool bAvail = S.Objs.IsValidIndex(j) && IsTargetAvailable(S.Objs[j]);
			return bAvail ? 4 : 2;
		}
		if (S.State == 5) { return 3; }
	}
	return 0;
}

// Порт `gridScanAnyActiveCell@0xD7E2E0`.
// ⚠ Эталон сравнивает с −1, а не с 0 (в отличие от `quest_GetEntryProgressState`). В данных этой
//   сборки значение −1 в поле «цель активна» не встречается НИ РАЗУ (0 в 78 418 слотах из 78 900),
//   поэтому у оригинала эта проверка всегда истинна. Воспроизводим как есть — вместе со следствием.
bool UAsdaNpcDialogSubsystem::GridScanAnyActiveCell(const FAsdaNsrStep& Step) const
{
	for (const FAsdaNsrStepSub& S : Step.Subs)
	{
		for (int32 j = 0; j < S.ObjActive.Num(); ++j)
		{
			if (S.ObjActive[j] > -1) { return true; }
		}
	}
	return false;
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ДЕЙСТВИЯ
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Ветка `this[24073] == 1` функции `uiCraftSlotClickHandler@0xD83D90` (ui_core.c:59078-59110).
void UAsdaNpcDialogSubsystem::RunMenuButton(int32 ButtonIndex)
{
	const FAsdaNsrButtonRow* B = FindButton(CurrentNsr, ButtonIndex);
	if (!B) { return; }

	for (int32 j = 0; j < B->Subs.Num(); ++j)
	{
		const FAsdaNsrButtonSub& S = B->Subs[j];
		if (S.State == 1)
		{
			// «Конец списка»: команда исполняется без проверки условия.
			if (S.Flag != 0) { ExecuteCommand(S.TalkId, -1); return; }
			if (S.TalkId > 0) { ExecuteCommand(S.CmdCode, S.Param); RenderPage(S.TalkId); return; }
			ExecuteCommand(S.CmdCode, S.Param);
			return;
		}
		if (S.State == 2)
		{
			FString Why;
			if (EvalCond(S.Cond, /*bStepOwner*/false, &Why))
			{
				if (S.Flag == 0) { ExecuteCommand(S.CmdCode, S.Param); }
				else             { ExecuteCommand(S.TalkId, -1); }
				return;
			}
			// Условие НЕ выполнено. Эталон зовёт `uiShowItemTooltipByCode(код команды)`, а та —
			//   ★ ДЕФЕКТ ОРИГИНАЛА D13 — знает РОВНО ОДИН код: 30015 → нотис 12457. Для остальных
			//   46 кодов игрок не получает ничего: кнопка «не работает» без объяснения.
			//   ИСПРАВЛЯЕМ: нотис 12457 показываем как в оригинале, а для прочих кодов пишем в ленту
			//   честную строку и разбор условия — в лог.
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] кнопка меню %d («%s»): условие под-записи %d НЕ выполнено\n%s"),
				ButtonIndex, *B->Caption, j, *Why);
			UAsdaTextSubsystem* TS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
			UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
			if (Msg)
			{
				FString Text;
				if (S.CmdCode == kCmdClanCreateCode && TS) { Text = TS->GetTextById(kLocClanCreateCond); }
				if (Text.IsEmpty()) { Text = FAsdaText::Loc(990496); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): «Условия для этого пункта не выполнены.»
				Msg->PostText(EAsdaMsgCategory::Quest, Text);
			}
			// ★ 1:1: эталон здесь НЕ выходит, а продолжает цикл по следующим под-записям.
			continue;
		}
	}
}
