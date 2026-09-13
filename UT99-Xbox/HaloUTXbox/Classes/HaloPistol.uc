class HaloPistol extends HaloWeapon;

#exec MESH MODELIMPORT MESH=PistolView MODELFILE=Models\Pistol1st.psk LODSTYLE=12
#exec MESH ORIGIN MESH=PistolView X=0 Y=0 Z=0 YAW=-64
#exec MESH MODELIMPORT MESH=PistolWorld MODELFILE=Models\Pistol3rd.psk LODSTYLE=12
// Anchor the authored Bone_weapon grip in reference mesh coordinates.
#exec MESH ORIGIN MESH=PistolWorld X=-0.240050 Y=22.606197 Z=6.057712 YAW=-64.453125
#exec ANIM IMPORT ANIM=PistolAnims ANIMFILE=Models\Pistol.psa IMPORTSEQS=1 COMPRESS=1
#exec ANIM DIGEST ANIM=PistolAnims
#exec MESH DEFAULTANIM MESH=PistolView ANIM=PistolAnims
#exec MESH DEFAULTANIM MESH=PistolWorld ANIM=PistolAnims
#exec TEXTURE IMPORT NAME=PistolSkin FILE=Textures\PistolSkin.pcx GROUP=Skins
#exec AUDIO IMPORT NAME=PistolFire FILE=Sounds\PistolFire.wav GROUP=Weapons
#exec MESHMAP SETTEXTURE MESHMAP=PistolView NUM=0 TEXTURE=PistolSkin
#exec MESHMAP SETTEXTURE MESHMAP=PistolWorld NUM=0 TEXTURE=PistolSkin

simulated function bool ClientAltFire(float Value)
{
    GotoState('Zooming');
    return true;
}

function AltFire(float Value)
{
    ClientAltFire(Value);
}

state Zooming
{
    simulated function BeginState()
    {
        local PlayerPawn P;
        P = PlayerPawn(Owner);
        if (P != None)
        {
            if (P.DesiredFOV == P.DefaultFOV)
                P.DesiredFOV = P.DefaultFOV / 2;
            else
                P.DesiredFOV = P.DefaultFOV;
        }
        else
        {
            Pawn(Owner).bAltFire = 0;
            Pawn(Owner).bFire = 1;
            Global.Fire(0);
        }
    }

    simulated function Tick(float DeltaTime)
    {
        if (Pawn(Owner) == None || Pawn(Owner).bAltFire == 0)
            GotoState('Idle');
    }
}

defaultproperties
{
    PlayerViewScale=0.7
    PlayerViewOffset=(X=20.0,Y=-9.0,Z=-12.5)
    ItemName="HaloUT Pistol"
    PickupMessage="You got the HaloUT Pistol."
    WeaponDescription="HaloUT UT-style pistol. Alternate fire toggles 2x zoom."
    Mesh=SkeletalMesh'HaloUTXbox.PistolWorld'
    PlayerViewMesh=SkeletalMesh'HaloUTXbox.PistolView'
    PickupViewMesh=SkeletalMesh'HaloUTXbox.PistolWorld'
    ThirdPersonMesh=SkeletalMesh'HaloUTXbox.PistolWorld'
    AmmoName=Class'HaloUTXbox.HaloPistolAmmo'
    PickupAmmoCount=60
    ShotDamageMin=20.0
    ShotDamageMax=20.0
    FireAnimRate=1.837037
    FireSound=Sound'HaloUTXbox.Weapons.PistolFire'
    InventoryGroup=2
    AutoSwitchPriority=2
    AIRating=0.35
    bRapidFire=False
}
