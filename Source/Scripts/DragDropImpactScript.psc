Scriptname DragDropImpactScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if !akCaster || !akTarget
        return
    endIf

    if akTarget == akCaster
        return
    endIf

    if akTarget.IsPlayerTeammate()
        return
    endIf

    if akTarget.IsDead()
        return
    endIf

    akCaster.PushActorAway(akTarget, 5.0)
    Debug.Trace("DragAndDrop: PushActorAway " + akTarget.GetDisplayName() + " from " + akCaster.GetDisplayName())
EndEvent
