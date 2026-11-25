// Copyright (c) 2026 @sitxovski (Discord)
// AsdaNpcDialogSubsystem_Quest.cpp — ★ 23.10.2026 часть UAsdaNpcDialogSubsystem, вынесенная из AsdaNpcDialogSubsystem.cpp БАЙТ В БАЙТ
//   (разнос по смысловым группам; доказательство — Tools/asda_split_cpp.py verify, план Reports/split_2026-10-23/AsdaNpcDialogSubsystem.plan.json).
//   Принять/сдать шаг квеста (5043/5041), гейт перед сдачей, кнопка 567 «ОК», нотис «задание получено», строки наград и приём 5042.
//   #include — те же, что у AsdaNpcDialogSubsystem.cpp, в том же порядке; общее — AsdaNpcDialogSubsystemInternal.h.
#include "Npc/AsdaNpcDialogSubsystem.h"
#include "AsdaWeightWire.h"   // ★ 29.10.2026 (вес ×2): AsdaWeightFromWire — вес персонажа по проводу u16
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
// 30030 — элитный магазин 6000 (волна В5); хвост смерти   ★ R3: комментарий снятого включения "UI/AsdaPremiumShopSubsystem.h" (файл им не пользовался) — сохранён
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
// ОБЩАЯ точка подсказок предмета (второй копии билдеров нет)   ★ R3: комментарий снятого включения "UI/AsdaItemTooltipSubsystem.h" (файл им не пользовался) — сохранён
#include "Engine/Engine.h"               // GEngine->GameViewport: размер вьюпорта для проверки центра
#include "Engine/GameViewportClient.h"
#include "World/AsdaPlayerController.h"
#include "World/AsdaWorldSubsystem.h"
#include "World/AsdaPlayerCharacter.h"
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
#include "Npc/AsdaDonationSubsystem.h"         // ★ волна 2-Б: 30051 — пожертвование (нумпад режима 6 → 6132)
#include "TimerManager.h"
#include "EngineUtils.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "HAL/IConsoleManager.h"            // ★ 27.09 itemsfix: asda.EventExchange.ServerGuard
#include "UI/AsdaDisassemblySubsystem.h"    // ★ волна 2-А: окно разборки 32000 видно ⇒ разговор молча не открывается
#include "Text/AsdaLang.h"                  // [ПОРТ] 09.10.2026 (мультиязычность): заголовок/кнопки/реплики сценария на языке сессии
#include "Text/AsdaLangRows.h"              // [ПОРТ] 09.10.2026 (мультиязычность): перевод копии страницы сценария
#include "AsdaNpcDialogSubsystemInternal.h"   // ★ 23.10.2026 разнос: общее для частей AsdaNpcDialogSubsystem*.cpp (LogAsdaNpcDlg, чистые хелперы)
#include "Quest/AsdaBoardQuestSubsystem.h"   // ★ 28.10.2026: 567 в «режиме доски» → 6593

static constexpr int32 kLocQuestLogFull = 6193;
// ★ Гейт сдачи `questCheckAcceptPrereq@0xD8AE50` (добавлен 2026-08-22): нотисы 6193/6194/6195.
static constexpr int32 kLocBagFull          = 6194;   // мало места в сумке под предметные награды
static constexpr int32 kLocQuestAlreadyHave = 6195;   // квест-награда уже есть у игрока
// ★ `uiCraftSlotClickSendReq@0xD95190`, ветка «ячейка награды не выбрана».
static constexpr int32 kLocRewardNotPicked  = 6173;
// Сумка под предметные награды: контейнер 2 (`g_InventoryArray2`), 60 записей по 57 байт.
static constexpr int32 kBagContainer = 2;
static constexpr int32 kSpendCells     = 60;   // ★ U21 (В1 дедупа №2): было `kBagCells` (контейнер 2)

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ПРИНЯТЬ / СДАТЬ ШАГ КВЕСТА (порт `sendStartQuestBookProcess@0xD9D9D0`, опкод 5043)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// РЕЖИМ 1 «ПРИНЯТЬ». Эталон:
//   1) `questKey = questEpisode_FindWordKeyById(g_pQuestMgr, i32 под-запись+1028)` — код эпизода
//      переводится в ключ квеста через `Episode_Table` (+200 → +236);
//   2) в записи `SNQInfo.Bin` ЭТОГО NPC ищется слот таблицы «ВЫДАЁТ» с `questId == questKey`,
//      у которого хоть один из пяти ключей > 0 — его НОМЕР (0…9) и уходит в пакет;
//   3) гейт `questSlots_HasFreeSlot` (журнал на 12 квестов) — иначе нотис 6193 и пакет не идёт;
//   4) тело 4 б: `u16 npcId`, `u16 номер слота`.
void UAsdaNpcDialogSubsystem::SendQuestAccept(int32 StepIndex, int32 SubIndex, const FAsdaNsrStepSub& S)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!Net || !QM) { return; }

	const int32 QuestKey = QM->EpisodeIdToQuestKey(S.AcceptQuestId);
	if (QuestKey < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] принять квест: эпизода %d нет в DT_EpisodeTable — пакет не отправлен"), S.AcceptQuestId);
		return;
	}
	const FAsdaNpcSnqRow* Snq = AsdaDt::RowByInt<FAsdaNpcSnqRow>(kDtNpcSnq, CurrentNpcId, TEXT("NpcDialog"));
	int32 Slot = -1;
	if (Snq)
	{
		for (int32 i = 0; i < Snq->Give.Num(); ++i)
		{
			if (Snq->Give[i].QuestId != QuestKey) { continue; }
			bool bAnyKey = false;
			for (int32 k : Snq->Give[i].Keys) { if (k > 0) { bAnyKey = true; break; } }
			if (bAnyKey) { Slot = i; }
		}
	}
	if (Slot < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] принять квест %d (эпизод %d): у NPC %d в SNQInfo нет слота выдачи — пакет не отправлен"),
			QuestKey, S.AcceptQuestId, CurrentNpcId);
		return;
	}
	if (!QM->HasFreeQuestSlot())
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] принять квест: журнал заполнен (12/12) — нотис %d"), kLocQuestLogFull);
		if (Msg) { Msg->PostLoc(EAsdaMsgCategory::Quest, kLocQuestLogFull); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		return;
	}
	bAcceptInFlight = true;                     // `this[159] = 1`
	AcceptSentAtS = FPlatformTime::Seconds();   // ★ 19.10.2026: для сторожа «сервер промолчал»
	SavedTalkId = CurrentTalkId; SavedStep = StepIndex; SavedSub = SubIndex;
	Net->SendQuestStepAccept5043(CurrentNpcId, Slot);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ГЕЙТ ПЕРЕД СДАЧЕЙ — порт `questCheckAcceptPrereq@0xD8AE50` (quest.c:6793)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ДОБАВЛЕНО 2026-08-22. В порту этой проверки не было ВОВСЕ, хотя эталон гоняет её РОВНО ПЕРЕД
