// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaSystemMenuSubsystem — панель ГЛАВНОГО МЕНЮ игры `SYSTEMMAIN_NEW_WIN` (JUI IDNum 73400,
//   `gamemain_jui.ini:349125`, рантайм-класс эталона `CSystemMainWnd`). Ряд кнопок-тогглов,
//   открывающих основные окна интерфейса, + кнопки свернуть/развернуть + плашка-подпись +
//   указатель-стрелка обучения. Полный реверс — `Reports/systemmain_win_port.md`.
//
// ═══════════════════════════ КАНОН: КАКОЕ ОКНО ЖИВО ════════════════════════════════════════════
//   `SYSTEMMAIN_NEW_WIN` (73400). Легаси `SYSTEMMAIN_WIN` (100) МЁРТВ: первая строка
//   `CSystemMainWnd_InitControls@0xE8EC80` — `SetVisible(FindControlByID(100), 0)`, а все привязки
//   идут по id 73400…73426. Закон `jui-prefer-new-windows` здесь совпал с бинарём (как у хотбара).
//   Оба окна обслуживает ОДИН класс, поэтому у легаси просто нет живых id.
//   Ассет — `WBP_SYSTEMMAIN_NEW_WIN` (импорт JUI); свой UI не рисуем (`no-invented-ui`), все
//   параметры вынесены в UPROPERTY (`ui-editable-in-ue`). Перегенерацию WBP НЕ запускали.
//
// ═══════════════════════════ РАСКЛАДКА ═════════════════════════════════════════════════════════
//   Корень — невидимая точка-якорь 10×10 (скин `NoImage`, `PlaneAlwaysInvisible=1`) в базе 1024×768
//   на (568, 621): `AlignXPos="2 -480 -94"` (правый край), `AlignYPos="8 -480 -94"` (нижний край).
//   Все контролы — POINT-анкор (1,1) ⇒ панель жёстко в НИЖНЕМ-ПРАВОМ углу, без растяжения.
//   Блока `EVS` НЕТ ни у одного контрола ⇒ окно НЕ перетаскивается (закон `ui-window-drag-hittest`).
//
// ═══════════════════════════ КНОПКИ И ЧТО ОНИ ОТКРЫВАЮТ ════════════════════════════════════════
//   Все 10 кнопок-иконок — скины типа `CheckButton` ⇒ КЛИК = TOGGLE. Эталон `checkBox_OnToggle`
//   @0x12FDF70 шлёт окну 67073 (0x10601, «включили») → `uiHotkey_DispatchCommand@0xE8F670` и
//   67074 (0x10602, «выключили») → `uiMainMenu_OnCommand@0xE8F880`.
//     73401 CHARINFO   → CHARAINFO_WIN 900       (таб 0, нотис 2271)
//     73402 ITEM       → INVEN_NEW_WIN 83500     (таб 1, нотис 2272)
//     73403 SKILL      → SKILL_WIN 12000         (таб 7, нотис 9418)
//     73404 QUEST      → QUEST_WIN 700           (таб 2, нотис 2273)
//     73405 COMUNITY   → COMMUNITY_WIN 6400      (таб 5, нотис 2275)
//     73406 SYSINFO    → SYSTEM_MenuWIN_NEW 69400 (таб 6, нотис 2276) ★ ИСПРАВЛЕНО 2026-09-17:
//                        это ESC-МЕНЮ, а не окно настроек 69500 (`uiPanelToggleOrClearTabs@0xE8FA70`
//                        тоггает `g_pOptionsWnd[0]` = контрол 69400). Разбор — в шапке
//                        `Public/UI/AsdaSystemOptionSubsystem.h`.
//     73407 SHOP       → премиум-магазин         (таб 3, нотис 2281; закрытия в эталоне НЕТ)
//     73409 ETC        → ETC_NEW_WIN 74600       (таб 9, нотис 9800; тогл одной функцией)
//     73410 PICK       → сцена гачи 68100        (таб 10, нотис 9801)
//     73408 SERIAL     → обработчика НЕТ, кнопка гасится в InitControls (нотис 2277)
//     73411 WEBMALL    → обработчика НЕТ (нотис 9802, в эталоне недостижим — дефект Д3)
//   Портированные окна: 73402 (инвентарь), 73403 (навыки), 73404 (журнал заданий), 73410 (гача).
//   Остальные окна в порту ЕЩЁ НЕ ПОРТИРОВАНЫ (реестр `Docs/Port/PORT_PENDING_ON_SYSTEMS.md`,
//   записи 28/33/34) — клик уходит в делегат `OnMenuCommand` + честную строку в журнал;
//   заглушек-окон не делаем (`no-simplifications-stubs`).
//
//   ★★★ ГРАБЛЯ ДЛЯ ТОГО, КТО БУДЕТ ПОРТИРОВАТЬ ОСТАЛЬНЫЕ ОКНА. Подключений ДВА, и они РАЗНЫЕ:
//     `Menu->OnMenuCommand.AddDynamic(...)` — САМА КОМАНДА кнопки (открыть/закрыть окно);
//     `Menu->RegisterWindow(<idnum>)`       — ТОЛЬКО подсветка кнопки из видимости окна.
//   Журнал заданий делал лишь второе — и кнопка 73404 не открывала окно, хотя клавиша Q работала
//   (заявка владельца 2026-08-23, разбор `Reports/quest_tracker_fix2.md` §Е3).
//   Доходит ли клик до кнопки вообще — покажет `asda.SysMenu.Dump`: он печатает для каждой
//   кнопки прямоугольник, видимость, родителя-«пряталку», перекрытия и вывод
//   «КЛИК ДОХОДИТ / НЕ ДОХОДИТ».
//
// ═══════════════════════════ ЧЕКНУТОСТЬ = «ОКНО ОТКРЫТО» ═══════════════════════════════════════
//   ★ Мисномер эталона: `uiPanel_SetTabVisible@0xE8FB10` НЕ про видимость. Внутри —
//   `CControl_SetVisibleState@0x12FE000`, который пишет латч `+608` (checked) и кадр плана:
//   с наведением checked→3 / unchecked→1, без наведения checked→2 / unchecked→0.
//   Т.е. это «подсветить кнопку, потому что её окно открылось». Зовут её САМИ окна
//   (`social.c:3795` при открытии комьюнити, `quest.c:1700` при закрытии квестов и т.д.).
//
//   ★★★ ПОЛНАЯ КАРТИНА ЭТАЛОНА (проверены ВСЕ 9 табов, оба направления):
//     таб 0  charinfo  — `uiWindow_OpenIfAllowed@0xE76370` ставит 1, `uiWindowClose@0xE76460` — 0;
//     таб 1  инвентарь — `uiOpenInventoryWindow@0xCFF300` ставит 1, `sowelWindowClose@0xCFF760` — 0,
//                        ОБЕ через аргумент-флаг (у инвентаря он есть, у прочих окон нет);
//     таб 2  квесты    — `quest.c:1700` (закрытие 0), `ui_core.c:33590` (открытие 1);
//     таб 3  магазин   — `ui_core.c:28534/30538` (0), `entity_char.c:45093` (1);
//     таб 5  комьюнити — `social.c:3795` (1) / `social.c:3821` (0);
//     таб 6  система   — `ui_core.c:80717` (0);
//     таб 7  навыки    — `skillwin_OpenSkillWindow@0xE52DE0` (1) / `uiPanel_Hide@0xE53240` (0);
//     таб 9  доп-меню  — `ui_core.c:87308/87313`;  таб 10 гача — `world_map.c:4752` (1) / `ui_core.c:7201` (0).
//   ⇒ ИНВАРИАНТ: подсветку ставит САМО ОКНО в конце своей функции показа/скрытия, а НЕ диспетчер
//   кнопки. Поэтому в оригинале открытие ПО КЛАВИШЕ подсвечивает кнопку так же, как клик по ней:
//   `input.c:1729/4285` зовут `uiToggleInventoryWindow(a1, 1)` — с флагом «обнови таб».
//   Диспетчеры кнопки (`uiHotkey_DispatchCommand@0xE8F670`) зовут с флагом 0 ровно потому, что
//   кадр кнопки уже сменил сам `checkBox_OnToggle` — а не потому, что таб не нужен.
//   Все прочие пути закрытия (пакет сервера `network.c:14291/27853`, смерть/смена карты
//   `entity_char.c:62959`, крестик, магазин/склад) зовут close С флагом 1 ⇒ таб гаснет всегда.
//
//   ★★★ КАК ЭТО СДЕЛАНО В ПОРТУ — ЕДИНЫМ МЕХАНИЗМОМ, А НЕ ВЫЗОВОМ ИЗ КАЖДОГО ОКНА.
//   Повторять эталонный ручной вызов в каждой ветке каждого окна — гарантированный источник
//   рассинхрона (ровно он и был: окно навыков подсветку не ставило вовсе). Вместо этого:
//     • таблица ниже (`TargetWindowName`) — источник истины «кнопка ↔ id окна», второй нет;
//     • окно ОДИН РАЗ зовёт `RegisterWindow(IdNum кнопки)`;
//     • `SyncCheckedFromWindows()` раз в кадр выводит подсветку из ФАКТИЧЕСКОЙ видимости окна
//       (`UAsdaWindowSubsystem::IsWindowVisible`) — по изменению видимости, как эталонный вызов
//       из конца Open/Close, но покрывая ВСЕ пути разом, включая будущие.
//   `SetTabChecked`/`SetButtonChecked` остались (их зовёт консоль и не подключённые ещё кнопки).
//
//   ★ Второй мисномер: `uiPanel_DestroyChildControls@0xE8FE40` ничего не разрушает — она ставит
//   всем кнопкам `+609 = 1` = DISABLED (`sub_9350A2`), а `uiPanelClearAllTabFlags@0xE8FF00`
//   ставит `+609 = 0` = enabled (`CControl_ClearField609@0xC776F0`). Доказательство парности —
//   `social.c:8080`: есть соулмейт → ClearField609, нет → нотис 8026 + sub_9350A2.
//   В порту это `SetAllButtonsEnabled` / `SetTabEnabled`.
//
// ═══════════════════════════ СВОРАЧИВАНИЕ ══════════════════════════════════════════════════════
//   Два эталонных пути, оба — мгновенная смена ВИДИМОСТИ (анимации сворачивания в оригинале НЕТ):
//     `CUIWindow_ShowHide@0xE8F370`  — путь кнопок 73425/73426 (+ `listMoveToEnd` = поднять окно);
//     `CUIPanel_SetExpanded@0xE8F150` — внешний путь (магазин/пати/скрытие HUD), пишет флаг +14.
//   Свёрнутое состояние = виден ОДИН контрол 73425 (стрелка вверх 21×21) на месте 73426.
//
// ═══════════════════════════ АНИМАЦИИ ══════════════════════════════════════════════════════════
//   В окне ровно ДВЕ покадровые анимации, обе — части указателя 73422:
//   `Ani_Gear_50_InnerCircle` (4 кадра 50×50, прогон 300 мс) и `Small_Highlight` (4 кадра 34×34,
//   200 мс). Импортёр уже создал их как `UAsdaAnimatedPlane` — своей обёртки не заводим.
//   Больше анимаций в окне НЕТ (не выдумываем, закон `never-invent-only-original`).
//
// ═══════════════════════════ ЗВУК ══════════════════════════════════════════════════════════════
//   `uiWindowMsgProc@0xE90220` играет `S_e_button06.wav` на 67073 (открытие). На 67074 звука нет.
//   Разворот/сворачивание внешним путём — `S_e_button06` / `S_e_button07`.
//
// ═══════════════════════════ ИСПРАВЛЕННЫЕ ДЕФЕКТЫ (закон fix-original-defects) ═════════════════
//   Д1 `UI_LoadWindowState@0xD61F20` читает 4-ю строку `WinState.bin` («SMO <состояние панели>»,
//      её пишет `UI_SaveWindowState@0xD61D40`) и ВЫБРАСЫВАЕТ — ветки `strcmp(tag,"SMO")` нет.
//      Свёрнутость сохранялась, но никогда не восстанавливалась.  `asda.SysMenu.LegacyNoRestore 1`
//   Д2 Кнопки 73425/73426 зовут `ShowHide(...,0)` ⇒ сворачивание БЕЗЗВУЧНО, хотя код звука в
//      функции есть и внешний путь его использует.                `asda.SysMenu.LegacyMuteToggle 1`
//   Д3 Цикл тултипов `for (i = 0; i < 11; ++i)` при таблице из 12 записей ⇒ подсказка 73411
//      (нотис 9802) недостижима.                                  `asda.SysMenu.LegacyTipLoop11 1`
//   Д4 `CUIPanel_SetExpanded` при сворачивании прячет 73409, но забывает 73410 (в `ShowHide`
//      прячутся обе) ⇒ кнопка гачи висит над свёрнутой панелью.   `asda.SysMenu.LegacyPickStays 1`
//   Д5 `uiPanel_ShowTab@0xE900A0` case 9 («разрешить доп-меню») зовёт `sub_9350A2` = ЗАПРЕТИТЬ,
//      тогда как все прочие case зовут `ClearField609` = разрешить. `asda.SysMenu.LegacyShowTab9 1`
//   Д6 (дефект ПОРТА, не оригинала) `UAsdaUmgBuilder::AddCheckBox` кладёт в стиль только кадры
//      0 и 2 и БЕЗ тинтов ⇒ у всех 10 кнопок не было ни hover, ни отличия «окно открыто».
//      Полный 4-кадровый стиль ставится в рантайме из тинтов скина (см. `FAsdaSysMenuTints`).
//      ★★★ ЗАКРЫТО В КОРНЕ 2026-08-08: импортёр теперь сам кладёт все четыре кадра (текстура+UV+
//      Diffuse каждого) во все шесть состояний Slate — см. `Reports/jui_import_checkbutton.md`.
//      Рантайм-путь ниже оставлен (он же обслуживает `SetTabChecked`), значения тинтов исправлены
//      на настоящие из `JuiSkin.ini`.
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSystemMenuSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UButton;
class UCheckBox;
class UTextBlock;
class UPanelWidget;
class UAsdaAnimatedPlane;
class UAsdaButtonRelay;

