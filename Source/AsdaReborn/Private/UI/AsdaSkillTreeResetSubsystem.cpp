// Copyright (c) 2026 @sitxovski (Discord)
// Реализация свитков сброса навыков: вопрос 12866 (полный сброс), окно выбора ветки 16500 и вопрос 12865 (сброс ветки).
//   Полный разбор эталона — в шапке Public/UI/AsdaSkillTreeResetSubsystem.h, здесь не повторяется.
#include "UI/AsdaSkillTreeResetSubsystem.h"
#include "AsdaDevConsole.h"   // ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

#include "AsdaNetSubsystem.h"
#include "AsdaSelf.h"                        // класс персонажа — общая точка (`CCharacter+0x438`)
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaJuiWindowHandle.h"          // каркас окна JUI (открыть WBP, привязать, скрыть)
#include "UI/AsdaInventorySubsystem.h"       // ячейка свитка и общая отправка 5450 (`UsePremiumItem`)
#include "UI/AsdaEnterGuard.h"               // вопрос о необратимом — без Enter
#include "UI/AsdaNotice.h"                   // нотисы (плашка 221)
#include "UI/AsdaNoticeColors.h"             // −11514 золотой
#include "UI/AsdaUiHelpers.h"                // BindButton, SlotOf
#include "Text/AsdaTextSubsystem.h"          // подстановка слотов [ss1]/[ss2]
#include "World/AsdaWorldSubsystem.h"        // своё имя персонажа
#include "Login/AsdaButtonRelay.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAsdaSkillReset, Log, All);

// Отправка 5450 с веткой частичного сброса: взвести/снять ветку следующей отправки (AsdaNetSubsystem_Items.cpp).
void AsdaArm5450TreeGrade(int32 Grade);

const FName UAsdaSkillTreeResetSubsystem::kWindowId(TEXT("Skill_OneTree_Reset_Win"));

// ★★★ КЛЮЧ ПОТОКА (25.10.2026): 1 — как оригинал (гейт профессии, вопрос 12866, окно ветки 16500 + вопрос 12865, 5450 с веткой);
//   0 — прежнее поведение порта: 5450 сразу по нажатию, ветка не передаётся (у подтипа 36 сервер сбрасывал ветку 0).
static ASDA_DEV_CVAR(int32, CVarSkillResetDialogs,
	TEXT("asda.SkillReset.Dialogs"), 1,
	TEXT("1 — свитки сброса навыков как в оригинале: вопрос 12866 (полный сброс), окно выбора ветки 16500 и вопрос 12865 (сброс ветки), ")
	TEXT("5450 с выбранной веткой; 0 — как прежде: 5450 сразу, без ветки."),
	ECVF_Default);

bool AsdaSkillResetDialogsOn()
{
	return CVarSkillResetDialogs.GetValueOnGameThread() != 0;
}

