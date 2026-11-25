// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaMobBrowserDebug — самопроверка обозревателя мобов и NPC без окна (зовётся из Python: Tools/asda_mob_browser_check.py).
//   Всё идёт ТЕМ ЖЕ путём, что действия пользователя: раскрытие группы, выбор строки, щелчок по заголовку, выбор клипа,
//   зажатая ЛКМ + движение мыши через вьюпорт-клиент. Кадр — вся вкладка в render target (FWidgetRenderer) вместе с 3D.
//   Проверка «живости» добавляет строку в DataTable ТОЛЬКО В ПАМЯТИ и сразу убирает её; ассет не сохраняется.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AsdaMobBrowserDebug.generated.h"

UCLASS()
class UAsdaMobBrowserDebug : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Образцы для кадров: маленький, большой, босс, моб без своих клипов, статический моб, NPC с разговором. «ключ=id;…».
	UFUNCTION(BlueprintCallable, Category = "Asda|MobBrowser")
	static FString PickSamples();

	// Проверки: данные, группы, сортировки, клипы, мышь, «живость» таблиц. Итог «ПРОВЕРОК n, ОШИБОК m».
	UFUNCTION(BlueprintCallable, Category = "Asda|MobBrowser")
	static FString SelfTest();

	// Кадр вкладки → PNG. bNpc — запись из группы NPC (0 = первая строка группы); SortColumn: "Name" | "Lvl" | "ID";
	//   ClipIndex < 0 — клип по умолчанию; DragPixels — «зажал ЛКМ и протащил мышь на N пикселей».
	UFUNCTION(BlueprintCallable, Category = "Asda|MobBrowser")
	static FString RenderBrowser(const FString& PngPath, bool bNpc, int32 Id, int32 ClipIndex, float DragPixels, float ClipTime,
		const FString& SortColumn, bool bDescending, const FString& Search, bool bEnglish, int32 Width, int32 Height);

	// Диагностика освещения: тот же кадр с режимом вьюпорта ViewMode (0 Lit, 1 только свет, 2 без света, 3 мировые нормали)
	//   и, при bNoMirror, без зеркального масштаба Y.
	UFUNCTION(BlueprintCallable, Category = "Asda|MobBrowser")
	static FString RenderDiag(const FString& PngPath, bool bNpc, int32 Id, int32 ViewMode, bool bNoMirror, float Yaw, int32 Width, int32 Height);
};