//   сборкой пакета сдачи (`sendStartQuestBookProcess`, ветка `requestMode == 2`):
//       qmemcpy(questPrereqParams, блок цели[tierIndex], 140);
//       if (questCheckAcceptPrereq(pChar, params[0..34])) { … собрать и отправить 5041 … }
//   Аргументы `a2…a36` — это 35 подряд идущих int'ов блока цели, то есть `aN = блок + 4*(N−2)`.
//   Внутри читаются:
//       a10  = блок+32  — квест (КЛЮЧ, `Episode_Table +236`), который ВЫДАЁТСЯ вместе со сдачей;
//       a26  = блок+96  — второй такой же;
//         для каждого: уже есть (`CQuest_HasActiveQuestOrReq(…, 0, …)`) → нотис 6195 и ОТКАЗ;
//                      журнал полон (`questSlots_HasFreeSlot`)          → нотис 6193 и ОТКАЗ;
//       число ПРЕДМЕТНЫХ наград = сколько положительных среди
//         блок+68, +76, +84 (гарантированные) и +112, +120, +128 (выбираемые)
//         — `questCountPositiveRewardFields@0xD8B1B0`, аргументы a18/a20/a22/a29/a31/a33, а его
//           собственные `aK = rewardFields[K−1] = блок + 4*(K−1)`;
//       свободных ячеек сумки (60 записей контейнера 2, `g_InventoryArray2`, занята при `+0 > 0`)
//         меньше, чем наград → нотис 6194 и ОТКАЗ.
bool UAsdaNpcDialogSubsystem::CheckTurnInPrereq(const FAsdaNsrStepObj& Obj) const
{
	UGameInstance* GI = GetGameInstance();
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	if (!QM) { return false; }

	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки → «[#id]» (общая точка UAsdaMessageSubsystem).
	auto Deny = [&](int32 Loc, const TCHAR* Why)
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] сдача не пущена гейтом: %s → нотис %d"), Why, Loc);
		if (Msg) { Msg->PostLoc(EAsdaMsgCategory::Quest, Loc); }
	};

	// Два поля «квест в награду»: +32 и +96 блока цели.
	const int32 GrantKeys[2] = { Obj.A32, Obj.S96 };
	for (int32 k = 0; k < 2; ++k)
	{
		if (GrantKeys[k] <= 0) { continue; }
		if (QM->HasActiveQuestOrReqByKey(GrantKeys[k]))
		{
			Deny(kLocQuestAlreadyHave,
				*FString::Printf(TEXT("квест-награда %d уже есть"), GrantKeys[k]));
			return false;
		}
		if (!QM->HasFreeQuestSlot())
		{
			Deny(kLocQuestLogFull, TEXT("журнал заполнен"));
			return false;
		}
	}

	// Сколько предметов даст награда (шесть полей блока цели).
	const int32 RewardItemFields[6] = { Obj.A68, Obj.A76, Obj.A84, Obj.S112, Obj.S120, Obj.S128 };
	int32 NeedCells = 0;
	for (int32 v : RewardItemFields) { if (v > 0) { ++NeedCells; } }
	if (NeedCells <= 0) { return true; }        // эталон: `if (rewardItemCount <= 0) return 1;`

	int32 Free = 0;
	if (const UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		for (int32 i = 0; i < kSpendCells; ++i) { if (Inv->GetCellItemCode(kBagContainer, i) <= 0) { ++Free; } }
	}
	else
	{
		// Инвентаря нет (офлайн-прогон) — эталон в этом случае насчитал бы 60 свободных.
		Free = kSpendCells;
	}
	if (Free < NeedCells)
	{
		Deny(kLocBagFull,
			*FString::Printf(TEXT("нужно ячеек %d, свободно %d"), NeedCells, Free));
		return false;
	}
	return true;
}

