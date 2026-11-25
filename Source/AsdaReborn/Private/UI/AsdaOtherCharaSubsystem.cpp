// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Сведения о чужом персонаже» `OTHERCHARA_DATA` (4200).
// Полный разбор эталона, раскладка обеих половин ответа (5479 + 7006), таблица нотисов и список
// найденных дефектов оригинала — в шапке `Public/UI/AsdaOtherCharaSubsystem.h`. Здесь не повторяется.
#include "UI/AsdaOtherCharaSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Net/NetOpcodes.h"
#include "Login/AsdaLoginData.h"          // DecodeCp1251 — строки клиента в cp1251
#include "Login/AsdaButtonRelay.h"        // мостик UButton::OnClicked
#include "World/AsdaWorldSubsystem.h"     // реестр соседей = `CharManager` порта
#include "Items/AsdaItemTableSubsystem.h" // записи предметов и самоцветов
#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaIconCellGrid.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaConstellationSubsystem.h"
#include "UI/AsdaSkillDatabase.h"
#include "UI/AsdaInventorySubsystem.h"    // FAsdaItemInstance — 57-байтная запись предмета
#include "Quest/AsdaQuestTypes.h"         // FAsdaQuestSlot — свои 12 слотов журнала
#include "UI/AsdaEquipCellText.h"         // ★ 08.11.2026: надпись надетой ячейки — общая точка с сумкой
#include "AsdaSelf.h"                     // свой userNum / уровень
#include "UI/AsdaNotice.h"
#include "UI/AsdaUiHelpers.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UI/AsdaJuiWindowHandle.h"        // ★ Дедуп №2, В1-B (U22): каркас `EnsureLazy` окна 4200

DEFINE_LOG_CATEGORY_STATIC(LogAsdaOtherChara, Log, All);

const FName UAsdaOtherCharaSubsystem::kWindowId(TEXT("OTHERCHARA_DATA"));

static const TCHAR* kWindowPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_OTHERCHARA_DATA");

// z окна: обычное игровое окно поверх HUD, ниже модалок (`ONEBUT_MSGBOX` 0, модалка смерти 800,
//   попап обрыва 900). Своего числа в .JUI у окна нет — z там задаётся порядком показа.
static constexpr int32 kWindowZ = 30;

// ── Константы раскладки записи (см. §2 шапки) ────────────────────────────────────────────────
static constexpr int32 kCharHalfSize  = 121;   // тело 5479
static constexpr int32 kGearHalfSize  = 631;   // тело 7006 = 4 б ключа + 627 б экипировки
static constexpr int32 kGearBytes     = 627;   // 11 × 57
static constexpr int32 kEquipSlots    = 11;
static constexpr int32 kSowelSlots    = 4;
static constexpr int32 kQuestRows     = 12;
static constexpr int32 kRecSize       = 57;    // FAsdaItemInstance::kRecordSize
static constexpr int32 kSowelOfsInRec = 16;    // эталонное `+180` − база экипировки `+164`

// ★ Индексы планов в именах узлов WBP (закон `jui-importer-widget-naming`: план зовётся
//   `{idc}_{ctrlIdx}_{plane}`). Сняты дампом реального ассета, а не угаданы:
//   `OTHERCHARA_EQUIP00_27_0` … `OTHERCHARA_EQUIP10_37_0` — шаг 1 от 27.
static constexpr int32 kEquipPlaneBase = 27;
// Гнёзда самоцветов: `OTHERCHARA_Sowel1_85_0` … `Sowel4_88_0` — шаг 1 от 85.
static const int32 kSowelPlane[kSowelSlots] = { 85, 86, 87, 88 };
// Затемнители: порядок в ассете НЕ по номеру — `SubSowelDim1_84`, `2_81`, `3_83`, `4_82`.
static const int32 kDimPlane[kSowelSlots]   = { 84, 81, 83, 82 };

// Имя узла-иконки слота надетого (общая точка: строится в трёх местах).
static FString EquipCellName(int32 Slot)
{
	return FString::Printf(TEXT("OTHERCHARA_EQUIP%02d_%d_0"), Slot, kEquipPlaneBase + Slot);
}
// Имя узла-иконки гнезда самоцвета.
static FString SowelCellName(int32 Index)
{
	return FString::Printf(TEXT("OTHERCHARA_Sowel%d_%d_0"), Index + 1, kSowelPlane[Index]);
}

// Максимум недособранных половин в очереди (лечение D-OC2: у эталона список НЕ чистится).
//   Больше двух одновременно не бывает по построению — запрос строго один (`win[40]`), но
//   потерянные половины копились бы вечно. Четыре — запас на перезапрос без ответа.
static constexpr int32 kMaxPendingHalves = 4;

// ── Нотисы окна (см. §5 шапки) ───────────────────────────────────────────────────────────────
namespace OcLoc
{
	constexpr int32 kLabelLevel   = 12008;    // TXT03 4206 «Уровень»
	constexpr int32 kLabelClass   = 12009;    // TXT05 4208 «Профессия»
	constexpr int32 kLabelClan    = 12010;    // TXT07 4210 «Клан»
	constexpr int32 kLabelEquip   = 12011;    // TXT09 4213
	constexpr int32 kLabelSign    = 110631;   // TXT10 4215 «Знак зодиака»
	constexpr int32 kLabelQuests  = 13300;    // QstInfo_Title 4290 «Текущие задания»
	constexpr int32 kLvShort      = 12215;    // «Ур.» — хвост строки уровня
	constexpr int32 kQuestDone    = 6004;     // «Готово» — префикс в квадратных скобках
}

// ★ Цвета строк заданий. Эталонные знаковые константы `CControl_SetField923` переведены в ARGB
//   ровно так же, как это уже сделано в порту для −4144960 (`AsdaPurchaseBoxSubsystem.cpp:66`).
static const FColor kQuestColor1 = FColor(0xF8, 0x94, 0x89, 0xFF);   // −486263
static const FColor kQuestColor2 = FColor(0xFF, 0x32, 0xDC, 0xFF);   // −52516
static const FColor kQuestColor3 = FColor(0x64, 0x64, 0xC8, 0xFF);   // −10197816
static const FColor kQuestColorN = FColor(0xC0, 0xC0, 0xC0, 0xFF);   // −4144960 (обычный квест)

// ★★★ КЛЮЧ ОТКАТА К ДЕФЕКТУ D-OC1 (см. §4 шапки): 1 = стадия и цвет строки берутся по
//   `questSlot`, как в оригинале, то есть при задании без названия «съезжают» на чужое.
static ASDA_DEV_CVAR(int32, CVarLegacyQuestIndex,
	TEXT("asda.OtherChara.LegacyQuestIndex"), 0,
	TEXT("1 = 1:1 дефект D-OC1 окна 4200: стадию и цвет строки задания брать по счётчику "
	     "заполненных строк, а не по номеру задания. 0 (умолчание) = исправлено."),
	ECVF_Default);

// ★★★ [ОРИГИНАЛ] 30.10.2026 — баг игроков «наведение на надетую вещь чужого игрока выделяет её без клика».
//   Эталон `npcTalkWnd_HandleMessage@0xDA9B70`: msg 8 (наведение) → только подсказка, msg 0xB (нажатие ЛКМ) →
//   курсор EQUIPSEL 4223 + самоцветы слота. Порт выполнял ветку msg 0xB на наведении (разбор — в заголовке у `OnEquipClick`).
static ASDA_DEV_CVAR(int32, CVarOtherCharaSelectOnClick,
	TEXT("asda.OtherChara.SelectOnClick"), 1,
	TEXT("1 (умолчание) = как в оригинале: курсор выбора 4223 и самоцветы слота надетого ставит ЩЕЛЧОК, наведение даёт "
	     "только подсказку. 0 = прежний порт: курсор и самоцветы ставит наведение."),
	ECVF_Default);

