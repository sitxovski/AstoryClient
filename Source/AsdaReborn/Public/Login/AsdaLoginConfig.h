// Copyright (c) 2026 @sitxovski (Discord)
// AsdaLoginConfig — DataAsset со ВСЕМИ настраиваемыми параметрами login/char-select сцены.
// ★ Правило Editor-Friendly ([[ue-editor-friendly]]): дизайнер правит эти значения ПРЯМО в редакторе
//   (Content Browser → DA_LoginConfig), а не в C++. Дефолты = эталонные 1:1 (login_charselect.c), но
//   переопределяемы. GameMode читает конфиг; если не задан — падает на эти же дефолты (ничего не ломается).
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AsdaLoginConfig.generated.h"

// Один звук-кью анимации шкатулки: на кадре Frame клипа проиграть S_e_creation{CreationSound}.
// Эталон charSelect_PlaySlotAnim@0xF9F090 (латч по кадрам, 2D-звук csoundPlayName2DStore).
USTRUCT(BlueprintType)
struct FAsdaCasketSoundCue
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	int32 Frame = 0;                 // кадр клипа (в кадрах, AnimFrameRate fps)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "1", ClampMax = "10"))
	int32 CreationSound = 1;         // S_e_creation{01..10}.wav (SFT.BIN id 45..54)
};

UCLASS(BlueprintType)
class ASDAREBORN_API UAsdaLoginConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UAsdaLoginConfig();

	// ПРИМЕЧАНИЕ: арт-параметры КАЖДОГО глифа (масштаб, EffectEmitterScale, спин) редактируются
	//   ПЕР-АКТОРНО на самих AsdaEffectActor (CharSelectGlyph_slot0/1/2) в уровне — Editor-Friendly.
	//   Здесь — только сценовые runtime-величины, которые GameMode применяет к рождаемым сущностям.

	// --- Персонаж в слоте / карусель ---
	// ★★★ 12.09.2026: было 1.f. Меши превью/шкатулки запечены glTF-импортом в ×100 (превью персонажа
	//   15 592 юн = 156 м вместо 1.56 м), а сцена логина переведена в ×1 (как все игровые карты,
	//   см. GLoginScale в AsdaLoginGameMode.cpp). Поэтому актору превью нужен масштаб 0.01.
	//   Меши НЕ переимпортируем: их делят системы одержимости/«души», переимпорт — отдельная работа.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	float CharacterScale = 0.01f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character")
	FVector SlotLocalOffset = FVector::ZeroVector;

	// --- Звук анимаций шкатулки (кадр→звук, латч). Эталон-дефолты в конструкторе. ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	float AnimFrameRate = 30.f;      // fps клипов шкатулки (61к/2.0с ≈ 30)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TArray<FAsdaCasketSoundCue> OpenSoundCues;   // клип открытия (case0)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TArray<FAsdaCasketSoundCue> TurnSoundCues;   // клип поворота L/R (case2/3)

	// --- Помощник «Минопио»: НАШИ строки для НАШИХ веток ------------------------------------------
	// ★★ 16.10.2026: ветка «поле адреса мира пустое» СНЯТА (адрес сервера — постоянный домен, поле SERVER_IP_INPUT не
	//   вводится; AsdaServerAddress.h). Код эти два поля больше не читает; они оставлены, чтобы не менять схему ассета
	//   DA_LoginConfig (правки владельца в ассетах не трогаем). Ниже — история ветки.
	// ★★★ ЗАЧЕМ ОТДЕЛЬНО ОТ `loginhelpertext`. Поле ввода адреса мира `SERVER_IP_INPUT` — НАШЕ:
	//   в оригинальном `login_jui.ini` такого контрола НЕТ ВООБЩЕ (у оригинала выбор мира — окно
	//   `SERVER_SELECT_WIN` со списком `SERVER_LIST`), а максимальный аргумент `uiSelectListItem`
	//   во всём срезе клиента = 55. Значит, записи под эту ветку в игровой таблице нет и быть не
	//   может — раньше код звал `ApplyHelper(100)`, промах молчал, и игрок видел ПУСТОЙ пузырь.
	//   ⚠ Дописать строку в `loginhelpertext.csv`/`DT_loginhelpertext` НЕЛЬЗЯ: они генерируются
	//   конвейером ItemDB, и перегенерация правку сотрёт (закон `itemdb-header-generated-not-manual`);
	//   к тому же игровая таблица обязана оставаться 1:1 с клиентом. Поэтому НАША строка живёт в
	//   НАШЕЙ конфигурации и правится дизайнером прямо в редакторе (правило Editor-Friendly).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helper")
	FString ServerAddrEmptyText;          // сырой текст с тегами <F..>, как в loginhelpertext; [ПОРТ] 09.10.2026: пусто = DT_GameText 990400 (язык сессии)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Helper", meta = (ClampMin = "0", ClampMax = "2"))
	int32 ServerAddrEmptyRobotMode = 2;   // god-robot: 0 sation, 1 praise, 2 warning (как у записей 10/11)

	// --- Ассеты сцены (soft refs — редактируемы в редакторе) ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TSoftObjectPtr<class USoundBase> LoginMusic;
	// ★★★ 13.09.2026 — ПЕРЕЛЁТ ВЫПОЛНЯЕТ КАМЕРА (решение владельца): фон и шкатулка стоят в конечной позе
	//   перелёта, LevelSequence ведёт LoginCamera. Так свет, поставленный в редакторе, в игре остаётся на сцене.
	//   Правится в Sequencer. Строит Tools/asda_build_login_flight_sequence.py (режим camera).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TSoftObjectPtr<class ULevelSequence> FlightSequence;
	// Режим перелёта. true (по умолчанию) = летит камера, берётся FlightSequence.
	//   false = ОТКАТ к прежней схеме «сцена летит к неподвижной камере», берётся FlightSequenceSceneMove.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	bool bFlightMovesCamera = true;
	// Прежняя секвенция «сцена летит» (поссессит шкатулку + LoginBackdrop_Flight) — только для отката.
	//   Строит Tools/asda_build_login_flight_sequence.py с ASDA_LOGIN_FLIGHT_MODE=scene.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Assets")
	TSoftObjectPtr<class ULevelSequence> FlightSequenceSceneMove;
};