// РЕЖИМ 2 «СДАТЬ». Эталон берёт `questId` из БЛОКА ЦЕЛИ (`под-запись + 188 + 140*ObjIdx + 4`), ищет
//   по нему слот таблицы «ПРИНИМАЕТ» того же NPC и шлёт 7 байт: `u16 npcId`, `u16 номер слота`,
//   `u8 индекс цели/награды`, `u16 номер слота ЖУРНАЛА` (`questSlots_FindSlotById`).
// ★★★ ИЗМЕНЕНО 2026-08-22: `ObjIdx` теперь ПАРАМЕТР, а не «первая активная цель». Так у эталона:
//   в `sendStartQuestBookProcess` режима 2 индекс блока цели приходит третьим аргументом, и у двух
//   вызывающих он РАЗНЫЙ — у прямой ветки это номер шага (`uiCraftSlotClickHandler`), у попапа
//   наград это ВЫБРАННАЯ ЯЧЕЙКА (`uiCraftSlotClickSendReq`). Прежний «первый активный» совпадал с
//   ними лишь случайно и терял выбор игрока.
// ★★★ Страница/шаг/под-запись эталон берёт не из аргументов, а из СОХРАНЁННОЙ тройки
//   `this[24101…24103]` — у нас это `SavedTalkId/SavedStep/SavedSub`, которые кладёт и прямая
//   ветка, и показ попапа. Поэтому здесь под-запись берётся по `SavedTalkId`, а не по текущей
//   странице: сервер мог уже увести диалог на другую.
void UAsdaNpcDialogSubsystem::SendQuestComplete(int32 StepIndex, int32 SubIndex, int32 ObjIdx)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	if (!Net || !QM) { return; }

	const FAsdaNsrPageRow* Page = FindPage(CurrentNsr, SavedTalkId > 0 ? SavedTalkId : CurrentTalkId);
	if (!Page || !Page->Steps.IsValidIndex(StepIndex))
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] сдать квест: страницы %d / шага %d нет — пакет не отправлен"), SavedTalkId, StepIndex);
		return;
	}
	const FAsdaNsrStep& Step = Page->Steps[StepIndex];
	if (!Step.Subs.IsValidIndex(SubIndex)) { return; }
	const FAsdaNsrStepSub& S = Step.Subs[SubIndex];
	if (!S.Objs.IsValidIndex(ObjIdx))
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] сдать квест: блока цели %d у шага %d под-записи %d нет (блоков %d) — пакет не отправлен"),
			ObjIdx, StepIndex, SubIndex, S.Objs.Num());
		return;
	}
	// Диагностика квирка оригинала (см. вызов из `RunStepAction`): расходится ли переданный индекс
	//   с первой активной целью.
	int32 FirstActive = -1;
	for (int32 j = 0; j < S.ObjActive.Num(); ++j) { if (S.ObjActive[j] > 0) { FirstActive = j; break; } }
	if (FirstActive >= 0 && FirstActive != ObjIdx)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] сдать квест: индекс цели %d ≠ первая активная %d (так считает и оригинал — см. отчёт)"),
			ObjIdx, FirstActive);
	}

	const FAsdaNsrStepObj& Obj = S.Objs[ObjIdx];
	const int32 QuestId = Obj.QuestId;

	// `removeBuffSlot = buffSlots_FindSlotById(g_pQuestMgr, removeBuffId)` — эталон при −1 пакет
	//   НЕ шлёт вовсе (весь блок сборки стоит внутри `if (removeBuffSlot > -1)`).
	const int32 SlotNo = QM->FindQuestSlotNo(QuestId);
	if (SlotNo < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] сдать квест %d: в журнале нет такого квеста — эталон здесь пакет не шлёт"), QuestId);
		return;
	}
	if (!CheckTurnInPrereq(Obj)) { return; }

	const FAsdaNpcSnqRow* Snq = AsdaDt::RowByInt<FAsdaNpcSnqRow>(kDtNpcSnq, CurrentNpcId, TEXT("NpcDialog"));
	int32 Slot = -1;
	if (Snq)
	{
		for (int32 i = 0; i < Snq->Take.Num(); ++i)
		{
			if (Snq->Take[i].QuestId != QuestId) { continue; }
			bool bAnyKey = false;
			for (int32 k : Snq->Take[i].Keys) { if (k > 0) { bAnyKey = true; break; } }
			if (bAnyKey) { Slot = i; }
		}
	}
	if (Slot < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] сдать квест %d: у NPC %d в SNQInfo нет слота приёма — пакет не отправлен"),
			QuestId, CurrentNpcId);
		return;
	}
	bCompleteInFlight = true;                   // `this[158] = 1`
	CompleteSentAtS = FPlatformTime::Seconds(); // ★ 19.10.2026: для сторожа «сервер промолчал»
	Net->SendQuestTurnIn5041(CurrentNpcId, Slot, ObjIdx, SlotNo);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  КНОПКА 567 «ОК» ПОД ПОПАПОМ НАГРАД — порт `uiCraftSlotClickSendReq@0xD95190` (ui_core.c:60378)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Тело эталона в интересующей нас ветке (окно крафта `g_pQuestCraftWnd` в диалоге NPC не активно,
//   поэтому `sub_936EB1(g_pQuestCraftWnd)` там всегда −1):
//     if (выбранная ячейка < 0)  → нотис 6173 в чат + тултип чата (ничего не выбрано);
//     else                       → netSendCraftSlotRequest(this, страница, ВЫБРАННАЯ ЯЧЕЙКА,
//                                                          …, режим 2 «сдать», запись SNQInfo).
// ★ 28.10.2026: ветки «окна крафта» (6593 / `uiEquipPanelReset`) — это ДОСКА ОБЪЯВЛЕНИЙ 75000 (миснейм IDA `g_pQuestCraftWnd`);
//   они перенесены (блок в начале тела). Вызывающий — кнопка 567 попапа 540 (и разговора NPC, и «режима доски»).
void UAsdaNpcDialogSubsystem::ConfirmRewardPopup()
{
	UE_LOG(LogAsdaNpcDlg, Log,
		TEXT("[NPC-диалог] 567 «ОК» под наградой: шаг %d под-запись %d, выбранная ячейка %d"),
		SavedStep, SavedSub, RewardSelIndex);
	// ═══ ★ 28.10.2026 ВЕТКИ ДОСКИ ОБЪЯВЛЕНИЙ (`uiCraftSlotClickSendReq@0xD95190`, ui_core.c:60378-60415) ═══
	//   `sub_936EB1(g_pQuestCraftWnd)` = задание доски, ждущее сдачи (`this[10]`, ставит кнопка 75281):
	//     выбор < 0: задание > −1 и у записи `+332 < 0` (наград на выбор нет) → 6593(задание, 99) + `uiEquipPanelReset`;
	//                иначе — нотис 6173 (ниже, общий с NPC);
	//     выбор ≥ 0: задание ≤ −1 → обычная ветка NPC (ниже); иначе → 6593(задание, байт выбора) + `uiEquipPanelReset`.
	{
		UGameInstance* GI = GetGameInstance();
		UAsdaBoardQuestSubsystem* Board = GI ? GI->GetSubsystem<UAsdaBoardQuestSubsystem>() : nullptr;
		const int32 BoardQ = Board ? Board->GetPendingTurnIn() : -1;
		if (BoardQ > -1)
		{
			const FAsdaRaidQuestRow* R = Board->FindQuest(BoardQ);
			const bool bNoChoice = R && R->ChoiceItemIds.IsValidIndex(0) && R->ChoiceItemIds[0] < 0;
			// ★ Ревизия М-1: `SendTurnIn6593` == false — запрос задержан замком «в полёте»: попап и ждущее сдачи остаются.
			if (RewardSelIndex < 0 && R && bNoChoice)
			{
				if (Board->SendTurnIn6593(BoardQ, UAsdaBoardQuestSubsystem::kChoiceNone)) { HideRewardPopup(); }   // `uiEquipPanelReset`
				return;
			}
			if (RewardSelIndex >= 0 && R)
			{
				// Байт выбора — индекс награды записи (строка панели выбора стоит на том же месте, `CQuestWin_AddRewardItem`).
				if (Board->SendTurnIn6593(BoardQ, RewardSelIndex)) { HideRewardPopup(); }
				return;
			}
			// выбор < 0 при наградах на выбор (или записи нет) — нотис 6173 ниже, как у эталона.
		}
	}
	if (RewardSelIndex < 0)
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог/награда] награда не выбрана — нотис %d"), kLocRewardNotPicked);
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLoc(EAsdaMsgCategory::Quest, kLocRewardNotPicked);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		}
		return;
	}
	// Гейт «заявка уже в полёте» — тот же, что у эталона в самом отправителе (`pChar[158] == 1`
	//   → нотис 6524 и выход): второе нажатие 567 до прихода 5042 не должно слать второй пакет.
	if (bCompleteInFlight)
	{
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 567: заявка 5041 уже в полёте — нажатие проигнорировано"));
		if (UAsdaMessageSubsystem* Msg = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaMessageSubsystem>() : nullptr)
		{
			Msg->PostLoc(EAsdaMsgCategory::Quest, kLocCompleteBusy);   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		}
		return;
	}
	SendQuestComplete(SavedStep, SavedSub, RewardSelIndex);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  НОТИС «ЗАДАНИЕ ПОЛУЧЕНО» — ТОЛЬКО ветка 5042 «сдача выдала следующий квест»
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ ОБЛАСТЬ ПРИМЕНЕНИЯ СУЖЕНА 2026-08-23 (замечание владельца, `Reports/quest_accept_window.md`).
// Эталон показывает строку 20197 «Получено задание [ss1].» в ДВУХ местах, и они РАЗНЫЕ по способу:
//   · `CQuest_RecvPacket@0xD9D260` (приём 5044, `resultCode == 0`) — строка уходит В ОКНО
//     `QUEST_Confirm_Win` 750 (`CQuest_PopulateRewardWindow(g_pQuestMgr, нотис 20197)`).
//     ⇒ Эту ветку теперь обслуживает `ShowQuestAcceptWindow`, а НЕ нотис.
//   · `chatBuildTransactionNotice@0xD9FC70` case 1 (`chat.c:6125`; её зовёт приём 5042, когда сдача
//     ВЫДАЛА следующий квест — записи тела `+59` / `+103`) — а вот здесь у оригинала именно
//     ЭКРАННЫЙ НОТИС: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24, строка, 1)`, окна 750 нет.
//     ⇒ Эта ветка и осталась за этой функцией.
// В обоих случаях название кладётся в слот 0 (`CObject_SetSlotString(…, 0, …)`), то есть в `[ss1]`.
//
// Канал показа — `WARMESSAGE_WIN` 221 (скин `Action_List`, ни фона, ни рамки; ЭТАЛОННЫЙ канал
//   «чистой» надписи по центру экрана, `CChat::AddWarMessage` → `uiChatLogAddLine(warMessageWin,
//   …, центр, слот 255)`). Цвет золотой + чёрная обводка тегом `<FO>` — осознанное отступление,
//   согласованное с владельцем 2026-08-22 (`Reports/quest_notice_sound_fix.md` §1); у
//   оригинала обводки нет. Отменяется без пересборки: бит канала в `DT_MessageRoutes`.
void UAsdaNpcDialogSubsystem::PostQuestReceivedNotice(int32 EpisodeId, int32 QuestKeyFallback, int32 SrcOpcode)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;
	UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	if (!Msg) { return; }

	FString EpTitle = QM ? QM->EpisodeTitleByEpisodeId(EpisodeId) : FString();
	if (EpTitle.IsEmpty() && QM && QuestKeyFallback >= 0)
	{
		EpTitle = QM->EpisodeTitleByQuestKey(QuestKeyFallback);
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] нотис %d: названия эпизода %d в DT_EpisodeTable нет; по ключу квеста %d — «%s»"),
			kLocRewardHeader, EpisodeId, QuestKeyFallback, *EpTitle);
	}
	FAsdaMsgParams P;
	P.Category   = EAsdaMsgCategory::Quest;
	P.LocId      = kLocRewardHeader;                  // 20197 «Получено задание [ss1].»
	P.StringArgs = { EpTitle };                       // слот 0 = `[ss1]` (эталонный CObject_SetSlotString)
	// [ПОРТ] 09.10.2026 (мультиязычность, этап 3): RU-фолбэк убран — нет строки 20197 → «[#20197]» (общая точка UAsdaMessageSubsystem).
	P.bUseColor  = true;
	P.Color      = AsdaNoticeColor::Gold;    // золотой — цвет категории `Quest`
	// ★★★ ПРАВКА 2026-08-22 (требование владельца, `Reports/quest_notice_sound_fix.md` §1).
	//   БЫЛО `HeroNotice` — окно 83400 `HERO_NOTICE_WIN`, скин `Notice_Tip`: у него ЕСТЬ
	//   подложка-текстура (`/Game/AsdaAssets/Textures/JUI/n_01` внутри `WBP_HERO_NOTICE_WIN`),
	//   поэтому нотис приезжал «в рамке», как оповещение.
	//   СТАЛО `WarWindow` — окно 221 `WARMESSAGE_WIN`, скин `Action_List`: единственный план
	//   объявлен `PlaneAlwaysInvisible=1`, ФОНА И РАМКИ НЕТ (в собранном `WBP_WARMESSAGE_WIN`
	//   нет ни одного Image/Texture2D). Это ЭТАЛОННЫЙ канал «чистой» надписи на экране
	//   (`CControl_SetTooltipText@0xBA9A30` → `uiChatLogAddLine(warMessageWin, …, центр, слот 255)`):
	//   центр по X, 28 % высоты по Y — выше центра экрана, как и просил владелец.
	//   ★ Каналы 83400 (редкая добыча) и 228 `NOTICE_WIN` (война и объявления администрации)
	//   этой правкой НЕ ЗАТРОНУТЫ: у них свои биты в `DT_MessageRoutes`, здесь меняется только
	//   перекрытие ЭТОГО сообщения.
	P.Targets    = (int32)EAsdaMsgTarget::WarWindow | (int32)EAsdaMsgTarget::SystemChat;
	// Золотой текст с ЧЁРНОЙ ОБВОДКОЙ. Обводка — эталонный тег `<FO>` общей разметки
	//   (`UAsdaRichText`), своей обводки не рисуем. У канала 221 обводки по умолчанию нет
	//   (эталон даёт ему только тень 0x400000), поэтому стиль задаёт сообщение — см. `StyleTag`.
	P.StyleTag   = TEXT("<FO>");
	P.SrcOpcode  = SrcOpcode;
	Msg->Post(P);
}

