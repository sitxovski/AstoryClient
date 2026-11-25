// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFloatingTextSubsystem — floating-цифры урона (порт CFloatingText/CCombatTextNode 1:1).
// Реверс: Reports/combat_floating_text_re.md + CombatText_DrawString@0x1162630 /
//   combatTextCharToGlyphUV@0x11632C0. РЕНДЕР = СПРАЙТ-АТЛАС font.TEX (T_AsdaCombatFont, 256×256,
//   клетка 0.125): цифры '0'-'9' → UV((i%8)·0.125, 0.75+(i/8)·0.125), '1' u=0.12625;
//   ★★★ СЛОВА рисуются ОДНИМ квадом, но у КАЖДОГО своя UV-ширина и своя ширина квада — это
//   ЧЕТЫРЕ РАЗНЫЕ функции отрисовки, а не одна (реверс `CombatText_DrawString`, ветки strcmp):
//     MISS   `uiSpriteDrawDigitQuadA@0x1162A60` — UV (0.25, 0.875), Δu 0.375, квад 5·scale;
//     DODGE  `uiSpriteDrawDigitQuadB@0x1162C10` — UV (0.0,  0.625), Δu 0.625, квад 9·scale;
//     IMMUNE `uiSpriteDrawDigitQuadC@0x1162DC0` — UV (0.0,  0.5  ), Δu 0.75,  квад 9·scale;
//     EXP    `uiSpriteDrawDigitQuadD@0x1162F70` — UV (0.0,  0.375), Δu 0.57,  квад 9·scale.
//   Ключ «EXP» — литерал `off_14B8FF0` («EXP\0POINT\0» @0x14B8FF0); в РУССКОМ атласе в этой клетке
//   нарисовано «ОПЫТ+» (кроп атласа — `Reports/exp_gain_ui.md` §2.3). Цвет = tint (атлас белый).
// Кривая (keyframe-треки @0x9A7D00): pop-in scale 10→20(30) за 40мс → удержание → ужатие до 11(17)
//   к 100(110)мс → равномерный подъём на 200px вверх с линейной альфой 1→0 до 1000(1200)мс.
//   Позиция фиксируется В МОМЕНТ спавна (экранная проекция цели).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"
#include "AsdaSessionReset.h"   // IAsdaSessionResettable — сброс состояния сессии
#include "AsdaTickGate.h"   // ★ R4, доля a: шаблон класса не тикает (общая точка)
#include "AsdaFloatingTextSubsystem.generated.h"

class SConstraintCanvas;
class UTexture2D;

// Слейт-виджет строки урона: рисует глифы из атласа (цифры по клеткам / слово одним широким квадом).
class SAsdaCombatGlyphs : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SAsdaCombatGlyphs) {}
		SLATE_ARGUMENT(UTexture2D*, Atlas)
		SLATE_ARGUMENT(FString, Text)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs);

	// Динамика кадра (из тика подсистемы): полу-размер глифа (эталон scale), цвет с альфой.
	// ★ 29.10.2026 (волна производительности): смена размера/цвета ИНВАЛИДИРУЕТ виджет. До правки поля менялись молча —
	//   при глобальной инвалидации Slate (Slate.EnableGlobalInvalidation=1, кэш отрисовки) цифра «замерзала» в первом
	//   кадре: рост, угасание альфы и смена размера не перерисовывались. Без изменений — без инвалидации (ноль работы).
	void SetGlyphState(float InScale, const FLinearColor& InColor)
	{
		if (InScale == Scale && InColor == Color) { return; }
		const bool bSizeChanged = (InScale != Scale);
		Scale = InScale; Color = InColor;
		Invalidate(bSizeChanged ? EInvalidateWidgetReason::Layout : EInvalidateWidgetReason::Paint);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

private:
	FString Text;
	bool bWord = false;          // слово = ОДИН квад из атласа (ширина/UV свои у каждого слова)
	FVector2D WordUV = FVector2D::ZeroVector;   // левый-верхний угол клетки слова
	float WordUVW = 0.375f;      // Δu слова (эталон: MISS 0.375 / DODGE 0.625 / IMMUNE 0.75 / EXP 0.57)
	float WordQuadW = 5.f;       // ширина квада в единицах scale (MISS 5·scale, остальные 9·scale)
	float Scale = 10.f;          // полу-размер глифа, px (эталонный scale трека)
	FLinearColor Color = FLinearColor::White;
	mutable TArray<FSlateBrush> Brushes;   // браши кадра (UV-клетки) — живут до конца отрисовки
	TWeakObjectPtr<UTexture2D> Atlas;
	static FVector2D DigitUV(TCHAR C);     // клетка цифры (реверс combatTextCharToGlyphUV)
};

