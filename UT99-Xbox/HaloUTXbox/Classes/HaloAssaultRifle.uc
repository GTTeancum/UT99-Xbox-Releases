class HaloAssaultRifle extends HaloWeapon;

var Texture Digits[10], CompassFaces[8];
var int DisplayedAmmo, DisplayedHeading;

#exec TEXTURE IMPORT NAME=Digit0 FILE=Textures\Digit0.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit1 FILE=Textures\Digit1.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit2 FILE=Textures\Digit2.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit3 FILE=Textures\Digit3.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit4 FILE=Textures\Digit4.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit5 FILE=Textures\Digit5.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit6 FILE=Textures\Digit6.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit7 FILE=Textures\Digit7.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit8 FILE=Textures\Digit8.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Digit9 FILE=Textures\Digit9.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass0 FILE=Textures\Compass0.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass1 FILE=Textures\Compass1.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass2 FILE=Textures\Compass2.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass3 FILE=Textures\Compass3.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass4 FILE=Textures\Compass4.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass5 FILE=Textures\Compass5.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass6 FILE=Textures\Compass6.pcx GROUP=Display
#exec TEXTURE IMPORT NAME=Compass7 FILE=Textures\Compass7.pcx GROUP=Display

#exec MESH MODELIMPORT MESH=AssaultView MODELFILE=Models\Assault1st.psk LODSTYLE=12
// Slightly expose the left side; the display stays toward the camera.
#exec MESH ORIGIN MESH=AssaultView X=0 Y=0 Z=0 YAW=-68
#exec MESH MODELIMPORT MESH=AssaultWorld MODELFILE=Models\Assault3rd.psk LODSTYLE=12
// Halo-ar_3rd's authored pivot and -16500-unit yaw are separate from PSK data.
// Anchor the authored Bone_weapon grip, evaluated through its reference parents.
#exec MESH ORIGIN MESH=AssaultWorld X=0 Y=17.965234 Z=6.045636 YAW=-64.453125
#exec ANIM IMPORT ANIM=AssaultAnims ANIMFILE=Models\Assault.psa IMPORTSEQS=1 COMPRESS=1
#exec ANIM DIGEST ANIM=AssaultAnims
#exec ANIM IMPORT ANIM=AssaultViewAnims ANIMFILE=Models\AssaultView.psa IMPORTSEQS=1 COMPRESS=1
#exec ANIM DIGEST ANIM=AssaultViewAnims
#exec MESH DEFAULTANIM MESH=AssaultView ANIM=AssaultViewAnims
#exec MESH DEFAULTANIM MESH=AssaultWorld ANIM=AssaultAnims
#exec TEXTURE IMPORT NAME=AssaultSkin FILE=Textures\AssaultSkin.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=Compass FILE=Textures\Compass.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=AmmoDisplay FILE=Textures\AmmoDisplay.pcx GROUP=Skins
#exec AUDIO IMPORT NAME=AssaultFire FILE=Sounds\AssaultFire.wav GROUP=Weapons
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=0 TEXTURE=AssaultSkin
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=1 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=2 TEXTURE=Compass
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=3 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=4 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultView NUM=5 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=0 TEXTURE=AssaultSkin
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=1 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=2 TEXTURE=Compass
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=3 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=4 TEXTURE=AmmoDisplay
#exec MESHMAP SETTEXTURE MESHMAP=AssaultWorld NUM=5 TEXTURE=AmmoDisplay

simulated function Tick(float DeltaTime)
{
    local int Count, Heading;
    Super.Tick(DeltaTime);
    if (Pawn(Owner) == None || Pawn(Owner).Weapon != self || AmmoType == None)
        return;
    Count = Clamp(AmmoType.AmmoAmount, 0, 99);
    Heading = (((Pawn(Owner).ViewRotation.Yaw & 65535) + 4096) / 8192) % 8;
    if (Count != DisplayedAmmo)
    {
        MultiSkins[3] = Digits[Count / 10];
        MultiSkins[4] = Digits[Count % 10];
        DisplayedAmmo = Count;
    }
    if (Heading != DisplayedHeading)
    {
        MultiSkins[2] = CompassFaces[Heading];
        DisplayedHeading = Heading;
    }
}

defaultproperties
{
    PlayerViewScale=0.30
    PlayerViewOffset=(X=8.0,Y=-4.5,Z=-5.6)
    ShakeMag=10.0
    ShakeTime=0.04
    ShakeVert=0.1
    DisplayedAmmo=-1
    DisplayedHeading=-1
    Digits(0)=Texture'HaloUTXbox.Display.Digit0'
    Digits(1)=Texture'HaloUTXbox.Display.Digit1'
    Digits(2)=Texture'HaloUTXbox.Display.Digit2'
    Digits(3)=Texture'HaloUTXbox.Display.Digit3'
    Digits(4)=Texture'HaloUTXbox.Display.Digit4'
    Digits(5)=Texture'HaloUTXbox.Display.Digit5'
    Digits(6)=Texture'HaloUTXbox.Display.Digit6'
    Digits(7)=Texture'HaloUTXbox.Display.Digit7'
    Digits(8)=Texture'HaloUTXbox.Display.Digit8'
    Digits(9)=Texture'HaloUTXbox.Display.Digit9'
    CompassFaces(0)=Texture'HaloUTXbox.Display.Compass0'
    CompassFaces(1)=Texture'HaloUTXbox.Display.Compass1'
    CompassFaces(2)=Texture'HaloUTXbox.Display.Compass2'
    CompassFaces(3)=Texture'HaloUTXbox.Display.Compass3'
    CompassFaces(4)=Texture'HaloUTXbox.Display.Compass4'
    CompassFaces(5)=Texture'HaloUTXbox.Display.Compass5'
    CompassFaces(6)=Texture'HaloUTXbox.Display.Compass6'
    CompassFaces(7)=Texture'HaloUTXbox.Display.Compass7'
    ItemName="HaloUT Assault Rifle"
    PickupMessage="You got the HaloUT Assault Rifle."
    WeaponDescription="HaloUT UT-style assault rifle. Primary and alternate fire are automatic."
    Mesh=SkeletalMesh'HaloUTXbox.AssaultWorld'
    PlayerViewMesh=SkeletalMesh'HaloUTXbox.AssaultView'
    PickupViewMesh=SkeletalMesh'HaloUTXbox.AssaultWorld'
    ThirdPersonMesh=SkeletalMesh'HaloUTXbox.AssaultWorld'
    AmmoName=Class'HaloUTXbox.HaloAssaultAmmo'
    PickupAmmoCount=300
    ShotDamageMin=6.0
    ShotDamageMax=10.0
    ShotSpread=1.0
    FireAnimRate=4.285714
    FireSound=Sound'HaloUTXbox.Weapons.AssaultFire'
}
