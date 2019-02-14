#pragma once

#include "ClipMovementData.h"

namespace AnimData {
	class ProjectDataBlock : public Block {

		std::list<ClipMovementData> movementData; // = new ArrayList<>();

	public: 
		std::list<ClipMovementData> getMovementData() {
			return movementData;
		}

		void setMovementData(std::list<ClipMovementData> movementData) {
			this->movementData = movementData;
		}

		void parseBlock(scannerpp::Scanner& input) override {
			while (input.hasNextLine()) {
				ClipMovementData b; // = new ClipMovementData();
				b.parseBlock(input);
				movementData.push_back(b);
				//input.nextLine();
			}
		}

		std::string getBlock() override {
			std::string out = "";
			for (ClipMovementData data : movementData)
				out += data.getBlock();
			return out;
		}

	};
}
