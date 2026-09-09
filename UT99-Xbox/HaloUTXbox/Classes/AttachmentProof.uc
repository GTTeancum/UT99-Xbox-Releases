// Explicit test mutator: give the selected weapon to every observed pawn.
// Activated only by the attachment proof harness, never by normal HaloWeapons.
class AttachmentProof extends Mutator config(AttachmentProof);

var config string TestWeaponName;
var class<Weapon> TestWeapon;

function PreBeginPlay()
{
    Super.PreBeginPlay();
    TestWeapon = class<Weapon>(DynamicLoadObject(TestWeaponName, class'Class'));
    Log("ATTACHMENTPROOF requested=" $ TestWeaponName $ " resolved=" $ TestWeapon);
}

function ModifyPlayer(Pawn Other)
{
    local Inventory Item, NextItem;
    local Weapon Selected;

    Super.ModifyPlayer(Other);
    if (TestWeapon == None)
        return;
    Selected = Weapon(Other.FindInventoryType(TestWeapon));
    if (Selected == None)
    {
        Selected = Spawn(TestWeapon);
        if (Selected == None)
            return;
        Selected.GiveTo(Other);
        Selected.GiveAmmo(Other);
        Selected.SetSwitchPriority(Other);
    }
    Item = Other.Inventory;
    while (Item != None)
    {
        NextItem = Item.Inventory;
        if (Item.IsA('Weapon') && Item != Selected)
        {
            Other.DeleteInventory(Item);
            Item.Destroy();
        }
        Item = NextItem;
    }
    Other.Weapon = Selected;
    Other.PendingWeapon = None;
    Selected.BringUp();
    Log("ATTACHMENTPROOF pawn=" $ Other $ " weapon=" $ Selected.Class);
}

function bool CheckReplacement(Actor Other, out byte bSuperRelevant)
{
    if (Other.IsA('Weapon'))
        return Other.Class == TestWeapon;
    return true;
}

defaultproperties
{
    TestWeaponName="Botpack.Enforcer"
}
