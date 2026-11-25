// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMiniStateSubsystem — реализация. Полный реверс-разбор окна — в шапке AsdaMiniStateSubsystem.h
//   (первоисточник: Reports/ministate_win_re_A.md + машинный дамп _ministate_full_dump.txt).
#include "UI/AsdaMiniStateSubsystem.h"
#include "AsdaPerf.h"   // ★ R4-b 14.11.2026: области замера времени игрового потока

#include "World/AsdaPlayerController.h"

#include "Components/CanvasPanel.h"
// inline-теги <FC=…>…</F> в тексте тултипа 110922   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён
#include "AsdaMiniStateSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaMiniStateSubsystem*.cpp

// ★ Время плавного хода полос здоровья и щитов — ОДНО на весь интерфейс (плашки над персонажами,
//   полосы игрока, панель цели). 0 = мгновенно, как в оригинале 2008 года.
//   Заявка владельца: «плавно, но быстро — чтобы момент лечения нельзя было проморгать».
static ASDA_DEV_CVAR(float, CVarAsdaBarSmoothSec,
	TEXT("asda.UI.BarSmoothSec"), 0.18f,
	TEXT("За сколько секунд полоса здоровья/щита проходит всю шкалу (0 = мгновенно, как в оригинале)."),
	ECVF_Default);

float FAsdaBarSmoother::SmoothSeconds()
{
	return FMath::Max(0.f, CVarAsdaBarSmoothSec.GetValueOnGameThread());
}

DEFINE_LOG_CATEGORY(LogAsdaMini);   // ★ R3-c 12.11.2026: было DEFINE_LOG_CATEGORY_STATIC — категория общая для частей

// Импортированные окна (генератор Tools/asda_jui_generic_umg.py).
static const TCHAR* kMiniWbp = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_MINISTATE_WIN.WBP_MINISTATE_WIN_C");
static const TCHAR* kTipWbp  = TEXT("/Game/AsdaAssets/UI/Player_UI/Common/WBP_ITEM_TIPWIN.WBP_ITEM_TIPWIN_C");
static const FName  kMiniWindowId(TEXT("MINISTATE_WIN"));
static const FName  kTipWindowId(TEXT("MINISTATE_ITEM_TIPWIN"));

// ★ 06.10.2026 [ПОРТ] ИСПРАВЛЕНИЕ ДЕФЕКТА ОРИГИНАЛА: значок главы отряда (9) эталон зажигает только по 5102
//   (`Party_SetMasterLocal`, social.c:419-455) и НЕ гасит ни при роспуске (5103), ни при выходе/исключении
//   (5099/5101) — значок «главы» висел на портрете без отряда до конца сессии. 1 = гасить, как только
//   отряда нет; 0 = как в оригинале (для сверки).
static ASDA_DEV_CVAR(int32, CVarPartyerHideOnLeave,
	TEXT("asda.Mini.PartyerHideOnLeave"),
	1,
	TEXT("Значок главы отряда на портрете: 1 = гаснет, когда отряда нет (исправление дефекта оригинала); "
	     "0 = как в оригинале (остаётся после роспуска/выхода)."),
	ECVF_Default);

static ASDA_DEV_CVAR(int32, CVarPlusPackTipOnHover,
	TEXT("asda.Mini.PlusPackTipOnHover"),
	1,
	TEXT("Описание Plus Package (иконка 5110): 1 = ЭТАЛОН — по наведению курсора (события 8/9/10 "
	     "маршрутизатора uiControlDispatchMouseLeft приходят от ХИТ-ТЕСТА, а не от кнопок мыши); "
	     "0 = по удержанию ПКМ (запасной режим)."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (решение владельца): 1 — свой персонаж берётся целью ОДНИМ кликом по портрету (зона 97); двойной
//   клик тоже работает и не срабатывает дважды; 0 — прежнее (как оригинал: только двойной клик). В Shipping ключа нет (там 1).
static ASDA_DEV_CVAR(int32, CVarAsdaSelfTargetSingleClick,
	TEXT("asda.UI.SelfTargetSingleClick"), 1,
	TEXT("Портрет мини-статуса: 1 — свой персонаж целью одним кликом (на отпускании без сдвига); 0 — как оригинал, только двойной клик."),
	ECVF_Default);
// Порог сдвига мыши между нажатием и отпусканием, пиксели Slate: дальше — это перетаскивание, а не клик.
static constexpr float kSelfClickSlopPx = 4.f;

// ══════════════════════════════ мелкие помощники ═══════════════════════════════════════════════
void UAsdaMiniStateSubsystem::SetVis(UWidget* W, bool bVisible)
{
	if (!W) { return; }
	// SelfHitTestInvisible: узлы мини-статуса ничего не должны отбирать у мира — клики ловим опросом.
	// ★ Волна 1 (14.09.2026, В3): правило видимости — общая точка `AsdaUi::SetShownDecor` (режим тот же).
	AsdaUi::SetShownDecor(W, bVisible);
}

bool UAsdaMiniStateSubsystem::GetSlotRect(const UWidget* W, FVector2D& OutPos, FVector2D& OutSize)
{
	// ★ Аудит №4 (B1): тело переехало в `AsdaUi::SlotRect` — здесь остаётся тонкий форвард
	//   (это же тело жило ПОБАЙТОВО одинаковым ещё у мини-статуса и хранителя душ).
	//   Знание «POINT-анкор импортёра: R/B = размер» теперь одно на порт.
	return AsdaUi::SlotRect(W, OutPos, OutSize);
}

void UAsdaMiniStateSubsystem::SetSlotPos(UWidget* W, const FVector2D& Pos)
{
	// ★ Аудит №4 (B1): тело переехало в `AsdaUi::SetSlotPos` — здесь тонкий форвард.
	AsdaUi::SetSlotPos(W, Pos);
}

// Клип заливки ES1 — закон ui-progressbar-animate-existing: анимируем САМ виджет, обрезая
//   ширину слота плана-заливки, а не подменяя материал/браш.
void UAsdaMiniStateSubsystem::ClipFill(const FAsdaMiniBarFill& Bar, float Frac)
{
	// ★ Волна 3: обрезка заливки — общая точка `FAsdaGaugeFill::Clip`. После `IsValid` (Fill есть, FullW > 0)
	//   её проверка `!S || FullW <= 0` совпадает с прежней `!S` — запись Left/Right та же.
	if (!Bar.IsValid()) { return; }
	FAsdaGaugeFill::Clip(Bar.Fill, Bar.BaseLeft, Bar.FullW, Frac);
}

// ★★★ Выравнивание текста по маске `EXS TextAlign` — тело переехало в `AsdaUi::ApplyJuiTextAlign`.
//   Переезд сделан вместе с правкой дефекта ДВОЙНОГО сдвига (отчёт `ministate_name_align.md`):
//   раньше здешняя копия брала `Slot.Top` как «сырой верх контроля», хотя импортёр уже
//   запекает в него свою центровку для контролей выше 14 px — имя персонажа (17 px) уезжало вниз.
//   Здесь остаётся тонкий форвард с доказательной строкой в лог (её читает живая проверка).
void UAsdaMiniStateSubsystem::ApplyJuiTextAlign(UTextBlock* Tb, int32 AlignMask, float JuiH, float TextPosY)
{
	if (!Tb) { return; }
	UCanvasPanelSlot* S = AsdaUi::SlotOf(Tb);
	const float TopWas = S ? S->GetOffsets().Top : 0.f;
	AsdaUi::ApplyJuiTextAlign(Tb, AlignMask, JuiH, TextPosY);
	const float NewTop = S ? S->GetOffsets().Top : 0.f;
	UE_LOG(LogAsdaMini, Log,
		TEXT("[мини-статус] текст '%s': маска=%d h(JUI)=%.0f строка=%.1f запечённое генератором=%.1f Top %.1f→%.1f, выравнивание=%s"),
		*Tb->GetName(), AlignMask, JuiH,
		AsdaUi::MeasureLineHeight(Tb->GetFont(), JuiH), AsdaUi::JuiBakedTextVOffset(AlignMask, JuiH),
		TopWas, NewTop,
		(AlignMask & 0x2) ? TEXT("право") : (AlignMask & 0x1) ? TEXT("центр") : TEXT("лево"));
}

UImage* UAsdaMiniStateSubsystem::CloneGlyph(UImage* Src, const TCHAR* NameHint)
{
	// Эталон делает CControl_InstantiateChild(глиф, 1, родитель, 0) — копию контрола в том же
	//   родителе. У нас копия = новый UImage с тем же брашем и тем же слотом канваса.
	if (!Src || !MiniWidget) { return nullptr; }
	UCanvasPanel* Canvas = Cast<UCanvasPanel>(Src->GetParent());
	UCanvasPanelSlot* SS = AsdaUi::SlotOf(Src);
	if (!Canvas || !SS) { return nullptr; }
	UImage* C = NewObject<UImage>(MiniWidget, UImage::StaticClass(),
		MakeUniqueObjectName(MiniWidget, UImage::StaticClass(), FName(NameHint)));
	C->SetBrush(Src->GetBrush());
	C->SetColorAndOpacity(Src->GetColorAndOpacity());
	C->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(C))
	{
		CS->SetAnchors(SS->GetAnchors());
		CS->SetAlignment(SS->GetAlignment());
		CS->SetOffsets(SS->GetOffsets());
		CS->SetZOrder(SS->GetZOrder());
	}
	return C;
}

// ══════════════════════════════ жизненный цикл ═════════════════════════════════════════════════
void UAsdaMiniStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());

	if (UAsdaNetSubsystem* Net = GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>())
	{
		Net->OnVitalsChanged.AddUObject(this, &UAsdaMiniStateSubsystem::OnVitalsChanged);
		Net->OnSelfWeight.AddUObject(this, &UAsdaMiniStateSubsystem::OnSelfWeight);
		Net->OnSelfExp.AddUObject(this, &UAsdaMiniStateSubsystem::OnSelfExpChanged);
		Net->OnPvpMode.AddUObject(this, &UAsdaMiniStateSubsystem::OnPvpMode);
		Net->OnWingState.AddUObject(this, &UAsdaMiniStateSubsystem::OnWingState);
		// ★ 06.10.2026: нотисы 4206 (команды 0/2), эмблема клана, значок главы отряда.
		Net->OnPvpModeRefused.AddUObject(this, &UAsdaMiniStateSubsystem::OnPvpModeRefused);
		Net->OnClanFlag.AddUObject(this, &UAsdaMiniStateSubsystem::OnClanFlagNet);
		Net->OnClanSnapshot.AddUObject(this, &UAsdaMiniStateSubsystem::OnClanSnapshotNet);
		Net->OnClanReset.AddUObject(this, &UAsdaMiniStateSubsystem::OnClanResetNet);
		Net->OnClanBaseInfoPush.AddUObject(this, &UAsdaMiniStateSubsystem::OnClanBaseInfoPushNet);
	}
	// ★ 06.10.2026: на сигнал отряда (значок главы) подписываемся ЛЕНИВО в Tick (признак `bPartyHooked`), а не
	//   `InitializeDependency`: отряд в своём Initialize берёт панель хотбара простым `GetSubsystem`, и
	//   принудительно ранний подъём отряда отсюда оставил бы его без подписки на клик по значку уведомления.
	// ЭТАП 2: модель эффектов живёт в отдельной подсистеме — мини-статус только рисует.
	Collection.InitializeDependency(UAsdaEffectsSubsystem::StaticClass());
	if (UAsdaEffectsSubsystem* Fx = GetGameInstance()->GetSubsystem<UAsdaEffectsSubsystem>())
	{
		Fx->OnEffectsChanged.AddUObject(this, &UAsdaMiniStateSubsystem::OnEffectsChanged);
		Fx->OnEventBonusChanged.AddUObject(this, &UAsdaMiniStateSubsystem::OnEventBonusChanged);
	}
}