// ★ 26.10.2026 (ревизия wave_1025d) [ИСПРАВЛЕНИЕ]: пока открыто окно 16500, второй свиток сброса его запрос не подменяет — использование
//   отклоняется. У эталона запрос окна и вопроса 12866 — один буфер (`unk_2C0A71C` = `g_pItemTooltipUI+13460`): второй свиток его
//   перезаписывал, и «ОК» окна отправлял 5450 другого свитка. 0 — как эталон (запрос перезаписывается).
static ASDA_DEV_CVAR(int32, CVarSkillResetKeepFirst,
	TEXT("asda.SkillReset.KeepFirstRequest"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — пока окно выбора ветки 16500 открыто, второй свиток сброса навыков запрос окна не подменяет (использование ")
	TEXT("отклонено); 0 — как эталон: запрос перезаписывается, «ОК» окна уходит 5450 последнего свитка."),
	ECVF_Default);

// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (исследование Reports/class_34_39/RESEARCH.md §4, §7 п.2): у класса вне трёх боевых полос
//   (ремесленные 34…39 — невыпущенная функция оригинала; метки 40+) у свитка сброса ВЕТКИ (подтип 36) ветки нет: эталон открывает
//   16500 с пустыми пунктами и флажком 16521, «ОК» шлёт 5450 с веткой 0 — сервер (`sub_480F20`) сбрасывает пустую категорию 0,
//   свиток тратится. 1 — свиток отклоняется ДО отправки окном-сообщением 9421 (нотис гейта окна навыков «профессия не выбрана»),
//   флажок 16521 у ремесленного класса не показывается; 0 — как эталон.
static ASDA_DEV_CVAR(int32, CVarSkillResetCraftGuard,
	TEXT("asda.SkillReset.CraftClassGuard"), 1,
	TEXT("[ИСПРАВЛЕНИЕ] 1 — у класса вне боевых полос (ремесленные 34…39) свиток сброса ветки отклоняется до отправки нотисом 9421 ")
	TEXT("(свиток не тратится), флажок 4-й профессии 16521 не показывается; 0 — как эталон: окно 16500 с пустыми ветками, 5450 с веткой 0."),
	ECVF_Default);

namespace
{
	const TCHAR* kWbpPath = TEXT("/Game/AsdaAssets/UI/Player_UI/Character/WBP_Skill_OneTree_Reset_Win");
	// z окна: полоса окон-сообщений `CMsgBoxMgr` (как 1559); вопросы 12865/12866 — в полосе модалок выше.
	constexpr int32 kWinZ = 8850;
	// UV состояний `RadioButton Smelting_V3Btn2` и `CheckButton Box_Check` (JuiSkin.ini; у обоих одинаковые):
	//   SS 0/1 — обычное, SS 2/3 — выбранное/поставленное.
	const FBox2D kUvNormal(FVector2D(0.479f, 0.176f), FVector2D(0.514f, 0.213f));
	const FBox2D kUvChosen(FVector2D(0.959f, 0.449f), FVector2D(0.994f, 0.486f));

	// ★ 26.10.2026: предикаты класса 4-й профессии — общая точка `AsdaSelf` (`IsJob4Class` = `uiTradeDialogInitFromItemType`,
	//   ui_core.c:59010; `Job4GradeOfClass` = ui_core.c:58444-58449; полоса — `AsdaSelf::BandOfClass`). Прежние копии здесь и в окне
	//   навыков сведены в одну. ★ [ОРИГИНАЛ-дефект] (ревизия wave_1025d): у классов 34…39 флажок 16521 эталон показывает, а ветки
	//   у них нет (−1) и полоса 0 ⇒ ветка пунктов `3·(0−1)+1+i` = −2…0, в +135 тела 5450 уходит 0 — сервер сбрасывает ветку 0,
	//   свиток тратится. Исправление — ключ `asda.SkillReset.CraftClassGuard` (BeginUse/OpenTreeWindow).
	// Показать узел и его поддерево (импортёр протаскивает `Visible = "0"` рекурсией на всё поддерево): кнопки — Visible,
	//   прочее — SelfHitTestInvisible. Скрыть — только сам узел.
	void ShowSubtree(UWidget* W, bool bShow)
	{
		if (!W) { return; }
		if (!bShow) { W->SetVisibility(ESlateVisibility::Collapsed); return; }
		W->SetVisibility((Cast<UButton>(W) || Cast<UCheckBox>(W)) ? ESlateVisibility::Visible : ESlateVisibility::SelfHitTestInvisible);
		if (UPanelWidget* P = Cast<UPanelWidget>(W))
		{
			for (int32 i = 0; i < P->GetChildrenCount(); ++i) { ShowSubtree(P->GetChildAt(i), true); }
		}
	}
	// Подставить UV плана.
	void SetUv(UImage* Img, bool bChosen)
	{
		if (!Img) { return; }
		FSlateBrush B = Img->GetBrush();
		B.SetUVRegion(bChosen ? kUvChosen : kUvNormal);
		Img->SetBrush(B);
	}
}

void UAsdaSkillTreeResetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(UAsdaNetSubsystem::StaticClass());
	Collection.InitializeDependency(UAsdaWindowSubsystem::StaticClass());
}

void UAsdaSkillTreeResetSubsystem::Deinitialize()
{
	Relays.Reset();
	Widget = nullptr;
	Super::Deinitialize();
}

void UAsdaSkillTreeResetSubsystem::ResetSessionState(EAsdaSessionResetReason Reason)
{
	// [ПОРТ] Оригинал при разрыве закрывает клиент; у порта сессия кончилась — запрос прежнего персонажа не должен уйти.
	//   Смена карты: виджеты сносит травел, а ячейку свитка новая карта переприсылает — запрос тоже снимаем.
	if (IsWindowShown()) { HideWindow(); }
	Pending = FPendingUse();
	Grade = -1; SelectedTree = -1; bFourthChecked = false;
	++AskSerial;   // висящий вопрос прошлой сессии ничего не отправит
	UE_LOG(LogAsdaSkillReset, Verbose, TEXT("[сброс навыков] сброс сессии (%s)"), AsdaSessionResetReasonName(Reason));
}

int32 UAsdaSkillTreeResetSubsystem::SelfClassId() const
{
	const UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	return Net ? AsdaSelf::GetClassId(Net) : -1;
}

int32 UAsdaSkillTreeResetSubsystem::BandOfClass(int32 C)
{
	// `itemGradeBandFromTypeIndex@0xEC97A0` (item_inventory.c:64176) — общая точка `AsdaSelf::BandOfClass`.
	return AsdaSelf::BandOfClass(C);
}

bool UAsdaSkillTreeResetSubsystem::BeginUse(int32 ContainerId, int32 CellIndex, int32 ItemCode, int32 SubType)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaNetSubsystem* Net = GI ? GI->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	if (!Net || !HandlesSubType(SubType)) { return false; }
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.SkillReset.KeepFirstRequest`): окно 16500 уже держит запрос первого свитка — не подменять.
	if (IsWindowShown() && Pending.IsSet() && CVarSkillResetKeepFirst.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] свиток %d (подтип %d, ячейка %d) отклонён: окно 16500 открыто для свитка %d (ячейка %d) — запрос не подменяется"),
			ItemCode, SubType, CellIndex, Pending.ItemCode, Pending.CellIndex);
		return true;
	}
	// Гейт `*(BYTE*)(g_LocalPlayer + 1093)` — профессия (`SelfJob`). 0 (новичок) ⇒ плашка 221 нотис 12848, без вопроса и пакета.
	//   −1 (поле ещё не пришло) не гейтим — сервер ответит 5451 кодом 15 (тот же 12848).
	if (Net->SelfJob == 0)
	{
		AsdaNotice::PlankLoc(GI, EAsdaMsgCategory::System, 12848, AsdaNoticeColor::Gold, 10000.f, 5450);
		UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] свиток %d (подтип %d): профессии нет — нотис 12848, без вопроса"), ItemCode, SubType);
		return false;
	}
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.SkillReset.CraftClassGuard`): сброс ВЕТКИ у класса без боевой полосы — отклонить до отправки.
	if (SubType == 36 && CVarSkillResetCraftGuard.GetValueOnGameThread() != 0)
	{
		const int32 C = SelfClassId();
		if (C > 0 && BandOfClass(C) == 0)
		{
			AsdaNotice::MsgBoxLoc(GI, EAsdaMsgCategory::ItemRefused, 9421, AsdaLoc::kBtnOk, 9421);
			UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] свиток %d (подтип 36): класс %d вне боевых полос (ремесленный 34…39 / метка) — ")
				TEXT("веток нет, нотис 9421, 5450 не отправлен, свиток цел"), ItemCode, C);
			return false;
		}
	}
	Pending.ContainerId = ContainerId;
	Pending.CellIndex = CellIndex;
	Pending.ItemCode = ItemCode;
	Pending.SubType = SubType;
	if (SubType == 36) { return OpenTreeWindow(); }
	// 35/54/55: вопрос (1, 12866), ветки нет.
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] свиток %d (подтип %d, ячейка %d): вопрос 12866"), ItemCode, SubType, CellIndex);
	AskAndSend(12866, -1);
	return true;
}

