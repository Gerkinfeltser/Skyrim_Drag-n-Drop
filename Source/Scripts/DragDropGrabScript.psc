Scriptname DragDropGrabScript extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    Debug.Trace("DragAndDrop: Start effect on " + akTarget.GetDisplayName() + " cast by " + akCaster.GetDisplayName())

    Actor grabbed = DragDrop.GetGrabbedNPC()
    if !grabbed
        Debug.Trace("DragAndDrop: No grabbed NPC found")
        return
    endIf

    Debug.Trace("DragAndDrop: " + grabbed.GetDisplayName() + " is currently grabbed")

    float paralysisValue = grabbed.GetActorValue("Paralysis")
    grabbed.SetActorValue("Paralysis", 0.0)
    Utility.Wait(0.1)
    grabbed.PushActorAway(grabbed, 1.0)
    Utility.Wait(0.1)
    grabbed.SetActorValue("Paralysis", paralysisValue)
    Debug.Trace("DragAndDrop: Reset ragdoll for " + grabbed.GetDisplayName() + " (paralysis was " + paralysisValue + ")")
EndEvent