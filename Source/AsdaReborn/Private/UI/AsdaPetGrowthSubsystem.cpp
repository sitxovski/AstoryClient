// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна 44200 «Рост питомца» (лимит-брейк).
//   Весь реверс с адресами — в шапке `UI/AsdaPetGrowthSubsystem.h` и в
//   `Reports/pet_windows_re_report.md` §4.3, §4.5, §6.4, §6.6.
#include "UI/AsdaPetGrowthSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-c (14.11.2026): области замера времени игрового потока
#include "UI/AsdaMessageSubsystem.h"   // ★ R1: прямое включение (раньше приходило транзитивно)
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaInventorySubsystem.h"
#include "UI/AsdaItemTooltipSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetCageSubsystem.h"
#include "Items/AsdaItemTableSubsystem.h"
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"
#include "AsdaNetSubsystem.h"
#include "AsdaBytes.h"
#include "Login/AsdaButtonRelay.h"
#include "Login/AsdaLoginData.h"   // FAsdaLoginData::EncodeCp1251 — имя питомца в синтезе 6149

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "AsdaOutgoingGate.h"               // ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)

DEFINE_LOG_CATEGORY_STATIC(LogAsdaPetGrowth, Log, All);

// ── Идентификатор и путь окна ──
static const FName  kGrowthWindowId(TEXT("Pet_LimitBreak_Win"));
static const TCHAR* kGrowthWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Pet/WBP_Pet_LimitBreak_Win.WBP_Pet_LimitBreak_Win_C");

// ══════════════════════════════════════════════════════════════════════════════════════════════
//  LOC-ИДЕНТИФИКАТОРЫ. Каждый снят по СВОЕМУ обработчику эталона (закон
//  `loc-per-handler-not-by-family`), файл:строка — рядом с каждой строкой.
// ══════════════════════════════════════════════════════════════════════════════════════════════
namespace PetGrowthLoc
{
	// ── Подписи окна — `uiInitTradeWindow@0xEE12F0` (ui_core.c:90495-90592) ──
	constexpr int32 kTitle       = 109930;  // :90511  44201 «Рост питомца»
	constexpr int32 kLvlLabel    = 110650;  // :90519  44204 «Текущий уровень»
	constexpr int32 kNewLvlLabel = 110651;  // :90522  44210 «Изменение уровня» ★ коллизия имён
	constexpr int32 kPotionLabel = 109932;  // :90528  44107 «Зелье расширения возможностей питомца»
	constexpr int32 kBtnOkLabel  = 109933;  // :90533  44232 «Развитие»
	constexpr int32 kBtnNoLabel  = 109934;  // :90536  44233 «Закрыть»
	constexpr int32 kTalisman    = 110409;  // :90540  44242 «Талисман защиты опыта питомца»

	// ── Открытие — `uiUpgradeDialog_Open@0xEE18E0` (ui_core.c:90605-90650) ──
	constexpr int32 kGateSummoned = 109975; // :90614  ГЕЙТ: питомец призван
	constexpr int32 kHint         = 109936; // :90625  44231 подсказка после открытия
	constexpr int32 kGateMaxLevel = 109946; // :90645  ГЕЙТ: уровень ≥ 10

	// ── Перерисовка — `uiUpgradeDialog_RefreshItemInfo@0xEE2A70` (ui_core.c:90700-90780) ──
	constexpr int32 kCanBreak    = 109945;  // :90736  44203 «Можно расширить возможности»
	constexpr int32 kNotEnough   = 109944;  // :90738  44203 «Недостаточно опыта»
	constexpr int32 kCapText     = 109935;  // :90747  44206 «Предел» (красный)

	// ── Проверка по «ОК» — `uiUpgradeDialogRefreshItemInfo@0xEE17B0` (ui_core.c:90565-90598) ──
	constexpr int32 kConfirm71   = 109939;  // :90580  msgbox 71 «Если расширение не удастся…»
	constexpr int32 kTooYoung    = 109938;  // :90584 И :90586 — нотис A И подпись 44231 сразу
	constexpr int32 kNoPotion    = 109937;  // :90592  «Необходимо зелье нового уровня питомца»
	constexpr int32 kMsg71Ok     = 108510;  // item_inventory.c:51110 кнопка «ОК» msgbox 70/71
	constexpr int32 kMsg71No     = 108511;  // item_inventory.c:51113 кнопка «Отмена»

	// ── Подсказка по наведению на 44242 — `uiUpgradeDialogWndProc` case 8 (ui_core.c:90826-90830) ──
	constexpr int32 kTipTitle    = 110113;  // :90830  «Предел»
	constexpr int32 kTipBody     = 110643;  // :90828  «Этот свиток предотвращает понижение уровня…»

	// ── Приём 6149 — `pet_OnRecvItemOpResult@0xEE2370` (npc_mob_pet.c:7718-7886) ──
	constexpr int32 kDbError     = 109980;  // :7722  код 0
	constexpr int32 kGrandSuccess= 109940;  // :7819  код 1
	constexpr int32 kSuccess     = 109941;  // :7825  код 2
	constexpr int32 kFailTpl     = 110406;  // :7833/:7849 шаблон «Состояние…: [%s]. Талисман…»
	constexpr int32 kFailWord    = 110407;  // :7832  «Неудача» — подстановка в [%s]
	constexpr int32 kFail        = 109942;  // :7840  код 3 без талисмана
	constexpr int32 kDisasterWord= 110408;  // :7848  «Полный провал» — подстановка в [%s]
	constexpr int32 kDisaster    = 109943;  // :7856  код 4 без талисмана
	constexpr int32 kItemError   = 109981;  // :7869  коды 5,6,7,8,0xC,0xD,0xE,0xF
	constexpr int32 kNotMaxLevel = 109982;  // :7873  код 9
	constexpr int32 kIsMaxLevel  = 109983;  // :7877  код 0xA
	constexpr int32 kNoPetForItem= 109984;  // :7882  коды 0xB, 0x10

	// Кнопка однокнопочного нотиса (канал A) — loc 2288 «ОК» (item_inventory.c:50880).
	constexpr int32 kBtnOk = 2288;
}

// ★ ДЕФЕКТ ПД5 ОРИГИНАЛА (помечаем исправление, закон `fix-original-defects`).
//   Строка 110406 содержит ЛИТЕРАЛЬНЫЙ `[%s]`, и обработчик прямо над вызовом считает подстановку:
//   `itemRecordCopy[27] = UI_FormatLocalizedNotice(…, 110407 | 110408, …)` (npc_mob_pet.c:7832, :7848).
//   Но сам вызов в декомпиле — `sprintf(&noticeBuf, v9)` БЕЗ аргумента (:7834, :7850), то есть
//   посчитанная строка никуда не передаётся, а `%s` съедает мусор со стека. Два прочтения:
//   либо IDA потеряла vararg (для `sprintf` это частое), либо это настоящая ошибка издателя —
//   в обоих случаях ВОСПРОИЗВЕСТИ неопределённое поведение нельзя. Поэтому по умолчанию
//   подставляем 110407/110408 (замысел виден и по тексту строки, и по соседним строкам кода),
//   а ключ `asda.Pet.GrowthTalismanLiteral 1` возвращает дословную ветку декомпила — `[%s]` как есть.
static ASDA_DEV_CVAR(int32, CVarGrowthTalismanLiteral,
	TEXT("asda.Pet.GrowthTalismanLiteral"), 0,
	TEXT("0 — подставлять 110407/110408 в [%s] строки 110406 (замысел, дефект ПД5 исправлен);\n")
	TEXT("1 — дословная ветка декомпила: шаблон печатается без подстановки."),
	ECVF_Default);

// ═════════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════

void UAsdaPetGrowthSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// Событие 0x15 (`uiUpgradeDialogWndProc` case 0x15, ui_core.c:90822-90824) — закрыть себя по Esc.
		WM->RegisterEscCloser(kGrowthWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaPetGrowthSubsystem::CloseGrowth));
	}
	if (UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ событие в очередь.
		HPetEvent = P->OnPetEvent.AddLambda([this](int32 Op, int32 Res, int32 Uid)
		{
			PetEvents.Add(TTuple<int32, int32, int32>(Op, Res, Uid));
		});
	}
	UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] подсистема поднята (окно 44200)"));
}

void UAsdaPetGrowthSubsystem::Deinitialize()
{
	if (UAsdaPetSubsystem* P = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetSubsystem>() : nullptr)
	{
		P->OnPetEvent.Remove(HPetEvent);
	}
	ResetWidget();
	Super::Deinitialize();
}

void UAsdaPetGrowthSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	CloseGrowth();
	TargetUid = -1;
	bCanBreak = false;
	// ★ Признак «запрос в полёте» снимается ТОЛЬКО приходом 6149 (npc_mob_pet.c:7718) — а после
	//   пересоздания сокета ответа не будет вовсе, и без этого сброса окно осталось бы немым.
	SendLatch.Disarm();
	bProtectChecked = false;
	bConfirmOpen = false;
	// ★ Стенд — тоже вещь СЕССИИ: перехват сети нельзя оставить висеть после пересоздания сокета.
	if (bStandArmed)
	{
		if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
		bStandArmed = false;
	}
	bStandDone = false;
	StandLeft = 0.f;
	StandExpectUid = StandExpectBag = StandExpectSlot = StandExpectPremium = -1;
	Potion = FAsdaPetGrowthItemRef();
	Talisman = FAsdaPetGrowthItemRef();
	PetEvents.Reset();
	Mouse.Reset();
	if (bTipShown)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::PetGrowth);
		}
		bTipShown = false;
	}
	(void)Reason;
}

void UAsdaPetGrowthSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.PetGrowth");   // ★ R4-c (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPetEvents();

	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ТАЙМАУТ ЗАСОВА «6148 В ПОЛЁТЕ». Прежде засов снимался ТОЛЬКО
	//   приходом 6149 и сбросом сессии — промолчавший сервер делал кнопку 44232 мёртвой до
	//   переподключения (закон `no-state-latch-before-shown`). Крутим ВСЕГДА, а не только при
	//   открытом окне: окно могли закрыть с висящим запросом, и тогда засов пережил бы его.
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, S2): общая точка `FAsdaReplyLatch` — срок стенными часами, ключ читается на тике (было `Tick(DeltaTime)` — игровой тик).
	if (SendLatch.TickTimeout(AsdaPetUi::SendTimeoutSec()))
	{
		UE_LOG(LogAsdaPetGrowth, Warning,
			TEXT("[питомец-рост] ответа 6149 нет %.0f с — засов «запрос в полёте» снят, кнопка 44232 снова жива ")
			TEXT("(ключ asda.Pet.SendTimeoutSec; у оригинала таймаута нет, там обрыв = выход из клиента)"),
			AsdaPetUi::SendTimeoutSec());
	}

	// ★ Стенд отправки: перехват снимается ЗДЕСЬ, а не из колбэка — трогать лямбду сети изнутри
	//   неё самой нельзя. И обязательно снимается по таймауту: пока он взведён, в сеть не уходит
	//   НИЧЕГО (см. `StartSendStand`).
	if (bStandArmed)
	{
		StandLeft -= DeltaTime;
		if (bStandDone || StandLeft <= 0.f)
		{
			if (!bStandDone)
			{
				UE_LOG(LogAsdaPetGrowth, Error,
					TEXT("[питомец-рост] СТЕНД 6148: тело НЕ СНЯТО за отведённое время — пакет из очереди не ушёл"));
			}
			if (UAsdaPetSubsystem* P = Pets()) { P->EndSendCapture(); }
			bStandArmed = false;
			bStandDone = false;
		}
	}

	Mouse.Update();
	if (!bOpen || !bReady) { return; }
	TickTooltip();
}

// ═══════════════════════════════════ КАРКАС ОКНА ══════════════════════════════════════════════

bool UAsdaPetGrowthSubsystem::EnsureWindow()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kGrowthWindowId, kGrowthWbp, kGrowthZ).Ensure(
		GetGameInstance(), Win, bReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kGrowthWindowId, this, Win, bOpen,
				TEXT("Pet_LimitBreak_Win_0_%d"), 9);
		},
		[]()
		{
			UE_LOG(LogAsdaPetGrowth, Error, TEXT("[питомец-рост] WBP_Pet_LimitBreak_Win не загрузился (%s)"), kGrowthWbp);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaPetGrowthSubsystem::ResetWidget()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kGrowthWindowId);
		WM->CloseWindow(kGrowthWindowId);
	}
	Win = nullptr;
	TxtTitle = TxtPetName = TxtCanBreak = TxtLvlLabel = TxtLvlCur = TxtLvlNew = nullptr;
	TxtNewLabel = TxtPotLabel = TxtPotCount = TxtHint = TxtOk = TxtNo = nullptr;
	TxtTalisman = TxtTalCount = TxtMark1 = TxtMark2 = TxtMark3 = nullptr;
	BtnX = BtnOk = BtnNo = nullptr;
	ChkProtect = nullptr;
	ImgPetIcon = ImgMark1 = ImgMark2 = ImgMark3 = ImgGfont3 = nullptr;
	TalismanGrp = nullptr;
	Relays.Reset();
	bReady = false;
	bOpen = false;
}

