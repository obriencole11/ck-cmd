#include <commands/RetargetCreature.h>
#include "stdafx.h"
#undef max
#undef min

#include <core/hkxcmd.h>
#include <core/hkxutils.h>
#include <core/AnimationCache.h>
#include <core/HKXWrangler.h>

#include <core/MathHelper.h>

#include <hkbBehaviorReferenceGenerator_0.h>
#include <hkbClipGenerator_2.h>
#include <hkbProjectData_2.h>
#include <hkbCharacterData_7.h>
#include <hkbBehaviorGraph_1.h>
#include <hkbExpressionDataArray_0.h>
#include <hkbExpressionDataArray_0.h>
#include <Animation/Animation/hkaAnimationContainer.h>
#include <BSSynchronizedClipGenerator_1.h>

#include <algorithm>
#include <vector>

#include <Skyrim/TES5File.h>
#include <Collection.h>
#include <ModFile.h>

struct ci_less
{
	// case-independent (ci) compare_less binary function
	struct nocase_compare
	{
		bool operator() (const unsigned char& c1, const unsigned char& c2) const {
			return tolower(c1) < tolower(c2);
		}
	};
	bool operator() (const std::string & s1, const std::string & s2) const {
		return std::lexicographical_compare
		(s1.begin(), s1.end(),   // source range
			s2.begin(), s2.end(),   // dest range
			nocase_compare());  // comparison
	}
};

string replace_all(const string& source, const string& pattern, const string& new_pattern)
{
	string result = source;
	string::size_type n = 0;
	while ((n = result.find(pattern, n)) != string::npos)
	{
		result.replace(n, new_pattern.size(), new_pattern);
		n += new_pattern.size();
	}
	return result;
}

template<typename T>
typename T::size_type GeneralizedLevensteinDistance(const T &source,
	const T &target,
	typename T::size_type insert_cost = 1,
	typename T::size_type delete_cost = 1,
	typename T::size_type replace_cost = 1) {
	if (source.size() > target.size()) {
		return GeneralizedLevensteinDistance(target, source, delete_cost, insert_cost, replace_cost);
	}

	using TSizeType = typename T::size_type;
	const TSizeType min_size = source.size(), max_size = target.size();
	std::vector<TSizeType> lev_dist(min_size + 1);

	lev_dist[0] = 0;
	for (TSizeType i = 1; i <= min_size; ++i) {
		lev_dist[i] = lev_dist[i - 1] + delete_cost;
	}

	for (TSizeType j = 1; j <= max_size; ++j) {
		TSizeType previous_diagonal = lev_dist[0], previous_diagonal_save;
		lev_dist[0] += insert_cost;

		for (TSizeType i = 1; i <= min_size; ++i) {
			previous_diagonal_save = lev_dist[i];
			if (source[i - 1] == target[j - 1]) {
				lev_dist[i] = previous_diagonal;
			}
			else {
				lev_dist[i] = std::min(std::min(lev_dist[i - 1] + delete_cost, lev_dist[i] + insert_cost), previous_diagonal + replace_cost);
			}
			previous_diagonal = previous_diagonal_save;
		}
	}

	return lev_dist[min_size];
}


using namespace ckcmd::info;
using namespace ckcmd::BSA;
using namespace ckcmd::HKX;

static void InitializeHavok();
static void CloseHavok();

REGISTER_COMMAND_CPP(RetargetCreatureCmd)

RetargetCreatureCmd::RetargetCreatureCmd()
{
}

RetargetCreatureCmd::~RetargetCreatureCmd()
{
}

string RetargetCreatureCmd::GetName() const
{
	return "RetargetCreature";
}

string RetargetCreatureCmd::GetHelp() const
{
	string name = GetName();
	transform(name.begin(), name.end(), name.begin(), ::tolower);

	// Usage: ck-cmd games
	string usage = "Usage: " + ExeCommandList::GetExeName() + " " + name + " <source_havok_project_cache> <source_havok_project_folder> <target_name>\r\n";

	//will need to check this help in console/
	const char help[] =
	R"(Creates a new havok project (to be used in a separate CK racial type) starting from an existing one.
		
	  Arguments:
	  <source_havok_project_cache> one of the txt file under meshes/animationdata folder for the source project
	  <source_havok_project_folder> the path under meshes/actor for the project to be retargeted. MUST match the cache file from first argument
	  <target_name> target project name

	  arguments are mandatory)";

	return usage + help;
}

string RetargetCreatureCmd::GetHelpShort() const
{
	return "Checks for installed bethesda games and their install path";
}

struct compare {
	bool operator()(const std::string& first, const std::string& second) {
		return first.size() < second.size();
	}
};

template<typename hkType>
hkRefPtr<hkType> load_havok_file(const fs::path& path, HKXWrapper& wrapper)
{
	Games& games = Games::Instance();
	Games::Game tes5 = Games::TES5;

	if (fs::exists(games.data(tes5) / path)) {
		fs::path file_path = games.data(tes5) / path;
		return wrapper.load<hkType>(file_path);
	}
	for (const auto& bsa_path : games.bsas(tes5)) {
		BSAFile bsa_file(bsa_path);
		bool found = bsa_file.find(path.string());
		if (found)
		{
			//get the shortest path
			size_t size = -1;
			const uint8_t* data = bsa_file.extract(path.string(), size);
			hkRefPtr<hkType> hkroot = wrapper.load<hkType>(data, size);
			delete data;
			return hkroot;
		}
	}
	throw runtime_error("File not found:" + path.string());
	return HK_NULL;
}

template<typename hkType>
hkRefPtr<hkType> load_havok_file(const fs::path& path, hkArray<hkVariant>& objects, HKXWrapper& wrapper)
{
	Games& games = Games::Instance();
	Games::Game tes5 = Games::TES5;

	if (fs::exists(games.data(tes5) / path)) {
		fs::path file_path = games.data(tes5) / path;
		return wrapper.load<hkType>(file_path, objects);
	}
	for (const auto& bsa_path : games.bsas(tes5)) {
		BSAFile bsa_file(bsa_path);
		bool found = bsa_file.find(path.string());
		if (found)
		{
			//get the shortest path
			size_t size = -1;
			const uint8_t* data = bsa_file.extract(path.string(), size);
			hkRefPtr<hkType> hkroot = wrapper.load<hkType>(data, size, objects);
			delete data;
			return hkroot;
		}
	}
	throw runtime_error("File not found:" + path.string());
	return HK_NULL;
}