bool UAsdaSkillTreeResetSubsystem::EnsureWidget()
{
	const EAsdaJuiEnsure R = FAsdaJuiWindowHandle(kWindowId, kWbpPath, kWinZ).Ensure(GetGameInstance(), Widget, bReady,
		[this](UAsdaWindowSubsystem& WM)
		{
			WM.CloseWindow(kWindowId);
			Widget = nullptr; bReady = false;
		},
		[this](UAsdaWindowSubsystem& WM)
		{
			SetupControls();
			WM.SetWindowPriority(kWindowId, true);   // `AlwaysTop = "1"` — поверх инвентаря, из которого свиток применён
		},
		[]()
		{
			UE_LOG(LogAsdaSkillReset, Error, TEXT("[сброс навыков] WBP_Skill_OneTree_Reset_Win не загрузился (%s)"), kWbpPath);
		});
	return FAsdaJuiWindowHandle::Succeeded(R) && bReady;
}

void UAsdaSkillTreeResetSubsystem::LogWidgetTree() const
{
	if (!Widget || !Widget->WidgetTree) { return; }
	TArray<UWidget*> All;
	Widget->WidgetTree->GetAllWidgets(All);
	for (UWidget* W : All)
	{
		const UCanvasPanelSlot* S = AsdaUi::SlotOf(W);
		const UTextBlock* T = Cast<UTextBlock>(W);
		UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] узел %s (%s) родитель=%s поз=(%.0f,%.0f) разм=(%.0f,%.0f) вид=%d%s"),
			*W->GetName(), *W->GetClass()->GetName(), W->GetParent() ? *W->GetParent()->GetName() : TEXT("-"),
			S ? S->GetPosition().X : 0.f, S ? S->GetPosition().Y : 0.f, S ? S->GetSize().X : 0.f, S ? S->GetSize().Y : 0.f,
			(int32)W->GetVisibility(),
			T ? *FString::Printf(TEXT(" перенос=%d ширина=%.0f"), T->GetAutoWrapText() ? 1 : 0, T->GetWrapTextAt()) : TEXT(""));
	}
}

