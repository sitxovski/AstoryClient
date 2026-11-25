// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaCooldownReadyIconSubsystem — значок «откат навыка истёк» по центру экрана (S→C 5274, опция 30479). [ОРИГИНАЛ]+[ПОРТ]
//
// ЧТО ЭТО. Когда у навыка кончается откат, сервер шлёт игроку 5274 (тело: [0..1] i16 id навыка). Эталон
//   `pktHandlerCharFxStop@0xF4EDF0` при включённой опции 30479 «Появится после отката.» кладёт в список мини-карты
//   (`CMap@0x2BC9CA0`, `0xB4F100`) запись, и `0xB4FA20` каждый кадр её тикает и рисует (`0xB4F180`): иконка навыка целиком,
//   квад s×s с центром (ширина/2, высота/2 + 100), белый с альфой. Разбор: `Reports/skill_cooldown_ready_icon/01_etalon.md`.
//
// МОДЕЛЬ (1:1 по дизасму, шаги эталона «×30/FPS» — это скорость в секунду):
//   s₀ = 256; фаза 0: s −= 600·Δt, альфа = min(100, |(s − 32)·255/224 − 255|), при s < 32 → s = 32, фаза 1;
//   фаза 1: держать дольше 500 мс (строго) → фаза 2; фаза 2: s −= 180·Δt, альфа остаётся, при s < 0 — запись удаляется
//   (кадр удаления не рисуется). Всего ≈ 1,05 с. Записи — в конец списка, без лимита и без проверки дублей; рисуются по порядку
//   (последняя сверху), в одной точке. Иконка — `UAsdaSkillDatabase::Get(id)->IconName` (запись +36, по индексу, как
//   `uiGetRecordByIndex`); пустое имя / нет текстуры — запись живёт, но не рисуется (у навыков соулмейта id < 100 иконок нет).
//
// ГЕЙТЫ. Опция 30479 — применённое значение `UAsdaGameUserSettings::InterfaceOther[8]` (байт 0x15CB4E6 эталона, меняется по «ОК»),
//   проверяется при приёме, как у эталона. Рисуется только при развёрнутой мини-карте (`CMap+0x248`) и в мире.
//   Слой — ПОД всеми окнами (эталон рисует значки до мини-карты и окон): канвас вьюпорта с z = −1.
//
// ★ [ИСПРАВЛЕНИЯ] дефектов оригинала:
//   • у эталона записи тикают только вместе с мини-картой: при свёрнутой копились и при разворачивании проигрывались залпом.
//     Здесь записи тикают всегда, а мини-карта гейтит только рисование;
//   • у эталона список не чистится никогда (клиент закрывается при разрыве) — здесь полный сброс сессии его очищает.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "AsdaSessionReset.h"
#include "AsdaCooldownReadyIconSubsystem.generated.h"

class SImage;
class UTexture2D;

UCLASS()
class ASDAREBORN_API UAsdaCooldownReadyIconSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Полный сброс сессии (разрыв, выход в выбор персонажа, новый вход) очищает список [ИСПРАВЛЕНИЕ]; смена карты — нет.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;

	// ── FTickableGameObject ──
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaCooldownReadyIconSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableWhenPaused() const override { return false; }
	virtual bool IsTickable() const override { return Entries.Num() > 0; }

	// Порт `0xB4F100`: добавить значок навыка в конец списка (без проверок — как эталон). Опцию проверяет вызывающий приём.
	void AddIcon(int32 SkillId);

	// Применённое значение опции 30479 (`InterfaceOther[8]`, умолчание — включено).
	static bool IsOptionOn();

	// ── Самотест ──
	// Шаг модели на DeltaSec без кадра движка (тот же путь, что `Tick`).
	void DebugStep(float DeltaSec) { StepAll(DeltaSec * 1000.f); SyncWidgets(); }
	// Число живых записей.
	int32 DebugCount() const { return Entries.Num(); }
	// Поля записи I: сторона, альфа (0…100), фаза, id навыка; false — нет такой записи.
	bool DebugEntry(int32 I, float& OutSize, float& OutAlpha, int32& OutPhase, int32& OutSkillId) const;
	// Есть ли у записи I своя картинка (иконка нашлась) и видна ли она сейчас.
	bool DebugHasImage(int32 I) const;
	bool DebugImageVisible(int32 I) const;
	// Прямоугольник картинки записи I в канвасе (левый верх и размер, пиксели вьюпорта) и есть ли у кисти материал.
	bool DebugSlotRect(int32 I, FVector2D& OutPos, FVector2D& OutSize) const;
	bool DebugHasMaterial(int32 I) const { return Entries.IsValidIndex(I) && Entries[I].Mid != nullptr; }
	// Сейчас рисовать разрешено (мини-карта развёрнута, мир).
	bool DebugDrawAllowed() const { return IsDrawAllowed(); }

private:
	// Одна запись списка (`new(0x18)` эталона: +0 s, +4 альфа, +0xC id, +0x10 фаза, +0x14 время входа в фазу 1).
	struct FEntry
	{
		float Size = 256.f;              // сторона квада, px
		float Alpha = 0.f;               // 0…100 (из 255)
		int32 SkillId = 0;               // id навыка
		int32 Phase = 0;                 // 0 сжатие, 1 пауза, 2 исчезание
		float HoldMs = 0.f;              // прошло в фазе 1, мс (у эталона — `timeGetTime()` входа)
		TSharedPtr<SImage> Image;        // виджет (nullptr — иконки нет, запись не рисуется)
		TSharedPtr<struct FSlateBrush> Brush;   // кисть с текстурой иконки
		SConstraintCanvas::FSlot* Slot = nullptr;   // слот канваса (живёт, пока виджет в канвасе)
		class UMaterialInstanceDynamic* Mid = nullptr;   // материал кисти (держит `HeldMids`)
	};

	// Шаг всех записей на DeltaMs (удаляет закончившиеся).
	void StepAll(float DeltaMs);
	// Позиции, размеры, альфа и видимость виджетов по записям.
	void SyncWidgets();
	// Рисовать можно: мини-карта развёрнута и это мир (не сцена входа).
	bool IsDrawAllowed() const;
	// Канвас вьюпорта (лениво), z = −1 — под окнами.
	void EnsureCanvas();
	// Приём 5274 из сети.
	void OnCooldownReady(int32 SkillId);

	TArray<FEntry> Entries;                         // список значков в порядке прихода
	TSharedPtr<SConstraintCanvas> Canvas;           // слой значков (текстуры держит кэш `UAsdaTextureSubsystem` — корень GC)
	FDelegateHandle HReady;                         // подписка на 5274
	UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> HeldMids;   // материалы живых записей (от GC)
};
