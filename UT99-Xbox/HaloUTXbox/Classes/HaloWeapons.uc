class HaloWeapons extends Mutator;

function ModifyPlayer(Pawn Other)
{
    local Weapon EnforcerWeapon, Pistol;
    Pistol = Weapon(Other.FindInventoryType(class'HaloPistol'));
    EnforcerWeapon = Weapon(Other.FindInventoryType(class'Botpack.Enforcer'));
    // DeathMatchPlus explicitly gives an Enforcer before GameInfo gives the
    // mutator's default weapon. Inventory spawned at the origin is deliberately
    // excluded by ReplaceWith, so finish that replacement after inventory setup.
    if (Pistol != None && EnforcerWeapon != None)
    {
        if (Other.Weapon == EnforcerWeapon)
            Other.Weapon = None;
        if (Other.PendingWeapon == EnforcerWeapon)
            Other.PendingWeapon = None;
        Other.DeleteInventory(EnforcerWeapon);
        EnforcerWeapon.Destroy();
        if (Other.Weapon == None)
            Pistol.WeaponSet(Other);
    }
    Super.ModifyPlayer(Other);
}

function bool CheckReplacement(Actor Other, out byte bSuperRelevant)
{
    local string Replacement;
    // Test the port's classes first: their stock ammo parents must not cause
    // recursive replacement while ReplaceWith spawns an ammo pickup.
    if (Other.IsA('HaloWeapon') || Other.IsA('HaloAssaultAmmo')
        || Other.IsA('HaloPistolAmmo') || Other.IsA('HaloPlasmaAmmo'))
        return true;
    if (Other.IsA('Enforcer') || Other.IsA('SniperRifle'))
        Replacement = "HaloUTXbox.HaloPistol";
    else if (Other.IsA('minigun2') || Other.IsA('ripper') || Other.IsA('UT_FlakCannon'))
        Replacement = "HaloUTXbox.HaloAssaultRifle";
    else if (Other.IsA('PulseGun') || Other.IsA('ShockRifle') || Other.IsA('UT_BioRifle') || Other.IsA('UT_Eightball'))
        Replacement = "HaloUTXbox.HaloPlasmaRifle";
    else if (Other.IsA('Miniammo') || Other.IsA('BladeHopper') || Other.IsA('FlakAmmo'))
        Replacement = "HaloUTXbox.HaloAssaultAmmo";
    else if (Other.IsA('BulletBox') || Other.IsA('RifleShell'))
        Replacement = "HaloUTXbox.HaloPistolAmmo";
    else if (Other.IsA('PAmmo') || Other.IsA('ShockCore') || Other.IsA('BioAmmo') || Other.IsA('RocketPack'))
        Replacement = "HaloUTXbox.HaloPlasmaAmmo";
    if (Replacement != "")
        return !ReplaceWith(Other, Replacement);
    return true;
}

defaultproperties
{
    DefaultWeapon=Class'HaloUTXbox.HaloPistol'
}
