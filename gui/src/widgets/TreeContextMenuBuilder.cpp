#include "TreeContextMenuBuilder.h"

using namespace ckcmd::HKX;

void TreeContextMenuBuilder::buildCharactersNodeMenu(std::vector<QAction*>& actions)
{
	actions.push_back(_actionHandler.createProjectAction());
}

void TreeContextMenuBuilder::buildMiscsNodeMenu(std::vector<QAction*>& actions)
{
	actions.push_back(_actionHandler.createProjectAction());
}

void TreeContextMenuBuilder::buildAnimationsMenu(std::vector<QAction*>& actions)
{
	actions.push_back(_actionHandler.importFBXAction());
}

void TreeContextMenuBuilder::buildAnimationMenu(std::vector<QAction*>& actions)
{
	actions.push_back(_actionHandler.removeAnimationAction());
}

QMenu* TreeContextMenuBuilder::build(NodeType type, QVariant action_data)
{
	std::vector<QAction*> applicable_actions;
	switch (type) {
		case NodeType::animationNames:
			buildAnimationsMenu(applicable_actions);
			break;
		case NodeType::animationName:
			buildAnimationMenu(applicable_actions);
			break;
		default:
			break;
	}
	if (!applicable_actions.empty())
	{
		QMenu* menu = new QMenu();
		for (auto& action : applicable_actions)
		{
			action->setData(action_data);
			menu->addAction(action);
		}
		return menu;
	}
	return nullptr;
}