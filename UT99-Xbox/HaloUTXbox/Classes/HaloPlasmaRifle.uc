class HaloPlasmaRifle extends HaloWeapon;

#exec MESH MODELIMPORT MESH=PlasmaView MODELFILE=Models\Plasma1st.psk LODSTYLE=12
#exec MESH ORIGIN MESH=PlasmaView X=0 Y=0 Z=0 YAW=-64
#exec MESH MODELIMPORT MESH=PlasmaWorld MODELFILE=Models\Plasma3rd.psk LODSTYLE=12
// Anchor the authored Bone_weapon grip in reference mesh coordinates.
#exec MESH ORIGIN MESH=PlasmaWorld X=-2.414851 Y=18.354713 Z=7.036623 YAW=-64.453125
#exec ANIM IMPORT ANIM=PlasmaAnims ANIMFILE=Models\Plasma.psa IMPORTSEQS=1 COMPRESS=1
#exec ANIM DIGEST ANIM=PlasmaAnims
#exec MESH DEFAULTANIM MESH=PlasmaView ANIM=PlasmaAnims
#exec MESH DEFAULTANIM MESH=PlasmaWorld ANIM=PlasmaAnims
#exec TEXTURE IMPORT NAME=PlasmaSkin FILE=Textures\PlasmaSkin.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=HeatGauge FILE=Textures\HeatGauge.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=Invisible FILE=Textures\Invisible.pcx GROUP=Skins FLAGS=2
#exec AUDIO IMPORT NAME=PlasmaFire FILE=Sounds\PlasmaFire.wav GROUP=Weapons
#exec MESHMAP SETTEXTURE MESHMAP=PlasmaView NUM=0 TEXTURE=HeatGauge
#exec MESHMAP SETTEXTURE MESHMAP=PlasmaView NUM=1 TEXTURE=PlasmaSkin
#exec MESHMAP SETTEXTURE MESHMAP=PlasmaWorld NUM=0 TEXTURE=Invisible
#exec MESHMAP SETTEXTURE MESHMAP=PlasmaWorld NUM=1 TEXTURE=HeatGauge
#exec MESHMAP SETTEXTURE MESHMAP=PlasmaWorld NUM=2 TEXTURE=PlasmaSkin

defaultproperties
{
    PlayerViewScale=0.4
    PlayerViewOffset=(X=26.0,Y=-14.0,Z=-20.0)
    ItemName="HaloUT Plasma Rifle"
    PickupMessage="You got the HaloUT Plasma Rifle."
    WeaponDescription="HaloUT UT-style plasma rifle. Both triggers fire plasma bolts."
    Mesh=SkeletalMesh'HaloUTXbox.PlasmaWorld'
    PlayerViewMesh=SkeletalMesh'HaloUTXbox.PlasmaView'
    PickupViewMesh=SkeletalMesh'HaloUTXbox.PlasmaWorld'
    ThirdPersonMesh=SkeletalMesh'HaloUTXbox.PlasmaWorld'
    AmmoName=Class'HaloUTXbox.HaloPlasmaAmmo'
    PickupAmmoCount=41
    FireAnimRate=2.833333
    FireSound=Sound'HaloUTXbox.Weapons.PlasmaFire'
    bInstantHit=False
    bAltInstantHit=False
    ProjectileClass=Class'HaloUTXbox.HaloPlasmaBolt'
    AltProjectileClass=Class'HaloUTXbox.HaloPlasmaBolt'
    ProjectileSpeed=6500.0
    AltProjectileSpeed=6500.0
    InventoryGroup=5
    AutoSwitchPriority=5
    bWarnTarget=True
    bAltWarnTarget=True
}