void UAsdaMiniStateSubsystem::Deinitialize()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			Net->OnVitalsChanged.RemoveAll(this);
			Net->OnSelfWeight.RemoveAll(this);
			Net->OnSelfExp.RemoveAll(this);
			Net->OnPvpMode.RemoveAll(this);
			Net->OnWingState.RemoveAll(this);
			Net->OnPvpModeRefused.RemoveAll(this);       // ★ 06.10.2026
			Net->OnClanFlag.RemoveAll(this);
			Net->OnClanSnapshot.RemoveAll(this);
			Net->OnClanReset.RemoveAll(this);
			Net->OnClanBaseInfoPush.RemoveAll(this);
		}
		if (UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>()) { Party->OnPartyMasterChanged.RemoveAll(this); }
		if (UAsdaEffectsSubsystem* Fx = GI->GetSubsystem<UAsdaEffectsSubsystem>())
		{
			Fx->OnEffectsChanged.RemoveAll(this);
			Fx->OnEventBonusChanged.RemoveAll(this);
		}
	}
	ResetWidget();
	Super::Deinitialize();
}

// ★★★ К-3: СБРОС СОСТОЯНИЯ СЕССИИ (шапка `AsdaSessionReset.h`). Подсистема живёт на
//   `GameInstance` и переживает разрыв связи, выход в выбор персонажа и травел; всё, что
//   ниже, — данные и защёлки ОДНОЙ сессии. Отправок здесь нет (закон `no-reentrant-net-callbacks`).
void UAsdaMiniStateSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// Виджеты мини-статуса привязаны к умершему миру, а «последнее применённое» — к персонажу.
	// ★ СМЯГЧЕНИЕ (решение владельца 2026-09-02): при РАЗРЫВЕ СВЯЗИ мир жив, виджеты валидны —
	//   ссылки не рвём, иначе полосы моргнут и пересоберутся без нужды. Кэш «последнее
	//   применённое» ниже сбрасываем ВСЕГДА: он заставит `Tick` перерисовать значения заново,
	//   а зеркала персонажа при разрыве сохранены (см. `UAsdaNetSubsystem::ResetSessionState`).
	if (!AsdaSessionResetKeepsWorldAlive(Reason))
	{
		ResetWidget();
	}
	LastLevelApplied = -1;
	LastWeightCur = -2; LastWeightMax = -2;
	LastClassApplied = -2;
	// ★ 06.10.2026 (ревизия, вне задачи): флаги нотисов перегруза — поля живого CHUD оригинала, он не
	//   пересоздаётся при смене карты. Сбрасываем их только при полном сбросе сессии, иначе при перегрузе
	//   ≥ 70 % нотис 2358 повторялся бы после каждого портала.
	if (AsdaIsFullSessionReset(Reason))
	{
		bWeightWarn70Shown = false;
		bWeightWarn90Shown = false;
	}
	bWeightBlinkOn = true;
	bWingActive = false;
	bTipShown = false;
	SelfDbl.Reset();
	BuffDbl.Reset();
	Mouse.Reset(); BuffMouse.Reset();
	// ★ 06.10.2026 (ревизия СРЕДН-1): значок главы — состояние живого CHUD; `sub_9CCD30` зовёт только
	//   `Party_SetMasterLocal` (social.c:447/452), 5102 после портала сервер повторно не шлёт, а отряд при
	//   смене карты состав и главу сохраняет. Поэтому гасим значок только при ПОЛНОМ сбросе сессии.
	if (AsdaIsFullSessionReset(Reason))
	{
		bPartyerOn = false;
		SetVis(PartyerIcon, false);
	}
	bLastSelfFlagOn = false;
	PendingClanMark.Reset(); PendingClanMarkWhy.Reset();
	LastNameApplied.Reset();
	bDeathResetLogged = false;
	(void)Reason;
}

