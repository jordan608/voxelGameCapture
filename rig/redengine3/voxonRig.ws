// Voxon Capture Rig - the cube of cameras for The Witcher 3 (REDengine 3), the script half.
//
// REDengine 3 cannot render a second camera, so the cube is swept by the game's own. Frame by frame the engine's free
// camera stands on the next face of a box round Geralt, looking in, with the world's near plane on the box's face -
// what stands between camera and box is cut away - and its far plane set to a code for the face. After the last face
// comes one frame of the player's own camera. The add-on (addon/sweep_red3.hpp) reads each frame's camera from the GPU,
// knows a face frame by its far plane, places it by its own camera, and builds the volume.
//
// The app's settings arrive in CVoxonRigLink, which the add-on finds in the game's memory and writes
// (addon/red3_link.hpp). The rig runs only while the add-on says the app is taking volumes, and never in a menu, a
// dialogue or a cutscene. While it runs the HUD is hidden: it would be drawn into the faces.
//
// The player's script (game/player/r4Player.ws) is the game's own with three additions the installer makes
// (r4Player.patch.json), marked VOXON RIG: this rig's state, a timer that runs every frame, and the timer started
// when Geralt spawns. The camera maths is addon/sweep_red3.hpp's Red3Place and the compact pass's, term for term.

// The settings, as the add-on writes them. Floats only, in this order (red3_link.hpp, LinkField).
class CVoxonRigLink
{
	var m0 : float;         // markers: the add-on finds the object by these three in a row
	var m1 : float;
	var m2 : float;
	var alive : float;      // this script's ticks: the live object's moves, a stale copy's does not
	var serial : float;     // raised by the add-on after each write
	var on : float;         // 1 while the app is taking volumes
	var radius : float;     // World radius (m): the display's half-width
	var boundR : float;     // the display's half-width and half-height, in its own units
	var boundZ : float;
	var scaleX : float;     // World X, Y, height: how much world each of the rig's axes holds
	var scaleY : float;
	var scaleZ : float;
	var feetX : float;      // where Geralt's feet stand in the display, in its units (+Z down)
	var feetY : float;
	var feetZ : float;
	var yaw : float;        // the rig turned (degrees)
	var turn : float;       // 1: the volume turns with the player's view
	var faces : float;      // the faces on, a bit each: 0 +X, 1 +Y, 2 -X, 3 -Y, 4 top
	var tanH : float;       // the free camera's lens: tan of its half fields of view, across and up
	var tanV : float;
	var native : float;     // 1: the add-on renders the faces itself, in the player's frame (addon/red3_native.hpp)
	var viewsOn : float;    // from here on the script reports: 1 while it wants the faces (native)
	var centreX : float;    // the volume's centre (world) and the rig's yaw (radians, the display's frame)
	var centreY : float;
	var centreZ : float;
	var rho : float;
}

class CVoxonRig
{
	var link : CVoxonRigLink;
	var running : bool;
	var gameNear : float;   // the world's own planes, put back on the player's frames and when the rig stops
	var gameFar : float;
	var step : int;         // where in the cycle: a face, or the player's own frame
	var freeOn : bool;      // the free camera on: turned on and off only when that changes
	var hudHidden : bool;   // this hid the HUD (the sweep does: its faces are the game's whole frames)
	var viewAngle : float;  // the player's view, an angle in the display's frame (radians), from their own frames

	function Init()
	{
		link = new CVoxonRigLink in this;
		link.m0 = 739147.25;
		link.m1 = -1234.0625;
		link.m2 = 98765.5;
		link.boundR = 4.0;
		link.boundZ = 2.0;
		link.radius = 4.0;
		link.scaleX = 1.0;
		link.scaleY = 1.0;
		link.scaleZ = 1.0;
		link.faces = 31.0;
		link.tanV = 0.48773;
		link.tanH = 0.78037;
	}

	function Wanted() : bool
	{
		return link.on > 0.5 && link.radius > 0.1 && link.tanH > 0.01 && link.tanV > 0.01
			&& !theGame.IsPaused() && !theGame.IsDialogOrCutscenePlaying() && !theGame.IsBlackscreenOrFading()
			&& !theGame.GetGuiManager().IsAnyMenu() && thePlayer.IsAlive();
	}

