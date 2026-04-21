ScriptName DragDropQuest extends Quest

Actor Property PlayerRef Auto

Event OnInit()
    RegisterForSingleUpdate(2.0)
EndEvent

Event OnUpdate()
    if DragDrop.IsDragging()
        Actor grabbed = DragDrop.GetGrabbedNPC()
        if grabbed
            if !grabbed.IsDead() && !grabbed.IsPlayerTeammate() && grabbed.GetActorValue("Paralysis") <= 0
                DragDrop.ReleaseNPC()
                Debug.Notification(grabbed.GetDisplayName() + " woke up!")
                RegisterForSingleUpdate(1.0)
                return
            endif
        endif
    endif
    RegisterForSingleUpdate(0.5)
EndEvent
