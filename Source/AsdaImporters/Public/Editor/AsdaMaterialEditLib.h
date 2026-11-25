// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMaterialEditLib — EDITOR-ONLY мостик к тем входам материала, которых нет в Python-обёртке (сет D «листва», 05.09.2026).
//
// ★ ЗАЧЕМ. `EMaterialProperty::MP_ShadingModel` объявлен `UMETA(Hidden)` (SceneTypes.h:178) ⇒ в `unreal.MaterialProperty`
//   его НЕТ (`AttributeError: MP_SHADING_MODEL`, headless_setD_masters.log), а `MaterialEditingLibrary.connect_material_property`
//   из Python принимает только значения этого перечисления. Свитч листвы (ShadingModel = FromMaterialExpression,
//   StaticSwitch Foliage → DefaultLit/TwoSidedFoliage) без подключения входа ShadingModel не собрать. Здесь — прямой вызов
//   `UMaterialEditingLibrary::ConnectMaterialProperty(..., MP_ShadingModel)` и чтение узла на этом входе для контроля ИЗ АССЕТА.
//   Без сохранения — сохраняет вызывающий Python (EditorAssetLibrary.save_loaded_asset).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaMaterialEditLib.generated.h"

class UMaterial;
class UMaterialExpression;

UCLASS()
class ASDAIMPORTERS_API UAsdaMaterialEditLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Подключить выход узла (OutputName, обычно "") ко входу ShadingModel мастера. true = соединилось. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Материалы")
	static bool ConnectShadingModel(UMaterial* Material, UMaterialExpression* Expression, const FString& OutputName);

	/** Узел на входе ShadingModel мастера (nullptr = вход пуст). Только чтение. */
	UFUNCTION(BlueprintCallable, Category = "Asda|Материалы")
	static UMaterialExpression* GetShadingModelInputExpression(UMaterial* Material);
};
