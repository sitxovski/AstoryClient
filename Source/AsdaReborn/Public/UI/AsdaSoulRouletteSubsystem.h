// Copyright (c) 2026 @sitxovski (Discord)
// ═══════════════════════════════════════════════════════════════════════════════════════════════
// UAsdaSoulRouletteSubsystem — РУЛЕТКА СОУЛМЕЙТА: окно `GOLD_roullete_WIN` (67000) и опкоды
//   6575 «крутить» / 6576 «результат прокрутки» / 6579 «забрать приз» / 6580 «выдача приза».
//
// Источник: `Reports/soulmate_skills_recall_roulette.md` §C + прямой реверс
//   `CGoldRoulette_InitUI@0xE615E0`, `gachaWheel_UpdateSpinAnimation@0xE62020`,
//   `RoulettePanel_UpdateRewardSlots@0xE61930`, `roulettePanelHandleNetEvent@0xE6E480`,
//   `Recv_RouletteResult@0xE6E790`, `CSoulmate_LoadRouletteData@0xE62450`.
//
// ═══════════════════════ ПОЛНЫЙ СТРУКТУРНЫЙ РАЗБОР ОКНА 67000 (JUI) ════════════════════════════
//   Снято с `gamemain_jui.ini:310388…311100`. Колонки: idc · IDNum · Visible · NoSelect · Origin · Size · скин.
//   GOLD_roullete_WIN            67000  vis 0  nosel 0  (0,0)     5×5      скин GOLD_roullete   ← окно
//    ├ GOLD_roullete_main        67050  vis 1  nosel 0  (0,0)     256×256  NoImage2_icon        ← подложка,
//    │                                   у неё EVS EVT=6 RET=20003 CtrlID=67000 — РУЧКА ПЕРЕТАСКИВАНИЯ окна
//    ├ GOLD_roullete_gfont1      67011  vis 1  nosel 1  (97,52)   64×64    NoImage_icon         ← плашка ВВЕРХ
//    │   ├ GOLD_roullete_gfont1_m1 67021 vis 0 nosel 1 (14,33)   32×32    Extract_Icon_Effect  ← иконка предмета
//    │   └ (idc ПУСТ)            67015  vis 0  nosel 0  (0,0)     64×64    Extract_Icon_Effect  ← слой «забрать»
//    │                                  ★ скин сверен по gamemain_jui.ini:310481-310484 (Animate=1,
//    │                                    TotalFrame 16, FrameInterval 1000 — JuiSkin.ini:25956)
//    ├ GOLD_roullete_gfont2      67012  vis 1  nosel 1  (156,111) 64×64  … 67022 / 67016        ← плашка ВПРАВО
//    ├ GOLD_roullete_gfont3      67013  vis 1  nosel 1  (97,170)  64×64  … 67023 / 67017        ← плашка ВНИЗ
//    ├ GOLD_roullete_gfont4      67014  vis 1  nosel 1  (38,111)  64×64  … 67024 / 67018        ← плашка ВЛЕВО
//    ├ GOLD_roullete_arrow       67001  vis 1  nosel 0  (114,104) 26×40   GOLD_roullete_arrow   ← СТРЕЛКА (вращается)
//    ├ GOLD_roullete_btn         67002  vis 1  nosel 0  (107,124) 40×40   btn_go                ← «крутить»
//    ├ GOLD_roullete_btn2        67019  vis 0  nosel 0  (107,124) 40×40   btn_stop              ← «стоп» (скрыта)
//    ├ GOLD_roullete_ar2_UP_ani     67007 vis 0 nosel 1 (106,20)  40×40   ar_B_UP   ← + ребёнок GOLD_roullete_ani1 67031
//    ├ GOLD_roullete_ar2_right_ani  67008 vis 0 nosel 1 (212,124) 40×40   ar_B_RIGHT           (67032)
//    ├ GOLD_roullete_ar2_DOWN_ani   67009 vis 0 nosel 1 (106,227) 40×40   ar_B_DOWN            (67033)
//    ├ GOLD_roullete_ar2_left_ani   67010 vis 0 nosel 1 (3,125)   40×40   ar_B_LEFT            (67034)
//    ├ GOLD_roullete_ar2_UP      67003  vis 0  nosel 1  (111,28)  32×32   arrow2_UP
//    ├ GOLD_roullete_ar2_right   67004  vis 0  nosel 1  (212,128) 32×32   arrow2_RIGHT
//    ├ GOLD_roullete_ar2_DOWN    67005  vis 0  nosel 1  (111,226) 32×32   arrow2_DOWN
//    └ GOLD_roullete_ar2_left    67006  vis 0  nosel 1  (12,127)  32×32   arrow2_LEFT
//   ★ ПОРЯДОК ЧЕТВЁРОК. `CGoldRoulette_InitUI` берёт их циклом `i + 67003` / `i + 67007` /
//     `i + 67011` / `i + 67015` / `i + 67021`, то есть индекс i = 0..3 читается как
//     **ВВЕРХ · ВПРАВО · ВНИЗ · ВЛЕВО** (это подтверждают и Origin плашек: 52 / 111 / 170 / 111).
//   ★★★ У контролов 67015…67018 idc В JUI ПУСТ, НО `IDNum` У НИХ ЕСТЬ (67015…67018,
//     `gamemain_jui.ini:310484/310551/310618/310685`), и эталон адресует их именно по числу
//     (`CGoldRoulette_InitUI@0xE615E0`: `FindControlByID(mgr, i + 67015)`; строка ошибки в бинаре —
//     «67015 UI Error», то есть имени не было и у авторов игры).
//     Импортёр порта `IDNum` в виджет не переносит, а безымянный контрол называет ПО ИМЕНИ СКИНА
//     (`Tools/asda_jui_generic_umg.py`: `base = san(node.idc or node.name) + "_" + idx`), поэтому
//     опознаём их ПО СКИНУ **`Extract_Icon_Effect`** — в окне 67000 он встречается ровно 4 раза,
//     по одному на сектор. Прежняя запись «скин 67015 = NoImage2_icon» была ошибкой дампа.
//   Сопутствующее (НЕ в 67000):
//     6539  SOUL_WHELL              vis 0 nosel 1 (0,0) 199×66 скин WheelWin_Nomal — колесо на вкладке соулмейта 6400;
//   ★ ИСПРАВЛЕНО 06.10.2026 (волна 1-Б): «яблоки» живут НЕ во вкладке 6400, а в окне соулмейта
//     `SOULMATE_POP_WIN` 6500 (группа 6691 `Soulmate_AppleBase`, `gamemain_jui.ini:20335-20600`):
//     6693  Soulmate_GoldenApple    — «Золотое яблоко», ЕДИНСТВЕННЫЙ ручной отправитель 6575;
//     6692 / 6695 / 6698 / 6699     — Soulmate_Apple / _ShakeApple / _AppEff3 / `Soulmate_Glass` — якорь позиции окна.
//     Вся панель «Яблока Дружбы» (6049/6051) — `UAsdaSoulAppleSubsystem`.
//
// ═══════════════════════════ ПОТОК (доказан по эталону, 1:1) ═══════════════════════════════════
//   1. Игрок жмёт «Золотое яблоко» (6693) → `netSendAntiCheatRequest` (★ МИСНЕЙМ) = **6575**.
//      ★ Кнопка 67002 пакета НЕ ШЛЁТ: она лишь запускает локальную анимацию (social.c:8222-8229).
//   2. Сервер (`Recv_UserSoulRoulettet@0x49A1C0`) проверяет 7 гейтов и шлёт **6576** {код, сектор, след.строка}.
//   3. Код 1 → ★ если окна 67000 на экране нет, ОНО ПОКАЗЫВАЕТСЯ ИМЕННО ЗДЕСЬ (`roulettePanelHandleNetEvent`
//      → `RoulettePanel_UpdateRewardSlots(…, a3 = 1, a4 = 1)`: строка 0, звук «готово»; иначе — звук старта).
//      Это ЕДИНСТВЕННЫЙ путь открытия окна в игре (волна 1-Б, 06.10.2026: три вызова панели во всём образе,
//      скан E8/E9). Затем колесо крутится к углу `сектор × 90°` (0 → 360°) — `gachaWheel_UpdateSpinAnimation`.
//   4. Колесо встало → показывается плашка выпавшего сектора:
//        следующая строка == −1 → слой «забрать» (67015+i); иначе → анимационная плашка (67007+i).
//   5. КОНЕЦ АНИМАЦИИ плашки (сообщение окна a4 == 24):
//        67007..67010 → снова **6575** (розыгрыш — ЦЕПОЧКА строк таблицы, social.c:7957-7973);
//        67015..67018 → **6579** «забрать» (social.c:7975-7979).
//      ★★★ ЧЕМ ЗАДАН СРОК ЭТОГО СОБЫТИЯ (доказано 31.08.2026, ключа-костыля больше нет).
//        Показ плашки — `ctrlShowWithItemFlags(ctrl, 2)` (social.c:6864/6869 → ui_core.c:109888):
//        на плане 0 КОНТРОЛА ставится `Animate`, признак `EFrameStop` («доиграть и остановиться»)
//        и **лимит прогонов = 2**; тик `CControl_BuildPlaneVertices@0x12DE440` по исчерпании
//        лимита прячет контрол и поднимает событие 24. ⇒ срок = `FrameInterval` скина × 2:
//          67007…67010 — скины `GOLD_roullete_ar_B_UP/RIGHT/DOWN/LEFT`: 3 кадра, 700 мс ⇒ 1400 мс;
//          67015…67018 — скин `Extract_Icon_Effect` (JuiSkin.ini:25956): 16 кадров, 1000 мс ⇒ 2000 мс.
//        ⚠ ПОПРАВКА К ОТЧЁТУ `clan_soulmate_gaps.md` §4: скин `Ani_Soul2` (22 кадра / 1000 мс) —
//          это ДЕТИ 67031…67034 плашек, декоративные; тайминг они НЕ задают. И `EFrameStop` в
//          `gamemain_jui.ini` у ВСЕХ этих контролов = "0" — признак ставится в РАНТАЙМЕ.
//   6. Сервер выдаёт приз и шлёт **6580** (73 б): эффект `Roul_*.jeb`, звук из клиентской таблицы,
//      строка в чат — ТОЛЬКО победителю.
//
// ═══════════════════ ДВЕ ТАБЛИЦЫ ПРИЗОВ: КЛИЕНТСКАЯ И СЕРВЕРНАЯ ════════════════════════════════
//   Клиентская `Data\Soulmate_Roulette.bin` — ТОЛЬКО отображение (иконки, тексты, звуки).
//   Серверная `dword_5F5F60` (40 б/строка) — вероятности и фактическая выдача. **Авторитет — СЕРВЕР.**
//   ★ ДЕФЕКТ D6: текст приза эталон берёт из КЛИЕНТСКОЙ таблицы по ЛОКАЛЬНОМУ углу колеса, а
//     начисляет сервер ⇒ при расхождении таблиц «показал одно, дал другое». Воспроизводится 1:1;
//     ключ `asda.Soul.Roulette.TextFromServer 1` заставляет строить текст по виду приза из 6580.
//
// ★★★ РАСКЛАДКА КЛИЕНТСКОЙ ЗАПИСИ УТОЧНЕНА ПО САМОМУ ФАЙЛУ (отчёт §C.1 знал не всё):
//   заголовок 20 б = {версия f32 / **count i32** / extra i32 / тег «SRL» / pad} — ровно схема
//   всех `.BIN` этого клиента (`Tools/unpack_bbz.py`), в живом файле count = 10, записей 10 × 340 б;
//   запись: +0 i32 id · **+4+4·s i32 ВЕС сектора (сумма четвёрки = 100000)** · +20+4·s i32 тип приза
//   (0 пусто / 1 опыт / 2 предмет / 3 ОЗ-ОМ) · +36+4·s i32 значение · +52+4·s i32 количество ·
//   +68+4·s i32 включённость (≤ 0 ⇒ плашка скрыта) · +84+64·s char[64] базовое имя wav.
//   (Отчёт считал +4..+19 частью «id/заголовка» — на данных это ЧЕТЫРЕ ВЕСА, зеркало серверных.)
#pragma once