// ★★★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 16, баг 4) — «Информация о персонаже» по СЕБЕ (функция 2/5 окна C, клавиша 61, пункт 2006).
//   Эталон шлёт 5478 и о себе (`otherCharaWnd_SetTargetUser@0xDA7640`), но сервер `Send_TargetChar_Information` (GameServer.c:126044,
//   case 0x1566) отвечает 5479+7006 только при `CUser+0x16B` (UserIndex) запросившего ≠ цели — о себе молчит, окно не открывается.
//   1 = окно собирается из СВОИХ данных клиента теми же полями 5479/7006 (`ShowSelf`), 5478 о себе не шлётся. 0 = как в оригинале.
static ASDA_DEV_CVAR(int32, CVarOtherCharaSelfLocal,
	TEXT("asda.OtherChara.SelfLocal"), 1,
	TEXT("1 (умолчание) = [ИСПРАВЛЕНИЕ] «Информация о персонаже» по себе открывает окно 4200 из своих данных (сервер на 5478 о себе "
	     "не отвечает). 0 = как в оригинале: 5478 о себе уходит, ответа нет, окно не открывается."),
	ECVF_Default);

// ★★★ 08.11.2026 [ИСПРАВЛЕНИЕ] (пачка 17, баг 6): эталон `uiRefreshItemOptionSlots@0xDA77E0` (ui_core.c) пишет в ячейку надетого
//   ТОЛЬКО количество слота 10, «+N%» заточки не рисует, хотя запись 57 б из 7006 (сервер копирует `CUser.gap_1AC` целиком) несёт
//   уровень (+24) и процент (+27). 1 = надпись как у надетого в сумке (`AsdaEquipCellText::Build`); 0 = как в оригинале.
static ASDA_DEV_CVAR(int32, CVarOtherCharaEnchantText,
	TEXT("asda.OtherChara.EnchantText"), 1,
	TEXT("1 (умолчание) = [ИСПРАВЛЕНИЕ] окно сведений о персонаже 4200 показывает «+N%» заточки на надетом, как сумка. "
	     "0 = как в оригинале: только количество в слоте 10."),
	ECVF_Default);

bool UAsdaOtherCharaSubsystem::IsSelfLocalEnabled() { return CVarOtherCharaSelfLocal.GetValueOnGameThread() != 0; }

// ═══════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═══════════════════════════════════════════════════

void UAsdaOtherCharaSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// ★★★ РЕВИЗИЯ 2026-09-17 (С5): ЗАВИСИМОСТЬ ОБЪЯВЛЯЕМ ЯВНО.
	//   Ниже мы подписываемся на делегат `UAsdaNetSubsystem` прямо здесь, в `Initialize`. Порядок
	//   подъёма подсистем коллекции без этой строки не определён, и подписка может уйти в другой
	//   экземпляр (именно так теряются подписки). Правило проекта — см. `AsdaAfterDeathSubsystem.cpp:70`,
	//   `AsdaCharaInfoSubsystem.cpp:174`, `AsdaSkillFxSubsystem.cpp:158`.
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());

	QuestRows.SetNum(kQuestRows);
	QuestIconN.SetNum(kQuestRows);
	QuestIconP.SetNum(kQuestRows);
	EquipQty.SetNum(kEquipSlots);
	SowelIcons.SetNum(kSowelSlots);
	SowelDims.SetNum(kSowelSlots);
	Hearts.SetNum(3);

	// Обе половины ответа приходят ОДНИМ делегатом «ответы контекст-меню по игроку».
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		HMenuResult = Net->OnPlayerMenuResult.AddUObject(this, &UAsdaOtherCharaSubsystem::OnMenuResult);
	}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("asda.OtherChara.Dump"),
		TEXT("Дамп окна OTHERCHARA_DATA 4200: разобранные контролы + состояние (цель, ожидание, очередь половин)."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogAsdaOtherChara, Log, TEXT("\n%s\n%s"), *DumpControls(), *DumpState());
		}),
		ECVF_Default);
#endif // ASDA_DEV_CONSOLE
}

void UAsdaOtherCharaSubsystem::Deinitialize()
{
	if (UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		if (HMenuResult.IsValid()) { Net->OnPlayerMenuResult.Remove(HMenuResult); }
	}
	HMenuResult.Reset();
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaOtherCharaSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// ⚠ Закон `no-reentrant-net-callbacks`: здесь ничего не отправляем — только локальная чистка.
	CloseWindow();
	Pending.Reset();
	Shown = FAsdaOtherCharaRecord();
	bHasShown = false;
	TargetUserNum = -1;
	bAwaitingReply = false;
	bSelfTarget = false;
	HoverSlot = -1;
	(void)Reason;
}

FString UAsdaOtherCharaSubsystem::Loc(int32 Id) const
{
	return AsdaNotice::Loc(GetGameInstance(), Id);
}

// ═══════════════════════════ ЗАПРОС ═══════════════════════════════════════════════════════════

// Порт `otherCharaWnd_SetTargetUser@0xDA7640` без отправки пакета (её делает меню игрока).
bool UAsdaOtherCharaSubsystem::BeginRequest(int32 InTargetUserNum)
{
	UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	// 1:1 гейт эталона `if (this[235])`: цели нет в `CharManager` ⇒ НИЧЕГО не делаем и молчим
	//   (строки отказа у эталона в этой точке нет — см. таблицу нотисов §5 шапки).
	if (!WS || !WS->GetPlayerActorByUserNum(InTargetUserNum))
	{
		UE_LOG(LogAsdaOtherChara, Warning,
			TEXT("[чужой персонаж] userNum=%d нет в реестре соседей — эталон здесь молча выходит"),
			InTargetUserNum);
		return false;
	}
	TargetUserNum  = InTargetUserNum;
	bSelfTarget    = false;  // ★ 08.11.2026: цель — сосед, поля актора снова из реестра соседей
	bAwaitingReply = true;   // эталонное `win[40] = 1` (его ставит `antiCheat_sendItemFlagDigest`)
	// Половины ПРОШЛОГО запроса к новому отношения не имеют: ключ пары у сервера новый.
	Pending.Reset();
	UE_LOG(LogAsdaOtherChara, Log,
		TEXT("[чужой персонаж] цель userNum=%d, ждём пару 5479+7006"), TargetUserNum);
	return true;
}

// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] — окно о себе из своих данных. Поля — ровно те, что сервер кладёт в 5479/7006 о соседе
//   (`Send_TargetChar_Information`, GameServer.c:126080-126101), из своих зеркал клиента:
//     5479 +3  17 б  клан       ← `UAsdaNetSubsystem::SelfClanName` (char+2410; сервер `CUser.gap_17E1D[1242]`)
//     5479 +20/+68/+92 12 слотов журнала ← `UAsdaQuestMarkerSubsystem::GetSlots`: сервер берёт запись слота 44 б с +18 —
//                    +18 id (i32, гейт > 0), +26 номер квеста, +28 стадия ⇒ у клиента `QuestId` (+0), `QuestNo` (+8), `Stage` (+10)
//     7006 +4  627 б 11 надетых записей по 57 б ← контейнер 3 (`UAsdaInventorySubsystem::GetCell(3, i)->Raw`; сервер `CUser.gap_1AC`)
//   Не заполняются (окно их не читает, у клиента их нет): +0/+2 служебные (`Nick_name_[12..14]`), +120 `secondaryCurrency`,
//   ключ пары (+116 / 7006 +0). Поля актора (имя, уровень, класс, знак, пол) `GetTarget` берёт из своих зеркал.
bool UAsdaOtherCharaSubsystem::ShowSelf(const TCHAR* Why)
{
	if (!IsSelfLocalEnabled()) { return false; }
	UGameInstance* GI = GetGameInstance();
	const int32 Self = AsdaSelf::GetUserIdxOr(GI, -1);
	if (!GI || Self < 0)
	{
		UE_LOG(LogAsdaOtherChara, Warning, TEXT("[чужой персонаж] %s: окно о себе — своего номера ещё нет, молча"), Why);
		return false;
	}
	FAsdaOtherCharaRecord R;
	R.bHasCharHalf = true;
	R.bHasGearHalf = true;
	if (const UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>()) { R.ClanName = Net->SelfClanName; }   // зеркало char+2410 (то же читает UAsdaClanSubsystem::SelfClanName)
	// Пустой слот — −1 во всех трёх полях: сервер готовит буфер 5479 `sub_558830@0x558830` (GameServer.c:132464: id/ключ/стадия = −1)
	//   и перезаписывает только слоты с id > 0. Ключ 0 у клиента находит эпизод (строка «Sowel Instruction»), −1 — нет.
	R.QuestId.Init(-1, kQuestRows);
	R.QuestKey.Init(-1, kQuestRows);
	R.QuestStage.Init(-1, kQuestRows);
	int32 Quests = 0;
	if (const UAsdaQuestMarkerSubsystem* QM = GI->GetSubsystem<UAsdaQuestMarkerSubsystem>())
	{
		const TArray<FAsdaQuestSlot>& Slots = QM->GetSlots();
		for (int32 i = 0; i < kQuestRows && i < Slots.Num(); ++i)
		{
			if (Slots[i].QuestId <= 0) { continue; }   // гейт сервера `gap_54F6[44*i+18] > 0`: иначе слот остаётся −1
			R.QuestId[i]    = Slots[i].QuestId;
			R.QuestKey[i]   = (int32)(int16)Slots[i].QuestNo;
			R.QuestStage[i] = (int32)(int16)Slots[i].Stage;
			++Quests;
		}
	}
	R.Gear.SetNumZeroed(kGearBytes);
	int32 Worn = 0;
	if (const UAsdaInventorySubsystem* Inv = GI->GetSubsystem<UAsdaInventorySubsystem>())
	{
		for (int32 S = 0; S < kEquipSlots; ++S)
		{
			const FAsdaItemInstance* It = Inv->GetCell(3, S);
			if (!It || It->Raw.Num() < kRecSize) { continue; }
			FMemory::Memcpy(&R.Gear[kRecSize * S], It->Raw.GetData(), kRecSize);
			if (It->ItemCode > 0) { ++Worn; }
		}
	}
	CloseWindow();
	Pending.Reset();
	TargetUserNum  = Self;
	bSelfTarget    = true;
	bAwaitingReply = false;   // 5478 не уходит — ответа не ждём
	Shown = MoveTemp(R);
	bHasShown = true;
	UE_LOG(LogAsdaOtherChara, Log,
		TEXT("[чужой персонаж] %s: окно 4200 о СЕБЕ из своих данных (userNum=%d): клан «%s», заданий %d, надето %d из %d; 5478 не шлётся"),
		Why, Self, *Shown.ClanName, Quests, Worn, kEquipSlots);
	SetData();
	return IsOpen();
}

