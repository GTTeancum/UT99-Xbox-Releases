class EliteBot extends MaleBotPlus;

// HumanBotPlus assumes one-frame jump/landing/dodge poses. These imported
// skeletal sequences contain motion and must play, rather than only tween.
function PlayInAir()
{
    BaseEyeHeight = 0.7 * Default.BaseEyeHeight;
    if (GetAnimGroup(AnimSequence) != 'Jumping')
        PlayAnim('JumpLGFR', 1.0, 0.08);
}

function FastInAir()
{
    PlayInAir();
}

function PlayDodge(bool bDuckLeft)
{
    if (bDuckLeft)
        PlayAnim('DodgeL', 1.0, 0.06);
    else
        PlayAnim('DodgeR', 1.0, 0.06);
}

function PlayLanded(float ImpactVel)
{
    Super.PlayLanded(ImpactVel);
    if (AnimSequence == 'LandLGFR' || AnimSequence == 'LandSMFR')
        PlayAnim(AnimSequence, 1.0, 0.04);
}

static function SetMultiSkin(Actor SkinActor, string SkinName, string FaceName, byte TeamNum)
{
    class'Elite'.static.SetMultiSkin(SkinActor, SkinName, FaceName, TeamNum);
}

static function GetMultiSkin(Actor SkinActor, out string SkinName, out string FaceName)
{
    class'Elite'.static.GetMultiSkin(SkinActor, SkinName, FaceName);
}

defaultproperties
{
    Mesh=SkeletalMesh'HaloUTXbox.EliteMesh'
    SelectionMesh="HaloUTXbox.EliteMesh"
    MenuName="HaloUT Elite"
    DefaultSkinName="HaloUTXbox.Skins.EliteSpecOps"
    DefaultPackage="HaloUTXbox.Skins."
    MultiSkins(0)=Texture'HaloUTXbox.Skins.EliteSpecOps'
    MultiSkins(1)=Texture'HaloUTXbox.Skins.EliteSpecOps'
    CarcassType=Class'HaloUTXbox.EliteCarcass'
    VoiceType="BotPack.VoiceMaleOne"
}
