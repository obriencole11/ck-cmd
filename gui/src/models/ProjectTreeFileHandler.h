#pragma once

#include <src/hkx/ResourceManager.h>
#include <src/models/ModelEdge.h>

#include <QVariant>

struct hkVariant;

namespace ckcmd {
	namespace HKX {

		class ProjectTreeFileHandler
		{

		public:

			static int getChildCount(int file_index, NodeType childType, ResourceManager& _manager);
			static QVariant data(int row, int column, int file_index, NodeType childType, ResourceManager& _manager);
			static bool hasChild(int row, int _project, int _file, NodeType _childType, ResourceManager& manager);
			static ModelEdge getChild(int index, int project, int file, NodeType childType, ResourceManager& _manager);
		};


	}
}