// ── Кадры-состояния скина CheckButton (`button_states_re.md`): у всех 10 кнопок панели UV один и
//    тот же во всех четырёх кадрах, различие ТОЛЬКО в `Diffuse`. Значения — из `JuiSkin.ini`,
//    хранятся строками sRGB «AARRGGBB» и переводятся в линейные `AsdaUi::SrgbHexToLinear`
//    (★ ЗАКОН `ui-srgb-gamma-rule`). Правятся в редакторе.
//
// ★ ПЕРЕПРОВЕРЕНО 2026-08-08 (сверка с `JuiSkin.ini` по трём копиям: `RE/docs`, `Donors/TRANSLATE_unpacked/jui_orig`,
//   `Donors/TRANSLATE_unpacked/jui_fanta` — совпадают). Значения ВЕРНЫ и взяты из данных: окно `SYSTEMMAIN_NEW_WIN`
//   использует скины `NEW_Myinfo / NEW_Bag / NEW_Skill / NEW_Quest / NEW_Commu / NEW_PrimumShop_M /
//   NEW_ETC_M / NEW_System_M / NEW_Serial_M / NEW_Pri_Pick / NEW_WebMall` (атлас `NE_SOULANI`), у всех
//   одиннадцати: SS0 = FFFFFFFF, SS1 = FFFEE745, SS2 = FFFFD280, SS3 = FFFFB935, `Animate = "0"`.
//   ⚠ НЕ путать со скинами `Main_*` (атлас `N_03`) — это СТАРОЕ окно `SYSTEMMAIN_WIN` (8 кнопок),
//   у них другой набор: FFFFFFFF / FFFFFFFF(анимир.) / FFFFFF80 / FFFFFF00(анимир.).
//
// ★ После правки импортёра (`UAsdaUmgBuilder::AddCheckBox` кладёт все четыре кадра сам) этот
//   рантайм-путь стал ИЗБЫТОЧНЫМ, но сохранён: он же обслуживает `SetTabChecked` и правку тинтов
//   из редактора, и ставит ровно то же, что импортёр.
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaSysMenuTints
{
	GENERATED_BODY()

	// Кадр 0 — окно закрыто, курсора нет.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu") FString Normal = TEXT("FFFFFFFF");
	// Кадр 1 — окно закрыто, курсор над кнопкой (hover).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu") FString Hover = TEXT("FFFEE745");
	// Кадр 2 — окно ОТКРЫТО (checked) / нажатие.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu") FString Checked = TEXT("FFFFD280");
	// Кадр 3 — окно открыто И курсор над кнопкой.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu") FString CheckedHover = TEXT("FFFFB935");
};