HKXWrapper wrapper;

struct clip_compare {
	bool operator() (const hkRefPtr<hkbClipGenerator>& lhs, const hkRefPtr<hkbClipGenerator>& rhs) const {
		return string(lhs->m_name) < string(rhs->m_name);
	}
};


void extract_clips(BSAFile& bsa_file, const string& behavior_path, const fs::path& root,  set<hkRefPtr<hkbClipGenerator>, clip_compare>& clips)
{
	string behavior_full_path = fs::canonical(root / behavior_path).string();
	size_t offset = behavior_full_path.find("meshes", 0);
	size_t end = behavior_full_path.length();
	if (offset < end)
		behavior_full_path = behavior_full_path.substr(offset, end);
	if (bsa_file.find(behavior_full_path))
	{
		hkRootLevelContainer* broot = NULL;
		string bdata = bsa_file.extract(behavior_full_path);
		hkArray<hkVariant> objects;
		hkRefPtr<hkbBehaviorGraph> bhkroot = wrapper.load<hkbBehaviorGraph>((const uint8_t *)bdata.c_str(), bdata.size(), broot, objects);
		Log::Info("Graph: %s", bhkroot->m_name);
		for (const auto& object : objects)
		{
			if (hkbClipGenerator::staticClass().getSignature() == object.m_class->getSignature())
			{
				hkRefPtr<hkbClipGenerator> clip = (hkbClipGenerator*)object.m_object;
				Log::Info("Clip: %s, animation: %s", clip->m_name, clip->m_animationName);
				clips.insert(clip);
				clip = broot->findObject<hkbClipGenerator>(clip);
			}
			if (hkbBehaviorReferenceGenerator::staticClass().getSignature() == object.m_class->getSignature())
			{
				hkRefPtr<hkbBehaviorReferenceGenerator> sub_graph = (hkbBehaviorReferenceGenerator*)object.m_object;
				Log::Info("Sub Graph: %s", sub_graph->m_behaviorName);
				extract_clips(
					bsa_file,
					string(sub_graph->m_behaviorName),
					root,
					clips
				);
			}
		}
	}
}


