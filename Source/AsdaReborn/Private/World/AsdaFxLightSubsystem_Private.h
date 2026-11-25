// Copyright (c) 2026 @sitxovski (Discord)
// AsdaFxLightSubsystem_Private.h — ★ R3-c 12.11.2026 (разрез AsdaFxLightSubsystem.cpp): ЗАКРЫТЫЙ заголовок частей AsdaFxLightSubsystem*.cpp.
//   Только нужное нескольким частям: категория журнала (определение — AsdaFxLightSubsystem.cpp), static-помощники и константы
//   без состояния (текст байт в байт, у каждой части своя копия), extern-объявления ключей/состояния
//   (одно определение — в домашней части). Подключать ПОСЛЕ включений пролога.
#pragma once

#include "World/AsdaFxLightSubsystem.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

// GAsdaFxLightBudget: определение — в домашней части (R2).
extern int32 GAsdaFxLightBudget;

// GAsdaFxLightForceShape: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceShape;

// GAsdaFxLightForceSegments: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceSegments;

// GAsdaFxLightForceTiles: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceTiles;

// GAsdaFxLightForceShapeSize: определение — в домашней части (R2).
extern float GAsdaFxLightForceShapeSize;

// GAsdaFxLightForceShapePitch: определение — в домашней части (R2).
extern float GAsdaFxLightForceShapePitch;

// GAsdaFxLightForceScope: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceScope;

// GAsdaFxLightPerEffect: определение — в домашней части (R2).
extern int32 GAsdaFxLightPerEffect;

// GAsdaFxLightScreenBudget: определение — в домашней части (R2).
extern float GAsdaFxLightScreenBudget;

// GAsdaFxLightFadeIn: определение — в домашней части (R2).
extern float GAsdaFxLightFadeIn;

// GAsdaFxLightFadeOut: определение — в домашней части (R2).
extern float GAsdaFxLightFadeOut;

// GAsdaFxLightDetailHold: определение — в домашней части (R2).
extern float GAsdaFxLightDetailHold;

// GAsdaFxLightPerCandMax: определение — в домашней части (R2).
extern int32 GAsdaFxLightPerCandMax;

// GAsdaFxLightRadiusCap: определение — в домашней части (R2).
extern float GAsdaFxLightRadiusCap;

// GAsdaFxLightScale: определение — в домашней части (R2).
extern float GAsdaFxLightScale;

// GAsdaFxLightRadiusScale: определение — в домашней части (R2).
extern float GAsdaFxLightRadiusScale;

// GAsdaFxLightGeomHoldSec: определение — в домашней части (R2).
extern float GAsdaFxLightGeomHoldSec;

// GAsdaFxLightDarkRepickSec: определение — в домашней части (R2).
extern float GAsdaFxLightDarkRepickSec;

// GAsdaFxLightForceColor: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceColor;
// GAsdaFxLightForceRadius: определение — в домашней части (R2).
extern int32 GAsdaFxLightForceRadius;
// GAsdaFxLightForcePos: определение — в домашней части (R2).
extern int32 GAsdaFxLightForcePos;

// GAsdaFxLightWatch: определение — в домашней части (R2).
extern int32 GAsdaFxLightWatch;

// GAsdaFxLightAutoReconcile: определение — в домашней части (R2).
extern int32 GAsdaFxLightAutoReconcile;

// GAsdaFxLightReconcileHz: определение — в домашней части (R2).
extern float GAsdaFxLightReconcileHz;

// GAsdaFxLightEditorPropWatch: определение — в домашней части (R2).
extern int32 GAsdaFxLightEditorPropWatch;

// GAsdaFxLightTraceRegister: определение — в домашней части (R2).
extern int32 GAsdaFxLightTraceRegister;

// GAsdaFxLightMaxCandidates: определение — в домашней части (R2).
extern int32 GAsdaFxLightMaxCandidates;

// GAsdaFxLightEmptyRecheck: определение — в домашней части (R2).
extern int32 GAsdaFxLightEmptyRecheck;

// GAsdaFxLightCeilingSkip: определение — в домашней части (R2).
extern int32 GAsdaFxLightCeilingSkip;
