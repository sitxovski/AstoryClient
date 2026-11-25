// Copyright (c) 2026 @sitxovski (Discord)
// AsdaSkillWindowSubsystem_Grid.cpp — ★ R3-c 12.11.2026 (разрез AsdaSkillWindowSubsystem.cpp, дословный перенос): перестройка: вкладки, сетка, шторки отката, очки, превью, дерево, полоса прокрутки.
//   Общее для частей — в AsdaSkillWindowSubsystem_Private.h; шапка и реверс — в AsdaSkillWindowSubsystem.cpp.
#include "UI/AsdaSkillWindowSubsystem.h"
#include "Resources/AsdaAssetPaths.h"   // ★ волна 2 п. 7 (14.09.2026): реестр путей и правил имён ассетов (генерируемый)

#include "Resources/AsdaTextureSubsystem.h"

#if PLATFORM_WINDOWS
// ★ Системный интервал двойного клика: оригинал получает событие 0xD от самой Windows
//   (`WM_LBUTTONDBLCLK`), то есть по значению «Панель управления → Мышь». В Slate такого запроса нет
//   (`FSlateApplication` умеет только маршрутизировать уже готовое событие), поэтому берём его у ОС
//   тем же вызовом, каким его берёт оконная процедура оригинала.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/MinWindows.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "Engine/Texture2D.h"
#include "AsdaSkillWindowSubsystem_Private.h"   // ★ R3-c 12.11.2026: общее для частей AsdaSkillWindowSubsystem*.cpp

static constexpr int32 kLocPointsCaption    = 12136;   // подпись строки очков
static constexpr int32 kLocJob4PointCaption = 111501;  // подпись строки очков 4-й профессии

static constexpr int32 kLocTreeCaption      = 12062;   // подпись выбранного дерева

namespace AsdaSkillWheel {
// ★ UV состояний плана опции селектора 12005 (скин `Skill_W124_3Btn`, тип RadioButton):
//   SS0/SS1 (обычное/наведение) — V 0.666..0.703, SS2/SS3 (выбрано) — V 0.709..0.746.
//   Числа из данных скина, не подобраны.
// ═══════ ПОЛОСА ПРОКРУТКИ СЕТКИ: ГДЕ ОНА СТОИТ (данные СКИНА — в общей полосе) ═══════
//   ★ Отдельного контрола полосы в JUI НЕТ. Оригинал строит её в рантайме:
//   `uiControl_AttachVertScrollbar@0x12F3160` читает у скина колёсного окна (`WheelWin_Nomal`,
//   контрол 12291 `SkillTree_Wheel`) ключ `ScrollBarFileNameVert = "Nomal_Scr"` и подвешивает
//   `CScrollBar` ребёнком этого окна.
//   ★★★ АУДИТ №3 (A1): числа СКИНА (атлас `n_01`, ширина 11, кнопка 11, минимальная длина бегунка 6,
//   все пять UV) отсюда УБРАНЫ — они жили здесь ВТОРОЙ ПОЛНОЙ КОПИЕЙ рядом с инвентарной, и правка
//   UV в одном окне разъезжала полосу второго. Теперь это `AsdaNomalScr` (`UI/AsdaScrollBarNomal.h`),
//   а раскладка планов (`uiScrollbarLayoutPlanes@0x12FE500`) и разбор ввода
//   (`uiScrollbarOnButtonEvent` / `uiScrollbarOnMouseDrag`) — в `FAsdaScrollBarNomal`.
//   Здесь остаётся ТОЛЬКО собственное окна: где у него стоит полоса.
	// Позиция = (rect.right, rect.top) КОЛЁСНОГО ОКНА 12291. Его JUI-прямоугольник — Origin (1,0),
	//   Size 400×477 внутри 12010 (слот `SkillTree_Wheel_grp` в ассете с 19.09.2026 — (144, 8, 400, 477)
	//   в родителе; прежнее (6, 8) — смещение внутри старой union-группы).
	//   ⚠ Это ФОЛБЭК на случай, если слота у группы ещё нет: живое положение берётся из слота
	//     колёсного окна (окно таскается мышью), см. `LayoutScrollBar`.
	// ★ 19.09.2026: координаты приведены к НОВОЙ системе — после правки ассета колёсная зона стоит
	//   (144, 8) 400×477 и обрезку держит сам ассет (рантайм-обрезка `ApplyScrollClip` удалена).
	//   Прежние (6, 8) относились к старой union-группе и при срабатывании фолбэка увели бы картинку.
	static constexpr float kWheelX   = 144.f;
	static constexpr float kWheelY   = 8.f;
	static constexpr float kWheelW   = 400.f;
}

// Высота полосы = высота колёсного окна и берётся из `WheelRectSize.Y` (UPROPERTY, 477 по JUI).
static constexpr float kTabUvNrmTop    = 0.666f;
static constexpr float kTabUvNrmBottom = 0.703f;
static constexpr float kTabUvSelTop    = 0.709f;
static constexpr float kTabUvSelBottom = 0.746f;

static constexpr int32 kLocSoulPanelTitle   = 110500;  // заголовок панели «Хранитель души» (56301)

static ASDA_DEV_CVAR(int32, CVarSkillLegacyPointFormula,
	TEXT("asda.Skill.LegacyPointFormula"), 0,
	TEXT("1 = воспроизводить дефект оригинала: при уровне >= 60 показывать «110 − потрачено» вместо серверного значения."),
	ECVF_Default);

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (сессия job4-points; жалоба «4-я профессия: можно выйти за лимит очков, уже −8») — ДВА ПУЛА ОЧКОВ.
//   Счёт очков у сервера ОДИН (CUser+0x19B = Charinfo.SkillPoint, 2·ур.−8), но эталон окна делит его на два пула:
//     • строка 1092 при уровне ≥ 60 — «110 − вложено в основные деревья» (`skillUiUpdatePointText@0xE56720`, `skillwin_OpenSkillWindow`);
//     • строка 12413 — «(вложено во все деревья + свободные) − 110 − вложено в дерево 4-й профессии» (`Skill_BuildTreeUI@0xE532E0`).
//   110 = все очки до 59-го уровня (2·59−8): стоковый сервер держал потолок 59 (наш P29 поднял до 100), очки с 60-го — пул 4-й
//   профессии. Гейт изучения эталона (`skillWndSelectSlot@0xE53C80`) и сервер (`Recv_SkillPoint_SkillActive@0x4774C0`) смотрят
//   ТОЛЬКО на общий счёт — поэтому навык 4-й профессии брал очки основного пула, а счётчик 12413 уходил в минус. Ответ 5254 код 8
//   (нотис 111701 «недостаточно очков после 4-й профессии») клиент оригинала знает, старый сервер его не шлёт (его ставит P110).
//   1 = показ по двум пулам (не больше свободных), подсветка «можно учить» и клик — по пулу навыка; 0 = прежнее (общий счёт).
static ASDA_DEV_CVAR(int32, CVarSkillJob4Pools,
	TEXT("asda.Skill.Job4Pools"), 1,
	TEXT("[ИСПРАВЛЕНИЕ 05.11.2026] 1 = очки делятся на пулы «основные деревья (110)» и «дерево 4-й профессии (сверх 110 с 60-го уровня)»: "
	     "показ, подсветка и клик по пулу навыка; 0 = прежнее (один общий счёт, как гейт оригинала)."),
	ECVF_Default);

