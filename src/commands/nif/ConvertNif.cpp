#define NOMINMAX

//1 SHU = 182.88 cm

#include <Skyrim/TES5File.h>
#include <Oblivion/TES4File.h>
#include <Collection.h>
#include <ModFile.h>

#include "stdafx.h"
#include <core/hkxcmd.h>
#include <core/hkfutils.h>
#include <core/log.h>

#include <core/HKXWrangler.h>
#include <commands/ConvertNif.h>
#include <commands/Geometry.h>
#include <core/games.h>
#include <core/bsa.h>
#include <core/NifFile.h>
#include <commands/NifScan.h>
#include <commands/Skeleton.h>
#include <commands/ImportKF.h>

#include <spt/SPT.h>

#include <Physics\Dynamics\Constraint\Bilateral\Ragdoll\hkpRagdollConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\BallAndSocket\hkpBallAndSocketConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\Hinge\hkpHingeConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\LimitedHinge\hkpLimitedHingeConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\Prismatic\hkpPrismaticConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\StiffSpring\hkpStiffSpringConstraintData.h>
#include <Physics\Dynamics\Constraint\Malleable\hkpMalleableConstraintData.h>

#include <Physics\Collide\Util\hkpTriangleUtil.h>

// Animation
#include <Animation/Animation/hkaAnimationContainer.h>
#include <Animation/Animation/Playback/Control/Default/hkaDefaultAnimationControl.h>
#include <Animation/Animation/Playback/hkaAnimatedSkeleton.h>
#include <Animation/Animation/Rig/hkaPose.h>
#include <Animation/Ragdoll/Controller/PoweredConstraint/hkaRagdollPoweredConstraintController.h>
#include <Animation/Ragdoll/Controller/RigidBody/hkaRagdollRigidBodyController.h>
#include <Animation/Ragdoll/Utils/hkaRagdollUtils.h>
#include <Animation/Animation/Animation/Deprecated/DeltaCompressed/hkaDeltaCompressedAnimation.h>
#include <Animation/Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>
#include <Animation/Animation/Animation/Quantized/hkaQuantizedAnimation.h>
#include <Animation/Animation/Animation/Util/hkaAdditiveAnimationUtility.h>
#include <Animation/Animation/Mapper/hkaSkeletonMapperUtils.h>

#include <hkbProjectStringData_1.h>
#include <hkbProjectData_2.h>
#include <hkbCharacterData_7.h>
#include <hkbVariableValueSet_0.h>
#include <hkbCharacterStringData_5.h>
#include <hkbMirroredSkeletonInfo_0.h>
#include <hkbCharacterDataCharacterControllerInfo_0.h>
#include <Common\Serialize\ResourceDatabase\hkResourceHandle.h>
#include <Animation\Animation\hkaAnimationContainer.h>
#include <hkbBehaviorGraph_1.h>
#include <hkbStateMachine_4.h>
#include <hkbBlendingTransitionEffect_1.h>
#include <BGSGamebryoSequenceGenerator_2.h>
#include <hkbClipGenerator_2.h>
#include <hkbProjectData_2.h>
#include <hkbCharacterData_7.h>
#include <hkbClipGenerator_2.h>
#include <hkbBehaviorReferenceGenerator_0.h>
#include <Common/Base/Container/String/hkStringBuf.h>
#include <hkbModifierGenerator_0.h>
#include <hkbModifierList_0.h>
#include <hkbPoweredRagdollControlsModifier_5.h>
#include <hkbRigidBodyRagdollControlsModifier_3.h>
#include <hkbEventDrivenModifier_0.h>
#include <hkbTimerModifier_1.h>
#include <hkbManualSelectorGenerator_0.h>
#include <hkbPoseMatchingGenerator_2.h>
#include <hkbGetUpModifier_2.h>
#include <hkbKeyframeBonesModifier_3.h>
#include <BSIsActiveModifier_1.h>
#include <BSSpeedSamplerModifier_1.h>
#include <hkbFootIkModifier_3.h>

#include <limits>
#include <array>
#include <unordered_map>
#include <regex>

#ifdef HAVE_SPEEDTREE
	#include <Core/Core.h>
#endif

#include <DirectXTex.h>


static inline hkTransform TOHKTRANSFORM(const Niflib::Matrix33& r, const Niflib::Vector4 t, const float scale = 1.0) {
	hkTransform out;
	out(0, 0) = r[0][0]; out(0, 1) = r[0][1]; out(0, 2) = r[0][2]; out(0, 3) = t[0] * scale;
	out(1, 0) = r[1][0]; out(1, 1) = r[1][1]; out(1, 2) = r[1][2]; out(1, 3) = t[1] * scale;
	out(2, 0) = r[2][0]; out(2, 1) = r[2][1]; out(2, 2) = r[2][2]; out(2, 3) = t[2] * scale;
	out(3, 0) = 0.0f;	 out(3, 1) = 0.0f;	  out(3, 2) = 0.0f;	   out(3, 3) = 1.0f;
	return out;
}

static bool BeginConversion(string importPath, string exportPath);
static void InitializeHavok();
static void CloseHavok();


static Games& games = Games::Instance();

ConvertNif::ConvertNif()
{
}

ConvertNif::~ConvertNif()
{
}

string ConvertNif::GetName() const
{
	return "ConvertNif";
}

string ConvertNif::GetHelp() const
{
	string name = GetName();
	transform(name.begin(), name.end(), name.begin(), ::tolower);

	// Usage: ck-cmd convertnif [-i <path_to_import>] [-e <path_to_export>]
	string usage = "Usage: " + ExeCommandList::GetExeName() + " " + name + " [<path_to_export>] [<path_to_import>] \r\n";

	//will need to check this help in console/
	const char help[] =
		R"(Convert Oblivion version models to Skyrim's format.
		
		Arguments:
			<path_to_export> path to exported models;
			<path_to_import> path to models which you want to convert

		If none of these are present, then the program will look through your Oblivion BSAs. (if present))";

	return usage + help;
}

string ConvertNif::GetHelpShort() const
{
	//I'm unsure about returning a string.. It doesn't show up on the console..
	//Log::Info("Convert Oblivion version models to Skyrim's format.");
	return "TODO: Short help message for ConvertNif";
}

bool ConvertNif::InternalRunCommand(map<string, docopt::value> parsedArgs)
{
	//We can improve this later, but for now this i'd say this is a good setup.
	string importPath="", exportPath="";

	if (parsedArgs["<path_to_import>"].isString())
		importPath = parsedArgs["<path_to_import>"].asString();
	if (parsedArgs["<path_to_export>"].isString())
		exportPath = parsedArgs["<path_to_export>"].asString();

	InitializeHavok();
	BeginConversion(importPath, exportPath);
	CloseHavok();
	return true;
}

using namespace ckcmd::info;
using namespace ckcmd::BSA;
using namespace ckcmd::Geometry;
using namespace ckcmd::NIF;
using namespace ckcmd::HKX;
using namespace ckcmd::nifscan;

static inline Niflib::Vector3 TOVECTOR3(const hkVector4& v) {
	return Niflib::Vector3(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2));
}

static inline Niflib::Vector4 TOVECTOR4(const hkVector4& v) {
	return Niflib::Vector4(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
}

static inline hkVector4 TOVECTOR4(const Niflib::Vector4& v) {
	return hkVector4(v.x, v.y, v.z, v.w);
}

static inline hkVector4 TOVECTOR3(const Niflib::Float3& v) {
	return hkVector4(v[0], v[1], v[2]);
}

static inline Niflib::Quaternion TOQUAT(const ::hkQuaternion& q, bool inverse = false) {
	Niflib::Quaternion qt(q.m_vec.getSimdAt(3), q.m_vec.getSimdAt(0), q.m_vec.getSimdAt(1), q.m_vec.getSimdAt(2));
	return inverse ? qt.Inverse() : qt;
}

static inline ::hkQuaternion TOQUAT(const Niflib::Quaternion& q, bool inverse = false) {
	hkVector4 v(q.x, q.y, q.z, q.w);
	v.normalize4();
	::hkQuaternion qt(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
	if (inverse) qt.setInverse(qt);
	return qt;
}

static inline ::hkQuaternion TOQUAT(const Niflib::hkQuaternion& q, bool inverse = false) {
	hkVector4 v(q.x, q.y, q.z, q.w);
	v.normalize4();
	::hkQuaternion qt(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
	if (inverse) qt.setInverse(qt);
	return qt;
}

static inline hkMatrix3 TOMATRIX3(const Niflib::InertiaMatrix& q, bool inverse = false) {
	hkMatrix3 m3;
	m3.setCols(TOVECTOR4(q.rows[0]), TOVECTOR4(q.rows[1]), TOVECTOR4(q.rows[2]));
	if (inverse) m3.invert(0.001);
	return m3;
}

static inline hkRotation TOMATRIX3(const Niflib::Matrix33& q, bool inverse = false) {
	hkRotation m3;
	m3.setCols(TOVECTOR3(q.rows[0]), TOVECTOR3(q.rows[1]), TOVECTOR3(q.rows[2]));
	if (inverse) m3.invert(0.001);
	return m3;
}

static inline Vector4 HKMATRIXROW(const hkTransform& q, const unsigned int row) {
	return Vector4(q(row, 0), q(row, 1), q(row, 2), q(row, 3));
}

static inline Matrix44 TOMATRIX44(const hkTransform& q, const float scale = 1.0f, bool inverse = false) {

	hkVector4 c0 = q.getColumn(0);
	hkVector4 c1 = q.getColumn(1);
	hkVector4 c2 = q.getColumn(2);
	hkVector4 c3 = q.getColumn(3);

	return Matrix44(
		c0.getSimdAt(0), c1.getSimdAt(0), c2.getSimdAt(0), (float)c3.getSimdAt(0) * scale,
		c0.getSimdAt(1), c1.getSimdAt(1), c2.getSimdAt(1), (float)c3.getSimdAt(1) * scale,
		c0.getSimdAt(2), c1.getSimdAt(2), c2.getSimdAt(2), (float)c3.getSimdAt(2) * scale,
		c0.getSimdAt(3), c1.getSimdAt(3), c2.getSimdAt(3), c3.getSimdAt(3)
	);
}

static inline hkTransform TOMATRIX4(const Matrix44& q) {

	hkTransform m;
	m(0, 0) = q[0][0]; m(0, 1) = q[0][1]; m(0, 2) = q[0][2]; m(0, 3) = q[0][3];
	m(1, 0) = q[1][0]; m(1, 1) = q[1][1]; m(1, 2) = q[1][2]; m(1, 3) = q[1][3];
	m(2, 0) = q[2][0]; m(2, 1) = q[2][1]; m(2, 2) = q[2][2]; m(2, 3) = q[2][3];
	m(3, 0) = q[3][0]; m(3, 1) = q[3][1]; m(3, 2) = q[3][2]; m(3, 3) = q[3][3];

	return m;
}

SkyrimHavokMaterial convert_havok_material(OblivionHavokMaterial material) {
	switch (material) {
	case OB_HAV_MAT_STONE: /*!< Stone */
		return SKY_HAV_MAT_STONE; // = 3741512247, /*!< Stone */
	case OB_HAV_MAT_CLOTH: /*!< Cloth */
		return SKY_HAV_MAT_CLOTH; // = 3839073443, /*!< Cloth */
	case OB_HAV_MAT_DIRT: /*!< Dirt */
		return SKY_HAV_MAT_DIRT; // = 3106094762, /*!< Dirt */
	case OB_HAV_MAT_GLASS: /*!< Glass */
		return SKY_HAV_MAT_GLASS; // = 3739830338, /*!< Glass */
	case OB_HAV_MAT_GRASS: /*!< Grass */
		return SKY_HAV_MAT_GRASS; //= 1848600814, /*!< Grass */
	case OB_HAV_MAT_METAL: /*!< Metal */
		return SKY_HAV_MAT_SOLID_METAL; // = 1288358971, /*!< Solid Metal */
	case OB_HAV_MAT_ORGANIC: /*!< Organic */
		return SKY_HAV_MAT_ORGANIC; // = 2974920155, /*!< Organic */
	case OB_HAV_MAT_SKIN: /*!< Skin */
		return SKY_HAV_MAT_SKIN; // = 591247106, /*!< Skin */
	case OB_HAV_MAT_WATER: /*!< Water */
		return SKY_HAV_MAT_WATER; // = 1024582599, /*!< Water */
	case OB_HAV_MAT_WOOD: /*!< Wood */
		return SKY_HAV_MAT_WOOD; // = 500811281, /*!< Wood */
	case OB_HAV_MAT_HEAVY_STONE: /*!< Heavy Stone */
		return SKY_HAV_MAT_HEAVY_STONE; // = 1570821952, /*!< Heavy Stone */
	case OB_HAV_MAT_HEAVY_METAL: /*!< Heavy Metal */
		return SKY_HAV_MAT_HEAVY_METAL; // = 2229413539, /*!< Heavy Metal */
	case OB_HAV_MAT_HEAVY_WOOD: /*!< Heavy Wood */
		return SKY_HAV_MAT_HEAVY_WOOD; // = 3070783559, /*!< Heavy Wood */
	case OB_HAV_MAT_CHAIN: /*!< Chain */
		return SKY_HAV_MAT_MATERIAL_CHAIN; // = 3074114406, /*!< Material Chain */ TODO: maybe SKY_HAV_MAT_MATERIAL_CHAIN_METAL?
	case OB_HAV_MAT_SNOW: /*!< Snow */
		return SKY_HAV_MAT_SNOW; // = 398949039, /*!< Snow */
								 //TODO: We do not have so much stairs		
	case OB_HAV_MAT_STONE_STAIRS: /*!< Stone Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 899511101, /*!< Stairs Stone */
	case OB_HAV_MAT_CLOTH_STAIRS: /*!< Cloth Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD;
	case OB_HAV_MAT_DIRT_STAIRS: /*!< Dirt Stairs */
		return SKY_HAV_MAT_STAIRS_BROKEN_STONE;
	case OB_HAV_MAT_GLASS_STAIRS: /*!< Glass Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 3739830338, /*!< Glass */
	case OB_HAV_MAT_GRASS_STAIRS: /*!< Grass Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD; //= 1848600814, /*!< Grass */
	case OB_HAV_MAT_METAL_STAIRS: /*!< Metal Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 1288358971, /*!< Solid Metal */
	case OB_HAV_MAT_ORGANIC_STAIRS: /*!< Organic Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD; // = 2974920155, /*!< Organic */
	case OB_HAV_MAT_SKIN_STAIRS: /*!< Skin Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD; // = 591247106, /*!< Skin */
	case OB_HAV_MAT_WATER_STAIRS: /*!< Water Stairs */
		return SKY_HAV_MAT_STAIRS_SNOW; // = 1024582599, /*!< Water */
	case OB_HAV_MAT_WOOD_STAIRS: /*!< Wood Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD; // = 1461712277, /*!< Stairs Wood */
	case OB_HAV_MAT_HEAVY_STONE_STAIRS: /*!< Heavy Stone Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 1570821952, /*!< Heavy Stone */
	case OB_HAV_MAT_HEAVY_METAL_STAIRS: /*!< Heavy Metal Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 2229413539, /*!< Heavy Metal */
	case OB_HAV_MAT_HEAVY_WOOD_STAIRS: /*!< Heavy Wood Stairs */
		return SKY_HAV_MAT_STAIRS_WOOD; // = 3070783559, /*!< Heavy Wood */
	case OB_HAV_MAT_CHAIN_STAIRS: /*!< Chain Stairs */
		return SKY_HAV_MAT_STAIRS_STONE; // = 3074114406, /*!< Material Chain */ TODO: maybe SKY_HAV_MAT_MATERIAL_CHAIN_METAL?
	case OB_HAV_MAT_SNOW_STAIRS: /*!< Snow Stairs */
		return SKY_HAV_MAT_STAIRS_SNOW; // = 1560365355, /*!< Stairs Snow */
	case OB_HAV_MAT_ELEVATOR: /*!< Elevator */
		return SKY_HAV_MAT_STONE; // = 3741512247, /*!< Stone */ TODO: I really don't know'
	case OB_HAV_MAT_RUBBER: /*!< Rubber */
		return SKY_HAV_MAT_ORGANIC; // = 2974920155, /*!< Organic */ TODO: I really don't know'
	}
	//DEFAULT
	return SKY_HAV_MAT_STONE; // = 3741512247, /*!< Stone */
}

SkyrimHavokMaterial convert_material_to_stairs(SkyrimHavokMaterial to_convert) {
	switch (to_convert) {
	case SKY_HAV_MAT_MATERIAL_BOULDER_LARGE: // = 1885326971, /*!< Material Boulder Large */
	case SKY_HAV_MAT_MATERIAL_STONE_AS_STAIRS: // = 1886078335, /*!< Material Stone As Stairs */
	case SKY_HAV_MAT_MATERIAL_BLADE_2HAND: // = 2022742644, /*!< Material Blade 2Hand */
	case SKY_HAV_MAT_MATERIAL_BOTTLE_SMALL: // = 2025794648, /*!< Material Bottle Small */
	case SKY_HAV_MAT_SAND: // = 2168343821, /*!< Sand */
	case SKY_HAV_MAT_HEAVY_METAL: // = 2229413539, /*!< Heavy Metal */
	case SKY_HAV_MAT_UNKNOWN_2290050264: // = 2290050264, /*!< Unknown in Creation Kit v1.9.32.0. Found in Dawnguard DLC in meshes\dlc01\clutter\dlc01sabrecatpelt.nif. */
	case SKY_HAV_MAT_DRAGON: // = 2518321175, /*!< Dragon */
	case SKY_HAV_MAT_MATERIAL_BLADE_1HAND_SMALL: // = 2617944780, /*!< Material Blade 1Hand Small */
	case SKY_HAV_MAT_MATERIAL_SKIN_SMALL: // = 2632367422, /*!< Material Skin Small */
	case SKY_HAV_MAT_MATERIAL_SKIN_LARGE: // = 2965929619, /*!< Material Skin Large */
	case SKY_HAV_MAT_ORGANIC: // = 2974920155, /*!< Organic */
	case SKY_HAV_MAT_MATERIAL_BONE: // = 3049421844, /*!< Material Bone */
	case SKY_HAV_MAT_HEAVY_WOOD: // = 3070783559, /*!< Heavy Wood */
	case SKY_HAV_MAT_MATERIAL_CHAIN: // = 3074114406, /*!< Material Chain */
	case SKY_HAV_MAT_DIRT: // = 3106094762, /*!< Dirt */
	case SKY_HAV_MAT_MATERIAL_ARMOR_LIGHT: // = 3424720541, /*!< Material Armor Light */
	case SKY_HAV_MAT_MATERIAL_SHIELD_LIGHT: // = 3448167928, /*!< Material Shield Light */
	case SKY_HAV_MAT_MATERIAL_COIN: // = 3589100606, /*!< Material Coin */
	case SKY_HAV_MAT_MATERIAL_SHIELD_HEAVY: // = 3702389584, /*!< Material Shield Heavy */
	case SKY_HAV_MAT_MATERIAL_ARMOR_HEAVY: // = 3708432437, /*!< Material Armor Heavy */
	case SKY_HAV_MAT_MATERIAL_ARROW: // = 3725505938, /*!< Material Arrow */
	case SKY_HAV_MAT_GLASS: // = 3739830338, /*!< Glass */
	case SKY_HAV_MAT_STONE: // = 3741512247, /*!< Stone */
	case SKY_HAV_MAT_CLOTH: // = 3839073443, /*!< Cloth */
	case SKY_HAV_MAT_MATERIAL_BLUNT_2HAND: // = 3969592277, /*!< Material Blunt 2Hand */
	case SKY_HAV_MAT_UNKNOWN_4239621792: // = 4239621792, /*!< Unknown in Creation Kit v1.9.32.0. Found in Dawnguard DLC in meshes\dlc01\prototype\dlc1protoswingingbridge.nif. */
	case SKY_HAV_MAT_MATERIAL_BOULDER_MEDIUM: // = 4283869410, /*!< Material Boulder Medium */
	case SKY_HAV_MAT_HEAVY_STONE: // = 1570821952, /*!< Heavy Stone */
	case SKY_HAV_MAT_UNKNOWN_1574477864: // = 1574477864, /*!< Unknown in Creation Kit v1.6.89.0. Found in actors\dragon\character assets\skeleton.nif. */
	case SKY_HAV_MAT_UNKNOWN_1591009235: // = 1591009235, /*!< Unknown in Creation Kit v1.6.89.0. Found in trap objects or clutter\displaycases\displaycaselgangled01.nif or actors\deer\character assets\skeleton.nif. */
	case SKY_HAV_MAT_MATERIAL_BOWS_STAVES: // = 1607128641, /*!< Material Bows Staves */
	case SKY_HAV_MAT_MATERIAL_WOOD_AS_STAIRS: // = 1803571212, /*!< Material Wood As Stairs */
	case SKY_HAV_MAT_WATER: // = 1024582599, /*!< Water */
	case SKY_HAV_MAT_UNKNOWN_1028101969: // = 1028101969, /*!< Unknown in Creation Kit v1.6.89.0. Found in actors\draugr\character assets\skeletons.nif. */
	case SKY_HAV_MAT_MATERIAL_BLADE_1HAND: // = 1060167844, /*!< Material Blade 1 Hand */
	case SKY_HAV_MAT_MATERIAL_BOOK: // = 1264672850, /*!< Material Book */
	case SKY_HAV_MAT_MATERIAL_CARPET: // = 1286705471, /*!< Material Carpet */
	case SKY_HAV_MAT_SOLID_METAL: // = 1288358971, /*!< Solid Metal */
	case SKY_HAV_MAT_MATERIAL_AXE_1HAND: // = 1305674443, /*!< Material Axe 1Hand */
	case SKY_HAV_MAT_UNKNOWN_1440721808: // = 1440721808, /*!< Unknown in Creation Kit v1.6.89.0. Found in armor\draugr\draugrbootsfemale_go.nif or armor\amuletsandrings\amuletgnd.nif. */
	case SKY_HAV_MAT_GRAVEL: // = 428587608, /*!< Gravel */
	case SKY_HAV_MAT_MATERIAL_CHAIN_METAL: // = 438912228, /*!< Material Chain Metal */
	case SKY_HAV_MAT_BOTTLE: // = 493553910, /*!< Bottle */
		return SKY_HAV_MAT_STAIRS_STONE;
	case SKY_HAV_MAT_MUD: // = 1486385281, /*!< Mud */
	case SKY_HAV_MAT_MATERIAL_BOULDER_SMALL: // = 1550912982, /*!< Material Boulder Small */
	case SKY_HAV_MAT_WOOD: // = 500811281, /*!< Wood */
	case SKY_HAV_MAT_SKIN: // = 591247106, /*!< Skin */
	case SKY_HAV_MAT_UNKNOWN_617099282: // = 617099282, /*!< Unknown in Creation Kit v1.9.32.0. Found in Dawnguard DLC in meshes\dlc01\clutter\dlc01deerskin.nif. */
	case SKY_HAV_MAT_BARREL: // = 732141076, /*!< Barrel */
	case SKY_HAV_MAT_MATERIAL_CERAMIC_MEDIUM: // = 781661019, /*!< Material Ceramic Medium */
	case SKY_HAV_MAT_MATERIAL_BASKET: // = 790784366, /*!< Material Basket */
	case SKY_HAV_MAT_LIGHT_WOOD: // = 365420259, /*!< Light Wood */
		return SKY_HAV_MAT_STAIRS_WOOD;
	case SKY_HAV_MAT_BROKEN_STONE: // = 131151687, /*!< Broken Stone */
		return SKY_HAV_MAT_STAIRS_BROKEN_STONE;
	case SKY_HAV_MAT_ICE: // = 873356572, /*!< Ice */
	case SKY_HAV_MAT_SNOW: // = 398949039, /*!< Snow */
		return SKY_HAV_MAT_STAIRS_SNOW;
	}
	return SKY_HAV_MAT_STAIRS_STONE;
}

bool is_stairs_material(SkyrimHavokMaterial material) {
	return material == SKY_HAV_MAT_STAIRS_STONE ||
		material == SKY_HAV_MAT_STAIRS_WOOD ||
		material == SKY_HAV_MAT_STAIRS_SNOW ||
		material == SKY_HAV_MAT_STAIRS_BROKEN_STONE;
}

SkyrimLayer convert_havok_layer(OblivionLayer layer) {
	switch (layer) {
	case OL_UNIDENTIFIED:
		return SKYL_UNIDENTIFIED; /*!< Unidentified */
	case OL_STATIC: /*!< Static (red) */
		return SKYL_STATIC; /*!< Static */
	case OL_ANIM_STATIC: /*!< AnimStatic (magenta) */
		return SKYL_ANIMSTATIC; /*!< Anim Static */
	case OL_TRANSPARENT: /*!< Transparent (light pink) */
		return SKYL_TRANSPARENT; /*!< Transparent */
	case OL_CLUTTER: /*!< Clutter (light blue) */
		return SKYL_CLUTTER; /*!< Clutter. Object with this layer will float on water surface. */
	case OL_WEAPON: /*!< Weapon (orange) */
		return SKYL_WEAPON;  /*!< Weapon */
	case OL_PROJECTILE: /*!< Projectile (light orange) */
		return SKYL_PROJECTILE; /*!< Projectile */
	case OL_SPELL: /*!< Spell (cyan) */
		return SKYL_SPELL; /*!< Spell */
	case OL_BIPED: /*!< Biped (green) Seems to apply to all creatures/NPCs */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_TREES: /*!< Trees (light brown) */
		return SKYL_TREES; /*!< Trees */
	case OL_PROPS: /*!< Props (magenta) */
		return SKYL_PROPS; /*!< Props */
	case OL_WATER: /*!< Water (cyan) */
		return SKYL_WATER; /*!< Water */
	case OL_TRIGGER: /*!< Trigger (light grey) */
		return SKYL_TRIGGER; /*!< Trigger */
	case OL_TERRAIN: /*!< Terrain (light yellow) */
		return SKYL_TERRAIN; /*!< Terrain */
	case OL_TRAP: /*!< Trap (light grey) */
		return SKYL_TRAP; /*!< Trap */
	case OL_NONCOLLIDABLE: /*!< NonCollidable (white) */
		return SKYL_NONCOLLIDABLE; /*!< NonCollidable */
	case OL_CLOUD_TRAP: /*!< CloudTrap (greenish grey) */
		return SKYL_CLOUD_TRAP; /*!< CloudTrap */
	case OL_GROUND: /*!< Ground (none) */
		return SKYL_GROUND; /*!< Ground. It seems that produces no sound when collide. */
	case OL_PORTAL: /*!< Portal (green) */
		return SKYL_PORTAL; /*!< Portal */
	case OL_STAIRS: /*!< Stairs (white) */
		return SKYL_STAIRHELPER; /*!< = 31,  Stair Helper */
	case OL_CHAR_CONTROLLER: /*!< CharController (yellow) */
		return SKYL_CHARCONTROLLER; /*!<= 30, /*!< Char Controller */
	case OL_AVOID_BOX: /*!< AvoidBox (dark yellow) */
		return SKYL_AVOIDBOX; /*!< = 34,  Avoid Box */
	case OL_UNKNOWN1: /*!< ? (white) */
		return SKYL_DEBRIS_SMALL; /*!<= 19,  Debris Small */
	case OL_UNKNOWN2: /*!< ? (white) */
		return SKYL_DEBRIS_LARGE; /*!< = 20,  Debris Small */
	case OL_CAMERA_PICK: /*!< CameraPick (white) */
		return SKYL_CAMERAPICK;  /*!<= 39, Camera Pick */
	case OL_ITEM_PICK: /*!< ItemPick (white) */
		return SKYL_ITEMPICK;  /*!<= 40,  Item Pick */
	case OL_LINE_OF_SIGHT: /*!< LineOfSight (white) */
		return SKYL_LINEOFSIGHT;/*!<= 41, < Line of Sight */
	case OL_PATH_PICK: /*!< PathPick (white) */
		return SKYL_PATHPICK; /*!< = 42, Path Pick */
	case OL_CUSTOM_PICK_1: /*!< CustomPick1 (white) */
		return SKYL_CUSTOMPICK1; /*!< = 43, /*!< Custom Pick 1 */
	case OL_CUSTOM_PICK_2: /*!< CustomPick2 (white) */
		return SKYL_CUSTOMPICK2; /*!<= 44, /*!< Custom Pick 2 */
	case OL_SPELL_EXPLOSION: /*!< SpellExplosion (white) */
		return SKYL_SPELLEXPLOSION; /*!< = 45, Spell Explosion */
	case OL_DROPPING_PICK: /*!< DroppingPick (white) */
		return SKYL_DEADBIP; /*!<  = 32, Dead Bip */
	case OL_OTHER: /*!< Other (white) */
		return SKYL_STATIC; /*!< Static */
	case OL_HEAD: /*!< Head */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_BODY: /*!< Body */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_SPINE1: /*!< Spine1 */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_SPINE2: /*!< Spine2 */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_UPPER_ARM: /*!< LUpperArm */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_FOREARM: /*!< LForeArm */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_HAND: /*!< LHand */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_THIGH: /*!< LThigh */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_CALF: /*!< LCalf */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_L_FOOT:/*!< LFoot */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_UPPER_ARM: /*!< RUpperArm */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_FOREARM: /*!< RForeArm */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_HAND: /*!< RHand */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_THIGH: /*!< RThigh */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_CALF: /*!< RCalf */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_R_FOOT:/*!< RFoot */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_TAIL: /*!< Tail */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_SIDE_WEAPON: /*!< SideWeapon */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_SHIELD: /*!< Shield */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_QUIVER: /*!< Quiver */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_BACK_WEAPON: /*!< BackWeapon */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_BACK_WEAPON2: /*!< BackWeapon (?) */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_PONYTAIL: /*!< PonyTail */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_WING: /*!< Wing */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	case OL_NULL: /*!< Null */
		return SKYL_BIPED; /*!< Biped. Seems to apply to all creatures/NPCs */
	}
	//DEFAULT
	return SKYL_STATIC;
}

#define COLLISION_RATIO 0.0999682116260354643752272923431f

#define MATERIAL_MASK 1984

#define STEP_SIZE 0.3
#define STEP_SIZE_TOLERANCE 0.15
#define ALMOST_DISTANCE 0.075
#define ALMOST_DISTANCE_TOUCH 0.1
#define PLANAR_MAX_ANGLE_COS 0.3
#define STEP_MAX_ANGLE_COS 0.1

class bhkRigidBodyUpgrader {};


class CMSPacker {};

template<>
class Accessor<CMSPacker> {
public:
	Accessor(hkpCompressedMeshShape* pCompMesh, bhkCompressedMeshShapeDataRef pData, const vector<SkyrimHavokMaterial>& materials)
	{
		short                                   chunkIdxNif(0);

		pData->SetBoundsMin(Vector4(pCompMesh->m_bounds.m_min(0), pCompMesh->m_bounds.m_min(1), pCompMesh->m_bounds.m_min(2), pCompMesh->m_bounds.m_min(3)));
		pData->SetBoundsMax(Vector4(pCompMesh->m_bounds.m_max(0), pCompMesh->m_bounds.m_max(1), pCompMesh->m_bounds.m_max(2), pCompMesh->m_bounds.m_max(3)));

		pData->SetBitsPerIndex(pCompMesh->m_bitsPerIndex);
		pData->SetBitsPerWIndex(pCompMesh->m_bitsPerWIndex);
		pData->SetMaskIndex(pCompMesh->m_indexMask);
		pData->SetMaskWIndex(pCompMesh->m_wIndexMask);
		pData->SetWeldingType(0); //seems to be fixed for skyrim pData->SetWeldingType(pCompMesh->m_weldingType);
		pData->SetMaterialType(1); //seems to be fixed for skyrim pData->SetMaterialType(pCompMesh->m_materialType);
		pData->SetError(pCompMesh->m_error);

		//  resize and copy bigVerts
		vector<Vector4 > tVec4Vec(pCompMesh->m_bigVertices.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_bigVertices.getSize(); ++idx)
		{
			tVec4Vec[idx].x = pCompMesh->m_bigVertices[idx](0);
			tVec4Vec[idx].y = pCompMesh->m_bigVertices[idx](1);
			tVec4Vec[idx].z = pCompMesh->m_bigVertices[idx](2);
			tVec4Vec[idx].w = pCompMesh->m_bigVertices[idx](3);
		}
		pData->SetBigVerts(tVec4Vec);

		//  resize and copy bigTris
		vector<bhkCMSDBigTris > tBTriVec(pCompMesh->m_bigTriangles.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_bigTriangles.getSize(); ++idx)
		{
			tBTriVec[idx].triangle1 = pCompMesh->m_bigTriangles[idx].m_a;
			tBTriVec[idx].triangle2 = pCompMesh->m_bigTriangles[idx].m_b;
			tBTriVec[idx].triangle3 = pCompMesh->m_bigTriangles[idx].m_c;
			tBTriVec[idx].material = pCompMesh->m_bigTriangles[idx].m_material;
			tBTriVec[idx].weldingInfo = pCompMesh->m_bigTriangles[idx].m_weldingInfo;
		}
		pData->SetBigTris(tBTriVec);

		//  resize and copy transform data
		vector<bhkCMSDTransform > tTranVec(pCompMesh->m_transforms.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_transforms.getSize(); ++idx)
		{
			tTranVec[idx].translation.x = pCompMesh->m_transforms[idx].m_translation(0);
			tTranVec[idx].translation.y = pCompMesh->m_transforms[idx].m_translation(1);
			tTranVec[idx].translation.z = pCompMesh->m_transforms[idx].m_translation(2);
			tTranVec[idx].translation.w = pCompMesh->m_transforms[idx].m_translation(3);
			tTranVec[idx].rotation.x = pCompMesh->m_transforms[idx].m_rotation(0);
			tTranVec[idx].rotation.y = pCompMesh->m_transforms[idx].m_rotation(1);
			tTranVec[idx].rotation.z = pCompMesh->m_transforms[idx].m_rotation(2);
			tTranVec[idx].rotation.w = pCompMesh->m_transforms[idx].m_rotation(3);
		}
		pData->chunkTransforms = tTranVec;

		vector<bhkCMSDMaterial > tMtrlVec(pCompMesh->m_materials.getSize());

		for (unsigned int idx(0); idx < pCompMesh->m_materials.getSize(); ++idx)
		{
			bhkCMSDMaterial& material = tMtrlVec[idx];
			material.material = materials[pCompMesh->m_materials[idx]];
			//TODO: may be unnecessary due to the fact that MOPP shouldn't be used for anything else;
			material.filter.layer_sk = SKYL_STATIC;
		}

		//  set material list
		pData->chunkMaterials = tMtrlVec;

		vector<bhkCMSDChunk> chunkListNif(pCompMesh->m_chunks.getSize());

		//  for each chunk
		for (hkArray<hkpCompressedMeshShape::Chunk>::iterator pCIterHvk = pCompMesh->m_chunks.begin(); pCIterHvk != pCompMesh->m_chunks.end(); pCIterHvk++)
		{
			//  get nif chunk
			bhkCMSDChunk&	chunkNif = chunkListNif[chunkIdxNif];

			//  set offset => translation
			chunkNif.translation.x = pCIterHvk->m_offset(0);
			chunkNif.translation.y = pCIterHvk->m_offset(1);
			chunkNif.translation.z = pCIterHvk->m_offset(2);
			chunkNif.translation.w = pCIterHvk->m_offset(3);

			//  force flags to fixed values
			chunkNif.materialIndex = pCIterHvk->m_materialInfo;
			chunkNif.reference = 65535;
			chunkNif.transformIndex = pCIterHvk->m_transformIndex;

			//  vertices
			chunkNif.numVertices = pCIterHvk->m_vertices.getSize();
			chunkNif.vertices.resize(chunkNif.numVertices);
			for (unsigned int i(0); i < chunkNif.numVertices; ++i)
			{
				chunkNif.vertices[i] = pCIterHvk->m_vertices[i];
			}

			//  indices
			chunkNif.numIndices = pCIterHvk->m_indices.getSize();
			chunkNif.indices.resize(chunkNif.numIndices);
			for (unsigned int i(0); i < chunkNif.numIndices; ++i)
			{
				chunkNif.indices[i] = pCIterHvk->m_indices[i];
			}

			//  strips
			chunkNif.numStrips = pCIterHvk->m_stripLengths.getSize();
			chunkNif.strips.resize(chunkNif.numStrips);
			for (unsigned int i(0); i < chunkNif.numStrips; ++i)
			{
				chunkNif.strips[i] = pCIterHvk->m_stripLengths[i];
			}

			chunkNif.weldingInfo.resize(pCIterHvk->m_weldingInfo.getSize());
			for (int k = 0; k < pCIterHvk->m_weldingInfo.getSize(); k++) {
				chunkNif.weldingInfo[k] = pCIterHvk->m_weldingInfo[k];
			}

			++chunkIdxNif;

		}

		//  set modified chunk list to compressed mesh shape data
		pData->chunks = chunkListNif;
		//----  Merge  ----  END
	}
};

struct vertexInfo {
	hkVector4 vertex;
	unsigned int index;

	vertexInfo(hkVector4 v, unsigned int i) : vertex(v), index(i) {}

	vertexInfo & operator=(vertexInfo & source) {
		vertex = source.vertex;
		index = source.index;
		return *this;
	}

	vertexInfo & operator=(const vertexInfo & source) {
		vertex = source.vertex;
		index = source.index;
		return *this;
	}

};

bool is_equal(const hkGeometry::Triangle& t1, const hkGeometry::Triangle& other) {
	//return t1.m_a == t2.m_a &&
	//	t2.m_b == t2.m_b &&
	//	t2.m_c == t2.m_c;

	return
		(t1.m_a == other.m_a && t1.m_b == other.m_b && t1.m_c == other.m_c) ||
		(t1.m_a == other.m_a && t1.m_b == other.m_c && t1.m_c == other.m_b) ||
		(t1.m_a == other.m_b && t1.m_b == other.m_a && t1.m_c == other.m_c) ||
		(t1.m_a == other.m_b && t1.m_b == other.m_c && t1.m_c == other.m_a) ||
		(t1.m_a == other.m_c && t1.m_b == other.m_a && t1.m_c == other.m_b) ||
		(t1.m_a == other.m_c && t1.m_b == other.m_b && t1.m_c == other.m_a);
}

bool is_equal(const hkGeometry::Triangle& t1, const std::array<int, 3 >& other) {
	//return t1.m_a == t2.m_a &&
	//	t2.m_b == t2.m_b &&
	//	t2.m_c == t2.m_c;

	return
		(t1.m_a == other[0] && t1.m_b == other[1] && t1.m_c == other[2]) ||
		(t1.m_a == other[0] && t1.m_b == other[2] && t1.m_c == other[1]) ||
		(t1.m_a == other[1] && t1.m_b == other[0] && t1.m_c == other[2]) ||
		(t1.m_a == other[1] && t1.m_b == other[2] && t1.m_c == other[0]) ||
		(t1.m_a == other[2] && t1.m_b == other[0] && t1.m_c == other[1]) ||
		(t1.m_a == other[2] && t1.m_b == other[1] && t1.m_c == other[0]);
}

class CollisionShapeVisitor : public RecursiveFieldVisitor<CollisionShapeVisitor> {

	hkGeometry					geometry; //vetrices and triangles with materials indices
	vector<SkyrimHavokMaterial> materials;
	vector<SkyrimLayer>			layers; //one per triangle

	NiAVObject*					target = NULL;

	bool isGeometryDegenerate() {
		for (hkGeometry::Triangle t : geometry.m_triangles) {
			if (!hkpTriangleUtil::isDegenerate(
				geometry.m_vertices[t.m_a],
				geometry.m_vertices[t.m_b],
				geometry.m_vertices[t.m_c]
			)) {
				return false;
			}

		}
		return true;
	}

	int convert_to_stairs(int material) {
		int material_index = -1;
		SkyrimHavokMaterial stair_material = convert_material_to_stairs(materials[material]);
		auto material_it = find(materials.begin(), materials.end(), stair_material);
		if (material_it != materials.end())
			return distance(materials.begin(), material_it);
		material_index = materials.size();
		materials.push_back(stair_material);
		return material_index;
	}

	typedef hkGeometry::Triangle hkTriangle;

	class walker {

		struct side_comparator {
			bool operator()(const std::array<int, 2>& c1, const std::array<int, 2>& c2) const {
				return c1[0] == c2[0] && c1[1] == c2[1];
			}

			size_t operator()(const std::array<int, 2>& c1) const
			{	// hash _Keyval to size_t value
				return hash<size_t>()((uint64_t)c1[0] << 32 | c1[1]);
			}
		};

		typedef unordered_map< std::array<int, 2>, int, side_comparator> adjacent_map_t;

		hkGeometry& geometry;
		vector < std::array<int, 3 >> planes;
		vector < std::array<int, 3 >> normal_planes;
		adjacent_map_t adjacent_map;
		set < std::array<int, 2 >> visited;
		set<std::array<int, 3 >> current_stair;

		struct order_by_plane_up {

			const hkGeometry& geometry;

			order_by_plane_up(const hkGeometry& geometry) : geometry(geometry) {}

			bool operator() (const std::array<int, 3>& v1, const std::array<int, 3>& v2) const {
				return geometry.m_vertices[v1[2]](2) < geometry.m_vertices[v2[2]](2);
			}
		};

		float calculate_angle_with_z(const hkGeometry::Triangle& v1) {
			hkVector4 v1ab = geometry.m_vertices[v1.m_b];
			v1ab.sub4(geometry.m_vertices[v1.m_a]);
			hkVector4 v1ac = geometry.m_vertices[v1.m_c];
			v1ac.sub4(geometry.m_vertices[v1.m_a]);
			hkVector4 z(0.0, 0.0, 1.0);
			hkVector4 n; n.setCross(v1ab, v1ac); n.normalize3();
			return n.dot3(z);
		}

		void get_planar_z_triangles() {
			for (int t = 0; t < geometry.m_triangles.getSize(); t++) {
				float z_angle = calculate_angle_with_z(geometry.m_triangles[t]);
				if (abs(z_angle) > 1 - PLANAR_MAX_ANGLE_COS)
					planes.push_back({ geometry.m_triangles[t].m_a,geometry.m_triangles[t].m_b, geometry.m_triangles[t].m_c });
			}
			sort(planes.begin(), planes.end(), [this](const std::array<int, 3>& v1, const std::array<int, 3>& v2) -> bool {
				return this->geometry.m_vertices[v1[0]](2) < this->geometry.m_vertices[v2[0]](2);
			});
		}

		void get_normal_z_triangles() {
			for (int t = 0; t < geometry.m_triangles.getSize(); t++) {
				float z_angle = calculate_angle_with_z(geometry.m_triangles[t]);
				if (abs(z_angle) < PLANAR_MAX_ANGLE_COS)
					normal_planes.push_back({ geometry.m_triangles[t].m_a,geometry.m_triangles[t].m_b, geometry.m_triangles[t].m_c });
			}
			sort(normal_planes.begin(), normal_planes.end(), [this](const std::array<int, 3>& v1, const std::array<int, 3>& v2) -> bool {
				return this->geometry.m_vertices[v1[0]](2) < this->geometry.m_vertices[v2[0]](2) &&
					this->geometry.m_vertices[v1[1]](2) < this->geometry.m_vertices[v2[1]](2) &&
					this->geometry.m_vertices[v1[2]](2) < this->geometry.m_vertices[v2[2]](2);
			});
		}

		std::array<int, 2> find_max_side(std::array<int, 3> tris) {
			hkVector4 a = geometry.m_vertices[tris[0]];
			hkVector4 b = geometry.m_vertices[tris[1]];
			hkVector4 c = geometry.m_vertices[tris[2]];
			hkVector4 ab = b; ab.sub4(a); float norm_ab = ab.lengthSquared3();
			hkVector4 bc = c; ab.sub4(b); float norm_bc = bc.lengthSquared3();
			hkVector4 ca = a; ab.sub4(c); float norm_ca = ca.lengthSquared3();
			return  norm_ab > norm_bc ?
				norm_ab > norm_ca ?
				std::array<int, 2>({ tris[0], tris[1] }) :
				std::array<int, 2>({ tris[2], tris[0] }) :
				norm_bc > norm_ca ?
				std::array<int, 2>({ tris[1], tris[2] }) :
				std::array<int, 2>({ tris[2], tris[0] });
		}

		void step(const std::array<int, 2>& side, int c) {
			if (visited.insert(side).second) {
				adjacent_map_t::iterator opposite = adjacent_map.find({ side[1], side[0] });
				if (opposite != adjacent_map.end() && opposite->second != c) {
					hkVector4 opposite_c = geometry.m_vertices[opposite->second];
					hkVector4 a = geometry.m_vertices[side[0]];
					hkVector4 b = geometry.m_vertices[side[1]];
					float max_ab_z = a(2) > b(2) ? a(2) : b(2);
					float min_ab_z = a(2) < b(2) ? a(2) : b(2);
					float step_size_left = abs(opposite_c(2) - max_ab_z);
					float step_size_right = abs(opposite_c(2) - min_ab_z);
					hkVector4 ab; ab.setSub4(b, a);
					hkVector4 a_opposite_c; a_opposite_c.setSub4(opposite_c, a);
					hkVector4 norm; norm.setCross(ab, a_opposite_c); norm.normalize3();
					float angle = norm.dot3(hkVector4(0.0, 0.0, 1.0));
					if (angle < sqrt(2) / 2) {
						if (step_size_left >(STEP_SIZE - STEP_SIZE_TOLERANCE) && step_size_left < (STEP_SIZE + STEP_SIZE_TOLERANCE) ||
							step_size_right >(STEP_SIZE - STEP_SIZE_TOLERANCE) && step_size_right < (STEP_SIZE + STEP_SIZE_TOLERANCE)) {
							//this is a walkable step up
							if (current_stair.empty()) {
								//first step, get the other adjiacent too
								std::array<int, 2> starting_side = find_max_side({ side[0], side[1],  c });
								adjacent_map_t::iterator opposite = adjacent_map.find({ starting_side[1], starting_side[0] });
								if (opposite != adjacent_map.end() && opposite->second != c) {
									current_stair.insert({ starting_side[0], starting_side[1],  opposite->second });
								}
							}
							current_stair.insert({ side[0], side[1],  c });
							current_stair.insert({ side[1], side[0],  opposite->second });

							step({ side[0], opposite->second }, side[1]);
							step({ opposite->second, side[1] }, side[0]);
						}
					}
					else {
						//Check corner cases: is there a really near normal plane

						if (!current_stair.empty()) {
							stairs.push_back(current_stair);
							current_stair.clear();
						}
					}

				}
			}
		}

		hkVector4 centeroid(const std::array<int, 3>& t) {
			hkVector4 res(0.0, 0.0, 0.0);
			res.add4(geometry.m_vertices[t[0]]);
			res.add4(geometry.m_vertices[t[1]]);
			res.add4(geometry.m_vertices[t[2]]);
			return hkVector4(res(0) / 3, res(1) / 3, res(2) / 3);
		}

		void restich() {
			vector<hkVector4> true_vertices;
			map<int, int> aliases;
			for (int d = 0; d < geometry.m_vertices.getSize(); d++) {
				bool is_alias = false;
				int true_index = -1;
				int alias_index = -1;
				for (int i = 0; i < true_vertices.size(); i++) {
					hkVector4 dist; dist.setSub4(geometry.m_vertices[d], true_vertices[i]);
					if (dist.lengthSquared3() < 0.0001) {
						is_alias = true;
						aliases[d] = i;
						break;
					}
				}
				if (!is_alias) {
					aliases[d] = true_vertices.size();
					true_vertices.push_back(geometry.m_vertices[d]);
				}
			}
			//clean triangle
			for (hkTriangle& t : geometry.m_triangles) {
				map<int, int>::iterator alias_it = aliases.find(t.m_a);
				if (alias_it != aliases.end())
					t.m_a = alias_it->second;
				alias_it = aliases.find(t.m_b);
				if (alias_it != aliases.end())
					t.m_b = alias_it->second;
				alias_it = aliases.find(t.m_c);
				if (alias_it != aliases.end())
					t.m_c = alias_it->second;
			}
			//clean vertices
			geometry.m_vertices.clear();
			for (hkVector4 v : true_vertices)
				geometry.m_vertices.pushBack(v);

			Log::Info("restiched!");
		}

	public:
		vector< set < std::array<int, 3 > >> stairs;

		walker(hkGeometry& geometry) :
			geometry(geometry) {
			// vanilla collisions suck
			restich();
			// build adjacency map			
			for (const hkTriangle& t : geometry.m_triangles) {
				std::array<int, 2> side = { t.m_a, t.m_b };
				adjacent_map[side] = t.m_c;
				side = { t.m_b, t.m_c };
				adjacent_map[side] = t.m_a;
				side = { t.m_c, t.m_a };
				adjacent_map[side] = t.m_b;
			}
			get_planar_z_triangles();
			get_normal_z_triangles();
			//walk all planes
			for (auto& tri : planes) {
				step({ tri[0],tri[1] }, tri[2]);
				step({ tri[1],tri[2] }, tri[0]);
				step({ tri[2],tri[0] }, tri[1]);
			}
		}
	};


	//madness.
	void check_stairs() {
		vector< set < std::array<int, 3 > >>  stairs = walker(geometry).stairs;
		//geometry.m_triangles.clear();
		for (const set< std::array<int, 3 >>& stair : stairs) {
			for (const std::array<int, 3 > &tris : stair) {
				hkTriangle t;
				t.m_a = tris[0];
				t.m_b = tris[1];
				t.m_c = tris[2];
				//geometry.m_triangles.pushBack(t);
				for (hkGeometry::Triangle& triangle : geometry.m_triangles) {
					if (is_equal(t, triangle)) {
						if (!is_stairs_material(materials[triangle.m_material]))
							triangle.m_material = convert_to_stairs(triangle.m_material);
						break;
					}
				}
			}
		}
	}

	void calculate_collision()
	{
		//----  Havok  ----  START
		hkpCompressedMeshShape*					pCompMesh(NULL);
		hkpMoppCode*							pMoppCode(NULL);
		hkpMoppBvTreeShape*						pMoppBvTree(NULL);
		hkpCompressedMeshShapeBuilder			shapeBuilder;
		hkpMoppCompilerInput					mci;
		vector<int>								geometryIdxVec;
		vector<bhkCMSDMaterial>					tMtrlVec;
		int										subPartId(0);
		int										tChunkSize(0);

		bhkCompressedMeshShapeDataRef pData = new bhkCompressedMeshShapeData();

		//  initialize shape Builder
		shapeBuilder.m_stripperPasses = 5000;

		//  create compressedMeshShape
		pCompMesh = shapeBuilder.createMeshShape(0.001f, hkpCompressedMeshShape::MATERIAL_SINGLE_VALUE_PER_CHUNK);

		try {
			//  add geometry to shape
			subPartId = shapeBuilder.beginSubpart(pCompMesh);
			shapeBuilder.addGeometry(geometry, hkMatrix4::getIdentity(), pCompMesh);
			shapeBuilder.endSubpart(pCompMesh);
			shapeBuilder.addInstance(subPartId, hkMatrix4::getIdentity(), pCompMesh);

			//add materials to shape
			for (int i = 0; i < materials.size(); i++) {
				pCompMesh->m_materials.pushBack(i);
			}
			//create welding info
			mci.m_enableChunkSubdivision = false;  //  PC version

			pMoppCode = hkpMoppUtility::buildCode(pCompMesh, mci);
		}
		catch (...) {
			throw runtime_error("Unable to calculate MOPP code!");
		}
		pMoppBvTree = new hkpMoppBvTreeShape(pCompMesh, pMoppCode);
		hkpMeshWeldingUtility::computeWeldingInfo(pCompMesh, pMoppBvTree, hkpWeldingUtility::WELDING_TYPE_TWO_SIDED);
		//----  Havok  ----  END

		//----  Merge  ----  START

		//  --- modify MoppBvTree ---
		// set origin
		pMoppShape->SetOrigin(Vector3(pMoppBvTree->getMoppCode()->m_info.m_offset(0), pMoppBvTree->getMoppCode()->m_info.m_offset(1), pMoppBvTree->getMoppCode()->m_info.m_offset(2)));

		// set scale
		pMoppShape->SetScale(pMoppBvTree->getMoppCode()->m_info.getScale());

		// set build Type
		pMoppShape->SetBuildType(MoppDataBuildType((Niflib::byte) pMoppCode->m_buildType));

		//  copy mopp data
		pMoppShape->SetMoppData(vector<Niflib::byte>(pMoppBvTree->m_moppData, pMoppBvTree->m_moppData + pMoppBvTree->m_moppDataSize));

		Accessor<CMSPacker> packer(pCompMesh, pData, materials);

		bhkCompressedMeshShapeRef shape = new bhkCompressedMeshShape();
		shape->SetRadius(pCompMesh->m_radius * COLLISION_RATIO);
		shape->SetRadiusCopy(pCompMesh->m_radius * COLLISION_RATIO);
		shape->SetData(pData);
		shape->SetTarget(target);

		pMoppShape->SetShape(DynamicCast<bhkShape>(shape));

		delete pMoppCode;
		delete pMoppBvTree;
		delete pCompMesh;
	}

	bool isGeometryValid() {
		return
			!geometry.m_triangles.isEmpty() &&
			!geometry.m_vertices.isEmpty() &&
			!materials.empty() &&
			!layers.empty() &&
			!isGeometryDegenerate();
	}

	Vector3 _collision_translation;

public:

	bhkMoppBvTreeShapeRef pMoppShape;

	CollisionShapeVisitor(bhkShapeRef shape, const NifInfo& info, NiAVObject* target, const Vector3& collision_translation) :
		target(target),
		_collision_translation(collision_translation),
		RecursiveFieldVisitor(*this, info) {
		shape->accept(*this, info);
		if (pMoppShape == NULL)
			pMoppShape = new bhkMoppBvTreeShape();

		check_stairs();

		if (isGeometryValid()) {
			try {
				calculate_collision();
			}
			catch (...) {
				Log::Warn("Unable to upgrade shape, making a new convex hull");
				//Something bad happened, try to make a new collision
			}
		}
		else {
			//here we definetly want to generate a new collision. An example is paintbrush01

		}
	}

	template<class T>
	inline void visit_object(T& obj) {}

	template<class T>
	inline void visit_compound(T& obj) {}

	template<class T>
	inline void visit_field(T& obj) {}

	template<>
	inline void visit_object(bhkMoppBvTreeShape& obj) {
		pMoppShape = &obj;
	}

	template<>
	inline void visit_object(bhkPackedNiTriStripsShape& obj) {
		hkPackedNiTriStripsDataRef	pData(DynamicCast<hkPackedNiTriStripsData>(obj.GetData()));

		if (pData != NULL)
		{
			vector<Vector3> vertices(pData->GetVertices());
			vector<TriangleData> in_packed_triangles(pData->GetTriangles()); 
			vector<Triangle> in_triangles;
			for (TriangleData tpd : in_packed_triangles) {
				in_triangles.push_back({ tpd.triangle.v1 , tpd.triangle.v2, tpd.triangle.v3 });
			}

			geometry.m_vertices.setSize(vertices.size());
			geometry.m_triangles.setSize(in_triangles.size());

			for (int v = 0; v < vertices.size(); v++) {
				geometry.m_vertices[v] = TOVECTOR4((vertices[v] - _collision_translation) * COLLISION_RATIO);
			}


			vector<unsigned int> material_bounds;
			vector<OblivionSubShape> shapes(obj.GetSubShapes());
			if (shapes.empty()) {
				//fo
				shapes = pData->GetSubShapes();
			}

			int bound = 0;
			for (auto& shape : shapes) {
				bound += shape.numVertices;
				material_bounds.push_back(bound);
			}

			//vertices should be ordered by material, given the subshapes
			for (int t = 0; t < in_triangles.size(); t++) {
				int material_indexes[3];
				for (int i = 0; i < 3; i++) {
					int vertex_index = in_triangles[t][i];
					for (int m = 0; m < material_bounds.size(); m++) {
						if (vertex_index < material_bounds[m]) {
							material_indexes[i] = m;
							break;
						}
					}
				}
				int selected_index = material_indexes[0];
				if (material_indexes[0] != material_indexes[1] ||
					material_indexes[1] != material_indexes[2] ||
					material_indexes[0] != material_indexes[2])
				{
					Log::Info("Found Triangle with heterogeneus material!");
					std::map<int, int> frequencyMap;
					int maxFrequency = 0;
					int mostFrequentElement = 0;
					for (int x : material_indexes)
					{
						int f = ++frequencyMap[x];
						if (f > maxFrequency)
						{
							maxFrequency = f;
							mostFrequentElement = x;
						}
					}

					selected_index = mostFrequentElement;
				}
				SkyrimHavokMaterial s_material = convert_havok_material(shapes[selected_index].material.material_ob);
				int material_index = -1;
				auto material_it = find(materials.begin(), materials.end(), s_material);
				if (material_it != materials.end())
					material_index = distance(materials.begin(), material_it);
				else
				{
					material_index = materials.size();
					materials.push_back(s_material);
				}

				SkyrimLayer s_layer = convert_havok_layer(shapes[selected_index].havokFilter.layer_ob);
				int layer_index = -1;
				auto layer_it = find(layers.begin(), layers.end(), s_layer);
				if (layer_it != layers.end())
					layer_index = distance(layers.begin(), layer_it);
				else
				{
					layer_index = layers.size();
					layers.push_back(s_layer);
				}

				hkGeometry::Triangle htri;
				htri.m_a = in_triangles[t][0];
				htri.m_b = in_triangles[t][1];
				htri.m_c = in_triangles[t][2];
				htri.m_material = material_index;

				geometry.m_triangles[t] = htri;
			}
		}
	}

	SkyrimHavokMaterial material_from_flags(const VectorFlags& vf) {
		int value = (vf & MATERIAL_MASK) >> 6;
		return convert_havok_material((OblivionHavokMaterial)(value));
	}

	template<>
	inline void visit_object(bhkNiTriStripsShape& obj) {
		vector<NiTriStripsDataRef> shapes(obj.GetStripsData());
		vector<HavokFilter> data_layers(obj.GetDataLayers());

		//this is mysterious
		float							factor(COLLISION_RATIO * 0.1428f);

		int shape_index = 0;

		for (auto& shape : shapes) {
			size_t voffset = geometry.m_vertices.getSize();

			vector<Vector3> vertices(shape->GetVertices());
			for (auto& v : vertices) {
				geometry.m_vertices.pushBack(TOVECTOR4((v - _collision_translation * 6.9) * factor));
			}

			SkyrimHavokMaterial s_material = material_from_flags(shape->GetVectorFlags());
			int material_index = -1;
			auto material_it = find(materials.begin(), materials.end(), s_material);
			if (material_it != materials.end())
				material_index = distance(materials.begin(), material_it);
			else
			{
				material_index = materials.size();
				materials.push_back(s_material);
			}

			SkyrimLayer s_layer = convert_havok_layer(data_layers[shape_index].layer_ob);
			int layer_index = -1;
			auto layer_it = find(layers.begin(), layers.end(), s_layer);
			if (layer_it != layers.end())
				layer_index = distance(layers.begin(), layer_it);
			else
			{
				layer_index = layers.size();
				layers.push_back(s_layer);
			}

			vector<Triangle> in_triangles(triangulate(shape->GetPoints()));

			for (int t = 0; t < in_triangles.size(); t++) {
				hkGeometry::Triangle htri;
				htri.m_a = in_triangles[t][0] + voffset;
				htri.m_b = in_triangles[t][1] + voffset;
				htri.m_c = in_triangles[t][2] + voffset;
				htri.m_material = material_index;

				geometry.m_triangles.pushBack(htri);
			}
			shape_index++;
		}
		if (shapes.size() == 1) {
			materials[0] = convert_havok_material(obj.GetMaterial().material_ob);
		}

	}

	template<>
	inline void visit_object(bhkMeshShape& obj) {
		vector<NiTriStripsDataRef> shapes(obj.GetStripsData());

		//this is mysterious
		float							factor(COLLISION_RATIO * 0.1428f);

		int shape_index = 0;

		for (auto& shape : shapes) {
			size_t voffset = geometry.m_vertices.getSize();

			vector<Vector3> vertices(shape->GetVertices());
			for (auto& v : vertices) {
				geometry.m_vertices.pushBack(TOVECTOR4((v - _collision_translation * 6.9) * factor));
			}

			SkyrimHavokMaterial s_material = material_from_flags(shape->GetVectorFlags());
			int material_index = -1;
			auto material_it = find(materials.begin(), materials.end(), s_material);
			if (material_it != materials.end())
				material_index = distance(materials.begin(), material_it);
			else
			{
				material_index = materials.size();
				materials.push_back(s_material);
			}

			SkyrimLayer s_layer = SKYL_STATIC;
			int layer_index = -1;
			auto layer_it = find(layers.begin(), layers.end(), s_layer);
			if (layer_it != layers.end())
				layer_index = distance(layers.begin(), layer_it);
			else
			{
				layer_index = layers.size();
				layers.push_back(s_layer);
			}

			vector<Triangle> in_triangles(triangulate(shape->GetPoints()));

			for (int t = 0; t < in_triangles.size(); t++) {
				hkGeometry::Triangle htri;
				htri.m_a = in_triangles[t][0] + voffset;
				htri.m_b = in_triangles[t][1] + voffset;
				htri.m_c = in_triangles[t][2] + voffset;
				htri.m_material = material_index;

				geometry.m_triangles.pushBack(htri);
			}
			shape_index++;
		}
	}
};

struct ListShapeSetter {};

template<>
struct Accessor<ListShapeSetter> {
	Accessor(bhkListShapeRef list, const vector<unsigned int>& keys)
	{
		list->unknownInts = keys;
	}
};

bhkShapeRef upgrade_shape(const bhkShapeRef& shape, const NifInfo& info, NiAVObject* target, const Vector3& collision_translation) {
	if (shape->IsSameType(bhkMoppBvTreeShape::TYPE) ||
		shape->IsSameType(bhkNiTriStripsShape::TYPE) ||
		shape->IsSameType(bhkPackedNiTriStripsShape::TYPE))
		return CollisionShapeVisitor(shape, info, target, collision_translation).pMoppShape;
	else if (shape->IsSameType(bhkMultiSphereShape::TYPE)) {
		bhkMultiSphereShapeRef multi = DynamicCast<bhkMultiSphereShape>(shape);
		bhkListShapeRef new_list = new bhkListShape();
		auto child_filter_property = new_list->GetChildFilterProperty();
		child_filter_property.capacityAndFlags = 2147483648;
		new_list->SetChildFilterProperty(child_filter_property);
		auto child_shape_property = new_list->GetChildShapeProperty();
		child_shape_property.capacityAndFlags = 2147483648;
		new_list->SetChildShapeProperty(child_shape_property);
		new_list->SetMaterial(multi->GetMaterial());
		auto shapes = new_list->GetSubShapes();
		vector<unsigned int> unknown_ints;
		shapes.resize(multi->GetSpheres().size());
		unknown_ints.resize(multi->GetSpheres().size(), 0);
		unknown_ints[0] = multi->GetSpheres().size();
		size_t i = 0;
		for (const auto& sphere : multi->GetSpheres()) {
			bhkSphereShapeRef new_sphere = new bhkSphereShape();
			new_sphere->SetMaterial(multi->GetMaterial());
			new_sphere->SetRadius(sphere.radius);

			bhkConvexTransformShapeRef trans = new bhkConvexTransformShape();
			trans->SetMaterial(multi->GetMaterial());
			trans->SetRadius(sphere.radius);
			Matrix44 transform; transform.SetTrans(sphere.center);
			trans->SetTransform(transform);

			trans->SetShape(StaticCast<bhkShape>(new_sphere));

			shapes[i++] = StaticCast<bhkShape>(trans);
		}
		new_list->SetSubShapes(shapes);
		Accessor<ListShapeSetter>(new_list, unknown_ints);
		return new_list;
	}
	else if (shape->IsSameType(bhkConvexSweepShape::TYPE)) {
		//I have no idea
		bhkConvexSweepShapeRef sweep = DynamicCast<bhkConvexSweepShape>(shape);
		return sweep->GetShape();
	}
	else
		return shape;
}

vector<bhkShapeRef> upgrade_shapes(const vector<bhkShapeRef>& shapes, const NifInfo& info, NiAVObject* target, const Vector3& collision_translation) {
	vector<bhkShapeRef> out;
	for (bhkShapeRef shape : shapes) {
		if (shape->IsSameType(bhkMoppBvTreeShape::TYPE) ||
			shape->IsSameType(bhkNiTriStripsShape::TYPE) ||
			shape->IsSameType(bhkPackedNiTriStripsShape::TYPE) ||
			shape->IsSameType(bhkMultiSphereShape::TYPE))
			out.push_back(upgrade_shape(shape, info, target, collision_translation));
		else
			out.push_back(shape);
	}
	return out;
}

template<>
class Accessor<NiBlendFloatInterpolator> {
public:
	float play_speed;

	Accessor(NiBlendFloatInterpolator& obj) {
		play_speed = obj.unknownByte;
	}
};

template<>
class Accessor<bhkRigidBodyUpgrader> {

	bhkBallAndSocketConstraintRef create_ball_socket(bhkMalleableConstraintRef& descriptor) {
		bhkBallAndSocketConstraintRef constraint = new bhkBallAndSocketConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetBallAndSocket(descriptor->GetMalleable().ballAndSocket);
		return constraint;
	}

	bhkHingeConstraintRef create_hinge(bhkMalleableConstraintRef& descriptor) {
		bhkHingeConstraintRef constraint = new bhkHingeConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetEntities(descriptor->GetEntities());
		constraint->SetHinge(descriptor->GetMalleable().hinge);
		return constraint;
	}

	bhkLimitedHingeConstraintRef create_limited_hinge(bhkMalleableConstraintRef& descriptor) {
		bhkLimitedHingeConstraintRef constraint = new bhkLimitedHingeConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetEntities(descriptor->GetEntities());
		constraint->SetLimitedHinge(descriptor->GetMalleable().limitedHinge);
		return constraint;
	}

	bhkPrismaticConstraintRef create_prismatic(bhkMalleableConstraintRef& descriptor) {
		bhkPrismaticConstraintRef constraint = new bhkPrismaticConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetEntities(descriptor->GetEntities());
		constraint->SetPrismatic(descriptor->GetMalleable().prismatic);
		return constraint;
	}

	bhkRagdollConstraintRef create_ragdoll(bhkMalleableConstraintRef& descriptor) {
		bhkRagdollConstraintRef constraint = new bhkRagdollConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetEntities(descriptor->GetEntities());
		constraint->SetRagdoll(descriptor->GetMalleable().ragdoll);
		return constraint;
	}

	bhkStiffSpringConstraintRef create_stiff_spring(bhkMalleableConstraintRef& descriptor) {
		bhkStiffSpringConstraintRef constraint = new bhkStiffSpringConstraint();
		//USUALLY NONE
		//constraint->SetEntities({ descriptor.entityA, descriptor.entityB });
		constraint->SetEntities(descriptor->GetEntities());
		constraint->SetStiffSpring(descriptor->GetMalleable().stiffSpring);
		return constraint;
	}


	bhkSerializableRef convert_malleable(bhkMalleableConstraintRef malleable) {
		//Malleables really don't suit skyrim afaik
		switch (malleable->GetMalleable().type) {
		case BALLANDSOCKET:
			return create_ball_socket(malleable);
		case HINGE:
			return create_hinge(malleable);
		case LIMITED_HINGE:
			return create_limited_hinge(malleable);
		case PRISMATIC:
			return create_prismatic(malleable);
		case RAGDOLL:
			return create_ragdoll(malleable);
		case STIFFSPRING:
			return create_stiff_spring(malleable);
		case MALLEABLE:
			throw runtime_error("Nested Malleable constraints!");
		default:
			throw runtime_error("Unknown malleable inner type!");
		}

		return NULL;
	}

	const NifInfo& this_info;

	Vector3 _collision_translation;

public:
	Accessor(bhkRigidBody& obj, const NifInfo& info, NiAVObject* target, Vector3 collision_translation, const hkTransform& world_transform) : this_info(info) {
		_collision_translation = collision_translation;
		//zero out
		obj.unknownInt = 0;

		obj.unusedByte1 = (::byte)0;
		obj.unknownInt1 = (unsigned int)0;
		obj.unknownInt2 = (unsigned int)0;
		obj.unusedByte2 = (::byte)0;
		obj.timeFactor = (float)1.0;
		obj.gravityFactor = (float)1.0;
		obj.rollingFrictionMultiplier = (float)0.0;

		obj.enableDeactivation = true; //Seems has to be fixed; obj.solverDeactivation != SOLVER_DEACTIVATION_OFF;
		obj.unknownFloat1 = (float)0.0;

		obj.unknownBytes1 = { 0,0,0,0,0,0,0,0,0,0,0,0 };
		obj.unknownBytes2 = { 0,0,0,0 };

		//convert
		obj.havokFilter.layer_sk = convert_havok_layer(obj.havokFilter.layer_ob);
		obj.havokFilterCopy = obj.havokFilter;

		obj.translation -= _collision_translation;

		obj.translation.x *= COLLISION_RATIO;
		obj.translation.y *= COLLISION_RATIO;
		obj.translation.z *= COLLISION_RATIO;
		
		obj.center -= obj.center;

		obj.center.x *= COLLISION_RATIO;
		obj.center.y *= COLLISION_RATIO;
		obj.center.z *= COLLISION_RATIO;

		obj.inertiaTensor *= COLLISION_RATIO * COLLISION_RATIO;

		//bhkMalleableConstraints are no more supported I guess; Nor limited hinges?
		for (int i = 0; i < obj.constraints.size(); i++) {
			if (obj.constraints[i]->IsDerivedType(bhkMalleableConstraint::TYPE)) {
				obj.constraints[i] = convert_malleable(DynamicCast<bhkMalleableConstraint>(obj.constraints[i]));
			}
		}

		//anvildoorucinteriorload01 has Penetration Depth:  3.40282e+38
		if (obj.penetrationDepth < 0. || obj.penetrationDepth > 1.)
		{		
			obj.penetrationDepth = 0.15;
		}

		//Seems like the old havok settings must be deactivated
		//obj.motionSystem = MO_SYS_BOX_STABILIZED;
		if (obj.motionSystem == MO_SYS_KEYFRAMED)
			obj.motionSystem = MO_SYS_BOX_INERTIA;
		if (obj.qualityType == MO_QUAL_KEYFRAMED || obj.qualityType == MO_QUAL_KEYFRAMED_REPORT)
			obj.qualityType = MO_QUAL_FIXED;

		//Better stability?
		if (obj.havokFilter.layer_sk == SKYL_BIPED)
		{
			obj.motionSystem = MO_SYS_SPHERE_INERTIA;
		}

		//anvildoorucinteriorload01 has motion system fixed but solver deactivation medium
		if (obj.motionSystem == MO_SYS_FIXED) {
			obj.solverDeactivation = SOLVER_DEACTIVATION_OFF;
		}

		//obsolete collisions
		if (obj.shape->IsSameType(bhkMoppBvTreeShape::TYPE) ||
			obj.shape->IsSameType(bhkNiTriStripsShape::TYPE) ||
			obj.shape->IsSameType(bhkPackedNiTriStripsShape::TYPE)) {
			obj.shape = upgrade_shape(obj.shape, this_info, target, _collision_translation);
		}

	}
};

BSFadeNode* convert_root(NiObject* root)
{
	int numref = root->GetNumRefs();
	void* fadeNodeMem = (BSFadeNode*)malloc(sizeof(BSFadeNode));
	BSFadeNode* fadeNode = new (fadeNodeMem) BSFadeNode();

	//trick to overcome strong types inside refobjects;
	memcpy(root, fadeNode, sizeof(NiObject));
	for (int i = 0; i < numref; i++)
		root->AddRef();
	
	//root->AddRef();
	free(fadeNodeMem);


	return (BSFadeNode*)root;
}

void update_tangentspace(NiTriShapeDataRef data)
{
	vector<Vector3> vertices = data->GetVertices();
	Vector3 COM;
	if (vertices.size() != 0)
		COM = (COM / 2) + (ckcmd::Geometry::centeroid(vertices) / 2);
	vector<Triangle> faces = data->GetTriangles();
	vector<Vector3> normals = data->GetNormals();
	if (vertices.size() != 0 && faces.size() != 0 && data->GetUvSets().size() != 0) {
		vector<TexCoord> uvs = data->GetUvSets()[0];
		//Seems that tangent space calculation wants the uv flipped
		for (auto& uv : uvs)
		{
			float u = uv.u;
			uv.u = uv.v;
			uv.v = u;
		}
		TriGeometryContext g(vertices, COM, faces, uvs, normals);
		data->SetHasNormals(1);
		//recalculate
		data->SetNormals(g.normals);
		data->SetTangents(g.tangents);
		data->SetBitangents(g.bitangents);
		if (vertices.size() != g.normals.size() || vertices.size() != g.tangents.size() || vertices.size() != g.bitangents.size())
			throw runtime_error("Geometry mismatch!");
		data->SetBsVectorFlags(static_cast<BSVectorFlags>(data->GetBsVectorFlags() | BSVF_HAS_TANGENTS));
	}
}

IndexString getStringFromPalette(std::string palette, size_t offset)
{
	size_t findex = palette.find_first_of('\0', offset);
	size_t len = findex - offset;
	return palette.substr(offset, len);
}

NiTriShapeRef convert_strip(NiTriStripsRef& stripsRef)
{
	//Convert NiTriStrips to NiTriShapes first of all.
	NiTriShapeRef shapeRef = new NiTriShape();
	shapeRef->SetName(stripsRef->GetName());
	shapeRef->SetExtraDataList(stripsRef->GetExtraDataList());
	shapeRef->SetTranslation(stripsRef->GetTranslation());
	shapeRef->SetRotation(stripsRef->GetRotation());
	shapeRef->SetScale(stripsRef->GetScale());
	shapeRef->SetFlags(524302);
	if (stripsRef->GetFlags() & 1)
		shapeRef->SetFlags(524303);
	shapeRef->SetData(stripsRef->GetData());
	shapeRef->SetShaderProperty(stripsRef->GetShaderProperty());
	shapeRef->SetProperties(stripsRef->GetProperties());

	//Then do the data..
	bool hasAlpha = false;

	NiTriStripsDataRef stripsData = DynamicCast<NiTriStripsData>(stripsRef->GetData());
	NiTriShapeDataRef shapeData = new  NiTriShapeData();

	shapeData->SetHasVertices(stripsData->GetHasVertices());
	shapeData->SetVertices(stripsData->GetVertices());
	shapeData->SetBsVectorFlags(static_cast<BSVectorFlags>(stripsData->GetVectorFlags()));
	shapeData->SetUvSets(stripsData->GetUvSets());
	if (!shapeData->GetUvSets().empty())
		shapeData->SetBsVectorFlags(static_cast<BSVectorFlags>(shapeData->GetBsVectorFlags() | BSVF_HAS_UV));
	shapeData->SetCenter(stripsData->GetCenter());
	shapeData->SetRadius(stripsData->GetRadius());
	shapeData->SetHasVertexColors(stripsData->GetHasVertexColors());
	shapeData->SetVertexColors(stripsData->GetVertexColors());
	shapeData->SetConsistencyFlags(stripsData->GetConsistencyFlags());
	vector<Triangle> triangles = triangulate(stripsData->GetPoints());
	shapeData->SetNumTriangles(triangles.size());
	shapeData->SetNumTrianglePoints(triangles.size() * 3);
	shapeData->SetHasTriangles(1);
	shapeData->SetTriangles(triangles);

	shapeData->SetHasNormals(stripsData->GetHasNormals());
	shapeData->SetNormals(stripsData->GetNormals());

	vector<Vector3> vertices = shapeData->GetVertices();
	Vector3 COM;
	if (vertices.size() != 0)
		COM = (COM / 2) + (ckcmd::Geometry::centeroid(vertices) / 2);
	vector<Triangle> faces = shapeData->GetTriangles();
	vector<Vector3> normals = shapeData->GetNormals();
	if (normals.size() != vertices.size() && (faces.empty() || shapeData->GetUvSets().empty()))
	{
		//normals are needed for sse, let's just fill the void here
		normals.resize(vertices.size());
		for (int i = 0; i< vertices.size(); i++)
		{
			normals[i] = COM - vertices[i];
			normals[i] = normals[i].Normalized();
		}
		shapeData->SetHasNormals(true);
		shapeData->SetNormals(normals);
	}
	if (shapeData->GetVertices().size() != 0 && shapeData->GetTriangles().size() != 0 && shapeData->GetUvSets().size() != 0) {
		update_tangentspace(shapeData);
	}
	else {
		shapeData->SetTangents(stripsData->GetTangents());
		shapeData->SetBitangents(stripsData->GetBitangents());
	}

	shapeRef->SetData(DynamicCast<NiGeometryData>(shapeData));

	//TODO: shared normals no more supported
	shapeData->SetMatchGroups(vector<MatchGroup>{});

	return shapeRef;
}

unsigned long upper_power_of_two(unsigned long v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

class BlendUnknownSet {};

template<>
class Accessor<BlendUnknownSet> {
public:
	Accessor(NiBlendFloatInterpolatorRef source, NiBlendFloatInterpolatorRef dest)
	{
		dest->unknownByte = source->unknownByte;
	}

	Accessor(BYTE source, NiBlendFloatInterpolatorRef dest)
	{
		dest->unknownByte = source;
	}
};

void checkDiffuseAlpha(const string& diffuse_name, const string& export_path) {
	DirectX::ScratchImage g_image;
	DirectX::ScratchImage g_timage;
	DirectX::TexMetadata g_original_info;
	vector<uint8_t> dds_bin;
	games.load(Games::TES4, diffuse_name, dds_bin);
	HRESULT result = DirectX::LoadFromDDSMemory(dds_bin.data(), dds_bin.size(),
		DirectX::DDS_FLAGS_NONE, &g_original_info, g_image);
	if (FAILED(result)) {
		Log::Info("Unable to load DDS Diffuse Map %s", diffuse_name);
		return;
	}

	if (DirectX::IsCompressed(g_image.GetMetadata().format))
	{
		size_t nimg = g_image.GetImageCount();
		result = DirectX::Decompress(g_image.GetImages(), nimg, g_original_info, DXGI_FORMAT_UNKNOWN /* picks good default */, g_timage);
	}
	else {
		g_timage = move(g_image);
	}
	const auto& g_meta = g_timage.GetMetadata();

	auto img = g_timage.GetImage(0, 0, 0);
	assert(img);
	size_t nimg = g_timage.GetImageCount();
	auto& info = g_timage.GetMetadata();

	string out_name = diffuse_name;
	out_name.insert(9, "tes4\\");
	fs::path out_path = fs::path(export_path) / out_name;
	fs::create_directories(out_path.parent_path());

	DirectX::ScratchImage converted;
	converted.InitializeFromImage(*img);


	HRESULT hr = DirectX::Convert(img, nimg, info, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_SRGB_OUT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
	int error = GetLastError();

	DirectX::ScratchImage compressed;
	compressed.InitializeFromImage(*converted.GetImage(0, 0, 0));
	hr = DirectX::Compress(converted.GetImages(), converted.GetImageCount(), converted.GetMetadata(), DXGI_FORMAT_BC1_UNORM_SRGB, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
	if (FAILED(hr))
	{
		Log::Info("Unable to compress glow map %s", diffuse_name);
		hr = DirectX::SaveToDDSFile(converted.GetImages(), converted.GetImageCount(), converted.GetMetadata(),
			DirectX::DDS_FLAGS_NONE,
			out_path.wstring().c_str());
		return;
	}
	hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
		DirectX::DDS_FLAGS_NONE,
		out_path.wstring().c_str());
}

void convertGlowMap(const string& glow_name, const string& export_path) {
	DirectX::ScratchImage g_image;
	DirectX::ScratchImage g_timage;
	DirectX::TexMetadata g_original_info;
	vector<uint8_t> dds_glow_bin;
	games.load(Games::TES4, glow_name, dds_glow_bin);
	HRESULT result = DirectX::LoadFromDDSMemory(dds_glow_bin.data(), dds_glow_bin.size(),
		DirectX::DDS_FLAGS_NONE, &g_original_info, g_image);
	if (FAILED(result)) {
		Log::Info("Unable to load DDS Glow Map %s", glow_name);
		return;
	}

	if (DirectX::IsCompressed(g_image.GetMetadata().format))
	{
		size_t nimg = g_image.GetImageCount();
		result = DirectX::Decompress(g_image.GetImages(), nimg, g_original_info, DXGI_FORMAT_UNKNOWN /* picks good default */, g_timage);
	}
	else {
		g_timage = move(g_image);
	}
	const auto& g_meta = g_timage.GetMetadata();

	auto img = g_timage.GetImage(0, 0, 0);
	assert(img);
	size_t nimg = g_timage.GetImageCount();
	auto& info = g_timage.GetMetadata();

	string out_name = glow_name;
	out_name.insert(9, "tes4\\");
	fs::path out_path = fs::path(export_path) / out_name;
	fs::create_directories(out_path.parent_path());

	DirectX::ScratchImage converted;
	converted.InitializeFromImage(*img);


	HRESULT hr = DirectX::Convert(img, nimg, info, DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_SRGB_OUT, DirectX::TEX_THRESHOLD_DEFAULT, converted);
	int error = GetLastError();

	DirectX::ScratchImage compressed;
	compressed.InitializeFromImage(*converted.GetImage(0,0,0));
	hr = DirectX::Compress(converted.GetImages(), converted.GetImageCount(), converted.GetMetadata(), DXGI_FORMAT_BC1_UNORM_SRGB, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed);
	if (FAILED(hr))
	{
		Log::Info("Unable to compress glow map %s", glow_name);
		hr = DirectX::SaveToDDSFile(converted.GetImages(), converted.GetImageCount(), converted.GetMetadata(),
			DirectX::DDS_FLAGS_NONE,
			out_path.wstring().c_str());
		return;
	}
	hr = DirectX::SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
		DirectX::DDS_FLAGS_NONE,
		out_path.wstring().c_str());
}

class FlipBookConverter
{

public:

	vector<pair<float, float>> uv_atlas;

	FlipBookConverter(NiFlipControllerRef controller, BSShaderPropertyRef property, bool& hasGlow, const string& export_path)
	{

		if (controller->GetTextureSlot() != BASE_MAP)
			throw runtime_error("Unsupported NiFlipControllerRef slot!");

		vector<string> vector_preferred = {};
		vector<DirectX::ScratchImage> images(controller->GetSources().size());
		vector<DirectX::ScratchImage> g_images(controller->GetSources().size());
		DirectX::TexMetadata original_info;
		DirectX::TexMetadata g_original_info;
		vector<DirectX::ScratchImage> timages(controller->GetSources().size());
		vector<DirectX::ScratchImage> g_timages(controller->GetSources().size());
		string first_name = "";
		string path = "";
		string glow_name = "";
		const auto& tex_images = controller->GetSources();
		for (int i=0; i<controller->GetSources().size(); i++) 
		{
			const auto& tex = tex_images[i];
			string name = tex->GetFileName();
			if (glow_name == "")
			{
				glow_name = name; 
				glow_name.insert(glow_name.size() - 4, "_g");
			}
			if (first_name == "")
				first_name = fs::path(name).filename().string();
			if (path == "")
				path = tex->GetFileName();

			vector<uint8_t> dds_bin;
			vector<uint8_t> dds_glow_bin;
			games.load(Games::TES4, name, dds_bin);
			games.load(Games::TES4, glow_name, dds_glow_bin);
			if (!dds_glow_bin.empty())
				hasGlow = true;

			HRESULT result =  DirectX::LoadFromDDSMemory(dds_bin.data(), dds_bin.size(),
				DirectX::DDS_FLAGS_NONE, &original_info, images[i]);

			if (hasGlow)
				result = DirectX::LoadFromDDSMemory(dds_glow_bin.data(), dds_glow_bin.size(),
					DirectX::DDS_FLAGS_NONE, &g_original_info, g_images[i]);

			if (FAILED(result))
				throw runtime_error("Unable to load NiFlipController source " + name);
			if (DirectX::IsCompressed(images[i].GetMetadata().format))
			{
				size_t nimg = images[i].GetImageCount();
				result = DirectX::Decompress(images[i].GetImages(), nimg, original_info, DXGI_FORMAT_UNKNOWN /* picks good default */, timages[i]);
			}
			else {
				timages[i] = move(images[i]);
			}

			if (hasGlow)
			{
				if (DirectX::IsCompressed(g_images[i].GetMetadata().format))
				{
					size_t nimg = g_images[i].GetImageCount();
					result = DirectX::Decompress(g_images[i].GetImages(), nimg, g_original_info, DXGI_FORMAT_UNKNOWN /* picks good default */, g_timages[i]);
				}
				else {
					g_timages[i] = move(g_images[i]);
				}
			}
		}

		const auto& meta = timages.begin()->GetMetadata();
		const auto& g_meta = g_timages.begin()->GetMetadata();

		//simple bin packing
		size_t total_area = meta.width * meta.height * timages.size();

		map<pair<int,int>, int> sizes;

		for (int i = 1; i <= timages.size(); i++)
		{
			int rows = i;

			for (int k = 1; k <= timages.size(); k++)
			{

				int colums = k;

				int width = upper_power_of_two(meta.width* rows);
				int height = upper_power_of_two(meta.height* colums);

				int waste = width * height - total_area;
				int c_size = rows * colums;
				if (waste > 0 && c_size >= timages.size())
					sizes[{rows, colums}] = width * height - total_area;
			}
		}

		int rows = INT_MAX;
		int columns = INT_MAX;
		int size = INT_MAX;

		for (const auto& entry : sizes)
		{
			if (entry.second == size)
			{
				if (abs(entry.first.first-entry.first.second) < abs(rows - columns))
				{
					rows = entry.first.first;
					columns = entry.first.second;
				}
			}
			if (entry.second < size)
			{
				rows = entry.first.first;
				columns = entry.first.second;
				size = entry.second;
			}
		}

		DirectX::ScratchImage collage_img;
		DirectX::ScratchImage g_collage_img;
		int collage_width = upper_power_of_two(meta.width * columns);
		int collage_height = upper_power_of_two(meta.height * rows);

		float u_scale = float(meta.width) / float(collage_width);
		float v_scale = float(meta.height) / float(collage_height);

		collage_img.Initialize2D(meta.format, collage_width, collage_height, 1, 1);
		memset(collage_img.GetPixels(), 0, collage_img.GetPixelsSize());

		if (hasGlow)
		{
			g_collage_img.Initialize2D(g_meta.format, collage_width, collage_height, 1, 1);
			memset(g_collage_img.GetPixels(), 0, g_collage_img.GetPixelsSize());
		}

		for (int i = 0; i < timages.size(); i++)
		{
			int row = (i / columns);
			int column = i % (columns);

			uv_atlas.push_back({ column * u_scale , row *v_scale });

			DirectX::ScratchImage& this_image = timages[i];

			HRESULT hr = DirectX::CopyRectangle(*this_image.GetImage(0, 0, 0), DirectX::Rect(0, 0, meta.width, meta.height),
				*collage_img.GetImages(), DirectX::TEX_FILTER_DEFAULT, column*meta.width, row*meta.height);
			if (hasGlow)
			{

				if (g_meta.width != meta.width || g_meta.height != meta.height)
				{
					DirectX::ScratchImage resized;
					resized.Initialize2D(g_timages[i].GetMetadata().format, meta.width, meta.height, 1, 1);
					HRESULT hr = DirectX::Resize(
						*g_timages[i].GetImage(0,0,0), meta.width, meta.height, DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_CUBIC, resized);

					hr = DirectX::CopyRectangle(*resized.GetImage(0, 0, 0), DirectX::Rect(0, 0, meta.width, meta.height),
						*g_collage_img.GetImages(), DirectX::TEX_FILTER_DEFAULT, column*meta.width, row*meta.height);
				}
				else {
					HRESULT hr = DirectX::CopyRectangle(*g_timages[i].GetImage(0, 0, 0), DirectX::Rect(0, 0, meta.width, meta.height),
						*g_collage_img.GetImages(), DirectX::TEX_FILTER_DEFAULT, column*meta.width, row*meta.height);
				}

				
			}
		}

		auto img = collage_img.GetImage(0, 0, 0);
		assert(img);
		size_t nimg = collage_img.GetImageCount();
		auto& info = collage_img.GetMetadata();

		DirectX::ScratchImage compressed;
		compressed.InitializeFromImage(*img);

		HRESULT hr = DirectX::Compress(img, nimg, info, original_info.format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed);

		auto cimg = compressed.GetImage(0, 0, 0);
		assert(cimg);
		size_t cnimg = compressed.GetImageCount();
		auto& cinfo = compressed.GetMetadata();
	
		path.insert(9, "tes4\\");
		fs::path out_path = fs::path(export_path) / path;
		fs::create_directories(out_path.parent_path());

		hr = DirectX::SaveToDDSFile(cimg, cnimg, cinfo,
			DirectX::DDS_FLAGS_NONE,
			out_path.wstring().c_str());

		if (FAILED(hr))
			throw runtime_error("Unable to save NiFlipController sources!");


		if (hasGlow)
		{
			auto img = g_collage_img.GetImage(0, 0, 0);
			assert(img);
			size_t nimg = g_collage_img.GetImageCount();
			auto& info = g_collage_img.GetMetadata();

			glow_name.insert(9, "tes4\\");
			fs::path out_path = fs::path(export_path) / glow_name;
			fs::create_directories(out_path.parent_path());

			DirectX::ScratchImage converted;
			converted.InitializeFromImage(*img);

			HRESULT hr = DirectX::Convert(img, nimg, info, collage_img.GetMetadata().format, DirectX::TEX_FILTER_FORCE_NON_WIC | DirectX::TEX_FILTER_SRGB_OUT, DirectX::TEX_THRESHOLD_DEFAULT, converted);

			auto convimg = converted.GetImage(0, 0, 0);
			assert(convimg);
			size_t nconvimg = converted.GetImageCount();
			auto& convinfo = converted.GetMetadata();

			DirectX::ScratchImage compressed;
			compressed.InitializeFromImage(*convimg);


			hr = DirectX::Compress(convimg, nconvimg, convinfo, convinfo.format, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed);

			auto cimg = compressed.GetImage(0, 0, 0);
			assert(cimg);
			size_t cnimg = compressed.GetImageCount();
			auto& cinfo = compressed.GetMetadata();



			hr = DirectX::SaveToDDSFile(cimg, cnimg, cinfo,
					DirectX::DDS_FLAGS_NONE,
					out_path.wstring().c_str());

		}

		NiFloatInterpControllerRef u_controller;
		NiFloatInterpControllerRef v_controller;
		if (property->IsSameType(BSLightingShaderProperty::TYPE))
		{
			BSLightingShaderPropertyRef ref = DynamicCast<BSLightingShaderProperty>(property);
			ref->SetUvScale({ u_scale , v_scale });
			BSLightingShaderPropertyFloatControllerRef u_temp_controller = new BSLightingShaderPropertyFloatController();
			u_temp_controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_OFFSET);
			BSLightingShaderPropertyFloatControllerRef v_temp_controller = new BSLightingShaderPropertyFloatController();
			v_temp_controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_OFFSET);
			u_controller = u_temp_controller;
			v_controller = v_temp_controller;
		}
		else {
			BSEffectShaderPropertyRef ref = DynamicCast<BSEffectShaderProperty>(property);
			ref->SetUvScale({ u_scale , v_scale });
			BSEffectShaderPropertyFloatControllerRef u_temp_controller = new BSEffectShaderPropertyFloatController();
			u_temp_controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_U_OFFSET);
			BSEffectShaderPropertyFloatControllerRef v_temp_controller = new BSEffectShaderPropertyFloatController();
			v_temp_controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_V_OFFSET);
			u_controller = u_temp_controller;
			v_controller = v_temp_controller;
		}
		u_controller->SetFlags(controller->GetFlags());
		u_controller->SetFrequency(controller->GetFrequency());
		u_controller->SetPhase(controller->GetPhase());
		u_controller->SetStartTime(controller->GetStartTime());
		u_controller->SetStopTime(controller->GetStopTime());
		u_controller->SetTarget(property);

		v_controller->SetFlags(controller->GetFlags());
		v_controller->SetFrequency(controller->GetFrequency());
		v_controller->SetPhase(controller->GetPhase());
		v_controller->SetStartTime(controller->GetStartTime());
		v_controller->SetStopTime(controller->GetStopTime());
		v_controller->SetTarget(property);

		NiFloatDataRef u_data = new NiFloatData();
		KeyGroup<float> u_keys;
		u_keys.interpolation = CONST_KEY;
		NiFloatDataRef v_data = new NiFloatData();
		KeyGroup<float> v_keys;
		v_keys.interpolation = CONST_KEY;

		NiFloatInterpolatorRef f_ref = DynamicCast<NiFloatInterpolator>(controller->GetInterpolator());
		if (f_ref == NULL)
		{
			NiBlendFloatInterpolatorRef f_ref = DynamicCast<NiBlendFloatInterpolator>(controller->GetInterpolator());
			if (f_ref != NULL)
			{
				//this is blended and must be resolved later into the sequence.
				u_controller->SetInterpolator(StaticCast<NiInterpolator>(controller->GetInterpolator()));
				NiBlendFloatInterpolatorRef v_ref = new NiBlendFloatInterpolator();
				v_ref->SetFlags(f_ref->GetFlags());
				Accessor<BlendUnknownSet>(f_ref, v_ref);
				v_ref->SetManagedControlled(f_ref->GetManagedControlled());
				v_ref->SetValue(f_ref->GetValue());

				v_controller->SetInterpolator(StaticCast<NiInterpolator>(v_ref));

				u_controller->SetNextController(StaticCast<NiTimeController>(v_controller));

				property->SetController(StaticCast<NiTimeController>(u_controller));

				return;

			}
			else {
				throw runtime_error("Unknown Interpolator Type into NiflipController");
			}
		}
		NiFloatInterpolatorRef u_interpolator = new NiFloatInterpolator();
		NiFloatInterpolatorRef v_interpolator = new NiFloatInterpolator();

		if (f_ref->GetData() != NULL)
		{
			for (const auto& frame : f_ref->GetData()->GetData().keys)
			{
				Key<float> u_value;
				Key<float> v_value;

				u_value.time = frame.time;
				v_value.time = frame.time;

				int atlas_key = (int)frame.data;

				pair<float, float> values = uv_atlas[atlas_key];

				u_value.data = values.first;
				v_value.data = values.second;

				u_keys.keys.push_back(u_value);
				v_keys.keys.push_back(v_value);
			}
			u_data->SetData(u_keys);
			v_data->SetData(v_keys);
			u_interpolator->SetData(u_data);
			v_interpolator->SetData(v_data);
		}

		if (f_ref->GetValue() >= 0)
		{
			pair<float, float> values = uv_atlas[f_ref->GetValue()];
			u_interpolator->SetValue(values.first);
			v_interpolator->SetValue(values.second);
		}

		u_controller->SetInterpolator(StaticCast<NiInterpolator>(u_interpolator));
		v_controller->SetInterpolator(StaticCast<NiInterpolator>(v_interpolator));

		u_controller->SetNextController(StaticCast<NiTimeController>(v_controller));

		property->SetController(StaticCast<NiTimeController>(u_controller));
		uv_atlas.clear();
	}
};

map<NiMaterialColorControllerRef, NiPoint3InterpControllerRef> material_controllers_map;
map<NiAlphaControllerRef, NiFloatInterpControllerRef> material_alpha_controllers_map;
map<NiFlipControllerRef, NiFloatInterpControllerRef> material_flip_controllers_map;
map<NiFlipControllerRef, pair< float, vector<pair<float, float>>>> deferred_blends;
map<NiTextureTransformControllerRef, NiFloatInterpControllerRef> material_transform_controllers_map;

void convert_tt_rotate(NiTextureTransformControllerRef oldController, NiFloatInterpControllerRef newController, NiInterpolatorRef oldInterpolator, NiFloatInterpControllerRef& new_v_controller, NiInterpolatorRef& newInterpolatorU, NiInterpolatorRef& newInterpolatorV)
{
	if (oldInterpolator == NULL)
		throw runtime_error("NULL intepolator while converting TT_ROTATE");
	if (!oldInterpolator->IsDerivedType(NiFloatInterpolator::TYPE))
		throw runtime_error("UNKNOWN intepolator TYPE while converting TT_ROTATE: " + oldInterpolator->GetInternalType().GetTypeName());
	NiFloatInterpolatorRef rotate_interpolator = DynamicCast<NiFloatInterpolator>(oldInterpolator);

	NiFloatInterpolatorRef u_interpolator = new NiFloatInterpolator();
	NiFloatInterpolatorRef v_interpolator = new NiFloatInterpolator();

	if (rotate_interpolator->GetData() != NULL)
	{
		NiFloatDataRef u_data = new NiFloatData();
		KeyGroup<float> u_keys;
		u_keys.interpolation = CONST_KEY;
		NiFloatDataRef v_data = new NiFloatData();
		KeyGroup<float> v_keys;
		v_keys.interpolation = CONST_KEY;

		vector < Key<float>> keys = rotate_interpolator->GetData()->GetData().keys;
		size_t samples = (keys[keys.size()-1].time - keys[0].time)/0.1;
		
		for (int i = 0; i < samples; i++)
		{
			Key<float> u_value;
			Key<float> v_value;

			u_value.time = keys[0].time + i*0.1;
			v_value.time = keys[0].time + i * 0.1;

			u_value.data = cos(keys[0].data + i * 0.1) - 0.5;
			v_value.data = sin(keys[0].data + i * 0.1) - 0.5;

			/*u_value.forward_tangent = sin(frame.forward_tangent);
			v_value.forward_tangent = cos(frame.forward_tangent);
			u_value.backward_tangent = sin(frame.backward_tangent);
			v_value.backward_tangent = cos(frame.backward_tangent);

			u_value.tension = sin(frame.tension);
			u_value.bias = sin(frame.bias);
			u_value.continuity = sin(frame.continuity);

			v_value.tension = cos(frame.tension);
			v_value.bias = cos(frame.bias);
			v_value.continuity = cos(frame.continuity);*/

			u_keys.keys.push_back(u_value);
			v_keys.keys.push_back(v_value);
		}

		u_data->SetData(u_keys);
		v_data->SetData(v_keys);
		u_interpolator->SetData(u_data);
		v_interpolator->SetData(v_data);
	}

	if (rotate_interpolator->GetValue() > numeric_limits<float>::min() && rotate_interpolator->GetValue() < numeric_limits<float>::max())
	{
		u_interpolator->SetValue(cos(rotate_interpolator->GetValue()));
		v_interpolator->SetValue(sin(rotate_interpolator->GetValue()));
	}

	if (newController->IsDerivedType(BSLightingShaderPropertyFloatController::TYPE))
	{
		BSLightingShaderPropertyFloatControllerRef v_controller = new BSLightingShaderPropertyFloatController();
		v_controller->SetNextController(newController->GetNextController());
		v_controller->SetFlags(newController->GetFlags());
		v_controller->SetFrequency(newController->GetFrequency());
		v_controller->SetPhase(newController->GetPhase());
		v_controller->SetStartTime(newController->GetStartTime());
		v_controller->SetStopTime(newController->GetStopTime());
		v_controller->SetTarget(newController->GetTarget());
		v_controller->SetInterpolator(StaticCast<NiInterpolator>(v_interpolator));
		v_controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_OFFSET);

		new_v_controller = v_controller;
	}
	else {
		BSEffectShaderPropertyFloatControllerRef v_controller = new BSEffectShaderPropertyFloatController();
		v_controller->SetNextController(newController->GetNextController());
		v_controller->SetFlags(newController->GetFlags());
		v_controller->SetFrequency(newController->GetFrequency());
		v_controller->SetPhase(newController->GetPhase());
		v_controller->SetStartTime(newController->GetStartTime());
		v_controller->SetStopTime(newController->GetStopTime());
		v_controller->SetTarget(newController->GetTarget());
		v_controller->SetInterpolator(StaticCast<NiInterpolator>(v_interpolator));
		v_controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_V_OFFSET);

		new_v_controller = v_controller;
	}

	newInterpolatorU = u_interpolator;
	newInterpolatorV = v_interpolator;
}

class ConverterVisitor : public RecursiveFieldVisitor<ConverterVisitor> {
	
	const NifInfo& this_info;
	set<void*> already_upgraded;
	const vector<NiObjectRef>& blocks;
	const fs::path& _export_path;

	map<void*, void*> collision_target_map;
	map<void*, hkTransform> collision_target_world_transform;

	//vector<NiTriShapeRef> particle_geometry;

	int controller_id = 1;
	bool _is_clutter = false;

	NiObjectRef find_parent(NiObjectRef& object) {
		for (auto& block : blocks) {
			if (block->IsDerivedType(NiNode::TYPE)) {
				auto& node = DynamicCast<NiNode>(block);
				for (auto& child : node->GetChildren()) {
					if (child == object)
						return node;
				}
				if (node->GetCollisionObject() != NULL) {
					if (object == node->GetCollisionObject())
						return node;
				}
			}
		}
		return NULL;
	}

	std::vector< NiObjectRef> find_parents(NiObjectRef& object) {
		std::vector< NiObjectRef> parents;
		NiObjectRef parent = object;
		while (parent != NULL) {
			parent = find_parent(parent);
			if (parent != NULL)
				parents.insert(parents.begin(), parent);
		}
		return parents;
	}


public:

	vector<string> nisequences;
	Vector3 _collision_translation;
	Vector3 _translation;

	ConverterVisitor(const NifInfo& info, NiObjectRef root, const vector<NiObjectRef>& blocks, const Vector3& translation, const Vector3& collision_translation, bool is_clutter, const fs::path& export_path) :
		RecursiveFieldVisitor(*this, info),
		this_info(info),
		blocks(blocks),
		_translation(translation),
		_collision_translation(collision_translation),
		_is_clutter(is_clutter),
		_export_path(export_path)
	{
		root->accept(*this, info);
		for (NiObjectRef obj : blocks) {
			if (obj->IsDerivedType(NiControllerSequence::TYPE)) {
				NiControllerSequenceRef nisblock = DynamicCast<NiControllerSequence>(obj);
				vector<ControlledBlock> blocks = nisblock->GetControlledBlocks();
				for (int i = 0; i != blocks.size(); i++) {
					if (blocks[i].controller->IsDerivedType(NiMaterialColorController::TYPE)) {
						map<NiMaterialColorControllerRef, NiPoint3InterpControllerRef>::iterator cc = material_controllers_map.find(DynamicCast<NiMaterialColorController>(blocks[i].controller));
						if (cc != material_controllers_map.end())
							blocks[i].controller = cc->second;
						else
							Log::Info("Not Found!");
					}
					if (blocks[i].controller->IsDerivedType(NiAlphaController::TYPE)) {
						map<NiAlphaControllerRef, NiFloatInterpControllerRef>::iterator cc = material_alpha_controllers_map.find(DynamicCast<NiAlphaController>(blocks[i].controller));
						if (cc != material_alpha_controllers_map.end())
							blocks[i].controller = cc->second;
						else
							Log::Info("Not Found!");
					}
					if (blocks[i].controller->IsDerivedType(NiFlipController::TYPE)) {
						map<NiFlipControllerRef, NiFloatInterpControllerRef>::iterator cc = material_flip_controllers_map.find(DynamicCast<NiFlipController>(blocks[i].controller));
						if (cc != material_flip_controllers_map.end())
						{
							//check if it's a blend defer
							auto df = deferred_blends.find(DynamicCast<NiFlipController>(blocks[i].controller));
							if (df != deferred_blends.end())
							{
								NiFloatInterpControllerRef u_controller = cc->second;
								NiFloatInterpControllerRef v_controller = DynamicCast<NiFloatInterpController>(u_controller->GetNextController());

								NiFloatInterpolatorRef f_ref = DynamicCast<NiFloatInterpolator>(blocks[i].interpolator);
								if (f_ref == NULL)
									throw runtime_error("NiFlipControllerRef Deferred interpolator without interpolator!");
								vector<pair<float, float>>& uv_atlas = df->second.second;
								NiFloatInterpolatorRef u_interpolator = new NiFloatInterpolator();
								NiFloatInterpolatorRef v_interpolator = new NiFloatInterpolator();						
								
								if (f_ref->GetData() != NULL)
								{
									NiFloatDataRef u_data = new NiFloatData();
									KeyGroup<float> u_keys;
									u_keys.interpolation = CONST_KEY;
									NiFloatDataRef v_data = new NiFloatData();
									KeyGroup<float> v_keys;
									v_keys.interpolation = CONST_KEY;

									for (const auto& frame : f_ref->GetData()->GetData().keys)
									{
										Key<float> u_value;
										Key<float> v_value;

										u_value.time = frame.time * df->second.first;
										v_value.time = frame.time * df->second.first;

										int atlas_key = (int)frame.data;

										pair<float, float> values = uv_atlas[atlas_key];

										u_value.data = values.first;
										v_value.data = values.second;

										u_keys.keys.push_back(u_value);
										v_keys.keys.push_back(v_value);
									}

									u_data->SetData(u_keys);
									v_data->SetData(v_keys);
									u_interpolator->SetData(u_data);
									v_interpolator->SetData(v_data);
								}
								
								if (f_ref->GetValue() >= 0)
								{
									pair<float, float> values = uv_atlas[f_ref->GetValue()];

									u_interpolator->SetValue(values.first);
									v_interpolator->SetValue(values.second);
								}

								ControlledBlock additional_v_block = blocks[i];
								blocks[i].controller = u_controller;
								blocks[i].interpolator = u_interpolator;
								blocks[i].controllerId = to_string(controller_id++);
								additional_v_block.controller = v_controller;
								additional_v_block.interpolator = v_interpolator;
								additional_v_block.controllerId = to_string(controller_id++);
								blocks.insert(blocks.begin() + i + 1, additional_v_block);

							}
							else {
								blocks[i].controller = cc->second;
							}
						}
						else
							Log::Info("Not Found!");
					}
					if (blocks[i].controller->IsDerivedType(NiTextureTransformController::TYPE)) {
						//if (blocks[i].propertyType == "BSLightingShaderProperty")
						//{
							map<NiTextureTransformControllerRef, NiFloatInterpControllerRef>::iterator cc = material_transform_controllers_map.find(DynamicCast<NiTextureTransformController>(blocks[i].controller));
							if (cc != material_transform_controllers_map.end())
							{
								if (cc->first->GetOperation() == TT_ROTATE)
								{
									NiTextureTransformControllerRef oldController = cc->first;
									NiFloatInterpControllerRef controller = cc->second;
									//convert, otherwise defer;
									NiFloatInterpControllerRef new_v_controller;
									NiInterpolatorRef newInterpolatorU;
									NiInterpolatorRef newInterpolatorV;

									convert_tt_rotate(oldController,
										StaticCast<NiFloatInterpController>(controller),
										blocks[i].interpolator,
										new_v_controller,
										newInterpolatorU,
										newInterpolatorV);

									NiBlendFloatInterpolatorRef f_ref = DynamicCast<NiBlendFloatInterpolator>(oldController->GetInterpolator());
									//u_controller->SetInterpolator(StaticCast<NiInterpolator>(controller->GetInterpolator()));
									NiBlendFloatInterpolatorRef v_ref = new NiBlendFloatInterpolator();
									v_ref->SetFlags(f_ref->GetFlags());
									Accessor<BlendUnknownSet>(f_ref, v_ref);
									v_ref->SetManagedControlled(f_ref->GetManagedControlled());
									v_ref->SetValue(f_ref->GetValue());

									new_v_controller->SetNextController(oldController->GetNextController());
									controller->SetNextController(StaticCast<NiTimeController>(new_v_controller));

									new_v_controller->SetInterpolator(StaticCast<NiInterpolator>(v_ref));

									ControlledBlock additional_v_block = blocks[i];
									blocks[i].controller = controller;
									blocks[i].interpolator = newInterpolatorU;
									blocks[i].controllerId = to_string(controller_id++);
									additional_v_block.controller = new_v_controller;
									additional_v_block.interpolator = newInterpolatorV;
									additional_v_block.controllerId = to_string(controller_id++);
									blocks.insert(blocks.begin() + i + 1, additional_v_block);

								}
								else
									blocks[i].controller = cc->second;
							}
						//}
						//else if (blocks[i].propertyType == "BSEffectShaderProperty") {

						//}
						else
							Log::Info("Not Found!");
					}
				}
				nisblock->SetControlledBlocks(blocks);
			}
		}
	}
	template<class T>
	inline void visit_object(T& obj) {}

	template<class T>
	inline void visit_compound(T& obj) {
	}

	//Actual Handling
	template<class T>
	inline void visit_field(T& field) {
	}


	NiFloatInterpControllerRef convert(NiTextureTransformControllerRef in) {
		if (!in == NULL && in->IsDerivedType(NiTextureTransformController::TYPE)) {
			NiTextureTransformControllerRef oldController = DynamicCast<NiTextureTransformController>(in);
			map<NiTextureTransformControllerRef, NiFloatInterpControllerRef>::iterator converted = material_transform_controllers_map.find(oldController);
			if (converted != material_transform_controllers_map.end()) {
				return converted->second;
			}
			else {
				BSLightingShaderPropertyFloatControllerRef controller = new BSLightingShaderPropertyFloatController();
				controller->SetNextController(oldController->GetNextController());
				controller->SetFlags(oldController->GetFlags());
				controller->SetFrequency(oldController->GetFrequency());
				controller->SetPhase(oldController->GetPhase());
				controller->SetStartTime(oldController->GetStartTime());
				controller->SetStopTime(oldController->GetStopTime());
				controller->SetTarget(oldController->GetTarget());
				controller->SetInterpolator(oldController->GetInterpolator());
				if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_U)
					controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_OFFSET);
				if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_V)
					controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_OFFSET);
				if (oldController->GetOperation() == TransformMember::TT_SCALE_U)
					controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_SCALE);
				if (oldController->GetOperation() == TransformMember::TT_SCALE_V)
					controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_SCALE);
				if (oldController->GetOperation() == TransformMember::TT_ROTATE)
				{
					controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_OFFSET);
					if (!oldController->GetInterpolator()->IsDerivedType(NiBlendInterpolator::TYPE))
					{
						//convert, otherwise defer;
						NiFloatInterpControllerRef new_v_controller; 
						NiInterpolatorRef newInterpolatorU; 
						NiInterpolatorRef newInterpolatorV;

						convert_tt_rotate(oldController,
							StaticCast<NiFloatInterpController>(controller),
							oldController->GetInterpolator(),
							new_v_controller,
							newInterpolatorU,
							newInterpolatorV);

						new_v_controller->SetNextController(oldController->GetNextController());
						controller->SetNextController(StaticCast<NiTimeController>(new_v_controller));

						controller->SetInterpolator(newInterpolatorU);
						new_v_controller->SetInterpolator(newInterpolatorV);
					}
				}

				material_transform_controllers_map[oldController] = controller;
				return controller;
			}
		}
	}


	template<>
	inline void visit_object(NiNode& obj) {
		vector<Ref<NiAVObject>> children = obj.GetChildren();
		vector<Ref<NiExtraData>> extras = obj.GetExtraDataList();
		int index = 0;

		//if (obj.GetName() == "Bip01 Head")
		//	obj.SetName(IndexString("NPC Head [Head]"));
		//else if (obj.GetName() == "Bip01 L Clavicle")
		//	obj.SetName(IndexString("NPC L Clavicle [LClv]"));
		//else if (obj.GetName() == "Bip01 R Clavicle")
		//	obj.SetName(IndexString("NPC R Clavicle [RClv]"));
		//else if (obj.GetName() == "Bip01 L UpperArmTwist")
		//	obj.SetName(IndexString("NPC L UpperarmTwist1 [LUt1]"));
		//else if (obj.GetName() == "Bip01 R UpperArmTwist")
		//	obj.SetName(IndexString("NPC R UpperarmTwist1 [RUt1]"));
		//else if (obj.GetName() == "Bip01 R ForearmTwist")
		//	obj.SetName(IndexString("NPC R ForearmTwist1 [RLt1]"));
		//else if (obj.GetName() == "Bip01 L ForearmTwist")
		//	obj.SetName(IndexString("NPC L ForearmTwist1 [LLt1]"));
		//else if (obj.GetName() == "Bip01 L UpperArm")
		//	obj.SetName(IndexString("NPC L UpperArm [LUar]"));
		//else if (obj.GetName() == "Bip01 R UpperArm")
		//	obj.SetName(IndexString("NPC R UpperArm [RUar]"));
		//else if (obj.GetName() == "Bip01 L Forearm")
		//	obj.SetName(IndexString("NPC L Forearm [LLar]"));
		//else if (obj.GetName() == "Bip01 R Forearm")
		//	obj.SetName(IndexString("NPC R Forearm [RLar]"));
		//else if (obj.GetName() == "Bip01 L Thigh") {
		//	obj.SetName(IndexString("NPC L Thigh [LThg]"));
		//	//obj.SetTranslation(Vector3(-6.615073f, 0.000394f, 68.911301f));
		//	//obj.SetRotation(Matrix33(
		//	//	-0.9943f, -0.0379f, 0.0999f,
		//	//	-0.0414f, 0.9986f, -0.0329f,
		//	//	-0.0985f, -0.0369f, -0.9944f));
		//}
		//else if (obj.GetName() == "Bip01 R Thigh") {
		//	obj.SetName(IndexString("NPC R Thigh [RThg]"));
		//	//obj.SetTranslation(Vector3(6.615073f, 0.000394f, 68.911301f));
		//	//obj.SetRotation(Matrix33(
		//	//	-0.9943f, 0.379f, -0.999f,
		//	//	0.0414f, 0.9986f, -0.0329f,
		//	//	0.0985f, -0.0368f, -0.9945f));
		//}
		//else if (obj.GetName() == "Bip01 L Hand")
		//	obj.SetName(IndexString("NPC L Hand [LHnd]"));
		//else if (obj.GetName() == "Bip01 R Hand")
		//	obj.SetName(IndexString("NPC R Hand [RHnd]"));
		//else if (obj.GetName() == "Bip01 Spine") {
		//	obj.SetName(IndexString("NPC Spine [Spn0]"));
		//	//obj.SetTranslation(Vector3(0.000007f, -5.239862f, 72.702919f));
		//	//obj.SetRotation(Matrix33(
		//	//	1.0f, -0.00f, 0.0f,
		//	//	0.0f, 0.9980f, -0.0436f,
		//	//	-0.00f, 0.0436f, 0.9990f));
		//}
		//else if (obj.GetName() == "Bip01 Spine1") {
		//	obj.SetName(IndexString("NPC Spine1 [Spn1]"));
		//	//obj.SetTranslation(Vector3(0.000006f, -4.858528f, 81.443321f));
		//	//obj.SetRotation(Matrix33(
		//	//	1.0f, -0.001f, 0.0f,
		//	//	0.0f, 0.9942f, 0.1071f,
		//	//	-0.001f, -0.1071f, 0.9942f));
		//}
		//else if (obj.GetName() == "Bip01 Spine2")
		//	obj.SetName(IndexString("NPC Spine2 [Spn2]"));
		//else if (obj.GetName() == "Bip01 Pelvis") {
		//	obj.SetName(IndexString("NPC Pelvis [Pelv]"));
		//	obj.SetTranslation(Vector3(-0.000003f, -0.000010f, 68.911301f));
		//	obj.SetRotation(Matrix33());
		//}
		//else if (obj.GetName() == "Bip01 Neck1")
		//	obj.SetName(IndexString("NPC Neck [Neck]"));

		auto it = children.begin();
		while (it != children.end())
		{
			NiAVObjectRef& block = *it;
			if (block == NULL) {
				it = children.erase(it);
				continue;
			}
			//daedricshrinehircine01.nif, obsolete
			if (block->IsSameType(NiDirectionalLight::TYPE)) {
				it = children.erase(it);
				continue;
			}
			//daedricshrinesanguine01.nif, obsolete
			if (block->IsSameType(NiAmbientLight::TYPE)) {
				it = children.erase(it);
				continue;
			}
			if (block->IsSameType(NiTriStrips::TYPE)) {
				NiTriStripsRef stripsRef = DynamicCast<NiTriStrips>(block);
				if (stripsRef->IsSameType(NiTriStrips::TYPE)) {
					NiTriShapeRef shape = convert_strip(stripsRef);
					children[index] = shape;
				}
			}
			//if (block->IsSameType(NiParticleSystem::TYPE)) {
			//	NiParticleSystemRef stripsRef = DynamicCast<NiParticleSystem>(block);
			//	visit_particle(*stripsRef, obj);
			//}
			it++;
			index++;
		}
		index = 0;
		//need to do furnitures here, we need to change BSFurnitureMarker to BSFurnitureMarkerNode
		for (auto it = extras.begin(); it != extras.end(); it) {
			if (*it == NULL)
				it = extras.erase(it);
			else
				it++;
		}

		for (NiExtraDataRef extra : extras)
		{
			if (extra->IsSameType(BSFurnitureMarker::TYPE)) {
				BSFurnitureMarkerRef oldNode = DynamicCast<BSFurnitureMarker>(extra);
				BSFurnitureMarkerNodeRef newNode = new BSFurnitureMarkerNode();
				newNode->SetName(IndexString("FRN")); //fix for furniture nodes with names which isn't FRN
				vector<FurniturePosition> newpositions;
				vector<FurniturePosition> positions = oldNode->GetPositions();

				for (FurniturePosition pos : positions)
				{
					FurniturePosition newpos = FurniturePosition();
					newpos.offset = pos.offset; //unudes by havok animation. Z offset is bad in general
					newpos.offset.z += 35; // For CK
					newpos.offset.z -= _translation.z; //Again for CK

					if (pos.positionRef1 == 1) {
						newpos.animationType = AnimationType::SLEEP;
						newpos.entryProperties = FurnitureEntryPoints::LEFT;
						newpos.offset.x -= -90.826172f;
					}
					if (pos.positionRef1 == 2) {
						newpos.animationType = AnimationType::SLEEP;
						newpos.entryProperties = FurnitureEntryPoints::RIGHT;
						newpos.offset.x -= 90.826172f;
					}
					if (pos.positionRef1 == 3) {
						newpos.animationType = AnimationType::SLEEP;
						newpos.entryProperties = FurnitureEntryPoints::RIGHT;
					}
					if (pos.positionRef1 == 4) {
						newpos.animationType = AnimationType::SLEEP;
						newpos.entryProperties = FurnitureEntryPoints::BEHIND;
					}
					if (pos.positionRef1 == 11) {
						newpos.animationType = AnimationType::SIT;
						newpos.entryProperties = FurnitureEntryPoints::LEFT;
						newpos.offset.x -= -51.330994f;
					}
					if (pos.positionRef1 == 12) {
						newpos.animationType = AnimationType::SIT;
						newpos.entryProperties = FurnitureEntryPoints::RIGHT;
						newpos.offset.x -= 51.826050f;
					}
					if (pos.positionRef1 == 13) {
						newpos.animationType = AnimationType::SIT;
						newpos.entryProperties = FurnitureEntryPoints::BEHIND;
						newpos.offset.y -= -54.735596f;
					}
					if (pos.positionRef1 == 14) {
						newpos.animationType = AnimationType::SIT;
						newpos.entryProperties = FurnitureEntryPoints::FRONT;
						newpos.offset.y -= 55.295258f;
					}
					newpositions.push_back(newpos);
				}
				newNode->SetPositions(newpositions);
				if (extras.size() > 1) //fix markermatsideentry.nif vector crash.
					index++;
				extras[index] = DynamicCast<BSFurnitureMarkerNode>(newNode);
			}
		}
		
		//FLAMES!
		string flame_name = "FlameNode";
		if (obj.GetName().find(flame_name) == 0) {
			int flame_num = 0;
			string flame_type = obj.GetName().substr(flame_name.size(), obj.GetName().size() - flame_name.size());
			switch (flame_type.at(0)) {
			case 'A': //10
			case 'B': //11
			case 'C': //12
			case 'D': //13
			case 'E': //14
			case 'F': //15
			case 'G': //16
			case 'H': //17
			case 'I': //18
			case 'J': //19
			case 'K': //20
			case '0':
				flame_num = 49;
				break;
			case '2':
				flame_num = 46;
				break;
			default:
				flame_num = 49;
				break;
			}

			auto parents = find_parents(StaticCast<NiObject>(&obj));
			parents.push_back(StaticCast<NiObject>(&obj));
			hkTransform world_transform; world_transform.setIdentity();
			for (auto& avobj : parents) {
				if (avobj->IsDerivedType(NiAVObject::TYPE)) {
					NiAVObjectRef av = DynamicCast<NiAVObject>(avobj);
					hkTransform local = TOHKTRANSFORM(av->GetRotation(), av->GetTranslation());
					world_transform.setMulEq(local);
				}
			}
			BSValueNodeRef flame_node = new BSValueNode();
			std::string suffix = obj.GetName().substr(std::string("FlameNode").size(), obj.GetName().size() - std::string("FlameNode").size());
			flame_node->SetName(string("AddOnNode")+to_string(flame_num) + suffix);
			flame_node->SetFlags(524302);
			flame_node->SetTranslation({ 0., 0.,0. });
			if (flame_num == 46)
				flame_node->SetTranslation({ 0., 8.,0. });
			hkRotation rot = world_transform.getRotation();
			rot.transpose();
			flame_node->SetRotation(
				Matrix33(
					rot(0, 0), rot(0, 1), rot(0, 2),
					rot(1, 0), rot(1, 1), rot(1, 2),
					rot(2, 0), rot(2, 1), rot(2, 2)
				)
			);
			flame_node->SetScale(1.);
			flame_node->SetValue(flame_num);
			flame_node->SetValueNodeFlags((BSValueNodeFlags)0);
			children.push_back(StaticCast<NiAVObject>(flame_node));
		}
		
		//TODO
		//properties are deprecated
		obj.SetProperties(vector<NiPropertyRef>{});
		obj.SetChildren(children);
		obj.SetExtraDataList(extras);
		//Not supported anymore. seems like dead exporter stuff like NiDirectionaLight
		//Crashes SE
		obj.SetEffects({});
	}

	template<>
	inline void visit_object(NiBillboardNode& obj) {
		vector<Ref<NiAVObject>> children = obj.GetChildren();
		vector<Ref<NiProperty>> properties = obj.GetProperties();
		int index = 0;
		for (NiAVObjectRef& block : children)
		{
			if (block == NULL) {
				children.erase(children.begin() + index);
				continue;
			}
			if (block->IsSameType(NiDirectionalLight::TYPE)) {
				children.erase(children.begin() + index);
				continue;
			}
			if (block->IsSameType(NiTriStrips::TYPE)) {
				NiTriStripsRef stripsRef = DynamicCast<NiTriStrips>(block);
				if (stripsRef->IsSameType(NiTriStrips::TYPE)) {
					NiTriShapeRef shape = convert_strip(stripsRef);
					if (obj.GetProperties().size() != 0) {
						vector<Ref<NiProperty>> sproperties = shape->GetProperties();
						sproperties.push_back(properties[0]);
						shape->SetProperties(sproperties);
					}
					children[index] = shape;
				}
			}

			index++;
		}
		if (obj.GetBillboardMode() == ROTATE_ABOUT_UP)
			obj.SetBillboardMode(BSROTATE_ABOUT_UP);
		obj.SetProperties(vector<NiPropertyRef>{});
		obj.SetExtraDataList(vector<NiExtraDataRef>{});
		obj.SetChildren(children);
	}

	template<>
	inline void visit_object(NiTriShapeData& obj) {
		VectorFlags vf = obj.GetVectorFlags();
		BSVectorFlags bvf = obj.GetBsVectorFlags();
		if (vf & VF_UV_2 || /*!< VF_UV_2 */
			vf & VF_UV_4 || /*!< VF_UV_4 */
			vf & VF_UV_8 || /*!< VF_UV_8 */
			vf & VF_UV_16 || /*!< VF_UV_16 */
			vf & VF_UV_32 /*!< VF_UV_32 */)
		{
			throw runtime_error("VF Unhandled");
		}

		if (vf != 0) {
			obj.SetBsVectorFlags(static_cast<BSVectorFlags>(obj.GetBsVectorFlags() | obj.GetVectorFlags()));
		}

		vector<Vector3>& vertices = obj.GetVertices();
		vector<Triangle>& faces = obj.GetTriangles();
		vector<Vector3>& normals = obj.GetNormals();
		Vector3 COM;
		if (vertices.size() != 0)
			COM = (COM / 2) + (ckcmd::Geometry::centeroid(vertices) / 2);
		if (normals.size() != vertices.size() && (faces.empty() || obj.GetUvSets().empty()))
		{
			//normals are needed for sse, let's just fill the void here
			normals.resize(vertices.size());
			for (int i = 0; i< vertices.size(); i++)
			{
				normals[i] = COM - vertices[i];
				normals[i] = normals[i].Normalized();
			}
			obj.SetHasNormals(true);
			obj.SetNormals(normals);
		}
		if (vertices.size() != 0 && faces.size() != 0 && obj.GetUvSets().size() != 0) {
			vector<TexCoord> uvs = obj.GetUvSets()[0];
			//Seems that tangent space calculation wants the uv flipped
			for (auto& uv : uvs)
			{
				float u = uv.u;
				uv.u = uv.v;
				uv.v = u;
			}
			TriGeometryContext g(vertices, COM, faces, uvs, normals);
			obj.SetHasNormals(1);
			//recalculate
			obj.SetNormals(g.normals);
			obj.SetTangents(g.tangents);
			obj.SetBitangents(g.bitangents);
			if (vertices.size() != g.normals.size() || vertices.size() != g.tangents.size() || vertices.size() != g.bitangents.size())
				throw runtime_error("Geometry mismatch!");
			obj.SetBsVectorFlags(static_cast<BSVectorFlags>(obj.GetBsVectorFlags() | BSVF_HAS_TANGENTS));
		}

		//grovestatue01.nif v10.0.0.2
		obj.SetHasTriangles(faces.size() > 0);

		//TODO: shared normals no more supported
		obj.SetMatchGroups(vector<MatchGroup>{});
	}

	void setMaterialProperties(BSLightingShaderProperty& property, NiMaterialPropertyRef material, bool& hasOwnEmit)
	{
		property.SetEmissiveColor(material->GetEmissiveColor());
		property.SetSpecularColor(material->GetSpecularColor());
		property.SetEmissiveMultiple(material->GetEmissiveMult());
		property.SetGlossiness(material->GetGlossiness());
		property.SetAlpha(material->GetAlpha());


		//TODO: check alpha property!

		Color3 em = material->GetEmissiveColor();
		if (em.r > 0.0 || em.g > 0.0 || em.b > 0)
			hasOwnEmit = true;
	}

	void setMaterialProperties(BSEffectShaderProperty& property, NiMaterialPropertyRef material, bool& hasOwnEmit)
	{
		//TODO: maybe use vc for the rest?
		Color3 emissive = material->GetEmissiveColor();
		property.SetEmissiveColor({emissive.r, emissive.g, emissive.b, material->GetAlpha()});
		property.SetEmissiveMultiple(material->GetEmissiveMult());

		Color3 em = material->GetEmissiveColor();
		if (em.r > 0.0 || em.g > 0.0 || em.b > 0)
			hasOwnEmit = true;
	}

	bool findTextureFromBSA(const string& name)
	{
		Games& games = Games::Instance();
		for (const auto& bsa_file : games.bsa_files()) {
			if (bsa_file.find(name)) {
				return true;
			}
		}
		return false;
	}

	static string replace_all(const string& source, const string& pattern, const string& new_pattern)
	{
		string result = source;
		string lower_source = source;
		std::transform(lower_source.begin(), lower_source.end(), lower_source.begin(), ::tolower);
		string lower_pattern = pattern;
		std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
		string::size_type n = 0;
		while ((n = lower_source.find(lower_pattern, n)) != string::npos)
		{
			lower_source.replace(n, pattern.size(), new_pattern);
			result.replace(n, pattern.size(), new_pattern);
			n += new_pattern.size();
		}
		return result;
	}

	void convertTexturingProperies()
	{

	}


	template<>
	inline void visit_object(NiTriShape& obj) {
		bool hasSpecular = false;
		bool hasZbuffer = false;
		bool hasGlow = false;
		bool hasOwnEmit = false;
		bool hasEnv = false;
		bool hasRefract = false;
		BSShaderPropertyRef lightingProperty; // = new BSLightingShaderProperty();
		BSShaderTextureSetRef textureSet = new BSShaderTextureSet();
		NiMaterialPropertyRef material = new NiMaterialProperty();
		NiTexturingPropertyRef texturing = new NiTexturingProperty();
		vector<Ref<NiProperty>> properties = obj.GetProperties();

		SkyrimShaderPropertyFlags1 shader1;
		SkyrimShaderPropertyFlags2 shader2;
		bool hasAlpha = false;

		//check vc
		if (obj.GetData() != NULL)
		{
			const auto& vcs = obj.GetData()->GetVertexColors();
			for (const auto & vc : vcs)
			{
				if (vc.a < 1.0)
				{
					hasAlpha = true;
					break;
				}
			}
		}

		bool hasUV = obj.GetData() != NULL && obj.GetData()->GetUvSets().size() > 0;
		if (hasUV)
		{
			BSLightingShaderProperty* temp = new BSLightingShaderProperty();			
			shader1 = temp->GetShaderFlags1_sk();
			shader2 = temp->GetShaderFlags2_sk();
			lightingProperty = temp;
		}
		else
		{
			BSEffectShaderProperty* temp = new BSEffectShaderProperty();
			shader1 = temp->GetShaderFlags1_sk();
			shader2 = temp->GetShaderFlags2_sk();
			lightingProperty = temp;
		}


		bool niflip = false;
		for (NiPropertyRef property : properties)
		{
			if (property->IsSameType(NiMaterialProperty::TYPE)) {
				material = DynamicCast<NiMaterialProperty>(property);

				if (lightingProperty->IsSameType(BSLightingShaderProperty::TYPE))
					setMaterialProperties(*DynamicCast<BSLightingShaderProperty>(lightingProperty), material, hasOwnEmit);
				else
					setMaterialProperties(*DynamicCast<BSEffectShaderProperty>(lightingProperty), material, hasOwnEmit);

				if (material->GetName().find("EnvMap") != string::npos ||
					material->GetGlossiness() > 10.0) {
					hasEnv = true;
				}


				//TODO:: Create some kind of recursive method to modify GetNextControllers.
				if (material->GetController() != NULL) {
					if (material->GetController()->IsSameType(NiMaterialColorController::TYPE)) {
						NiMaterialColorControllerRef oldController = DynamicCast<NiMaterialColorController>(material->GetController());
						NiPoint3InterpControllerRef controller;
						if (lightingProperty->IsSameType(BSLightingShaderProperty::TYPE)) {
							BSLightingShaderPropertyColorControllerRef typed_controller = new BSLightingShaderPropertyColorController();
							if (oldController->GetTargetColor() == MaterialColor::TC_SELF_ILLUM)
								typed_controller->SetTypeOfControlledColor(LightingShaderControlledColor::LSCC_EMISSIVE_COLOR);
							controller = typed_controller;
						}
						else {
							BSEffectShaderPropertyColorControllerRef typed_controller = new BSEffectShaderPropertyColorController();
							if (oldController->GetTargetColor() == MaterialColor::TC_SELF_ILLUM)
								typed_controller->SetTypeOfControlledColor(EffectShaderControlledColor::ECSC_EMISSIVE_COLOR);
						}
						controller->SetFlags(oldController->GetFlags());
						controller->SetFrequency(oldController->GetFrequency());
						controller->SetPhase(oldController->GetPhase());
						controller->SetStartTime(oldController->GetStartTime());
						controller->SetStopTime(oldController->GetStopTime());
						controller->SetTarget(lightingProperty);
						controller->SetInterpolator(oldController->GetInterpolator());

						//constructor sets to specular.
						//Seems this also likes a niblendfloat
						if (lightingProperty->IsSameType(BSLightingShaderProperty::TYPE))
						{
							//We're gonna leave this here and rewire it later
							NiFloatInterpolatorRef interpolator = new NiFloatInterpolator();
							NiFloatDataRef data = new NiFloatData();
							KeyGroup<float > tkeys;
							Key<float> start;
							start.time = oldController->GetStartTime();
							start.data = 1.;
							Key<float> stop;
							stop.time = oldController->GetStopTime();
							start.data = 1.;
							tkeys.keys.push_back(start);
							tkeys.keys.push_back(stop);
							interpolator->SetData(data);


							BSLightingShaderPropertyFloatControllerRef additional_controller = new BSLightingShaderPropertyFloatController();
							additional_controller->SetFlags(oldController->GetFlags());
							additional_controller->SetFrequency(oldController->GetFrequency());
							additional_controller->SetPhase(oldController->GetPhase());
							additional_controller->SetStartTime(oldController->GetStartTime());
							additional_controller->SetStopTime(oldController->GetStopTime());
							additional_controller->SetTarget(lightingProperty);
							additional_controller->SetInterpolator(StaticCast<NiInterpolator>(interpolator));
							additional_controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_EMISSIVE_MULTIPLE);




							controller->SetNextController(StaticCast<NiTimeController>(additional_controller));
						}
						lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
						material_controllers_map[oldController] = controller;
					}
					if (material->GetController()->IsSameType(NiAlphaController::TYPE)) {
						NiAlphaControllerRef oldController = DynamicCast<NiAlphaController>(material->GetController());
						BSLightingShaderPropertyFloatControllerRef controller = new BSLightingShaderPropertyFloatController();
						controller->SetFlags(oldController->GetFlags());
						controller->SetFrequency(oldController->GetFrequency());
						controller->SetPhase(oldController->GetPhase());
						controller->SetStartTime(oldController->GetStartTime());
						controller->SetStopTime(oldController->GetStopTime());
						controller->SetTarget(lightingProperty);
						controller->SetInterpolator(oldController->GetInterpolator());
						controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_ALPHA);

						lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
						material_alpha_controllers_map[oldController] = controller;
					}
				}
			}

			/*NifSkope Texture "Slots"
				Slot 1: Diffuse Texture(*.dds)
				Slot 2 : Normal Map(*_n.dds)
				Slot 3 : Glow Map(*_g.dds)
				Slot 4 : parallax ()
				Slot 5 : Effects map (*_e.dds)
				Slot 6 : Environment Mask Map (*_m.dds)*/

			if (property->IsSameType(NiTexturingProperty::TYPE)) {
				texturing = DynamicCast<NiTexturingProperty>(property);
				string textureName;
				if (texturing->GetHasGlowTexture() == true)
					hasGlow = true;
				if (texturing->GetBaseTexture().source != NULL) {
					textureName += texturing->GetBaseTexture().source->GetFileName();
					//fix for orconebraid
					if (textureName == "Grey.dds" || textureName == "grey.dds")
						textureName = "textures\\characters\\hair\\Grey.dds";

					if (textureName.find("Refract.dds") != std::string::npos)
						hasRefract = true;

					string basename = textureName;
					basename.erase(basename.end() - 4, basename.end());
					if (basename.rfind('_') != string::npos)
						basename.erase(basename.begin() + basename.rfind('_'), basename.end());

					string override_base = basename;
					override_base.insert(9, "tes4\\");

					textureName.insert(9, "tes4\\");
					string textureOverrideNormal = override_base + "_n.dds";
					string textureNormal = basename + "_n.dds";

					//setup textureSet (TODO)
					std::vector<std::string> textures(9);
					textures[0] = textureName;

					Games& games = Games::Instance();
					const Games::GamesPathMapT& installations = games.getGames();

					if ( (games.isGameInstalled(Games::TES5) && fs::exists(games.data(Games::TES5) / textureOverrideNormal))
						|| (games.isGameInstalled(Games::TES5SE) && fs::exists(games.data(Games::TES5SE) / textureOverrideNormal))
						|| (games.isGameInstalled(Games::TES4) && fs::exists(games.data(Games::TES4) / textureNormal))
						|| findTextureFromBSA(textureNormal))
						textures[1] = textureOverrideNormal;
					else
						textures[1] = "textures\\default_n.dds";

					//finally set them.
					textureSet->SetTextures(textures);
				}

				if (textureSet->GetTextures().size() > 0)
				{
					vector<string>& textures = textureSet->GetTextures();
					string texture_glow = textures[0];
					texture_glow.erase(texture_glow.end() - 4, texture_glow.end());
					if (texture_glow.rfind('_') != string::npos)
						texture_glow.erase(texture_glow.begin() + texture_glow.rfind('_'), texture_glow.end());
					texture_glow += "_g.dds";

					string texture_glow_bsa = replace_all(texture_glow, "tes4\\", "");

					if ((games.isGameInstalled(Games::TES5) && fs::exists(games.data(Games::TES5) / texture_glow))
						|| (games.isGameInstalled(Games::TES5SE) && fs::exists(games.data(Games::TES5SE) / texture_glow))
						|| (games.isGameInstalled(Games::TES4) && fs::exists(games.data(Games::TES4) / texture_glow_bsa))
						|| findTextureFromBSA(texture_glow_bsa))
					{
						hasGlow = true;
						if (games.isGameInstalled(Games::TES4) && fs::exists(games.data(Games::TES4) / texture_glow_bsa) ||
							findTextureFromBSA(texture_glow_bsa))
							convertGlowMap(texture_glow_bsa, _export_path.string());
					}
				}


				if (texturing->GetController() == NULL)
					continue;

				if (texturing->GetController()->IsSameType(NiFlipController::TYPE)) {
					niflip = true;
					//FlipController packs up sprites, but skyrim doesn't support that.
					//instead, we have to load all the images into one and transform this into an UV animation.
					NiFlipControllerRef oldController = DynamicCast<NiFlipController>(texturing->GetController());
					float speed = 1.;
					auto blend = DynamicCast<NiBlendFloatInterpolator>(oldController->GetInterpolator());
					if (NULL != blend && blend->GetFlags() == 3)
					{
						speed = (float)Accessor<NiBlendFloatInterpolator>(*blend).play_speed;
					}
					FlipBookConverter conv(oldController, lightingProperty, hasGlow, _export_path.string());

					if (!conv.uv_atlas.empty())
					{
						//if (deferred_blends.find(oldController) != deferred_blends.end())
						//	printf("lol");
						deferred_blends[oldController] = { speed, conv.uv_atlas };
					}
					material_flip_controllers_map[oldController] = DynamicCast<NiFloatInterpController>(lightingProperty->GetController());
				}

				if (texturing->GetController()->IsSameType(NiTextureTransformController::TYPE)) {
					NiTextureTransformControllerRef oldController = DynamicCast<NiTextureTransformController>(texturing->GetController());
					NiFloatInterpControllerRef controller = convert(oldController);
					controller->SetTarget(lightingProperty);
					lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
					while (controller->GetNextController() != NULL) {
						oldController = DynamicCast<NiTextureTransformController>(controller->GetNextController());
						if (oldController != NULL) {
							controller = convert(oldController);
						}
						else {
							controller = DynamicCast<BSLightingShaderPropertyFloatController>(controller->GetNextController());
						}
						if (controller != NULL) {
							controller->SetTarget(lightingProperty);
						}
						else
							break;
					}
				}
			}
			if (property->IsSameType(NiStencilProperty::TYPE)) {
				shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 + SkyrimShaderPropertyFlags2::SLSF2_DOUBLE_SIDED);
			}
			if (property->IsSameType(NiAlphaProperty::TYPE)) {
				NiAlphaPropertyRef alpha = new NiAlphaProperty();
				alpha->SetFlags(DynamicCast<NiAlphaProperty>(property)->GetFlags());
				alpha->SetThreshold(DynamicCast<NiAlphaProperty>(property)->GetThreshold());

				//WRONG!
				//check if both blending and testing are set
				//alpha_flags a_flags; a_flags.value = alpha->GetFlags();
				//if (a_flags.bits.alpha_test_enable && a_flags.bits.color_blending_enable)
				//{
				//	//prefer test
				//	a_flags.bits.color_blending_enable = 0;
				//	alpha->SetFlags(a_flags.value);
				//}

				alpha_flags a_flags; a_flags.value = alpha->GetFlags();
				string texture_diffuse = "";
				if (a_flags.bits.color_blending_enable && a_flags.bits.readsAlphaSource() && !niflip)
				{
					//check if diffuse has alpha
					vector<string>& textures = textureSet->GetTextures();
					if (textures.empty()) {
						for (NiPropertyRef property : properties)
						{
							if (property->IsSameType(NiTexturingProperty::TYPE)) {
								texturing = DynamicCast<NiTexturingProperty>(property);
								if (texturing->GetBaseTexture().source != NULL) {
									texture_diffuse = texturing->GetBaseTexture().source->GetFileName();
									break;
								}
							}
						}
					}
					else {
						texture_diffuse = textures[0];
						texture_diffuse = replace_all(texture_diffuse, "tes4\\", "");
					}
					if (!texture_diffuse.empty())
					{
						checkDiffuseAlpha(texture_diffuse, _export_path.string());
					}
					else {
						Log::Error("Alpha blend with no textures! Model will CTD");
						//avoid crash
							a_flags.bits.color_blending_enable = 0;
							alpha->SetFlags(a_flags.value);
					}
				}

				obj.SetAlphaProperty(alpha);
			}
			if (property->IsSameType(NiSpecularProperty::TYPE)) {
				hasSpecular = true;
				hasEnv = true;
			}
			if (property->IsSameType(NiZBufferProperty::TYPE)) {
				hasZbuffer = true;
				NiZBufferPropertyRef buffer = DynamicCast<NiZBufferProperty>(property);
			}
		}
		NiTriShapeDataRef data = DynamicCast<NiTriShapeData>(obj.GetData());
		if (data != NULL) {

			if (!data->GetVertexColors().empty()) {
				data->SetHasVertexColors(true);
				shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 | SkyrimShaderPropertyFlags2::SLSF2_VERTEX_COLORS);
				if (hasAlpha)
					shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_VERTEX_ALPHA);
			}
			else {
				if (hasOwnEmit || hasGlow)
				{
					data->SetHasVertexColors(true);
					shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 | SkyrimShaderPropertyFlags2::SLSF2_VERTEX_COLORS);
					std::vector<Niflib::Color4>  value(data->GetVertices().size(), { 1.,1.,1.,1. });
					data->SetVertexColors(value);
				}
				else {
					data->SetHasVertexColors(false);
					shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 & ~SkyrimShaderPropertyFlags2::SLSF2_VERTEX_COLORS);
				}
			}
		}
		if (!hasSpecular)
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 &  ~SkyrimShaderPropertyFlags1::SLSF1_SPECULAR);
		else
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1  | SkyrimShaderPropertyFlags1::SLSF1_SPECULAR);
		
		if (hasZbuffer) {
			shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 & ~SkyrimShaderPropertyFlags2::SLSF2_ZBUFFER_WRITE);
		}

		if (hasRefract) {
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_REFRACTION);
		}

		if (obj.GetSkinInstance() != NULL)
		{
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_SKINNED);
		}

		//OWN EMIT is required for any form of glow. glow map eventually tones it up or down. Thanks Candoran2
		if (!hasGlow)
		{
			shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 & ~SkyrimShaderPropertyFlags2::SLSF2_GLOW_MAP);
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 & ~SkyrimShaderPropertyFlags1::SLSF1_EXTERNAL_EMITTANCE);
		}
		else {
			shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 | SkyrimShaderPropertyFlags2::SLSF2_GLOW_MAP);
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_EXTERNAL_EMITTANCE);
		}

		if (hasOwnEmit)
		{
			//Own emission need glow map shader flaf even if it is not a glow
			shader2 = static_cast<SkyrimShaderPropertyFlags2>(shader2 | SkyrimShaderPropertyFlags2::SLSF2_GLOW_MAP);
		}

		if (hasOwnEmit || hasGlow) {
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_OWN_EMIT);
		}
		else {
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 & ~SkyrimShaderPropertyFlags1::SLSF1_OWN_EMIT);
		}

		if (!hasGlow && !hasOwnEmit && hasEnv) {
			shader1 = static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_ENVIRONMENT_MAPPING);
		}

		if (lightingProperty->IsSameType(BSLightingShaderProperty::TYPE))
		{
			BSLightingShaderPropertyRef ls = DynamicCast<BSLightingShaderProperty>(lightingProperty);
			if (textureSet->GetTextures().size() == 0)
				obj.SetFlags(obj.GetFlags() + 1);


			//hasGlow is unused, just check for its existance
			if (hasGlow && textureSet->GetTextures().size()>0)
			{
				vector<string>& textures = textureSet->GetTextures();
				string texture_glow = textures[0];
				texture_glow.erase(texture_glow.end() - 4, texture_glow.end());
				texture_glow += "_g.dds";

				textures[2] = texture_glow;
				textureSet->SetTextures(textures);

				ls->SetSkyrimShaderType(ST_GLOW_SHADER);
			}

			if (hasOwnEmit) {
				ls->SetSkyrimShaderType(ST_GLOW_SHADER);		
			}

			if (!hasGlow && !hasOwnEmit && hasEnv) {

				ls->SetSkyrimShaderType(ST_ENVIRONMENT_MAP);
				vector<string>& textures = textureSet->GetTextures();

				textures[4] = "textures\\cubemaps\\ShinyGlass_e.dds"; //TODO: material
				textures[5] = textures[0]; //use diffuse as env;
				ls->SetEnvironmentMapScale(0.3); //decent value;
				textureSet->SetTextures(textures);
			}

			ls->SetTextureSet(textureSet);
			ls->SetShaderFlags1_sk(shader1);
			ls->SetShaderFlags2_sk(shader2);
		}
		else {
			BSEffectShaderPropertyRef ls = DynamicCast<BSEffectShaderProperty>(lightingProperty);
			if (textureSet!= NULL && textureSet->GetTextures().size()>0)
				ls->SetSourceTexture(textureSet->GetTextures()[0]);
			ls->SetShaderFlags1_sk(shader1);
			ls->SetShaderFlags2_sk(shader2);
			ls->SetShaderFlags1_sk(static_cast<SkyrimShaderPropertyFlags1>(shader1 | SkyrimShaderPropertyFlags1::SLSF1_ZBUFFER_TEST));

		}
		obj.SetShaderProperty(lightingProperty);
		obj.SetExtraDataList(vector<Ref<NiExtraData>> {});
		obj.SetProperties(vector<Ref<NiProperty>> {});

		if (obj.GetSkinInstance() != NULL) {
			NiSkinInstanceRef skinInstance = DynamicCast<NiSkinInstance>(obj.GetSkinInstance());
			NiSkinDataRef skinData = DynamicCast<NiSkinData>(skinInstance->GetData());
			NiSkinPartitionRef skinPartition = DynamicCast<NiSkinPartition>(skinInstance->GetSkinPartition());
			if (skinPartition != NULL)
			{
				vector<SkinPartition> partitionBlocks = skinPartition->GetSkinPartitionBlocks();


				for (int i = 0; i != partitionBlocks.size(); i++) {
					SkinPartition* block = &partitionBlocks[i];
					if (block->numStrips > 0)
					{
						//if (block->numTriangles > 0)
						//{
						//	throw runtime_error("Found mixed strips and triangles, unsupported!");
						//}
						auto triangles = triangulate(block->strips);
						block->triangles.insert(block->triangles.end(), triangles.begin(), triangles.end());
						block->numTriangles = block->triangles.size();
						block->numStrips = 0;
						block->strips = vector<vector<unsigned short>>(0);
						block->stripLengths = vector<unsigned short>(0);
					}
				}

				skinPartition->SetSkinPartitionBlocks(partitionBlocks);
				skinInstance->SetSkinPartition(skinPartition);
				obj.SetSkinInstance(skinInstance);
			}
			else {
				Log::Error("Skin Instance without partitions!");
			}
		}

		if (data->GetVertices().size() != 0 && data->GetTriangles().size() != 0 && data->GetUvSets().size() != 0) {
			update_tangentspace(data);
		}
	}

	//inline void visit_particle(NiParticleSystem& obj, NiAVObject& parent) {
	template<>
	inline void visit_object(NiParticleSystem& obj) {
		bool hasSpecular = false;
		bool hasZBuffer = false;
		bool hasOwnEmit = false;
		bool hasGlow = false;
		BSEffectShaderPropertyRef lightingProperty = new BSEffectShaderProperty();
		//BSShaderTextureSetRef textureSet = new BSShaderTextureSet();
		NiMaterialPropertyRef material = new NiMaterialProperty();
		NiTexturingPropertyRef texturing = new NiTexturingProperty();
		vector<Ref<NiProperty>> properties = obj.GetProperties();

		for (NiPropertyRef property : properties)
		{
			if (property->IsSameType(NiMaterialProperty::TYPE)) {
				material = DynamicCast<NiMaterialProperty>(property);
				lightingProperty->SetShaderType(BSShaderType::SHADER_DEFAULT);
				Color3 em = material->GetEmissiveColor();
				lightingProperty->SetEmissiveColor(Color4(em.r, em.g, em.b, 1.0));
				if (em.r > 0.0 || em.g > 0.0 || em.b > 0)
					hasOwnEmit = true;
				//lightingProperty->SetSpecularColor(material->GetSpecularColor());
				lightingProperty->SetEmissiveMultiple(1);
				//lightingProperty->SetGlossiness(material->GetGlossiness());
				//lightingProperty->SetAlpha(material->GetAlpha());

				//TODO:: Create some kind of recursive method to modify GetNextControllers.
				if (material->GetController() != NULL) {
					if (material->GetController()->IsSameType(NiMaterialColorController::TYPE)) {
						NiMaterialColorControllerRef oldController = DynamicCast<NiMaterialColorController>(material->GetController());
						BSEffectShaderPropertyColorControllerRef controller = new BSEffectShaderPropertyColorController();
						controller->SetFlags(oldController->GetFlags());
						controller->SetFrequency(oldController->GetFrequency());
						controller->SetPhase(oldController->GetPhase());
						controller->SetStartTime(oldController->GetStartTime());
						controller->SetStopTime(oldController->GetStopTime());
						controller->SetTarget(lightingProperty);
						controller->SetInterpolator(oldController->GetInterpolator());
						if (oldController->GetTargetColor() == MaterialColor::TC_SELF_ILLUM)
							controller->SetTypeOfControlledColor(EffectShaderControlledColor::ECSC_EMISSIVE_COLOR);
						//constructor sets to specular.

						lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
						material_controllers_map[oldController] = controller;
					}
					if (material->GetController()->IsSameType(NiAlphaController::TYPE)) {
						NiAlphaControllerRef oldController = DynamicCast<NiAlphaController>(material->GetController());
						BSEffectShaderPropertyFloatControllerRef controller = new BSEffectShaderPropertyFloatController();
						controller->SetFlags(oldController->GetFlags());
						controller->SetFrequency(oldController->GetFrequency());
						controller->SetPhase(oldController->GetPhase());
						controller->SetStartTime(oldController->GetStartTime());
						controller->SetStopTime(oldController->GetStopTime());
						controller->SetTarget(lightingProperty);
						controller->SetInterpolator(oldController->GetInterpolator());
						controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_ALPHA_TRANSPARENCY);

						lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
						material_alpha_controllers_map[oldController] = controller;
					}
				}
			}

			if (property->IsSameType(NiTexturingProperty::TYPE)) {
				texturing = DynamicCast<NiTexturingProperty>(property);
				string textureName;

				if (texturing->GetBaseTexture().source != NULL) {
					textureName += texturing->GetBaseTexture().source->GetFileName();
					//fix for orconebraid
					if (textureName == "Grey.dds" || textureName == "grey.dds")
						textureName = "textures\\characters\\hair\\Grey.dds";

					textureName.insert(9, "tes4\\");
					lightingProperty->SetSourceTexture(textureName);

					if (texturing->GetController() == NULL)
						continue;

					if (texturing->GetController()->IsSameType(NiFlipController::TYPE)) {
						NiFlipControllerRef oldController = DynamicCast<NiFlipController>(texturing->GetController());
						FlipBookConverter conv(oldController, StaticCast<BSShaderProperty>(lightingProperty), hasGlow, _export_path.string());
						float speed = 1.;
						auto blend = DynamicCast<NiBlendFloatInterpolator>(oldController->GetInterpolator());
						if (NULL != blend && blend->GetFlags() == 3)
						{
							speed = (float)Accessor<NiBlendFloatInterpolator>(*blend).play_speed;
						}
						if (!conv.uv_atlas.empty())
							deferred_blends[oldController] = { speed, conv.uv_atlas };
						material_flip_controllers_map[oldController] = DynamicCast<NiFloatInterpController>(lightingProperty->GetController());
					}

					if (texturing->GetController()->IsSameType(NiTextureTransformController::TYPE)) {
						NiTextureTransformControllerRef oldController = DynamicCast<NiTextureTransformController>(texturing->GetController());
						BSEffectShaderPropertyFloatControllerRef controller = new BSEffectShaderPropertyFloatController();
						controller->SetFlags(oldController->GetFlags());
						controller->SetFrequency(oldController->GetFrequency());
						controller->SetPhase(oldController->GetPhase());
						controller->SetStartTime(oldController->GetStartTime());
						controller->SetStopTime(oldController->GetStopTime());
						controller->SetTarget(lightingProperty);
						controller->SetInterpolator(oldController->GetInterpolator());
						if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_U)
							controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_U_OFFSET);
						if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_V)
							controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_V_OFFSET);
						if (oldController->GetOperation() == TransformMember::TT_SCALE_U)
							controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_U_SCALE);
						if (oldController->GetOperation() == TransformMember::TT_SCALE_V)
							controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_V_SCALE);
						if (oldController->GetOperation() == TransformMember::TT_ROTATE)
						{
							controller->SetTypeOfControlledVariable(EffectShaderControlledVariable::ESCV_U_OFFSET);
							if (!oldController->GetInterpolator()->IsDerivedType(NiBlendInterpolator::TYPE))
							{
								//convert, otherwise defer;
								NiFloatInterpControllerRef new_v_controller;
								NiInterpolatorRef newInterpolatorU;
								NiInterpolatorRef newInterpolatorV;

								convert_tt_rotate(oldController,
									StaticCast<NiFloatInterpController>(controller),
									oldController->GetInterpolator(),
									new_v_controller,
									newInterpolatorU,
									newInterpolatorV);

								new_v_controller->SetNextController(oldController->GetNextController());
								controller->SetNextController(StaticCast<NiTimeController>(new_v_controller));

								controller->SetInterpolator(newInterpolatorU);
								controller->SetInterpolator(newInterpolatorV);
							}
						}

						lightingProperty->SetController(DynamicCast<NiTimeController>(controller));
						material_transform_controllers_map[oldController] = controller;
					}
				}
			}
			if (property->IsSameType(NiStencilProperty::TYPE)) {
				lightingProperty->SetShaderFlags2_sk(static_cast<SkyrimShaderPropertyFlags2>(lightingProperty->GetShaderFlags2_sk() + SkyrimShaderPropertyFlags2::SLSF2_DOUBLE_SIDED));
			}
			if (property->IsSameType(NiAlphaProperty::TYPE)) {
				NiAlphaPropertyRef alpha = new NiAlphaProperty();
				alpha->SetFlags(DynamicCast<NiAlphaProperty>(property)->GetFlags());
				alpha->SetThreshold(DynamicCast<NiAlphaProperty>(property)->GetThreshold());
				obj.SetAlphaProperty(alpha);
			}
			if (property->IsSameType(NiSpecularProperty::TYPE)) {
				hasSpecular = true;
			}
			if (property->IsSameType(NiZBufferProperty::TYPE)) {
				hasZBuffer = true;
			}
		}
		//extract geometry
		NiPSysDataRef data = DynamicCast<NiPSysData>(obj.NiGeometry::GetData());
		if (data != NULL) {
			if (!data->GetVertexColors().empty()) {
				data->SetHasVertexColors(true);
				lightingProperty->SetShaderFlags2_sk(static_cast<SkyrimShaderPropertyFlags2>(lightingProperty->GetShaderFlags2_sk() | SkyrimShaderPropertyFlags2::SLSF2_VERTEX_COLORS));
			}
			else {
				data->SetHasVertexColors(false);
				lightingProperty->SetShaderFlags2_sk(static_cast<SkyrimShaderPropertyFlags2>(lightingProperty->GetShaderFlags2_sk() & ~SkyrimShaderPropertyFlags2::SLSF2_VERTEX_COLORS));
			}
			data->SetBsMaxVertices(data->GetVertices().size());
			data->NiGeometryData::SetVertices(vector<Vector3>());
			data->SetVertices(vector<Vector3>());
			data->SetVertexColors(vector<Color4>{});
			data->SetHasRadii(true);
			data->SetRadii(vector<float>{});
			data->SetSizes(vector<float>{});
			data->SetAspectRatio(1.0f);

			//vector<Vector3>& vertices = geom_data->GetVertices();

			//Vector3 COM = { 0,0,0 };
			//if (vertices.size() != 0)
			//	COM = ckcmd::Geometry::centeroid(vertices);
			//
			//vector<Vector3>& normals = data->GetNormals();
			//if (normals.size() != vertices.size())
			//{
			//	//normals are needed for sse, let's just fill the void here
			//	normals.resize(vertices.size());
			//	for (int i=0; i< vertices.size(); i++)
			//	{
			//		normals[i] = COM - vertices[i];
			//		normals[i] = normals[i].Normalized();
			//	}
			//}
			//geom_data->SetHasNormals(true);
			//geom_data->SetNormals(normals);
			

			//NiTriShapeRef particle_geom = new NiTriShape();
			//particle_geom->SetData(StaticCast<NiGeometryData>(geom_data));

			//particle_geom->SetName(obj.GetName() + "Emitter");

			//particle_geometry.push_back(particle_geom);

			//NiPSysMeshEmitterRef emitter = new NiPSysMeshEmitter();
			//vector<NiAVObject * > emittee = emitter->GetEmitterMeshes();
			//emittee.push_back(StaticCast<NiAVObject>(particle_geom));
			//emitter->SetEmitterMeshes(emittee);

			//vector<Ref<NiPSysModifier > > mods = obj.GetModifiers();
			//mods.push_back(StaticCast<NiPSysModifier>(emitter));
			//obj.SetModifiers(mods);

			
		}
		lightingProperty->SetTextureClampMode(3);
		lightingProperty->SetLightingInfluence(255);
		obj.SetFlags(524302);

		NiPSysEmitterRef emitter = NULL;

		vector<Ref<NiPSysModifier > > mods = obj.GetModifiers();
		for (int i = 0; i < mods.size(); i++)
		{
			//if (mods[i]->IsDerivedType(NiPSysEmitter::TYPE))
			//{
			//	emitter = mods[i];
			//}
			/*if (mods[i]->IsDerivedType(NiPSysColorModifier::TYPE))
			{
				NiPSysColorModifierRef old_cm = DynamicCast<NiPSysColorModifier>(mods[i]);
				BSPSysSimpleColorModifierRef cm = new BSPSysSimpleColorModifier();
				cm->SetName(old_cm->GetName());
				cm->SetOrder(old_cm->GetOrder());
				cm->SetTarget(old_cm->GetTarget());
				cm->SetActive(old_cm->GetActive());
				Niflib::array<3, Color4 > col_array = cm->GetColors();
				NiColorDataRef colors = old_cm->GetData();
				vector<Key<Color4 > > old_colors_arra = colors->GetData().keys;
				for (int j = 0; j < old_colors_arra.size(); j++)
				{
					if (j == 3) break;
					col_array[j] = old_colors_arra[j].data;
				}
				cm->SetColors(col_array);
				mods[i] = cm;
			}*/
			//	mods.erase(mods.begin() + i);
		}
		//for (int i = 0; i < mods.size(); i++)
		//{
		//	if (mods[i]->IsDerivedType(NiPSysGrowFadeModifier::TYPE))
		//	{
		//		mods.erase(mods.begin() + i);
		//	}
		//}
		//BSPSysLODModifierRef lodm = new BSPSysLODModifier();
		//lodm->SetActive(true);
		//lodm->SetOrder(1);
		//lodm->SetTarget(&obj);
		//lodm->SetActive(true);
		//lodm->SetLodBeginDistance(0.111);
		//lodm->SetLodEndDistance(0.111);
		//lodm->SetEndEmitScale(1.0);
		//lodm->SetEndSize(1.0);

		//mods.insert(mods.begin() + 1, StaticCast<NiPSysModifier>(lodm));

		//add controller
		//if (emitter != NULL)
		//{
		//	NiPSysEmitterCtlrRef ectrl = new NiPSysEmitterCtlr();
		//	ectrl->SetFlags(72);
		//	ectrl->SetPhase(0.125126);
		//	ectrl->SetTarget(StaticCast<NiObjectNET>(&obj));
		//	ectrl->SetInterpolator(new NiFloatInterpolator);
		//	ectrl->SetVisibilityInterpolator(new NiBoolInterpolator());
		//	ectrl->SetModifierName(emitter->GetName());
		//	obj.SetController(ectrl);
		//}
		obj.SetModifiers(mods);

		if (!hasSpecular)
			lightingProperty->SetShaderFlags1_sk(static_cast<SkyrimShaderPropertyFlags1>(lightingProperty->GetShaderFlags1_sk() & ~SkyrimShaderPropertyFlags1::SLSF1_SPECULAR));

		if (hasZBuffer) {
			lightingProperty->SetShaderFlags2_sk(static_cast<SkyrimShaderPropertyFlags2>(lightingProperty->GetShaderFlags2_sk() & ~SkyrimShaderPropertyFlags2::SLSF2_ZBUFFER_WRITE));
		}

		if (!hasOwnEmit)
			lightingProperty->SetShaderFlags1_sk(static_cast<SkyrimShaderPropertyFlags1>(lightingProperty->GetShaderFlags1_sk() & ~SkyrimShaderPropertyFlags1::SLSF1_OWN_EMIT));
		else
			lightingProperty->SetShaderFlags1_sk(static_cast<SkyrimShaderPropertyFlags1>(lightingProperty->GetShaderFlags1_sk() | SkyrimShaderPropertyFlags1::SLSF1_OWN_EMIT));


		lightingProperty->SetShaderFlags1_sk(static_cast<SkyrimShaderPropertyFlags1>(SkyrimShaderPropertyFlags1::SLSF1_ZBUFFER_TEST));

		//lightingProperty->SetTextureSet(textureSet);
		obj.SetShaderProperty(DynamicCast<BSShaderProperty>(lightingProperty));
		obj.SetExtraDataList(vector<Ref<NiExtraData>> {});
		obj.SetProperties(vector<Ref<NiProperty>> {});
	}

	template<>
	inline void visit_object(NiControllerSequence& obj)
	{

		if (find(nisequences.begin(), nisequences.end(), obj.GetName()) == nisequences.end())
			nisequences.push_back(obj.GetName());
		vector<ControlledBlock> cblocks = obj.GetControlledBlocks();
		vector<ControlledBlock> nblocks;

		//for some reason, oblivion's NIF blocks have empty NiTransforms, time to remove.
		for (int i = 0; i != cblocks.size(); i++) {
			NiInterpolator* intp = cblocks[i].interpolator;
			if (intp == NULL)
				continue;
			//if (intp->IsDerivedType(NiTransformInterpolator::TYPE)) {
			//	NiTransformInterpolator* tintp = DynamicCast<NiTransformInterpolator>(intp);
			//	if (tintp->GetData() == NULL)
			//		continue;
			//}
			if (cblocks[i].stringPalette != NULL)
			{
				//Deprecated. Maybe we can handle with tri facegens
				if (cblocks[i].controller != NULL && cblocks[i].controller->IsDerivedType(NiGeomMorpherController::TYPE))
					continue;

				cblocks[i].nodeName = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				cblocks[i].controllerType = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].controllerTypeOffset);

				if (cblocks[i].propertyTypeOffset != 4294967295)
					cblocks[i].propertyType = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].propertyTypeOffset);

				if (cblocks[i].controllerIdOffset != 4294967295)
					cblocks[i].controllerId = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].controllerIdOffset);

				if (cblocks[i].interpolatorIdOffset != 4294967295)
					cblocks[i].interpolatorId = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].interpolatorIdOffset);
			}

			if (cblocks[i].controller != NULL && cblocks[i].controller->IsDerivedType(NiMaterialColorController::TYPE))
			{
				//std::string node_name = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				//NiNodeRef controlled = NULL;
				//for (auto& niblock : blocks) {
				//	if (DynamicCast<NiNode>(niblock) != NULL) {
				//		NiNodeRef node = DynamicCast<NiNode>(niblock);
				//		if (node->GetName() == node_name)
				//		{
				//			controlled = node;
				//		}
				//	}
				//} 
				//if (NULL != controlled && controlled->IsDerivedType(NiParticleSystem::TYPE)) {
				//	cblocks[i].propertyType = "BSEffectShaderProperty";
				//	cblocks[i].controllerType = "BSEffectShaderPropertyColorController";
				//}
				//else {
				//	cblocks[i].propertyType = "BSLightingShaderProperty";
				//	cblocks[i].controllerType = "BSLightingShaderPropertyColorController";
				//}
			}
			if (cblocks[i].controller != NULL && cblocks[i].controller->IsDerivedType(NiTextureTransformController::TYPE))
			{
				//std::string node_name = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				//NiNodeRef controlled = NULL;
				//for (auto& niblock : blocks) {
				//	if (DynamicCast<NiNode>(niblock) != NULL) {
				//		NiNodeRef node = DynamicCast<NiNode>(niblock);
				//		if (node->GetName() == node_name)
				//		{
				//			controlled = node;
				//		}
				//	}
				//}
				//if (NULL != controlled && controlled->IsDerivedType(NiParticleSystem::TYPE)) {
				//	cblocks[i].propertyType = "BSEffectShaderProperty";
				//	cblocks[i].controllerType = "BSEffectShaderPropertyFloatController";
				//}
				//else {
				//	cblocks[i].propertyType = "BSLightingShaderProperty";
				//	cblocks[i].controllerType = "BSLightingShaderPropertyFloatController";
				//}
				//std::string node_name = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				//cblocks[i].propertyType = "BSLightingShaderProperty";
				//cblocks[i].controllerType = "BSLightingShaderPropertyFloatController";
			}
			if (cblocks[i].controller != NULL && (cblocks[i].controller->IsDerivedType(NiAlphaController::TYPE) || cblocks[i].controller->IsDerivedType(NiFlipController::TYPE))) //hoping this works
			{
				//std::string node_name = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				//NiNodeRef controlled = NULL;
				//for (auto& niblock : blocks) {
				//	if (DynamicCast<NiNode>(niblock) != NULL) {
				//		NiNodeRef node = DynamicCast<NiNode>(niblock);
				//		if (node->GetName() == node_name)
				//		{
				//			controlled = node;
				//		}
				//	}
				//}
				//if (NULL != controlled && controlled->IsDerivedType(NiParticleSystem::TYPE)) {
				//	cblocks[i].propertyType = "BSEffectShaderProperty";
				//	cblocks[i].controllerType = "BSEffectShaderPropertyFloatController";
				//}
				//else {
				//	cblocks[i].propertyType = "BSLightingShaderProperty";
				//	cblocks[i].controllerType = "BSLightingShaderPropertyFloatController";
				//}
				//std::string node_name = getStringFromPalette(cblocks[i].stringPalette->GetPalette().palette, cblocks[i].nodeNameOffset);
				//cblocks[i].propertyType = "BSLightingShaderProperty";
				//cblocks[i].controllerType = "BSLightingShaderPropertyFloatController";
			}

			//set to default... if above doesn't work
			if (cblocks[i].controllerType == "")
				throw runtime_error("controller type is null; will cause errors.");

			cblocks[i].stringPalette = NULL;
			nblocks.push_back(cblocks[i]);
		}
		obj.SetControlledBlocks(nblocks);
		obj.SetStringPalette(NULL);
	}

	template<>
	inline void visit_object(NiTransformController& obj)
	{
		//disable geomorph on ghost skeleton
		if (obj.GetNextController() != NULL && obj.GetNextController()->IsDerivedType(NiGeomMorpherController::TYPE))
			obj.SetNextController(NULL);

	}

	template<>
	inline void visit_object(NiTextKeyExtraData& obj)
	{
		vector<Key<IndexString>> textKeys = obj.GetTextKeys();

		for (int i = 0; i != textKeys.size(); i++) {
			if (std::strstr(textKeys[i].data.c_str(), "Sound:") || std::strstr(textKeys[i].data.c_str(), "sound:")) {
				textKeys[i].data.insert(7, "TES4");
			}
		}

		obj.SetTextKeys(textKeys);
	}

	//from now on, we must switch from an old type to hkTransform, so it is useful to directly use havok data

	template<>
	inline void visit_object(bhkCollisionObject& obj) {
		bhkRigidBodyRef ref = DynamicCast<bhkRigidBody>(obj.GetBody());
		if (ref->GetHavokFilter().layer_ob == OblivionLayer::OL_CLUTTER) {
			obj.SetFlags((bhkCOFlags)(obj.GetFlags() | BHKCO_SYNC_ON_UPDATE));
		}
		if (already_upgraded.insert(&obj).second) {
			if (obj.GetBody() != NULL)
				collision_target_map[&*obj.GetBody()] = obj.GetTarget();
		}
	}

	template<>
	inline void visit_object(bhkBlendCollisionObject& obj) {
		obj.SetFlags((bhkCOFlags)(obj.GetFlags() | BHKCO_SET_LOCAL | BHKCO_SYNC_ON_UPDATE));

		auto parents = find_parents(StaticCast<NiObject>(&obj));
		parents.push_back(StaticCast<NiObject>(&obj));
		hkTransform world_transform; world_transform.setIdentity();
		for (auto& avobj : parents) {
			if (avobj->IsDerivedType(NiAVObject::TYPE)) {
				NiAVObjectRef av = DynamicCast<NiAVObject>(avobj);
				hkTransform local = TOHKTRANSFORM(av->GetRotation(), av->GetTranslation());
				world_transform.setMulEq(local);
			}
		}

		if (already_upgraded.insert(&obj).second) {
			if (obj.GetBody() != NULL)
			{
				collision_target_map[&*obj.GetBody()] = obj.GetTarget();
				collision_target_world_transform[&*obj.GetBody()] = world_transform;
			}
		}
	}

	template<>
	inline void visit_object(bhkSPCollisionObject& obj) {
		if (already_upgraded.insert(&obj).second) {
			if (obj.GetBody() != NULL)
				collision_target_map[&*obj.GetBody()] = obj.GetTarget();
		}
	}

	template<>
	inline void visit_object(bhkRigidBody& obj) {
		if (obj.GetHavokFilter().layer_ob == OblivionLayer::OL_CLUTTER) {
			obj.SetMotionSystem(MO_SYS_DYNAMIC);
			obj.SetSolverDeactivation(SOLVER_DEACTIVATION_LOW);
			obj.SetQualityType(MO_QUAL_MOVING);
		}
		if (already_upgraded.insert(&obj).second) {
			NiAVObject* target = (NiAVObject*)collision_target_map[&obj];
			hkTransform world_transform = hkTransform::getIdentity();
			if (collision_target_world_transform.find(&obj) != collision_target_world_transform.end())
			{
				world_transform = collision_target_world_transform[&obj];
			}
			Accessor<bhkRigidBodyUpgrader> upgrader(obj, this_info, target, _collision_translation, world_transform);
		}
	}

	template<>
	inline void visit_object(bhkRigidBodyT& obj) {
		HavokFilter ref = obj.GetHavokFilter();
		if (obj.GetHavokFilter().layer_ob == OblivionLayer::OL_CLUTTER) {
			obj.SetMotionSystem(MO_SYS_DYNAMIC); //All of skyrim clutter
			obj.SetSolverDeactivation(SOLVER_DEACTIVATION_LOW);
			obj.SetQualityType(MO_QUAL_MOVING);
			obj.SetMass(obj.GetMass() * COLLISION_RATIO);
		}
		if (already_upgraded.insert(&obj).second) {
			NiAVObject* target = (NiAVObject*)collision_target_map[&obj];
			Accessor<bhkRigidBodyUpgrader> upgrader(obj, this_info, target, _collision_translation, hkTransform::getIdentity());
		}
	}

	//Upgrade shapes

	template<typename T> void convertMaterialAndRadius(T& shape) {
		HavokMaterial material = shape.GetMaterial();
		material.material_sk = convert_havok_material(material.material_ob);
		shape.SetMaterial(material);
		shape.SetRadius(shape.GetRadius() * COLLISION_RATIO);
	}

	//phantoms
	template<>
	inline void visit_object(bhkSimpleShapePhantom& obj) {
		if (already_upgraded.insert(&obj).second) {
			HavokFilter layer = obj.GetHavokFilter();
			layer.layer_sk = convert_havok_layer(layer.layer_ob);
			obj.SetHavokFilter(layer);
		}
	}

	//Containers
	template<>
	inline void visit_object(bhkConvexTransformShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
			Matrix44 transform = obj.GetTransform();
			Vector3 trans = transform.GetTrans();
			trans -= _collision_translation;
			trans.x *= COLLISION_RATIO;
			trans.y *= COLLISION_RATIO;
			trans.z *= COLLISION_RATIO;
			transform.SetTrans(trans);
			obj.SetTransform(transform);
			obj.SetShape(upgrade_shape(obj.GetShape(), this_info, NULL, _collision_translation));
		}
	}

	template<>
	inline void visit_object(bhkTransformShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
			Matrix44 transform = obj.GetTransform();
			Vector3 trans = transform.GetTrans();
			trans -= _collision_translation;
			trans.x *= COLLISION_RATIO;
			trans.y *= COLLISION_RATIO;
			trans.z *= COLLISION_RATIO;
			transform.SetTrans(trans);
			obj.SetTransform(transform);
			obj.SetShape(upgrade_shape(obj.GetShape(), this_info, NULL, _collision_translation));
		}
	}

	template<>
	inline void visit_object(bhkListShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			HavokMaterial material = obj.GetMaterial();
			material.material_sk = convert_havok_material(material.material_ob);
			obj.SetMaterial(material);
			obj.SetSubShapes(upgrade_shapes(obj.GetSubShapes(), this_info, NULL, _collision_translation));
		}
	}

	//TODO: need to upgrade shapes into containers

	//Shapes
	template<>
	inline void visit_object(bhkSphereShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
		}
	}

	template<>
	inline void visit_object(bhkBoxShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
			Vector3 half_extent = obj.GetDimensions();
			half_extent.x *= COLLISION_RATIO;
			half_extent.y *= COLLISION_RATIO;
			half_extent.z *= COLLISION_RATIO;
			obj.SetDimensions(half_extent);
		}
	}

	template<>
	inline void visit_object(bhkCapsuleShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
			obj.SetRadius1(obj.GetRadius1() * COLLISION_RATIO);
			obj.SetRadius2(obj.GetRadius2() * COLLISION_RATIO);
			obj.SetFirstPoint((obj.GetFirstPoint() - _collision_translation) * COLLISION_RATIO);
			obj.SetSecondPoint((obj.GetSecondPoint() - _collision_translation) * COLLISION_RATIO);
		}
	}

	template<>
	inline void visit_object(bhkConvexVerticesShape& obj) {
		if (already_upgraded.insert(&obj).second) {
			convertMaterialAndRadius(obj);
			vector<Vector4> vertices = obj.GetVertices();
			for (Vector4& v : vertices) {
				v -= _collision_translation;
				v.x *= COLLISION_RATIO;
				v.y *= COLLISION_RATIO;
				v.z *= COLLISION_RATIO;
			}

			obj.SetVertices(vertices);
			vector<Vector4> normals = obj.GetNormals();
			for (Vector4& n : normals) {
				n.w = n.w * COLLISION_RATIO;
			}
			obj.SetNormals(normals);
		}
	}

	//Upgrade Constraints

	template<>
	void visit_compound(HingeDescriptor& descriptor) {
		if (already_upgraded.insert(&descriptor).second) {

			auto a = TOVECTOR4(descriptor.parentSpace.axis);
			auto b = TOVECTOR4(descriptor.axis);
			hkVector4 c; c.setCross(a, b);

			descriptor.axleA = TOVECTOR3(c); // descriptor.parentSpace.axis^ descriptor.axis;
			descriptor.perp2AxleInA1 = descriptor.parentSpace.axis;
			descriptor.perp2AxleInA2 = descriptor.axis;
			descriptor.pivotA = (descriptor.parentSpace.pivot - _collision_translation) * COLLISION_RATIO;

			descriptor.axleB = descriptor.childSpace.axis;
			descriptor.pivotB = (descriptor.childSpace.pivot - _collision_translation) * COLLISION_RATIO;

			//find rotation matrix
			auto row1 = TOVECTOR4(descriptor.axleA);
			auto row2 = TOVECTOR4(descriptor.pivotA);  row2.normalize3();
			hkVector4 row3; row3.setCross(row1, row2);
			hkRotation r1; r1.setCols(row1, row2, row3);


			auto row1b = TOVECTOR4(descriptor.axleB);
			auto row2b = TOVECTOR4(descriptor.pivotB);  row2b.normalize3();
			hkVector4 row3b; row3b.setCross(row1b, row2b);
			hkRotation r1b; r1b.setCols(row1b, row2b, row3b);

			hkRotation ab; ab.setMulInverseMul(r1b, r1);

			hkVector4 perp2AxleInB1; perp2AxleInB1.setMul3(ab, TOVECTOR4(descriptor.perp2AxleInA1));
			hkVector4 perp2AxleInB2; perp2AxleInB2.setMul3(ab, TOVECTOR4(descriptor.perp2AxleInA2));

			descriptor.perp2AxleInB1 = TOVECTOR3(perp2AxleInB1);
			descriptor.perp2AxleInB2 = TOVECTOR3(perp2AxleInB2);
		}
	}

	template<>
	void visit_compound(LimitedHingeDescriptor& descriptor) {
		if (already_upgraded.insert(&descriptor).second) {

			descriptor.axleA = descriptor.parentSpace.referenceSystem.xAxis;
			descriptor.perp2AxleInA1 = descriptor.parentSpace.referenceSystem.yAxis;
			descriptor.perp2AxleInA2 = descriptor.axis;
			descriptor.pivotA = (descriptor.parentSpace.pivot - _collision_translation) * COLLISION_RATIO;

			auto a = TOVECTOR4(descriptor.childSpace.referenceSystem.yAxis);
			auto b = TOVECTOR4(descriptor.childSpace.referenceSystem.xAxis);
			hkVector4 c; c.setCross(a, b);

			descriptor.axleB = descriptor.childSpace.referenceSystem.xAxis;
			descriptor.perp2AxleInB1 = TOVECTOR3(c);//descriptor.childSpace.referenceSystem.yAxis ^ descriptor.childSpace.referenceSystem.xAxis;
			descriptor.perp2AxleInB2 = descriptor.childSpace.referenceSystem.yAxis;
			descriptor.pivotB = (descriptor.childSpace.pivot - _collision_translation) * COLLISION_RATIO;
		}
	}

	template<>
	void visit_compound(BallAndSocketDescriptor& descriptor) {
		descriptor.pivotA -= _collision_translation;
		descriptor.pivotB -= _collision_translation;
	}

	template<>
	void visit_compound(PrismaticDescriptor& descriptor) {
		if (already_upgraded.insert(&descriptor).second) {

			descriptor.slidingA = descriptor.plane.xAxis;
			descriptor.rotationA = descriptor.parentSpace.referenceSystem.xAxis;
			descriptor.planeA = descriptor.parentSpace.referenceSystem.yAxis;
			descriptor.pivotA = (descriptor.parentSpace.pivot - _collision_translation) * COLLISION_RATIO;

			descriptor.slidingB = descriptor.childSpace.referenceSystem.yAxis;
			descriptor.rotationB = descriptor.childSpace.pivot;
			descriptor.planeB = descriptor.childSpace.referenceSystem.xAxis;
			descriptor.pivotB = (descriptor.plane.yAxis - _collision_translation) * COLLISION_RATIO;
		}
	}

	template<>
	void visit_compound(StiffSpringDescriptor& descriptor) {
		//Nothing to do;
		descriptor.pivotA -= _collision_translation;
		descriptor.pivotB -= _collision_translation;
	}

	template<>
	void visit_compound(RagdollDescriptor& descriptor) {
		if (already_upgraded.insert(&descriptor).second) {

			descriptor.twistA = descriptor.parentSpace.referenceSystem.yAxis;
			descriptor.planeA = descriptor.parentSpace.referenceSystem.xAxis;
			auto a = TOVECTOR4(descriptor.twistA);
			auto b = TOVECTOR4(descriptor.planeA);
			hkVector4 c; c.setCross(a, b);
			descriptor.motorA = TOVECTOR3(c); // descriptor.twistA^ descriptor.planeA;
			descriptor.pivotA = (descriptor.parentSpace.pivot - _collision_translation) * COLLISION_RATIO;

			descriptor.twistB = descriptor.childSpace.referenceSystem.yAxis;
			descriptor.planeB = descriptor.childSpace.referenceSystem.xAxis;
			a = TOVECTOR4(descriptor.twistB);
			b = TOVECTOR4(descriptor.planeB);
			c.setCross(a, b);
			descriptor.motorB = TOVECTOR3(c);//descriptor.twistB ^ descriptor.planeB;
			descriptor.pivotB = (descriptor.childSpace.pivot - _collision_translation) * COLLISION_RATIO;
		}
	}
};

class RebuildVisitor : public RecursiveFieldVisitor<RebuildVisitor> {
	set<NiObject*> objects;
public:
	vector<NiObjectRef> blocks;

	RebuildVisitor(NiObject* root, const NifInfo& info) :
		RecursiveFieldVisitor(*this, info) {
		root->accept(*this, info);

		for (NiObject* ptr : objects) {
			blocks.push_back(ptr);
		}
	}


	template<class T>
	inline void visit_object(T& obj) {
		objects.insert(&obj);
	}


	template<class T>
	inline void visit_compound(T& obj) {
	}

	template<class T>
	inline void visit_field(T& obj) {}
};

class FixTargetsVisitor : public RecursiveFieldVisitor<FixTargetsVisitor> {
	vector<NiObjectRef>& blocks;
public:


	FixTargetsVisitor(NiObject* root, const NifInfo& info, vector<NiObjectRef>& blocks) :
		RecursiveFieldVisitor(*this, info), blocks(blocks) {
		root->accept(*this, info);
	}


	template<class T>
	inline void visit_object(T& obj) {}

	template<>
	inline void visit_object(NiDefaultAVObjectPalette& obj) {
		vector<AVObject > av_objects = obj.GetObjs();
		for (AVObject& av_object : av_objects) {
			for (NiObjectRef ref : blocks) {
				if (ref->IsDerivedType(NiAVObject::TYPE)) {
					NiAVObjectRef av_ref = DynamicCast<NiAVObject>(ref);
					if (av_ref->GetName() == av_object.name) {
						av_object.avObject = DynamicCast<NiAVObject>(av_ref);
					}
				}
			}
		}
		obj.SetObjs(av_objects);
	}

	template<>
	inline void visit_object(NiPSysMeshEmitter& obj) {
		vector<NiAVObject * >& meshes = obj.GetEmitterMeshes();
		for (int i = 0; i < meshes.size(); i++) {
			if (NULL != meshes[i])
			{
				for (NiObjectRef ref : blocks) {
					if (ref->IsDerivedType(NiAVObject::TYPE)) {
						NiAVObjectRef av_ref = DynamicCast<NiAVObject>(ref);
						if (av_ref->GetName() == meshes[i]->GetName()) {
							meshes[i] = av_ref;
						}
					}
				}
			}
		}
		obj.SetEmitterMeshes(meshes);
	}

	template<>
	inline void visit_object(NiControllerSequence& obj) {
		vector<ControlledBlock> cblocks = obj.GetControlledBlocks();
		vector<ControlledBlock> blocks_to_add;

		for (int i = 0; i != cblocks.size(); i++) {
			NiTimeControllerRef controller = cblocks[i].controller;

			//specific fix for oblivion fountains. 
			if (controller->IsSameType(BSLightingShaderPropertyColorController::TYPE) && controller->GetTarget()->IsSameType(BSEffectShaderProperty::TYPE)) {

				BSLightingShaderPropertyColorControllerRef lightingController = DynamicCast<BSLightingShaderPropertyColorController>(controller);
				BSEffectShaderPropertyColorController* newController = new BSEffectShaderPropertyColorController();

				if (lightingController->GetNextController() != NULL)
					Log::Warn("Has nested controller");

				newController->SetFlags(lightingController->GetFlags());
				newController->SetFrequency(lightingController->GetFrequency());
				newController->SetPhase(lightingController->GetPhase());
				newController->SetStartTime(lightingController->GetStartTime());
				newController->SetStopTime(lightingController->GetStopTime());
				newController->SetPhase(lightingController->GetPhase());
				newController->SetTarget(lightingController->GetTarget());
				newController->SetTypeOfControlledColor(EffectShaderControlledColor::ECSC_EMISSIVE_COLOR);

				cblocks[i].controller = newController;
				cblocks[i].propertyType = "BSEffectShaderProperty";
				cblocks[i].controllerType = "BSEffectShaderPropertyColorController";

				BSEffectShaderPropertyRef shader = DynamicCast<BSEffectShaderProperty>(newController->GetTarget());
				shader->SetController(cblocks[i].controller);
			}
			else if (controller->IsSameType(BSLightingShaderPropertyColorController::TYPE) && 
				DynamicCast<BSLightingShaderPropertyColorController>(controller)->GetTypeOfControlledColor() == LightingShaderControlledColor::LSCC_EMISSIVE_COLOR) {
				BSLightingShaderPropertyColorControllerRef lightingController = DynamicCast<BSLightingShaderPropertyColorController>(controller);
				
				if (lightingController->GetNextController() == NULL) {
					Log::Error("Found BSLightingShaderPropertyColorController Emissive without next controller!");
					continue;
				}



				BSLightingShaderPropertyFloatControllerRef floatController = DynamicCast<BSLightingShaderPropertyFloatController>(lightingController->GetNextController());

				NiFloatInterpolatorRef float_interpolator = DynamicCast< NiFloatInterpolator>(floatController->GetInterpolator());

				NiBlendFloatInterpolatorRef blend_interpolator = new NiBlendFloatInterpolator();
				Accessor<BlendUnknownSet>(2, blend_interpolator);
				blend_interpolator->SetFlags(InterpBlendFlags::MANAGER_CONTROLLED);
				blend_interpolator->SetManagedControlled(true);



				//blocks.push_back(StaticCast<NiObject>(float_interpolator));

				ControlledBlock another_block = cblocks[i];
				another_block.interpolator = float_interpolator;
				another_block.controller = floatController;
				another_block.controllerType = "BSLightingShaderPropertyFloatController";
				cblocks.insert(cblocks.begin() + i + 1, another_block);

				floatController->SetInterpolator(StaticCast<NiInterpolator>(blend_interpolator));
			}
		}
		//renumber
		size_t controller_id = 1;
		for (int i = 0; i < cblocks.size(); i++) {
			NiTimeControllerRef controller = cblocks[i].controller;


			//Fix names
			if (controller->IsSameType(BSLightingShaderPropertyColorController::TYPE))
			{
				cblocks[i].propertyType = "BSLightingShaderProperty";
				cblocks[i].controllerType = "BSLightingShaderPropertyColorController";
			}
			if (controller->IsSameType(BSEffectShaderPropertyColorController::TYPE))
			{
				cblocks[i].propertyType = "BSEffectShaderProperty";
				cblocks[i].controllerType = "BSEffectShaderPropertyColorController";
			}
			if (controller->IsSameType(BSLightingShaderPropertyFloatController::TYPE))
			{
				cblocks[i].propertyType = "BSLightingShaderProperty";
				cblocks[i].controllerType = "BSLightingShaderPropertyFloatController";
			}
			if (controller->IsSameType(BSEffectShaderPropertyFloatController::TYPE))
			{
				cblocks[i].propertyType = "BSEffectShaderProperty";
				cblocks[i].controllerType = "BSEffectShaderPropertyFloatController";
			}

			if (controller->IsSameType(BSLightingShaderPropertyColorController::TYPE) || 
				controller->IsSameType(BSEffectShaderPropertyColorController::TYPE) ||
				controller->IsSameType(BSLightingShaderPropertyFloatController::TYPE) || 
				controller->IsSameType(BSEffectShaderPropertyFloatController::TYPE))
			{


				cblocks[i].controllerId = std::to_string(controller_id++);
			}
		}

		multimap<NiObjectNET*, NiTimeControllerRef> object_controller_map;

		//Compose back controllers over properties
		for (int i = 0; i < cblocks.size(); i++) {
			if (cblocks[i].controller->IsSameType(NiMultiTargetTransformController::TYPE))
				continue;
			NiTimeControllerRef this_controller = cblocks[i].controller;
			while (this_controller != NULL) {
				if (!this_controller->IsSameType(NiMultiTargetTransformController::TYPE))
					object_controller_map.insert({ this_controller->GetTarget(), this_controller });
				this_controller = this_controller->GetNextController();
			}
		}

		set<NiObjectNET*> targets;

		for (multimap<NiObjectNET*, NiTimeControllerRef>::iterator it = object_controller_map.begin(), 
			end = object_controller_map.end(); it != end; it = object_controller_map.upper_bound(it->first))
		{
			targets.insert(it->first);
		}

		for (const auto& target : targets) {
			auto its = object_controller_map.equal_range(target);
			NiTimeControllerRef last = NULL;
			if (distance(its.first, its.second)>1)
			{
				for (auto it = its.first; it != its.second; it++)
				{
					if (!last) {
						target->SetController(it->second);
					}
					else {
						last->SetNextController(it->second);
					}
					last = it->second;
				}
				last->SetNextController(NULL);
			}
		}


		obj.SetControlledBlocks(cblocks);
	}

	template<>
	inline void visit_object(NiTriShape& obj) {
		if (obj.GetSkinInstance() != NULL) {

			NiSkinInstanceRef skinInstance = DynamicCast<NiSkinInstance>(obj.GetSkinInstance());
			NiSkinDataRef skinData = DynamicCast<NiSkinData>(skinInstance->GetData());
			NiSkinPartitionRef skinPartition = DynamicCast<NiSkinPartition>(skinInstance->GetSkinPartition());
			NiTriShapeDataRef shapeData = DynamicCast<NiTriShapeData>(obj.GetData());

			vector<BoneData> boneList = skinData->GetBoneList();
			if (skinPartition != nullptr)
			{
				SkinPartition skinBlock = skinPartition->GetSkinPartitionBlocks()[0];

				for (int i = 0; i != skinInstance->GetBones().size(); i++) {
					NiNodeRef bone = skinInstance->GetBones()[i];

					//if (bone->GetName() == "NPC Pelvis [Pelv]") {
					//	vector<BoneVertData> vertData = boneList[i].vertexWeights;
					//	vector<Vector3> vertices = vector<Vector3>(vertData.size());
					//	for (int i = 0; i != vertData.size(); i++) {
					//		vertices[i] = shapeData->GetVertices()[skinBlock.vertexMap[vertData[i].index]];
					//	}
					//	Vector3 newPos = centeroid(vertices);
					//	Vector3 bonePos = Vector3(-0.000003f, -0.000010f, 68.911301f);
					//	bonePos.x = -bonePos.x;
					//	bonePos.y = -bonePos.y;
					//	bonePos.z = -bonePos.z;
					//	boneList[i].skinTransform.translation = bonePos + newPos;
					//	boneList[i].skinTransform.rotation = Matrix33();
					//}
					//if (bone->GetName() == "NPC L Thigh [LThg]") {
					//	vector<BoneVertData> vertData = boneList[i].vertexWeights;
					//	vector<Vector3> vertices = vector<Vector3>(vertData.size());
					//	for (int i = 0; i != vertData.size(); i++) {
					//		vertices[i] = shapeData->GetVertices()[skinBlock.vertexMap[vertData[i].index]];
					//	}
					//	//correct? dunno :D
					//	Matrix44 matrix = Matrix44();
					//	matrix.rows[0][0] = bone->GetRotation().rows[0][0];
					//	matrix.rows[0][1] = bone->GetRotation().rows[0][1];
					//	matrix.rows[0][2] = bone->GetRotation().rows[0][2];
					//	matrix.rows[0][3] = bone->GetTranslation().x;
					//	matrix.rows[1][0] = bone->GetRotation().rows[1][0];
					//	matrix.rows[1][1] = bone->GetRotation().rows[1][1];
					//	matrix.rows[1][2] = bone->GetRotation().rows[1][2];
					//	matrix.rows[1][3] = bone->GetTranslation().y;
					//	matrix.rows[2][0] = bone->GetRotation().rows[2][0];
					//	matrix.rows[2][1] = bone->GetRotation().rows[2][1];
					//	matrix.rows[2][2] = bone->GetRotation().rows[2][2];
					//	matrix.rows[2][3] = bone->GetTranslation().z;






						//Vector3 newPos = centeroid(vertices);
						//Vector3 bonePos = Vector3(-6.615073f, 0.000394f, 68.911301f);
						//boneList[i].skinTransform.translation = bonePos + newPos;
						//boneList[i].skinTransform.translation = Vector3(-13.517195f, 1.999292f, 67.866142f);
						//boneList[i].skinTransform.rotation = Matrix33(
						//	-0.9942f, -0.0410f, -0.0933f,
						//	-0.0375f, 0.9986f, -0.0369f,
						//	0.1007f, -0.0330f, -0.9944);
					//}
					//if (bone->GetName() == "NPC R Thigh [RThg]") {
					//	vector<BoneVertData> vertData = boneList[i].vertexWeights;
					//	vector<Vector3> vertices = vector<Vector3>(vertData.size());
					//	for (int i = 0; i != vertData.size(); i++) {
					//		vertices[i] = shapeData->GetVertices()[skinBlock.vertexMap[vertData[i].index]];
					//	}
					//	Vector3 newPos = centeroid(vertices);
					//	Vector3 bonePos = Vector3(6.615068f, 0.000394f, 68.911301f);
					//	boneList[i].skinTransform.translation = bonePos + newPos;
					//	//boneList[i].skinTransform.translation = Vector3(13.461305f, 1.992750f, 67.877457f);
					//	boneList[i].skinTransform.rotation = Matrix33(
					//		-0.9943f, 0.0413f, 0.0985f,
					//		0.0378f, 0.9986f, -0.0369f,
					//		-0.0998f, -0.0330f, -0.9945f);
					//}
					//if (bone->GetName() == "NPC Spine [Spn0]") {
					//	boneList[i].skinTransform.translation = Vector3(-0.000022f, 8.403816f, -72.405434f);
					//	boneList[i].skinTransform.rotation = Matrix33(
					//		1.0f, 0.0000f, -0.0000f,
					//		-0.000f, 0.9990f, 0.0436f,
					//		0.000f, -0.0436f, 0.9990f);
				}
				//if (bone->GetName() == "NPC Spine1 [Spn1]") {
				//	boneList[i].skinTransform.translation = Vector3(-0.000018f, -3.890695f, -81.495285f);
				//	boneList[i].skinTransform.rotation = Matrix33(
				//		1.0f, 0.0000f, -0.0000f,
				//		-0.000f, 0.9942f, -0.1071f,
				//		0.000f, 0.1071f, 0.9942f);
				//}
				//if (bone->GetName() == "NPC Spine2 [Spn2]") {
				//	boneList[i].skinTransform.translation = Vector3(-0.000093f, 18.403816f, -89.638092f);
				//	boneList[i].skinTransform.rotation = Matrix33(
				//		1.0f, 0.0000f, -0.0000f,
				//		-0.000f, 0.9910f, 0.1336f,
				//		0.000f, -0.1336f, 0.9910f);
				//}
				//if (bone->GetName() == "NPC Head [Head]") {
				//	boneList[i].skinTransform.translation = Vector3(-0.000256f, -1.547517f, 0);
				//	boneList[i].skinTransform.rotation = Matrix33();
				//}
				//if (bone->GetName() == "NPC R Clavicle [RClv]") {
				//	boneList[i].skinTransform.translation = Vector3(93.688957f, 51.393600f, 25.169991f);
				//	boneList[i].skinTransform.rotation = Matrix33(
				//		-0.4490f, 0.3924f, 0.8028f,
				//		-0.3051f, 0.7771f, -0.5505f,
				//		-0.8398f, -0.4921f, -0.2292f);
				//}
				//if (bone->GetName() == "NPC L Clavicle [LClv]") {
				//	boneList[i].skinTransform.translation = Vector3(-93.689011f, 51.393459f, 25.170134f);
				//	boneList[i].skinTransform.rotation = Matrix33(
				//		-0.4490f, -0.3924f, -0.8028f,
				//		0.3051f, 0.7771f, -0.5505f,
				//		0.8398f, -0.4921f, -0.2292f);
				//}
			//}
			}

			skinData->SetBoneList(boneList);
			skinInstance->SetData(skinData);
			obj.SetSkinInstance(skinInstance);
		}
	}

	template<class T>
	inline void visit_compound(T& obj) {
	}

	template<>
	void visit_compound(AVObject& avlink) {
		//relink av objects on converted nistrips;

	}

	template<class T>
	inline void visit_field(T& obj) {}

	template<>
	void visit_field(NiTimeControllerRef& in) {
		if (!in == NULL && in->IsDerivedType(NiTextureTransformController::TYPE)) {
			NiTextureTransformControllerRef oldController = DynamicCast<NiTextureTransformController>(in);
			map<NiTextureTransformControllerRef, NiFloatInterpControllerRef>::iterator converted = material_transform_controllers_map.find(oldController);
			if (converted != material_transform_controllers_map.end()) {
				in = converted->second;
			}
			else {
				throw runtime_error("Found unconverted controller!");
				//BSLightingShaderPropertyFloatControllerRef controller = new BSLightingShaderPropertyFloatController();
				//controller->SetNextController(oldController->GetNextController());
				//controller->SetFlags(oldController->GetFlags());
				//controller->SetFrequency(oldController->GetFrequency());
				//controller->SetPhase(oldController->GetPhase());
				//controller->SetStartTime(oldController->GetStartTime());
				//controller->SetStopTime(oldController->GetStopTime());
				//controller->SetTarget(oldController->GetTarget());
				//controller->SetInterpolator(oldController->GetInterpolator());
				//if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_U)
				//	controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_OFFSET);
				//if (oldController->GetOperation() == TransformMember::TT_TRANSLATE_V)
				//	controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_OFFSET);
				//if (oldController->GetOperation() == TransformMember::TT_SCALE_U)
				//	controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_SCALE);
				//if (oldController->GetOperation() == TransformMember::TT_SCALE_V)
				//	controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_V_SCALE);
				//if (oldController->GetOperation() == TransformMember::TT_ROTATE)
				//{
				//	controller->SetTypeOfControlledVariable(LightingShaderControlledVariable::LSCV_U_OFFSET);
				//	if (!oldController->GetInterpolator()->IsDerivedType(NiBlendInterpolator::TYPE))
				//	{
				//		//convert, otherwise defer;
				//		NiFloatInterpControllerRef new_v_controller;
				//		NiInterpolatorRef newInterpolatorU;
				//		NiInterpolatorRef newInterpolatorV;

				//		convert_tt_rotate(oldController,
				//			StaticCast<NiFloatInterpController>(controller),
				//			oldController->GetInterpolator(),
				//			new_v_controller,
				//			newInterpolatorU,
				//			newInterpolatorV);

				//		new_v_controller->SetNextController(oldController->GetNextController());
				//		controller->SetNextController(StaticCast<NiTimeController>(new_v_controller));

				//		controller->SetInterpolator(newInterpolatorU);
				//		controller->SetInterpolator(newInterpolatorV);
				//	}
				//}
				//material_transform_controllers_map[oldController] = controller;
				//in = controller;
			}
		}
	}

};

void findFiles(fs::path startingDir, string extension, vector<fs::path>& results) {
	if (!exists(startingDir) || !is_directory(startingDir)) return;
	for (auto& dirEntry : fs::recursive_directory_iterator(startingDir))
	{
		if (fs::is_directory(dirEntry.path()))
			continue;

		std::string entry_extension = dirEntry.path().extension().string();
		transform(entry_extension.begin(), entry_extension.end(), entry_extension.begin(), ::tolower);
		if (entry_extension == extension) {
			results.push_back(dirEntry.path().string());
		}
	}
}

void check_bb_max_min(Vector3& max, Vector3& min, const hkVector4& vertex) {
	if (vertex(0) < min[0]) min[0] = vertex(0);
	if (vertex(1) < min[1]) min[1] = vertex(1);
	if (vertex(2) < min[2]) min[2] = vertex(2);
	if (vertex(0) > max[0]) max[0] = vertex(0);
	if (vertex(1) > max[1]) max[1] = vertex(1);
	if (vertex(2) > max[2]) max[2] = vertex(2);
}

Vector3 center_model(NiObjectRef root, vector<NiObjectRef>& blocks)
{
	std::deque<NiNodeRef> visit_stack;
	std::vector<std::pair<hkTransform, NiGeometryRef>> geometries;

	std::function<void(NiObjectRef, std::deque<NiNodeRef>&)> findShapesToBeTranslated = [&](NiObjectRef current_node, std::deque<NiNodeRef>& stack) {
		//recurse until we find a shape, avoiding rb as their mesh will be taken into account later
		if (current_node->IsDerivedType(NiNode::TYPE))
		{
			stack.push_front(DynamicCast<NiNode>(current_node));
			vector<NiAVObjectRef> children = DynamicCast<NiNode>(current_node)->GetChildren();
			for (int i = 0; i < children.size(); i++) {
				if (NULL == children[i])
					continue;
				if (children[i]->IsDerivedType(NiGeometry::TYPE))
				{
					//found leaf;
					hkTransform transform_from_rigid_body; transform_from_rigid_body.setIdentity();
					for (const auto& node : stack) {
						hkTransform this_transform = TOHKTRANSFORM(node->GetRotation(), node->GetTranslation(), node->GetScale());
						transform_from_rigid_body.setMul(this_transform, transform_from_rigid_body);
					}
					auto this_node = DynamicCast<NiAVObject>(children[i]);
					hkTransform this_transform = TOHKTRANSFORM(this_node->GetRotation(), this_node->GetTranslation(), this_node->GetScale());
					transform_from_rigid_body.setMul(this_transform, transform_from_rigid_body);

					geometries.push_back({ transform_from_rigid_body , DynamicCast< NiGeometry>(children[i]) });
				}
				else
					findShapesToBeTranslated(StaticCast<NiObject>(children[i]), stack);
			}
			stack.pop_front();
		}
	};

	findShapesToBeTranslated(root, visit_stack);


	Vector3 center = { 0., 0., 0. };
	Vector3 bb_max = { std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min() };
	Vector3 bb_min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
	for (const auto geometry : geometries) {
		const hkTransform& transform = geometry.first;
		const NiGeometryRef& shape = geometry.second;

		if (shape->IsSameType(NiTriShape::TYPE)) {
			auto data = DynamicCast<NiTriShape>(shape)->GetData();
			const auto vertices = data->GetVertices();
			for (const auto& vertex : vertices) {
				hkVector4 hvertex = { vertex[0],  vertex[1], vertex[2] };
				hvertex.setTransformedPos(transform, hvertex);
				check_bb_max_min(bb_max, bb_min, hvertex);
			}
		}
		if (shape->IsSameType(NiTriStrips::TYPE)) {
			auto data = DynamicCast<NiTriStrips>(shape)->GetData();
			const auto vertices = data->GetVertices();
			for (const auto& vertex : vertices) {
				hkVector4 hvertex = { vertex[0],  vertex[1], vertex[2] };
				hvertex.setTransformedPos(transform, hvertex);
				check_bb_max_min(bb_max, bb_min, hvertex);
			}
		}
	}

	center = (bb_max + bb_min) / 2;
	Vector3 translation = { center[0], center[1], bb_min[2] };
	//Vector3 translation = { 0, 0, 0 };
	//Vector3 collision_translation = { translation[0] * 0.1428f, translation[1] * 0.1428f, translation[2] * 0.1428f }; //oblivion scale
	Vector3 collision_translation = { translation[0] * 0.1428f, translation[1] * 0.1428f, translation[2] * 0.1428f }; //oblivion scale
	

	auto avroot = DynamicCast<NiNode>(root);
	hkTransform transform(TOMATRIX3(avroot->GetRotation()), TOVECTOR4(avroot->GetTranslation()));

	for (const auto& children : avroot->GetChildren()) {
		//move the whole first level hierarchy
		if (NULL == children)
			continue;
		if (children->IsDerivedType(NiAVObject::TYPE)) {
			auto avchildren = DynamicCast<NiAVObject>(children); 
			hkTransform avchildren_transform(TOMATRIX3(avchildren->GetRotation()), TOVECTOR4(avchildren->GetTranslation()));

			avchildren_transform.setMul(transform, avchildren_transform);

			avchildren->SetTranslation(
				TOMATRIX44(avchildren_transform).GetTrans() - translation
			);
			avchildren->SetRotation(
				TOMATRIX44(avchildren_transform).GetRotation().Transpose()
			);
			avchildren->SetScale(
				avroot->GetScale() * avchildren->GetScale()
			);
		}
	}

	std::function<bhkShapeRef(bhkShapeRef, const hkTransform&)> moveShape = [&](bhkShapeRef shape, const hkTransform& root_transform) -> bhkShapeRef {
		if (shape->IsSameType(bhkConvexTransformShape::TYPE)) 
		{
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkConvexTransformShape>(shape);
			hkTransform avchildren_transform(TOMATRIX4(obj->GetTransform()));
			avchildren_transform.setMul(transform, avchildren_transform);

			obj->SetTransform(TOMATRIX44(avchildren_transform));
			return obj;
		}

		else if (shape->IsSameType(bhkListShape::TYPE)) {

			auto obj = DynamicCast<bhkListShape>(shape);
			auto shapes = obj->GetSubShapes();
			for (int i = 0; i < shapes.size(); i++) {
				shapes[i] = moveShape(shapes[i], transform);
			}
			obj->SetSubShapes(shapes);
			return obj;
		}

		else if (shape->IsSameType(bhkConvexVerticesShape::TYPE)) {
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkConvexVerticesShape>(shape);
			vector<Vector4> vertices = obj->GetVertices();
			for (Vector4& v : vertices) {
				hkVector4 vv = TOVECTOR4(v);
				vv.setTransformedPos(transform, vv);
				v = TOVECTOR3(vv);
			}
			obj->SetVertices(vertices);
			return obj;
		}

		else if (shape->IsSameType(bhkMeshShape::TYPE)) {
			auto obj = DynamicCast<bhkMeshShape>(shape);
			float							factor(COLLISION_RATIO * 0.1428f);
			vector<NiTriStripsDataRef> shapes(obj->GetStripsData());

			for (auto& shape : shapes) {
				vector<Vector3> vertices(shape->GetVertices());
				for (auto& v : vertices) {
					hkVector4 vv = TOVECTOR4(v);
					vv.setTransformedPos(root_transform, vv);
					v = TOVECTOR3(vv);
				}
				shape->SetVertices(vertices);
			}
			return obj;
		}
		else if (shape->IsSameType(bhkNiTriStripsShape::TYPE)) {
			auto obj = DynamicCast<bhkNiTriStripsShape>(shape);
			vector<NiTriStripsDataRef> shapes(obj->GetStripsData());

			int shape_index = 0;

			for (auto& shape : shapes) {
				vector<Vector3> vertices(shape->GetVertices());
				for (auto& v : vertices) {
					hkVector4 vv = TOVECTOR4(v);
					vv.setTransformedPos(root_transform, vv);
					v = TOVECTOR3(vv);
				}
				shape->SetVertices(vertices);
			}
			return obj;
		}

		else if (shape->IsSameType(bhkSphereShape::TYPE)) {
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkSphereShape>(shape);
			bhkTransformShapeRef mover = new bhkConvexTransformShape();
			mover->SetShape(shape);
			mover->SetTransform(TOMATRIX44(transform));
			mover->SetMaterial(obj->GetMaterial());
			blocks.push_back(StaticCast<NiObject>(mover));
			return mover;
		}
		else if (shape->IsSameType(bhkBoxShape::TYPE)) {
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkBoxShape>(shape);
			bhkTransformShapeRef mover = new bhkConvexTransformShape();
			mover->SetShape(shape);
			mover->SetTransform(TOMATRIX44(transform));
			mover->SetMaterial(obj->GetMaterial());
			blocks.push_back(StaticCast<NiObject>(mover));
			return mover;
		}
		else if (shape->IsSameType(bhkCapsuleShape::TYPE)) {
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkCapsuleShape>(shape);
			hkVector4 vv = TOVECTOR4(obj->GetFirstPoint());
			vv.setTransformedPos(transform, vv);
			obj->SetFirstPoint(TOVECTOR3(vv));
			vv = TOVECTOR4(obj->GetSecondPoint());
			vv.setTransformedPos(transform, vv);
			obj->SetSecondPoint(TOVECTOR3(vv));

			return shape;
		}
		else if (shape->IsSameType(bhkMoppBvTreeShape::TYPE)) {
			auto obj = DynamicCast<bhkMoppBvTreeShape>(shape);
			moveShape(obj->GetShape(), root_transform);
			return obj;
		}
		else if (shape->IsSameType(bhkMultiSphereShape::TYPE)) {
			auto transform = root_transform;
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation())) * 0.1428f;
			transform.setTranslation(TOVECTOR4(collision_trans));

			auto obj = DynamicCast<bhkMultiSphereShape>(shape);
			auto spheres = obj->GetSpheres();
			for (auto& sphere : spheres) {
				hkVector4 vv = TOVECTOR4(sphere.center);
				vv.setTransformedPos(transform, vv);
				sphere.center = TOVECTOR3(vv);
			}
			obj->SetSpheres(spheres);
			return obj;
		}
		else {
			Log::Info("Collision Bug!");
		}
		return shape;
	};


	//Now move the root collision
	if (NULL != avroot->GetCollisionObject())
	{
		bhkCollisionObjectRef root_collision = DynamicCast<bhkCollisionObject>(avroot->GetCollisionObject());
		bhkRigidBodyRef body = DynamicCast<bhkRigidBody>(root_collision->GetBody());
		if (body->IsSameType(bhkRigidBodyT::TYPE)) {
			hkQTransform hbody(TOQUAT(body->GetRotation()), TOVECTOR4(body->GetTranslation()));
			::hkQuaternion hqr(TOMATRIX3(avroot->GetRotation()));
			hkQTransform hroot(hqr, TOVECTOR4(avroot->GetTranslation()));

			hroot.setMul(hroot, hbody);


			auto qrot = hroot.getRotation();

			Niflib::hkQuaternion f;
			f.x = qrot(0);
			f.y = qrot(1);
			f.z = qrot(2);
			f.w = qrot(3);
			body->SetRotation(
				f
			);

			body->SetTranslation(
				(TOVECTOR3(hroot.getTranslation()) - translation* 0.1428f)
			);

		}
		else {
			auto collision_matrix = TOMATRIX44(transform);
			auto collision_trans = (TOVECTOR3(transform.getTranslation()) - translation);
			transform.setTranslation(TOVECTOR4(collision_trans));
			body->SetShape(moveShape(body->GetShape(), transform));
		}
	}

	//TODO: adjust absolutes RB


	//remove transform from root node
	avroot->SetTranslation({});
	avroot->SetRotation({});
	avroot->SetScale(1.);

	return translation;
}

int is_clutter_or_furniture(vector<NiObjectRef>& blocks)
{
	for (const auto& block : blocks)
	{
		if (block->IsDerivedType(bhkRigidBody::TYPE)) {
			bhkRigidBodyRef ref = DynamicCast<bhkRigidBody>(block);
			if (ref->GetHavokFilter().layer_ob == OblivionLayer::OL_CLUTTER || ref->GetHavokFilter().layer_ob == OblivionLayer::OL_PROPS) {
				return 1;
			}
		}
		if (block->IsDerivedType(BSFurnitureMarker::TYPE)) {
			return 2;
		}
	}
	return 0;
}

void convert_blocks(
	vector<NiObjectRef>& blocks, 
	vector<NiObjectRef>& out_blocks, 
	NiObjectRef& root, 
	HKXWrapperCollection& wrappers,
	NifInfo& info,
	fs::path nif_file_path,
	fs::path exportPath,
	vector<pair<string, Vector3>>& metadata,
	bool doProxyRoot)
{

	//this is all hacky but ehhhh.
	bool isBillboardRoot = false;
	BillboardMode mode = BillboardMode::ALWAYS_FACE_CAMERA;

	//vector<NiObjectRef> blocks = ReadNifList(nifs[i].string().c_str(), &info);
	root = GetFirstRoot(blocks);

	Vector3 translation = { 0., 0., 0. };
	int is_clutter_or_furn = is_clutter_or_furniture(blocks);
	if (is_clutter_or_furn)
	{
		translation = center_model(root, blocks);
		metadata.push_back(
			{
				nif_file_path.filename().string(),
				translation
			}
		);
	}

	NiNode* rootn = DynamicCast<NiNode>(root);

	ConverterVisitor fimpl(info, root, blocks, translation, { 0.,0.,0. }, is_clutter_or_furn == 1, exportPath);

	if (root->IsSameType(NiBillboardNode::TYPE)) {
		isBillboardRoot = true;
		mode = DynamicCast<NiBillboardNode>(root)->GetBillboardMode();
	}

	info.userVersion = 12;
	info.userVersion2 = 83;
	info.version = Niflib::VER_20_2_0_7;
	vector<string> sequences = fimpl.nisequences;
	//if (!NifFile::hasExternalSkinnedMesh(blocks, rootn)) {
		root = convert_root(root);
		BSFadeNodeRef bsroot = DynamicCast<BSFadeNode>(root);
		//fixed?
		bsroot->SetFlags(524302);

		//TODO: fix along creatures creating animationdata/ animationsetdata
		//string out_havok_path = "";
		//if (!sequences.empty()) {
		//	fs::path in_file = nif_file_path.filename();
		//	string out_name = in_file.filename().replace_extension("").string();
		//	fs::path out_path = fs::path("tes4") / fs::path("animations") / in_file.parent_path() / out_name;
		//	fs::path out_path_abs = exportPath / out_path;
		//	if (exportPath.empty())
		//		out_path_abs = games.data(Games::TES5SE) / "meshes" / out_path;
		//	string out_path_a = out_path_abs.string();
		//	out_havok_path = wrappers.wrap(out_name, out_path.parent_path().string(), out_path_a, "TES4", sequences);
		//	vector<Ref<NiExtraData > > list = bsroot->GetExtraDataList();
		//	BSBehaviorGraphExtraDataRef havokp = new BSBehaviorGraphExtraData();
		//	havokp->SetName(string("BGED"));
		//	havokp->SetBehaviourGraphFile(out_havok_path);
		//	havokp->SetControlsBaseSkeleton(false);
		//	list.insert(list.begin(), StaticCast<NiExtraData>(havokp));
		//	bsroot->SetExtraDataList(list);
		//}

		if (!sequences.empty()) {
			if (sequences == vector<string>({ "Open", "Close" }))
			{
				//NTD, doors do not need behaviors
			}
			else
			{
				fs::path relative_path = fs::path("meshes") / fs::path("tes4") / fs::path("behaviors") / "two_idle_trans";
				fs::path out_path_abs = exportPath / relative_path;
				string BGED_file = "";

				if (exportPath.empty())
					out_path_abs = games.data(Games::TES5SE) / relative_path;

				if (sequences == vector<string>({ "SpecialIdle", "Forward", "Unequip", "Backward" })) {
					//Oblivion Gates!
					BGED_file = wrappers.wrap(
						"AutoPlayRagdoll", 
						relative_path.string(), 
						out_path_abs.string(), 
						"TES4", 
						HKXWrapper::DefaultBehaviors::autoplay_with_transitions
					);
				}
				else if (sequences == vector<string>({ "Left", "Equip", "SpecialIdle", "Forward", "Unequip", "Backward" })) {
					//Oblivion Gates!

				}
				else if (sequences == vector<string>({ "Equip", "Forward", "Unequip" })) {
					//Oblivion Gates!

				}

				if (!BGED_file.empty())
				{
					vector<Ref<NiExtraData > > list = bsroot->GetExtraDataList();
					BSBehaviorGraphExtraDataRef havokp = new BSBehaviorGraphExtraData();
					havokp->SetName(string("BGED"));
					havokp->SetBehaviourGraphFile(BGED_file);
					havokp->SetControlsBaseSkeleton(false);
					list.insert(list.begin(), StaticCast<NiExtraData>(havokp));
					bsroot->SetExtraDataList(list);
				}
			}
		}

		std::vector<NiAVObjectRef> children;

		if (isBillboardRoot) //fxmistoblivion01 has NiBillboardNode root
		{
			NiBillboardNode* proxyRoot = new NiBillboardNode();
			if (mode == ROTATE_ABOUT_UP)
				mode = BSROTATE_ABOUT_UP;
			proxyRoot->SetBillboardMode(mode);
			proxyRoot->SetFlags(bsroot->GetFlags());
			proxyRoot->SetChildren(bsroot->GetChildren());
			proxyRoot->SetRotation(bsroot->GetRotation());
			proxyRoot->SetTranslation(bsroot->GetTranslation());
			proxyRoot->SetCollisionObject(bsroot->GetCollisionObject());

			if (proxyRoot->GetCollisionObject() != NULL)
				DynamicCast<NiCollisionObject>(proxyRoot->GetCollisionObject())->SetTarget(proxyRoot);

			children.push_back(proxyRoot);
			bsroot->SetChildren(children);
			bsroot->SetCollisionObject(NULL);
		}
		else
		{
			//bsroot->SetTranslation(displacement);

			if ((bsroot->GetTranslation().Magnitude() != 0 ||
				!bsroot->GetRotation().isIdentity()) && doProxyRoot)
			{
				NiNode* proxyRoot = new NiNode();
				proxyRoot->SetName(IndexString("ProxyRoot"));
				proxyRoot->SetFlags(bsroot->GetFlags());
				proxyRoot->SetChildren(bsroot->GetChildren());
				proxyRoot->SetRotation(bsroot->GetRotation());
				proxyRoot->SetTranslation(bsroot->GetTranslation());
				proxyRoot->SetCollisionObject(bsroot->GetCollisionObject());

				if (proxyRoot->GetCollisionObject() != NULL)
					DynamicCast<NiCollisionObject>(proxyRoot->GetCollisionObject())->SetTarget(proxyRoot);

				children.push_back(proxyRoot);
				bsroot->SetChildren(children);
				bsroot->SetCollisionObject(NULL);

				bsroot->SetRotation(Matrix33());
				bsroot->SetTranslation(Vector3());
			}
		}





		//to calculate the right flags, we need to rebuild the blocks
		vector<NiObjectRef> new_blocks = RebuildVisitor(root, info).blocks;

		//fix targets from nitrishapes substitution
		FixTargetsVisitor(root, info, new_blocks);

		if (DynamicCast<NiNode>(root) != NULL && DynamicCast<NiNode>(root)->GetCollisionObject() == NULL) {
			bhkCollisionObjectRef root_collision = NULL;
			int num_collisions = 0;
			//Optimize single collision models
			for (NiObjectRef block : new_blocks) {
				if (block->IsDerivedType(bhkCollisionObject::TYPE))
				{
					num_collisions++;
					root_collision = DynamicCast<bhkCollisionObject>(block);
				}
			}
			if (num_collisions == 1 && root_collision != NULL) {

				vector<NiAVObjectRef> children = bsroot->GetChildren();
				auto root_collision_position = find(children.begin(), children.end(), StaticCast<NiAVObject>(root_collision));
				if (root_collision_position != children.end()) {
					children.erase(root_collision_position);
					bsroot->SetCollisionObject(StaticCast<NiCollisionObject>(root_collision));
					bsroot->SetChildren(children);
				}
			}
		}


		bsx_flags_t calculated_flags = calculateSkyrimBSXFlags(new_blocks, info);

		bool bsx_flag_found = false;
		for (NiObjectRef ref : blocks) {
			if (ref->IsDerivedType(BSXFlags::TYPE)) {
				BSXFlagsRef bref = DynamicCast<BSXFlags>(ref);
				bref->SetIntegerData(calculated_flags.to_ulong());
				bsx_flag_found = true;
				break;
			}
		}
		if (!bsx_flag_found && calculated_flags != 0)
		{
			BSXFlagsRef bref = new BSXFlags();
			bref->SetIntegerData(calculated_flags.to_ulong());
			vector<NiExtraDataRef> list = bsroot->GetExtraDataList();
			list.push_back(StaticCast<NiExtraData>(bref));
			bsroot->SetExtraDataList(list);
		}
		bool human_attachment = false;
		for (const auto& block : new_blocks)
		{
			if (block->IsDerivedType(BSInvMarker::TYPE))
			{
				human_attachment = true;
				break;
			}
			/*
			Shields: Bip01 L ForearmTwist

			For Hair Bip01Head

			One - Handed weapons : SideWeapon

			Bows and Two - Handed weapons : BackWeapon

			Torches and similar : Torch
			*/
		}
		if (human_attachment)
		{
			for (auto& block : new_blocks)
			{
				if (block->IsDerivedType(NiStringExtraData::TYPE))
				{
					NiStringExtraDataRef ed = DynamicCast<NiStringExtraData>(block);
					if (ed->GetName().find("Prn") != string::npos)
					{
						if (ed->GetStringData().find("Bip01 L ForearmTwist")!= string::npos)
							ed->SetStringData(IndexString("SHIELD"));
						if (ed->GetStringData().find("SideWeapon") != string::npos)
							ed->SetStringData(IndexString("WeaponSword"));
						if (ed->GetStringData().find("BackWeapon") != string::npos)
							ed->SetStringData(IndexString("WeaponBack"));
					}
				}
			}
		}
		out_blocks = new_blocks;
	//}
	//else {
	//	//TODO
	//	//FixTargetsVisitor(root, info, blocks);
	//	//Attach to skyrim skeleton
	//	
	//}
}

// CREA entries were split
// RACE(WNAM) -> ARMO {ARMA *} collect the physical properties

void findCreatures
(
	std::map<fs::path, std::set<fs::path>>& skeletons,
	std::map<fs::path, std::set<Ob::CREARecord*>>& actors,
	map<fs::path, map<set<string>, set< Ob::CREARecord*>>>& skins,
	Collection& oblivionCollection,
	const std::string& oblivionData
)
{
	std::map<fs::path, std::set<Ob::CREARecord*>> test_actors;

	for (auto record_it = oblivionCollection.FormID_ModFile_Record.begin(); record_it != oblivionCollection.FormID_ModFile_Record.end(); record_it++)
	{
		Record* record = record_it->second;
		if (record->GetType() == REV32(CREA)) {
			Ob::CREARecord* creature = dynamic_cast<Ob::CREARecord*>(record);
			if (nullptr != creature && creature->MODL.value != nullptr)
			{
				if (std::string(creature->MODL.value->MODL.value).find("Goblin") != string::npos)
				{
					std::string skeleton = std::string("meshes\\") + creature->MODL.value->MODL.value;
					transform(skeleton.begin(), skeleton.end(), skeleton.begin(), ::tolower);
					actors[skeleton].insert(creature);
					Log::Info("Found NPC entry: %s, skeleton to be converted: %s", creature->EDID.value, skeleton.c_str());
					set<string> models;
					set<string> models_lowercase;
					for (const auto& model : creature->NIFZ.value)
					{
						std::string s_model_upper(model);
						std::string s_model(model);
						transform(s_model.begin(), s_model.end(), s_model.begin(), tolower);
						//draugh.nif doesn't exist
						if (s_model == "dreugh.nif")
						{
							continue;
						}
						//storm atronach uses skeleton.nif as mesh
						if (s_model == "skeleton.nif")
						{
							continue;
						}
						//knighs missing some prefixes
						if (s_model == "ayleid.nif")
						{
							s_model = "ndayleid.nif";
							s_model_upper = "ndayleid.nif";
						}
						if (s_model == "ayleidlegs.nif")
						{
							s_model = "ndayleidlegs.nif";
							s_model_upper = "ndayleidlegs.nif";
						}
						if (s_model == "greaves.nif")
						{
							s_model = "ndgreaves.nif";
							s_model_upper = "ndgreaves.nif";
						}
						if (s_model == "minionhead.nif")
						{
							s_model = "ndminionhead.nif";
							s_model_upper = "ndminionhead.nif";
						}
						if (s_model == "bosshead.nif")
						{
							s_model = "ndbosshead.nif";
							s_model_upper = "ndbosshead.nif";
						}
						if (s_model == "wingpouldrons.nif")
						{
							s_model = "ndwingpouldrons.nif";
							s_model_upper = "ndwingpouldrons.nif";
						}
						if (s_model == "minionhead.nif")
						{
							s_model = "ndminionhead.nif";
							s_model_upper = "ndminionhead.nif";
						}
						if (std::string(creature->EDID.value) == "MQ07TestXivilaiAmorah")
						{
							s_model = "xivilai.nif";
							s_model_upper = "xivilai.nif";
						}
						skeletons[skeleton].insert(s_model_upper);
						models.insert(s_model_upper);
						models_lowercase.insert(s_model);
					}
					skins[skeleton][models_lowercase].insert(creature);
				}
			}
		}
	}
}

void ConvertCreatures
(
	const std::string& prefix,
	std::map<fs::path, std::set<Ob::CREARecord*>>& actors,
	std::map<fs::path, Sk::RACERecord*>& races,
	map<fs::path, map<set<string>, set< Ob::CREARecord*>>>& skins,
	Collection& oblivionCollection,
	Collection& convertedCollection,
	ModFile* convertedPlugin
)
{
	for (const auto& entry : actors)
	{
		std::string creature_tag = entry.first.parent_path().filename().string();
		creature_tag[0] = toupper(creature_tag[0]);
		std::string RACE_EDID = prefix + creature_tag + "Race";
		Sk::RACERecord* race = (Sk::RACERecord*)convertedCollection.CreateRecord(
			convertedPlugin, 
			REV32(RACE), 
			NULL, 
			(char* const)RACE_EDID.c_str(), 
			NULL, 
			0
		);
		race->EDID = RACE_EDID;
		race->FULL = prefix + " " + creature_tag + " Race";
		if (actors.at(entry.first).empty())
		{
			Log::Error("Race without actors: %s", RACE_EDID.c_str());
		}
		else {
			std::string description = "Race used by original actors: ";
			for (const auto& actor : actors.at(entry.first))
			{
				description += std::string(actor->EDID.value) + ", ";
			}
			description = description.substr(0, description.size() - 2);
			race->DESC = description;
		}
		race->DATA.value.maleHeight = 1.;
		race->DATA.value.femaleHeight = 1.;
		race->DATA.value.maleWeight = 1.;
		race->DATA.value.femaleWeight = 1.;
		race->DATA.value.baseCarryWeight = 9999.0;
		race->DATA.value.baseMass = 1.;
		race->DATA.value.accelerationRate = 1.;
		race->DATA.value.decelerationRate = 1.;
		race->DATA.value.size = 1;
		race->DATA.value.unarmedDamage = 1.;
		race->DATA.value.unarmedReach = 96.;
		race->DATA.value.angularAccelerationRate = 0.12;
		race->DATA.value.angularAccelerationRate = 5.0;
		race->DATA.value.flags2 = 0x00000001; // Use Advanced Avoidance

		//SLOTS
		race->DATA.value.headBipedObject = -1;
		race->DATA.value.hairBipedObject = -1;
		race->DATA.value.shieldBipedObject = -1;
		race->DATA.value.bodyBipedObject = -1;

		races[entry.first] = race;
	}
}

std::vector<Ref<NiObject>>  load_override_or_bsa_nif_file(const fs::path& path, const fs::path& overridePath, NifInfo& info)
{
	fs::path absolute = overridePath / path;

	if (fs::exists(absolute)) {
		return ReadNifList(absolute.string().c_str(), &info);
	}
	else {
		if (games.bsa_files().empty())
		{
			games.loadBsas(Games::TES4);
		}
		for (const auto& bsa_file : games.bsa_files()) {
			bool found = bsa_file.find(path.string());
			if (found)
			{
				//get the shortest path
				size_t size = -1;
				const uint8_t* data = bsa_file.extract(path.string(), size);
				std::string sdata((char*)data, size);
				std::istringstream iss(sdata);
				auto result = ReadNifList(iss, &info);
				delete data;
				return result;
			}
		}
	}
	throw runtime_error("File not found:" + path.string());
	return {};
}


void replacepath(std::string& str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // ...
	}
}

NifFolderType load_override_or_bsa_nif_folder(const fs::path& skeleton_path, const fs::path& overridePath, NifInfo& info)
{
	NifFolderType out;
	fs::path path = skeleton_path.parent_path();
	if (games.bsa_files().empty())
	{
		games.loadBsas(Games::TES4);
	}
	for (const auto& bsa_file : games.bsa_files()) {
		std::string ex = ".*" + path.string() + "\.*\.nif";
		replacepath(ex, "\\", "\\\\");
		auto assets = bsa_file.assets(ex);
		for (const auto& asset : assets)
		{
			size_t size = -1;
			const uint8_t* data = bsa_file.extract(asset, size);
			std::string sdata((char*)data, size);
			std::istringstream iss(sdata);
			auto result = ReadNifList(iss, &info);
			if (fs::path(asset) == skeleton_path)
			{
				std::get<0>(out) = result;
			}
			else {
				std::get<1>(out)[asset] = (result);
			}
			delete data;
		}
		ex = ".*" + path.string() + "\.*\.kf";
		replacepath(ex, "\\", "\\\\");
		assets = bsa_file.assets(ex);
		for (const auto& asset : assets)
		{
			size_t size = -1;
			const uint8_t* data = bsa_file.extract(asset, size);
			std::string sdata((char*)data, size);
			std::istringstream iss(sdata);
			auto result = ReadNifList(iss, &info);
			std::get<2>(out)[asset] = (result);
			delete data;
		}
	}
	//replace overrides
	fs::path absolute = overridePath / path;
	if (fs::exists(absolute))
	{
		vector<fs::path> assets;
		findFiles(absolute, ".nif", assets);
		for (const auto& asset : assets)
		{
			auto result = ReadNifList(asset.string(), &info);
			if (fs::path(asset) == skeleton_path)
			{
				std::get<0>(out) = result;
			}
			else {
				std::get<1>(out)[asset] = (result);
			}
		}
		assets.clear();
		findFiles(absolute, ".kf", assets);
		for (const auto& asset : assets)
		{
			auto result = ReadNifList(asset.string(), &info);
			std::get<2>(out)[asset] = (result);
		}
	}
	return out;
}

//Movement Cycles:
//Default
//Animations\idle.hkx



class AnimationSetAnalyzer
{
	const std::map<fs::path, ckcmd::HKX::RootMovement>& animations;
public:
	enum class GETUP_DIRECTION {
		face_up = 0,
		face_down,
		left,
		right,
		lift
	};

	enum class MOVE_DIRECTION {
		idle = 0,
		forward,
		backward,
		left,
		right,
		all
	};

	enum class TURN_DIRECTION {
		left = 0,
		right,
		all
	};

	enum class RUN_DIRECTION {
		forward = 0,
		backward,
		all
	};

	enum class JUMP_STATE {
		start = 0,
		loop,
		land
	};

	enum class STANCE_STATE {
		equip = 0,
		block,
		blockidle,
		blockhit,
		stagger,
		recoil,
		unequip,
		death
	};
private:
	std::map<GETUP_DIRECTION, fs::path> getup;

	std::map<MOVE_DIRECTION, fs::path> default_move;
	std::map<MOVE_DIRECTION, fs::path> bow_move;
	std::map<MOVE_DIRECTION, fs::path> swim_move;
	std::map<MOVE_DIRECTION, fs::path> h2h_move;
	std::map<MOVE_DIRECTION, fs::path> swimh2h_move;
	std::map<MOVE_DIRECTION, fs::path> swim1h_move;
	std::map<MOVE_DIRECTION, fs::path> oneh_move;
	std::map<MOVE_DIRECTION, fs::path> staff_move;
	std::map<MOVE_DIRECTION, fs::path> twoh_move;

	std::map<TURN_DIRECTION, fs::path> default_turn;
	std::map<TURN_DIRECTION, fs::path> bow_turn;
	std::map<TURN_DIRECTION, fs::path> swim_turn;
	std::map<TURN_DIRECTION, fs::path> h2h_turn;
	std::map<TURN_DIRECTION, fs::path> swimh2h_turn;
	std::map<TURN_DIRECTION, fs::path> swim1h_turn;
	std::map<TURN_DIRECTION, fs::path> oneh_turn;
	std::map<TURN_DIRECTION, fs::path> staff_turn;
	std::map<TURN_DIRECTION, fs::path> twoh_turn;

	std::map<RUN_DIRECTION, fs::path> default_run; // h2h for multiple stances
	std::map<RUN_DIRECTION, fs::path> bow_run;
	std::map<RUN_DIRECTION, fs::path> swim_run;
	std::map<RUN_DIRECTION, fs::path> h2h_run;
	std::map<RUN_DIRECTION, fs::path> swimh2h_run;
	std::map<RUN_DIRECTION, fs::path> swim1h_run;
	std::map<RUN_DIRECTION, fs::path> oneh_run;
	std::map<RUN_DIRECTION, fs::path> staff_run;
	std::map<RUN_DIRECTION, fs::path> twoh_run;

	std::map<JUMP_STATE, fs::path> jump;

	std::map<STANCE_STATE, fs::path> h2h_stance;
	std::map<STANCE_STATE, fs::path> bow_stance;
	std::map<STANCE_STATE, fs::path> swim_stance;
	std::map<STANCE_STATE, fs::path> swimh2h_stance;
	//std::map<STANCE_STATE, fs::path> swim1h_stance;
	std::map<STANCE_STATE, fs::path> oneh_stance;
	std::map<STANCE_STATE, fs::path> staff_stance;
	std::map<STANCE_STATE, fs::path> twoh_stance;

	std::vector<fs::path> additional_idles;

	void analyzeGetUp()
	{
		//	Animations\idleanims\getupfaceup.hkx
		//	Animations\idleanims\getup_faceup.hkx
		if (hasAnimation("Animations\\idleanims\\getupfaceup.hkx"))
			getup[GETUP_DIRECTION::face_up] = "Animations\\idleanims\\getupfaceup.hkx";
		if (hasAnimation("Animations\\idleanims\\getupfaceup.hkx"))
			getup[GETUP_DIRECTION::face_up] = "Animations\\idleanims\\getupfaceup.hkx";
		
		//	Animations\idleanims\getupfacedown.hkx
		//  Animations\idleanims\getup_facedown.hkx
		if (hasAnimation("Animations\\idleanims\\getupfacedown.hkx"))
			getup[GETUP_DIRECTION::face_down] = "Animations\\idleanims\\getupfacedown.hkx";
		if (hasAnimation("Animations\\idleanims\\getup_facedown.hkx"))
			getup[GETUP_DIRECTION::face_down] = "Animations\\idleanims\\getup_facedown.hkx";
		
		//	Animations\idleanims\getupleft.hkx
		//	Animations\idleanims\getup_left.hkx
		//  Animations\idleanims\specialidle_getupleft.hkx
		if (hasAnimation("Animations\\idleanims\\getupleft.hkx"))
			getup[GETUP_DIRECTION::left] = "Animations\\idleanims\getupleft.hkx";
		if (hasAnimation("Animations\\idleanims\getup_left.hkx"))
			getup[GETUP_DIRECTION::left] = "Animations\\idleanims\getup_left.hkx";
		if (hasAnimation("Animations\\idleanims\specialidle_getupleft.hkx"))
			getup[GETUP_DIRECTION::left] = "Animations\\idleanims\specialidle_getupleft.hkx";
		
		//	Animations\idleanims\getupright.hkx
		//	Animations\idleanims\getup_right.hkx
		//	Animations\idleanims\specialidle_getupright.hkx
		if (hasAnimation("Animations\\idleanims\\getupright.hkx"))
			getup[GETUP_DIRECTION::right] = "Animations\\idleanims\\getupright.hkx";
		if (hasAnimation("Animations\\idleanims\\getup_right.hkx"))
			getup[GETUP_DIRECTION::right] = "Animations\\idleanims\\getup_right.hkx";
		if (hasAnimation("Animations\\idleanims\\specialidle_getupright.hkx"))
			getup[GETUP_DIRECTION::right] = "Animations\\idleanims\\specialidle_getupright.hkx";

		//	Animations\idleanims\specialidle_getuplift.hkx
		if (hasAnimation("Animations\\idleanims\\specialidle_getuplift.hkx"))
			getup[GETUP_DIRECTION::lift] = "Animations\\idleanims\\specialidle_getuplift.hkx";
	}

	void analyzeWalking()
	{
		// WALK DEFAULT
		//Animations\idle.hkx
		//Animations\walkforward.hkx / Animations\forward.hkx / Animations\forwardwalk.hkx
		//Animations\backward.hkx / Animations\backwardwalk.hkx
		//Animations\left.hkx
		//Animations\right.hkx
		if (hasAnimation("Animations\\idle.hkx"))
			default_move[MOVE_DIRECTION::idle] = "Animations\\idle.hkx";

		if (hasAnimation("Animations\\walkforward.hkx"))
			default_move[MOVE_DIRECTION::forward] = "Animations\\walkforward.hkx";
		if (hasAnimation("Animations\\forward.hkx"))
			default_move[MOVE_DIRECTION::forward] = "Animations\\forward.hkx";
		if (hasAnimation("Animations\\forwardwalk.hkx"))
			default_move[MOVE_DIRECTION::forward] = "Animations\\forwardwalk.hkx";

		if (hasAnimation("Animations\\backward.hkx"))
			default_move[MOVE_DIRECTION::backward] = "Animations\\backward.hkx";
		if (hasAnimation("Animations\\backwardwalk.hkx"))
			default_move[MOVE_DIRECTION::backward] = "Animations\\backwardwalk.hkx";

		if (hasAnimation("Animations\\left.hkx"))
			default_move[MOVE_DIRECTION::left] = "Animations\\left.hkx";

		if (hasAnimation("Animations\\right.hkx"))
			default_move[MOVE_DIRECTION::right] = "Animations\\right.hkx";

		// SWIM
		//	Animations\swimidle.hkx
		//	Animations\swimforward.hkx
		//  Animations\swimbackward.hkx
		//	Animations\swimleft.hkx
		//	Animations\swimright.hkx
		if (hasAnimation("Animations\\swimidle.hkx"))
			swim_move[MOVE_DIRECTION::idle] = "Animations\\swimidle.hkx";

		if (hasAnimation("Animations\\swimforward.hkx"))
			swim_move[MOVE_DIRECTION::forward] = "Animations\\swimforward.hkx";

		if (hasAnimation("Animations\\swimbackward.hkx"))
			swim_move[MOVE_DIRECTION::backward] = "Animations\\swimbackward.hkx";

		if (hasAnimation("Animations\\swimleft.hkx"))
			swim_move[MOVE_DIRECTION::left] = "Animations\\swimleft.hkx";

		if (hasAnimation("Animations\\swimright.hkx"))
			swim_move[MOVE_DIRECTION::right] = "Animations\\swimright.hkx";


		//BOW
		//	Animations\bowidle.hkx
		//	Animations\bowforward.hkx
		//	Animations\bowbackward.hkx
		//	Animations\bowleft.hkx
		//	Animations\bowright.hkx
		if (hasAnimation("Animations\\bowidle.hkx"))
			bow_move[MOVE_DIRECTION::idle] = "Animations\\bowidle.hkx";

		if (hasAnimation("Animations\\bowforward.hkx"))
			bow_move[MOVE_DIRECTION::forward] = "Animations\\bowforward.hkx";

		if (hasAnimation("Animations\\bowbackward.hkx"))
			bow_move[MOVE_DIRECTION::backward] = "Animations\\bowbackward.hkx";

		if (hasAnimation("Animations\\bowleft.hkx"))
			bow_move[MOVE_DIRECTION::left] = "Animations\\bowleft.hkx";

		if (hasAnimation("Animations\\bowright.hkx"))
			bow_move[MOVE_DIRECTION::right] = "Animations\\bowright.hkx";

		// H2H
		//	Animations\handtohandidle.hkx /Animations\handtohandilde.hkx
		//	Animations\handtohandforward.hkx /Animations\handtohandforwardwalk.hkx
		//	Animations\handtohandback.hkx / Animations\handtohandbackward.hkx
		//	Animations\handtohandleft.hkx
		//	Animations\handtohandright.hkx
		if (hasAnimation("Animations\\handtohandidle.hkx"))
			h2h_move[MOVE_DIRECTION::idle] = "Animations\\handtohandidle.hkx";
		if (hasAnimation("Animations\\handtohandilde.hkx"))
			h2h_move[MOVE_DIRECTION::idle] = "Animations\\handtohandilde.hkx";

		if (hasAnimation("Animations\\handtohandforward.hkx"))
			h2h_move[MOVE_DIRECTION::forward] = "Animations\\handtohandforward.hkx";
		if (hasAnimation("Animations\\handtohandforwardwalk.hkx"))
			h2h_move[MOVE_DIRECTION::forward] = "Animations\\handtohandforwardwalk.hkx";

		if (hasAnimation("Animations\\handtohandback.hkx"))
			h2h_move[MOVE_DIRECTION::backward] = "Animations\\handtohandback.hkx";
		if (hasAnimation("Animations\\handtohandbackward.hkx"))
			h2h_move[MOVE_DIRECTION::backward] = "Animations\\handtohandbackward.hkx";

		if (hasAnimation("Animations\\handtohandleft.hkx"))
			h2h_move[MOVE_DIRECTION::left] = "Animations\\handtohandleft.hkx";

		if (hasAnimation("Animations\\handtohandright.hkx"))
			h2h_move[MOVE_DIRECTION::right] = "Animations\\handtohandright.hkx";

		//SWIM H2H
		//  Animations\swimhandtohandidle.hkx
		//	Animations\swimhandtohandforward.hkx
		//	Animations\swimhandtohandbackward.hkx
		//	Animations\swimhandtohandleft.hkx
		//	Animations\swimhandtohandright.hkx
		if (hasAnimation("Animations\\swimhandtohandidle.hkx"))
			swimh2h_move[MOVE_DIRECTION::idle] = "Animations\\swimhandtohandidle.hkx";

		if (hasAnimation("Animations\\swimhandtohandforward.hkx"))
			swimh2h_move[MOVE_DIRECTION::forward] = "Animations\\swimhandtohandforward.hkx";

		if (hasAnimation("Animations\\swimhandtohandbackward.hkx"))
			swimh2h_move[MOVE_DIRECTION::backward] = "Animations\\swimhandtohandbackward.hkx";

		if (hasAnimation("Animations\\swimhandtohandleft.hkx"))
			swimh2h_move[MOVE_DIRECTION::left] = "Animations\\swimhandtohandleft.hkx";

		if (hasAnimation("Animations\\swimhandtohandright.hkx"))
			swimh2h_move[MOVE_DIRECTION::right] = "Animations\\swimhandtohandright.hkx";

		// 1H
		//  Animations\onehandidle.hkx
		//	Animations\onehandforward.hkx
		//	Animations\onehandbackward.hkx
		//	Animations\onehandleft.hkx
		//	Animations\onehandright.hkx
		if (hasAnimation("Animations\\onehandidle.hkx"))
			oneh_move[MOVE_DIRECTION::idle] = "Animations\\onehandidle.hkx";

		if (hasAnimation("Animations\\onehandforward.hkx"))
			oneh_move[MOVE_DIRECTION::forward] = "Animations\\onehandforward.hkx";

		if (hasAnimation("Animations\\onehandbackward.hkx"))
			oneh_move[MOVE_DIRECTION::backward] = "Animations\\onehandbackward.hkx";

		if (hasAnimation("Animations\\onehandleft.hkx"))
			oneh_move[MOVE_DIRECTION::left] = "Animations\\onehandleft.hkx";

		if (hasAnimation("Animations\\onehandright.hkx"))
			oneh_move[MOVE_DIRECTION::right] = "Animations\\onehandright.hkx";

		//SWIM 1H
		//	Animations\swimonehandidle.hkx
		//	Animations\swimonehandforward.hkx
		//  Animations\swimonehandbackward.hkx
		if (hasAnimation("Animations\\swimonehandidle.hkx"))
			swim1h_move[MOVE_DIRECTION::idle] = "Animations\\swimonehandidle.hkx";

		if (hasAnimation("Animations\\swimonehandforward.hkx"))
			swim1h_move[MOVE_DIRECTION::forward] = "Animations\\swimonehandforward.hkx";

		if (hasAnimation("Animations\\swimonehandbackward.hkx"))
			swim1h_move[MOVE_DIRECTION::backward] = "Animations\\swimonehandbackward.hkx";

		//STAFF
		//  Animations\staffidle.hkx
		//	Animations\staffforward.hkx
		//	Animations\staffbackward.hkx
		//	Animations\staffleft.hkx
		//	Animations\staffright.hkx
		if (hasAnimation("Animations\\staffidle.hkx"))
			staff_move[MOVE_DIRECTION::idle] = "Animations\\staffidle.hkx";

		if (hasAnimation("Animations\\staffforward.hkx"))
			staff_move[MOVE_DIRECTION::forward] = "Animations\\staffforward.hkx";

		if (hasAnimation("Animations\\staffbackward.hkx"))
			staff_move[MOVE_DIRECTION::backward] = "Animations\\staffbackward.hkx";

		if (hasAnimation("Animations\\staffleft.hkx"))
			staff_move[MOVE_DIRECTION::left] = "Animations\\staffleft.hkx";

		if (hasAnimation("Animations\\staffright.hkx"))
			staff_move[MOVE_DIRECTION::right] = "Animations\\staffright.hkx";

		//2H
		//  Animations\twohandidle.hkx
		//	Animations\twohandforward.hkx
		//	Animations\twohandbackward.hkx
		//	Animations\twohandleft.hkx
		//	Animations\twohandright.hkx
		if (hasAnimation("Animations\\twohandidle.hkx"))
			twoh_move[MOVE_DIRECTION::idle] = "Animations\\twohandidle.hkx";

		if (hasAnimation("Animations\\twohandforward.hkx"))
			twoh_move[MOVE_DIRECTION::forward] = "Animations\\twohandforward.hkx";

		if (hasAnimation("Animations\\twohandbackward.hkx"))
			twoh_move[MOVE_DIRECTION::backward] = "Animations\\twohandbackward.hkx";

		if (hasAnimation("Animations\\twohandleft.hkx"))
			twoh_move[MOVE_DIRECTION::left] = "Animations\\twohandleft.hkx";

		if (hasAnimation("Animations\\twohandright.hkx"))
			twoh_move[MOVE_DIRECTION::right] = "Animations\\twohandright.hkx";

	}

	void analyzeTurning()
	{
		//TURN DEFAULT
		//  Animations\turnleft.hkx
		//  Animations\turnright.hkx
		if (hasAnimation("Animations\\turnleft.hkx"))
			default_turn[TURN_DIRECTION::left] = "Animations\\turnleft.hkx";

		if (hasAnimation("Animations\\turnright.hkx"))
			default_turn[TURN_DIRECTION::right] = "Animations\\turnright.hkx";

		//TURN BOW
		//  Animations\bowturnleft.hkx
		//  Animations\bowturnright.hkx
		if (hasAnimation("Animations\\bowturnleft.hkx"))
			bow_turn[TURN_DIRECTION::left] = "Animations\\bowturnleft.hkx";

		if (hasAnimation("Animations\\bowturnright.hkx"))
			bow_turn[TURN_DIRECTION::right] = "Animations\\bowturnright.hkx";

		//TURN SWIM
		//	Animations\swimturnleft.hkx
		//	Animations\swimturnright.hkx
		if (hasAnimation("Animations\\swimturnleft.hkx"))
			swim_turn[TURN_DIRECTION::left] = "Animations\\swimturnleft.hkx";

		if (hasAnimation("Animations\\swimturnright.hkx"))
			swim_turn[TURN_DIRECTION::right] = "Animations\\swimturnright.hkx";

		//H2H TURN
		//	Animations\handtohandturnleft.hkx
		//	Animations\handtohandturnright.hkx
		if (hasAnimation("Animations\\handtohandturnleft.hkx"))
			h2h_turn[TURN_DIRECTION::left] = "Animations\\handtohandturnleft.hkx";

		if (hasAnimation("Animations\\handtohandturnright.hkx"))
			h2h_turn[TURN_DIRECTION::right] = "Animations\\handtohandturnright.hkx";

		//SWIM H2H TURN
		//  Animations\swimhandtohandturnleft.hkx
		//	Animations\swimhandtohandturnright.hkx
		if (hasAnimation("Animations\\swimhandtohandturnleft.hkx"))
			swimh2h_turn[TURN_DIRECTION::left] = "Animations\\swimhandtohandturnleft.hkx";

		if (hasAnimation("Animations\\swimhandtohandturnright.hkx"))
			swimh2h_turn[TURN_DIRECTION::right] = "Animations\\swimhandtohandturnright.hkx";

		//1H TURN
		//  Animations\onehandturnleft.hkx
		//	Animations\onehandturnright.hkx
		if (hasAnimation("Animations\\onehandturnleft.hkx"))
			oneh_turn[TURN_DIRECTION::left] = "Animations\\onehandturnleft.hkx";

		if (hasAnimation("Animations\\onehandturnright.hkx"))
			oneh_turn[TURN_DIRECTION::right] = "Animations\\onehandturnright.hkx";

		//SWIM 1H TURN
		//	Animations\swimonehandturnleft.hkx
		//	Animations\swimonehandturnright.hkx
		if (hasAnimation("Animations\\swimonehandturnleft.hkx"))
			swim1h_turn[TURN_DIRECTION::left] = "Animations\\swimonehandturnleft.hkx";

		if (hasAnimation("Animations\\swimonehandturnright.hkx"))
			swim1h_turn[TURN_DIRECTION::right] = "Animations\\swimonehandturnright.hkx";

		//STAFF TURN
		// Animations\staffturnleft.hkx
		// Animations\staffturnright.hkx
		if (hasAnimation("Animations\\staffturnleft.hkx"))
			staff_turn[TURN_DIRECTION::left] = "Animations\\staffturnleft.hkx";

		if (hasAnimation("Animations\\staffturnright.hkx"))
			staff_turn[TURN_DIRECTION::right] = "Animations\\staffturnright.hkx";

		//TURN 2H
		//  Animations\twohandturnleft.hkx
		//	Animations\twohandturnright.hkx
		if (hasAnimation("Animations\\twohandturnleft.hkx"))
			staff_turn[TURN_DIRECTION::left] = "Animations\\twohandturnleft.hkx";

		if (hasAnimation("Animations\\twohandturnright.hkx"))
			staff_turn[TURN_DIRECTION::right] = "Animations\\twohandturnright.hkx";
	}

	void analyzeRunning()
	{
		// DEFAUT RUN
		//Animations\fastforward.hkx
		//Animations\fastfoward.hkx
		//Animations\runforward.hkx
		//Animations\fastbackward.hkx
		if (hasAnimation("Animations\\fastforward.hkx"))
			default_run[RUN_DIRECTION::forward] = "Animations\\fastforward.hkx";
		if (hasAnimation("Animations\\fastfoward.hkx"))
			default_run[RUN_DIRECTION::forward] = "Animations\\fastfoward.hkx";
		if (hasAnimation("Animations\\runforward.hkx"))
			default_run[RUN_DIRECTION::forward] = "Animations\\runforward.hkx";

		if (hasAnimation("Animations\\fastbackward.hkx"))
			default_run[RUN_DIRECTION::backward] = "Animations\\fastbackward.hkx";

		// BOW  
		//	Animations\bowfastforward.hkx
		if (hasAnimation("Animations\\bowfastforward.hkx"))
			bow_run[RUN_DIRECTION::forward] = "Animations\\bowfastforward.hkx";
		
		// SWIM
		//	Animations\swimfastforward.hkx
		if (hasAnimation("Animations\\swimfastforward.hkx"))
			swim_run[RUN_DIRECTION::forward] = "Animations\\swimfastforward.hkx";

		// H2H
		//	Animations\handtohandfastforward.hkx
		if (hasAnimation("Animations\\handtohandfastforward.hkx"))
			h2h_run[RUN_DIRECTION::forward] = "Animations\\handtohandfastforward.hkx";
		
		// SWIM H2H
		//  Animations\swimhandtohandfastforward.hkx
		if (hasAnimation("Animations\\swimhandtohandfastforward.hkx"))
			swimh2h_run[RUN_DIRECTION::forward] = "Animations\\swimhandtohandfastforward.hkx";

		//std::map<RUN_DIRECTION, fs::path> swim1h_run;

		// 1H
		//  Animations\onehandfastforward.hkx
		//	Animations\onehandforwardrun.hkx
		if (hasAnimation("Animations\\onehandfastforward.hkx"))
			oneh_run[RUN_DIRECTION::forward] = "Animations\\onehandfastforward.hkx";

		// STAFF
		// Animations\stafffastforward.hkx
		if (hasAnimation("Animations\\stafffastforward.hkx"))
			staff_run[RUN_DIRECTION::forward] = "Animations\\stafffastforward.hkx";

		// 2H
		// Animations\twohandfastforward.hkx
		if (hasAnimation("Animations\\twohandfastforward.hkx"))
			twoh_run[RUN_DIRECTION::forward] = "Animations\\twohandfastforward.hkx";
	}

	void analyzeJump()
	{
		// JUMP
		//Animations\jumpstart.hkx
		//Animations\jumploop.hkx
		//Animations\jumpland.hkx
		if (hasAnimation("Animations\\jumpstart.hkx"))
			jump[JUMP_STATE::start] = "Animations\\jumpstart.hkx";
		if (hasAnimation("Animations\\jumploop.hkx"))
			jump[JUMP_STATE::loop] = "Animations\\jumploop.hkx";
		if (hasAnimation("Animations\\jumpland.hkx"))
			jump[JUMP_STATE::land] = "Animations\\jumpland.hkx";
	}

	void analyzeStance() {

		// DEATH (special case that doesn't ragdoll
		//Animations\death.hkx
		if (hasAnimation("Animations\\death.hkx"))
			h2h_stance[STANCE_STATE::death] = "Animations\\death.hkx";

		//H2H
		//Animations\equip.hkx / Animations\handtohandattackequip.hkx / Animations\handtohandequip.hkx
		//Animations\unequip.hkx / Animations\handtohandattackunequip.hkx / Animations\handtohandunequip.hkx
		//Animations\block.hkx / Animations\handtohandblock.hkx
		//Animations\blockhit.hkx / Animations\handtohandblockhit.hkx
		//Animations\blockidle.hkx / Animations\handtohandblockidle.hkx
		//Animations\stagger.hkx / Animations\handtohandstagger.hkx
		//Animations\recoil.hkx /Animations\handtohandrecoil.hkx
		if (hasAnimation("Animations\\equip.hkx"))
			h2h_stance[STANCE_STATE::equip] = "Animations\\equip.hkx";
		if (hasAnimation("Animations\\handtohandattackequip.hkx"))
			h2h_stance[STANCE_STATE::equip] = "Animations\\handtohandattackequip.hkx";
		if (hasAnimation("Animations\\handtohandequip.hkx"))
			h2h_stance[STANCE_STATE::equip] = "Animations\\handtohandequip.hkx";

		if (hasAnimation("Animations\\unequip.hkx"))
			h2h_stance[STANCE_STATE::unequip] = "Animations\\unequip.hkx";
		if (hasAnimation("Animations\\handtohandattackunequip.hkx"))
			h2h_stance[STANCE_STATE::unequip] = "Animations\\handtohandattackunequip.hkx";
		if (hasAnimation("Animations\\handtohandunequip.hkx"))
			h2h_stance[STANCE_STATE::unequip] = "Animations\\handtohandunequip.hkx";

		if (hasAnimation("Animations\\block.hkx"))
			h2h_stance[STANCE_STATE::block] = "Animations\\block.hkx";
		if (hasAnimation("Animations\\handtohandblock.hkx"))
			h2h_stance[STANCE_STATE::block] = "Animations\\handtohandblock.hkx";

		if (hasAnimation("Animations\\blockhit.hkx"))
			h2h_stance[STANCE_STATE::blockhit] = "Animations\\blockhit.hkx";
		if (hasAnimation("Animations\\handtohandblockhit.hkx"))
			h2h_stance[STANCE_STATE::blockhit] = "Animations\\handtohandblockhit.hkx";

		if (hasAnimation("Animations\\blockidle.hkx"))
			h2h_stance[STANCE_STATE::blockidle] = "Animations\\blockidle.hkx";
		if (hasAnimation("Animations\\handtohandblockidle.hkx"))
			h2h_stance[STANCE_STATE::blockidle] = "Animations\\handtohandblockidle.hkx";

		if (hasAnimation("Animations\\stagger.hkx"))
			h2h_stance[STANCE_STATE::stagger] = "Animations\\stagger.hkx";
		if (hasAnimation("Animations\\handtohandstagger.hkx"))
			h2h_stance[STANCE_STATE::stagger] = "Animations\\handtohandstagger.hkx";

		if (hasAnimation("Animations\\recoil.hkx"))
			h2h_stance[STANCE_STATE::recoil] = "Animations\\recoil.hkx";
		if (hasAnimation("Animations\\handtohandrecoil.hkx"))
			h2h_stance[STANCE_STATE::recoil] = "Animations\\handtohandrecoil.hkx";

		//BOW
		//	Animations\bowequip.hkx
		//	Animations\bowunequip.hkx
		//	Animations\bowblockhit.hkx
		//	Animations\bowblockidle.hkx
		if (hasAnimation("Animations\\bowequip.hkx"))
			bow_stance[STANCE_STATE::equip] = "Animations\\bowequip.hkx";

		if (hasAnimation("Animations\\bowunequip.hkx"))
			bow_stance[STANCE_STATE::unequip] = "Animations\\bowunequip.hkx";

		if (hasAnimation("Animations\\bowblockidle.hkx"))
		{
			bow_stance[STANCE_STATE::block] = "Animations\\bowblockidle.hkx";
			bow_stance[STANCE_STATE::blockidle] = "Animations\\bowblockidle.hkx";
		}

		if (hasAnimation("Animations\\bowblockhit.hkx"))
			bow_stance[STANCE_STATE::blockhit] = "Animations\\bowblockhit.hkx";

		//SWIM
		//	Animations\swimequip.hkx
		//	Animations\swimunequip.hkx
		//	Animations\swimblock.hkx
		//	Animations\swimblockhit.hkx
		//	Animations\swimstagger.hkx
		//	Animations\swimrecoil.hkx
		if (hasAnimation("Animations\\swimequip.hkx"))
			swim_stance[STANCE_STATE::equip] = "Animations\\swimequip.hkx";

		if (hasAnimation("Animations\\swimunequip.hkx"))
			swim_stance[STANCE_STATE::unequip] = "Animations\\swimunequip.hkx";

		if (hasAnimation("Animations\\swimblock.hkx"))
		{
			swim_stance[STANCE_STATE::block] = "Animations\\swimblock.hkx";
			swim_stance[STANCE_STATE::blockidle] = "Animations\\swimblock.hkx";
		}

		if (hasAnimation("Animations\\swimstagger.hkx"))
			swim_stance[STANCE_STATE::stagger] = "Animations\\swimstagger.hkx";

		if (hasAnimation("Animations\\swimrecoil.hkx"))
			swim_stance[STANCE_STATE::recoil] = "Animations\\swimrecoil.hkx";

		//SWIM H2H
		//  Animations\swimhandtohandequip.hkx
		//	Animations\swimhandtohandattackequip.hkx
		//	Animations\swimhandtohandunequip.hkx
		//	Animations\swimhandtohandattackunequip.hkx
		//	Animations\swimhandtohandstagger.hkx
		//	Animations\swimhandtohandrecoil.hkx
		if (hasAnimation("Animations\\swimhandtohandequip.hkx"))
			swimh2h_stance[STANCE_STATE::equip] = "Animations\\swimhandtohandequip.hkx";
		if (hasAnimation("Animations\\swimhandtohandattackequip.hkx"))
			swimh2h_stance[STANCE_STATE::equip] = "Animations\\swimhandtohandattackequip.hkx";

		if (hasAnimation("Animations\\swimhandtohandunequip.hkx"))
			swimh2h_stance[STANCE_STATE::unequip] = "Animations\\swimhandtohandunequip.hkx";
		if (hasAnimation("Animations\\swimhandtohandattackunequip.hkx"))
			swimh2h_stance[STANCE_STATE::unequip] = "Animations\\swimhandtohandattackunequip.hkx";

		if (hasAnimation("Animations\\swimhandtohandstagger.hkx"))
			swimh2h_stance[STANCE_STATE::stagger] = "Animations\\swimhandtohandstagger.hkx";

		if (hasAnimation("Animations\\swimhandtohandrecoil.hkx"))
			swimh2h_stance[STANCE_STATE::recoil] = "Animations\\swimhandtohandrecoil.hkx";

		//1H
		// Animations\onehandequip.hkx
		// Animations\onehandunequip.hkx
		// Animations\onehandblock.hkx
		// Animations\onehandblockhit.hkx
		// Animations\onehandblockidle.hkx
		// Animations\onehandstagger.hkx
		// Animations\onehandrecoil.hkx
		if (hasAnimation("Animations\\onehandequip.hkx"))
			oneh_stance[STANCE_STATE::equip] = "Animations\\onehandequip.hkx";

		if (hasAnimation("Animations\\onehandunequip.hkx"))
			oneh_stance[STANCE_STATE::unequip] = "Animations\\onehandunequip.hkx";

		if (hasAnimation("Animations\\onehandblock.hkx"))
			oneh_stance[STANCE_STATE::block] = "Animations\\onehandblock.hkx";

		if (hasAnimation("Animations\\onehandblockhit.hkx"))
			h2h_stance[STANCE_STATE::blockhit] = "Animations\\onehandblockhit.hkx";

		if (hasAnimation("Animations\\onehandblockidle.hkx"))
			h2h_stance[STANCE_STATE::blockidle] = "Animations\\onehandblockidle.hkx";

		if (hasAnimation("Animations\\onehandstagger.hkx"))
			h2h_stance[STANCE_STATE::stagger] = "Animations\\onehandstagger.hkx";

		if (hasAnimation("Animations\\onehandrecoil.hkx"))
			h2h_stance[STANCE_STATE::recoil] = "Animations\\onehandrecoil.hkx";
	}

	void amalyzeIdles()
	{
		//Animations\idleanims\agony.hkx
		//	Animations\idleanims\bah.hkx
		//	Animations\idleanims\check.hkx
		//	Animations\idleanims\cheer.hkx
		//	Animations\idleanims\croak.hkx
		//	Animations\idleanims\dig.hkx
		//	Animations\idleanims\dynamicidle_sleep.hkx
		//	Animations\idleanims\eat.hkx
		//	Animations\idleanims\gaze.hkx
		//	Animations\idleanims\graze.hkx
		//	Animations\idleanims\grazes.hkx
		//	Animations\idleanims\horsedismount.hkx
		//	Animations\idleanims\horsemount.hkx
		//	Animations\idleanims\idle02.hkx
		//	Animations\idleanims\idleb.hkx
		//	Animations\idleanims\idlec.hkx
		//	Animations\idleanims\idleclench.hkx
		//	Animations\idleanims\idlecrouch.hkx
		//	Animations\idleanims\idledrool.hkx
		//	Animations\idleanims\idlelook.hkx
		//	Animations\idleanims\idlescowl.hkx
		//	Animations\idleanims\idlescratch.hkx
		//	Animations\idleanims\idleslack.hkx
		//	Animations\idleanims\idlesniff.hkx
		//	Animations\idleanims\idlestamp.hkx
		//	Animations\idleanims\idlesway.hkx
		//	Animations\idleanims\idlethump.hkx
		//	Animations\idleanims\land.hkx
		//	Animations\idleanims\look.hkx
		//	Animations\idleanims\lookleft.hkx
		//	Animations\idleanims\lookright.hkx
		//	Animations\idleanims\pathorse.hkx
		//	Animations\idleanims\paw.hkx
		//	Animations\idleanims\praise.hkx
		//	Animations\idleanims\rest.hkx
		//	Animations\idleanims\roar.hkx
		//	Animations\idleanims\scan.hkx
		//	Animations\idleanims\scratch.hkx
		//	Animations\idleanims\scratch2.hkx
		//	Animations\idleanims\search.hkx
		//	Animations\idleanims\sigh.hkx
		//	Animations\idleanims\sleep.hkx
		//	Animations\idleanims\snif.hkx
		//	Animations\idleanims\special idle_lookaround.hkx
		//	Animations\idleanims\special idle_twist.hkx
		//	Animations\idleanims\special_idle.hkx
		//	Animations\idleanims\special_reach.hkx
		//	Animations\idleanims\specialidel_cute1.hkx
		//	Animations\idleanims\specialidel_cute2.hkx
		//	Animations\idleanims\specialidle.hkx
		//	Animations\idleanims\specialidle_01noshield.hkx
		//	Animations\idleanims\specialidle__8.hkx
		//	Animations\idleanims\specialidle__jump.hkx
		//	Animations\idleanims\specialidle__side_to_side.hkx
		//	Animations\idleanims\specialidle_aware.hkx
		//	Animations\idleanims\specialidle_b.hkx
		//	Animations\idleanims\specialidle_c.hkx
		//	Animations\idleanims\specialidle_clean.hkx
		//	Animations\idleanims\specialidle_dance1.hkx
		//	Animations\idleanims\specialidle_dodge.hkx
		//	Animations\idleanims\specialidle_dogbark.hkx
		//	Animations\idleanims\specialidle_doge.hkx
		//	Animations\idleanims\specialidle_eat.hkx
		//	Animations\idleanims\specialidle_flee.hkx
		//	Animations\idleanims\specialidle_howl.hkx
		//	Animations\idleanims\specialidle_intimidate.hkx
		//	Animations\idleanims\specialidle_intimidate2.hkx
		//	Animations\idleanims\specialidle_intimidate3.hkx
		//	Animations\idleanims\specialidle_look.hkx
		//	Animations\idleanims\specialidle_lookleft.hkx
		//	Animations\idleanims\specialidle_lookright.hkx
		//	Animations\idleanims\specialidle_praying1.hkx
		//	Animations\idleanims\specialidle_praying2.hkx
		//	Animations\idleanims\specialidle_rooting.hkx
		//	Animations\idleanims\specialidle_sandup.hkx
		//	Animations\idleanims\specialidle_scratch.hkx
		//	Animations\idleanims\specialidle_seep.hkx
		//	Animations\idleanims\specialidle_sit.hkx
		//	Animations\idleanims\specialidle_sitbark.hkx
		//	Animations\idleanims\specialidle_sleeplookaround.hkx
		//	Animations\idleanims\specialidle_smell.hkx
		//	Animations\idleanims\specialidle_starteat.hkx
		//	Animations\idleanims\specialidle_tracking.hkx
		//	Animations\idleanims\specialidle_whirlwind.hkx
		//	Animations\idleanims\specialldle_guard.hkx
		//	Animations\idleanims\srcatch.hkx
		//	Animations\idleanims\staggered.hkx
		//	Animations\idleanims\startle.hkx
		//	Animations\idleanims\stretch.hkx
		//	Animations\idleanims\swat.hkx
		//	Animations\idleanims\taunt.hkx

		for (const auto& entry : animations)
		{
			if (entry.first.string().find("idleanims") != string::npos)
			{
				additional_idles.push_back(entry.first);
			}
		}
	}

public:

	AnimationSetAnalyzer(const std::map<fs::path, ckcmd::HKX::RootMovement>& animations) : animations(animations) 
	{
		analyzeGetUp();
		analyzeWalking();
		analyzeTurning();
		analyzeRunning();
		analyzeJump();
		analyzeStance();
		amalyzeIdles();
	}

	inline bool hasAnimation(const std::string& name)
	{
		return animations.find(name) != animations.end();
	}

	fs::path idle() {
		if (default_move.find(MOVE_DIRECTION::idle) != default_move.end())
			return default_move.at(MOVE_DIRECTION::idle);
		return "";
	}

	const std::map<GETUP_DIRECTION, fs::path>& getups()
	{
		return getup;
	}

	enum class MOVEMENT_TYPE {
		normal = 0,
		bow,
		swim,
		h2h,
		swimh2h,
		swim1h,
		oneh,
		staff,
		twoh
	};

	const std::map<MOVE_DIRECTION, fs::path>& moves(MOVEMENT_TYPE type)
	{
		switch (type)
		{
		case MOVEMENT_TYPE::normal:
			return default_move;
		case MOVEMENT_TYPE::bow:
			return bow_move;
		case MOVEMENT_TYPE::swim:
			return swim_move;
		case MOVEMENT_TYPE::h2h:
			return h2h_move;
		case MOVEMENT_TYPE::swimh2h:
			return swimh2h_move;
		case MOVEMENT_TYPE::swim1h:
			return swim1h_move;
		case MOVEMENT_TYPE::oneh:
			return oneh_move;
		case MOVEMENT_TYPE::staff:
			return staff_move;
		case MOVEMENT_TYPE::twoh:
			return twoh_move;
		default:
			break;
		}
		return {};
	}

	const std::map<TURN_DIRECTION, fs::path>& turns(MOVEMENT_TYPE type)
	{
		switch (type)
		{
		case MOVEMENT_TYPE::normal:
			return default_turn;
		case MOVEMENT_TYPE::bow:
			return bow_turn;
		case MOVEMENT_TYPE::swim:
			return swim_turn;
		case MOVEMENT_TYPE::h2h:
			return h2h_turn;
		case MOVEMENT_TYPE::swimh2h:
			return swimh2h_turn;
		case MOVEMENT_TYPE::swim1h:
			return swim1h_turn;
		case MOVEMENT_TYPE::oneh:
			return oneh_turn;
		case MOVEMENT_TYPE::staff:
			return staff_turn;
		case MOVEMENT_TYPE::twoh:
			return twoh_turn;
		}
		return {};
	}

	// BOW
	//  Animations\bowattack.hkx / Animations\attackbow.hkx


	//CAST
	//  Animations\castself.hkx
	//	Animations\castself_a.hkx
	//	Animations\castself_b.hkx
	//	Animations\castself_c.hkx
	//	Animations\casttarget.hkx
	//	Animations\casttarget_a.hkx
	//	Animations\casttarget_b.hkx
	//	Animations\casttarget_c.hkx
	//	Animations\casttouch.hkx
	//	Animations\casttouch_a.hkx
	//	Animations\casttouch_b.hkx
	//	Animations\casttouch_c.hkx

	//HAND 2 HAND

	//  Animations\handtohandattackbackpower.hkx
	//	Animations\attackforwardpower.hkx / Animations\handtohandattackfowardpower.hkx / Animations\handtohandattackforwardpower.hkx
	//	Animations\attackpower.hkx / Animations\handtohandattackpower.hkx

	
	//	Animations\handtohandattackleft.hkx
	//	Animations\handtohandattackleft_a.hkx
	//	Animations\handtohandattackleft_b.hkx
	//	Animations\handtohandattackleft_backhand.hkx
	//	Animations\handtohandattackleft_bite.hkx
	//	Animations\handtohandattackleft_c.hkx
	//	Animations\handtohandattackleft_chop.hkx
	//	Animations\handtohandattackleft_cross.hkx
	//	Animations\handtohandattackleft_scratch.hkx
	//	Animations\handtohandattackleft_swing.hkx
	//	Animations\handtohandattackleft_uppercut.hkx
	//	Animations\handtohandattacklefta.hkx
	//	Animations\handtohandattackleftb.hkx
	//	Animations\handtohandattackleftpower.hkx

	//	Animations\handtohandattackright.hkx
	//	Animations\handtohandattackright_a.hkx
	//	Animations\handtohandattackright_b.hkx
	//	Animations\handtohandattackright_backhand.hkx
	//	Animations\handtohandattackright_bite.hkx
	//	Animations\handtohandattackright_c.hkx
	//	Animations\handtohandattackright_chop.hkx
	//	Animations\handtohandattackright_cross.hkx
	//	Animations\handtohandattackright_overhead.hkx
	//	Animations\handtohandattackright_scratch.hkx
	//	Animations\handtohandattackright_swing.hkx
	//	Animations\handtohandattackright_uppercut.hkx
	//	Animations\handtohandattackrighta.hkx
	//	Animations\handtohandattackrightb.hkx
	//	Animations\handtohandattackrightpower.hkx




	// SWIM

};

/*


Animations\onehandattackbackpower.hkx
Animations\onehandattackforwardpower.hkx
Animations\onehandattackleft.hkx
Animations\onehandattackleft_1handslice.hkx
Animations\onehandattackleft_a.hkx
Animations\onehandattackleft_b.hkx
Animations\onehandattackleft_chop.hkx
Animations\onehandattackleft_slash.hkx
Animations\onehandattackleft_slice.hkx
Animations\onehandattackleftpower.hkx
Animations\onehandattackpower.hkx
Animations\onehandattackright.hkx
Animations\onehandattackright_1handslice.hkx
Animations\onehandattackright_a.hkx
Animations\onehandattackright_b.hkx
Animations\onehandattackright_chop.hkx
Animations\onehandattackright_slash.hkx
Animations\onehandattackright_slice.hkx
Animations\onehandattackrightpower.hkx

Animations\specialanims\casttouch.hkx
Animations\specialanims\casttouch_axe.hkx
Animations\specialanims\casttouch_fist.hkx
Animations\specialanims\casttouch_mace.hkx
Animations\specialanims\casttouch_punch.hkx
Animations\specialanims\casttouch_sword.hkx

Animations\specialanims\forward_sharman.hkx
Animations\specialanims\forward_wolf.hkx

Animations\specialanims\handtohandattack_wolf.hkx
Animations\specialanims\handtohandattackleft.hkx
Animations\specialanims\handtohandattackleft_leftarm.hkx
Animations\specialanims\handtohandattackleft_rightarm.hkx
Animations\specialanims\handtohandattacklefta.hkx
Animations\specialanims\handtohandattackleftb.hkx
Animations\specialanims\handtohandattackleftc.hkx
Animations\specialanims\handtohandattackleftpower.hkx
Animations\specialanims\handtohandattackleftpower_rightarm.hkx
Animations\specialanims\handtohandattackpower.hkx
Animations\specialanims\handtohandattackpowera.hkx
Animations\specialanims\handtohandattackpowerb.hkx
Animations\specialanims\handtohandattackright.hkx
Animations\specialanims\handtohandattackright_leftarm.hkx
Animations\specialanims\handtohandattackright_rightarm.hkx
Animations\specialanims\handtohandattackrighta.hkx
Animations\specialanims\handtohandattackrightb.hkx
Animations\specialanims\handtohandattackrightc.hkx
Animations\specialanims\handtohandattackrightpower.hkx
Animations\specialanims\handtohandattackrightpower_leftarm.hkx
Animations\specialanims\handtohandcrouchequip.hkx
Animations\specialanims\handtohandleanbackequip.hkx
Animations\specialanims\handtohandswayequip.hkx

Animations\specialanims\idle_sharman.hkx
Animations\specialanims\idle_wolf.hkx

Animations\specialanims\idlecrouch.hkx
Animations\specialanims\idleleanback.hkx
Animations\specialanims\runforwardarmsdown.hkx
Animations\specialanims\runforwardarmsup.hkx
Animations\specialanims\special_idle.hkx
Animations\specialanims\special_reach.hkx

Animations\specialanims\stafffastforward.hkx
Animations\specialanims\staffforwardwalk.hkx
Animations\specialanims\staffidle.hkx

Animations\specialanims\sway.hkx
Animations\specialanims\walk01armsup.hkx
Animations\specialanims\walk02limp.hkx
Animations\specialanims\walk03leanback.hkx



Animations\staffattackright.hkx

Animations\staffblock.hkx
Animations\staffblockhit.hkx
Animations\staffequip.hkx





Animations\staffstagger.hkx

Animations\staffunequip.hkx



Animations\swimhandtohandattackbackpower.hkx

Animations\swimhandtohandattackforwardpower.hkx
Animations\swimhandtohandattackleft.hkx
Animations\swimhandtohandattackleftpower.hkx
Animations\swimhandtohandattackpower.hkx
Animations\swimhandtohandattackright.hkx
Animations\swimhandtohandattackrightpower.hkx





Animations\swimonehandattackleft.hkx
Animations\swimonehandattackright.hkx



Animations\twohandattackbackpower.hkx
Animations\twohandattackequip.hkx
Animations\twohandattackforwardpower.hkx
Animations\twohandattackleft.hkx
Animations\twohandattackleft_a.hkx
Animations\twohandattackleft_b.hkx
Animations\twohandattackleft_c.hkx
Animations\twohandattackleftpower.hkx
Animations\twohandattackpower.hkx
Animations\twohandattackright.hkx
Animations\twohandattackright_a.hkx
Animations\twohandattackright_b.hkx
Animations\twohandattackright_c.hkx
Animations\twohandattackrightpower.hkx
Animations\twohandattackunequip.hkx

Animations\twohandblockhit.hkx
Animations\twohandblockidle.hkx
Animations\twohandequip.hkx




Animations\twohandrecoil.hkx

Animations\twohandstagger.hkx

Animations\twohandunequip.hkx
*/


/* Variables

iSyncTurnState:VARIABLE_TYPE_INT32:0
iSyncIdleLocomotion:VARIABLE_TYPE_INT32:0
isMoving:VARIABLE_TYPE_BOOL:0
iSyncForwardState:VARIABLE_TYPE_INT32:0

iState:VARIABLE_TYPE_INT32:0
iState_TES4MudcrabMovementDefault:VARIABLE_TYPE_INT32:0

bAnimationDriven:VARIABLE_TYPE_BOOL:0

Speed:VARIABLE_TYPE_REAL:0
Direction:VARIABLE_TYPE_REAL:0
TurnDelta:VARIABLE_TYPE_REAL:0
SpeedSampled:VARIABLE_TYPE_REAL:0
TurnDeltaDamped:VARIABLE_TYPE_REAL:0

FootIKEnable:VARIABLE_TYPE_BOOL:0
m_onOffGain:VARIABLE_TYPE_REAL:0
m_groundAscendingGain:VARIABLE_TYPE_REAL:0
m_groundDescendingGain:VARIABLE_TYPE_REAL:0
m_footPlantedGain:VARIABLE_TYPE_REAL:0
m_footRaisedGain:VARIABLE_TYPE_REAL:0
m_footUnlockGain:VARIABLE_TYPE_REAL:0
m_worldFromModelFeedbackGain:VARIABLE_TYPE_REAL:0
m_errorUpDownBias:VARIABLE_TYPE_REAL:0
m_alignWorldFromModelGain:VARIABLE_TYPE_REAL:0
m_hipOrientationGain:VARIABLE_TYPE_REAL:0

blendDefault:VARIABLE_TYPE_REAL:0
iGetUpType:VARIABLE_TYPE_INT8:0
IsUnequipping:VARIABLE_TYPE_BOOL:0
iEquippedItemState:VARIABLE_TYPE_INT32:0
iLeftHandType:VARIABLE_TYPE_INT32:0
IsAttackReady:VARIABLE_TYPE_BOOL:1
iRightHandType:VARIABLE_TYPE_INT32:0
iCombatStance:VARIABLE_TYPE_INT32:0
IsAttacking:VARIABLE_TYPE_BOOL:0
bIsSynced:VARIABLE_TYPE_BOOL:0
TargetLocation:VARIABLE_TYPE_VECTOR4:(0.000000 0.000000 0.000000 0.000000)
bEquipOk:VARIABLE_TYPE_BOOL:1

*/

class TES4BehaviorAssembler
{
	const std::string& prefix;
	const std::string& creature_tag;
	const std::string& output_file;
	fs::path behavior_file;
	std::map<fs::path, ckcmd::HKX::RootMovement>& root_movements;
	Collection& conversionCollection;
	ModFile*& conversionPlugin;
	const std::set<Ob::CREARecord*>& CREAs;
	const std::map<std::string, Sk::AACTRecord*>& actions;
	AnimationSetAnalyzer& analyzer;
	map<hkRefPtr<hkbClipGenerator>, fs::path>& generators;
	map<int, string>& behavior_events;

	std::string creature_tag_camel;
	int ragdollIndex_pelvis; //pelvis
	int ragdollIndex_rightUpperArm; //R UpperArm01
	int ragdollIndex_head; //head

	//internal map to use strings;
	map<string, int> _event_map;	
	map<string, int> _variables_map;

	//Internal
	hkbBehaviorGraph graph;
	hkbBehaviorGraphData root_data;
	hkbBehaviorGraphStringData root_string_data;
	hkbVariableValueSet root_data_init_vars;

	hkRefPtr<hkbBlendingTransitionEffect> _default_transition_effect;

	int createEvent(const std::string& event_name, bool isSyncPoint)
	{
		hkbEventInfo info;
		info.m_flags = isSyncPoint ? hkbEventInfo::FLAG_SYNC_POINT : 0;
		int index = root_data.m_eventInfos.getSize();
		root_data.m_eventInfos.pushBack(info);
		root_string_data.m_eventNames.pushBack(event_name.c_str());
		_event_map[event_name] = index;
		behavior_events[index] = event_name;
		return index;
	}

	template<typename T>
	hkbVariableInfo::VariableType variableType(const T& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_INVALID;
	}
	//VARIABLE_TYPE_BOOL = 0
	template<>
	hkbVariableInfo::VariableType variableType(const bool& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_BOOL;
	}
	//VARIABLE_TYPE_INT8 = 1
	template<>
	hkbVariableInfo::VariableType variableType(const int8_t& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_INT8;
	}
	//VARIABLE_TYPE_INT16 = 2
	template<>
	hkbVariableInfo::VariableType variableType(const int16_t& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_INT16;
	}
	//VARIABLE_TYPE_INT32 = 3
	template<>
	hkbVariableInfo::VariableType variableType(const int32_t& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_INT32;
	}
	//VARIABLE_TYPE_REAL = 4
	template<>
	hkbVariableInfo::VariableType variableType(const float& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_REAL;
	}
	//VARIABLE_TYPE_POINTER = 5
	template<>
	hkbVariableInfo::VariableType variableType(const hkReferencedObject& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_POINTER;
	}
	//VARIABLE_TYPE_VECTOR3 = 6
	template<>
	hkbVariableInfo::VariableType variableType(const Vector3& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_VECTOR3;
	}
	//VARIABLE_TYPE_VECTOR4 = 7
	template<>
	hkbVariableInfo::VariableType variableType(const hkVector4& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_VECTOR4;
	}
	//VARIABLE_TYPE_QUATERNION = 8
	template<>
	hkbVariableInfo::VariableType variableType(const ::hkQuaternion& value)
	{
		return hkbVariableInfo::VARIABLE_TYPE_QUATERNION;
	}

	template<typename T>
	void initVariable(const T& initialValue)
	{
		hkbVariableValue value;
		value.m_value = initialValue;
		root_data_init_vars.m_wordVariableValues.pushBack(value);
	}

	template<>
	void initVariable(const hkVector4& initialValue)
	{
		hkbVariableValue value;
		value.m_value = root_data_init_vars.m_quadVariableValues.getSize();
		root_data_init_vars.m_quadVariableValues.pushBack(initialValue);
		root_data_init_vars.m_wordVariableValues.pushBack(value);
	}

	template<>
	void initVariable(const Vector3& initialValue)
	{
		hkbVariableValue value;
		value.m_value = root_data_init_vars.m_quadVariableValues.getSize();
		root_data_init_vars.m_quadVariableValues.pushBack(TOVECTOR4(initialValue));
		root_data_init_vars.m_wordVariableValues.pushBack(value);
		return ;
	}

	template<>
	void initVariable(const ::hkQuaternion& initialValue)
	{
		hkbVariableValue value;
		value.m_value = root_data_init_vars.m_quadVariableValues.getSize();
		root_data_init_vars.m_quadVariableValues.pushBack(initialValue.m_vec);
		root_data_init_vars.m_wordVariableValues.pushBack(value);
		return;
	}

	template<>
	void initVariable(const hkReferencedObject& initialValue)
	{
		hkbVariableValue value;
		value.m_value = root_data_init_vars.m_variantVariableValues.getSize();
		root_data_init_vars.m_variantVariableValues.pushBack((hkReferencedObject*)&initialValue);
		root_data_init_vars.m_wordVariableValues.pushBack(value);
		return;
	}

	template<typename T>
	int createVariable(const std::string& variable_name, const T& initial_value)
	{
		int index = root_string_data.m_variableNames.getSize();
		root_string_data.m_variableNames.pushBack(variable_name.c_str());
		hkbRoleAttribute role_attribute;
		role_attribute.m_role = hkbRoleAttribute::ROLE_DEFAULT;
		role_attribute.m_flags = hkbRoleAttribute::FLAG_NONE;
		hkbVariableInfo info;
		info.m_role = role_attribute;
		info.m_type = variableType(initial_value); //hkbVariableInfo::VARIABLE_TYPE_BOOL;
		root_data.m_variableInfos.pushBack(info);
		initVariable(initial_value);
		_variables_map[variable_name] = index;
		return index;
	}

	hkRefPtr<hkbBlendingTransitionEffect> effect(
		const std::string& name, 
		float duration = 0.,
		hkbBlendingTransitionEffect::SelfTransitionMode self_transition_mode = hkbBlendingTransitionEffect::SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC,
		hkbBlendingTransitionEffect::EventMode event_mode = hkbBlendingTransitionEffect::EVENT_MODE_IGNORE_FROM_GENERATOR,
		hkbBlendingTransitionEffect::EndMode end_mode = hkbBlendingTransitionEffect::END_MODE_NONE,
		hkbBlendCurveUtils::BlendCurve blend_curve = hkbBlendCurveUtils::BLEND_CURVE_SMOOTH
	)
	{
		hkRefPtr<hkbBlendingTransitionEffect> transition_effect = new hkbBlendingTransitionEffect();
		transition_effect->m_name = name.c_str();
		transition_effect->m_userData = 0;
		transition_effect->m_selfTransitionMode = self_transition_mode;
		transition_effect->m_eventMode = event_mode;
		transition_effect->m_duration = duration;
		transition_effect->m_toGeneratorStartTimeFraction = 0.0;
		transition_effect->m_flags = 0;
		transition_effect->m_endMode = end_mode;
		transition_effect->m_blendCurve = blend_curve;
		return transition_effect;
	}

	hkRefPtr<hkbClipGenerator> clip(const fs::path& clip_path, hkbClipGenerator::PlaybackMode mode)
	{
		hkRefPtr<hkbClipGenerator> generator = new hkbClipGenerator();
		generator->m_name = clip_path.filename().replace_extension("").string().c_str();
		generator->m_userData = 0;
		generator->m_animationName = clip_path.string().c_str();
		generator->m_cropStartAmountLocalTime = 0.0;
		generator->m_cropEndAmountLocalTime = 0.0;
		generator->m_startTime = 0.0;
		generator->m_playbackSpeed = 1.0;
		generator->m_enforcedDuration = 0.0;
		generator->m_userControlledTimeFraction = 0.0;
		generator->m_animationBindingIndex = -1;
		generator->m_mode = mode;
		generator->m_flags = 0;

		generators[generator] = clip_path;

		return generator;
	}

	void trigger(hkRefPtr<hkbClipGenerator> clip, const std::string& event, float local_time, bool relativeToEnd = false)
	{
		if (clip->m_triggers == NULL)
		{
			clip->m_triggers = new hkbClipTriggerArray();
		}
		hkbClipTrigger t;
		t.m_acyclic = false;
		t.m_event.m_id = _event_map.at(event);
		t.m_event.m_payload = NULL;
		t.m_isAnnotation = false;
		t.m_relativeToEndOfClip = relativeToEnd;
		t.m_localTime = local_time;

		clip->m_triggers->m_triggers.pushBack(t);
	}

	hkRefPtr<hkbStateMachineStateInfo> state(hkRefPtr<hkbStateMachine> fsm, const std::string& name, hkRefPtr<hkbGenerator> generator)
	{
		hkRefPtr<hkbStateMachineStateInfo> state = new hkbStateMachineStateInfo();
		state->m_name = name.c_str();
		state->m_stateId = fsm->m_states.getSize();
		state->m_probability = 1.000000;
		state->m_enable = true;
		state->m_generator = generator;
		fsm->m_states.pushBack(state);
		return state;
	}

	hkRefPtr<hkbStateMachineStateInfo> starting_state(hkRefPtr<hkbStateMachine> fsm, const std::string& name, hkRefPtr<hkbGenerator> generator)
	{
		auto s_state = state(fsm, name, generator);
		fsm->m_startStateId = s_state->m_stateId;
	}

	hkRefPtr<hkbStateMachineStateInfo> clip_state(hkRefPtr<hkbStateMachine> fsm, const fs::path& clip_path, hkbClipGenerator::PlaybackMode mode)
	{
		auto g_clip = clip(clip_path, mode);
		std::string state_name = std::string(g_clip->m_name.cString()) + "_state";
		auto s_state = state(fsm, state_name, g_clip.val());
		fsm->m_startStateId = s_state->m_stateId;
		return s_state;
	}

	hkRefPtr<hkbStateMachine> fsm(const std::string& name)
	{
		hkRefPtr<hkbStateMachine> root_fsm = new hkbStateMachine();
		root_fsm->m_name = name.c_str();
		root_fsm->m_startStateId = 0;
		root_fsm->m_returnToPreviousStateEventId = -1;
		root_fsm->m_randomTransitionEventId = -1;
		root_fsm->m_transitionToNextHigherStateEventId = -1;
		root_fsm->m_transitionToNextLowerStateEventId = -1;
		root_fsm->m_syncVariableIndex = -1;
		root_fsm->m_userData = 0;
		root_fsm->m_eventToSendWhenStateOrTransitionChanges.m_id = -1;
		root_fsm->m_eventToSendWhenStateOrTransitionChanges.m_payload = NULL;
		root_fsm->m_wrapAroundStateId = false;
		root_fsm->m_maxSimultaneousTransitions = 32;
		root_fsm->m_startStateMode = hkbStateMachine::START_STATE_MODE_DEFAULT;
		root_fsm->m_selfTransitionMode = hkbStateMachine::SELF_TRANSITION_MODE_NO_TRANSITION;
		return root_fsm;
	}

	void transition(
		hkRefPtr<hkbStateMachineTransitionInfoArray>& container,
		const std::string& event, 
		hkRefPtr<hkbStateMachineStateInfo> state_to,
		hkRefPtr<hkbTransitionEffect> effect,
		hkInt16 flags = hkbStateMachineTransitionInfo::FLAG_DISABLE_CONDITION,
		hkRefPtr<hkbStateMachineStateInfo> nested = NULL
	)
	{
		hkbStateMachineTransitionInfo transition;

		transition.m_triggerInterval.m_enterEventId = -1;
		transition.m_triggerInterval.m_exitEventId = -1;
		transition.m_triggerInterval.m_enterTime = 0.0;
		transition.m_triggerInterval.m_exitTime = 0.0;

		transition.m_initiateInterval.m_enterEventId = -1;
		transition.m_initiateInterval.m_exitEventId = -1;
		transition.m_initiateInterval.m_enterTime = 0.0;
		transition.m_initiateInterval.m_exitTime = 0.0;

		transition.m_transition = effect;
		transition.m_eventId = _event_map.at(event);
		transition.m_toStateId = state_to->m_stateId;
		transition.m_fromNestedStateId = 0;
		if (NULL != nested)
			transition.m_toNestedStateId = nested->m_stateId;
		else
			transition.m_toNestedStateId = 0;
		transition.m_priority = 0;
		transition.m_flags = flags;
		container->m_transitions.pushBack(transition);
	}

	void transition(
		hkRefPtr<hkbStateMachine> fsm,
		const std::string& event,
		hkRefPtr<hkbStateMachineStateInfo> to_state,
		hkRefPtr<hkbTransitionEffect> effect,
		hkRefPtr<hkbStateMachineStateInfo> from_state = NULL,
		hkRefPtr<hkbStateMachineStateInfo> nested = NULL
	)
	{
		if (from_state == NULL)
		{
			if (fsm->m_wildcardTransitions == NULL)
			{
				fsm->m_wildcardTransitions = new hkbStateMachineTransitionInfoArray();
			}
			transition(
				fsm->m_wildcardTransitions,
				event,
				to_state,
				effect,
				hkbStateMachineTransitionInfo::FLAG_DISABLE_CONDITION | hkbStateMachineTransitionInfo::FLAG_IS_LOCAL_WILDCARD,
				nested
			);
		}
		else {
			if (from_state->m_transitions == NULL)
			{
				from_state->m_transitions = new hkbStateMachineTransitionInfoArray();
			}
			transition(
				from_state->m_transitions,
				event,
				to_state,
				effect,
				hkbStateMachineTransitionInfo::FLAG_DISABLE_CONDITION,
				nested
			);
		}	
	}

	hkRefPtr<hkbModifierGenerator> modify(hkRefPtr<hkbModifier> modifier, hkRefPtr<hkbGenerator> generator)
	{
		hkRefPtr<hkbModifierGenerator> msg = new hkbModifierGenerator();
		msg->m_modifier = modifier;
		msg->m_generator = generator;
		msg->m_userData = 0;
		std::string name = generator->m_name.cString(); name += "_msg";
		msg->m_name = name.c_str();
		return msg;
	}

	hkRefPtr<hkbGenerator> idle_fsm()
	{
		hkRefPtr<hkbStateMachine> idle_fsm = fsm("idle_fsm");
		auto idle_state = clip_state(idle_fsm, analyzer.idle(), hkbClipGenerator::PlaybackMode::MODE_LOOPING);
		return idle_fsm.val();
	}

	void calculateMOVTs()
	{
		createVariable("iState", 0);

		{ //Default
			createVariable("iState_"+ prefix + creature_tag_camel + "Default", 0);

			std::string movt_EDID = prefix + creature_tag_camel + "DefaultMovement";
			Sk::MOVTRecord* MOVT_record = (Sk::MOVTRecord*)conversionCollection.CreateRecord(conversionPlugin, REV32(MOVT), NULL, (char*)movt_EDID.c_str(), NULL, 0);
			MOVT_record->EDID = movt_EDID;
			MOVT_record->MNAM = prefix + creature_tag_camel + "Default"; //Behavior Movement state variable name (without iState prefix)
			//Scaling: CK angles are in degrees, Havok in radians
			MOVT_record->INAM.Load();
			MOVT_record->INAM->directionalScale = 1.;
			MOVT_record->INAM->movementSpeedScale = 1.;
			MOVT_record->INAM->rotationSpeedScale = 1.;
			
			//Default movements are forward backward left right
			const auto& default_moves = analyzer.moves(AnimationSetAnalyzer::MOVEMENT_TYPE::normal);
			auto forward_endpoint = *root_movements.at(default_moves.at(AnimationSetAnalyzer::MOVE_DIRECTION::forward)).translations.rbegin();
			MOVT_record->ESPED.value.forwardWalk = abs(get<1>(forward_endpoint)(1) / get<0>(forward_endpoint));
			auto backward_endpoint = *root_movements.at(default_moves.at(AnimationSetAnalyzer::MOVE_DIRECTION::backward)).translations.rbegin();
			MOVT_record->ESPED.value.backWalk = abs(get<1>(backward_endpoint)(1) / get<0>(backward_endpoint));
			auto left_endpoint = *root_movements.at(default_moves.at(AnimationSetAnalyzer::MOVE_DIRECTION::left)).translations.rbegin();
			MOVT_record->ESPED.value.leftWalk = abs(get<1>(left_endpoint)(0) / get<0>(left_endpoint));
			auto right_endpoint = *root_movements.at(default_moves.at(AnimationSetAnalyzer::MOVE_DIRECTION::right)).translations.rbegin();
			MOVT_record->ESPED.value.rightWalk = abs(get<1>(right_endpoint)(0) / get<0>(right_endpoint));

			//default turning
			const auto& default_turns = analyzer.turns(AnimationSetAnalyzer::MOVEMENT_TYPE::normal);
			if (default_turns.find(AnimationSetAnalyzer::TURN_DIRECTION::left) != default_turns.end() && 
				default_turns.find(AnimationSetAnalyzer::TURN_DIRECTION::right) != default_turns.end())
			{
				//turn speed seems to be related to CREA's TNAM, but is mostly 0
				float turnSpeed = 0.;
				for (const auto& CREA : CREAs)
				{
					if (CREA->TNAM.value != 0.)
					{
						if (turnSpeed != 0.)
						{
							int debug = 1;
						}
						turnSpeed = CREA->TNAM.value;
					}
				}
				if (turnSpeed == 0.)
					turnSpeed = 90.;
				MOVT_record->ESPED.value.rotateInPlaceWalk = turnSpeed;
			}
		}
	}

	void IDLERecord(const std::string& name, const std::string& event, const std::string& action)
	{
		std::string deathIdleEDID = prefix + creature_tag_camel + name;
		Sk::IDLERecord* death_record = (Sk::IDLERecord*)conversionCollection.CreateRecord(conversionPlugin, REV32(IDLE), NULL, (char*)deathIdleEDID.c_str(), NULL, 0);
		death_record->EDID = deathIdleEDID; //Editor ID
		death_record->DNAM = behavior_file.string().c_str(); //Path to hkx BEHAVIOR!.
		death_record->ENAM = event; //Behaviour event.
		if (!actions.empty())
			death_record->ANAM.value.parent = actions.at(action)->formID;
		death_record->ANAM.value.sibling = NULL;
		death_record->DATA.value.flags = 0;
		death_record->DATA.value.min = 0;
		death_record->DATA.value.max = 0;
		death_record->DATA.value.flags = 0;
		death_record->DATA.value.unk = 0;
		death_record->DATA.value.replay = 0;
	}

	//Skyrim requires a MOVT entry for each kind of moving set of animations
	// for the AI to decide how to move the creature, when bAnimationDriven is not set
	// otherwise the movements are decided by the animation itself

	//the speed and possible movements are decided by the SPED structure.
	//the MOVT are switched by the behavior using the iState variables and state tagging modifiers
	hkRefPtr<hkbGenerator> move(hkRefPtr<hkbGenerator> fsm)
	{
		hkRefPtr<hkbStateMachine> move_fsm = new hkbStateMachine();
		calculateMOVTs();

		createVariable("Direction", 0.);
		createVariable("Speed", 0.);
		createVariable("SpeedSampled", 0.);

		hkRefPtr<hkbModifierList> list = new hkbModifierList();
		list->m_name = "move_ml";
		list->m_userData = 0;

		hkRefPtr<BSSpeedSamplerModifier> speed_sampler = new BSSpeedSamplerModifier();
		speed_sampler->m_state = 0;
		speed_sampler->m_direction = 0.;
		speed_sampler->m_goalSpeed = 0.;
		speed_sampler->m_speedOut = 0.;

		bind(speed_sampler.val(), "state", "iState");
		bind(speed_sampler.val(), "direction", "Direction");
		bind(speed_sampler.val(), "goalSpeed", "Speed");
		bind(speed_sampler.val(), "speedOut", "SpeedSampled");

		list->m_modifiers.pushBack(speed_sampler);
		return modify(list.val(), fsm.val());
	}

	hkRefPtr<hkbPoweredRagdollControlsModifier> powered_ragdoll(const std::string& name, double max_force)
	{
		hkRefPtr<hkbPoweredRagdollControlsModifier> ragdoll_engine = new hkbPoweredRagdollControlsModifier();
		ragdoll_engine->m_enable = true;
		ragdoll_engine->m_name = name.c_str();
		ragdoll_engine->m_userData = 1; //Unknown

		ragdoll_engine->m_controlData.m_maxForce = max_force;
		ragdoll_engine->m_controlData.m_tau = 0.8;
		ragdoll_engine->m_controlData.m_damping = 1.;
		ragdoll_engine->m_controlData.m_proportionalRecoveryVelocity = 2.;
		ragdoll_engine->m_controlData.m_constantRecoveryVelocity = 1.;

		ragdoll_engine->m_worldFromModelModeData.m_mode = hkbWorldFromModelModeData::WorldFromModelMode::WORLD_FROM_MODEL_MODE_RAGDOLL;
		ragdoll_engine->m_worldFromModelModeData.m_poseMatchingBone0 = ragdollIndex_pelvis; //pelvis
		ragdoll_engine->m_worldFromModelModeData.m_poseMatchingBone1 = ragdollIndex_rightUpperArm; //R UpperArm01
		ragdoll_engine->m_worldFromModelModeData.m_poseMatchingBone2 = ragdollIndex_head; //head

		ragdoll_engine->m_bones = new hkbBoneIndexArray();
		ragdoll_engine->m_boneWeights = new hkbBoneWeightArray();

		return ragdoll_engine;
	}

	hkRefPtr<hkbEventDrivenModifier> event_driven_modifier(
		const std::string& name, 
		const std::string& activate_event,
		const std::string& deactivate_event,
		bool activeByDefault,
		bool enable,
		hkRefPtr<hkbModifier> modifier
	)
	{
		hkRefPtr<hkbEventDrivenModifier> edm = new hkbEventDrivenModifier();
		edm->m_name = name.c_str();
		if (_event_map.find(activate_event) == _event_map.end())
		{
			edm->m_activateEventId = -1;
		}
		else {
			edm->m_activateEventId = _event_map.at(activate_event);
		}
		edm->m_activeByDefault = activeByDefault;
		if (_event_map.find(deactivate_event) == _event_map.end())
		{
			edm->m_deactivateEventId = -1;
		}
		else {
			edm->m_deactivateEventId = _event_map.at(deactivate_event);
		}
		edm->m_enable = enable;
		edm->m_userData = 1;
		edm->m_modifier = modifier;
		return edm;
	}

	hkRefPtr<hkbTimerModifier> timer_modifier(const std::string& name, const std::string& event, double alarm_time_seconds)
	{
		hkRefPtr<hkbTimerModifier> timer = new hkbTimerModifier();
		timer->m_name = name.c_str();
		timer->m_enable = true;
		timer->m_alarmTimeSeconds = alarm_time_seconds;
		timer->m_alarmEvent.m_id = _event_map.at(event);
		timer->m_userData = 0;
		return timer;
	}

	hkRefPtr<hkbModifierList> full_ragdoll_modifier_list()
	{
		hkRefPtr<hkbModifierList> list = new hkbModifierList();
		list->m_name = "full_ragdoll_ml";
		list->m_userData = 0;
		list->m_modifiers.pushBack
		(
			event_driven_modifier
			(
				"full_ragdoll_edm",
				"",
				"GetUpBegin",
				true,
				true,
				powered_ragdoll("full_ragdoll_powered_no_matching", 0.).val()
			)
		);

		hkRefPtr<hkbModifierList> sub_list = new hkbModifierList();
		{
			sub_list->m_name = "match_and_send_getup_ml";
			sub_list->m_userData = 0;
			sub_list->m_modifiers.pushBack
			(
				powered_ragdoll("full_ragdoll_powered_matching", 200.).val()
			);
			sub_list->m_modifiers.pushBack
			(
				timer_modifier("getup_timer", "GetUpStart", 0.5).val()
			);
		}

		list->m_modifiers.pushBack
		(
			event_driven_modifier
			(
				"turn_on_matching_edm",
				"GetUpBegin",
				"",
				false,
				true,
				sub_list.val()
			)
		);
		return list;
	}

	hkRefPtr<hkbManualSelectorGenerator> manual_selector(const std::string& name, std::vector<hkRefPtr<hkbGenerator>> generators)
	{
		hkRefPtr<hkbManualSelectorGenerator> msg = new hkbManualSelectorGenerator();
		msg->m_name = name.c_str();
		msg->m_userData = 0;
		msg->m_currentGeneratorIndex = 0;
		msg->m_selectedGeneratorIndex = 0;
		for (const auto& generator : generators)
			msg->m_generators.pushBack(generator);
		return msg;
	}

	hkRefPtr<hkbPoseMatchingGenerator> pose_matcher
	(
		const std::string& name,
		const std::string& start_matching_event,
		const std::string& start_playing_event,
		const std::vector<std::pair<double, hkRefPtr<hkbGenerator>>>& generators
	)
	{
		hkRefPtr<hkbPoseMatchingGenerator> matcher = new hkbPoseMatchingGenerator();
		matcher->m_name = name.c_str();
		matcher->m_userData = 0;
		matcher->m_flags = 0;
		matcher->m_subtractLastChild = false;
		matcher->m_rootBoneIndex = 0;
		matcher->m_pelvisIndex = ragdollIndex_pelvis;
		matcher->m_anotherBoneIndex = ragdollIndex_rightUpperArm;
		matcher->m_otherBoneIndex = ragdollIndex_head;
		matcher->m_startMatchingEventId = _event_map.at(start_matching_event);
		matcher->m_startPlayingEventId = _event_map.at(start_playing_event);
		matcher->m_blendParameter = 0;
		matcher->m_blendSpeed = 1.;
		matcher->m_indexOfSyncMasterChild = -1;
		matcher->m_maxCyclicBlendParameter = 1.;
		matcher->m_minCyclicBlendParameter = 0.;
		matcher->m_minSpeedToSwitch = 0.2;
		matcher->m_minSwitchTimeFullError = 0.;
		matcher->m_minSwitchTimeNoError = 0.2;
		matcher->m_mode = hkbPoseMatchingGenerator::Mode::MODE_MATCH;
		matcher->m_referencePoseWeightThreshold = 0.;
		matcher->m_worldFromModelRotation = { 0., 0., 0., 1. };

		for (const auto& generator : generators)
		{
			hkRefPtr child = new hkbBlenderGeneratorChild();
			child->m_boneWeights = new hkbBoneWeightArray();
			child->m_worldFromModelWeight = 1.;
			child->m_generator = generator.second;
			child->m_weight = generator.first;
			matcher->m_children.pushBack(
				child
			);
		}
		return matcher;
	}

	void add_notify_event
	(
		hkRefPtr<hkbStateMachineStateInfo> state, 
		const std::string& enterEvent,
		const std::string& exitEvent
	) 
	{
		hkbEventProperty e;
		e.m_id = _event_map.at(enterEvent);
		e.m_payload = NULL;
		if (_event_map.find(enterEvent) != _event_map.end())
		{
			if (state->m_enterNotifyEvents == NULL)
			{
				state->m_enterNotifyEvents = new hkbStateMachineEventPropertyArray();
			}
			state->m_enterNotifyEvents->m_events.pushBack(e);
		}
		if (_event_map.find(exitEvent) != _event_map.end())
		{
			if (state->m_exitNotifyEvents == NULL)
			{
				state->m_exitNotifyEvents = new hkbStateMachineEventPropertyArray();
			}
			state->m_exitNotifyEvents->m_events.pushBack(e);
		}
	}

	void bind(hkRefPtr<hkbBindable> object, const std::string& object_path, const std::string& variable)
	{
		if (object->m_variableBindingSet == NULL)
		{
			object->m_variableBindingSet = new hkbVariableBindingSet();
			object->m_variableBindingSet->m_indexOfBindingToEnable = -1;
		}
		hkbVariableBindingSetBinding bind;
		bind.m_bindingType = hkbVariableBindingSetBinding::BindingType::BINDING_TYPE_VARIABLE;
		bind.m_memberPath = object_path.c_str();
		bind.m_variableIndex = _variables_map.at(variable);
		bind.m_bitIndex = -1;
		object->m_variableBindingSet->m_bindings.pushBack(bind);
	}

	hkRefPtr<hkbGetUpModifier> getup_modifier(const std::string& name)
	{
		hkRefPtr<hkbGetUpModifier> getup = new hkbGetUpModifier();
		getup->m_name = name.c_str();
		getup->m_rootBoneIndex = ragdollIndex_pelvis;
		getup->m_anotherBoneIndex = ragdollIndex_rightUpperArm;
		getup->m_otherBoneIndex = ragdollIndex_head;
		getup->m_alignWithGroundDuration = 0.25;
		getup->m_duration = 1.;
		getup->m_enable = true;
		getup->m_groundNormal = hkVector4(0., 0., 1., 0.);
		getup->m_userData = 0;
		return getup;
	}

	hkRefPtr<hkbKeyframeBonesModifier> keyframe_modifier(const std::string& name)
	{
		hkRefPtr<hkbKeyframeBonesModifier> keyframe = new hkbKeyframeBonesModifier();
		keyframe->m_name = name.c_str();
		keyframe->m_userData = 1;
		keyframe->m_enable = true;
		keyframe->m_keyframedBonesList = new hkbBoneIndexArray();
		keyframe->m_keyframedBonesList->m_boneIndices.pushBack(ragdollIndex_pelvis);
		keyframe->m_keyframedBonesList->m_boneIndices.pushBack(ragdollIndex_head);
		keyframe->m_keyframedBonesList->m_boneIndices.pushBack(ragdollIndex_rightUpperArm);
		return keyframe;
	}

	hkRefPtr<hkbRigidBodyRagdollControlsModifier> rigidbodies_modifier(const std::string& name)
	{
		hkRefPtr<hkbRigidBodyRagdollControlsModifier> driver = new  hkbRigidBodyRagdollControlsModifier();
		driver->m_name = name.c_str();
		driver->m_bones = new hkbBoneIndexArray();
		driver->m_controlData.m_durationToBlend = 0.5f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_accelerationGain = 1.f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_hierarchyGain = 0.17f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_positionGain = 0.05f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_positionMaxAngularVelocity = 1.8f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_positionMaxLinearVelocity = 1.4f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_snapGain = 0.1f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_snapMaxAngularDistance = 0.1f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_snapMaxAngularVelocity = 0.3f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_snapMaxLinearDistance = 0.03f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_snapMaxLinearVelocity = 0.3f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_velocityDamping = 0.f;
		driver->m_controlData.m_keyFrameHierarchyControlData.m_velocityGain = 0.6f;
		driver->m_enable = false;
		return driver;
	}

	hkRefPtr<BSIsActiveModifier> is_active_modifier(const std::string& name)
	{
		hkRefPtr<BSIsActiveModifier> modifier = new  BSIsActiveModifier();
		modifier->m_name = name.c_str();
		modifier->m_enable = true;
		modifier->m_userData = 2;
		modifier->m_bIsActive0 = false;
		modifier->m_bInvertActive0 = false;
		modifier->m_bIsActive1 = false;
		modifier->m_bInvertActive1 = false;
		modifier->m_bIsActive2 = false;
		modifier->m_bInvertActive2 = false;
		modifier->m_bIsActive3 = false;
		modifier->m_bInvertActive3 = false;
		modifier->m_bIsActive4 = false;
		modifier->m_bInvertActive4 = false;
		return modifier;
	}

	std::pair<hkRefPtr<hkbModifier>, hkRefPtr<hkbModifier>> animate_common_modifiers()
	{
		return {
			keyframe_modifier("ragdoll_to_animate_keyframe").val(),
			rigidbodies_modifier("ragdoll_to_animate_driver").val()
		};
	}

	hkRefPtr<hkbModifierList> ragdoll_to_animate_modifier_list()
	{
		hkRefPtr<hkbModifierList> list = new hkbModifierList();
		list->m_userData = 0;
		list->m_name = "ragdoll_to_animate_ml";

		list->m_modifiers.pushBack(getup_modifier("ragdoll_to_animate_getup").val());
		
		auto is_active_mod = is_active_modifier("ragdoll_to_animate_isactive");
		bind(is_active_mod.val(), "bIsActive0", "bAnimationDriven");
		list->m_modifiers.pushBack(is_active_mod.val());

		return list;
	}

	hkRefPtr<hkbModifierList> animated_modifers()
	{
		hkRefPtr<hkbModifierList> list = new hkbModifierList();
		list->m_userData = 0;
		list->m_name = "animated_ml";
		return list;
	}

	hkRefPtr<hkbGenerator> physics(hkRefPtr<hkbGenerator> fsm_in)
	{
		createEvent("GetUp", false);
		createEvent("Reanimated", false);
		createEvent("GetUpBegin", false);
		createEvent("GetUpEnd", false);
		createEvent("GetUpStart", false);
		createEvent("Ragdoll", false);
		createEvent("RagdollInstant", false);
		createEvent("AddCharacterControllerToWorld", false);
		createEvent("RemoveCharacterControllerFromWorld", false);

		createVariable("iGetUpType", 0);
		createVariable("bAnimationDriven", false);

		//IDLE actions
		IDLERecord("Death", "Ragdoll", "ActionDeathWait");
		IDLERecord("RagdollInstant", "RagdollInstant", "ActionRagdollInstant");

		hkRefPtr<hkbStateMachine> ragdoll_fsm = fsm("ragdoll_fsm");
		
		//State 0 is default animation state

		//common modifiers for animated state and ragdoll to animate
		auto anim_modifers = animate_common_modifiers();
		auto animated_ml = animated_modifers();
		animated_ml->m_modifiers.pushBack(anim_modifers.first);
		animated_ml->m_modifiers.pushBack(anim_modifers.second);
		auto keyframed_state = state(ragdoll_fsm, "default_state", modify(animated_ml.val(), fsm_in.val()).val());

		//State 1 is animate -> full ragdoll
		auto full_ragdoll_ml = full_ragdoll_modifier_list();
		std::vector<std::pair<double, hkRefPtr<hkbGenerator>>> getup_animations;
		std::vector<std::pair<double, hkRefPtr<hkbGenerator>>> reanimate_animations;
		for (const auto& entry : analyzer.getups())
		{
			auto getup_clip = clip(entry.second, hkbClipGenerator::PlaybackMode::MODE_SINGLE_PLAY);
			trigger(getup_clip, "GetUpEnd", 0, true);
			trigger(getup_clip, "AddCharacterControllerToWorld", -0.2, true);
			trigger(getup_clip, "GetUp", -0.3, true);
			auto reanimate_clip = clip(entry.second, hkbClipGenerator::PlaybackMode::MODE_SINGLE_PLAY);
			trigger(reanimate_clip, "GetUpEnd", 0, true);
			trigger(reanimate_clip, "AddCharacterControllerToWorld", -0.2, true);
			trigger(reanimate_clip, "Reanimated", -0.3, true);

			getup_animations.push_back({ 1., getup_clip.val() });
			reanimate_animations.push_back({ 1., reanimate_clip.val() });
		}

		auto full_ragdoll_selector = manual_selector(
			"getup_pose_matcher",
			{
				//iGetUpType == 0, reanimate
				pose_matcher(
					"reanimate_matcher",
					"Ragdoll",
					"GetUpStart",
					reanimate_animations
				).val(),
				//iGetUpType == 1, get up
				pose_matcher(
					"getup_matcher",
					"Ragdoll",
					"GetUpStart",
					getup_animations
				).val()
			}
		);
		bind(full_ragdoll_selector.val(), "selectedGeneratorIndex", "iGetUpType");

		auto full_ragdoll_state = state(
			ragdoll_fsm, 
			"full_ragdoll_state", 
			modify(
				full_ragdoll_ml.val(),
				full_ragdoll_selector.val()
			).val()
		);
		add_notify_event(full_ragdoll_state,"RemoveCharacterControllerFromWorld","");

		//State 1 is full ragdoll -> animate
		auto ragdoll_to_animate_ml = ragdoll_to_animate_modifier_list();
		ragdoll_to_animate_ml->m_modifiers.pushBack(anim_modifers.first);
		ragdoll_to_animate_ml->m_modifiers.pushBack(anim_modifers.second);
		auto ragdoll_to_default_state = state(
			ragdoll_fsm, 
			"ragdoll_to_default_state", 
			modify(
				ragdoll_to_animate_ml.val(),
				full_ragdoll_selector.val()
			).val()
		);

		auto ragdoll_effect = effect(
			"RagdollBlend",
			0.2,
			hkbBlendingTransitionEffect::SELF_TRANSITION_MODE_BLEND,
			hkbBlendingTransitionEffect::EVENT_MODE_DEFAULT
		);

		//Any state -> ragdoll on Ragdoll, RagdollInstant
		transition(ragdoll_fsm, "Ragdoll", full_ragdoll_state, ragdoll_effect.val());
		transition(ragdoll_fsm, "RagdollInstant", full_ragdoll_state, NULL);

		//ragdoll_to_default_state -> default_state
		transition(ragdoll_fsm, "GetUpEnd", keyframed_state, ragdoll_effect.val(), ragdoll_to_default_state);
		transition(ragdoll_fsm, "GetUpStart", ragdoll_to_default_state, NULL, full_ragdoll_state);

		return ragdoll_fsm.val();
	}

	void assemble(hkRefPtr<hkbGenerator> root_fsm)
	{
		root_data.m_variableInitialValues = &root_data_init_vars;
		root_data.m_stringData = &root_string_data;

		std::string behavior_name = prefix + creature_tag + "Behavior";
		graph.m_name = behavior_name.c_str();
		graph.m_userData = 0;
		graph.m_variableMode = hkbBehaviorGraph::VARIABLE_MODE_DISCARD_WHEN_INACTIVE;
		graph.m_rootGenerator = root_fsm;
		graph.m_data = &root_data;

		hkRootLevelContainer container;
		container.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant("hkbBehaviorGraph", &graph, &graph.staticClass()));
		HKXWrapper().write_xml(&container, output_file);
	}

public:
	TES4BehaviorAssembler(
		const std::string& prefix,
		const std::string& creature_tag,
		const std::string& output_file,
		const fs::path& behavior_file,
		std::map<fs::path, ckcmd::HKX::RootMovement>& root_movements,
		int ragdollIndex_pelvis,
		int ragdollIndex_rightUpperArm,
		int ragdollIndex_head,
		Collection& conversionCollection,
		ModFile*& conversionPlugin,
		const std::set<Ob::CREARecord*>& CREAs,
		std::map<std::string, Sk::AACTRecord*>& actions,
		AnimationSetAnalyzer& analyzer,

		map<hkRefPtr<hkbClipGenerator>, fs::path>& generators,
		map<int, string>& behavior_events
	) :
		prefix(prefix),
		creature_tag(creature_tag),
		output_file(output_file),
		behavior_file(behavior_file),
		root_movements(root_movements),
		ragdollIndex_pelvis(ragdollIndex_pelvis),
		ragdollIndex_rightUpperArm(ragdollIndex_rightUpperArm),
		ragdollIndex_head(ragdollIndex_head),
		conversionCollection(conversionCollection),
		conversionPlugin(conversionPlugin),
		CREAs(CREAs),
		actions(actions),
		analyzer(analyzer),
		generators(generators),
		behavior_events(behavior_events)
	{
		_default_transition_effect = effect("default_transition");
		creature_tag_camel = creature_tag;
		creature_tag_camel[0] = toupper(creature_tag_camel[0]);
		assemble(
			physics(
				move(
					idle_fsm()
				)
			)
		);
	}
};

void CreateDummyBehavior
(
	const std::string& prefix,
	const std::string& creature_tag,
	const std::string& output_file,
	std::map<fs::path, ckcmd::HKX::RootMovement>& root_movements,
	AnimationSetAnalyzer& analyzer,
	map<hkRefPtr<hkbClipGenerator>, fs::path>& generators,
	map<int, string>& behavior_events
)
{
	//Assemble a behavior for the creature

	hkbStateMachine root_fsm;
	hkbBehaviorGraph graph;
	hkbBehaviorGraphData root_data;
	hkbBehaviorGraphStringData root_string_data;
	hkbVariableValueSet root_data_init_vars;
	hkbBlendingTransitionEffect transition_effect;

	size_t event_count = 0;
	map<string, int> event_map;
	vector<string> events;
	//Default events: Start, End, Next
	event_map[prefix + "Start"] = event_count++;
	events.push_back(prefix + "Start");
	event_map[prefix + "End"] = event_count++;
	events.push_back(prefix + "End");
	event_map[prefix + "Next"] = event_count++;
	events.push_back(prefix + "Next");

	//String Data
	root_string_data.m_eventNames.setSize(event_count);
	int count = 0;
	for (string event : events)
		root_string_data.m_eventNames[count++] = event.c_str();

	root_string_data.m_variableNames.pushBack("bAnimationDriven");

	//Data
	root_data.m_eventInfos.setSize(event_count);
	for (int e = 0; e < event_count; e++)
		root_data.m_eventInfos[e].m_flags = 0;

	hkbRoleAttribute role_attribute;
	role_attribute.m_role = hkbRoleAttribute::ROLE_DEFAULT;
	role_attribute.m_flags = hkbRoleAttribute::FLAG_NONE;
	hkbVariableInfo info;
	info.m_role = role_attribute;
	info.m_type = hkbVariableInfo::VARIABLE_TYPE_BOOL;
	root_data.m_variableInfos.pushBack(info);
	hkbVariableValue value;
	value.m_value = 1;
	root_data_init_vars.m_wordVariableValues.pushBack(value);

	root_data.m_variableInitialValues = &root_data_init_vars;
	root_data.m_stringData = &root_string_data;

	//prepare transition effect
	transition_effect.m_name = "zero_duration";
	transition_effect.m_userData = 0;
	transition_effect.m_selfTransitionMode = hkbBlendingTransitionEffect::SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC;
	transition_effect.m_eventMode = hkbBlendingTransitionEffect::EVENT_MODE_IGNORE_FROM_GENERATOR;
	transition_effect.m_duration = 0.0;
	transition_effect.m_toGeneratorStartTimeFraction = 0.0;
	transition_effect.m_flags = 0;
	transition_effect.m_endMode = hkbBlendingTransitionEffect::END_MODE_NONE;
	transition_effect.m_blendCurve = hkbBlendCurveUtils::BLEND_CURVE_SMOOTH;

	//Root FSM
	root_fsm.m_name = "RootBehavior";
	root_fsm.m_startStateId = 0;
	root_fsm.m_returnToPreviousStateEventId = -1;
	root_fsm.m_randomTransitionEventId = -1;
	root_fsm.m_transitionToNextHigherStateEventId = -1;
	root_fsm.m_transitionToNextLowerStateEventId = -1;
	root_fsm.m_syncVariableIndex = -1;
	root_fsm.m_userData = 0;
	root_fsm.m_eventToSendWhenStateOrTransitionChanges.m_id = -1;
	root_fsm.m_eventToSendWhenStateOrTransitionChanges.m_payload = NULL;
	root_fsm.m_wrapAroundStateId = false;
	root_fsm.m_maxSimultaneousTransitions = 32;
	root_fsm.m_startStateMode = hkbStateMachine::START_STATE_MODE_DEFAULT;
	root_fsm.m_selfTransitionMode = hkbStateMachine::SELF_TRANSITION_MODE_NO_TRANSITION;

	int state_index = -1;
	int start_state_index = 0;
	for (const auto& entry : root_movements) {
		hkRefPtr<hkbStateMachineStateInfo> state = new hkbStateMachineStateInfo(); state_index++;
		if (analyzer.idle() == entry.first)
			start_state_index = state_index;

		//create notify events;
		hkRefPtr<hkbStateMachineEventPropertyArray> enter_notification = new hkbStateMachineEventPropertyArray();
		enter_notification->m_events.setSize(1);
		enter_notification->m_events[0].m_id = event_map[prefix + "Start"];
		enter_notification->m_events[0].m_payload = NULL;
		state->m_enterNotifyEvents = enter_notification;

		hkRefPtr<hkbStateMachineEventPropertyArray> exit_notification = new hkbStateMachineEventPropertyArray();
		exit_notification->m_events.setSize(1);
		exit_notification->m_events[0].m_id = event_map[prefix + "End"];
		exit_notification->m_events[0].m_payload = NULL;
		state->m_exitNotifyEvents = exit_notification;

		if (state_index >= 0) {
			//create transition to the next state;
			hkRefPtr<hkbStateMachineTransitionInfoArray> transition = new hkbStateMachineTransitionInfoArray();
			transition->m_transitions.setSize(1);
			transition->m_transitions[0].m_triggerInterval.m_enterEventId = -1;
			transition->m_transitions[0].m_triggerInterval.m_exitEventId = -1;
			transition->m_transitions[0].m_triggerInterval.m_enterTime = 0.0;
			transition->m_transitions[0].m_triggerInterval.m_exitTime = 0.0;

			transition->m_transitions[0].m_initiateInterval.m_enterEventId = -1;
			transition->m_transitions[0].m_initiateInterval.m_exitEventId = -1;
			transition->m_transitions[0].m_initiateInterval.m_enterTime = 0.0;
			transition->m_transitions[0].m_initiateInterval.m_exitTime = 0.0;

			transition->m_transitions[0].m_transition = &transition_effect;
			transition->m_transitions[0].m_eventId = event_map[prefix + "Next"];
			int next_state_id = state_index + 1 != root_movements.size() ? state_index + 1 : 0;
			transition->m_transitions[0].m_toStateId = next_state_id;
			transition->m_transitions[0].m_fromNestedStateId = 0;
			transition->m_transitions[0].m_toNestedStateId = 0;
			transition->m_transitions[0].m_priority = 0;
			transition->m_transitions[0].m_flags = hkbStateMachineTransitionInfo::FLAG_DISABLE_CONDITION;
			state->m_transitions = transition;
		}

		//generator
		std::string sequenceName = fs::path(entry.first).filename().replace_extension("").string();
		hkRefPtr<hkbClipGenerator> generator = new hkbClipGenerator();
		generator->m_name = sequenceName.c_str();
		generator->m_userData = 0;
		generator->m_animationName = fs::path(entry.first).string().c_str();
		generator->m_cropStartAmountLocalTime = 0.0;
		generator->m_cropEndAmountLocalTime = 0.0;
		generator->m_startTime = 0.0;
		generator->m_playbackSpeed = 1.0;
		generator->m_enforcedDuration = 0.0;
		generator->m_userControlledTimeFraction = 0.0;
		generator->m_animationBindingIndex = -1;
		generator->m_mode = hkbClipGenerator::PlaybackMode::MODE_LOOPING;
		generator->m_flags = 0;
		state->m_generator = generator;

		generators[generator] = fs::path(entry.first);

		//finish packing the state;
		std::string stateName = sequenceName + "State";
		state->m_name = stateName.c_str();
		state->m_stateId = state_index;
		state->m_probability = 1.000000;
		state->m_enable = true;

		root_fsm.m_states.pushBack(state);
	}

	root_fsm.m_startStateId = start_state_index;

	std::string behavior_name = prefix + creature_tag + "Behavior";
	graph.m_name = behavior_name.c_str();
	graph.m_userData = 0;
	graph.m_variableMode = hkbBehaviorGraph::VARIABLE_MODE_DISCARD_WHEN_INACTIVE;
	graph.m_rootGenerator = &root_fsm;
	graph.m_data = &root_data;

	hkRootLevelContainer container;
	container.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant("hkbBehaviorGraph", &graph, &graph.staticClass()));
	HKXWrapper().write_xml(&container, output_file);

	for (auto& entry : event_map)
	{
		behavior_events[entry.second] = entry.first;
	}
}

void CreateHavokProject(
	const std::string& relative_character_file,
	const std::string& output_file
) {
	hkbProjectStringData string_data;
	string_data.m_characterFilenames.pushBack(relative_character_file.c_str());
	hkbProjectData data;
	data.m_worldUpWS = hkVector4(0.000000, 0.000000, 1.000000, 0.000000);
	data.m_stringData = &string_data;
	data.m_defaultEventMode = hkbTransitionEffect::EVENT_MODE_DEFAULT;

	hkRootLevelContainer container;
	container.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant("hkbProjectData", &data, &data.staticClass()));

	HKXWrapper().write_xml(&container, output_file);
}

void CreateHavokCharacter(
	const std::string& character_name,
	const std::string& rig_relative_file,
	const std::string& behavior_relative_file,
	const std::string& output_file,
	const std::map<fs::path, ckcmd::HKX::RootMovement>& animations_relative_files
) {
	hkbCharacterData data;
	hkbVariableValueSet values;
	hkbCharacterStringData string_data;
	hkbMirroredSkeletonInfo skel_info;

	skel_info.m_mirrorAxis = hkVector4(1.000000, 0.000000, 0.000000, 0.000000);

	// hkbCharacterStringData
	string_data.m_name = character_name.c_str();
	string_data.m_rigName = rig_relative_file.c_str();
	string_data.m_ragdollName = rig_relative_file.c_str();
	string_data.m_behaviorFilename = behavior_relative_file.c_str();

	for (const auto& animation : animations_relative_files)
	{
		string_data.m_animationNames.pushBack(animation.first.string().c_str());
	}

	//data
	hkbCharacterDataCharacterControllerInfo char_info;
	char_info.m_capsuleHeight = 1.700000;
	char_info.m_capsuleRadius = 0.400000;
	char_info.m_collisionFilterInfo = 1;
	char_info.m_characterControllerCinfo = NULL;
	data.m_characterControllerInfo = char_info;

	data.m_modelUpMS = hkVector4(0.000000, 0.000000, 1.000000, 0.000000);
	data.m_modelForwardMS = hkVector4(0.000000, 1.000000, 0.000000, 0.000000);
	data.m_modelRightMS = hkVector4(1.000000, 0.000000, 0.000000, 0.000000);

	data.m_characterPropertyValues = &values;
	data.m_stringData = &string_data;
	data.m_mirroredSkeletonInfo = &skel_info;
	data.m_scale = 1.0;

	hkRootLevelContainer container;
	container.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant("hkbCharacterData", &data, &data.staticClass()));

	HKXWrapper().write_xml(&container, output_file);
}

Sk::SKBOD2::BodyParts FindBodyPart(
	const fs::path& file,
	const std::string creature_tag,
	vector<NiObjectRef>& out_blocks,
	NiObjectRef& out_root,
	std::map<fs::path, std::string>& skeleton_body_parts,
	vector<string>& slots,
	std::map<std::set<std::string>, std::string>& bodySlots
)
{
	auto meshes = DynamicCast<NiTriShape>(out_blocks);
	std::string part_name = file.filename().replace_extension("").string();
	Sk::SKBOD2::BodyParts out = Sk::SKBOD2::BodyParts::bpBody;
	if (skeleton_body_parts.find(file) != skeleton_body_parts.end())
	{
		part_name = skeleton_body_parts.at(file);
	}
	else {
		std::set<std::string> skin_bones;
		for (auto& geometry : meshes)
		{
			if (geometry->GetSkinInstance() != NULL)
			{
				auto skin = geometry->GetSkinInstance();
				auto& bones = skin->GetBones();
				for (const auto& bone : bones)
				{
					skin_bones.insert(bone->GetName());
				}
			}
		}
		if (bodySlots.find(skin_bones) != bodySlots.end())
		{
			part_name = bodySlots.at(skin_bones);
		}
		else {
			bodySlots[skin_bones] = part_name;
		}
	}
	{
		//Sanitize part name
		while (isdigit(part_name[part_name.size() - 1]) || part_name[part_name.size() - 1] == ':' || part_name[part_name.size() - 1] == ' ')
			part_name = part_name.substr(0, part_name.size() - 1);
		std::transform(part_name.begin(), part_name.end(), part_name.begin(), toupper);
	}
	auto slot_it = std::find(slots.begin(), slots.end(), part_name);
	int index = 0;
	if (slot_it == slots.end())
	{
		slots.push_back(part_name);
		index = slots.size() - 1;
	}
	else {
		index = std::distance(slots.begin(), slot_it);
	}
	out = (Sk::SKBOD2::BodyParts)(1 << index);
	int partition_index = 30 + index;

	bool isBody = true; // DynamicCast<NiNode>(out_root)->GetExtraDataList().size() == 0; // no prn
	for (auto& block : out_blocks)
	{
		if (block->IsDerivedType(NiStringExtraData::TYPE))
		{
			NiStringExtraDataRef ed = DynamicCast<NiStringExtraData>(block);
			if (ed->GetName().find("Prn") != string::npos)
			{
				isBody = false;
				break;
			}
		}
	}

	if (!isBody)
	{
		//bake transforms on shapes
		auto niroot = DynamicCast<NiNode>(out_root);
		hkQsTransform root_transform; root_transform.setIdentity();
		root_transform.setTranslation(TOVECTOR4(niroot->GetTranslation()));
		root_transform.setRotation(TOQUAT(niroot->GetRotation().AsQuaternion()));
		niroot->SetTranslation({ 0., 0., 0. });
		niroot->SetRotation(Matrix33::IDENTITY);
		//root_transform.setScale({ 1., 1., 1. });
		auto shapes = DynamicCast<NiTriShape>(out_blocks);
		for (auto& shape : shapes)
		{
			if (auto data = DynamicCast<NiTriShapeData>(shape->GetData()))
			{
				hkQsTransform shape_transform;
				shape_transform.setTranslation(TOVECTOR4(shape->GetTranslation()));
				shape_transform.setRotation(TOQUAT(shape->GetRotation().AsQuaternion()));
				shape_transform.setScale({ shape->GetScale(), shape->GetScale(), shape->GetScale() });

				hkQsTransform transform(root_transform); transform.setMulEq(shape_transform);
				auto& vertices = data->GetVertices();
				{
					for (auto& vertex : vertices)
					{
						hkVector4 vector4 = TOVECTOR4(vertex);
						vector4.setTransformedPos(transform, vector4);
						vertex = TOVECTOR3(vector4);
					}
				}

				hkVector4 vector4 = TOVECTOR4(data->GetCenter());
				vector4.setTransformedPos(transform, vector4);
				data->SetCenter(TOVECTOR3(vector4));

				shape->SetTranslation({ 0., 0., 0. });
				shape->SetRotation(Matrix33::IDENTITY);
				shape->SetScale(1.);
				data->SetVertices(vertices);
			}
		}
	}

	auto nodes = DynamicCast<NiNode>(out_blocks);
	for (auto& node : nodes)
	{
		auto shape_children = DynamicCast<NiTriShape>(node->GetChildren());
		std::vector<NiTriShapeRef> split;
		std::set<NiTriShapeRef> to_delete;
		for (auto& shape : shape_children)
		{
			if (auto skin = shape->GetSkinInstance())
			{
				if (skin->GetBones().size() > 80)
				{
					int bones = skin->GetBones().size();

					to_delete.insert(shape);
					auto old_shape_data = DynamicCast<NiTriShapeData>(shape->GetData());
					auto old_shaders = DynamicCast<BSLightingShaderProperty>(shape->GetShaderProperty());
					auto old_alpha = DynamicCast<NiAlphaProperty>(shape->GetAlphaProperty());
					auto old_shape_triangles = old_shape_data->GetTriangles();
					auto old_shape_vertices = old_shape_data->GetVertices();
					auto old_shape_normals = old_shape_data->GetNormals();
					auto old_shape_tangents = old_shape_data->GetTangents();
					auto old_shape_bitangents = old_shape_data->GetBitangents();
					auto old_shape_uvs = old_shape_data->GetUvSets()[0];
					auto old_shape_vcs = old_shape_data->GetVertexColors();

					auto old_shape_bones = skin->GetBones();
					auto old_shape_bonelist = skin->GetData()->GetBoneList();

					int new_meshes_size = 2; // = bones / 80;
					//if (bones % 80 > 0) {
					//	new_meshes_size++;
					//}
					split.resize(new_meshes_size);
					for (int i = 0; i < new_meshes_size; ++i)
					{
						NiTriShapeRef new_shape = new NiTriShape();
						meshes.push_back(new_shape);
						NiTriShapeDataRef new_data = new NiTriShapeData();
						NiSkinDataRef new_skin_data = new NiSkinData();
						NiSkinPartitionRef new_skin_partition = new NiSkinPartition();
						NiSkinInstanceRef new_skin_instance = new NiSkinInstance();
						BSLightingShaderPropertyRef new_shader = new BSLightingShaderProperty();
						BSShaderTextureSetRef new_textures = new BSShaderTextureSet();
						new_shape->SetSkinInstance(new_skin_instance);
						new_shape->SetData(StaticCast<NiGeometryData>(new_data));
						new_shape->SetShaderProperty(StaticCast<BSShaderProperty>(new_shader));
						new_skin_instance->SetSkinPartition(new_skin_partition);
						new_skin_instance->SetData(new_skin_data);
						new_skin_instance->SetSkeletonRoot(skin->GetSkeletonRoot());

						new_shape->SetName(shape->GetName() + ":" + to_string(i));
						new_shape->SetFlags(shape->GetFlags());
						new_shape->SetTranslation(shape->GetTranslation());
						new_shape->SetRotation(shape->GetRotation());
						new_shape->SetScale(shape->GetScale());

						new_data->SetVectorFlags(old_shape_data->GetVectorFlags());
						new_data->SetBsVectorFlags(old_shape_data->GetBsVectorFlags());
						new_data->SetConsistencyFlags(old_shape_data->GetConsistencyFlags());
						new_data->SetUvSets({vector<TexCoord>()});
						new_data->SetHasVertexColors(false);
						
						new_shader->SetSkyrimShaderType(old_shaders->GetSkyrimShaderType());
						new_shader->SetName(old_shaders->GetName());
						new_shader->SetExtraDataList(old_shaders->GetExtraDataList());
						new_shader->SetShaderFlags1_sk(old_shaders->GetShaderFlags1_sk());
						new_shader->SetShaderFlags2_sk(old_shaders->GetShaderFlags2_sk());
						new_shader->SetUvOffset(old_shaders->GetUvOffset());
						new_shader->SetUvScale(old_shaders->GetUvScale());

						new_textures->SetTextures(old_shaders->GetTextureSet()->GetTextures());
						new_shader->SetTextureSet(new_textures);
						new_shader->SetEmissiveColor(old_shaders->GetEmissiveColor());
						new_shader->SetEmissiveMultiple(old_shaders->GetEmissiveMultiple());
						new_shader->SetTextureClampMode(old_shaders->GetTextureClampMode());
						new_shader->SetAlpha(old_shaders->GetAlpha());
						new_shader->SetRefractionStrength(old_shaders->GetRefractionStrength());
						new_shader->SetGlossiness(old_shaders->GetGlossiness());
						new_shader->SetSpecularColor(old_shaders->GetSpecularColor());
						new_shader->SetSpecularStrength(old_shaders->GetSpecularStrength());
						new_shader->SetLightingEffect1(old_shaders->GetLightingEffect1());
						new_shader->SetLightingEffect2(old_shaders->GetLightingEffect2());

						new_skin_partition->SetSkinPartitionBlocks({ SkinPartition() });

						split[i] = new_shape;
					}

					vector<vector<Triangle>> triangles_by_z(new_meshes_size);

					for (const auto& triangle : old_shape_triangles)
					{
						float tris_min_x = numeric_limits<float>::max();
						for (int i = 0; i < 3; ++i)
						{
							auto vertex = old_shape_vertices[triangle[i]];
							if (vertex[0] < tris_min_x)
								tris_min_x = vertex[0];
						}

						if (tris_min_x <= 0.)
						{
							triangles_by_z[0].push_back(triangle);
						}
						else {
							triangles_by_z[1].push_back(triangle);
						}
					}

					for (int i = 0; i < new_meshes_size; ++i)
					{ 
						map<int, int> old_to_new_vertices;

						NiTriShapeRef new_shape = split[i];
						NiTriShapeDataRef new_data = StaticCast<NiTriShapeData>(split[i]->GetData());
						NiSkinInstanceRef new_skin_instance = split[i]->GetSkinInstance();
						NiSkinDataRef new_skin_data = new_skin_instance->GetData();
						NiSkinPartitionRef new_skin_partition = new_skin_instance->GetSkinPartition();

						auto& new_shape_triangles = new_data->GetTriangles();
						auto& new_shape_vertices = new_data->GetVertices();
						auto& new_shape_normals = new_data->GetNormals();
						auto& new_shape_tangents = new_data->GetTangents();
						auto& new_shape_bitangents = new_data->GetBitangents();
						auto new_shape_uvs = new_data->GetUvSets()[0];
						auto& new_shape_vcs = new_data->GetVertexColors();

						vector<NiNode* > new_shape_bones;
						auto& new_shape_bonelist = new_skin_data->GetBoneList();
						SkinPartition new_shape_partition;
						new_shape_partition.hasVertexMap = true;
						new_shape_partition.hasFaces = true;
						new_shape_partition.numWeightsPerVertex = 4;
						new_shape_partition.hasVertexWeights = true;
						new_shape_partition.hasBoneIndices = true;

						for (const auto& triangle : triangles_by_z[i])
						{
							Triangle new_triangle;

							for (int i = 0; i < 3; ++i)
							{
								int old_index = triangle[i];
								int new_index = new_shape_vertices.size();
								if (old_to_new_vertices.find(old_index) == old_to_new_vertices.end())
								{
									auto vertex = old_shape_vertices[old_index];

									new_shape_partition.vertexMap.push_back(new_index);
									new_shape_vertices.push_back(old_shape_vertices[triangle[i]]);
									new_shape_normals.push_back(old_shape_normals[triangle[i]]);
									new_shape_tangents.push_back(old_shape_tangents[triangle[i]]);
									new_shape_bitangents.push_back(old_shape_bitangents[triangle[i]]);
									new_shape_uvs.push_back(old_shape_uvs[triangle[i]]);

									if (old_shape_data->GetHasVertexColors())
									{
										new_shape_vcs.push_back(old_shape_vcs[triangle[i]]);
									}

									old_to_new_vertices[old_index] = new_index;
								}
								else {
									new_index = old_to_new_vertices.at(old_index);
								}
								
								new_triangle[i] = new_index;

								//add bones
								vector<BoneVertData >::const_iterator BoneVertData_it;
								auto bone_data_it = find_if(old_shape_bonelist.begin(), old_shape_bonelist.end(),
									[old_index, &BoneVertData_it](const BoneData& bonedata) {
										BoneVertData_it = find_if(
											bonedata.vertexWeights.begin(),
											bonedata.vertexWeights.end(),
											[old_index, &BoneVertData_it](const BoneVertData& data) {return data.index == old_index; });
										return BoneVertData_it != bonedata.vertexWeights.end();
									});
								if (bone_data_it != old_shape_bonelist.end())
								{
									auto old_bone = old_shape_bones[distance(old_shape_bonelist.begin(), bone_data_it)];
									auto new_bone_index = find_if(new_shape_bones.begin(), new_shape_bones.end(),
										[old_bone](const NiNode* bone) {return bone->GetName() == old_bone->GetName(); });
									int new_bonelist_index;
									if (new_bone_index == new_shape_bones.end())
									{
										new_bonelist_index = new_shape_bonelist.size();
										new_shape_bones.push_back(old_bone);
										new_shape_bonelist.push_back(*bone_data_it);
										new_shape_partition.bones.push_back(new_bonelist_index);
									}
								}
							}

							new_shape_triangles.push_back(new_triangle);
							new_shape_partition.triangles.push_back(new_triangle);
						}
						//Prune other partition weights
						for (int b = 0; b < new_shape_bonelist.size(); ++b)
						{
							for (auto it = new_shape_bonelist[b].vertexWeights.begin(); it != new_shape_bonelist[b].vertexWeights.end(); )
							{
								if (old_to_new_vertices.find(it->index) == old_to_new_vertices.end())
								{
									it = new_shape_bonelist[b].vertexWeights.erase(it);
								}
								else {
									it->index = old_to_new_vertices.at(it->index);
									it++;
								}
							}
						}

						//Rebuild partition
						for (int b = 0; b < new_shape_bonelist.size(); ++b)
						{
							for (const BoneVertData& vertexData : new_shape_bonelist[b].vertexWeights)
							{
								if (new_shape_partition.vertexWeights.size() <= vertexData.index)
									new_shape_partition.vertexWeights.resize(vertexData.index + 1);
								new_shape_partition.vertexWeights[vertexData.index].push_back(vertexData.weight);

								if (new_shape_partition.boneIndices.size() <= vertexData.index)
									new_shape_partition.boneIndices.resize(vertexData.index + 1);
								new_shape_partition.boneIndices[vertexData.index].push_back(b);
							}
						}

						//Set sizes
						for (int b = 0; b < new_shape_bonelist.size(); ++b)
						{
							for (const BoneVertData& vertexData : new_shape_bonelist[b].vertexWeights)
							{
								new_shape_partition.vertexWeights[vertexData.index].resize(4);

								float sum = 0.;
								for (int i = 0; i < 4; ++i)
								{
									sum += new_shape_partition.vertexWeights[vertexData.index][i];
								}
								for (int i = 0; i < 4; ++i)
								{
									new_shape_partition.vertexWeights[vertexData.index][i] /= sum;
								}

								new_shape_partition.boneIndices[vertexData.index].resize(4);
							}
							
						}
						
						new_skin_data->SetBoneList(new_shape_bonelist);
						new_skin_instance->SetBones(new_shape_bones);						

						new_skin_partition->SetSkinPartitionBlocks({ new_shape_partition });

						new_data->SetNumTriangles(new_shape_triangles.size());
						new_data->SetNumTrianglePoints(new_shape_triangles.size() * 3);
						new_data->SetHasTriangles(true);
						new_data->SetHasVertices(true);
						new_data->SetHasNormals(true);

						new_data->SetTriangles(new_shape_triangles);
						new_data->SetVertices(new_shape_vertices);
						new_data->SetNormals(new_shape_normals);
						new_data->SetTangents(new_shape_tangents);
						new_data->SetBitangents(new_shape_bitangents);
						new_data->SetUvSets({ new_shape_uvs });
						if (old_shape_data->GetHasVertexColors())
						{
							new_data->SetVertexColors(new_shape_vcs);
							new_data->SetHasVertexColors(true);
						}
					}
				}
			}
		}
	
		if (!to_delete.empty())
		{
			auto children = node->GetChildren();
			for (auto& shape : to_delete)
			{
				auto it = find(children.begin(), children.end(), shape);
				if (it != children.end())
					children.erase(it);
			}
			for (auto& shape : split)
			{
				children.push_back(DynamicCast<NiAVObject>(shape));
			}
			node->SetChildren(children);
		}
	}

	if (isBody)
	{
		NiNodeRef newRoot = new NiNode();
		newRoot->SetName(std::string("Scene Root"));
		newRoot->SetChildren(StaticCast<NiNode>(out_root)->GetChildren());
		newRoot->SetFlags(524302);
		newRoot->SetTranslation({ 0., 0., 0. });
		newRoot->SetRotation(Matrix33::IDENTITY);
		newRoot->SetScale(1.);
		
		out_root = StaticCast<NiObject>(newRoot);
	}

	for (auto& geometry : meshes)
	{
		if (auto skin = geometry->GetSkinInstance())
		{
			BSDismemberSkinInstanceRef creature_skin = new BSDismemberSkinInstance();
			creature_skin->SetBones(geometry->GetSkinInstance()->GetBones());
			creature_skin->SetData(geometry->GetSkinInstance()->GetData());
			creature_skin->SetSkeletonRoot(DynamicCast<NiNode>(out_root));
			auto& partitions = geometry->GetSkinInstance()->GetSkinPartition();
			creature_skin->SetSkinPartition(partitions);
			if (partitions != NULL)
			{
				auto& blocks = partitions->GetSkinPartitionBlocks();
				vector<BodyPartList >  value;
				for (const auto& partition : blocks)
				{
					BodyPartList bodyPart;
					bodyPart.partFlag = (BSPartFlag)(BSPartFlag::PF_EDITOR_VISIBLE | BSPartFlag::PF_START_NET_BONESET);
					bodyPart.bodyPart = (BSDismemberBodyPartType)partition_index;
					value.push_back(bodyPart);
				}
				creature_skin->SetPartitions(value);
			}

			geometry->SetSkinInstance(StaticCast<NiSkinInstance>(creature_skin));
		}
	}

	return out;
}

string LCS(string X, string Y, int m, int n)
{
	int maxlen = 0;         // stores the max length of LCS
	int endingIndex = m;    // stores the ending index of LCS in `X`

	// `lookup[i][j]` stores the length of LCS of substring `X[0�i-1]`, `Y[0�j-1]`
	vector<vector<int>> lookup;//[m + 1][n + 1];
	lookup.resize(m + 1);
	for (int i = 0; i < m + 1; ++i)
	{
		lookup[i].resize(n + 1, 0);
	}

	// initialize all cells of the lookup table to 0
	// memset(lookup, 0, sizeof(lookup));

	// fill the lookup table in a bottom-up manner
	for (int i = 1; i <= m; i++)
	{
		for (int j = 1; j <= n; j++)
		{
			// if the current character of `X` and `Y` matches
			if (X[i - 1] == Y[j - 1])
			{
				lookup[i][j] = lookup[i - 1][j - 1] + 1;

				// update the maximum length and ending index
				if (lookup[i][j] > maxlen)
				{
					maxlen = lookup[i][j];
					endingIndex = i;
				}
			}
		}
	}

	// return longest common substring having length `maxlen`
	return X.substr(endingIndex - maxlen, maxlen);
}

static std::string crc_32(std::string& to_crc)
{
	transform(to_crc.begin(), to_crc.end(), to_crc.begin(), ::tolower);
	long long crc = stoll(HkCRC::compute(to_crc), NULL, 16);
	return to_string(crc);
}

void ConvertAssets(
	const std::string& prefix,
	const fs::path oblivionDataFolder,
	std::map<fs::path, std::set<fs::path>>& skeletons,
	std::map<fs::path, Sk::RACERecord*>& races,
	std::map<fs::path, std::set<Ob::CREARecord*>> actors,
	map<fs::path, map<set<string>, set< Ob::CREARecord*>>>& skins,
	const fs::path outputFolder,
	HKXWrapperCollection& wrappers,
	vector<pair<string, Vector3>>& metadata,
	Collection& conversionCollection,
	ModFile*& conversionPlugin,
	std::map<std::string, Sk::AACTRecord*>& actions
)
{
	int index = 0;

	fs::path animdata = outputFolder / "meshes" / "animationdatasinglefile.txt";
	fs::path animsetdata = outputFolder / "meshes" / "animationsetdatasinglefile.txt";
	AnimationCache cache(animdata, animsetdata);

	for (const auto& entry : skeletons)
	{
		Log::Info("Converting %d/%d: %s", ++index, skeletons.size(), entry.first.string().c_str());
		std::string creature_tag = entry.first.parent_path().filename().string();
		std::string creature_tag_upper = creature_tag;
		creature_tag_upper[0] = toupper(creature_tag_upper[0]);
		std::string creature_path_no_meshes = entry.first.parent_path().string();
		replacepath(creature_path_no_meshes, "meshes", "tes4");
		std::string creature_path = entry.first.parent_path().string();
		replacepath(creature_path, "meshes", "meshes\\tes4");
		NifInfo info;
		vector<Ref<NiObject>> skeleton_converted_blocks;
		std::map<fs::path, ckcmd::HKX::RootMovement> root_movements;
		std::map<fs::path, std::string> body_parts;
		string creature_subfolder = fs::path(creature_path).filename().string();

		auto assets = load_override_or_bsa_nif_folder(entry.first, oblivionDataFolder, info);
		set<NiNodeRef> other_bones_in_accum;;
		hkTransform pelvis_local;
		std::map<std::string, hkTransform> original_local_pre_pelvis_transforms;
		std::map<std::string, std::string> renamed_specials;

		int ragdollPelvisIndex = -1;
		int ragdollRUpperArmIndex = -1;
		int ragdollHeadIndex = -1;
		bool hasRagdoll = false;

		{
			//Convert skeleton NIF
			fs::path creature_output_skeleton = outputFolder / creature_path / "Character Assets" / entry.first.filename();
			fs::create_directories(creature_output_skeleton.parent_path());		
			NifInfo info;
			info.userVersion = 12;
			info.userVersion2 = 83;
			info.version = Niflib::VER_20_2_0_7;

			auto& original_nodes = DynamicCast<NiNode>(std::get<0>(assets));

			//Convert special nodes
			for (auto& node : original_nodes)
			{
				auto name = node->GetName();
				if (name.find("magicnode") != string::npos || name.find("magicNode") != string::npos)
				{
					renamed_specials[node->GetName()] = "MagicEffectsNode";
					node->SetName(std::string("MagicEffectsNode"));
				}
			}

			NiObjectRef root;
			convert_blocks(
				std::get<0>(assets),
				skeleton_converted_blocks,
				root,
				wrappers,
				info,
				entry.first,
				outputFolder,
				metadata,
				true
			);

			//add skeleton root
			NiNodeRef rootn = DynamicCast<NiNode>(root);
			NiNodeRef toor = new NiNode();
			toor->SetName(std::string("NPC Root [Root]"));
			toor->SetChildren(rootn->GetChildren());
			toor->SetFlags(524302);
			toor->SetTranslation({ 0., 0., 0. });
			toor->SetRotation(Matrix33::IDENTITY);
			toor->SetScale(1.);

			rootn->SetChildren({StaticCast<NiAVObject>(toor)});
			skeleton_converted_blocks.push_back(StaticCast<NiObject>(toor));

			//Move ragdoll on the right node
			auto nodes = DynamicCast<NiNode>(skeleton_converted_blocks);
			NiNodeRef pelvis = NULL;
			std::map< NiNodeRef, NiNodeRef> parent_map;
			for (auto& node : nodes)
			{
				//Doesn't need no transform controller
				node->SetController(NULL);
				//Extradata is also ignored
				if (node != root)
					node->SetExtraDataList({});

				if (node->GetName().find("NonAccum") != string::npos)
				{
					auto ragdoll = DynamicCast<bhkBlendCollisionObject>(node->GetCollisionObject());
					auto bones = DynamicCast<NiNode>(node->GetChildren());
					pelvis = bones[0];
					for (auto& bone : bones)
					{
						//The real pelvis is the start of the ragdoll, if any
						if (bone->GetName().find("Bip") != string::npos)
						{
							other_bones_in_accum.insert(bone);
						}
					}
					if (other_bones_in_accum.size() > 1)
					{
						for (const auto& bone : other_bones_in_accum)
						{
							if (auto collision = DynamicCast<bhkCollisionObject>(bone->GetCollisionObject()))
							{
								if (auto body  = DynamicCast<bhkRigidBody>(collision->GetBody()))
								{
									if (body->GetConstraints().empty())
									{
										pelvis = bone;
										break;
									}
								}
							}
						}
					}
					other_bones_in_accum.erase(pelvis);


					if (NULL != ragdoll)
					{
						if (pelvis->GetCollisionObject() != NULL)
						{
							//just the slaughterfish which doesn't have any rotation, so it's fine
							//NTD
							//int debug = 1;
						}
						else {
							ragdoll->SetTarget(StaticCast<NiAVObject>(pelvis));
							pelvis->SetCollisionObject(StaticCast<NiCollisionObject>(ragdoll));

							auto pelvis_translation = pelvis->GetTranslation();
							auto pelvis_rotaton = pelvis->GetRotation();
							auto pelvis_scale = pelvis->GetScale();
							pelvis_local.setRotation(TOQUAT(pelvis_rotaton.AsQuaternion()));
							pelvis_local.setTranslation(TOVECTOR4(pelvis_translation));

							static float bhkScaleFactorInverse = 0.01428f; // 1 skyrim unit = 0,01428m
							bhkRigidBodyRef rbody = DynamicCast<bhkRigidBody>(ragdoll->GetBody());
							hkTransform nifRbTransform; nifRbTransform.setIdentity();
							nifRbTransform.setTranslation(TOVECTOR4(rbody->GetTranslation() / bhkScaleFactorInverse));
							nifRbTransform.setRotation(TOQUAT(rbody->GetRotation()));

							hkTransform nifRbTransform2; nifRbTransform2.setMul(nifRbTransform, pelvis_local);

							rbody->SetTranslation(TOVECTOR3(nifRbTransform2.getTranslation()) * bhkScaleFactorInverse);
							auto local_r = nifRbTransform2.getRotation();
							::hkQuaternion rr; rr.set(local_r);

							Niflib::hkQuaternion f;
							f.x = rr(0);
							f.y = rr(1);
							f.z = rr(2);
							f.w = rr(3);
							rbody->SetRotation(
								f
							);

							node->SetCollisionObject(NULL);
						}
					}
				}
				auto children = DynamicCast<NiNode>(node->GetChildren());
				for (auto child : children)
				{
					parent_map[child] = node;
				}
			}
	
			auto bodies = DynamicCast<bhkRigidBody>(skeleton_converted_blocks);
			//check constraints
			vector<bhkSerializableRef> constraints;
			set<bhkRigidBodyRef> constrained_bodies;
			for (auto& body : bodies)
			{
				if (body->GetConstraints().size() > 1)
				{
					Log::Warn("Found multiple constrained body. Cutting out additional ones");
					body->SetConstraints({ body->GetConstraints()[0] });
				}
				for (auto& constraint : body->GetConstraints())
				{
					if (bhkConstraintRef cc = DynamicCast<bhkConstraint>(constraint))
					{
						if (cc->GetEntities().size() != 2)
						{
							int debug = 1;
						}
						bhkRigidBodyRef cbody = (bhkRigidBody*)cc->GetEntities()[0];
						if (cbody != body)
						{
							int debug = 1;
						}
						for (auto* entity : cc->GetEntities())
						{

							constrained_bodies.insert((bhkRigidBody*)entity);
						}
					}
					constraints.push_back(constraint);
				}
			}

			if (!bodies.empty() && constraints.size() + 1 != bodies.size())
			{
				if (constrained_bodies.size() == bodies.size())
				{
					Log::Info("Disconnected Ragdoll found!");
				}
			}

			hkTransform accum_t; accum_t.setIdentity();
			std::vector< hkTransform> transforms;
			//collapse transforms on pelvis
			if (NULL != pelvis)
			{
				auto pelvis_translation = pelvis->GetTranslation();
				auto pelvis_rotaton = pelvis->GetRotation();
				auto pelvis_scale = pelvis->GetScale();
				NiNodeRef parent = pelvis;
				while (parent_map.find(parent) != parent_map.end()) {
					auto translation = parent->GetTranslation();
					auto rotaton = parent->GetRotation();
					auto scale = parent->GetScale();
					hkTransform local; // = TOHKTRANSFORM(rotaton, translation);
					local.setRotation(TOQUAT(rotaton.AsQuaternion()));
					local.setTranslation(TOVECTOR4(translation));
					transforms.insert(transforms.begin(),local);
					original_local_pre_pelvis_transforms[parent->GetName()] = local;
					accum_t.setMul(local, accum_t);
					parent->SetTranslation({ 0., 0., 0. });
					parent->SetRotation(Matrix33::IDENTITY);
					parent->SetScale(1.);
					parent = parent_map.at(parent);
				}
				//adjust wrong parenting in land dreugh
				if (!other_bones_in_accum.empty())
				{
					NiNodeRef nonaccum = parent_map.at(pelvis);
					vector<NiAVObjectRef> children;
					auto actual_children = nonaccum->GetChildren();
					for (auto& child : actual_children)
					{
						if (other_bones_in_accum.find(DynamicCast<NiNode>(child)) == other_bones_in_accum.end())
						{
							children.push_back(child);
						}
					}
					nonaccum->SetChildren(children);
					auto pelvis_children = pelvis->GetChildren();
					pelvis_local.setRotation(TOQUAT(pelvis_rotaton.AsQuaternion()));
					pelvis_local.setTranslation(TOVECTOR4(pelvis_translation));
					for (auto& fix_bone : other_bones_in_accum)
					{
						auto translation = fix_bone->GetTranslation();
						auto rotaton = fix_bone->GetRotation();
						auto scale = fix_bone->GetScale();
						hkTransform local; // = TOHKTRANSFORM(rotaton, translation);
						local.setRotation(TOQUAT(rotaton.AsQuaternion()));
						local.setTranslation(TOVECTOR4(translation));
						transforms.insert(transforms.begin(), local);
						local.setMulInverseMul(pelvis_local, local);
						fix_bone->SetTranslation(TOVECTOR3(local.getTranslation()));
						auto local_r = local.getRotation();
						::hkQuaternion rr; rr.set(local_r);
						fix_bone->SetRotation(
							TOQUAT(rr).AsMatrix().Transpose()
						);
						fix_bone->SetScale(1.);
						pelvis_children.push_back(StaticCast<NiAVObject>(fix_bone));
					}
					pelvis->SetChildren(pelvis_children);
				}
				pelvis->SetTranslation(TOVECTOR3(accum_t.getTranslation()));
				auto accum_r = accum_t.getRotation();
				::hkQuaternion rr; rr.set(accum_r);
				pelvis->SetRotation(
					TOQUAT(rr).AsMatrix().Transpose()
				);
				pelvis->SetScale(1.);
			}

			WriteNifTree(creature_output_skeleton.string(), root, info);
			std::get<0>(assets) = skeleton_converted_blocks;
			material_controllers_map.clear();
			material_alpha_controllers_map.clear();
			material_flip_controllers_map.clear();
			deferred_blends.clear();
			material_transform_controllers_map.clear();
		}

		auto* race = races[entry.first];
		fs::path rig_relative_havok_file = "Character Assets" / entry.first.filename().replace_extension(".hkx");
		fs::path rig_relative_file = fs::path(creature_path) / "Character Assets" / entry.first.filename();
		{
			//Create skeleton.hkx
			fs::path creature_output_skeleton = outputFolder / rig_relative_file;
			creature_output_skeleton.replace_extension(".hkx");
			hkRefPtr<hkaSkeleton> hkx_skeleton = nullptr;
			hkRefPtr<hkaSkeleton> hkx_ragdoll = nullptr;
			auto result = Skeleton::Convert
			(
				assets, 
				creature_output_skeleton.string(), 
				hkx_skeleton, 
				hkx_ragdoll, 
				body_parts, 
				renamed_specials
			);
			if (result == Skeleton::conversion_result::hkSkeletonAndRagdoll)
			{
				hasRagdoll = true;
				// R UpperArm o RUpperArm
				//Bip01 Head o Bip01 Skull o Spine0 //Mudcrab only

				ragdollPelvisIndex = 0;
				ragdollRUpperArmIndex = -1;
				ragdollHeadIndex = -1;
				//set indices
				int boneIndex = 0;
				for (const auto& bone : hkx_ragdoll->m_bones)
				{
					std::string s_bone = bone.m_name.cString();
					if (
						s_bone.find("R UpperArm") != string::npos ||
						s_bone.find("RUpperArm") != string::npos ||
						s_bone.find("Bip01 L Calf") != string::npos //skeleton
					)
					{
						ragdollRUpperArmIndex = boneIndex;
					}
					if (
						s_bone.find("Bip01 Spine0") != string::npos ||
						s_bone.find("Bip01 Head") != string::npos ||
						s_bone.find("Bip02 Head") != string::npos || //horse
						s_bone.find("Bip01 Skull") != string::npos || 
						s_bone.find("Bip01 Spine2") != string::npos ||  //skeleton
						s_bone.find("Bip01 Spine") != string::npos //slaughterfish
						)
					{
						ragdollHeadIndex = boneIndex;
					}
					boneIndex++;
				}

				if (ragdollRUpperArmIndex == -1 || ragdollHeadIndex == -1)
				{
					Log::Error("Ragdoll found but unable to find control indices");
				}

			}

			//Use the converted skeleton to convert animations
			fs::path creature_output_animations_folder = outputFolder / creature_path / "Animations";
			ImportKF::ExportAnimations(
				assets, 
				hkx_skeleton, 
				creature_output_animations_folder.string(), 
				root_movements,
				other_bones_in_accum,
				pelvis_local,
				original_local_pre_pelvis_transforms,
				renamed_specials
			);
		}

		//Meshes and actors
		{
			vector<string> slot_names;
			std::map<std::string, Sk::ARMARecord*> armas_records;

			//Convert meshes
			auto& meshes = std::get<1>(assets);
			std::map<std::set<std::string>, std::string> bodySlots;
			for (auto& asset : meshes)
			{
				std::string relative_output_mesh = (fs::path(creature_path) / "Character Assets" / asset.first.filename()).string();
				replacepath(relative_output_mesh, "meshes\\", "");
				fs::path creature_output_mesh = outputFolder / "meshes" / relative_output_mesh;
				fs::create_directories(creature_output_mesh.parent_path());
				NifInfo info;
				info.userVersion = 12;
				info.userVersion2 = 83;
				info.version = Niflib::VER_20_2_0_7;

				NiObjectRef root;
				vector<Ref<NiObject>> converted_blocks;
				convert_blocks(
					asset.second,
					converted_blocks,
					root,
					wrappers,
					info,
					entry.first,
					outputFolder,
					metadata,
					false
				);
				Sk::SKBOD2::BodyParts part = FindBodyPart(
					asset.first, 
					creature_subfolder, 
					converted_blocks, 
					root, 
					body_parts, 
					slot_names, 
					bodySlots
				);
				//Create Armor Addon
				std::string this_mesh_name = asset.first.filename().replace_extension("").string();
				this_mesh_name[0] = toupper(this_mesh_name[0]);
				std::string creature_subfolder_camel = creature_subfolder;
				creature_subfolder_camel[0] = toupper(creature_subfolder_camel[0]);
				std::string arma_EDID = prefix + creature_subfolder_camel + "Skin" + this_mesh_name;
				Sk::ARMARecord* arma_record = (Sk::ARMARecord*)conversionCollection.CreateRecord(conversionPlugin, REV32(ARMA), NULL, (char*)arma_EDID.c_str(), NULL, 0);
				arma_record->EDID = arma_EDID;
				arma_record->RNAM.value = race->formID;
				arma_record->BOD2.value.body_part = part;
				arma_record->MO2.value.MODL = relative_output_mesh;
				armas_records[asset.first.filename().string()] = arma_record;

				WriteNifTree(creature_output_mesh.string(), root, info);
				material_controllers_map.clear();
				material_alpha_controllers_map.clear();
				material_flip_controllers_map.clear();
				deferred_blends.clear();
				material_transform_controllers_map.clear();
			}

			Sk::ARMORecord* most_common_armo = nullptr;
			int most_common_armo_actors = 0;

			//Assemble armors
			{
				auto& this_skeleton_skin = skins[entry.first];
				std::set<string> already_used_edid;
				int edid_index = 0;
				for (auto& skin : this_skeleton_skin)
				{
					set<string> ob_edids;
					for (const auto* edid : skin.second)
					{
						ob_edids.insert(edid->EDID.value);
					}

					//these are flawed, the whole group
					//SEBlackrootLairPatrol01TEMPLATE
					//SEFetidPatrol01TEMPLATE

					if (ob_edids.find("SEBlackrootLairPatrol01TEMPLATE") != ob_edids.end() ||
						ob_edids.find("SEFetidPatrol01TEMPLATE") != ob_edids.end())
					{
						continue;
					}

					std::map<string, int> possible_names;

					for (const auto& name1 : ob_edids)
					{
						for (const auto& name2 : ob_edids)
						{
							auto string = LCS(name1, name2, name1.length(), name2.length());
							if (possible_names.find(string) == possible_names.end())
								possible_names[string] = 0;
							possible_names[string]++;
						}
					}

					std::string skin_name = possible_names.begin()->first;
					std::string skin_lower_name = possible_names.begin()->first;
					std::transform(skin_lower_name.begin(), skin_lower_name.end(), skin_lower_name.begin(), tolower);
					if (skin_lower_name == creature_subfolder && possible_names.size() > 1)
					{
						skin_name = (possible_names.begin()++)->first;
					}
					if (skin_lower_name.find(creature_subfolder) == string::npos)
					{
						std::string prefix = creature_subfolder;
						prefix[0] = toupper(prefix[0]);
						skin_name = prefix + skin_name;
					}

					std::string armo_EDID = prefix + skin_name + "Skin";
					while (!already_used_edid.insert(armo_EDID).second)
					{
						Log::Warn("EDID Collision %s", armo_EDID.c_str());
						armo_EDID = prefix + skin_name + "Skin" + to_string(++edid_index);
					}
					edid_index = 0;
					Sk::ARMORecord* armo_record = (Sk::ARMORecord*)conversionCollection.CreateRecord(conversionPlugin, REV32(ARMO), NULL, (char*)armo_EDID.c_str(), NULL, 0);
					armo_record->EDID = armo_EDID;
					for (const auto& model : skin.first)
					{
						auto* arma_record = armas_records[model];

						armo_record->RNAM.value = race->formID;
						armo_record->BOD2.value.body_part |= arma_record->BOD2.value.body_part;
						armo_record->MODL.value.push_back(arma_record->formID);
					}
					if (skin.second.size() > most_common_armo_actors)
					{
						most_common_armo_actors = skin.second.size();
						most_common_armo = armo_record;
					}
				}
			}

			race->WNAM.value = most_common_armo->formID;
			race->BOD2->body_part = most_common_armo->BOD2.value.body_part;

			//Slots
			Log::Info("Race %s", race->EDID.value);
			race->NAME.clear();
			for (const auto& name : slot_names)
			{
				Log::Info("Slot %s", name.c_str());
				StringRecord n; n = name;
				race->NAME.push_back(n);
			}

			//Create a test actor for this RACE
			std::string npc_test_EDID = std::string(race->EDID.value) + "Test";
			Sk::NPC_Record* race_test_record = (Sk::NPC_Record*)conversionCollection.CreateRecord(conversionPlugin, REV32(NPC_), NULL, (char*)npc_test_EDID.c_str(), NULL, 0);
			race_test_record->EDID = npc_test_EDID;
			race_test_record->RNAM.Load();
			race_test_record->RNAM.value = race->formID;
			race_test_record->WNAM.Load();
			race_test_record->WNAM.value = most_common_armo->formID;
			std::string name = std::string("TES4 ") + creature_tag_upper + " Test";
			race_test_record->FULL.value = new char[name.size() + 1];
			memset(race_test_record->FULL.value, 0, name.size() + 1);
			memcpy(race_test_record->FULL.value, name.c_str(), name.size());

			memset(&race_test_record->AIDT.value, 0, sizeof(race_test_record->AIDT.value));
			race_test_record->AIDT.value.energyLevel = 50;

			race_test_record->ACBS.Load();
			race_test_record->ACBS.value.speedMultiplier = 100.;

			race_test_record->DNAM.Load();
			race_test_record->DNAM.value->gearedUpWeapons = 1;

			race_test_record->NAM5.value = 0x00FF;
			race_test_record->NAM6.value = 1.;
			race_test_record->NAM7.value = 50.0;
			race_test_record->QNAM.Load();
			race_test_record->QNAM.value->red = 0.5;
			race_test_record->QNAM.value->green = 0.5;
			race_test_record->QNAM.value->blue = 0.5;

		}

		//HAVOK
		{
			const std::set<Ob::CREARecord*>& CREAs = actors.at(entry.first);

			std::string creature_name = creature_subfolder;
			creature_name[0] = toupper(creature_name[0]);
		
			AnimationSetAnalyzer analyzer(root_movements);

			map<hkRefPtr<hkbClipGenerator>, fs::path> generators;
			map<int, string> events;

			//Behavior
			fs::path behavior_relative_file = fs::path("Behaviors") / (prefix + creature_name + "Behavior.hkx");
			fs::path behavior_idle_file = creature_path_no_meshes / fs::path("Behaviors") / (prefix + creature_name + "Behavior.hkx");
			fs::path creature_output_behavior = outputFolder / creature_path / behavior_relative_file;
			fs::create_directories(creature_output_behavior.parent_path());

			TES4BehaviorAssembler(
				prefix,
				creature_subfolder,
				creature_output_behavior.string(),
				behavior_idle_file,
				root_movements,
				ragdollPelvisIndex,
				ragdollRUpperArmIndex,
				ragdollHeadIndex,
				conversionCollection,
				conversionPlugin,
				CREAs,
				actions,
				analyzer,
				generators,
				events
			);
			//CreateDummyBehavior(
			//	prefix,
			//	creature_subfolder,
			//	creature_output_behavior.string(),
			//	root_movements,
			//	analyzer,
			//	generators,
			//	events
			//);


			//Character
			
			fs::path character_relative_file = fs::path("Characters") / (prefix + creature_name + "Character.hkx");
			fs::path creature_output_character = outputFolder / creature_path / character_relative_file;
			fs::create_directories(creature_output_character.parent_path());
			
			CreateHavokCharacter(
				creature_name,
				rig_relative_havok_file.string(),
				behavior_relative_file.string(),
				creature_output_character.string(),
				root_movements
			);

			//project
			fs::path project_relative_file = fs::path(creature_path) / (prefix + creature_name + "Project.hkx");
			fs::path creature_output_project = outputFolder / project_relative_file;
			fs::create_directories(creature_output_project.parent_path());
			CreateHavokProject(
				character_relative_file.string(),
				creature_output_project.string()
			);

			Sk::GenericModel behavior_entry; behavior_entry.MODL = (fs::path(creature_path_no_meshes) / (prefix + creature_name + "Project.hkx")).string();
			race->behaviors.maleGraph.value.push_back(behavior_entry);
			race->behaviors.femaleGraph.value.push_back(behavior_entry);

			fs::path nif_skeleton_entry = creature_path_no_meshes / rig_relative_havok_file.replace_extension(".nif");

			race->MNAM_ANAM = nif_skeleton_entry.string();
			race->FNAM_ANAM = nif_skeleton_entry.string();

			//CACHE
			auto entry = dynamic_pointer_cast<CreatureCacheEntry>(cache.findOrCreate(prefix + creature_name + +"Project", true));

			//create movement data first
			std::map<fs::path, int> movements_index_map;
			std::vector<AnimData::ClipMovementData> movements;
			for (auto& entry : root_movements)
			{
				AnimData::ClipMovementData data;
				data.setCacheIndex(movements.size());
				data.setDuration(AnimData::ClipMovementData::tes_float_cache_to_string(entry.second.duration));
				data.setTraslations(entry.second.getClipTranslations());
				data.setRotations(entry.second.getClipRotations());
				movements.push_back(data);
				movements_index_map[entry.first] = data.getCacheIndex();
			}
			//Assemble Clips
			std::list<AnimData::ClipGeneratorBlock> clips;
			for (auto& clip : generators)
			{
				AnimData::ClipGeneratorBlock data;
				data.setName(clip.first->m_name.cString());
				data.setCacheIndex(movements_index_map.at(clip.second));
				float duration = root_movements.at(clip.second).duration;
				data.setPlaybackSpeed(AnimData::ClipMovementData::tes_float_cache_to_string(clip.first->m_playbackSpeed));
				data.setCropStartTime(AnimData::ClipMovementData::tes_float_cache_to_string(clip.first->m_cropStartAmountLocalTime));
				data.setCropEndTime(AnimData::ClipMovementData::tes_float_cache_to_string(clip.first->m_cropEndAmountLocalTime));
				//merge animation and clip events
				std::multimap<float, std::string> triggers;
				auto& animation_events = root_movements.at(clip.second).events;
				for (auto& entry : animation_events)
				{
					triggers.insert({ get<0>(entry), get<1>(entry)});
				}
				if (nullptr != clip.first->m_triggers)
				{

					for (int t = 0; t < clip.first->m_triggers->m_triggers.getSize(); ++t)
					{
						auto& trigger = clip.first->m_triggers->m_triggers[t];
						float time;
						if (trigger.m_relativeToEndOfClip == true)
						{
							time = duration + trigger.m_localTime;
						}
						else {
							time = trigger.m_localTime;
						}
						triggers.insert({ time, events[trigger.m_event.m_id]});
					}
				}
				//load triggers inside anim
				AnimData::StringListBlock eventList; eventList.reserve(triggers.size());
				for (const auto& trigger : triggers)
				{
					eventList.append(trigger.second + ":" + AnimData::ClipMovementData::tes_float_cache_to_string(trigger.first));
				}
				data.setEvents(eventList);
				clips.push_back(data);
			}
			//Files
			AnimData::StringListBlock project_hkx_files;
			project_hkx_files.append(behavior_relative_file.string());
			project_hkx_files.append(character_relative_file.string());
			project_hkx_files.append(rig_relative_havok_file.replace_extension(".hkx").string());

			//Sets
			AnimData::ProjectAttackBlock block;
			AnimData::ClipFilesCRC32Block crc32Data;
			for (auto& entry : root_movements)
			{
				auto full_path = fs::path(creature_path) / entry.first;
				auto sanitized_name = full_path.filename().replace_extension("").string();
				transform(sanitized_name.begin(), sanitized_name.end(), sanitized_name.begin(), ::tolower);
				std::string sanitized_name_crc32 = crc_32(sanitized_name);

				auto meshes_path = full_path.parent_path().string();
				transform(meshes_path.begin(), meshes_path.end(), meshes_path.begin(), ::tolower);
				std::string meshes_path_crc32 = crc_32(meshes_path);
				crc32Data.append(meshes_path_crc32, sanitized_name_crc32);
			}
			block.setCrc32Data(crc32Data);

			//Assemble Dummy Fullbody
			entry->block.setHasProjectFiles(true);
			entry->block.setProjectFiles(project_hkx_files);
			entry->block.setClips(clips);
			entry->block.setHasAnimationCache(true);
			entry->movements.setMovementData(movements);
			entry->sets.putProjectAttack("FullBody.txt", block);
		}
	}

	cache.save(animdata, animsetdata);
}

void findActions(Collection& conversionCollection, std::map<std::string, Sk::AACTRecord*>& actions)
{
	for (auto record_it = conversionCollection.FormID_ModFile_Record.begin(); record_it != conversionCollection.FormID_ModFile_Record.end(); record_it++)
	{
		Record* record = record_it->second;
		if (record->GetType() == REV32(AACT)) {
			Sk::AACTRecord* action = dynamic_cast<Sk::AACTRecord*>(record);
			if (NULL != action)
			{
				actions[action->EDID.value] = action;
			}
		}
	}
}

bool BeginConversion(string importPath, string exportPath) 
{
	char fullName[MAX_PATH], exeName[MAX_PATH];
	GetModuleFileName(NULL, fullName, MAX_PATH);
	_splitpath(fullName, NULL, NULL, exeName, NULL);

	NifInfo info;
	vector<fs::path> nifs;
	vector<fs::path> spts;
	std::map<fs::path, std::set<fs::path>> skeletons;
	std::map<fs::path, std::set<Ob::CREARecord*>> actors;
	std::map<fs::path, Sk::RACERecord*>	races;
	map<fs::path, map<set<string>, set< Ob::CREARecord*>>> skins;
	set<set<string>> sequences_groups;
	HKXWrapperCollection wrappers;
	std::vector<std::pair<std::string, Vector3>> metadata;

	const Games::GamesPathMapT& installations = games.getGames();
	auto oblivionData = games.data(Games::TES4).string();
	if (!fs::exists(oblivionData))
	{
#ifndef _DEBUG
		Log::Warn("Unable to find Oblivion Data directory, there WILL be errors and creatures won't be converted!");
		return false;
#else 
		oblivionData = "D:\\The Elder Scrolls IV Oblivion\\Data";
#endif

	}


	char* argvv[4];
	argvv[0] = new char();
	argvv[1] = new char();
	argvv[2] = new char();
	argvv[3] = new char();
	logger.init(4, argvv);

	char* conversionPluginName = "SkyblivionCreatures.esp";
	exportPath = "D:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\Data";

	Collection conversionCollection = Collection((char* const)exportPath.c_str(), 3);
	ModFlags masterSkFlags = ModFlags(0xA);
	//ModFile* skyrim_esm = conversionCollection.AddMod("Skyrim.esm", masterSkFlags);
	masterSkFlags.IsCreateNew = true;
	masterSkFlags.IsSaveable = true;
	ModFile* conversionPlugin = conversionCollection.AddMod(conversionPluginName, masterSkFlags);
	conversionPlugin->TES4.MAST.push_back("Skyrim.esm");
	conversionCollection.Load();

	Log::Info("Found Oblivion installation: %s", oblivionData.c_str());
	//Load esms;
	Collection oblivionCollection = Collection((char* const)(oblivionData.c_str()), 0);
	ModFlags masterFlags = ModFlags(0xA);
	ModFile* esm = oblivionCollection.AddMod("Oblivion.esm", masterFlags);
	ModFile* SI = oblivionCollection.AddMod("DLCShiveringIsles.esp", masterFlags);
	ModFile* Knights = oblivionCollection.AddMod("Knights.esp", masterFlags);
	oblivionCollection.Load();

	std::string prefix = "TES4";

	std::map<std::string, Sk::AACTRecord*> actions;
	findActions(conversionCollection, actions);
	findCreatures(skeletons, actors, skins, oblivionCollection, oblivionData);
	ConvertCreatures(prefix, actors, races, skins, oblivionCollection, conversionCollection, conversionPlugin);
	ConvertAssets
	(
		prefix,
		oblivionData, 
		skeletons,
		races,
		actors,
		skins,
		exportPath, 
		wrappers, 
		metadata, 
		conversionCollection, 
		conversionPlugin,
		actions
	);

	return true;

	ModSaveFlags skSaveFlags = ModSaveFlags(2);
	skSaveFlags.IsCleanMasters = true;
	conversionCollection.SaveMod(conversionPlugin, skSaveFlags, conversionPluginName);

	return true;

	if (fs::exists(importPath) && fs::is_directory(importPath))
		findFiles(importPath, ".nif", nifs);

	//spt
#ifdef HAVE_SPEEDTREE
	if (fs::exists(importPath) && fs::is_directory(importPath))
		findFiles(importPath, ".spt", nifs);
#endif

	if (fs::exists(importPath) && fs::is_directory(importPath))
		findFiles(importPath, ".spt", spts);


	fs::path out_path;
 	if (nifs.empty() && spts.empty()) {
		Log::Info("No NIFs found.. trying BSAs");
		const Games::GamesPathMapT& installations = games.getGames();
		games.loadBsas(Games::TES4);
		exportPath = games.data(Games::TES5SE).string();
		for (const auto& bsa_file : games.bsa_files()) {
			//std::cout << "Checking: " << bsa.filename() << std::endl;
			//BSAFile bsa_file(bsa);

			for (const auto& nif : bsa_file.assets(".*\.nif")) {
				Log::Info("Current File: %s", nif.c_str());

				std::string nif_path = nif;
				if (nif.find("meshes") == 0) {
					nif_path = "meshes\\tes4" + nif_path.substr(std::string("meshes").size(), nif_path.size());
				}

				out_path = games.data(Games::TES5SE) / nif_path;

				if (nif.find("meshes\\landscape\\lod") != string::npos) {
					Log::Warn("Ignored LOD file: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\marker_") != string::npos) {
					Log::Warn("Ignored marker file: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\minotaurold") != string::npos) {
					Log::Warn("Ignored malformed file: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\sky\\") != string::npos) {
					Log::Warn("Ignored obsolete sky nifs: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\menus\\") != string::npos) {
					Log::Warn("Ignored obsolete menu nifs: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\creatures\\") != string::npos) {
					Log::Warn("temporarily ignoring: %s", nif.c_str());
					continue;
				}
				if (nif.find("\\hair\\") != string::npos) {
					Log::Warn("temporarily ignoring: %s", nif.c_str());
					continue;
				}
				size_t size = -1;
				const uint8_t* data = bsa_file.extract(nif, size);

				std::string sdata((char*)data, size);
				std::istringstream iss(sdata);

				NiObjectRef root;
				vector<NiObjectRef> blocks = ReadNifList(iss, &info);
				vector<NiObjectRef> new_blocks;
				convert_blocks(
					blocks,
					new_blocks,
					root,
					wrappers,
					info,
					nif,
					exportPath,
					metadata,
					true
				);

				//out_path = nif_out / nif;
				fs::create_directories(out_path.parent_path());
				WriteNifTree(out_path.string(), root, info);
				material_controllers_map.clear();
				material_alpha_controllers_map.clear();
				material_flip_controllers_map.clear();
				deferred_blends.clear();
				material_transform_controllers_map.clear();
				delete data;
			}
		}
	
		//Log::Info("Scanning TES4 Override");
		////Convert overrides for uesp patch
		//importPath = (games.data(Games::TES4) / "meshes").string();
		//if (fs::exists(importPath) && fs::is_directory(importPath))
		//	findFiles(importPath, ".nif", nifs);

		//for (size_t i = 0; i < nifs.size(); i++) {
		//	Log::Info("Current File: %s", nifs[i].string().c_str());

		//	if (nifs[i].string().find("lod") != string::npos || nifs[i].string().find("LOD") != string::npos)
		//		continue;

		//	NiObjectRef root;
		//	vector<NiObjectRef> blocks = ReadNifList(nifs[i].string().c_str(), &info);
		//	vector<NiObjectRef> new_blocks;
		//	convert_blocks(
		//		blocks,
		//		new_blocks,
		//		root,
		//		wrappers,
		//		info,
		//		nifs[i],
		//		exportPath,
		//		metadata
		//	);

		//	size_t offset = nifs[i].parent_path().string().find("meshes", 0);
		//	size_t end = nifs[i].parent_path().string().length();
		//	std::string newPath = exportPath;
		//	if (offset < end) {
		//		std::string relative_path = nifs[i].parent_path().string().substr(offset, end);
		//		relative_path.insert(6, "\\tes4");
		//		newPath += std::string("\\") + relative_path;
		//	}
		//	out_path = newPath / nifs[i].filename();
		//	fs::create_directories(out_path.parent_path());
		//	WriteNifTree(out_path.string(), root, info);
		//	material_controllers_map.clear();
		//	material_alpha_controllers_map.clear();
		//	material_flip_controllers_map.clear();
		//	deferred_blends.clear();
		//	material_transform_controllers_map.clear();
		//}
	
	}
	else {
		//Load metadata
		map<string, string> tree_metadata;
		ifstream  exported_metadata("D:\\gitroot\\ck-cmd\\xedit\\Skyblivion_metadata\\trees.txt");
		if (exported_metadata.is_open() && exported_metadata.good())
		{
			std::string line;
			while (std::getline(exported_metadata, line)) {
				size_t index = line.find(";");
				if (index != string::npos) {
					tree_metadata.insert(
						{
							line.substr(0, index),
							line.substr(index + 1, line.size() - index - 1)
						}
					);
				}
			}
			exported_metadata.close();
		}


		for (size_t i = 0; i < spts.size(); i++) {
			sptconvert(spts[i], exportPath, tree_metadata);
		}

		for (size_t i = 0; i < nifs.size(); i++) {
			Log::Info("Current File: %s", nifs[i].string().c_str());
#ifdef HAVE_SPEEDTREE
			if (nifs[i].string().find("spt") != string::npos)
			{
				SpeedTree::CTree spt_tree;
				bool isauth = spt_tree.IsAuthorized();
				bool result = spt_tree.LoadTree(nifs[i].string().c_str());
				if (!result)
					Log::Error("Failed to load [%s]: %s\n", nifs[i].string().c_str(), SpeedTree::CCore::GetError());
				continue;
			}
#endif

			if (nifs[i].string().find("lod") != string::npos)
				continue;

			NiObjectRef root;
			vector<NiObjectRef> blocks = ReadNifList(nifs[i].string().c_str(), &info);
			vector<NiObjectRef> new_blocks;
			convert_blocks(
				blocks,
				new_blocks,
				root,
				wrappers,
				info,
				nifs[i],
				exportPath,
				metadata,
				true
			);

			size_t offset = nifs[i].parent_path().string().find("meshes", 0);
			size_t end = nifs[i].parent_path().string().length();
			std::string newPath = exportPath;
			if (offset < end)
				newPath += nifs[i].parent_path().string().substr(offset, end);
			out_path = newPath / nifs[i].filename();
			fs::create_directories(out_path.parent_path());
			WriteNifTree(out_path.string(), root, info);
			material_controllers_map.clear();
			material_alpha_controllers_map.clear();
			material_flip_controllers_map.clear();
			deferred_blends.clear();
			material_transform_controllers_map.clear();
		}
	}
	if (metadata.size())
	{
		Log::Info("Writing Metadata...");
		std::ofstream metadata_out;
		metadata_out.open((fs::path(exportPath) / "skyblivion.metadata").string(), ios::out | ios::trunc);
		for (const auto& data : metadata) {
			metadata_out << "[" << data.first << "]" << endl;
			metadata_out << "TRANSLATE_VECTOR" << " " <<
				to_string(data.second.x) << " " <<
				to_string(data.second.y) << " " <<
				to_string(data.second.z) << " " << endl;
			metadata_out << endl;
		}
		metadata_out.close();
	}
	Log::Info("Done");
	return true;
}

static void HelpString(hkxcmd::HelpType type) {
	switch (type)
	{
	case hkxcmd::htShort: Log::Info("About - Help about this program."); break;
	case hkxcmd::htLong: {
		char fullName[MAX_PATH], exeName[MAX_PATH];
		GetModuleFileName(NULL, fullName, MAX_PATH);
		_splitpath(fullName, NULL, NULL, exeName, NULL);
		Log::Info("Usage: %s about", exeName);
		Log::Info("  Prints additional information about this program.");
	}
						 break;
	}
}

//Havok initialization

static void HK_CALL errorReport(const char* msg, void*)
{
	Log::Error("%s", msg);
}

static void HK_CALL debugReport(const char* msg, void* userContext)
{
	Log::Debug("%s", msg);
}


static hkThreadMemory* threadMemory = NULL;
static char* stackBuffer = NULL;
static void InitializeHavok()
{
	// Initialize the base system including our memory system
	hkMemoryRouter*		pMemoryRouter(hkMemoryInitUtil::initDefault(hkMallocAllocator::m_defaultMallocAllocator, hkMemorySystem::FrameInfo(5000000)));
	hkBaseSystem::init(pMemoryRouter, errorReport);
	LoadDefaultRegistry();
}

static void CloseHavok()
{
	hkBaseSystem::quit();
	hkMemoryInitUtil::quit();
}

//static bool ExecuteCmd(hkxcmdLine &cmdLine) {
//	InitializeHavok();
//	BeginConversion();
//	CloseHavok();
//	return true;
//}
//
//REGISTER_COMMAND(ConvertNif, HelpString, ExecuteCmd);