bool RetargetCreatureCmd::InternalRunCommand(map<string, docopt::value> parsedArgs)
{
	string source_havok_project_cache = "", source_havok_project_folder = "", output_havok_project_name = "";
	if (parsedArgs["<source_havok_project_cache>"].isString())
		source_havok_project_cache = parsedArgs["<source_havok_project_cache>"].asString();
	if (parsedArgs["<source_havok_project_folder>"].isString())
		source_havok_project_folder = parsedArgs["<source_havok_project_folder>"].asString();
	if (parsedArgs["<target_name>"].isString())
		output_havok_project_name = parsedArgs["<target_name>"].asString();

	if (!fs::exists(source_havok_project_cache) || !fs::is_regular_file(source_havok_project_cache)) {
		Log::Error("Invalid cache skeleton file: %s", source_havok_project_cache.c_str());
		return false;
	}

	if (!fs::exists(source_havok_project_folder) || !fs::is_directory(source_havok_project_folder)) {
		Log::Error("Invalid havok project path: %s", source_havok_project_folder.c_str());
		return false;
	}

	if (output_havok_project_name.empty()) {
		Log::Error("Invalid target project name: %s", output_havok_project_name.c_str());
		return false;
	}

	fs::path animDataPath = fs::path(source_havok_project_cache).parent_path().parent_path() / "animationdatasinglefile.txt";
	fs::path animDataSetPath = fs::path(source_havok_project_cache).parent_path().parent_path() / "animationsetdatasinglefile.txt";
	Log::Info("Loading cache, loading %s and %s", animDataPath.string().c_str(), animDataSetPath.string().c_str());
	AnimationCache cache(animDataPath, animDataSetPath);
	Log::Info("Loaded. Creating target dir: %s", output_havok_project_name.c_str());
	fs::path output = fs::path(".") / output_havok_project_name;
	fs::create_directories(output);
	vector<fs::path> projects;
	find_files_non_recursive(source_havok_project_folder, ".hkx", projects);

	fs::path project_file;
	string cache_name = fs::path(source_havok_project_cache).filename().replace_extension("").string();
	int distance = std::numeric_limits<int>::max();
	for (const auto& file : projects) {
		int actual_distance = GeneralizedLevensteinDistance
		(
			file.filename().replace_extension("").string(),
			cache_name
		);
		if (actual_distance < distance) {
			project_file = file;
			distance = actual_distance;
		}
	}

	Log::Info("Project copied. Retargeting project: %s", project_file.string().c_str());
	InitializeHavok();
	hkRootLevelContainer* root = NULL;
	hkbProjectData* hkroot = wrapper.load<hkbProjectData>(project_file, root);
	if (hkroot == NULL)
	{
		Log::Error("Unable to load havok project file: %s", project_file.string().c_str());
		return false;
	}
	std::map<string, string, ci_less> retarget_map;
	hkbProjectStringData* sdata = hkroot->m_stringData;
	fs::path new_char_name;
	fs::path old_char_file = source_havok_project_folder / fs::path(sdata->m_characterFilenames[0].cString());
	if (sdata->m_characterFilenames.getSize() <= 0)
	{
		Log::Error("Havok project file contains no character file, exiting");
		return false;
	}
	new_char_name = fs::path(sdata->m_characterFilenames[0].cString()).parent_path() / (output_havok_project_name + ".hkx");
	retarget_map[string(sdata->m_characterFilenames[0].cString())] = new_char_name.string();
	Log::Info("Project's character: %s, new name: %s", sdata->m_characterFilenames[0], new_char_name.string().c_str());
	sdata->m_characterFilenames[0] = new_char_name.string().c_str();
	wrapper.write(root, output / (output_havok_project_name + "project.hkx"));

	Log::Info("Project file retargeted. loading %s", old_char_file.string().c_str());
	root = NULL;
	hkbCharacterData* hk_ch_root = wrapper.load<hkbCharacterData>(old_char_file, root);
	if (hk_ch_root == NULL)
	{
		Log::Error("Unable to load havok character file: %s", old_char_file.string().c_str());
		return false;
	}
	string old_name = hk_ch_root->m_stringData->m_name;
	hk_ch_root->m_stringData->m_name = output_havok_project_name.c_str();

	string old_behavior_name = hk_ch_root->m_stringData->m_behaviorFilename.cString();
	fs::path new_behavior_relative_path = fs::path(old_behavior_name).parent_path() / fs::path(replace_all(old_behavior_name, old_name, output_havok_project_name)).filename();
	retarget_map[old_behavior_name] = new_behavior_relative_path.string();
	fs::path old_behavior_file = fs::path(source_havok_project_folder) / old_behavior_name;
	hk_ch_root->m_stringData->m_behaviorFilename = new_behavior_relative_path.string().c_str();
	fs::path old_rig_path = fs::path(source_havok_project_folder) / string(hk_ch_root->m_stringData->m_rigName);
	//Just upgrade
	hkRootLevelContainer* rig_root = NULL;
	if (NULL == wrapper.load<hkaAnimationContainer>(old_rig_path, rig_root))
	{
		Log::Error("Unable to load skeleton file: %s", old_rig_path.string().c_str());
		return false;
	}
	else {
		fs::create_directories(fs::path(fs::path(output) / string(hk_ch_root->m_stringData->m_rigName)).parent_path());
		wrapper.write(rig_root, output / string(hk_ch_root->m_stringData->m_rigName));
	}

	//Check paired
	for (int i = 0; i < hk_ch_root->m_stringData->m_animationNames.getSize(); i++)
	{
		string name = hk_ch_root->m_stringData->m_animationNames[i];
		if (name.find("..") == 0) {
			Log::Warn("found an animation outside of the creature folder: %s", name.c_str());
			//these animations are not in the current copied folder
			//do they contain the old name?
			if (name.find(old_name) != string::npos) {
				string new_name = name;
				string::size_type n = 0;
				while ((n = new_name.find(old_name, n)) != string::npos)
				{
					new_name.replace(n, old_name.size(), output_havok_project_name);
					n += output_havok_project_name.size();
				}
				Log::Info("Will substitute %s references with %s", name.c_str(), new_name.c_str());
				hk_ch_root->m_stringData->m_animationNames[i] = new_name.c_str();
				retarget_map[name] = new_name;
				if (fs::exists(fs::path(source_havok_project_folder) / name) &&
					fs::is_regular_file(fs::path(source_havok_project_folder) / name))
				{
					Log::Info("Found %s", (fs::path(source_havok_project_folder) / name).string().c_str());					
					hkRootLevelContainer* root = NULL;
					hkRefPtr<hkaAnimationContainer> hkroot = wrapper.load<hkaAnimationContainer>(fs::path(source_havok_project_folder) / name, root);
					if (hkroot == NULL)
					{
						Log::Error("Unable to load havok project file: %s", project_file.string().c_str());
					}
					else {
						string out = new_name;
						transform(out.begin(), out.end(), out.begin(), ::tolower);
						fs::create_directories(fs::path(new_name).parent_path());
						wrapper.write(root, out);
					}
					
					Log::Info("Copied (and upgraded) into %s", fs::canonical(new_name).string().c_str());
				}
				else {
					Log::Error("COULD NOT FIND %s, copy it and rename manually (and upgrade if LE format)", (fs::path(source_havok_project_folder) / name).string().c_str());
				}
			}
		}
		else {
			//Just upgrade
			hkRootLevelContainer* root = NULL;
			hkRefPtr<hkaAnimationContainer> hkroot = wrapper.load<hkaAnimationContainer>(fs::path(source_havok_project_folder) / name, root);
			if (hkroot == NULL)
			{
				Log::Error("Unable to load animation file file: %s", (fs::path(source_havok_project_folder) / name).string().c_str());
			}
			else {
				string out = (output / name).string();
				transform(out.begin(), out.end(), out.begin(), ::tolower);
				fs::create_directories(fs::path(out).parent_path());
				wrapper.write(root, out);
			}
		}
		// Calculate relative path for crc
		fs::path abs = fs::canonical(fs::path(source_havok_project_folder) / name).parent_path();
		fs::path base = source_havok_project_folder;
		while (base.has_parent_path())
		{
			base = base.parent_path();
			if (base.filename().string() == "meshes")
				break;
		}
		if (base.string().find("meshes") == string::npos) {
			Log::Error("Cannot find meshes folder into the source_havok_project_folder! Please use the original extracted bsa path fully!");
			return false;
		}
		fs::path relative = "meshes" / relative_to(abs, base);
		string to_crc = relative.string();
		transform(to_crc.begin(), to_crc.end(), to_crc.begin(), ::tolower);
		string::size_type n = 0;
		while ((n = to_crc.find('/', n)) != string::npos)
		{
			to_crc.replace(n, 1, "\\");
			n += 1;
		}
		long long crc = stoll(HkCRC::compute(to_crc),NULL,16);
		string crc_str = to_string(crc);
		//new
		string lower_output = output_havok_project_name;
		transform(lower_output.begin(), lower_output.end(), lower_output.begin(), ::tolower);
		string lower_project = fs::path(source_havok_project_folder).filename().string();
		transform(lower_project.begin(), lower_project.end(), lower_project.begin(), ::tolower);
		string new_to_crc = to_crc;
		n = 0;
		while ((n = new_to_crc.find(lower_project, n)) != string::npos)
		{
			new_to_crc.replace(n, lower_project.size(), lower_output);
			n += lower_output.size();
		}
		long long new_crc = stoll(HkCRC::compute(new_to_crc), NULL, 16);
		string new_crc_str = to_string(new_crc);
		retarget_map[crc_str] = new_crc_str;

	}
	fs::create_directories(fs::path(fs::path(output) / new_char_name).parent_path());
	wrapper.write(root, output / new_char_name);
	fs::path old_behavior_dir = source_havok_project_folder / fs::path(old_behavior_name).parent_path();
	vector<fs::path> behaviors;
	set<string> retarget_SOUN;
	set<string> retarget_MOVT;
	find_files_non_recursive(old_behavior_dir, ".hkx", behaviors);
	for (const auto& file : behaviors) {
		hkArray<hkVariant> objects;
		hkRootLevelContainer* root = NULL;
		hkRefPtr<hkbBehaviorGraph> bhkroot = wrapper.load<hkbBehaviorGraph>(file, root, objects);
		Log::Info("Graph: %s", bhkroot->m_name);
		Log::Info("Retargeting Events:");
		for (int i = 0; i < bhkroot->m_data->m_stringData->m_eventNames.getSize(); i++)
		{
			string event_name = bhkroot->m_data->m_stringData->m_eventNames[i];
			if (event_name.find(old_name) != string::npos)
			{
				string new_name = event_name;
				string::size_type n = 0;
				while ((n = new_name.find(old_name, n)) != string::npos)
				{
					new_name.replace(n, old_name.size(), output_havok_project_name);
					n += output_havok_project_name.size();
				}
				Log::Info("Will substitute %s references with %s", event_name.c_str(), new_name.c_str());
				if (event_name.find("SoundPlay.") == 0)
					retarget_SOUN.insert(event_name.substr(sizeof("SoundPlay.")-1, event_name.size()));
				retarget_map[string(bhkroot->m_data->m_stringData->m_eventNames[i])] = new_name;
				bhkroot->m_data->m_stringData->m_eventNames[i] = new_name.c_str();
			}
		}
		Log::Info("Retargeting Variables:");
		for (int i = 0; i < bhkroot->m_data->m_stringData->m_variableNames.getSize(); i++)
		{
			string variable_name = bhkroot->m_data->m_stringData->m_variableNames[i];
			if (variable_name.find(old_name) != string::npos)
			{
				string new_name = variable_name;
				string::size_type n = 0;
				while ((n = new_name.find(old_name, n)) != string::npos)
				{
					new_name.replace(n, old_name.size(), output_havok_project_name);
					n += output_havok_project_name.size();
				}
				Log::Info("Will substitute %s references with %s", variable_name.c_str(), new_name.c_str());
				if (variable_name.find("iState_") == 0) {
					retarget_MOVT.insert(variable_name.substr(sizeof("iState_")-1, variable_name.size()));
				}

				retarget_map[string(bhkroot->m_data->m_stringData->m_variableNames[i])] = new_name;
				bhkroot->m_data->m_stringData->m_variableNames[i] = new_name.c_str();				
			}
		}
		for (const auto& object : objects)
		{
			if (hkbClipGenerator::staticClass().getSignature() == object.m_class->getSignature())
			{
				hkRefPtr<hkbClipGenerator> clip = (hkbClipGenerator*)object.m_object;
				
				auto it = retarget_map.find(string(clip->m_animationName));
				if (it != retarget_map.end())
				{
					Log::Info("Retargeting clip: %s, animation: %s to %s", clip->m_name, clip->m_animationName, it->second.c_str());
					clip->m_animationName = it->second.c_str();
				}
				string clip_name = clip->m_name;
				string::size_type n = 0;
				while ((n = clip_name.find(old_name, n)) != string::npos)
				{
					clip_name.replace(n, old_name.size(), output_havok_project_name);
					n += output_havok_project_name.size();
				}
				clip->m_name = clip_name.c_str();
			}
			if (BSSynchronizedClipGenerator::staticClass().getSignature() == object.m_class->getSignature())
			{
				hkRefPtr<BSSynchronizedClipGenerator> clip = (BSSynchronizedClipGenerator*)object.m_object;

				string clip_name = clip->m_name;
				string::size_type n = 0;
				while ((n = clip_name.find(old_name, n)) != string::npos)
				{
					clip_name.replace(n, old_name.size(), output_havok_project_name);
					n += output_havok_project_name.size();
				}
				clip->m_name = clip_name.c_str();
			}
			if (hkbExpressionDataArray::staticClass().getSignature() == object.m_class->getSignature())
			{
				hkRefPtr<hkbExpressionDataArray> expression = (hkbExpressionDataArray*)object.m_object;
				for (int i = 0; i < expression->m_expressionsData.getSize(); i++)
				{
					auto data = expression->m_expressionsData[i];
					string expression_string = expression->m_expressionsData[i].m_expression;
					if (expression_string.find(old_name) != string::npos)
					{
						string new_name = expression_string;
						string::size_type n = 0;
						while ((n = new_name.find(old_name, n)) != string::npos)
						{
							new_name.replace(n, old_name.size(), output_havok_project_name);
							n += output_havok_project_name.size();
						}
						Log::Info("Will substitute %s expression with %s", expression_string.c_str(), new_name.c_str());
						expression->m_expressionsData[i].m_expression = new_name.c_str();
					}
				}
			}
		}
		string relative = (file.parent_path().filename() / file.filename()).string();
		auto it = retarget_map.find(string(relative));
		fs::path behavior_output_file = output;
		if (it != retarget_map.end())
			behavior_output_file = behavior_output_file / it->second;
		else
			behavior_output_file = behavior_output_file / relative;
		fs::create_directories(behavior_output_file.parent_path());
		wrapper.write(root, behavior_output_file);
	}

	//Now adjust the cache
	string output_havok_project_lower_name = output_havok_project_name;
	transform(output_havok_project_lower_name.begin(), output_havok_project_lower_name.end(), output_havok_project_lower_name.begin(), ::tolower);
	auto cache_ptr = cache.cloneCreature(cache_name, output_havok_project_lower_name + "project");
	//retarget caches
	auto block = cache_ptr->block.toASCII();
	string::size_type n = 0;
	while ((n = block.find(old_name, n)) != string::npos)
	{
		block.replace(n, old_name.size(), output_havok_project_name);
		n += output_havok_project_name.size();
	}
	cache_ptr->block.clear();
	cache_ptr->block.fromASCII(block);
	auto set_block = cache_ptr->sets.getBlock();
	n = 0;
	while ((n = set_block.find(old_name, n)) != string::npos)
	{
		set_block.replace(n, old_name.size(), output_havok_project_name);
		n += output_havok_project_name.size();
	}
	//adjust crc
	for (const auto& it : retarget_map) {
		string token = it.first;
		n = 0;
		while ((n = set_block.find(token, n)) != string::npos)
		{
			set_block.replace(n, token.size(), it.second);
			n += it.second.size();
		}
	}
	cache_ptr->sets.clear();
	cache_ptr->sets.parseBlock(scannerpp::Scanner(set_block));

	cache.save_creature(output_havok_project_name + "project", cache_ptr, "animationdatasinglefile.txt", "animationsetdatasinglefile.txt");


	Games& games = Games::Instance();
	Games::Game tes5 = Games::TES5;

	if (!games.isGameInstalled(tes5)) {
		Log::Error("This command only works on TES5, and doesn't seem to be installed. Be sure to run the game at least once.");
		return false;
	}

	Collection skyrimCollection = Collection((char * const)(games.data(tes5).string().c_str()), 3);
	ModFlags masterFlags = ModFlags(0xA);
	ModFlags skyblivionFlags = ModFlags(0xA);
	ModFile* esm = skyrimCollection.AddMod("Skyrim.esm", masterFlags);
	ModFile* update = skyrimCollection.AddMod("Update.esm", masterFlags);
	ModFile* dawnguard = skyrimCollection.AddMod("Dawnguard.esm", masterFlags);
	ModFile* Hearthfires = skyrimCollection.AddMod("Hearthfires.esm", masterFlags);
	ModFile* Dragonborn = skyrimCollection.AddMod("Dragonborn.esm", masterFlags);
	ModFlags espFlags = ModFlags(0x1818);
	espFlags.IsNoLoad = false;
	espFlags.IsFullLoad = true;
	espFlags.IsMinLoad = false;
	espFlags.IsCreateNew = true;
	ModFile* skyrimMod = skyrimCollection.AddMod("zombie_template.esp", espFlags);

	char * argvv[4];
	argvv[0] = new char();
	argvv[1] = new char();
	argvv[2] = new char();
	argvv[3] = new char();
	logger.init(4, argvv);

	skyrimCollection.Load();
	auto it = skyrimCollection.FormID_ModFile_Record.equal_range(REV32(IDLE));

	map<string, Sk::MOVTRecord*> movts;
	map<string, Sk::SNDRRecord*> sndrs;
	map<string, Sk::IDLERecord*> idles;
	for (auto idle_record_it = skyrimCollection.FormID_ModFile_Record.begin(); idle_record_it != skyrimCollection.FormID_ModFile_Record.end(); idle_record_it++)
	{
		Record* record = idle_record_it->second;
		if (record->GetType() == REV32(MOVT)) {
			Sk::MOVTRecord* movt = dynamic_cast<Sk::MOVTRecord*>(record);
			if (retarget_MOVT.find(string(movt->MNAM.value)) != retarget_MOVT.end())
				movts[string(movt->EDID.value)]=movt;
		}
		if (record->GetType() == REV32(SNDR)) {
			Sk::SNDRRecord* sndr = dynamic_cast<Sk::SNDRRecord*>(record);
			string SDless = string(sndr->EDID.value).substr(0, string(sndr->EDID.value).size() - 2);
			if (retarget_SOUN.find(SDless) != retarget_SOUN.end() || retarget_SOUN.find(string(sndr->EDID.value)) != retarget_SOUN.end())
				sndrs[string(sndr->EDID.value)] = sndr;
		}
		else if (record->GetType() == REV32(IDLE)) {
			Sk::IDLERecord* idle = dynamic_cast<Sk::IDLERecord*>(record);
			auto behavior = idle->DNAM.value;
			if (NULL != behavior && std::string(behavior).find(old_name) != std::string::npos)
			{
				idles[string(idle->EDID.value)] = idle;
				//there are some errors in vanilkla esm regarding behavior assignments, let's try to correct them
				auto references = idle->ANAM;
				auto find_it = skyrimCollection.FormID_ModFile_Record.find(references.value.parent);
				if (find_it != skyrimCollection.FormID_ModFile_Record.end())
				{
					if (find_it->second->GetType() == REV32(IDLE)) {
						Sk::IDLERecord* parent = dynamic_cast<Sk::IDLERecord*>(find_it->second);
						if (parent) {
							parent->DNAM = idle->DNAM;
							idles[string(parent->EDID.value)] = parent;
						}
					}
				}
				find_it = skyrimCollection.FormID_ModFile_Record.find(references.value.sibling);
				if (find_it != skyrimCollection.FormID_ModFile_Record.end())
				{
					if (find_it->second->GetType() == REV32(IDLE)) {
						Sk::IDLERecord* sibling = dynamic_cast<Sk::IDLERecord*>(find_it->second);
						if (sibling) {
							sibling->DNAM = idle->DNAM;
							idles[string(sibling->EDID.value)] = sibling;
						}
					}
				}
			}
		}
	}

	std::map<FORMID, FORMID> retargeted;
	std::vector<Sk::IDLERecord*> new_records;

	for (auto& movt_it : movts) {
		auto movt = movt_it.second;
		std::string new_edid = replace_all(movt->EDID.value, old_name, output_havok_project_name);
		std::string new_MNAM = replace_all(movt->MNAM.value, old_name, output_havok_project_name);

		// declaring character array 
		char* char_array = new char[new_edid.size() + 1];

		// copying the contents of the 
		// string to char array 
		strcpy(char_array, new_edid.c_str());

		Record* to_copy = static_cast<Record*>(movt);
		Record* result = skyrimCollection.CopyRecord(to_copy, skyrimMod, NULL, NULL, char_array, 0);
		Sk::MOVTRecord* copied = dynamic_cast<Sk::MOVTRecord*>(result);

		char* mnam_char = new char[new_MNAM.size() + 1];
		strcpy(mnam_char, new_MNAM.c_str());
		copied->EDID.value = char_array;
		copied->MNAM.value = mnam_char;
		copied->ESPED = movt->ESPED;
		copied->INAM = movt->INAM;

		result->IsChanged(true);
		result->IsLoaded(true);
		FormIDMasterUpdater checker(skyrimMod->FormIDHandler);
		checker.Accept(result->formID);
	}

	for (auto& sndr_it : sndrs) {
		auto sndr = sndr_it.second;
		std::string new_edid = replace_all(sndr->EDID.value, old_name, output_havok_project_name);
		if (!ends_with(new_edid, "SD"))
			new_edid += "SD";

		// declaring character array 
		char* char_array = new char[new_edid.size() + 1];

		// copying the contents of the 
		// string to char array 
		strcpy(char_array, new_edid.c_str());

		Record* to_copy = static_cast<Record*>(sndr);
		Record* result = skyrimCollection.CopyRecord(to_copy, skyrimMod, NULL, NULL, char_array, 0);
		Sk::SNDRRecord* copied = dynamic_cast<Sk::SNDRRecord*>(result);
		copied->EDID.value = char_array;
		copied->CNAM = sndr->CNAM;
		copied->GNAM = sndr->GNAM;
		copied->SNAM = sndr->SNAM;
		copied->FNAM = sndr->FNAM;
		copied->ANAM = sndr->ANAM;
		copied->ONAM = sndr->ONAM;
		copied->CTDA = sndr->CTDA;
		copied->LNAM = sndr->LNAM;
		copied->BNAM = sndr->BNAM;

		result->IsChanged(true);
		result->IsLoaded(true);
		FormIDMasterUpdater checker(skyrimMod->FormIDHandler);
		checker.Accept(result->formID);

	}

	for (auto& idle_it : idles) {
		auto idle = idle_it.second;
		std::string new_name = idle->EDID.value;
		std::string::size_type n = 0;
		if (new_name.find(old_name) != std::string::npos)
		{
			while ((n = new_name.find(old_name, n)) != std::string::npos)
			{
				new_name.replace(n, std::string(old_name).size(), output_havok_project_name);
				n += std::string(output_havok_project_name).size();
			}
		}
		else {
			new_name = output_havok_project_name + new_name;
		}

		const int nn = new_name.length();

		// declaring character array 
		char* char_array = new char[nn + 1];

		// copying the contents of the 
		// string to char array 
		strcpy(char_array, new_name.c_str());

		Record* to_copy = static_cast<Record*>(idle);

		Record* result = skyrimCollection.CopyRecord(to_copy, skyrimMod, NULL, NULL, char_array, 0);
		result->IsChanged(true);
		Sk::IDLERecord* copied = dynamic_cast<Sk::IDLERecord*>(result);
		result->IsChanged(true);
		retargeted[idle->formID] = copied->formID;
		new_records.push_back(copied);
		copied->EDID.value = char_array;
		copied->ANAM = idle->ANAM;
		copied->CTDA = idle->CTDA;
		copied->DATA = idle->DATA;
		copied->ENAM = idle->ENAM;

		std::string temp = replace_all(idle->DNAM.value, old_name, output_havok_project_name);
		char* behavior_char = new char[temp.size() + 1];
		strcpy(behavior_char, temp.c_str());

		copied->DNAM.value = behavior_char;

		result->IsChanged(true);
		result->IsLoaded(true);
		FormIDMasterUpdater checker(skyrimMod->FormIDHandler);
		checker.Accept(result->formID);

	}

	for (int i = 0; i < new_records.size(); i++) {
		auto idle = new_records[i];
		Sk::IDLEANAM a = idle->ANAM.value;
		if (retargeted.find(idle->ANAM.value.parent) != retargeted.end())
			a.parent = retargeted[idle->ANAM.value.parent];
		if (retargeted.find(idle->ANAM.value.sibling) != retargeted.end())
			a.sibling = retargeted[idle->ANAM.value.sibling];
		idle->ANAM.value = a;
	}


	ModSaveFlags skSaveFlags = ModSaveFlags(2);
	skSaveFlags.IsCleanMasters = true;
	string esp_name = (output_havok_project_name + ".esp");
	skyrimCollection.SaveMod(skyrimMod, skSaveFlags, (char* const)esp_name.c_str());



	//InitializeHavok();

	//BSAFile bsa_file("C:\\git_ref\\resources\\bsa\\Skyrim - Animations.bsa");
	//const std::regex re_actors("meshes\\\\actors\\\\(?!.*(animations|characters|character assets|characterassets|sharedkillmoves|behaviors)).*hkx", std::regex_constants::icase);
	//const std::regex re_misc("(?!.*(\\\\actors|\\\\.*animations|\\\\.*characters|\\\\.*character assets|\\\\.*characterassets|\\\\.*sharedkillmoves|\\\\behaviors|\\\\.+behaviors\\\\.*behavior|handeffect|handfx|fx.*cloak)).*hkx", std::regex_constants::icase);


	//vector<string> projects;
	//vector<string> misc;
	//set<fs::path> filenames;

	////find all projects
	//vector<string> assets = bsa_file.assets(".*\.hkx");
	//for (const auto& hkx_file : assets)
	//{
	//	string data = bsa_file.extract(hkx_file);
	//	hkRootLevelContainer* root = NULL;
	//	hkRefPtr<hkbProjectData> hkroot = wrapper.load<hkbProjectData>((const uint8_t *)data.c_str(), data.size(), root);
	//	if (hkroot != NULL)
	//	{
	//		if (fs::path(hkx_file).parent_path().string().find("actors") != string::npos)
	//			projects.push_back(hkx_file);
	//		else
	//		{
	//			string filename = fs::path(hkx_file).filename().string();
	//			//if (!filenames.insert(filename).second)
	//			//	continue;
	//			if (filename.find("fxfirecloak01.hkx") != string::npos ||
	//				filename.find("fxicecloak") != string::npos ||
	//				filename.find("healcontargetfx01") != string::npos ||
	//				filename.find("healfxhand01.hkx") != string::npos ||
	//				filename.find("healmystcontargetfx01.hkx") != string::npos ||
	//				filename.find("healmystfxhand01.hkx") != string::npos ||
	//				filename.find("healmysttargetfx.hkx") != string::npos ||
	//				filename.find("invisfxbody01.hkx") != string::npos ||
	//				filename.find("invisfxhand01.hkx") != string::npos ||
	//				filename.find("lightspellactorfx.hkx") != string::npos ||
	//				filename.find("lightspellhazard.hkx") != string::npos ||
	//				filename.find("shockhandeffects.hkx") != string::npos ||
	//				filename.find("soultrapcastpointfx01.hkx") != string::npos ||
	//				filename.find("soultraphandeffects.hkx") != string::npos ||
	//				filename.find("soultraptargeteffects.hkx") != string::npos ||
	//				filename.find("soultraptargetpointfx.hkx") != string::npos ||
	//				filename.find("summontargetfx.hkx") != string::npos ||
	//				filename.find("turnundeadfxhand01.hkx") != string::npos ||
	//				filename.find("turnundeadtargetfx.hkx") != string::npos ||
	//				hkx_file.find("traps\\leantotrap\\norretractablebridge01.hkx") != string::npos ||
	//				filename.find("fxlightspellhandeffects") != string::npos
	//			)
	//				continue;
	//			hkRefPtr<hkbProjectStringData> sdata = hkroot->m_stringData;
	//			if (sdata->m_characterFilenames.getSize() > 0)
	//			{
	//				string char_rel_path = sdata->m_characterFilenames[0];
	//				hkRootLevelContainer* croot = NULL;
	//				string char_path = fs::canonical((fs::path(hkx_file).parent_path() / char_rel_path)).string();
	//				size_t offset = char_path.find("meshes", 0);
	//				size_t end = char_path.length();
	//				if (offset < end)
	//					char_path = char_path.substr(offset, end);
	//				if (bsa_file.find(char_path))
	//				{
	//					string cdata = bsa_file.extract(char_path);
	//					hkRefPtr<hkbCharacterData> chkroot = wrapper.load<hkbCharacterData>((const uint8_t *)cdata.c_str(), cdata.size(), croot);
	//					misc.push_back(hkx_file);
	//				}
	//				else {
	//					Log::Warn("Project file with character file not found!: %s", hkx_file.c_str());
	//				}
	//			}
	//			else {
	//				Log::Warn("Project file without character: %s", hkx_file.c_str());
	//			}
	//		}
	//	}

	//}
	//std::sort(projects.begin(), projects.end(),
	//	[](const string& lhs, const string& rhs) -> bool
	//{
	//	fs::path pllhs = lhs;
	//	fs::path plrhs = rhs;

	//	if (pllhs.parent_path() != plrhs.parent_path())
	//		return plrhs.parent_path() > pllhs.parent_path();

	//	return plrhs.filename() > pllhs.filename();
	//});

	//std::sort(misc.begin(), misc.end(),
	//	[](const string& lhs, const string& rhs) -> bool
	//{
	//	fs::path pllhs = lhs; 
	//	fs::path plrhs = rhs;

	//	if (pllhs.parent_path() != plrhs.parent_path())
	//		return plrhs.parent_path() > pllhs.parent_path();

	//	return plrhs.filename() > pllhs.filename();
	//});


	////AnimationCache::rebuild_from_bsa(bsa_file, projects, misc);

	//string animDataContent;
	//string animSetDataContent;

	//const std::string animDataPath = "meshes\\animationdatasinglefile.txt";
	//const std::string animSetDataPath = "meshes\\animationsetdatasinglefile.txt";

	//string anim_data_content = bsa_file.extract(animDataPath);
	//string anim_set_data_content = bsa_file.extract(animSetDataPath);

	//AnimationCache cache(anim_data_content, anim_set_data_content);

	//int cache_index = 0;

	//for (const auto& creature_project_file : projects)
	//{
	//	string project_file_data = bsa_file.extract(creature_project_file);

	//	hkRootLevelContainer* root = NULL;
	//	hkRefPtr<hkbProjectData>  project_file_hkroot =
	//		wrapper.load<hkbProjectData>((const uint8_t *)project_file_data.c_str(), project_file_data.size(), root);

	//	Log::Info("Analyzing project: %s", creature_project_file.c_str());

	//	//Characters
	//	if (project_file_hkroot->m_stringData != NULL)
	//	{
	//		for (const auto& character : project_file_hkroot->m_stringData->m_characterFilenames)
	//		{
	//			Log::Info("Found Character: %s", character);
	//			string char_path = fs::canonical((fs::path(creature_project_file).parent_path() / string(character))).string();
	//			size_t offset = char_path.find("meshes", 0);
	//			size_t end = char_path.length();
	//			if (offset < end)
	//				char_path = char_path.substr(offset, end);
	//			Log::Info("Canonical Character Path: %s", char_path.c_str());

	//			string character_file_data = bsa_file.extract(char_path);
	//			hkRootLevelContainer* character_root = NULL;
	//			hkRefPtr<hkbCharacterData>  character_file_hkroot =
	//				wrapper.load<hkbCharacterData>((const uint8_t *)character_file_data.c_str(), character_file_data.size(), root);
	//			set<hkRefPtr<hkbClipGenerator>, clip_compare> generators;
	//			if (character_file_hkroot->m_stringData != NULL)
	//			{
	//				Log::Info("Animations: %d", character_file_hkroot->m_stringData->m_animationNames.getSize());
	//				Log::Info("Behavior: %s", character_file_hkroot->m_stringData->m_behaviorFilename);
	//				Log::Info("Rig: %s", character_file_hkroot->m_stringData->m_rigName);
	//				Log::Info("Ragdoll: %s", character_file_hkroot->m_stringData->m_ragdollName);

	//				
	//				extract_clips(
	//					bsa_file,
	//					string(character_file_hkroot->m_stringData->m_behaviorFilename),
	//					fs::path(creature_project_file).parent_path(),
	//					generators
	//				);
	//			}
	//			set<string> referenced_animations;
	//			for (const auto& generator : generators)
	//			{
	//				referenced_animations.insert(string(generator->m_animationName));
	//			}

	//			CreatureCacheEntry& entry = cache.creature_entries[cache_index];
	//			if (character_file_hkroot->m_stringData->m_animationNames.getSize() !=
	//				referenced_animations.size())
	//			{
	//				Log::Warn("Declared animations %d, referenced by clips %d",
	//					character_file_hkroot->m_stringData->m_animationNames.getSize(),
	//					referenced_animations.size());
	//				for (const auto& decl_anim : character_file_hkroot->m_stringData->m_animationNames)
	//				{
	//					if (referenced_animations.find(string(decl_anim)) == referenced_animations.end())
	//					{
	//						Log::Warn("Declared but not in clip %s", decl_anim);
	//					}
	//				}
	//			}
	//			if (character_file_hkroot->m_stringData->m_animationNames.getSize() !=
	//				entry.movements.getMovementData().size())
	//				Log::Warn("Declared animations %d, cache movements %d",
	//					character_file_hkroot->m_stringData->m_animationNames.getSize(),
	//					entry.movements.getMovementData().size());
	//			if (generators.size() !=
	//				entry.block.getClips().size())
	//				Log::Warn("Behavior clips %d, cache blocks %d",
	//					generators.size(),
	//					entry.block.getClips().size());

	//		}
	//	}
	//	cache_index++;

	//}
	//
	//fs::path project_path;

	//TODO: take load order into account!

	//find the project and load it
	//if (cache.hasCreatureProject(source_havok_project)) {
	//	Log::Info("Retargeting %s into %s", source_havok_project.c_str(), output_havok_project_name.c_str());
	//	//search into override
	//	string to_find = fs::path(source_havok_project).replace_extension(".hkx").string();
	//	transform(to_find.begin(), to_find.end(), to_find.begin(), ::tolower);
	//	for (auto& p : fs::recursive_directory_iterator(games.data(tes5))) {
	//		string filename = p.path().filename().string();
	//		transform(filename.begin(), filename.end(), filename.begin(), ::tolower);
	//		if (filename == to_find) {
	//			project_path = p.path();
	//			project = wrapper.load<hkbProjectData>(p.path());
	//			in_override = true;
	//			break;
	//		}
	//	}
	//	if (!in_override) {
	//		for (const auto& bsa_path : games.bsas(tes5)) {
	//			BSAFile bsa_file(bsa_path);
	//			std::vector<std::string> results = bsa_file.assets(".*" + to_find);
	//			if (results.size() > 0)
	//			{
	//				//get the shortest path
	//				std::sort(results.begin(), results.end(), compare());
	//				project_path = results[0];
	//				Log::Info("%s", results[0].c_str());
	//				size_t size = -1;
	//				const uint8_t* data = bsa_file.extract(results[0], size);
	//				project = wrapper.load<hkbProjectData>(data, size);
	//				delete data;
	//				break;
	//			}					
	//		}
	//	}

	//}

	//fs::path out = games.data(tes5) / project_path.parent_path() / output_havok_project_name;
	//
	////skyrim files just usually have the character file here
	//vector<fs::path> character_paths;
	//hkArray<hkStringPtr>& characters_files = project->m_stringData->m_characterFilenames;
	//for (const auto& char_path_string : characters_files) {
	//	character_paths.push_back(project_path.parent_path() / fs::path(char_path_string.cString()));
	//}

	//vector<hkRefPtr<hkbCharacterData>> characters;
	////If not in override, we must extract all resources from bsa
	//for (const auto& rel_path : character_paths) {
	//	characters.push_back(load_havok_file<hkbCharacterData>(rel_path, wrapper));
	//}

	//vector<fs::path> rigs_paths;
	//vector<hkRefPtr<hkaAnimationContainer>> rigs;
	//vector<fs::path> behaviors_paths;
	//vector<hkRefPtr<hkbBehaviorGraph>> behaviors;
	//map<hkRefPtr<hkbBehaviorGraph>, hkArray<hkVariant>> behaviors_objects;
	//
	//for (const auto& char_ptr : characters) {
	//	hkRefPtr<hkbCharacterStringData> data = char_ptr->m_stringData;
	//	rigs_paths.push_back(project_path.parent_path() / fs::path(data->m_rigName.cString()));
	//	rigs.push_back(load_havok_file<hkaAnimationContainer>(project_path.parent_path() / fs::path(data->m_rigName.cString()), wrapper));
	//	behaviors_paths.push_back(project_path.parent_path() / fs::path(data->m_behaviorFilename.cString()));
	//	hkArray<hkVariant> behavior_objs;
	//	hkRefPtr<hkbBehaviorGraph> behavior_root = load_havok_file<hkbBehaviorGraph>(project_path.parent_path() / fs::path(data->m_behaviorFilename.cString()), behavior_objs, wrapper);
	//	behaviors_objects[behavior_root] = behavior_objs;
	//	behaviors.push_back(behavior_root);
	//}
	//size_t size = behaviors.size();
	//for (size_t i = 0; i < size; ++i)
	//{
	//	//look into behaviors for referenced files
	//	hkArray<hkVariant>& objects = behaviors_objects[behaviors[i]];
	//	for (const auto& obj : objects) {
	//		if (strcmp(obj.m_class->getName(), "hkbBehaviorReferenceGenerator") == 0) {
	//			hkRefPtr<hkbBehaviorReferenceGenerator> brg = (hkbBehaviorReferenceGenerator*)obj.m_object;
	//			fs::path sub_behavior_path = project_path.parent_path() / brg->m_behaviorName.cString();
	//			if (find(behaviors_paths.begin(), behaviors_paths.end(), sub_behavior_path) == behaviors_paths.end()) {
	//				hkArray<hkVariant> behavior_objs;
	//				behaviors_paths.push_back(sub_behavior_path);
	//				hkRefPtr<hkbBehaviorGraph> behavior_root = load_havok_file<hkbBehaviorGraph>(sub_behavior_path, behavior_objs, wrapper);
	//				behaviors_objects[behavior_root] = behavior_objs;
	//				behaviors.push_back(behavior_root);
	//				size++;
	//			}
	//		}
	//	}

	//}

	//vector<fs::path> animations_paths;
	//vector<hkRefPtr<hkaAnimationContainer>> animations;

	//for (const auto& behavior_ptr : behaviors) {
	//	hkArray<hkVariant>& objects = behaviors_objects[behavior_ptr];
	//	for (const auto& obj : objects) {
	//		if (strcmp(obj.m_class->getName(), "hkbClipGenerator") == 0) 
	//		{
	//			hkRefPtr<hkbClipGenerator> clip = (hkbClipGenerator*)obj.m_object;
	//			fs::path clip_path = project_path.parent_path() / clip->m_animationName.cString();
	//			if (find(animations_paths.begin(), animations_paths.end(), clip_path) == animations_paths.end())
	//			{
	//				wrapper.save(load_havok_file<hkaAnimationContainer>(clip_path, wrapper),
	//					out / clip->m_animationName.cString());

	//			}
	//		}
	//	}
	//}

	CloseHavok();
	return true;
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