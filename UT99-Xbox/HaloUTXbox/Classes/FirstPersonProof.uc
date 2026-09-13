// Explicit process-local verification. Never enabled by the player mutator.
class FirstPersonProof extends AttachmentProof;

var PlayerPawn TestPlayer;
var int Phase;

function ModifyPlayer(Pawn Other)
{
    Super.ModifyPlayer(Other);
    if (PlayerPawn(Other) != None)
    {
        TestPlayer = PlayerPawn(Other);
        TestPlayer.ViewTarget = None;
        TestPlayer.bBehindView = false;
        TestPlayer.DesiredFOV = TestPlayer.DefaultFOV;
        TestPlayer.Health = 1000;
        SetTimer(1.0, true);
    }
}

function Timer()
{
    if (TestPlayer == None || TestPlayer.Weapon == None)
        return;
    Phase++;
    TestPlayer.ViewTarget = None;
    TestPlayer.bBehindView = false;
    if (TestPlayer.Weapon.AmmoType != None)
        TestPlayer.Weapon.AmmoType.AmmoAmount = 999;
    if (Phase >= 2 && Phase <= 5)
        Log("FIRSTPERSONPROOF idle player=" $ TestPlayer.Class $ " weapon=" $ TestPlayer.Weapon.Class);
    if (Phase == 7)
    {
        TestPlayer.bFire = 1;
        TestPlayer.Weapon.Fire(0);
    }
    if (Phase >= 7 && Phase <= 11)
        Log("FIRSTPERSONPROOF primary player=" $ TestPlayer.Class $ " weapon=" $ TestPlayer.Weapon.Class);
    if (Phase == 12)
        TestPlayer.bFire = 0;
    if (Phase == 14)
    {
        TestPlayer.bAltFire = 1;
        TestPlayer.Weapon.AltFire(0);
    }
    if (Phase >= 14 && Phase <= 18)
        Log("FIRSTPERSONPROOF alternate player=" $ TestPlayer.Class $ " weapon=" $ TestPlayer.Weapon.Class);
    if (Phase == 19)
    {
        TestPlayer.bAltFire = 0;
        TestPlayer.DesiredFOV = TestPlayer.DefaultFOV;
    }
    if (Phase >= 21 && Phase <= 23)
        Log("FIRSTPERSONPROOF loading-pending");
    if (Phase == 24)
    {
        Log("FIRSTPERSONPROOF loading-begin");
        TestPlayer.ClientTravel("CityIntro.unr?Game=Engine.GameInfo?Class=Engine.Spectator", TRAVEL_Absolute, false);
        SetTimer(0, false);
    }
}