#include "CoreMinimal.h"
#include "AsdaTickGate.h"   // ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает
#include "Engine/DataTable.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
// ★ R1 (10.11.2026): включение UI/AsdaUiHelpers.h снято — заголовок им не пользовался.
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaSoulRouletteSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UAsdaNetSubsystem;

// ───────────────────────────────────────────────────────────────────────────────────────────────
// СТРОКА КЛИЕНТСКОЙ ТАБЛИЦЫ ПРИЗОВ — `Data\Soulmate_Roulette.bin`, запись 340 б.
//   Имя строки DataTable = id записи (+0). Всё, кроме отображения, здесь НЕ решается: розыгрыш и
//   начисление — на сервере (закон «авторитет = сервер»).
// ───────────────────────────────────────────────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct ASDAREBORN_API FAsdaRouletteRow : public FTableRowBase
{
	GENERATED_BODY()

	/** +0 i32 — id строки (он же ключ строки и значение поля «следующая строка» пакета 6576). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") int32 RowId = -1;

	/** +4+4·s i32 — вес сектора s (сумма четвёрки = 100000). Зеркало серверной таблицы;
	 *  клиент его НЕ разыгрывает — держим ради редактора данных и сверки с сервером. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<int32> Weights;

	/** +20+4·s i32 — тип приза: 0 пусто · 1 опыт · 2 предмет · 3 ОЗ/ОМ. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<int32> Types;

	/** +36+4·s i32 — значение: опыт (в тексте делится на 10) · itemId · процент ОЗ/ОМ. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<int32> Values;

	/** +52+4·s i32 — количество предметов (в тексте печатается как есть; >1 рисуется на иконке). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<int32> Counts;

	/** +68+4·s i32 — включённость сектора: ≤ 0 ⇒ плашка 67003+s СКРЫТА. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<int32> Enabled;

	/** +84+64·s char[64] — базовое имя звука приза (эталон дописывает «.wav»). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Рулетка") TArray<FString> Sounds;
};

// Состояние вращения колеса — порт `gachaWheel_UpdateSpinAnimation@0xE62020` (dword-поля CSoulmateWnd).
UENUM()
enum class EAsdaRouletteSpin : uint8
{
	Idle    = 0 UMETA(DisplayName = "0/1 — покой (эталонное +496 != 2..4)"),
	Start   = 2 UMETA(DisplayName = "2 — старт: посчитать полный путь"),
	Spin    = 3 UMETA(DisplayName = "3 — кручение с торможением"),
	Stopped = 4 UMETA(DisplayName = "4 — встали: показать плашку сектора"),
};

UCLASS()
class ASDAREBORN_API UAsdaSoulRouletteSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	/** Таблица призов — ТОЛЬКО DataTable (сырым `.bin` в `Content` не место, см. `SoulGuarderTable`). */
	static const TCHAR* kPrizeTablePath;     // DT_Soulmate_Roulette
	static const TCHAR* kWindowWbp;          // WBP_GOLD_roullete_WIN
	static const FName  kWindowId;           // "GOLD_roullete_WIN"

	/** Слой окна. Рулетка — окно-спутник вкладки соулмейта, поверх окна сообщества (у того 400). */
	static constexpr int32 kWinZ = 430;

	/** Секторов у колеса ровно четыре; угол сектора s = s × 90°, при s == 0 берётся 360°. */
	static constexpr int32 kSectors = 4;

	/** Эталонная стартовая скорость вращения: `*((_DWORD*)wnd + 497) = 1117782016` = 80.0f. */
	static constexpr float kSpinSpeed0 = 80.f;
	/** Полный путь = 360 − текущий + **1800** + целевой (пять полных оборотов «на разгон»). */
	static constexpr float kSpinExtra = 1800.f;
	/** Добавка к скорости, чтобы торможение не выродилось в ноль: `speed += 0.30000001f`. */
	static constexpr float kSpinFloor = 0.30000001f;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Находка Р5 отчёта 14_soulmate: подсистема жила на GameInstance и переживала
	// разрыв, смену персонажа и травел вместе со списками и защёлками сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Тик нужен колесу (вращение) и таймеру конца анимации плашки. Молчит, пока окно закрыто.
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaSoulRouletteSubsystem, STATGROUP_Tickables); }
	// ★ R4-c (14.11.2026): шаблон класса (CDO) не тикает — раньше `Tick` звался дважды за кадр, у рабочего объекта и у шаблона
	//   (ключ `asda.Perf.NoCdoTick`, разбор — `AsdaTickGate.h`). Рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** Открыть/закрыть окно рулетки (67000). В игре окно открывает ТОЛЬКО ответ 6576 код 1 (`OnSpinResult6576`);
	 *  консоль `asda.Soul.Roulette.Open` — диагностика. Закрытие — ESC (`kEscCloseWindows`) и ветка «забрать». */
	UFUNCTION(BlueprintCallable, Category="Asda|Рулетка") void OpenWindow();
	UFUNCTION(BlueprintCallable, Category="Asda|Рулетка") void CloseWindow();
	UFUNCTION(BlueprintPure,     Category="Asda|Рулетка") bool IsOpen() const;

	/** ★ Волна 1-Б: БОЛЬШЕ НИЧЕГО НЕ ПРИВЯЗЫВАЕТ. «Золотое яблоко» 6693 живёт в окне соулмейта 6500
	 *  (`SOULMATE_POP_WIN`, группа 6691), а не во вкладке окна 6400 — прежний поиск здесь не находил его
	 *  НИКОГДА. Клик ловит `UAsdaSoulAppleSubsystem` (вся панель «Яблока Дружбы»). Метод оставлен, потому
	 *  что его зовёт `UAsdaSoulmateSubsystem` (файл волны 1-А) — тело только пишет в лог. */
	void BindTabControls(UUserWidget* CommunityWindow);

	/** ★ Кнопка 67002 `GOLD_roullete_btn` (эталон `SoulmateUI_HandleButtonCommand@0xE69F90`, game.c:474286):
	 *  только при флаге «яблоко созрело» (+495, `UAsdaSoulAppleSubsystem`) — снять флаг и ЛОКАЛЬНО крутить
	 *  колесо (сессия = 1, угол 0, состояние 2, скорость 80, звук старта). Пакета НЕТ. Без флага — ничего. */
	void OnSpinButton67002();

	// ── Диагностика (самотест asda.UI.SelfTestWave1B) ──
	/** Окно 67000 собрано и ВИДНО (эталон `CControl+0x124 isVisible` окна). */
	bool IsWindowShown() const;
	/** Состояние колеса (эталон +496), углы стрелки/цели, строки, счётчики пакетов. */
	EAsdaRouletteSpin GetSpinState() const { return SpinState; }
	float GetArrowAngle() const { return CurAngle; }
	float GetTargetAngle() const { return TargetAngle; }
	int32 GetCurRowId() const { return CurRowId; }
	int32 GetNextRowId() const { return NextRowId; }
	int32 GetRx6576() const { return Rx6576; }
	int32 GetTx6575() const { return Tx6575; }
	int32 GetTx6579() const { return Tx6579; }
	/** Узлы окна по секторам (для проверки разметки): плашка-иконка 67011+s и стрелка-указатель 67003+s. */
	UWidget* GetGfontNode(int32 Sector) const { return Gfont.IsValidIndex(Sector) ? (UWidget*)Gfont[Sector].Get() : nullptr; }
	UWidget* GetPlateNode(int32 Sector) const { return Plate.IsValidIndex(Sector) ? Plate[Sector].Get() : nullptr; }
	UWidget* GetPlateAniNode(int32 Sector) const { return PlateAni.IsValidIndex(Sector) ? PlateAni[Sector].Get() : nullptr; }
	UWidget* GetSpinButton() const;
	/** ★ Ревизия М-3: позиционирование к якорю 6699 ещё ждёт отрисовки; куда встала подложка 67050 (абс.). */
	bool IsAnchorPending() const { return bAnchorPending; }
	FVector2D GetAnchorTarget() const { return AnchorTarget; }

	/** Ручной пуск прокрутки (порт `case 0xB`, `a3 == 6693`): отправка 6575. */
	UFUNCTION(BlueprintCallable, Category="Asda|Рулетка") bool Spin();

	/** Строка таблицы призов по id (эталон `findEntryById340@0xE62690`). */
	const FAsdaRouletteRow* GetRow(int32 RowId) const;

	FString DumpState() const;