void UAsdaMiniStateSubsystem::ResetWidget()
{
	MiniWidget = nullptr; TipWidget = nullptr; SharedTip = nullptr;
	HpBar = FAsdaMiniBarFill(); MpBar = FAsdaMiniBarFill();
	WeightBar = FAsdaMiniBarFill(); WeightBar2 = FAsdaMiniBarFill(); WeightBar3 = FAsdaMiniBarFill();
	BonusBar = FAsdaMiniBarFill();
	HpGhost = nullptr; MpGhost = nullptr; HpText = nullptr; MpText = nullptr; CharNameText = nullptr;
	WeiDigitOnes.Reset(); WeiDigitTens.Reset(); WeiDigitHundreds.Reset(); WeiPercentGlyph = nullptr;
	LvDigitOnes.Reset(); LvDigitTens.Reset(); LvDigitHundreds.Reset();
	ClassIcons.Reset(); ElementIcons.Reset(); ElementBase = nullptr;
	PvpModeIcon = nullptr; PlusPackIcon = nullptr; PartyerIcon = nullptr; ClickZone = nullptr;
	ClanMarkBase = nullptr; ClanMarkImg = nullptr; bClanMarkBaseShown = false;   // ★ 06.10.2026
	LastNameApplied.Reset();   // новое окно — имя записать заново
	Gear1Group = nullptr; AniGear1Group = nullptr;
	bTipShown = false;
	BuffSlots.Reset(); TimerProtos.Reset(); HoverBuffSlot = -1;
	EventBuffGroup = nullptr; ExpBuffIcons.Reset(); DropBuffIcons.Reset();
	bControlsReady = false;
	LastLevelApplied = -1;
	LastWeightCur = -2; LastWeightMax = -2; LastClassApplied = -2;
	WeightBlinkNextS = -1.0; bWeightBlinkOn = true;
}

bool UAsdaMiniStateSubsystem::EnsureWidget()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return false; }
	if (WM->IsWindowLive(MiniWidget))
	{
		return true;
	}
	ResetWidget();
	// ★ SelfHitTestInvisible на корне: растянутый на вьюпорт UUserWidget с Visible съедает клики
	//   по миру (регрессия мини-карты 2026-08-01). AlwaysTop=0 у всех контролов ⇒ обычная полоса z.
	MiniWidget = WM->OpenWindowByPath(kMiniWindowId, kMiniWbp, kMiniZ, ESlateVisibility::SelfHitTestInvisible);
	if (!MiniWidget)
	{
		UE_LOG(LogAsdaMini, Error, TEXT("[мини-статус] WBP_MINISTATE_WIN не загрузился (%s)"), kMiniWbp);
		return false;
	}
	SetupControls();
	RefreshAll();
	// ★ 06.10.2026: окно поднимается вместе с миром = эталонный вход на карту (game_main.c:3876, 3935-3949).
	ApplyClanMarkOnMapEntry();
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] окно открыто (z=%d), контролов найдено: %s"),
		kMiniZ, bControlsReady ? TEXT("да") : TEXT("НЕТ"));
	return true;
}