void UAsdaSkillTreeResetSubsystem::SetupControls()
{
	if (!Widget) { return; }
	Relays.Reset();
	TxtTrees.Reset(); BtnTrees.Reset(); ImgTrees.Reset();
	auto Txt = [this](const TCHAR* N) { return Cast<UTextBlock>(Widget->GetWidgetFromName(FName(N))); };
	auto Btn = [this](const TCHAR* N) { return Cast<UButton>(Widget->GetWidgetFromName(FName(N))); };
	TxtTitle  = Txt(TEXT("Skill_Reset_Title"));            // 16501
	TxtBody   = Txt(TEXT("Skill_Reset_Txt00"));            // 16502
	BtnOk     = Btn(TEXT("Skill_Reset_OkBtn"));            // 16503
	BtnCancel = Btn(TEXT("Skill_Reset_CanBtn"));           // 16504
	CapOk     = Txt(TEXT("Skill_Reset_OkBtn_Text"));
	CapCancel = Txt(TEXT("Skill_Reset_CanBtn_Text"));
	for (int32 i = 0; i < 3; ++i)
	{
		TxtTrees.Add(Txt(*FString::Printf(TEXT("Skill_Reset_Tree%d"), i + 1)));            // 16511…16513
		BtnTrees.Add(Btn(*FString::Printf(TEXT("Skill_Reset_SelBtn_OPT%d"), i)));          // пункты 16510
	}
	// Планы пунктов радиогруппы 16510: картинки `Skill_Reset_SelBtn_*` (импортёр раскладывает элементы `ES 0…2` по планам) —
	//   порядок пунктов = порядок по вертикали.
	if (Widget->WidgetTree)
	{
		TArray<UWidget*> All;
		Widget->WidgetTree->GetAllWidgets(All);
		TArray<UImage*> Sel;
		for (UWidget* W : All)
		{
			UImage* Img = Cast<UImage>(W);
			if (Img && Img->GetName().StartsWith(TEXT("Skill_Reset_SelBtn"))) { Sel.Add(Img); }
		}
		Sel.Sort([](const UImage& A, const UImage& B)
		{
			const UCanvasPanelSlot* SA = AsdaUi::SlotOf(&A);
			const UCanvasPanelSlot* SB = AsdaUi::SlotOf(&B);
			return (SA ? SA->GetPosition().Y : 0.f) < (SB ? SB->GetPosition().Y : 0.f);
		});
		for (UImage* Img : Sel) { ImgTrees.Add(Img); }
		// Флажок 16521 `Box_Check` (`CheckButton`, импортёр — UCheckBox `…_CHK00`) с подписью 16522 (`…_TXT00`).
		FourthGroup = Widget->GetWidgetFromName(TEXT("Box_Check_grp"));
		ChkFourth = Cast<UCheckBox>(Widget->GetWidgetFromName(TEXT("Skill_OneTree_Reset_Win_CHK00")));
		TxtFourth = Txt(TEXT("Skill_OneTree_Reset_Win_TXT00"));
		if (ChkFourth) { ChkFourth->OnCheckStateChanged.AddUniqueDynamic(this, &UAsdaSkillTreeResetSubsystem::OnFourthCheckChanged); }
	}
	for (int32 i = 0; i < BtnTrees.Num(); ++i)
	{
		AsdaUi::BindButton(this, BtnTrees[i].Get(), i, [this](int32 Idx) { OnTreeClicked(Idx); }, Relays);
	}
	AsdaUi::BindButton(this, BtnOk.Get(), 0, [this](int32) { OnOkClicked(); }, Relays);
	AsdaUi::BindButton(this, BtnCancel.Get(), 0, [this](int32) { OnCancelClicked(); }, Relays);
	// Подписи инициализации (`CMsgBoxMgr_InitControls`, ui_core.c:55889-55934): «ОК» 12863, «Отмена» 12864, 16522 = 111800.
	UGameInstance* GI = GetGameInstance();
	if (CapOk)     { CapOk->SetText(FText::FromString(AsdaNotice::Loc(GI, 12863))); }
	if (CapCancel) { CapCancel->SetText(FText::FromString(AsdaNotice::Loc(GI, 12864))); }
	if (TxtFourth) { TxtFourth->SetText(FText::FromString(AsdaNotice::Loc(GI, 111800))); }
	const int32 NTree = TxtTrees.FilterByPredicate([](const TObjectPtr<UTextBlock>& T) { return T != nullptr; }).Num();
	const int32 NBtn = BtnTrees.FilterByPredicate([](const TObjectPtr<UButton>& B) { return B != nullptr; }).Num();
	UE_LOG(LogAsdaSkillReset, Log,
		TEXT("[сброс навыков] окно 16500 разобрано: заголовок=%d текст=%d ОК=%d Отмена=%d ветки %d/3 пункты %d/3 планы %d/3 флажок: группа=%d флажок=%d подпись=%d"),
		TxtTitle != nullptr, TxtBody != nullptr, BtnOk != nullptr, BtnCancel != nullptr, NTree, NBtn, ImgTrees.Num(),
		FourthGroup != nullptr, ChkFourth != nullptr, TxtFourth != nullptr);
	if (!bLoggedTree) { LogWidgetTree(); bLoggedTree = true; }
	bReady = BtnOk && BtnCancel && NBtn == 3;
	if (!bReady)
	{
		UE_LOG(LogAsdaSkillReset, Error, TEXT("[сброс навыков] окно 16500: нет кнопок ОК/Отмена или пунктов радиогруппы — окно не используется"));
	}
}