private:
	void LoadTable();

	// ── Окно ──
	bool BindWindowControls();
	void ReleaseWindow();
	/** Порт `RoulettePanel_UpdateRewardSlots@0xE61930`: перерисовать 4 плашки по строке `RowId`.
	 *  `bPlayReady` — ветка `a3 == 1` (звук `S_e_sm_ro_ready_00.wav` + позиционирование окна);
	 *  `bVisible`   — ветка `a4` (показать/скрыть само окно). */
	void UpdateRewardSlots(int32 RowId, bool bPlayReady, bool bVisible);
	/** Поворот стрелки 67001 (эталон `sub_926C2D(arrow, angle)`), угол в градусах. */
	void SetArrowAngle(float Degrees);
	/** Показать/скрыть одну из четвёрок 67003+ / 67007+ / 67015+.
	 *  Побочно ЗАПУСКАЕТ кадровую анимацию показанной плашки и кладёт её длительность
	 *  (из данных скина) в `PlateAnimSec` — по ней ставится «конец анимации». */
	void ShowPlate(int32 Sector, bool bAniLayer, bool bClaimLayer);
	/** ★ Слой «забрать» IDNum 67015+Sector: в JUI у контрола пустой `ID`, зато есть `IDNum`, а
	 *  импортёр порта именует такие узлы ПО СКИНУ (`Extract_Icon_Effect_{индекс}`). Ищем сперва
	 *  среди детей плашки 67011+Sector, затем по всему окну (иерархия бывает расплющена). */
	UWidget* FindClaimLayer(UWidget* PlateGfont, int32 Sector);
	/** План кадровой анимации САМОГО контрола (эталон берёт `CContainer_GetItemAt(this, 0)`).
	 *  Ищется по узлу, а при промахе — по ПРЕФИКСУ имени (`idc` контрола либо имя его скина).
	 *  ⚠ Обходом поддерева искать НЕЛЬЗЯ: у плашки 67007 есть декоративный ребёнок 67031
	 *  (`Ani_Soul2`, 22 кадра / 1000 мс), и он тайминг НЕ задаёт. */
	class UAsdaAnimatedPlane* FindOwnAnimPlane(UWidget* PlateNode, const TCHAR* IdcPrefix) const;
	/** Пустить анимацию плашки на ДВА ПРОГОНА (эталон: `ctrlShowWithItemFlags(ctrl, 2)` ставит
	 *  EFrameStop и лимит прогонов 2) и вернуть её полную длительность в СЕКУНДАХ =
	 *  `FrameInterval` скина × 2. `FallbackMs` — значение скина на случай отсутствия плана. */
	double StartPlateAnim(UWidget* PlateNode, const TCHAR* IdcPrefix, float FallbackMs, const TCHAR* Tag);

	// ── Сеть ──
	void OnSpinResult6576(const TArray<uint8>& Body);   //  3 б
	void OnPrize6580(const TArray<uint8>& Body);        // 73 б

	// ── Анимация ──
	void BeginSpinTo(int32 Sector);
	void TickSpin(float DeltaTime);

	// ── Утилиты ──
	FString Loc(int32 Id) const;
	void ChatPrize(const FString& Text) const;          // ЧАТ, цвет −11514 (эталон 6580)
	void ChatRefuse(int32 LocId) const;                 // ЧАТ, цвет −65536 (эталон 6576 коды 10/11)
	void PlaySound2D(const FString& WavName) const;
	UAsdaNetSubsystem* Net() const;

	// ── Данные ──
	UPROPERTY(Transient) TMap<int32, FAsdaRouletteRow> Rows;   // ключ = RowId
	bool bTableLoaded = false;

	// ── Виджеты окна 67000 ──
	UPROPERTY(Transient) UUserWidget* Wnd = nullptr;
	UPROPERTY(Transient) UWidget*     Arrow = nullptr;                    // 67001
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  Gfont;               // 67011..67014 (тип приза)
	UPROPERTY(Transient) TArray<TObjectPtr<UImage>>  GfontIcon;           // 67021..67024 (иконка предмета)
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> Plate;               // 67003..67006
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PlateAni;            // 67007..67010
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> PlateClaim;          // 67015..67018 (idc пуст)
	/** Все узлы окна со скином `Extract_Icon_Effect` в порядке обхода дерева = порядок секторов 0..3.
	 *  Запасной путь опознания 67015…67018, когда импортёр расплющил иерархию и узел лежит не под
	 *  своей плашкой (в данных их РОВНО четыре — сверено по `gamemain_jui.ini`). */
	UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> ClaimByWindowOrder;
	UPROPERTY(Transient) TObjectPtr<class UButton> SpinBtn = nullptr;      // 67002 GOLD_roullete_btn
	UPROPERTY(Transient) TArray<TObjectPtr<class UAsdaButtonRelay>> Relays;   // реле кнопки 67002
	bool bWndBound = false;

	// ── Состояние прокрутки (эталонные dword-поля CSoulmateWnd) ──
	EAsdaRouletteSpin SpinState = EAsdaRouletteSpin::Idle;   // +496
	float CurAngle    = 0.f;      // +503 текущий угол стрелки
	float TargetAngle = 0.f;      // +502 целевой угол (сектор × 90°, 0 → 360)
	float Speed       = kSpinSpeed0;  // +497
	float TotalPath   = 0.f;      // +501 полный путь
	float PrevAngle   = 0.f;      // g_flGachaSpinPrevAngle
	float AngleAccum  = 0.f;      // g_flGachaSpinAngleAccum
	int32 CurRowId    = 0;        // +498 id текущей строки таблицы
	int32 NextRowId   = -1;       // +499 следующая строка из 6576 (−1 = терминал)
	bool  bSession    = false;    // +505 «идёт сессия прокрутки»

	/** Отложенный «конец анимации плашки» — эталонное событие окна a4 == 24, которое поднимает сам
	 *  движок анимации плана (`CControl_BuildPlaneVertices@0x12DE440`, по исчерпании лимита прогонов).
	 *  ★ Срок ЦЕЛИКОМ ИЗ ДАННЫХ: `FrameInterval` скина ПЛАНА КОНТРОЛА × 2 прогона
	 *  (`ctrlShowWithItemFlags(ctrl, 2)`): плашки 67007…67010 — 700×2 = 1400 мс
	 *  (`GOLD_roullete_ar_B_*`), слои «забрать» 67015…67018 — 1000×2 = 2000 мс
	 *  (`Extract_Icon_Effect`, `JuiSkin.ini:25956`). Ключ `asda.Soul.Roulette.PlateAnimSec` УБРАН. */
	double PlateTimerEndS = 0.0;
	/** Полная длительность анимации показанной сейчас плашки, СЕКУНДЫ (заполняет `ShowPlate`). */
	double PlateAnimSec   = 2.0;
	int32  PlateSector    = -1;
	bool   bPlateIsClaim  = false;

	// ── Делегаты и счётчики ──
	FDelegateHandle HResult, HPrize;
	bool bNetBound = false;
	// ── ★ Ревизия 1-Б М-3: окно у якоря 6699 при каждом показе (`asda.Soul.Roulette.AnchorPos`) ──
	bool  bAnchorPending = false;    // показ был — ждём отрисовки якоря и подложки
	int32 AnchorTries = 0;           // кадров ожидания (предел 30)
	FVector2D AnchorTarget = FVector2D::ZeroVector;   // куда встала подложка 67050 (абс.), для самотеста
	void TickAnchor();               // порт позиционирования ветки a3 == 1
	int32 Rx6576 = 0, Rx6580 = 0, Tx6575 = 0, Tx6579 = 0;
	// ★ Волна 1-Б: кадр мыши «Золотого яблока» УДАЛЁН — клик по 6693 ловит `UAsdaSoulAppleSubsystem`.
};