// ═══════════════════════════ ПРИЁМ ОБЕИХ ПОЛОВИН ══════════════════════════════════════════════

void UAsdaOtherCharaSubsystem::OnMenuResult(int32 Opcode, const TArray<uint8>& Body)
{
	if (Opcode == (int32)OP_PLAYER_INFO_WIN_ON_RECV_CHAR_DATA) { OnCharHalf(Body); return; }
	if (Opcode == (int32)OP_NET_HANDLE_PLAYER_INFO_PACKET)     { OnGearHalf(Body); return; }
	// Остальные опкоды делегата (4151/5283/5201/6083) этому окну не адресованы.
}

// 5479 — порт `playerInfoWin_onRecvCharData@0xDA8680`, половина «персонаж».
void UAsdaOtherCharaSubsystem::OnCharHalf(const TArray<uint8>& Body)
{
	// 1:1 гейт эталона `if (pWindow[40] == 1)`: не ждём ответа — пакет игнорируется целиком.
	if (!bAwaitingReply)
	{
		UE_LOG(LogAsdaOtherChara, Verbose,
			TEXT("[чужой персонаж] 5479 пришёл, но окно ответа не ждёт (win[40]=0) — игнор, как эталон"));
		return;
	}
	if (Body.Num() < kCharHalfSize)
	{
		UE_LOG(LogAsdaOtherChara, Warning,
			TEXT("[чужой персонаж] 5479: тело %d б < %d — половина отброшена"), Body.Num(), kCharHalfSize);
		return;
	}
	const uint8* B = Body.GetData();

	FAsdaOtherCharaRecord H;
	H.bHasCharHalf = true;
	H.Svc0     = (int32)AsdaBytes::RdI16(&B[0]);
	H.Svc2     = (int32)B[2];
	H.ClanName = FAsdaLoginData::DecodeCp1251(&B[3], 17);
	H.QuestId.SetNumZeroed(kQuestRows);
	H.QuestKey.SetNumZeroed(kQuestRows);
	H.QuestStage.SetNumZeroed(kQuestRows);
	for (int32 i = 0; i < kQuestRows; ++i)
	{
		H.QuestId[i]    = AsdaBytes::RdI32(&B[20 + 4 * i]);
		H.QuestKey[i]   = (int32)AsdaBytes::RdI16(&B[68 + 2 * i]);
		H.QuestStage[i] = (int32)AsdaBytes::RdI16(&B[92 + 2 * i]);
	}
	H.PairKey = AsdaBytes::RdI32(&B[116]);
	H.Svc120  = (int32)B[120];

	int32 KeyedQuests = 0;
	for (int32 K : H.QuestKey) { if (K > 0) { ++KeyedQuests; } }
	UE_LOG(LogAsdaOtherChara, Log,
		TEXT("[чужой персонаж] ←5479 (%d б): ключ пары %d, клан «%s», заданий с ключом %d"),
		Body.Num(), H.PairKey, *H.ClanName, KeyedQuests);

	MergeOrQueue(MoveTemp(H));
}

// 7006 — порт `netHandlePlayerInfoPacket@0xDA8BD0`, половина «экипировка».
void UAsdaOtherCharaSubsystem::OnGearHalf(const TArray<uint8>& Body)
{
	if (!bAwaitingReply)
	{
		UE_LOG(LogAsdaOtherChara, Verbose,
			TEXT("[чужой персонаж] 7006 пришёл, но окно ответа не ждёт (win[40]=0) — игнор, как эталон"));
		return;
	}
	if (Body.Num() < kGearHalfSize)
	{
		UE_LOG(LogAsdaOtherChara, Warning,
			TEXT("[чужой персонаж] 7006: тело %d б < %d — половина отброшена"), Body.Num(), kGearHalfSize);
		return;
	}
	const uint8* B = Body.GetData();

	FAsdaOtherCharaRecord H;
	H.bHasGearHalf = true;
	H.PairKey = AsdaBytes::RdI32(&B[0]);
	// Эталон копирует 627 байт тремя движениями (624 + 2 + 1) — это один непрерывный блок
	//   `Body[4…630]`, разрезанный декомпилятором по границам dword'ов записи.
	H.Gear.SetNumUninitialized(kGearBytes);
	FMemory::Memcpy(H.Gear.GetData(), &B[4], kGearBytes);

	int32 Filled = 0;
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		if (AsdaBytes::RdI32(&H.Gear[kRecSize * S]) > 0) { ++Filled; }
	}
	UE_LOG(LogAsdaOtherChara, Log,
		TEXT("[чужой персонаж] ←7006 (%d б): ключ пары %d, занятых слотов %d из %d"),
		Body.Num(), H.PairKey, Filled, kEquipSlots);

	MergeOrQueue(MoveTemp(H));
}

// Общий хвост обеих веток эталона: найти в списке половину с тем же ключом и дособрать запись,
//   иначе положить себя в список и ждать пару.
void UAsdaOtherCharaSubsystem::MergeOrQueue(FAsdaOtherCharaRecord&& Half)
{
	for (int32 i = 0; i < Pending.Num(); ++i)
	{
		if (Pending[i].PairKey != Half.PairKey) { continue; }

		FAsdaOtherCharaRecord Full = MoveTemp(Pending[i]);
		Pending.RemoveAt(i);   // эталон здесь делает `erase` из списка

		// Дособираем недостающую половину. Пару «две одинаковые половины» (сервер прислал 5479
		//   дважды) отсекаем: перезаписываем и остаёмся неполными — такого у сервера не бывает,
		//   но молча показать полузаполненное окно хуже, чем подождать.
		if (Half.bHasCharHalf)
		{
			Full.bHasCharHalf = true;
			Full.Svc0 = Half.Svc0; Full.Svc2 = Half.Svc2; Full.Svc120 = Half.Svc120;
			Full.ClanName   = MoveTemp(Half.ClanName);
			Full.QuestId    = MoveTemp(Half.QuestId);
			Full.QuestKey   = MoveTemp(Half.QuestKey);
			Full.QuestStage = MoveTemp(Half.QuestStage);
		}
		if (Half.bHasGearHalf)
		{
			Full.bHasGearHalf = true;
			Full.Gear = MoveTemp(Half.Gear);
		}
		if (!Full.IsComplete())
		{
			UE_LOG(LogAsdaOtherChara, Warning,
				TEXT("[чужой персонаж] ключ %d: пришла ВТОРАЯ такая же половина — запись всё ещё неполная, ждём"),
				Full.PairKey);
			Pending.Add(MoveTemp(Full));
			return;
		}

		Shown = MoveTemp(Full);
		bHasShown = true;
		bAwaitingReply = false;   // эталонное `pSelf[40] = 0` после `CPlayerInfoWin_SetData`
		UE_LOG(LogAsdaOtherChara, Log,
			TEXT("[чужой персонаж] ключ %d: обе половины собраны — заполняем окно"), Shown.PairKey);
		SetData();
		return;
	}

	// Пары ещё нет — кладём себя в список. У эталона список не ограничен (дефект D-OC2).
	const UWorld* W = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	Half.StampSec = W ? W->GetTimeSeconds() : 0.0;
	if (Pending.Num() >= kMaxPendingHalves)
	{
		UE_LOG(LogAsdaOtherChara, Warning,
			TEXT("[чужой персонаж] очередь половин переполнена (%d) — вытесняем самую старую (лечение D-OC2)"),
			Pending.Num());
		Pending.RemoveAt(0);
	}
	Pending.Add(MoveTemp(Half));
}