UCLASS()
class ASDAREBORN_API UAsdaFloatingTextSubsystem : public UGameInstanceSubsystem, public FTickableGameObject,
	public IAsdaSessionResettable
{
	GENERATED_BODY()

public:
	// ★ Ключ глифа-слова «опыт» в атласе (эталонный литерал off_13E5200 = "EXP"; картинка «ОПЫТ+»).
	//   Держим его ЗДЕСЬ, чтобы вызывающие не плодили свои строковые литералы.
	static const TCHAR* ExpWordKey() { return TEXT("EXP"); }

	// Спавн текста стилем оригинала над мировой точкой (проекция в экран сейчас; 0x12 = no-op).
	//   ★ `ScreenOffset` — сдвиг В ПИКСЕЛЯХ от спроецированной точки. Он нужен, потому что
	//   оригинал позиционирует надписи НЕ по центру сущности: у цифр урона сдвига нет, а у надписи
	//   опыта `netHandleHpExpUpdate@0xF427A0` жёстко задаёт свои оффсеты (см. exp_gain_ui.md §2.1).
	//   ★ `ScreenAlign` — какая ТОЧКА виджета садится на позицию (доли размера). Значение по
	//   умолчанию (0.5, 1.0) — исторический якорь цифр урона порта. Эталонная геометрия квадов
	//   даёт другую точку и, главное, НЕ ЗАВИСЯЩУЮ ОТ МАСШТАБА: у строки цифр первый глиф занимает
	//   [X−1.5s, X+0.5s] ⇒ доля 0.75/n по X, у слова [X−5s, X+4s] ⇒ доля 5/9; по Y обе — 0.75.
	//   Именно так позиция остаётся эталонной и на пике «поп-ина» (s = 30), и после ужатия (s = 17).
	void SpawnStyled(int32 Style, const FString& Text, const FVector& WorldPos,
	                 const FVector2D& ScreenOffset = FVector2D::ZeroVector,
	                 const FVector2D& ScreenAlign = FVector2D(0.5, 1.0));
	// Хелпер: над актором (центр баундов — эталон Entity_GetScreenPos от soundPos-центра).
	void SpawnOverActor(int32 Style, const FString& Text, const AActor* Over,
	                    const FVector2D& ScreenOffset = FVector2D::ZeroVector,
	                    const FVector2D& ScreenAlign = FVector2D(0.5, 1.0));

	// ★ 19.10.2026: спавн в ЭКРАННОЙ точке (пиксели вьюпорта), без проекции из мира — крупная цифра обратного отсчёта 6145
	//   (`FloatingText_Spawn(…, x, y, 0x16, …)` у `recallCountdown_Tick`: узел `CCombatTextNode_VariantB` берёт x/y как есть).
	//   Гейт «режим показа цифр 2» тот же, что у `SpawnStyled`. Разбор стиля — `Reports/gm_original_commands/style_0x16.md`.
	void SpawnScreen(int32 Style, const FString& Text, const FVector2D& ScreenPos, const FVector2D& ScreenAlign);
	// Число живых надписей (самотест).
	int32 DebugLiveCount() const { return Entries.Num(); }

	// ★ Живой дамп канала (консоль `asda.Fx.FloatDump`): канвас/атлас/размер вьюпорта и список
	//   живых надписей с экранной позицией. Без него «надписи нет» неотличимо от «надпись есть,
	//   но под окном или за экраном» — закон `false-negatives-method`.
	void DumpLive() const;

	virtual void Deinitialize() override;

	// ── IAsdaSessionResettable (К-3) ──
	// Всплывающие цифры живут в оверлее вьюпорта, который умирает вместе с миром.
	virtual void ResetSessionState(EAsdaSessionResetReason Reason) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UAsdaFloatingTextSubsystem, STATGROUP_Tickables); }
	// ★ R4, доля a (14.11.2026): шаблон класса (CDO) не тикает — `AsdaTickGate.h`, ключ `asda.Perf.NoCdoTick`; рабочий объект тикает как раньше.
	virtual bool IsTickable() const override { return AsdaTickGate::NotCdo(this); }
	virtual bool IsTickableWhenPaused() const override { return false; }

private:
	struct FEntry
	{
		TSharedPtr<SAsdaCombatGlyphs> Widget;
		FVector2D Pos0 = FVector2D::ZeroVector;   // экранная позиция спавна
		float T = 0.f;                            // мс
		float LifeMs = 1000.f;                    // 1000 (трек 0) / 1200 (трек 1)
		bool bTrack1 = false;                     // крит-трек (scale 30/17)
		bool bStatic = false;                     // ★ стиль 0x16: неподвижно, масштаб 0 до 10 мс, затем 30; альфа 1; 1000 мс
		float ScaleMul = 1.f;                     // ×0.7 для MISS/DODGE
		FLinearColor Color = FLinearColor::White;
	};
	TArray<FEntry> Entries;
	TSharedPtr<SConstraintCanvas> Canvas;
	UPROPERTY() TObjectPtr<UTexture2D> Atlas = nullptr;   // T_AsdaCombatFont (лениво)
	void EnsureCanvas();
	static bool StyleParams(int32 Style, FLinearColor& OutColor, bool& bOutTrack1, float& OutScaleMul);
};