// ── Одна кнопка панели. Таблица заполняется РОВНО по эталону (`CSystemMainWnd_InitControls`,
//    таблицы `cmdIdTable`/`noticeIdTable` в `uiWindowMsgProc`, `uiPanel_SetTabVisible`).
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaSysMenuButton
{
	GENERATED_BODY()

	// IDNum контрола в JUI (73401…73411) — им же эталон адресует команды.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") int32 IdNum = 0;
	// Имя виджета в WBP (точное имя idc — импортёр даёт его интерактив-контролам).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") FName Widget;
	// Индекс «таба» в эталонных `uiPanel_SetTabVisible/ShowTab/HideTab`; −1 = кнопка в них не значится.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") int32 TabIndex = -1;
	// String-ID подсказки при наведении (`noticeIdTable`); −1 = подсказки нет.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") int32 NoticeId = -1;
	// IDNum целевого ОКНА, которое кнопка открывает (0 = обработчика в эталоне нет).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") int32 TargetWindowId = 0;
	// Человекочитаемое имя целевого окна (для журнала и точки подключения).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") FString TargetWindowName;
	// Участвует ли кнопка в сворачивании панели (эталонные списки `ShowHide`/`SetExpanded`).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") bool bInCollapseSet = false;
	// Видима ли кнопка вообще (73408 и 73411 в данных `Visible="0"`, 73408 ещё и гасится в InitControls).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asda|SysMenu") bool bVisibleByDefault = true;

	// ── рантайм ──
	UPROPERTY(Transient) TObjectPtr<UCheckBox> Box = nullptr;
	UPROPERTY(Transient) bool bChecked = false;    // латч +608: «целевое окно открыто»
	UPROPERTY(Transient) bool bEnabled = true;     // инверсия латча +609 (disabled)
	UPROPERTY(Transient) bool bHovered = false;    // для подсказки (эталонные сообщения 8 / 0xA)

	// ── ПРИВЯЗКА К ОКНУ (единый механизм синхронизации, см. блок в шапке файла) ──
	// Окно подключилось к своей кнопке (`RegisterWindow`). Пока false — подсветку кнопки
	//   никто не выводит, она живёт только тем, что поставил клик (кнопок ещё не портированных окон).
	UPROPERTY(Transient) bool bBound = false;
	// Id окна в `UAsdaWindowSubsystem`. По умолчанию = `TargetWindowName` из таблицы эталона;
	//   окно может при регистрации задать свой (если рантайм-id отличается от имени JUI).
	UPROPERTY(Transient) FName BoundWindowId;
	// Последняя НАБЛЮДЁННАЯ видимость окна — по её изменению и ставится подсветка
	//   (порт-эквивалент эталонного `uiPanel_SetTabVisible` из конца Open/Close самого окна).
	UPROPERTY(Transient) bool bWindowOpenObserved = false;
	// Сколько кадров ещё ждать, пока команда кнопки «устаканится» (окна открываются отложенно).
	//   0 = ждать нечего. По истечении сверяем подсветку с фактом и лечим расхождение.
	UPROPERTY(Transient) int32 SettleFrames = 0;
};

