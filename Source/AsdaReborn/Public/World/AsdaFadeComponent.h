// Copyright (c) 2026 @sitxovski (Discord)
// UAsdaFadeComponent — фейд-ин/аут актора через временный свап материалов на translucent M_AsdaPreviewFade
//   (Tex+FadeIn; asda_build_preview_fade_mat.py). ЗАКОН фейдов: общие masked-мастера НЕ трогаем (бинарный
//   порог/дизер ломает мир) — частная фича = отдельный материал + свап (ui_charselect_fixes.md §7).
// Использование: сетевые NPC/мобы (спавн 4013/4067 = фейд-ин, деспавн 4011 = фейд-аут + Destroy владельца)
//   и ИГРОКИ (свой + чужие): вход в мир/воскрешение (PlayEntryFx) и спавн соседа 4005/4065 — фейд-ин 1.5 с
//   (эталон бит 1 флагов +0x1E4, alpha=f146/1500). Поздний экип подхватывается AdoptSlots в тике.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/MeshComponent.h"   // ★ В2-C (F9): шаблон AsdaTranslucentSlots::Adopt обходит слоты меш-компонентов
#include "AsdaFadeComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

// ★★★ ИСПРАВЛЕНИЕ 11.10.2026 (дедуп №2, В2-C, F9): ОДИН СВАП СЛОТОВ НА ПОЛУПРОЗРАЧНЫЙ MID.
//
// ЧТО БЫЛО. Подмену материала слота на полупрозрачный MID делали ТРИ копии: фейд (`UAsdaFadeComponent::AdoptSlots`),
//   «душа»/GM-hide (`AAsdaPlayerCharacter::ApplySoulTranslucency`) и превью выбора персонажа
//   (`AAsdaLoginGameMode::CollectPreviewFadeComps`). Разошлись они в ИМЕНИ MID: «душа» называла его
//   `MIDSoul_<исходник>`, потому что система сборки ищет секции базового тела ПО ИМЕНИ материала слота
//   (`UAsdaCharAssembly`: `face` — лицо, `hair` — причёска, `chst/pant/shoe` — части под бронёй); фейд и логин создавали
//   БЕЗЫМЯННЫЙ MID (`MaterialInstanceDynamic_N`). Итог: экипировка/лицо, приехавшие в окно фейд-ина 1,5 с (поздний
//   экип 0xFB5 и батчи приходят именно туда), не находили свою секцию — лицо не применялось, база под нагрудником
//   оставалась видимой до следующей пересборки.
// СТАЛО. `MakeSwapMid` — одна точка создания MID-подмены (имя `<Prefix>_<исходник>`, перенос `Tex`, скаляров и альфы),
//   `Adopt` — один обход «найти или подменить слот» для фейда и «души» (у логина свой учёт записей — он зовёт
//   только `MakeSwapMid`). Поведение «души» не меняется (её имя и скаляры — те же); у фейда и логина меняется имя MID.
namespace AsdaTranslucentSlots
{
	// MID-подмена материала слота `Cur` на мастере `Base` (владелец объекта — `Outer`).
	//   nullptr — у `Cur` нет текстуры `Tex` (чужой мастер: такой слот не подменяется) или `Cur`/`Base` пусты.
	//   Имя — `<Prefix>_<имя Cur>` (уникальное в `Outer`): поиск секций сборки по имени материала переживает подмену.
	//   Переносится `Tex`, затем скаляры `CopiedScalars` (только те, что у `Cur` есть), затем `AlphaParam = Alpha`.
	ASDAREBORN_API UMaterialInstanceDynamic* MakeSwapMid(UMaterialInterface* Cur, UMaterialInterface* Base, UObject* Outer,
		const TCHAR* Prefix, FName AlphaParam, float Alpha, TConstArrayView<const TCHAR*> CopiedScalars = TConstArrayView<const TCHAR*>());

	// Обход слотов `Targets`: слот, где уже стоит наш MID, пропускается; иначе (новый слот или материал сменили поверх
	//   нашего MID — поздний экип) — подмена через `Make(Cur)` и запись в `Swaps` (поля записи: Comp, Slot, Orig, Mid).
	//   `Skip(Cur)` — вызывающий отказывается подменять этот материал (например, чужой полупрозрачный MID).
	//   Возврат — сколько слотов подменено этим вызовом.
	template<typename TSwap, typename TMake, typename TSkip>
	int32 Adopt(TConstArrayView<UMeshComponent*> Targets, TArray<TSwap>& Swaps, TMake&& Make, TSkip&& Skip)
	{
		int32 NNew = 0;
		for (UMeshComponent* Cm : Targets)
		{
			if (!Cm) { continue; }
			const int32 N = Cm->GetNumMaterials();
			for (int32 s = 0; s < N; ++s)
			{
				UMaterialInterface* Cur = Cm->GetMaterial(s);
				if (!Cur) { continue; }
				TSwap* Known = Swaps.FindByPredicate([Cm, s](const TSwap& Sw) { return Sw.Comp.Get() == Cm && Sw.Slot == s; });
				if (Known && Cur == Known->Mid) { continue; }   // слот уже наш
				if (Skip(Cur)) { continue; }
				UMaterialInstanceDynamic* Mid = Make(Cur);
				if (!Mid) { continue; }                          // слот без Tex — не наш мастер
				if (Known) { Known->Orig = Cur; Known->Mid = Mid; }
				else
				{
					TSwap Sw;
					Sw.Comp = Cm; Sw.Slot = s; Sw.Orig = Cur; Sw.Mid = Mid;
					Swaps.Add(MoveTemp(Sw));
				}
				Cm->SetMaterial(s, Mid);
				++NNew;
			}
		}
		return NNew;
	}
}