// ═══════════════════════════ ОКНО И КОНТРОЛЫ ══════════════════════════════════════════════════

UWidget* UAsdaOtherCharaSubsystem::Find(const TCHAR* Name) const
{
	UWidget* W = Win ? Win->GetWidgetFromName(FName(Name)) : nullptr;
	if (!W)
	{
		// Эталон в этом месте показывает MessageBoxA «Point NULL» — у нас строка в лог.
		UE_LOG(LogAsdaOtherChara, Error, TEXT("[чужой персонаж] контрол %s в WBP не найден"), Name);
	}
	return W;
}

void UAsdaOtherCharaSubsystem::SetShown(UWidget* W, bool bShown) const
{
	if (W) { AsdaUi::SetShownDecor(W, bShown); }
}

bool UAsdaOtherCharaSubsystem::IsOpen() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && WM->IsOpen(kWindowId);
}

bool UAsdaOtherCharaSubsystem::EnsureWidget()
{
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	// ★ Дедуп №2, В1-B (U22, 11.10.2026): каркас «открыть при первом показе» `FAsdaJuiWindowHandle::EnsureLazy`:
	//   кэш годен → true; иначе сброс полей (`ResetWidget`) → открыть → `SetupControls` → закрыватель. Порядок прежний.
	// Окно закрывается общими путями порта (Esc / «закрыть все окна игрока») тем же телом,
	//   что и кнопка 4201.
	return FAsdaJuiWindowHandle::Succeeded(FAsdaJuiWindowHandle(kWindowId, kWindowPath, kWindowZ).EnsureLazy(*WM, Win, /*bReady*/true,
		ESlateVisibility::SelfHitTestInvisible,
		[this]() { ResetWidget(); },
		[]() { UE_LOG(LogAsdaOtherChara, Error, TEXT("[чужой персонаж] %s не загрузился — окно показать нечем"), kWindowPath); },
		[this](UAsdaWindowSubsystem&) { SetupControls(); },
		FSimpleDelegate::CreateWeakLambda(this, [this]() { CloseWindow(); })));
}

void UAsdaOtherCharaSubsystem::ResetWidget()
{
	Relays.Reset();
	Win = nullptr;
	TxtName = nullptr; TxtLevel = nullptr; TxtClass = nullptr; TxtClan = nullptr; TxtSign = nullptr;
	EquipSel = nullptr; SowelSel = nullptr;
	EquipGrid = nullptr; SowelGrid = nullptr;
	for (TObjectPtr<UWidget>& W : Hearts) { W = nullptr; }
	for (TObjectPtr<UTextBlock>& T : QuestRows) { T = nullptr; }
	for (TObjectPtr<UWidget>& W : QuestIconN) { W = nullptr; }
	for (TObjectPtr<UWidget>& W : QuestIconP) { W = nullptr; }
	for (TObjectPtr<UTextBlock>& T : EquipQty) { T = nullptr; }
	for (TObjectPtr<UImage>& I : SowelIcons) { I = nullptr; }
	for (TObjectPtr<UWidget>& W : SowelDims) { W = nullptr; }
	bControlsReady = false;
	HoverSlot = -1;
}