// ★ 19.10.2026 [ПОРТ] Сторож «заявка в полёте»: ответ на 5043/5041 приходит за десятки мс; если его нет дольше
//   этого срока — сервер промолчал (неверный слот выдачи, JOB4), и флаг снимается, чтобы следующий клик прошёл.
//   0 = как оригинал (флаг висит до ответа/перезахода).
static ASDA_DEV_CVAR(float, CVarQuestInFlightTimeoutSec,
	TEXT("asda.Quest.InFlightTimeoutSec"), 4.f,
	TEXT("Через сколько секунд без ответа сервера снимать «заявку на квест в полёте» (по умолчанию 4; 0 = как оригинал, висит до перезахода)."),
	ECVF_Default);

void UAsdaNpcDialogSubsystem::ExpireStaleInFlight()
{
	const float Timeout = CVarQuestInFlightTimeoutSec.GetValueOnGameThread();
	if (Timeout <= 0.f) { return; }
	const double Now = FPlatformTime::Seconds();
	if (bAcceptInFlight && Now - AcceptSentAtS > Timeout)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] заявка 5043 (принять, NPC %d, страница %d, шаг %d/%d) без ответа сервера %.1f с — флаг снят (сервер промолчал)"),
			CurrentNpcId, SavedTalkId, SavedStep, SavedSub, Now - AcceptSentAtS);
		bAcceptInFlight = false;
	}
	if (bCompleteInFlight && Now - CompleteSentAtS > Timeout)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] заявка 5041 (сдать, NPC %d, страница %d) без ответа сервера %.1f с — флаг снят (сервер промолчал)"),
			CurrentNpcId, SavedTalkId, Now - CompleteSentAtS);
		bCompleteInFlight = false;
	}
}

