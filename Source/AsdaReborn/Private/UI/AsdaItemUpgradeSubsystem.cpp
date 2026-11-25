// Copyright (c) 2026 @sitxovski (Discord)
// Реализация окна «Улучшение снаряжения» (заточка) `Item_Upgrade_Win` (JUI 20000) и попапа 19100.
//   Весь разбор эталона, адреса и флаги — в шапке `AsdaItemUpgradeSubsystem.h` и в отчёте
//   `Reports/gear_upgrade_re_report.md`; отчёт захода — `Reports/gear_upgrade/wave2-4_client.md`.
//   Структурный дамп ассетов (каждый контрол) — `Reports/gear_upgrade/_itemupgrade_wbp_tree.txt`
//   (зонд `Tools/_probe_itemupgrade_widgets.py`).
#include "UI/AsdaItemUpgradeSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b (14.11.2026): области замера времени игрового потока


#include "AsdaItemUpgradeSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaItemUpgradeSubsystem*.cpp

DEFINE_LOG_CATEGORY(LogAsdaUpgrade);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей
// ★ U21 (В1 дедупа №2): имя константы — по контейнеру; значение обязано совпадать с размером контейнера инвентаря.
static_assert(UAsdaItemUpgradeSubsystem::kShellCells == UAsdaInventorySubsystem::kShellSlots, "размер контейнера разошёлся с инвентарём");

// ★ Д-11 + N-6. Панель «+N%» (20192…20195) в оригинале МЕРТВА: обе ветки `CItemWindow_UpdateItemInfo`
//   прячут 20193/20194 (item_inventory.c:44570-44584), порог в коде 77 при подсказке «+88%», контрол
//   20195 (подложка полосы и носитель подсказки 110112) не включает никто. Исправление: порог 88
//   (он живёт на сервере — `Recv_User_Disassemble`, GameServer.c:98327), 20193 с текстом «+(88 − %)»
//   при проценте ниже порога, подложка 20195 с подсказкой — пока выбран предмет.
//   ⚠ Ограничения ассета (подробно — в отчёте): полоса 20193 ПЕРЕКРЫВАЕТСЯ кнопкой «Особый» 20196/20197
//   (обе стоят в одной строке y = 341…372 в `.JUI`), а у 20194 глифы русского атласа — обрезки чужой
//   надписи. Поэтому 20194 не включаем никогда, а вся панель — за этим ключом.
// ★★★ Ревизия R-3 (25.09.2026): УМОЛЧАНИЕ 0. Включённая панель ВИДИМО НЕВЕРНА до правки АССЕТА:
//   глифы 20193 в русском атласе `NE_Booster_Chimney80` — «Отменить привязку предмета … Осталось %.»
//   (игрок прочёл бы «Отменить привязку предмета +11 Осталось %.»), а кнопка «Особый» 20196/20197
//   перекрывает полосу, из-под неё торчит хвост глифа. Разметку рантаймом не чиним (закон
//   no-runtime-widget-fixes) — ждём решения владельца по ассету (сдвиг полосы/подложки, замена глифов);
//   после правки ассета умолчание вернуть в 1.
ASDA_DEV_CVAR(int32, CVarEnhancePanel,
	TEXT("asda.Upgrade.EnhancePanel"), 0,
	TEXT("Дефекты оригинала Д-11 + N-6: 1 = панель «+N% до переноса» (20193, порог 88) и подложка 20195 "
	     "с подсказкой 110112 видны; 0 (умолчание до правки ассета владельцем) = как оригинал — панель мертва."),
	ECVF_Default);

// ★ Д-17. Подсказка 20013 у вещи с пределом 0: эталон ставит 110117/110118 («уровень улучшения достиг предела…»),
//   потому что `+26 == +24 == 0`. Это вводит в заблуждение: вещь не точится вовсе (клик даёт 50011).
ASDA_DEV_CVAR(int32, CVarHonestNoUpgradeGuide,
	TEXT("asda.Upgrade.HonestNoUpgradeGuide"), 1,
	TEXT("Дефект оригинала Д-17: 1 = у вещи с пределом попыток 0 подсказка окна — 50011 «Предмет нельзя улучшить»; "
	     "0 = как оригинал — «уровень улучшения достиг предела…» (110117/110118)."),
	ECVF_Default);

// ★ Д-18. Сетка 20000…20029 у эталона (`uiRefreshInventorySlots@0xD29A20`) берёт из сумки снаряжения ВСЁ, кроме подтипов
//   66/200/201, — тип предмета не смотрит. Сервер же точит только типы 0…4, 8, 9 (`Recv_User_ShellItemUpgrade(22)`,
//   GameServer.c:98868-98905, иначе код 7 → 50011). Ивентовые вещи (подарочные корзины, флаги, ткань — тип 11) попадали
//   в окно и в слот цели (баг 30.10, Caroling Gift Basket 75538: стоковый сервер считает этот код ботинками, клиент — корзиной).
ASDA_DEV_CVAR(int32, CVarGearOnlyGrid,
	TEXT("asda.Upgrade.GearOnlyGrid"), 1,
	TEXT("Дефект оригинала Д-18: 1 = в сетке окна улучшения только снаряжение (тип 0…4, 8, 9 — маска сервера); "
	     "0 = как оригинал — всё из сумки снаряжения, кроме подтипов 66/200/201."),
	ECVF_Default);