// Разбор импортированного дерева. Имена — по закону jui-importer-widget-naming:
//   интерактив/Text = точное имя idc; план = `{idc}_{idx}_{plane}`; группа = `{idc}_grp`;
//   невидимая зона хит-теста = `{idc}_{idx}_HIT`.
void UAsdaMiniStateSubsystem::SetupControls()
{
	bControlsReady = false;
	if (!MiniWidget || !MiniWidget->WidgetTree) { return; }

	TArray<UWidget*> All;
	MiniWidget->WidgetTree->GetAllWidgets(All);

	// Планы баров: собираем по префиксу в порядке дерева ( _0 = ES0, _1 = ES1 ).
	TArray<UImage*> HpPlanes, MpPlanes, WPlanes, W2Planes, W3Planes, BonusPlanes;
	// Атлас-цифры: индекс цифры берём из ИМЕНИ (WEICount<d>_*, MINISTATE_COUNT<d>_*).
	UImage* WeiBase[10] = {}; UImage* LvBase[10] = {};
	UImage* ClsIcon[4] = {}; UImage* EleIcon[6] = {};

	for (UWidget* W : All)
	{
		if (!W) { continue; }
		const FString N = W->GetName();
		if (UImage* Im = Cast<UImage>(W))
		{
			if (N.StartsWith(TEXT("MINISTATE_HPBAR")))        { HpPlanes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_MPBAR")))        { MpPlanes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_WEIGHTBAR2")))   { W2Planes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_WEIGHTBAR3")))   { W3Planes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_WEIGHTBAR")))    { WPlanes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_BONUSBAR")))     { BonusPlanes.Add(Im); continue; }
			if (N.StartsWith(TEXT("MINISTATE_HPBuff")))       { HpGhost = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_MPBuff")))       { MpGhost = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_ClsTenderfoot"))){ ClsIcon[0] = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_ClsWarrior")))   { ClsIcon[1] = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_ClsArcher")))    { ClsIcon[2] = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_ClsWizard")))    { ClsIcon[3] = Im; continue; }
			if (N.StartsWith(TEXT("MiniState_PvPMode")))      { PvpModeIcon = Im; continue; }
			if (N.StartsWith(TEXT("pluscpack")))              { PlusPackIcon = Im; continue; }
			if (N.StartsWith(TEXT("MINISTATE_Partyer")))      { PartyerIcon = Im; continue; }
			// ★ 06.10.2026: план эмблемы 271 (`MINISTATE_ClanMark_146_0`); НЕ `ClanMark2_*` (273, ссылок в срезе нет)
			//   и НЕ планы рамки `ClanMarkBase_145_*` (их показывает группа 270 целиком).
			if (N.StartsWith(TEXT("MINISTATE_ClanMark_")))    { ClanMarkImg = Im; continue; }
			if (N.StartsWith(TEXT("WEIPercent")))             { WeiPercentGlyph = Im; continue; }
			if (N.StartsWith(TEXT("WEICount")) && N.Len() > 8)
			{
				const int32 D = (int32)(N[8] - TCHAR('0'));
				if (D >= 0 && D <= 9 && !WeiBase[D]) { WeiBase[D] = Im; }
				continue;
			}
			if (N.StartsWith(TEXT("MINISTATE_COUNT")) && N.Len() > 15)
			{
				const int32 D = (int32)(N[15] - TCHAR('0'));
				if (D >= 0 && D <= 9 && !LvBase[D]) { LvBase[D] = Im; }
				continue;
			}
			if (N.StartsWith(TEXT("MINISTATE_ELE0")) && N.Len() > 14)
			{
				const int32 E = (int32)(N[14] - TCHAR('0'));
				if (E >= 0 && E <= 5 && !EleIcon[E]) { EleIcon[E] = Im; }
				continue;
			}
			if (N == TEXT("MiniState_Click_148_HIT") || (N.StartsWith(TEXT("MiniState_Click")) && N.EndsWith(TEXT("_HIT"))))
			{
				ClickZone = Im; continue;
			}
		}
		else if (UTextBlock* Tb = Cast<UTextBlock>(W))
		{
			// Закон ui-autowrap-canvas-gotcha: однострочные подписи НЕ переносим.
			// ★ Выравнивание — по EXS TextAlign из .JUI (у всех трёх = 5 = центр/центр), а НЕ по
			//   CaptionInfo, которую взял генератор. Истинные высоты контролов из .JUI:
			//   HPTEXT (95,39,90,10), MPTEXT (95,59,90,10), CharName (69,14,160,17).
			//   ★★★ Последний аргумент — `EXS TextPos.y` из gamemain_jui.ini: у HP/MP он «0 0», у имени «0 1».
			//   Эталон прибавляет его ТОЛЬКО к ВЕРХУ прямоугольника, поэтому он входит в окно
			//   центровки и передаётся отдельно — разбор у `AsdaUi::ApplyJuiTextAlign`.
			if (N == TEXT("MINISTATE_HPTEXT"))   { HpText = Tb; Tb->SetAutoWrapText(false); ApplyJuiTextAlign(Tb, 5, 10.f, 0.f); continue; }
			if (N == TEXT("MINISTATE_MPTEXT"))   { MpText = Tb; Tb->SetAutoWrapText(false); ApplyJuiTextAlign(Tb, 5, 10.f, 0.f); continue; }
			if (N == TEXT("MINISTATE_CharName")) { CharNameText = Tb; Tb->SetAutoWrapText(false); ApplyJuiTextAlign(Tb, 5, 17.f, 1.f); continue; }
		}
		if (N == TEXT("MINISTATE_ELE_Base_grp"))    { ElementBase = W; continue; }
		if (N == TEXT("MINISTATE_Gear1_grp"))       { Gear1Group = W; continue; }
		if (N == TEXT("MINISTATE_AniGear1_grp"))    { AniGear1Group = W; continue; }
		if (N == TEXT("MINISTATE_ClanMarkBase_grp")) { ClanMarkBase = W; continue; }   // ★ 06.10.2026: контрол 270
	}

	// ── Бары. FullW ВСЕГДА у подложки ES0, если она есть: у ProgressBar-скинов с двумя планами
	//   импортёр масштабирует ES1 по шаблону max-rect (LightGray_Bar: ES0 30, ES1 20 → 96 и 64 px),
	//   то есть ширина заливки в ассете — статичный ДИЗАЙН-процент, а не «полный бар».
	//   У HP_RedBar/MP_BlueBar ES0 помечен PlaneAlwaysInvisible=1 и импортёром выброшен, поэтому
	//   единственный оставшийся план `_0` — это ES1, и он уже растянут на весь контрол (125 px).
	auto MakeBar = [](const TArray<UImage*>& Planes, const TCHAR* Tag) -> FAsdaMiniBarFill
	{
		FAsdaMiniBarFill B;
		if (Planes.Num() == 0) { return B; }
		B.Back = (Planes.Num() >= 2) ? Planes[0] : nullptr;
		B.Fill = (Planes.Num() >= 2) ? Planes[1] : Planes[0];
		FVector2D P, S;
		const UWidget* Ref = B.Back ? static_cast<const UWidget*>(B.Back.Get()) : static_cast<const UWidget*>(B.Fill.Get());
		if (GetSlotRect(Ref, P, S)) { B.FullW = S.X; }
		FVector2D FP, FS;
		if (GetSlotRect(B.Fill.Get(), FP, FS)) { B.BaseLeft = FP.X; }
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] бар %s: планов=%d, полная ширина=%.0f, left=%.0f"),
			Tag, Planes.Num(), B.FullW, B.BaseLeft);
		return B;
	};
	HpBar      = MakeBar(HpPlanes,    TEXT("HP (3)"));
	MpBar      = MakeBar(MpPlanes,    TEXT("MP (4)"));
	WeightBar  = MakeBar(WPlanes,     TEXT("вес (6)"));
	WeightBar2 = MakeBar(W2Planes,    TEXT("вес2 (95)"));
	WeightBar3 = MakeBar(W3Planes,    TEXT("вес3 (96)"));
	BonusBar   = MakeBar(BonusPlanes, TEXT("бонус (165)"));

	// «Отстающие» слои: полная ширина из ассета (Box_Orange 100, Box_Skyblue 90).
	{
		FVector2D P, S;
		if (GetSlotRect(HpGhost, P, S)) { HpGhostFullW = S.X; }
		if (GetSlotRect(MpGhost, P, S)) { MpGhostFullW = S.X; }
	}

	// ── Цифры веса. Базовые = ряд ЕДИНИЦ (эталон pWeightDigit0[3d+2]); два ряда клонов —
	//   десятки [3d+1] и сотни [3d]. Родитель клонов у эталона — MINISTATE_WEIGHTTEXT (36);
	//   у нас это канвас-группа MINISTATE_WEIGHTTEXT_grp, в которой уже лежат базовые глифы.
	WeiDigitOnes.Reset(); WeiDigitTens.Reset(); WeiDigitHundreds.Reset();
	for (int32 D = 0; D < 10; ++D)
	{
		WeiDigitOnes.Add(WeiBase[D]);
		WeiDigitTens.Add(CloneGlyph(WeiBase[D], TEXT("WEICountTens")));
		WeiDigitHundreds.Add(CloneGlyph(WeiBase[D], TEXT("WEICountHundreds")));
		SetVis(WeiBase[D], false);
	}
	{
		// posY эталона = Y контрола pWeightDigit0[3] = WEICount1 (локаль (0,2) внутри группы).
		FVector2D P, S;
		if (GetSlotRect(WeiBase[1], P, S)) { WeiDigitY = P.Y; }
	}
	SetVis(WeiPercentGlyph, true);   // знак «%» статичен и всегда видим (эталон)

	// ── Цифры уровня. Базовые = ЕДИНИЦЫ (v48[17+ones]), клон A = десятки, клон B = сотни.
	LvDigitOnes.Reset(); LvDigitTens.Reset(); LvDigitHundreds.Reset();
	for (int32 D = 0; D < 10; ++D)
	{
		LvDigitOnes.Add(LvBase[D]);
		LvDigitTens.Add(CloneGlyph(LvBase[D], TEXT("MINISTATE_COUNT_Tens")));
		LvDigitHundreds.Add(CloneGlyph(LvBase[D], TEXT("MINISTATE_COUNT_Hundreds")));
		SetVis(LvBase[D], false);
	}
	{
		// Прототип эталона — клон COUNT0, значит его позиция = позиция COUNT0 (локаль (0,3)),
		//   а ширина LVWIN берётся у самого MINISTATE_LVWIN (40). Группа `MINISTATE_LVWIN_grp`
		//   в импорте построена по union-габаритам (192,33)-(232,72) ⇒ её ширина и есть 40.
		FVector2D P, S;
		if (GetSlotRect(LvBase[0], P, S)) { LvProtoPos = P; LvDigitW = S.X; }
		if (UWidget* LvGrp = MiniWidget->GetWidgetFromName(TEXT("MINISTATE_LVWIN_grp")))
		{
			FVector2D GP, GS;
			if (GetSlotRect(LvGrp, GP, GS) && GS.X > 0.f) { LvWinW = GS.X; }
		}
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] уровень: прототип=(%.0f,%.0f) глиф=%.0f LVWIN=%.0f"),
			LvProtoPos.X, LvProtoPos.Y, LvDigitW, LvWinW);
	}

	ClassIcons.Reset();
	for (int32 i = 0; i < 4; ++i) { ClassIcons.Add(ClsIcon[i]); }
	ElementIcons.Reset();
	for (int32 i = 0; i < 6; ++i) { ElementIcons.Add(EleIcon[i]); }

	// ── Стартовое состояние = ровно то, что в данных JUI ───────────────────────────────────────
	//   Иконки классов: видима ТОЛЬКО ClsTenderfoot (Visible="1" в JUI), остальные три — 0.
	for (int32 i = 1; i < 4; ++i) { SetVis(ClassIcons[i], false); }
	SetVis(ClassIcons[0], true);
	//   PvP-иконка и премиум-пакет скрыты (Visible="0").
	SetVis(PvpModeIcon, false);
	SetVis(PlusPackIcon, false);
	//   ★ 06.10.2026: Partyer (9) показывает 5102 (`Party_SetMasterLocal`). CHUD оригинала живёт всю сессию,
	//     поэтому значок переживает смену карты; у нас окно пересоздаётся с миром — состояние берём из модели.
	SetVis(PartyerIcon, bPartyerOn);
	//   ★ 06.10.2026: эмблема клана 270 — Visible="0" в JUI; показ решает вход на карту (EnsureWidget).
	SetVis(ClanMarkBase, false);
	bClanMarkBaseShown = false;
	//   Доп. полосы веса и бонус-полоса скрыты.
	SetVis(WeightBar2.Fill, false); SetVis(WeightBar2.Back, false);
	SetVis(WeightBar3.Fill, false); SetVis(WeightBar3.Back, false);
	SetVis(BonusBar.Fill,   false); SetVis(BonusBar.Back,   false);
	//   ★ MINISTATE_ELE_Base остаётся СКРЫТЫМ — в оригинале иконки стихий физически не рисуются
	//     (родитель Visible="0", IDNum=−1 ⇒ коду недоступен; рендер не заходит в детей скрытого).
	SetVis(ElementBase, false);
	//   ★ Слои «буффа» HP/MP, наоборот, ВСЕГДА видимы: покадровый HUD_RefreshHPMPGauges_Frame
	//     зовёт SetVisible(1) вопреки Visible="0" из JUI. НО стартовая ширина = 0: у свежего
	//     CCharacter поле `gap_4C0[12]`/`[16]` нулевое, значит процент 0 (иначе в ассете осталась бы
	//     дизайн-ширина 100/90 px и мы получили бы оранжевую полосу поверх HP с первого кадра).
	//   (вызов ApplyVitalGhosts — в самом конце, после bControlsReady)
	//   Анимированная шестерёнка (8) скрыта, статичная (7) видима — как в JUI. Кто запускает
	//     анимацию, в срезе эталона НЕ НАЙДЕНО (у pAniGear1 только резолв и обнуление).
	SetVis(AniGear1Group, false);
	SetVis(Gear1Group, true);

	// ═════════════════ ЭТАП 2: слоты активных эффектов + ивент-иконки ═══════════════════════════
	//   Имена в WBP (импортёр): группа `STATE_ICON<NN>_grp`, план `STATE_ICON<NN>_<idx>_0`,
	//   подпись-стек `STATE_ICON<NN>` (Text-контрол), кулдаун `COOLTIME<NN>_<idx>_0` + `COOLTIME<NN>`.
	//   Прототипы таймера лежат ТОЛЬКО внутри STATE_ICON00_grp: `COOL_black_10_0`,
	//   `COOL_SFONT1_11_0` … `COOL_SFONT4_14_0` — их клонируем на все 28 слотов, как эталон
	//   (`CControl_InstantiateChild(buffTimeCtrls[i], 1, pStatusIconCtrl0[icon], 0)`, ui_core.c:74740).
	{
		BuffSlots.Reset(); BuffSlots.SetNum(28);
		TimerProtos.Reset(); TimerProtos.SetNum(5);
		for (UWidget* W : All)
		{
			if (!W) { continue; }
			const FString N = W->GetName();
			if (N.StartsWith(TEXT("COOL_black")))  { TimerProtos[0] = Cast<UImage>(W); continue; }
			if (N.StartsWith(TEXT("COOL_SFONT1"))) { TimerProtos[1] = Cast<UImage>(W); continue; }
			if (N.StartsWith(TEXT("COOL_SFONT2"))) { TimerProtos[2] = Cast<UImage>(W); continue; }
			if (N.StartsWith(TEXT("COOL_SFONT3"))) { TimerProtos[3] = Cast<UImage>(W); continue; }
			if (N.StartsWith(TEXT("COOL_SFONT4"))) { TimerProtos[4] = Cast<UImage>(W); continue; }
		}
		for (int32 S = 0; S < 28; ++S)
		{
			const FString Base = FString::Printf(TEXT("STATE_ICON%02d"), S);
			FAsdaBuffSlotUi& Slot = BuffSlots[S];
			Slot.Group     = MiniWidget->GetWidgetFromName(FName(*(Base + TEXT("_grp"))));
			Slot.StackText = Cast<UTextBlock>(MiniWidget->GetWidgetFromName(FName(*Base)));
			const FString CoolBase = FString::Printf(TEXT("COOLTIME%02d"), S);
			for (UWidget* W : All)
			{
				if (!W) { continue; }
				UImage* Im = Cast<UImage>(W);
				if (!Im) { continue; }
				const FString N = Im->GetName();
				if (!Slot.Icon && N.StartsWith(Base + TEXT("_")) && N.EndsWith(TEXT("_0"))
					&& !N.StartsWith(Base + TEXT("_grp")))
				{
					Slot.Icon = Im; continue;
				}
				if (!Slot.CoolTime && N.StartsWith(CoolBase + TEXT("_"))) { Slot.CoolTime = Im; }
			}
			// ★ ВТОРОЙ РЯД (индексы 14..27) сдвигается на +9 по Y — эталон HUD_InitControls.
			if (S >= 14 && Slot.Group)
			{
				FVector2D P, Sz;
				if (GetSlotRect(Slot.Group, P, Sz)) { SetSlotPos(Slot.Group, FVector2D(P.X, P.Y + BuffRow2OffsetY)); }
			}
			// Клоны таймера: 5 штук в ту же канву, что и иконка слота.
			Slot.Timer.Reset(); Slot.Timer.SetNum(5);
			for (int32 i = 0; i < 5; ++i)
			{
				UImage* Proto = TimerProtos.IsValidIndex(i) ? TimerProtos[i].Get() : nullptr;
				if (!Proto || !Slot.Icon) { continue; }
				UCanvasPanel* Canvas = Cast<UCanvasPanel>(Slot.Icon->GetParent());
				UCanvasPanelSlot* IconSlot = AsdaUi::SlotOf(Slot.Icon);
				if (!Canvas || !IconSlot) { continue; }
				UImage* C = NewObject<UImage>(MiniWidget, UImage::StaticClass(),
					MakeUniqueObjectName(MiniWidget, UImage::StaticClass(), FName(*FString::Printf(TEXT("BuffTimer%02d_%d"), S, i))));
				C->SetBrush(Proto->GetBrush());
				C->SetVisibility(ESlateVisibility::Collapsed);
				if (UCanvasPanelSlot* CS = Canvas->AddChildToCanvas(C))
				{
					const FMargin IO = IconSlot->GetOffsets();
					CS->SetAnchors(IconSlot->GetAnchors());
					CS->SetAlignment(IconSlot->GetAlignment());
					// i == 0 — подложка 24×10 в локале (0,24); глифы 9×9 позиционирует DrawBuffTimer.
					CS->SetOffsets(i == 0 ? FMargin(IO.Left, IO.Top + BuffTimerY, 24.f, 10.f)
					                      : FMargin(IO.Left, IO.Top + BuffTimerY, 9.f, 9.f));
					CS->SetZOrder(IconSlot->GetZOrder() + 1);
				}
				Slot.Timer[i] = C;
			}
			// Стартовое состояние = как в эталоне после HUD_InitControls: всё погашено.
			SetVis(Slot.Icon, false);
			SetVis(Slot.CoolTime, false);                     // COOLTIME00..27 резолвятся и гасятся
			if (Slot.StackText) { Slot.StackText->SetText(FText::GetEmpty()); Slot.StackText->SetAutoWrapText(false); }
			Slot.BuffId = -1; Slot.TypeFlag = 0; Slot.StackCount = 0;
		}
		// Прототипы прячем: в оригинале они остаются видимыми, но их скин NoImage2_icon — пустой
		//   пиксель, то есть визуально ничего не рисуют. У нас клоны берут тот же браш.
		for (const TObjectPtr<UImage>& P : TimerProtos) { SetVis(P.Get(), false); }
		UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] эффекты: слотов %d, прототипов таймера %d/5"),
			BuffSlots.Num(), (TimerProtos[0] ? 1 : 0) + (TimerProtos[1] ? 1 : 0) + (TimerProtos[2] ? 1 : 0)
			                 + (TimerProtos[3] ? 1 : 0) + (TimerProtos[4] ? 1 : 0));
	}
	// Ивент-иконки: контейнер EXP_Buff (5100) + 7 EXP + 3 Drop. Все стартуют скрытыми (Visible=0 в JUI).
	{
		EventBuffGroup = MiniWidget->GetWidgetFromName(TEXT("EXP_Buff_grp"));
		static const TCHAR* kExp[7]  = { TEXT("EXP_Buff05"), TEXT("EXP_Buff15"), TEXT("EXP_Buff2"),
		                                 TEXT("EXP_Buff25"), TEXT("EXP_Buff3"),  TEXT("EXP_Buff35"), TEXT("EXP_Buff4") };
		static const TCHAR* kDrop[3] = { TEXT("Drop_Buff2"), TEXT("Drop_Buff3"), TEXT("Drop_Buff4") };
		ExpBuffIcons.Reset();  ExpBuffIcons.SetNum(7);
		DropBuffIcons.Reset(); DropBuffIcons.SetNum(3);
		for (UWidget* W : All)
		{
			UImage* Im = Cast<UImage>(W);
			if (!Im) { continue; }
			const FString N = Im->GetName();
			for (int32 i = 0; i < 7; ++i)
			{
				// Точный префикс с «_»: иначе EXP_Buff2 поймал бы EXP_Buff25.
				if (!ExpBuffIcons[i] && N.StartsWith(FString(kExp[i]) + TEXT("_"))) { ExpBuffIcons[i] = Im; }
			}
			for (int32 i = 0; i < 3; ++i)
			{
				if (!DropBuffIcons[i] && N.StartsWith(FString(kDrop[i]) + TEXT("_"))) { DropBuffIcons[i] = Im; }
			}
		}
		SetVis(EventBuffGroup, false);
		for (const TObjectPtr<UImage>& I : ExpBuffIcons)  { SetVis(I.Get(), false); }
		for (const TObjectPtr<UImage>& I : DropBuffIcons) { SetVis(I.Get(), false); }
	}

	bControlsReady = true;
	++WidgetGeneration;   // ★ ЭТАП 3: система хранителя увидит новое поколение и перерезолвит свои узлы

	// Стартовые значения показателей = «пусто», как у только что созданного CCharacter.
	ApplyVitalGhosts(0, 1, 0, 1);   // слои буффа видимы, но нулевой ширины
	HpSmooth.Reset(); MpSmooth.Reset();   // ★ первый показ панели — без «наливания»
	ClipFill(HpBar, 0.f);           // импортёр оставил в ассете дизайн-процент — перекрываем
	ClipFill(MpBar, 0.f);
	ClipFill(WeightBar, 0.f);
	ClipFill(WeightBar2, 0.f);
	ClipFill(WeightBar3, 0.f);
	ClipFill(BonusBar, 0.f);
}

