// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobFxMaterial.cpp — ключи материалов эффектных подсеток мобов/NPC (01.11.2026, mob_fx_audit). Разбор — в заголовке.
#include "Npc/AsdaMobFxMaterial.h"
#include "AsdaDevConsole.h"   // ★ античит А2: ASDA_DEV_CVAR — дев-ключи вне Shipping
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"

// [ИСПРАВЛЕНИЕ] Подсетки с битом очереди рисуются с альфой текстуры (M_AsdaMobFxBlend): 1 — как оригинал; 0 — прежний порт
//   (непрозрачный мастер класса CHAR: «жёлтые конусы» вместо лучей).
static ASDA_DEV_CVAR(int32, CVarAsdaMobFxBlend,
	TEXT("asda.Mob.FxBlend"), 1,
	TEXT("1 = как оригинал: подсетки мобов/NPC с битом очереди и полутоном альфы — прозрачные (материалы __mobfx); "
	     "0 = прежний порт: непрозрачные."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] Пер-типовые текстуры DT_MobTextures на класс-корректных мастерах (__char / __oblend): 1 — так;
//   0 — прежний порт: все пер-типовые MI на M_AsdaObjectLit (masked 0.5).
static ASDA_DEV_CVAR(int32, CVarAsdaMobTexClassMasters,
	TEXT("asda.Mob.TexClassMasters"), 1,
	TEXT("1 = пер-типовые текстуры мобов на мастере класса подсетки (CHAR — непрозрачный, tNoSkin+бит — блендинг); "
	     "0 = прежний порт: все на masked M_AsdaObjectLit."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] Гард DT_MobTextures принимает скелетный вариант клипов базы `<меш>__<база>` (с 30.10 у 30 мешей):
//   1 — так (пер-типовая текстура применяется); 0 — прежний порт: вариант отвергался, оставалась текстура ассета.
static ASDA_DEV_CVAR(int32, CVarAsdaMobTexGuardClipBase,
	TEXT("asda.Mob.TexGuardClipBase"), 1,
	TEXT("1 = пер-типовые текстуры применяются и к скелетным вариантам <меш>__<база>; 0 = прежний порт: не применяются."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] «Запись глубины» прозрачных подсеток-эффектов: компонент с материалом __mobfx пишет Custom Depth, мастер
//   M_AsdaMobFxBlend гасит пиксели дальше ближайшей поверхности — у призрака видна только внешняя оболочка, как в оригинале
//   (очередь рисуется с ZWRITE=1, CRenderer_ApplyAlphaFogRenderState@0x9766A0). 1 — так; 0 — сквозь тело видны внутренние слои.
static ASDA_DEV_CVAR(int32, CVarAsdaMobFxDepth,
	TEXT("asda.Mob.FxDepth"), 1,
	TEXT("1 = прозрачные подсетки мобов показывают только ближнюю к камере поверхность (Custom Depth, как запись глубины "
	     "оригинала); 0 = видны все слои."),
	ECVF_Default);

// [ИСПРАВЛЕНИЕ] Глубина прозрачных деталей экипировки (M_AsdaCharLitBlend, узлы глубины 01.11.2026): 1 — компонент пишет
//   Custom Depth и видна только ближняя поверхность; 0 — без глубины и без прохода Custom Depth (DepthCull = 0 у MID детали).
//   Согласовано с откатом `equip` (mfx_rollback.py ставит умолчание DepthCull = 0 в самом мастере).
static ASDA_DEV_CVAR(int32, CVarAsdaItemAlphaDepth,
	TEXT("asda.Char.ItemAlphaDepth"), 1,
	TEXT("1 = прозрачные детали экипировки показывают только ближнюю к камере поверхность (Custom Depth); "
	     "0 = без глубины и без прохода Custom Depth (видны все слои)."),
	ECVF_Default);

bool AsdaMobFx::ItemAlphaDepthOn()
{
	return CVarAsdaItemAlphaDepth.GetValueOnGameThread() != 0;
}

// Вид материала-«с глубиной» на компоненте: 0 — нет, 1 — M_AsdaMobFxBlend, 2 — M_AsdaCharLitBlend (у MID/MIC — корень цепочки).
static int32 DepthKind(const UMeshComponent* MC)
{
	int32 K = 0;
	for (int32 i = 0; MC && i < MC->GetNumMaterials(); ++i)
	{
		const UMaterialInterface* M = MC->GetMaterial(i);
		const UMaterial* Base = M ? M->GetMaterial() : nullptr;
		if (!Base) { continue; }
		if (Base->GetName() == AsdaMobFx::kFxMaster) { return 1; }
		if (Base->GetName() == AsdaMobFx::kCharBlendMaster) { K = 2; }
	}
	return K;
}

bool AsdaMobFx::HasDepthMaterial(const UMeshComponent* MC)
{
	return DepthKind(MC) != 0;
}

bool AsdaMobFx::WantsCustomDepth(const UMeshComponent* MC)
{
	switch (DepthKind(MC))
	{
	case 1:  return CVarAsdaMobFxDepth.GetValueOnGameThread() != 0;
	case 2:  return CVarAsdaItemAlphaDepth.GetValueOnGameThread() != 0;
	default: return false;
	}
}

bool AsdaMobFx::ClipBaseGuardOn()
{
	return CVarAsdaMobTexGuardClipBase.GetValueOnGameThread() != 0;
}

void AsdaMobFx::ApplyDepth(UMeshComponent* MC)
{
	if (!MC || !HasDepthMaterial(MC)) { return; }   // без материалов «с глубиной» — не наш компонент, его Custom Depth не трогаем
	// Трафарет 0 — наша запись; чужие значения (подсветка цели, пометки свечения 10/11) не трогаем: они сами пишут Custom Depth,
	//   а при их снятии состояние возвращает тот, кто снимает (ApplyGlowStencilToComponents → WantsCustomDepth).
	if (MC->CustomDepthStencilValue != 0) { return; }
	const bool bWant = WantsCustomDepth(MC);
	if (MC->bRenderCustomDepth != bWant) { MC->SetRenderCustomDepth(bWant); }
}

int32 AsdaMobFx::ApplyKeys(UMeshComponent* MC)
{
	if (!MC) { return 0; }
	const bool bFx = CVarAsdaMobFxBlend.GetValueOnGameThread() != 0;            // эффекты прозрачные
	const bool bCls = CVarAsdaMobTexClassMasters.GetValueOnGameThread() != 0;   // класс-корректные пер-типовые MI
	int32 Changed = 0;
	ON_SCOPE_EXIT { ApplyDepth(MC); };   // глубина — по материалам, оставшимся после ключей
	if (bFx && bCls) { return 0; }
	for (int32 i = 0; i < MC->GetNumMaterials(); ++i)
	{
		UMaterialInterface* M = MC->GetMaterial(i);
		if (!M) { continue; }
		FString Name = M->GetName();   // имя ассета; суффиксы снимаются по цепочке: __mobfx, затем __char / __oblend
		bool bStrip = false;           // имя изменилось
		if (!bFx && Name.EndsWith(AsdaMobFx::kFxMatSuffix))
		{
			Name.LeftChopInline(FCString::Strlen(AsdaMobFx::kFxMatSuffix)); bStrip = true;
		}
		if (!bCls)
		{
			for (const TCHAR* Suf : { AsdaMobFx::kCharMatSuffix, AsdaMobFx::kObjBlendMatSuffix })
			{
				if (Name.EndsWith(Suf)) { Name.LeftChopInline(FCString::Strlen(Suf)); bStrip = true; break; }
			}
		}
		if (!bStrip) { continue; }
		// Прежний ассет лежит в той же папке: «<папка>/<имя>.<имя>».
		const FString Pkg = M->GetOutermost()->GetName();
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *FPaths::GetPath(Pkg), *Name, *Name);
		if (UMaterialInterface* Old = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			MC->SetMaterial(i, Old);
			++Changed;
		}
	}
	return Changed;
}