// Приём 5044 — порт `CQuest_RecvPacket@0xD9D260` (quest.c:7634). Тело 47 байт:
//   `+0 u8 resultCode`, `+1 i16 talkId`, `+3 i32 episodeId`, `+11 i16 questId`, `+13 i16 subId`.
void UAsdaNpcDialogSubsystem::HandleQuestStepResult(int32 Result, int32 TalkId, const TArray<uint8>& Body)
{
	bAcceptInFlight = false;                    // обе заявки эталон гасит приёмом ответа
	bCompleteInFlight = false;

	UGameInstance* GI = GetGameInstance();
	UAsdaMessageSubsystem* Msg = GI ? GI->GetSubsystem<UAsdaMessageSubsystem>() : nullptr;

	if (Result == 0)
	{
		// Звук эталона: `csoundPlayName2DStore(g_pSoundMgr, 3, 0)`.
		// `csoundPlayName2DStore(g_pSoundMgr, 3, 0)` — звук интерфейса №3 (общая точка порта).
		if (UAsdaSoundSubsystem* Snd = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoundSubsystem>() : nullptr)
		{
			Snd->PlaySound2DById(3);
		}
		// Своей сборки байт нет (дедуп Д4): порядок и знаковость знает `AsdaBytes`.
		const int32 EpisodeId = (Body.Num() >= 7) ? AsdaBytes::RdI32(&Body[3]) : -1;
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог] 5044: шаг квеста принят/сдан, переход на страницу %d (эпизод %d)"), TalkId, EpisodeId);

		// ★★★ ИСПРАВЛЕНИЕ (2026-08-21, `Reports/quest_accept_fix.md`).
		//   Эталон здесь зовёт `cquestStoreQuestSlotData@0xC61F60` — кладёт пришедшую в теле
		//   ГОТОВУЮ запись слота журнала в `CQuest`, НЕ дожидаясь следующего 5047. Порт этого не
		//   делал: локально квест оставался «не взятым», шаг диалога продолжал предлагаться, и
		//   повторное нажатие получало от сервера отказ 6 («квест уже взят или уже пройден») —
		//   именно это и наблюдалось в живом логе 21.08 (NPC 47, квест 254, 18 отказов подряд).
		//   ЕДИНСТВЕННАЯ копия состояния квестов в порте — `UAsdaQuestMarkerSubsystem`, пишем туда.
		if (UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
		{
			QM->ApplyAcceptedQuestSlot(Body);
		}

		RenderPage(TalkId);

		// ════════════════════════════════════════════════════════════════════════════════════════
		//  ОКНО «ПОЛУЧЕНО ЗАДАНИЕ» 750 (порт `CQuest_RecvPacket`, ветка `resultCode == 0`)
		// ════════════════════════════════════════════════════════════════════════════════════════
		// Эталон делает РОВНО ЧЕТЫРЕ вещи (`quest.c:7712…7736`):
		//   questBuildRewardSet(g_pQuestMgr, тело+11 /*questNo*/, тело+13 /*stage*/);
		//   if (!ok) questBuildRewardSet(…, stage + 1);
		//   episodeName = CQuest_Episode_FindById(g_pQuestMgr, тело+3);  CObject_SetSlotString(…, 0, …);
		//   CQuest_PopulateRewardWindow(g_pQuestMgr, нотис 20197);        ← ПОКАЗ ОКНА 750
		//
		// ★★★ ИСПРАВЛЕНО 2026-08-23 (замечание владельца: «нужно не нотис, а полноценное модальное
		//   окно, как в оригинале»). Прежде порт вместо окна слал строку 20197 в экранный канал 221
		//   `WARMESSAGE_WIN` (отступление от 2026-08-22, `Reports/quest_notice_sound_fix.md` §1).
		//   Теперь ПРИЁМ показывает окно, как оригинал; нотис 20197 остался ТОЛЬКО там, где он есть у
		//   оригинала — в ветке 5042 «сдача выдала следующий квест» (`chatBuildTransactionNotice`
		//   case 1, `chat.c:6125`: там вызывается ЭКРАННЫЙ нотис, а не окно). Разбор —
		//   `Reports/quest_accept_window.md`.
		{
			// Ключ набора наград — пара (questNo, stage) из ЗАПИСИ СЛОТА в теле пакета.
			//   Раскладка тела подтверждена двумя независимыми источниками: сервером
			//   (`GameServer!sub_45CA70`, см. `ApplyAcceptedQuestSlot`) и эталоном клиента —
			//   `questBuildRewardSet(g_pQuestMgr, *(i16*)confirmRowsT2, *(i16*)&confirmRowsT2[2])`,
			//   где `confirmRowsT2` лежит на `confirmRowsT1 + 52` (это доказывает цикл `i = 4…7`
			//   в `CQuest_PopulateRewardWindow`), то есть тело `+11` и тело `+13`.
			const int32 QuestNo = (Body.Num() >= 13) ? AsdaBytes::RdI16(&Body[11]) : -1;   // дедуп Д4
			const int32 Stage   = (Body.Num() >= 15) ? AsdaBytes::RdI16(&Body[13]) : 0;    // дедуп Д4
			ShowQuestAcceptWindow(EpisodeId, QuestNo, Stage);
		}
		return;
	}

	// Коды отказа 1…13 → нотисы (таблица `CQuest_RecvPacket`, ветки switch).
	static const int32 kDenyNotice[14] = { 0, 6191, 6192, 6176, 6181, 6182, 6183, 6184, 6185, 6186,
	                                       6187, 6188, 6189, 6190 };
	if (Result >= 1 && Result <= 13)
	{
		const int32 Loc = kDenyNotice[Result];
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5044: отказ, код %d → нотис %d"), Result, Loc);
		if (Msg) { Msg->PostLoc(EAsdaMsgCategory::Quest, Loc); }   // [ПОРТ] 09.10.2026 (мультиязычность, этап 3): без RU-фолбэка
		return;
	}
	// `default:` эталона — выход без действий. Молчать нельзя (D6) — пишем в лог.
	UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 5044: код результата %d вне 0…13 — оригинал молчит"), Result);
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  СТРОКИ НАГРАД СДАЧИ 5042 — порт `chatBuildTransactionNotice@0xD9FC70` (chat.c:5926-6213) из
//  `net_ApplyPlayerFullUpdate@0xD828A0` (network.c:21731-22940). 19.10.2026.
// ════════════════════════════════════════════════════════════════════════════════════════════════
// Тело 5042 = кадр с +9 (`memcpy(savedThis + 60364, packet + 9, 0x1F5)` ⇒ body[i] = `savedThis + 60364 + i`); аргументы
//   `chatBuildTransactionNotice` — копия тела по значению (`a4` = body[8..11], `a16+3` = body[59], `a36+3` = body[147],
//   `a123+3` = body[495]). Сервер (`GameServer!recvRewardQuestProcess@0x45CF90` → `sub_45DEE0`) кладёт в тело ИТОГИ после
//   выдачи: body[491] = золото (`sub_4D7690`), body[495] = обаяние (`sub_43FD20`), body[489] = вес (`sub_43FA00`), body[147+57i] —
//   выданные записи предметов. Отдельных пакетов клиенту для этих наград сервер НЕ шлёт (прочее — 2xxx к агенту БД),
//   опыт приходит своим 4038 (у 5042 строки опыта НЕТ ни в одной ветке).
// ТАБЛИЦА «событие → loc → канал → цвет» (только ветка `resultCode == body[0] == 0`, порядок вызовов эталона):
//   вид 0 смена профессии (`jobChangeFlag`)            → 20200/6177 — у `UAsdaChangeJobBannerSubsystem::PostJobChangeNotice`;
//   вид 1 выдан новый квест (`body[59]|body[103] > 0`)  → 20197 — у `PostQuestReceivedNotice` (канал 221 — решение владельца);
//   вид 2 ЗОЛОТО (`body[491] − золото > 0`)            → 20198 «Получено [ss1] золота.», [ss1] = разность С РАЗРЯДАМИ
//         (`String_FormatThousands`); журнал 2572 (`CChat_AddTokenizedLines`, буфер A, 10 с) + плашка 221
//         (`CControl_SetTooltipText`), цвет −256 = 0xFFFFFF00; перед строкой — `uiSetHonorPointsDisplay(body[491])` (миснейм:
//         ставит ЗОЛОТО) и `invGetFreeSlotByMarker(body[491])` (ячейка 20551) = наш `SetGoldDisplay`;
//   вид 3 слот журнала (`i32 body[15] > 0 && body[5] != 2`) → 6019 «Обновлена информация о задании.»; журнал + плашка, −256;
//         звук 13 — перед строкой (уже у `HandleQuestTurnInResult`);
//   вид 4 ПРЕДМЕТ (каждая из 6 записей `i32 body[147+57i] > 0`) → 20199 «Получена награда: [ss1].», [ss1] = имя: таблица
//         предметов (`ItemData_FindByCode`), иначе самоцветов (`sowelTableFindRecordByCode`), иначе спец-предметов
//         (`itemGetTooltipById(code)->name`); ни в одной — строки нет. Журнал + плашка, −256. До строки — запись в сумку
//         (`recvItemUpdateRefreshWeight` → `OnRecv_ItemUpdate`, запись как есть = `ApplyExternalRecord(AsIs)`) и вес
//         `HUD_UpdateMiniWeightBar(i16 body[489])`;
//   вид 5 очки умений (`!body[10..12] && i16 body[13] > 0`) → 6024 — блок ниже в `HandleQuestTurnInResult` (18.10);
//   вид 6 ОБАЯНИЕ (`i32 body[495] > 0`)                → 6025 «Повышено обаяние.»; `sprintf("%d %s", разность, текст)` при
//         разности > 0, иначе только текст (разность БЕЗ разрядов); новое значение — в поле обаяния; журнал + плашка, −256.
//   Ветка `resultCode` 1…13 (отказ сервера) → окно ONEBUT 1500 ключ (0, 24) (`CItem_BuildTooltipText(g_pItemTooltipUI, 0, 24,
//         текст, 1)`), loc 6191, 6192, 6176, 6181…6190; прочие коды — молча (`default`).
//   Нигде больше в порте эти строки не выводятся (20198/20199/6025 — нет вовсе; 6019 у журнала — только 5052).
// ЗАВЕРШЕНИЕ квеста по 5052 (`questItemResult_apply` → `charApplyFullStatusPacket@0xD83670`, entity_char.c:46312-46780): тот же
//   блок наград, сдвинутый на +7 (сервер `recvPickupQuestItemToMonsterOrObject`: `memcpy(&v39[17], a7, 0x1EB)` против
//   `memcpy(v23 /*тело+10*/, a7, 491)` у 5042) — предметы @154+57i, вес @496, золото @498, обаяние @502. Эталон собирает из него
//   блок в раскладке 5042 (`memcpy(&statusBlock[10], тело + 17, 0x1EB)`) и зовёт те же виды 0/1/2/4/5/6 — вида 3 (6019) там НЕТ.
// Ключ отката — `asda.Quest.TurnInRewardLines` (0 — прежнее поведение: ни строк, ни применения золота/предметов/обаяния).
//   ⚠ Поле `UAsdaNetSubsystem::SelfCharm` из блока 1017 при 4048 пишется НЕЗАВИСИМО от ключа (это состояние, а не строка;
//   при ключе 0 его никто не читает).
static ASDA_DEV_CVAR(int32, CVarQuestTurnInRewardLines,
	TEXT("asda.Quest.TurnInRewardLines"), 1,
	TEXT("5042: 1 — строки наград сдачи квеста по эталону (золото 20198, слот 6019, предметы 20199, обаяние 6025, отказы 1…13 окном) ")
	TEXT("с применением золота/предметов/веса/обаяния; 0 — прежний путь (ничего из этого)."),
	ECVF_Default);

namespace AsdaQuestTurnInReward
{
	constexpr int32 kOffSlotEpisode = 15;    // i32 эпизод записи слота сдаваемого квеста (`+60379`)
	constexpr int32 kOffItems       = 147;   // первая из 6 записей предметов по 57 б (`+60511`)
	constexpr int32 kItemRecords    = 6;     // `for (slotIndex = 0; slotIndex < 6; ++slotIndex)`
	constexpr int32 kOffWeight      = 489;   // i16 вес после выдачи (`+60853`, `LOWORD(nextStepMapPair)`)
	constexpr int32 kOffGold        = 491;   // i32 золото после выдачи (`+60855`)
	constexpr int32 kOffCharm       = 495;   // i32 обаяние после выдачи (`+60859`)
	constexpr int32 kLocGold        = 20198; // «Получено [ss1] золота.»
	constexpr int32 kLocSlot        = 6019;  // «Обновлена информация о задании.»
	constexpr int32 kLocItem        = 20199; // «Получена награда: [ss1].»
	constexpr int32 kLocCharm       = 6025;  // «Повышено обаяние.»
	constexpr int32 kSrcOpcode      = 5042;  // источник для диагностики маршрутизатора

	// Ключ включён?
	static bool IsOn() { return CVarQuestTurnInRewardLines.GetValueOnGameThread() != 0; }

	// Цвет всех строк наград: −256 = 0xFFFFFF00 (чистый жёлтый).
	static FColor Yellow() { return FColor((uint32)0xFFFFFF00); }

	// Пара эталона `CChat_AddTokenizedLines(чат, текст, −256, 10000, 0)` + `CControl_SetTooltipText(чат, текст, −256, 10000)`:
	//   сначала журнал 2572 (буфер A), затем плашка 221. Категория `System` — у маршрута нет звука (у эталона его нет).
	static void PostYellowPair(const UGameInstance* GI, const FString& Text)
	{
		AsdaNotice::ChatColored(GI, EAsdaMsgCategory::System, Text, Yellow(), 10000.f);
		AsdaNotice::Plank(GI, EAsdaMsgCategory::System, Text, Yellow(), 10000.f, kSrcOpcode);
	}

	// Вид 2 — золото (`updateFlags[1]`, разность считается ДО записи нового значения, network.c:21810-21816 / :22171-22177).
	static void PostGold(UGameInstance* GI, const TArray<uint8>& Body)
	{
		UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		if (!Inv || Body.Num() < kOffGold + 4) { return; }
		const int32 NewGold = AsdaBytes::RdI32(&Body[kOffGold]);
		const int32 OldGold = Inv->GetGold();
		const int32 Delta = NewGold - OldGold;
		if (Delta <= 0) { return; }
		Inv->SetGoldDisplay(NewGold);   // `uiSetHonorPointsDisplay` + `invGetFreeSlotByMarker` — одна точка инвентаря
		PostYellowPair(GI, AsdaNotice::LocSlot(GI, kLocGold, AsdaUi::FormatThousands(Delta)));
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5042: золото %d → %d (+%d) → строка %d"), OldGold, NewGold, Delta, kLocGold);
	}

	// Вид 3 — слот журнала сдаваемого квеста обновлён (`updateFlags[2]`).
	static void PostSlotUpdated(UGameInstance* GI)
	{
		PostYellowPair(GI, AsdaNotice::LocRaw(GI, kLocSlot));
	}

	// Вид 4 — предметы награды (`updateFlags[3]`, network.c:22439-22606): запись в сумку, вес, строка с именем.
	static void PostItems(UGameInstance* GI, UAsdaNetSubsystem* Net, const TArray<uint8>& Body)
	{
		if (!GI || Body.Num() < kOffWeight + 2) { return; }
		UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>();
		const UAsdaItemTableSubsystem* IT = GI->GetSubsystem<UAsdaItemTableSubsystem>();
		const int32 Weight = AsdaWeightFromWire(AsdaBytes::RdI16(&Body[kOffWeight]));   // ★ 29.10.2026: вес u16
		for (int32 i = 0; i < kItemRecords; ++i)
		{
			const int32 Off = kOffItems + FAsdaItemInstance::kRecordSize * i;
			const int32 Code = AsdaBytes::RdI32(&Body[Off]);
			if (Code <= 0) { continue; }
			// `recvItemUpdateRefreshWeight`: 56 б записи + байт +56 = запись 57 б целиком → `OnRecv_ItemUpdate`, затем вес.
			//   ⚠ `ApplyExternalRecord` здесь зовётся из сетевого колбэка (делегат 5042/5052), а не из Tick: правка модели сумки
			//   соединение не трогает (закон no-reentrant-net-callbacks — о разрыве сети); оговорка «только из Tick» у объявления
			//   `ApplyExternalRecord` писалась для окна снасти — осознанное отступление, отложенной очереди здесь нет.
			TArray<uint8> Rec(&Body[Off], FAsdaItemInstance::kRecordSize);
			if (Inv) { Inv->ApplyExternalRecord(Rec, UAsdaInventorySubsystem::EExternalRecordMode::AsIs); }
			if (Net) { Net->SetSelfWeight(Weight, -1, TEXT("5042")); }
			// Имя: предмет → самоцвет → спец-предмет (порядок `chatBuildTransactionNotice` case 4).
			FString Name;
			bool bNamed = false;
			FAsdaSowellRow Sw;
			FAsdaSpecialItemlistRow Sp;
			if (IT && IT->GetItem(Code)) { Name = IT->GetItemName(Code); bNamed = true; }
			else if (IT && IT->GetSowell(Code, Sw)) { Name = Sw.Name; bNamed = true; }
			else if (IT && IT->GetSpecialItem(Code, Sp))
			{
				FAsdaSpecialitemTooltiplistRow Tip;   // `itemGetTooltipById(code)->name`
				if (IT->GetSpecialItemTooltip(Code, Tip)) { Name = Tip.Name; bNamed = true; }
				else
				{
					// [ПОРТ] исправление дефекта: эталон разыменовал бы nullptr подсказки — у нас строки нет, пишем в лог.
					UE_LOG(LogAsdaNpcDlg, Warning, TEXT("[NPC-диалог] 5042: спец-предмет %d без подсказки — строки 20199 нет"), Code);
				}
			}
			if (bNamed) { PostYellowPair(GI, AsdaNotice::LocSlot(GI, kLocItem, Name)); }
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5042: награда-предмет #%d код %d «%s», вес %d%s"),
				i, Code, *Name, Weight, bNamed ? TEXT(" → строка 20199") : TEXT(" — ни в одной таблице, строки нет (как эталон)"));
		}
	}

	// Вид 6 — обаяние (`updateFlags[5]`, chat.c:6198-6207): разность к текущему полю, затем новое значение в поле.
	static void PostCharm(UGameInstance* GI, UAsdaNetSubsystem* Net, const TArray<uint8>& Body)
	{
		if (!Net || Body.Num() < kOffCharm + 4) { return; }
		const int32 NewCharm = AsdaBytes::RdI32(&Body[kOffCharm]);
		if (NewCharm <= 0) { return; }
		const int32 Delta = NewCharm - Net->SelfCharm;
		Net->SelfCharm = NewCharm;            // `uiSetThousandsFormattedValue(g_pGameClient, body[495])`
		Net->bSelfCharmFromBlock = true;      // [ПОРТ] поздний 4048 не вернёт устаревшее значение блока
		const FString Text = AsdaNotice::LocRaw(GI, kLocCharm);
		PostYellowPair(GI, Delta > 0 ? FString::Printf(TEXT("%d %s"), Delta, *Text) : Text);
		UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5042: обаяние → %d (разность %d) → строка %d"), NewCharm, Delta, kLocCharm);
	}

	// Вид 5 — строка 6024 «Получены очки навыка.» (chat.c:6192-6196); поле очков пишет вызывающий.
	static void PostSkillPointsLine(UGameInstance* GI)
	{
		PostYellowPair(GI, AsdaNotice::LocRaw(GI, 6024));
	}

	// Блок 5052 → раскладка 5042 (`charApplyFullStatusPacket`: `memset(statusBlock, 0, 0x1F5)`,
	//   `memcpy(&statusBlock[5] /*байт 10*/, тело + 17, 0x1EB)`). Короткое тело — хвост нулями (как memset эталона).
	constexpr int32 kBlockLen    = 501;   // 0x1F5
	constexpr int32 kSrc5052     = 17;    // начало блока в теле 5052
	constexpr int32 kDst5042     = 10;    // то же место в раскладке 5042
	constexpr int32 kBlockCopy   = 491;   // 0x1EB
	static TArray<uint8> StatusBlockFrom5052(const TArray<uint8>& Body5052)
	{
		TArray<uint8> S; S.SetNumZeroed(kBlockLen);
		const int32 N = FMath::Clamp(Body5052.Num() - kSrc5052, 0, kBlockCopy);
		if (N > 0) { FMemory::Memcpy(S.GetData() + kDst5042, Body5052.GetData() + kSrc5052, N); }
		return S;
	}

	// Ветка `resultCode` 1…13 (network.c:22873-22937): окно ONEBUT 1500 с ключом очереди (0, 24). false — код вне 1…13.
	static bool PostRefusal(UGameInstance* GI, int32 StepCode)
	{
		static const int32 kLoc[14] = { 0, 6191, 6192, 6176, 6181, 6182, 6183, 6184, 6185, 6186, 6187, 6188, 6189, 6190 };
		if (StepCode < 1 || StepCode > 13) { return false; }
		AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::Quest, kLoc[StepCode], AsdaLoc::kBtnOk, /*EtalonNoticeId*/24);
		return true;
	}
}