// «Просят окно»: IdNum кнопки, IdNum целевого окна, bOpen (true = 67073, false = 67074).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAsdaMenuCommand, int32, ButtonIdNum, int32, TargetWindowId, bool, bOpen);

UCLASS()
class ASDAREBORN_API UAsdaSystemMenuSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()
	friend struct FAsdaFishZoneSelfTest;   // ★ 08.10.2026 (рыбалка В4): самотест гейта «идёт рыбалка» `asda.Fish.ZoneSelfTest`

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSystemMenuSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── Константы эталона (не менять) ──
	static constexpr int32 kWindowIdNum   = 73400;   // сама панель
	static constexpr int32 kLegacyIdNum   = 100;     // легаси SYSTEMMAIN_WIN (гасится InitControls)
	static constexpr int32 kMessageIdNum  = 73421;   // плашка-подпись
	static constexpr int32 kArrowIdNum    = 73422;   // указатель обучения NEW_SKILL_POP
	static constexpr int32 kBtnExpandId   = 73425;   // кнопка РАЗВЕРНУТЬ (видна в свёрнутом виде)
	static constexpr int32 kBtnCollapseId = 73426;   // кнопка СВЕРНУТЬ (видна в развёрнутом виде)
	static constexpr int32 kArrowHelpTopic = 30;     // UI_BuildHelpTooltipText topic 30
	static constexpr int32 kArrowHelpLocId = 9168;   // «Получены очки навыка. Открой окно навыков»
	static constexpr int32 kSysMenuZ      = 93;      // между хотбаром 92 и мини-статусом 95

	// ═══════════════ ПУБЛИЧНОЕ API ═══════════════

	// Показать/скрыть ВСЁ окно панели (аналог видимости корня 73400).
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetWindowShown(bool bShown);

	UFUNCTION(BlueprintPure, Category="Asda|SysMenu")
	bool IsWindowShown() const { return bWindowShown; }

	// Эталонный `CUIWindow_ShowHide@0xE8F370` — путь кнопок 73425/73426: поднимает окно наверх,
	//   прячет/показывает и 73409, и 73410. bPlaySound: эталон зовёт с 0 (дефект Д2).
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void ShowHide(bool bShow, bool bPlaySound);

	// Эталонный `CUIPanel_SetExpanded@0xE8F150` — внешний путь (магазин/пати/скрытие HUD):
	//   пишет флаг «развёрнуто», наверх окно НЕ поднимает. Зовётся с bPlaySound=1.
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetExpanded(bool bOn, bool bPlaySound);

	UFUNCTION(BlueprintPure, Category="Asda|SysMenu")
	bool IsExpanded() const { return bExpanded; }

	// ══════════ ★★★ ЕДИНЫЙ МЕХАНИЗМ «КНОПКА ↔ ОКНО» ══════════
	//   Окно подключается к своей кнопке ОДНОЙ строкой в своей ленивой привязке:
	//       Menu->RegisterWindow(kMenuButtonIdNum);
	//   Дальше подсветка кнопки выводится ОДНИМ местом (`SyncCheckedFromWindows`) из ФАКТИЧЕСКОЙ
	//   видимости окна в `UAsdaWindowSubsystem` — какой бы путь окно ни открыл/закрыл: клавиша,
	//   кнопка меню, крестик окна, пакет сервера, смерть, смена карты, консоль. Правок в самой
	//   панели меню при добавлении нового окна НЕ требуется: id окна уже лежит в таблице эталона
	//   (`TargetWindowName`), регистрация лишь говорит «это окно теперь есть».
	//
	//   WindowId — переопределение id, если рантайм-имя окна отличается от имени JUI в таблице.
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void RegisterWindow(int32 ButtonIdNum, FName WindowId = NAME_None);

	// То же для целей, которые живут НЕ в оконном менеджере (напр. сцена гачи 73410):
	//   цель даёт предикат «я сейчас на экране». Имя отдельное, потому что перегрузку UFUNCTION
	//   рефлексия UE не принимает, а TFunction в UFUNCTION не передать.
	void RegisterWindowPredicate(int32 ButtonIdNum, TFunction<bool()> IsOpen);

	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void UnregisterWindow(int32 ButtonIdNum);

	UFUNCTION(BlueprintPure, Category="Asda|SysMenu")
	bool IsWindowRegistered(int32 ButtonIdNum) const;

	// ── Чекнутость кнопки = «её окно открыто» (эталонный `uiPanel_SetTabVisible`). Зовут ОКНА. ──
	//   ⚠ Для ПОДКЛЮЧЁННОЙ кнопки ручная установка живёт лишь до ближайшего кадра: её перебьёт
	//   вывод из фактической видимости окна. Это осознанно — источник истины один.
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetTabChecked(int32 TabIndex, bool bChecked);

	// То же, но адресация по IDNum кнопки (73401…73411) — удобнее для внешних систем.
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetButtonChecked(int32 ButtonIdNum, bool bChecked);

	UFUNCTION(BlueprintPure, Category="Asda|SysMenu")
	bool IsButtonChecked(int32 ButtonIdNum) const;
	// ★ fishfix2 09.10 (МЕЛК-2): IDNum кнопки, чья подсказка показана сейчас (−1 — нет); для самотеста наведения сквозь окно.
	int32 GetTipShownFor() const { return TipShownFor; }
	// ★ R5-fix8-b 14.11.2026: сколько нажатий мышью панель отказала «курсор над окном выше панели» — только наблюдение для самотеста
	//   (`UI.SelfTestBodyPress` ТРЕЙ0 ждёт этого события, а не времени); на поведение не влияет.
	int32 GetMouseRefusedCount() const { return MouseRefusedCount; }

	// ── Запрет/разрешение кнопок (латч +609). Эталон: `uiPanel_HideTab`/`uiPanel_ShowTab`. ──
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetTabEnabled(int32 TabIndex, bool bEnabled);

	// Эталонные `uiPanel_DestroyChildControls` (false) / `uiPanelClearAllTabFlags` (true).
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetAllButtonsEnabled(bool bEnabled);

	// Программно «нажать» кнопку — проходит ровно тот же путь, что клик мышью (67073/67074).
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void ClickButton(int32 ButtonIdNum);

	// ── Плашка-подпись 73421 (эталон: текст ставится `CControl_SetText`, гасится сообщением 0xA) ──
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetMessageText(const FString& Text);   // пустая строка = спрятать (эталонный путь 0xA)

	// ── Указатель обучения 73422. Саму систему обучения делает ДРУГАЯ система — это её точка входа. ──
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void SetTutorialArrowVisible(bool bVisible);

	UFUNCTION(BlueprintPure, Category="Asda|SysMenu")
	bool IsTutorialArrowVisible() const { return bArrowVisible; }

	// Перезапустить обе покадровые анимации указателя (шестерня 300 мс + подсветка 200 мс).
	UFUNCTION(BlueprintCallable, Category="Asda|SysMenu")
	void RestartArrowAnimations();

	// ── Точка подключения будущих окон ──
	UPROPERTY(BlueprintAssignable, Category="Asda|SysMenu")
	FAsdaMenuCommand OnMenuCommand;

	// Диагностика: дамп КАЖДОГО разобранного контрола + состояние модели (закон full-structural-analysis).
	FString DumpControls() const;

	// Показать подсказку кнопки (эталонное сообщение 8 + таблица нотисов).
	void ShowButtonTip(int32 ButtonIdNum);
	// Скрыть подсказку и плашку (эталонное сообщение 0xA).
	void HideTip();

	// Найти запись кнопки по IDNum / по индексу в таблице. nullptr если нет.
	const FAsdaSysMenuButton* FindButton(int32 ButtonIdNum) const;
	int32 ButtonCount() const { return Buttons.Num(); }
	const FAsdaSysMenuButton& ButtonAt(int32 Index) const { return Buttons[Index]; }

	// ═══════════════ НАСТРОЙКИ (редактируются в UE) ═══════════════

	// Тинты кадров кнопок-тогглов (см. `FAsdaSysMenuTints`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu")
	FAsdaSysMenuTints Tints;

	// Звук «окно открыли» (эталон `uiWindowMsgProc` при 67073) и звуки разворота/сворачивания.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu")
	FName SoundOpen = TEXT("S_e_button06");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu")
	FName SoundCollapse = TEXT("S_e_button07");

	// Путь к ассету окна. Правится в редакторе (правило `ue-editor-friendly`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Asda|SysMenu")
	FString WindowAssetPath = TEXT("/Game/AsdaAssets/UI/Player_UI/HUD/WBP_SYSTEMMAIN_NEW_WIN.WBP_SYSTEMMAIN_NEW_WIN_C");

