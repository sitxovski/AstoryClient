// Copyright (c) 2026 @sitxovski (Discord)
// AsdaMobBrowserData — слой данных обозревателя мобов и NPC (редакторский, только чтение).
//
// ★ ЗАКОН ИСТОЧНИКОВ (владелец 09.11.2026): данные — ТОЛЬКО из тех же ассетов и таблиц, что читает игра; своих CSV/JSON,
//   копий списков и таблиц в коде нет. Таблицы перечитываются при КАЖДОМ открытии окна (`Reload`) — добавили моба/NPC в
//   DataTable, переоткрыли окно — запись на месте. Нет значения — в окне «-».
//
//   МОБЫ: список/имя/модель — `DT_MobNames` (строка = MobId−1; RU — ассет, EN — оверлей `MobNames`), модель через
//     `AsdaEntityText::MobModel` (= Mob list +0x24), скелетный вариант клипов базы — игровая автоген-таблица
//     `AsdaMobClipBase.inl` (тот же файл, что компилирует игра) и ключ `asda.Mob.ClipBaseModel` — как
//     `UAsdaWorldSubsystem::SpawnNetMobDynamic`; клиентские поля — `DT_MobStats` (Mob list.BIN); текстуры — `DT_MobTextures`.
//   NPC: список/имя — `DT_NpcNames` (ключ = NpcId; EN — оверлей `NpcNames`), запись npc_list — `DT_NpcQuestNpc` (ключ NpcId:
//     модель +0x46, сценарий, высота, карта, тип). Модель — `/Models/NPC/<модель>` + клипы `anim_NN` или одиночный `_Anim`,
//     как запекание `asda_place_npcs.py` и `AAsdaNpcActor::SetEntityAnimState`.
//   Боевые числа мобов (атака, защита, меткость…) клиент не читает — они есть только на сервере. Решение владельца 09.11:
//     в окне их НЕТ вовсе (ни строк, ни прочерков): показываются только поля клиентских ассетов.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;

// Вид записи списка.
enum class EAsdaBrowserKind : uint8
{
	Mob,   // моб (DT_MobNames)
	Npc,   // NPC (DT_NpcNames / DT_NpcQuestNpc)
};

// Клиентская запись типа моба (`DT_MobStats`, Mob list.BIN 0x110).
struct FAsdaMobClientStats
{
	bool bValid = false;              // строка MobId−1 в DT_MobStats есть
	int32 Level = 0;                  // +0x98
	int32 MaxHp = 0;                  // +0x94
	int32 Exp = 0;                    // +0x9C
	int32 Element = 0;                // +0xF8 (колонка Rank — мисней): стихия 0..5 (PVP_MARK00..05)
	int32 Rank = 0;                   // +0xFC (колонка Aggression — мисней): 1/5 обычный, 2 именной, 3 босс, 4 элита
	int32 AttackFirst = 0;            // +0x100 — «атакует первым»
	int32 Lives = 0;                  // +0x10C — сегменты полосы HP у боссов
	int32 SizeClass = 0;              // +0x90 — размерный класс
	int32 SizePercent = 100;          // +0x108 — масштаб эффекта удара, %
	float HalfX = 0.f, HalfY = 0.f, HalfZ = 0.f; // +0x84 — полу-размер AABB (высота плашки = 2·HalfY + 50)
	int32 SoundRange = 0;             // +0x104 — радиус звука
};

// Запись NPC (`DT_NpcQuestNpc` = npc_list.BIN).
struct FAsdaNpcInfo
{
	bool bValid = false;              // строка NpcId в DT_NpcQuestNpc есть
	FString ModelName;                // +0x46 — модель и портрет
	FString NsrName;                  // +0x26 — сценарий диалога (.nsr)
	int32 PlateHeight = -1;           // +0x68 — высота сущности (−1 = не задана, игра берёт 150)
	int32 MapId = -1;                 // +0x6C — карта
	float WorldX = 0.f, WorldY = 0.f; // +0x70/+0x74 — точка в мире (игровые см)
	int32 NpcType = 0;                // +0x7C — тип (класс сущности)
};

