// Copyright (c) 2026 @sitxovski (Discord)
// SAsdaNormalStudio — панель «Студия нормалей» (вкладка редактора, в игру не попадает).
//
// Слева: папки внутри /Game/AsdaAssets/Models и список моделей с поиском.
// Центр: настоящий 3D-вьюпорт UE со светом, тенями и PBR (FAdvancedPreviewScene).
// Справа: параметры рельефа, света и показа.
//
// Карты (нормаль + маска «шероховатость/свечение») берутся из /Game/_NormalStudio/Maps и
// применяются ТОЛЬКО здесь: материалы игры не меняются, в рантайме ничего не подключается.
#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

#include "SAsdaNormalStudioViewport.h"

class SSearchBox;

class SAsdaNormalStudio : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAsdaNormalStudio) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	using FEntry = TSharedPtr<FAssetData>;

	// ── список моделей ──
	void ScanModels();                     // один проход по реестру ассетов при открытии вкладки
	void RebuildList();                    // фильтр по выбранной папке и строке поиска
	TSharedRef<ITableRow> OnGenerateRow(FEntry Entry, const TSharedRef<STableViewBase>& Owner);
	void OnSelectionChanged(FEntry Entry, ESelectInfo::Type SelectInfo);

	// ── правая панель ──
	TSharedRef<SWidget> MakeSliderRow(const FText& Label, float Min, float Max,
		TFunction<float()> Get, TFunction<void(float)> Set);
	void PushSettings();                   // отдать настройки во вьюпорт

	FText GetStatusText() const;

	TSharedPtr<SAsdaNormalStudioViewport> Viewport;
	TSharedPtr<SListView<FEntry>> ListView;
	TSharedPtr<SSearchBox> SearchBox;

	TMap<FString, TArray<FEntry>> ByFolder;   // папка (Char, Mob, NPC…) → модели
	TArray<TSharedPtr<FString>> Folders;      // порядок папок для выпадающего списка
	TArray<FEntry> Visible;                   // то, что сейчас в списке

	FString CurrentFolder;
	FString Filter;
	FString CurrentModel;

	FAsdaNsSettings Settings;
	float LightYaw = -45.f;
	float LightPitch = -35.f;
	float LightBrightness = 3.f;
	float SkyBrightness = 1.f;
};
