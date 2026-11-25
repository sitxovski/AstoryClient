# Copyright (c) 2026 @sitxovski (Discord)
import unreal
p="/Game/AsdaAssets/UI/Player_UI/Premium/WBP_Package_Contents_Win"
bp=unreal.EditorAssetLibrary.load_asset(p)
tree=bp.get_editor_property("widget_tree")
names={"Package_ClsBtn","Package_Contents_Title_grp","Package_Contents_Win_0_0",
       "Package_Contents_Win_0_2","Package_Contents_Move_101_DRAG","Package_Contents_Title_1_0",
       "WinSm_RBar_4_0","Package_Contents_Move"}
def dump(w,parent):
    nm=w.get_name()
    if nm in names:
        s=w.slot
        if isinstance(s,unreal.CanvasPanelSlot):
            lay=s.get_editor_property("layout")
            o=lay.offsets; a=lay.anchors
            unreal.log("[DUMP] %-32s родитель=%-28s L=%.1f T=%.1f W=%.1f H=%.1f anchors=%.2f,%.2f..%.2f,%.2f"
                %(nm,parent,o.left,o.top,o.right,o.bottom,a.minimum.x,a.minimum.y,a.maximum.x,a.maximum.y))
        else:
            unreal.log("[DUMP] %-32s родитель=%-28s слот=%s"%(nm,parent,type(s).__name__ if s else "None"))
    n=0
    try: n=w.get_children_count()
    except Exception: n=0
    for i in range(n):
        try: dump(w.get_child_at(i),nm)
        except Exception: pass
root=tree.get_editor_property("root_widget")
unreal.log("[DUMP] === начало ===")
dump(root,"(root)")
unreal.log("[DUMP] === конец ===")
