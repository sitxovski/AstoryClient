// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_Script.cpp — ★ 23.10.2026 часть UAsdaNpcDialogSubsystem, вынесенная из AsdaNpcDialogSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaNpcDialogSubsystem.plan.json).
//   Ветка «перебор записей сценария» (uiCraftSlotClickHandler) и исполнитель команд кнопок ExecuteCommand.
//   #include — те же, что у AsdaNpcDialogSubsystem.cpp, в том же порядке; общее — AsdaNpcDialogSubsystemInternal.h.
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
// UAsdaRichText::ApplyFormatted — теги <FC=…> в баннере 599   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "UI/AsdaTipPanel.h"             // общая точка пересборки 9-slice (порт uiPanel9SliceLayout) + EAsdaTipOwner
#include "UI/AsdaItemTooltipSubsystem.h" // ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
#include "World/AsdaLoadManagerSubsystem.h"
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "Quest/AsdaNsrScriptPage.h"       // ★ 04.11.2026: правило «запись без страницы» — одна точка с метками
#include "Quest/AsdaQuestLogSubsystem.h"   // ОБЩАЯ точка `BuildRewardLines` — разбор RewardTable.BIN (второй копии нет)
#include "Data/AsdaEffectMobTableRows.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"              // ★ ДЕДУП Д4: чтение little-endian из тела пакета — одна точка на порт
#include "Resources/AsdaSoundSubsystem.h"
#include "UI/AsdaIconCellGrid.h"   // общая точка: прозрачные клик-кнопки поверх JUI-ячеек (строки выбора награды)
#include "UI/AsdaEffectsSubsystem.h"
#include "UI/AsdaCraftWindowSubsystem.h"   // ★ 30009/30088/30010 — окно создания снаряжения 76000 (ремесло, волна В2)
#include "UI/AsdaCraftMakeWindowSubsystem.h"   // ★ хвост смерти entity_char.c:16910 — сброс окна изготовления 77500 (В8)
#include "UI/AsdaCraftSubsystem.h"             // ★ ключ К-9 `asda.Craft.FixDeathRestoreOrder` (В8)
#include "UI/AsdaHuntReportSubsystem.h"         // ★ волна 2-Б: 30068 — окно «Доклад об охоте» 56450
#include "UI/AsdaTownWarSubsystem.h"            // ★ волна 2-Г: 30076 — окно налога города 79300
#include "UI/AsdaFactionSelectSubsystem.h"        // ★ 30.10.2026: 30056 — выбор армии 44000; 6700 при разговоре с NPC войны
#include "UI/AsdaCampWarSubsystem.h"             // ★ 30.10.2026: 30057/30074/30075 — окно «Битва армий» 47000
#include "Npc/AsdaDonationSubsystem.h"         // ★ волна 2-Б: 30051 — пожертвование (нумпад режима 6 → 6132)
#include "TimerManager.h"
#include "EngineUtils.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)
#include "HAL/IConsoleManager.h"            // ★ 27.09 itemsfix: asda.EventExchange.ServerGuard
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "World/AsdaCoord.h"                  // ★ 26.10.2026 (savepoint): мир → тайл для 5280 (`helper_scaleIfNonzero`)

// `CGameUI_HandleControlCommand@0xD87590`: «закрыть диалог»; всё, что > 31000, эталон игнорирует.
static constexpr int32 kCmdCloseDialog = 31000;
static constexpr int32 kCmdMax = 31000;
// Сторож обмена 6098 (см. `IsEventExchangeServerSafe`). 0 — как оригинал: слать 6098 без сверки с сервером.
static ASDA_DEV_CVAR(int32, CVarEventExchangeServerGuard,
	TEXT("asda.EventExchange.ServerGuard"), 1,
	TEXT("1 (по умолчанию) — не слать 6098 для строк обмена, требования которых на сервере другие ")
	TEXT("(сервер списал бы не те предметы или ничего не сделал бы при «успехе»); 0 — как оригинал."),
	ECVF_Default);
// «Не удалось передать предмет. Повтори попытку позже.» — ровно тот текст, которым оригинал показывает
//   отказ сервера по неверной строке обмена (6099 коды 0/2/3, `netHandleTradeStatusNotice@0x973640`,
//   network.c:3466-3470). Используем его же, а не выдумываем свою фразу.
static constexpr int32 kLocExchangeServerRefuse = 7502;
// ★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-6): отказ СТОРОЖА 6098 показывается ТЕМ ЖЕ каналом, что отказ сервера у эталона —
//   однокнопочным окном ONEBUT 1500 с ключом очереди (0, 24): `netHandleTradeStatusNotice@0x973640` (network.c:3466-3470)
//   `CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, UI_FormatLocalizedNotice(7502), 1)`; порт того же приёма —
//   `AsdaNetSubsystem.cpp`, case 6099. Прежде — строка ленты `PostLoc Quest` (отступление, отчёт clan_btn_6099 «Осталось» п.2).
//   [ПРОТОКОЛ] сам сторож — порт (пакета нет вовсе), канал вывода — оригинальный.
static void ShowExchangeGuardRefusal(const UGameInstance* GI)
{
	AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Trade, kLocExchangeServerRefuse, AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
}

// Материалы и предметы, зашитые в диспетчер команд (game_main.c:9892 и далее).
static constexpr int32 kCraftMaterialFirst = 20977;   // 30043…30045: пять кодов 20977…20981
static constexpr int32 kSoulBindItem       = 47902;   // 30048: нужно >= 11 штук
static constexpr int32 kQuadItemFirst      = 47903;   // 30049: нужны все четыре 47903…47906
static constexpr int32 kRaidFixedItem      = 20966;   // 30061: >= 6 штук
static constexpr int32 kRaidFixedCount     = 6;
static constexpr int32 kLocExchangeLack = 98101;
static constexpr int32 kLocCraftLack    = 98102;
static constexpr int32 kLocRaidLack     = 98103;
static constexpr int32 kLocSavePointTip = 6178;   // «Сохранить текущее местоположение.» — вопрос окна (8, 6178)
static constexpr int32 kLocSavePointDone = 6179;  // «Текущее местоположение сохранено.» — журнал + плашка 221 после 5280
static constexpr int32 kLocSavePointNoNpc = 6180; // «При закрытом окне персонажа сохранить местоположение не удастся.» — только журнал
static constexpr int32 kLocAcceptBusy   = 6523;

// ★ 26.10.2026 (savepoint) [ИСПРАВЛЕНИЕ] баг игроков «точка сохранения не запоминается» (недоработка порта: 30022 показывал
//   только баннер с текстом 6178 и НИЧЕГО не слал; реестр PORT_PENDING_ON_SYSTEMS.md, «Команда NPC „точка сохранения“ 30022»).
//   1 (по умолчанию) — как оригинал: вопрос TWOBUT (8, 6178) → «Да» → 5280 + 6179; 0 — прежнее поведение порта (баннер 599).
static ASDA_DEV_CVAR(int32, CVarNpcSavePointSend,
	TEXT("asda.Npc.SavePointSend"), 1,
	TEXT("1 (по умолчанию) — кнопка NPC 30022 «Сохранить местоположение» как в оригинале: вопрос (8, 6178), по «Да» опкод 5280 ")
	TEXT("и нотис 6179; 0 — прежний порт: только баннер с текстом 6178, на сервер ничего не уходит."),
	ECVF_Default);

// ★ 26.10.2026 (savepoint) [ОРИГИНАЛ] «Да» окна (8, 6178): `OnSendItemUpgrade` msgId 1523, `case 0x1822` →
//   `itemDropToGroundRequest(g_pNpcWnd)@0xDA2C60` (AsdaStory_game.c:380299 → 399578, имя ВРЁТ — это «сохранить место»):
//     npcId = `g_pNpcWnd+0x178A8` (его пишет только 30022 и конструктор −1);
//     npcId > −1 → 5280 {npcId, тайл X, тайл Z своего персонажа} + 6179 в журнал (`CChat_AddTokenizedLines(…, −1, 10000, 0)`)
//                  и в плашку 221 (`CControl_SetTooltipText(g_pChatWnd, …, −1, 10000)`), белым, 10 с; ответа сервер не шлёт;
//     иначе     → 6180 только в журнал (белым, 10 с), пакета нет.
//   Позиция — `Class_GetVec3_86(свой персонаж)` → `helper_scaleIfNonzero` (мир → тайл, округление вниз) = `AsdaCoord::GameToTile`.
//   [ПОРТ] павна нет (не бывает в мире) → тайл из последнего серверного снимка `SelfTileX/Z`; оба ≤ 0 — сервер сам отобьёт.
static void SavePointConfirmed(UGameInstance* GI, int32 NpcId)
{
	if (!GI) { return; }
	if (NpcId <= -1)
	{
		const FString T = AsdaNotice::Loc(GI, kLocSavePointNoNpc);
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, T.IsEmpty() ? FAsdaLang::MissingTag(kLocSavePointNoNpc) : T,
			FColor(0xFF, 0xFF, 0xFF, 0xFF), 10000.f);
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] точка сохранения: npcId %d ≤ −1 → нотис %d, 5280 не уходит"), NpcId, kLocSavePointNoNpc);
		return;
	}
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	FIntPoint Tile(Net ? Net->SelfTileX : -1, Net ? Net->SelfTileZ : -1);
	const TCHAR* Src = TEXT("снимок сервера SelfTileX/Z");
	if (AAsdaPlayerController* PC = Cast<AAsdaPlayerController>(GI->GetFirstLocalPlayerController()))
	{
		if (const AAsdaPlayerCharacter* Ch = Cast<AAsdaPlayerCharacter>(PC->GetPawn()))
		{
			Tile = AsdaCoord::GameToTile(Ch->GetGamePosXZ());
			Src = TEXT("позиция персонажа");
		}
	}
	const bool bSent = Net && Net->SendSaveVillagePos5280(NpcId, Tile.X, Tile.Y);
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] точка сохранения: «Да» (8, 6178) → 5280 npcId %d тайл (%d,%d) [%s] %s → нотис %d"),
		NpcId, Tile.X, Tile.Y, Src, bSent ? TEXT("ушёл") : TEXT("НЕ ушёл (нет связи)"), kLocSavePointDone);
	// Эталон печатает 6179 сразу после `CNetwork_Send`, не глядя на его итог (ответа у 5280 нет вовсе).
	AsdaNotice::PlankAndChatLoc(GI, EAsdaMsgCategory::System, kLocSavePointDone, FColor(0xFF, 0xFF, 0xFF, 0xFF), 10000.f,
		/*SrcId*/5280, /*bChatFirst*/true);
}