// Порт `COtherCharaWnd_InitControls@0xDA6C90`: кэш указателей по idnum + статические подписи.
//   Имена узлов сняты дампом реального WBP (`Reports/headless_win_common_dump.log`, 160 узлов),
//   а не угаданы: по закону `jui-importer-widget-naming` интерактив/Text носит ТОЧНОЕ имя idc,
//   а план/image — `{idc}_{ctrlIdx}_{plane}`.
void UAsdaOtherCharaSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	TxtName  = Cast<UTextBlock>(Find(TEXT("OTHERCHARA_DATATXT00")));   // 4202 имя
	TxtLevel = Cast<UTextBlock>(Find(TEXT("OTHERCHARA_DATATXT04")));   // 4207 уровень
	TxtClass = Cast<UTextBlock>(Find(TEXT("OTHERCHARA_DATATXT06")));   // 4209 класс
	TxtClan  = Cast<UTextBlock>(Find(TEXT("OTHERCHARA_DATATXT08")));   // 4211 клан
	TxtSign  = Cast<UTextBlock>(Find(TEXT("OTHERCHARA_DATATXT11")));   // 4216 знак | влечение

	// Однострочные подписи: autowrap в канвасе ломает раскладку (закон `ui-autowrap-canvas-gotcha`).
	for (UTextBlock* T : { TxtName.Get(), TxtLevel.Get(), TxtClass.Get(), TxtClan.Get(), TxtSign.Get() })
	{
		if (T) { T->SetAutoWrapText(false); }
	}

	// Статические подписи — ставятся один раз, как в `InitControls`.
	auto SetStatic = [this](const TCHAR* Name, int32 LocId)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Find(Name)))
		{
			T->SetAutoWrapText(false);
			const FString S = Loc(LocId);
			// Пусто в таблице ⇒ оставляем подпись ассета (закон `no-invented-ui`).
			if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
		}
	};
	SetStatic(TEXT("OTHERCHARA_DATATXT03"),      OcLoc::kLabelLevel);   // 4206 ← 12008
	SetStatic(TEXT("OTHERCHARA_DATATXT05"),      OcLoc::kLabelClass);   // 4208 ← 12009
	SetStatic(TEXT("OTHERCHARA_DATATXT07"),      OcLoc::kLabelClan);    // 4210 ← 12010
	SetStatic(TEXT("OTHERCHARA_DATATXT09"),      OcLoc::kLabelEquip);   // 4213 ← 12011
	SetStatic(TEXT("OTHERCHARA_DATATXT10"),      OcLoc::kLabelSign);    // 4215 ← 110631
	SetStatic(TEXT("OtherChara_QstInfo_Title"),  OcLoc::kLabelQuests);  // 4290 ← 13300
	// 4215 эталон дополнительно делает видимым (`SetVisible(win[24], 1)`).
	SetShown(Find(TEXT("OTHERCHARA_DATATXT10")), true);
	SetShown(Find(TEXT("OTHERCHARA_DATATXT11")), true);   // `SetVisible(win[8], 1)`

	// Три «сердца» 4217…4219 — планы-картинки; в WBP они уже Collapsed.
	static const TCHAR* const kHeartNames[3] = {
		TEXT("OTHERCHARA_DATA_heart1_13_0"), TEXT("OTHERCHARA_DATA_heart2_14_0"),
		TEXT("OTHERCHARA_DATA_heart3_15_0") };
	for (int32 i = 0; i < 3; ++i) { Hearts[i] = Find(kHeartNames[i]); }

	// Курсоры-подсветки 4223 и 4233 (`AsdaAnimatedPlane`).
	EquipSel = Find(TEXT("OTHERCHARA_EQUIPSEL_38_0"));
	SowelSel = Find(TEXT("OTHERCHARA_SowelSel_89_0"));
	SetShown(EquipSel, false);
	SetShown(SowelSel, false);

	// 12 строк заданий и по две иконки у каждой. В InitControls эталон чистит текст и прячет
	//   ОБЕ иконки каждой строки.
	for (int32 m = 0; m < kQuestRows; ++m)
	{
		QuestRows[m] = Cast<UTextBlock>(Find(*FString::Printf(TEXT("OtherChara_Qst%02d"), m + 1)));
		if (QuestRows[m])
		{
			QuestRows[m]->SetAutoWrapText(false);
			QuestRows[m]->SetText(FText::GetEmpty());
		}
		// Индекс плана в имени идёт с шагом 3 от 44 (n) и 45 (p) — снят дампом WBP.
		QuestIconN[m] = Find(*FString::Printf(TEXT("OtherChara_QstIcon%02dn_%d_0"), m + 1, 44 + 3 * m));
		QuestIconP[m] = Find(*FString::Printf(TEXT("OtherChara_QstIcon%02dp_%d_0"), m + 1, 45 + 3 * m));
		SetShown(QuestIconN[m], false);
		SetShown(QuestIconP[m], false);
	}

	// Тексты количества у 11 слотов надетого (эталон пишет их в ТОТ ЖЕ контрол, что и иконку;
	//   импортёр развёл иконку и текст в два узла — `OTHERCHARA_EQUIPnn_XX_0` и `OTHERCHARA_EQUIPnn`).
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		EquipQty[S] = Cast<UTextBlock>(Find(*FString::Printf(TEXT("OTHERCHARA_EQUIP%02d"), S)));
		if (EquipQty[S]) { EquipQty[S]->SetAutoWrapText(false); EquipQty[S]->SetText(FText::GetEmpty()); }
	}

	// 4 иконки самоцветов 4227…4230 и 4 затемнителя 4234…4237 (индексы планов — файловые константы).
	for (int32 j = 0; j < kSowelSlots; ++j)
	{
		SowelIcons[j] = Cast<UImage>(Find(*SowelCellName(j)));
		SowelDims[j]  = Find(*FString::Printf(TEXT("OTHERCHARA_SubSowelDim%d_%d_0"), j + 1, kDimPlane[j]));
		SetShown(SowelIcons[j], true);    // `SetVisible(win[k+31], 1)` в InitControls
	}

	// Кнопка закрытия 4201.
	if (UButton* B = Cast<UButton>(Find(TEXT("OTHERCHARA_DATACLOSE"))))
	{
		B->OnClicked.Clear();
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Callback = [this](int32) { OnCloseClicked(); };
		B->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, B);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// ── Зоны наведения поверх ячеек. Общая точка порта `UAsdaIconCellGrid` (второй копии
	//    клик-оверлея не заводим — закон `port-shared-points`). Иконки слотов рисует ОНА же
	//    (`CodeProvider` → `ApplyItemIcon`), самоцветы — тоже: их коды лежат в `sowell_list`,
	//    и `GetItemIcon` уводит их в `FromSowelIconPath`, ровно как эталонный
	//    `CItemManager_GetSowelRecord->iconPath`.
	TArray<FName> EquipCells;
	for (int32 S = 0; S < kEquipSlots; ++S) { EquipCells.Add(FName(*EquipCellName(S))); }
	EquipGrid = NewObject<UAsdaIconCellGrid>(this);
	EquipGrid->SetOverlayTag(TEXT("OCEQUIP"));
	EquipGrid->SetCellSizePx(32.f);            // .JUI: слоты 32×32 (дамп WBP подтверждает)
	EquipGrid->bClearEmptyCells = true;        // эталон гасит пустой слот дефолт-текстурой
	EquipGrid->SetHoverHandlers(
		[this](int32 Cell) { OnEquipHover(Cell); },
		[this]()           { OnEquipUnhover(); });
	// ★ 30.10.2026: щелчок по слоту — эталонный msg 0xB (`uiWindowSlot_OnHoverShowIcon`), прежний комментарий
	//   «клика у эталона НЕТ» был ошибкой: msg 0xB и есть одиночное нажатие ЛКМ (разбор — заголовок, `OnEquipClick`).
	EquipGrid->SetupNamed(Win, EquipCells, /*bMale*/true,
		[this](int32 Cell) { return SlotItemId(Cell); },
		[this](int32 Cell) { OnEquipClick(Cell); });

	TArray<FName> SowelCells;
	for (int32 j = 0; j < kSowelSlots; ++j) { SowelCells.Add(FName(*SowelCellName(j))); }
	SowelGrid = NewObject<UAsdaIconCellGrid>(this);
	SowelGrid->SetOverlayTag(TEXT("OCSOWEL"));
	SowelGrid->SetCellSizePx(32.f);
	SowelGrid->bClearEmptyCells = true;
	SowelGrid->SetHoverHandlers(
		[this](int32 Cell) { OnSowelHover(Cell); },
		[this]()           { OnSowelUnhover(); });
	SowelGrid->SetupNamed(Win, SowelCells, /*bMale*/true,
		[this](int32 Cell) { return (HoverSlot >= 0) ? SlotSowelId(HoverSlot, Cell) : 0; },
		/*OnClick*/ [](int32) {});

	bControlsReady = true;
	UE_LOG(LogAsdaOtherChara, Log, TEXT("[чужой персонаж] контролы окна 4200 разобраны"));
}

// ═══════════════════════════ ЗАПОЛНЕНИЕ ═══════════════════════════════════════════════════════

bool UAsdaOtherCharaSubsystem::GetTarget(FString& OutName, int32& OutClassId, int32& OutSubClassId,
	int32& OutLevel, int32& OutConstellation, bool& bOutMale) const
{
	const UAsdaWorldSubsystem* WS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	if (!WS) { return false; }
	// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] окно о себе: те же поля актора (`char+1056/1080/1081/1082/1120/1079`) — из своих зеркал.
	if (bSelfTarget)
	{
		const UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>();
		if (!Net) { return false; }
		OutName          = WS->GetSelfCharName();
		OutClassId       = FMath::Max(0, Net->SelfClassId);
		OutSubClassId    = FMath::Max(0, Net->SelfSubJob);
		OutLevel         = AsdaSelf::GetLevel(Net);
		OutConstellation = (int32)Net->SelfConstellation;
		bOutMale         = (WS->SelfSex != 2);
		return true;
	}
	int32 ClanId = -1;
	if (!WS->GetNetPlayerInfo(TargetUserNum, OutName, OutClassId, OutLevel, ClanId)) { return false; }
	OutSubClassId = 0; OutConstellation = 0;
	WS->GetNetPlayerClassExtra(TargetUserNum, OutSubClassId, OutConstellation);
	// Пол цели (`char+1079`) эталон передаёт в `CItemManager_GetImgOptionPtr` — от него зависит,
	//   какой вариант иконки предмета взять. Берём из реестра соседей, а не угадываем.
	int32 Sex = 1;
	WS->GetNetPlayerSex(TargetUserNum, Sex);
	bOutMale = (Sex != 2);
	return true;
}

