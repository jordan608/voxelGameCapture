--[[
  main.lua -- the UE4SS mod's entry point. It loads the camera rig (rig.lua) and binds F7 to stop and start it.

  The rig arms itself when the game loads and starts capturing once there is a player to follow. It reads its
  settings from voxon_capture_ue.ini beside the game's exe, and from the Voxon Capture Rig app's control file,
  which that ini names.
]]

local ok, rig = pcall(require, "rig")
if not ok then
    print("[VoxonRig] rig.lua did not load: " .. tostring(rig) .. "\n")
    return
end
rig.Start()
RegisterKeyBind(Key.F7, function() ExecuteInGameThread(function() pcall(rig.Toggle) end) end)