// ★★★ РАЗБОР КОНТРОЛОВ. Порядок и состав — `uiInitTradeWindow@0xEE12F0` (ui_core.c:90495-90592),
//   номер за номером. Имена виджетов — из ЖИВОГО структурного дампа
//   `Reports/pet/ui_dump/44200_Pet_LimitBreak_Win.md` и `wbp_raw/WBP_Pet_LimitBreak_Win.txt`.
//
// ★ КОЛЛИЗИЯ `Pet_LimitBreak_Text_8` (44210 ↔ 44242) разводится ПО РОДИТЕЛЮ, а не по имени:
//     44210 — подпись `Pet_LimitBreak_Text_8_Text` в `Pet_LimitBreak_Box_grp`;
//     44242 — подпись `Pet_LimitBreak_Text_8`      в `Pet_LimitBreak_Text_8_grp`.
//   Родитель проверяется явно, несовпадение пишется в лог — чтобы будущий переимпорт ассета
//   не подменил контролы молча.
void UAsdaPetGrowthSubsystem::SetupControls()
{
	bReady = false;
	if (!Win || !Win->WidgetTree) { return; }

	TArray<UWidget*> All;
	Win->WidgetTree->GetAllWidgets(All);

	// Имя родителя узла (пусто, если родителя нет) — ключ разведения коллизии.
	auto ParentName = [](const UWidget* W) -> FString
	{
		const UPanelWidget* P = W ? W->GetParent() : nullptr;
		return P ? P->GetName() : FString();
	};

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* B = Cast<UButton>(W))
		{
			if (N == TEXT("Pet_LimitBreak_X"))  { BtnX  = B; continue; }   // 44239
			if (N == TEXT("Pet_LimitBreak_OK")) { BtnOk = B; continue; }   // 44232
			if (N == TEXT("Pet_LimitBreak_NO")) { BtnNo = B; continue; }   // 44233
			continue;
		}
		if (UCheckBox* C = Cast<UCheckBox>(W))
		{
			if (N == TEXT("Pet_LimitBreak_Check")) { ChkProtect = C; }     // 44241
			continue;
		}
		if (UTextBlock* T = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Pet_LimitBreak_Top_Textmove")) { TxtTitle    = T; continue; }  // 44201
			if (N == TEXT("Pet_LimitBreak_Text_1"))       { TxtPetName  = T; continue; }  // 44202
			if (N == TEXT("Pet_LimitBreak_Text_2"))       { TxtCanBreak = T; continue; }  // 44203
			if (N == TEXT("Pet_LimitBreak_Text_3_Text"))  { TxtLvlLabel = T; continue; }  // 44204
			if (N == TEXT("Pet_LimitBreak_Text_4"))       { TxtLvlCur   = T; continue; }  // 44205
			if (N == TEXT("Pet_LimitBreak_Text_5"))       { TxtLvlNew   = T; continue; }  // 44206
			if (N == TEXT("Pet_LimitBreak_Text_6"))       { TxtPotLabel = T; continue; }  // 44107
			if (N == TEXT("Pet_LimitBreak_Text_7"))       { TxtPotCount = T; continue; }  // 44208
			if (N == TEXT("Pet_LimitBreak_Tooltip"))      { TxtHint     = T; continue; }  // 44231
			if (N == TEXT("Pet_LimitBreak_OK_Text"))      { TxtOk       = T; continue; }  // подпись 44232
			if (N == TEXT("Pet_LimitBreak_NO_Text"))      { TxtNo       = T; continue; }  // подпись 44233
			if (N == TEXT("Pet_LimitBreak_Text_9"))       { TxtTalCount = T; continue; }  // 44243
			if (N == TEXT("Pet_LimitBreak_SETMARK"))      { TxtMark1    = T; continue; }  // 44251
			if (N == TEXT("Pet_LimitBreak_SETMARK2"))     { TxtMark2    = T; continue; }  // 44252
			if (N == TEXT("Pet_LimitBreak_SETMARK3"))     { TxtMark3    = T; continue; }  // 44253
			// ★ Коллизия: оба следующих имени — узлы 44210 и 44242.
			if (N == TEXT("Pet_LimitBreak_Text_8_Text"))
			{
				TxtNewLabel = T;                                                          // 44210
				const FString PN = ParentName(T);
				if (PN != TEXT("Pet_LimitBreak_Box_grp"))
				{
					UE_LOG(LogAsdaPetGrowth, Warning,
						TEXT("[питомец-рост] 44210 `Pet_LimitBreak_Text_8_Text` лежит в [%s], а ожидался ")
						TEXT("`Pet_LimitBreak_Box_grp` — разметка ассета изменилась, коллизия 44210/44242 не разведена"), *PN);
				}
				continue;
			}
			if (N == TEXT("Pet_LimitBreak_Text_8"))
			{
				TxtTalisman = T;                                                          // 44242
				const FString PN = ParentName(T);
				if (PN != TEXT("Pet_LimitBreak_Text_8_grp"))
				{
					UE_LOG(LogAsdaPetGrowth, Warning,
						TEXT("[питомец-рост] 44242 `Pet_LimitBreak_Text_8` лежит в [%s], а ожидался ")
						TEXT("`Pet_LimitBreak_Text_8_grp` — разметка ассета изменилась, коллизия 44210/44242 не разведена"), *PN);
				}
				continue;
			}
			continue;
		}
		if (UImage* I = Cast<UImage>(W))
		{
			if (N == TEXT("Pet_LimitBreak_Mark_14_0"))     { ImgPetIcon = I; continue; }  // 44209
			if (N == TEXT("Pet_LimitBreak_SETMARK_36_0"))  { ImgMark1   = I; continue; }  // 44251
			if (N == TEXT("Pet_LimitBreak_SETMARK2_31_0")) { ImgMark2   = I; continue; }  // 44252
			if (N == TEXT("Pet_LimitBreak_SETMARK3_32_0")) { ImgMark3   = I; continue; }  // 44253
			if (N == TEXT("Pet_LimitBreak_Gfont_3_33_0"))  { ImgGfont3  = I; continue; }  // 44250
			continue;
		}
		// Группа 44242/44243/44241 — её надо ПОКАЗАТЬ (см. шапку `.h`).
		if (N == TEXT("Pet_LimitBreak_Text_8_grp")) { TalismanGrp = W; continue; }
	}

	// ── Кнопки. Событие 66561 (`uiUpgradeDialogWndProc`, ui_core.c:90795-90803) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		AsdaUi::BindButton(this, Btn, IdNum, MoveTemp(Fn), Relays);   // ★ U20: реле клика — общая точка
	};
	Bind(BtnOk, 44232, [this](int32) { OnOkClicked(); });
	Bind(BtnNo, 44233, [this](int32) { OnCloseClicked(); });
	Bind(BtnX,  44239, [this](int32) { OnCloseClicked(); });

	// ── Галка 44241: события 67073 / 67074 (ui_core.c:90806-90816) ──
	if (ChkProtect)
	{
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = 44241;
		// `bChecked == true` — эталонное событие 67073, `false` — 67074 (ui_core.c:90805-90816).
		R->CheckCallback = [this](int32, bool bChecked) { OnProtectToggled(bChecked); };
		ChkProtect->OnCheckStateChanged.AddDynamic(R, &UAsdaButtonRelay::OnCheckChanged);
		AsdaUi::AnchorRelay(Relays, R, ChkProtect);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	}

	// ── Подписи init. КАЖДАЯ — по своей строке эталона (ui_core.c:90511-90583) ──
	auto SetLoc = [this](UTextBlock* T, int32 Id)
	{
		if (!T) { return; }
		const FString S = Loc(Id);
		if (!S.IsEmpty()) { T->SetText(FText::FromString(S)); }
	};
	SetLoc(TxtTitle,    PetGrowthLoc::kTitle);        // :90511  44201
	SetLoc(TxtLvlLabel, PetGrowthLoc::kLvlLabel);     // :90519  44204
	SetLoc(TxtNewLabel, PetGrowthLoc::kNewLvlLabel);  // :90522  44210
	SetLoc(TxtPotLabel, PetGrowthLoc::kPotionLabel);  // :90528  44107
	SetLoc(TxtOk,       PetGrowthLoc::kBtnOkLabel);   // :90533  44232
	SetLoc(TxtNo,       PetGrowthLoc::kBtnNoLabel);   // :90536  44233
	SetLoc(TxtTalisman, PetGrowthLoc::kTalisman);     // :90540  44242

	// 44107 поднимается явно (:90527), 44242/44243 тоже (:90542, :90545), 44243 обнуляется (:90544).
	AsdaUi::SetShownDecor(TxtPotLabel, true);
	AsdaUi::SetShownDecor(TalismanGrp, true);
	AsdaUi::SetShownDecor(TxtTalisman, true);
	AsdaUi::SetShownDecor(TxtTalCount, true);
	if (TxtTalCount) { TxtTalCount->SetText(FText::GetEmpty()); }

	// 44253 / 44251 / 44252 поднимаются тремя одинаковыми блоками (:90584-90598 эталона:
	//   `SetVisible(1)` + `SetField924(9)` + `SetField923(-256)` + `SetField926(0x800000)`).
	//   `SetField923(-256)` — это ARGB 0xFFFFFF00, то есть ЖЁЛТЫЙ цвет подписи счётчика.
	const FLinearColor MarkColor = AsdaUi::SrgbU32ToLinear(0xFFFFFF00u);
	UImage*     MarkImg[3] = { ImgMark3, ImgMark1, ImgMark2 };
	UTextBlock* MarkTxt[3] = { TxtMark3, TxtMark1, TxtMark2 };
	for (int32 i = 0; i < 3; ++i)
	{
		AsdaUi::SetShownDecor(MarkImg[i], true);
		AsdaUi::SetShownDecor(MarkTxt[i], true);
		if (MarkTxt[i]) { MarkTxt[i]->SetColorAndOpacity(FSlateColor(MarkColor)); }
	}

	bReady = (TxtLvlCur != nullptr && TxtLvlNew != nullptr && BtnOk != nullptr && BtnNo != nullptr);
	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] 44200 разобрано: 44205=%s 44206=%s 44210=%s 44242=%s 44241=%s 44232=%s 44233=%s 44239=%s"),
		TxtLvlCur ? TEXT("да") : TEXT("НЕТ"), TxtLvlNew ? TEXT("да") : TEXT("НЕТ"),
		TxtNewLabel ? TEXT("да") : TEXT("НЕТ"), TxtTalisman ? TEXT("да") : TEXT("НЕТ"),
		ChkProtect ? TEXT("да") : TEXT("НЕТ"), BtnOk ? TEXT("да") : TEXT("НЕТ"),
		BtnNo ? TEXT("да") : TEXT("НЕТ"), BtnX ? TEXT("да") : TEXT("НЕТ"));
}

// ══════════════════════════ ОТКРЫТИЕ: uiUpgradeDialog_Open@0xEE18E0 ═══════════════════════════

