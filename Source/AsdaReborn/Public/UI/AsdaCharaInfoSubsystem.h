// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCharaInfoSubsystem.h — ОКНО ХАРАКТЕРИСТИК ПЕРСОНАЖА `CHARAINFO_WIN` (JUI IDNum 900)
//   + вкладка ЭМОЦИЙ (`CHARAINFO_IMOTIONBACK`, IDNum 1100), которая живёт в том же окне.
//
// ПОЛНЫЙ РЕВЕРС ПЕРЕД КОДОМ: `Reports/char_info_window.md` — там состав окна (241 контрол,
//   список «живое/мёртвое»), раскладка обоих серверных стат-блоков, все пути открытия, эмоции
//   и 12 дефектов оригинала. Ниже — только то, что нужно читателю кода.
//
// ЧТО ЭТО ЗА ОКНО
//   Две главные вкладки (радио-контрол `CHARAINFO_3BUT`, IDNum 902):
//     • 0 — «Статус» (`CHARAINFO_STBACK`, 903): три под-страницы, переключатель `CHARAINFO_STMENU`
//           (981) — 982 «Информация», 983 «Параметры», 984 «Характеристики»;
//     • 1 — «Эмоции» (`CHARAINFO_IMOTIONBACK`, 1100): три лотка иконок — обычные (12), отряд (6),
//           навыки (12).
//
// ТРИ КЛАССА ЭТАЛОНА, КОТОРЫЕ ЗДЕСЬ СВЕДЕНЫ В ОДНУ ПОДСИСТЕМУ (окно-то одно):
//   `CCharaInfoWin_InitControls@0xE75E70` (каркас) + `CUICharInfo_InitControls@0xB91E60`
//   (страница статов) + `CursorMotion_DebugName@0xC51D50` (страница эмоций, он же
//   `CEmotionWnd_InitControls`).
//
// ГЛАВНЫЕ ПРАВИЛА ЭТАЛОНА, ВОСПРОИЗВЕДЁННЫЕ 1:1
//   • Открытие `uiWindow_OpenIfAllowed@0xE76240`: шесть гейтов (инвентарь заблокирован /
//     парикмахерская / магазин NPC / склад / окно тайла / диалог количества), отказ при открытой
//     ЛИЧНОЙ ЛАВКЕ с нотисом 107022, затем пакет 5466, показ окна, подсветка кнопки 73401,
//     звук `S_e_button06`.
//   • Закрытие `uiWindowClose@0xE76460`: погасить свою подсказку, скрыть, снять подсветку,
//     звук `S_e_button07`.
//   • Тумблер `uiWindowToggleTab@0xE76160`: тот же таб → закрыть; другой таб → переключить;
//     закрыто → сначала переключить таб, потом открывать.
//   • Числа страницы «Статус» — `UI_FillEquipDetailPanel@0xB93A60`: одна точка, зовётся на каждый
//     стат-пакет (4060/4064/4036), надевание вещи и изучение умения.
//   • Полосы ОЗ/ОМ/опыта/веса эталон обновляет ОБЩИМИ функциями HUD (`uiBar_SetCurMax_Slot33/34/36`,
//     `uiBar_SetPercent_Slot35`) — второго источника значений НЕТ. В порту это значит: подписываемся
//     на ТЕ ЖЕ делегаты `UAsdaNetSubsystem`, что и мини-статус, и ничего своего не считаем.
//
// ЧЕГО ЭТО ОКНО НЕ ДЕЛАЕТ (чтобы не искать)
//   • Не распределяет очки характеристик: механики «вложить очко в Силу» в оригинале НЕТ ни в
//     окне, ни в пакетах — базовые статы приходят готовыми (4064), кнопок «+» в разметке нет.
//     Очки умений вкладывает окно навыков (12000), это другая подсистема.
//   • Не показывает 3D-превью персонажа: портрета/модели в окне 900 нет ни одного контрола.
//   • Не считает производные характеристики: ВСЁ, включая атаку, защиту, блок и стихии, приходит
//     числами с сервера. Клиент только форматирует.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "UI/AsdaBarSmoother.h"   // ★ общий сглаживатель полос здоровья/маны
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaMiniStateSubsystem.h"   // FAsdaMiniBarFill — ОДНО определение клипуемой полосы на порт
#include "UI/AsdaTipPanel.h"             // EAsdaTipOwner — владелец ОБЩЕЙ плашки-подсказки
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "Styling/SlateTypes.h"          // FTextBlockStyle — базовый стиль строки, снятый из JUI
#include "Framework/Text/TextLayout.h"   // ETextJustify — выравнивание из `EXS TextAlign`
#include "UI/AsdaMarkup.h"               // FAsdaFmtText — строка с тегами <FC=…> (общая точка разметки)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaCharaInfoSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;
class UButton;
class UCanvasPanel;
class UAsdaButtonRelay;
class AAsdaPlayerController;

