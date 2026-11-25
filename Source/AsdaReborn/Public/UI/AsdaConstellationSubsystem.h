// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaConstellationSubsystem — ОКНО «ЗНАК ЗОДИАКА»: `Constellation_S_WIN` (JUI IDNum **74500**).
//   Открывается кнопкой диалога NPC **30073** (`case 30073` в `CGameUI_HandleControlCommand
//   @0xD87590`, `game_main.c:10297`) → `uiUpgradeWndOpen@0xECBED0` (`ui_core.c:86886`).
//   Полный реверс с адресами и побайтовыми раскладками — `Reports/npc_button_30073.md`.
//
// ═════════ ★★★ ЧТО ЭТО ЗА ОКНО НА САМОМ ДЕЛЕ (подпись «апгрейд» — ЛОЖЬ) ════════════════════════
//   Прежняя ветка порта отвечала «окно апгрейда (вариант B)». Это НЕВЕРНО и опровергнуто телом:
//   `uiUpgradeWndOpen` показывает контрол **74500**, чьи idc в диагностических `MessageBoxA`
//   эталона — `Constellation_S_WIN` / `Constellation_S_SEL` / `Constellation_S_TEXT` /
//   `Constellation_S_TEXTMOVE` / `Constellation_S_Xbtn` (`CConstellationSelWin_InitUI@0xECBB50`).
//   Подписи окна — нотисы 2580 «Сменить знак зодиака», 2583 «Знак зодиака выбирается раз и
//   навсегда», заголовок списка 110801 «Знак зодиака». Список наполняется из
//   **`./Data/Constellation.BIN`** (`ConstellationTable_Load@0xF1A280`) — 12 знаков зодиака
//   (Овен…Рыбы). Никакой заточки, уровня предмета или «апгрейда» тут нет ВООБЩЕ.
//   Миснеймы эталона, снятые здесь: `uiUpgradeWndOpen`, `uiUpgradeWndButtonHandler`,
//   `netSendMaskedPacket_op6586`, `itemGradeNotice_HandleResult`, `g_nConstellationCount`
//   (это ЧИСЛО знаков, а не «созвездий сервера»), `OP_RECV_USER_STAR_POINT_UPDATE`
//   («star point» = знак зодиака, а не очки), `TW_Serial_OKbtn` / `TW_Serial_text_1`
//   (копипаст-имена в `MessageBoxA` у контролов 74504/74505 — они из окна TW_Serial).
//
// ═════════ ЗАЧЕМ ЗНАК НУЖЕН ═════════════════════════════════════════════════════════════════════
//   Знак живёт в `CCharacter+0x460` (char+1120, u8; проверено дизассемблером
//   `movzx eax, byte ptr [eax+0x460]` @0xECBEE6) и приходит с сервера в 1017 тело[72] и в
//   снапшотах появления 4005/4065. Он показывается:
//     • в своём окне характеристик 900 (`UI_FillEquipDetailPanel`, `ui_core.c:11282`);
//     • в окне чужого персонажа (`CCharInfoWnd_UpdateInfo@0xF092A0`, метка 110631);
//     • в паре со «ВЛЕЧЕНИЕМ» (`uiFormatPartyRelationNotice@0xF1A520`, нотисы 110632/110633/110634)
//       — совместимость двух знаков берётся из **`./Data/Constellation_Match.BIN`**
//       (`ConstellationMatchTable_Load@0xF1A3D0`, записи по 5 б: свой знак · «максимальное
//       влечение» · «сильное влечение» · месяц · день).
//   Знак выбирается ОДИН РАЗ: и клиент (гейт открытия), и сервер (`Recv_UserStarPointUpdate
//   @0x539210`, ветка `LOBYTE(v7->secondaryCurrency)`) отказывают, если он уже стоит.
//
// ═════════ ДЕРЕВО КОНТРОЛОВ (`gamemain_jui.ini:350108…350600`) ══════════════════════════════════
//   74500 `Constellation_S_WIN`      скин `Black_BottomRound_White2Border` 208×304, Visible=0,
//                                    центрируется (AlignX 16/50/50, AlignY 32/50/50), 9 планов
//     ├ `WinSm_CBar` 102×24 @(53,−22) + `WinSm_LBar`(+`Boder_Bullet_Bolt1`) + `WinSm_RBar` — шапка
//     ├ `Win_Tab` 200×296 @(−1,9)
//     │   ├ 74502 `Constellation_S_TEXT`      `NoImage` 186×100 @(8,152), TextAlign 0 → нотис 2583
//     │   └ 74501 `Constellation_S_SEL`       `Bk_ComboBox` 140×20 @(31,18) → caption нотис 110801
//     ├ 74503 `Constellation_S_TEXTMOVE`      `NoImage` 208×24 @(0,−21), TextAlign 5 → нотис 2580;
//     │                                       ★ это РУЧКА ПЕРЕТАСКИВАНИЯ (EVS EVT=6 RET=0x20003)
//     ├ 74509 `Constellation_S_Xbtn`          `CloseBtn_WinSm` 13×13 @(−22,−15)
//     ├ `Btn_CBase` @(110,−44) → 74505 `Constellation_S_XXbtn` `RdBtn_W100` 72×23 → нотис 2424
//     └ `Btn_CBase` @(28,−44)  → 74504 `Constellation_S_OK_btn` `WkBtn_W72`  72×23 → нотис 2423
//   ⚠ ОК слева (x=28), «Отмена» справа (x=110) — в оригинале именно так.
//
// ═════════ КНОПКИ (1:1 `uiUpgradeWndButtonHandler@0xECBFD0`, дизасм 0xECBFD0…0xECC084) ══════════
//   Гейт сообщения: `a3 == 0x10401` (66561) — клик. Дальше `switch (a2)`:
//     0x12308 (74504, ОК)  → `sub_91FE14` = СПРЯТАТЬ ОКНО, затем прочитать выбор списка,
//                            `sel = индекс + 1`; при `sel < 1` — нотис 2582 и ВЫХОД (пакет не идёт),
//                            иначе `netSendMaskedPacket_op6586`;
//     0x12309 (74505, Отмена) → `sub_91FE14` (спрятать);
//     0x1230D (74509, ×)      → `sub_91FE14` (спрятать).
//   Других веток НЕТ: ни Esc, ни колёсика, ни двойного клика окно не обрабатывает.
//
// ═════════ ПАКЕТЫ ══════════════════════════════════════════════════════════════════════════════
//   C→S **6586** (29 б = 8 секрет + 16 MD5 + 5 тело: i32 userIdx @0 · u8 знак @4).
//   S→C **6587** (тело 6 б: u8 код @0 · i32 charId @1 · u8 знак @5).
//   Коды результата (`itemGradeNotice_HandleResult@0xECC6F0` + два отправителя сервера):
//     1  — успех: знак записан, окно 900 обновлено, нотис 2584 «Твой знак зодиака: [ss1]»;
//     2  — знак уже стоял, нотис 2581;
//     98 — нотис 117 (⚠ ни один отправитель GameServer этот код НЕ шлёт — мёртвая ветка клиента);
//     99 — ошибка БД (`Recv_DBStarPointUpdate DB Error`), нотис 117.
//   Раскладки — у объявлений в `AsdaNetSubsystem.h`.
//
// ═════════ НУМПАД ═════════════════════════════════════════════════════════════════════════════
//   ★ ОКНО ВВОДА КОЛИЧЕСТВА 1410 В ЭТОЙ ФИЧЕ НЕ УЧАСТВУЕТ. Доказано ДВУМЯ способами:
//     (1) перебором всех семи функций класса окна (0xECBAD0 ctor, 0xECBB50 init, 0xECBED0 open,
//         0xECBFA0 hide, 0xECBFD0 buttons, 0xECC0C0 send, 0xECC6F0 recv) — вызова нет;
//     (2) полным перечнем ВСЕХ вызывающих `uiOpenContextPopupWithHandlers@0xDA55A0` во всём
//         мастер-декомпиле (47 390 функций, 22 разных вызывающих) — ни одна из них не лежит в
//         диапазоне класса 0xECBAD0…0xECC6F0.
//
// ═════════ ЗВУКИ И ЭФФЕКТЫ ════════════════════════════════════════════════════════════════════
//   ★ СВОИХ ЗВУКОВ И РОЛИКОВ У ОКНА НЕТ. Доказано: (1) в семи функциях класса нет ни одного вызова
//   звуковой подсистемы (дизассемблированы все три интерактивные — 0xECBED0/0xECBFD0/0xECC6F0);
//   (2) в `.JUI`-дереве окна нет ни одного плана с `Animate="1"`. Единственный звук пути — «error»
//   общего модального окна `ONEBUT_MSGBOX`, его ставит `UAsdaWindowSubsystem::ShowGameMessage`.
//
// ═════════ ДЕФЕКТЫ ОРИГИНАЛА ══════════════════════════════════════════════════════════════════
//   D1. ★ ОТКРЫТИЕ ПРЕДВЫБИРАЕТ ПЕРВЫЙ ЗНАК, НО ПОКАЗЫВАЕТ ЗАГЛУШКУ. Дизасм 0xECBF23:
//       `push 0; call SetSelectedItem` — список получает выбранный индекс **0** (Овен), а сразу
//       следом caption перетирается нотисом 110801 «Знак зодиака». Игрок видит «ничего не выбрано»,
//       но ОК без единого клика отправит Овна — а выбор НЕОБРАТИМ. Побочно ветка нотиса 2582
//       («Знак зодиака не выбран») становится недостижимой. Чиним: по умолчанию НЕ предвыбираем,
//       и 2582 начинает работать. Откат к 1:1 — `asda.Constellation.PreselectFirst 1`.
//   D2. Кнопка ОК ПРЯЧЕТ ОКНО ДО проверки выбора: при ошибке 2582 окно уже закрыто, и вернуться
//       к списку можно только заново через NPC. Воспроизведено 1:1 (порядок вызовов сохранён),
//       потому что при исправленном D1 путь «ОК без выбора» и так почти не встречается.
//   D3. `sub_91FE14` только СКРЫВАЕТ окно (`CControl_SetVisible(win, 0)`), но НЕ снимает его из
//       списка менеджера окон, куда открытие его положило (`CClass_MapInsert`). Для порта
//       безвредно: у нас видимостью и z-порядком владеет `UAsdaWindowSubsystem`.
//   D4. Клиент обрабатывает код результата 98, которого ни один отправитель GameServer не шлёт.
//       Воспроизведено 1:1 (тот же нотис 117), помечено в отчёте.
//   D5. `findRecord5ByFirstByte@0xF1A770` (таблица влечения) берёт ПЕРВУЮ запись со своим знаком и
//       НЕ смотрит на поля месяца/дня, хотя в `Constellation_Match.BIN` лежат 5 недельных блоков
//       (12 × 5 = 60 записей, даты 10/7, 10/14, 10/21, 10/28, 11/4). ⇒ «влечение» всегда считается
//       по первому блоку, а 48 записей остальных недель — мёртвый груз.
//       ★ РЕАЛИЗОВАНО ЗДЕСЬ (`MatchIndex`): по умолчанию 1:1 (первый блок), исправленный учёт
//       даты — за CVar `asda.Constellation.UseDate 1`. Обе ветки живут рядом, см. `FindMatchRecord`.
//   D7. При СВОЁМ знаке 0 (не выбран) поиск записи не находит ничего и эталон возвращает индекс 2 —
//       окна зажигают картинку «Обычное», НО текст в буфер не пишется и остаётся ПУСТЫМ.
//       Воспроизведено 1:1 (`MatchIndex` возвращает 2 с пустым `OutText`).
//   D9. В блоках 10/14…11/04 есть САМОСОВПАДЕНИЯ (Телец→Телец, Рак→Рак, Скорпион→Скорпион,
//       Рыбы→Рыбы, Весы→Весы). Это дефект ДАННЫХ, а данные — вещественное доказательство:
//       переносим 1:1, помечаем колонкой `SelfMatch`, НЕ чиним. В оригинале он не виден вовсе,
//       потому что D5 не пускает игру дальше блока 10/07.
//
// ═════════ ВЛЕЧЕНИЕ («совместимость знаков») ═══════════════════════════════════════════════════
//   `uiFormatPartyRelationNotice@0xF1A520` (`AsdaStory_game.c:538137`) — имя МИСНЕЙМ, к партии
//   отношения не имеет. Тело 1:1 (порт — `MatchIndex`):
//     нет буфера / нет локального игрока / нет записи → return 2 БЕЗ текста;
//     rec[1] == чужой знак → текст 110632, return 0;
//     rec[2] == чужой знак → текст 110633, return 1;
//     иначе                → текст 110634, return 2.
//   Считается ОТ МОЕГО знака (`LocalPlayer+0x460`), симметрия НЕ гарантирована — симметричен
//   только блок 10/07, остальные четыре нет.
//   Два потребителя индекса, оба гасят ВСЕ ТРИ картинки и зажигают РОВНО ОДНУ:
//     `soulmateWin_RefreshList@0xE72EE0`  (`:477889`) — строка списка соулмейтов;
//     `CPlayerInfoWin_SetData@0xDA7E30`   (`:401353`) — окно чужого персонажа.
//   Связывание с этими окнами делает оркестратор: подсистема отдаёт только индекс и текст.
//
// ═════════ КТО ЗОВЁТ КНОПКУ 30073 ═════════════════════════════════════════════════════════════
//   Ветка ЖИВАЯ (в отличие от 30034). В импортированных данных `.NSR` (`NpcNsrButton.json`,
//   156 строк) код 30073 встречается РОВНО ОДИН раз: сценарий **`soul_statue`**, кнопка меню
//   индекс 1, подпись «Select constellation», условие `Opcode=1087`, без требований по уровню,
//   классу, квестам и предметам. Владелец сценария — **NPC 106 «Статуя души»**
//   (`NpcNsrHeader.json`: NsrName=soul_statue, NpcId=106, MenuCount=2). Вторая кнопка того же
//   NPC — 30023 (соулмейт).
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает
#include "Engine/DataTable.h"    // FTableRowBase — строка таблицы влечения
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaUiInputTypes.h"   // ★ R1: FAsdaMouseFrame / FAsdaDragArm / FAsdaDragGhost … — поля ввода (без всего AsdaUiHelpers.h)
#include "AsdaNetQueue.h"          // TAsdaNetQueue — очередь «колбэк копит, Tick применяет»
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaHeldTables.h"     // FAsdaHeldTables — удержание DT_Constellation на сессию (15.09.2026, dt_gc_hold, хвост ревизии п.2)
#include "AsdaConstellationSubsystem.generated.h"