bool UAsdaSkillTreeResetSubsystem::OpenTreeWindow()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM || !EnsureWidget()) { return false; }
	const int32 ClassId = SelfClassId();
	const int32 Band = BandOfClass(ClassId);
	if (Band == 0)
	{
		UE_LOG(LogAsdaSkillReset, Warning, TEXT("[сброс навыков] класс %d вне полос 1…3 — названия веток эталон берёт как есть (9397+)"), ClassId);
	}
	// 16521 — только у классов 4-й профессии; снят.
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Job4.StageFromSubJob`): класс предиката — ступень 4-й профессии (подкласс той же полосы, если ≠ 0;
	//   эталон смотрел только основной класс, который у сменившего профессию остаётся 3/14/25). Ремесленному классу 34…39 флажок не
	//   показывается (`asda.SkillReset.CraftClassGuard`): ветки у него нет.
	const int32 StageClass = AsdaSelf::GetJob4StageClass(GI);
	bFourthShown = AsdaSelf::IsJob4Class(StageClass)
		&& !(CVarSkillResetCraftGuard.GetValueOnGameThread() != 0 && (AsdaSelf::IsCraftClass(StageClass) || Band == 0));
	ShowSubtree(FourthGroup.Get(), bFourthShown);
	bFourthChecked = false;
	// 16501 / 16502.
	const UAsdaTextSubsystem* TS = GI->GetSubsystem<UAsdaTextSubsystem>();
	if (TxtTitle) { TxtTitle->SetText(FText::FromString(AsdaNotice::Loc(GI, 12861))); }
	// Эталон: loc `12300 + основной класс` (ui_core.c:59021) — слот [ss2] текста 12862. ★ 26.10.2026 [ИСПРАВЛЕНИЕ]
	//   (`asda.Job4.StageFromSubJob`): при подклассе ≠ 0 — его имя, как в окне характеристик (`uiEquipPanelSetGradeText`,
	//   ui_core.c:11234: подкласс ? подкласс : класс). ⚠ В текущих RU/EN строках 12862 слотов нет — имя видно только в журнале.
	const int32 SubJobForName = AsdaSelf::GetSubJob(GI);
	const int32 NameClass = (AsdaSelf::IsJob4StageFromSubJobOn() && SubJobForName > 0) ? SubJobForName : ClassId;
	const FString ClassName = AsdaNotice::Loc(GI, 12300 + FMath::Max(0, NameClass));
	if (TxtBody)
	{
		const UAsdaWorldSubsystem* WS = GI->GetSubsystem<UAsdaWorldSubsystem>();
		const FString Name = WS ? WS->GetSelfCharName() : FString();
		const FString Body = TS ? TS->FormatById(12862, { Name, ClassName }, {}) : AsdaNotice::Loc(GI, 12862);
		TxtBody->SetText(FText::FromString(Body));
	}
	// 16510: пункт 0; 16511…16513: названия веток полосы.
	SelectedTree = 0;
	for (int32 i = 0; i < TxtTrees.Num(); ++i)
	{
		if (UTextBlock* T = TxtTrees[i].Get()) { T->SetText(FText::FromString(AsdaNotice::Loc(GI, 3 * (Band - 1) + i + 9400))); }
	}
	Grade = 3 * (Band - 1) + 1;
	RefreshSelectionVisual();
	WM->SetWindowVisible(kWindowId, true);
	WM->BringWindowToFront(kWindowId);
	// `AlignXPos/AlignYPos = 16/32 50 50` — по центру экрана; постановщик держит центр и при смене размера.
	TWeakObjectPtr<UAsdaWindowSubsystem> WeakWM(WM);
	TWeakObjectPtr<UAsdaSkillTreeResetSubsystem> WeakSelf(this);
	TFunction<void()> Place = [WeakWM, WeakSelf]()
	{
		if (UAsdaWindowSubsystem* M = WeakWM.Get()) { if (WeakSelf.IsValid() && WeakSelf->Widget) { M->CenterWindowOnScreen(WeakSelf->Widget, nullptr); } }
	};
	Place();
	WM->SetWindowRePlacer(kWindowId, MoveTemp(Place));
	// ★ 26.10.2026 (ревизия wave_1025d): поглотителя ESC нет — признака `CControl+0x228` у 16500 нет (в `kEscCloseWindows` окна нет),
	//   ESC идёт дальше по цепочке, окно остаётся (как эталон). Прежний `RegisterEscSwallower` отклонялся с предупреждением.
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] окно 16500: свиток %d ячейка %d, класс %d (ступень 4-й профессии %d, имя класса «%s») полоса %d, ветки %d…%d, флажок 4-й профессии %s, ветка %d"),
		Pending.ItemCode, Pending.CellIndex, ClassId, StageClass, *ClassName, Band, 3 * (Band - 1) + 9400, 3 * (Band - 1) + 9402,
		bFourthShown ? TEXT("виден") : TEXT("скрыт"), Grade);
	return true;
}

void UAsdaSkillTreeResetSubsystem::HideWindow()
{
	if (UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr)
	{
		WM->SetWindowVisible(kWindowId, false);
	}
}

bool UAsdaSkillTreeResetSubsystem::IsWindowShown() const
{
	const UAsdaWindowSubsystem* WM = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	return WM && Widget && WM->IsWindowVisible(kWindowId);
}

void UAsdaSkillTreeResetSubsystem::RefreshSelectionVisual()
{
	for (int32 i = 0; i < ImgTrees.Num(); ++i) { SetUv(ImgTrees[i].Get(), i == SelectedTree); }
	if (ChkFourth) { ChkFourth->SetIsChecked(bFourthChecked); }   // программная установка делегат не зовёт
}

void UAsdaSkillTreeResetSubsystem::OnFourthCheckChanged(bool bIsChecked)
{
	// 67073 (поставлен) / 67074 (снят повторным щелчком) — разбор в OnFourthClicked; состояние флажка ведёт подсистема.
	UE_LOG(LogAsdaSkillReset, Verbose, TEXT("[сброс навыков] флажок 16521: щелчок игрока → %d"), bIsChecked ? 1 : 0);
	OnFourthClicked();
}

void UAsdaSkillTreeResetSubsystem::OnTreeClicked(int32 Index)
{
	if (!IsWindowShown() || Index < 0 || Index > 2) { return; }
	// 66817 по 16510: ветка = пункт + 3·(полоса−1) + 1; флажок 16521 снят.
	SelectedTree = Index;
	Grade = Index + 3 * (BandOfClass(SelfClassId()) - 1) + 1;
	bFourthChecked = false;
	RefreshSelectionVisual();
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] выбран пункт %d → ветка %d"), Index, Grade);
}

void UAsdaSkillTreeResetSubsystem::OnFourthClicked()
{
	if (!IsWindowShown() || !bFourthShown) { return; }
	if (bFourthChecked)
	{
		// 67074: флажок снова поставлен (`SetVisibleState(16521, 1)`), ветка прежняя.
		UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] флажок 4-й профессии уже поставлен — остаётся (эталон 67074), ветка %d"), Grade);
		RefreshSelectionVisual();
		return;
	}
	// 67073: радиогруппа без выбора, ветка 10/11/12 по классу.
	bFourthChecked = true;
	SelectedTree = -1;
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Job4.StageFromSubJob`): ветка — по ступени 4-й профессии (подкласс), эталон — по основному классу.
	const int32 G = AsdaSelf::Job4GradeOfClass(AsdaSelf::GetJob4StageClass(GetGameInstance()));
	if (G > 0) { Grade = G; }
	RefreshSelectionVisual();
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] флажок 4-й профессии поставлен → ветка %d"), Grade);
}