// Один клип модели.
struct FAsdaMobClip
{
	int32 Index = 0;                  // NN из anim_NN (одиночный клип `_Anim` — 0)
	FString AssetPath;                // путь UAnimSequence для UAsdaModelAnimSubsystem::LoadClipChecked
};

// Запись списка (моб или NPC).
struct FAsdaMobEntry
{
	EAsdaBrowserKind Kind = EAsdaBrowserKind::Mob;
	int32 Id = 0;                     // моб: серверный type (строка таблиц = Id − 1); NPC: NpcId (строка = Id)
	FString NameRu;                   // имя из русского ассета
	FString NameEn;                   // имя из оверлея L10N/en (пусто — перевода нет)
	FString Model;                    // моб: DT_MobNames.Model; NPC: DT_NpcQuestNpc.ModelName
	FAsdaMobClientStats Client;       // только мобы: DT_MobStats
	FAsdaNpcInfo Npc;                 // только NPC

	// Имя на языке обозревателя (EN без перевода → RU; всё пусто → «-»).
	FString DisplayName(bool bEnglish) const;
	// Уровень для списка: моб — DT_MobStats; NPC и моб без строки — INDEX_NONE (в окне «-»).
	int32 DisplayLevel() const;
};

// Разрешённые ассеты модели — по правилу игры.
struct FAsdaMobModelAssets
{
	FString Model;                    // имя модели
	FString SkelModel;                // имя скелетной папки (= Model или вариант «<меш>__<база>» у моба)
	bool bClipBaseVariant = false;    // моб: взят вариант клипов базы (AsdaMobClipBase.inl)
	USkeletalMesh* Skel = nullptr;    // скелетный меш, если есть
	UStaticMesh* Static = nullptr;    // моб: статический фолбэк, если скелета нет
	TArray<FAsdaMobClip> Clips;       // клипы по возрастанию номера
	TArray<TPair<FName, UMaterialInterface*>> MaterialOverrides; // моб: DT_MobTextures слот → материал (гард модели)
};

class FAsdaMobBrowserData : public TSharedFromThis<FAsdaMobBrowserData>
{
public:
	// Общий экземпляр.
	static TSharedRef<FAsdaMobBrowserData> Get();

	// Перечитать игровые таблицы (зовётся при каждом открытии окна и кнопкой Reload).
	bool Reload();

	const TArray<TSharedPtr<FAsdaMobEntry>>& GetMobs() const { return Mobs; }
	const TArray<TSharedPtr<FAsdaMobEntry>>& GetNpcs() const { return Npcs; }
	const TArray<TSharedPtr<FAsdaMobEntry>>& GetList(EAsdaBrowserKind K) const { return K == EAsdaBrowserKind::Npc ? Npcs : Mobs; }
	TSharedPtr<FAsdaMobEntry> Find(EAsdaBrowserKind K, int32 Id) const;
	const FString& GetLoadError() const { return LoadError; }

	// Ассеты модели записи (грузятся при выборе).
	static FAsdaMobModelAssets ResolveModel(const FAsdaMobEntry& E);

	// Подпись клипа по слоту.
	static FString ClipLabel(EAsdaBrowserKind K, int32 Index);
	static FString RankLabel(int32 Rank);
	// Сравнение имён по правилам языка (ICU, без учёта регистра) — сортировка списка.
	static int32 CompareNames(const FString& A, const FString& B);
	// Нижний регистр по правилам языка (ICU) — поиск.
	static FString SearchKey(const FString& S);
	static FString ElementLabel(int32 Element);

	// Пути таблиц (объектные пути ассетов игры).
	static const TCHAR* NpcRecordTablePath();

private:
	void LoadMobs();                  // DT_MobNames + DT_MobStats
	void LoadNpcs();                  // DT_NpcNames + DT_NpcQuestNpc

	TArray<TSharedPtr<FAsdaMobEntry>> Mobs;        // по возрастанию Id
	TArray<TSharedPtr<FAsdaMobEntry>> Npcs;        // по возрастанию Id
	TMap<int32, TSharedPtr<FAsdaMobEntry>> MobById;
	TMap<int32, TSharedPtr<FAsdaMobEntry>> NpcById;
	FString LoadError;
};