// ★ N-2 (клиентская часть). Замок «пакет в полёте» у эталона — байт +3394 (отправитель ставит 0,
//   ответ 6505 — 1), но `uiCombineWindowResetOpen` при КАЖДОМ открытии ставит его обратно в 1, то есть
//   «закрыл-открыл окно» = второй 6504 до ответа на первый = два броска ГСЧ по одному предмету.
ASDA_DEV_CVAR(int32, CVarSingleInFlight,
	TEXT("asda.Upgrade.SingleInFlight"), 1,
	TEXT("Дефект оригинала N-2: 1 = пока 6504 без ответа 6505, новую попытку не начинать (замок "
	     "переживает переоткрытие окна; снимают ответ, разрыв и таймаут); 0 = как оригинал."),
	ECVF_Default);

// ★ N-2: закон «не защёлкивать состояние заранее» — молчание сервера не должно давать вечный тупик.
//   4 ветки сервера (сбой секрета/MD5/поиска пользователя) не отвечают вовсе.
// ★ Ревизия R-5: значение СВЯЗАНО с серверным замком патча P33 (`LockTimeoutMs` = 15 000 мс,
//   `AstoryServer/Patches/AsdaPatch/src/patches/p33_gear_upgrade_server_defects.cpp:93/513`). Клиент обязан ждать
//   ДОЛЬШЕ сервера: иначе при медленной БД (10–15 с) повторная попытка уходит, сервер отбивает её
//   кодом 0 (молча), а без P33 — второй бросок. 20 с = 15 с сервера + запас 5 с.
static ASDA_DEV_CVAR(float, CVarReplyTimeoutSec,
	TEXT("asda.Upgrade.ReplyTimeoutSec"), 20.f,
	TEXT("Секунд ожидания ответа 6505, после которых замок «пакет в полёте» снимается. "
	     "0 = без таймаута (снимают только ответ и разрыв)."),
	ECVF_Default);

// ★ Наше исправление (найдено при переносе): текст результата живёт в буфере +3405 и выводится ТОЛЬКО
//   по событию «анимация 20004/20005 доиграла». Если анимацию срезал тик проверки выбора (он гасит все
//   эффекты, когда список свитков сдвинулся после списания последнего свитка) или окно спрятано и план
//   не рисуется — в оригинале сообщение теряется. Ключ включает гарантированную доставку.
ASDA_DEV_CVAR(int32, CVarResultNeverLost,
	TEXT("asda.Upgrade.ResultNeverLost"), 1,
	TEXT("1 = текст результата и попап доставляются, даже если анимацию 20004/20005 срезали или окно "
	     "спрятано (крайний срок = длительность анимации + 1.5 с); 0 = как оригинал."),
	ECVF_Default);

namespace {
	const TCHAR* kWindowWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Item_Upgrade_Win.WBP_Item_Upgrade_Win_C");
	const TCHAR* kPopupWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Craft/WBP_Get_UpgradeOtion.WBP_Get_UpgradeOtion_C");


	// Имя idc JUI для списка ячеек: «<префикс><NN>» / «<префикс><N>».
	FString CellIdc(const TCHAR* Prefix, int32 N, bool bTwoDigits)
	{
		return bTwoDigits ? FString::Printf(TEXT("%s%02d"), Prefix, N) : FString::Printf(TEXT("%s%d"), Prefix, N);
	}
}

// ═════════════════════════════ ЖИЗНЕННЫЙ ЦИКЛ ═════════════════════════════════════════════════
void UAsdaItemUpgradeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		// ★ ESC (событие 21, `CGameWindow_OnCommand` case 0x15, ui_core.c:47996-48010):
		//   `cmdId == 0x4E20` (20000) → `uiCombineWindow_Close` — тумблер при ВИДИМОМ окне = только спрятать;
		//   `cmdId == 0x4A9C` (19100) → `CControl_SetVisible(self[657], 0)` — спрятать попап.
		WM->RegisterEscCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaItemUpgradeSubsystem::HideWindow));
		WM->RegisterEscCloser(kPopupId,  FSimpleDelegate::CreateUObject(this, &UAsdaItemUpgradeSubsystem::HidePopup));
		// ★ `uiCloseAllWindows@0xD61960` (ui_core.c:54381): `if (sub_939058(g_pInventoryWnd) == 1)
		//   uiCombineWindow_Close(g_pInventoryWnd)` — видимое окно заточки прячется. Закрыватель идемпотентен.
		WM->RegisterWindowCloser(kWindowId, FSimpleDelegate::CreateUObject(this, &UAsdaItemUpgradeSubsystem::HideWindow));
		// ★★★ [ПОРТ] 29.10.2026 (инвентарь ×2): КОЛЕСО НАД СЕТКОЙ 20020…20049 — прокрутка по рядам (`BagScroll`, шапка
		//   `AsdaBagListScroll.h`). Сетка эталона 30 без страниц; при 60 ячейках сумки вещи сверх 30-й были бы недостижимы.
		//   Колесо съедается только над ячейками сетки (список свитков 20050…20055 и прочее окно — как было).
		WM->RegisterWheelHandler(kWindowId, FAsdaUiWheelHandler::CreateWeakLambda(this, [this](int32 Notches) -> bool
		{
			if (!bOpen || !bControlsReady) { return false; }
			bool bOver = false;
			for (const TObjectPtr<UImage>& I : GridIcons) { if (I && AsdaUi::IsCursorOverWidget(I)) { bOver = true; break; } }
			if (!bOver) { return false; }
			if (BagScroll.Wheel(Notches))
			{
				TipKind = TipIdx = -1;   // плашка прежней позиции — к другой вещи
				if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr)
				{
					Tip->HideTooltipFor(EAsdaTipOwner::ItemUpgrade);
				}
				RefreshGridView();
			}
			return true;
		}));
	}
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		// Закон `no-reentrant-net-callbacks`: из колбэка только КЛАДЁМ тело в очередь.
		HUpgradeResult = Net->OnItemUpgradeResult.AddLambda([this](const TArray<uint8>& B) { PendingResults.Push(B); });
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		// Один ответ двигает до четырёх ячеек — пересобираем списки один раз в Tick.
		HInvCell = Inv->OnCellChanged.AddLambda([this](int32, int32) { bDirty = true; });
	}
	FxGroups.SetNum((int32)EAsdaUpgradeFx::Count);
	FxPlanes.SetNum((int32)EAsdaUpgradeFx::Count);
	UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] подсистема окна 20000 поднята"));
}