// ★ 19.10.2026 — порт ветки завершения 5052: `missionSetEntryFlag@0xC638F0` (quest.c:3446-3453) + `charApplyFullStatusPacket@0xD83670`
//   (entity_char.c:46312-46780); объявление в .h. Эталон (порядок):
//     1) `missionSetEntryFlag(запись слота @22, i16 тело@0 = номер квеста)`: бит карты завершения + слот освобождается;
//     2) звук 14 (у журнала квестов, до вызова сюда);
//     3) вид 0 — профессия/класс/подкласс/очки (`a2+17..+20`) + нотис 20200/6177 + `npcUiRefreshActionList`;
//     4) вид 1 — записи новых квестов `a2+66` / `a2+110` (`CQuest_SetActiveSlot`) + нотис 20197 (звука 16 здесь НЕТ);
//     5) виды 2 золото → 4 предметы → 5 очки (строка) → 6 обаяние.
//   В порте: (3) — `UAsdaNetSubsystem::ApplyCompletionCharBlock5052` (та же `ApplyQuestTurnInCharBlock`, событие
//   `OnSelfJobChanged`); (1)+(4) — `UAsdaQuestMarkerSubsystem::ApplyTurnInResult(квест, стадия, завершён, блок)` на блоке в
//   раскладке 5042 (там @59/@103 = тело @66/@110 — одна функция с 5042, второй копии разбора нет). Профессия применяется
//   РАНЬШЕ журнала, а `ApplyTurnInResult` заканчивает пересчётом меток ⇒ метки видят и новый класс, и новый журнал.
//   `ApplyQuestProgressBlob` (ветка завершения) ни бита, ни слотов, ни профессии не трогает — задвоения нет.
void UAsdaNpcDialogSubsystem::ApplyQuestCompletionRewards5052(const TArray<uint8>& Body5052)
{
	using namespace AsdaQuestTurnInReward;
	if (!IsOn()) { return; }
	UGameInstance* GI = GetGameInstance();
	if (!GI || Body5052.Num() < 66) { return; }   // 22 + 44 — запись слота должна быть целиком
	UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>();
	UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>();
	const TArray<uint8> S = StatusBlockFrom5052(Body5052);
	const int32 QuestNo = AsdaBytes::RdI16(&Body5052[0]);    // `a13` = `*(i16*)(CQuest+1324)` = тело @0 (сервер: `iQuestIndex`)
	const int32 Stage   = AsdaBytes::RdI16(&Body5052[32]);   // запись слота @22 +10
	// (3) вид 0 и поле очков вида 5 — сеть (ключ `asda.Quest.ApplyJobChange`, как у 5042).
	if (Net && UAsdaNetSubsystem::IsQuestJobChangeApplyOn()) { Net->ApplyCompletionCharBlock5052(S); }
	// (1)+(4) журнал: завершение + выданные вместе с ним квесты, затем пересчёт меток и `OnQuestStateChanged`.
	TArray<int32> Granted;
	if (QM) { Granted = QM->ApplyTurnInResult(QuestNo, Stage, /*bFinished*/ true, S); }
	for (int32 Ep : Granted) { PostQuestReceivedNotice(Ep, -1, 5052); }
	PostGold(GI, S);            // вид 2 — `a2 + 498`
	PostItems(GI, Net, S);      // вид 4 — `a2 + 154 + 57i`, вес `a2 + 496`
	// Вид 5 (`!a2[17] && !a2[18] && !a2[19] && i16 a2[20] > 0`): поле очков уже записала сеть (ветка 2 `ApplyQuestTurnInCharBlock`).
	if (S[10] == 0 && S[11] == 0 && S[12] == 0 && AsdaBytes::RdI16(&S[13]) > 0) { PostSkillPointsLine(GI); }
	PostCharm(GI, Net, S);      // вид 6 — `a2 + 502`
	UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5052 завершение квеста %d: бит завершения, выдано квестов %d, блок наград разобран (тело %d б)"),
		QuestNo, Granted.Num(), Body5052.Num());
}