// Порт `CPlayerInfoWin_SetData@0xDA7E30`.
void UAsdaOtherCharaSubsystem::SetData()
{
	FString Name; int32 ClassId = 0, SubClassId = 0, Level = 0, Sign = 0; bool bMale = true;
	// 1:1 `win[235] = CharManager_FindByUserIdx(...); if (win[235]) {...}` — цели нет ⇒ окно
	//   не заполняется и НЕ ПОКАЗЫВАЕТСЯ (строки отказа у эталона нет, см. §5 шапки).
	if (!GetTarget(Name, ClassId, SubClassId, Level, Sign, bMale))
	{
		UE_LOG(LogAsdaOtherChara, Warning,
			TEXT("[чужой персонаж] ответ пришёл, но цель userNum=%d уже вне зоны — окно не показываем (1:1)"),
			TargetUserNum);
		return;
	}
	if (!EnsureWidget()) { return; }

	UGameInstance* GI = GetGameInstance();

	// ── Имя (4202) ──
	if (TxtName) { TxtName->SetText(FText::FromString(Name)); }

	// ── Уровень (4207): `sprintf("%s %s ", FormatThousands(level), loc 12215)` ──
	if (TxtLevel)
	{
		TxtLevel->SetText(FText::FromString(
			FString::Printf(TEXT("%s %s "), *AsdaUi::FormatThousands(Level), *Loc(OcLoc::kLvShort))));
	}

	// ── Класс (4209): подкласс, если он не нуль, иначе базовый класс ──
	//   Эталон `sub_926C05` = `JobType.BIN + 64*id`; в порту та же таблица — `DT_jobtype`.
	if (TxtClass)
	{
		FString ClassName;
		if (const UAsdaSkillDatabase* Sk = GI ? GI->GetSubsystem<UAsdaSkillDatabase>() : nullptr)
		{
			ClassName = Sk->GetJobName((SubClassId != 0) ? SubClassId : ClassId);
		}
		TxtClass->SetText(FText::FromString(ClassName));
	}

	// ── Знак зодиака и влечение (4216 + три сердца) ──
	//   ★ Порядок эталона важен: сперва считается ВЛЕЧЕНИЕ (оно же перетирает буфер, в котором
	//     до этого лежало имя класса), затем — сердца, и только потом строка «знак | влечение».
	int32 MatchIdx = 2;
	FString MatchText;
	FString SignName;
	if (const UAsdaConstellationSubsystem* Con = GI ? GI->GetSubsystem<UAsdaConstellationSubsystem>() : nullptr)
	{
		MatchIdx = Con->MatchIndex(Con->SelfSign(), (uint8)Sign, MatchText);
		SignName = Con->SignName((uint8)Sign);
	}
	// Все три гасим, зажигаем РОВНО одну (`win[factionIdx + 25]`).
	for (int32 i = 0; i < 3; ++i) { SetShown(Hearts[i], false); }
	if (Hearts.IsValidIndex(MatchIdx)) { SetShown(Hearts[MatchIdx], true); }
	if (TxtSign)
	{
		TxtSign->SetText(FText::FromString(FString::Printf(TEXT("%s | %s"), *SignName, *MatchText)));
	}

	// ── Клан (4211): 17-байтная строка ПАКЕТА (эталонное `sprintf("%s", &a2 + 3)`) ──
	if (TxtClan) { TxtClan->SetText(FText::FromString(Shown.ClanName)); }

	// ── Панель самоцветов в исходное состояние + курсоры скрыты ──
	SetShown(EquipSel, false);
	SetShown(SowelSel, false);
	HoverSlot = -1;
	ResetSowelPanel();

	// ── Иконки надетого ──
	if (EquipGrid) { EquipGrid->SetMale(bMale); }
	RefreshEquipSlots();

	// ── Список заданий ──
	const bool bLegacyIdx = (CVarLegacyQuestIndex.GetValueOnGameThread() != 0);
	const UAsdaQuestMarkerSubsystem* QM = GI ? GI->GetSubsystem<UAsdaQuestMarkerSubsystem>() : nullptr;
	int32 QuestSlot = 0;
	for (int32 k = 0; k < kQuestRows; ++k)
	{
		// Эталон чистит строку и обе её иконки ДО проверки названия — по индексу `k`.
		if (QuestRows.IsValidIndex(k) && QuestRows[k]) { QuestRows[k]->SetText(FText::GetEmpty()); }
		SetShown(QuestIconN[k], false);
		SetShown(QuestIconP[k], false);

		const int32 KeyForTitle = Shown.QuestKey.IsValidIndex(k) ? Shown.QuestKey[k] : 0;
		const FString Title = QM ? QM->EpisodeTitleByQuestKey(KeyForTitle) : FString();
		if (Title.IsEmpty()) { continue; }   // 1:1 `if (questTitle)` — строка пропускается

		// D-OC1: эталон берёт стадию и цвет по `questSlot`, а название — по `k`.
		const int32 DataIdx = bLegacyIdx ? QuestSlot : k;
		const int32 KeyForQct = Shown.QuestKey.IsValidIndex(DataIdx) ? Shown.QuestKey[DataIdx] : 0;
		const int32 StageOfTarget = Shown.QuestStage.IsValidIndex(DataIdx) ? Shown.QuestStage[DataIdx] : 0;

		int32 QctStage = 0, QctField132 = 0;
		if (QM) { QM->GetQuestCompleteRawPair(KeyForQct, QctStage, QctField132); }

		FString Text = Title;
		if (QctStage == StageOfTarget)
		{
			Text = FString::Printf(TEXT("[%s]%s"), *Loc(OcLoc::kQuestDone), *Title);
		}

		UTextBlock* Row = QuestRows.IsValidIndex(QuestSlot) ? QuestRows[QuestSlot].Get() : nullptr;
		if (Row)
		{
			FColor C = kQuestColorN;
			bool bSpecialIcon = false;
			switch (QctField132)
			{
				case 1: C = kQuestColor1; bSpecialIcon = true; break;
				case 2: C = kQuestColor2; bSpecialIcon = true; break;
				case 3: C = kQuestColor3; bSpecialIcon = true; break;
				default: break;   // 1:1: любое другое значение — «обычный квест»
			}
			Row->SetColorAndOpacity(FSlateColor(FLinearColor(C)));
			Row->SetText(FText::FromString(Text));
			SetShown(QuestIconN[QuestSlot], !bSpecialIcon);
			SetShown(QuestIconP[QuestSlot], bSpecialIcon);
		}
		++QuestSlot;
	}

	// 1:1 финал эталона: `CControl_SetVisible(win[1], 1)`.
	Win->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UE_LOG(LogAsdaOtherChara, Log,
		TEXT("[чужой персонаж] окно 4200 заполнено: «%s», ур. %d, класс %d/%d, знак %d (влечение %d), "
		     "клан «%s», строк заданий %d"),
		*Name, Level, ClassId, SubClassId, Sign, MatchIdx, *Shown.ClanName, QuestSlot);
}

// ═══════════════════════════ ЭКИПИРОВКА И САМОЦВЕТЫ ═══════════════════════════════════════════

int32 UAsdaOtherCharaSubsystem::SlotItemId(int32 Slot) const
{
	if (Slot < 0 || Slot >= kEquipSlots) { return 0; }
	if (Shown.Gear.Num() < kRecSize * (Slot + 1)) { return 0; }
	const int32 Id = AsdaBytes::RdI32(&Shown.Gear[kRecSize * Slot]);
	return (Id > 0) ? Id : 0;   // 1:1 гейт эталона `*pSlotItemId > 0`
}

int32 UAsdaOtherCharaSubsystem::SlotSowelId(int32 Slot, int32 j) const
{
	if (Slot < 0 || Slot >= kEquipSlots || j < 0 || j >= kSowelSlots) { return 0; }
	const int32 Ofs = kRecSize * Slot + kSowelOfsInRec + 2 * j;
	if (Shown.Gear.Num() < Ofs + 2) { return 0; }
	return (int32)AsdaBytes::RdI16(&Shown.Gear[Ofs]);
}

TArray<uint8> UAsdaOtherCharaSubsystem::SlotRecord(int32 Slot) const
{
	TArray<uint8> Out;
	if (Slot < 0 || Slot >= kEquipSlots) { return Out; }
	if (Shown.Gear.Num() < kRecSize * (Slot + 1)) { return Out; }
	Out.SetNumUninitialized(kRecSize);
	FMemory::Memcpy(Out.GetData(), &Shown.Gear[kRecSize * Slot], kRecSize);
	return Out;
}

// Порт `uiRefreshItemOptionSlots@0xDA77E0`.
void UAsdaOtherCharaSubsystem::RefreshEquipSlots()
{
	if (EquipGrid) { EquipGrid->Refresh(); }   // иконки всех 11 слотов (пустые гасятся)

	// Текст количества эталон пишет ТОЛЬКО у слота 10 (`if (slotIdx == 10)`), у остальных
	//   ставит пустую строку; пустой слот тоже получает пустую строку.
	const bool bEnchantText = (CVarOtherCharaEnchantText.GetValueOnGameThread() != 0);
	const UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		UTextBlock* T = EquipQty.IsValidIndex(S) ? EquipQty[S].Get() : nullptr;
		if (!T) { continue; }
		const int32 Id = SlotItemId(S);
		// ★ 08.11.2026 [ИСПРАВЛЕНИЕ] (`asda.OtherChara.EnchantText`): надпись — по правилу НАДЕТОЙ ячейки сумки, общей точкой
		//   `AsdaEquipCellText::Build` («+N%» при записи+24 != 0, слот 10 — количество). Гейт эталона на иконку тот же:
		//   `*pSlotItemId > 0` и запись `item_list` есть, иначе пустая строка.
		if (bEnchantText)
		{
			const TArray<uint8> Rec = SlotRecord(S);
			uint32 Argb = 0;
			FString Txt;
			if (Id > 0 && Rec.Num() == kRecSize && IT && IT->GetItem(Id))
			{
				FAsdaItemInstance It;
				It.SetRaw(Rec.GetData(), Rec.Num());
				Txt = AsdaEquipCellText::Build(S, It, Argb);
			}
			T->SetText(Txt.IsEmpty() ? FText::GetEmpty() : FText::FromString(Txt));
			if (Argb != 0)   // закон `ui-srgb-gamma-rule`: цвет из данных — sRGB, UMG ждёт linear
			{
				T->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbHexToLinear(FString::Printf(TEXT("%08X"), Argb))));
			}
			continue;
		}
		if (S == kEquipSlots - 1 && Id > 0 && Shown.Gear.Num() >= kRecSize * S + 13)
		{
			// `sprintf("%d", *(int*)((char*)pSlotItemId + 9))` — количество лежит в записи на +9.
			T->SetText(FText::AsNumber(AsdaBytes::RdI32(&Shown.Gear[kRecSize * S + 9])));
		}
		else
		{
			T->SetText(FText::GetEmpty());
		}
	}
}

