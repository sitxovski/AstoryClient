// Copyright (c) 2026 @sitxovski (Discord)
// AsdaCastBarSubsystem.h — ПОЛОСА ПРИМЕНЕНИЯ НАВЫКА `SKILL_BAR_Win` (JUI IDNum 66000).
//
// ПОЛНЫЙ РЕВЕРС ПЕРЕД КОДОМ: `Reports/skill_use_system.md` §1 (структурный дамп всех четырёх
//   контролов, скины, формула позиции, логика показа, источник значения и максимума).
//
// ★★★ ГЛАВНОЕ, ЧТО НАДО ПОНЯТЬ ПРО ЭТУ ПОЛОСУ
//   Это **НЕ таймер каста**. Поле `CastingTime` (`skill_info.sif +0x2D`, i16, мс) в клиенте
//   ВООБЩЕ НЕ ЧИТАЕТСЯ (grep по всему срезу `RE/etalon/*.c` — 0 обращений; отчёт §6.5), и по сети
//   длительность каста тоже не приходит (§5.4: сервер держит её у себя, клиенту шлёт только
//   «каст начался» 5270 и «каст кончился» 5271).
//   Полоса показывает **ПРОГРЕСС АНИМАЦИИ ПРИМЕНЕНИЯ от 0 до кадра ПЕРВОГО УДАРА**:
//     значение = текущее время клипа каста в МИЛЛИСЕКУНДАХ (эталон `Object_GetFloat13@0x99EDA0`
//               = `*(float*)(animObj + 0x34)`, продвигается на `dt · animSpeedScale`);
//     максимум = `COMBO0` — кадр первого удара, тоже в мс (слот эффекта `+0x58`; в `structs_ru.h`
//               это поле названо `color` — МИСНЕЙМ, отчёт §10).
//
// ЛОГИКА ПОКАЗА 1:1 (`uiUpdateActiveSkillSlot@0xEDC480`, `ui_core.c:89705`):
//     if (activeSlot == -1)                       скрыть окно
//     else { показать; имя навыка → 66002; SetRange(0,max) при смене max; SetValue(value) }
//     if (state != 14 && state != 27 && state != 13 && state != 24)  скрыть окно
//   ⇒ полоса видна только в состояниях 13 / 14 / 24 / 27 действия персонажа.
//   ⚠ Соулмейт-навык уходит в состояние 25 — в нём полоса НЕ показывается (отчёт §1.5: дефект
//     оригинала либо переход 25→24 вне среза; воспроизводим как есть).
//
// ЗАКОНЫ, КОТОРЫЕ ЗДЕСЬ СОБЛЮДЕНЫ
//   ui-progressbar-animate-existing — заливка ES1 (`SKILL_BAR_1_1`) клипуется по ширине слота,
//     подложка ES0 (`SKILL_BAR_1_0`) не трогается; клип — общий `UAsdaMiniStateSubsystem::ClipFill`
//     (НЕ копия: закон дедупа code_dedup_audit_2).
//   ui-window-drag-hittest — у контрола `SKILL_BAR` в .JUI есть `EVS{EVT=6, RET=0x20003}`,
//     т.е. полоса ПЕРЕТАСКИВАЕМАЯ. Ручка приезжает из `DA_JuiWindowMeta` автоматически при
//     регистрации окна в `UAsdaWindowSubsystem` — своего механизма здесь нет и быть не должно.
//   no-invented-ui — окно = импортированный `WBP_SKILL_BAR_Win`, ни один виджет не рисуется руками.
//   ui-text-color-exs — цвет/выравнивание подписей уже применены импортёром из EXS (белый FFFFFFFF).
//
// ЧЕГО ЗДЕСЬ НЕТ И ПОЧЕМУ
//   • Контрол 66003 (`SKILL-TEST_TEXT2`, узел `SKILL_TEST_TEXT2`) — в `uiUpdateActiveSkillSlot`
//     НЕ УПОМИНАЕТСЯ вовсе; в `uiBindDialogControls` ему ставят стиль и ПУСТУЮ строку.
//     Формулировка по закону dead-code-markings-suspect: **ссылок на обновление текста в срезе нет**.
//   • Иконки навыка в полосе нет — в окне 66000 всего 3 контрола.
//   • Флаг «полоса перевалила за половину» (`byte_2FFC888`) в срезе только ПИШЕТСЯ, читателей нет —
//     воспроизведён как `bHalfPassed` ровно с той же семантикой (пишется, не читается).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "UI/AsdaMiniStateSubsystem.h"   // FAsdaMiniBarFill + статические ClipFill/GetSlotRect (общее ядро баров)
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaCastBarSubsystem.generated.h"

class UUserWidget;
class UWidget;
class UImage;
class UTextBlock;

