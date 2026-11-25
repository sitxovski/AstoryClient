// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobFxMaterial.h — материалы ЭФФЕКТНЫХ подсеток мобов/NPC и класс-корректные пер-типовые текстуры (01.11.2026, mob_fx_audit).
//
// ★★★ ЭТАЛОН. Подсетка .jbm с битом очереди `mr+0x004 & 1|2` в опак-проходе не рисуется (meshNodeRenderDispatch@0x111E110)
//   и уходит в отсортированную очередь g_RenderQueue. В МИРОВОМ кадре очередь сбрасывается с ВЫКЛЮЧЕННЫМ шейдерным путём:
//   CRenderer_RenderWorldFrame@0x9773B0 (RE/etalon/render_core.c:1423-1433 и 1443-1446: `g_bUseEffectPath = 0`,
//   ALPHABLEND SRCALPHA/INVSRCALPHA, ALPHATEST ≥ 8) → CMeshNode_DrawSkinned@0x1119540 (model_anim.c:24709) берёт не FX-технику
//   tSkin/tRigid (там альфа = 1), а vs1.1 + фиксированную стадию ALPHAOP=MODULATE(TEXTURE, DIFFUSE). Значит у ЛЮБОЙ техники
//   альфа текстуры подсетки с битом = ПРОЗРАЧНОСТЬ: «лучи из глаз» Чепухона (id 806, Object06), призрачные перекраски
//   (текстуры с альфой 115…170), полупрозрачные ленты/вуали NPC. Порт относил tSkin/tRigid с битом к непрозрачному классу
//   CHAR (M_AsdaCharLit) — отсюда жёлтые непрозрачные конусы. Отчёт: Reports/mob_fx_audit/.
//
// ★ ДАННЫЕ (ассеты, скрипты Reports/mob_fx_audit/scripts/):
//   • слот `<слот>_asdafx` — подсетки-эффекты смешанного слота (UAsdaAlphaSplitLib::SplitSlotSubsets);
//   • материал `<имя>__mobfx` — родитель M_AsdaMobFxBlend (translucent, альфа текстуры);
//   • в DT_MobTextures: `MI_<текстура>__char` (родитель M_AsdaCharLit) / `__oblend` (M_AsdaObjectBlendLit) — класс слота;
//     прежде все пер-типовые MI были M_AsdaObjectLit (masked 0.5) и перебивали класс-корректные мастера ассета меша.
// ★ КЛЮЧИ (0 = прежнее поведение): выключенный ключ снимает свой суффикс с имени материала слота — грузится прежний ассет;
// ★ NPC, декор карт, задник создания, лут (решение владельца 01.11.2026: «всё в ассетах») — материалы `__mobfx` назначены В АССЕТАХ
//   (меши; у задника — MIC MI_*_blend в /Models/Backdrop и слотов _blend мешей cre_*, Login.umap не меняется); код только
//   включает Custom Depth (ApplyDepth). Откат категории — скрипт
//   Reports/mob_fx_audit/scripts/mfx_rollback.py (прежние материалы из манифеста).
#pragma once

#include "CoreMinimal.h"

class UMeshComponent;

namespace AsdaMobFx
{
	// Хвост слота подсеток-эффектов (смешанный слот разделён).
	inline constexpr const TCHAR* kFxSlotSuffix = TEXT("_asdafx");
	// Хвост материала-эффекта (родитель M_AsdaMobFxBlend).
	inline constexpr const TCHAR* kFxMatSuffix = TEXT("__mobfx");
	// Имя мастера эффектных подсеток (translucent + «запись глубины» через Custom Depth).
	inline constexpr const TCHAR* kFxMaster = TEXT("M_AsdaMobFxBlend");
	// Имя мастера прозрачных деталей экипировки (те же узлы глубины с 01.11.2026).
	inline constexpr const TCHAR* kCharBlendMaster = TEXT("M_AsdaCharLitBlend");
	// Хвосты класс-корректных пер-типовых MI в DT_MobTextures.
	inline constexpr const TCHAR* kCharMatSuffix = TEXT("__char");
	inline constexpr const TCHAR* kObjBlendMatSuffix = TEXT("__oblend");

	// Ключ глубины прозрачных деталей экипировки (asda.Char.ItemAlphaDepth) — для UAsdaCharAssembly.
	bool ItemAlphaDepthOn();

	// У компонента есть материал на мастере «с глубиной» (M_AsdaMobFxBlend или M_AsdaCharLitBlend).
	bool HasDepthMaterial(const UMeshComponent* MC);

	// Компоненту нужен Custom Depth: материал «с глубиной» и включён его ключ (asda.Mob.FxDepth / asda.Char.ItemAlphaDepth).
	//   Пользуются ApplyDepth и снятие пометок свечения (UAsdaVideoSettingsSubsystem) — чтобы не гасить чужую глубину.
	bool WantsCustomDepth(const UMeshComponent* MC);

	// Ключ гарда DT_MobTextures для скелетных вариантов клипов базы `<меш>__<база>` (true — вариант принимается).
	bool ClipBaseGuardOn();

	// Применить ключи к материалам компонента: суффиксы выключенных исправлений снимаются (материал без суффикса).
	// Вызывать ПОСЛЕ назначения материалов (ассет меша / ApplyMobTextures). Возвращает число заменённых слотов.
	int32 ApplyKeys(UMeshComponent* MC);

	// Custom Depth компонента с материалом «с глубиной» (WantsCustomDepth): мастер по нему оставляет видимой только ближнюю
	// поверхность. Чужой трафарет (подсветка, свечение) не трогается. Зовётся из ApplyKeys, NPC, лута, логина и экипировки.
	void ApplyDepth(UMeshComponent* MC);
}