// ════════════════════════════════════════════════════════════════════════════════════════════════
//  ПРИЁМ 5042 — РЕЗУЛЬТАТ СДАЧИ ШАГА КВЕСТА (ответ на 5041)
// ════════════════════════════════════════════════════════════════════════════════════════════════
// ★★★ Отдельный канал от приёма квеста. Отправитель — `GameServer!recvRewardQuestProcess@0x45CF90`.
//   Сервер, прежде чем ответить, уже начислил награду (`sub_45DEE0` — предметы/деньги/опыт),
//   двинул стадию или закрыл квест (`NpcServer!sub_420C10@0x420C10`, ветки `SHIBYTE(v35[17])`
//   1 = «шаг вперёд», 2 = «квест завершён»: взводит бит карты завершения и чистит слот).
//   Наше дело — снять флаг «заявка в полёте», применить новое состояние журнала и перейти на
//   страницу диалога, которую назвал сервер.
void UAsdaNpcDialogSubsystem::HandleQuestTurnInResult(int32 StepCode, int32 QuestNo, int32 Stage,
                                                      int32 FinishType, int32 TalkId, const TArray<uint8>& Body)
{
	bCompleteInFlight = false;
	bAcceptInFlight = false;

	UGameInstance* GI = GetGameInstance();

	// ★★★ ДОБАВЛЕНО 2026-08-22. Эталон `net_ApplyPlayerFullUpdate@0xD828A0`, ветка `resultCode == 0`,
	//   первым делом делает `savedThis[437] = 0` (+ `uiTogglePanelTab(0)` и `sub_92E450(g_pWindowMgr)`)
	//   — то есть ГАСИТ РЕЖИМ ПОПАПА НАГРАД и убирает само окно. Без этого окно диалога навсегда
	//   оставалось в режиме «принимаю только 567», и после сдачи по нему нельзя было щёлкнуть
	//   вообще ничем. Гасим ДО применения состояния — на случай отказа окно тоже надо разблокировать.
	if (bRewardPopupMode || RwWin) { HideRewardPopup(); }   // внутри: `bRewardPopupMode = false`

	UAsdaSoundSubsystem* Snd = GI ? GI->GetSubsystem<UAsdaSoundSubsystem>() : nullptr;

	// ════════════════ ЗВУКИ ПЕРЕХОДА КВЕСТА (эталон `net_ApplyPlayerFullUpdate@0xD828A0`) ═══════
	// ★★★ ДОБАВЛЕНО 2026-08-22 (`Reports/quest_notice_sound_fix.md` §2): их не было ВООБЩЕ,
	//   поэтому сдача квеста проходила молча — жалоба владельца «после получения награды звука не
	//   услышал». Порт играл только звук 16, и только когда сдача выдавала СЛЕДУЮЩИЙ квест.
	//   Раскладка тела 5042 у эталона: база `savedThis+60364` = `body[0]`.
	//     `body[5] == 2`                          → квест ЗАВЕРШЁН целиком   → звук 14 (S_Episord03)
	//     `body[15] (i32) > 0 && body[5] != 2`    → слот журнала обновлён    → звук 13 (S_Episord02)
	//     `body[59] > 0 || body[103] > 0`         → выдан новый квест        → звук 16 (S_Episord05)
	//   ⚠ У предметных наград (`updateFlags[3]`, 6 записей по 57 б от `body[147]`) СВОЕГО звука в
	//   оригинале НЕТ — они только пишутся строками в чат. «Звук награды» = звук перехода квеста.
	const bool bQuestFinished = (FinishType == 2);
	// ★ Блок `body[5] == 2` стоит в эталоне ВЫШЕ `switch (resultCode)` ⇒ звук 14 играет при ЛЮБОМ
	//   коде шага, в том числе когда сервер вышел, ничего не начислив. Воспроизводим порядок.
	if (bQuestFinished && Snd) { Snd->PlaySound2DById(14); }

	// Ветка сервера `if (*(__int16 *)((char *)&v16[6] + 1))` — ненулевой код шага: сервер выходит
	//   СРАЗУ, ничего не начислив, и всё равно шлёт 5042. Состояние журнала не трогаем.
	if (StepCode != 0)
	{
		UE_LOG(LogAsdaNpcDlg, Warning,
			TEXT("[NPC-диалог] 5042: сервер вернул код шага %d (квест %d) — награда НЕ начислена, ")
			TEXT("состояние журнала не меняем"), StepCode, QuestNo);
		// ★ 19.10.2026: эталонная ветка `resultCode` 1…13 — окно-сообщение (таблица у `AsdaQuestTurnInReward`).
		if (AsdaQuestTurnInReward::IsOn()) { AsdaQuestTurnInReward::PostRefusal(GI, StepCode); }
	}
	else
	{
		const bool bFinished = bQuestFinished;
		TArray<int32> Granted;
		if (UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr)
		{
			Granted = QM->ApplyTurnInResult(QuestNo, Stage, bFinished, Body);
		}
		UE_LOG(LogAsdaNpcDlg, Log,
			TEXT("[NPC-диалог] 5042: квест %d %s (стадия %d), переход на страницу %d"),
			QuestNo, bFinished ? TEXT("ЗАВЕРШЁН") : TEXT("сдвинут на следующий шаг"), Stage, TalkId);

		// ★ ПАРНОЕ УВЕДОМЛЕНИЕ ПРИ СДАЧЕ. Баннера «задание сдано» у оригинала НЕТ (просмотрены все
		//   ветки `net_ApplyPlayerFullUpdate@0xD828A0` — при успехе там только строки наград в чат).
		//   Но нотис 20197 «Получено задание …» он показывает и здесь — когда сдача ВЫДАЛА
		//   следующий квест (`chatBuildTransactionNotice@0xD9FC70` case 1, по записям тела +59/+103).
		//   Плюс эталонный звук 16 (`csoundPlayName2DStore(g_pSoundMgr, 16, 0)`) в той же ветке.
		if (Granted.Num() > 0)
		{
			if (Snd) { Snd->PlaySound2DById(16); }
			for (int32 Ep : Granted) { PostQuestReceivedNotice(Ep, -1, 5042); }
		}

		// ★ 19.10.2026 — строки наград по эталону (таблица у `AsdaQuestTurnInReward`), порядок `case 0`: вид 1 (выше) → 2 → 3 → 4 → 5 → 6.
		const bool bRewardLines = AsdaQuestTurnInReward::IsOn();
		UAsdaNetSubsystem* NetSys = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
		if (bRewardLines) { AsdaQuestTurnInReward::PostGold(GI, Body); }   // вид 2 — золото

		// ★★★ ЗВУК ОБЫЧНОЙ СДАЧИ (эталонный `updateFlags[2]`): слот журнала сдаваемого квеста
		//   приехал в теле по `+15` и не пуст, а квест НЕ закрыт целиком (иначе отработал звук 14
		//   выше). Именно этот путь проходит подавляющее большинство сдач — и именно он молчал.
		//   Порядок вызовов внутри `case 0` у эталона: 15 (смена профессии) → 16 → 13.
		const bool bSlotUpdated = !bQuestFinished && Body.Num() >= 19 && AsdaBytes::RdI32(&Body[15]) > 0;
		if (bSlotUpdated && Snd) { Snd->PlaySound2DById(13); }
		if (bSlotUpdated && bRewardLines) { AsdaQuestTurnInReward::PostSlotUpdated(GI); }   // вид 3 — 6019 после звука 13
		if (bRewardLines) { AsdaQuestTurnInReward::PostItems(GI, NetSys, Body); }        // вид 4 — предметы
		if (!bQuestFinished && !bSlotUpdated)
		{
			// Молчать нельзя (закон «нет данных → пиши, чего не хватило»): звука не будет, и
			//   владелец должен понимать почему — сервер не прислал запись слота по +15.
			UE_LOG(LogAsdaNpcDlg, Warning,
				TEXT("[NPC-диалог] 5042: квест %d — тело %d б, запись слота по +15 пуста ⇒ эталонный ")
				TEXT("звук 13 не играется (у оригинала условие `body[15] > 0`)"), QuestNo, Body.Num());
		}

		// ★ 18.10.2026 — ВЕТКА «ТОЛЬКО ОЧКИ УМЕНИЙ» (`updateFlags[4]`, network.c:21825-21826 / :22607-22611): профессия не менялась
		//   (`!body[10] && !body[11] && !body[12]`), а `i16 body[13] > 0` — сами очки уже записала сеть
		//   (`UAsdaNetSubsystem::ApplyQuestTurnInCharBlock`), здесь строка `chatBuildTransactionNotice` вид 5 (chat.c:6192-6196):
		//   loc 6024 «Получены очки навыка.» в чат и в плашку 221, цвет −256 (0xFFFFFF00), 10 с. Порядок — после строк квеста.
		if (UAsdaNetSubsystem::IsQuestJobChangeApplyOn() && Body.Num() >= 15
			&& Body[10] == 0 && Body[11] == 0 && Body[12] == 0 && AsdaBytes::RdI16(&Body[13]) > 0)
		{
			static constexpr int32 kLocSkillPointsGot = 6024;
			// ★ 19.10.2026: пара «журнал + плашка 221», −256 — общая точка строк наград (`AsdaQuestTurnInReward::PostYellowPair`).
			static_assert(kLocSkillPointsGot == 6024, "loc вида 5");
			AsdaQuestTurnInReward::PostSkillPointsLine(GI);
			UE_LOG(LogAsdaNpcDlg, Log, TEXT("[NPC-диалог] 5042: квест %d — получены очки умений %d → строка 6024"),
				QuestNo, AsdaBytes::RdI16(&Body[13]));
		}
		if (bRewardLines) { AsdaQuestTurnInReward::PostCharm(GI, NetSys, Body); }   // вид 6 — обаяние, последней
	}

	// Переход на страницу диалога — только если сервер её назвал (иначе окно остаётся как есть).
	if (TalkId > 0) { RenderPage(TalkId); }
}