void UAsdaSkillTreeResetSubsystem::OnOkClicked()
{
	if (!IsWindowShown()) { return; }
	// 66561 по 16503: окно скрыто, вопрос (1, 12865).
	HideWindow();
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] «ОК» 16503: вопрос 12865, ветка %d"), Grade);
	AskAndSend(12865, Grade);
}

void UAsdaSkillTreeResetSubsystem::OnCancelClicked()
{
	if (!IsWindowShown()) { return; }
	// 66561 по 16504: окно скрыто, ничего не уходит — свиток не тратится.
	HideWindow();
	Pending = FPendingUse();
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] «Отмена» 16504: окно закрыто, 5450 не отправлен"));
}

void UAsdaSkillTreeResetSubsystem::AskAndSend(int32 NoticeId, int32 SendGrade)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaWindowSubsystem* WM = GI ? GI->GetSubsystem<UAsdaWindowSubsystem>() : nullptr;
	if (!WM) { return; }
	const uint32 Serial = ++AskSerial;
	TWeakObjectPtr<UAsdaSkillTreeResetSubsystem> WeakSelf(this);
	// Вопрос о необратимом — подтверждение только щелчком (таблица Enter, AsdaEnterGuard).
	AsdaEnterGuard::NoEnter(WM)->ShowGameConfirm(AsdaNotice::Loc(GI, NoticeId),
		[WeakSelf, Serial, SendGrade, NoticeId](bool bYes)
		{
			UAsdaSkillTreeResetSubsystem* Self = WeakSelf.Get();
			if (!Self || Serial != Self->AskSerial) { return; }   // сессия сменилась — ответ прошлого вопроса
			if (!bYes)
			{
				UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] вопрос %d: «Отмена» — 5450 не отправлен, свиток цел"), NoticeId);
				Self->Pending = FPendingUse();
				return;
			}
			Self->SendUse(SendGrade);
		},
		0.f, 2288, 2289, NoticeId, /*EtalonKind*/ 1);
}

