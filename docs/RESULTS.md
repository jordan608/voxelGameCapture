# The camera rig, measured

> Measured while the rig was developed in VoxelSteamTap (2026-10-11), on The Ascent and an RTX 3060 Ti, with the
> host of that time (Voxel64.Host). Paths and names below are that project's. **Section 10 is the current state**:
> six cameras, the scene-buffer fix, and a host that keeps up. **Section 11 is the Unity rig**, on Risk of Rain 2,
> measured in this project (2026-10-12).

What six scene-capture cameras cost a shipped Unreal game, and what reaches the display. **Read
[`../CLAUDE.md`](../CLAUDE.md) first**; the rig itself is [`PLAN.md`](PLAN.md) STATE. Measured 2026-10-11 on
Machine C.

| | |
|---|---|
| Game | The Ascent, UE 4.26, Steam build 9339875, 2580 x 1490 windowed, the character standing still in the same workshop |
| GPU | RTX 3060 Ti (8 GB). The game is GPU-bound: 94% utilisation with five cameras running |
| Capture | 30 captures a second, every camera each time; the merge at 56 cells a display unit (a VX2-XL's 448 across) |
| Display | Voxel64.Host's LedWin **simulator**, on the same GPU as the game. On the hardware the volume rate is the device's |

**How each cell was measured.** The camera count and size were set through the host's control file; the add-on
logs a line every 5 s. Each cell is the second such window after the cameras were rebuilt (the first holds the
rebuild). *Game fps* is the game's own presents. *VPS* is the host's volumes drawn a second and *new/s* the new
volumes it received a second, from the host's once-a-second status line. *Voxels* is a volume after the merge, and
*merged* the share of pixels that landed in a display cell another pixel had already filled.

**The display columns understate the host.** Every VPS and *new/s* figure in sections 1-4 was taken while the host
was stalling its own render loop four times a second, and the add-on was dropping a volume in five at the pipe
(section 9, both fixed the same day). Since the fixes the add-on sends every volume it decodes and the host takes
every one; the game-side columns are unaffected.

**Section 10 supersedes sections 1-2 for six cameras**: it was measured after a second fix, to the scene buffers,
which tripled the frame rate wherever a camera was larger than the game's window.

**Compare within a table.** The game's own frame rate with no capture differed between sessions in the same room
(D3D12: 234 fps in the orthographic run, 180 in the perspective one), so the per-camera costs in section 5 are taken
against each run's own capture-off baseline.

**What the camera sizes mean.** The size is the width in pixels. The top and bottom cameras are square; the four side
cameras are half as tall, because a VX2-XL is twice as wide as it is tall. Six cameras at 1024 are 4.2 million pixels.
The cameras are added in this order: top, X+, Y+, X-, Y-, bottom.

---

## 1. Perspective cameras (the default), D3D12

The rig's default since 2026-10-11: UE4 drops dynamic lights and shadows from orthographic captures
([`PLAN.md`](PLAN.md) STATE, [`../DEVIATIONS.md`](../DEVIATIONS.md) §14), so the cameras are ordinary 90-degree lenses
standing back from the volume, the level in front of them clipped away.

Capture off: 178-181 fps.

| Cameras: game fps | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 161 | 160 | 158 | 156 | 153 |
| 2 | 147 | 145 | 143 | 141 | 137 |
| 3 | 138 | 136 | 132 | 130 | 125 |
| 4 | 130 | 126 | 125 | 120 | 115 |
| 5 | 122 | 117 | 114 | 112 | 105 |
| 6 | 115 | 107 | 103 | 100 | 92 |

| Cameras: display VPS (new volumes/s) | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 261 (25) | 201 (24) | 171 (24) | 144 (24) | 127 (24) |
| 2 | 255 (25) | 178 (25) | 147 (23) | 137 (24) | 120 (24) |
| 3 | 248 (25) | 161 (25) | 131 (23) | 116 (24) | 106 (23) |
| 4 | 225 (25) | 141 (23) | 121 (23) | 105 (24) | 97 (23) |
| 5 | 243 (26) | 148 (24) | 125 (23) | 107 (22) | 93 (22) |
| 6 | 192 (25) | 97 (21) | 84 (21) | 71 (20) | 60 (18) |

| Cameras: voxels a volume (merged away) | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 22k (0%) | 86k (0%) | 133k (1%) | 184k (4%) | 240k (29%) |
| 2 | 30k (1%) | 114k (5%) | 168k (10%) | 223k (17%) | 284k (40%) |
| 3 | 39k (2%) | 146k (8%) | 210k (15%) | 269k (24%) | 340k (46%) |
| 4 | 49k (2%) | 177k (10%) | 249k (19%) | 315k (28%) | 401k (48%) |
| 5 | 56k (3%) | 199k (13%) | 277k (22%) | 343k (33%) | 435k (52%) |
| 6 | 114k (2%) | 398k (12%) | 502k (29%) | 587k (42%) | 717k (60%) |

| Cameras: volumes sent/s | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 30.0 | 30.0 | 30.2 | 30.0 | 30.0 |
| 2 | 30.0 | 30.0 | 29.8 | 30.0 | 30.0 |
| 3 | 30.0 | 30.0 | 30.0 | 29.8 | 29.4 |
| 4 | 29.8 | 30.0 | 29.9 | 29.6 | 29.6 |
| 5 | 29.7 | 29.6 | 29.0 | 28.8 | 28.1 |
| 6 | 29.2 | 28.0 | 27.8 | 27.8 | 27.1 |


## 2. Above 1024 px, and denser displays (perspective, D3D12, six cameras)

The rig goes to 2560 px (six cameras side by side in one texture stay inside D3D's 16384). *Merge 56* is today's
VX2-XL; *112* is a display twice as fine on every axis, and *0* no merge at all - every pixel sent.

| Camera px | Merge (cells/unit) | Game fps | Volumes sent/s | Voxels a volume (merged) | Display VPS (new/s) | Worst present | GPU memory |
|---|---|---|---|---|---|---|---|
| 1024 | 56 | 93 | 27.4 | 717k (60%) | 59 (18) | 1.12 ms | 4492 MiB |
| 1280 | 56 | 81 | 26.4 | 849k (70%) | 49 (16) | 1.35 ms | 4670 MiB |
| 1536 | 56 | 38 | 19.0 | 991k (75%) | 53 (10) | 2.14 ms | 7063 MiB |
| 1792 | 56 | 40 | 19.1 | 1162k (79%) | 45 (10) | 3.26 ms | 5643 MiB |
| 2048 | 56 | 32 | 19.8 | 1344k (81%) | 44 (10) | 2.94 ms | 6880 MiB |
| 2304 | 56 | 27 | 19.8 | 1540k (83%) | 32 (11) | 3.63 ms | 4384 MiB |
| 2560 | 56 | 37 | 10.4 | 1773k (84%) | 37 (3) | 32.70 ms | 4938 MiB |
| 1024 | 112 | 90 | 21.2 | 1577k (13%) | 28 (9) | 2.89 ms | 3418 MiB |
| 2048 | 112 | 27 | 10.0 | 2916k (59%) | 22 (5) | 5.05 ms | 6081 MiB |
| 1024 | 0 | not measured: the cameras were not found again after the switch down from 2048 px | | | | | |
| 2048 | 0 | 24 | 1.0 | 7149k (0%) | the pipe broke: 107 MB a volume | 37.51 ms | 6128 MiB |

GPU memory is the whole card's, other programs included, so it is noisy; the game alone sat near 4-5 GB.

## 3. Perspective cameras, D3D11

Capture off: 165-176 fps.

| Cameras: game fps | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 163 | 158 | 155 | 155 | 151 |
| 2 | 148 | 139 | 101 | 106 | 87 |
| 3 | 127 | 109 | 105 | 78 | 78 |
| 4 | 109 | 97 | 92 | 72 | 68 |
| 5 | 91 | 81 | 80 | 64 | 58 |
| 6 | 82 | 65 | 66 | 48 | 45 |

| Cameras: display VPS (new volumes/s) | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 143 (25) | 125 (24) | 120 (25) | 106 (25) | 91 (25) |
| 2 | 147 (26) | 125 (25) | 105 (25) | 95 (23) | 82 (24) |
| 3 | 159 (26) | 125 (25) | 111 (25) | 95 (22) | 84 (22) |
| 4 | 161 (27) | 124 (25) | 108 (26) | 92 (22) | 85 (22) |
| 5 | 164 (26) | 128 (24) | 107 (24) | 96 (21) | 84 (21) |
| 6 | 147 (24) | 92 (20) | 82 (21) | 68 (17) | 59 (17) |

| Cameras: voxels a volume (merged away) | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 22k (0%) | 86k (0%) | 133k (1%) | 184k (4%) | 240k (29%) |
| 2 | 30k (1%) | 114k (5%) | 168k (10%) | 223k (17%) | 285k (40%) |
| 3 | 39k (2%) | 146k (8%) | 210k (15%) | 269k (24%) | 340k (46%) |
| 4 | 49k (2%) | 177k (10%) | 249k (19%) | 315k (28%) | 401k (48%) |
| 5 | 56k (3%) | 199k (13%) | 277k (22%) | 343k (33%) | 435k (52%) |
| 6 | 114k (2%) | 398k (12%) | 502k (29%) | 587k (42%) | 717k (60%) |

| Cameras: volumes sent/s | 256 px | 512 px | 640 px | 768 px | 1024 px |
|---|---|---|---|---|---|
| 1 | 30.0 | 30.0 | 30.0 | 30.0 | 30.0 |
| 2 | 30.0 | 30.0 | 25.8 | 30.0 | 28.4 |
| 3 | 30.0 | 29.8 | 29.8 | 27.4 | 27.4 |
| 4 | 29.8 | 29.2 | 28.9 | 26.2 | 26.8 |
| 5 | 28.9 | 27.8 | 27.0 | 25.5 | 28.0 |
| 6 | 26.1 | 25.3 | 24.1 | 24.5 | 25.0 |


## 4. Orthographic cameras, D3D12 and D3D11 (before the switch)

The plugin's own cameras, measured the same day before the lighting was found missing. Faster - the lit passes
the lenses now pay for were being skipped - and wrong on UE4. Capture off: D3D12 228-240 fps, D3D11 234-244.

Game fps, every cell delivering 29.4-30.2 volumes a second:

| Cameras | D3D12 256 | 512 | 640 | 768 | 1024 | D3D11 256 | 512 | 640 | 768 | 1024 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 217 | 216 | 214 | 213 | 211 | 237 | 225 | 225 | 223 | 217 |
| 2 | 202 | 196 | 194 | 192 | 189 | 210 | 195 | 192 | 189 | 183 |
| 3 | 192 | 184 | 181 | 178 | 173 | 187 | 172 | 167 | 167 | 163 |
| 4 | 184 | 178 | 175 | 172 | 168 | 183 | 166 | 162 | 161 | 153 |
| 5 | 182 | 175 | 171 | 169 | 163 | 177 | 158 | 154 | 151 | 149 |
| 6 | 174 | 164 | 160 | 157 | 150 | 162 | 139 | 133 | 133 | 128 |

Display VPS (the host received 25-27 new volumes a second throughout; a dash is a cell the host's log did not line up with cleanly). The host draws slower while the game runs D3D11 - they share the GPU:

| Cameras | D3D12 256 | 512 | 640 | 768 | 1024 | D3D11 256 | 512 | 640 | 768 | 1024 |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 246 | 154 | 158 | 157 | 149 | 145 | 114 | 115 | 104 | 102 |
| 2 | 253 | 158 | 149 | 139 | 142 | 139 | 105 | 105 | 97 | 89 |
| 3 | 239 | 140 | 136 | 132 | 122 | 129 | 98 | 92 | - | 83 |
| 4 | 245 | 149 | 141 | 135 | - | 133 | 99 | 95 | 92 | 85 |
| 5 | 242 | 143 | 138 | 132 | 122 | 140 | 97 | 98 | 92 | - |
| 6 | 220 | 118 | 112 | 112 | 108 | 136 | 90 | 88 | 84 | 80 |


## 5. What the numbers say

- **Six cameras at 1024 px is the working point on this machine.** Perspective lenses on D3D12: 92 fps, 27
  volumes a second, 717,000 voxels a volume. 1280 px still holds (81 fps, 26 a second). From 1536 px the game falls
  under 40 fps and the rig stops making 30 captures a second (22-26), so about 19 volumes a second arrive; at 2560 px,
  10.

- **A camera costs its whole render, so its size matters less than its count.** GPU time per camera per capture,
  six cameras, from each run against its own capture-off baseline:

  | | 256 px | 512 px | 1024 px |
  |---|---|---|---|
  | Orthographic, D3D12 | 1.4 ms | 1.7 ms | 2.0 ms |
  | Orthographic, D3D11 | 1.8 ms | 2.3 ms | 2.6 ms |
  | Perspective, D3D12 | 2.0 ms | 2.3 ms | 2.7 ms |
  | Perspective, D3D11 | 2.9 ms | 3.4 ms | 4.1 ms |

  Perspective costs 35-45% more than orthographic: the light and shadow passes UE4 skipped orthographically, which
  are the point of the switch.

- **Use D3D12.** It matches D3D11 with one camera and pulls ahead with each one added: six perspective cameras at
  1024 px run the game at 92 fps on D3D12 and 45 on D3D11.

- **Above about 512 px, most extra pixels merge away at today's display.** Six cameras: 12% of the pixels land in an
  already-filled display cell at 512 px, 60% at 1024, 84% at 2560. A VX2-XL has 448 voxels across, and a 1024 px
  camera samples each about twice on each axis. The extra pixels fill surfaces the cameras see edge-on; they cannot
  add detail the display does not have.

- **The display side is the next limit, not the game.** The simulator (on the game's GPU) received 25 new volumes a
  second up to five cameras, but 17-21 at six cameras of 512 px and up, drawing 60-100 VPS at 400,000-700,000 voxels.
  A display twice as fine (112 cells a unit) makes six cameras at 1024 px 1.58 million voxels a volume, and 9 of them
  a second arrive. For denser displays the volume's path to the display - the pipe and the host - is what needs work
  first.

- **The merge is not optional.** Six cameras at 2048 px with no merge are 7.1 million points, 107 MB a volume: the
  pipe broke. Merged, the same cameras send 1.3 million.

## 6. How it got to 30 volumes a second

| Fix | Before | After |
|---|---|---|
| Captures timed by the player's world clock, the loop ticking three times as fast | 15 captures a second at 56 fps (the loop waited on the game thread); 85 once a stalled clock was read | 30 |
| One voxel per display cell, on the GPU | 21% of a three-camera volume drawn twice, adding its light | none |
| Display transform on the GPU, readback trimmed to the volume, wire packed in place | six orthographic cameras at 1024 px: 17 volumes a second, 132 fps (2.27 M points, 100 MB copied back a volume) | 30, 150 fps |

## 7. The cost settings

Live in the game's `voxon_capture_ue.ini`, off by default. Orthographic, five cameras at 512 px, D3D12:

| Setting | Game fps |
|---|---|
| none | 114 |
| `lodFactor=4` (lower LODs) | 114 |
| `aoOff=1` (no SSAO) | 118 |
| `ssrOff=1` (no SSR) | 116 |
| all three | 118.5 |

About 0.3 ms of the 2.1 ms five cameras cost: a camera's price is its whole render - culling, shadows, the base
pass, lighting - not the screen-space passes. `persistState=1` (render state kept between captures) is untried.

## 8. Not measured here

- Any other game: every profile but The Ascent's is untested ([`PLAN.md`](PLAN.md), *Testing a game on Machine A*).
- The hardware's own volume rate: the VPS above is the simulator's, sharing the game's GPU.
- Sections 1-4 again with the host and scene-buffer fixes: section 10 re-measures six cameras only, in a smaller
  window. Fewer cameras, and the game at full screen, are not re-measured.

## 9. Why the display received fewer volumes than were sent - found and fixed

The add-on sent 30 volumes a second and the host showed 24-25 (one camera), 17-21 (six). Two causes, measured
with temporary counters in both (2026-10-11, five perspective cameras of 1024 px, D3D12):

1. **The host stalled its own display four times a second.** A check for whether a catalogue game is running
   enumerated the machine's processes once per catalogue entry - about 80 ms - on the render loop, every 250 ms.
   Every volume that arrived inside a stall was replaced by the next before it was drawn. Moved to a background
   task and made one enumeration (Voxel64, `RenderLoop.cs`, `GameCatalog.AnyRunning`):

   | | Before | After |
   |---|---|---|
   | Host passes over 25 ms | 4 a second, 75-105 ms | none; the worst 13-22 ms |
   | Host VPS | ~110 | 160-168 |
   | Volumes the host takes as frames / draws | 11-19 / 10-16 a second | 21-22 / 20-22 - every one sent |

2. **The host took a big volume in slowly, and the add-on gave up on the pipe at once.** The add-on drops a volume
   when the previous write is unfinished (`dropped host`); it decoded 21-24 a second and sent 15-19. Two fixes:

   - **GameKit allocated three large buffers a volume** - the message, the parsed array, the stored list - so the
     host ran a full collection 5-7 times a second and took 18 ms to take in a 12.7 MB volume. They are reused now,
     and the reader decodes straight from the bytes: GameKit branch `fix/feed-buffer-reuse` (797dbc1, on
     `origin/main`), with a self-test that fails if a batch still waiting to be drawn is refilled.
   - **The add-on dropped on the first busy pipe.** Volumes reach its worker unevenly, some a few ms behind the last,
     so even with the host down to 4-5 ms a volume it still lost about 4 a second. The worker - its own thread, not
     the game's - now waits up to 30 ms for the write, and drops only if the host is stuck or a newer volume has
     arrived meanwhile (`voxon_capture_ue.cpp`, `FeedPipe::WaitIdle`).

   | | Before | GameKit fixed | Both fixed |
   |---|---|---|---|
   | Scene (the game restarted before the last column) | 846,000 voxels, 12.7 MB | the same | 717,000, 10.8 MB; then 1.1 M, 16.5 MB |
   | Host: read, then parse and store, a volume | 2.0 + 18.3 ms | 1.5 + 3.0 ms | 1.4 + 2.6 ms (at 10.8 MB) |
   | Host: full collections | 6-7 a second | none | none |
   | Add-on: decoded / sent a second | 21-24 / 15-17 | 23-24 / 19-20 | 22-24 / all of them, 0 dropped |
   | Host: new volumes drawn a second | 15-16 | 16-22 | 19-24 (at both sizes) |

   The host takes every volume sent; the few it draws fewer are pairs that land inside one display pass, where the
   newer replaces the older before either is drawn. VPS is left out of the table: the simulator shares the game's
   GPU, and the game itself ran faster after the change (47 fps, then 55-59), for reasons not isolated. It read 110
   before, 85-105 after at 717,000-846,000 voxels, and 81-91 at 1.1 M.

Not the cause, measured: garbage-collection pauses (3-8 ms a second in all), the host's draw (at most 21 ms) or its
preview (at most 8 ms). An earlier version of this section also ruled out the add-on bunching its volumes, because no
two sends were closer than 8 ms. That was wrong: a volume close behind another was the one dropped, so it never
became a send to measure.

## 10. Choke points and the camera-size limit (host fixed; six cameras)

Measured 2026-10-11, evening, in the same workshop: six perspective cameras, rig scale 1.25 m a display unit, merge 56,
the host from section 9. **The game ran in a 1000 x 662 window** (a remote-desktop session had shrunk the desktop), so
its own view is cheap and its frame rates are not comparable with sections 1-4: capture off was 346-417 fps on D3D12
and 150-172 on D3D11, against 180 at 2580 x 1490. Each row is two 5 s windows once the change had settled; *VPS* and
*drawn* are the host's medians over the same seconds; CPU is per process over 5 s, 100% being one of 16 threads.

### 10.1 The first choke point: the scene buffers resized to every camera - fixed

By default UE resizes its scene render targets to each view it renders (`r.SceneRenderTargetResizeMethod 0`). A camera
taller or wider than the game's window therefore reallocates them twice a frame - for the camera, then back for the
player's view - and the GPU sits idle between. The first sweep hit it as a cliff between 640 and 768 px (191 fps, then
55, the GPU 33% busy); 768 is the first size taller than the 662-pixel window. It is also section 2's cliff between
1280 and 1536 px, where the window was 1490 tall. The rig now sets the engine's own "grow to the largest view"
(`r.SceneRenderTargetResizeMethod 2`, ini `growTargets=1`, the default). The same scene, switched in place:

| | Resized (engine default) | Grown (`growTargets=1`) |
|---|---|---|
| D3D12, 768 px | 63 fps, 25 volumes/s | **196 fps, 30** |
| D3D12, 1024 px | 58 fps, 23-24 | **175-179 fps, 30** |
| D3D11, 768 px | 46 fps, 24 | 55 fps, 24 |
| D3D11, 1024 px | 44 fps, 25 | 50 fps, 25 |

The price: the buffers keep the largest camera size used since the game started. On D3D11, after 2048 px cameras, the
game's own view with capture off fell from 160 to 77 fps; on D3D12 it did not move (372, then 378). Set the size and
leave it; a restart of the game resets it.

### 10.2 Camera size (every voxel sent)

| Camera px | D3D12 fps | D3D12 volumes sent / drawn a s | Voxels (merged away) | D3D12 VPS | D3D11 fps | D3D11 sent / drawn | D3D11 VPS |
|---|---|---|---|---|---|---|---|
| 256 | 241 | 30.0 / 29.9 | 114k (2%) | 199 | 77 | 26.8 / 26.4 | 175 |
| 512 | 209 | 30.0 / 29.9 | 398k (12%) | 120 | 60 | 24.4 / 24.4 | 114 |
| 640 | 198 | 30.0 / 29.9 | 503k (29%) | 101 | 54 | 24.4 / 22.9 | 100 |
| 768 | 190 | 30.0 / 29.9 | 595k (42%) | 92 | 69 | 22.9 / 22.8 | 94 |
| 1024 | 168 | 30.0 / 29.9 | 717k (60%) | 84 | 56 | 23.2 / 21.9 | 80 |
| 1280 | 114 | 29.6 / 28.5 | 849k (70%) | 77 | 47 | 23.5 / 20.8 | 75 |
| 1536 | 91 | 28.3 / 26.9 | 991k (75%) | 69 | 47 | 22.9 / 19.9 | 66 |
| 1792 | 76 | 25.2 / 24.6 | 1.16M (79%) | 61 | 41 | 23.1 / 19.6 | 58 |
| 2048 | 53 | 26.8 / 26.5 | 1.34M (81%) | 52 | 38 | 22.2 / 21.8 | 52 |
| 2560 | 27 | 18.9 / 18.7 | 1.80M (84%) | 44 | 25 | 19.5 / 19.2 | 39 |

D3D12 is GPU-bound from 1280 px (100% at 2560). D3D11 is CPU-bound throughout - its GPU 31-57% busy until 2048 px -
because each camera's draws are submitted on the render thread: six cameras at 256 px already cost it about 7 ms a
frame, and it never reaches 30 volumes a second. Its 768 px row above its 640 is the scene moving (people walk through
the workshop), not the rig.

### 10.3 Voxels alone, at a fixed camera size

Measured with a **temporary** switch that thinned whole display cells evenly down to a target count (the plugin's own
budget controller, which ships at 500,000). It was taken out again the same day: **the rig sends every voxel it
captures, with no limit** - the host takes them all (10.4), and the game runs well at a million and more. The rows only
show what voxel count costs on its own. Same cameras, fewer voxels:

| API, camera px | Thinned to | Voxels (cells kept) | Game fps | Volumes drawn a s | VPS |
|---|---|---|---|---|---|
| D3D12, 1024 | off | 717k (100%) | 179 | 29.9 | 91 |
| | 500,000 | 514k (72%) | 200 | 29.9 | 113 |
| | 250,000 | 257k (36%) | 232 | 29.9 | 167 |
| D3D12, 2048 | off | 1.60M (100%) | 47 | 24.7 | 43 |
| | 1,000,000 | 1.03M (64%) | 56 | 27.8 | 64 |
| | 750,000 | 769k (48%) | 67 | 26.9 | 88 |
| | 500,000 | 514k (32%) | 74 | 26.4 | 125 |
| | 250,000 | 257k (16%) | 79 | 28.4 | 186 |
| D3D11, 1024 | off | 717k (100%) | 57 | 22.4 | 91 |
| | 500,000 | 509k (70%) | 64 | 23.4 | 113 |
| | 250,000 | 255k (35%) | 74 | 24.9 | 155 |
| D3D11, 2048 | off | 1.34M (100%) | 40 | 22.5 | 55 |
| | 1,000,000 | 993k (74%) | 47 | 20.3 | 75 |
| | 750,000 | 745k (55%) | 51 | 21.2 | 90 |
| | 500,000 | 495k (37%) | 51 | 23.8 | 110 |
| | 250,000 | 258k (19%) | 70 | 23.6 | 144 |

(The D3D12 2048 px scene held 1.60 M voxels against 10.2's 1.34 M: more people in the workshop.)

### 10.4 The host

- **It takes every volume**, at every size and voxel count: drawn within one of sent, `dropped host` 0 throughout, up to
  1.8 M voxels (27 MB) a volume. Taking volumes in is no longer a choke point (section 9).
- **VPS is about 65-70 million voxels a second divided by the volume's voxels**: 257k x 186 = 48 M, 514k x 125 = 64 M,
  769k x 88 = 68 M, 1.03M x 64 = 66 M, 1.60M x 43 = 69 M. That is the LedWin simulator drawing on this PC, not the
  hardware.
- **It uses 3-3.5 cores whatever the voxel count**, because it presents as fast as it can and so draws each volume 3-6
  times. On one PC those cores are the game's: six cameras of 1024 px on D3D12 ran the game at 178 fps with the host
  and 254-264 with it closed.

### 10.5 What it says

**The choke points, in the order they bite on this PC:**

1. The scene buffers resizing to each camera - fixed (10.1).
2. D3D11's render thread: six cameras cost it about 7 ms a frame at any size, and it never reaches 30 volumes a
   second. Use D3D12 wherever the game offers it.
3. The GPU, by camera size (D3D12): 30 volumes a second to 1024 px at 168-179 fps, nearly to 1280 (29.6, 114 fps);
   25-28 from 1536 to 2048 at 53-91 fps; 19 at 2560, the GPU full.
4. The host's own CPU, when it shares the game's PC: about 80 fps of the game at 1024 px.
5. Voxels cost a little, but do not limit anything: at a fixed camera size more voxels take some of the game's frame
   rate - readback, reformatting, the pipe and the host's memory traffic (D3D12 2048 px: 47 fps at 1.6 M, 79 at
   257k) - and the simulator's VPS falls as 1/voxels, while every volume still arrives.

**The camera-size limit.** On a VX2-XL (448 cells across) the merge discards 60% of pixels at 1024 px and 84% at 2560,
so from 1024 to 2560 px the GPU cost grows about six-fold (168 fps to 27) for 2.5 times the voxels. Past 1024-1280 px,
resolution buys this display little; it is a finer display (merge 112, section 2) that would use it. **1024 px is the
working ceiling for six cameras on an RTX 3060 Ti, 1280 the limit at 30 volumes a second.**

**No voxel limit.** Nothing here needs one: the host takes every volume up to 1.8 M voxels, and six cameras of 1024 px
deliver 717,000 voxels 30 times a second with the game at 168-179 fps. The plugin's 500,000 budget is not carried
over. If the hardware turns out to have a voxel rate of its own, that is the place to look again - it is not measured
here, and the simulator's 65-70 million voxels a second is not the device's.

## 11. Risk of Rain 2: the Unity rig (DX11)

2026-10-12. Unity 2021.3 Mono with BepInEx 5, on this PC (RTX 3060 Ti), with the profile's settings: five cameras
(the four sides and the top), 512 px, 30 captures a second, perspective lenses, 30 m radius. The app ran on the same
PC and drew to the simulator.

| | |
|---|---|
| Captures, decoded, sent, drawn | 30 a second each |
| Voxels a volume | 120,000-200,000 (stage 1, two spawns), 16% merged where faces overlap |
| Dropped anywhere | none |
| The add-on's worst Present | under 1 ms |
| The game | 30 fps, which is its own Frame Rate Limit (`fps_max 30` in its config), and the same with the rig stopped (F7) |

The older Unity rig, one orthographic camera read back on the CPU, took 20-64 ms a capture. This one never leaves the
GPU until the voxels.

**Not mirrored.** Two grey test blocks went into the level 15 m from the player: a tall one along Unity +Z and a low
one along Unity +X. In a decoded volume the tall one stood at display (+1.99, -0.10), 1.26 units tall (10 m is 1.33),
and the low one on display +y, 0.34 units tall (3 m is 0.40). Unity's forward, right and up land where Unreal's do.

**Not measured yet:** the game's own frame rate with its limit raised.

**Player look, 2026-10-15.** Six cameras of 1024 px, each with its player camera: 30 volumes a second, the game at its
own 30 fps. Commando's model lives apart from the body the rig follows, so the profile names it
(`playerModel=ModelLocator.modelTransform`: four renderers); with no name free among the 32 layers, the player cameras
draw `PlayerFakeActor`, which the faces never draw. Paused, with the player's brightness at +6, saturation 0 and
density 4, Commando turned white and solid in the volume while the escape pod and the ground beside him stayed as
they were.

## 12. Mortal Kombat 1 (Unreal 4.27, NetherRealm's fork, D3D12)

2026-10-12, this PC (RTX 3060 Ti), MK1 on its Low preset, 60 fps cap, windowed.

**What it took.**
- UE4SS 3.0.1 crashed MK1 four seconds in, every launch, whatever was switched off: a null read inside an always-on
  hook. The experimental build lets every hook be switched off; with only `EngineTick` (what the rig's loop needs) MK1
  runs.
- Engine calls then did nothing: NetherRealm's UObject has two more virtual functions than stock, so UE4SS's
  `ProcessEvent` (stock slot 68) was a `return 0` stub; the real one is at 70, found in `MK12.exe` by its code. A
  `VTableLayout.ini` with the two extra slots fixes it (`rig/unreal/games/mk1/`).
- MK1's fighters are `MKCharacterActor`s, not pawns: the profile's `followClass` makes the volume follow the middle of
  the visible ones.

| Cameras | Volumes a second | Voxels | MK1 |
|---|---|---|---|
| 3 of 1024 px, lit | 30 | about 340,000 | 41-48 fps |
| 5 of 1024 px, surface colour (each renders twice) | 30 | about 550,000 | 60 fps (its cap) |

**Its lighting.** The cameras' raw light spans more than 20 stops in one stage (shadow 0.002, lit floor 100-400,
daylight through the windows 8,000-30,000), and the six cameras' averages differ eightfold. The game's own camera
squeezes that with its post-processing; one exposure for the volume cannot. The surfaces' own colours (base colour,
`lighting=0`) fall between 0.03 and 0.96: everything visible, nothing clipped. The game's finished picture as a
capture came out nearly black (its eye adaptation does not settle in a capture camera).

## 13. Cyberpunk 2077 (REDengine 4, 2.31, D3D12): the engine's own cameras

2026-10-14, this PC (RTX 3060 Ti), the game windowed at 1440 x 900 (DLSS), V standing in a garage (The Heist, before the Afterlife).

**What it took.** Five of the engine's render-to-texture cameras on V (`rig/redengine4`), placed every frame, rendered
in the same frame as the player's view; the add-on takes each face's picture and carries its depth out of the
cameras' shared depth buffer (`addon/red4_rtt.hpp`). Along the way, each measured:
- Cameras added to V while she is in the world never render (nor does a light so added): only those added while she is
  loaded are attached.
- A camera renders at its own resolution, 100 x 100 unless set, and all of them at the largest one's, into one shared
  depth buffer, one after another; only the output pictures differ in size.
- The rig first took `Game.GetPlayer()`'s new object every frame for a new V and switched the cameras off and on
  every frame: 5 fps. Found once, the cameras cost what is below.
- Copying the shared depth buffer at a face's draw, in the state a trace had shown, faulted the GPU once in the main
  menu; copied only at the game's own barriers, into a texture of the add-on's (the carry), it has not.
- **All five cameras rendered one view** until each had a `virtualCameraName` of its own (found 2026-10-14, after the
  first results below were taken). With one face on the volume looked right; with two opposite faces the car came out
  twice; with all five the top face's view stood on its side in every side face, sheets crossing in an X. Read back,
  every face's picture and every depth pass of every camera was the top face's view. Placing the cameras in the other
  order changed nothing; a camera on another entity (a spawned holocall camera) rendered its own; five on V, each
  named, rendered five. Switching the cameras on in turns, one a frame, instead faulted the GPU within seconds.
- **The far plane.** Decoded with an infinite far plane, the top face put the ground 12.5 cm below where the four side
  faces did (they agreed with each other within 1 cm): unset, the cameras' far plane is about a kilometre (fitted from
  the two, 1,000-1,100 m). The rig now sets it just past the box and the decode uses it: top and side faces agree on the
  ground within 0.6 cm.

| Cameras | World radius | Volumes a second | Voxels | The game |
|---|---|---|---|---|
| none | | | | 250-260 fps |
| 5 of 512 px, lit | 12 m | 30 | about 330,000 | 63-70 fps |
| 5 of 512 px, lit | 6 m (the profile's) | 30 | about 260,000 | 67 fps |

With a camera on, the GPU's load fell from 90% to about 55%: each camera costs mostly the render thread's time, about
2-3 ms, not the GPU's. One 1000 px camera measured alone: 4.2 ms lit, 1.8 ms with only its G-buffer
(`renderingMode=GBufferOnly`, the surfaces' own colours), a cheaper option not yet offered.

After both fixes (each camera its own view; the far plane just past the box, so nothing beyond it is drawn), on a street
at dusk:

| Cameras | World radius | Volumes a second | Voxels | The game |
|---|---|---|---|---|
| 2 of 1024 px (+Y side, top), lit | 4.5 m | 30 | about 250,000 | 111 fps |
| 5 of 1024 px, lit | 3 m | 30 | about 600,000 | 55 fps |

## 14. Star Wars Jedi: Fallen Order (Unreal 4.21, Respawn's build, D3D11): player look

2026-10-15, this PC (RTX 3060 Ti), the game windowed, Cal standing on a walkway on Bracca. Five faces of 2048 px,
World radius 4 m, tilt 45, turning with the player.

**What it took.** UE 4.21 numbers its enums differently from later engines (`RTF_RGBA16f` 5, `SCS_SceneColorSceneDepth`
4), so the rig reads them by name; `CreateRenderTarget2D` takes four arguments; there is no `AddComponentByClass`, so
every capture is an actor of its own. The main menu has its own Cal (`BP_Hero_C` at the origin), which the rig follows
until the story loads.

**Player look.** In the game's own lighting Cal came out much darker than the walkway round him. With Player look on,
each face's player capture is found by its code and decoded with the face:
- The first codes, 3000 + face, are not all exact in half floats (3001 read back as 3000, 3003 as 3004): two faces were
  taken for others and the decode stalled. At 1500 + face every face is found at its first look.
- The menu's Cal has three actors attached (two lightsabers, a poncho). Listed by `GetAttachedActors`, they crashed the
  game inside UE4SS (a null read) as the menu's cameras were built; the story's Cal, with none listed, had not. Listed by
  walking the components' `AttachChildren`, all three are found, and the menu and the story both run.
- Player brightness +2 turned Cal from a dark shape into a figure; density 3 made him solid. The walkway did not change.

| Player look | Volumes a second | Voxels | The game |
|---|---|---|---|
| off | 28 | about 495,000 | 56 fps |
| on (five more captures, the player alone) | 25-27 | about 500,000 | 42-49 fps |

## 15. Re-checks after the shared changes (2026-10-15)

REDengine's faces moved to the sides the app names, the pack pass's exposure changed, and rig.lua learned to read the
engine's enums by name and to make render targets three ways. Each game was installed again from the app and run:

| Game | What ran | Result |
|---|---|---|
| The Witcher 3 | five faces through the renderer hook | 34-35 world frames a second with faces, 30 volumes; one Geralt, one path, colours as in the game |
| The Ascent (UE 4.26, D3D12) | three faces of 512 px | the enums read as 4.26 numbers them (`RTF_RGBA16f` 6, `SCS_SceneColorSceneDepth` 3), the six-argument `CreateRenderTarget2D`; 30 volumes, the game at 182 fps |
| Mortal Kombat 1 (its UE 4.27 fork, D3D12) | five faces of 2048 px, the tutorial | the same enum values, both fighters followed; 30 volumes, about 670,000 voxels |
| Risk of Rain 2 (Unity) | six faces of 1024 px | 30 volumes (section 11) |

## 16. See-through surfaces (Unreal and Unity), 2026-10-15

A see-through material writes no depth, so a face put its colour on whatever stood behind it: Cal's lightsaber blade
landed on the wall behind him, and The Ascent's hologram doors were not in the volume at all, so a closed door looked
like an open way. The rig now lists the see-through materials it meets, the app shows them as tick boxes, and the
ticked ones are drawn opaque by one more capture a face, whose depth the add-on takes where it is nearer than the
face's own (`docs/ARCHITECTURE.md`, See-through surfaces).

**Jedi: Fallen Order** (UE 4.21, D3D11, six faces of 1536 px). The list held the blade's two materials,
`M_Lightsaber` and `M_Lightsaber_XPlane` (on `BP_Lightsaber_Hero_C`), beside blaster muzzle flashes, fire, smoke, fog
volumes, decals and Cal's eyes. With the two ticked, 4 slots were swapped each capture and the solid captures were
found by the add-on; with every see-through material solid (21-25 components near), 25 volumes a second against 30. The
blade in the volume itself is still to be looked at by eye (in these runs Cal's blade was mostly sheathed). Reading a
component's `Bounds` through UE4SS crashed the game on Cal's face mesh; the rig asks where a component is instead
(`K2_GetComponentLocation`).

Looked at by eye (the user, 2026-10-16): the blade stood in the volume, but as a flat card, wider than the blade. How the
game draws it, from its saber actor (`BP_Lightsaber_Hero_C`) read through UE4SS: the blade is one static mesh,
`SM_Lightsaber`, with two translucent dynamic instances - slot 0 `M_Lightsaber_XPlane` (crossed planes: the glow card)
and slot 1 `M_Lightsaber` (the blade), their parameters `Sheath`, `Swipe Angle`, `Inner Color`, `Outer Color` - and two
particle systems, `P_Lightsaber_Glow` (round and axis-aligned glow sprites) and `P_Lightsaber_Extend` (the ignition
flare). The mesh's bounds are pushed out to about 5 m each way, as the engine needs when a material moves the vertices
on the GPU: the swing's smear is the blade's own vertices moved back across the swing (the actor keeps
`PreviousTipLocation` and `PreviousRootLocation`), not a post-process blur. With `M_Lightsaber` alone the blade was still
too wide: its mesh is wider than the glowing core its material draws.

So each camera now also captures the chosen surfaces as the game draws them, before they are made opaque (the glow
capture, `solidOpacity`; `docs/ARCHITECTURE.md`). Measured on one side face, Cal's lit blade at rest: of 2,927 texels
the stand-in drew, the glow's alpha (the light let through) was 0.9-1.0 on 1,775 and 1.0 on 650 - under 10% opaque -
and below 0.5 on about 150, the core. Mid-swing the glow covered up to 1,829 texels the stand-in did not (the smear),
which stay unsolid. The profile now ticks `M_Lightsaber` alone, solid from 50% opaque. Six 1024 px faces with player
look, in a fight: 43-46 fps with the glow capture, 47-50 without (Unreal 4.21 has no `AddComponentByClass`, so each is a
whole render); the blade put away is hidden, and the solid and glow captures are skipped: 55-60 fps. The GPU self-test
draws the box with an opaque upper half and a 20%-opaque lower half: from 50%, the lower half's 189 voxels no longer
stand and the upper half's do; from 10%, all of it stands (D3D11 and D3D12).

Two more found on the way. After a save was loaded, nothing of the level was listed - Jedi: Fallen Order streams its
levels into one world, so the rig's cameras stay and it never looked again; it now looks through the world when the
player changes and every 10 s. And with UE4SS's BeginPlay hook on (meant to catch new actors, though here it caught
none), the game crashed inside UE4SS three minutes into a fight (0xC0000264, a lock released by a thread that did not
hold it); with the hook gone, a two-minute fight and the quarter of an hour of testing after it ran without one.

**The Ascent** (UE 4.26, D3D12, three faces of 512 px). The list held `Hologram_door_red`, `_green` and `_blue` (on
`Door_SM_Holo_C`), and also the player's own outline and occlusion materials, fog sheets and hundreds of decals: every
one at once would wrap the player in a shell, so ticking is the way. A snapshot of each face's solid capture (the
rig's `snapshot` key now writes them) held drawn texels only where doors were in view (82, 229 and 379 texels on the
three faces beside the green door, none elsewhere); the doors' place in the volume is still to be judged by eye - from
the preview's angles tried, solid and see-through looked alike. Frame rates, the game's view and three cameras:

| | The game | Volumes a second |
|---|---|---|
| Nothing ticked, the list on or off | 196-204 fps | 30 |
| A whole-scene solid capture, every listed component asked where it is every capture (about 3,500) | 23 fps | 13 |
| The same, asked once a second each | 70-80 fps | 27 |
| The solid capture drawing the ticked components alone, as a depth-only capture: nothing near | 130 fps | 30 |
| The same, the doors near (32 components) | 110-114 fps | 29 |
| The same, with player look | 77 fps | 27 |

With nothing ticked near the player the solid capture is now skipped altogether. Watching for new components with
UE4SS's `NotifyOnNewObject` crashed The Ascent as the rig started (the engine makes objects on its loading thread);
new actors are found as their `BeginPlay` runs instead.

**Risk of Rain 2** (Unity, D3D11, six faces of 1024 px). The list filled with the game's effect materials (its
`Hopoo Games/FX/...` shaders: tracers, shrine symbols, rain, dust); the stand-in is `Hidden/Internal-Colored`. With
the tracers and shrine symbols ticked, all six solid cameras were found and the volume held 30 a second (the game's
own limit, 30 fps, unchanged). With every see-through material solid the volume grew from about 246,000 voxels to
250,000 (rain, dust and effects given depth). GPU self-tests: the see-through box's colour lands
behind it, and the solid capture puts it back, on D3D11, D3D12 and Unity's D3D11.

REDengine (The Witcher 3, Cyberpunk 2077) has no see-through surfaces made solid yet: its faces are drawn by the game's
own renderer (The Witcher 3) or its render-to-texture cameras (Cyberpunk), and a material there would have to be
swapped through the engine's own types, which the rigs have not reached.

## 17. Need for Speed Heat (Frostbite, D3D11): parked, 2026-10-16

The Steam copy (started by the EA app), at 1600x900 windowed on an RTX 3060 Ti, about 230 frames a second in the open
world. ReShade 6.7.3 and an add-on load (D3D11 only; the game has no DX12 option). **Not supported**: the game was
parked on 2026-10-16, its folder and settings put back as they were, and the code taken out of the add-on.

**Why.** Frostbite gives a mod no camera of its own and no script to run, and the game renders no extra view that the
faces could take over. So the only cube built was a sweep, from the add-on alone: the game's own camera moved to a face
each frame. The user judged it not working ("gameplay camera does not work") and ruled sweeps out for every game: only
cameras the engine renders in the same frame as the player's view, as in Cyberpunk 2077 and Jedi: Fallen Order.

**What was measured** (for a later Frostbite game).
- The main view's camera is a RenderView whose transform the game sets once a frame, by a function (exe+0x21DD0C0) that
  also places about 80 objects that are not cameras; the main view's call returns to exe+0xC512852. A hardware
  breakpoint on that return (no game code changed) let the add-on rewrite the camera before it was used. The view's
  layout: the camera's rows (right, up, back - the camera looks along minus the third - and place) at +0x00 and again at
  +0x40, the vertical field of view at +0x100, near and far at +0x118 and +0x11C (reversed depth, far 32000.3 m), aspect
  at +0x120. Frostbite's world is in metres, right-handed, Y up.
- The sweep itself worked as far as it went: five faces round the parked car, looking in, near planes on the box's faces
  (Motion Blur, TAA and Depth of Field off), 205-209 frames a second against 233 alone, the player seeing one frame in
  six. The scene's HDR colour (R11G11B10, no HUD) and depth (R32G8X24) are intact at present.
- It failed on knowing which presented frame came from which face: the camera's updates and the presented frames do not
  run one for one (165-207 against 177-187 a second), and the view constants are written into three 1 MB ring buffers
  that the render thread binds a slice at a time, so reading them back named the wrong frame too often.
- No extra view to reuse: the small square depth targets drawn each frame are the sun's shadow cascades, and there is
  no reflection cube.

**What a Frostbite game would need.** Battlefront II (section 18) showed it: the renderer's own list of views, which
the add-on adds the faces to, each drawn in full in the player's frame. Need for Speed Heat may well keep the same list;
it has not been looked for there.

## 18. Star Wars Battlefront II (2017; Frostbite, D3D11): the engine's own views - working in play, 2026-10-16

The Epic copy (installed and started by the EA app), 1.1.8.16175, DirectX 11 by its settings, on an RTX 3060 Ti. No
anti-cheat in the install: the EA app starts `starwarsbattlefrontii.exe` directly (Squadrons ships Easy Anti-Cheat and its
launcher starts it, and Battlefield 6 needs EA Javelin and Secure Boot: both refused). The exe's image does not move
(base 0x140000000). Its code is protected on disk; everything here was read from the running game.

**Frostbite's types.** The engine registers its types at run time, with their fields' names and offsets: about 23,000,
read from a snapshot of the running game's image. Among them, a camera's data (`CameraEntityBaseData`) carries a
`ViewId` (`LocalPlayerViewId`: the root view, a secondary one, four custom ones) and a `RenderTarget`, and the renderer's
view state is a static part (`TargetTexture`, `Priority`) and a dynamic one (`Transform`, `NearPlane`, `FarPlane`,
`Aspect`, `FovRad`, `Viewport`, `Visible`, `PhysicalCameraEnabled`). The renderer's frame builder
(buildGameRenderViewParams, exe+0x1524910) walks a list of views headed at exe+0x3FFCD88 - nodes of { id, static state,
dynamic state, next, prev, linked } - and renders each.

**What works.**
- A node of the add-on's own on that list - a copy of the root view's state with its own id (1000 and up in the menu;
  in play the ids just past the game's own, 1 and up; one sharing the root's id is merged into it) - is rendered in full: its own G-buffer, lighting, shadow maps and exposure, in
  targets of its viewport's size, not put on the screen. Its camera is set where the frame builder has each view's camera
  rows in xmm2-5 (exe+0x1524B12, a hardware execute breakpoint): the same rows as the root's, Frostbite's (Y up; the
  camera looks along minus its third row - a camera given pitch -90 with the third row down measured the ceiling
  coming nearer as it rose).
- Its near and far planes are the ones given: looking down at the floor from four heights, the depth (reversed) fitted
  near 0.06 m and far 20,000 m left alone, and with 0.5 m given put the floor at the same height (-3.735 m) to a
  millimetre. Its lens is the one given only with `PhysicalCameraEnabled` off: on, as the menu's camera is, the faces came
  out with a vertical field of view of 26.6 degrees (fitted from the floor's depth) for the 90 asked, and decoded about
  four times too wide.
- Four views beyond the player's run at the game's frame rate; a fifth took it to about 5 a second, whatever the faces'
  size (256 or 512 px) - the card's memory was full too (DaVinci Resolve held 4.5 GB of its 8), but four at 512 px ran
  at 200 frames a second where five at 256 did not.
- A view's targets are its viewport's size rounded up to a multiple of 4 (513 drew into 516), and views of one size share
  them in turn; 8 texels apart, each keeps its own, intact at present. In each, the scene depth (R32G8X24) is cleared
  to 0, the G-buffer drawn, the lit HDR colour (R11G11B10) drawn with the depth bound, then the view's post-processing;
  later another R11G11B10 target is drawn with the same depth (mostly black: taken for the colour at first, it pinned the
  automatic exposure at +8 stops and most voxels came out black and were dropped).
- In the main menu, with three sides and the top at 512 px: 30 volumes a second of about 250,000 voxels, every face
  putting the menu's floor at the same height and 11% of voxels shared where faces overlap; the game at about 180 frames
  a second with them (200 alone, its cap). The GPU self-test draws the four faces as the add-on places them and checks the
  decode against the surfaces (every voxel within 1.5 px; at most 1.4 cm off).

**In play** (Instant Action, Naboo, offline, the same evening):
- The lit colour first taken in play was black: each face has two R11G11B10 targets bound with its depth, the lit one
  (written whole by the lighting, never cleared) and one cleared to black each frame - in the menu the lit one comes
  first, in play the black one. Taken by "not cleared this frame", play's colour came right: the automatic exposure
  fell from +7.4 stops to about 0.
- Five views crash the game in play (the table of four, above), so three faces at most. A fourth face taking turns with
  the others - three drawn each frame, each face every frame or two - ran at about 40 frames a second against 150 for
  three every frame: the engine draws a view cheaply only while it stays on the list.
- The game stopped itself (0xC0000409, no crash dump of its own) every few minutes with the faces on - after 2 s, 2
  minutes, 6 and 8. A debugger of our own, attached to the game and passing every exception back, caught it: a
  fail-fast of FAST_FAIL_FATAL_APP_EXIT (abort), from a `std::system_error` thrown in the add-on and let out through
  ReShade. At present the add-on held its lock on the faces (`g_fbMtx`) while it called D3D11 to copy them, and any D3D11
  call can end in ReShade's destroy_resource event on the calling thread - D3D11 finishes destroying what the game has
  let go of during the next call made, whoever makes it (caught once in our release of a copy, once in CopyResource) -
  whose handler took the lock again: MSVC's std::mutex throws on that. Now no D3D call is made under the lock: the faces'
  game textures are held (a reference each, taken where they are found, given back when they change), the present
  copies them from a list made under the lock, and what is let go of under it waits until it is free. The Unreal path's
  candidates and Cyberpunk's copies had the same shape and are changed the same way. Eight changes of face resolution
  in a row with the faces on - each one makes every copy again - no longer stop the game.
- The player: the soldier, found among 43 by its vtable as the one beside the camera, and the static list that holds it
  (exe+0x3A93190); its feet stood 1.8 m ahead of the camera, 0.35-0.45 m left and 1.79 m below - and 0.23 m ahead when
  the camera was pushed in against a wall. Read each frame (150 a second), the floor under the feet stayed at the same
  display height as the player looked up and down, where the camera's offsets had moved it.
- Three 1024 px faces: 30 volumes a second of 400,000 to 1,100,000 voxels (the display reported twice the VX2-XL's
  width), the game at 110-150 frames a second (200 alone).
- Player look, the player known by where they stand: the voxels within a display unit of the feet went from 3,949 to
  10,124 at density 3 and from 68.9 to 132.6 in brightness at +2 stops; the rest stayed at 50.7.

## 19. The Sims 4 (Maxis's engine, D3D11): the game's renderer, from cameras of the add-on's own - working, 2026-10-18

The Epic copy (installed and started by the EA app), 1.128.90.1030, DirectX 11 (its default), at 2048 x 1032 in a window,
on an RTX 3060 Ti. No anti-cheat in the install; the exe's image does not move (base 0x140000000) and carries its RTTI,
so the renderer's objects are known by their class names (`EA::Sims4::Renderer::SceneManager`,
`Sims3::Cameras::Camera`). Everything here was read from the running game.

**How a frame is drawn.** The scene manager's per-frame entry (exe+0x11C45A0) hands one of two render jobs to the job
system: in the main menu exe+0x11C46D0, in a lot exe+0x11C4910 - a member function taking only the manager, run on a
worker thread, which records the frame into a D3D11 deferred context (the add-on's draw events see that context, not the
immediate one). About 650 draws make the player's own frame. The manager holds four cameras (+0x60 is the main one) and
nine scene containers; its own view's flags and mask are at +0x710 and +0x714 (0x81 and 0x7FFFF000 in live mode).

**The thumbnail renderer's routine** (exe+0x11C7CD0: manager, scene, camera, flags, mask, a bool, a pointer) draws the
world from any camera: about 1600 draws, recorded into the same deferred context when called from the lot's job.
- Called after the game's job with a copy of the main camera, it drew the same picture into the frame's main colour
  (B8G8R8A8) and depth (D24 S8): 387 draws there became 2080. With the copy turned half round the orbit point, the
  screen did not change and a copy of the main colour taken just after showed the house from behind: its draws reach the
  GPU before the frame's own scene, which clears the targets and draws over them.
- It clears nothing: what it did not draw over showed the previous frame, the HUD included.
- Between its passes it copies the picture so far into targets of its own (1024 x 512, 2048 x 1032), setting their
  viewports and putting back the one it read before. Turned to targets 512 texels square, with every viewport scaled to
  them, the faces shrank each time round (512, 128, 32, 8 ... texels wide) and only a corner of the first came out.
  Targets the size of the game's picture, every viewport left alone, and a lens as wide and tall as the picture at the
  face's scale, shifted so that its top-left square is the face's 90 degrees (the camera's lens centre, +0x1F0), gave
  five whole faces.
- Called as the thumbnail renderer calls it (flags 0, mask -1), it draws the roofs - and, it seems, the levels above the
  one shown - that the player's view leaves out: the top face saw the house's roof 2.94 m above the orbit point, and the
  volume showed a flat sheet over the rooms. With the manager's own flags (0x81) the roofs are gone and every face sees
  into the rooms; the mask alone changed nothing.
- The camera: its lens as extents at distance 1 (width +0x14, height +0x18, so 2 x tan of the half angles), the
  horizontal angle +0x1C, aspect +0x20, near and far +0x24 / +0x28, rows right, up, back and place at +0x30, the
  projection +0x70 (D3D's forward depth: read back as f / (n - f) and n f / (n - f) for the planes given), and the point
  it orbits +0x200. The camera's own update (exe+0x1208EB0) rebuilds view, projection, world to screen and frustum from
  these; read back from the faces' cameras, each projection was the off-axis one asked for, to four decimals. The
  camera is 0x700 bytes (the game allocates it so, 16-aligned).

**The volume.**
- Five faces of 2048 x 1032, decoded at 512 from the top-left 1032-texel squares: 30 volumes a second of 96,000 to
  145,000 voxels, the game at 75-90 frames a second where it ran at 170 without them; about 1600 draws a face. F7, build
  mode, leaving the lot for the main menu and loading it again: no fault, and nothing decoded while no lot is drawn.
- The orbit point is on the ground the camera looks at: the house's floor, under the main camera's line of sight 7 m
  ahead and 2.5 m down. Panned outside, it followed the terrain down 1 m; the volume's largest layer of voxels (64,000)
  stayed at the feet's height (display z 1.5). Turned with the game's camera by 123 degrees, the rig's yaw followed by the
  same.
- The house's floor stands on a foundation about 0.85 m above the terrain round it, so with the feet 0.5 display units
  above the bottom (feetZ 1.5 of 2, 1 m a unit) the garden lies just below the volume.
- The GPU self-test draws the five faces into pictures wider and taller than square (480 x 256, 256 x 384), the rest
  near and bright, in 8-bit colour and forward depth: every voxel within 1.5 px of a surface, at most 1.5 cm off.

## 20. Crysis (CryEngine 2, D3D10): the game's 3D engine, from cameras of the add-on's own - working, 2026-10-20

The 2007 original, the EA Play copy the EA app installed (through the Xbox app) at `D:\XboxGames\Crysis`: patch 1.2.1
(build 6156), the 64-bit game (`Bin64\Crysis64.exe`), DirectX 10, in a 1920 x 1080 window, on an RTX 3060 Ti. No
anti-cheat in single player (PunkBuster only on multiplayer servers). The engine is in separate DLLs, and the interface
layouts are those of the Crysis Mod SDK's headers as the CryMP project keeps them (read, not copied).

**Starting the game.** As installed it did not start on this PC, with or without the rig.
- Through the EA app's stub (`Bin64\Crysis.exe`, which runs the game under a Vista SP2 compatibility layer the installer
  set) it hung at startup: a black window, one core busy in `RtlQueryPerformanceCounter`, nothing ever drawn. Cause not
  found.
- Started directly (`Crysis64.exe`) it stopped at "Initializing AI System" with an inexact-result floating-point
  exception (0xC000008F) at CrySystem+0x85BDC, on an ordinary `(float)atof(...)`. That is the game's copy protection:
  CrySystem loads `b64.dll`, calls a check in it, and when the check fails calls `_controlfp(0, 0x8001F)`, unmasking every
  floating-point exception so that the game falls over soon after. The rig does not touch this.
- The community's c1-launcher (v8, github.com/ccomrade/c1-launcher, downloaded by the player; no licence, so never
  bundled) starts it: the menu, the levels and play.

**How a frame is drawn.** Once a frame `CSystem::Render` calls `I3DEngine::RenderWorld` - slot 2 of the 3D engine's
vtable - with flags 0x327 (HDR, sorting, the Z pass, post-processing, ambient occlusion, water) and the name
"CSystem:Render I3DEngine", on the game's main thread; the water's reflection calls it too, from inside, as
"FX_DrawToRenderTarget" (flags 0x3, draw flags 0xC00). This build has no render thread (no `r_MultiThreaded`): a call
returns with its view drawn. The game makes its D3D10 device single-threaded.

**The faces.** The add-on replaces that slot (no code is patched) and, before the game's own view, calls the original
once per face with a camera of its own: a copy of the game's CCamera (536 bytes) given the face's matrix, a lens of the
game's picture size with a vertical angle of 90 degrees, and its frustum worked out again as the engine's own inline
code does. Its matrix's columns are the camera's right, forward (it looks along +Y) and up - REDengine's frame.
- Each face renders its own four 512 x 512 sun shadow maps, leaves its finished picture in the back buffer and its depth
  in the engine's linear depth target (`$ZTarget`, R32_FLOAT, the game's size: view depth over the far plane - 0.001036
  at the middle of a top face 3 m above the camera, with a far plane of 4268 m, 4.4 m).
