#include "ProjectBuilder.h"
#include <src/hkx/TreeBuilder.h>
#include <src/hkx/SkeletonBuilder.h>

#include <hkbProjectStringData_1.h>
#include <hkbCharacterStringData_5.h>
#include <hkbBehaviorGraph_1.h>

using namespace ckcmd::HKX;

void ProjectBuilder::buildBranch(hkVariant& root, ProjectNode* root_node, const fs::path& path) {
	HkxTableVariant h(root);
	std::set<void*> visited;
	TreeBuilder b(root_node, _resourceManager, path, visited);
	h.accept(b);
}

ProjectBuilder::ProjectBuilder(
	ProjectNode* parent,
	ResourceManager& resourceManager,
	const std::string& name
) :
	_parent(parent),
	_resourceManager(resourceManager),
	_name(name)
{
	auto project_path = fs::path(_name);
	auto project_folder = project_path.parent_path();
	hkVariant project_root;
	hkbProjectStringData* project_data = loadHkxFile<hkbProjectStringData>(project_path, hkbProjectStringDataClass, project_root);
	auto project_file_index = _resourceManager.index(project_path);
	ProjectNode* project_node = _resourceManager.createSupport(project_file_index, {"Projects", project_path.string().c_str() }, _parent);
	_parent->appendChild(project_node);
	buildBranch(project_root, project_node, project_path);
	
	if (project_data)
	{
		ProjectNode* characters_node = _resourceManager.createSupport(project_file_index, { "Characters" }, _parent);
		_parent->appendChild(characters_node);
		//Load characters
		for (int c = 0; c < project_data->m_characterFilenames.getSize(); c++)
		{
			//character data
			auto character_path = project_folder / project_data->m_characterFilenames[c].cString();
			hkVariant character_root;
			hkbCharacterStringData* character_data = loadHkxFile<hkbCharacterStringData>(character_path, hkbCharacterStringDataClass, character_root);
			if (character_data == NULL)
				throw std::runtime_error("hkbCharacterStringData variant not found in " + character_path.string());
			auto character_file_index = _resourceManager.index(character_path);
			ProjectNode* character_node = _resourceManager.createCharacter(character_file_index, { character_data->m_name.cString(), character_path.string().c_str() }, characters_node);
			characters_node->appendChild(character_node);
			buildBranch(character_root, character_node, character_path);

			//skeleton
			SkeletonBuilder* skeleton_builder = nullptr;
			if (!std::string(character_data->m_rigName).empty())
			{
				auto rig_path = project_folder / character_data->m_rigName.cString();
				auto& rig_contents = _resourceManager.get(rig_path);
				auto rig_index = _resourceManager.index(rig_path);
				skeleton_builder = new SkeletonBuilder(_resourceManager, rig_index);
				ProjectNode* rig_node = _resourceManager.createSkeleton(rig_index, { "Skeleton", rig_path.string().c_str() }, character_node);
				character_node->appendChild(rig_node);
				_resourceManager.setClassHandler(
					rig_index,
					static_cast<ITreeBuilderClassHandler*>(skeleton_builder)
				);
				_resourceManager.setFieldHandler(
					rig_index,
					static_cast<ISpecialFieldsHandler*>(skeleton_builder)
				);
				buildBranch(rig_contents.first, rig_node, rig_path);
			}

			//behavior
			auto behavior_path = project_folder / character_data->m_behaviorFilename.cString();
			hkVariant behavior_root;
			hkbBehaviorGraph* behavior_data = loadHkxFile<hkbBehaviorGraph>(behavior_path, hkbBehaviorGraphClass, behavior_root);
			if (behavior_data == NULL)
				throw std::runtime_error("hkbBehaviorGraph variant not found in " + behavior_path.string());
			auto behavior_index = _resourceManager.index(behavior_path);
			ProjectNode* behavior_node = _resourceManager.createBehavior(behavior_index, { behavior_data->m_name.cString(), behavior_path.string().c_str() }, character_node);
			character_node->appendChild(behavior_node);
			auto behavior_handler = new BehaviorBuilder(_resourceManager, behavior_index);
			behavior_handler->setSkeleton(skeleton_builder);
			_resourceManager.setClassHandler(
				behavior_index,
				static_cast<ITreeBuilderClassHandler*>(behavior_handler)
			);
			_resourceManager.setFieldHandler(
				behavior_index,
				static_cast<ISpecialFieldsHandler*>(behavior_handler)
			);
			buildBranch(behavior_root, behavior_node, behavior_path);
			//additional behaviors
			for (const auto& additional_behavior : behavior_handler->referenced_behaviors())
			{
				auto behavior_path = project_folder / additional_behavior;
				hkVariant behavior_root;
				hkbBehaviorGraph* behavior_data = loadHkxFile<hkbBehaviorGraph>(behavior_path, hkbBehaviorGraphClass, behavior_root);
				if (behavior_data == NULL)
					throw std::runtime_error("hkbBehaviorGraph variant not found in " + behavior_path.string());
				auto behavior_index = _resourceManager.index(behavior_path);
				ProjectNode* behavior_node = _resourceManager.createBehavior(behavior_index, { behavior_data->m_name.cString(), behavior_path.string().c_str() }, character_node);
				character_node->appendChild(behavior_node);
				
				auto behavior_handler = new BehaviorBuilder(_resourceManager, behavior_index);
				behavior_handler->setSkeleton(skeleton_builder);
				_resourceManager.setClassHandler(
					behavior_index,
					static_cast<ITreeBuilderClassHandler*>(behavior_handler)
				);
				_resourceManager.setFieldHandler(
					behavior_index,
					static_cast<ISpecialFieldsHandler*>(behavior_handler)
				);
				buildBranch(behavior_root, behavior_node, behavior_path);
			}


			//animations
			if (character_data->m_animationNames.getSize() > 0)
			{
				ProjectNode* animations_node = _resourceManager.createSupport(character_file_index, { "Animations" }, character_node);
				character_node->appendChild(animations_node);
				for (int a = 0; a < character_data->m_animationNames.getSize(); a++)
				{
					auto animation_path = project_folder / character_data->m_animationNames[a].cString();
					if (fs::exists(animation_path))
					{
						auto& animation_contents = _resourceManager.get(animation_path);
						ProjectNode* animation_node = _resourceManager.createAnimation(character_file_index, { character_data->m_animationNames[a].cString(), animation_path.string().c_str() }, animations_node);
						animations_node->appendChild(animation_node);
						//kinda pointless to open all the animations
						//buildBranch(animation_contents.first, animation_node, animation_path);
					}
				}
			}
		}
	}
}