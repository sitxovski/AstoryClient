// Copyright (c) 2026 @sitxovski (Discord)
// AsdaQuestMarkerSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaQuestMarkerSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaQuestMarkerSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaQuestMarkerSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "Quest/AsdaQuestMarkerSubsystem.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR / ASDA_DEV_CONSOLE — дев-ключи и команды вне Shipping

// FAsdaMarkup::StripTags — единая точка разбора тегов <F..>   ★ R3: комментарий снятого включения "UI/AsdaMarkup.h" (файл им не пользовался) — сохранён

#include "Engine/Engine.h"

// Категория журнала частей AsdaQuestMarkerSubsystem*.cpp: определение — AsdaQuestMarkerSubsystem.cpp.
DECLARE_LOG_CATEGORY_EXTERN( LogAsdaQuest, Log, All );

// GAsdaFakeSelfLevel: определение — в домашней части (R2).
extern int32 GAsdaFakeSelfLevel;
// GAsdaFakeSelfClass: определение — в домашней части (R2).
extern int32 GAsdaFakeSelfClass;
// GAsdaFakeSelfSub: определение — в домашней части (R2).
extern int32 GAsdaFakeSelfSub;

// Данные ветки «спец-запись опкод 123» (`questNpc_CollectDeliverableItems@0xD89550`).
//   Шапка `.NSR` нужна ровно за одним полем — `блок+0` = npcId владельца сценария: эталон берёт
//   его из загруженного файла (`npcSlotLoadModelAndSetup@0xD8A6B0`) и по нему копирует запись
//   `SNQInfo.Bin` вызовом `findCopyRecordById16@0xDA11F0`. Имена этих же таблиц объявлены и в
//   `AsdaNpcDialogSubsystem.cpp`, но там они `static` (внутреннее связывание) — переиспользовать
//   нельзя, поэтому здесь свои константы с теми же путями.
static const TCHAR* kNsrHeaderDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcNsrHeader.DT_NpcNsrHeader");
static const TCHAR* kSnqDT       = TEXT("/Game/AsdaAssets/Data/NPC/DT_NpcSnq.DT_NpcSnq");
static const TCHAR* kSnqIctDT    = TEXT("/Game/AsdaAssets/Data/NPC/DT_SnqIct.DT_SnqIct");

// Справочники подсказки робота «Минопио» (окно 599) — импортёр `Tools/asda_import_await_maps.py`.
static const TCHAR* kTimDT = TEXT("/Game/AsdaAssets/Data/NPC/DT_TimList.DT_TimList");

// GAsdaQuestAwaitHint: определение — в домашней части (R2).
extern int32 GAsdaQuestAwaitHint;

// GAsdaQuestLevelCapEnforce: определение — в домашней части (R2).
extern int32 GAsdaQuestLevelCapEnforce;

// GAsdaQuestMarkRebuildPeriod: определение — в домашней части (R2).
extern float GAsdaQuestMarkRebuildPeriod;
