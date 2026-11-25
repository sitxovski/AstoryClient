// Copyright (c) 2026 @sitxovski (Discord)
// Реализация мостика ко входу ShadingModel мастера. Зачем — в заголовке AsdaMaterialEditLib.h.
#include "Editor/AsdaMaterialEditLib.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"

bool UAsdaMaterialEditLib::ConnectShadingModel(UMaterial* Material, UMaterialExpression* Expression, const FString& OutputName)
{
	if (!Material || !Expression || Expression->Material != Material) { return false; }
	// Тот же путь, что у Python connect_material_property, только с скрытым от UENUM значением MP_ShadingModel.
	return UMaterialEditingLibrary::ConnectMaterialProperty(Expression, OutputName, MP_ShadingModel);
}

UMaterialExpression* UAsdaMaterialEditLib::GetShadingModelInputExpression(UMaterial* Material)
{
	if (!Material) { return nullptr; }
	FExpressionInput* In = Material->GetExpressionInputForProperty(MP_ShadingModel);
	return In ? In->Expression : nullptr;
}
