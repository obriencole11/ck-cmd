#pragma once

#include "ClipAttacksBlock.h"
#include "ClipFilesCRC32Block.h"
#include "UnkEventData.h"

namespace AnimData {

	class ProjectAttackBlock : public BlockObject {

		std::string animVersion = "V3";
		StringListBlock unkEventList; // = new StringListBlock();
		UnkEventData unkEventData; // = new UnkEventData();
		ClipAttacksBlock attackData; // = new ClipAttacksBlock();
		ClipFilesCRC32Block crc32Data; // = new ClipFilesCRC32Block();
	public:
		StringListBlock getUnkEventList() {
			return unkEventList;
		}

		void setUnkEventList(StringListBlock unkEventList) {
			this->unkEventList = unkEventList;
		}

		UnkEventData getUnkEventData() {
			return unkEventData;
		}

		void setUnkEventData(UnkEventData unkEventData) {
			this->unkEventData = unkEventData;
		}

		ClipAttacksBlock getAttackData() {
			return attackData;
		}

		void setAttackData(ClipAttacksBlock attackData) {
			this->attackData = attackData;
		}
		
		ClipFilesCRC32Block getCrc32Data() {
			return crc32Data;
		}
		
		void setCrc32Data(ClipFilesCRC32Block crc32Data) {
			this->crc32Data = crc32Data;
		}


		void parseBlock(scannerpp::Scanner& input) override {
			animVersion = input.nextLine();
			unkEventList.fromASCII(input);
			unkEventData.fromASCII(input);
			int numAttackBlocks = input.nextInt();//input.nextLine();
			attackData.setBlocks(numAttackBlocks);
			attackData.parseBlock(input);
			crc32Data.fromASCII(input);
		}
		
		std::string getBlock() override {
			std::string out = animVersion + "\n";
			out += unkEventList.toASCII();
			out += unkEventData.toASCII();
			out += attackData.getBlocks() + "\n";
			out += attackData.getBlock();
			out += crc32Data.toASCII();
			return out;
		}
	};
}