// «Предмета нет»: 4 иконки — дефолт, все 4 затемнителя видимы (эталон делает ровно это и в
//   `CPlayerInfoWin_SetData`, и в ветке `ItemRecord == nullptr` обработчика наведения).
void UAsdaOtherCharaSubsystem::ResetSowelPanel()
{
	for (int32 j = 0; j < kSowelSlots; ++j)
	{
		if (UImage* I = SowelIcons.IsValidIndex(j) ? SowelIcons[j].Get() : nullptr)
		{
			UAsdaIconCellGrid::ApplyItemIcon(this, I, /*Code*/0, /*bMale*/true, 32.f, 32.f, /*bClearIfEmpty*/true);
		}
		SetShown(SowelDims.IsValidIndex(j) ? SowelDims[j].Get() : nullptr, true);
	}
}

// Порт `itemSlotDrawIconAndSowels@0xDA96B0`.
void UAsdaOtherCharaSubsystem::DrawSowelsForSlot(int32 Slot)
{
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!IT) { return; }
	const int32 ItemId = SlotItemId(Slot);
	const FAsdaItemRecord* R = IT->GetItem(ItemId);
	// 1:1: эталон разыменовывает запись без проверки; сюда он приходит только с валидной записью.
	if (!R) { ResetSowelPanel(); return; }

	// `itemGetCategoryById(code) == 1` — «прочее из item_list»: у такого предмета гнёзд нет,
	//   все четыре гасятся заглушкой.
	if (IT->GetItemCategory(R->ItemCode) == 1)
	{
		ResetSowelPanel();
		return;
	}

	for (int32 j = 0; j < kSowelSlots; ++j)
	{
		const int32 SowelId = SlotSowelId(Slot, j);
		if (UImage* I = SowelIcons.IsValidIndex(j) ? SowelIcons[j].Get() : nullptr)
		{
			// Код самоцвета живёт в `sowell_list`; общая точка `ApplyItemIcon` сама уводит его
			//   в `FromSowelIconPath` — это и есть эталонный `SowelRecord->iconPath`.
			//   Нет записи ⇒ дефолт (эталонный `CClass_GetSubObjectAtFixedOff`).
			UAsdaIconCellGrid::ApplyItemIcon(this, I, SowelId, /*bMale*/true, 32.f, 32.f, /*bClearIfEmpty*/true);
		}
		// 1:1 гейт затемнителя: `j > enchantOrSocket || enchantOrSocket >= 4`.
		const bool bDim = (j > R->EnchantOrSocket) || (R->EnchantOrSocket >= 4);
		SetShown(SowelDims.IsValidIndex(j) ? SowelDims[j].Get() : nullptr, bDim);
	}
}

// ═══════════════════════════ НАВЕДЕНИЕ ════════════════════════════════════════════════════════

// Эталон msg 8 → `npcTradeWndOnHover@0xDA98C0`: ТОЛЬКО подсказка предмета (гейт `!helper_getField1` — пока
//   предмет «в руке», подсказки нет). Курсор 4223 и самоцветы — щелчок (`OnEquipClick`, msg 0xB). При ключе
//   `asda.OtherChara.SelectOnClick 0` наведение выполняет и ветку msg 0xB, как порт до 30.10.2026.
void UAsdaOtherCharaSubsystem::OnEquipHover(int32 Slot)
{
	if (Slot < 0 || Slot >= kEquipSlots) { return; }
	const bool bLegacySelect = (CVarOtherCharaSelectOnClick.GetValueOnGameThread() == 0);
	if (bLegacySelect)
	{
		// Прежний порт: ветка msg 0xB на наведении. Окно могла закрыть ветка «цель ушла» — тогда подсказки нет.
		OnEquipClick(Slot);
		if (!IsOpen()) { return; }
	}
	// Гейт эталона `if (!helper_getField1(g_pUIManager))` — порт поля: общий реестр призрака захвата.
	if (FAsdaDragGhost::IsAnyActive()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;

	// 1:1: цель ушла из зоны ⇒ гасим плашку и ЗАКРЫВАЕМ окно (`npcTalkWndHide`).
	if (!bSelfTarget && (!WS || !WS->GetPlayerActorByUserNum(TargetUserNum)))   // ★ 08.11.2026: о себе — цель всегда «в зоне»
	{
		if (Tip) { Tip->HideTooltipFor(EAsdaTipOwner::OtherChara); }
		UE_LOG(LogAsdaOtherChara, Log,
			TEXT("[чужой персонаж] цель userNum=%d вышла из зоны — окно закрывается (1:1 эталон)"),
			TargetUserNum);
		CloseWindow();
		return;
	}

	// Тултип предмета (msg 8). Эталонный гейт `itemCode` в диапазоне item_list выполнен самим
	//   фактом того, что `GetItem` вернула запись — вне диапазона её не существует. Нет записи — эталон молчит.
	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	if (!Tip || !IT || !IT->GetItem(SlotItemId(Slot))) { return; }
	const TArray<uint8> Rec = SlotRecord(Slot);
	if (Rec.Num() == kRecSize)
	{
		FAsdaItemInstance It;
		It.SetRaw(Rec.GetData(), Rec.Num());
		// Эталон зовёт `ItemTooltip_Build(…, −1, 0, 0, 1, 0, 0)` — ПОЛНЫЙ билдер без
		//   подавления сравнения, ровно как окно «хвастовства» для чужой вещи.
		Tip->ShowItemTooltip(It, /*bSuppressCompare*/false, EAsdaTipOwner::OtherChara, /*SourceKind*/1);
	}
}

// Эталон msg 0xB → `uiWindowSlot_OnHoverShowIcon@0xDA7C00` (имя — мисномер IDA: это обработчик НАЖАТИЯ ЛКМ по
//   слоту): курсор EQUIPSEL 4223 на слот, `win[198]` = слот, иконки и затемнители гнёзд самоцветов. Подсказку не трогает.
void UAsdaOtherCharaSubsystem::OnEquipClick(int32 Slot)
{
	if (Slot < 0 || Slot >= kEquipSlots) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;

	// 1:1: цель ушла из зоны ⇒ гасим плашку и ЗАКРЫВАЕМ окно (`npcTalkWndHide`).
	if (!bSelfTarget && (!WS || !WS->GetPlayerActorByUserNum(TargetUserNum)))   // ★ 08.11.2026: о себе — цель всегда «в зоне»
	{
		if (UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::OtherChara);
		}
		UE_LOG(LogAsdaOtherChara, Log,
			TEXT("[чужой персонаж] цель userNum=%d вышла из зоны — окно закрывается (1:1 эталон)"),
			TargetUserNum);
		CloseWindow();
		return;
	}

	UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	const FAsdaItemRecord* R = IT ? IT->GetItem(SlotItemId(Slot)) : nullptr;
	if (!R)
	{
		// Ветка эталона `ItemRecord == nullptr`: панель самоцветов в исходное, курсор слота гасится.
		ResetSowelPanel();
		SetShown(EquipSel, false);
		HoverSlot = -1;
		return;
	}

	// Курсор-подсветка 4223 встаёт РОВНО на слот. Эталон: `Class_GetVec2_74(слот)` →
	//   `CControl_SetField74_75(win[21], x, y)`. У нас курсор и иконки слотов лежат в ОДНОМ
	//   канвасе (`OTHERCHARA_EQUIPWIN_grp`, подтверждено дампом), поэтому позиция слота
	//   переносится напрямую, без пересчёта систем координат.
	if (EquipSel && EquipGrid)
	{
		if (const UWidget* Cell = Win ? Win->GetWidgetFromName(FName(*EquipCellName(Slot))) : nullptr)
		{
			if (const UCanvasPanelSlot* From = AsdaUi::SlotOf(Cell))
			{
				if (UCanvasPanelSlot* To = AsdaUi::SlotOf(EquipSel)) { To->SetPosition(From->GetPosition()); }
			}
		}
		SetShown(EquipSel, true);
	}
	HoverSlot = Slot;
	DrawSowelsForSlot(Slot);
	UE_LOG(LogAsdaOtherChara, Log, TEXT("[чужой персонаж] щелчок по слоту %d: курсор 4223 и самоцветы слота (msg 0xB)"), Slot);
	// ⚠ `SowelGrid->Refresh()` здесь НЕ зовём: иконки гнёзд уже нарисовал `DrawSowelsForSlot` —
	//   это 1:1 порт `itemSlotDrawIconAndSowels`, который рисует иконку И решает судьбу
	//   затемнителя одним проходом. Сетка гнёзд нужна только ради зон наведения (тултип самоцвета);
	//   второй отрисовки тех же четырёх картинок быть не должно.
}

