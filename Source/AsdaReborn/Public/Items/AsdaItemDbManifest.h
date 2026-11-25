// Copyright (c) 2026 @sitxovski (Discord)
// AsdaItemDbManifest.h — АВТО-СГЕНЕРИРОВАНО gen_itemdb_structs.py.
// Список DataTable item-БД: {имя таблицы, /Game-путь ассета, ключевое поле}.
// ⚠ В массив иногда дописывают строки ВРУЧНУЮ (таблицы, которых нет в реестре
//   декодера). Перегенерация их сотрёт — сверяйтесь с git/бэкапом перед запуском.
#pragma once
#include "CoreMinimal.h"

namespace AsdaItemDb
{
	struct FTableInfo { const TCHAR* Table; const TCHAR* DataTablePath; const TCHAR* KeyField; };
	static const FTableInfo GTables[] = {
		{ TEXT("item_list"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_list.DT_item_list"), TEXT("ItemCode") },
		{ TEXT("item_img_table"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_img_table.DT_item_img_table"), TEXT("") },
		{ TEXT("item_tooltip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_tooltip.DT_item_tooltip"), TEXT("ItemCode") },
		{ TEXT("item_upgrade_cost"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_upgrade_cost.DT_item_upgrade_cost"), TEXT("") },
		{ TEXT("item_upgrade_price"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_item_upgrade_price.DT_item_upgrade_price"), TEXT("") },
		{ TEXT("itemoptiontable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemoptiontable.DT_itemoptiontable"), TEXT("") },
		{ TEXT("itemoptionvalue"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemoptionvalue.DT_itemoptionvalue"), TEXT("") },
		{ TEXT("itemoptionrange"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemoptionrange.DT_itemoptionrange"), TEXT("") },
		{ TEXT("premiumleveltable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_premiumleveltable.DT_premiumleveltable"), TEXT("") },
		{ TEXT("making_recipe"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_making_recipe.DT_making_recipe"), TEXT("") },
		{ TEXT("making_rate"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_making_rate.DT_making_rate"), TEXT("") },
		{ TEXT("itemchangegrade"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemchangegrade.DT_itemchangegrade"), TEXT("") },
		{ TEXT("itemopgraft"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_itemopgraft.DT_itemopgraft"), TEXT("") },
		{ TEXT("special_itemlist"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_special_itemlist.DT_special_itemlist"), TEXT("") },
		{ TEXT("specialitem_tooltiplist"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_specialitem_tooltiplist.DT_specialitem_tooltiplist"), TEXT("") },
		{ TEXT("specialitem_buftooltiplist"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_specialitem_buftooltiplist.DT_specialitem_buftooltiplist"), TEXT("") },
		{ TEXT("event_item_list"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_event_item_list.DT_event_item_list"), TEXT("") },
		{ TEXT("booster_list"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_booster_list.DT_booster_list"), TEXT("") },
		{ TEXT("premium_gatya"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya.DT_premium_gatya"), TEXT("") },
		{ TEXT("premium_gatya_data"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_premium_gatya_data.DT_premium_gatya_data"), TEXT("") },
		{ TEXT("packagelist"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_packagelist.DT_packagelist"), TEXT("") },
		{ TEXT("packagetable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_packagetable.DT_packagetable"), TEXT("") },
		{ TEXT("pricetable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_pricetable.DT_pricetable"), TEXT("ItemCode") },
		{ TEXT("decompositiontable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_decompositiontable.DT_decompositiontable"), TEXT("") },
		{ TEXT("graderate"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_graderate.DT_graderate"), TEXT("") },
		{ TEXT("upgradetable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_upgradetable.DT_upgradetable"), TEXT("") },
		{ TEXT("refinehigh"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_refinehigh.DT_refinehigh"), TEXT("") },
		{ TEXT("refineupmoney"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_refineupmoney.DT_refineupmoney"), TEXT("") },
		{ TEXT("advalue"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_advalue.DT_advalue"), TEXT("") },
		{ TEXT("combo_time"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_combo_time.DT_combo_time"), TEXT("") },
		{ TEXT("charequiptable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_charequiptable.DT_charequiptable"), TEXT("") },
		{ TEXT("charexp"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_charexp.DT_charexp"), TEXT("") },
		{ TEXT("smcharexp"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_smcharexp.DT_smcharexp"), TEXT("") },
		{ TEXT("hairtable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_hairtable.DT_hairtable"), TEXT("") },
		{ TEXT("jobtype"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_jobtype.DT_jobtype"), TEXT("") },
		{ TEXT("sowell_list"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_sowell_list.DT_sowell_list"), TEXT("Id") },
		{ TEXT("sowell_tooltip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_sowell_tooltip.DT_sowell_tooltip"), TEXT("Id") },
		{ TEXT("sowell_calc_table"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_sowell_calc_table.DT_sowell_calc_table"), TEXT("") },
		{ TEXT("title_list"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_list.DT_title_list"), TEXT("Id") },
		{ TEXT("title_list_old"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_list_old.DT_title_list_old"), TEXT("") },
		{ TEXT("title_effect"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_effect.DT_title_effect"), TEXT("") },
		{ TEXT("title_option"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_option.DT_title_option"), TEXT("") },
		{ TEXT("title_rank"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_title_rank.DT_title_rank"), TEXT("") },
		{ TEXT("soulmateskilltable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskilltable.DT_soulmateskilltable"), TEXT("") },
		{ TEXT("soulsoulmatetext"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulsoulmatetext.DT_soulsoulmatetext"), TEXT("") },
		{ TEXT("soulmateskill"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulmateskill.DT_soulmateskill"), TEXT("") },
		{ TEXT("petexptable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_petexptable.DT_petexptable"), TEXT("") },
		{ TEXT("petoptiontable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_petoptiontable.DT_petoptiontable"), TEXT("") },
		{ TEXT("pettable"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_pettable.DT_pettable"), TEXT("Id") },
		{ TEXT("skilltooltip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_skilltooltip.DT_skilltooltip"), TEXT("") },
		{ TEXT("emotiontip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_emotiontip.DT_emotiontip"), TEXT("") },
		{ TEXT("loginhelpertext"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_loginhelpertext.DT_loginhelpertext"), TEXT("") },
		{ TEXT("stateinfotip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_stateinfotip.DT_stateinfotip"), TEXT("") },
		{ TEXT("soulstateinfotip"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_soulstateinfotip.DT_soulstateinfotip"), TEXT("") },
		{ TEXT("skill_info"), TEXT("/Game/AsdaAssets/Data/ItemDB/DT_skill_info.DT_skill_info"), TEXT("") },
	};
	static constexpr int32 GTableCount = UE_ARRAY_COUNT(GTables);   // записей в реестре: 55
}