- Flags 0x327, 0x307, 0x107 and 0x7 gave the same lit, tone-mapped picture; 0x327 adds the player's motion blur. The faces
  use 0x307 (no post-processing). The main thread spends about 1.2 ms on a face (2.9 ms with post-processing); the
  game's own view after it is untouched.
- The middle of the picture, as many texels a side as it is tall, is exactly 90 degrees each way: the add-on copies
  that square of the back buffer and of the depth target the moment each face is drawn.

**The decode.** D3D10 has no compute shaders, so the decode runs on a D3D11 device of the add-on's own on the same GPU.
The copies go into textures the two devices share, each with a keyed mutex (key 0: the game's device writes; key 1: the
add-on's reads); everything runs on the main thread. The pack pass reads the depth as linear (`DepthLinear`: view depth
= value x the face's far plane).

**The volume.**
- Five faces of the 1080-texel square, decoded at 512: 30 volumes a second of 314,000 to 342,000 voxels at the start of
  the second level ("village"), the game at 270-280 frames a second where it ran at 374 without them (`on=0`, same
  place): about 1 ms a frame, 2 ms a face on the frames that take a volume. No faults, no face without its depth target,
  no texture busy.
- The app showed the ground, the rocks, the ferns and a tree's trunk round the player as one surface each, nothing
  doubled. Not yet checked by orbiting the simulator against the game.
- The feet: the game framework's client actor (CryMP's IGame and IGameFramework headers) never answered; those layers are
  CryMP's own work and do not match the game's. The entity system - the game's own `CryEntitySystem.dll` - finds the
  player's entity by its name, "Dude" (IEntitySystem slot 9), and its world matrix (IEntity slot 22) holds its place.
  Read once a frame while the game was paused in a menu, it put the camera 0.85 m above the feet; not yet checked in play.
