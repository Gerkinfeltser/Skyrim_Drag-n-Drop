Scriptname DragDropGrabScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Debug.Trace("DragAndDrop: Start effect on " + akTarget.GetDisplayName() + " cast by " + akCaster.GetDisplayName())
    ; float paralysisValue = akTarget.GetActorValue("Paralysis")
    ; akTarget.SetActorValue("Paralysis", 0.0)
    ; Utility.Wait(0.1)
    ; akTarget.PushActorAway(akTarget, 0.0)
    ; Utility.Wait(0.1)
    ; akTarget.SetActorValue("Paralysis", paralysisValue)
    ; Debug.Trace("DragAndDrop: Reset ragdoll for " + akTarget.GetDisplayName())
EndEvent
