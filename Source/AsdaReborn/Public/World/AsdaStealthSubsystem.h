// Copyright (c) 2026 @sitxovski (Discord)
// AsdaStealthSubsystem.h — ★★★ ВИД НЕВИДИМОСТИ НАВЫКА (состояния 112 «Скрытность» / 186 «Быстрая маскировка»), 24.10.2026.
//
// [ОРИГИНАЛ] Эталон AsdaStory (поле α персонажа +6329, флаги +47552 0x10000 / 0x20000):
//   • наложение 112/186 — обработчик начала состояния (`sub_9B1890` для 112, `charClearTargetAndOpenBag@0x9B3A70` для 186,
//     таблица `char + 52304 + id`) взводит 0x10000 «затухание»: `charEntityUpdateTick@0xA1ED30` за 2000 мс гасит α шагом
//     0.0005/мс до пола: свой персонаж и (у наблюдателя в отряде) член отряда — 0.3, Друг (соулмейт) — 0.3, прочие — 0
//     (по концу — «не рисуется»);
//   • каждый кадр `CharManager_UpdateAll@0x9D56C0` (только ЧУЖИЕ) у невидимого ведёт α к цели наблюдателя: отряд 0.2,
//     Друг 0.4 (`gap_878[47] > 0` и charId == `gap_8B4`), прочие 0 — шагом 0.00025/мс (вместе с 0x10000 α 1→0 ≈ 1.33 с);
//     если у НАБЛЮДАТЕЛЯ состояние 154 «Обнаружение маскировки» — в радиусе (`skillCheckRangeVsDist@0xA38750`) α растёт до 0.5;
//     α == 0 и наблюдатель под GM-скрытием → α 0.5 (game.c:55656-55664); отряд — по charId (`Party_FindMemberByUserIdx` — миснейм);
//     α ≤ цели и цель 0 → `RenderCtx_SetEnable(…, 0)` (не рисуется); не невидимый — α 1 сразу;
//   • снятие 112/186 (`cChar_removeSkillEffect@0xA2B010`) снимает 0x10000 (α := 0.5, если ещё гас) и взводит 0x20000:
//     α += 0.001/мс до 1 за 1000 мс (у чужих `CharManager_UpdateAll` и так ставит 1 сразу);
//   • выбор цели: невидимый персонаж-цель снимается каждый кадр (`game.c:10153`), выбор/Tab его пропускают.
//   Радиус детектора: `AoeRadius` (+0x37) записи навыка, давшего 154 (`запись+2`), × 100; разница уровней N = свой − его:
//   N > 0 — + min(N,5) · 20 % радиуса, N < 0 — − min(|N|,5) · 10 %.
// [ИСПРАВЛЕНИЕ] Эталон сравнивает радиус с | |p_свой| − |p_его| | — разностью ДЛИН векторов позиций от начала координат
//   (`0x934D6E` дважды, затем вычитание), а не с расстоянием между точками: детектор «видит» на кольце вокруг (0,0).
//   У нас — расстояние между персонажами. Ключ `asda.Stealth.DetectRealDist 0` — формула эталона.
// [ПОРТ] Подсистема заменяет обе ветки эталона одним проходом по всем персонажам (`ForEachPlayerActorWithCharId`); вид —
//   третья причина подмены слотов персонажа (`AAsdaPlayerCharacter::SetStealthVisual`), «не рисуется» — скрытие актора с
//   подвесными эффектами и хранителем (как GM-скрытие; обе причины сводятся — снятие одной не проявляет другую).
//   Анимация (присед донора / walk01 эталона) — `AAsdaPlayerCharacter::SetSkillStealth`. Ключ `asda.Stealth.Visual 0` — вид
//   как до 24.10 (невидимые рисуются обычно), `asda.Stealth.ClearTarget 0` — цель не снимается.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaStealthSubsystem.generated.h"

class AAsdaPlayerCharacter;

// Состояние одного персонажа (эталонные поля α +6329, флаги 0x10000/0x20000 и их часы +52171).
struct FAsdaStealthState
{
	bool  bInvisible = false;   // есть 112/186 (прошлый кадр)
	float Alpha      = 1.f;     // α модели
	int32 Phase      = 0;       // 0 — нет, 1 — затухание (0x10000), 2 — проявление (0x20000)
	float PhaseMs    = 0.f;     // часы фазы, мс
	bool  bRender    = true;    // рисуется ли (RenderCtx)
	bool  bHiddenByUs = false;  // актор скрыт этой подсистемой (вернуть при снятии)
};

UCLASS()
class ASDAREBORN_API UAsdaStealthSubsystem : public UGameInstanceSubsystem, public FTickableGameObject, public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject — проход по персонажам каждый кадр (эталон: кадр рендера).
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaStealthSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return !IsTemplate() && bInitialized; }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }

	// ── IAsdaSessionResettable ── состояния персонажей прошлой карты/сессии.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// Самотест: α / фаза / «рисуется» персонажа (false — подсистема его ещё не видела).
	bool GetStateFor(const AAsdaPlayerCharacter* Ch, float& OutAlpha, int32& OutPhase, bool& OutRender) const;
	// Selftest: elapsed time of the current fade phase in ms, on the subsystem's own clock (−1 — not seen yet). Lets a test judge α
	//   by the time the subsystem actually integrated, so a frame hitch between the test timer and this tick does not fake a failure.
	float GetPhaseMsFor(const AAsdaPlayerCharacter* Ch) const;
	// Самотест: принудительно задать «у персонажа есть 112/186» мимо списка эффектов (−1 — снять подмену).
	void DebugForceInvisible(const AAsdaPlayerCharacter* Ch, int32 Force);
	// Самотест: принудительно задать наблюдателю «есть 154» с радиусом в игровых единицах (≤ 0 — снять подмену).
	void DebugForceDetector(float RangeUnits) { DebugDetectorRange = RangeUnits; }
	// Самотест: принудительно задать «персонаж — член отряда» / «Друг» (−1 — снять подмену).
	void DebugForceRelation(const AAsdaPlayerCharacter* Ch, int32 bParty, int32 bMate);

private:
	bool bInitialized = false;
	TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, FAsdaStealthState> States;
	TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, int32> DebugInvisible;     // самотест: 1 / 0 — подмена «есть 112/186»
	TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, int32> DebugParty;         // самотест: подмена «член отряда»
	TMap<TWeakObjectPtr<AAsdaPlayerCharacter>, int32> DebugMate;          // самотест: подмена «Друг»
	float DebugDetectorRange = 0.f;                                       // самотест: радиус детектора 154 (> 0 — подмена)

	// Есть ли у персонажа 112/186 (список эффектов или подмена самотеста).
	bool IsInvisible(const AAsdaPlayerCharacter* Ch, int32 UserIdx) const;
	// Радиус детектора 154 своего персонажа против персонажа уровня OtherLevel; ≤ 0 — 154 нет.
	float DetectorRange(int32 OtherLevel) const;
	// Применить «рисуется» к актору: скрыть/показать его, подвесные эффекты и хранителя (с учётом GM-скрытия).
	void ApplyRender(AAsdaPlayerCharacter* Ch, FAsdaStealthState& S, bool bRender);
	// Один шаг кадра для персонажа: фазы, цель наблюдателя, α, «рисуется».
	void StepCharacter(AAsdaPlayerCharacter* Ch, bool bSelf, int32 Uidx, int32 CharId, float DtMs, const AAsdaPlayerCharacter* Self);
};