	function Tick(dt : float)
	{
		var faceList : array<int>;
		var i, next : int;

		link.alive += 1;
		if (link.alive > 1000000)
			link.alive = 1;
		if (!Wanted())
		{
			Stop();
			return;
		}
		if (!running)
			Start();
		if (link.native > 0.5)
		{
			NativeTick();
			return;
		}
		for (i = 0; i < 5; i += 1)
			if (FaceOn(i))
				faceList.PushBack(i);
		if (faceList.Size() == 0)
		{
			Stop();
			return;
		}
		// Every face on, then one frame of the player's own camera: the cube refreshes at a (faces + 1)th of the
		// frame rate, and so does the player's view. The engine takes the free camera's on and off, and the world's
		// planes, in the frame they are set, but a move a frame later (measured: a frame's planes came with the camera
		// of the face before). So this frame's face gets the camera on and its planes now, and the camera is moved for
		// the next frame's face.
		step = (step + 1) % (faceList.Size() + 1);
		if (step < faceList.Size())
		{
			FreeCamera(true);
			FacePlanes(faceList[step]);
		}
		else
			PlayerFrame();
		next = (step + 1) % (faceList.Size() + 1);
		if (next < faceList.Size())
			MoveFace(faceList[next]);
	}

	// The add-on renders the faces itself, in the same frame as the player's view: no free camera, no sweep. The script
	// says where the volume is - its centre and the rig's yaw - and that it wants the faces.
	function NativeTick()
	{
		var dir, c : Vector;
		var rho : float;
		FreeCamera(false);
		dir = RotForward(theGame.GetGameCamera().GetCameraWorldRotation());
		if (AbsF(dir.X) + AbsF(dir.Y) > 0.01)
			viewAngle = AtanF(-dir.Y, dir.X);      // the display's frame is the world with Y turned over
		rho = Rho();
		c = Centre(rho);
		link.centreX = c.X;
		link.centreY = c.Y;
		link.centreZ = c.Z;
		link.rho = rho;
		link.viewsOn = 1;
	}

	function FaceOn(face : int) : bool
	{
		var bits : int;
		bits = (int)link.faces;
		return ((bits / PowInt(2, face)) % 2) == 1;
	}

	function PowInt(b : int, e : int) : int
	{
		var r : int;
		r = 1;
		while (e > 0) { r *= b; e -= 1; }
		return r;
	}

	function Start()
	{
		var hud : CR4ScriptedHud;
		gameNear = VxWorldNear();
		gameFar = VxWorldFar();
		running = true;
		step = 0;
		hud = (CR4ScriptedHud)theGame.GetHud();
		hudHidden = false;
		if (hud && link.native < 0.5)
		{
			hud.ForceShow(false, HVS_System);
			hudHidden = true;
		}
		LogChannel('VoxonRig', "rig on: radius " + FloatToString(link.radius) + " m, the game's near plane " + FloatToString(gameNear));
	}

	function Stop()
	{
		var hud : CR4ScriptedHud;
		link.viewsOn = 0;
		if (!running)
			return;
		running = false;
		FreeCamera(false);
		VxSetWorldPlanes(gameNear, gameFar);
		hud = (CR4ScriptedHud)theGame.GetHud();
		if (hud && hudHidden)
			hud.ForceShow(true, HVS_System);
		hudHidden = false;
		LogChannel('VoxonRig', "rig off");
	}

	// The player's own frame: the game's camera and planes; and where it looks, for a volume that turns with it.
	function PlayerFrame()
	{
		var dir : Vector;
		FreeCamera(false);
		VxSetWorldPlanes(gameNear, gameFar);
		dir = RotForward(theGame.GetGameCamera().GetCameraWorldRotation());
		if (AbsF(dir.X) + AbsF(dir.Y) > 0.01)
			viewAngle = AtanF(-dir.Y, dir.X);      // the display's frame is the world with Y turned over
	}

	// The engine's free camera on or off, only when that changes.
	function FreeCamera(on : bool)
	{
		if (on == freeOn)
			return;
		theGame.EnableFreeCamera(on);
		freeOn = on;
	}

	// The rig's yaw (radians, the display's frame): its own, and the player's view when turning with it.
	function Rho() : float
	{
		if (link.turn > 0.5)
			return viewAngle + Deg2Rad(link.yaw);
		return Deg2Rad(link.yaw);
	}

	// The volume's centre: Geralt's feet, less where they stand in the display - the compact pass undone: its turn,
	// each axis's scale, then the rig's yaw, then the display's frame back to the world's.
	function Centre(rho : float) : Vector
	{
		var s, ex, ey, ez, t, qx, qy, qz, rx, ry : float;
		var c : Vector;

		s = link.radius / link.boundR;
		ex = link.radius * link.scaleX;
		ey = link.radius * link.scaleY;
		ez = link.boundZ * s * link.scaleZ;
		t = Deg2Rad(link.yaw);
		if (link.turn > 0.5)
			t = Deg2Rad(link.yaw - 90);
		qx = (link.feetX * CosF(t) + link.feetY * SinF(t)) * ex / link.radius * s;
		qy = (-link.feetX * SinF(t) + link.feetY * CosF(t)) * ey / link.radius * s;
		qz = link.feetZ * link.scaleZ * s;
		rx = qx * CosF(rho) - qy * SinF(rho);
		ry = qx * SinF(rho) + qy * CosF(rho);
		c = thePlayer.GetWorldPosition();
		c.X -= rx;
		c.Y += ry;
		c.Z += qz;
		return c;
	}