// ═══════════════════════════════════════════════════════════════════════════════════════════════
//  СТРОКА ТАБЛИЦЫ ВЛЕЧЕНИЯ — импорт `./Data/Constellation_Match.BIN` (320 б, магия "CTM").
//  Загрузчик оригинала: `ConstellationMatchTable_Load@0xF1A3D0` (`AsdaStory_game.c:538111`) —
//    шапка 20 б, `pMgr[172]` = count (60), `pMgr[173]` = массив, запись РОВНО 5 байт:
//      [0] мой знак · [1] «максимальное влечение» · [2] «сильное» · [3] месяц · [4] день.
//  60 записей = 5 недельных блоков по 12 знаков (10/07, 10/14, 10/21, 10/28, 11/04, шаг 7 дней) —
//    таблица задумана как еженедельный «гороскоп», обновляемый патчем.
//  Раскладка и полный дамп — `Reports/constellation_full.md` §3.3.
//  Собирается `Tools/asda_build_constellation_match_dt.py` из `RawAssets/Data/Constellation_Match.BIN`
//    в `/Game/AsdaAssets/Data/DT_ConstellationMatch` (промежуточный CSV — `RawAssets/Data/ConstellationMatch.csv`).
//  ★ ЗНАЧЕНИЯ В КОДЕ НЕ ДУБЛИРУЮТСЯ (закон «данные только из таблиц»): и состав, и порядок,
//    и даты блоков приходят ТОЛЬКО отсюда — в проекте планируется редактор этих данных.
// ═══════════════════════════════════════════════════════════════════════════════════════════════
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaConstellationMatchRow : public FTableRowBase
{
	GENERATED_BODY()

	// Порядковый номер записи В ФАЙЛЕ (0…59). ★ ПОРЯДОК ЗНАЧИМ: 1:1-поиск эталона
	//   (`findRecord5ByFirstByte@0xF1A770`) берёт ПЕРВУЮ запись с нужным знаком, то есть блок 10/07.
	//   Строки `UDataTable` неупорядочены, поэтому порядок файла хранится явно.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 Order = 0;

	// [0] МОЙ знак 1…12 — ключ поиска (`байт[0]` записи).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 MySign = 0;

	// [1] знак «МАКСИМАЛЬНОГО влечения» → возврат 0, нотис 110632, картинка img[0].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 BestSign = 0;

	// [2] знак «СИЛЬНОГО влечения» → возврат 1, нотис 110633, картинка img[1].
	//   Всё остальное (10 знаков из 12) — «обычное»: возврат 2, нотис 110634, картинка img[2].
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 StrongSign = 0;

	// [3] МЕСЯЦ блока. ★ ДЕФЕКТ ОРИГИНАЛА D5: эталон это поле НЕ читает нигде.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 Month = 0;

	// [4] ДЕНЬ блока. Там же, D5.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	int32 Day = 0;

	// ★ НАША служебная пометка (в файле её НЕТ): запись, где знак совпадает сам с собой
	//   (Телец→Телец 10/14; Рак→Рак, Скорпион→Скорпион, Рыбы→Рыбы 10/21; Весы→Весы 10/28).
	//   Это дефект ДАННЫХ оригинала D9 — НЕ чиним, данные переносим 1:1, только помечаем.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак")
	bool SelfMatch = false;

	// Имена знаков — только для чтения таблицы глазами в редакторе; код их НЕ использует
	//   (имя берётся из `Constellation.BIN` через `SignName`, чтобы источник был ОДИН).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак") FString MyName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак") FString BestName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Зодиак") FString StrongName;
};

