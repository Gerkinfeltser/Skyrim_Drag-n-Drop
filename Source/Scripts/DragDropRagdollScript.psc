Scriptname DragDropRagdollScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    float paralysisValue = akTarget.GetActorValue("Paralysis")
    if paralysisValue <= 0.0
        Debug.Trace("DragAndDrop: RagdollEffect skipped (paralysis=0) on " + akTarget.GetDisplayName())
        return
    endIf

    Debug.Trace("DragAndDrop: RagdollEffect start on " + akTarget.GetDisplayName() + " (paralysis=" + paralysisValue + ")")

    akTarget.SetActorValue("Paralysis", 0.0)
    Utility.Wait(0.1)
    akTarget.PushActorAway(akTarget, 1.0)
    Utility.Wait(0.2)
    akTarget.SetActorValue("Paralysis", paralysisValue)

    Debug.Trace("DragAndDrop: RagdollEffect done for " + akTarget.GetDisplayName())
EndEvent