void UAsdaItemUpgradeSubsystem::Deinitialize()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr)
	{
		Net->OnItemUpgradeResult.Remove(HUpgradeResult);
	}
	if (UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr)
	{
		Inv->OnCellChanged.Remove(HInvCell);
	}
	ResetPopup();
	ResetWidget();
	Super::Deinitialize();
}

// ★ Сброс состояния сессии: подсистема живёт на GameInstance и переживает разрыв, выход в выбор
//   персонажа и травел. Замок N-2 снимается ЗДЕСЬ при разрыве — ответ на старый 6504 уже не придёт.
void UAsdaItemUpgradeSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	HidePopup();
	HideWindow();
	bLive = false;
	bIdle = true;
	bArmed = false;
	bConfirmOpen = false;
	ReleaseInFlight(AsdaSessionResetReasonName(Reason));
	Target.Clear(); Scroll.Clear(); Helper97.Clear(); Helper98.Clear();
	TargetIdx = ScrollIdx = H97Idx = H98Idx = -1;
	GridList.Reset(); ScrollList.Reset(); H97List.Reset(); H98List.Reset();
	BagScroll.Reset();   // ★ 29.10.2026 (инвентарь ×2)
	SavedScrollSlot = SavedTargetSlot = -1;
	SavedScrollContainer = 0;
	ResultText.Reset();
	ResultRecord.Reset();
	bResultPending = false;
	bIconHover = false;
	for (double& D : FxDeadline) { D = 0.0; }
	PendingResults.Clear();
	TipKind = TipIdx = -1;
	LastValidateTime = -1.0;
	Mouse.Reset();
}

void UAsdaItemUpgradeSubsystem::Tick(float /*DeltaTime*/)
{
	ASDA_PERF_SCOPE("Tick.ItemUpgrade");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	DrainPendingPackets();
	const double Now = FPlatformTime::Seconds();

	// ★ N-2: таймаут замка «пакет в полёте» — работает и при закрытом окне.
	if (ReplyLatch.IsExpired(CVarReplyTimeoutSec.GetValueOnGameThread()))   // ★ S2 (В2-A дедупа №2, 11.10.2026): та же формула
	{
		// Эффект ожидания и флаги — как у ответа с кодом «ничего»: окно снова свободно.
		bIdle = true;
		bArmed = false;
		ReleaseInFlight(TEXT("таймаут ответа 6505"));
	}

	// ★ Крайние сроки одноразовых эффектов (ResultNeverLost): план не рисуется (окно спрятано) или
	//   данных анимации нет — событие «доиграла» дадим сами, чтобы цепочка не встала навсегда.
	if (CVarResultNeverLost.GetValueOnGameThread() != 0)
	{
		for (EAsdaUpgradeFx Fx : { EAsdaUpgradeFx::Effect1, EAsdaUpgradeFx::Success, EAsdaUpgradeFx::FailAni })
		{
			const double D = FxDeadline[(int32)Fx];
			if (D > 0.0 && Now > D)
			{
				UE_LOG(LogAsdaUpgrade, Log, TEXT("[заточка] эффект %d не доложил о конце в срок — событие 0x18 даём сами"), (int32)Fx);
				OnEffectFinished(Fx);
			}
		}
	}

	if (!bControlsReady) { return; }

	// Тик проверки выбора эталона (`game_main.c:1478`) идёт, пока окно «живо» (+3392), в том числе
	//   спрятанное тумблером — ровно как у оригинала (флаг снимает только `uiHidePopups`).
	if (bLive && (LastValidateTime < 0.0 || (Now - LastValidateTime) * 1000.0 > (double)kTickValidateMs))
	{
		LastValidateTime = Now;
		TickValidateSelection();
	}

	if (!bOpen) { return; }
	if (bDirty)
	{
		bDirty = false;
		RefreshInventorySlots();
		RefreshScrolls();
		RedrawCards();
	}
	Mouse.Update();
	// ★ Модальный гейт (П8-9): под модальным нотисом эталон не отдаёт мышь окнам ниже.
	if (AsdaUi::IsModalDispatchBlocked(GetGameInstance()))
	{
		if (TipKind >= 0)
		{
			TipKind = TipIdx = -1;
			if (UAsdaItemTooltipSubsystem* Tip = GetGameInstance()->GetSubsystem<UAsdaItemTooltipSubsystem>())
			{
				Tip->HideTooltipFor(EAsdaTipOwner::ItemUpgrade);
			}
		}
		Mouse.Reset();
		return;
	}
	TickMouse();
}