bool UAsdaPetGrowthSubsystem::OpenGrowth(int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return false; }

	// Г1 — `if (g_pActiveBuffEntry)` (ui_core.c:90612): есть ПРИЗВАННЫЙ ⇒ нотис и выход.
	//   ★ Проверяется ПЕРВЫМ и на аргумент не смотрит вовсе.
	if (P->GetActivePet() != nullptr)
	{
		NoticeMsgBox(PetGrowthLoc::kGateSummoned);   // 109975
		UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] гейт 109975: питомец призван, окно 44200 не открыто"));
		return false;
	}

	// Г2 — `else if (a2)` (:90617): питомца не передали ⇒ МОЛЧА ничего (ветки `else` нет).
	const FAsdaPetSlot* Pet = (PetUniqueId >= 0) ? P->FindByUniqueId(PetUniqueId) : nullptr;
	if (!Pet)
	{
		UE_LOG(LogAsdaPetGrowth, Log,
			TEXT("[питомец-рост] окно 44200 не открыто: питомца uid=%d нет (эталон в этой ветке молчит)"), PetUniqueId);
		return false;
	}

	// Г3 — `if (*(int*)(a2 + 80) < 10)` (:90621): +80 — это УРОВЕНЬ (см. И1 в шапке).
	if (Pet->Level >= kLevelCap)
	{
		NoticeMsgBox(PetGrowthLoc::kGateMaxLevel);   // 109946
		UE_LOG(LogAsdaPetGrowth, Log,
			TEXT("[питомец-рост] гейт 109946: уровень %d ≥ %d, окно 44200 не открыто"), Pet->Level, kLevelCap);
		return false;
	}

	if (!EnsureWindow()) { return false; }

	TargetUid = PetUniqueId;                       // filler[88] = a2 (:90619)
	bOpen = true;                                  // filler[92] = 1 (:90622)
	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ОТКРЫТИЕ ТОЖЕ СНИМАЕТ ЗАСОВ. Второй рубеж к тому, что стоит
	//   в `CloseGrowth`: окно могли не закрывать штатно (Esc, смена карты, пересоздание виджета),
	//   а игрок вправе рассчитывать, что заново открытое окно работает.
	SendLatch.Disarm();
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// `CClass_MapInsert(g_pWindowMgr, …)` + `SetVisible(1)` (:90623-90624) — показать и поднять.
		WM->SetWindowVisible(kGrowthWindowId, true);
	}
	// 44231 ← 109936 (:90625).
	if (TxtHint)
	{
		const FString S = Loc(PetGrowthLoc::kHint);
		if (!S.IsEmpty()) { TxtHint->SetText(FText::FromString(S)); }
	}

	// ★★★ ДЕФЕКТ ВЛАДЕЛЬЦА №4 (25.09.2026) — ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, ПОМЕЧЕНО.
	//   И5 остаётся верным как факт об эталоне: `uiUpgradeDialog_Open@0xEE18E0` зовёт
	//   `uiCloseWindowReset(g_bDeviceReloadPending)` (`ui_core.c:90627`), и тот гасит контрол
	//   43000 (доказательство — у объявления `CloseCageForChildWindow` в `AsdaPetCageSubsystem.h`).
	//   Владелец потребовал обратного; решение принимает ЕДИНАЯ ТОЧКА с ключом отката
	//   `asda.Pet.KeepCageOpen`, чтобы отступление не пришлось держать в двух местах.
	if (UAsdaPetCageSubsystem* Cage = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
	{
		Cage->CloseCageForChildWindow(TEXT("44200 «Развитие»"));
		// Клетка осталась открытой ⇒ 44200 обязано быть ВЫШЕ неё и не лежать на ней целиком.
		// ⚠ Родство (`SetWindowOwner`) здесь НЕ объявляем — разбор у парной строки в
		//   `UAsdaPetEvolutionSubsystem::OpenEvolution`: `RaiseOwnedAbove` стаскивал бы окно на
		//   `z(клетки) + 1`, а окно стоит СБОКУ и перекрываться с клеткой не может.
		if (Cage->IsCageOpen())
		{
			if (UAsdaWindowSubsystem* WMz = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
			{
				WMz->BringWindowToFront(kGrowthWindowId);
			}
			Cage->PlaceChildBesideCage(kGrowthWindowId, Win);
		}
	}

	// filler[105] = (Level >= MaxLevel) (:90629-90632). Именно это правило сервер проверяет
	//   кодом 9 «Level != MaxLevel» (GameServer.c:36718).
	bCanBreak = (Pet->Level >= Pet->MaxLevel);

	RefreshItemInfo();                              // :90633

	// Автовзвод галки при наличии талисмана (:90634-90639) — ★ И2: это СОСТОЯНИЕ ГАЛКИ, не видимость.
	Talisman = FindTalisman209();
	if (Talisman.Count > 0)
	{
		bProtectChecked = true;
		if (ChkProtect) { ChkProtect->SetIsChecked(true); }
		// Эталон после взвода галку НЕ перерисовывает — следующая перерисовка придёт по событию.
		//   Но видимую часть, завязанную на галку (44250 и 44251), надо привести в тон сразу,
		//   иначе кадр между открытием и первым событием показал бы рассинхрон.
		AsdaUi::SetShownDecor(ImgGfont3, true);
	}

	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] 44200 открыто: pet=%d порода=%d уровень=%d предел=%d можно-расти=%s ")
		TEXT("зелий(207)=%d талисманов(209)=%d галка=%s"),
		TargetUid, Pet->PetIndex, Pet->Level, Pet->MaxLevel, bCanBreak ? TEXT("да") : TEXT("нет"),
		Potion.Count, Talisman.Count, bProtectChecked ? TEXT("да") : TEXT("нет"));
	return true;
}

void UAsdaPetGrowthSubsystem::CloseGrowth()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kGrowthWindowId, false);
	}
	if (bTipShown)
	{
		if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
		{
			Tip->HideTooltipFor(EAsdaTipOwner::PetGrowth);
		}
		bTipShown = false;
	}
	// ★★★ ЗАСОВ ПОДТВЕРЖДЕНИЯ СНИМАЕТСЯ ЗДЕСЬ (замечание ревизии волны 4, МЕЛК-4).
	//   `bConfirmOpen` — наш засов «второго msgbox 71 не открывать»; он снимался только в колбэке
	//   самого окна подтверждения и на сбросе сессии. Если окно 44200 закрыть, пока подтверждение
	//   висит (крестик 44239, «Закрыть» 44233, Esc, либо приход 6149), колбэк уже не придёт, засов
	//   остался бы взведён — и кнопка «Развитие» замолчала бы до конца сессии. Это ровно тот
	//   тупик автомата, за который получила замечание волна 3 (закон `no-state-latch-before-shown`).
	//   ⚠ У эталона такого засова нет вовсе: там msgbox один на весь клиент и им владеет не окно.
	bConfirmOpen = false;
	// ★★★ ВОЛНА 8 (пункт 1 ревизии): ЗАСОВ «6148 В ПОЛЁТЕ» СНИМАЕТСЯ И ЗДЕСЬ. Ревизия нашла, что
	//   он не снимался НИ в `CloseGrowth`, НИ в `OpenGrowth` — только приходом 6149 и сбросом
	//   сессии. Если сервер промолчал, «закрыть → открыть» кнопку 44232 не оживляло, и она
	//   молчала до переподключения. Тот же закон `no-state-latch-before-shown`, по которому строкой
	//   выше снимается `bConfirmOpen`.
	//   ⚠ Почему это БЕЗОПАСНО и не даёт задвоения запроса: закрытие окна у эталона и так рвёт
	//   диалог (`uiCloseWindowReset`), а повторный 6148 сервер обрабатывает по СВОЕМУ состоянию —
	//   при уже выполненном лимит-брейке он вернёт код 9 «Level != MaxLevel» (GameServer.c:36718),
	//   а не сделает второй бросок.
	SendLatch.Disarm();
	bOpen = false;
	Mouse.Reset();
}

// ════════════════ ПЕРЕРИСОВКА: uiUpgradeDialog_RefreshItemInfo@0xEE2A70 ═══════════════════════
//   Порядок шагов дословный (ui_core.c:90700-90780). Тексты гасятся ДО проверки цели —
//   у эталона это сделано так же, до `if (v12[22])`.
void UAsdaPetGrowthSubsystem::RefreshItemInfo()
{
	if (!bReady) { return; }

	// :90713-90714 — пересчёт расходников каждой перерисовкой.
	Potion   = FindPotion207();
	Talisman = FindTalisman209();

	// :90715-90721 — пустая иконка 32×32 в 44209 и пять пустых текстов.
	AsdaUi::ClearImageBrush(ImgPetIcon);
	auto Clear = [](UTextBlock* T) { if (T) { T->SetText(FText::GetEmpty()); } };
	Clear(TxtPetName);    // 44202
	Clear(TxtCanBreak);   // 44203
	Clear(TxtLvlCur);     // 44205
	Clear(TxtLvlNew);     // 44206
	Clear(TxtPotCount);   // 44208

	// :90723 — `if (v12[22])`: цели нет ⇒ дальше ничего.
	const FAsdaPetSlot* Pet = Target();
	if (!Pet) { return; }

	// :90725-90731 — иконка породы 64×64 из `pettable +0x118 / +0x138 / +0x13C`.
	FAsdaPetRow Pr;
	if (PetRow(Pet->PetIndex, Pr))
	{
		AsdaUi::ApplyJuiIconCell(ImgPetIcon, Pr.ResultIcon0, Pr.ResultIcon0U, Pr.ResultIcon0V,
			kPetIconPx, (float)kPetIconPx);
	}

	// :90732-90733 — имя питомца (`sprintf("%s", (char*)v12[22] + 12)`, то есть `CPetSlot+0x0C`).
	if (TxtPetName) { TxtPetName->SetText(FText::FromString(Pet->Name)); }

	// :90735-90739 — 44203: «можно расширить» / «недостаточно опыта» (парные ветки, §6.10).
	if (TxtCanBreak)
	{
		const FString S = Loc(bCanBreak ? PetGrowthLoc::kCanBreak : PetGrowthLoc::kNotEnough);
		if (!S.IsEmpty()) { TxtCanBreak->SetText(FText::FromString(S)); }
	}

	// :90740-90750 — 44205 = УРОВЕНЬ, 44206 = уровень+1; при уровне 9 вместо числа «Предел» красным.
	if (TxtLvlCur) { TxtLvlCur->SetText(FText::FromString(FString::FromInt(Pet->Level))); }
	if (TxtLvlNew)
	{
		TxtLvlNew->SetText(FText::FromString(FString::FromInt(Pet->Level + 1)));
		TxtLvlNew->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(kNormalColorArgb)));
		if (Pet->Level == kLastLevelBeforeCap)
		{
			const FString S = Loc(PetGrowthLoc::kCapText);
			if (!S.IsEmpty()) { TxtLvlNew->SetText(FText::FromString(S)); }
			TxtLvlNew->SetColorAndOpacity(FSlateColor(AsdaUi::SrgbU32ToLinear(kCapColorArgb)));
		}
	}

	// :90751-90752 — 44208 = число ячеек с зельем 207.
	if (TxtPotCount) { TxtPotCount->SetText(FText::FromString(FString::FromInt(Potion.Count))); }

	// :90753-90755 — галка стоит, а талисманов нет ⇒ снять галку.
	if (bProtectChecked && Talisman.Count <= 0)
	{
		bProtectChecked = false;
		if (ChkProtect) { ChkProtect->SetIsChecked(false); }
	}

	// :90756-90761 — три подписи гасятся, две иконки затираются пустыми 32×32.
	Clear(TxtMark2); Clear(TxtMark3); Clear(TxtMark1);
	AsdaUi::ClearImageBrush(ImgMark2);
	AsdaUi::ClearImageBrush(ImgMark1);

	// :90762 — 44243 = число ячеек с талисманом 209.
	if (TxtTalCount) { TxtTalCount->SetText(FText::FromString(FString::FromInt(Talisman.Count))); }

	// :90763-90764 — ЖЁСТКО ЗАШИТЫЕ иконки: 44253 ← предмет 404, 44252 ← предмет 23237.
	UGameInstance* GI = GetGameInstance();
	AsdaUi::ApplyItemIcon(GI, ImgMark3, kTalismanIconItem, FVector2D((float)kBlankIconPx, (float)kBlankIconPx));
	AsdaUi::ApplyItemIcon(GI, ImgMark2, kPotionIconItem,   FVector2D((float)kBlankIconPx, (float)kBlankIconPx));

	// :90765-90772 — талисман найден ⇒ 44253 показывает счётчик, и ПО ГАЛКЕ в 44251 ложится иконка 404.
	if (Talisman.IsSet())
	{
		if (TxtMark3) { TxtMark3->SetText(FText::FromString(FString::FromInt(Talisman.Count))); }
		if (bProtectChecked)
		{
			AsdaUi::ApplyItemIcon(GI, ImgMark1, kTalismanIconItem, FVector2D((float)kBlankIconPx, (float)kBlankIconPx));
		}
	}

	// :90773-90777 — зелье найдено ⇒ 44252 показывает счётчик зелий.
	if (Potion.IsSet())
	{
		if (TxtMark2) { TxtMark2->SetText(FText::FromString(FString::FromInt(Potion.Count))); }
	}

	// :90778-90781 — 44250 `Gfont_Protection` виден ровно по состоянию галки.
	AsdaUi::SetShownDecor(ImgGfont3, bProtectChecked);
}

// ★ [ИСПРАВЛЕНИЕ] 07.11.2026, баг «окно лимита не видит зелье». Поле `linkCode` эталона — смещение +0x60, в порту `LinkCode`
//   (`RE/ida/itemrecord_fields.tsv`; сырая запись 23237: +0x60 = 207, +0x64 = 0). Порт сравнивал `LinkedGroupCode` (+0x64).
//   1 — поле эталона; 0 — прежнее сравнение, только для воспроизведения дефекта.
static ASDA_DEV_CVAR(int32, CVarPetPotionLinkField,
	TEXT("asda.Pet.LimitBreakPotionLinkField"), 1,
	TEXT("Окно 44200: 1 — зелье ищется по linkCode (+0x60, как эталон); 0 — старое сравнение с +0x64, окно зелья не видит."),
	ECVF_Default);
// ★ [ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА] 07.11.2026. Сервер принимает и свиток из Function_Table с подтипом 207 (код 399, контейнер 1:
//   GameServer.c ветка sub_532B10, «+8 != 207 → код 7»), а искатель эталона смотрит только контейнер 2.
//   1 — если зелья 23237 нет, окно берёт такой свиток; 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarPetLimitBreakScroll,
	TEXT("asda.Pet.LimitBreakScroll"), 1,
	TEXT("Окно 44200: 1 — без зелья 23237 принять свиток special_itemlist с подтипом 207 из контейнера 1; 0 — как эталон."),
	ECVF_Default);
