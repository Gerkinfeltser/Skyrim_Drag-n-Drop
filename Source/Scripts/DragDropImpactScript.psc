Scriptname DragDropImpactScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Debug.Trace("DragAndDrop: ImpactHit fired! target=" + akTarget + " caster=" + akCaster)

    if !akCaster || !akTarget
        Debug.Trace("DragAndDrop: ImpactHit ABORT - null actor")
        return
    endIf

    if akTarget == akCaster
        Debug.Trace("DragAndDrop: ImpactHit ABORT - target==caster")
        return
    endIf

    Debug.Trace("DragAndDrop: ImpactHit checking " + akTarget.GetDisplayName() + " (teammate=" + akTarget.IsPlayerTeammate() + " dead=" + akTarget.IsDead() + ")")

    if akTarget.IsPlayerTeammate()
        return
    endIf

    if akTarget.IsDead()
        return
    endIf

    akTarget.PushActorAway(akCaster, 5.0)
    Debug.Trace("DragAndDrop: ImpactHit pushed " + akTarget.GetDisplayName() + " from " + akCaster.GetDisplayName())
EndEvent
