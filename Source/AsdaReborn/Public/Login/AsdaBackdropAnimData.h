// Copyright (c) 2026 @sitxovski (Discord)
// AsdaBackdropAnimData — DataAsset с оригинальной анимацией login-задника (клип `cre_creback-a_00_0.jbb`).
//
// ★ ОТКУДА ДАННЫЕ: Tools/asda_export_backdrop_anim.py (реверс + сверка, `Reports/login_backdrop_anim.md`).
//   Задник = 697 РИГИДНЫХ под-мешей одной модели `cre_creback-a_00.jbm`; у каждого своя кость клипа,
//   найденная ПО ИМЕНИ УЗЛА под-меша. Клип 692 кости × 201 ключ, 30 к/с, 6.6667 с, ЗАЦИКЛЕН
//   (ключ 0 == ключ 200 байт-в-байт; эталон сбрасывает время при `time >= duration`).
//
// ★ ЧТО ИМЕННО ЛЕЖИТ В ТРЕКЕ: НЕ поза кости, а ЛОКАЛЬНАЯ ДЕЛЬТА элемента в UE-пространстве (см),
//   уже сопряжённая базовым трансформом элемента. Применение: `NewRelative = Delta * BaseRelative`.
//   Благодаря сопряжению правка дизайнера (элемент подвинули/повернули руками) НЕ ломает качание —
//   оно едет вместе с элементом (закон [[ue-editor-friendly]]); для нетронутых элементов результат
//   тождественно равен оригиналу (доказано численно, расхождение 5e-8 отн.).
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsdaBackdropAnimData.generated.h"

// Один трек: элемент задника ↔ ключи его локальной дельты.
USTRUCT(BlueprintType)
struct FAsdaBackdropAnimTrack
{
	GENERATED_BODY()

	// Имя элемента = метка актора без префикса `BackdropElem_` (напр. `bg_000_a01_23`). По нему
	// аниматор находит актор в уровне (тег на акторе), поэтому имя должно быть уникальным.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	FName Element;

	// Имя кости клипа, из которой взят трек (== имя узла под-меша в .jbm). Информационное поле:
	// позволяет свериться с оригиналом, не открывая экспортёр.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	FName Bone;

	// Ключи ПЛОСКИМ массивом, шаг 7 float: LocX, LocY, LocZ, QuatX, QuatY, QuatZ, QuatW.
	// Плоский массив, а не TArray<FTransform>: 632 трека × 201 ключ = 127 тыс. ключей, и массив
	// структур раздувает .uasset и время его генерации на порядок, ничего не давая — эти числа
	// правятся не руками, а перегенерацией экспортёром.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	TArray<float> Keys;
};

UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaBackdropAnimData : public UDataAsset
{
	GENERATED_BODY()
public:
	// Имя исходного клипа оригинала (для трассируемости; менять не нужно).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	FString SourceClip;

	// Имя исходной модели оригинала (для трассируемости).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	FString SourceModel;

	// Частота ключей клипа, к/с (оригинал: 30). Шаг ключей равномерный — проверено экспортёром.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	float FrameRate = 30.f;

	// Длительность клипа, с (оригинал: 6.6667 = 200 кадров).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	float Duration = 0.f;

	// Число ключей в каждом треке (оригинал: 201, последний дублирует первый = шов лупа).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	int32 NumKeys = 0;

	// Треки движущихся элементов (оригинал: 632 из 697).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	TArray<FAsdaBackdropAnimTrack> Tracks;

	// Элементы, чья кость в клипе ЕСТЬ, но неподвижна (оригинал: 59). Ничего не выброшено молча —
	// список ведётся явно (закон [[no-scope-cutting]]), эти элементы честно остаются статикой.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	TArray<FName> StaticElements;

	// Элементы, для которых кости с таким именем в клипе НЕТ вовсе (оригинал: 6, узлы f00_464..471).
	// В оригинале они тоже неподвижны — двигать их нечем.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Asda|Backdrop")
	TArray<FName> UnboundElements;

	// Выборка трека на момент времени Time (сек, уже приведённый в [0, Duration]) с линейной
	// интерполяцией позиции и slerp поворота. Возвращает false, если трек пуст/индекс неверен.
	bool SampleTrack(int32 TrackIndex, float Time, FTransform& OutDelta) const;
};