class UUserWidget;
class UWidget;
class UButton;
class UTextBlock;
class UAsdaButtonRelay;
class UAsdaJuiComboBox;

UCLASS()
class ASDAREBORN_API UAsdaConstellationSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaConstellationSubsystem, STATGROUP_Tickables); }
	// ★ R4-b (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ── IDNum контролов окна (`CConstellationSelWin_InitUI@0xECBB50`) ──
	static constexpr int32 kIdWin      = 74500;   // `Constellation_S_WIN`
	static constexpr int32 kIdSel      = 74501;   // `Constellation_S_SEL`      — список знаков
	static constexpr int32 kIdText     = 74502;   // `Constellation_S_TEXT`     — нотис 2583
	static constexpr int32 kIdTitle    = 74503;   // `Constellation_S_TEXTMOVE` — нотис 2580 + ручка
	static constexpr int32 kIdBtnOk    = 74504;   // `Constellation_S_OK_btn`   — нотис 2423
	static constexpr int32 kIdBtnCancel= 74505;   // `Constellation_S_XXbtn`    — нотис 2424
	static constexpr int32 kIdBtnClose = 74509;   // `Constellation_S_Xbtn`
	// Z-порядок в семье «окна от NPC» (инвентарь 720, магазин 730, ремонт 731, витрина 732).
	static constexpr int32 kWinZ       = 733;

	// ═══ ОТКРЫТИЕ / ЗАКРЫТИЕ ═══
	/** Порт `uiUpgradeWndOpen@0xECBED0`. false — знак уже стоит (показан нотис 2581) либо окна нет. */
	UFUNCTION(BlueprintCallable, Category="Asda|Зодиак")
	bool OpenWindow();

	/** Порт `sub_ECBFA0` = `CControl_SetVisible(win, 0)` — просто спрятать окно. */
	UFUNCTION(BlueprintCallable, Category="Asda|Зодиак")
	void CloseWindow();

	UFUNCTION(BlueprintPure, Category="Asda|Зодиак")
	bool IsOpen() const { return bOpen; }

	// ★ R5-fix-b 13.11.2026 (второй заход): только для самотеста `asda.Constellation.SelfTestFramePlanes` — дерево окна и собранные планы рамки.
	UUserWidget* DebugWidget() const { return Widget; }                                        // виджет окна 74500 (или nullptr)
	const TArray<TObjectPtr<UWidget>>& DebugFramePlanes() const { return FramePlanes; }        // итог `CollectByIdc(kIdcWin)`

	// ═══ ДЕЙСТВИЯ (в эталоне — ветки `uiUpgradeWndButtonHandler`) ═══
	void OnOkClicked();       // 74504
	void OnCancelClicked();   // 74505 и 74509 — одна и та же ветка эталона

	/** Выбрать пункт списка по индексу 0…N−1 (тот же путь, что и клик мышью по строке). */
	UFUNCTION(BlueprintCallable, Category="Asda|Зодиак")
	void SelectIndex(int32 Index);

	/** Текущий выбор в виде ЗНАКА 1…12 (0 = не выбрано) — то же, что эталонное поле `this+0x20`. */
	UFUNCTION(BlueprintPure, Category="Asda|Зодиак")
	int32 GetSelectedSign() const;

	// ═══════════════ ВЛЕЧЕНИЕ (совместимость знаков) — КОНТРАКТ ДЛЯ ОКОН ═══════════════════════
	//   Три метода ниже — ВСЁ, что нужно окну соулмейта (`soulmateWin_RefreshList@0xE72EE0`) и окну
	//   чужого персонажа (`CPlayerInfoWin_SetData@0xDA7E30`). Сами окна их только зовут.

	/** Название знака 1…12 из `Constellation.BIN` (таблица `DT_Constellation`).
	 *  Порт `cfgFindRecordByKey65@0xF1A670`: линейный поиск по байту[0], возврат = строка записи.
	 *  0 (знак не выбран) или ключ, которого нет в таблице → ПУСТАЯ строка. */
	UFUNCTION(BlueprintPure, Category="Asda|Зодиак")
	FString SignName(uint8 SignId) const;

	/** Совместимость МОЕГО знака с чужим. Порт `uiFormatPartyRelationNotice@0xF1A520` (миснейм:
	 *  к партии отношения не имеет, это «влечение»).
	 *  Возврат: 0 = максимальное (нотис 110632), 1 = сильное (110633), 2 = обычное (110634);
	 *           −1 = ДАННЫХ НЕТ (таблица `DT_ConstellationMatch` не собрана) — этого состояния
	 *           в оригинале не бывает, окно в таком случае не должно зажигать ни одну картинку.
	 *  `OutText` — готовая loc-строка. ★ ДЕФЕКТ D7 1:1: если МОЕГО знака нет в таблице (в т.ч.
	 *  когда он 0 = не выбран), возврат 2, но текст ПУСТОЙ — ровно как в эталоне.
	 *  Эталон берёт свой знак у локального игрока сам; здесь он приходит параметром, чтобы окна
	 *  могли считать влечение и для чужой пары (например, в дампе-самопроверке). */
	int32 MatchIndex(uint8 MySign, uint8 OtherSign, FString& OutText) const;

	/** Мой знак: зеркало `CCharacter+0x460` = `UAsdaNetSubsystem::SelfConstellation`
	 *  (приходит в 1017 тело[72], обновляется приёмом 6587 кодом 1).
	 *  0 = не выбран; 0xFF («сервер ещё не присылал») тоже отдаётся как 0. */
	UFUNCTION(BlueprintPure, Category="Asda|Зодиак")
	uint8 SelfSign() const;

	// ★ УДЕРЖАНИЕ `DT_Constellation` (15.09.2026, отчёт dt_gc_hold, хвост ревизии п.2). Список окна (`SignNames`) собирается
	//   один раз при `Initialize`, но имя знака в игре ещё читает `FAsdaCharRecord::ConstellationName` →
	//   `FAsdaText::Get("Constellation")` (окно 900 при каждом `RefreshBasicPage`, запас в приёме 6587). Там кэш СЛАБЫЙ, и после
	//   выгрузки GC промах `FAsdaText::FindTable` заново перебирает категории (5 пустых `LoadObject` до `World`).
	//   Держим сильно на сессию, лениво в ИГРОВЫХ местах первого использования (окно 900, приём 6587). На экране логина
	//   (та же цепочка у выбора/создания персонажа) не зовётся. Путь — `kSignTablePath` этой подсистемы
	//   (тот же ассет, что находит `FAsdaText::FindTable`; сверяет стенд `asda.DtHold.GcCheck`). Возврат — 0/1.
	int32 HoldGameTables() const;
	const FAsdaHeldTables& GetHeldTables() const { return HeldTables; }

	// ═══ ДИАГНОСТИКА ═══
	FString DumpControls() const;   // КАЖДЫЙ разобранный контрол (закон full-structural-analysis)
	FString DumpState() const;      // список знаков, выбор, гейт, флаги
	FString DumpMatchMatrix() const;// матрица 12×12 влечения (консоль `asda.Constellation.MatchDump`)