UCLASS()
class ASDAREBORN_API UAsdaCastBarSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Окно, его защёлка «запрос в полёте» и полученные от сервера данные
	// принадлежат СЕССИИ и переживали разрыв связи вместе с GameInstance.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCastBarSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableInEditor() const override { return false; }
	virtual bool IsTickableWhenPaused() const override { return false; }

	// ★ ЗАЖЕЧЬ полосу. Эталонный аналог — присвоение `effectSlots[100].field_4 = <индекс слота>`
	//   в `Recv_SkillCast` (успех) / `Recv_SkillCastIdle` (старт каста) + `CChar_SetState(..., 24/14)`.
	//     MaxMs   — COMBO0 (кадр первого удара, мс). <= 0 ⇒ у эталона максимум подменяется на 1.0,
	//               см. `Begin` — там же честная запись в лог, откуда максимум взялся.
	//     FullMs  — полная длительность клипа (мс, 0 = неизвестна): нужна только для авто-гашения,
	//               эталонный признак «анимация доиграла» = `obj[13] >= obj[11]`.
	//     State   — состояние действия персонажа (14 = накопление, 24 = исполнение навыка).
	void Begin(int32 SkillCode, float MaxMs, float FullMs, int32 State);

	// Сменить состояние действия персонажа (гейт видимости 13/14/24/27).
	void SetState(int32 State);

	// ★ ПОГАСИТЬ полосу (эталон: `effectSlots[100].field_4 = −1`). Reason — только в лог.
	void End(const TCHAR* Reason);

	bool  IsActive()   const { return ActiveSkill >= 0; }
	int32 GetSkill()   const { return ActiveSkill; }
	float GetValueMs() const { return ValueMs; }
	float GetMaxMs()   const { return MaxMs; }
	int32 GetState()   const { return CharState; }

	// Принудительный показ для проверки без сервера (консоль `asda.Skill.CastBar.Test`).
	void ForceShowForTest(int32 SkillCode, float TestMaxMs);

	FString DumpState() const;

	// ── Настройки (все — порт эталонных величин, менять только осознанно) ──

	// Множитель скорости анимации персонажа (эталон `pChar->animSpeedScale`; 1.0 = нормальная).
	UPROPERTY(EditAnywhere, Category = "Asda|КастБар") float AnimSpeedScale = 1.f;

	// Гасить полосу, когда КЛИП доиграл целиком (эталон `Game_IsPausedOrMenu@0x99ED60`:
	//   `obj[13] >= obj[11]`, т.е. текущее время >= полной длительности). Работает только если
	//   `FullMs > 0`; иначе гасим по приходу 5271 / отказа, как и эталон.
	UPROPERTY(EditAnywhere, Category = "Asda|КастБар") bool bAutoEndOnClipFinished = true;

	// Z-порядок окна. Рядом с EXP-баром (kExpBarZ = 90) — полоса живёт в том же HUD-слое.
	UPROPERTY(EditAnywhere, Category = "Asda|КастБар") int32 CastBarZ = 92;

private:
	bool EnsureControls();
	void ResetControls();
	void ApplyVisibility();
	void ApplyValue();
	void ApplyName();

	UPROPERTY(Transient) TObjectPtr<UUserWidget> Win = nullptr;

	// Узлы дерева `WBP_SKILL_BAR_Win` (имена — из самого ассета, проверены дампом FName-таблицы):
	//   `SKILL_BAR_Win_0_0`  — план Box_Black (подложка окна, IDNum 66000)
	//   `SKILL_BAR_1_0`      — план ES0 скина Orange_Bar (подложка полосы,   IDNum 66001)
	//   `SKILL_BAR_1_1`      — план ES1 скина Orange_Bar (ЗАЛИВКА FFFF6C00,  IDNum 66001)
	//   `SKILL_BAR_1_DRAG`   — ручка перетаскивания (EVS 6/0x20003) — её ведёт UAsdaWindowSubsystem
	//   `SKILL_TEST_TEXT`    — имя навыка (IDNum 66002)
	//   `SKILL_TEST_TEXT2`   — резервная подпись (IDNum 66003; апдейта в срезе НЕТ)
	UPROPERTY(Transient) FAsdaMiniBarFill Bar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> NameText = nullptr;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ReserveText = nullptr;

	bool bControlsReady = false;

	// ── Состояние полосы (порт полей персонажа) ──
	int32 ActiveSkill = -1;   // эталон `effectSlots[100].field_4` ≥ 0 ⇔ навык применяется; −1 = нет
	float ValueMs = 0.f;      // эталон `animObj + 0x34` — текущее время клипа каста, мс
	float MaxMs = 0.f;        // эталон слот `+0x58` = COMBO0, мс
	float FullMs = 0.f;       // эталон `animObj + 0x2C` — полная длительность клипа, мс (0 = неизв.)
	int32 CharState = 0;      // эталон `effectSlots[112].field_8` — состояние действия персонажа
	float LastAppliedMax = -1.f;   // эталон `g_fSkillBarLastMax` — кэш, чтобы не звать SetRange каждый кадр
	// Порт `byte_2FFC888`: эталон взводит его, когда значение перевалило за половину максимума,
	//   и НИГДЕ не читает (закон dead-code-markings-suspect: «ссылок на чтение в срезе нет»).
	bool bHalfPassed = false;
	bool bWindowShown = false;

	// Один раз на навык печатаем, откуда взялся максимум (или что его нет) — чтобы дыра была видна.
	int32 LastLoggedSkill = -1;
};
