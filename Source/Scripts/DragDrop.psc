ScriptName DragDrop Hidden

bool function ReleaseGrabbedActor() native global
bool function ReleaseNPC() native global
bool function ThrowNPC(float force) native global
Actor function GetGrabbedNPC() native global
bool function IsDragging() native global

function RequestGrab(int aiTargetFormID) Global
    Actor target = Game.GetForm(aiTargetFormID) as Actor
    if !target
        return
    endif
    Spell grabSpell = Game.GetFormFromFile(0x800, "DragAndDrop.esp") as Spell
    Actor player = Game.GetPlayer()
    if player && grabSpell
        grabSpell.Cast(player, target)
    endif
EndFunction
