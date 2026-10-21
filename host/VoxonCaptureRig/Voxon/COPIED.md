# Copied from GameKit

These files are copied unchanged from GameKit (github.com/jordan608/GameKit), `origin/main` at **2dc424e**. They are
the Voxon SDK's C# wrappers and GameKit's display layer, which starts the SDK, draws voxels with `DrawVox_Batch`,
renders the simulator preview, and checks for hardware. They are copied rather than referenced, so that nothing
outside this repository can change how the app behaves.

| File | GameKit path |
|---|---|
| `VoxonDisplay.cs` | `src/Voxon.GameKit.Display/VoxonDisplay.cs` |
| `PreviewSurface.cs` | `src/Voxon.GameKit.Display/PreviewSurface.cs` |
| `VledRuntime.cs` | `src/Voxon.GameKit.Display/VledRuntime.cs` |
| `VoxonHardwareCheck.cs` | `src/Voxon.GameKit.Display/VoxonHardwareCheck.cs` |
| `Sdk/LedHostCS.cs`, `Sdk/LedWinCS.cs`, `Sdk/VoxonTypes.cs` | `src/Voxon.GameKit.Display/Sdk/` |
| `Voxel.cs` | `src/Voxon.GameKit/Voxelizer/Voxel.cs` |

Their namespaces are GameKit's, kept so that a file can be compared with its original line for line. To take a fix
from GameKit, copy the file again and update the commit above.