// Подтип свитка снятия лимита во второй таблице (Function_Table +8, проверка сервера).
static constexpr int32 kLimitBreakScrollSubType = 207;

// ═══════════════════ ПОИСК РАСХОДНИКОВ — ДВА РАЗНЫХ ПРАВИЛА ЭТАЛОНА ═══════════════════════════

FAsdaPetGrowthItemRef UAsdaPetGrowthSubsystem::FindPotion207() const
{
	// Порт `inventoryFindItemByType207@0xEE1B00` (item_inventory.c:65219-65246):
	//   60 ячеек `g_InventoryArray2`, условие `ItemRecord->linkCode == 207`;
	//   указатель перезаписывается на КАЖДОМ совпадении (остаётся ПОСЛЕДНЕЕ), счётчик растёт.
	// ★ Именно `linkCode` (+0x60, `FAsdaItemRecord::LinkCode`), а НЕ `itemSubType`: соседний искатель `invFindItemByType105`
	//   (item_inventory.c:54613) ищет по подтипу — расхождение эталонное, не наше.
	FAsdaPetGrowthItemRef R;
	const UAsdaInventorySubsystem* IV = Inv();
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IV || !IT) { return R; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kSpendSlots; ++i)
	{
		const FAsdaItemInstance* It = IV->GetCell(kSpendContainer, i);
		if (!It || It->IsEmpty()) { continue; }
		const FAsdaItemRecord* Rec = IT->GetItem(It->ItemCode);
		if (!Rec) { continue; }
		const int32 Link = CVarPetPotionLinkField.GetValueOnGameThread() != 0 ? Rec->LinkCode : Rec->LinkedGroupCode;
		if (Link != kPotionLinkCode) { continue; }
		R.Container = kSpendContainer;
		R.Cell = i;
		R.ItemCode = It->ItemCode;
		++R.Count;
	}
	// Свиток считается в общий счётчик, но зелье контейнера 2 расходуется первым: при нём запрос остаётся как у эталона.
	if (CVarPetLimitBreakScroll.GetValueOnGameThread() != 0)
	{
		for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i)
		{
			const FAsdaItemInstance* It = IV->GetCell(kShellContainer, i);
			if (!It || It->IsEmpty()) { continue; }
			FAsdaSpecialItemlistRow Sp;
			if (!IT->GetSpecialItem(It->ItemCode, Sp) || Sp.SubType != kLimitBreakScrollSubType) { continue; }
			if (R.Container != kSpendContainer)
			{
				R.Container = kShellContainer;
				R.Cell = i;
				R.ItemCode = It->ItemCode;
			}
			++R.Count;
		}
	}
	return R;
}

FAsdaPetGrowthItemRef UAsdaPetGrowthSubsystem::FindTalisman209() const
{
	// Порт `bagFindItemByType209@0xEE1BC0` (ui_core.c:90653-90682):
	//   30 ячеек `g_InventoryBag`, запись ВТОРОЙ таблицы, условие `subType == 209`;
	//   так же остаётся ПОСЛЕДНЯЯ ячейка и считаются все.
	FAsdaPetGrowthItemRef R;
	const UAsdaInventorySubsystem* IV = Inv();
	const UAsdaItemTableSubsystem* IT = Items();
	if (!IV || !IT) { return R; }
	for (int32 i = 0; i < UAsdaInventorySubsystem::kShellSlots; ++i)
	{
		const FAsdaItemInstance* It = IV->GetCell(kShellContainer, i);
		if (!It || It->IsEmpty()) { continue; }
		FAsdaSpecialItemlistRow Sp;
		if (!IT->GetSpecialItem(It->ItemCode, Sp) || Sp.SubType != kTalismanSubType) { continue; }
		R.Container = kShellContainer;
		R.Cell = i;
		R.ItemCode = It->ItemCode;
		++R.Count;
	}
	return R;
}

// ═══════════════════════════════════ ДЕЙСТВИЯ ИГРОКА ══════════════════════════════════════════

// 44232 «Развитие» — порт `uiUpgradeDialogRefreshItemInfo@0xEE17B0` (ui_core.c:90565-90598).
//   ★ Имя функции — МИСНЕЙМ: она ничего не перерисовывает, это ПРОВЕРКА расходников.
void UAsdaPetGrowthSubsystem::OnOkClicked()
{
	// :90569-90570 — сначала ищем зелье (и заодно обновляем счётчик).
	Potion = FindPotion207();
	if (!Potion.IsSet())
	{
		// :90592 — зелья нет.
		NoticeMsgBox(PetGrowthLoc::kNoPotion);   // 109937
		return;
	}
	// :90573 — заодно ищется талисман (результат в этой ветке не используется, но эталон его зовёт).
	Talisman = FindTalisman209();

	if (!bCanBreak)
	{
		// :90584-90587 — ОДНОВРЕМЕННО модальный нотис A и подпись 44231 (`*(this+12)` = filler[48]).
		NoticeMsgBox(PetGrowthLoc::kTooYoung);   // 109938
		if (TxtHint)
		{
			const FString S = Loc(PetGrowthLoc::kTooYoung);
			if (!S.IsEmpty()) { TxtHint->SetText(FText::FromString(S)); }
		}
		return;
	}

	// :90580 — msgbox 71 (двухкнопочный, текст 109939, кнопки 108510/108511);
	//   по «ОК» диспетчер `OnSendItemUpgrade` case 0x47 зовёт `net_sendTradeConfirmRequest`
	//   (ui_core.c:57104) — то есть отправку 6148.
	if (bConfirmOpen) { return; }
	UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const FString Text = Loc(PetGrowthLoc::kConfirm71);
	if (Text.IsEmpty()) { return; }
	bConfirmOpen = true;
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(Text, [this](bool bYes)
	{
		bConfirmOpen = false;
		if (!bYes) { return; }
		SendLimitBreak();
	}, /*GaugeGateSec*/ 0.f, PetGrowthLoc::kMsg71Ok, PetGrowthLoc::kMsg71No, /*очередь: эталон (1, 71) ui_core.c:90580*/71, 1);
}

void UAsdaPetGrowthSubsystem::OnCloseClicked()
{
	// :90800-90803 — и 44233, и 44239 зовут один и тот же `sub_91FDCE`.
	CloseGrowth();
}

void UAsdaPetGrowthSubsystem::OnProtectToggled(bool bChecked)
{
	// :90806-90816 — 67073 ставит галку, 67074 снимает; обе ветки затем перерисовывают окно.
	bProtectChecked = bChecked;
	if (ChkProtect && ChkProtect->IsChecked() != bChecked) { ChkProtect->SetIsChecked(bChecked); }
	RefreshItemInfo();
	UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] 44241 галка защиты = %s (талисманов %d)"),
		bChecked ? TEXT("взведена") : TEXT("снята"), Talisman.Count);
}

// Порт `net_sendTradeConfirmRequest@0xEE1C80` (network.c:32791-32930).
void UAsdaPetGrowthSubsystem::SendLimitBreak()
{
	// :32817 — байт +104: пока прошлый запрос не отвечен, второго не шлём.
	if (SendLatch.IsArmed())
	{
		UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] 6148 не отправлен: прошлый запрос ещё в полёте (осталось %.1f с)"), SendLatch.RemainingSec(AsdaPetUi::SendTimeoutSec()));
		return;
	}
	// :32819-32821 — перед сборкой зелье ищется ЗАНОВО; не нашли ⇒ молча ничего.
	Potion = FindPotion207();
	if (!Potion.IsSet())
	{
		UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] 6148 не отправлен: зелья 207 в инвентаре нет"));
		return;
	}
	const FAsdaPetSlot* Pet = Target();
	if (!Pet)
	{
		UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] 6148 не отправлен: цели нет"));
		return;
	}

	// :32824-32832 — тело: `+0 petUid` (★ БЕЗ charId), `+4 u16 слот`, `+6 u8 сумка`,
	//   `+7 u16 слот премиума` — последний ставится ТОЛЬКО если галка взведена И талисман найден.
	int32 PremiumSlot = -1;
	if (bProtectChecked)
	{
		Talisman = FindTalisman209();
		if (Talisman.IsSet()) { PremiumSlot = Talisman.Cell; }
	}

	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	// Отправка отложенная: запрос кладётся в очередь волны 1 и уходит из её `Tick`
	//   (закон `no-reentrant-net-callbacks`).
	if (!P->RequestLimitBreak(Pet->UniqueId, Potion.Container, Potion.Cell, PremiumSlot))
	{
		UE_LOG(LogAsdaPetGrowth, Warning, TEXT("[питомец-рост] очередь волны 1 запрос 6148 не приняла"));
		return;
	}
	// :32926 — признак «в полёте» ставится ПОСЛЕ отправки.
	// ★★★ ЗАЩИТА ОТ ТУПИКА (закон `no-state-latch-before-shown`). Эталон ставит признак
	//   БЕЗУСЛОВНО, сразу за `CNetwork_Send`, и его это не подводит: у оригинала обрыв связи
	//   означает выход из клиента (закон `original-quits-client-on-disconnect`), то есть залипшего
	//   признака никто не увидит. У нас же отправка ОТЛОЖЕНА на кадр, и если связи нет, пакет
	//   не уйдёт, 6149 не придёт — и кнопка «Развитие» замолчала бы НАВСЕГДА, ровно как автомат
	//   инкубатора в волне 3. Поэтому признак ставится только тогда, когда отправка ДЕЙСТВИТЕЛЬНО
	//   состоится. Условие повторяет `UAsdaNetSubsystem::IsSendLinkUp` (`AsdaNetSubsystem.cpp:749-751`)
	//   — «сокет жив ЛИБО стоит перехватчик стенда», — но собрано из ПУБЛИЧНОГО API:
	//   `IsConnected()` у сети и `IsSendCaptureArmed()` у модели питомцев. Сам `IsSendLinkUp`
	//   закрыт, а открывать чужой класс ради диагностики окна нельзя.
	UAsdaNetSubsystem* N2 = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const bool bLink = (N2 != nullptr) && (N2->IsConnected() || P->IsSendCaptureArmed());
	// ★★★ ВОЛНА 9: засов взводится С КЛЮЧОМ ЭХА. Ответ 6149 несёт ячейку+контейнер потраченного
	//   зелья (+45..47, `GameServer.c:36841-36843`) — то самое, про что был запрос 6148.
	if (bLink) { SendLatch.Arm(6148,   // ★ S2 (В2-A): опкод запроса; таймаут — ключ на каждом тике
		AsdaPetUi::EchoKeyBagSlot(Potion.Container, Potion.Cell)); }
	if (!bLink)
	{
		UE_LOG(LogAsdaPetGrowth, Warning,
			TEXT("[питомец-рост] связи нет — 6148 из очереди не уйдёт; признак «в полёте» НЕ взведён, ")
			TEXT("кнопка 44232 остаётся живой (у оригинала такой ветки нет: там обрыв = выход из клиента)"));
	}
	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] 6148 отправлен: pet=%d зелье=(сумка %d, ячейка %d, код %d), премиум-ячейка=%d"),
		Pet->UniqueId, Potion.Container, Potion.Cell, Potion.ItemCode, PremiumSlot);
}

// ═══════════════ СТЕНД ОТПРАВКИ 6148 ПУТЁМ ИГРОКА (обоснование — в шапке `.h`) ════════════════