// ★★★ 04.11.2026 [ИСПРАВЛЕНИЕ] (баг владельца «часть NPC не взаимодействует»: Силарис — Волшебница Хмелла, Солнечный берег).
//   В данных оригинала 50 сценариев (`Nsr.bsz` всех трёх клиентов) содержат записи, чья страница в `.NSR` ОТСУТСТВУЕТ: у Хмеллы
//   (npc 20) #3…#8 «задание 93/99/100/135–137 в работе» и #13…#19 «предыдущее сдано, следующее не взято» → страницы 4…20, у Куриана
//   (19) #5…#17, у Мелве (26), Могана (27), Тида (45), Язарка (92), Ваэнди (13), Фхашу (14), кристаллов/«ивентовых» NPC 2/7/21–23/37…
//   (#9…#11 ур. 140+, задание 340/398). Эталон на такой записи останавливает перебор, `npcDialogRenderPage` страницы не находит и
//   НИЧЕГО не рисует — окно остаётся открытым со старой страницей и меню (список заданий работает). Порт же закрывал разговор
//   («перебор ничего не выбрал») — NPC выглядел мёртвым как раз тогда, когда его задание в работе. Теперь запись с отсутствующей
//   страницей пропускается и перебор идёт дальше (до общей реплики сценария). Ключ 0 — прежнее поведение порта.
// ★ 04.11.2026 (questmark-chmella): ключ `asda.Npc.SkipMissingPage` и само правило переехали в ОДНУ точку
//   `AsdaNsrScript::ShouldSkipRecordWithoutPage` (`Public/Quest/AsdaNsrScriptPage.h`) — её же зовёт выбор квестовой метки
//   `UAsdaQuestMarkerSubsystem::EvalMarkForNsr`, чтобы метка не обещала запись, которую разговор пропустит.

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ВЕТКА «ПЕРЕБОР ЗАПИСЕЙ СЦЕНАРИЯ» (порт `uiCraftSlotClickHandler@0xD83D90`, ui_core.c:59111-59139)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Эталонное тело ветки `else if (this->field2_object)` дословно:
//     for (k = 0; k < 64; ++k)
//       if (sub_920B43(&rec[k])) {                       // ← гейт входа в запись
//         if (rec[k][0] == 123) { if (questNpc_CollectDeliverableItems(this, 0) >= 2) goto RENDER; }
//         else if (rec[k][0] == 122) { this[0x4A4] = 1; RENDER; return; }
//       } else if (rec[k][0] != 121) {
//         v = itemTooltip_BuildStatFlagsTable1(this, &rec[k], 0);
//         if (charEquip_VerifyStatChecksum(this, &rec[k], v, 0)) { RENDER; return; }
//       }
//     RENDER: npcDialogRenderPage(this, u16 rec[k]+4);
// Гейт `sub_920B43` — переходник (`E9` @0x920B43, проверено по бинарю) на `sub_D87180@0xD87180`
//   (item_inventory.c:70794), тело целиком: `return *a1 == 122 || *a1 == 123;`. То есть «запись
//   специальная». Ровно так же его читает и уже портированный сканер меток
//   (`UAsdaQuestMarkerSubsystem::EvalMarkForNsr`) — общая точка, второй копии условия нет.
// Пара `itemTooltip_BuildStatFlagsTable1` + `charEquip_VerifyStatChecksum` — это и есть
//   `UAsdaQuestMarkerSubsystem::CheckRecord` (★ ЕДИНСТВЕННЫЙ вычислитель условий в порту).
void UAsdaNpcDialogSubsystem::RunScriptRecordScan()
{
	const UDataTable* Dt = AsdaDt::Cached(kDtScript);
	if (!Dt)
	{
		UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] перебор записей сценария: таблица %s не загружена"), kDtScript);
		return;
	}
	UAsdaQuestMarkerSubsystem* QM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;

	// Импортёр отбрасывает записи, которые НИКОГДА не срабатывают (опкод 0 и ни одного условия),
	//   поэтому индексы в таблице разрежены — восстанавливаем эталонный ПОРЯДОК по `RecIndex`.
	TArray<const FAsdaNpcQuestScriptRow*> Recs;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaNpcQuestScriptRow* R = reinterpret_cast<const FAsdaNpcQuestScriptRow*>(It.Value);
		if (R && R->NsrName == CurrentNsr) { Recs.Add(R); }
	}
	Recs.Sort([](const FAsdaNpcQuestScriptRow& A, const FAsdaNpcQuestScriptRow& B) { return A.RecIndex < B.RecIndex; });

	// ★ 04.11.2026: запись, чьей страницы нет в сценарии, пропускается (см. `CVarAsdaNpcSkipMissingPage`). Опкод 122 — страница из
	//   `Talk/<nsr>.bin`, её не проверяем.
	auto PageMissing = [this](const FAsdaNpcQuestScriptRow& R)
	{
		if (!AsdaNsrScript::ShouldSkipRecordWithoutPage(CurrentNsr, R.PageId)) { return false; }   // одна точка с метками
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] запись сценария #%d «%s»: страницы %d в сценарии нет — запись пропущена, перебор дальше"),
			R.RecIndex, *CurrentNsr, R.PageId);
		return true;
	};

	for (const FAsdaNpcQuestScriptRow* Rp : Recs)
	{
		const FAsdaNpcQuestScriptRow& R = *Rp;
		if (R.RecIndex >= 64) { break; }             // эталон сканирует РОВНО 64 записи

		if (R.Opcode == 122 || R.Opcode == 123)      // `sub_920B43` = «специальная запись»
		{
			if (R.Opcode == 123)
			{
				// `questNpc_CollectDeliverableItems(this, 0)` возвращает СУММУ обоих списков
				//   («выдаёт» + «принимает»); порог эталона — 2.
				BuildQuestLists();
				const int32 Total = GiveList.Num() + TakeList.Num();
				if (Total >= 2 && !PageMissing(R))
				{
					UE_LOG(LogAsdaNpcDlg, Log,
						TEXT("[NPC-диалог] запись сценария #%d опкод 123: сдать/взять можно %d (>= 2) → страница %d"),
						R.RecIndex, Total, R.PageId);
					RenderPage(R.PageId);
					return;
				}
				// `< 2` — эталон НЕ выходит, а идёт к следующей записи.
				continue;
			}
			// Опкод 122: страница берёт реплику из `Talk/<nsr>.bin`, а не из `.NSR`.
			bTalkPagePending = true;                 // `*(_DWORD *)&this->gap_2D0[468] = 1`
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] запись сценария #%d опкод 122 → страница %d со случайной репликой Talk"),
				R.RecIndex, R.PageId);
			RenderPage(R.PageId);
			return;
		}
		if (R.Opcode == 121) { continue; }            // крафт — эталон запись пропускает

		if (QM && !AsdaNsrScript::StartClosed(R.QuestFreeA) && QM->CheckRecord(R) && !PageMissing(R))   // ★ 06.11.2026: выдача закрыта
		{
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] запись сценария #%d опкод %d: условия сошлись → страница %d"),
				R.RecIndex, R.Opcode, R.PageId);
			RenderPage(R.PageId);
			return;
		}
	}
	// Ни одна из 64 записей не подошла: эталон просто выходит из цикла и НИЧЕГО не делает —
	//   страница остаётся прежней. 1:1, но со строкой в лог (молчание было бы дефектом D6).
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] перебор записей сценария «%s» (%d шт.): подошедших нет — страница не меняется"),
		*CurrentNsr, Recs.Num());
}

// Порт `npcTalkGetEpisodeRecordOff@0xD94D40`:
//     if (npcTalk_LoadEpisodeBin(this, npcIndex) == 1) return buffer + 208 * randInRange(0, 29) + 5;
// Файл — `./Data/NPC/Episode/Talk/<npc_list+0x26>.bin` (`npcTalk_LoadEpisodeBin@0xD9A650`), то есть
//   имя файла = имя NSR-сценария; запись 208 б, текст с `+5`. В порту он уже импортирован в
//   `DT_Talk` (`Tools/extract_text_tables.py::extract_talk`): поле `npc` = имя файла,
//   `line` = номер реплики в файле, `text` = сама реплика.
// ⚠ ДЕФЕКТ ОРИГИНАЛА: `randInRange(0, 29)` берёт 30 записей ВСЕГДА, не глядя на счётчик в шапке
//   файла. В данных этой сборки у 151 файла из 153 записей ровно 30, а у двух — 29, и на них
//   оригинал читает за границей буфера. Здесь при промахе честно берём другую строку и пишем в лог.
FString UAsdaNpcDialogSubsystem::PickEpisodeTalkLine() const
{
	const UDataTable* Dt = AsdaDt::Cached(kDtTalk);
	if (!Dt) { return FString(); }
	TArray<const FAsdaTextRow*> Mine;
	for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
	{
		const FAsdaTextRow* R = reinterpret_cast<const FAsdaTextRow*>(It.Value);
		if (R && R->npc == CurrentNsr) { Mine.Add(R); }
	}
	if (Mine.Num() == 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] опкод 122: реплик сценария «%s» в DT_Talk нет — страница останется без текста"),
			*CurrentNsr);
		return FString();
	}
	const int32 Want = FMath::RandRange(0, 29);       // `randInRange(0, 29)` эталона
	for (const FAsdaTextRow* R : Mine)
	{
		// ★ [ПОРТ] 09.10.2026 (мультиязычность): оверлей `Talk`, ключ `<npc>:<line>` (решение ведущего 09.10.2026).
		if (R->line == Want) { return FAsdaLang::Resolve(TEXT("Talk"), FString::Printf(TEXT("%s:%d"), *R->npc, R->line), R->text); }
	}
	// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА: записи с таким номером в файле нет (файл на 29 реплик).
	const FAsdaTextRow* Fallback = Mine[FMath::RandRange(0, Mine.Num() - 1)];
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] FIX-ORIG: у сценария «%s» реплики №%d нет (всего %d) — эталон здесь читает ")
		TEXT("за границей буфера, берём другую"), *CurrentNsr, Want, Mine.Num());
	return Fallback ? FAsdaLang::Resolve(TEXT("Talk"), FString::Printf(TEXT("%s:%d"), *Fallback->npc, Fallback->line), Fallback->text)
	                : FString();   // [ПОРТ] 09.10.2026 (мультиязычность)
}

// `npcExchangeBuyRequest@0xDA3F60` (npc_mob_pet.c:3814-3834): пять пар «id `+68+4j` / кол-во `+88+4j`»
//   строки `Type`; id < 0 пропускается; нехватка ⇒ нотис 98101 и выход; иначе 6098.
//   Тело перенесено из лямбды `Exchange` команды NPC дословно — теперь это одна точка и для предмета 202.
// ★★ Сторож обмена 6098 — обоснование в объявлении (СРЕДН-4 ревизии items_consistency, 27.09.2026).
//   Требования сравниваются как МНОЖЕСТВА пар «код, кол-во» с семантикой каждой стороны:
//     • клиент (`npcExchangeBuyRequest@0xDA3F60`, npc_mob_pet.c:3814-3834): код < 0 пропускается, пара с
//       «нужно ≤ 0» выполняется всегда — ничего не требует (строки 30/31: «5, −1»);
//     • сервер (`EventItemRequestReq@0x554330`, GameServer.c:130783-130806): идёт по парам ДО ПЕРВОГО кода < 0,
//       каждую найденную списывает; строка с `idx != позиции` — отказ 6099 код 3 (:130780).
//   Совпало — сервер спишет ровно то, что проверил клиент. Не совпало — сервер либо спишет ДРУГИЕ предметы
//   (строки 4-8, 17, 19-21, 58-60), либо ответит «успехом», ничего не сделав (3, 9-12, 18, 38, 39, 42, 57).
bool UAsdaNpcDialogSubsystem::IsEventExchangeServerSafe(int32 Type, FString* OutWhy) const
{
	if (CVarEventExchangeServerGuard.GetValueOnGameThread() == 0)
	{
		if (OutWhy) { *OutWhy = ASDA_DEV_TEXT(TEXT("сторож выключен (asda.EventExchange.ServerGuard 0)"), TEXT("сторож выключен")); }
		return true;
	}
	// Требования клиента для обмена по строке — ИЗ САМОЙ СТРОКИ DT_EventItem (так проверяет `npcExchangeBuyRequest`).
	const FAsdaEventItemRow* C = AsdaDt::RowByInt<FAsdaEventItemRow>(kDtEventItem, Type, TEXT("EventGuardCli"));
	if (!C)
	{
		if (OutWhy) { *OutWhy = FString::Printf(TEXT("строки %d нет в DT_EventItem (клиент)"), Type); }
		return false;
	}
	TArray<TPair<int32, int32>> Cli;
	for (int32 j = 0; j < C->ItemIds.Num(); ++j)
	{
		const int32 Need = C->ItemCounts.IsValidIndex(j) ? C->ItemCounts[j] : 0;
		if (C->ItemIds[j] >= 0 && Need > 0) { Cli.Emplace(C->ItemIds[j], Need); }
	}
	return IsEventExchangeServerSafe(Type, Cli, OutWhy);
}