private:
	// Собрать таблицу кнопок по эталону (зовётся один раз в Initialize).
	void BuildButtonTable();

	// Открыть окно и разобрать дерево (аналог `CSystemMainWnd_InitControls`). false = ещё не готово.
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();

	// Полный 4-кадровый стиль чекбокса из тинтов скина (лечение дефекта Д6).
	void ApplyCheckStyles();
	// Применить к виджету текущее состояние латчей (checked) — эталонный `CControl_SetVisibleState`.
	void ApplyCheckedVisual(FAsdaSysMenuButton& B);
	// Единственная точка смены латча +608: пишет состояние и применяет визуал.
	//   Через неё идут ВСЕ пути (вывод из окна, `SetTabChecked`, `SetButtonChecked`).
	void ApplyChecked(FAsdaSysMenuButton& B, bool bChecked, const TCHAR* Why);

	// ★★★ ЕДИНОЕ МЕСТО, откуда берётся подсветка кнопок: фактическая видимость их окон.
	//   bForce = применить безусловно (после пересборки виджета панели).
	void SyncCheckedFromWindows(bool bForce = false);
	// Окно кнопки сейчас на экране? false и для неподключённых кнопок.
	bool ObserveWindowOpen(const FAsdaSysMenuButton& B) const;
	// Изменяемый доступ к записи (для внутренних путей).
	FAsdaSysMenuButton* FindButtonMutable(int32 ButtonIdNum);

	// Приложить текущее «развёрнуто/свёрнуто» к видимости контролов.
	//   bIncludePick — прятать ли 73410 (в `ShowHide` да, в `SetExpanded` эталон забывает — Д4).
	void ApplyExpandedVisibility(bool bIncludePick);

	// Клик по чекбоксу (эталон `checkBox_OnToggle` → 67073/67074 → диспетчеры).
	void OnButtonToggled(int32 TableIndex, bool bChecked);
	// ★ fishfix 09.10: клик МЫШЬЮ по чекбоксу — сперва гейт «курсор над окном выше панели» (эталон
	//   `uiWindowMgrDispatchMouse`: мышь только верхнему корню), затем `OnButtonToggled`.
	void OnButtonToggledByMouse(int32 TableIndex, bool bChecked);
	// ★ fishfix 09.10: курсор над окном выше панели 73400 или над чужим всплывающим слоем (и ключ отката выключен);
	//   `What` — что нажато (для журнала).
	bool IsMouseOverHigherWindow(const TCHAR* What) const;
	mutable int32 MouseRefusedCount = 0;   // ★ R5-fix8-b 14.11.2026: отказов мыши панели (`GetMouseRefusedCount`), только наблюдение
	// ★ fishfix2 09.10 (МЕЛК-2): то же условие без записи в журнал — для опроса наведения каждый кадр (`TickHover`).
	bool IsPanelMouseBlocked() const;
	// Кнопки свернуть/развернуть (эталонное сообщение 0x10401 в `uiWindowMsgProc`).
	void OnCollapseButton(int32 Which);   // 0 = 73425 развернуть, 1 = 73426 свернуть

	// Гейты диспетчера (эталон): чат-ввод в фокусе, режим перетаскивания предмета, режим панелей.
	bool IsTextInputFocused() const;

	// Опрос наведения (у `UCheckBox` нет UMG-событий hover) — эталонные сообщения 8 / 9 / 0xA.
	void TickHover();

	// Сохранение/восстановление свёрнутости (эталон писал «SMO» в WinState.bin и терял её — Д1).
	void SaveState() const;
	void LoadState();

	void PlayUiSound(FName Name) const;
	// Локализованная строка нотиса (эталон `UI_FormatLocalizedNotice`).
	FString Notice(int32 LocId) const;

	// ── состояние ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> MenuWidget = nullptr;
	UPROPERTY(Transient) TArray<FAsdaSysMenuButton> Buttons;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;

	UPROPERTY(Transient) TObjectPtr<UButton>     BtnExpand = nullptr;    // 73425 (скин MainQuick_Up)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnCollapse = nullptr;  // 73426 (скин MainQuick_Down)
	UPROPERTY(Transient) TObjectPtr<UPanelWidget> MessageGroup = nullptr; // группа плашки 73421
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  MessageText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UPanelWidget> ArrowGroup = nullptr;   // группа указателя 73422
	UPROPERTY(Transient) TObjectPtr<UImage>      ArrowImage = nullptr;
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaAnimatedPlane>> ArrowAnims;
	UPROPERTY(Transient) TObjectPtr<UWidget>     RootMarker = nullptr;    // TextBlock корня 73400
	UPROPERTY(Transient) TObjectPtr<UWidget>     RootHitZone = nullptr;   // `SYSTEMMAIN_NEW_WIN_0_HIT`

	// Предикаты «окно на экране» для целей вне оконного менеджера (см. вторую `RegisterWindow`).
	//   Ключ — IDNum кнопки. TFunction нерефлексируем, поэтому лежит отдельно от таблицы.
	TMap<int32, TFunction<bool()>> WindowOpenOverrides;

	// Сколько кадров даётся окну на реакцию после нажатия кнопки, прежде чем подсветка будет
	//   приведена к факту. Окна порта открываются отложенно (колбэк меню прилетает изнутри
	//   обработчика чекбокса), одного кадра мало — берём 3.
	static constexpr int32 kSettleFrames = 3;

	bool bControlsReady = false;
	bool bWindowShown   = true;
	bool bExpanded      = true;    // эталонный флаг +14; InitControls заканчивается ShowHide(1,0)
	bool bArrowVisible  = false;   // JUI Visible="0"
	bool bStateLoaded   = false;
	int32 TipShownFor   = -1;      // IDNum кнопки, чья подсказка сейчас показана (−1 = нет)
};