// ★ R4-b 14.11.2026 [ПОРТ] (R4, доля b, B8): имя своего персонажа для сверки «имя изменилось» каждый кадр копировалось (выделение памяти);
//   геттер отдаёт ссылку на поле мира — теперь ссылкой. Сверка та же.
// ★ R4-b закрытие (15.11.2026): ключ `asda.Perf.MiniStateName` — старый путь и ключ удалены — только новое (решение владельца).

// ══════════════════════════════ ТИК ════════════════════════════════════════════════════════════
void UAsdaMiniStateSubsystem::Tick(float DeltaTime)
{
	ASDA_PERF_SCOPE("Tick.MiniState");   // ★ R4-b (14.11.2026): область замера (AsdaPerf.h; в Shipping пусто)
	UGameInstance* GI = GetGameInstance();
	if (!GI) { return; }
	UAsdaLoadManagerSubsystem* LM = GI->GetSubsystem<UAsdaLoadManagerSubsystem>();
	UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
	// Мини-статус живёт только В МИРЕ (эталон: CHUD создаётся при входе, g_GameState == 1).
	// ★ 06.10.2026: ленивая подписка на сигнал отряда 5102 (см. Initialize) — раньше гейта «в мире»:
	//   состав и смена главы могут прийти до спавна павна.
	if (!bPartyHooked)
	{
		if (UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>())
		{
			Party->OnPartyMasterChanged.AddUObject(this, &UAsdaMiniStateSubsystem::OnPartyMasterChanged);
			bPartyHooked = true;
		}
	}
	if (!LM || LM->CurrentMapId < 0 || !WS || !IsValid(WS->LocalPlayerActor)) { return; }
	if (!EnsureWidget()) { return; }

	TickVitalBars(DeltaTime);   // ★ плавный ход заливки жизни и маны

	// Мигание тёмно-красной полосы при перегрузе ≥ 90 % — эталонный WM_TIMER 25 (250 мс)
	//   → uiControlToggleVisible@0xE817A0 (переключает видимость pWeightBarExtra1).
	if (WeightBlinkNextS >= 0.0)
	{
		UWorld* W = GI->GetWorld();
		const double Now = W ? W->GetTimeSeconds() : 0.0;
		if (Now >= WeightBlinkNextS)
		{
			bWeightBlinkOn = !bWeightBlinkOn;
			SetVis(WeightBar3.Fill, bWeightBlinkOn);
			SetVis(WeightBar3.Back, bWeightBlinkOn);
			WeightBlinkNextS = Now + (double)FMath::Max(OverloadBlinkSec, 0.05f);
		}
	}

	// ★★★ ДОГОН ДАННЫХ. Мини-статус может открыться РАНЬШЕ, чем приедут показатели (и наоборот:
	//   блок char-select 1017 приходит ЗАДОЛГО до мира). Делегатов на всё подряд в эталоне нет —
	//   там `hudRefreshFromCharacterStats` дёргается из нескольких мест по факту изменения полей
	//   CCharacter. У нас эквивалент — дешёвая проверка «значение изменилось» каждый кадр.
	if (bControlsReady)
	{
		if (UAsdaNetSubsystem* Net = GI->GetSubsystem<UAsdaNetSubsystem>())
		{
			if (Net->SelfWeightCur != LastWeightCur || Net->SelfWeightMax != LastWeightMax)
			{
				LastWeightCur = Net->SelfWeightCur; LastWeightMax = Net->SelfWeightMax;
				ApplyWeight(Net->SelfWeightCur, Net->SelfWeightMax);
			}
			const int32 Lvl = ResolveSelfLevel();
			if (Lvl > 0 && Lvl != LastLevelApplied) { ApplyLevel(Lvl); }
			const int32 Cls = ResolveSelfClass();
			if (Cls != LastClassApplied)
			{
				LastClassApplied = Cls;
				UAsdaWorldSubsystem* WS2 = GI->GetSubsystem<UAsdaWorldSubsystem>();
				ApplyNameClassElements(WS2 ? WS2->GetSelfCharName() : FString(), Cls, TArray<int32>());
			}
			// ★ 06.10.2026: ИМЯ переприменяется и само по себе. Эталон пишет имя в `hudRefreshClassAndElementIcons`
			//   при каждом полном пересчёте (вход в мир game_main.c:2839, 4060 network.c:10201); раньше здесь
			//   имя обновлялось только вместе со сменой класса — другой персонаж того же класса оставлял старое имя.
			ASDA_PERF_SCOPE("MiniState.Name");   // ★ R4-b 14.11.2026: область замера (AsdaPerf.h; в Shipping пусто)
			// ★ R4-b 14.11.2026 (B8): имя — ссылкой на поле мира, копия только на редкой смене. ★ R4-b закрытие (15.11.2026): старый путь и ключ удалены — только новое (решение владельца).
			const FString& SelfNameRef = WS->GetSelfCharName();
			if (!SelfNameRef.IsEmpty() && SelfNameRef != LastNameApplied)
			{
				const FString SelfName = SelfNameRef;   // копия — только на редкой смене (вызов как прежде, со своей строкой)
				ApplyNameClassElements(SelfName, -1, TArray<int32>());   // −1 = класс не трогать
			}
		}
	}

	// ★ 06.10.2026: эмблема клана — события, накопленные колбэками (данные своей плашки уже обновлены миром).
	ProcessPendingClanMark();

	// ★ 06.10.2026: СМЕРТЬ. Эталон каждый кадр `HUD_RefreshHPMPGauges_Frame` (ui_core.c:75482-75497):
	//   состояние действия 6 (= мёртв; `Net_GetCurrentMapType` — миснейм) → слои 37/89 и «второе» HP/MP = 0.
	if (IsSelfDeadExact()) { ApplyDeathGaugeReset(); }
	else
	{
#if ASDA_COMBAT_TRACE
		if (bDeathResetLogged)
		{
			ASDA_CTRACE(EAsdaCT::Death, TEXT("своя смерть в HUD СНЯТА (состояние 6 ушло: воскрешение/выход) — HP в HUD «%s»"),
				HpText ? *HpText->GetText().ToString() : TEXT("нет"));
		}
#endif
		bDeathResetLogged = false;
	}

	// ★ 06.10.2026 [ПОРТ] значок главы гаснет, когда отряда нет (дефект оригинала, ключ asda.Mini.PartyerHideOnLeave).
	if (bPartyerOn && CVarPartyerHideOnLeave.GetValueOnGameThread() != 0)
	{
		const UAsdaPartySubsystem* Party = GI->GetSubsystem<UAsdaPartySubsystem>();
		if (Party && !Party->IsInParty())
		{
			SetPartyerIcon(false);
			UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] [ПОРТ] значок главы (9) погашен: отряда больше нет (оригинал оставлял его висеть)"));
		}
	}

	TickPointer();
	TickBuffPointer();   // ЭТАП 2: наведение/клик по иконкам эффектов (события 8/9/10/11 эталона)
}