	// How far a face's camera stands back from the box's face, so the face fits its picture (sweep_red3.hpp, Red3Place).
	function StandOff(face : int) : float
	{
		var s, ex, ey, ez : float;
		s = link.radius / link.boundR;
		ex = link.radius * link.scaleX;
		ey = link.radius * link.scaleY;
		ez = link.boundZ * s * link.scaleZ;
		if (face == 4)
			return MaxF(ex / link.tanH, ey / link.tanV);
		if (face == 1 || face == 3)
			return MaxF(ex / link.tanH, ez / link.tanV);
		return MaxF(ey / link.tanH, ez / link.tanV);
	}

	// A face's planes: the near one on the box's face, the far one carrying which face this is.
	function FacePlanes(face : int)
	{
		VxSetWorldPlanes(StandOff(face) * 0.98, 5000.0 + 100.0 * face);
	}

	// The free camera to one face (sweep_red3.hpp, Red3Place): side face 0 on the rig's +X side, 1 on +Y, 2 on -X, 3 on
	// -Y, as the app names them, standing back from its face until the face fits the picture, looking in; the top one
	// looks straight down, its picture's top the rig's +Y (the display's front).
	function MoveFace(face : int)
	{
		var rho, s, ex, ey, ez, along, dist : float;
		var centre, look, ax, ay, pos : Vector;
		var rot, flat : EulerAngles;

		rho = Rho();
		s = link.radius / link.boundR;
		ex = link.radius * link.scaleX;
		ey = link.radius * link.scaleY;
		ez = link.boundZ * s * link.scaleZ;
		centre = Centre(rho);
		ax = Vector(CosF(rho), -SinF(rho), 0);     // the rig's X and Y, in the world
		ay = Vector(-SinF(rho), -CosF(rho), 0);
		if (face < 4)
		{
			if (face == 0) look = Vector(-ax.X, -ax.Y, 0);
			else if (face == 1) look = Vector(-ay.X, -ay.Y, 0);
			else if (face == 2) look = ax;
			else look = ay;
			along = ex;
			if (face == 1 || face == 3)
				along = ey;
			dist = along + StandOff(face);
			pos = centre - look * dist;
			rot = VecToRotation(look);
		}
		else
		{
			dist = ez + StandOff(face);
			pos = centre;
			pos.Z += dist;
			// Straight down (exactly: a hair off it came out looking up), then turned so the picture's up is the rig's Y.
			rot = VecToRotation(Vector(0, 0, -1));
			flat = VecToRotation(ay);
			rot.Yaw = flat.Yaw;
		}
		theGame.MoveFreeCamera(pos, rot);
	}
}

function VxWorldNear() : float
{
	var world : CGameWorld;
	world = (CGameWorld)theGame.GetWorld();
	if (!world)
		return 0.2;
	return world.environmentParameters.renderSettings.cameraNearPlane;
}

function VxWorldFar() : float
{
	var world : CGameWorld;
	world = (CGameWorld)theGame.GetWorld();
	if (!world)
		return 6500;
	return world.environmentParameters.renderSettings.cameraFarPlane;
}

// The world's camera planes, written in place: copying the whole environment struct out and back crashed the game.
function VxSetWorldPlanes(nearM : float, farM : float)
{
	var world : CGameWorld;
	world = (CGameWorld)theGame.GetWorld();
	if (!world)
		return;
	world.environmentParameters.renderSettings.cameraNearPlane = nearM;
	world.environmentParameters.renderSettings.cameraFarPlane = farM;
}

// The rig on the player: made the first time its timer runs.
function VxRig() : CVoxonRig
{
	var player : CR4Player;

	player = thePlayer;                 // thePlayer itself is const: its members are set through a variable
	if (!player.vxRig)
	{
		player.vxRig = new CVoxonRig in player;
		player.vxRig.Init();
	}
	return player.vxRig;
}

// The player's VxRigTimer (game/player/r4Player.ws, VOXON RIG), every frame.
function VxRigTick(dt : float)
{
	VxRig().Tick(dt);
}

// From the debug console: what the rig has from the app.
exec function vxrig()
{
	var l : CVoxonRigLink;
	var mode : string;
	l = VxRig().link;
	mode = "stopped";
	if (VxRig().running)
		mode = "running";
	LogChannel('VoxonRig', "link: serial " + FloatToString(l.serial) + " on " + FloatToString(l.on) + " radius "
		+ FloatToString(l.radius) + " scale " + FloatToString(l.scaleX) + "/" + FloatToString(l.scaleY) + "/" + FloatToString(l.scaleZ)
		+ " faces " + FloatToString(l.faces) + " lens " + FloatToString(l.tanH) + " x " + FloatToString(l.tanV) + ", " + mode);
}