void UAsdaPetGrowthSubsystem::StartSendStand(int32 TestCharId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	if (bStandArmed)
	{
		UE_LOG(LogAsdaPetGrowth, Warning, TEXT("[питомец-рост] СТЕНД 6148 уже взведён — повторный запуск игнорирую"));
		return;
	}
	const FAsdaPetSlot* Pet = Target();
	if (!Pet)
	{
		UE_LOG(LogAsdaPetGrowth, Error, TEXT("[питомец-рост] СТЕНД 6148: окно не открыто на питомце — нечего отправлять"));
		return;
	}
	// ★ ПРОВЕРКА ДО ВЗВЕДЕНИЯ ПЕРЕХВАТА. `SendLimitBreak` молча откажет, если прошлый 6148 ещё
	//   в полёте (признак снимает ТОЛЬКО приход 6149, npc_mob_pet.c:7718) — и тогда перехват
	//   остался бы висеть до таймаута, а он на это время ГЛУШИТ ВСЮ исходящую сеть. Первая
	//   редакция стенда это и делала: во втором прогоне подряд появлялось «ПЕРЕХВАТ ВЗВЕДЁН»,
	//   сразу за ним «6148 не отправлен: прошлый запрос ещё в полёте», и тело не приходило.
	if (SendLatch.IsArmed())
	{
		UE_LOG(LogAsdaPetGrowth, Warning,
			TEXT("[питомец-рост] СТЕНД 6148 НЕ ЗАПУЩЕН: прошлый 6148 ещё в полёте (ответа 6149 не было). ")
			TEXT("Это ПРАВИЛО ОРИГИНАЛА (байт +104, network.c:32817), а не сбой стенда: подайте ответ ")
			TEXT("командой `asda.Pet.GrowthResult <код>` и повторите."));
		return;
	}
	// ★★★ ВОЛНА 8 (пункт 9 ревизии): ЧУЖОЙ СТЕНД УЖЕ ДЕРЖИТ ПЕРЕХВАТ.
	//   Перехватчик исходящих ОДИН на весь клиент. Без этой проверки второе окно
	//   взводило свой стенд, не получало тела (`Begin` второй раз игнорируется),
	//   печатало ЛОЖНЫЙ вердикт «тело НЕ СНЯТО» и по таймауту звало `EndSendCapture`,
	//   снимая перехват ПЕРВОГО — то есть ломало и свой вердикт, и чужой.
	if (P->IsSendCaptureArmed())
	{
		UE_LOG(LogAsdaPetGrowth, Error,
			TEXT("[питомец-рост] СТЕНД 6148 НЕ ЗАПУЩЕН: перехват исходящих уже держит ДРУГОЙ стенд. ")
			TEXT("Дождитесь его завершения либо снимите его — два стенда разом дают ложный вердикт."));
		return;
	}
	// Ожидания снимаем ДО отправки — ровно теми же данными, какими их посчитает `SendLimitBreak`.
	Potion = FindPotion207();
	Talisman = FindTalisman209();
	StandExpectUid     = Pet->UniqueId;
	StandExpectBag     = Potion.Container;
	StandExpectSlot    = Potion.Cell;
	StandExpectPremium = (bProtectChecked && Talisman.IsSet()) ? Talisman.Cell : -1;

	bStandDone = false;
	bStandArmed = true;
	StandLeft = 5.f;                      // с запасом на несколько кадров слива очереди
	P->BeginSendCapture([this](int32 Op, const TArray<uint8>& Body, bool /*bPad*/, TCHAR Env)
	{
		// Лямбда только СНИМАЕТ и печатает; перехват снимет `Tick` (см. комментарий там).
		if (CheckStandBody(Op, Body, Env)) { }
		bStandDone = true;
	}, TestCharId);

	// ★ Боевой путь: зовём метод окна, а не сборщик сети напрямую.
	//   `IsSendLinkUp()` при взведённом перехвате true, поэтому признак «в полёте» встанет —
	//   это и проверяется заодно.
	SendLimitBreak();
	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] СТЕНД 6148 запущен: ждём тело (pet=%d, зелье=(%d,%d), премиум-ячейка=%d, charId=%d)"),
		StandExpectUid, StandExpectBag, StandExpectSlot, StandExpectPremium, TestCharId);
}

bool UAsdaPetGrowthSubsystem::CheckStandBody(int32 Opcode, const TArray<uint8>& Body, TCHAR Envelope) const
{
	// Раскладка тела 6148 — СЕРВЕРНАЯ (GameServer.c:36624-36640), ★ БЕЗ поля charId:
	//   `+0 i32 petUid`, `+4 u16 itemSlot`, `+6 u8 itemBag`, `+7 u16 premiumSlot` (−1 = нет).
	const int32 ExpectLen = 9;
	// ★ ВОЛНА 8 (дубли, пункт 7): безопасное чтение полей тела — общая точка
	//   `AsdaPetUi::StandI32/StandI16/StandU8` (эти три лямбды были скопированы байт в байт
	//   в два стенда). Сами вердикты не дублируются: у каждого пакета своя раскладка.
	auto I32 = [&Body](int32 O) { return AsdaPetUi::StandI32(Body, O); };
	auto I16 = [&Body](int32 O) { return AsdaPetUi::StandI16(Body, O); };
	auto U8b = [&Body](int32 O) { return AsdaPetUi::StandU8 (Body, O); };

	const bool bOp    = (Opcode == OP_PET_LIMITBREAK_REQ);
	const bool bLen   = (Body.Num() == ExpectLen);
	const bool bEnv   = (Envelope == TEXT('S'));          // «с секретом»: 8 QPC + 16 MD5 + тело
	const bool bUid   = (I32(0) == StandExpectUid);
	const bool bSlot  = (I16(4) == StandExpectSlot);
	const bool bBag   = (U8b(6) == StandExpectBag);
	const bool bPrem  = (I16(7) == StandExpectPremium);
	const bool bAll   = bOp && bLen && bEnv && bUid && bSlot && bBag && bPrem;

	// ★ «кадр» ниже — МИНИМУМ БЕЗ ПАДДИНГА (10 заголовка + 24 конверта + тело), а не измеренная
	//   длина: отправка идёт с `a6 = 1`, и тогда `UAsdaNetSubsystem` дописывает хвостовой
	//   паддинг и байт-терминатор (`AsdaNetSubsystem.cpp:51525-51526`), то есть реальный кадр
	//   почти всегда ДЛИННЕЕ. Паддинг ставится правильно — здесь он просто не измеряется.
	// ★ И ещё честно: ОЖИДАЕМЫЕ ЗНАЧЕНИЯ (`StandExpect*`) берутся теми же `FindPotion207` /
	//   `FindTalisman209`, которыми пользуется и отправитель. Независимо проверена РАСКЛАДКА
	//   ТЕЛА (смещения сняты у сервера, GameServer.c:36631/36674 — мимо кода сборщика), а вот
	//   сами номера ячеек сверяются с самими собой. Чтобы это не было тавтологией и по ним,
	//   в прогонах 3-4 они менялись руками (галка вкл/выкл ⇒ премиум 20 и −1).
	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] СТЕНД 6148: опкод=%d(%s) тело=%d б(%s) конверт=%c(%s) кадр≥%d б (без паддинга)\n")
		TEXT("    +0 petUid  = %d (ждали %d) %s\n")
		TEXT("    +4 слот    = %d (ждали %d) %s\n")
		TEXT("    +6 сумка   = %d (ждали %d) %s\n")
		TEXT("    +7 премиум = %d (ждали %d) %s\n")
		TEXT("    ИТОГ: %s"),
		Opcode, bOp ? TEXT("OK") : TEXT("ОШИБКА"),
		Body.Num(), bLen ? TEXT("OK") : TEXT("ОШИБКА"),
		Envelope, bEnv ? TEXT("OK") : TEXT("ОШИБКА"), 10 + 24 + Body.Num(),
		I32(0), StandExpectUid,     bUid  ? TEXT("OK") : TEXT("ОШИБКА"),
		I16(4), StandExpectSlot,    bSlot ? TEXT("OK") : TEXT("ОШИБКА"),
		U8b(6), StandExpectBag,     bBag  ? TEXT("OK") : TEXT("ОШИБКА"),
		I16(7), StandExpectPremium, bPrem ? TEXT("OK") : TEXT("ОШИБКА"),
		bAll ? TEXT("6148 СОБРАН ВЕРНО") : TEXT("6148 СОБРАН НЕВЕРНО"));
	return bAll;
}

// ══════════════════ ПОДСКАЗКА ПО 44242 — события 8/9/10 (ui_core.c:90825-90835) ═══════════════
void UAsdaPetGrowthSubsystem::TickTooltip()
{
	UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	if (!Tip) { return; }
	// Эталон показывает подсказку при `!helper_getField1(g_pUIManager)` — «нет захвата
	//   перетаскиванием»; в порту это отсутствие зажатой ЛКМ.
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (поправка финальной приёмки, СРЕДН-4). Единственный мышиный путь этого
	//   окна — наведение на 44242, и оно тоже идёт ГЕОМЕТРИЕЙ (`AsdaUi::IsCursorOverWidget`), мимо
	//   слейтовского блокера модалки. У эталона захваченный модальный контрол забирает ВЕСЬ
	//   диспетч (`ui_core.c:104149-104153`), то есть подсказка под нотисом не всплывает.
	//   Под модалкой подсказку ГАСИМ, а не просто «не показываем»: иначе уже висящая осталась бы.
	UAsdaWindowSubsystem* WMod = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	// ★ Дедуп №2, В1-B (U9, 11.10.2026): общая точка модального гейта (ключ отката `asda.UI.ModalGatePolling`).
	const bool bModalUp = (WMod != nullptr) && AsdaUi::IsModalDispatchBlocked(GetGameInstance());
	const bool bOver = !bModalUp && !Mouse.bLmb && TxtTalisman && AsdaUi::IsCursorOverWidget(TxtTalisman);
	if (bOver == bTipShown) { return; }
	bTipShown = bOver;
	if (!bOver) { Tip->HideTooltipFor(EAsdaTipOwner::PetGrowth); return; }
	// `tutorialPopupLayoutSmall(&g_ItemTooltip, 110113, 110643, -1, -1)` — цвет −1/−1 = белый,
	//   таймера у канала D нет (отчёт §6.1).
	FAsdaTipMetrics M; M.FontPx = 8;
	Tip->ShowTextTip(EAsdaTipOwner::PetGrowth, Loc(PetGrowthLoc::kTipTitle), Loc(PetGrowthLoc::kTipBody), M);
}

// ══════════════════════════════ СОБЫТИЯ МОДЕЛИ (пакеты) ═══════════════════════════════════════

void UAsdaPetGrowthSubsystem::DrainPetEvents()
{
	if (PetEvents.Num() == 0) { return; }
	// ★ ВОЛНА 8 (дубли, пункт 7): слив очереди — общая точка `AsdaPetUi::DrainEvents`
	//   (это тело было скопировано в четыре окна). Хвост у каждого окна свой — он ниже.
	AsdaPetUi::DrainEvents(PetEvents,
		[this](int32 Op, int32 Res, int32 Uid) { OnPetEvent(Op, Res, Uid); });
}

void UAsdaPetGrowthSubsystem::OnPetEvent(int32 Opcode, int32 Result, int32 PetUniqueId)
{
	// ★ Окно 44200 реагирует ТОЛЬКО на 6149. Соседние пет-пакеты в оригинале обрабатывают другие
	//   объекты (`CPetData` в `item_enhance.c`, `CPetWindow` в `npc_mob_pet.c`) — проверено по
	//   диспетчеру `network.c:630`, на `g_pUpgradeDialog` завязан один-единственный 6149.
	if (Opcode != OP_PET_LIMITBREAK_RESULT) { return; }
	OnLimitBreakResult(Result, PetUniqueId);
}

