#pragma once   // ★ включается из нескольких .cpp (unity-сборка склеивает их в одну единицу)
// ★★★ 09.11.2026 [ПОРТ, решения владельца] ВСТРОЕННЫЕ СПИСКИ NPC (пачка 20, баг 7). Данные, не правило в коде; живут в бинаре, а не в
//   ini: владелец 09.11 — «why the hell it configures in user-accesible config? Internal mechanics needed» (ini игрок переопределяет
//   в Saved/Config и обходил бы GM-фильтр или подменял модели). Правка списка — здесь, с пересборкой клиента.
//   Источник — полная сверка (Reports/batch20/flammio_npc/full_audit.tsv): клиентский npc_list и map\<папка>\<папка>.NPC против
//   СТОКОВЫХ серверных .npc, файл карты — по NpcServer\ini\MapInfo.ini (карта 4 = Night_Valley.npc, папка alpen_dungeon; карта 5 =
//   LostValley.npc, папка nightvalley = Forgotten Canyon). Серверная часть — P119 + P119r.
//   ★ Записи — ПАРЫ (NPC, карта): один и тот же NPC может стоять на нескольких картах (Arshes 37: сток — Рейн, P119 — Альпия), и
//     служебным он должен быть только там, где его нет в стоке. Владелец 09.11: «Arshez 37 (in alpen) should be in alpen map, but hidden
//     for players!» — в Альпии только GM_, в Рейне — всем.
//   ★ Видны всем, в списках НЕТ: Agnes 7 (Фламмио, владелец «Без префикса»); стоковые NPC, у которых в клиенте порта не было акторов —
//     Forgotten Canyon 21, 108–115, 177, Night Valley 22 (владелец: «yes, but make sure you are not duplicating them»); стражи шахты
//     202–204 (владелец: «okay, then make it visible» — единственный выход из шахты).
namespace AsdaNpcLists
{
	// Пара «NPC на карте».
	struct FNpcOnMap { int32 NpcId; int32 MapId; };

	// NPC «только для GM_» на этой карте: показ (модель, имя, метки, клик, разговор) только персонажу с именем на GM_ (`AsdaNpcGmOnly`).
	//   На сервере их нет ни в одном стоковом .npc этой карты: Альпия (3) — 2 Event Summon Statue, 37 Arshes, 98 Magic Mirror,
	//   99 Celestial Mirror; Fireway (16) — 201 Moving Crystal; Инферион (18) — 200 Statue.
	static const FNpcOnMap kGmOnly[] = { {2, 3}, {37, 3}, {98, 3}, {99, 3}, {201, 16}, {200, 18} };

	// Красный префикс «[MISSING]» перед именем (`AsdaNpcMissing`): те же пары.
	static const FNpcOnMap kMissingPrefix[] = { {2, 3}, {37, 3}, {98, 3}, {99, 3}, {201, 16}, {200, 18} };

	// Модель-замена (стражник): NPC, чьих моделей нет ни в наших ассетах, ни в NpcModel.bnz доноров (Interlude/_MINE/_orig_copy/Fable/
	//   WorldOfDreams/Arabic/XYT/Fantazium): 6 Pablo, 11 Aaron, 44 Demi (карта −1, сервер их не спавнит). Кроме списка замену получает
	//   любой NPC, у меша которого модель не загрузилась.
	static const int32 kModelFallback[] = { 6, 11, 44 };
	static const TCHAR* const kFallbackMesh = TEXT("/Game/AsdaAssets/Models/NPC/npc_m_h_soldier-a_00/SkeletalMeshes/npc_m_h_soldier-a_00.npc_m_h_soldier-a_00");
	static const TCHAR* const kFallbackAnim = TEXT("/Game/AsdaAssets/Models/NPC/npc_m_h_soldier-a_00/SkeletalMeshes/npc_m_h_soldier-a_00anim_00.npc_m_h_soldier-a_00anim_00");

	// Есть ли пара (NpcId, MapId) в списке.
	template <int32 N> static bool Has(const FNpcOnMap (&L)[N], int32 NpcId, int32 MapId)
	{
		for (const FNpcOnMap& E : L) { if (E.NpcId == NpcId && E.MapId == MapId) { return true; } }
		return false;
	}
}