private:
	// ── Жизненный цикл окна ──
	bool EnsureWidget();
	void ResetWidget();
	void SetupControls();
	void RegisterZone();
	/** Собрать ВСЕ виджеты одного контрола `.JUI` (сам виджет, планы `_<seq>_<plane>`, `_grp`,
	 *  капшн `_Text`, хит-зону `_HIT`) — гасить/показывать надо ВСЁ поддерево по префиксу. */
	void CollectByIdc(const FString& Idc, TArray<TObjectPtr<UWidget>>& Out) const;

	// ── Данные ──
	/** Порт цикла `for (i < g_nConstellationCount) cfgFindRecordByKey65(g_LoginCtxBuf, i + 1)`:
	 *  наполняет `SignIds` / `SignNames` из `DT_Constellation` (импорт `Constellation.BIN`). */
	void BuildSignList();

	/** Порт `ConstellationMatchTable_Load@0xF1A3D0`: читает `DT_ConstellationMatch` в `MatchRows`
	 *  и упорядочивает их по полю `Order` — ПОРЯДОК ФАЙЛА, на который опирается 1:1-поиск.
	 *  Ленивая и идемпотентная: зовётся из `Initialize` и из `EnsureMatchTable`. */
	void BuildMatchTable() const;

	/** Догрузить таблицу влечения, если её ещё нет (ассет мог быть собран после старта игры). */
	void EnsureMatchTable() const;

	/** Порт `findRecord5ByFirstByte@0xF1A770` + исправление дефекта D5.
	 *  ВЕТКА 1:1 (по умолчанию, `asda.Constellation.UseDate 0`): ПЕРВАЯ запись с этим знаком —
	 *    то есть всегда блок 10/07, а поля месяца/дня не читаются вовсе.
	 *  ВЕТКА ИСПРАВЛЕННАЯ (`asda.Constellation.UseDate 1`): блок выбирается по текущей дате.
	 *  nullptr — записи с таким знаком в таблице нет (в т.ч. знак 0). */
	const FAsdaConstellationMatchRow* FindMatchRecord(uint8 MySign) const;

	/** Индекс НЕДЕЛЬНОГО БЛОКА для исправленной ветки D5. Список блоков берётся из самих данных
	 *  (различные пары «месяц/день» в порядке файла), в коде дат НЕТ. */
	int32 CurrentBlockIndex() const;

	// ── Нотисы ──
	FString Loc(int32 LocId) const;
	/** Эталон: `CItem_BuildTooltipText(g_pItemTooltipUI, 0, −1, текст)` = `ONEBUT_MSGBOX` 1500. */
	void NoticeMsgBox(const FString& Text) const;
	void NoticeMsgBoxLoc(int32 LocId) const { NoticeMsgBox(Loc(LocId)); }

	// ── Сеть ──
	/** Порт `itemGradeNotice_HandleResult@0xECC6F0`. Тело — 6 байт пакета 6587. */
	void HandleResult(const TArray<uint8>& Body);

	// ── Состояние ──
	UPROPERTY(Transient) TObjectPtr<UUserWidget> Widget = nullptr;
	UPROPERTY(Transient) TObjectPtr<UAsdaJuiComboBox> Combo = nullptr;     // 74501
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtHint = nullptr;        // 74502 (нотис 2583)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtTitle = nullptr;       // 74503 (нотис 2580)
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtComboCaption = nullptr;// `Constellation_S_SEL_Text`
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnOk = nullptr;          // 74504
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtBtnOk = nullptr;       // подпись 74504 (нотис 2423)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnCancel = nullptr;      // 74505
	UPROPERTY(Transient) TObjectPtr<UTextBlock>  TxtBtnCancel = nullptr;   // подпись 74505 (нотис 2424)
	UPROPERTY(Transient) TObjectPtr<UButton>     BtnClose = nullptr;       // 74509
	UPROPERTY(Transient) TArray<TObjectPtr<UAsdaButtonRelay>> Relays;
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> FramePlanes;          // 9 планов рамки 74500

	bool bControlsReady = false;
	bool bOpen = false;
	/** Эталонное поле `this+0x20`: выбранный ЗНАК (индекс списка + 1); −1 при открытии. */
	int32 PendingSign = -1;
	/** Пакет 6586 отправлен, ждём 6587 (у эталона своего флага нет — окно уже спрятано). */
	bool bAwaitingResult = false;

	TArray<int32>   SignIds;     // ключи записей `Constellation.BIN` (1…12)
	TArray<FString> SignNames;   // имена знаков в том же порядке

	// Сильная ссылка на `DT_Constellation` на игровую сессию (см. `HoldGameTables`).
	UPROPERTY(Transient)
	mutable FAsdaHeldTables HeldTables;

	// ── Таблица влечения (`Constellation_Match.BIN` → `DT_ConstellationMatch`) ──
	//   `mutable`, потому что таблица догружается лениво из константных `MatchIndex`/`SignName`:
	//   ассет мог быть пересобран инструментом уже после старта игры (`AsdaDt::Cached` — слабая
	//   ссылка, ровно на это и рассчитан).
	mutable TArray<FAsdaConstellationMatchRow> MatchRows;   // ВСЕ 60 записей в порядке файла
	mutable TArray<FIntPoint> MatchBlocks;                  // (месяц, день) блоков в порядке файла

	FAsdaMouseFrame Mouse;                 // один кадр мыши на окно (общая точка)
	TAsdaNetQueue<TArray<uint8>> PendingResults;  // закон `no-reentrant-net-callbacks`: колбэк только КОПИТ
	FDelegateHandle HResult;
};