- The GPU self-test draws the five faces as these cameras place them, the colour display-encoded and the depth linear (1
  for the sky): every voxel on the scene. It also checks each face camera's frustum: a point ahead inside all six planes,
  one past each side outside it.

**Testing beside other work.** The game pauses into its menu whenever its window loses focus, and reads the keyboard
through DirectInput, which a window without focus does not get. A scratch add-on (not part of the rig) told the window it
was always active and pressed Space through the engine's own input (IInput::PostInputEvent, slot 7) to get past
"Press any key"; `+map village` started the level. Each `+map` writes an autosave.

## 21. Gang Beasts (Unity 2021.3 IL2CPP, URP, D3D11): the Unity rig through BepInEx 6 - working in a local match, 2026-10-21

The Xbox app (PC Game Pass) copy, `D:\XboxGames\Gang Beasts\Content`: Unity 2021.3.33f1, built with IL2CPP (a
`GameAssembly.dll`, no managed assemblies), the Universal Render Pipeline, D3D11. No anti-cheat in local play. The rig's
Unity module is a BepInEx 5 plugin, which cannot load into IL2CPP, so the same sources are built a second time as a
BepInEx 6 plugin (`rig/unity/il2cpp`), against the interop assemblies BepInEx 6 generates on the game's first start
(BepInEx 6.0.0 build 788, LGPL-2.1, in `redist/`).
- Mods load in the Xbox app copy: ReShade as `dxgi.dll`, and BepInEx 6 through its doorstop, in `Content\`.
- BepInEx 6 loads the rig, and the game registers the rig's camera class. On the title screen, following the scene's
  camera as a stand-in, all five face cameras render through URP, 28.8 captures a second, and the add-on finds every
  face's target and its own depth buffer.
- The game tags no player, so the rig follows the first `Femur.Actor` that is local and not AI, its feet from the
  hips and feet bones (the beast is a ragdoll: its own object stays where it was spawned). Unity's old input is off in
  the game, so F7 is read from Windows while the game is in front. The game pauses when it loses focus.
- In a local match (2026-10-21) the rig follows the player's beast at once, and the cameras capture 30 times a second,
  but at first every volume came out with **no voxels**: the faces arrived, their depth buffers were empty. URP draws a
  camera into textures of its own and copies only the colour across to the camera's target whenever its renderer has
  features and keeps intermediate textures "always" (URP's default), or the camera wants a depth or opaque texture or
  post-processing. Gang Beasts' renderer (forward) has four: OpaqueSurfaceFog, SSAO, NewKawaseBlur,
  NewScreenSpaceReflections. Turning off the camera's own depth and opaque textures, post-processing and anti-aliasing
  was not enough. Now (`rig/unity/Urp.cs`), while the rig's cameras render, the renderer's features are off and its
  intermediate textures are left to "auto" (and depth priming off); straight after, the renderer is as the game had it,
  so the player's view keeps them. The add-on's statistics line now counts the draws made into each face's target
  ("drawn into"): one or two a capture means only a copy reached it.
- Measured then, five faces of 512 px (four sides 512x256, the top 512x512), radius 10 m: 30 volumes a second, 7,500
  to 240,000 voxels a volume by where the beast was, 25 to 240 draws into each face a capture, the game
  holding its 60 frames a second and the worst present 1.3 ms. In the simulator the curved roof the beast lay on, and
  the beast, came through in place. Colours read darker than the game's, which has its fog, blur and grading on top:
  the Cameras tab's brightness lifts them.
- Not yet: the beast's look checked closely (player look), a second local player. Risk of Rain 2, which shares the
  follow code and now `Urp.cs` (it has no URP, so `Urp` does nothing there), is to be run again.

## 22. Gears 5 and Gears Tactics (The Coalition's Unreal 4 branch, Xbox app): not supported, 2026-10-21

- **Gears 5** (`D:\XboxGames\Gears 5`, 1.1.967.0; Hivebusters is add-on content run by the same exe): the Xbox app
  starts `Gears5_EAC.exe`, Easy Anti-Cheat's launcher, which starts `Gears5.exe`, and the install adds the Easy
  Anti-Cheat service - over every mode, the campaign too. Nothing was installed.
- **Gears Tactics** (`D:\XboxGames\Gears Tactics`, 1.0.151.0, D3D12): no anti-cheat; started straight from its exe.
  Installed through the app (the standard Unreal install), it ran about 17 s and ended itself: a fail-fast
  (0xC0000409) in `GearsTactics.exe` at exe+0x5D4F600. UE4SS meanwhile reported the engine as **Unreal 4.11** and
  failed every scan for the engine's object list and name functions (115 tries): UE4SS supports 4.12 and up. Then,
  one change at a time, the same end at the same address: with ReShade and the add-on only, and with no DLL of the
  rig's loading at all (only its settings files and UE4SS's Mods folder present). With the folder put back by Remove
  rig (checked file by file against a list taken before the install) the game ran on. So the game checks its own
  folder and ends itself when anything is added - copy protection (reported as Arxan), which the rig does not get
  round. The ReShade add-on had loaded and its decoder started on the game's D3D12 device before the end.
- Kept from the attempt: the app's Launch game now finds an Xbox app manifest up to three folders above the exe, where
  an Unreal game's exe sits (`Content\GearGame\Binaries\WinStore`), with a self-test check.
- The other Gears games on PC Game Pass: Gears of War 1-3 and Judgment are Xbox 360 games, streamed from the cloud on
  a PC; Gears of War 4 and Ultimate Edition are old Store (UWP) packages in the locked `WindowsApps` folder (and
  Ultimate Edition is Unreal 3); Gears of War: Reloaded and E-Day are reported to use Easy Anti-Cheat.

## 23. Totally Accurate Battle Simulator (Unity 2019.4 Mono, D3D11): the volume follows the game camera's view - working, 2026-10-21

The Xbox app (PC Game Pass) copy, `D:\XboxGames\Totally Accurate Battle Simulator\Content`, 1.0.27.0: Unity
2019.4.33f1, Mono (`TABS_Data\Managed`), Unity's built-in renderer, D3D11. No anti-cheat in the install; the Xbox app
starts `TABS.exe` directly. Photon's libraries are there for online battles: campaign and sandbox only.
- The standard Unity install (BepInEx 5 and the rig, ReShade and the add-on) went in through the app and the game ran.
  The rig is built against Risk of Rain 2's Unity 2021.3; it also builds against this game's own 2019.4 assemblies,
  so it uses nothing newer, and it loaded and ran here unchanged. Unity's old input is on: F7 is read the usual way.
- Nobody is the player: armies are placed, then watched from a free camera (or one soldier taken over). The first
  try stood the volume on the battlefield's middle (`CameraMovement.m_mapCenter`): 30 volumes a second of the sand,
  about 133,000 voxels, but the fighting was not in it, and what the volume should hold is what the screen shows.
- So the Unity rig can now follow the view (`followView`, `rig/unity/Follow.cs`): the feet are the first thing
  straight ahead of the game's camera that the game does not move (the ground, a wall; a soldier's body is a
  rigidbody, and is passed over), and the volume holds World radius when the camera is `followView` metres from it,
  in proportion nearer and further; both ease over a fifth of a second. The rig scales itself, every length with the
  radius (`Geometry.Derive`'s zoom); perspective lenses take the same pictures as of a world that much smaller, so the
  add-on decodes it at the settings' radius and needs no word of it. Turning with the player turns it with the game's
  camera. Measured: the volume's centre followed the view, and its scale ran from 1.98 to 4.41 m per display unit (a
  radius of 8 to 18 m) as the camera drew back; 30 volumes a second, five 512 px faces, 117,000-133,000 voxels, the
  game at 440-670 fps in battle.
- The sand fills the faces, so the automatic exposure darkens it (about -0.9 stops): the profile's Brightness is 0.
- Not yet: the look on the display judged, a unit taken over (`CameraAbilityPossess`), the faces at 1024 px measured.

## 24. F1 2020 (Codemasters' EGO, D3D12): the game's own reflection cubemap, from the add-on's cameras - the Showroom working, 2026-10-23

Steam, `D:\SteamLibrary\steamapps\common\F1 2020`, build 759689. Two exes: `F1_2020.exe` (DirectX 11) and
`F1_2020_dx12.exe`. No anti-cheat in the install; offline modes only.
- **DirectX 11 is out on this PC**: the game stopped with "D3D Device has been removed (0x887A0005)" as a race loaded,
  with the rig in and with it taken out again (Remove rig), with TAA alone and checkerboard off. DirectX 12 runs.
- **The route.** The game's views (a pass probe in the add-on, `voxon_probe.cmd`, since removed): the player's, the
  mirrors (512 x 192), the car's reflection map (two car-aligned views) and, with Vehicle Reflections at Ultra High, a
  **reflection cubemap round the car**: six 512 px views every frame, each the full deferred pipeline (G-buffer,
  lighting, its own depth; GPU markers `Cubemap_0_-X0GbufferExport`), in a race and in the Showroom, the game at 59 fps.
  Those are extra cameras drawn in the same frame as the player's: the cube's faces, once they stand where the rig's do.
- **The views' cameras** (no RTTI in the exe; traced with a hardware-breakpoint debugger and memory scans). Every view
  is an object with a camera record at +0x10 and last frame's at +0x160 (0x150 bytes: near +0x2C, far +0x30, lens
  extents +0x34/+0x38, 2 / field of view +0x68, projection +0x70 with reversed depth, world to view +0xB0, view to
  world +0xF0 with rows right, up, back, place - Frostbite's frame). A cube face's: near 0.001 m, far 200 m, 90 degrees
  square, along the world's axes, centred on the car. The renderer replays the game's commands once a frame
  (exe+0x155A0E0, from exe+0x15573D0); a view's camera arrives as a lens command and a transform command, each merged
  into the record by a handler (exe+0x1497AE0). Written straight after the handler, the rig's camera was half undone by
  the next command; so the handler only learns which views are the cube's (and where the car is), and the rig's cameras
  go in once the replay has returned to the per-frame renderer, before anything reads them.
- **The pictures.** The game names its D3D12 resources after its assets: the cube's colour is
  `cubemap_target_0` (R11G11B10, 512 px, 6 layers, 9 levels), and every face is drawn with one shared depth buffer
  (`cubemap_gbuffer_depth_stencil_0`, R24G8). A face's lighting pass binds its slice of the colour with that depth; at
  that bind the depth holds the face whole and is copied, in the game's own command list, out of the state its barriers
  left it in. The colour keeps every face until the next frame and is copied at present. The cube's slices are in D3D's
  order (+X -X +Y -Y +Z -Z) and are the views' looks; the rig's five faces take five of them, the one looking up stays
  the game's. Decoded in Frames mode (the depth reversed, with each face's near and far).
- **Measured, the Showroom**: the rig's views showed the car on its turntable from the sides and from above (dumped and
  looked at); 30 volumes a second to the app, three faces of 512 px, the game at 59 fps (its limiter is at 60; not measured without the rig). At a World
  radius of 6.5 m the showroom's roof is inside the volume and covers the car; at 2.25 m the car came through (55,000
  voxels, its own colours), longer than the display at that scale: the profile's radius is 3 m.
- **In a race** the volume follows the car and turns with the player's view (the view with the screen's lens, found
  when the game updates it). But the game leaves the player's own car out of its cubemap (no self-reflection): the
  volume holds the track, the barriers and the other cars, not the player's car. A lead: the per-object render flags
  the exe prints as `ShouldAllowInEnvmaps` (+0x24 of a render-settings block).
- While the rig is on, the car's paint reflects the rig's views. The game's settings file (My Games\F1 2020) was
  changed for the tests: Vehicle Reflections Ultra High, the window 1280 x 720 at 60 fps.

## 25. Minecraft Dungeons II (Unreal 5.6, Xbox app): the rig as a Blueprint Loader mod - working, 2026-10-23; Minecraft Dungeons: not supported

Both from the Xbox app (PC Game Pass); neither has anti-cheat; both exes are protected. Minecraft Dungeons II's
protection ends the game once its code is hooked, so UE4SS cannot carry the rig there; but the game loads content mods
through Blueprint Loader, and the rig went in as one, with none of the game's code touched.

**Minecraft Dungeons II through Blueprint Loader: working** (`D:\XboxGames\Minecraft Dungeons II`, 1.1.3.0).
- **The way in.** Blueprint Loader (ewanhowell5195's, Nexus Mods; the player's own download) loads the pak files in
  `Dungeons\Content\Paks\~mods\<mod>\` and spawns each mod's `ModActor` Blueprint in every level. The rig's mod
  (`redist/blueprint/mcdungeons2/VoxonRig_P.*`) is such an actor: rig.lua's cube as a Blueprint, made of the engine's
  own classes only (a pak mod cannot bring code). ReShade and the add-on go in beside the exe as for any Unreal game;
  `engine=blueprint` takes the add-on's Unreal path, which finds the targets by their face codes.
- **Built, not drawn by hand.** `tools/mcdungeons2-mod/make_mod.py` makes the whole Blueprint - 18 capture components,
  about 640 graph nodes - inside a copy of the Blueprint Mod Template, in Unreal 5.6.1's editor run headless. Stock
  Python cannot make graph nodes, so a small editor plugin of ours (`VoxonGraph`) makes them for it. The template's
  `build_mod.py` cooks and packs the result (`tools/mcdungeons2-mod/README.md`).
- **Settings.** A Blueprint cannot read `control.txt`. The app works the rig out itself - each camera's place, turn,
  lens and clip, as rig.lua's geometry() and place() do - and writes it with the settings the mod acts on as a 32 x 1
  PNG of 16 bits a channel beside the game's project (`Dungeons\voxon_rig.png`, `BlueprintBridge.cs`): an image is the
  one file a Blueprint can read. The mod reads it twice a second (ImportFileAsTexture2D, drawn into a 32-bit float
  target, read back) - 72 values: each face on or off, its place, turn, lens and clip; the rate; the rig's yaw and
  turning with the player; the face size; Player look; Lighting below 1; perspective or orthographic. The add-on reads
  `control.txt` as it always has. Every change on the Cameras tab reached the game within a second (faces off and on,
  World radius, Lighting, Player look, perspective and orthographic lenses); the rig scale keys move it in half-second
  steps. The app writes the file only while the rig is installed, so Remove rig leaves the folder as it was.
- **Rebuilds.** The add-on looks for a face's code only in targets made in its last 5 s, and lets go of a face turned
  off for 5 s. The first build cleared its targets once: a face turned off and on again was never found again, and the
  volumes stopped (decoded 0 a second). rig.lua makes new targets whenever which faces, their size, Lighting below 1 or
  Player look change; the mod now does the same (`CreateRenderTarget2D`, the codes cleared in, captures held off 2 s),
  and a face turned back on was found again at once.
- **Player look.** Each face's third capture shows only the level's Pawns, listed again every second: the hero and the
  mobs take the player's brightness, gamma, saturation and density.
- **Measured**, the game windowed at about 1980 x 1100: five 512 px faces, 30 volumes a second; about 134,000 voxels at
  World radius 8 m, 193,000 at 16 m, 259,000 with orthographic lenses; the game at 330-420 fps, about 260-280 with
  Player look on and Lighting below 100% (three captures a face).
- **Not in it:** see-through surfaces (a Blueprint has no way to list the materials the cameras meet), and F7 (the
  Unreal rig binds it in its Lua; the faces' boxes on the Cameras tab do the same).
- Install rig refuses until Blueprint Loader is in `~mods`, and says where to get it. Remove rig takes out the mod's
  folder, the settings file, ReShade and the add-on, and leaves Blueprint Loader: checked file by file in the game, and
  in `--selftest` (section 20).

**Minecraft Dungeons II through UE4SS: not possible.** Kept as the record of why the route above was needed.

- **Minecraft Dungeons** (`D:\XboxGames\Minecraft Dungeons`, `Microsoft.Lovika` 1.17.0.0; Unreal 4.22.3, D3D11).
  With the rig in, the game stopped as it started, inside UE4SS (0xC0000005 at UE4SS.dll+0x91a840), before UE4SS had
  written a line. UE4SS hooks `LoadLibrary*` through the import table of every module, the exe first (PolyHook's
  `IatHook`), and expects each import entry to carry a table of names. Read from the running game's memory, this exe's
  import table is a stub: every DLL has one import and no name table (`OriginalFirstThunk` 0); its protection fills
  the real imports in itself. PolyHook reads the exe's header as a name and faults. Both UE4SS builds in `redist/`
  do the same. Without UE4SS (ReShade and the add-on only) the game ran to its title. Changing the protected exe's
  import table in memory to suit UE4SS would be working round its protection: not done.
- **Minecraft Dungeons II** (`D:\XboxGames\Minecraft Dungeons II`, 1.1.3.0; `Dungeons-WinGDK-Shipping.exe`, Unreal
  **5.6.1** with Hazelight's AngelScript plugin, D3D12). Untouched, it runs. What was found, in order:
  1. Loaded at the start (UE4SS's `dwmapi.dll`), UE4SS's first thread runs the exe's per-thread start-up code, whose
     one-time initialiser calls a function still **encrypted in memory** (exe+0x11116a0, a function in `.pdata` whose
     bytes are noise): the game decrypts its own code later in its start-up. The fault could not be unwound, and the
     game died in `__chkstk` (ntdll).
  2. Loaded later, at the title screen (by hand, for the test), UE4SS's scans all succeeded (GUObjectArray,
     FName::ToString, GameEngineTick, ...; engine 5.6).
  3. It then read `UClass::ClassDefaultObject` from the wrong place: the AngelScript engine adds 0x60 bytes to UClass
     after `ClassWithin` (a map with one entry per native function, then a pointer and an int). Measured from the
     game's objects (KismetStringLibrary, Actor): `ClassConfigName` 0x144, `ClassReps` 0x150, `NetFields` 0x160,
     `FirstOwnedClassRep` 0x168, `ClassDefaultObject` 0x170, `SparseClassData` 0x178, `SparseClassDataStruct` 0x180,
     `FuncMap` 0x188, `FuncMapLock` 0x1D8, `AllFunctionsCache` 0x1E0, `AllFunctionsCacheLock` 0x230, `Interfaces`
     0x238, `ReferenceSchema` 0x248, `NativeFunctionLookupTable` 0x250 (UClass is 0x260 bytes); everything before,
     and UStruct, UFunction and FField/FProperty, are stock 5.6. With those in a `MemberVariableLayout.ini` beside
     UE4SS, UE4SS started completely: its hooks in, its object-hash self-test passed, the rig's Lua mod armed.
  4. About 3 s after UE4SS's hooks went into the game's code, the game ended - no crash, no Windows Error Reporting
     entry, no Unreal crash report, nothing from UE4SS's own crash handler. In a level, a debugger merely attaching
     (to watch how it ends, before UE4SS was loaded) met two illegal-instruction probes and the game ended at once.
  Code decrypted at run time, an anti-debugger check, and a silent end once its code is changed: anti-tamper
  protection. The Unreal rig has to hook the game's code (at the least its engine tick); finding a way the protection
  does not notice would be working round it. So the rig went in as content instead (above).
- Kept: nothing of UE4SS in the code. The UClass layout above is here for anyone who looks at an AngelScript Unreal
  5.6 game that has no such protection.
- Both folders were checked file by file after Remove rig against lists taken before the install (the only files left
  are ones the game writes itself when it runs: Minecraft Dungeons' `ClockSkew.json` and an empty `debug.log`).
  Minecraft Dungeons was not tried through a content mod.

## 26. Undisputed (Unity 2022.3 IL2CPP, HDRP), and four games with anti-cheat (Xbox app), 2026-10-23

The user's list after Minecraft Dungeons II: Madden NFL 26, EA Sports FC 26, Undisputed, Rematch and F1 25, all
Xbox app (PC Game Pass) copies on this PC. Each install was looked at before anything went in.

- **Madden NFL 26, EA Sports FC 26, F1 25: not supported - EA Javelin anti-cheat.** Each folder holds EA's
  anti-cheat launcher (`EAAntiCheat.GameServiceLauncher.exe` with its `.cfg` and `.dll`; F1 25 also its installer)
  *(measured)*. EA's anti-cheat covers single player too (`docs/EA_GAMES.md`), and the rig - ReShade and an add-on in the
  game's process - is what it exists to stop. Starting the games' exes without it would be getting round it. Nothing was
  installed. (F1 25 is the EGO engine of F1 2020, whose reflection-cubemap route would otherwise carry over.)
- **Rematch: not supported - Easy Anti-Cheat on every launch.** The Xbox app starts `start_protected_game.exe`
  (`MicrosoftGame.config`), Easy Anti-Cheat's launcher, which installs `EasyAntiCheat_EOS` *(measured)*: the same as
  Gears 5 (section 22). Nothing was installed.
- **Undisputed** (`D:\XboxGames\Undisputed\Content`, 2.0.6.0; Unity 2022.3.45f1, IL2CPP, HDRP, DX11; no anti-cheat in its
  folder): **in progress.** The Unity rig's IL2CPP build through BepInEx 6, as Gang Beasts; BepInEx built the game's
  interop assemblies on the first start (about two minutes) and the rig loaded.
  - **HDRP under IL2CPP.** HDRP hands a camera's target only its finished picture, never its depth; the rig asks HDRP
    for the lit colour and the depth through AOV requests (Cities: Skylines II, Mono). `Hdrp.cs` now does the same in
    IL2CPP games, through BepInEx's interop classes: the game's fields are properties there, the array of outputs an
    `Il2CppStructArray`, and HDRP's two callbacks game delegates made from ours (`DelegateSupport.ConvertDelegate`).
    In Undisputed HDRP accepted the requests for all five cameras, the expensive frame settings went off for them, and
    the add-on found every face and its HDRP depth target (codes 1100-1104) on its first look: 30 volumes a second, the
    game at its 60 fps.
  - **In a fight (the first-run tutorial bout): working.** The volume follows the view - the ring the broadcast camera
    looks at, turning with it - and holds the ring floor, the ropes and corner posts, both boxers and the lighting truss
    above: about 100,000 voxels, 23-27 volumes a second, with five 512 px faces; the game at 26-28 fps at 3840 x 2160
    (each face is a full HDRP render). The colours come out pale at the profile's exposure. The title screen has nothing
    the cameras can see. The game ignores injected keys and clicks (GameInput, as Gang Beasts), so for the test its menus
    were driven by a virtual Input System gamepad from a test-only BepInEx plugin, not part of the rig.
  - **Centred between the two boxers** (the user's ask): the Unity rig can now follow every object of a class, not the
    first (`followAll`), its feet above their middle - here each boxer's `BoxerCore` (the game's own class), by its
    `head`: the component's own transform is not where the boxer stands (following it put the pair at the volume's back
    edge). The height comes from `feetHeight`, now for any follow, not only the view: the ring's canvas, world height 0.
    In an AI-only quick fight (Ali against Ali) both boxers stand on the display's centre axis, the canvas across the
    volume and the ropes round it; about 240,000-280,000 voxels at 22-24 volumes a second (five 768 px faces).
  - The rig's targets vanished when the fight loaded after its intro (faces found, then never written, and the next
    rebuild failed releasing them): the game unloads unused assets at a level change, and under IL2CPP nothing on the
    game's side refers to the rig's render textures. They are now `HideAndDontSave`, which keeps them.
  - Remove rig left `ReShade2.ini` behind: ReShade writes a numbered settings file when a second process of the game's
    loads it while the first holds `ReShade.ini`. Remove now deletes ReShade's numbered settings and logs with its own
    (`--selftest`), and Undisputed's folder was then exactly as before, file by file.