// ═════════════════════════════ ОКНО ═══════════════════════════════════════════════════════════
bool UAsdaItemUpgradeSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWindowWbp, kUpgradeZ).Ensure(GetGameInstance(), Widget, bControlsReady,
		[this](UAsdaWindowSubsystem&) { ResetWidget(); },
		[this](UAsdaWindowSubsystem&) { SetupControls(); RegisterZones(); },
		[]() { UE_LOG(LogAsdaUpgrade, Error, TEXT("[заточка] WBP_Item_Upgrade_Win не загрузился (%s)"), kWindowWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bControlsReady;
}

#if !UE_BUILD_SHIPPING
// ★ 31.10.2026 самотест GC-якорей (`asda.SelfTest.RelayAnchors`) — разбор у объявления.
UButton* UAsdaItemUpgradeSubsystem::DebugRecreateWindowKeepPopup()
{
	if (!EnsureWidget() || !EnsurePopup()) { return nullptr; }
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->CloseWindow(kWindowId);   // окно заточки больше не живо, попап жив
	}
	EnsureWidget();   // штатный `Ensure`: окно не живо ⇒ `ResetWidget` (сброс его якоря) + разметка заново
	return PopOkBtn;
}
#endif // !UE_BUILD_SHIPPING

bool UAsdaItemUpgradeSubsystem::EnsurePopup()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kPopupId, kPopupWbp, kPopupZ).Ensure(GetGameInstance(), PopupWidget, bPopupReady,
		[this](UAsdaWindowSubsystem&) { ResetPopup(); },
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupPopupControls();
			// Попап держится НАД окном заточки при любых подъёмах (эталон поднимает его `CClass_MapInsert`).
			WM.SetWindowOwner(kPopupId, kWindowId);
			FAsdaJuiWindowHandle::RegisterFrameZone(WM, kPopupId, this, PopupWidget, bPopupOpen, TEXT("Get_UpgradeOtion_0_%d"), 9);
		},
		[]() { UE_LOG(LogAsdaUpgrade, Error, TEXT("[заточка] WBP_Get_UpgradeOtion не загрузился (%s)"), kPopupWbp); });
	return FAsdaJuiWindowHandle::Succeeded(R) && bPopupReady;
}

void UAsdaItemUpgradeSubsystem::ResetWidget()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kWindowId);
		WM->CloseWindow(kWindowId);
	}
	Widget = nullptr;
	bControlsReady = false;
	bOpen = false;
	BtnClose = BtnUpgrade = BtnCancel = BtnSpecial = nullptr;
	BtnCardClear.Reset();
	TxtTitle = TxtLevelLbl = TxtLevelBef = TxtLevelAft = TxtEffectLbl = TxtEffectBef = TxtEffectAft = nullptr;
	TxtTriesLbl = TxtTriesBef = TxtTriesAft = TxtTabGear = TxtTabScroll = TxtTabHelpers = nullptr;
	TxtCardTitle.Reset();
	TxtBtnUpgrade = TxtBtnCancel = TxtBtnSpecial = TxtPanelValue = nullptr;
	FmtName = FAsdaFmtText();
	FmtGuide = FAsdaFmtText();
	GridIcons.Reset(); GridLabels.Reset(); ScrollIcons.Reset(); ScrollLabels.Reset();
	H97Icons.Reset(); H97Labels.Reset(); H98Icons.Reset(); H98Labels.Reset(); CardIcons.Reset();
	TargetIcon = nullptr;
	FrameItemSel = FrameScrollSel = FrameSubSel0 = FrameSubSel1 = nullptr;
	DisableGroups.Reset();
	for (TArray<TObjectPtr<UWidget>>& G : FxGroups) { G.Reset(); }
	for (TObjectPtr<UAsdaAnimatedPlane>& P : FxPlanes) { P = nullptr; }
	Panel20193 = Panel20194 = Panel20195 = Panel20196 = DragHandle20014 = Group20080 = nullptr;
	GridIconsGrid = ScrollGrid = H97Grid = H98Grid = CardGrid = TargetGrid = nullptr;
	Relays.Reset();
	GridLabelBase.Reset(); H97LabelBase.Reset(); H98LabelBase.Reset();
	Mouse.Reset();
}

void UAsdaItemUpgradeSubsystem::ResetPopup()
{
	UGameInstance* GI = GetGameInstance();
	if (UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->UnregisterUiZone(kPopupId);
		WM->CloseWindow(kPopupId);
	}
	PopupWidget = nullptr;
	bPopupReady = false;
	bPopupOpen = false;
	PopTitle = PopText = PopIconLabel = PopOptTitle = PopOkText = nullptr;
	PopIcon = nullptr;
	PopOkBtn = nullptr;
	FmtPopOpt = FAsdaFmtText();
	PopIconGrid = nullptr;
	PopupRelays.Reset();   // ★ 31.10.2026: якорь «ОК» 19106 живёт и умирает вместе с попапом (`asda.UI.RelayAnchorSplit`)
}