// ★★ 27.09.2026 (itemsdatafix, КРИТ-2 ревизии items_data) [ПРОТОКОЛ][ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА].
//   Сторож с ЯВНЫМИ требованиями клиента. Кнопки 30048/30049 (game_main.c:10170-10205) проверяют НЕ строку
//   таблицы обмена, а ЗАШИТЫЕ в диспетчер коды (47902 ≥ 11; 47903…47906 по ≥ 1) и шлют 6098 тип 3/4 напрямую.
//   Сравнивать для них DT_EventItem[3/4] с сервером бессмысленно: после приведения строки 4 к серверу (Q1 27.09)
//   сторож «сходился», а кнопка по-прежнему проверяла четыре 47903…47906 — сервер же по строке 4 списал бы
//   75513 и 50×75462. Поэтому сравниваем с сервером ТО, ЧТО РЕАЛЬНО ПРОВЕРЯЕТ КНОПКА.
//   `ClientReq` — пары «код, нужно ≥»; пары с кодом < 0 или «нужно ≤ 0» ничего не требуют и отбрасываются.
bool UAsdaNpcDialogSubsystem::IsEventExchangeServerSafe(int32 Type, const TArray<TPair<int32, int32>>& ClientReq, FString* OutWhy) const
{
	auto Why = [OutWhy](const FString& S) { if (OutWhy) { *OutWhy = S; } };
	if (CVarEventExchangeServerGuard.GetValueOnGameThread() == 0) { Why(ASDA_DEV_TEXT(TEXT("сторож выключен (asda.EventExchange.ServerGuard 0)"), TEXT("сторож выключен"))); return true; }
	if (!AsdaDt::Cached(kDtEventItemServer))
	{
		Why(TEXT("нет DT_EventItemServer (снимок таблицы сервера) — безопасность не доказать"));
		return false;
	}
	const FAsdaEventItemRow* S = AsdaDt::RowByInt<FAsdaEventItemRow>(kDtEventItemServer, Type, TEXT("EventGuardSrv"));
	if (!S)
	{
		Why(FString::Printf(TEXT("строки %d нет в DT_EventItemServer (сервер)"), Type));
		return false;
	}
	if (S->Index != Type)
	{
		Why(FString::Printf(TEXT("у сервера idx строки %d = %d (сервер ответит отказом 3)"), Type, S->Index));
		return false;
	}
	TArray<TPair<int32, int32>> Cli, Srv;
	for (const TPair<int32, int32>& P : ClientReq)
	{
		if (P.Key >= 0 && P.Value > 0) { Cli.Add(P); }
	}
	for (int32 j = 0; j < S->ItemIds.Num(); ++j)
	{
		if (S->ItemIds[j] < 0) { break; }
		Srv.Emplace(S->ItemIds[j], S->ItemCounts.IsValidIndex(j) ? S->ItemCounts[j] : 0);
	}
	auto ByPair = [](const TPair<int32, int32>& A, const TPair<int32, int32>& B)
	{ return A.Key != B.Key ? A.Key < B.Key : A.Value < B.Value; };
	Cli.Sort(ByPair); Srv.Sort(ByPair);
	if (Cli == Srv) { return true; }
	auto Fmt = [](const TArray<TPair<int32, int32>>& L)
	{
		FString R;
		for (const TPair<int32, int32>& P : L) { R += FString::Printf(TEXT("%s%d×%d"), R.IsEmpty() ? TEXT("") : TEXT(", "), P.Key, P.Value); }
		return R.IsEmpty() ? FString(TEXT("ничего")) : R;
	};
	Why(FString::Printf(TEXT("строка %d: клиент требует [%s], сервер спишет [%s]"), Type, *Fmt(Cli), *Fmt(Srv)));
	return false;
}

// ★★ 27.09.2026 (itemsdatafix, КРИТ-2). ОДНА точка «что проверяет кнопка обмена при нажатии»: коды зашиты
//   в диспетчер эталона (game_main.c:10170-10205), строку таблицы кнопки не читают. Её же зовут сами кнопки
//   (ExecuteCommand 30048/30049) и самотесты — чтобы проверка и ожидание теста не разошлись.
bool UAsdaNpcDialogSubsystem::IsExchangeButtonServerSafe(int32 Code, FString* OutWhy) const
{
	if (Code == 30048)
	{
		return IsEventExchangeServerSafe(3, { TPair<int32, int32>(kSoulBindItem, 11) }, OutWhy);
	}
	if (Code == 30049)
	{
		return IsEventExchangeServerSafe(4, { TPair<int32, int32>(kQuadItemFirst, 1), TPair<int32, int32>(kQuadItemFirst + 1, 1),
		                                      TPair<int32, int32>(kQuadItemFirst + 2, 1), TPair<int32, int32>(kQuadItemFirst + 3, 1) }, OutWhy);
	}
	if (OutWhy) { *OutWhy = FString::Printf(TEXT("код %d — не кнопка обмена с зашитыми требованиями"), Code); }
	return false;
}

bool UAsdaNpcDialogSubsystem::RequestEventExchange(int32 Type, int32 NpcId)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	// ★★ [ПРОТОКОЛ][ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] сторож обмена (СРЕДН-4, 27.09.2026) — ДО проверки материалов:
	//   расходящуюся с сервером строку не шлём вовсе; нотис 7502 — текст оригинала для отказа сервера по строке.
	{
		FString Why;
		if (!IsEventExchangeServerSafe(Type, &Why))
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] обмен тип %d (NPC %d): 6098 НЕ отправлен — %s"), Type, NpcId, *Why);
			ShowExchangeGuardRefusal(GI);
			return false;
		}
	}
	const FAsdaEventItemRow* E = AsdaDt::RowByInt<FAsdaEventItemRow>(kDtEventItem, Type, TEXT("NpcDialog"));
	if (E && QM)
	{
		for (int32 j = 0; j < E->ItemIds.Num(); ++j)
		{
			const int32 Id = E->ItemIds[j];
			const int32 Need = E->ItemCounts.IsValidIndex(j) ? E->ItemCounts[j] : 0;
			if (Id < 0) { continue; }
			if (QM->CountItemById(Id) >= Need) { continue; }
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] обмен тип %d: не хватает предмета %d (нужно %d) — нотис 98101"), Type, Id, Need);
			// ★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-6) [ОРИГИНАЛ] канал эталона — ОКНО, не лента: `npcExchangeBuyRequest@0xDA3F60`
			//   (npc_mob_pet.c:3827-3828) `CItem_BuildTooltipText(g_pItemTooltipUI, 0, -1, UI_FormatLocalizedNotice(98101), 1)`
			//   = ONEBUT 1500, ключ очереди (0, −1).
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocExchangeLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
			return false;
		}
	}
	else if (!E)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] обмен тип %d: записи в DT_EventItem нет — проверку материалов пропускаем"), Type);
	}
	// ★ 27.09.2026 (clan_btn_6099): [ОРИГИНАЛ] payload[0] 6098 — `effectSlots[111].field_5B8` = СВОЙ userNum i16
	//   (game_main.c:10182, npc_mob_pet.c:3833), а не charId: сервер отвечает 6099 с тем же userNum (`v22 = arg0`), и приём
	//   сверяет с ним. Сервер поле не проверяет (`EventItemRequestReq` читает только тип), поведение не меняется.
	// ★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-6) [ОРИГИНАЛ] без своего userNum пакет НЕ уходит, молча. Эталон берёт поле из записи
	//   своего персонажа (`CClass_GetField142C(&g_InventoryBag)->effectSlots[111].field_5B8`), которая заполняется при входе
	//   в мир; ветки 30048/30049 того же эталона при `LocalPlayer == NULL` уходят в `LABEL_133` (`Net_RecvUiTick()` — пустая)
	//   и ничего не шлют. Порт прежде слал 6098 с userNum 0 — адресат ответа 6099 не совпал бы ни с кем.
	if (!Net || !Net->IsSelfUserNumKnown())
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] обмен тип %d: свой userNum ещё не пришёл (4003) — 6098 не отправлен, молча (эталон LABEL_133)"), Type);
		return false;
	}
	const int32 SelfUserNum = (int32)Net->SelfUserNum;   // свой userNum (закон charid-vs-usernum)
	return Net->SendNpcExchange6098(SelfUserNum, NpcId, Type);
}

// `objFindRecordIndexByField@0xDA4060`: перебор строк 0…count−1 и пяти id каждой; первая совпавшая.
int32 UAsdaNpcDialogSubsystem::FindEventExchangeRowByItem(int32 ItemCode) const
{
	const UDataTable* Dt = AsdaDt::Cached(kDtEventItem);
	if (!Dt) { return -1; }
	const int32 Count = Dt->GetRowMap().Num();
	for (int32 i = 0; i < Count; ++i)
	{
		const FAsdaEventItemRow* E = AsdaDt::RowByInt<FAsdaEventItemRow>(kDtEventItem, i, TEXT("EventExchange"));
		if (!E) { continue; }
		for (int32 j = 0; j < E->ItemIds.Num() && j < 5; ++j)
		{
			if (E->ItemIds[j] == ItemCode) { return i; }
		}
	}
	return -1;
}

