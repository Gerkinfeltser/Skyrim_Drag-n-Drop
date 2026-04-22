Scriptname DragDropReleaseScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if Game.GetPlayerGrabbedRef()
        DragDrop.ReleaseGrabbedActor()
        Self.Dispel()
    endif
EndEvent