// ПОЛНЫЙ порт `pet_OnRecvItemOpResult@0xEE2370` (npc_mob_pet.c:7691-7888).
//   Запись питомца уже применена моделью (волна 1) — здесь то, что делает ОКНО: инвентарь,
//   вес, закрытие себя, открытие 43000 и нотис своей ветки.
void UAsdaPetGrowthSubsystem::OnLimitBreakResult(int32 Result, int32 PetUniqueId)
{
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return; }
	const FAsdaPetLimitBreakEcho& Echo = P->GetLastLimitBreakEcho();

	// :7718 — признак «в полёте» снимается ПЕРВОЙ строкой, ДО switch, то есть на ЛЮБОМ коде.
	// ★★★ ВОЛНА 9, ПОПРАВКА РЕВИЗИИ: ГОНКА ЛЕЧИТСЯ СВЕРКОЙ ЭХА. Прежняя запись «номера запроса
	//   в телах 6148/6149 НЕТ» была ЛОЖНЫМ «нет»: ответ 6149 несёт ячейку+контейнер потраченного
	//   зелья (+45..47, `GameServer.c:36841-36843`). Разошлось ⇒ это ответ на ПРЕДЫДУЩИЙ 6148.
	//   ⚠ ЧЕСТНЫЙ ОСТАТОК: повтор ТОГО ЖЕ запроса (то же зелье из той же ячейки) неразличим.
	if (SendLatch.IsForeignEcho(AsdaPetUi::EchoKeyBagSlot(Echo.UsedBag, Echo.UsedSlot)))
	{
		UE_LOG(LogAsdaPetGrowth, Warning,
			TEXT("[питомец-рост] 6149 относится к ДРУГОМУ запросу: эхо ячейки зелья (сумка %d ")
			TEXT("ячейка %d) не совпало с отправленным — это ответ на ПРЕДЫДУЩИЙ 6148. ")
			TEXT("Пакет не применяем, засов не снимаем."),
			Echo.UsedBag, Echo.UsedSlot);
		return;
	}
	if (SendLatch.ConsumeTimedOut())
	{
		UE_LOG(LogAsdaPetGrowth, Warning,
			TEXT("[питомец-рост] 6149 пришёл ПОСЛЕ таймаута, но эхо ячейки зелья СОВПАЛО — ответ наш. ")
			TEXT("Неразличим лишь повтор ТОГО ЖЕ запроса (то же зелье из той же ячейки)."));
	}
	SendLatch.Disarm();

	UAsdaInventorySubsystem* IV = Inv();
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;

	switch (Result)
	{
		case 0:
			// :7720-7723 — только нотис.
			NoticeMsgBox(PetGrowthLoc::kDbError);   // 109980
			break;

		case 1: case 2: case 3: case 4:
		{
			// :7727-7817 — общий блок всех четырёх исходов.
			// 1) Запись питомца — уже применена волной 1 (`AddOrUpdate`), второй раз не трогаем.
			// ★ ОДНА ПЕРЕСБОРКА ХОТБАРА НА ВЕСЬ БЛОК. Эталон трогает до двух ячеек и зовёт
			//   `CInventoryUI_ResetAndRefresh` ОДИН раз в конце (:7775, :7801). Без батча каждая
			//   `ClearCell`/`DebugPutItem` тянула бы свою полную пересборку кэша панели.
			if (IV) { IV->BeginCellBatch(); }
			// 2) :7770-7774 — снять потраченное зелье с названной ячейки. Сумка 2 = расходники,
			//    1 = премиум-сумка; слот −1 = поля нет.
			if ((Echo.UsedBag == kShellContainer || Echo.UsedBag == kSpendContainer) && Echo.UsedSlot >= 0)
			{
				if (IV) { IV->ClearCell(Echo.UsedBag, Echo.UsedSlot); }
			}
			// 3) :7777 — полоса веса.
			if (Net) { Net->SetSelfWeight(Echo.WeightCur, -1, TEXT("6149")); }
			// 4) :7778-7800 — стопка вернулась или кончилась.
			//    Количество 0 ⇒ ячейку чистим; >0 ⇒ кладём остаток обратно.
			if ((Echo.RetItemBag == kShellContainer || Echo.RetItemBag == kSpendContainer) && Echo.RetItemSlot >= 0)
			{
				if (IV)
				{
					if (Echo.RetItemQty > 0 && Echo.RetItemCode != 0)
					{
						// ★★★ ВОЛНА 8 (БЛОКЕР-2 сквозной ревизии): КЛАДЁМ ЗАПИСЬ ЦЕЛИКОМ, 57 б.
						//   БЫЛО: `DebugPutItem(сумка, ячейка, код, штук)` — диагностическая
						//   эмуляция «без сервера» (`AsdaInventorySubsystem.h:811`): `I.Clear()`
						//   плюс четыре поля (+0 код, +4 контейнер, +5 слот, +9 количество).
						//   ЗДЕСЬ ЭТО БЫЛО ОСОБЕННО ДОРОГО: возвращаемый предмет лимит-брейка —
						//   ТАЛИСМАН, и вся его ценность лежит в затираемых байтах: +13 прочность,
						//   +14 вес стопки, гнёзда самоцветов +16/+18/+20/+22, ранг +24,
						//   процент заточки +27 и пять пар заточки с +29. Игрок получал бы обратно
						//   «голый» талисман. Эталон возвращает запись ОДНИМ куском:
						//   `memcpy(… + 57 * слот, (char*)&itemOpPayload[13] + 1, 0x39u)`
						//   (npc_mob_pet.c:7795).
						//   СТАЛО: общая точка `ApplyExternalRecord` (`AsdaInventorySubsystem.h:657`),
						//   маршрут по полям самой записи (закон `port-shared-points`).
						if (Echo.RetItemRec57.Num() == 57)
						{
							IV->ApplyExternalRecord(Echo.RetItemRec57);
						}
						else
						{
							// Тело короче 110 б до сюда не доходит (гейт приёмника), но молча
							//   класть обрезанную запись запрещено (закон `no-simplifications-stubs`).
							UE_LOG(LogAsdaPetGrowth, Error,
								TEXT("[питомец-рост] 6149: в снимке нет сырой записи 57 б (есть %d) — ячейку (%d,%d) НЕ трогаю"),
								Echo.RetItemRec57.Num(), Echo.RetItemBag, Echo.RetItemSlot);
						}
					}
					else
					{
						IV->ClearCell(Echo.RetItemBag, Echo.RetItemSlot);
					}
				}
			}
			if (IV) { IV->EndCellBatch(); }
			// 5) :7812-7813 — цель и найденное зелье сбрасываются ДО перерисовки.
			TargetUid = -1;
			Potion = FAsdaPetGrowthItemRef();
			RefreshItemInfo();          // :7814
			CloseGrowth();              // :7815 `sub_91FDCE`
			// 6) :7816 — окно 43000 открывается ЗАНОВО.
			if (UAsdaPetCageSubsystem* Cage = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaPetCageSubsystem>() : nullptr)
			{
				Cage->OpenCage();
			}
			// 7) Нотис СВОЕЙ ветки (:7818-7860).
			if (Result == 1) { NoticeMsgBox(PetGrowthLoc::kGrandSuccess); }   // 109940
			else if (Result == 2) { NoticeMsgBox(PetGrowthLoc::kSuccess); }   // 109941
			else
			{
				// ★ Ветвление по КОДУ ВОЗВРАЩЁННОГО ПРЕДМЕТА (+53) — см. И4 в шапке.
				const bool bTalismanPath = (Echo.RetItemCode != 0);
				const int32 PlainLoc = (Result == 3) ? PetGrowthLoc::kFail     : PetGrowthLoc::kDisaster;
				const int32 WordLoc  = (Result == 3) ? PetGrowthLoc::kFailWord : PetGrowthLoc::kDisasterWord;
				if (!bTalismanPath)
				{
					NoticeMsgBox(PlainLoc);                                   // 109942 / 109943
				}
				else
				{
					FString Tpl = Loc(PetGrowthLoc::kFailTpl);                // 110406 «…: [%s]. …»
					const FString Word = Loc(WordLoc);                        // 110407 / 110408
					// Дефект ПД5: по умолчанию подставляем, ключом — дословно (см. комментарий выше).
					if (CVarGrowthTalismanLiteral.GetValueOnGameThread() == 0 && !Word.IsEmpty())
					{
						Tpl = Tpl.Replace(TEXT("%s"), *Word, ESearchCase::CaseSensitive);
					}
					if (!Tpl.IsEmpty())
					{
						AsdaNotice::MsgBox(GetGameInstance(), EAsdaMsgCategory::ItemRefused, Tpl, PetGrowthLoc::kBtnOk);
					}
				}
			}
			break;
		}

		case 5: case 6: case 7: case 8:
		case 0xC: case 0xD: case 0xE: case 0xF:
			// :7862-7870 — «Ошибка данных предмета».
			NoticeMsgBox(PetGrowthLoc::kItemError);      // 109981
			break;

		case 9:
			// :7872-7874 — сервер отказал: `Level != MaxLevel` (GameServer.c:36718).
			NoticeMsgBox(PetGrowthLoc::kNotMaxLevel);    // 109982
			break;

		case 0xA:
			// :7876-7878 — `MaxLevel` вне (1, 10) (GameServer.c:36720).
			NoticeMsgBox(PetGrowthLoc::kIsMaxLevel);     // 109983
			break;

		case 0xB: case 0x10:
			// :7880-7883 — питомец не найден среди 30 (GameServer.c:36705).
			NoticeMsgBox(PetGrowthLoc::kNoPetForItem);   // 109984
			break;

		default:
			// :7885 — `return` без действий. Признак «в полёте» при этом уже снят (см. выше).
			UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] 6149 код %d — эталон в `default` молчит"), Result);
			break;
	}

	UE_LOG(LogAsdaPetGrowth, Log,
		TEXT("[питомец-рост] 6149 код=%d pet=%d зелье=(сумка %d, ячейка %d) вес=%d возврат=(код %d, сумка %d, ячейка %d, штук %d)"),
		Result, PetUniqueId, Echo.UsedBag, Echo.UsedSlot, Echo.WeightCur,
		Echo.RetItemCode, Echo.RetItemBag, Echo.RetItemSlot, Echo.RetItemQty);
}

// ═══════════════════════════════════════ ДАННЫЕ ═══════════════════════════════════════════════

UAsdaPetSubsystem* UAsdaPetGrowthSubsystem::Pets() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
}

UAsdaInventorySubsystem* UAsdaPetGrowthSubsystem::Inv() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
}

UAsdaItemTableSubsystem* UAsdaPetGrowthSubsystem::Items() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
}

const FAsdaPetSlot* UAsdaPetGrowthSubsystem::Target() const
{
	UAsdaPetSubsystem* P = Pets();
	return (P && TargetUid >= 0) ? P->FindByUniqueId(TargetUid) : nullptr;
}

bool UAsdaPetGrowthSubsystem::PetRow(int32 PetIndex, FAsdaPetRow& Out) const
{
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::PetRow`. Свой `FindRow`
	//   шёл мимо общей `UAsdaItemTableSubsystem::GetPet` — четвёртая копия могла разойтись.
	//   Возврат по-прежнему ПО ЗНАЧЕНИЮ (окна держат разные строки одновременно).
	return AsdaPetUi::PetRow(GetGameInstance(), PetIndex, Out);
}

// ══════════════════════════════════════ НОТИСЫ ════════════════════════════════════════════════

FString UAsdaPetGrowthSubsystem::Loc(int32 LocId) const
{
	return AsdaNotice::Loc(GetGameInstance(), LocId);
}

void UAsdaPetGrowthSubsystem::NoticeMsgBox(int32 LocId) const
{
	// Канал A отчёта §6.1: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст, 1)` —
	//   однокнопочное модальное окно `ONEBUT_MSGBOX` 1500, кнопка = loc 2288 «ОК».
	//   ★ Чат у питомцев НЕ используется (доказано двумя методами, отчёт §6.1).
	// ★ ВОЛНА 8 (дубли, пункт 7): тело — в общей точке `AsdaPetUi::NoticeMsgBox`
	//   (там же и единственная константа подписи кнопки 2288).
	AsdaPetUi::NoticeMsgBox(GetGameInstance(), LocId);
}

// ════════════════════════════════════ ДИАГНОСТИКА ═════════════════════════════════════════════

FString UAsdaPetGrowthSubsystem::DumpControls() const
{
	FString S = TEXT("=== окно 44200 «Рост питомца»: разобранные контролы ===\n");
	auto Line = [&S](const TCHAR* Id, const UObject* W)
	{
		S += FString::Printf(TEXT("  %-52s %s\n"), Id, W ? TEXT("есть") : TEXT("НЕТ"));
	};
	Line(TEXT("44201 Pet_LimitBreak_Top_Textmove"),  TxtTitle);
	Line(TEXT("44202 Pet_LimitBreak_Text_1"),        TxtPetName);
	Line(TEXT("44203 Pet_LimitBreak_Text_2"),        TxtCanBreak);
	Line(TEXT("44204 Pet_LimitBreak_Text_3_Text"),   TxtLvlLabel);
	Line(TEXT("44205 Pet_LimitBreak_Text_4"),        TxtLvlCur);
	Line(TEXT("44206 Pet_LimitBreak_Text_5"),        TxtLvlNew);
	Line(TEXT("44209 Pet_LimitBreak_Mark_14_0"),     ImgPetIcon);
	Line(TEXT("44210 Pet_LimitBreak_Text_8_Text ★"), TxtNewLabel);
	Line(TEXT("44107 Pet_LimitBreak_Text_6"),        TxtPotLabel);
	Line(TEXT("44208 Pet_LimitBreak_Text_7"),        TxtPotCount);
	Line(TEXT("44231 Pet_LimitBreak_Tooltip"),       TxtHint);
	Line(TEXT("44232 Pet_LimitBreak_OK"),            BtnOk);
	Line(TEXT("44232 Pet_LimitBreak_OK_Text"),       TxtOk);
	Line(TEXT("44233 Pet_LimitBreak_NO"),            BtnNo);
	Line(TEXT("44233 Pet_LimitBreak_NO_Text"),       TxtNo);
	Line(TEXT("44239 Pet_LimitBreak_X"),             BtnX);
	Line(TEXT("44241 Pet_LimitBreak_Check"),         ChkProtect);
	Line(TEXT("44242 Pet_LimitBreak_Text_8 ★"),      TxtTalisman);
	Line(TEXT("44243 Pet_LimitBreak_Text_9"),        TxtTalCount);
	Line(TEXT("44250 Pet_LimitBreak_Gfont_3_33_0"),  ImgGfont3);
	Line(TEXT("44251 Pet_LimitBreak_SETMARK_36_0"),  ImgMark1);
	Line(TEXT("44251 Pet_LimitBreak_SETMARK"),       TxtMark1);
	Line(TEXT("44252 Pet_LimitBreak_SETMARK2_31_0"), ImgMark2);
	Line(TEXT("44252 Pet_LimitBreak_SETMARK2"),      TxtMark2);
	Line(TEXT("44253 Pet_LimitBreak_SETMARK3_32_0"), ImgMark3);
	Line(TEXT("44253 Pet_LimitBreak_SETMARK3"),      TxtMark3);
	Line(TEXT("     Pet_LimitBreak_Text_8_grp"),     TalismanGrp);
	S += TEXT("  ── НЕ ПРИВЯЗЫВАЕТСЯ ОРИГИНАЛОМ (в `uiInitTradeWindow` этих номеров нет): ──\n");
	S += TEXT("     44220 Gfont_Limitbreak · 44221 Gfont_Bomb · 44222/44223 Ani_Holy\n");
	// ★ Коллизия имён — печатаем родителей, чтобы развод был виден глазами.
	auto Par = [](const UWidget* W) -> FString
	{
		const UPanelWidget* P = W ? W->GetParent() : nullptr;
		return P ? P->GetName() : TEXT("—");
	};
	S += FString::Printf(TEXT("  коллизия `Pet_LimitBreak_Text_8`: 44210 в [%s], 44242 в [%s]\n"),
		*Par(TxtNewLabel), *Par(TxtTalisman));
	return S;
}

