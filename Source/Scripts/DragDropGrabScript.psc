Scriptname DragDropGrabScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    if akTarget.GetActorValue("Paralysis") as float > 0.0
        akTarget.SetActorValue("Paralysis", 0.0)
        Utility.Wait(0.1)
        akTarget.PushActorAway(akTarget, 0.0)
        Utility.Wait(0.1)
        akTarget.SetActorValue("Paralysis", 1.0)
        Debug.Trace("DragAndDrop: Reset ragdoll for " + akTarget.GetDisplayName())
    endif
EndEvent