void UAsdaItemUpgradeSubsystem::RegisterZones()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Рамка окна — девять планов `Item_Upgrade_Win_0_0…8` (скин `Black_BottomRound_White2Border`).
	FAsdaJuiWindowHandle::RegisterFrameZone(*WM, kWindowId, this, Widget, bOpen, TEXT("Item_Upgrade_Win_0_%d"), 9);
}

void UAsdaItemUpgradeSubsystem::CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const
{
	// ★ R5-b 12.11.2026: тело — общая точка `AsdaUi::CollectByIdc` (дословно то же).
	AsdaUi::CollectByIdc(Widget, Idc, Out);
}

void UAsdaItemUpgradeSubsystem::SetGroupVisible(const TArray<TObjectPtr<UWidget>>& Group, bool bVisible)
{
	// Декор/маски — `HitTestInvisible`: они не должны съедать клики по ячейкам под ними.
	//   U3 (дедуп №2, В2-A, 11.10.2026): правило «показать без хит-теста» — общая `AsdaUi::SetShownNoHit` (вынос, поведение то же).
	for (const TObjectPtr<UWidget>& W : Group) { AsdaUi::SetShownNoHit(W, bVisible); }
}

void UAsdaItemUpgradeSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!Widget || !Widget->WidgetTree) { return; }

	GridIcons.SetNum(kGridCells);   GridLabels.SetNum(kGridCells);
	ScrollIcons.SetNum(kScrollCells); ScrollLabels.SetNum(kScrollCells);
	H97Icons.SetNum(kHelperCells);  H97Labels.SetNum(kHelperCells);
	H98Icons.SetNum(kHelperCells);  H98Labels.SetNum(kHelperCells);
	CardIcons.SetNum(3);            BtnCardClear.SetNum(3);  TxtCardTitle.SetNum(3);
	TArray<FName> GridNames;   GridNames.SetNum(kGridCells);
	TArray<FName> ScrollNames; ScrollNames.SetNum(kScrollCells);
	TArray<FName> H97Names;    H97Names.SetNum(kHelperCells);
	TArray<FName> H98Names;    H98Names.SetNum(kHelperCells);
	TArray<FName> CardNames;   CardNames.SetNum(3);
	FName TargetName;

	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();

		if (UButton* Btn = Cast<UButton>(W))
		{
			if (N == TEXT("Item_Upgrade_CloseBtn")) { BtnClose   = Btn; continue; }   // 20002
			if (N == TEXT("Item_Upgrade_Btn"))      { BtnUpgrade = Btn; continue; }   // 20190
			if (N == TEXT("Item_Upgrade_ClsBtn2"))  { BtnCancel  = Btn; continue; }   // 20191
			if (N == TEXT("Option_st"))             { BtnSpecial = Btn; continue; }   // 20197
			for (int32 K = 0; K < 3; ++K)
			{
				if (N == FString::Printf(TEXT("Item_Upgrade_MatClaBtn%d"), K)) { BtnCardClear[K] = Btn; }   // 20152/55/58
			}
			continue;
		}
		if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			if (N == TEXT("Item_Upgrade_Title"))          { TxtTitle     = Tb; continue; }   // 20001
			if (N == TEXT("Item_Upgrade_Name"))           { CaptureFmt(FmtName, Tb); continue; }   // 20006
			if (N == TEXT("Item_Upgrade_Grade"))          { TxtLevelLbl  = Tb; continue; }   // 20007
			if (N == TEXT("Item_Upgrade_GradeBefore"))    { TxtLevelBef  = Tb; continue; }   // 20008
			if (N == TEXT("Item_Upgrade_GradeAfter"))     { TxtLevelAft  = Tb; continue; }   // 20009
			if (N == TEXT("Item_Upgrade_Power"))          { TxtEffectLbl = Tb; continue; }   // 20010
			if (N == TEXT("Item_Upgrade_PowerBefore"))    { TxtEffectBef = Tb; continue; }   // 20011
			if (N == TEXT("Item_Upgrade_PowerAfter"))     { TxtEffectAft = Tb; continue; }   // 20012
			if (N == TEXT("Item_Upgrade_Chance"))         { TxtTriesLbl  = Tb; continue; }   // 20015
			if (N == TEXT("Item_Upgrade_hanceBefore"))    { TxtTriesBef  = Tb; continue; }   // 20016
			if (N == TEXT("Item_Upgrade_hanceAfter"))     { TxtTriesAft  = Tb; continue; }   // 20017
			if (N == TEXT("Item_Upgrade_GuideTxt"))       { CaptureFmt(FmtGuide, Tb); continue; }   // 20013
			if (N == TEXT("Item_Upgrade_ItemSec_Text"))   { TxtTabGear   = Tb; continue; }   // 20066
			if (N == TEXT("Item_Ugrade_ScrollTitle_Text")){ TxtTabScroll = Tb; continue; }   // 20065
			if (N == TEXT("Item_Ugrade_SubTitle_Text"))   { TxtTabHelpers= Tb; continue; }   // 20142
			if (N == TEXT("Item_Upgrade_Btn_Text"))       { TxtBtnUpgrade= Tb; continue; }
			if (N == TEXT("Item_Upgrade_ClsBtn2_Text"))   { TxtBtnCancel = Tb; continue; }
			if (N == TEXT("Option_st_Text"))              { TxtBtnSpecial= Tb; continue; }
			if (N == TEXT("Item_Upgrade_p_text_Text"))    { TxtPanelValue= Tb; continue; }   // 20192
			for (int32 K = 0; K < 3; ++K)
			{
				if (N == FString::Printf(TEXT("Item_Upgrade_MatTitle%d"), K)) { TxtCardTitle[K] = Tb; }   // 20151/54/57
			}
			// Подписи ячеек: импортёр зовёт `TextBlock` ровно как idc контрола.
			for (int32 C = 0; C < kGridCells; ++C)   { if (N == CellIdc(TEXT("Item_Upgrade_Item"), C, true))  { GridLabels[C]   = Tb; } }
			for (int32 C = 0; C < kScrollCells; ++C) { if (N == CellIdc(TEXT("Item_Ugrade_Scroll"), C, true)) { ScrollLabels[C] = Tb; } }
			for (int32 C = 0; C < kHelperCells; ++C)
			{
				if (N == CellIdc(TEXT("Item_Ugrade_Sub0_"), C, false)) { H97Labels[C] = Tb; }
				if (N == CellIdc(TEXT("Item_Ugrade_Sub1_"), C, false)) { H98Labels[C] = Tb; }
			}
			continue;
		}

		// Узлы-группы без idnum-кода (полоса «+N%» и подложка «Особого») — имена из дампа ассета.
		if (N == TEXT("NoImage_icon_grp"))          { Panel20193 = W; continue; }   // 20193
		if (N == TEXT("GFONT_item_S_grp"))          { Panel20194 = W; continue; }   // 20194
		if (N == TEXT("Box_Brown_226_0"))           { Panel20195 = W; continue; }   // 20195
		if (N == TEXT("Option_st_w_grp"))           { Panel20196 = W; continue; }   // 20196
		if (N == TEXT("Item_Upgrade_Move_214_DRAG")){ DragHandle20014 = W; continue; }   // 20014 (ручка)
		if (N == TEXT("Item_Ugrade_SubMat_grp"))    { Group20080 = W; continue; }   // 20080

		// Планы `<idc>_<juiIdx>_<plane>`.
		FString Idc; int32 JuiIdx = 0, Plane = 0;
		if (!AsdaUi::ParsePlaneName(N, Idc, JuiIdx, Plane) || Plane != 0) { continue; }
		if (Idc == TEXT("Item_Upgrade_ItemSel"))  { FrameItemSel   = W; BaseItemSel   = SlotPos(W); continue; }   // 20060
		if (Idc == TEXT("Item_Ugrade_ScrollSel")) { FrameScrollSel = W; BaseScrollSel = SlotPos(W); continue; }   // 20058
		if (Idc == TEXT("Item_Ugrade_SubSel0"))   { FrameSubSel0   = W; BaseSubSel0   = SlotPos(W); continue; }   // 20140
		if (Idc == TEXT("Item_Ugrade_SubSel1"))   { FrameSubSel1   = W; BaseSubSel1   = SlotPos(W); continue; }   // 20141
		UImage* Img = Cast<UImage>(W);
		if (!Img) { continue; }
		if (Idc == TEXT("Item_Upgrade_Icon")) { TargetIcon = Img; TargetName = FName(*N); continue; }   // 20003
		for (int32 K = 0; K < 3; ++K)
		{
			if (Idc == FString::Printf(TEXT("Item_Upgrade_MatIcon%d"), K)) { CardIcons[K] = Img; CardNames[K] = FName(*N); }
		}
		for (int32 C = 0; C < kGridCells; ++C)
		{
			if (Idc == CellIdc(TEXT("Item_Upgrade_Item"), C, true)) { GridIcons[C] = Img; GridNames[C] = FName(*N); }
		}
		for (int32 C = 0; C < kScrollCells; ++C)
		{
			if (Idc == CellIdc(TEXT("Item_Ugrade_Scroll"), C, true)) { ScrollIcons[C] = Img; ScrollNames[C] = FName(*N); }
		}
		for (int32 C = 0; C < kHelperCells; ++C)
		{
			if (Idc == CellIdc(TEXT("Item_Ugrade_Sub0_"), C, false)) { H97Icons[C] = Img; H97Names[C] = FName(*N); }
			if (Idc == CellIdc(TEXT("Item_Ugrade_Sub1_"), C, false)) { H98Icons[C] = Img; H98Names[C] = FName(*N); }
		}
	}

	// ── Затемнители 20100…20129 и шесть эффектов иконки — целыми поддеревьями ──
	DisableGroups.SetNum(kGridCells);
	for (int32 C = 0; C < kGridCells; ++C) { CollectByIdc(CellIdc(TEXT("Item_Upgrade_Disable"), C, true), DisableGroups[C]); }
	static const TCHAR* kFxIdc[(int32)EAsdaUpgradeFx::Count] = {
		TEXT("Item_Upgrade_Effect1"), TEXT("Item_Upgrade_Effect2"), TEXT("Item_Upgrade_Click"),
		TEXT("Item_Upgrade_Success"), TEXT("Item_Upgrade_Fail_Ani"), TEXT("Item_Upgrade_Fail") };
	FxGroups.SetNum((int32)EAsdaUpgradeFx::Count);
	FxPlanes.SetNum((int32)EAsdaUpgradeFx::Count);
	for (int32 F = 0; F < (int32)EAsdaUpgradeFx::Count; ++F)
	{
		CollectByIdc(kFxIdc[F], FxGroups[F]);
		FxPlanes[F] = nullptr;
		for (const TObjectPtr<UWidget>& W : FxGroups[F])
		{
			if (UAsdaAnimatedPlane* P = Cast<UAsdaAnimatedPlane>(W)) { FxPlanes[F] = P; }
		}
	}
	// ★ Событие 0x18 «анимация доиграла» — только у трёх одноразовых. Ветки конца 20071/20072 в эталоне
	//   мёртвые (Animate = 0 / EFrameStop = 0 — события им не приходит), не переносим (§13 отчёта).
	if (UAsdaAnimatedPlane* P = FxPlanes[(int32)EAsdaUpgradeFx::Effect1]) { P->OnAnimFinished.AddUniqueDynamic(this, &UAsdaItemUpgradeSubsystem::HandleEffect1Finished); }
	if (UAsdaAnimatedPlane* P = FxPlanes[(int32)EAsdaUpgradeFx::Success]) { P->OnAnimFinished.AddUniqueDynamic(this, &UAsdaItemUpgradeSubsystem::HandleSuccessFinished); }
	if (UAsdaAnimatedPlane* P = FxPlanes[(int32)EAsdaUpgradeFx::FailAni]) { P->OnAnimFinished.AddUniqueDynamic(this, &UAsdaItemUpgradeSubsystem::HandleFailAniFinished); }

	// ── Сетки иконок через ОБЩУЮ точку. Клик ловим сами по живой геометрии (`TickMouse`). ──
	auto MakeGrid = [this](const TArray<FName>& Names, float Px, TFunction<int32(int32)>&& Code) -> UAsdaIconCellGrid*
	{
		UAsdaIconCellGrid* G = NewObject<UAsdaIconCellGrid>(this);
		G->SetCellSizePx(Px);
		G->SetClickOverlayEnabled(false);
		G->bClearEmptyCells = true;   // эталон затирает иконку пустой текстурой
		G->SetupNamed(Widget, Names, /*bMale*/true, MoveTemp(Code), [](int32) {});
		return G;
	};
	// ★ 29.10.2026 (инвентарь ×2): видимая ячейка → позиция `GridList` через окно прокрутки.
	GridIconsGrid = MakeGrid(GridNames, 32.f, [this](int32 C) { const int32 L = BagScroll.ToList(C); return GridList.IsValidIndex(L) ? GridList[L].ItemCode : 0; });
	ScrollGrid    = MakeGrid(ScrollNames, 32.f, [this](int32 C) { return ScrollList.IsValidIndex(C) ? ScrollList[C].ItemCode : 0; });
	H97Grid       = MakeGrid(H97Names, 32.f, [this](int32 C) { return H97List.IsValidIndex(C) ? H97List[C].ItemCode : 0; });
	H98Grid       = MakeGrid(H98Names, 32.f, [this](int32 C) { return H98List.IsValidIndex(C) ? H98List[C].ItemCode : 0; });
	CardGrid      = MakeGrid(CardNames, 32.f, [this](int32 K)
	{
		const FAsdaItemUpgradeEntry& E = (K == 0) ? Scroll : (K == 1) ? Helper97 : Helper98;
		const FAsdaItemInstance* It = LiveOf(E);
		return (It && !It->IsEmpty()) ? It->ItemCode : 0;
	});
	if (!TargetName.IsNone())
	{
		TArray<FName> One; One.Add(TargetName);
		TargetGrid = MakeGrid(One, 64.f, [this](int32)   // Size = "64 64" у `Item_Upgrade_Icon`
		{
			const FAsdaItemInstance* It = LiveOf(Target);
			return (It && !It->IsEmpty()) ? It->ItemCode : 0;
		});
	}

	// ── Кнопки (эталон — ветка msgType 66561 `CGameWindow_OnCommand`, ui_core.c:47640-47690) ──
	auto Bind = [this](UButton* Btn, int32 IdNum, TFunction<void(int32)>&& Fn)
	{
		if (!Btn) { return; }
		UAsdaButtonRelay* R = NewObject<UAsdaButtonRelay>(this);
		R->Index = IdNum;
		// ★ Волна 5: ветка msgType 66561 эталона играет звук id 5 (`S_e_button06`) на ЛЮБОЙ клик по кнопке
		//   окна (ui_core.c:47649) — ДО разбора cmdId, то есть раньше звуков самих действий.
		R->Callback = [this, IdNum, F = MoveTemp(Fn)](int32 I) { PlayButtonSfx(IdNum); F(I); };
		Btn->OnClicked.AddDynamic(R, &UAsdaButtonRelay::OnClicked);
		AsdaUi::AnchorRelay(Relays, R, Btn);   // ★ 31.10.2026: якорь чистит мёртвые/перепривязанные (`asda.UI.RelayAnchorPrune`)
	};
	Bind(BtnClose,   20002, [this](int32) { OnCancelClicked(); });    // `uiHidePopups`
	Bind(BtnCancel,  20191, [this](int32) { OnCancelClicked(); });    // `uiHidePopups`
	Bind(BtnUpgrade, 20190, [this](int32) { OnUpgradeClicked(); });   // `uiItemUpgradeValidate`
	Bind(BtnSpecial, 20197, [this](int32) { OnSpecialClicked(); });   // `craftWnd_Open` → 78200
	for (int32 K = 0; K < 3; ++K)
	{
		Bind(BtnCardClear[K], 20152 + 3 * K, [this, K](int32) { OnCardClearClicked(K); });
	}

	// ── Статичные подписи (`CItemUpgradeWin_InitControls`) ──
	auto SetLoc = [this](UTextBlock* Tb, int32 LocId)
	{
		if (!Tb) { return; }
		Tb->SetText(FText::FromString(Loc(LocId)));
	};
	SetLoc(TxtTitle,      AsdaItemUpgradeLoc::kTitle);
	SetLoc(TxtLevelLbl,   AsdaItemUpgradeLoc::kLabelLevel);
	SetLoc(TxtEffectLbl,  AsdaItemUpgradeLoc::kLabelEffect);
	SetLoc(TxtTriesLbl,   AsdaItemUpgradeLoc::kLabelTries);
	SetLoc(TxtTabScroll,  AsdaItemUpgradeLoc::kTabScroll);
	SetLoc(TxtTabHelpers, AsdaItemUpgradeLoc::kTabHelpers);
	SetLoc(TxtTabGear,    AsdaItemUpgradeLoc::kTabGear);
	SetLoc(TxtBtnUpgrade, AsdaItemUpgradeLoc::kBtnUpgrade);
	SetLoc(TxtBtnCancel,  AsdaItemUpgradeLoc::kBtnCancel);
	SetLoc(TxtBtnSpecial, AsdaItemUpgradeLoc::kBtnSpecial);
	SetLoc(TxtCardTitle[0], AsdaItemUpgradeLoc::kCardScroll);
	SetLoc(TxtCardTitle[1], AsdaItemUpgradeLoc::kCardHelper97);
	SetLoc(TxtCardTitle[2], AsdaItemUpgradeLoc::kCardHelper98);
	// Эталон пустыми строками затирает имя 20006, гайд 20013 и подписи ячеек (`CControl_SetText(…, &Src)`).
	SetFmt(FmtName, FString());
	SetFmt(FmtGuide, FString());
	GridLabelBase.SetNum(kGridCells); H97LabelBase.SetNum(kHelperCells); H98LabelBase.SetNum(kHelperCells);
	for (int32 C = 0; C < kGridCells; ++C)
	{
		if (GridLabels[C]) { GridLabelBase[C] = GridLabels[C]->GetColorAndOpacity(); GridLabels[C]->SetText(FText::GetEmpty()); }
	}
	for (int32 C = 0; C < kScrollCells; ++C) { if (ScrollLabels[C]) { ScrollLabels[C]->SetText(FText::GetEmpty()); } }
	for (int32 C = 0; C < kHelperCells; ++C)
	{
		if (H97Labels[C]) { H97LabelBase[C] = H97Labels[C]->GetColorAndOpacity(); H97Labels[C]->SetText(FText::GetEmpty()); }
		if (H98Labels[C]) { H98LabelBase[C] = H98Labels[C]->GetColorAndOpacity(); H98Labels[C]->SetText(FText::GetEmpty()); }
	}
	if (TxtLevelAft)  { LevelAftBase  = TxtLevelAft->GetColorAndOpacity(); }
	if (TxtEffectAft) { EffectAftBase = TxtEffectAft->GetColorAndOpacity(); }
	// ★ `CControl_SetVisible(self[935] /*20196*/, 1)` — подложку кнопки «Особый» эталон включает в Init
	//   (в `.JUI` она `Visible = "0"`). Это поведение КОДА оригинала, а не правка разметки.
	if (Panel20196) { Panel20196->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	// Панель «+N%» стартует скрытой (как в `.JUI`); включает её только `UpdateEnhancePanel`.
	UpdateEnhancePanel(0, false);

	bControlsReady = (GridIcons[0] != nullptr && ScrollIcons[0] != nullptr && TargetIcon != nullptr && BtnUpgrade != nullptr);
	UE_LOG(LogAsdaUpgrade, Log, TEXT("%s"), *DumpControls());
	if (!bControlsReady)
	{
		UE_LOG(LogAsdaUpgrade, Error, TEXT("[заточка] ключевые контролы не разобраны: сетка=%s свитки=%s иконка=%s кнопка=%s"),
			GridIcons[0] ? TEXT("есть") : TEXT("НЕТ"), ScrollIcons[0] ? TEXT("есть") : TEXT("НЕТ"),
			TargetIcon ? TEXT("есть") : TEXT("НЕТ"), BtnUpgrade ? TEXT("есть") : TEXT("НЕТ"));
	}
}