// ★ ЕДИНСТВЕННЫЙ вход «код команды → действие» — порт `CGameUI_HandleControlCommand@0xD87590`
//   (game_main.c:9892) ЦЕЛИКОМ, включая пропуски нумерации.
void UAsdaNpcDialogSubsystem::ExecuteCommand(int32 Code, int32 Param)
{
	// Эталон игнорирует всё, что больше 31000 (в том числе talkId, попавший сюда по ветке `Flag`).
	if (Code > kCmdMax || Code <= 0) { return; }

	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;

	// ═══ ПРЕАМБУЛА ЭТАЛОНА (выполняется ПЕРЕД switch, при любом коде) ═══
	//   `netSendSimpleCmd_5455(localPlayer[52176])` + `charRemoveBuffBySkillId(localPlayer, 15)` —
	//   «слезть с транспорта и снять его баф» перед любым действием у NPC.
	//   ★ Код предмета берём ИЗ ОБЩЕЙ ТОЧКИ — списка состояний (`UAsdaEffectsSubsystem`, тип 2,
	//   подтип 15): ровно оттуда его берёт и инвентарь (`itemUseDispatch@0xA105A0`,
	//   `localPlayer2[1].mountItemId`). Если транспорта нет — эталонное поле нулевое и пакет не
	//   имеет смысла, поэтому НЕ шлём (это не «упрощение»: слать 0 значило бы выдумать код).
	if (UAsdaEffectsSubsystem* Eff = GI ? GI->GetSubsystem<UAsdaEffectsSubsystem>() : nullptr)
	{
		const int32 SelfNum = Net ? (int32)Net->SelfUserNum : -1;
		if (const FAsdaEffectEntry* Mount = Eff->FindByTypeSub(SelfNum, /*SubType*/15, /*Type*/2))
		{
			if (Net && Mount->ItemCode > 0) { Net->SendFunctionItemToggle5455(Mount->ItemCode); }
			Eff->RemoveBySkillState(SelfNum, /*Id*/15, /*Type*/2);
		}
	}

	// ★ 27.09.2026 (аудит отправителей charId, `Reports/owner_0927/charid_senders_audit.md`): два РАЗНЫХ номера своего персонажа.
	//   charId (i32, `CCharacter::userIdx` = `char+1052` — миснейм IDA) — в 6124/6746/6749;
	//   userNum (i16, `effectSlots[111].field_5B8` = `char+190188`) — в 6059/6091/6098/6146.
	const int32 SelfCharId = Net ? Net->SelectedCharId : 0;
	const int32 SelfUserNum = Net ? (int32)Net->SelfUserNum : -1;

	// Отказ с записью в реестр отложенного — ЕДИНАЯ форма для всех неисполнимых кодов
	//   (★ исправление D6: в оригинале такая кнопка не делает РОВНО НИЧЕГО).
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): название окна/подсистемы — строка DT_GameText WhatLoc (язык сессии),
	//   сам нотис — своя строка 990419 «%s» ещё не перенесено в порт (код %d).
	auto Refuse = [&](int32 WhatLoc)
	{
		const FString What = FAsdaText::Loc(WhatLoc);   // название на языке сессии
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] команда %d (параметр %d) от NPC %d «%s»: %s (loc %d) — подсистемы в порту нет ")
			TEXT("(реестр — Docs/Port/PORT_PENDING_ON_SYSTEMS.md)"), Code, Param, CurrentNpcId, *CurrentNsr, *What, WhatLoc);
		if (Msg)
		{
			Msg->PostText(EAsdaMsgCategory::Quest,
				UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990419), { What, FString::FromInt(Code) }));
		}
	};
	// Телепорт/портал: слово в 5477 + закрыть диалог (эталон делает именно в таком порядке).
	auto Portal = [&](int32 Word)
	{
		if (Net) { Net->SendPortalMove5477(Word); }
		Close();
	};
	// Покупка у NPC по коду обмена: проверка пяти пар из `DT_EventItem`, затем 6098 — одна точка
	//   `RequestEventExchange` (её же зовёт использование предмета подтипа 202).
	auto Exchange = [&](int32 Type)
	{
		RequestEventExchange(Type, CurrentNpcId);
	};
	// Призыв рейд-босса: рецепт по паре (npcId, mapId) из `DT_CallMob`.
	auto RaidCall = [&](int32 FixedItem, int32 FixedCount)
	{
		// ★ 27.09.2026 (МЕЛК-6, та же семья) [ОРИГИНАЛ] 30050 / 30061 (game_main.c:10206-10208, :10250/else): без своего
		//   персонажа — `LABEL_133`, молча, ДО поиска рецепта и проверки предметов.
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		int32 ItemId = FixedItem, Need = FixedCount;
		if (FixedItem <= 0)
		{
			const UAsdaLoadManagerSubsystem* LM = GI ? GI->GetSubsystem<UAsdaLoadManagerSubsystem>() : nullptr;
			const int32 MapId = LM ? LM->CurrentMapId : -1;
			const UDataTable* Dt = AsdaDt::Cached(kDtCallMob);
			const FAsdaCallMobRow* Hit = nullptr;
			if (Dt)
			{
				for (const TPair<FName, uint8*>& It : Dt->GetRowMap())
				{
					const FAsdaCallMobRow* R = reinterpret_cast<const FAsdaCallMobRow*>(It.Value);
					// Поля таблицы: `NpcId` = ПЕРВЫЙ ключ поиска, `MapId` = ВТОРОЙ,
					//   `MaterialId` = код нужного предмета, `NeedCount` = сколько его нужно (см. .h).
					if (R && R->NpcId == CurrentNpcId && R->MapId == MapId) { Hit = R; break; }
				}
			}
			if (!Hit)
			{
				// ★ ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА D14: эталон НЕ проверяет возврат поиска и читает
				//   `*(DWORD*)8` при непопадании. Здесь — честный отказ.
				UE_LOG(LogAsdaNpcDlg, Warning,
					TEXT("[NPC-диалог] FIX-ORIG D14: рецепта призыва для npcId=%d на карте %d в DT_CallMob нет ")
					TEXT("(эталон в этом месте читает по нулевому указателю)"), CurrentNpcId, MapId);
				AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocRaidLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);   // канал LABEL_113 эталона
				return;
			}
			ItemId = Hit->MaterialId;
			Need = Hit->NeedCount;
		}
		if (QM && QM->CountItemById(ItemId) < Need)
		{
			// ★ 27.09.2026 (МЕЛК-6, та же семья) [ОРИГИНАЛ] `LABEL_113` (game_main.c:10252-10255): 98103 окном (0, −1).
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocRaidLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
			return;
		}
		// ★ 27.09.2026 [ОРИГИНАЛ]: +0 — charId (`pLocalPlayer3->userIdx`, game_main.c:10215/:10258). Прежде уходил userNum —
		//   сервер отвечал 6125 с кодом 2 «UserInfo incorrect» (GameServer.c:17917).
		if (Net) { Net->SendRaidMonsterCall6124(SelfCharId, ItemId, Need); }
	};
	// Крафт ×N: эталон требует ПО N штук каждого из пяти материалов 20977…20981.
	auto Craft = [&](int32 Count)
	{
		// ★ 27.09.2026 (МЕЛК-6, та же семья) [ОРИГИНАЛ] 30043…30045 (game_main.c:10135-10155): без своего персонажа —
		//   `LABEL_133`, молча, без пакета и без нотиса.
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		bool bOk = true;
		if (QM)
		{
			for (int32 k = 0; k < 5; ++k)
			{
				if (QM->CountItemById(kCraftMaterialFirst + k) < Count) { bOk = false; break; }
			}
		}
		if (!bOk)
		{
			// ★ 27.09.2026 (МЕЛК-6, та же семья) [ОРИГИНАЛ] 30043…30045 (game_main.c:10138-10161): 98102 окном (0, −1).
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocCraftLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
			return;
		}
		// ★ 27.09.2026 [ОРИГИНАЛ]: +0 — СВОЙ userNum (`Target_GetSelectedUserIdx(Field142C)` = номер переданного, т.е. свой).
		//   Прежде уходил номер выбранной цели (−1 без цели).
		if (Net) { Net->SendItemCraft6091(SelfUserNum, CurrentNpcId, Count); }
	};

	switch (Code)
	{
	// ── МАГАЗИН NPC: окно `NPCSHOP_WIN` (1300), подсистема `UAsdaNpcShopSubsystem` ──
	//   Эталон `CGameUI_HandleControlCommand@0xD87590` (game_main.c:9944-10068): у каждой вкладки
	//   свой `shopType`, и переключение происходит ТОЛЬКО если поле `this[290]` («какая вкладка
	//   открыта») отличается. Закрытие предыдущей стороны и открытие новой в порту делает один
	//   `OpenShop`: у нас обе стороны — одна подсистема (обоснование — шапка её .h).
	case 30001:
	case 30002:
	case 30003:
	case 30004:
	case 30026:
	{
		// вкладка → (эталонное значение `this[290]`, shopType)
		const int32 Tab  = (Code == 30001) ? 0 : (Code == 30002) ? 1 : (Code == 30003) ? 2 : (Code == 30004) ? 3 : 13;
		const int32 Type = (Code == 30001) ? 5 : (Code == 30002) ? 6 : (Code == 30003) ? 7 : (Code == 30004) ? 8 : 10;
		// ★ У 30026 эталон гейта по `this[290]` НЕ ставит — открывает всегда (game_main.c:10063).
		if (Code != 30026 && ShopTabState == Tab) { return; }
		ShopTabState = Tab;
		if (UAsdaNpcShopSubsystem* Shop = GI ? GI->GetSubsystem<UAsdaNpcShopSubsystem>() : nullptr)
		{
			Shop->OpenShop(Type, CurrentNpcId);
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда %d: магазин NPC, тип %d, npcId %d"),
				Code, Type, CurrentNpcId);
		}
		else { Refuse(990420); }
		return;
	}

	// ── окно изготовления предмета `g_pItemMainWnd` ──
	//   ★ ПОРТИРОВАНО волной В2 ремесла (25.09.2026). Эталон `CGameUI_HandleControlCommand@0xD87590`:
	//     30009 (game_main.c:9980-9983) — если 76000 видно, `uiWindowHideAndCleanup`, затем как 30088;
	//     30088 (:10330-10334) — `tileInteractGateAndOpen(g_pItemMainWnd)` (тумблер с гейтами).
	//   ★★★ ДЕФЕКТ ОРИГИНАЛА (найден волной В2, доказан адресом): первый гейт `uiCheckPartyInviteInvLockGuard`
	//     проверяет `g_bInventoryLocked` = `dword_2C0F2EC` = `g_pNpcWnd (0x2C0B2E0) + 0x400C` = поле [4099]
	//     «идёт разговор с NPC» (ставит `regionWindow_OpenAndPathMove`, снимает `UI_resetAllWindowsAndLeave_5045`).
	//     Кнопка ремесла живёт ВНУТРИ открытого разговора ⇒ в оригинале она всегда отвечает чатом 5591
	//     «Открыто окно диалога. Закрой его и повтори попытку.» и окна не открывает.
	//     Исправление (`asda.Craft.FixNpcEntry` 1): разговор закрывается ДО гейтов (`Close()`, как у 30035),
	//     потом тумблер. 0 — как оригинал: гейт видит открытый разговор и отвечает 5591.
	//   ★★ ВЕТКА НЕДОСТИЖИМА В ТЕКУЩИХ ДАННЫХ (ревизия В0–В2 С-2, перепроверено волной В3 26.09.2026): команд
	//     30009 / 30010 / 30088 нет ни в одном сценарии NPC порта (`RawAssets/Data/NPC/NpcNsrButton.json` — 72
	//     сценария, 158 кнопок; `NpcNsrPage/NpcNsrHeader/NpcQuestScript` — 0 вхождений) и ни в одном россыпном
	//     `.NSR` семи клиентов (Interlude/MINE/копия/WoS/AsdaGlobal/Arabic/XYT/Фантазиум/NEW). Кнопки ремесла у NPC в
	//     игре нет — дефект латентный, ключ исправляет ветку, в которую игрок не попадает.
	//   ★ Снимок окон (волна В3): `Close()` теперь переоткрывает окна, снятые при входе в разговор
	//     (`UAsdaWindowSubsystem::ReopenCapturedWindows`), — у ветки ремесла это происходит сразу, до показа 76000.
	//   ⚠ Отступление (`Close()` вместо `dialogOnConfirmClose`): эталон прячет только окно диалога и оставляет
	//     свёрнутый HUD до ухода от NPC; у нас разговор кончается целиком и HUD возвращается сразу. Окно 76000
	//     при уходе от NPC НЕ закрывается (как у оригинала: оно не «окно торговца», его открывают и Z/452/меню),
	//     правило владельца 26.08 о закрытии окон торговца его не касается.
	case 30009:
	case 30088:
	{
		UAsdaCraftWindowSubsystem* CraftWin = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr;
		if (!CraftWin) { Refuse(990421); return; }
		if (Code == 30009 && CraftWin->IsOpen()) { CraftWin->HideAndCleanup(); }   // `uiWindowHideAndCleanup`
		const bool bDone = CraftWin->GateAndToggleFromNpc(Code);
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда %d: окно создания снаряжения 76000 %s%s"),
			Code, CraftWin->IsOpen() ? TEXT("открыто") : TEXT("закрыто"), bDone ? TEXT("") : TEXT(" (отклонено гейтом)"));
		return;
	}
	// ★ 30010 (game_main.c:9984-9994): ремесло НЕ открывает — при `pThis[290] != 7` закрывает 76000
	//   (`uiWindowHideAndCleanup`), зовёт `g_pRecvUiHandler->vtbl+8` и пустую `sub_92CA1F` (= `sub_E1EF00`,
	//   тело пустое), ставит состояние 7. Окна `g_pRecvUiHandler` (легаси-заточка 5033/5034) в сборке нет —
	//   см. приём 5034 в `AsdaNetSubsystem.cpp`; этот вызов не переносится.
	case 30010:
		if (ShopTabState != 7)
		{
			if (UAsdaCraftWindowSubsystem* CraftWin = GI ? GI->GetSubsystem<UAsdaCraftWindowSubsystem>() : nullptr)
			{
				if (CraftWin->IsOpen()) { CraftWin->HideAndCleanup(); }
			}
			ShopTabState = 7;
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30010: окно 76000 закрыто, состояние 7"));
		}
		return;

	// ── склад у кладовщика ──
	//   ★ Прежние подписи «склад, вкладка 1 / вкладка 0» БЫЛИ ЛОЖНЫМИ: это не вкладки, а два РЕЖИМА
	//   одного окна 4000 (`warehouseUI_BuildAndOpenBag@0xE89AC0`, `game_main.c:9995/9999`):
	//     30011 = режим 1 «ХРАНИТЬ» (кнопка NPC «Store Items»)  — в сетке ваш инвентарь, кнопка 4002;
	//     30012 = режим 0 «ЗАБРАТЬ» (кнопка NPC «Retrieve Items») — в сетке склад, кнопка 4049.
	//   Присвоение `ShopTabState` (эталон `pThis[290] = 10 / 9`) сохраняем — его читают другие ветки.
	case 30011:
	case 30012:
	{
		const int32 Mode = (Code == 30011) ? 1 : 0;
		ShopTabState = (Code == 30011) ? 10 : 9;
		if (UAsdaWarehouseSubsystem* Ware = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWarehouseSubsystem>() : nullptr)
		{
			Ware->OpenWindow(Mode);
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] команда %d: склад (окно 4000), режим %d (%s)"),
				Code, Mode, Mode == 1 ? TEXT("хранить") : TEXT("забрать"));
		}
		return;
	}
	// ★ 30034 — НЕ склад и НЕ хранилище (подпись в порте была ложной). Эталон `game_main.c:10100`
	//   зовёт `uiStorageWindow_Open`, которая открывает `BOOSTER_SHOP_WIN` (JUI 15300) — КЭШ-МАГАЗИН
	//   БУСТЕРОВ, и шлёт 5466 (запрос баланса кэша). Кода 30034 нет НИ В ОДНОМ из 187 файлов
	//   диалогов ни в одной из 5 сборок клиента — ветка мёртвая, ни один NPC её не вызывает;
	//   магазин бустеров открывается кнопкой премиум-меню 30002 — но и она в оригинале НЕНАЖИМАЕМА:
	//   клиент кладёт сегменты 1 («Бустер») и 3 («Набор») в список ненажимаемых пунктов
	//   (`sub_931C4A(pPremiumMenuBtn, 1)` и `(…, 3)`, `AsdaStory_ida.c:420670-420671`), а читатель
	//   `uiList_OnItemClickSelect@0x12F6440` молча выходит до `RaiseEvent(0x10501)`, если индекс в
	//   этом списке. ⇒ у окна 15300 живого входа в оригинале НЕ БЫЛО ВООБЩЕ.
	//   ★ РЕШЕНИЕ ВЛАДЕЛЬЦА (2026-08-28): дать окну живой вход — кнопка «Магазин Бустеров» у NPC
	//   146 «Робот "Пре-Пре 19"». Сама кнопка добавлена НЕ правкой файла данных (переимпорт стёр бы
	//   её), а в импортёре `Tools/asda_import_nsr_dialog.py`, список `EXTRA_BUTTONS`; там же поднят
	//   `MenuCount` этого NPC с 0 до 1. Подпись задал владелец: готовой строки в таблицах игры нет.
	//   Сегмент 1 премиум-меню при этом оставлен честно недоступным, как в оригинале.
	//   Разбор: `Reports/premium_mall_port.md` (этап 2), `warehouse_entrypoints_npcs.md`.
	case 30034:
	{
		if (UAsdaBoosterShopSubsystem* BS = GI ? GI->GetSubsystem<UAsdaBoosterShopSubsystem>() : nullptr)
		{
			BS->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] команда 30034: магазин бустеров (15300), npcId %d"), CurrentNpcId);
		}
		else { Refuse(990422); }   // окно 15300 магазин бустеров; номер окна в текст loc 990422 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		return;
	}

	// ── КЛАН (а вовсе НЕ ремонт) ──
	//   ★★★ ВСЕ ТРИ ПОДПИСИ БЫЛИ ЛОЖНЫМИ. Врали и имена функций эталона: `uiRepairWindowToggle`
	//   к ремонту отношения не имеет. Проверено дизассемблером и по данным диалогов:
	//     30015 → СОЗДАНИЕ клана, окно `CLAN_MAKING_WIN` (14400), опкод 4104 → 4105,
	//             вступительный взнос 10 000 золота (константа @0xBBBC2E);
	//     30016 → РОСПУСК клана,  окно `CLAN_DELETE_BOX` (14470), опкод 4106 → 4107;
	//     30027 → расширение клана, окно `CLAN_INCREASE_BOX` (14450), опкод 4136 → 4137
	//             (★ 27.09.2026: сервер даёт +10 мест и 5 расширений; клиент-эталон рисовал +5 и 4 —
	//             порт показывает серверные правила, шапка `AsdaClanSubsystem.h`).
	//   ★ Кнопки зовёт ровно ОДИН NPC — 107 «Командир рыцарей Элла» (кнопка «Основать клан» и
	//   «Распустить клан»). ★ Кнопки с кодом 30027 нет ни в одном из пяти деревьев данных
	//   (187…195 файлов диалогов; проверено и по структуре записи, и сырым поиском константы), но код
	//   клиента и сервер живые — окно подключено целиком и откроется, как только код появится в данных.
	//   ★ [ПОРТ] 27.09.2026 — решение владельца: кнопка «Расширить клан» (код 30027, строка 990349) добавлена Элле в
	//   данные порта (`EXTRA_BUTTONS` в `Tools/asda_import_nsr_dialog.py`, слот 3 — сразу за видимыми, скрытая
	//   кнопка данных уехала в слот 4 и осталась скрытой; `MenuCount` 3 → 4). Отчёт `Reports/owner_0927/clan_btn_6099.md`.
	//   Разбор (18 миснеймов, дефекты D1…D7): `Reports/npc_repair_variants_30015.md`.
	case 30015:
	{
		if (UAsdaClanSubsystem* Clan = GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr)
		{
			Clan->OpenCreateWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30015: создание клана (окно 14400)"));
		}
		else { Refuse(12216); }
		return;
	}
	case 30016:
	{
		if (UAsdaClanSubsystem* Clan = GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr)
		{
			Clan->OpenDisbandWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30016: роспуск клана (окно 14470)"));
		}
		else { Refuse(990423); }
		return;
	}
	// ★ 27.09.2026: окно расширения реализовано (прежде — честный отказ). Кнопка — у Эллы, добавка порта (см. выше).
	case 30027:
	{
		if (UAsdaClanSubsystem* Clan = GI ? GI->GetSubsystem<UAsdaClanSubsystem>() : nullptr)
		{
			Clan->OpenIncreaseWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30027: расширение клана (окно 14450)"));
		}
		else { Refuse(990424); }   // окно 14450 увеличение мест в клане; номер окна в текст loc 990424 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		return;
	}
	// ★★★ 30069 — «Ремонт вещи». Эталон `CGameUI_HandleControlCommand@0xD87590` (game_main.c:10280):
	//   `uiShowUniconWarWindow(g_pShopSellWnd)` @0xEDA280 — ИМЯ ФУНКЦИИ ВРЁТ, это ПОКАЗ ОКНА
	//   РЕМОНТА `INVEN_Repair_Win` (JUI 57300). Ложная зацепка «`repairPanel`, ui_core.c:15744»
	//   снята: та функция шлёт `OP_CLAN_CREATE_REQ` и относится к созданию клана.
	//   Ремонт стоит ТОЛЬКО ЗОЛОТА (подпись 57325 = нотис 13241 «Золото:»).
	//   Полный разбор — `Reports/repair_and_warshop_port.md` §1.
	case 30069:
	{
		if (UAsdaRepairSubsystem* Rep = GI ? GI->GetSubsystem<UAsdaRepairSubsystem>() : nullptr)
		{
			Rep->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30069: окно ремонта (57300)"));
		}
		else { Refuse(990425); }
		return;
	}

	// ── АУКЦИОН: окно `EXCHANGE_WIN` (13000), подсистема `UAsdaExchangeSubsystem` ──
	//   ★★★ Прежние подписи «посредник, вкладка N» ВРАЛИ ДВАЖДЫ. Во-первых, это не «вкладки», а
	//   РЕЖИМЫ одного окна: гейт `sub_922402@0x922402` → `get_o4C8@0xD88960` читает байт `+0x4C8`
	//   («текущий режим»), а не индекс вкладки (`game_main.c:10009-10076`). Во-вторых, номера у
	//   двух режимов ЗЕРКАЛЬНЫ относительно подписей:
	//     30018 → режим 1: выставить лот из ИНВЕНТАРЯ (контейнер 2, 60 ячеек);
	//     30017 → режим 2: выставить лот из СУМКИ     (контейнер 1, 30 ячеек);
	//     30019 → режим 3: ★ ЭТО НЕ ОКНО, А ДЕЙСТВИЕ — шлёт запрос «мои лоты» (9907) и окно
	//                      НЕ показывает вовсе (гейт `if (a2 != 3)`, `item_inventory.c:46788`);
	//     30021 → режим 4: поиск и покупка, набор фильтров `RADIOBTN2` (7 кнопок);
	//     30028 → режим 5: поиск и покупка, набор фильтров `RADIOBTN1` (8 кнопок)
	//                      — ★ имена контролов у наборов тоже перепутаны местами.
	//   Гейт «режим уже активен» живёт внутри `OpenMode`, поэтому зовём безусловно.
	//   Полный разбор (479 узлов, опкоды 9901…9915, 16 миснеймов): `Reports/npc_exchange_win_13000.md`.
	case 30017:
	case 30018:
	case 30019:
	case 30021:
	case 30028:
	{
		const int32 ExMode = (Code == 30018) ? 1 : (Code == 30017) ? 2
		                   : (Code == 30019) ? 3 : (Code == 30021) ? 4 : 5;
		if (UAsdaExchangeSubsystem* Ex = GI ? GI->GetSubsystem<UAsdaExchangeSubsystem>() : nullptr)
		{
			Ex->OpenMode(ExMode);
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] команда %d: аукцион (13000), режим %d%s"),
				Code, ExMode, ExMode == 3 ? TEXT(" — запрос «мои лоты», окно не показывается") : TEXT(""));
		}
		else { Refuse(990426); }   // окно 13000 аукцион; номер окна в текст loc 990426 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		return;
	}
	// ── МАГАЗИН СНАРЯЖЕНИЯ: окно `SHELLITEM_STORE2_WIN` (6800), подсистема `UAsdaEquipShopSubsystem` ──
	//   ★★★ 30020 — НЕ «посредник» и НЕ «окно торговли» (так его звал прежний реестр): эталон
	//   `CGameUI_HandleControlCommand@0xD87590` (game_main.c:10021) зовёт `shopWin_Open@0xC37110`
	//   на `g_pShopWnd`. Это магазин снаряжения с ПРИМЕРКОЙ, витрина — `./Data/NPC/Shop/FreeShop.she`,
	//   валюта — золото, опкоды 5020 → 5021. Подписи кнопок NPC: 140npc «Купить вещь»,
	//   shuren / yazark «Купить вещи» (все трое — «Торговец снаряжением»).
	//   ★ Эталонный гейт `if (this[290] != 8)` НЕ пропускает открытие, когда уже открыта вкладка 8;
	//   такой вкладки ни одна кнопка не ставит (значения 0…3, 7, 9, 10, 13), поэтому гейт всегда
	//   истинен — воспроизводим его буквально полем `ShopTabState`.
	case 30020:
	{
		if (ShopTabState == 8) { return; }
		if (UAsdaEquipShopSubsystem* Eq = GI ? GI->GetSubsystem<UAsdaEquipShopSubsystem>() : nullptr)
		{
			Eq->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30020: магазин снаряжения (6800), npcId %d"), CurrentNpcId);
			// ★ Волна В4 ремесла (ревизия В3 С-1): `shopWin_Open@0xC37110` зовёт `dialogOnConfirmClose(g_pNpcWnd, 1)`
			//   (shop_trade.c:3857) — разговор прячется под магазином; выход из разговора и восстановление снимка окон —
			//   в закрытии магазина (`shopWin_Close` → 5045, shop_trade.c:3918).
			if (Eq->IsOpen()) { HideForChildWindow(TEXT("6800")); }
		}
		else { Refuse(990427); }
		return;
	}

	// ── «Сохранить местоположение» (точка возрождения и свитка возврата) ──
	//   ★ 26.10.2026 (savepoint) [ОРИГИНАЛ] game_main.c:10029-10032: `pThis[24106] = pThis[4322]` (npcId разговора в
	//   g_pNpcWnd+0x178A8) и `CItem_BuildTooltipText(g_pItemTooltipUI, 8, 6178, nullptr, 1)` — НЕ подсказка, а вопрос
	//   TWOBUT 1520 вида 8 (item_inventory.c ветка `case 8`: текст loc noticeId, кнопки 2288 «ОК» / 2289 «Отмена»).
	//   «Да» → `SavePointConfirmed` (5280 + 6179), «Нет» — ничего. Разговор с NPC остаётся открытым (эталон его не трогает).
	//   Кнопка — слот 0 у трёх NPC: 24 «Помощник Вуди» (Силарис), 101 «Эко» (Альпен), 138 (Фламмио).
	//   npcId берётся В МОМЕНТ показа (у эталона поле перезапишет и повторный 30022 при уже открытом вопросе, который очередь
	//   (8, 6178) отбросит; сервер npcId не читает — расхождение без следствий).
	case 30022:
	{
		if (CVarNpcSavePointSend.GetValueOnGameThread() != 0)
		{
			UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
			if (!WM) { Refuse(kLocSavePointTip); return; }
			const FString Q = AsdaNotice::Loc(GI, kLocSavePointTip);
			const int32 SaveNpcId = CurrentNpcId;   // `pThis[24106] = pThis[4322]`
			TWeakObjectPtr<UGameInstance> WeakGI(GI);
			UUserWidget* Box = WM->ShowGameConfirm(Q.IsEmpty() ? FAsdaLang::MissingTag(kLocSavePointTip) : Q,
				[WeakGI, SaveNpcId](bool bYes)
				{
					if (!bYes) { UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] точка сохранения: «Нет» (8, 6178) — ничего")); return; }
					SavePointConfirmed(WeakGI.Get(), SaveNpcId);
				}, 0.f, 2288, 2289, /*очередь: эталон (8, 6178)*/kLocSavePointTip, /*вид*/8);
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30022: вопрос (8, %d) «%s», npcId %d%s"), kLocSavePointTip, *Q,
				SaveNpcId, Box ? TEXT("") : TEXT(" — уже в очереди, повтор отброшен"));
			return;
		}
		// asda.Npc.SavePointSend 0 — прежний порт: баннер с текстом 6178, на сервер ничего не уходит.
		UAsdaTextSubsystem* TS = GI ? GI->GetSubsystem<UAsdaTextSubsystem>() : nullptr;
		const FString Text = TS ? TS->GetTextById(kLocSavePointTip) : FString();
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30022: подсказка %d «%s»"), kLocSavePointTip, *Text);
		ShowQuestInfo(Text.IsEmpty() ? FAsdaLang::MissingTag(kLocSavePointTip) : Text);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		return;
	}

	// ── СОУЛМЕЙТ («Друг») — ★ ВОЛНА 1-А (06.10.2026), отчёт `Reports/owner_1006/wave1A_soulmate.md` ──
	//   [ОРИГИНАЛ] `CGameUI_HandleControlCommand` (AsdaStory_game.c:387981-388012). Гейты читают слот Друга своего
	//   персонажа `gap_878[47]` (−1 — Друга нет); у 30023 сравнение `>= 0`, у 30024/30025 — `<= 0`, как у эталона.
	//   В данных: 30023 — кнопка 0 у NPC 106 «Статуя души» (`soul_statue`), 30024 — кнопка 0 у NPC 40 (`cave`),
	//   30025 — ни у кого (код живой, входа в данных нет).
	case 30023:
	{
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Soul) { Refuse(990429); return; }
		if (Soul->GetFriendSlot() >= 0)
		{
			// Друг уже есть: loc 8505 в журнал и плашку 221 (белым, 10 с); разговор НЕ закрывается.
			Soul->NoticeFriendAlreadyExists8505();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30023: Друг уже есть (слот %d) → нотис 8505"), Soul->GetFriendSlot());
			return;
		}
		// Друга нет: `UI_resetAllWindowsAndLeave_5045(this)` — выход из разговора, затем `editBoxShowAndFocus` — окно 9100.
		ResetAllWindowsAndLeave(TEXT("30023 → окно 9100 (UI_resetAllWindowsAndLeave_5045)"));
		Soul->OpenLinkWindow();
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30023: разговор закрыт, окно 9100 «предложить Дружбу по имени»"));
		return;
	}
	case 30024:
	case 30025:
	{
		UAsdaSoulmateSubsystem* Soul = GI ? GI->GetSubsystem<UAsdaSoulmateSubsystem>() : nullptr;
		if (!Soul) { Refuse(990429); return; }
		// Гейт «нет Друга или разрыв уже запрошен» → (0, 24) loc 6035; иначе подтверждение: 30024 → (1, 8036) loc 8036,
		//   30025 → (1, 29) loc 8030; «Да» у обоих → 5204. Разговор с NPC в обеих ветках остаётся открытым.
		if (Code == 30024) { Soul->NpcForceBreak30024(); } else { Soul->NpcBreak30025(); }
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда %d: разрыв Дружбы (слот %d)"), Code, Soul->GetFriendSlot());
		return;
	}

	// ── парикмахерская, extract-shop, обмен предметами, нумпад, honor shop ──
	// ── САЛОН ПРИЧЁСОК: окно `HAIRSHOP_WIN` (15000), подсистема `UAsdaHairShopSubsystem` ──
	//   ★ Живого входа у окна в оригинале НЕ БЫЛО: код 30029 не встречается в данных диалогов ни у
	//   одного из 187 NPC, а сегмент 2 премиум-меню ненажимаем (индексы лежат в контейнере `+0x268`,
	//   читатель `uiList_OnItemClickSelect@0x12F6440` для них молча выходит).
	//   ★ РЕШЕНИЕ ВЛАДЕЛЬЦА (2026-08-28): вход даёт кнопка «Салон причесок» у NPC 132 «Парикмахер
	//   Юки». Кнопка добавлена не правкой файла данных (переимпорт стёр бы её), а списком
	//   `EXTRA_BUTTONS` в `Tools/asda_import_nsr_dialog.py`; там же поднят `MenuCount` этого NPC.
	//   Подпись — строка 99402 «Салон причесок» из таблицы игры.
	//   Разбор окна: `Reports/npc_hairshop_30029.md`, порт — `premium_mall_port.md`.
	case 30029:
	{
		if (UAsdaHairShopSubsystem* HS = GI ? GI->GetSubsystem<UAsdaHairShopSubsystem>() : nullptr)
		{
			HS->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] команда 30029: салон причёсок (15000), npcId %d"), CurrentNpcId);
		}
		else { Refuse(99402); }
		return;
	}
	case 30032: Refuse(6803); return;
	// ── ПЕРЕДАЧА ПАРАМЕТРОВ: окно `EXTRACT_WIN` (16000), класс `CExtractShop@0xC8E390`.
	//   ⚠ Прежняя подпись «extract-shop» ничего не говорила о сути: окно ПЕРЕСАЖИВАЕТ пять пар
	//   заточки/зачарования (поля записи +29…+48) с одного предмета на другой; источник
	//   УНИЧТОЖАЕТСЯ, приёмник привязывается к владельцу. Валюта — ЗОЛОТО (док врал про Honor).
	//   ★ Волна В4 ремесла (ревизия В3 С-3): эталон при открытии зовёт `dialogOnConfirmClose@0xDA3BB0`
	//   (`extractShop_Open`, item_enhance.c:940) — разговор ПРЯЧЕТСЯ, а не заканчивается; выход из разговора и
	//   восстановление снимка окон делает закрытие окна (`extractShop_Close` → 5045, item_enhance.c:1003). Прежний
	//   порт звал здесь `Close()`, и окна снимка переоткрывались в момент ОТКРЫТИЯ 16000.
	case 30035:
	{
		if (UAsdaExtractShopSubsystem* Ext = GI ? GI->GetSubsystem<UAsdaExtractShopSubsystem>() : nullptr)
		{
			Ext->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30035: окно передачи параметров (16000)"));
			if (Ext->IsOpen()) { HideForChildWindow(TEXT("16000")); } else { Close(); }
		}
		else { Refuse(990430); Close(); }
		return;
	}
	// ★ Волна 2-Б (06.10.2026). 30051 «Donation» (NPC 163) — ПОЖЕРТВОВАНИЕ, эталон game_main.c:10217-10220:
	//   `uiOpenContextPopupWithHandlers(g_pNumPadDialog, CountItemById(60073), 6, 0)` — нумпад режима 6 ПО ЦЕНТРУ, разговор
	//   остаётся; ОК → 6132. Поток, отправка и приём 6133 — `UAsdaDonationSubsystem` (отчёт `owner_1006/wave2B_hunt_donation.md` §3).
	case 30051:
	{
		if (UAsdaDonationSubsystem* Don = GI ? GI->GetSubsystem<UAsdaDonationSubsystem>() : nullptr) { Don->OpenNumPad(); }
		else { Refuse(990431); }
		return;
	}
	// ★★★ 30070 — «Военный магазин». Эталон (game_main.c:10283): `uiHonorShopDlgShow` @0xB768D0 —
	//   окно `HONOR_itemmall_Win` (JUI 67200); серверный обработчик покупки называется
	//   `CharRoseWarStoreBuy@0x514FE0`. Валюта — ПРЕДМЕТ 20628 «Монеты славы» плюс требование
	//   воинского звания фракции. Полный разбор — `Reports/repair_and_warshop_port.md` §2.
	//   ★ Эталон после открытия зовёт `UI_resetAllWindowsAndLeave_5045` — выход из разговора.
	case 30070:
	{
		if (UAsdaWarShopSubsystem* War = GI ? GI->GetSubsystem<UAsdaWarShopSubsystem>() : nullptr)
		{
			War->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30070: военный магазин (67200)"));
		}
		else { Refuse(990432); }
		return;
	}

	// ── ЭЛИТНЫЙ МАГАЗИН 6000 `SHELLITEM_STORE_WIN` (волна В5 ремесла, ревизия В4 W4-3) ──
	//   Эталон (`CGameUI_HandleControlCommand`, game_main.c:10081-10084):
	//     `if (!helper_getField2075(g_pShopWnd)) uiInventoryOpenWindow(g_pShopWnd); exchangeItemClose(g_pExchangeItemWnd);`
	//   Оба имени IDA врут: `uiInventoryOpenWindow@0xC3DCB0` — открытие элитного магазина (`CShellItemStore_InitControls`
	//   берёт окно 6000, item_inventory.c:19551), `+2075` — «магазин открыт». Прежний порт открывал здесь СУМКУ — неверно.
	//   Гейты и порядок — внутри общей точки `UAsdaPremiumShopSubsystem::OpenWindow` (снимок окон, «закрыть всё», разговор
	//   прячется `dialogOnConfirmClose`, закрыт магазин NPC и нумпад, запрос баланса 5466).
	//   `exchangeItemClose` (окно обмена предметами `CExchangeItem`, команда 30032) — окна в порту нет (30032 отказывается).
	//   ⚠ Данные: в `Nsr.bsz` (единственный источник сценариев, загрузчик `CNpcEpisodeData_LoadNSR_Bank0@0xD9A990`) кнопки с
	//   кодом 30030 нет ни у одного NPC; он есть только в ЛИШНЕМ файле `Data/NPC/Episode/BOOK/131NPC.NSR`, который клиент не
	//   читает (папка BOOK — только `%s.QST`), — у оригинала живого входа в данных нет (отчёт `wave5.md`).
	//   ★ Решение владельца 26.09.2026: кнопка «Элитный магазин» (loc 30100) добавлена NPC 131 портовой добавкой
	//   `EXTRA_BUTTONS` импортёра `Tools/asda_import_nsr_dialog.py` (131npc, кнопка 1) — по BOOK-файлу того же NPC.
	case 30030:
	{
		UAsdaPremiumShopSubsystem* Ps = GI ? GI->GetSubsystem<UAsdaPremiumShopSubsystem>() : nullptr;
		if (!Ps) { Refuse(990507); return; }   // окно 6000 элитный магазин; номер окна в текст loc 990507 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		if (!Ps->IsOpen()) { Ps->OpenWindow(/*bWantPackage*/false); }   // гейт `!helper_getField2075(g_pShopWnd)`
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30030: элитный магазин 6000 %s; exchangeItemClose — окна обмена предметами в порту нет"),
			Ps->IsOpen() ? TEXT("открыт") : TEXT("НЕ открыт"));
		return;
	}

	// ── ТЕЛЕПОРТ И ПОРТАЛЫ (опкод 5477) ──
	case 30031:
	{
		// `netSendReqWord_op5477(this, *(u8*)(WarpListTable + 16*listIndex + 4))` — МЛАДШИЙ БАЙТ mapId.
		if (Param < 0)
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 30031: параметр (номер строки WarpListTable) < 0 — эталон только закрывает окно"));
			Close();
			return;
		}
		const FAsdaWarpListRow* W = AsdaDt::RowByInt<FAsdaWarpListRow>(kDtWarpList, Param, TEXT("NpcDialog"));
		if (!W)
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 30031: строки %d в DT_WarpList нет"), Param);
			Close();
			return;
		}
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 30031 телепорт: строка %d → карта %d (клетка %d,%d)"),
			Param, W->MapId, W->CellX, W->CellY);
		Portal(W->MapId & 0xFF);
		return;
	}
	case 30033: Portal(3);  return;
	case 30036: Portal(4);  return;
	case 30037: Portal(5);  return;
	case 30039:
	case 30041: Portal(0);  return;
	case 30040: Portal(3);  return;
	case 30042: Portal(7);  return;
	case 30063: Portal(21); return;
	case 30064: Portal(22); return;

	// ── событийный NPC (6059) ──
	//   ★ 27.09.2026 [ОРИГИНАЛ] (game_main.c:10115-10120): без своего персонажа — `LABEL_133`, молча; слово +0 — СВОЙ userNum
	//   (`localPlayer->effectSlots[111].field_5B8`), прежде уходил charId. Суточный отказ 7501 — внутри отправителя.
	case 30038:
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		Net->SendEventNpc6059(SelfUserNum, CurrentNpcId, 1);
		return;

	// ── крафт ×1 / ×5 / ×10 (6091) ──
	case 30043: Craft(1);  return;
	case 30044: Craft(5);  return;
	case 30045: Craft(10); return;

	// ── СБРОС/ВОССТАНОВЛЕНИЕ УЛУЧШЕНИЙ: ОДНО окно `Item_Initializ_Win` (20200) в ДВУХ режимах ──
	//   ⚠ Прежние подписи «рефайн» врали: к рефайн-шопу ветка не относится ни одной ссылкой.
	//   Режим 0 (30046) — полный сброс заточки, попытки в максимум, платим ЗОЛОТОМ.
	//   Режим 1 (30047) — уровень СОХРАНЯЕТСЯ, возвращаются только неудачные попытки, платим СВИТКАМИ.
	//   Оба режима обслуживает одна подсистема (два агента пришли к этому независимо).
	case 30046:
	case 30047:
	{
		const int32 Mode = (Code == 30047) ? 1 : 0;
		if (UAsdaItemResetSubsystem* Reset = GI ? GI->GetSubsystem<UAsdaItemResetSubsystem>() : nullptr)
		{
			Reset->OpenWindow(Mode);
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] команда %d: окно сброса улучшений (20200), режим %d"), Code, Mode);
		}
		else { Refuse(990433); }   // окно 20200 сброс улучшений; номер окна в текст loc 990433 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		// ★ Волна В4 ремесла (ревизия В3 С-3): `refineWnd_Open@0xD30170` (item_enhance.c:2045) разговор НЕ закрывает и не
		//   прячет — окна снимка возвращает уход от NPC. Прежний порт звал здесь `Close()`, и они переоткрывались в момент
		//   открытия 20200. Правило владельца 26.08 соблюдено: окно 20200 закрывается вместе с разговором (`Close()`).
		return;
	}
	// ── ИЗМЕНЕНИЕ ПАРАМЕТРОВ ПРЕДМЕТА: окно `ItemOptionValue_WIN` (50300) ──
	//   ⚠ Прежняя подпись «окно апгрейда предмета» врала: настоящий апгрейд — 30073.
	//   Здесь значения ДВУХ параметров вещи перебрасываются в пределах её диапазонов,
	//   расходник — «Свиток обмена параметрами» (`special_itemlist.SubType == 67`).
	case 30060:
	{
		if (UAsdaItemOptionSubsystem* Opt = GI ? GI->GetSubsystem<UAsdaItemOptionSubsystem>() : nullptr)
		{
			Opt->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30060: окно смены параметров предмета (50300)"));
		}
		else { Refuse(109600); }
		Close();
		return;
	}
	// ── ВЫБОР ЗНАКА ЗОДИАКА: окно `Constellation_S_WIN` (74500) ──
	//   ★ Подпись «окно апгрейда (вариант B)» БЫЛА ЛОЖНОЙ. Эталон `game_main.c:10297` зовёт
	//   `uiUpgradeWndOpen@0xECBED0` — имя тоже врёт: функция открывает окно выбора ЗНАКА ЗОДИАКА
	//   (12 знаков из `./Data/Constellation.BIN`). Выбор НЕОБРАТИМ, знак ложится в `CCharacter+0x460`,
	//   отправка — пакет 6586, ответ 6587. Владелец кнопки — NPC 106 «Статуя души» (Alpen).
	//   Ни ячеек, ни предметов, ни валюты у окна нет. Разбор: `Reports/npc_button_30073.md`.
	//   ★ Эталон окно диалога НЕ закрывает — `Close()` здесь НЕ зовём (1:1).
	case 30073:
	{
		if (UAsdaConstellationSubsystem* Cons = GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr)
		{
			Cons->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30073: окно выбора знака зодиака (74500)"));
		}
		else { Refuse(990434); }   // окно 74500 выбор знака зодиака; номер окна в текст loc 990434 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		return;
	}

	// ── обмен с NPC по коду (6098) ──
	case 30048:
		// ★ 27.09.2026 (clan_btn_6099_fix, МЕЛК-6) [ОРИГИНАЛ] первый шаг эталона (game_main.c:10171-10173):
		//   `CharManager_GetLocalPlayer() == NULL → LABEL_133` — молча, без пакета. У порта «свой персонаж есть» = userNum пришёл (4003).
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		// ★★ сторож обмена (СРЕДН-4, 27.09.2026; ★ КРИТ-2 itemsdatafix 27.09): кнопка шлёт 6098 тип 3 напрямую и проверяет
		//   СВОЁ зашитое требование — 47902 ≥ 11 (game_main.c:10174-10175), а не строку DT_EventItem[3]. С сервером сравниваем
		//   именно его: у сервера строка 3 пуста (EventItem_Table.txt) — «успех» без обмена ⇒ отказ окном 7502.
		if (FString Why; !IsExchangeButtonServerSafe(30048, &Why))
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 30048: 6098 тип 3 НЕ отправлен — требование кнопки расходится с сервером: %s"), *Why);
			ShowExchangeGuardRefusal(GI);
			return;
		}
		if (QM && QM->CountItemById(kSoulBindItem) < 11)
		{
			// [ОРИГИНАЛ] game_main.c:10177-10178: 98102 окном (0, −1).
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocCraftLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
			return;
		}
		if (Net) { Net->SendNpcExchange6098((int32)Net->SelfUserNum, CurrentNpcId, 3); }   // ★ 27.09: userNum, как эталон (game_main.c:10182/10203)
		return;
	case 30049:
	{
		// ★ 27.09.2026 (МЕЛК-6) [ОРИГИНАЛ] game_main.c:10192-10194: без своего персонажа — `LABEL_133`, молча, без пакета.
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		// ★★ сторож обмена (СРЕДН-4; ★ КРИТ-2 itemsdatafix 27.09): кнопка проверяет СВОИ зашитые 47903…47906 по ≥ 1
		//   (game_main.c:10190-10196), а сервер по строке 4 спишет 75513×1 и 75462×50. Сравниваем с сервером требование
		//   КНОПКИ, а не DT_EventItem[4] (её Q1 привёл к серверу, и сторож по строке стал пустым) ⇒ отказ окном 7502.
		if (FString Why; !IsExchangeButtonServerSafe(30049, &Why))
		{
			UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 30049: 6098 тип 4 НЕ отправлен — требование кнопки расходится с сервером: %s"), *Why);
			ShowExchangeGuardRefusal(GI);
			return;
		}
		bool bOk = true;
		if (QM) { for (int32 k = 0; k < 4; ++k) { if (QM->CountItemById(kQuadItemFirst + k) <= 0) { bOk = false; break; } } }
		if (!bOk)
		{
			// [ОРИГИНАЛ] game_main.c:10198-10199: 98102 окном (0, −1).
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLocCraftLack, AsdaLoc::kBtnOk, /*EtalonNoticeId*/-1);
			return;
		}
		if (Net) { Net->SendNpcExchange6098((int32)Net->SelfUserNum, CurrentNpcId, 4); }   // ★ 27.09: userNum, как эталон (game_main.c:10182/10203)
		return;
	}
	case 30052: Exchange(9);  return;
	case 30053: Exchange(10); return;
	case 30054: Exchange(11); return;
	case 30055: Exchange(12); return;
	case 30079: Exchange(58); return;
	case 30080: Exchange(59); return;
	case 30081: Exchange(60); return;
	case 30082: Exchange(57); return;
	case 30083: Exchange(48); return;
	case 30084: Exchange(49); return;
	case 30085: Exchange(50); return;

	// ── призыв рейд-босса (6124) ──
	case 30050: RaidCall(-1, 0);                       return;   // рецепт из DT_CallMob по (npcId, mapId)
	case 30061: RaidCall(kRaidFixedItem, kRaidFixedCount); return;

	// ── CampWar ──
	// ★★★ 30.10.2026 (баг игроков «нет кнопки выбора фракции»): 30056 — ВЫБОР АРМИИ, окно `CAMPWAR_Win` 44000
	//   (NPC 133/179/180, «Select Team»). Прежде здесь стоял отказ «окно выбора армии ещё не перенесено» (loc 990435) —
	//   и разговор оставался открытым, поэтому каждое нажатие печатало строку заново. Эталон (game_main.c:10233-10236):
	//   `campWarWindowShow(g_pCampWarClan)`, затем `UI_resetAllWindowsAndLeave_5045` — окно показано, разговор закрыт.
	//   Окно, подтверждение 46000, 6702 и ответ 6703 — `UAsdaFactionSelectSubsystem` (`Reports/faction_select/FLOW.md`).
	case 30056:
	{
		if (UAsdaFactionSelectSubsystem* Faction = GI ? GI->GetSubsystem<UAsdaFactionSelectSubsystem>() : nullptr)
		{
			Faction->OpenFromNpc();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30056: окно выбора армии (44000)"));
		}
		Close();   // `UI_resetAllWindowsAndLeave_5045`
		return;
	}
	// ★★★ 30.10.2026 (CampWar целиком): 30057 (180npc, Альпия) / 30074 (179npc, Силарис) / 30075 (133npc, Пламения) — окно
	//   «Битва армий» `CAMPWAR_Join` 47000. Прежде здесь стоял отказ «инфо-окно CampWar ещё не перенесено» (loc 990436), и
	//   разговор оставался открытым. Эталон (game_main.c:10237-10242): `CCampWar_ShowInfoWindow(g_pCampWarClan)`, затем
	//   `UI_resetAllWindowsAndLeave_5045` — окно показано, разговор закрыт. Регистрация, очередь, вызов и вход —
	//   `UAsdaCampWarSubsystem` (`Reports/faction_select/REG_SPEC.md`).
	case 30057:
	case 30074:
	case 30075:
	{
		if (UAsdaCampWarSubsystem* CampWar = GI ? GI->GetSubsystem<UAsdaCampWarSubsystem>() : nullptr)
		{
			CampWar->OpenJoinWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда %d: окно «Битва армий» (47000)"), Code);
		}
		Close();   // `UI_resetAllWindowsAndLeave_5045`
		return;
	}
	// ★★★ 06.10.2026 (волна 2-Г): 30076 — ОКНО ГОРОДА «Установить налог» `Town_as_WIN` 79300 (NPC 133npc/179npc/180npc).
	//   Прежний отказ «панель фракции CampWar» был неверен: эталон (game_main.c:10300-10302) зовёт
	//   `CUniconWar_UpdateFactionInfoPanel(g_pCommunityWnd)` — окно ставки налога города, затем
	//   `UI_resetAllWindowsAndLeave_5045` (разговор закрыт). Окно, 6744 и 6745 — `UAsdaTownWarSubsystem`
	//   (отчёт `Reports/owner_1006/wave2D_gift_townwar.md` §2).
	case 30076:
	{
		if (UAsdaTownWarSubsystem* Town = GI ? GI->GetSubsystem<UAsdaTownWarSubsystem>() : nullptr)
		{
			Town->OpenFromNpc();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30076: окно налога города (79300)"));
		}
		else { Refuse(990437); }   // окно 79300 налог города; номер окна в текст loc 990437 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		Close();   // `UI_resetAllWindowsAndLeave_5045`
		return;
	}
	case 30077:
		if (Net) { Net->SendCampWarReq6746(SelfCharId); }
		return;
	case 30078:
	{
		// ★★★ РАЗБЛОКИРОВАНО 2026-08-20 (`Reports/npc_visual_fix.md` §4.3). Раньше здесь стоял
		//   отказ: «второе поле пакета 6749 — неразобранный глобал `word_2BFF13C`».
		//   Глобал разобран арифметикой адресов: `g_pCampWarClan` = 0x2BFE5C0
		//   (`RE/ida/bng_002.tsv:7`), а 0x2BFE5C0 + 0x0B7C = 0x2BFF13C ⇒ `word_2BFF13C` — это то же
		//   поле, что стоит вторым в пакете осады 6723: **id фракции/лагеря**, который заполняет
		//   приём 6713 `Recv_ClanMyMembership@0xCA4AB0`. Подтверждение с другой стороны: у обоих
		//   пакетов одинаковая пара «i32 charId + u16 фракция», найденная независимо.
		//   Эталон `netSendReqSelfId_op6749@0xFEA180` (network.c:50233):
		//     msg[0] = *(i32*)(g_LocalPlayer + 1052);   LOWORD(msg[1]) = word_2BFF13C;
		const UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
		if (Net && WS) { Net->SendCampWarSelfId6749(SelfCharId, WS->GetSelfCampId()); }
		return;
	}

	// ── прыжок к цели (6146): две фиксированные шестёрки констант из таблицы диспетчера ──
	case 30071:
	case 30072:
	{
		// ★ 27.09.2026 [ОРИГИНАЛ]: второе поле — СВОЙ userNum: `Target_GetSelectedUserIdx(CharManager_GetLocalPlayer())`
		//   (game_main.c:10281-10288) — миснейм, геттер возвращает номер ПЕРЕДАННОГО персонажа. Прежде уходил номер выбранной цели.
		//   [исправление дефекта оригинала] эталон здесь НЕ проверяет `LocalPlayer == NULL` и читал бы по нулевому указателю;
		//   порт без своего персонажа молчит, как соседние коды (`LABEL_133`).
		if (!Net || !Net->IsSelfUserNumKnown()) { return; }
		static const int32 kJumpA[6] = { 259, 234, 285, 277, 272, 256 };   // 30071
		static const int32 kJumpB[6] = { 225, 234, 252, 277, 238, 256 };   // 30072
		Net->SendClientTargetJump6146(0, SelfUserNum, (Code == 30071) ? kJumpA : kJumpB);
		return;
	}

	// ── «Доклад об охоте» (окно 56450), волна 2-Б (06.10.2026) ──
	//   ★ ПОПРАВКА К РЕЕСТРУ: символ эталона называется `uiSowelWindow_Show`, но открывает он окно
	//   `Hunt report_Win` (JUI IDNum 56450, контролы 56461…56493 «Hunt report_Mark_*»), а НЕ окно
	//   самоцветов. Подсистема самоцветов порта (`UAsdaSowelSubsystem`) к этому коду отношения не имеет.
	//   Эталон game_main.c:10276-10279: `uiSowelWindow_Show(g_pSowelWindow); UI_resetAllWindowsAndLeave_5045(...)` — окно
	//   показано, разговор закрыт (окно 56450 в «окна торговца» `Close()` НЕ входит и остаётся открытым).
	//   Окно, сдача знаков 6150 и приём 6151 — `UAsdaHuntReportSubsystem` (отчёт `owner_1006/wave2B_hunt_donation.md` §2).
	case 30068:
	{
		if (UAsdaHuntReportSubsystem* Hunt = GI ? GI->GetSubsystem<UAsdaHuntReportSubsystem>() : nullptr)
		{
			Hunt->OpenWindow();
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] команда 30068: окно «Доклад об охоте» (56450)"));
		}
		else { Refuse(990438); }   // окно 56450 «Доклад об охоте»; номер окна в текст loc 990438 не выводится (ревизия мультиязычности 09.10.2026, МЕЛК-1)
		Close();   // `UI_resetAllWindowsAndLeave_5045`
		return;
	}

	// ── закрыть диалог ──
	case kCmdCloseDialog: Close(); return;

	default:
		// ★ ИСПРАВЛЕНИЕ D6: коды без `case` в оригинале (30005…30008, 30013, 30014, 30058, 30059,
		//   30062, 30065…30067, 30086, 30087) не делают РОВНО НИЧЕГО — кнопка нажимается «в пустоту».
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] FIX-ORIG D6: код %d (параметр %d) от NPC %d «%s» — у ОРИГИНАЛА для него ")
			TEXT("нет ветки вовсе, кнопка не делала ничего"), Code, Param, CurrentNpcId, *CurrentNsr);
		if (Msg)
		{
			Msg->PostText(EAsdaMsgCategory::Quest,
				UAsdaTextSubsystem::FormatPrintf(FAsdaText::Loc(990439), { FString::FromInt(Code) }));   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3)
		}
		return;
	}
}

