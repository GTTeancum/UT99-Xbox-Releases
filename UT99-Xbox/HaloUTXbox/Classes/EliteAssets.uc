// Project Torlan HaloUT Elite, converted from the ModDB v1.751 release.
// Models/textures: EvilEngine and the Project Torlan team. See CONTENT_CREDITS.
class EliteAssets extends Object;

#exec MESH MODELIMPORT MESH=EliteMesh MODELFILE=Models\Elite.psk LODSTYLE=12
#exec ANIM IMPORT ANIM=EliteAnims ANIMFILE=Models\Elite.psa IMPORTSEQS=1 COMPRESS=1
#exec ANIM DIGEST ANIM=EliteAnims
#exec MESH DEFAULTANIM MESH=EliteMesh ANIM=EliteAnims
// Authored forward is -Y; the shared skeletal Y reflection makes it +Y.
// Rotate that to the pawn's +X forward without changing the shared evaluator.
#exec MESH ORIGIN MESH=EliteMesh X=0 Y=0 Z=124 YAW=-64
#exec MESHMAP SCALE MESHMAP=EliteMesh X=0.30 Y=0.30 Z=0.30
// The authored socket's -Y axis points along the hand's weapon direction.
// UT99's classic weapon attachment expects local +X to be the barrel axis.
#exec MESH WEAPONATTACH MESH=EliteMesh BONE="Bone_weapon"
// Bridge the exported socket to the classic weapon frame after the skeletal
// Y reflection. Positive quarter-roll leaves the grip below the barrel;
// negative quarter-roll inverted it on Elite (stock Soldier was unaffected).
#exec MESH WEAPONPOSITION MESH=EliteMesh YAW=-64 PITCH=0 ROLL=64 X=0 Y=0 Z=0
#exec TEXTURE IMPORT NAME=EliteBlue FILE=Textures\EliteBlue.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=EliteRed FILE=Textures\EliteRed.pcx GROUP=Skins
#exec TEXTURE IMPORT NAME=EliteSpecOps FILE=Textures\EliteSpecOps.pcx GROUP=Skins
#exec MESHMAP SETTEXTURE MESHMAP=EliteMesh NUM=0 TEXTURE=EliteSpecOps
#exec MESHMAP SETTEXTURE MESHMAP=EliteMesh NUM=1 TEXTURE=EliteSpecOps