// Уровень self: рантайм-поле сети, иначе запись выбранного персонажа char-select
//   (тот же фолбэк, что у EXP-бара: в сессии без 4005 SelfLevel остаётся нулём).
int32 UAsdaMiniStateSubsystem::ResolveSelfLevel() const
{
	// ★ Волна 1 (14.09.2026, В9): общая точка `AsdaSelf::GetLevel` — рантайм-поле, иначе запись ВЫБРАННОГО персонажа (как было).
	return AsdaSelf::GetLevel(GetGameInstance());
}

// Класс self (байт +0x438): 1017-блок / 4005-self, иначе запись char-select (0x3EE rec+24).
int32 UAsdaMiniStateSubsystem::ResolveSelfClass() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	// ★ Дедуп №2, В2-A (U5d): то же правило — общая точка `AsdaSelf::GetClassId` (поведение мини-статуса не меняется).
	return AsdaSelf::GetClassId(Net);
}

// ══════════════════════════════ ВВОД ═══════════════════════════════════════════════════════════
// ЭТАП 3: общий тултип для системы «хранителя» (окно ITEM_TIPWIN одно на весь мини-статус).
void UAsdaMiniStateSubsystem::ShowSharedTip(const FString& Title, const FString& BodyRaw, EAsdaTipOwner Owner)
{
	if (!EnsureTipWidget()) { return; }
	LayoutAndShowTip(Title, BodyRaw, /*bBuffFormula=*/true, Owner);
}