bool UAsdaSkillTreeResetSubsystem::SendUse(int32 SendGrade)
{
	UGameInstance* GI = GetGameInstance();
	UAsdaInventorySubsystem* Inv = GI ? GI->GetSubsystem<UAsdaInventorySubsystem>() : nullptr;
	const FPendingUse P = Pending;
	Pending = FPendingUse();
	if (!Inv || !P.IsSet()) { return false; }
	const FAsdaItemInstance* Cell = Inv->GetCell(P.ContainerId, P.CellIndex);
	if (!Cell || Cell->IsEmpty() || Cell->ItemCode != P.ItemCode)
	{
		// [ИСПРАВЛЕНИЕ] Эталон шлёт запомненный запрос как есть; за время вопроса ячейка могла смениться — не тратим чужой предмет.
		UE_LOG(LogAsdaSkillReset, Warning, TEXT("[сброс навыков] ячейка %d уже не держит свиток %d — 5450 не отправлен"), P.CellIndex, P.ItemCode);
		return false;
	}
	if (P.SubType == 36) { AsdaArm5450TreeGrade(SendGrade); }
	const bool bOk = Inv->UsePremiumItem(P.ContainerId, P.CellIndex, FString());
	AsdaArm5450TreeGrade(-1);   // отправка не состоялась (гейты) — ветка не должна достаться следующей
	UE_LOG(LogAsdaSkillReset, Log, TEXT("[сброс навыков] «Да»: свиток %d (подтип %d) ячейка %d ветка %d → 5450 %s"),
		P.ItemCode, P.SubType, P.CellIndex, SendGrade, bOk ? TEXT("отправлен") : TEXT("НЕ отправлен"));
	return bOk;
}

FString UAsdaSkillTreeResetSubsystem::DumpState() const
{
	return FString::Printf(TEXT("окно=%d свиток=%d ячейка=%d подтип=%d ветка=%d пункт=%d флажок=%d/%d"),
		IsWindowShown() ? 1 : 0, Pending.ItemCode, Pending.CellIndex, Pending.SubType, Grade, SelectedTree,
		bFourthShown ? 1 : 0, bFourthChecked ? 1 : 0);
}
