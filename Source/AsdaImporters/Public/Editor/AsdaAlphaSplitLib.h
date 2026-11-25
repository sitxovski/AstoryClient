// Copyright (c) 2026 @sitxovski (Discord)
// AsdaAlphaSplitLib — EDITOR-ONLY: отделение подсеток .jbm с битом очереди в собственный слот материала (31.10.2026,
//   баг 8 bug_chest_invisible, отчёт Reports/bug_chest_invisible/). [ИСПРАВЛЕНИЕ]
//
// ★★★ ЗАЧЕМ. Оригинал рисует подсетку с битом `mr+0x004 & 1` в очереди прозрачных (альфа текстуры = прозрачность,
//   meshNodeRenderDispatch@0x111E110 → g_RenderQueue), а подсетки БЕЗ бита — обычным непрозрачным проходом.
//   При импорте частей экипировки подсетки .jbm слиты в ОДНУ секцию UE с одним слотом. Волна acce_alpha (alpha-5.0.4)
//   переводила на прозрачный мастер весь слот — у смешанных мешей непрозрачный «корпус» (жилет, штаны, шлем) стал
//   translucent без записи глубины: двусторонняя изнанка рисовалась поверх лица детали, вещь «пропадала».
//   Здесь треугольники бит-подсеток переносятся в новую группу полигонов со слотом `<слот0><Suffix>` (материал —
//   тот же, что у слота 0). Позиции, UV, нормали, веса — НЕ меняются: меняется только принадлежность треугольника группе.
//   Выбор материала по слоту — UAsdaCharAssembly::ApplyItemTexture (рантайм).
//
// ★ Треугольник находится по ключу «отсортированные UV углов, округление 1e-4» (данные .jbm —
//   Reports/bug_chest_invisible/scripts/bci_split_data.py). Ось V пробуется прямой и перевёрнутой — берётся та,
//   при которой совпали все треугольники. Одинаковый ключ у нескольких треугольников разбирается очередью в порядке
//   .jbm (порядок подсеток = порядок треугольников импорта). ★ 01.11.2026: ключ с треугольниками ОБЕИХ групп (зеркальные
//   треугольники с общей UV — ch1_f_03_chst104, ch1_m_00_pant107) разбирается по позиции: X центра треугольника .jbm
//   против оси ассета, найденной регрессией по однозначным треугольникам. Уже разделённый меш сверяется заново
//   (при bApply расхождения исправляются).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaAlphaSplitLib.generated.h"

class UStaticMesh;

UCLASS()
class ASDAIMPORTERS_API UAsdaAlphaSplitLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/**
	 * Отделить треугольники бит-подсеток меша (USkeletalMesh или UStaticMesh, LOD0) в новый слот материала.
	 * @param MeshAsset  скелетный или статический меш части.
	 * @param TriUV      UV углов треугольников .jbm подряд (6 чисел на треугольник), в порядке подсеток .jbm.
	 * @param TriBit     на треугольник: 1 — подсетка с битом очереди, 0 — без бита (длина = TriUV/6).
	 * @param TriX       на треугольник: X центра в модели .jbm (разбор неоднозначных ключей UV).
	 * @param Suffix     хвост имени нового слота (имя = имя слота 0 + хвост).
	 * @param bApply     false — только сверка (ничего не меняется); true — перенос и пересборка (сохраняет вызывающий).
	 * @return строка отчёта: «OK …» — всё сошлось (и при bApply — применено), «FAIL …» — причина, меш не тронут.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|AlphaSplit")
	static FString SplitBitSubsets(UObject* MeshAsset, const TArray<float>& TriUV, const TArray<uint8>& TriBit,
		const TArray<float>& TriX, const FString& Suffix, bool bApply);

	/**
	 * ★ 01.11.2026 (mob_fx_audit) [ИСПРАВЛЕНИЕ]: то же для МНОГОСЛОТОВОГО меша моба/NPC. Внутри ОДНОГО слота SlotName
	 * треугольники подсеток-эффектов (TriBit = 1: бит очереди и полутон альфы в отпечатке UV) переносятся в новую группу
	 * полигонов со слотом `<SlotName><Suffix>` (материал — как у SlotName); остальные слоты меша не трогаются.
	 * @param MeshAsset  скелетный или статический меш модели моба/NPC.
	 * @param SlotName   имя слота материала, в котором лежат подсетки (MaterialSlotName).
	 * @param TriUV      UV углов треугольников подсеток ЭТОГО слота подряд (6 чисел на треугольник), в порядке .jbm.
	 * @param TriBit     на треугольник: 1 — подсетка-эффект (в новый слот), 0 — остаётся.
	 * @param TriX       на треугольник: X центра в модели .jbm (разбор неоднозначных ключей UV).
	 * @param Suffix     хвост имени нового слота.
	 * @param bApply     false — только сверка; true — перенос и пересборка (сохраняет вызывающий).
	 * @return «OK …» или «FAIL …» (меш не тронут).
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|AlphaSplit")
	static FString SplitSlotSubsets(UObject* MeshAsset, FName SlotName, const TArray<float>& TriUV, const TArray<uint8>& TriBit,
		const TArray<float>& TriX, const FString& Suffix, bool bApply);

	/**
	 * ★ 01.11.2026: дождаться асинхронной сборки статического меша и его поля расстояний (Mesh Distance Field) —
	 * после Build() поле строится в фоне, и сохранение до его конца пишет в тег DistanceFieldSize неполный размер.
	 * @return размер поля LOD0, байт (как тег DistanceFieldSize: ресурс + стриминговые мипы); −1 — нет данных.
	 */
	UFUNCTION(BlueprintCallable, Category = "Asda|AlphaSplit")
	static int64 FinishDistanceField(UStaticMesh* Mesh);
};
