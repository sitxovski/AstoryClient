// Copyright (c) 2026 @sitxovski (Discord)
// AsdaPetCageSubsystemInternal.h — ★ R2 10.11.2026 (разрез AsdaPetCageSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaPetCageSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaPetCageSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "UI/AsdaPetCageSubsystem.h"
#include "UI/AsdaEnterGuard.h"   // ★ 25.10.2026: вопросы этого файла Enter не подтверждает (только щелчок)
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping
#include "UI/AsdaCraftSubsystem.h"   // ★ гейты Г1/Г2: имена окон ремесла 76000/77500 (волна В2)

#include "UI/AsdaJuiWindowHandle.h"
#include "UI/AsdaNotice.h"
#include "UI/AsdaWindowSubsystem.h"
#include "UI/AsdaTipPanel.h"
#include "UI/AsdaUiHelpers.h"
#include "UI/AsdaPetGrowthSubsystem.h"   // ★ волна 4: кнопка 43055 открывает окно 44200
#include "UI/AsdaPetEvolutionSubsystem.h" // ★ волна 5: кнопка 43056 открывает окно 43500
#include "UI/AsdaPetEvoTipSubsystem.h"   // ★ волна 4: наведение на слот показывает окно 43300
#include "UI/AsdaPetPreviewActor.h"      // ★ волна 9: превью 43052 — свой RT 240×170 и вращение
#include "Engine/TextureRenderTarget2D.h"   // дамп размера буфера превью
#include "Materials/MaterialInstanceDynamic.h"   //   кисть кадра для 43052 (и для 43576 окна 43500)
#include "Materials/MaterialInterface.h"        // ★ 24.09: мастер-материал кисти превью
#include "Kismet/KismetRenderingLibrary.h"      // ★ 24.09: выгрузка буфера превью в PNG (asda.Pet.PreviewDumpRt)
#include "Engine/World.h"                //   спавн актора сцены превью
#include "Items/AsdaItemDbRows.h"
#include "Pet/AsdaPetSubsystem.h"
#include "Net/NetOpcodes.h"          // OP_PET_SLOT_COUNT — диагностическая подача 6123
// ★ волна 6: SetSelfWeight — полоса веса из 6109/6122   ★ R3: комментарий снятого включения "AsdaNetSubsystem.h" (файл им не пользовался) — сохранён
#include "AsdaBytes.h"               // ★ волна 6: сборка тел 6109/6122 для стенда

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/EditableText.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
// ★ С-Р1: общий офлайн-гейт отладочных команд (RefuseDebugWhenOnline)   ★ R3: комментарий снятого включения "AsdaOutgoingGate.h" (файл им не пользовался) — сохранён
#include "Login/AsdaLoginData.h"   // [ПОРТ] 08.10.2026: лимиты ввода в байтах провода (TruncateToWireLimit)
#include "Test/AsdaSelfTestKit.h"   // ★ 21.10.2026: итог самотеста asda.Pet.SelfTestNameRule
#include "Text/AsdaLang.h"          // ★ 21.10.2026: английские названия пород (оверлей pettable) в самотесте кличек
#include "Text/AsdaLangRows.h"       // [ПОРТ] 09.10.2026 (мультиязычность): формат опции питомца на языке сессии

// Категория журнала частей AsdaPetCageSubsystem*.cpp: определение — AsdaPetCageSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaPetCage, Log, All );

// ── Идентификаторы и пути трёх окон ──
static const FName  kCageWindowId  (TEXT("Pet_Cage_Win"));
static const FName  kPanelWindowId (TEXT("Pet_Button_Win"));
static const FName  kStatusWindowId(TEXT("Pet_Status_Win"));

// ── Окна-гейты открытия 43000 (см. шапку .h, Г1…Г4). Имена — как в реестре окон порта. ──
// ★ Волна В2 ремесла (25.09.2026): имена берутся из ЕДИНОЙ точки `UAsdaCraftSubsystem`. Прежнее имя Г2
//   `ITEM_QtyMake_Dlg` в `.JUI` не существует (окно `g_pItemQtyMakeDlg` = `ITEM_making_Mark_WIN` 77500,
//   `gamemain_jui.ini` L365810) — гейт Г2 не срабатывал никогда (дефект порта, ревизия ремесла §6).
static const FName kGateCraftWindow  (UAsdaCraftSubsystem::kMainWindowId);   // Г1 g_pItemMainWnd    — 76000
static const FName kGateQtyWindow    (UAsdaCraftSubsystem::kMakeWindowId);   // Г2 g_pItemQtyMakeDlg — 77500
static const FName kGateEvolutionWin (TEXT("Pet_Evolution_Win"));   // Г3 43500
static const FName kGateLimitBreakWin(TEXT("Pet_LimitBreak_Win"));  // Г4 44200

namespace PetCageLoc {
	constexpr int32 kBtn43053     = 100602;  // :4098  43053 «Призыв»

	constexpr int32 kExpLabel     = 100606;  // :4122  43074 «Опыт»

	constexpr int32 kNutLabel     = 100608;  // :4131  43080 «Питание»

	constexpr int32 kNameLocked   = 101118;  // :3970  43083, имя уже меняли
}

// ★ [ПОРТ] 21.10.2026 (владелец: «запретить ставить клички на арабском и русском»): кличка — только латинские буквы и цифры
//   (символы-разделители эталонный фильтр выше уже выбросил). Сервер проверяет то же (процедура `UPPetRename` → код 2), здесь —
//   чтобы игрок увидел понятную причину до отправки, а не общий отказ «запрещённое слово».
static bool IsLatinPetName(const FString& S)
{
	if (S.IsEmpty()) { return false; }
	for (const TCHAR C : S)
	{
		if (!((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('0') && C <= TEXT('9')))) { return false; }
	}
	return true;
}
