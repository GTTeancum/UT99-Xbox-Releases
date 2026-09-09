class Elite extends TournamentMale;

function PlayInAir()
{
    BaseEyeHeight = 0.7 * Default.BaseEyeHeight;
    if (GetAnimGroup(AnimSequence) != 'Jumping')
        PlayAnim('JumpLGFR', 1.0, 0.08);
}

function PlayLanded(float ImpactVel)
{
    Super.PlayLanded(ImpactVel);
    if (AnimSequence == 'LandLGFR' || AnimSequence == 'LandSMFR')
        PlayAnim(AnimSequence, 1.0, 0.04);
}

static function SetMultiSkin(Actor SkinActor, string SkinName, string FaceName, byte TeamNum)
{
    local Texture Armor;
    Armor = Texture'HaloUTXbox.Skins.EliteSpecOps';
    if (TeamNum == 0)
        Armor = Texture'HaloUTXbox.Skins.EliteRed';
    else if (TeamNum == 1)
        Armor = Texture'HaloUTXbox.Skins.EliteBlue';
    SkinActor.MultiSkins[0] = Armor;
    SkinActor.MultiSkins[1] = Armor;
}

static function GetMultiSkin(Actor SkinActor, out string SkinName, out string FaceName)
{
    SkinName = "HaloUTXbox.Skins.EliteSpecOps";
    FaceName = "";
}

defaultproperties
{
    Mesh=SkeletalMesh'HaloUTXbox.EliteMesh'
    SelectionMesh="HaloUTXbox.EliteMesh"
    SpecialMesh="HaloUTXbox.EliteMesh"
    MenuName="HaloUT Elite"
    DefaultSkinName="HaloUTXbox.Skins.EliteSpecOps"
    DefaultPackage="HaloUTXbox.Skins."
    MultiSkins(0)=Texture'HaloUTXbox.Skins.EliteSpecOps'
    MultiSkins(1)=Texture'HaloUTXbox.Skins.EliteSpecOps'
    CarcassType=Class'HaloUTXbox.EliteCarcass'
    VoiceType="BotPack.VoiceMaleOne"
}