void UAsdaOtherCharaSubsystem::OnEquipUnhover()
{
	// msg 0xA эталона: `CControl_KillFocus(&g_ItemTooltip)`. Гасим ТОЛЬКО свою плашку
	//   (закон `ui-shared-tip-ownership`).
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::OtherChara);
	}
	// ⚠ Курсор 4223 эталон при уходе с ячейки НЕ гасит: он остаётся на последнем слоте, пока не
	//   наведёшься на другой или пока предмет не окажется пустым. Воспроизводим 1:1.
}

void UAsdaOtherCharaSubsystem::OnSowelHover(int32 Index)
{
	if (HoverSlot < 0 || Index < 0 || Index >= kSowelSlots) { return; }
	const int32 SowelId = SlotSowelId(HoverSlot, Index);
	UAsdaItemTableSubsystem* IT = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
	FAsdaSowellRow Row;
	// 1:1 гейт эталона: тултип показывается ТОЛЬКО если запись самоцвета существует.
	if (!IT || !IT->GetSowell(SowelId, Row)) { return; }
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		// Эталон: `CControl_SetState(&g_ItemTooltip, 1, sowelId, 0, 1, −1, 0)` — построитель
		//   самоцвета. В порту в него уводит `ShowItemTooltip` по признаку `IsSowelCode`.
		Tip->ShowStampSlotTooltip(SowelId, EAsdaTipOwner::OtherChara);
	}
}

void UAsdaOtherCharaSubsystem::OnSowelUnhover()
{
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::OtherChara);
	}
}

// ═══════════════════════════ ЗАКРЫТИЕ ═════════════════════════════════════════════════════════

// Кнопка 4201 и msg 21 самого окна — у эталона одно и то же тело.
void UAsdaOtherCharaSubsystem::OnCloseClicked()
{
	CloseWindow();
}

void UAsdaOtherCharaSubsystem::CloseWindow()
{
	if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
	{
		Tip->HideTooltipFor(EAsdaTipOwner::OtherChara);   // `CControl_KillFocus(&g_ItemTooltip)`
	}
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterWindowCloser(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	ResetWidget();
}

// ═══════════════════════════ ДИАГНОСТИКА ══════════════════════════════════════════════════════

FString UAsdaOtherCharaSubsystem::DumpControls() const
{
	FString Out = TEXT("=== OTHERCHARA_DATA 4200: разобранные контролы ===\n");
	if (!Win) { return Out + TEXT("окно не открыто\n"); }
	auto Line = [&Out](const TCHAR* Id, const TCHAR* Name, const UWidget* W)
	{
		Out += FString::Printf(TEXT("  %-6s %-34s %s\n"), Id, Name, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("4202"), TEXT("OTHERCHARA_DATATXT00 имя"),     TxtName);
	Line(TEXT("4207"), TEXT("OTHERCHARA_DATATXT04 уровень"), TxtLevel);
	Line(TEXT("4209"), TEXT("OTHERCHARA_DATATXT06 класс"),   TxtClass);
	Line(TEXT("4211"), TEXT("OTHERCHARA_DATATXT08 клан"),    TxtClan);
	Line(TEXT("4216"), TEXT("OTHERCHARA_DATATXT11 знак"),    TxtSign);
	Line(TEXT("4223"), TEXT("OTHERCHARA_EQUIPSEL"),          EquipSel);
	Line(TEXT("4233"), TEXT("OTHERCHARA_SowelSel"),          SowelSel);
	for (int32 i = 0; i < Hearts.Num(); ++i)
	{
		Line(*FString::FromInt(4217 + i), *FString::Printf(TEXT("сердце %d"), i + 1), Hearts[i]);
	}
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		Line(*FString::FromInt(4240 + S), *FString::Printf(TEXT("текст количества слота %d"), S), EquipQty[S]);
	}
	for (int32 j = 0; j < kSowelSlots; ++j)
	{
		Line(*FString::FromInt(4227 + j), *FString::Printf(TEXT("иконка самоцвета %d"), j + 1), SowelIcons[j]);
		Line(*FString::FromInt(4234 + j), *FString::Printf(TEXT("затемнитель %d"), j + 1),      SowelDims[j]);
	}
	for (int32 m = 0; m < kQuestRows; ++m)
	{
		Line(*FString::FromInt(4251 + 3 * m), *FString::Printf(TEXT("строка задания %02d"), m + 1), QuestRows[m]);
		Line(*FString::FromInt(4252 + 3 * m), *FString::Printf(TEXT("иконка обычного %02d"), m + 1), QuestIconN[m]);
		Line(*FString::FromInt(4253 + 3 * m), *FString::Printf(TEXT("иконка особого  %02d"), m + 1), QuestIconP[m]);
	}
	return Out;
}

FString UAsdaOtherCharaSubsystem::DumpState() const
{
	FString Out = FString::Printf(
		TEXT("=== OTHERCHARA_DATA 4200: состояние ===\n")
		TEXT("  цель userNum=%d, ждём ответ=%s, окно=%s, контролы=%s, слот под курсором=%d\n")
		TEXT("  очередь недособранных половин: %d (потолок %d)\n"),
		TargetUserNum, bAwaitingReply ? TEXT("да") : TEXT("нет"),
		IsOpen() ? TEXT("открыто") : TEXT("закрыто"),
		bControlsReady ? TEXT("разобраны") : TEXT("НЕТ"), HoverSlot,
		Pending.Num(), kMaxPendingHalves);
	for (const FAsdaOtherCharaRecord& P : Pending)
	{
		Out += FString::Printf(TEXT("    ключ %d: 5479=%s 7006=%s, положено на %.1f с\n"),
			P.PairKey, P.bHasCharHalf ? TEXT("да") : TEXT("нет"),
			P.bHasGearHalf ? TEXT("да") : TEXT("нет"), P.StampSec);
	}
	if (!bHasShown) { return Out + TEXT("  показанной записи нет\n"); }
	Out += FString::Printf(
		TEXT("  показано: ключ %d, клан «%s», служебные +0=%d +2=%d +120=%d\n"),
		Shown.PairKey, *Shown.ClanName, Shown.Svc0, Shown.Svc2, Shown.Svc120);
	for (int32 S = 0; S < kEquipSlots; ++S)
	{
		Out += FString::Printf(TEXT("    слот %2d: itemId=%d, самоцветы %d/%d/%d/%d\n"),
			S, SlotItemId(S), SlotSowelId(S, 0), SlotSowelId(S, 1), SlotSowelId(S, 2), SlotSowelId(S, 3));
	}
	for (int32 k = 0; k < kQuestRows; ++k)
	{
		Out += FString::Printf(TEXT("    задание %2d: id=%d ключ=%d стадия=%d\n"), k,
			Shown.QuestId.IsValidIndex(k) ? Shown.QuestId[k] : 0,
			Shown.QuestKey.IsValidIndex(k) ? Shown.QuestKey[k] : 0,
			Shown.QuestStage.IsValidIndex(k) ? Shown.QuestStage[k] : 0);
	}
	return Out;
}