FString UAsdaPetGrowthSubsystem::DumpState() const
{
	FString S = TEXT("\n═══════════ 44200 «Рост питомца»: состояние ═══════════\n");
	const FAsdaPetSlot* Pet = Target();
	UAsdaPetSubsystem* P = Pets();
	S += FString::Printf(TEXT("  окно открыто=%s, контролы разобраны=%s, цель uid=%d\n"),
		bOpen ? TEXT("да") : TEXT("нет"), bReady ? TEXT("да") : TEXT("нет"), TargetUid);
	if (Pet)
	{
		S += FString::Printf(TEXT("  питомец: порода=%d имя=[%s] уровень=%d предел=%d опыт=%d сытость=%d\n"),
			Pet->PetIndex, *Pet->Name, Pet->Level, Pet->MaxLevel, Pet->Exp, Pet->Fullness);
	}
	else
	{
		S += TEXT("  питомец: цели нет\n");
	}
	S += FString::Printf(TEXT("  можно расти (Level>=MaxLevel)=%s, запрос 6148 в полёте=%s, галка защиты=%s\n"),
		bCanBreak ? TEXT("да") : TEXT("нет"), SendLatch.IsArmed() ? TEXT("ДА") : TEXT("нет"),
		bProtectChecked ? TEXT("взведена") : TEXT("снята"));
	S += FString::Printf(TEXT("  зелье 207 (linkCode, контейнер 2; свиток 207, контейнер 1): ячеек=%d последняя=(%d,%d) код=%d\n"),
		Potion.Count, Potion.Container, Potion.Cell, Potion.ItemCode);
	S += FString::Printf(TEXT("  талисман 209 (subType, контейнер 1): ячеек=%d последняя=(%d,%d) код=%d\n"),
		Talisman.Count, Talisman.Container, Talisman.Cell, Talisman.ItemCode);
	S += FString::Printf(TEXT("  гейт 109975 (призван)=%s, гейт 109946 (уровень>=%d)=%s\n"),
		(P && P->GetActivePet() != nullptr) ? TEXT("СРАБОТАЕТ") : TEXT("нет"), kLevelCap,
		(Pet && Pet->Level >= kLevelCap) ? TEXT("СРАБОТАЕТ") : TEXT("нет"));
	if (P)
	{
		const FAsdaPetLimitBreakEcho& E = P->GetLastLimitBreakEcho();
		S += FString::Printf(TEXT("  последний 6149: код=%d зелье=(%d,%d) вес=%d возврат=(код %d, %d:%d ×%d)\n"),
			E.Result, E.UsedBag, E.UsedSlot, E.WeightCur, E.RetItemCode, E.RetItemBag, E.RetItemSlot, E.RetItemQty);
	}
	S += FString::Printf(TEXT("  ключ asda.Pet.GrowthTalismanLiteral = %d (%s)\n"),
		CVarGrowthTalismanLiteral.GetValueOnGameThread(),
		CVarGrowthTalismanLiteral.GetValueOnGameThread() ? TEXT("дословно, [%s] не подставляется")
		                                                 : TEXT("подстановка 110407/110408 (ПД5 исправлен)"));
	return S;
}

FString UAsdaPetGrowthSubsystem::DumpOdds() const
{
	FString S = TEXT("\n═══════════ Лимит-брейк: ФАКТИЧЕСКИЕ пороги броска ═══════════\n");
	S += TEXT("★ ОКНО 44200 ШАНСОВ НЕ ПОКАЗЫВАЕТ — оригинал их не выводит (см. шапку .h).\n");
	S += TEXT("★ Ниже — то, что РЕАЛЬНО сделает сервер: таблица адресуется MaxLevel, а её загрузчик\n");
	S += TEXT("  делает 4 fscanf на 5 колонок (дефект ПД4, GameServer.c:69814-69828) — колонки съехали.\n");
	UAsdaPetSubsystem* P = Pets();
	if (!P) { return S + TEXT("  модель питомцев недоступна\n"); }
	S += TEXT("  MaxLevel | порог1 | порог2 | порог3 | что это значит на практике\n");
	for (int32 Lv = 2; Lv <= 9; ++Lv)
	{
		int32 T1 = 0, T2 = 0, T3 = 0;
		if (!P->GetLimitBreakThresholds(Lv, T1, T2, T3))
		{
			S += FString::Printf(TEXT("  %8d | строки нет\n"), Lv);
			continue;
		}
		// Бросок: `rand(0,100000)`; `< T1` → исход 1, `< T2` → 2, `< T3` → 3, иначе 4
		//   (GameServer.c:36725-36775). Пороги НЕ обязаны возрастать — после ПД4 они не возрастают.
		FString Note;
		if (T1 >= 100000)      { Note = TEXT("★ исход 1 ВСЕГДА (порог ≥ 100000)"); }
		else if (T1 <= 10)     { Note = TEXT("★ исход 1 практически недостижим"); }
		if (T2 < T1)           { Note += TEXT(" · порядок порогов нарушен (2 < 1)"); }
		if (T3 < T2)           { Note += TEXT(" · порядок порогов нарушен (3 < 2)"); }
		if (Note.IsEmpty())    { Note = TEXT("порядок порогов не нарушен"); }
		S += FString::Printf(TEXT("  %8d | %6d | %6d | %6d | %s\n"), Lv, T1, T2, T3, *Note);
	}
	return S;
}

FString UAsdaPetGrowthSubsystem::DumpNotices() const
{
	FString S = TEXT("\n═══════════ 44200: событие → loc → канал ═══════════\n");
	auto Row = [this, &S](const TCHAR* Event, int32 LocId, const TCHAR* Chan, const TCHAR* Where)
	{
		const FString T = Loc(LocId);
		S += FString::Printf(TEXT("  %-46s %6d  %-2s  %-34s [%s]\n"),
			Event, LocId, Chan, Where, T.IsEmpty() ? TEXT("СТРОКИ НЕТ") : *T.Left(60));
	};
	Row(TEXT("init 44201 заголовок"),                 PetGrowthLoc::kTitle,        TEXT("C"), TEXT("ui_core.c:90511"));
	Row(TEXT("init 44204 подпись"),                   PetGrowthLoc::kLvlLabel,     TEXT("C"), TEXT("ui_core.c:90519"));
	Row(TEXT("init 44210 подпись ★"),                 PetGrowthLoc::kNewLvlLabel,  TEXT("C"), TEXT("ui_core.c:90522"));
	Row(TEXT("init 44107 подпись"),                   PetGrowthLoc::kPotionLabel,  TEXT("C"), TEXT("ui_core.c:90528"));
	Row(TEXT("init 44232 подпись"),                   PetGrowthLoc::kBtnOkLabel,   TEXT("C"), TEXT("ui_core.c:90533"));
	Row(TEXT("init 44233 подпись"),                   PetGrowthLoc::kBtnNoLabel,   TEXT("C"), TEXT("ui_core.c:90536"));
	Row(TEXT("init 44242 подпись ★"),                 PetGrowthLoc::kTalisman,     TEXT("C"), TEXT("ui_core.c:90540"));
	Row(TEXT("открытие: гейт «питомец призван»"),     PetGrowthLoc::kGateSummoned, TEXT("A"), TEXT("ui_core.c:90614"));
	Row(TEXT("открытие: 44231 подсказка"),            PetGrowthLoc::kHint,         TEXT("C"), TEXT("ui_core.c:90625"));
	Row(TEXT("открытие: гейт «уровень >= 10»"),       PetGrowthLoc::kGateMaxLevel, TEXT("A"), TEXT("ui_core.c:90645"));
	Row(TEXT("перерисовка 44203: можно расти"),       PetGrowthLoc::kCanBreak,     TEXT("C"), TEXT("ui_core.c:90736"));
	Row(TEXT("перерисовка 44203: мало опыта"),        PetGrowthLoc::kNotEnough,    TEXT("C"), TEXT("ui_core.c:90738"));
	Row(TEXT("перерисовка 44206: предел (красный)"),  PetGrowthLoc::kCapText,      TEXT("C"), TEXT("ui_core.c:90747"));
	Row(TEXT("ОК 44232: подтверждение msgbox 71"),    PetGrowthLoc::kConfirm71,    TEXT("B"), TEXT("ui_core.c:90580"));
	Row(TEXT("ОК 44232: молод (нотис И 44231)"),      PetGrowthLoc::kTooYoung,     TEXT("A+C"), TEXT("ui_core.c:90584/90586"));
	Row(TEXT("ОК 44232: нет зелья"),                  PetGrowthLoc::kNoPotion,     TEXT("A"), TEXT("ui_core.c:90592"));
	Row(TEXT("наведение 44242: заголовок"),           PetGrowthLoc::kTipTitle,     TEXT("D"), TEXT("ui_core.c:90830"));
	Row(TEXT("наведение 44242: тело"),                PetGrowthLoc::kTipBody,      TEXT("D"), TEXT("ui_core.c:90828"));
	Row(TEXT("6149 код 0: ошибка БД"),                PetGrowthLoc::kDbError,      TEXT("A"), TEXT("npc_mob_pet.c:7722"));
	Row(TEXT("6149 код 1: грандиозный успех"),        PetGrowthLoc::kGrandSuccess, TEXT("A"), TEXT("npc_mob_pet.c:7819"));
	Row(TEXT("6149 код 2: успех"),                    PetGrowthLoc::kSuccess,      TEXT("A"), TEXT("npc_mob_pet.c:7825"));
	Row(TEXT("6149 код 3: шаблон талисмана"),         PetGrowthLoc::kFailTpl,      TEXT("A"), TEXT("npc_mob_pet.c:7833"));
	Row(TEXT("6149 код 3: слово в [%s]"),             PetGrowthLoc::kFailWord,     TEXT("A"), TEXT("npc_mob_pet.c:7832"));
	Row(TEXT("6149 код 3: без талисмана"),            PetGrowthLoc::kFail,         TEXT("A"), TEXT("npc_mob_pet.c:7840"));
	Row(TEXT("6149 код 4: слово в [%s]"),             PetGrowthLoc::kDisasterWord, TEXT("A"), TEXT("npc_mob_pet.c:7848"));
	Row(TEXT("6149 код 4: без талисмана"),            PetGrowthLoc::kDisaster,     TEXT("A"), TEXT("npc_mob_pet.c:7856"));
	Row(TEXT("6149 коды 5-8,0xC-0xF: данные"),        PetGrowthLoc::kItemError,    TEXT("A"), TEXT("npc_mob_pet.c:7869"));
	Row(TEXT("6149 код 9: не максимальный уровень"),  PetGrowthLoc::kNotMaxLevel,  TEXT("A"), TEXT("npc_mob_pet.c:7873"));
	Row(TEXT("6149 код 0xA: это максимальный"),       PetGrowthLoc::kIsMaxLevel,   TEXT("A"), TEXT("npc_mob_pet.c:7877"));
	Row(TEXT("6149 коды 0xB/0x10: нет питомца"),      PetGrowthLoc::kNoPetForItem, TEXT("A"), TEXT("npc_mob_pet.c:7882"));
	S += TEXT("  6149 default — эталон молчит (`return` без действий, npc_mob_pet.c:7885)\n");
	S += TEXT("  ★ Чат (EAsdaMsgTarget) окно НЕ использует: у питомцев его нет вовсе (отчёт §6.1).\n");
	S += TEXT("  ★ Нумпад (1410) окно НЕ вызывает: в диапазоне ui_core.c:90495…90840 вхождений\n");
	S += TEXT("    `g_pNumPadDialog` нет, в теле отправителя 6148 и приёмника 6149 — тоже нет.\n");
	return S;
}

// ════════════════════════════════ КОНСОЛЬНЫЕ КОМАНДЫ ══════════════════════════════════════════