bool UAsdaMiniStateSubsystem::HideSharedTip(EAsdaTipOwner Owner)
{
	// ★★★ ТОЛЬКО свой тултип. Раньше здесь был безусловный `HideTooltip()`, и покадровый
	//   `UAsdaSoulGuardSubsystem::TickPointer` (он зовёт HideSharedTip КАЖДЫЙ КАДР, пока окно
	//   хранителя закрыто) сносил чужую подсказку — тултип предмета не доживал до отрисовки.
	UGameInstance* GI = GetGameInstance();
	UAsdaItemTooltipSubsystem* Tip = GI ? GI->GetSubsystem<UAsdaItemTooltipSubsystem>() : nullptr;
	const bool bHidden = Tip && Tip->HideTooltipFor(Owner);
	if (bHidden && Owner == EAsdaTipOwner::MiniState) { bTipShown = false; }
	return bHidden;
}

// ★ D1 (аудит `code_dedup_audit_2.md`): тело переехало в `AsdaUi::IsCursorOverWidget` — раньше эта
//   математика жила семью побайтово одинаковыми копиями по подсистемам. Метод оставлен тонким
//   форвардом: он приватный, зовётся из десятков мест этого файла и держит const-контракт.
bool UAsdaMiniStateSubsystem::IsCursorOver(const UWidget* W) const
{
	return AsdaUi::IsCursorOverWidget(W);
}

