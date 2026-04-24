Scriptname DragDropRagdollScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Debug.Trace("DragAndDrop: RagdollEffect start on " + akTarget.GetDisplayName() + " (paralysis=" + akTarget.GetActorValue("Paralysis") + ")")

    if akTarget.GetActorValue("Paralysis") > 0.0
        akTarget.SetActorValue("Paralysis", 0.0)
        Utility.Wait(0.1)
    endIf

    akTarget.PushActorAway(akTarget, 1.0)
    Utility.Wait(0.05)

    Debug.Trace("DragAndDrop: RagdollEffect done for " + akTarget.GetDisplayName() + " (paralysis after=" + akTarget.GetActorValue("Paralysis") + ")")
EndEvent