// ★ 29.10.2026 [ИСПРАВЛЕНИЕ] шаг «Принять задание» с невыполненным условием не уводит на страницу «Спасибо…» (разбор — в
//   `RunStepAction`, ветка «условие НЕ выполнено»). 0 = как оригинал.
static ASDA_DEV_CVAR(int32, CVarQuestAcceptFailStay,
	TEXT("asda.Quest.AcceptFailStay"), 1,
	TEXT("1 = шаг выдачи квеста с невыполненным условием остаётся на своей странице и пишет «условия не выполнены»; ")
	TEXT("0 = как оригинал (уводит на страницу после взятия, квест не выдаётся)."),
	ECVF_Default);

// ★ ЕДИНСТВЕННЫЙ вход «шаг диалога → действие» — третья ветка `uiCraftSlotClickHandler@0xD83D90`
//   (ui_core.c:59136-59185), порт целиком.
void UAsdaNpcDialogSubsystem::RunStepAction(int32 StepIndex)
{
	if (StepIndex < 0) { return; }
	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, CurrentTalkId);
	if (!Page || !Page->Steps.IsValidIndex(StepIndex)) { return; }
	const FAsdaNsrStep& Step = Page->Steps[StepIndex];

	const int32 Progress = GetEntryProgressState(Step);

	for (int32 m = 0; m < Step.Subs.Num(); ++m)
	{
		const FAsdaNsrStepSub& S = Step.Subs[m];

		// `if (state == 1) goto LABEL_61;` — сразу переход на страницу `+228`.
		if (S.State == 1) { RenderPage(S.Res2); return; }

		FString Why;
		if (EvalCond(S.Cond, /*bStepOwner*/true, &Why))
		{
			// Гейт эталона `gap_1D8[160] / [164]` — «заявка уже в полёте»: тогда он ЧИСТИТ маску
			//   категорий и выходит молча. У нас те же два флага, но с явным нотисом 6523/6524
			//   (их показывает сам отправитель 5043 — см. `sendStartQuestBookProcess`).
			ExpireStaleInFlight();   // ★ 19.10.2026: молчание сервера не должно запирать квесты до перезахода
			if (bAcceptInFlight || bCompleteInFlight)
			{
				UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] шаг %d: заявка 5043 уже в полёте — нажатие проигнорировано"), StepIndex);
				if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
				{
					Msg->PostLoc(EAsdaMsgCategory::Quest, bAcceptInFlight ? kLocAcceptBusy : kLocCompleteBusy);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
				}
				return;
			}

			if (Progress == 1) { SendQuestAccept(StepIndex, m, S); return; }

			if (Progress != 2)
			{
				if (Progress == 4)
				{
					SavedTalkId = CurrentTalkId; SavedStep = StepIndex; SavedSub = m;
					// ★ 1:1 с эталоном: `netSendCraftSlotRequest(this, страница, gap_C[4] /*НОМЕР
					//   ШАГА*/, m, 2, …)` — третьим аргументом (индекс блока цели / выбранной
					//   награды) уходит НОМЕР ШАГА, а не индекс активной цели. Это квирк оригинала;
					//   в данных этой сборки он безвреден: все 11 под-записей с
					//   `EntryProgressState == 4` стоят на шаге 0 и активна у них цель 0
					//   (зонд `Tools/_probe_quest_turnin.py`, 11 из 11). Расхождение — в лог.
					SendQuestComplete(StepIndex, m, StepIndex);
				}
				// `if (u16 +224) → RenderPage(u16 +80)`, иначе `RenderPage(u16 +228)`.
				if (S.Res0 != 0) { RenderPage(S.BranchTalkId); return; }
				RenderPage(S.Res2);
				return;
			}

			// Progress == 2 — попап наград (`uiQuestRewardPanelPopulate@0xD93F90`). Э6: СДЕЛАН.
			if (GridScanAnyActiveCell(Step))
			{
				SavedTalkId = CurrentTalkId; SavedStep = StepIndex; SavedSub = m;
				bRewardPopupMode = true;                 // `this[437] = 1` — окно принимает только 567
				if (!ShowRewardPopup(StepIndex, m))
				{
					// Эталон в этой ветке (`questBuildRewardSlots` вернул > 0) делает
					//   `npcDialogPanelReset` и попапа НЕ показывает; запирать окно тоже не на чем.
					bRewardPopupMode = false;
					ResetPanel();
				}
			}
			return;
		}

		// Условие НЕ выполнено.
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] шаг %d под-запись %d: условие НЕ выполнено\n%s"),
			StepIndex, m, *Why);
		// ★★★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 29.10.2026 (жалоба «квест не берётся: диалог обрывается кнопкой
		//   „Завершить разговор“»). У ВСЕХ 403 шагов «Принять задание» в данных ровно одна под-запись с `+226 = 0`, поэтому при
		//   невыполненном условии эталон (`uiCraftSlotClickHandler`, та же ветка, что ниже) уходит на страницу `+228` — а это
		//   страница ПОСЛЕ взятия («О, спасибо… Я на тебя рассчитываю» + «Завершить разговор»). 5043 не уходит, квеста нет, а
		//   игрок прочёл, что его взял. Так обрываются, например, квесты, у которых вводную страницу сценарий открывает при
		//   более мягком условии, чем у самого шага (уровень 4 при шаге с 5, класс-новичок при «классе 99» и т. п.).
		//   Исправление: шаг ВЫДАЧИ при невыполненном условии остаётся на своей странице и говорит «условия не выполнены»
		//   (строка 990496, RU/EN). Прочие шаги — как в оригинале. Откат: `asda.Quest.AcceptFailStay 0`.
		if (Progress == 1 && CVarQuestAcceptFailStay.GetValueOnGameThread() != 0)
		{
			UE_LOG(LogAsdaNpcDlg, Log,
				TEXT("[NPC-диалог] шаг %d выдаёт эпизод %d, условие не выполнено — остаёмся на странице %d (оригинал увёл бы на %d)"),
				StepIndex, S.AcceptQuestId, CurrentTalkId, S.Res2);
			if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
			{
				Msg->PostText(EAsdaMsgCategory::Quest, FAsdaText::Loc(990496));   // «Условия для этого пункта не выполнены.»
			}
			return;
		}
		// ★ ДЕФЕКТ ОРИГИНАЛА D11 (найден в этом заходе): эталон пишет
		//     `if (*(unsigned __int16*)(… + 226) != -1) goto LABEL_61;`
		//   — беззнаковое СЛОВО расширяется до int и НИКОГДА не равно −1, то есть условие истинно
		//   ВСЕГДА, и цикл по пяти под-записям на деле обрывается на первой же неподошедшей.
		//   Автор явно имел в виду знаковое `(i16) != -1`. ИСПРАВЛЯЕМ: переход делаем только когда
		//   поле действительно не равно −1, иначе идём к следующей под-записи.
		if ((int16)S.Res1 != -1) { RenderPage(S.Res2); return; }
	}
}