UCLASS()
class ASDAREBORN_API UAsdaFadeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAsdaFadeComponent();

	// Запустить фейд. bIn=true: 0→1 и по завершении ВЕРНУТЬ исходные материалы (+ самоуничтожение компонента).
	// bIn=false: 1→0; по завершении bDestroyOwner? Destroy владельца : только СКРЫТЬ (запечённый NPC — не уничтожать,
	//   он актор уровня; вернуть материалы + hide). Повторный вызов перезапускает.
	void BeginFade(bool bIn, float Duration = 1.0f, bool bDestroyOwner = true);

	// ★★★ Аудит №4 (B3): ЕДИНАЯ ТОЧКА ЗАПУСКА ФЕЙДА — «найти или создать» + регистрация + `BeginFade`.
	//
	// ЗАЧЕМ. Мест запуска фейда семь, и ШЕСТЬ из них делали безусловный `NewObject` — то есть
	//   повторный фейд на том же акторе (AoI-реген поверх фейд-ина, деспавн поверх появления,
	//   уход хранителя поверх его же появления) вешал ВТОРОЙ компонент. Два компонента на одном
	//   акторе фатальны: первый уже свапнул материалы слотов на свои MID, второй в `AdoptSlots`
	//   принимает ЧУЖОЙ MID за «оригинал» и запоминает его в `Orig` — после завершения фейдов актор
	//   остаётся на фейд-материале с чужой альфой. Седьмое место (`AsdaWorldSubsystem.cpp:686`)
	//   эту аварию уже задокументировало и делало find-or-create руками; теперь так делают все.
	//
	// `BeginFade` сам корректно перезапускается поверх активного фейда (возвращает исходники и
	//   собирает свапы заново), поэтому переиспользование компонента — это и есть правильный путь.
	//
	// `CreateName` — имя ТОЛЬКО для вновь создаваемого компонента (диагностика в дампах актора);
	//   при переиспользовании существующего игнорируется. NAME_None = авто-имя, как было раньше.
	// Возвращает компонент, либо nullptr если владельца нет.
	static UAsdaFadeComponent* Begin(AActor* Owner, bool bIn, float Duration = 1.0f,
	                                 bool bDestroyOwner = true, FName CreateName = NAME_None);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ★★★ СНЯТЬ ИДУЩИЙ ФЕЙД-ИН: вернуть исходные материалы и уничтожить компонент. Фейд-аут не
	//   трогается (актор уходит — ему всё равно). Зовёт `AAsdaPlayerCharacter::SetSoulTranslucent(true)`:
	//   [ОРИГИНАЛ] у «души» (SoulState==1) спавн-фейд НЕ умножается — ветка `+2241 == 1` тика
	//   `charEntityUpdateTick :80703` ставит ровно 0.3 и до ветки фейда не доходит. Если 6152 со
	//   значением 1 пришёл ПОСЛЕ спавна, фейд уже держит слоты на своих MID — их нельзя принять за
	//   исходники, поэтому фейд снимается целиком, а не ждётся его конец.
	void CancelFadeIn();

	// ★★ 19.09.2026 (glow_fix4, найдено прогоном G1): ССЫЛКИ СВАПОВ ДЛЯ СБОРЩИКА МУСОРА. `FSlotSwap` — не USTRUCT,
	//   его `Orig`/`Mid` сборщик не видел: исходный материал слота после свапа держала ТОЛЬКО эта запись, и
	//   рантайм-экземпляр (MID детали персонажа) собирался посреди фейда — `RestoreMaterials` ставил в слот
	//   освобождённый объект (EXCEPTION_ACCESS_VIOLATION, AsdaFadeComponent.cpp:128 из TickComponent :155,
	//   game_G1.log прогона). Теперь оба указателя каждой записи — ссылки компонента.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	void ApplyAlpha(float Alpha);
	void RestoreMaterials();
	// Подхват слотов, появившихся/переустановленных ПОСЛЕ старта фейда (экип игрока приходит пакетами
	//   следом за спавном): новый меш-слот с Tex-материалом свапается на фейд-MID с ТЕКУЩЕЙ альфой,
	//   а слот, чей материал заменили внешним кодом поверх нашего MID, — пере-свапывается заново.
	void AdoptSlots(float Alpha);

	// (компонент, слот) → исходный материал; фейд-MID параллельным массивом.
	struct FSlotSwap
	{
		TWeakObjectPtr<class UMeshComponent> Comp;
		int32 Slot = 0;
		TObjectPtr<class UMaterialInterface> Orig;
		TObjectPtr<class UMaterialInstanceDynamic> Mid;
	};
	TArray<FSlotSwap> Swaps;
	TObjectPtr<class UMaterialInterface> FadeBase;   // кэш M_AsdaPreviewFade на время фейда (для AdoptSlots)
	bool bFadeIn = true;
	bool bDestroyOnDone = true;   // fade-out: Destroy владельца (клон) vs только hide (запечённый NPC)
	float Dur = 1.f;
	float T = -1.f;   // <0 = неактивен
};
