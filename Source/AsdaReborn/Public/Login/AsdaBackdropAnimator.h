// Copyright (c) 2026 @sitxovski (Discord)
// AAsdaBackdropAnimator — проигрыватель оригинальной анимации login-задника поверх декомпозиции
// на 697 редактируемых акторов `BackdropElem_*`.
//
// ★ ЗАЧЕМ ОТДЕЛЬНЫЙ АКТОР-ДИРИЖЁР, а не скелетный меш и не 632 трека в Level Sequence:
//   * скелетный меш вернул бы запечённый монолит и убил поэлементную правку (нарушение
//     [[ue-editor-friendly]] — ровно то, от чего уходили при декомпозиции);
//   * Level Sequence на 632 объекта × 3 трека — это ~1900 треков в одной секвенции: редактор такой
//     Sequencer открывает минутами, а секвенция ПОССЕССИТ акторы, то есть ручные правки дизайнера
//     затирались бы треком (та же грабля, из-за которой перелёт двигает `LoginBackdrop_Flight`,
//     а не `_Anchor`).
//   Дирижёр не владеет акторами: он читает их АВТОРСКИЙ относительный трансформ при старте игры и
//   накладывает дельту поверх. В редакторе он не тикает вовсе — правки в уровне неприкосновенны.
//
// ★ ОТКАТ К СТАТИКЕ: снять галку `bEnabled` на этом акторе, либо консоль `asda.LoginBackdropAnim 0`
//   (cvar работает и в упакованной игре, применяется на лету и возвращает авторские трансформы).
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AsdaBackdropAnimator.generated.h"

class UAsdaBackdropAnimData;

UCLASS(Blueprintable)
class ASDAREBORN_API AAsdaBackdropAnimator : public AActor
{
	GENERATED_BODY()
public:
	AAsdaBackdropAnimator();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// Данные анимации (генерируются Tools/asda_export_backdrop_anim.py + asda_build_backdrop_anim.py).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop")
	TObjectPtr<UAsdaBackdropAnimData> AnimData;

	// Главный выключатель. Снятая галка = задник статичен (как было до 24.08.2026).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop")
	bool bEnabled = true;

	// Множитель скорости проигрывания. 1.0 = как в оригинале (30 к/с, цикл 6.667 с).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop", meta = (ClampMin = "0.0"))
	float PlayRate = 1.f;

	// Сила качания: 0 = стоит, 1 = как в оригинале. Промежуточные значения — художественная правка
	// (ОТСТУПЛЕНИЕ ОТ ОРИГИНАЛА, дефолт 1.0 = точный оригинал).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Amplitude = 1.f;

	// Зациклить. Оригинал зациклен (эталон сбрасывает время клипа при `time >= duration`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop")
	bool bLoop = true;

	// Смещение старта по времени, с. 0 = как в оригинале.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop", meta = (ClampMin = "0.0"))
	float StartTime = 0.f;

	// Тег-маркер, которым помечены акторы элементов задника (ставит asda_build_backdrop_anim.py).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop")
	FName ElementTag = FName(TEXT("AsdaBackdropElem"));

	// Элементы, которые дизайнер хочет оставить неподвижными (имена как в треках, напр. `bg_000_a01_23`).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Asda|Backdrop")
	TArray<FName> ExcludedElements;

	// Включить/выключить анимацию из Blueprint (например, кнопкой отладки).
	UFUNCTION(BlueprintCallable, Category = "Asda|Backdrop")
	void SetAnimationEnabled(bool bInEnabled);

	// Сколько элементов реально привязано к трекам (диагностика; заполняется в BeginPlay).
	UFUNCTION(BlueprintCallable, Category = "Asda|Backdrop")
	int32 GetBoundElementCount() const { return Bound.Num(); }

private:
	// Один привязанный элемент: актор, его трек и АВТОРСКИЙ относительный трансформ (база).
	struct FBoundElement
	{
		TWeakObjectPtr<AActor> Actor;   // актор элемента (`BackdropElem_*`)
		int32 TrackIndex = INDEX_NONE;  // индекс трека в UAsdaBackdropAnimData::Tracks
		FTransform Base = FTransform::Identity;  // относительный трансформ, как его оставил дизайнер
	};

	// Привязки, построенные один раз в BeginPlay.
	TArray<FBoundElement> Bound;

	// Текущее время проигрывания клипа, с.
	float PlayTime = 0.f;

	// Наложена ли сейчас дельта (чтобы при выключении вернуть базу РОВНО ОДИН раз).
	bool bDeltaApplied = false;

	// Найти акторы элементов и запомнить их авторские трансформы.
	void BindElements();

	// Вернуть всем привязанным элементам авторский трансформ (откат к статике).
	void RestoreBaseTransforms();
};