// Один слот лотка эмоций (иконка 32×32 + подпись-заглушка + подложка).
USTRUCT()
struct FAsdaEmotionSlotUi
{
	GENERATED_BODY()

	UPROPERTY(Transient) TObjectPtr<UImage>     Icon = nullptr;   // `IMOTION_NOMAL00_181_0` и т. п.
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label = nullptr;  // `IMOTION_NOMAL00` (эталон текст не пишет)
	UPROPERTY(Transient) TObjectPtr<UImage>     Base = nullptr;   // `IMOTION_TRAY00_00_168_0` — рамка слота

	int32 Code = -1;      // код эмоции для сети (группа·100 + индекс — см. `EmotionCode`)
	int32 Group = -1;     // 0 обычные, 1 отряд, 2 навыки
	int32 IndexInGroup = -1;
};

// `FAsdaFmtText` — строка окна с тегами `<FC=…>` — живёт в общей точке разметки `UI/AsdaMarkup.h`
//   (перенесена 25.09.2026, её используют окна 900, 20000 и 50300).

UCLASS()
class ASDAREBORN_API UAsdaCharaInfoSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ── Кнопка панели быстрого доступа и команды клавиш (Shortcutkey.ini) ──
	static constexpr int32 kMenuButtonIdNum   = 73401;   // NEW_SYSTEMMAIN_CHARINFOBUT, индекс таба 0
	static constexpr int32 kCmdInterfaceChar  = 30;      // «INTERFACE CHARACTOER» — по умолчанию C
	static constexpr int32 kCmdInterfaceEmote = 31;      // «INTERFACE EMOTION»    — по умолчанию E

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;

	// ★ Полосы жизни и маны в окне характеристик идут тем же общим механизмом, что и остальные
	//   (см. FAsdaBarSmoother) — иначе одна и та же величина в двух окнах вела бы себя по-разному.
	FAsdaBarSmoother HpSmooth;
	FAsdaBarSmoother MpSmooth;
	void TickVitalBars(float DeltaSeconds);
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCharaInfoSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Публичный API (он же точка входа консольных проверок `asda.CharInfo.*`) ──

	// Тумблер по клавише/кнопке. Tab: 0 = «Статус», 1 = «Эмоции». Порт `uiWindowToggleTab@0xE76160`.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void ToggleTab(int32 Tab);

	// Открыть (со всеми гейтами эталона). Порт `uiWindow_OpenIfAllowed@0xE76240`.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void OpenWindow();

	// Закрыть. Порт `uiWindowClose@0xE76460`.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void CloseWindow();

	// Окно сейчас ВИДНО (не «зарегистрировано» — см. грабли окна навыков).
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	bool IsWindowOpen() const;

	// Переключить главную вкладку без открытия/закрытия. Порт `uiWindowSwitchTabMode@0xE76510`.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void SwitchTabMode(int32 Tab);

	// Переключить под-страницу страницы «Статус» (0/1/2). Порт `uiTabSelectVisible@0xB95A40`.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void SelectStatPage(int32 Page);

	// Перерисовать всё содержимое (порт `UI_FillEquipDetailPanel` + спутники).
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void RefreshAll();

	// Многострочный дамп состояния для живой проверки (консоль `asda.CharInfo.Dump`).
	FString DumpState() const;
	// Живая диагностика переключателя под-вкладок 981 (`asda.CharInfo.TabProbe`) — см. .cpp.
	FString DumpTabProbe() const;
	// НАСТОЯЩИЙ клик мышью по кнопке под-вкладки (`asda.CharInfo.TabClick <i>`) — проверка хит-теста.
	void ClickStatTabAt(int32 Index);

	// ── ★★★ Окно «Обо мне» (8300) — публичная часть для консольной проверки ──
	//   Порт `findSoulmateWinOpen@0xE72C20` / `uiListPagerHandleCommand` case 8304/8305/8399.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void OpenIntroWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void CloseIntroWindow();
	// Отправить текст поля на сервер (пакет 6080) и закрыть окно.
	UFUNCTION(BlueprintCallable, Category="Asda|Характеристики")
	void SubmitIntroText();
	// Положить текст в поле ввода (консольная проверка; в игре текст набирает игрок).
	void SetIntroFieldText(const FString& Text);
	// Однострочный дамп состояния окна «Обо мне».
	FString DumpIntroState() const;
	// Дамп живых виджетов окна (класс узла, видимость, текст, цвет подписей под-вкладок).
	FString DumpWidgets() const;
	// Строка 997 «Навык рыбалки» (живой узел + последняя сырая строка с тегами) — для самотеста В6 рыбалки.
	const FAsdaFmtText& DebugFishSkillFmt() const { return FishFmt; }
	// Строка страницы «Параметры» 931…944 по индексу (11 = 942 «Ур. ремесла») — для самотеста пачки 15 (баг 8); nullptr вне диапазона.
	const FAsdaFmtText* DebugStatFmt(int32 Index) const { return StatFmt.IsValidIndex(Index) ? &StatFmt[Index] : nullptr; }
	// Узел 926 «Звание» (CHARAINFO_CAMPWAR-T3-S) и значок 927 — для самотеста пачки 15 (баг 9).
	UTextBlock* DebugGradeValue() const { return GradeValue.Get(); }
	UImage* DebugGradeMark() const { return GradeMark.Get(); }
	/** Диагностика: живой прямоугольник кнопки 988 «Мой блог» + настоящий клик в её центр. */
	void DebugClickBlog();

	// ★ Проиграть эмоцию по коду — общая точка для окна, хотбара и чата.
	//   Порт эталонного `UI_DispatchTargetContextMenu@0xC532D0`, ветка `menuCategory == 0`.
	UFUNCTION(BlueprintCallable, Category="Asda|Эмоции")
	bool PlayEmotion(int32 Code);
	// ★ 06.11.2026 (`asda.Soul.SlotTruth`): перерисовать лотки действий — эталон `EmoteWnd_WndProc@0xC55780` сверяет слот пары на каждое сообщение.
	void RefreshEmotionIcons() { RefreshEmotionTab(); }
	// ★ 27.09.2026 (pkt4024_facing.md §6): порт `UI_SetSitStandButtonIcon@0xC54CF0` — в лотке «функции» (группа 2)
	//   первая запись с именем `act_updown`/`act_standup` получает иконку по состоянию: 7/8 → `act_standup`,
	//   иначе `act_updown`. Лоток не построен — ничего (эталон: пустой список). Зовёт `ToggleSitStand`.
	void SetSitStandButtonIcon(int32 ActionState);
	// Имя, которое последним поставил `SetSitStandButtonIcon` (пусто — не ставил); для самотеста.
	const FString& GetSitStandIconShown() const { return SitStandIconShown; }

	// Имя текстуры иконки эмоции по коду (мост для хотбара; таблица `emotiontip`).
	FString GetEmotionIconName(int32 Code) const;

	// ★★★ УЛОЖИТЬ ЭМОЦИЮ В СЛОТ ПАНЕЛИ БЫСТРОГО ДОСТУПА — порт `hotbarSetQuickSlotItem@0xC55530`
	//   и его диспетчера `hotbarAssignQuickSlotByControlId@0xC55450`. `AbsSlot` < 0 ⇒ слот берётся
	//   ХИТ-ТЕСТОМ под курсором (`UAsdaHotbarSubsystem::HitTestSlotUnderCursor`) — так работает
	//   бросок мышью; явный номер нужен консольной проверке. Возвращает false, если под курсором
	//   не слот панели или это колонка 12 (слот оружия эмоцию не принимает — разбор в .cpp).
	UFUNCTION(BlueprintCallable, Category="Asda|Эмоции")
	bool DropEmotionToHotbar(int32 Code, int32 AbsSlot = -1);

	// Код эмоции = группа·100 + индекс в группе. Так же кодирует слот хотбара
	//   (`UAsdaHotbarSubsystem`, слот типа `Emotion` хранит именно этот код).
	static int32 MakeEmotionCode(int32 Group, int32 IndexInGroup) { return Group * 100 + IndexInGroup; }
	static int32 EmotionGroupOf(int32 Code) { return Code / 100; }
	static int32 EmotionIndexOf(int32 Code) { return Code % 100; }

