#pragma once

#include "stdafx.h"

#include <core/log.h>
#include <bs/AnimDataFile.h>
#include <bs/AnimSetDataFile.h>
#include <filesystem>

namespace fs = std::experimental::filesystem;

/**
* Created by Bet on 10.03.2016.
*/
class HkCRC {
	//int crc_order = 32;
	//std::string crc_poly = "4C11DB7";
	//std::string initial_value = "000000";
	//std::string final_value = "000000";

	static int reflectByte(int c)
	{
		int outbyte = 0;
		int i = 0x01;
		int j;

		for (j = 0x80; j>0; j >>= 1)
		{
			int val = c & i;
			if (val > 0) outbyte |= j;
			i <<= 1;
		}
		return (outbyte);
	}

	static void reflect(int* crc, int bitnum, int startLSB)
	{
		int i, j, k, iw, jw, bit;

		for (k = 0; k + startLSB<bitnum - 1 - k; k++) {

			iw = 7 - ((k + startLSB) >> 3);
			jw = 1 << ((k + startLSB) & 7);
			i = 7 - ((bitnum - 1 - k) >> 3);
			j = 1 << ((bitnum - 1 - k) & 7);

			bit = crc[iw] & jw;
			if ((crc[i] & j)>0) crc[iw] |= jw;
			else crc[iw] &= (0xff - jw);
			if ((bit)>0) crc[i] |= j;
			else crc[i] &= (0xff - j);
		}
		//return(crc);
	}
public:
	
	static std::string compute(std::string input) {
		// computes crc value
		int i, j, k, bit, datalen, len, flag, counter, c, order, ch, actchar;
		std::string data, output;
		int* mask = new int[8];
		const char hexnum[] = { '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F' };
		int polynom[] = { 0,0,0,0,4,193,29,183 };
		int init[] = { 0,0,0,0,0,0,0,0 };
		int crc[] = { 0,0,0,0,0,0,0,0,0 };
		int xor[] = { 0,0,0,0,0,0,0,0 };
		order = 32;

		// generate bit mask
		counter = order;
		for (i = 7; i>0; i--)
		{
			if (counter >= 8) mask[i] = 255;
			else mask[i] = (1 << counter) - 1;
			counter -= 8;
			if (counter<0) counter = 0;
		}
		data = input;
		datalen = data.length();
		len = 0;
		for (i = 0; i<datalen; i++)
		{
			c = (int)data.at(i);
			if (data.at(i) == '%')				// unescape byte by byte (%00 allowed)
			{
				if (i>datalen - 3)
					Log::Error("Invalid data sequence");

				try {
					ch = (int)data.at(++i);
					c = (int)data.at(++i);
					c = (c & 15) | ((ch & 15) << 4);
				}
				catch (...) {
					Log::Error("Invalid data sequence");
					return "failure";
				}

			}
			c = reflectByte(c);
			for (j = 0; j<8; j++)
			{
				bit = 0;
				if ((crc[7 - ((order - 1) >> 3)] & (1 << ((order - 1) & 7)))>0) bit = 1;
				if ((c & 0x80)>0) bit ^= 1;
				c <<= 1;
				for (k = 0; k<8; k++)		// rotate all (max.8) crc bytes
				{
					crc[k] = ((crc[k] << 1) | (crc[k + 1] >> 7)) & mask[k];
					if ((bit)>0) crc[k] ^= polynom[k];
				}
			}
			len++;
		}
		reflect(crc, order, 0);
		for (i = 0; i<8; i++) crc[i] ^= xor[i];
		output = "";
		flag = 0;
		for (i = 0; i<8; i++)
		{
			actchar = crc[i] >> 4;
			if (flag>0 || actchar>0)
			{
				output += hexnum[actchar];
				flag = 1;
			}

			actchar = crc[i] & 15;
			if (flag>0 || actchar>0 || i == 7)
			{
				output += hexnum[actchar];
				flag = 1;
			}
		}
		return output;
	}
};

struct AnimationCache {
	AnimData::AnimDataFile animationData;
	AnimData::AnimSetDataFile animationSetData;

	map<string, int> data_map;
	map<string, int> set_data_map;

	map<string, string> data_set_data_map;

	AnimationCache(const string&  animationDataContent, const string&  animationSetDataContent) {

		animationData.parse(animationDataContent);
		animationSetData.parse(animationSetDataContent);

		int index = 0;
		for (string creature : animationSetData.getProjectsList().getStrings()) {
			string creature_project_name = fs::path(creature).filename().replace_extension("").string();
			set_data_map[creature_project_name] = index++;
		}
		index = 0;
		for (string misc : animationData.getProjectList().getStrings()) {
			string misc_project_name = fs::path(misc).filename().replace_extension("").string();
			data_map[misc_project_name] = index++;
		}
		for (const auto& pair : set_data_map) {
			if (data_map.find(pair.first) == data_map.end())
				Log::Error("Unpaired set data: %s", pair.first);
		}


	}

	void printInfo() {
		Log::Info("Parsed correctly %d havok projects", data_map.size());
		Log::Info("Found %d creatures projects:", set_data_map.size());
		checkInfo();
	}

	void checkInfo() {
		for (const auto& pair : set_data_map) {
			Log::Info("\tID:%d\tName: %s", pair.second, pair.first.c_str());

			int data_index = data_map[pair.first];
			int set_data_index = set_data_map[pair.first];

			AnimData::ProjectBlock& this_data = animationData.getProjectBlock(data_index);
			AnimData::ProjectDataBlock& this_movement_data = animationData.getprojectMovementBlock(data_index);
			AnimData::ProjectAttackListBlock& this_set_data = animationSetData.getProjectAttackBlock(set_data_index);
			Log::Info("\t\tHavok Files:%d", this_data.getProjectFiles().getStrings().size());
			for (auto& havok_file : this_data.getProjectFiles().getStrings())
				Log::Info("\t\t\t%s", havok_file.c_str());

			//Check CRC clips number
			size_t movements = this_movement_data.getMovementData().size();
			set<string> paths;
			
			for (auto& block : this_set_data.getProjectAttackBlocks())
			{
				auto& strings = block.getCrc32Data().getStrings();
				std::list<std::string>::iterator it;
				int i = 0;
				string this_path;
				for (it = strings.begin(); it != strings.end(); ++it) {
					if (i % 3 == 0)
						this_path = *it;
					if (i % 3 == 1)
						paths.insert(this_path + *it);
					i++;
				}
			}
			size_t crcs = paths.size();
			if (movements != crcs)
				Log::Info("Warning: unaddressed movement data!");

		}
	}

	void printCreatureInfo(const string& project_name)
	{
		if (hasCreatureProject(project_name))
		{
			int data_index = data_map[project_name];
			int set_data_index = set_data_map[project_name];

			AnimData::ProjectBlock& this_data = animationData.getProjectBlock(data_index);
			AnimData::ProjectAttackListBlock& this_set_data = animationSetData.getProjectAttackBlock(set_data_index);
		}
	}

	bool hasCreatureProject (const string& project_name) {
		return set_data_map.find(project_name) != set_data_map.end();
	}

};