// Мышь у нас ловится ОПРОСОМ: узлы импортированных окон — UImage, а SImage не поглощает
//   OnMouseButtonDown (грабля №2 оконной подсистемы). Эталонные коды событий воспроизводим сами:
//     11 = двойной ЛКМ по MiniState_Click (97) → взять себя целью;
//      8 = ПКМ вниз по pluscpack (5110) → показать тултип; 9 = ПКМ вверх → уложить у курсора;
//     10 = ПКМ клик (отпускание) → скрыть тултип.
void UAsdaMiniStateSubsystem::TickPointer()
{
	if (!bControlsReady || !FSlateApplication::IsInitialized()) { return; }
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	// Чужой всплывающий слой поверх нас держит ввод (реестр оконной подсистемы). Латч здесь
	//   НЕ трогается сознательно — выход стоит до опроса.
	// ⚠ МОДАЛКА ИЗ ЭТОГО УСЛОВИЯ УБРАНА и проверяется НИЖЕ, под `Mouse.Update()` — см. гейт там.
	if (WM->IsPointerBlockedByTopmost(kMiniWindowId)) { return; }

	// ★ Аудит №3 (B3): опрос обеих кнопок и фронты — общая точка, вызов стоит РОВНО там, где стоял
	//   прежний опрос: ранние выходы выше (нет контролов, модалка, чужой слой поверх) латч не трогают.
	Mouse.Update();
	// ★★★ МОДАЛЬНЫЙ ГЕЙТ (П8-9, волна 9): под открытым модальным нотисом эталон не отдаёт мышь
	//   НИКОМУ ниже (`ui_core.c:104149-104153`). Обоснование, порядок и ключ отката
	//   `asda.UI.ModalGatePolling` — в шапке `AsdaUi::IsModalDispatchBlocked` (`AsdaUiHelpers.h`).
	//   ★ ПОЧЕМУ ПРОВЕРКА МОДАЛКИ ПЕРЕЕХАЛА СЮДА, ПОД ОПРОС. Раньше она стояла одним условием с
	//     `IsPointerBlockedByTopmost` ВЫШЕ `Mouse.Update()`, и латч замирал в доносном состоянии:
	//     на возврате из-под нотиса он выстреливал ЛОЖНЫЙ фронт ОТПУСКАНИЯ — тот самый «клик,
	//     которого игрок не делал». `Reset()` кладёт латч в «кнопка отпущена», поэтому удержанная
	//     кнопка после закрытия нотиса даст ЧЕСТНЫЙ фронт нажатия. Возвращать проверку наверх нельзя.
	if (AsdaUi::IsModalDispatchBlocked(GI))
	{
		SelfDbl.Reset();                                        // накопитель двойного клика — защёлка
		if (bTipShown && HoverBuffSlot < 0) { HideTooltip(); }   // висящую плашку pluscpack ГАСИМ
		Mouse.Reset();
		return;
	}
	// ── ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (решение владельца): выбор себя ОДНИМ кликом по зоне 97. У оригинала себя выбирает только
	//   событие 11 (`CControl_OnLButtonDblClick@0x12DC190`, `HUD_PlayerBuffIcon_OnClick` case 11 / 'a', ui_core.c:76881-76889),
	//   одиночных 5/6/7 у зоны нет. Срабатывает на отпускании без сдвига (перетаскивание не выбирает), второе нажатие пары
	//   двойного — без повтора. Ключ `asda.UI.SelfTargetSingleClick 0` — прежнее (только двойной). ──
	if (CVarAsdaSelfTargetSingleClick.GetValueOnGameThread() != 0)
	{
		if (Mouse.bLmbDown)
		{
			double Now = 0.0, Win = 0.0;
			DblClickNowAndWindow(Now, Win);
			const bool bInZone = IsCursorOver(ClickZone) && !IsCursorOverOwnControlInClickZone();
			bSelfPressIsPair = SelfDbl.ClickAt(bInZone ? 0 : -1, -1, Now, Win);
			bSelfPressArmed = bInZone;
			SelfPressPos = Mouse.Cursor;
		}
		else if (Mouse.bLmbUp && bSelfPressArmed)
		{
			bSelfPressArmed = false;
			const bool bStill = FVector2D::Distance(Mouse.Cursor, SelfPressPos) <= kSelfClickSlopPx;
			if (bStill && !bSelfPressIsPair && IsCursorOver(ClickZone)) { OnSelfTargetDoubleClick(); }
		}
		else if (!Mouse.bLmb) { bSelfPressArmed = false; }
	}
	else
	// ── событие 11: двойной ЛКМ по зоне MiniState_Click (227×58 @ (10,14)) ──
	// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U1): общий `FAsdaDblClick` — порог Windows `GetDoubleClickTime`, реальное время (было — `DoubleClickSec`
	//   0,5 с ИГРОВЫМ временем `World->GetTimeSeconds()`: пауза и замедление мира растягивали/сжимали порог). Ключ отката
	//   `asda.UI.DblClickLegacy 1` — прежнее: `DoubleClickSec` и игровое время (`DblClickNowAndWindow`).
	if (Mouse.bLmbDown)
	{
		double Now = 0.0, Win = 0.0;
		DblClickNowAndWindow(Now, Win);
		// Тройной клик не считается вторым двойным; нажатие мимо зоны рвёт пару (ключ −1).
		if (SelfDbl.ClickAt(IsCursorOver(ClickZone) ? 0 : -1, -1, Now, Win)) { OnSelfTargetDoubleClick(); }
	}

	// ── события 8/9/10 по pluscpack: в оригинале это НАВЕДЕНИЕ (разбор — у CVarPlusPackTipOnHover):
	//     8  курсор вошёл  → построить тултип
	//     9  курсор над ним → Tooltip_LayoutNearCursor (тянуть за курсором)
	//     10 курсор ушёл    → CControl_KillFocus (скрыть)
	const bool bHoverMode = (CVarPlusPackTipOnHover.GetValueOnAnyThread() != 0);
	if (bHoverMode)
	{
		const bool bOver = IsCursorOver(PlusPackIcon);
		if (bOver && !bTipShown)       { ShowPlusPackTooltip(); }
		else if (bOver && bTipShown)   { LayoutTooltipAtCursor(); }
		// ★ ЭТАП 2: не гасить тултип, который сейчас держит иконка ЭФФЕКТА (её ведёт TickBuffPointer).
		else if (!bOver && bTipShown && HoverBuffSlot < 0) { HideTooltip(); }
	}
	else
	{
		if (Mouse.bRmbDown)
		{
			if (IsCursorOver(PlusPackIcon)) { ShowPlusPackTooltip(); }
		}
		else if (Mouse.bRmb && bTipShown)
		{
			LayoutTooltipAtCursor();                       // эталон case 9: Tooltip_LayoutNearCursor
		}
		else if (Mouse.bRmbUp && bTipShown)
		{
			HideTooltip();                                 // эталон case 10: CControl_KillFocus
		}
	}
	// ★ Аудит №3 (B3): запись обоих латчей делает `Mouse.Update()` в начале функции.
}

// Эталон HUD_PlayerBuffIcon_OnClick, case 11 / a2 == 97 (ui_core.c:76884-76895):
//   снять служебные флаги, выйти из режима ввода, взять СЕБЯ целью и перерисовать плашку/фрейм.
// ★ U1 (В2-A дедупа №2): копия распознавателя с прежним порогом окна — для самотеста (описание — у объявления).
FAsdaDblClick UAsdaMiniStateSubsystem::DebugDblClickProbe(int32 Which) const
{
	FAsdaDblClick C = (Which == 1) ? BuffDbl : SelfDbl;
	C.LegacySec = (double)DoubleClickSec;   // прежний порог — значение свойства (игровое время — отдельная строка таблицы)
	return C;
}

// ★ U1 (В2-A дедупа №2, 11.10.2026): время и окно двойного щелчка мини-статуса (описание — у объявления).
void UAsdaMiniStateSubsystem::DblClickNowAndWindow(double& OutNow, double& OutWindow) const
{
	if (FAsdaDblClick::IsLegacy())
	{
		const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		OutNow = World ? World->GetTimeSeconds() : 0.0;   // прежнее: игровое время мира
		OutWindow = (double)DoubleClickSec;
		return;
	}
	OutNow = FPlatformTime::Seconds();
	OutWindow = FAsdaDblClick::WindowSec();
}

bool UAsdaMiniStateSubsystem::IsCursorOverOwnControlInClickZone() const
{
	if (IsCursorOver(PlusPackIcon)) { return true; }
	for (const auto& B : BuffSlots)
	{
		if (B.BuffId >= 0 && IsCursorOver(B.Icon.Get())) { return true; }
	}
	return false;
}

void UAsdaMiniStateSubsystem::OnSelfTargetDoubleClick()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWorldSubsystem* WS = GI ? GI->GetSubsystem<UAsdaWorldSubsystem>() : nullptr;
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	AAsdaPlayerController* PC = World ? Cast<AAsdaPlayerController>(World->GetFirstPlayerController()) : nullptr;
	if (!WS || !PC || !IsValid(WS->LocalPlayerActor))
	{
		UE_LOG(LogAsdaMini, Warning, TEXT("[мини-статус] двойной ЛКМ: локальный игрок/контроллер недоступны"));
		return;
	}
	// ★ 06.10.2026: ПОЛНЫЙ СБРОС, как эталон (ui_core.c:76880-76889), а не только «поставить цель»:
	//   1) `hudHideAndResetSoundFlags` → `hudResetCursorItemSlot`: спрятать панель цели и обнулить её кэш —
	//      у нас `ClearTarget()` (снимает прежнюю цель: плашку, подсветку, бой) — даже если целью уже был я;
	//   2) `g_nInputControlState = 0` (вид цели «персонаж») — в порту вид выводится из актора цели, свой павн = персонаж;
	//   3) `g_nTargetUserIdx = мой userNum` + `TargetNameplate_Update` → `SetTarget(свой павн)`;
	//   4) `HUD_UpdateTargetFrame` + `hud_setTarget` → полная пересборка панели цели (`ForceRefresh`).
	//   Прежде `SetTarget` при цели «уже я» выходил сразу — панель не пересобиралась.
	ASDA_CTRACE(EAsdaCT::Input, TEXT("двойной ЛКМ по своему мини-статусу → сброс цели и цель = свой персонаж (эталон case 11, id 97)"));
	PC->ClearTarget();
	PC->SetTarget(WS->LocalPlayerActor);
	if (UAsdaTargetFrameSubsystem* TF = GI->GetSubsystem<UAsdaTargetFrameSubsystem>()) { TF->ForceRefresh(); }
	UE_LOG(LogAsdaMini, Log, TEXT("[мини-статус] двойной ЛКМ по панели → сброс цели и цель = СВОЙ персонаж (эталон case 11, id 97)"));
}