// Уровень, с которого есть пул 4-й профессии (`cmp level, 3Ch` в `Skill_BuildTreeUI@0xE532E0`; ниже счётчик 12413 = «0»).
static constexpr int32 kJob4PoolMinLevel = 60;

void UAsdaSkillWindowSubsystem::ResetAllSkillCooldowns()
{
	// Эталон `skillHotbarRefreshForUser` (skill_buff.c:9-23) → `skillHotbarRefreshLocked`: очистить реестр и перестроить дерево.
	const int32 N = Cooldowns.Num();
	Cooldowns.Reset();
	if (IsWindowOpen()) { RebuildAll(); }
	UE_LOG(LogAsdaSkillWin, Log, TEXT("[навыки] 6058: сброшено откатов навыков %d"), N);
}

void UAsdaSkillWindowSubsystem::RebuildAll()
{
	RebuildTabs();
	RefreshTreeArt();
	RebuildGrid();
	RefreshPointTexts();
	RefreshPreviewPanel();
	LayoutScrollBar();
}

void UAsdaSkillWindowSubsystem::RebuildTabs()
{
	const int32 Band = GetSelfBand();
	// Эталон `skillwin_OpenSkillWindow` (skill_buff.c:32759): кнопка 4-й профессии видима только у продвинутых классов
	//   job ∈ {4..7, 15..18, 26..29, 34..39} — по ОСНОВНОМУ классу `gap_438[0]`.
	// ★ 26.10.2026 [ИСПРАВЛЕНИЕ] (`asda.Job4.StageFromSubJob`): сервер пишет 4-ю ступень в ПОДКЛАСС (`sub_461010`), основной класс
	//   остаётся 3/14/25 — класс предиката берётся общей точкой `AsdaSelf::Job4StageClass` (подкласс той же полосы, если ≠ 0).
	//   Подмена класса для проверки (`asda.Skill.Class <job> [sub]`) идёт тем же правилом: обе пары берутся окном.
	const int32 Job = AsdaSelf::Job4StageClass(GetSelfJob(), GetSelfSubJob());
	const bool bJob4 = AsdaSelf::IsJob4Class(Job);
	if (Job4Frame)      { Job4Frame->SetVisibility(bJob4 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (Job4Button)     { Job4Button->SetVisibility(bJob4 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (Job4ButtonText)
	{
		Job4ButtonText->SetVisibility(bJob4 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		Job4ButtonText->SetText(FText::FromString(LocRaw(kLocJob4Button)));
	}
	// Строка очков 4-й профессии показывается только на вкладке 3 (эталон `skillPanelShowTree`).
	if (Job4PointFrame) { Job4PointFrame->SetVisibility(CurTree == 3 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed); }
	if (TxtJob4Back)    { TxtJob4Back->SetText(FText::FromString(LocRaw(kLocJob4PointCaption))); }
	if (TxtPointBack)   { TxtPointBack->SetText(FText::FromString(LocRaw(kLocPointsCaption))); }

	// ★★★ СЕЛЕКТОРЫ ВКЛАДОК. Эталон: 12005 (3 кнопки) `SetVisible(1)` в `CSkillWindow_InitControls`
	//   и НИКОГДА не скрывается; 12006 (4 кнопки) в JUI `Visible=0`, и вызова `CControl_SetVisible`
	//   для него НЕТ НИ В ОДНОМ месте экспорта — четырёхкнопочный селектор в этой сборке не
	//   показывается вообще, а на дерево 4-й профессии ведёт ТОЛЬКО кнопка `SOUL_SP_W_T` (12401).
	//   Порт раньше подменял 12005 на 12006 у продвинутых классов — это было наоборот.
	for (int32 i = 0; i < Tab3Buttons.Num(); ++i)
	{
		if (Tab3Buttons[i]) { Tab3Buttons[i]->SetVisibility(ESlateVisibility::Visible); }
		if (Tab3Texts.IsValidIndex(i) && Tab3Texts[i]) { Tab3Texts[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
	}
	for (int32 i = 0; i < Tab4Buttons.Num(); ++i)
	{
		if (Tab4Buttons[i]) { Tab4Buttons[i]->SetVisibility(ESlateVisibility::Collapsed); }
		if (Tab4Texts.IsValidIndex(i) && Tab4Texts[i]) { Tab4Texts[i]->SetVisibility(ESlateVisibility::Collapsed); }
	}
	// ★ Состояние «выбрано» у 12005 — это план SS2 скина `Skill_W124_3Btn` (RadioButton): тот же
	//   атлас, но UV по V сдвинут на 0.709..0.746 вместо 0.666..0.703. На дереве 4-й профессии
	//   эталон снимает выделение целиком (`SetSelectedIndex(m_pCharaBtn1, -1)` в `skillPanelShowTree`).
	for (int32 i = 0; i < Tab3Planes.Num(); ++i)
	{
		UImage* Pl = Tab3Planes[i].Get();
		if (!Pl) { continue; }
		Pl->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		const bool bSel = (CurTree != 3) && (i == CurTree);
		FSlateBrush B = Pl->GetBrush();
		const FBox2f Uv = B.GetUVRegion();
		const float U0 = (Uv.Max.X > Uv.Min.X) ? Uv.Min.X : 0.f;
		const float U1 = (Uv.Max.X > Uv.Min.X) ? Uv.Max.X : 1.f;
		B.SetUVRegion(bSel ? FBox2f(FVector2f(U0, kTabUvSelTop), FVector2f(U1, kTabUvSelBottom))
		                   : FBox2f(FVector2f(U0, kTabUvNrmTop), FVector2f(U1, kTabUvNrmBottom)));
		Pl->SetBrush(B);
	}
	// Подписи вкладок: 9400..9408 по полосе (эталон `uiPopulateUpgradeOptionList@0xE54900`),
	//   4-я вкладка — по подклассу (9422..9427). Каждая опция получает СВОЙ текст над своей картинкой
	//   (в эталоне это отдельные элементы списка контрола 12005/12006; импортёр свёл их в один TextBlock,
	//   поэтому подписи опций создаются в рантайме — см. BuildTabCaptions).
	auto TabCaption = [this, Band](int32 T) -> FString
	{
		const int32 Loc = (T == 3) ? UAsdaSkillDatabase::Job4TabLocId(Band, GetSelfSubJob())
		                           : UAsdaSkillDatabase::TreeTabLocId(Band, T);
		return (Loc > 0) ? LocRaw(Loc) : FString();
	};
	for (int32 i = 0; i < Tab3Texts.Num(); ++i)
	{
		if (!Tab3Texts[i]) { continue; }
		Tab3Texts[i]->SetAutoWrapText(false);
		// Активная вкладка помечается ПЛАНОМ SS2 (см. переключение UV ниже), а не текстом.
		Tab3Texts[i]->SetText(FText::FromString(TabCaption(i)));
	}
	for (int32 i = 0; i < Tab4Texts.Num(); ++i)
	{
		if (!Tab4Texts[i]) { continue; }
		Tab4Texts[i]->SetAutoWrapText(false);
		Tab4Texts[i]->SetText(FText::FromString(TabCaption(i)));
	}
	// ★ СОСТОЯНИЕ «какая вкладка активна» — ОБЩАЯ ТОЧКА (закон `port-shared-points`): активной —
	//   контрастный цвет, остальным их базовый ИЗ АССЕТА (решение владельца 2026-08-27).
	//   ⚠ ДВЕ ПОЛОСЫ ОКНА В РАЗНЫХ СЕМЕЙСТВАХ, и обе обслуживает одна и та же точка: у 12005
	//   (`SKILL_CHARA_BTN1`) скин СВЕТЛЫЙ и базовый цвет тёмный `ff4c4c4c` → активная ЧЁРНАЯ;
	//   у 12006 (`SKILL_CHARA_BTN2`) скин ТЁМНЫЙ и базовый светлый `ffe2e2c7` → зеркальный случай:
	//   активная БЕЛАЯ, неактивные — тот же `ffe2e2c7`, приглушённый по яркости. Плана SS2 это
	//   не отменяет — эталонное выделение картинкой остаётся, подпись лишь перестаёт сливаться.
	//   ⚠ На дереве 4-й профессии (`CurTree == 3`) эталон снимает выделение у 12005 целиком
	//   (`SetSelectedIndex(m_pCharaBtn1, -1)`) — передаём −1, «активной нет».
	AsdaUi::ApplyTabCaptionState(Tab3Texts, CurTree == 3 ? -1 : CurTree, Tab3IdleColors);
	AsdaUi::ApplyTabCaptionState(Tab4Texts, CurTree, Tab4IdleColors);

	if (TxtTreeCaption)
	{
		// ★★★ ПОДСТАНОВКА [ss1]. Эталон (`skillWnd_msgProc@0xE52A10`, строки 32665/32676) делает
		//   `CObject_SetSlotString(g_pLocNoticeCtx, 0, подпись_выбранной_опции)` и только ПОТОМ
		//   `UI_FormatLocalizedNotice(..., 12062, ...)`. Нотис 12062 = «На эту характеристику затрачено
		//   [ss1] оч.», где [ss1] — строковый слот 0, то есть НАЗВАНИЕ ВЫБРАННОГО ДЕРЕВА.
		//   Порт раньше печатал сырую строку и приклеивал « : имя» — заглушка так и оставалась в тексте.
		//   ⚠ Подставляем в СЫРУЮ строку (LocRaw), а не через UAsdaTextSubsystem::FormatById: тот
		//   работает поверх GetTextById, который СРЕЗАЕТ inline-теги (грабля ministate-win-port).
		const FString Tree = TabCaption(CurTree);
		FString Line = LocRaw(kLocTreeCaption);
		Line.ReplaceInline(TEXT("[ss1]"), *Tree, ESearchCase::CaseSensitive);
		TxtTreeCaption->SetAutoWrapText(false);
		TxtTreeCaption->SetText(FText::FromString(Line));
	}
}

// Подписи опций селекторов вкладок. Импортёр свёл три (четыре) подписи в ОДИН TextBlock шириной во
//   весь селектор, поэтому создаём по TextBlock'у на опцию и кладём его РОВНО на прямоугольник
//   соответствующей кнопки (`SKILL_CHARA_BTN{1,2}_OPT{k}`) — так же, как эталон рисует caption
//   элемента списка в его собственном прямоугольнике. Шрифт и цвет копируем из сведённого текста.
void UAsdaSkillWindowSubsystem::BuildTabCaptions()
{
	if (!Win || !Win->WidgetTree) { return; }
	Tab3Texts.Reset(); Tab4Texts.Reset();
	// Подписи пересоздаются ⇒ кэш базовых цветов недействителен: общая точка снимет их заново.
	Tab3IdleColors.Reset(); Tab4IdleColors.Reset();

	auto Build = [this](const TArray<TObjectPtr<UButton>>& Btns, UTextBlock* Tpl,
	                    TArray<TObjectPtr<UTextBlock>>& Out, const TCHAR* Tag)
	{
		Out.SetNum(Btns.Num());
		if (!Tpl) { return; }
		UCanvasPanel* Parent = Cast<UCanvasPanel>(Tpl->GetParent());
		const UCanvasPanelSlot* TplSlot = AsdaUi::SlotOf(Tpl);
		if (!Parent || !TplSlot) { return; }
		// Сведённый текст больше не нужен — эталонных «одной строкой» подписей не бывает.
		Tpl->SetVisibility(ESlateVisibility::Collapsed);
		for (int32 k = 0; k < Btns.Num(); ++k)
		{
			UButton* B = Btns[k].Get();
			const UCanvasPanelSlot* Bs = B ? AsdaUi::SlotOf(B) : nullptr;
			if (!Bs) { continue; }
			UTextBlock* T = Win->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
				FName(*FString::Printf(TEXT("SkillTabCaption_%s_%d"), Tag, k)));
			T->SetFont(Tpl->GetFont());
			T->SetColorAndOpacity(Tpl->GetColorAndOpacity());
			T->SetJustification(ETextJustify::Center);
			// ★ Закон ui-autowrap-canvas-gotcha: в Canvas перенос выключаем.
			T->SetAutoWrapText(false);
			// Подпись НЕ должна перехватывать клик по кнопке (закон ui-button-hover-click-rule).
			T->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UCanvasPanelSlot* Cs = Parent->AddChildToCanvas(T))
			{
				const FMargin BOff = Bs->GetOffsets();
				const FMargin TOff = TplSlot->GetOffsets();
				// По X — прямоугольник кнопки, по Y — базовая линия сведённого текста (в JUI она на 3 px
				//   ниже плана кнопки, у обоих селекторов одинаково).
				Cs->SetAnchors(Bs->GetAnchors());
				Cs->SetAutoSize(false);
				Cs->SetOffsets(FMargin(BOff.Left, TOff.Top, BOff.Right, TOff.Bottom));
				Cs->SetZOrder(4);
			}
			Out[k] = T;
		}
	};
	Build(Tab3Buttons, Tab3TextTemplate.Get(), Tab3Texts, TEXT("BTN1"));
	Build(Tab4Buttons, Tab4TextTemplate.Get(), Tab4Texts, TEXT("BTN2"));
}

void UAsdaSkillWindowSubsystem::ApplyIcon(UImage* Img, const FString& TexName) const
{
	if (!Img) { return; }
	if (TexName.IsEmpty()) { Img->SetVisibility(ESlateVisibility::Collapsed); return; }
	// ★ Аудит №3 (B8): менеджер текстур достаётся общим инлайном `AsdaTexMgr()` — ресурс только через
	//   менеджер (кэш + refCount), закон `resource-subsystems`.
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();
	UTexture2D* Tex = TexMgr ? TexMgr->LoadTextureCached(AsdaAssetPaths::JuiTextureObject(TexName)) : nullptr;   // ★ волна 2 п. 7
	if (!Tex) { Img->SetVisibility(ESlateVisibility::Collapsed); return; }
	// Иконка навыка — ОТДЕЛЬНАЯ текстура (эталон `DrawTextureRect(tex, 0, 0, 32, 32)`).
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U4d): крой левого-верхнего 32×32 логических пикселей по эталону (`AsdaUi::ApplyJuiIconRegionTex`),
	//   было UV 0..1 у любой текстуры. Все иконки навыков в данных — 32×32 (перепись `asda.UI.WholeIconCensus`) ⇒ кисть прежняя.
	AsdaUi::ApplyJuiIconRegionTex(Img, Tex, 0, 0, 32, 32, IconDrawSize, IconDrawSize);
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

// Как ApplyIcon, но с произвольным размером отрисовки: эталон для портрета хранителя зовёт
//   `CControl_DrawTextureRect(ctrl, tex, 0, 0, 126, 190)` — та же «текстура целиком», другой прямоугольник.
void UAsdaSkillWindowSubsystem::ApplyPanelTexture(UImage* Img, const FString& TexName, const FVector2D& DrawSize) const
{
	if (!Img) { return; }
	if (TexName.IsEmpty()) { Img->SetVisibility(ESlateVisibility::Collapsed); return; }
	UAsdaTextureSubsystem* TexMgr = AsdaTexMgr();   // ★ Аудит №3 (B8): та же единая точка доступа
	UTexture2D* Tex = TexMgr ? TexMgr->LoadUITexture(FName(*TexName)) : nullptr;
	if (!Tex)
	{
		Img->SetVisibility(ESlateVisibility::Collapsed);
		UE_LOG(LogAsdaSkillWin, Verbose, TEXT("[навыки] текстура панели «%s» не найдена в /Game/AsdaAssets/Textures/JUI"), *TexName);
		return;
	}
	// ★ ИСПРАВЛЕНИЕ (дедуп №2, В2-B, U4d): крой `DrawSize` логических пикселей от левого-верхнего угла по эталону
	//   (`DrawTextureRect(ctrl, tex, 0, 0, 126, 190)`), было UV 0..1. Портреты хранителей `im_sb_*` — ровно 126×190 ⇒ кисть прежняя.
	AsdaUi::ApplyJuiIconRegionTex(Img, Tex, 0, 0, FMath::RoundToInt32(DrawSize.X), FMath::RoundToInt32(DrawSize.Y),
		(float)DrawSize.X, (float)DrawSize.Y);
	Img->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UAsdaSkillWindowSubsystem::RebuildGrid()
{
	if (!Db) { return; }
	const int32 Band = GetSelfBand();

	// 1) Полная очистка 45 узлов (эталон `uiRefreshSkillList` начинает ровно с этого).
	for (int32 SlotI = 0; SlotI < Slots.Num(); ++SlotI)
	{
		FAsdaSkillSlotWidgets& S = Slots[SlotI];
		S.SkillCode = -1;
		if (S.Frame)      { S.Frame->SetVisibility(ESlateVisibility::Collapsed); }
		if (S.Icon)       { S.Icon->SetVisibility(ESlateVisibility::Collapsed); }
		if (S.Level)      { S.Level->SetText(FText::GetEmpty()); S.Level->SetVisibility(ESlateVisibility::Collapsed); }
		if (S.Selectable) { S.Selectable->SetVisibility(ESlateVisibility::Collapsed); }
		if (S.Selected)   { S.Selected->SetVisibility(ESlateVisibility::Collapsed); }
		// ⚠ Шторку отката (`S.Cool`) здесь НЕ трогаем: ею владеет покадровый `TickCooldownCurtains`.
		//   Гасить её тут — значит каждый кадр (а `RebuildGrid` при живом откате зовётся каждый кадр)
		//   сбивать прогон кадров в самое начало. Узел опустел ⇒ ключ шторки сменится ⇒ она погаснет
		//   сама на ближайшем тике. Ровно тот же приём, что у вспышки CED с активным таймером ниже.
		if (S.CoolText)   { S.CoolText->SetText(FText::GetEmpty()); S.CoolText->SetVisibility(ESlateVisibility::Collapsed); }
		// Активную вспышку окончания кулдауна не трогаем — её гасит собственный таймер.
		if (S.Ced && !CedTimers.Contains(SlotI)) { S.Ced->SetVisibility(ESlateVisibility::Collapsed); }
		for (const TObjectPtr<UImage>& I : S.CoinIcons)     { if (I) { I->SetVisibility(ESlateVisibility::Collapsed); } }
		for (const TObjectPtr<UTextBlock>& T : S.CoinTexts) { if (T) { T->SetVisibility(ESlateVisibility::Collapsed); } }
	}

	// 2) Разложить навыки дерева по узлам.
	TArray<int32> Codes;
	Db->GetTreeSkills(Band, CurTree, Codes);
	int32 SpentInTree = 0;
	int32 Placed = 0;
	for (int32 Code : Codes)
	{
		const FAsdaSkillDef* D = Db->Get(Code);
		if (!D) { continue; }
		const int32 Slot = D->AbsSlot() - GridColumns * ScrollRow;
		const FAsdaLearnedSkill* L = Learned.Find(Code);
		const int32 Level = L ? L->Level : 0;
		if (Level > 0) { SpentInTree += Level; }
		if (Slot < 0 || Slot >= Slots.Num()) { continue; }

		FAsdaSkillSlotWidgets& S = Slots[Slot];
		if (!S.Group) { continue; }
		S.SkillCode = Code;
		++Placed;

		if (S.Frame) { S.Frame->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		// Иконка: выучен — обычная, не выучен — тот же файл с суффиксом «-g» (эталон Skill_BuildTreeUI).
		ApplyIcon(S.Icon, (Level > 0) ? D->IconName : (D->IconName.IsEmpty() ? FString() : D->IconName + TEXT("-g")));

		if (S.Level)
		{
			S.Level->SetAutoWrapText(false);
			if (Level > 0)
			{
				S.Level->SetText(FText::FromString(FString::FromInt(Level)));   // U10e (В2-A, 11.10.2026): `sprintf("%d")` — Skill_BuildTreeUI@0xE532E0, skill_buff.c:32898
				S.Level->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
		// Оверлей «доступен для изучения». ★ Эталон `Skill_BuildTreeUI`: в ветке «уровень <= 0» —
		//   показать, если `availableFlag != 0` И `CCharacter+0x43B[8] > 0`; в ветке «уровень > 0» —
		//   ЯВНО `SetVisible(0)`. Проверки `maxLevelOrTier` здесь НЕТ (она только в клике по узлу),
		//   а порт её ставил вместо условия «ещё не выучен» — подсветка горела на выученных.
		// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (`asda.Skill.Job4Pools`): «есть очки» — в ПУЛЕ навыка (основной / 4-я профессия), не в общем счёте;
		//   при ключе 0 `PointsLeftForSkill` = общий счёт (прежнее).
		const bool bCanLearn = (Level == 0) && (SkillPoints > 0) && (PointsLeftForSkill(Code) > 0) && (L ? L->bAvailable : false);
		if (S.Selectable && bCanLearn) { S.Selectable->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
		if (S.Selected && Slot == SelectedSlot) { S.Selected->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }

		// Остаток отката цифрой под узлом. Сама ШТОРКА (`S.Cool`) — на покадровом `TickCooldownCurtains`.
		if (const FSkillCooldown* Cd = Cooldowns.Find(Code))
		{
			if (S.CoolText)
			{
				S.CoolText->SetAutoWrapText(false);
				S.CoolText->SetText(FText::AsNumber(FMath::CeilToInt(Cd->RemainSec)));
				S.CoolText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}

		// Цена в монетах. ★ Эталон (`uiRefreshSkillList@0xE54420`) на КАЖДОМ занятом узле показывает
		//   контейнер целиком — три шарика и три числа, — а числа берёт из `SkillCoinTable.bin`
		//   (rec[1..3]); при отсутствии записи в них остаётся «0» из сброса. Никакого условия «> 0»
		//   в эталоне нет, поэтому и здесь монеты показываются всегда, когда узел занят.
		const int32 Coins[3] = { D->Coin0, D->Coin1, D->Coin2 };
		for (int32 i = 0; i < 3; ++i)
		{
			if (S.CoinIcons.IsValidIndex(i) && S.CoinIcons[i]) { S.CoinIcons[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible); }
			if (S.CoinTexts.IsValidIndex(i) && S.CoinTexts[i])
			{
				S.CoinTexts[i]->SetText(FText::FromString(FString::FromInt(FMath::Max(Coins[i], 0))));   // U10e (В2-A, 11.10.2026): `sprintf("%d")` — uiRefreshSkillList@0xE54420, ui_core.c:72201-72205
				S.CoinTexts[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
	}

	if (TxtTreeSpent)
	{
		TxtTreeSpent->SetAutoWrapText(false);
		TxtTreeSpent->SetText(FText::FromString(FString::FromInt(SpentInTree)));   // U10e (В2-A, 11.10.2026): `sprintf("%d", totalPts)` — skill_buff.c:32908
	}
	UE_LOG(LogAsdaSkillWin, Verbose, TEXT("[навыки] сетка: вкладка %d, прокрутка %d, узлов %d/%d, вложено %d"),
		CurTree, ScrollRow, Placed, Codes.Num(), SpentInTree);
}

// ═══════════ ★★★ ШТОРКА ОТКАТА НА УЗЛАХ ДЕРЕВА ═══════════════════════════════════════════════
//
// ЧТО ЗДЕСЬ ОРИГИНАЛЬНОЕ, А ЧТО НАШЕ (честное разделение, закон `false-negatives-method`).
//   Прежняя формулировка отчёта `item_cooldown_premium.md` §2 — «шторка есть ТОЛЬКО на панели» —
//   для ДЕРЕВА НАВЫКОВ НЕВЕРНА, и это снято вторым методом (по телам, а не по имени контрола):
//     • `skillAddAndRefreshBars@0xE49A20` (`skill_buff.c:30214-30244`) — «навык ушёл в откат»:
//           v2 = skillCalcUpgradeSpCost(this, skillId);                 // = номер узла сетки
//           if (v2 <= 0x2C) {
//               uiControl_ShowWithItem((int*)v4[17*v2 + 151], 120, 127, 8, 1000, 0);
//               CControl_SetVisible((CControl*)v4[17*v2 + 151], 1);
//           }
//       где `v4[17·v2 + 151]` = `m_aSkillSlot[68·v2 + 12]` = контрол `SKILLTREE_COOL{NN}`
//       (скин `CoolTime_Effect`, тот же, что у `QUICK_NEW_COOLn`; idnum 12013/12019/…).
//     • `skillRemoveAndRefreshBars@0xE49BB0` (`skill_buff.c:30259`) гасит его: `CControl_CloseHide`.
//   То есть САМ ПОКАЗ шторки на узле — эталонный. Аргументы `(120, 127, 8, 1000, animate=0)` значат
//   «ЗАМОРОЖЕННЫЙ кадр 120», то есть оригинал просто затемняет узел на время отката, без обратного
//   отсчёта.
//
// НАШЕ ОСОЗНАННОЕ ОТСТУПЛЕНИЕ (решение юзера 2026-08-14, ключ `asda.Cooldown.ShowEverywhere`):
//   при 1 (умолчание) вместо замороженного кадра гоним ЖИВОЙ ПРОГОН 0…117, синхронный с остатком, —
//   ровно та же механика, что на панели быстрого доступа; при 0 возвращается эталонный кадр 120.
//
// ИСТОЧНИК ДАННЫХ — один на всё окно: `UAsdaHotbarSubsystem::QuerySkillCooldown`. Реестр откатов
//   навыков физически живёт здесь (как вектор `CSkillWnd+56` эталона) и отдаётся панели мостом,
//   поэтому запрос уходит в панель и возвращается сюда же через мост — зато вход в реестр ОДИН для
//   всех трёх окон (закон `port-shared-points`), и «своего» счётчика ни у кого нет.
void UAsdaSkillWindowSubsystem::TickCooldownCurtains()
{
	UGameInstance* GI = GetGameInstance();
	UAsdaHotbarSubsystem* Hb = GI ? GI->GetSubsystem<UAsdaHotbarSubsystem>() : nullptr;
	if (!Hb) { return; }
	const double Now = AsdaCooldownNowS(GI);   // ★ 25.10.2026: часы реестра откатов (EndS шторки сравнивается в одной шкале)
	const bool bLive = UAsdaHotbarSubsystem::IsCooldownShownEverywhere();

	for (FAsdaSkillSlotWidgets& S : Slots)
	{
		// План шторки: родной контрол узла; если импортёр по какой-то причине сделал из него не
		//   кадровый план — собираем свой по геометрии иконки узла (визуал берётся с плана панели).
		if (!S.Curtain.Plane && S.Icon)
		{
			S.Curtain.Plane = Hb->MakeCurtainOverCell(Win, S.Icon, TEXT("SKILLTREE_CoolTime"));
		}
		if (!S.Curtain.Plane) { continue; }

		FAsdaHotbarCooldown CD;
		if (S.SkillCode > 0) { CD = Hb->QuerySkillCooldown(S.SkillCode); }
		if (!bLive && CD.State == 1)
		{
			// Эталон: `uiControl_ShowWithItem(ctrl, 120, 127, 8, 1000, animate=0)` — состояние 2
			//   (замороженный кадр), стартовый кадр 120. Остаток при этом на кадр не влияет.
			CD.State = 2;
			CD.IconFrame = 120;
			CD.RemainMs = 0.f;
		}
		Hb->DriveCurtain(S.Curtain, S.SkillCode, CD, Now);
	}
}

// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 — ДВА ПУЛА ОЧКОВ (шапка ключа `asda.Skill.Job4Pools`). Формулы — эталонные, но с потолком «не больше
//   свободных» (у сервера счёт один): основной = min(свободные, 110 − основные) при уровне ≥ 60, ниже — свободные; 4-я профессия =
//   min(свободные, основные + свободные − 110) при уровне ≥ 60, ниже — 0. Отрицательное число — долг старых данных (до P110 сервер
//   пропускал вложение сверх пула), его не прячем.
UAsdaSkillWindowSubsystem::FAsdaSkillPointPools UAsdaSkillWindowSubsystem::ComputePointPools() const
{
	FAsdaSkillPointPools P;
	P.bOn = CVarSkillJob4Pools.GetValueOnGameThread() != 0;
	P.Free = FMath::Max(SkillPoints, 0);
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	P.Level = AsdaSelf::GetLevel(Net);
	if (Db)
	{
		const int32 Band = GetSelfBand();
		for (int32 T = 0; T < 4; ++T)
		{
			TArray<int32> Codes;
			Db->GetTreeSkills(Band, T, Codes);
			int32 Sum = 0;
			for (int32 C : Codes) { if (const FAsdaLearnedSkill* L = Learned.Find(C)) { Sum += L->Level; } }
			if (T == 3) { P.Job4Spent += Sum; } else { P.MainSpent += Sum; }
		}
	}
	if (P.Level >= kJob4PoolMinLevel)
	{
		P.MainShown = FMath::Min(P.Free, kMainTreePointCap - P.MainSpent);
		P.Job4Shown = FMath::Min(P.Free, P.MainSpent + P.Free - kMainTreePointCap);
	}
	else
	{
		P.MainShown = P.Free;
		P.Job4Shown = 0;
	}
	return P;
}

bool UAsdaSkillWindowSubsystem::IsJob4TreeSkill(int32 SkillCode) const
{
	const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
	return D && UAsdaSkillDatabase::TreeFromGrade(D->Grade) == 3;
}

int32 UAsdaSkillWindowSubsystem::PointsLeftForSkill(int32 SkillCode) const
{
	const FAsdaSkillPointPools P = ComputePointPools();
	if (!P.bOn) { return P.Free; }                       // ключ 0 — прежний общий счёт
	const FAsdaSkillDef* D = Db ? Db->Get(SkillCode) : nullptr;
	if (!D || D->Grade < 1 || D->Grade > 12) { return P.Free; }   // навык вне деревьев 1..12 — пулов у него нет (как у сервера: дерево 0)
	return FMath::Max(0, IsJob4TreeSkill(SkillCode) ? P.Job4Shown : P.MainShown);
}

void UAsdaSkillWindowSubsystem::RefreshPointTexts()
{
	// Сумма вложенного по каждому из 4 деревьев — эталонный массив `gap_68[4*tree+4]`, который
	//   заполняет `Skill_BuildTreeUI`. Нужен обеим формулам ниже.
	int32 SpentByTree[4] = { 0, 0, 0, 0 };
	if (Db)
	{
		const int32 Band = GetSelfBand();
		for (int32 T = 0; T < 4; ++T)
		{
			TArray<int32> Codes;
			Db->GetTreeSkills(Band, T, Codes);
			for (int32 C : Codes) { if (const FAsdaLearnedSkill* L = Learned.Find(C)) { SpentByTree[T] += L->Level; } }
		}
	}
	UAsdaNetSubsystem* Net = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaNetSubsystem>() : nullptr;
	const int32 Level = AsdaSelf::GetLevel(Net);   // ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U5c): уровень — общая точка `AsdaSelf::GetLevel` (до 4005/4036 — уровень выбранного персонажа, а не 0)

	if (TxtSkillPoint)
	{
		TxtSkillPoint->SetAutoWrapText(false);
		if (SkillPoints < 0)
		{
			// Такого быть не должно: число приходит блоком char-select 1017 (+39) ещё до входа в мир.
			TxtSkillPoint->SetText(FText::GetEmpty());
			UE_LOG(LogAsdaSkillWin, Warning,
				TEXT("[навыки] счётчик свободных очков пуст: 1017 (блок+39 = CCharacter+0x443) не приходил, ")
				TEXT("ответов 5254/5462/5464 тоже не было — смотри строку лога «1017 блок: очки умений=»."));
		}
		else
		{
			int32 Show = SkillPoints;
			const FAsdaSkillPointPools Pools = ComputePointPools();
			if (Pools.bOn)
			{
				// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (`asda.Skill.Job4Pools`): остаток ОСНОВНОГО пула (шапка `ComputePointPools`).
				Show = Pools.MainShown;
			}
			else if (CVarSkillLegacyPointFormula.GetValueOnGameThread() != 0 && Level >= 60)
			{
				// ★ Воспроизведение ДЕФЕКТА оригинала: «110 − сумма вложенного в деревья 0..2»
				//   (`skillUiUpdatePointText@0xE56720`: 110 − (gap_68[12]+gap_68[8]+gap_68[4])).
				Show = 110 - (SpentByTree[0] + SpentByTree[1] + SpentByTree[2]);
			}
			// ★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-A, U10e): было `FText::AsNumber` (разделитель разрядов по культуре: EN «1,234», RU «1 234») — стало `FString::FromInt`, как `sprintf("%d")` эталона (`skillUiUpdatePointText@0xE56720`, skill_buff.c:33625/33629).
			TxtSkillPoint->SetText(FText::FromString(FString::FromInt(Show)));
		}
	}

	if (TxtJob4Point)
	{
		// ★ Счётчик очков 4-й профессии (12413) считается ОТДЕЛЬНОЙ формулой эталона
		//   (`Skill_BuildTreeUI@0xE532E0`, хвост): при уровне < 60 — «0», иначе
		//   (сумма вложенного по 4 деревьям + свободные) − 110 − вложенное в дерево 4-й профессии.
		//   Раньше порт дублировал сюда число свободных очков — это было неверно.
		int32 Job4 = 0;
		if (Level >= 60)
		{
			const int32 Sum = SpentByTree[0] + SpentByTree[1] + SpentByTree[2] + SpentByTree[3]
			                + FMath::Max(SkillPoints, 0);
			Job4 = Sum - 110 - SpentByTree[3];
		}
		const FAsdaSkillPointPools Pools = ComputePointPools();
		if (Pools.bOn)
		{
			// ★ [ИСПРАВЛЕНИЕ] 05.11.2026 (`asda.Skill.Job4Pools`): та же формула эталона, но не больше свободных очков сервера
			//   (иначе при выученном сверх лимита основном пуле счётчик обещал очки, которых у сервера нет).
			Job4 = Pools.Job4Shown;
		}
		TxtJob4Point->SetAutoWrapText(false);
		TxtJob4Point->SetText(FText::FromString(FString::FromInt(Job4)));   // U10e (В2-A, 11.10.2026): `sprintf("%d")` — Skill_BuildTreeUI, skill_buff.c:32913/32920
	}
}

// ══════════════════════════════ ЛЕВАЯ ПАНЕЛЬ «ХРАНИТЕЛЬ ДУШИ» (56300) ══════════════════════════
//
// ★★★ ЭТО НЕ ПРЕВЬЮ ВЫБРАННОГО НАВЫКА. Эталон `skillPanelRefreshNextLevelSlots@0xE587C0` берёт
//   ДВЕ записи из `Data/SoulGuarderTable.bin` (шаг 1956) поиском `findRecordByTwoKeys@0xEF76B0`
//   по ключам (+4 SkillType, +8 RowIndex):
//     SkillType = 3*(полоса−1) + вкладка + 1      (эталонное `this[98] + 1`)
//     RowIndex  = (прокрутка >= 2 ? 1 : 0) + k,  k = 0,1
//   и рисует: +1892 портрет 126x190, +16 имя, иконку навыка +152 (32x32, имя файла — Skilltooltip +36),
//   плюс показывает плашку ступени 56305+RowIndex («1ST»/«2ND»/«3RD»).
//   Раньше порт клал сюда иконку навыка под курсором — отсюда «дублированные иконки вместо хранителя».
void UAsdaSkillWindowSubsystem::RefreshPreviewPanel()
{
	if (PreviewTitle) { PreviewTitle->SetAutoWrapText(false); PreviewTitle->SetText(FText::FromString(LocRaw(kLocSoulPanelTitle))); }

	const int32 Band = GetSelfBand();
	// Эталонный индекс дерева внутри полосы: у вкладки 4-й профессии это ТРЕТЬЕ дерево полосы (v5 = 2/5/8).
	const int32 TreeForType = (CurTree == 3) ? 2 : CurTree;
	const int32 SkillType   = (Band >= 1) ? (3 * (Band - 1) + TreeForType + 1) : 0;
	const int32 FirstRow    = (ScrollRow >= 2) ? 1 : 0;

	const UAsdaSoulGuardSubsystem* Soul = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAsdaSoulGuardSubsystem>() : nullptr;

	for (int32 k = 0; k < SoulRows.Num(); ++k)
	{
		FAsdaSoulPreviewRow& R = SoulRows[k];
		// 1) Сброс строки — эталон делает его безусловно в начале.
		R.GuardIndex = -1; R.GuardianSkillId = -1; R.BuffSkillId = -1; R.Tier = -1;
		if (R.Illust)   { R.Illust->SetVisibility(ESlateVisibility::Collapsed); }
		if (R.NextIcon) { R.NextIcon->SetVisibility(ESlateVisibility::Collapsed); }
		if (R.Name)     { R.Name->SetAutoWrapText(false); R.Name->SetText(FText::GetEmpty()); }
		for (const TObjectPtr<UWidget>& Lb : R.Tiers) { if (Lb) { Lb->SetVisibility(ESlateVisibility::Collapsed); } }

		const FAsdaSoulGuarderRow* Rec = (Soul && SkillType > 0) ? Soul->FindGuarderByTypeRow(SkillType, FirstRow + k) : nullptr;
		if (!Rec) { continue; }

		R.GuardIndex      = Rec->SoulGuardIndex;
		R.GuardianSkillId = Rec->GuardianSkillId;
		R.BuffSkillId     = Rec->BuffSkillId;
		R.Tier            = Rec->RowIndex;

		// Портрет: эталон `CControl_DrawTextureRect(ctrl, tex, 0, 0, 126, 190)` — текстура целиком.
		if (R.Illust) { ApplyPanelTexture(R.Illust, Rec->Portrait, FVector2D(126.f, 190.f)); }
		if (R.Name)   { R.Name->SetText(FText::FromString(Rec->Name)); }
		// Иконка навыка хранителя: имя файла из Skilltooltip.BIN +36 по коду +152.
		if (R.NextIcon)
		{
			const FAsdaSkillDef* GD = Db ? Db->Get(Rec->GuardianSkillId) : nullptr;
			ApplyIcon(R.NextIcon, GD ? GD->IconName : FString());
		}
		if (R.Tiers.IsValidIndex(Rec->RowIndex) && R.Tiers[Rec->RowIndex])
		{
			R.Tiers[Rec->RowIndex]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}
}

// Подложка выбранного дерева (эталон `skillPanel_selectTreeByClass@0xE581A0`): видим РОВНО один план
//   из 15, его Y едет вверх на 53 px за ряд прокрутки (`... − 53 * m_nSelectedOptionIdx`).
void UAsdaSkillWindowSubsystem::RefreshTreeArt()
{
	const int32 Band = GetSelfBand();
	if (Band < 1 || Band > 3) { return; }
	// Эталонное `v5`: полоса 1/2/3 → база 0/3/6, плюс индекс вкладки. Вкладка 4-й профессии (3) в
	//   эталоне берёт ОТДЕЛЬНЫЙ план из хвоста таблицы; в срезе его порядок не восстановлен, а
	//   кандидаты 12263..12268 в JUI пусты — поэтому под ней подложку просто не показываем.
	const int32 Idx = (CurTree == 3) ? -1 : (3 * (Band - 1) + CurTree);

	for (int32 i = 0; i < TreeArt.Num(); ++i)
	{
		UWidget* W = TreeArt[i].Get();
		if (!W) { continue; }
		if (i != Idx) { SetSubtreeVisible(W, false); continue; }
		SetSubtreeVisible(W, true);
		if (UCanvasPanelSlot* Cs = AsdaUi::SlotOf(W))
		{
			const FMargin Off = Cs->GetOffsets();
			Cs->SetOffsets(FMargin(Off.Left, -static_cast<float>(TreeRowStep * ScrollRow), Off.Right, Off.Bottom));
		}
	}
}

// Показ/скрытие всего поддерева плана-подложки.
//   ★ ЗАЧЕМ РЕКУРСИЯ: в JUI сам контрол `SKILLTREE_ARR_*` объявлен скрытым (vis=0), а ВСЕ его
//   потомки-стрелки — видимыми; импортёр же свернул и созданные им union-группы, и часть листьев.
//   Поэтому «показать» = пройти поддерево.
//   ★ ИСКЛЮЧЕНИЯ: шесть сегментов в JUI помечены vis=0 самими художниками — их держим скрытыми.
//   Имена импортёра несут JUI-индекс (`{idc}_{juiIdx}_{план}`), по нему и отбираем:
//     450 (AB1), 523 «sin» и 525 «sin2» (WB1), 557 «sin», 559 «sin1», 560 «sin2» (WB2).
// ══════════════════════════════ ПОЛОСА ПРОКРУТКИ СЕТКИ (12291 → CScrollBar «Nomal_Scr») ════════
void UAsdaSkillWindowSubsystem::BuildScrollBar()
{
	if (ScrollBar.IsBuilt() || !WheelPanel || !Win) { return; }
	// ★ Полоса — СНАРУЖИ колёсного окна: её X = правый край окна. Само окно обрезано по ClipToBounds
	//   (обрезка лежит в ассете), поэтому вешаем планы в РОДИТЕЛЬСКУЮ канву, а не в само окно.
	UCanvasPanel* Host = Cast<UCanvasPanel>(WheelPanel->GetParent());
	const UCanvasPanelSlot* WheelSlot = AsdaUi::SlotOf(WheelPanel);
	if (!Host || !WheelSlot) { return; }
	ScrollHost = Host;
	// ★ Аудит №3 (A1 + A2): планы, их кисти и порядок z собирает ОБЩАЯ полоса — те же имена узлов
	//   (`SKILL_WHEELWIN_Scr_*`) и тот же z (дорожка 20 → бегунок 21 → стрелки 22).
	//   Атлас она берёт ТОЛЬКО у `UAsdaTextureSubsystem` (закон `resource-subsystems`): прежний
	//   фолбэк прямым `LoadObject<UTexture2D>` обходил кэш и refCount менеджера и здесь снят.
	//   Менеджер текстуру не отдал ⇒ полоса не строится и «своим» цветом не рисуется (`no-invented-ui`) —
	//   предупреждение печатает сама полоса.
	ScrollBar.Build(Win, Host, TEXT("SKILL_WHEELWIN_Scr"), /*ZTrack*/ 20);
}

// Где полоса стоит и какой у неё диапазон рядов — это модель ОКНА, поэтому осталось здесь.
void UAsdaSkillWindowSubsystem::LayoutScrollBar()
{
	if (!ScrollBar.IsBuilt()) { return; }
	const int32 MaxRow = MaxScrollRow();
	// Начало полосы = (правый край, верх) прямоугольника колёсного окна В КООРДИНАТАХ РОДИТЕЛЯ.
	//   Слот окна в ассете равен ровно его контрольному прямоугольнику (144,8,400,477).
	float X = AsdaSkillWheel::kWheelX + AsdaSkillWheel::kWheelW;
	float Y = AsdaSkillWheel::kWheelY;
	if (const UCanvasPanelSlot* Ws = WheelPanel ? AsdaUi::SlotOf(WheelPanel) : nullptr)
	{
		const FMargin O = Ws->GetOffsets();
		X = O.Left + (float)WheelRectSize.X;
		Y = O.Top;
	}
	// Доля видимого у СПИСКА рядов = одна позиция из (MaxRow + 1) — это и есть прежняя формула
	//   длины бегунка `TrackLen / (MaxRow + 1)` (`uiScrollbarSetRange`), только теперь общая.
	//   Окно не масштабируется ⇒ множитель авторских размеров 1.
	//   ★ Волна 14 (МЕЛК-1): целыми аргументами эталона `(1, MaxRow + 1)` — длину считает `EtalonThumbLen`.
	ScrollBar.Layout(FVector2D(X, Y), (float)WheelRectSize.Y, /*Scale*/ 1.f, 1, MaxRow + 1, MaxRow, ScrollRow);
}

bool UAsdaSkillWindowSubsystem::IsCursorOverScrollBar() const
{
	return ScrollBar.IsCursorOver();
}

// Ввод полосы — ветки `uiScrollbarOnButtonEvent@0x12FF2C0` / `uiScrollbarOnMouseDrag@0x12FF020`
//   (живут в общей полосе). Шаг стрелки и шаг «страницы» ОБА равны 1 ряду:
//   `uiControlSetScrollPage(12291, 1, 1)`, авто-повтора при удержании в эталоне нет.
//   Окну остаётся ПРИМЕНИТЬ действие своей моделью прокрутки.
void UAsdaSkillWindowSubsystem::TickScrollBar(bool bLmb, bool bLmbDownEdge)
{
	if (!ScrollBar.IsBuilt())
	{
		ScrollBar.bThumbGrab = false;
		ScrollBar.PressedPlane = -1;
		return;
	}
	int32 Value = 0;
	switch (ScrollBar.Tick(bLmb, bLmbDownEdge, MaxScrollRow(), Value))
	{
	case EAsdaScrollAction::ScrollBy: SetScrollRow(ScrollRow + Value); break;  // стрелка / «страница» = 1 ряд
	case EAsdaScrollAction::SetRow:   SetScrollRow(Value);             break;  // перетаскивание бегунка
	default: break;
	}
}

void UAsdaSkillWindowSubsystem::SetSubtreeVisible(UWidget* Root, bool bVisible)
{
	if (!Root) { return; }
	bool bShow = bVisible;
	if (bShow)
	{
		//   450 (AB1), 525 «sin2», 559 «sin1», 560 «sin2» — vis=0 в самом JUI;
		//   517 и 527 («goo», IDNum 12293/12295) — в JUI vis=1, но цикл `CSkillWindow_InitControls`
		//   ПРИНУДИТЕЛЬНО ставит 12293→0, 12294→1, 12295→0, 12296→1. Итог: 523 и 557 («sin»,
		//   12294/12296) наоборот ПОКАЗЫВАЮТСЯ, хотя в JUI скрыты.
		static const int32 kHiddenJuiIdx[6] = { 450, 517, 525, 527, 559, 560 };
		TArray<FString> Tok;
		Root->GetName().ParseIntoArray(Tok, TEXT("_"), true);
		if (Tok.Num() >= 2 && Tok.Last().IsNumeric() && Tok[Tok.Num() - 2].IsNumeric())
		{
			const int32 JuiIdx = FCString::Atoi(*Tok[Tok.Num() - 2]);
			for (int32 H : kHiddenJuiIdx) { if (H == JuiIdx) { bShow = false; break; } }
		}
	}
	Root->SetVisibility(bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (UPanelWidget* P = Cast<UPanelWidget>(Root))
	{
		// Скрытый сегмент — это ЛИСТ; его потомков (если бы были) эталон тоже не рисует.
		for (int32 i = 0; i < P->GetChildrenCount(); ++i) { SetSubtreeVisible(P->GetChildAt(i), bVisible && bShow); }
	}
}
