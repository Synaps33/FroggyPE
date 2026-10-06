#include "LevelStorageSource.h"
#include <set>

const std::string LevelStorageSource::TempLevelId = "_LastJoinedServer";

std::string LevelStorageSource::makeUniqueLevelId(const std::string& wanted)
{
	LevelSummaryList levels;
	getLevelList(levels);

	std::set<std::string> used;
	for (size_t i = 0; i < levels.size(); ++i)
		used.insert(levels[i].id);

	std::string s = wanted.empty() ? std::string("World") : wanted;
	while (used.find(s) != used.end())
		s += "-";
	return s;
}