private:
	// ── Жизненный цикл виджета ──
	bool EnsureWidget();
	void ResetWidgets();
	void ScanWidgets();
	void BindButtons();
	void ApplyStaticTexts();
	// Подписи под-вкладок: клон `CHARAINFO_STMENU_Text` на каждую кнопку (см. .cpp).
	void BuildStatTabCaptions(bool bThreeTabs);
	// Цвет подписи под-вкладки по кадру скина (0 покой / 1 наведение / 2 нажата-выбрана).
	//   ★ ОТСТУПЛЕНИЕ: по умолчанию чёрное семейство, ключ отката `asda.CharInfo.TabCaptionBlack 0`.
	static FLinearColor TabCaptionColor(int32 Frame);
	// Перекрасить подписи под-вкладок по текущему выбору (выбранная — кадр 2, прочие — кадр 0).
	void RefreshTabCaptionColors();
	// Цвет подписи под-вкладки по кадру состояния (0 покой / 1 наведение / 2 выбрана).
	FLinearColor StatTabFrameColor(int32 Page, int32 Frame) const;
	// Базовые цвета подписей ИЗ АССЕТА — их снимает и применяет общая точка `AsdaUi`.
	TArray<FLinearColor> StatTabIdleColors;
	// Снять обводку со ВСЕХ текстов окна (требование владельца; обоснование — в .cpp).
	void ApplyOutlinePolicy();
	// Подогнать высоту рамки окна (и подложки страницы эмоций) под фактическое содержимое —
	//   лечение «чёрной бороды» снизу. Обоснование и доказательство дефекта оригинала — в .cpp.
	void FitWindowHeight();
	// ★★★ Поднять ВСЁ поддерево страницы. Импортёр протаскивает `Visible="0"` родителя рекурсией
	//   на потомков (закон jui-importer), поэтому страницы 983/984 и вкладка эмоций приходят
	//   свёрнутыми ЦЕЛИКОМ — включая подписи и значения. Эталон этой проблемы не знает: там
	//   видимость наследуется динамически. Зовётся один раз на страницу, дальше поверх ложатся
	//   ЭТАЛОННЫЕ правила «что прятать» (см. ApplyStaticTexts / RefreshElementPage).
	void RevealSubtree(UWidget* Root);
	bool bRevealedData = false;      // страницу 983 уже поднимали
	bool bRevealedElement = false;   // страницу 984 уже поднимали
	bool bRevealedEmotion = false;   // вкладку эмоций уже поднимали
	TArray<int32> StatTabPageOf;     // индекс кнопки под-вкладки → номер страницы (−1 = кнопка скрыта)

	// ── Наполнение ──
	void RefreshBasicPage();     // 982: имя/звание/уровень/профессия/фракция/полосы/зодиак
	void RefreshDataPage();      // 983: 14 числовых строк + рыбалка
	void RefreshFishSkill();     // 997 «Навык рыбалки» — эталонный формат (`uiUpdateStatValueLabel@0xFCAAF0` и др., В6 рыбалки)
	FString FishSkillText() const;   // текст строки 997: формат эталона всегда (голый «0» до 6164 — только откат asda.CharInfo.FishingZero 1)
	int32 RodFishBonus() const;  // `itemCalcEnchantBonus@0xFC8010`: бонус надетой удочки (экип-слот 9)
	void OnFishSkillChanged(int32 Skill);   // делегат автомата рыбалки В1 (6164 / 6172 / сброс) → перерисовка 997
	void RefreshElementPage();   // 984: стихии атаки/защиты + значок доминирующей стихии
	void RefreshEmotionTab();    // 1100: три лотка иконок

	void ApplyHp(int32 Cur, int32 Max);
	void ApplyMp(int32 Cur, int32 Max);
	void ApplyExp(int32 Level, int64 Exp);
	void ApplyWeight(int32 Cur, int32 Max);

	// ── ★★★ Окно «Обо мне» 8300 `My_Introduction_Win` (кнопка 987) ──
	//   Реверс: `Reports/char_info_fix2.md` §4. Сеть — опкод 6080 в ОБЕ стороны
	//   (`UAsdaNetSubsystem::SendSoulmateIntroduction6080` / `SelfIntroduction`).
	bool EnsureIntroWidget();
	UPROPERTY(Transient) TObjectPtr<UUserWidget> IntroWin = nullptr;            // 8300
	UPROPERTY(Transient) TObjectPtr<class UEditableText> IntroEdit = nullptr;   // 8302 (LimitText 128)
	UPROPERTY(Transient) TObjectPtr<UButton>    IntroOkBtn = nullptr;           // 8304
	UPROPERTY(Transient) TObjectPtr<UButton>    IntroCancelBtn = nullptr;       // 8305
	UPROPERTY(Transient) TObjectPtr<UButton>    IntroCloseBtn = nullptr;        // 8399
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IntroGuide = nullptr;           // 8303
	UPROPERTY(Transient) TObjectPtr<UTextBlock> IntroTitle = nullptr;           // 8301
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> IntroRelays;

	// ── Подписки ──
	void OnVitalsChanged(int32 UserIdx, const FAsdaVitals& V);
	void OnSelfWeight(int32 Cur, int32 Max);
	void OnSelfExpChanged(int32 Level, int64 Exp, int64 Gained, bool bFullExp, int32 SrcRenderId);
	void OnSelfStatsChanged();
	// ★ 18.10.2026: смена профессии 5042 — эталон `UI_FillEquipDetailPanel(&g_pCharInfoWnd)` (network.c:21850): строка «Профессия».
	void OnSelfJobChanged(int32 OldClassId, int32 Job, int32 ClassId, int32 SubJob);
	void OnSelfFaction(int32 FactionId, int32 Points, int32 Grade);
	void OnSelfTitleChanged();

	UFUNCTION() void OnMenuCommand(int32 ButtonIdNum, int32 TargetWindowId, bool bOpen);
	UFUNCTION() void OnGameCommand(int32 Cmd);

	// ── Подсказки (общая плашка; владелец — только мы) ──
	void ShowStatTooltip(int32 RowIndex);   // строки STTT01…STTT13 → loc 12033+i / 1401+i
	void ShowTitleTooltip();                // кнопка звания → loc 2432
	void ShowEmotionTooltip(int32 Code);
	void HideOwnTooltip();
	void TickPointer();

	// ── Перетаскивание эмоции из лотка в панель быстрого доступа (эталон msg 5 / 7) ──
	void BeginEmotionDrag(int32 SlotIdx);
	void EndEmotionDrag(bool bWasDragging, int32 SlotIdx);
	void CancelEmotionDragVisual(int32 SlotIdx);
	void CancelEmotionDrag();
	// Прозрачность «призрака» и приглушение исходной иконки — те же числа, что у книги навыков,
	//   инвентаря и самой панели (значения согласованы с владельцем, менять только вместе).
	static constexpr float kEmoteDragGhostOpacity  = 0.4f;
	static constexpr float kEmoteDragSourceOpacity = 0.4f;
	FAsdaDragArm EmoteDragArm;                            // взвод «нажали ≠ тащим»
	UPROPERTY(Transient) FAsdaDragGhost EmoteDragGhost;   // летящая за курсором иконка
	bool bEmoteDragActive = false;

	// ── Утилиты ──
	void SetText(UTextBlock* T, const FString& S);
	// Снять стиль строки из JUI ДО первой подмены на rich (см. шапку `FAsdaFmtText`).
	static void CaptureFmt(FAsdaFmtText& F, UTextBlock* T);
	// Показать СЫРОЙ текст с тегами через ЕДИНУЮ точку `UAsdaRichText::ApplyFormatted`
	//   и переприсвоить кэш узла (иначе он осиротеет на выброшенном виджете).
	void SetTextFmt(FAsdaFmtText& F, const FString& Raw);
	static FString RangeText(int32 Lo, int32 Hi);
	static FString BaseBonusText(int32 Base, int32 Bonus);
	FString Loc(int32 Id) const;
	FString LocTagged(int32 Id) const;   // ★ Дедуп №2, В1-B (U10a): было `LocRaw` — по смыслу это `AsdaNotice::LocTagged` (теги сохранены, маркеры [ss1]/[sd1] не тронуты)
	void Notice(int32 LocId);
	int32 ResolveSelfClassId() const;

	// ══════════════ ВИДЖЕТЫ ══════════════
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UObject>> Relays;

	// Каркас
	UPROPERTY(Transient) TObjectPtr<UButton>  CloseButton = nullptr;      // 980
	UPROPERTY(Transient) TObjectPtr<UWidget>  GearStatic = nullptr;       // 985
	UPROPERTY(Transient) TObjectPtr<UWidget>  GearAnimated = nullptr;     // 986
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MainTabButtons;      // 902: OPT0/OPT1
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  MainTabPlanes;       // 902: подсветка выбранного
	// Девять планов рамки самого окна (`CHARAINFO_WIN_0_0…0_8`) и девять планов подложки страницы
	//   эмоций (`CHARAINFO_IMOTIONBase_164_0…8`) — нужны подгонке высоты под содержимое (см. .cpp).
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  WinFramePlanes;
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  MotionBasePlanes;
	UPROPERTY(Transient) TObjectPtr<UWidget>  StatBack = nullptr;         // 903
	UPROPERTY(Transient) TObjectPtr<UWidget>  MotionBack = nullptr;       // 1100
	UPROPERTY(Transient) TObjectPtr<UWidget>  MotionBase = nullptr;       // 2000000027

	// Под-табы страницы «Статус»
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> StatTabButtons;      // 981: OPT0..OPT2
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  StatTab2Planes;      // 981 (2 кнопки, `_162_*`)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  StatTab3Planes;      // 981 (3 кнопки, `_161_*`)
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatTab2Text = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatTab3Text = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatTabCaptions;   // по одной подписи на кнопку
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> StatPages;           // 982/983/984

	// Страница 982 «Информация»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText = nullptr;       // 904
	UPROPERTY(Transient) TObjectPtr<UButton>    TitleButton = nullptr;    // 905
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleFront = nullptr;     // 919
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TitleSecond = nullptr;    // 920
	UPROPERTY(Transient) TObjectPtr<UTextBlock> LevelText = nullptr;      // 906
	UPROPERTY(Transient) TObjectPtr<UTextBlock> JobText = nullptr;        // 907
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ConsText = nullptr;       // 946
	UPROPERTY(Transient) TObjectPtr<UWidget>    ConsRow = nullptr;        // 945 (родитель 946)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> RowLabels;        // 12300…12305 (12304 пропущен)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> BarLabels;        // 12306…12309
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FactionLabel = nullptr;   // 921
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FactionValue = nullptr;   // 922
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HonorLabel = nullptr;     // 923
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HonorValue = nullptr;     // 924
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GradeLabel = nullptr;     // 925
	UPROPERTY(Transient) TObjectPtr<UTextBlock> GradeValue = nullptr;     // 926
	UPROPERTY(Transient) TObjectPtr<UImage>     GradeMark = nullptr;      // 927
	// Строки с тегами на странице «Информация»: 922 фракция (`<FC=%0x>…`), 919/920 звание
	//   (`<FC=%08x>…` из пакета 6068). Живые узлы + снятый стиль — см. шапку `FAsdaFmtText`.
	UPROPERTY(Transient) FAsdaFmtText FactionFmt;                         // 922
	UPROPERTY(Transient) FAsdaFmtText TitleFrontFmt;                      // 919
	UPROPERTY(Transient) FAsdaFmtText TitleSecondFmt;                     // 920
	UPROPERTY(Transient) TObjectPtr<UButton>    IntroButton = nullptr;    // 987
	// ★ 988 `CharInfo_Blog` «Мой блог» (подпись loc 11752) — эталон прячет всю строку 989,
	//   владелец попросил вернуть. Ведёт в окно встроенного браузера 19000, который мы не
	//   портируем ⇒ пока честный отказ с нотисом и записью в журнал.
	UPROPERTY(Transient) TObjectPtr<UButton>    BlogButton = nullptr;     // 988
	UPROPERTY(Transient) TObjectPtr<UTextBlock> BlogCaption = nullptr;    // `CharInfo_Blog_Text`
	UPROPERTY(Transient) TObjectPtr<UWidget>    BlogRow = nullptr;        // 989 — эталон скрывает
	UPROPERTY(Transient) TObjectPtr<UWidget>    CashRow = nullptr;        // 990 — эталон скрывает
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CashLabel = nullptr;      // 12305
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CashValue = nullptr;      // 930
	UPROPERTY(Transient) TObjectPtr<UWidget>    PreCashLabel = nullptr;   // 40006
	UPROPERTY(Transient) TObjectPtr<UWidget>    PreCashValue = nullptr;   // 40007
	UPROPERTY(Transient) TObjectPtr<UWidget>    MacCharButton = nullptr;  // 12341
	UPROPERTY(Transient) TObjectPtr<UWidget>    CashCheckButton = nullptr;// 12342

	// Полосы (порт `uiBar_*`): заливка + текст
	UPROPERTY(Transient) FAsdaMiniBarFill HpBar;         // 910 + 909
	UPROPERTY(Transient) FAsdaMiniBarFill MpBar;         // 912 + 911
	UPROPERTY(Transient) FAsdaMiniBarFill ExpBar;        // 916 + 915
	UPROPERTY(Transient) FAsdaMiniBarFill WeightBar;     // 918 + 917
	UPROPERTY(Transient) TObjectPtr<UImage> HpBuff = nullptr;   // 913
	UPROPERTY(Transient) TObjectPtr<UImage> MpBuff = nullptr;   // 914
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HpText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MpText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ExpText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WeightText = nullptr;

	// Страница 983 «Параметры»
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatLabels;  // 12310…12323 (14, последняя мёртвая)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StatValues;  // 931…944     (14, последняя мёртвая)
	// ★ Те же 14 строк значений, но ЖИВЫМИ узлами: после первого `ApplyFormatted` строка становится
	//   `URichTextBlock`, и `StatValues[i]` указывает на выброшенный виджет. Видимость, хит-тест
	//   подсказки и запись текста идут ТОЛЬКО через `StatFmt[i].Node`.
	UPROPERTY(Transient) TArray<FAsdaFmtText> StatFmt;               // 931…944
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     StatHits;    // прозрачные зоны наведения строк
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FishLabel = nullptr; // 996
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FishValue = nullptr; // 997 (узел из JUI; после первой записи с тегом живой узел — `FishFmt.Node`)
	// ★ В6 рыбалки (08.10.2026): строка 997 несёт тег `<FC=FFFFFF00>` (эталонный формат навыка) — пишется ТОЛЬКО через
	//   `SetTextFmt(FishFmt, …)`; видимость и дамп — через `FishFmt.Node` (как у `StatFmt`).
	UPROPERTY(Transient) FAsdaFmtText FishFmt;                        // 997 живым узлом
	UPROPERTY(Transient) TObjectPtr<UWidget>    FishRow = nullptr;   // 995

	// Страница 984 «Характеристики»
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EleHeadAtk = nullptr;   // 12325
	UPROPERTY(Transient) TObjectPtr<UTextBlock> EleHeadDef = nullptr;   // 12333
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>     EleDomIcons;    // 950…955
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EleAtkLabels;   // 12327…12332
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EleAtkValues;   // 960…965
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EleDefLabels;   // 12334…12339
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EleDefValues;   // 966…971

	// Вкладка эмоций
	UPROPERTY(Transient) TArray<FAsdaEmotionSlotUi> EmotionSlots;      // 12 + 6 + 12
	FString SitStandIconShown;   // ★ 27.09.2026: последняя иконка «сесть/встать», поставленная `SetSitStandButtonIcon`
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> EmotionHeads;  // 1152/1153/1154
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> EmotionCursors;   // 1102/1103/1104
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> EmotionGroups;    // IMOTION_B00_s_grp и т. п.

	// ══════════════ СОСТОЯНИЕ ══════════════
	int32 CurTab = 0;          // 0 статы / 1 эмоции (эталонное `curTab`)
	int32 CurStatPage = 0;     // 0/1/2
	bool  bTipShown = false;   // эталонное `bTooltipActive`
	int32 HoverRow = -1;       // строка статов под курсором (−1 нет)
	int32 HoverEmotion = -1;   // слот эмоции под курсором (индекс в `EmotionSlots`)
	int32 HoverKind = 0;       // 0 нет, 1 строка статов, 2 звание, 3 эмоция

	bool bNetBound = false;
	bool bFishBound = false;     // подписка на `UAsdaFishingSubsystem::OnFishSkillChanged` выполнена (В6 рыбалки)
	bool bMenuBound = false;
	bool bHotbarBridged = false;
	TWeakObjectPtr<AAsdaPlayerController> BoundController;

	// Последние применённые значения — чтобы окно, открытое ПОСЛЕ прихода пакета, не было пустым.
	int32 LastHpCur = -1, LastHpMax = -1;
	int32 LastMpCur = -1, LastMpMax = -1;
	int32 LastFactionId = -1, LastFactionPoints = 0, LastFactionGrade = 0;

	int32 WindowZ = 94;        // как у окна навыков — над панелью меню (93)
};
