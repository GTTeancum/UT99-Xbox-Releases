// UT-style HaloUT weapons: UT99 handles inventory, replication and firing states.
class HaloWeapon extends TournamentWeapon abstract;

var float ShotDamageMin, ShotDamageMax;
var float ShotSpread;
var float FireAnimRate;

function TraceFire(float Accuracy)
{
    Super.TraceFire(ShotSpread);
}

function ProcessTraceHit(Actor Other, Vector HitLocation, Vector HitNormal, Vector X, Vector Y, Vector Z)
{
    if (Other != None && Other != self && Other != Owner)
        Other.TakeDamage(ShotDamageMin + FRand() * (ShotDamageMax - ShotDamageMin),
                         Pawn(Owner), HitLocation, 2500 * X, MyDamageType);
}

simulated function PlayFiring()
{
    PlayOwnedSound(FireSound, SLOT_None, Pawn(Owner).SoundDampening * 2.0);
    PlayAnim('Fire', FireAnimRate, 0.0);
}

simulated function PlayAltFiring()
{
    PlayFiring();
}

simulated function PlayIdleAnim()
{
    LoopAnim('Idle', 1.0, 0.2);
}

simulated function TweenDown()
{
    // HaloUT names the lowering sequence PutDown; TournamentWeapon expects Down.
    PlayAnim('PutDown', 1.0, 0.05);
}

defaultproperties
{
    FireAnimRate=1.0
    bInstantHit=True
    bAltInstantHit=True
    bRapidFire=True
    RefireRate=0.95
    AltRefireRate=0.95
    AIRating=0.6
    AutoSwitchPriority=4
    InventoryGroup=4
    MyDamageType=shot
    AltDamageType=shot
    PlayerViewOffset=(X=2.0,Y=-1.0,Z=-2.0)
    PlayerViewScale=0.045
    PickupViewScale=0.35
    ThirdPersonScale=0.25
    CollisionRadius=24.0
    CollisionHeight=10.0
    FireOffset=(X=12.0,Y=-5.0,Z=-8.0)
    bDrawMuzzleFlash=False
    bMeshEnviroMap=False
    ShakeMag=80.0
    ShakeTime=0.08
    ShakeVert=1.0
}