static UAsdaPetGrowthSubsystem* GrowthFromWorld(UWorld* W)
{
	UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UAsdaPetGrowthSubsystem>() : nullptr;
}

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthOpen(
	TEXT("asda.Pet.Growth"),
	TEXT("asda.Pet.Growth [uid] — открыть окно 44200 БОЕВЫМ путём (`uiUpgradeDialog_Open`, оба гейта). ")
	TEXT("Без аргумента берётся ВЫБРАННЫЙ в окне 43000 питомец — ровно как делает кнопка 43055."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W);
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		if (!S || !P) { return; }
		const int32 Uid = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : P->GetSelectedUniqueId();
		const bool bOk = S->OpenGrowth(Uid);
		UE_LOG(LogAsdaPetGrowth, Log, TEXT("[питомец-рост] asda.Pet.Growth %d → %s"),
			Uid, bOk ? TEXT("открыто") : TEXT("гейт/отказ"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthControls(
	TEXT("asda.Pet.GrowthControls"),
	TEXT("asda.Pet.GrowthControls — печать КАЖДОГО разобранного контрола окна 44200 и развода коллизии имён."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W))
		{
			UE_LOG(LogAsdaPetGrowth, Log, TEXT("%s"), *S->DumpControls());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthState(
	TEXT("asda.Pet.GrowthState"),
	TEXT("asda.Pet.GrowthState — состояние окна 44200: цель, уровни, расходники, галка, гейты, последний 6149."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W))
		{
			UE_LOG(LogAsdaPetGrowth, Log, TEXT("%s"), *S->DumpState());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthNotices(
	TEXT("asda.Pet.GrowthNotices"),
	TEXT("asda.Pet.GrowthNotices — таблица «событие → loc → канал» окна 44200 с фактическими строками порта."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W))
		{
			UE_LOG(LogAsdaPetGrowth, Log, TEXT("%s"), *S->DumpNotices());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthOdds(
	TEXT("asda.Pet.GrowthOdds"),
	TEXT("asda.Pet.GrowthOdds — ФАКТИЧЕСКИЕ пороги броска лимит-брейка (дефект ПД4). В UI не выводятся: ")
	TEXT("оригинал шансов не показывает."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		if (UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W))
		{
			UE_LOG(LogAsdaPetGrowth, Log, TEXT("%s"), *S->DumpOdds());
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthClick(
	TEXT("asda.Pet.GrowthClick"),
	TEXT("asda.Pet.GrowthClick <44232|44233|44239|44241> — нажатие по узлу окна 44200 БОЕВЫМ обработчиком. ")
	TEXT("Для 44241 вторым аргументом 0/1 — состояние галки."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W);
		if (!S || Args.Num() < 1) { return; }
		switch (FCString::Atoi(*Args[0]))
		{
			case 44232: S->OnOkClicked();    break;
			case 44233: S->OnCloseClicked(); break;
			case 44239: S->OnCloseClicked(); break;
			case 44241: S->OnProtectToggled(Args.Num() >= 2 ? (FCString::Atoi(*Args[1]) != 0) : true); break;
			default: UE_LOG(LogAsdaPetGrowth, Warning, TEXT("[питомец-рост] нет такого узла окна 44200")); break;
		}
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthFill(
	TEXT("asda.Pet.GrowthFill"),
	TEXT("asda.Pet.GrowthFill — положить в инвентарь зелье лимит-брейка (item_list linkCode 207, ")
	TEXT("контейнер 2) и талисман (special_itemlist subType 209, контейнер 1). Печатает найденные коды."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельные зелье/талисман — ОК отправил бы 6148 с поддельными слотами.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.GrowthFill"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaInventorySubsystem* IV = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
		UAsdaItemTableSubsystem* IT = GI ? GI->GetSubsystem<UAsdaItemTableSubsystem>() : nullptr;
		if (!IV || !IT) { return; }
		// Зелье: ТОЧНО тем же условием, каким его ищет окно — `linkCode == 207`.
		TArray<int32> Potions;
		for (const FAsdaItemRecord& R : IT->GetAllItems())
		{
			if (R.LinkCode == UAsdaPetGrowthSubsystem::kPotionLinkCode) { Potions.Add(R.ItemCode); }
			if (Potions.Num() >= 3) { break; }
		}
		TArray<int32> Talismans;
		for (int32 Code = 1; Code <= 600 && Talismans.Num() < 2; ++Code)
		{
			FAsdaSpecialItemlistRow Sp;
			if (IT->GetSpecialItem(Code, Sp) && Sp.SubType == UAsdaPetGrowthSubsystem::kTalismanSubType)
			{
				Talismans.Add(Code);
			}
		}
		for (int32 i = 0; i < Potions.Num(); ++i)   { IV->DebugPutItem(2, 20 + i, Potions[i], 1); }
		for (int32 i = 0; i < Talismans.Num(); ++i) { IV->DebugPutItem(1, 20 + i, Talismans[i], 1); }
		UE_LOG(LogAsdaPetGrowth, Log,
			TEXT("[питомец-рост] GrowthFill: зелий(linkCode 207)=%d [%s] в (2,20…), талисманов(subType 209)=%d [%s] в (1,20…)"),
			Potions.Num(), Potions.Num() ? *FString::JoinBy(Potions, TEXT(","), [](int32 C) { return FString::FromInt(C); }) : TEXT("—"),
			Talismans.Num(), Talismans.Num() ? *FString::JoinBy(Talismans, TEXT(","), [](int32 C) { return FString::FromInt(C); }) : TEXT("—"));
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthSendStand(
	TEXT("asda.Pet.GrowthSendStand"),
	TEXT("asda.Pet.GrowthSendStand [charId] — проверить ОТПРАВКУ 6148 путём игрока: взвести перехват ")
	TEXT("волны 1 (UAsdaPetSubsystem::BeginSendCapture), позвать SendLimitBreak() окна и сверить ")
	TEXT("снятое тело по СЕРВЕРНЫМ смещениям. Пока перехват взведён, в сеть не уходит ничего; ")
	TEXT("снимается он сам на ближайшем тике после снятия тела или через 5 с."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: стенд шлёт лимит-брейк 6148 без модалки; при сбое перехвата ушёл бы настоящий.
		static const uint16 kGuardOps[] = { 6148 };
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.GrowthSendStand"), 10.f, MakeArrayView(kGuardOps))) { return; }
		UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W);
		if (!S) { return; }
		S->StartSendStand(Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 777);
	}));
#endif // ASDA_DEV_CONSOLE

#if ASDA_DEV_CONSOLE   // ★ античит А2: дев-команда, в Shipping не регистрируется
static FAutoConsoleCommandWithWorldAndArgs GCmdGrowthResult(
	TEXT("asda.Pet.GrowthResult"),
	TEXT("asda.Pet.GrowthResult <код 0..16> [uid] [новыйУровень] [новыйПредел] [кодВозврата] [штук] — ")
	TEXT("синтезировать НАСТОЯЩЕЕ тело 6149 (110 б) и скормить его БОЕВОМУ приёмнику ")
	TEXT("UAsdaPetSubsystem::OnPetPacket. `кодВозврата` != 0 включает ветку талисмана."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* W)
	{
		// ★ С-Р1 (26.09.2026), офлайн-гейт: поддельный 6149 меняет уровень/предел питомца и снимает замок в живой сессии.
		if (FAsdaOutgoingGate::RefuseDebugWhenOnline(W, TEXT("asda.Pet.GrowthResult"))) { return; }
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		UAsdaPetSubsystem* P = GI ? GI->GetSubsystem<UAsdaPetSubsystem>() : nullptr;
		UAsdaPetGrowthSubsystem* S = GrowthFromWorld(W);
		if (!P || !S || Args.Num() < 1) { return; }

		const int32 Code = FCString::Atoi(*Args[0]);
		const int32 Uid  = (Args.Num() >= 2) ? FCString::Atoi(*Args[1]) : P->GetSelectedUniqueId();
		const FAsdaPetSlot* Old = P->FindByUniqueId(Uid);
		const int32 NewLevel = (Args.Num() >= 3) ? FCString::Atoi(*Args[2]) : (Old ? Old->Level : 1);
		const int32 NewMax   = (Args.Num() >= 4) ? FCString::Atoi(*Args[3]) : (Old ? Old->MaxLevel : 2);
		const int32 RetCode  = (Args.Num() >= 5) ? FCString::Atoi(*Args[4]) : 0;
		const int32 RetQty   = (Args.Num() >= 6) ? FCString::Atoi(*Args[5]) : 0;
		// ★ волна 8 (блокер-2): сумка и ячейка возврата — аргументами, иначе запись некуда класть.
		const int32 RetBag   = (Args.Num() >= 7) ? FCString::Atoi(*Args[6]) : 2;
		const int32 RetCell  = (Args.Num() >= 8) ? FCString::Atoi(*Args[7]) : -1;
		// ★ «1» девятым аргументом ⇒ заполнить ВСЮ 57-байтовую запись узнаваемым узором:
		//   прочность, вес стопки, четыре гнезда самоцветов, ранг, процент и пять пар заточки.
		//   Без этого потеря 53 байт была недоказуема живым прогоном: стенд клал в тело
		//   те же четыре поля, что читал `DebugPutItem`, и разницы в ячейке не возникало.
		const bool bFullRec  = (Args.Num() >= 9) && (FCString::Atoi(*Args[8]) != 0);

		// Тело 110 б — раскладка из шапки `FAsdaPetLimitBreakEcho`.
		TArray<uint8> Body;
		Body.SetNumZeroed(110);
		AsdaBytes::WrI32(Body.GetData() + 0, Uid);                       // +0  petUid (★ не charId)
		// PetRecord(41) с +4: порода, имя, сытость, уровень, предел, опыт.
		if (Old)
		{
			AsdaBytes::WrI16(Body.GetData() + 4, (int16)Old->PetIndex);   // +4  PetIndex
			// Имя — cp1251 без терминатора, ровно как на проводе (единственный энкодер порта).
			FAsdaLoginData::EncodeCp1251(Old->Name, Body.GetData() + 6, 16);      // +6  имя[16]
			Body[22] = (uint8)FMath::Clamp(Old->Fullness, 0, 255);       // +22 сытость
			AsdaBytes::WrI16(Body.GetData() + 25, (int16)Old->Exp);      // +25 опыт
		}
		Body[23] = (uint8)FMath::Clamp(NewLevel, 0, 255);                // +23 уровень
		Body[24] = (uint8)FMath::Clamp(NewMax, 0, 255);                  // +24 предел
		AsdaBytes::WrI16(Body.GetData() + 45, (int16)-1);                // +45 слот зелья (−1 = нет)
		Body[47] = 0;                                                     // +47 сумка зелья
		Body[48] = (uint8)FMath::Clamp(Code, 0, 255);                    // +48 код результата
		AsdaBytes::WrI32(Body.GetData() + 49, 0);                        // +49 вес
		AsdaBytes::WrI32(Body.GetData() + 53, RetCode);                  // +53 Item+0 код возврата
		Body[57] = (uint8)FMath::Clamp(RetBag, 0, 255);                  // +57 Item+4 сумка
		AsdaBytes::WrI16(Body.GetData() + 58, (int16)RetCell);           // +58 Item+5 ячейка
		AsdaBytes::WrI32(Body.GetData() + 62, RetQty);                   // +62 Item+9 количество
		// ★★★ ВОЛНА 8 (блокер-2): ОСТАЛЬНЫЕ 53 БАЙТА ЗАПИСИ ТАЛИСМАНА.
		//   Раскладка — `FAsdaItemInstance::SyncFromRaw` (`AsdaInventorySubsystem.cpp:324-347`);
		//   Item(57) в теле 6149 начинается с +53 (`npc_mob_pet.c:7795`).
		if (bFullRec && RetQty > 0)
		{
			constexpr int32 IOfs = 53;
			AsdaBytes::WrI16(Body.GetData() + IOfs + 7,  1);      // +7  второй индекс
			Body[IOfs + 13] = 77;                                  // +13 ПРОЧНОСТЬ
			AsdaBytes::WrI16(Body.GetData() + IOfs + 14, 55);      // +14 вес стопки
			AsdaBytes::WrI16(Body.GetData() + IOfs + 16, 201);     // +16 гнездо самоцвета 0
			AsdaBytes::WrI16(Body.GetData() + IOfs + 18, 202);     // +18 гнездо 1
			AsdaBytes::WrI16(Body.GetData() + IOfs + 20, 203);     // +20 гнездо 2
			AsdaBytes::WrI16(Body.GetData() + IOfs + 22, 204);     // +22 гнездо 3
			Body[IOfs + 24] = 6;                                   // +24 ранг самоцвета
			AsdaBytes::WrI16(Body.GetData() + IOfs + 27, 88);      // +27 процент заточки
			for (int32 e = 0; e < 5; ++e)                          // +29 пять пар заточки
			{
				AsdaBytes::WrI16(Body.GetData() + IOfs + 29 + 4 * e, (int16)(310 + e));
				AsdaBytes::WrI16(Body.GetData() + IOfs + 31 + 4 * e, (int16)(410 + e));
			}
		}

		P->OnPetPacket(OP_PET_LIMITBREAK_RESULT, Body);
		UE_LOG(LogAsdaPetGrowth, Log,
			TEXT("[питомец-рост] синтез 6149: код=%d uid=%d уровень=%d предел=%d возврат=(код %d, штук %d)"),
			Code, Uid, NewLevel, NewMax, RetCode, RetQty);
	}));
#endif // ASDA_DEV_CONSOLE
