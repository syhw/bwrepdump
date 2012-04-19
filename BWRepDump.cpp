#include "BWRepDump.h"
#include <float.h>
#include <sstream>
#include <iomanip>

// TODO CHANGE THESE CONSTANTS XXX
#define MIN_CDREGION_RADIUS 9
#define SECONDS_SINCE_LAST_ATTACK 13
#define DISTANCE_TO_OTHER_ATTACK 14*TILE_SIZE // in pixels
#define MAX_ATTACK_RADIUS 21.0*TILE_SIZE
#define MIN_ATTACK_RADIUS 7.0*TILE_SIZE
#define ARMY_TACTICAL_IMPORTANCE 1.0
#define OFFENDER_WIN_COEFFICIENT 2.0
#define REP_TIME_LIMIT 45

using namespace BWAPI;

bool analyzed;
bool analysis_just_finished;
BWTA::Region* home;
BWTA::Region* enemy_base;

/* Return TRUE if file 'fileName' exists */
bool fileExists(const char *fileName)
{
    DWORD fileAttr;
    fileAttr = GetFileAttributesA(fileName);
    if (0xFFFFFFFF == fileAttr)
        return false;
    return true;
}

std::string convertInt(int number)
{
	std::stringstream ss;
	ss << number;
	return ss.str();
}

int hash(const BWAPI::TilePosition& p)
{
	return (((p.x() + 1) << 16) | p.y());
}

int hashRegionCenter(BWTA::Region* r)
{
	/// Max size for a map is 512x512 build tiles => 512*32 = 16384 = 2^14 pixels
	/// Unwalkable regions will map to 0
	TilePosition p(r->getPolygon().getCenter());
	return hash(p);
}

BWAPI::TilePosition cdrCenter(ChokeDepReg c)
{
	/// /!\ This is will give centers out of the ChokeDepRegions for some coming from BWTA::Region
	return TilePosition(((0xFFFF0000 & c) >> 16) - 1, 0x0000FFFF & c);
}

BWAPI::TilePosition BWRepDump::findClosestWalkable(const BWAPI::TilePosition& tp)
{
	/// Finds the closest-to-"p" walkable position in the given "c"
	double m = DBL_MAX;
	BWAPI::TilePosition ret(tp);
	for (int x = max(0, tp.x() - 4); x < min(Broodwar->mapWidth(), tp.x() + 4); ++x)
		for (int y = max(0, tp.y() - 4); y < min(Broodwar->mapHeight(), tp.y() + 4); ++y)
		{
			TilePosition tmp(x, y);
			double dist = DBL_MAX;
			if (isWalkable(tmp) 
				&& tp.getDistance(tmp) < m)
			{
				m = tp.getDistance(tmp);
				ret = tmp;
			}
		}
	if (ret == tp)
	{
		for (int x = max(0, tp.x() - 10); x < min(Broodwar->mapWidth(), tp.x() + 10); ++x)
			for (int y = max(0, tp.y() - 10); y < min(Broodwar->mapHeight(), tp.y() + 10); ++y)
			{
				TilePosition tmp(x, y);
				double dist = DBL_MAX;
				if (isWalkable(tmp) 
					&& tp.getDistance(tmp) < m)
				{
					m = tp.getDistance(tmp);
					ret = tmp;
				}
			}
	}
	return ret;
}

BWAPI::TilePosition BWRepDump::findClosestWalkableSameCDR(const BWAPI::TilePosition& tp, ChokeDepReg c)
{
	/// Finds the closest-to-"p" walkable position in the given "c"
	double m = DBL_MAX;
	BWAPI::TilePosition ret(tp);
	for (int x = max(0, tp.x() - 4); x < min(Broodwar->mapWidth(), tp.x() + 4); ++x)
	{
		for (int y = max(0, tp.y() - 4); y < min(Broodwar->mapHeight(), tp.y() + 4); ++y)
		{
			TilePosition tmp(x, y);
			if (rd.chokeDependantRegion[x][y] == c 
				&& isWalkable(tmp) 
				&& tp.getDistance(tmp) < m)
			{
				m = tp.getDistance(tmp);
				ret = tmp;
			}
		}
	}
	if (rd.chokeDependantRegion[ret.x()][ret.y()] != c)
	{
		for (int x = max(0, tp.x() - 10); x < min(Broodwar->mapWidth(), tp.x() + 10); ++x)
			for (int y = max(0, tp.y() - 10); y < min(Broodwar->mapHeight(), tp.y() + 10); ++y)
			{
				TilePosition tmp(x, y);
				if (rd.chokeDependantRegion[x][y] == c 
					&& isWalkable(tmp) 
					&& tp.getDistance(tmp) < m)
				{
					m = tp.getDistance(tmp);
					ret = tmp;
				}
			}
	}
	if (ret == tp) // sometimes centers are in convex subregions
		return findClosestWalkable(tp);
	return ret;
}

BWTA::Region* findClosestRegion(const TilePosition& tp)
{
	double m = DBL_MAX;
	Position tmp(tp);
	BWTA::Region* ret = BWTA::getRegion(tp);
	for each (BWTA::Region* r in BWTA::getRegions())
	{
		if (r->getCenter().getDistance(tmp) < m)
		{
			m = r->getCenter().getDistance(tmp);
			ret = r;
		}
	}
	return ret;
}

ChokeDepReg BWRepDump::findClosestCDR(const BWAPI::TilePosition& tp)
{
	ChokeDepReg ret = rd.chokeDependantRegion[tp.x()][tp.y()];
	double m = DBL_MAX;
	for each (ChokeDepReg cdr in allChokeDepRegs)
	{
		if (cdrCenter(cdr).getDistance(tp) < m)
		{
			m = cdrCenter(cdr).getDistance(tp);
			ret = cdr;
		}
	}
	return ret;
}

BWTA::Region* BWRepDump::findClosestReachableRegion(BWTA::Region* q, BWTA::Region* r)
{
	double m = DBL_MAX;
	BWTA::Region* ret = q;
	for each (BWTA::Region* rr in BWTA::getRegions())
	{
		if (r->getReachableRegions().count(rr) && _pfMaps.distRegions[hashRegionCenter(rr)][hashRegionCenter(q)] < m)
		{
			m = _pfMaps.distRegions[hashRegionCenter(rr)][hashRegionCenter(q)];
			ret = rr;
		}
	}
	return ret;
}

ChokeDepReg BWRepDump::findClosestReachableCDR(ChokeDepReg q, ChokeDepReg cdr)
{
	double m = DBL_MAX;
	ChokeDepReg ret = q;
	for each (ChokeDepReg cdrr in allChokeDepRegs)
	{
		if (_pfMaps.distCDR[cdr][cdrr] && _pfMaps.distCDR[q][cdrr] < m)
		{
			m = _pfMaps.distCDR[cdrr][q];
			ret = cdrr;
		}
	}
	return ret;
}

std::string attackTypeToStr(AttackType at)
{
	if (at == DROP)
		return "DropAttack";
	else if (at == GROUND)
		return "GroundAttack";
	else if (at == AIR)
		return "AirAttack";
	else if (at == INVIS)
		return "InvisAttack";
	return "UnknownAttackError";
}

void BWRepDump::createChokeDependantRegions()
{
	char buf[1000];
	char buf2[1000];
	sprintf_s(buf, "bwapi-data/AI/terrain/%s.cdreg", BWAPI::Broodwar->mapHash().c_str());
	if (fileExists(buf))
	{	
		// fill our own regions data (rd) with the archived file
		std::ifstream ifs(buf, std::ios::binary);
		boost::archive::binary_iarchive ia(ifs);
		ia >> rd;
	}
	else
	{
		std::map<BWTA::Chokepoint*, int> maxTiles; // max tiles for each CDRegion
		int k = 1; // 0 is reserved for unwalkable regions
		/// 1. for each region, max radius = max(MIN_CDREGION_RADIUS, choke size)
		for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
		{
			maxTiles.insert(std::make_pair(c, max(MIN_CDREGION_RADIUS, static_cast<int>(c->getWidth())/TILE_SIZE)));
		}
		/// 2. Voronoi on both choke's regions
		for (int x = 0; x < Broodwar->mapWidth(); ++x)
			for (int y = 0; y < Broodwar->mapHeight(); ++y)
			{
				TilePosition tmp(x, y);
				BWTA::Region* r = BWTA::getRegion(tmp);
				double minDist = DBL_MAX - 100.0;
				for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
				{
					TilePosition chokeCenter(c->getCenter().x() / TILE_SIZE, c->getCenter().y() / TILE_SIZE);
					double tmpDist = tmp.getDistance(chokeCenter);
					double pathFindDist = DBL_MAX;
					if (tmpDist < minDist && tmpDist <= 1.0 * maxTiles[c]
						&& (pathFindDist=BWTA::getGroundDistance(tmp, chokeCenter)) < minDist
						&& pathFindDist >= 0.0 // -1 means no way to go there
					    && (c->getRegions().first == r || c->getRegions().second == r)
					)
					{
						minDist = pathFindDist;
						rd.chokeDependantRegion[x][y] = hash(chokeCenter);
					}
				}
			}
		/// 3. Complete with (amputated) BWTA regions
		for (int x = 0; x < Broodwar->mapWidth(); ++x)
			for (int y = 0; y < Broodwar->mapHeight(); ++y)
			{
				TilePosition tmp(x, y);
				if (rd.chokeDependantRegion[x][y] == -1 && BWTA::getRegion(tmp) != NULL)
					this->rd.chokeDependantRegion[x][y] = hashRegionCenter(BWTA::getRegion(tmp));
			}
		std::ofstream ofs(buf, std::ios::binary);
		{
			boost::archive::binary_oarchive oa(ofs);
			oa << rd;
		}
	}
	// initialize allChokeDepRegs
	for (int i = 0; i < Broodwar->mapWidth(); ++i)
	{
		for (int j = 0; j < Broodwar->mapHeight(); ++j)
		{
			if (rd.chokeDependantRegion[i][j] != -1)
				allChokeDepRegs.insert(rd.chokeDependantRegion[i][j]);
		}
	}

	sprintf_s(buf2, "bwapi-data/AI/terrain/%s.pfdrep", BWAPI::Broodwar->mapHash().c_str());
	if (fileExists(buf2))
	{	
		std::ifstream ifs(buf2, std::ios::binary);
		boost::archive::binary_iarchive ia(ifs);
		ia >> _pfMaps;
	}
	else
	{
		/// Fill regionsPFCenters (regions pathfinding aware centers, 
		/// min of the sum of the distance to chokes on paths between/to chokes)
		const std::set<BWTA::Region*> allRegions = BWTA::getRegions();
		for (std::set<BWTA::Region*>::const_iterator it = allRegions.begin();
			it != allRegions.end(); ++it)
		{
			std::list<Position> chokesCenters;
			for (std::set<BWTA::Chokepoint*>::const_iterator it2 = (*it)->getChokepoints().begin();
				it2 != (*it)->getChokepoints().end(); ++it2)
				chokesCenters.push_back((*it2)->getCenter());
			if (chokesCenters.empty())
				_pfMaps.regionsPFCenters.insert(std::make_pair(hashRegionCenter(*it), std::make_pair((*it)->getCenter().x(), (*it)->getCenter().y())));
			else
			{
				std::list<TilePosition> validTilePositions;
				for (std::list<Position>::const_iterator c1 = chokesCenters.begin();
					c1 != chokesCenters.end(); ++c1)
				{
					for (std::list<Position>::const_iterator c2 = chokesCenters.begin();
						c2 != chokesCenters.end(); ++c2)
					{
						if (*c1 != *c2)
						{
							std::vector<TilePosition> buffer = BWTA::getShortestPath(TilePosition(*c1), TilePosition(*c2));
							for (std::vector<TilePosition>::const_iterator vp = buffer.begin();
								vp != buffer.end(); ++vp)
								validTilePositions.push_back(*vp);
						}
					}
				}
				double minDist = DBL_MAX;
				TilePosition centerCandidate = TilePosition((*it)->getCenter());
				for (std::list<TilePosition>::const_iterator vp = validTilePositions.begin();
					vp != validTilePositions.end(); ++vp)
				{
					double tmp = 0.0;
					for (std::list<Position>::const_iterator c = chokesCenters.begin();
						c != chokesCenters.end(); ++c)
					{
						tmp += BWTA::getGroundDistance(TilePosition(*c), *vp);
					}
					if (tmp < minDist)
					{
						minDist = tmp;
						centerCandidate = *vp;
					}
				}
				Position tmp(centerCandidate);
				_pfMaps.regionsPFCenters.insert(std::make_pair(hashRegionCenter(*it), std::make_pair(tmp.x(), tmp.y())));
			}
		}

		/// Fill distRegions with the mean distance between each Regions
		/// -1 if the 2 Regions are not mutualy/inter accessible by ground
		for (std::set<BWTA::Region*>::const_iterator it = BWTA::getRegions().begin();
			it != BWTA::getRegions().end(); ++it)
		{
			_pfMaps.distRegions.insert(std::make_pair(hashRegionCenter(*it),
				std::map<int, double>()));
			for (std::set<BWTA::Region*>::const_iterator it2 = BWTA::getRegions().begin();
				it2 != BWTA::getRegions().end(); ++it2)
			{
				if (_pfMaps.distRegions.count(hashRegionCenter(*it2)))
					_pfMaps.distRegions[hashRegionCenter(*it)].insert(std::make_pair(hashRegionCenter(*it2),
						_pfMaps.distRegions[hashRegionCenter(*it2)][hashRegionCenter(*it)]));
				else
					_pfMaps.distRegions[hashRegionCenter(*it)].insert(std::make_pair(hashRegionCenter(*it2), 
						BWTA::getGroundDistance(TilePosition(regionsPFCenters(*it)), TilePosition(regionsPFCenters(*it2)))));
						//BWTA::getGroundDistance(TilePosition((*it)->getCenter()), TilePosition((*it2)->getCenter()))));
			}
		}

		/// Fill distCDR
		/// -1 if the 2 Regions are not mutualy/inter accessible by ground
		for each (ChokeDepReg cdr in allChokeDepRegs)
		{
			_pfMaps.distCDR.insert(std::make_pair(cdr,
				std::map<int, double>()));
			for each (ChokeDepReg cdr2 in allChokeDepRegs)
			{
				if (_pfMaps.distCDR.count(cdr2))
					_pfMaps.distCDR[cdr].insert(std::make_pair(cdr2, _pfMaps.distCDR[cdr2][cdr]));
				else
				{
					BWAPI::TilePosition tmp = cdrCenter(cdr);
					BWAPI::TilePosition tmp2 = cdrCenter(cdr2);
					if (!isWalkable(tmp) || rd.chokeDependantRegion[tmp.x()][tmp.y()] != cdr)
						tmp = findClosestWalkableSameCDR(tmp, cdr);
					if (!isWalkable(tmp2) || rd.chokeDependantRegion[tmp2.x()][tmp2.y()] != cdr2)
						tmp2 = findClosestWalkableSameCDR(tmp2, cdr2);
					_pfMaps.distCDR[cdr].insert(std::make_pair(cdr2,
						BWTA::getGroundDistance(TilePosition(tmp), TilePosition(tmp2))));
				}
			}
		}

		std::ofstream ofs(buf2, std::ios::binary);
		{
			boost::archive::binary_oarchive oa(ofs);
			oa << _pfMaps;
		}
	}

}

BWAPI::Position BWRepDump::regionsPFCenters(BWTA::Region* r)
{
	int tmp = hashRegionCenter(r);
	return Position(_pfMaps.regionsPFCenters[tmp].first, _pfMaps.regionsPFCenters[tmp].second);
}

void BWRepDump::displayChokeDependantRegions()
{
#ifdef __DEBUG_CDR_FULL__
	for (int x = 0; x < Broodwar->mapWidth(); x += 4)
	{
		for (int y = 0; y < Broodwar->mapHeight(); y += 2)
		{
			Broodwar->drawTextMap(x*TILE_SIZE+6, y*TILE_SIZE+2, "%d", rd.chokeDependantRegion[x][y]);
			if (BWTA::getRegion(TilePosition(x, y)) != NULL)
				Broodwar->drawTextMap(x*TILE_SIZE+6, y*TILE_SIZE+10, "%d", hashRegionCenter(BWTA::getRegion(TilePosition(x, y))));
		}
	}
#endif
	int n = 0;
	for each (ChokeDepReg cdr in allChokeDepRegs)
	{
		if (cdr != -1)
		{
			Broodwar->drawCircleMap(cdrCenter(cdr).x()*TILE_SIZE+TILE_SIZE/2, cdrCenter(cdr).y()*TILE_SIZE+TILE_SIZE/2, 10, Colors::Green, true);
			Broodwar->drawTextMap(cdrCenter(cdr).x()*TILE_SIZE+TILE_SIZE/2, cdrCenter(cdr).y()*TILE_SIZE+TILE_SIZE/2, "%d", n++);
		}
	}
	//for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
	//	Broodwar->drawBoxMap(c->getCenter().x() - 4, c->getCenter().y() - 4, c->getCenter().x() + 4, c->getCenter().y() + 4, Colors::Brown, true);
}

std::map<BWAPI::Player*, std::list<BWAPI::Unit*> > BWRepDump::getPlayerMilitaryUnits(const std::set<BWAPI::Unit*>& unitsAround)
{
	std::map<Player*, std::list<Unit*> > playerUnits;
	for each (Player* p in activePlayers)
		playerUnits.insert(make_pair(p, std::list<Unit*>()));
	for each (Unit* tmp in unitsAround)
	{
		if (tmp->getPlayer()->isNeutral() || tmp->getPlayer()->isObserver())
			continue;
		if (tmp->isGatheringGas() || tmp->isGatheringMinerals()
			|| tmp->isRepairing() 
			|| (tmp->getType().isBuilding() && !tmp->getType().canAttack())
			|| tmp->getType() == UnitTypes::Zerg_Larva
			|| tmp->getType() == UnitTypes::Zerg_Broodling
			|| tmp->getType() == UnitTypes::Zerg_Egg
			|| tmp->getType() == UnitTypes::Zerg_Cocoon
			//|| tmp->getType() == UnitTypes::Protoss_Interceptor
			|| tmp->getType() == UnitTypes::Protoss_Scarab
			|| tmp->getType() == UnitTypes::Terran_Nuclear_Missile)
			continue; // we take only military/aggressive units
		playerUnits[tmp->getPlayer()].push_back(tmp);
	}
	return playerUnits;
}

std::map<BWAPI::Player*, std::list<BWAPI::Unit*> > BWRepDump::getPlayerMilitaryUnitsNotInAttack(const std::set<BWAPI::Unit*>& unitsAround)
{
	std::map<Player*, std::list<Unit*> > playerUnits;
	for each (Player* p in activePlayers)
		playerUnits.insert(make_pair(p, std::list<Unit*>()));
	for each (Unit* tmp in unitsAround)
	{
		if (tmp->getPlayer()->isNeutral() || tmp->getPlayer()->isObserver())
			continue;
		if (tmp->isGatheringGas() || tmp->isGatheringMinerals()
			|| tmp->isRepairing() 
			|| (tmp->getType().isBuilding() && !tmp->getType().canAttack())
			|| tmp->getType() == UnitTypes::Zerg_Larva
			|| tmp->getType() == UnitTypes::Zerg_Broodling
			|| tmp->getType() == UnitTypes::Zerg_Egg
			|| tmp->getType() == UnitTypes::Zerg_Cocoon
			//|| tmp->getType() == UnitTypes::Protoss_Interceptor
			|| tmp->getType() == UnitTypes::Protoss_Scarab
			|| tmp->getType() == UnitTypes::Terran_Nuclear_Missile)
			continue; // we take only military/aggressive units
		bool found = false;
		for each (attack a in attacks)
		{
			if (a.battleUnits[tmp->getPlayer()].count(tmp))
			{
				found = true;
				break;
			}
		}
		if (!found)
			playerUnits[tmp->getPlayer()].push_back(tmp);
	}
	return playerUnits;
}

int countWorkingPeons(const std::set<Unit*>& units)
{
	int count = 0;
	for each (Unit* u in units)
	{
		if (u->getType().isWorker() && 
			(u->isGatheringGas() || u->isGatheringMinerals()
			|| u->isConstructing() || u->isRepairing()))
			++count;
	}
	return count;
}

int countDetectorUnits(const std::set<Unit*>& units)
{
	int count = 0;
	for each (Unit* u in units)
	{
		if (u->getType().isDetector())
			++count;
	}
	return count;
}

std::set<Unit*> getTownhalls(const std::set<Unit*>& units)
{
	std::set<Unit*> ret;
	for each (Unit* u in units)
	{
		if (u && u->exists() && u->getType().isResourceDepot())
			ret.insert(u);
	}
	return ret;
}

double scoreUnits(const std::list<Unit*>& eUnits)
{
	double minPrice = 0.0;
	double gasPrice = 0.0;
	double supply = 0.0;
	for each (Unit* u in eUnits)
	{
		UnitType ut = u->getType();
		minPrice += ut.mineralPrice();
		gasPrice += ut.gasPrice();
		supply += ut.supplyRequired();
	}
	return minPrice + (4.0/3)*gasPrice + 25*supply;
}

double scoreUnitsGround(const std::set<Unit*>& eUnits)
{
	double minPrice = 0.0;
	double gasPrice = 0.0;
	double supply = 0.0;
	for each (Unit* u in eUnits)
	{
		UnitType ut = u->getType();
		if (ut.groundWeapon() == WeaponTypes::None
			&& ut != UnitTypes::Protoss_High_Templar
			&& ut != UnitTypes::Protoss_Dark_Archon
			&& ut != UnitTypes::Zerg_Defiler
			&& ut != UnitTypes::Zerg_Queen
			&& ut != UnitTypes::Terran_Medic
			&& ut != UnitTypes::Terran_Science_Vessel
			&& ut != UnitTypes::Terran_Bunker)
			continue;
		minPrice += ut.mineralPrice();
		gasPrice += ut.gasPrice();
		supply += ut.supplyRequired();
		if (ut == UnitTypes::Terran_Siege_Tank_Siege_Mode) // a small boost for sieged tanks and lurkers
			supply += ut.supplyRequired();
	}
	return minPrice + (4.0/3)*gasPrice + 25*supply;
}

double scoreUnitsAir(const std::set<Unit*>& eUnits)
{
	double minPrice = 0.0;
	double gasPrice = 0.0;
	double supply = 0.0;
	for each (Unit* u in eUnits)
	{
		UnitType ut = u->getType();
		if (ut.airWeapon() == WeaponTypes::None
			&& ut != UnitTypes::Protoss_High_Templar
			&& ut != UnitTypes::Protoss_Dark_Archon
			&& ut != UnitTypes::Zerg_Defiler
			&& ut != UnitTypes::Zerg_Queen
			&& ut != UnitTypes::Terran_Medic
			&& ut != UnitTypes::Terran_Science_Vessel
			&& ut != UnitTypes::Terran_Bunker)
			continue;
		minPrice += ut.mineralPrice();
		gasPrice += ut.gasPrice();
		supply += ut.supplyRequired();
	}
	return minPrice + (4.0/3)*gasPrice + 25*supply;
}

struct heuristics_analyser
{
	BWAPI::Player* p;
	BWRepDump* bwrd;
	std::map<BWTA::Region*, std::set<Unit*> > unitsByRegion;
	std::map<ChokeDepReg, std::set<Unit*> > unitsByCDR;
	std::map<BWTA::Region*, double> ecoRegion;
	std::map<ChokeDepReg, double> ecoCDR;
	std::map<BWTA::Region*, double> tacRegion;
	std::map<ChokeDepReg, double> tacCDR;
	std::set<ChokeDepReg> cdrSet;
	std::set<Unit*> emptyUnitsSet;

	heuristics_analyser(Player* pl, BWRepDump* bwrepdump)
		: p(pl), bwrd(bwrepdump)
	{
		for each (Unit* u in p->getUnits())
		{
			TilePosition tp(u->getTilePosition());
			BWTA::Region* r = BWTA::getRegion(tp);
			if (unitsByRegion.count(r))
				unitsByRegion[r].insert(u);
			else
			{
				std::set<Unit*> tmpSet;
				tmpSet.insert(u);
				unitsByRegion.insert(make_pair(r, tmpSet));
			}
			ChokeDepReg cdr = bwrepdump->rd.chokeDependantRegion[tp.x()][tp.y()];
			cdrSet.insert(cdr);
			if (unitsByCDR.count(cdr))
				unitsByCDR[cdr].insert(u);
			else
			{
				std::set<Unit*> tmpSet;
				tmpSet.insert(u);
				unitsByCDR.insert(make_pair(cdr, tmpSet));
			}
		}
	}

	const std::set<Unit*>& getUnitsCDRegion(ChokeDepReg cdr)
	{
		if (unitsByCDR.count(cdr))
			return unitsByCDR[cdr];
		return emptyUnitsSet;
	}

	const std::set<Unit*>& getUnitsRegion(BWTA::Region* r)
	{
		if (unitsByRegion.count(r))
			return unitsByRegion[r];
		return emptyUnitsSet;
	}

	// ground forces
	double scoreGround(ChokeDepReg cdr)
	{
		return scoreUnitsGround(getUnitsCDRegion(cdr));
	}
	double scoreGround(BWTA::Region* r)
	{
		return scoreUnitsGround(getUnitsRegion(r));
	}

	// air forces
	double scoreAir(ChokeDepReg cdr)
	{
		return scoreUnitsAir(getUnitsCDRegion(cdr));
	}
	double scoreAir(BWTA::Region* r)
	{
		return scoreUnitsAir(getUnitsRegion(r));
	}

	// detection
	double scoreDetect(ChokeDepReg cdr)
	{
		return countDetectorUnits(getUnitsCDRegion(cdr));
	}
	double scoreDetect(BWTA::Region* r)
	{
		return countDetectorUnits(getUnitsRegion(r));
	}

	// economy
	double economicImportance(BWTA::Region* r)
	{
		if (ecoRegion.empty())
		{
			double s = 0.0;
			for each (BWTA::Region* rr in BWTA::getRegions())
			{
				int c = countWorkingPeons(getUnitsRegion(rr));
				ecoRegion.insert(std::make_pair(rr, c));
				s += c;
			}
			for each (BWTA::Region* rr in BWTA::getRegions())
				ecoRegion[rr] = ecoRegion[rr] / s;
		}
		return ecoRegion[r];
	}
	double economicImportance(ChokeDepReg cdr)
	{
		if (ecoCDR.empty())
		{
			double s = 0.0;
			for each (ChokeDepReg cdrr in cdrSet)
			{
				int c = countWorkingPeons(getUnitsCDRegion(cdrr));
				ecoCDR.insert(std::make_pair(cdrr, c));
				s += c;
			}
			for each (ChokeDepReg cdrr in cdrSet)
				ecoCDR[cdrr] = ecoCDR[cdrr] / s;
		}
		return ecoCDR[cdr];
	}

	/// tactical importance = normalized relative importance of sum of the square distances
	/// from this region to the baseS of the player + from this region to the mean position of his army
	double tacticalImportance(BWTA::Region* r)
	{
		if (tacRegion.count(r))
			return tacRegion[r];
		std::set<Unit*> ths = getTownhalls(p->getUnits());
		std::list<Unit*> army = bwrd->getPlayerMilitaryUnits(p->getUnits())[p];
		Position mean(0, 0);
		for each (Unit* u in army)
			mean += u->getPosition();
		mean = Position(mean.x()/army.size(), mean.y()/army.size());
		TilePosition meanWalkable(mean);
		if (!bwrd->isWalkable(meanWalkable))
			meanWalkable = bwrd->findClosestWalkable(meanWalkable);
		BWTA::Region* meanArmyReg = BWTA::getRegion(mean);
		if (meanArmyReg == NULL)
			meanArmyReg = findClosestRegion(meanWalkable);
		double s = 0.0;
		for each (BWTA::Region* rr in BWTA::getRegions())
		{
			tacRegion.insert(std::make_pair(rr, 0.0));
			for each (Unit* th in ths)
			{
				BWTA::Region* thr = BWTA::getRegion(th->getTilePosition());
				if (thr != NULL && thr->getReachableRegions().count(rr))
				{
					double tmp = bwrd->_pfMaps.distRegions[hashRegionCenter(rr)][hashRegionCenter(thr)];
					tacRegion[rr] += tmp*tmp;
				}
				else // if rr is an island, it will be penalized a lot
					tacRegion[rr] += Broodwar->mapWidth() * Broodwar->mapHeight();
			}
			double tmp = 0.0;
			if (rr->getReachableRegions().count(meanArmyReg))
				tmp = bwrd->_pfMaps.distRegions[hashRegionCenter(rr)][hashRegionCenter(meanArmyReg)];
			else
				tmp = bwrd->_pfMaps.distRegions[hashRegionCenter(rr)][hashRegionCenter(bwrd->findClosestReachableRegion(meanArmyReg, rr))];
			tacRegion[rr] += tmp*tmp * ARMY_TACTICAL_IMPORTANCE;
			s += tacRegion[rr];
		}
		for (std::map<BWTA::Region*, double>::iterator it = tacRegion.begin();
			it != tacRegion.end(); ++it)
			tacRegion[it->first] = s - it->second;
		return tacRegion[r];
	}

	double tacticalImportance(ChokeDepReg cdr)
	{
		if (tacCDR.count(cdr))
			return tacCDR[cdr];
		std::set<Unit*> ths = getTownhalls(p->getUnits());
		std::list<Unit*> army = bwrd->getPlayerMilitaryUnits(p->getUnits())[p];
		Position mean(0, 0);
		for each (Unit* u in army)
			mean += u->getPosition();
		mean = Position(mean.x()/army.size(), mean.y()/army.size());
		TilePosition m(mean);
		if (!bwrd->isWalkable(m))
			m = bwrd->findClosestWalkable(m);
		ChokeDepReg meanArmyCDR = bwrd->rd.chokeDependantRegion[m.x()][m.y()];
		if (meanArmyCDR == -1)
			meanArmyCDR = bwrd->findClosestCDR(m);
		double s = 0.0;
		for each (ChokeDepReg cdrr in bwrd->allChokeDepRegs)
		{
			tacCDR.insert(std::make_pair(cdrr, 0.0));
			for each (Unit* th in ths)
			{
				ChokeDepReg thcdr = bwrd->rd.chokeDependantRegion[th->getTilePosition().x()][th->getTilePosition().y()];
				if (thcdr != -1 && bwrd->_pfMaps.distCDR[thcdr][cdrr] >= 0.0) // is reachable
				{
					double tmp = bwrd->_pfMaps.distCDR[thcdr][cdrr];
					tacCDR[cdrr] += tmp*tmp;
				}
				else // if rr is an island, it will be penalized a lot
					tacCDR[cdrr] += Broodwar->mapWidth() * Broodwar->mapHeight();
			}
			double tmp = 0.0;
			if (bwrd->_pfMaps.distCDR[cdrr][meanArmyCDR] >= 0.0) // is reachable
				tmp = bwrd->_pfMaps.distCDR[cdrr][meanArmyCDR];
			else
				tmp = bwrd->_pfMaps.distCDR[cdrr][bwrd->findClosestReachableCDR(meanArmyCDR, cdrr)];
			tacCDR[cdrr] += tmp*tmp * ARMY_TACTICAL_IMPORTANCE;
			s += tacCDR[cdrr];
		}
		for (std::map<ChokeDepReg, double>::iterator it = tacCDR.begin();
			it != tacCDR.end(); ++it)
			tacCDR[it->first] = s - it->second;
		return tacCDR[cdr];
	}
};

void attack::computeScores(BWRepDump* bwrd)
{
	if (bwrd == NULL || defender == NULL || defender->isObserver() || defender->isNeutral()
		|| !initPosition.isValid())
	{
		scoreGroundCDR = -1.0;
		scoreGroundRegion = -1.0;
		scoreAirCDR = -1.0;
		scoreAirRegion = -1.0;
		scoreDetectCDR = -1.0;
		scoreDetectRegion = -1.0;
		economicImportanceCDR = -1.0;
		economicImportanceRegion = -1.0;
		tacticalImportanceCDR = -1.0;
		tacticalImportanceRegion = -1.0;
		return;
	}
	heuristics_analyser ha(defender, bwrd);
	TilePosition tp(initPosition);
	if (!bwrd->isWalkable(tp))
		tp = bwrd->findClosestWalkable(tp);
	BWTA::Region* r = BWTA::getRegion(tp);
	if (r == NULL)
		r = findClosestRegion(tp);
	ChokeDepReg cdr = bwrd->rd.chokeDependantRegion[tp.x()][tp.y()];
	if (cdr == -1)
		cdr = bwrd->findClosestCDR(tp);
	scoreGroundCDR = ha.scoreGround(cdr);
	scoreGroundRegion = ha.scoreGround(r);
	scoreAirCDR = ha.scoreAir(cdr);
	scoreAirRegion = ha.scoreAir(r);
	scoreDetectCDR = ha.scoreDetect(cdr);
	scoreDetectRegion = ha.scoreDetect(r);
	economicImportanceCDR = ha.economicImportance(cdr);
	economicImportanceRegion = ha.economicImportance(r);
	tacticalImportanceCDR = ha.tacticalImportance(cdr);
	tacticalImportanceRegion = ha.tacticalImportance(r);
}

bool BWRepDump::isWalkable(const TilePosition& tp)
{
	return _lowResWalkability[tp.x() + tp.y()*Broodwar->mapWidth()];
}

void BWRepDump::onStart()
{
	// Enable some cheat flags
	//Broodwar->enableFlag(Flag::UserInput);
	// Uncomment to enable complete map information
	//Broodwar->enableFlag(Flag::CompleteMapInformation);

	//read map information into BWTA so terrain analysis can be done in another thread
	BWTA::readMap();
	BWTA::analyze();
	analyzed=false;
	analysis_just_finished=false;
	_lowResWalkability = new bool[Broodwar->mapWidth() * Broodwar->mapHeight()]; // Build Tiles resolution
	for (int x = 0; x < Broodwar->mapWidth(); ++x)
		for (int y = 0; y < Broodwar->mapHeight(); ++y)
		{
			_lowResWalkability[x + y*Broodwar->mapWidth()] = true;
			for (int i = 0; i < 4; ++i)
				for (int j = 0; j < 4; ++j)
					_lowResWalkability[x + y*Broodwar->mapWidth()] &= Broodwar->isWalkable(x*4+i, y*4+j);
		}
	this->createChokeDependantRegions();

	show_bullets=false;
	show_visibility_data=false;
	unitDestroyedThisTurn=false;

	for each (Player* p in activePlayers)
	{
		lastDropOrderByPlayer.insert(std::make_pair(p, - SECONDS_SINCE_LAST_ATTACK));
	}

	if (Broodwar->isReplay())
	{
		Broodwar->setLocalSpeed(0);
		//Broodwar->setLatCom(false);
		//Broodwar->setFrameSkip(0);
		std::ofstream myfile;
		std::string filepath = Broodwar->mapPathName() + ".rgd";
		std::string locationfilepath = Broodwar->mapPathName() + ".rld";
		std::string ordersfilepath = Broodwar->mapPathName() + ".rod";
		replayDat.open(filepath.c_str());
		replayLocationDat.open(locationfilepath.c_str());
		std::string tmpColumns("Regions,");
		for each (std::pair<int, std::map<int, double> > regDists in _pfMaps.distRegions)
		{
			tmpColumns += convertInt(regDists.first) + ",";
		}
		tmpColumns[tmpColumns.size()-1] = '\n';
		replayLocationDat << tmpColumns;
		for (std::map<int, std::map<int, double> >::const_reverse_iterator it = _pfMaps.distRegions.rbegin();
			it != _pfMaps.distRegions.rend(); ++it)
		{
			replayLocationDat << it->first;
			for each (std::pair<int, double> rD in it->second)
			{
				if (it->first == rD.first)
					break; // line == column, symmetrical => useless to write from now on
				replayLocationDat << "," << (int)rD.second;
			}
			replayLocationDat << "\n";
		}
		tmpColumns = "ChokeDepReg,";
		for each (std::pair<int, std::map<int, double> > cdrDists in _pfMaps.distCDR)
		{
			tmpColumns += convertInt(cdrDists.first) + ",";
		}
		tmpColumns[tmpColumns.size()-1] = '\n';
		replayLocationDat << tmpColumns;
		for (std::map<int, std::map<int, double> >::const_reverse_iterator it = _pfMaps.distCDR.rbegin();
			it != _pfMaps.distCDR.rend(); ++it)
		{
			replayLocationDat << it->first;
			for each (std::pair<int, double> cdrD in it->second)
			{
				if (it->first == cdrD.first)
					break; // line == column, symmetrical => useless to write from now on
				replayLocationDat << "," << (int)cdrD.second;
			}
			replayLocationDat << "\n";
		}
		replayOrdersDat.open(ordersfilepath.c_str());
		replayLocationDat << "[Replay Start]\n";
		replayDat << "[Replay Start]\n" << std::fixed << std::setprecision(4);
		//myfile.close();
		Broodwar->printf("RepPath: %s", Broodwar->mapPathName().c_str());
		replayDat << "RepPath: " << Broodwar->mapPathName() << "\n"; 
		Broodwar->printf("MapName: %s", Broodwar->mapName().c_str());
		replayDat << "MapName: " << Broodwar->mapName() << "\n";
		Broodwar->printf("NumStartPositions: %d", Broodwar->getStartLocations().size());
		replayDat << "NumStartPositions: " << Broodwar->getStartLocations().size() << "\n";
		Broodwar->printf("The following players are in this replay:");
		replayDat << "The following players are in this replay:\n";
		for(std::set<Player*>::iterator p=Broodwar->getPlayers().begin();p!=Broodwar->getPlayers().end();p++)
		{
			if (!(*p)->getUnits().empty() && !(*p)->isNeutral())
			{
				int startloc = -1;
				bool foundstart = false;
				for(std::set<TilePosition>::iterator it = Broodwar->getStartLocations().begin(); it != Broodwar->getStartLocations().end() && foundstart == false; it++)
				{
					startloc++;
					if((*p)->getStartLocation() == (*it))
			  {
				  foundstart = true;
			  }
				}

				Broodwar->printf("%s, %s, %d",(*p)->getName().c_str(),(*p)->getRace().getName().c_str(), startloc);
				replayDat << (*p)->getID() << ", " << (*p)->getName() << ", " << (*p)->getRace().getName() << ", " << startloc << "\n";
				this->activePlayers.insert(*p);
			}
		}
		replayDat << "Begin replay data:\n";
	}
}

void BWRepDump::onEnd(bool isWinner)
{
	for (std::list<attack>::iterator it = attacks.begin();
		it != attacks.end(); )
	{
		endAttack(it, NULL, NULL);
		attacks.erase(it++);
	}
	this->replayDat << "[EndGame]\n";
	this->replayDat.close();
	this->replayLocationDat.close();
	this->replayOrdersDat.close();
    delete [] _lowResWalkability;
	if (isWinner)
	{
		//log win to file
	}
}

void BWRepDump::handleVisionEvents()
{
	for each(Player * p in this->activePlayers)
	{
		for each(Unit* u in p->getUnits())
		{
			checkVision(u);
		}
	}
}

void BWRepDump::checkVision(Unit* u)
{
	for each(Player * p in this->activePlayers)
	{
		if(p != u->getPlayer())
		{
			for each(std::pair<Unit*, UnitType> visionPair in this->unseenUnits[u->getPlayer()])
			{
				Unit* visionTarget = visionPair.first;
				int sight = u->getType().sightRange();
				sight = sight * sight;
				int ux = u->getPosition().x();
				int uy = u->getPosition().y();
				int tx = visionTarget->getPosition().x();
				int ty = visionTarget->getPosition().y();
				int dx = ux - tx;
				int dy = uy - ty;
				int dist = (dx * dx) + (dy * dy);
				if(dist <= sight)
				{
					if(this->seenThisTurn[u->getPlayer()].find(visionTarget) == this->seenThisTurn[u->getPlayer()].end())
					{
						//Broodwar->printf("Player %i Discovered Unit: %s [%i]", p->getID(), visionTarget->getType().getName().c_str());
						this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",Discovered," << visionTarget->getID() << "," << visionTarget->getType().getName() << "\n";
						this->seenThisTurn[u->getPlayer()].insert(visionTarget);
					}
					//this->unseenUnits[u->getPlayer()].erase(std::pair<Unit*, UnitType>(visionTarget, visionTarget->getType()));
					//Vision Event.
				}
			}
		}
	}
}

void BWRepDump::handleTechEvents()
{
	for each(Player * p in this->activePlayers)
	{
		std::map<Player*, std::list<TechType>>::iterator currentTechIt = this->listCurrentlyResearching.find(p);
		for each (BWAPI::TechType currentResearching in BWAPI::TechTypes::allTechTypes())
		{
			std::list<TechType>* techListPtr;
			if(currentTechIt != this->listCurrentlyResearching.end())
			{
				techListPtr = &((*currentTechIt).second);
			}
			else
			{
				this->listCurrentlyResearching[p] = std::list<TechType>();
				techListPtr = &this->listCurrentlyResearching[p];
			}
			std::list<TechType> techList = (*techListPtr);

			bool wasResearching = false;
			for each (BWAPI::TechType lastFrameResearching in techList)
			{
				if(lastFrameResearching.getID() == currentResearching.getID())
				{
					wasResearching = true;
					break;
				}
			}
			if(p->isResearching(currentResearching))
			{
				if(!wasResearching)
				{
					this->listCurrentlyResearching[p].push_back(currentResearching);
					this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",StartResearch," << currentResearching.getName() << "\n";
					//Event - researching new tech
				}
			}
			else
			{
				if(wasResearching)
				{
					if(p->hasResearched(currentResearching))
					{
						this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",FinishResearch," << currentResearching.getName() << "\n";
						if(this->listResearched.count(p) > 0)
						{
							this->listResearched[p].push_back(currentResearching);
						}
						this->listCurrentlyResearching[p].remove(currentResearching);
						//Event - research complete
					}
					else
					{
						this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",CancelResearch," << currentResearching.getName() << "\n";
						this->listCurrentlyResearching[p].remove(currentResearching);
						//Event - research cancelled
					}
				}
			}

		}
		std::map<Player*, std::list<UpgradeType>>::iterator currentUpgradeIt = this->listCurrentlyUpgrading.find(p);
		for each (BWAPI::UpgradeType checkedUpgrade in BWAPI::UpgradeTypes::allUpgradeTypes())
		{
			std::list<UpgradeType>* upgradeListPtr;
			if(currentUpgradeIt != this->listCurrentlyUpgrading.end())
			{
				upgradeListPtr = &((*currentUpgradeIt).second);
			}
			else
			{
				this->listCurrentlyUpgrading[p] = std::list<UpgradeType>();
				upgradeListPtr = &this->listCurrentlyUpgrading[p];
			}
			std::list<UpgradeType> upgradeList = (*upgradeListPtr);


			bool wasResearching = false;
			for each (BWAPI::UpgradeType lastFrameUpgrading in upgradeList)
			{
				if(lastFrameUpgrading.getID() == checkedUpgrade.getID())
				{
					wasResearching = true;
					break;
				}
			}
			if(p->isUpgrading(checkedUpgrade))
			{
				if(!wasResearching)
				{
					this->listCurrentlyUpgrading[p].push_back(checkedUpgrade);
					this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",StartUpgrade," << checkedUpgrade.getName() << "," << (p->getUpgradeLevel(checkedUpgrade) + 1) << "\n";
					//Event - researching new upgrade
				}
			}
			else
			{
				if(wasResearching)
				{
					int lastlevel = 0;
					for each (std::pair<UpgradeType, int> upgradePair in this->listUpgraded[p])
					{
						if(upgradePair.first == checkedUpgrade && upgradePair.second > lastlevel)
						{
							lastlevel = upgradePair.second;
						}
					}
					if(p->getUpgradeLevel(checkedUpgrade) > lastlevel)
					{
						this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",FinishUpgrade," << checkedUpgrade.getName() << "," << p->getUpgradeLevel(checkedUpgrade) << "\n";
						if(this->listUpgraded.count(p) > 0)
						{
							this->listUpgraded[p].push_back(std::pair<UpgradeType, int>(checkedUpgrade, p->getUpgradeLevel(checkedUpgrade)));
						}
						this->listCurrentlyUpgrading[p].remove(checkedUpgrade);
						//Event - upgrade complete
					}
					else
					{
						this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",CancelUpgrade," << checkedUpgrade.getName() << "," << (p->getUpgradeLevel(checkedUpgrade) + 1) << "\n";
						this->listCurrentlyUpgrading[p].remove(checkedUpgrade);
						//Event - upgrade cancelled
					}
				}
			}
		}
	}
}

void BWRepDump::updateAttacks()
{
	if (attacks.size() > 20)
		Broodwar->printf("Bug, attacks is bigger than 20");
	for (std::list<attack>::iterator it = attacks.begin();
		it != attacks.end(); )
	{
#ifdef __DEBUG_OUTPUT__
		Broodwar->drawCircleMap(it->position.x(), it->position.y(), static_cast<int>(it->radius), Colors::Red);
		Broodwar->drawBoxMap(it->position.x() - 6, it->position.y() - 6, it->position.x() + 6, it->position.y() + 6, Colors::Red, true);
		int i = 0;
		for each (AttackType at in it->types)
		{
			Broodwar->drawTextMap(max(0, it->position.x() - 2*TILE_SIZE), max(0, it->position.y() - TILE_SIZE + (i * 16)), 
				"%s on %s (race %s)",
				attackTypeToStr(at).c_str(), it->defender->getName().c_str(), it->defender->getRace().c_str());
			++i;
		}
#endif
		std::map<Player*, std::list<Unit*> > playerUnits = getPlayerMilitaryUnits(
			Broodwar->getUnitsInRadius(it->position, static_cast<int>(it->radius))
			);
		for each (std::pair<Player*, std::list<Unit*> > pp in playerUnits)
		{
			if (!it->unitTypes.count(pp.first))
				it->unitTypes.insert(std::make_pair(pp.first, std::map<BWAPI::UnitType, int>()));
			if (!it->battleUnits.count(pp.first))
				it->battleUnits.insert(std::make_pair(pp.first, std::set<BWAPI::Unit*>()));
			for each (Unit* uu in pp.second)
			{
				UnitType ut = uu->getType();
				if ((ut.canAttack() && !ut.isWorker()) // non workers non casters (counts interceptors)
					|| uu->isAttacking() // attacking workers
					|| ut == UnitTypes::Protoss_High_Templar || ut == UnitTypes::Protoss_Dark_Archon || ut == UnitTypes::Protoss_Observer || ut == UnitTypes::Protoss_Shuttle || ut == UnitTypes::Protoss_Carrier
					|| ut == UnitTypes::Zerg_Defiler || ut == UnitTypes::Zerg_Queen || ut == UnitTypes::Zerg_Lurker || ut == UnitTypes::Zerg_Overlord
					|| ut == UnitTypes::Terran_Medic|| ut == UnitTypes::Terran_Dropship || ut == UnitTypes::Terran_Science_Vessel)
					it->addUnit(uu);
				if (ut.isWorker())
					it->workers[uu->getPlayer()].insert(uu);
			}
		}
		// TODO modify, currently 2 players (1v1) only
		BWAPI::Player* winner = NULL;
		BWAPI::Player* loser = NULL;
		BWAPI::Player* offender = NULL;
		for each (std::pair<BWAPI::Player*, std::map<BWAPI::UnitType, int> > p in it->unitTypes)
		{
			if (p.first != it->defender)
				offender = p.first;
		}
		BWAPI::Position pos = BWAPI::Position(0, 0);
		int attackers = 0;
		std::list<BWAPI::Unit*> tmp = playerUnits[offender];
		tmp.insert(tmp.end(), playerUnits[it->defender].begin(), playerUnits[it->defender].end());
		for each (BWAPI::Unit* u in tmp)
		{
			if (u && u->exists() && (u->isAttacking() || u->isUnderAttack()))
			{
				pos += u->getPosition();
				++attackers;
			}
		}
		if (attackers > 0)
		{
			it->position = BWAPI::Position(pos.x() / attackers, pos.y() / attackers);
			for each (BWAPI::Unit* u in tmp)
			{
				double range_and_dist = u->getDistance(it->position) + 
					max(u->getType().groundWeapon().maxRange(),
					u->getType().airWeapon().maxRange());
				if ((u->isAttacking() || u->isUnderAttack())
					&& u->getDistance(it->position) > it->radius)
					it->radius = u->getDistance(it->position);
			}
			if (it->radius < MIN_ATTACK_RADIUS)
				it->radius = MIN_ATTACK_RADIUS;
			if (it->radius > MAX_ATTACK_RADIUS) 
				it->radius = MAX_ATTACK_RADIUS;
			it->frame = Broodwar->getFrameCount();
			++it;
		}
		else if (Broodwar->getFrameCount() - it->frame >= 24*SECONDS_SINCE_LAST_ATTACK)
		{			
			// Attack is finished, who won the battle ? (this is not essential, as we output enough data to recompute it)
			std::map<BWAPI::Player*, std::list<BWAPI::Unit*> > aliveUnits;
			for each (std::pair<Player*, std::set<Unit*> > p in it->battleUnits)
			{
				aliveUnits.insert(std::make_pair(p.first, std::list<Unit*>()));
				for each (Unit* u in p.second)
				{
					if (u && u->exists())
						aliveUnits[p.first].push_back(u);
				}
			}
			//if (scoreUnits(playerUnits[it->defender]) * OFFENDER_WIN_COEFFICIENT < scoreUnits(playerUnits[offender]))
			if (scoreUnits(aliveUnits[it->defender]) * OFFENDER_WIN_COEFFICIENT < scoreUnits(aliveUnits[offender]))
			{
				winner = offender; 
				loser = it->defender;
			}
			else
			{
				loser = offender; 
				winner = it->defender;
			}
			endAttack(it, loser, winner);
			// if the currently examined attack is too old and too far,
			// remove it (no longer a real attack)
			attacks.erase(it++);
		}
		else 
			++it;
	}
}

void BWRepDump::endAttack(std::list<attack>::iterator it, BWAPI::Player* loser, BWAPI::Player* winner)
{
#ifdef __DEBUG_OUTPUT__
	if (winner != NULL && loser != NULL)
	{
		Broodwar->printf("Player %s (race %s) won the battle against player %s (race %s) at Position (%d,%d)",
			winner->getName().c_str(), winner->getRace().c_str(), 
			loser->getName().c_str(), loser->getRace().c_str(),
			it->position.x(), it->position.y());
	}
#endif
	std::string tmpAttackType("(");
    if (it->types.empty())
        tmpAttackType += ")";
    else
    {
        for each (AttackType t in it->types)
            tmpAttackType += attackTypeToStr(t) + ",";
        tmpAttackType[tmpAttackType.size()-1] = ')';
    }
	std::string tmpUnitTypes("{");
	for each (std::pair<BWAPI::Player*, std::map<BWAPI::UnitType, int> > put in it->unitTypes)
	{
		std::string tmpUnitTypesPlayer(":{");
		for each (std::pair<BWAPI::UnitType, int> pp in put.second)
			tmpUnitTypesPlayer += pp.first.getName() + ":" + convertInt(pp.second) + ",";
		if (tmpUnitTypesPlayer[tmpUnitTypesPlayer.size()-1] == '{')
			tmpUnitTypesPlayer += "}";
		else
			tmpUnitTypesPlayer[tmpUnitTypesPlayer.size()-1] = '}';
		tmpUnitTypes += convertInt(put.first->getID()) + tmpUnitTypesPlayer + ",";
	}
	if (tmpUnitTypes[tmpUnitTypes.size()-1] == '{')
		tmpUnitTypes += "}";
	else
		tmpUnitTypes[tmpUnitTypes.size()-1] = '}';
	std::string tmpUnitTypesEnd("{");
	for each (std::pair<BWAPI::Player*, std::set<BWAPI::Unit*> > pu in it->battleUnits)
	{
		std::map<BWAPI::UnitType, int> tmp;
		for each (BWAPI::Unit* u in pu.second)
		{
			if (!u->exists())
				continue;
			if (tmp.count(u->getType()))
				tmp[u->getType()] += 1;
			else
				tmp.insert(std::make_pair(u->getType(), 1));
		}
		std::string tmpUnitTypesPlayer(":{");
		for each (std::pair<BWAPI::UnitType, int> pp in tmp)
			tmpUnitTypesPlayer += pp.first.getName() + ":" + convertInt(pp.second) + ",";
		if (tmpUnitTypesPlayer[tmpUnitTypesPlayer.size()-1] == '{')
			tmpUnitTypesPlayer += "}";
		else
			tmpUnitTypesPlayer[tmpUnitTypesPlayer.size()-1] = '}';
		tmpUnitTypesEnd += convertInt(pu.first->getID()) + tmpUnitTypesPlayer + ",";
	}
	if (tmpUnitTypesEnd[tmpUnitTypesEnd.size()-1] == '{')
		tmpUnitTypesEnd += "}";
	else
		tmpUnitTypesEnd[tmpUnitTypesEnd.size()-1] = '}';
	std::string tmpWorkersDead("{");
	for each (std::pair<BWAPI::Player*, std::set<BWAPI::Unit*> > pu in it->workers)
	{
		int c = 0;
		tmpWorkersDead += convertInt(pu.first->getID()) + ":";
		for each (Unit* u in pu.second)
		{
			if (u && !u->exists())
				++c;
		}
		tmpWorkersDead += convertInt(c) + ",";
	}
	tmpWorkersDead[tmpWorkersDead.size()-1] = '}';
	/// $firstFrame, $defenderId, isAttacked, $attackType, 
	/// ($initPosition.x, $initPosition.y), {$playerId:{$type:$maxNumberInvolved}}, 
	/// ($scoreGroundCDR, $scoreGroundRegion, $scoreAirCDR, $scoreAirRegion, $scoreDetectCDR, $scoreDetectRegion,
	/// $ecoImportanceCDR, $ecoImportanceRegion, $tactImportanceCDR, $tactImportanceRegion),
	/// {$playerId:{$type:$numberAtEnd}}, ($lastPosition.x, $lastPosition.y),
	/// {$playerId:$nbWorkersDead},$lastFrame, $winnerId

    BWAPI::TilePosition tmptp(it->initPosition);
	replayDat << it->firstFrame << "," << it->defender->getID() << ",IsAttacked," << tmpAttackType << ",("
		<< it->initPosition.x() << "," << it->initPosition.y() << ")," 
        << rd.chokeDependantRegion[tmptp.x()][tmptp.y()] << ","
        << hashRegionCenter(BWTA::getRegion(tmptp)) << ","
        << tmpUnitTypes << ",("
		<< it->scoreGroundCDR << "," << it->scoreGroundRegion << ","
		<< it->scoreAirCDR << "," << it->scoreAirRegion << ","
		<< it->scoreDetectCDR << "," << it->scoreDetectRegion << ","
		<< it->economicImportanceCDR << "," << it->economicImportanceRegion << ","
		<< it->tacticalImportanceCDR << "," << it->tacticalImportanceRegion
		<< ")," << tmpUnitTypesEnd << ",(" << it->position.x() << "," << it->position.y() << ")," 
		<< tmpWorkersDead << "," << Broodwar->getFrameCount();
	if (winner != NULL)
		replayDat << ",winner:" << winner->getID() << "\n";
	else
		replayDat << "\n";
	replayDat.flush();
}

void BWRepDump::onFrame()
{
#ifdef __DEBUG_CDR_FULL__
	for (int x = 0; x < Broodwar->mapWidth(); ++x)
		for (int y = 0; y < Broodwar->mapHeight(); ++y)
		{
			if (!_lowResWalkability[x + y*Broodwar->mapWidth()])
				Broodwar->drawBoxMap(32*x+2, 32*y+2, 32*x+30, 32*y+30, Colors::Red);
		}
	for each (ChokeDepReg cdr in allChokeDepRegs)
	{
		Position p(cdrCenter(cdr));
		Broodwar->drawBoxMap(p.x()+2, p.y()+2, p.x()+30, p.y()+30, Colors::Blue);
	}
#endif
	//  if (show_visibility_data)
	//    drawVisibilityData();

	//  if (show_bullets)
	//    drawBullets();

	if (Broodwar->isReplay())
	{
#ifdef REP_TIME_LIMIT
		if (Broodwar->getFrameCount() > 24*60*REP_TIME_LIMIT)
			Broodwar->leaveGame();
#endif

		/// Update attacks
		updateAttacks();

#ifdef __DEBUG_CDR__
		this->displayChokeDependantRegions();
		for(std::set<BWTA::Region*>::const_iterator r=BWTA::getRegions().begin();r!=BWTA::getRegions().end();r++)
		{
			BWTA::Polygon p=(*r)->getPolygon();
			for(int j=0;j<(int)p.size();j++)
			{
				Position point1=p[j];
				Position point2=p[(j+1) % p.size()];
				Broodwar->drawLine(CoordinateType::Map,point1.x(),point1.y(),point2.x(),point2.y(),Colors::Green);
			}
		}
		for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
			Broodwar->drawLineMap(c->getSides().first.x(), c->getSides().first.y(), c->getSides().second.x(), c->getSides().second.y(), Colors::Red);
		/*for each (std::pair<int, std::map<int, double> > regDists in _pfMaps.distRegions)
		{
			BWAPI::TilePosition tp = cdrCenter(regDists.first);
			BWAPI::Position p(tp);
			for each (std::pair<int, double> rD in regDists.second)
			{
				BWAPI::TilePosition tp2 = cdrCenter(rD.first);
				BWAPI::Position p2(tp2);
				Broodwar->drawLineMap(p.x(), p.y(), p2.x(), p2.y(), Colors::Blue);
				Broodwar->drawTextMap((p.x()+p2.x())/2, (p.y()+p2.y())/2, "%f", rD.second);
			}
		}*/
		for each (std::pair<int, std::map<int, double> > cdrDists in _pfMaps.distCDR)
		{
			BWAPI::TilePosition tp = cdrCenter(cdrDists.first);
			BWAPI::Position p(tp);
			for each (std::pair<int, double> rD in cdrDists.second)
			{
				if (rD.second >= 0.0)
					continue;
				BWAPI::TilePosition tp2 = cdrCenter(rD.first);
				BWAPI::Position p2(tp2);
				Broodwar->drawLineMap(p.x(), p.y(), p2.x(), p2.y(), Colors::Blue);
				Broodwar->drawTextMap((p.x()+p2.x())/2, (p.y()+p2.y())/2, "%f", rD.second);
			}
		}
#endif
#ifdef __DEBUG_OUTPUT__
		char mousePos[100];
		sprintf_s(mousePos, "%d, %d", 
			Broodwar->getScreenPosition().x() + Broodwar->getMousePosition().x(), 
			Broodwar->getScreenPosition().y() + Broodwar->getMousePosition().y());
		Broodwar->drawTextMouse(12, 0, mousePos);
		char mouseTilePos[100];
		sprintf_s(mouseTilePos, "%d, %d", 
			(Broodwar->getScreenPosition().x() + Broodwar->getMousePosition().x())/32, 
			(Broodwar->getScreenPosition().y() + Broodwar->getMousePosition().y())/32);
		Broodwar->drawTextMouse(12, 16, mouseTilePos);
#endif

		int resourcesRefreshSpeed = 25;
		if(Broodwar->getFrameCount() % resourcesRefreshSpeed == 0)
		{
			for each (Player* p in this->activePlayers)
			{
				this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",R," << p->minerals() << "," << p->gas() << "," << p->gatheredMinerals() << "," << p->gatheredGas() << ","  << p->supplyUsed() << "," << p->supplyTotal() << "\n";
			}
		}
		int refreshSpeed = 100;
		UnitType workerTypes[3];
		workerTypes[0] = BWAPI::UnitTypes::Zerg_Drone;
		workerTypes[1] = BWAPI::UnitTypes::Protoss_Probe;
		workerTypes[2] = BWAPI::UnitTypes::Terran_SCV;

		handleTechEvents();
		if(Broodwar->getFrameCount() % 12 == 0)
		{
			handleVisionEvents();
		}

		for each(Player* p in this->activePlayers)
		{
			for each(Unit* u in this->seenThisTurn[p])
			{
				this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(u, u->getType()));
			}
			this->seenThisTurn[p].clear();
		}

		for each(Unit* u in Broodwar->getAllUnits())
		{
			if (!u->getType().isBuilding() && u->getType().spaceProvided()
				&& (u->getOrder() == Orders::Unload || u->getOrder() == Orders::MoveUnload))
				lastDropOrderByPlayer[u->getPlayer()] = Broodwar->getFrameCount();

			bool mining = false;
			if(u->getType().isWorker() && (u->isGatheringMinerals() || u->isGatheringGas()))
				mining = true;

			bool newOrders = false;

			if((!mining || (u->isGatheringMinerals() && u->getOrder() != BWAPI::Orders::WaitForMinerals && u->getOrder() != BWAPI::Orders::MiningMinerals && u->getOrder() != BWAPI::Orders::ReturnMinerals) ||
				(u->isGatheringGas()      && u->getOrder() != BWAPI::Orders::WaitForGas && u->getOrder() != BWAPI::Orders::HarvestGas && u->getOrder() != BWAPI::Orders::ReturnGas)) 
				&&
				(u->getOrder() != BWAPI::Orders::ResetCollision) &&
				(u->getOrder() != BWAPI::Orders::Larva)
				)
			{
				if(this->unitOrders.count(u) != 0)
				{
					if(this->unitOrders[u] != u->getOrder() || this->unitOrdersTargets[u] != u->getOrderTarget() || this->unitOrdersTargetPositions[u] != u->getOrderTargetPosition())
					{
						this->unitOrders[u] = u->getOrder();
						this->unitOrdersTargets[u] = u->getOrderTarget();
						this->unitOrdersTargetPositions[u] = u->getOrderTargetPosition();
						newOrders = true;
					}
				}
				else
				{
					this->unitOrders[u] = u->getOrder();
					this->unitOrdersTargets[u] = u->getOrderTarget();
					this->unitOrdersTargetPositions[u] = u->getOrderTargetPosition();
					newOrders = true;
				}
			}
			if(mining)
			{
				int oldmins = -1;
				if(this->minerResourceGroup.count(u) != 0)
				{
					oldmins = this->minerResourceGroup[u];
				}
				if(u->getOrderTarget() != NULL)
				{
					if(u->getOrderTarget()->getResourceGroup() == oldmins)
					{
						newOrders = false;
					}
					else
					{
						this->minerResourceGroup[u] = u->getOrderTarget()->getResourceGroup();
					}
				}
			}
			if(newOrders && Broodwar->getFrameCount() > 0)
			{
				//this->replayOrdersDat << Broodwar->getFrameCount() << "," << u->getType().getName() << "," << u->getOrder().getName() << u->getOrder().getID() << "\n";
				//this->replayOrdersDat << Broodwar->getFrameCount() << "," << u->getID() << "," << u->getOrder().getID() << "\n";
				if(u->getTarget() != NULL)
				{
					this->replayOrdersDat << Broodwar->getFrameCount() << "," << u->getID() << "," << u->getOrder().getName() << ",T," << u->getTarget()->getPosition().x() << "," << u->getTarget()->getPosition().y() << "\n";
				}
				else
				{
					this->replayOrdersDat << Broodwar->getFrameCount() << "," << u->getID() << "," << u->getOrder().getName() << ",P," << u->getTargetPosition().x() << "," << u->getTargetPosition().y() << "\n";
				}
			}

			if(Broodwar->getFrameCount() % refreshSpeed == 0 || unitDestroyedThisTurn || newOrders)
			{

				if(u->exists() && !(u->getPlayer()->getID() == -1) && !(u->getPlayer()->isNeutral()) && u->getPosition().isValid()
					&& (!mining && !newOrders) && u->getType() != BWAPI::UnitTypes::Zerg_Larva && unitPositionMap[u] != u->getPosition())
				{
					Position p = u->getPosition();
					TilePosition tp = u->getTilePosition();
					this->unitPositionMap[u] = p;
					this->replayLocationDat << Broodwar->getFrameCount() << "," << u->getID() << "," << p.x() << "," << p.y() << "\n";
					if (unitCDR[u] != rd.chokeDependantRegion[tp.x()][tp.y()])
					{
						ChokeDepReg r = rd.chokeDependantRegion[tp.x()][tp.y()];
						if (r >= 0)
						{
							unitCDR[u] = r;
							this->replayLocationDat << Broodwar->getFrameCount() << "," << u->getID() << ",CDR," << r << "\n";
						}
					}
					if (unitRegion[u] != BWTA::getRegion(p))
					{
						BWTA::Region* r = BWTA::getRegion(p);
						if (r != NULL)
						{
							unitRegion[u] = r;
							this->replayLocationDat << Broodwar->getFrameCount() << "," << u->getID() << ",Reg," << hashRegionCenter(r) << "\n";
						}
					}
				}
			}
		}
		unitDestroyedThisTurn = false;
	}
	/*
	drawStats();
	if (analyzed && Broodwar->getFrameCount()%30==0)
	{
	//order one of our workers to guard our chokepoint.
	for(std::set<Unit*>::const_iterator i=Broodwar->self()->getUnits().begin();i!=Broodwar->self()->getUnits().end();i++)
	{
	if ((*i)->getType().isWorker())
	{
	//get the chokepoints linked to our home region
	std::set<BWTA::Chokepoint*> chokepoints= home->getChokepoints();
	double min_length=10000;
	BWTA::Chokepoint* choke=NULL;

	//iterate through all chokepoints and look for the one with the smallest gap (least width)
	for(std::set<BWTA::Chokepoint*>::iterator c=chokepoints.begin();c!=chokepoints.end();c++)
	{
	double length=(*c)->getWidth();
	if (length<min_length || choke==NULL)
	{
	min_length=length;
	choke=*c;
	}
	}

	//order the worker to move to the center of the gap
	(*i)->rightClick(choke->getCenter());
	break;
	}
	}
	}
	if (analyzed)
	drawTerrainData();

	if (analysis_just_finished)
	{
	Broodwar->printf("Finished analyzing map.");
	analysis_just_finished=false;
	}
	*/
}

void BWRepDump::onSendText(std::string text)
{
	/*
	if (text=="/show bullets")
	{
	show_bullets = !show_bullets;
	} else if (text=="/show players")
	{
	showPlayers();
	} else if (text=="/show forces")
	{
	showForces();
	} else if (text=="/show visibility")
	{
	show_visibility_data=!show_visibility_data;
	} else if (text=="/analyze")
	{
	if (analyzed == false)
	{
	Broodwar->printf("Analyzing map... this may take a minute");
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AnalyzeThread, NULL, 0, NULL);
	}
	} else
	{
	Broodwar->printf("You typed '%s'!",text.c_str());
	Broodwar->sendText("%s",text.c_str());
	}
	*/
}

void BWRepDump::onReceiveText(BWAPI::Player* player, std::string text)
{
	if(Broodwar->isReplay())
	{
		//Broodwar->printf("%s said '%s'", player->getName().c_str(), text.c_str());
		this->replayDat << Broodwar->getFrameCount() << "," << player->getID() << ",SendMessage," << text << "\n";
	}
}

void BWRepDump::onPlayerLeft(BWAPI::Player* player)
{
	if(Broodwar->isReplay())
	{
		//Broodwar->sendText("%s left the game.",player->getName().c_str());
		this->replayDat << Broodwar->getFrameCount() << "," << player->getID() << ",PlayerLeftGame\n";
	}
}

void BWRepDump::onNukeDetect(BWAPI::Position target)
{
	/*
	if (target!=Positions::Unknown)
	Broodwar->printf("Nuclear Launch Detected at (%d,%d)",target.x(),target.y());
	else
	Broodwar->printf("Nuclear Launch Detected");
	*/
	if(Broodwar->isReplay())
	{
		this->replayDat << Broodwar->getFrameCount() << "," << (-1) << ",NuclearLaunch,(" << target.x() << target.y() << "\n";
	}
}

void BWRepDump::onUnitDiscover(BWAPI::Unit* unit)
{
	/*
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	Broodwar->sendText("A %s [%x] has been discovered at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	*/
}

void BWRepDump::onUnitEvade(BWAPI::Unit* unit)
{
	/*
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	Broodwar->sendText("A %s [%x] was last accessible at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	*/
}

void BWRepDump::onUnitShow(BWAPI::Unit* unit)
{
	/*
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	Broodwar->sendText("A %s [%x] has been spotted at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	*/
}

void BWRepDump::onUnitHide(BWAPI::Unit* unit)
{
	/*
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	Broodwar->sendText("A %s [%x] was last seen at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	*/
}

void BWRepDump::onUnitCreate(BWAPI::Unit* unit)
{
	BWAPI::Position p = unit->getPosition();
	this->unitPositionMap[unit] = p;
	BWAPI::TilePosition tp = unit->getTilePosition();
	this->unitCDR[unit] = rd.chokeDependantRegion[tp.x()][tp.y()];
	this->unitRegion[unit] = BWTA::getRegion(p);
	/*
	if (Broodwar->getFrameCount()>1)
	{

	if (!Broodwar->isReplay())
	Broodwar->sendText("A %s [%x] has been created at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	else
	{
	if (unit->getType().isBuilding() && unit->getPlayer()->isNeutral()==false)
	{
	int seconds=Broodwar->getFrameCount()/24;
	int minutes=seconds/60;
	seconds%=60;
	Broodwar->sendText("%.2d:%.2d: %s creates a %s",minutes,seconds,unit->getPlayer()->getName().c_str(),unit->getType().getName().c_str());
	}
	}

	Broodwar->sendText("A %s [%x] has been created at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	}
	*/
	if(Broodwar->isReplay())
	{
		//Broodwar->sendText("A %s [%x] has been created at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
		this->replayDat << Broodwar->getFrameCount() << "," << unit->getPlayer()->getID()  << ",Created," << unit->getID() << "," << unit->getType().getName() << ",(" << unit->getPosition().x() << "," << unit->getPosition().y() <<")";
		TilePosition tp(unit->getPosition());
		this->replayDat << "," << rd.chokeDependantRegion[tp.x()][tp.y()]; // if there is no CDR it's -1
		if (BWTA::getRegion(tp) != NULL)
			this->replayDat << "," << hashRegionCenter(BWTA::getRegion(tp)) << "\n";
		else
			this->replayDat << ",-1\n"; // no BWTA::Region
		if(unit->getType() != BWAPI::UnitTypes::Zerg_Larva)
		{
			for each(Player* p in this->activePlayers)
			{
				if(this->activePlayers.find(unit->getPlayer()) != this->activePlayers.end())
				{
					if(p->getType() != BWAPI::UnitTypes::Zerg_Larva)
					{
						if(p != unit->getPlayer())
						{
							this->unseenUnits[p].insert(std::pair<Unit*, UnitType>(unit, unit->getType()));
						}
					}
				}
			}
		}
	}
}

void BWRepDump::updateAggroPlayers(BWAPI::Unit* u)
{
	/// A somewhat biased (because it waits for a unit to die to declare there 
	/// was an attack) heuristic to detect who attacks who

	/// Check if it is part of an existing attack (a continuation)
	for (std::list<attack>::iterator it = attacks.begin();
		it != attacks.end(); ++it)
	{
		if (u->getPosition().getDistance(it->position) < it->radius)
			return;
	}

	/// Initialization
	std::map<Player*, std::list<Unit*> > playerUnits = getPlayerMilitaryUnitsNotInAttack(
		Broodwar->getUnitsInRadius(u->getPosition(), (int)MAX_ATTACK_RADIUS));
	
	/// Removes lonely scout (probes, zerglings, obs) dying 
	/// or attacks with one unit which did NO kill (epic fails)
	if (playerUnits[u->getPlayer()].empty() || playerUnits[u->getLastAttackingPlayer()].empty())
		return;

	/// It's a new attack, seek for attacking players
	std::set<Player*> attackingPlayers = activePlayers;
	std::map<BWAPI::Player*, int> scorePlayersPosition;
	for each (Player* p in activePlayers)
		scorePlayersPosition.insert(std::make_pair(p, 0)); // could put a prior on the start location distance
	for each (Player* p in activePlayers)
	{
		for each (Unit* tmp in playerUnits[p])
		{
			if (tmp->getType().isResourceContainer())
				scorePlayersPosition[p] += 12;
			else if (tmp->getType().isWorker())
				scorePlayersPosition[p] += 1;
			else if (tmp->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode)
				scorePlayersPosition[p] += 1;
		}
	}
	Player* defender = NULL;
	int maxScore = 0;
	for each (std::pair<Player*, int> ps in scorePlayersPosition)
	{
		if (ps.second > maxScore)
		{
			maxScore = ps.second;
			defender = ps.first;
		}
	}
	if (defender == NULL)
		defender = u->getPlayer();
	attackingPlayers.erase(defender);

	/// Determine the position of the attack
	BWAPI::Position tmpPos = BWAPI::Position(0, 0);
	int attackers = 0;
	for each (Player* p in activePlayers)
	{
		for each (Unit* tmp in playerUnits[p])
		{
			if (tmp->isAttacking() || tmp->isUnderAttack())
			{
				tmpPos += tmp->getPosition();
				++attackers;
			}
		}
	}
	BWAPI::Position attackPos = u->getPosition();
	double radius = MAX_ATTACK_RADIUS;
	if (attackers > 0)
	{
		radius = 0.0;
		attackPos = BWAPI::Position(tmpPos.x() / attackers, tmpPos.y() / attackers);
		for each (Player* p in activePlayers)
		{
			for each (Unit* tmp in playerUnits[p])
			{
				double range_and_dist = tmp->getDistance(attackPos) + 
					max(tmp->getType().groundWeapon().maxRange(),
					tmp->getType().airWeapon().maxRange());
				if ((tmp->isAttacking() || tmp->isUnderAttack())
					&& range_and_dist > radius)
					radius = range_and_dist;
			}
		}
		if (radius < MIN_ATTACK_RADIUS)
			radius = MIN_ATTACK_RADIUS;
	}
#ifdef __DEBUG_OUTPUT__
	Broodwar->setScreenPosition(max(0, attackPos.x() - 320),
		max(0, attackPos.y() - 240));
#endif

	/// Determine the attack type
	std::set<AttackType> currentAttackType;
	for each (Player* p in attackingPlayers)
	{
		for each (Unit* tmp in playerUnits[p])
		{
			UnitType ut = tmp->getType();
			std::string nameStr = ut.getName();
			if (ut.canAttack()
				&& !ut.isBuilding() // ruling out tower rushes :(
				&& !ut.isWorker())
			{
				if (ut.isFlyer())
				{
					if (ut.spaceProvided() > 0
						&& (Broodwar->getFrameCount() - lastDropOrderByPlayer[p]) < 24*SECONDS_SINCE_LAST_ATTACK * 2)
						currentAttackType.insert(DROP);
					else if (ut.canAttack()
						|| ut == UnitTypes::Terran_Science_Vessel
						|| ut == UnitTypes::Zerg_Queen)
						currentAttackType.insert(AIR);
					// not DROP nor AIR for observers / overlords
				}
				else // not a flyer (ruling out obs)
				{
					if (tmp->isCloaked() || tmp->getType() == UnitTypes::Zerg_Lurker)
						currentAttackType.insert(INVIS);
					else if (ut.canAttack())
						currentAttackType.insert(GROUND);
				}
			}
		}
	}

	/// Create the attack to the corresponding players
	// add the attack
	attacks.push_back(attack(currentAttackType,
		Broodwar->getFrameCount(),
		attackPos, 
		radius,
		defender,
		playerUnits));
	attacks.back().computeScores(this);
	
#ifdef __DEBUG_OUTPUT__
	// and record it
	for each (AttackType at in currentAttackType)
	{
		Broodwar->printf("Player %s is attacked at Position (%d,%d) type %d, %s",
			defender->getName().c_str(), attackPos.x(), attackPos.y(), at, attackTypeToStr(at).c_str());
		//this->replayDat << Broodwar->getFrameCount() << "," << attackTypeToStr(at).c_str() << "," << defender << "," << ",(" << attackPos.x() << "," << attackPos.y() <<")\n";
	}
#endif
}

void BWRepDump::onUnitDestroy(BWAPI::Unit* unit)
{
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	{
	}
	//Broodwar->sendText("A %s [%x] has been destroyed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	else
	{
		updateAggroPlayers(unit);

		//Broodwar->sendText("A %s [%x] has been destroyed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
		this->replayDat << Broodwar->getFrameCount() << "," << unit->getPlayer()->getID()  << ",Destroyed," << unit->getID() << "," << unit->getType().getName() << ",(" << unit->getPosition().x() << "," << unit->getPosition().y() <<")\n";
		unitDestroyedThisTurn = true;
		for each(Player* p in this->activePlayers)
		{
			if(p != unit->getPlayer())
			{
				this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, unit->getType()));
			}
		}
	}
}

void BWRepDump::onUnitMorph(BWAPI::Unit* unit)
{
	/*
	if (!Broodwar->isReplay())
	Broodwar->sendText("A %s [%x] has been morphed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	else
	{
	if (unit->getType().isBuilding() && unit->getPlayer()->isNeutral()==false)
	{
	int seconds=Broodwar->getFrameCount()/24;
	int minutes=seconds/60;
	seconds%=60;
	Broodwar->sendText("%.2d:%.2d: %s morphs a %s",minutes,seconds,unit->getPlayer()->getName().c_str(),unit->getType().getName().c_str());
	}
	}
	*/
	if(Broodwar->isReplay())
	{
		//Broodwar->printf("A %s [%x] has been morphed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
		this->replayDat << Broodwar->getFrameCount() << "," << unit->getPlayer()->getID() << ",Morph," << unit->getID() << ","  << unit->getType().getName() << ",(" << unit->getPosition().x() << "," << unit->getPosition().y() <<")\n";
		for each(Player* p in this->activePlayers)
		{
			if(unit->getType() != BWAPI::UnitTypes::Zerg_Egg)
			{
				if(p != unit->getPlayer())
				{
					if(unit->getType().getRace() == BWAPI::Races::Zerg)
					{
						if(unit->getType() == BWAPI::UnitTypes::Zerg_Lurker)
						{
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Hydralisk));
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Lurker_Egg));
						}
						else if (unit->getType() == BWAPI::UnitTypes::Zerg_Devourer || unit->getType() == BWAPI::UnitTypes::Zerg_Guardian)
						{
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Mutalisk));
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Cocoon));
						}
						else if (unit->getType().getRace() == BWAPI::Races::Zerg && unit->getType().isBuilding() )
						{
							if(unit->getType() == BWAPI::UnitTypes::Zerg_Lair)
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Hatchery));
							}
							else if(unit->getType() == BWAPI::UnitTypes::Zerg_Hive)
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Lair));
							}
							else if(unit->getType() == BWAPI::UnitTypes::Zerg_Greater_Spire)
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Spire));
							}
							else if(unit->getType() == BWAPI::UnitTypes::Zerg_Sunken_Colony)
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Creep_Colony));
							}
							else if(unit->getType() == BWAPI::UnitTypes::Zerg_Spore_Colony)
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Creep_Colony));
							}
							else
							{
								this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Zerg_Drone));
							}
						}
					}
					else if(unit->getType().getRace() == BWAPI::Races::Terran)
					{
						if(unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode)
						{
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode));
						}
						else if(unit->getType() == BWAPI::UnitTypes::Terran_Siege_Tank_Tank_Mode)
						{
							this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, BWAPI::UnitTypes::Terran_Siege_Tank_Siege_Mode));
						}
					}
					if(this->activePlayers.find(unit->getPlayer()) != this->activePlayers.end())
					{
						this->unseenUnits[p].insert(std::pair<Unit*, UnitType>(unit, unit->getType()));
					}
				}
			}
		}
	}
}

void BWRepDump::onUnitRenegade(BWAPI::Unit* unit)
{
	//if (Broodwar->isReplay())
	//Broodwar->printf("A %s [%x] is now owned by %s",unit->getType().getName().c_str(),unit,unit->getPlayer()->getName().c_str());
	if(Broodwar->isReplay())
	{
		this->replayDat << Broodwar->getFrameCount() << "," << unit->getPlayer()->getID() << ",ChangedOwnership," << unit->getID() << "\n";
		for each(Player* p in this->activePlayers)
		{
			if(p != unit->getPlayer())
			{
				if(this->activePlayers.find(unit->getPlayer()) != this->activePlayers.end())
				{
					this->unseenUnits[p].insert(std::pair<Unit*, UnitType>(unit, unit->getType()));
				}
			}
			else
			{
				this->unseenUnits[p].erase(std::pair<Unit*, UnitType>(unit, unit->getType()));
			}
		}
	}
}

void BWRepDump::onSaveGame(std::string gameName)
{
	//Broodwar->printf("The game was saved to \"%s\".", gameName.c_str());
}

DWORD WINAPI AnalyzeThread()
{
	BWTA::analyze();

	//self start location only available if the map has base locations
	if (BWTA::getStartLocation(BWAPI::Broodwar->self())!=NULL)
	{
		home       = BWTA::getStartLocation(BWAPI::Broodwar->self())->getRegion();
	}
	//enemy start location only available if Complete Map Information is enabled.
	if (BWTA::getStartLocation(BWAPI::Broodwar->enemy())!=NULL)
	{
		enemy_base = BWTA::getStartLocation(BWAPI::Broodwar->enemy())->getRegion();
	}
	analyzed   = true;
	analysis_just_finished = true;
	return 0;
}

void BWRepDump::drawStats()
{
	std::set<Unit*> myUnits = Broodwar->self()->getUnits();
	Broodwar->drawTextScreen(5,0,"I have %d units:",myUnits.size());
	std::map<UnitType, int> unitTypeCounts;
	for(std::set<Unit*>::iterator i=myUnits.begin();i!=myUnits.end();i++)
	{
		if (unitTypeCounts.find((*i)->getType())==unitTypeCounts.end())
		{
			unitTypeCounts.insert(std::make_pair((*i)->getType(),0));
		}
		unitTypeCounts.find((*i)->getType())->second++;
	}
	int line=1;
	for(std::map<UnitType,int>::iterator i=unitTypeCounts.begin();i!=unitTypeCounts.end();i++)
	{
		Broodwar->drawTextScreen(5,16*line,"- %d %ss",(*i).second, (*i).first.getName().c_str());
		line++;
	}
}

void BWRepDump::drawBullets()
{
	std::set<Bullet*> bullets = Broodwar->getBullets();
	for(std::set<Bullet*>::iterator i=bullets.begin();i!=bullets.end();i++)
	{
		Position p=(*i)->getPosition();
		double velocityX = (*i)->getVelocityX();
		double velocityY = (*i)->getVelocityY();
		if ((*i)->getPlayer()==Broodwar->self())
		{
			Broodwar->drawLineMap(p.x(),p.y(),p.x()+(int)velocityX,p.y()+(int)velocityY,Colors::Green);
			Broodwar->drawTextMap(p.x(),p.y(),"\x07%s",(*i)->getType().getName().c_str());
		}
		else
		{
			Broodwar->drawLineMap(p.x(),p.y(),p.x()+(int)velocityX,p.y()+(int)velocityY,Colors::Red);
			Broodwar->drawTextMap(p.x(),p.y(),"\x06%s",(*i)->getType().getName().c_str());
		}
	}
}

void BWRepDump::drawVisibilityData()
{
	for(int x=0;x<Broodwar->mapWidth();x++)
	{
		for(int y=0;y<Broodwar->mapHeight();y++)
		{
			if (Broodwar->isExplored(x,y))
			{
				if (Broodwar->isVisible(x,y))
					Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Green);
				else
					Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Blue);
			}
			else
				Broodwar->drawDotMap(x*32+16,y*32+16,Colors::Red);
		}
	}
}

void BWRepDump::drawTerrainData()
{
	//we will iterate through all the base locations, and draw their outlines.
	for(std::set<BWTA::BaseLocation*>::const_iterator i=BWTA::getBaseLocations().begin();i!=BWTA::getBaseLocations().end();i++)
	{
		TilePosition p=(*i)->getTilePosition();
		Position c=(*i)->getPosition();

		//draw outline of center location
		Broodwar->drawBox(CoordinateType::Map,p.x()*32,p.y()*32,p.x()*32+4*32,p.y()*32+3*32,Colors::Blue,false);

		//draw a circle at each mineral patch
		for(std::set<BWAPI::Unit*>::const_iterator j=(*i)->getStaticMinerals().begin();j!=(*i)->getStaticMinerals().end();j++)
		{
			Position q=(*j)->getInitialPosition();
			Broodwar->drawCircle(CoordinateType::Map,q.x(),q.y(),30,Colors::Cyan,false);
		}

		//draw the outlines of vespene geysers
		for(std::set<BWAPI::Unit*>::const_iterator j=(*i)->getGeysers().begin();j!=(*i)->getGeysers().end();j++)
		{
			TilePosition q=(*j)->getInitialTilePosition();
			Broodwar->drawBox(CoordinateType::Map,q.x()*32,q.y()*32,q.x()*32+4*32,q.y()*32+2*32,Colors::Orange,false);
		}

		//if this is an island expansion, draw a yellow circle around the base location
		if ((*i)->isIsland())
			Broodwar->drawCircle(CoordinateType::Map,c.x(),c.y(),80,Colors::Yellow,false);
	}

	//we will iterate through all the regions and draw the polygon outline of it in green.
	for(std::set<BWTA::Region*>::const_iterator r=BWTA::getRegions().begin();r!=BWTA::getRegions().end();r++)
	{
		BWTA::Polygon p=(*r)->getPolygon();
		for(int j=0;j<(int)p.size();j++)
		{
			Position point1=p[j];
			Position point2=p[(j+1) % p.size()];
			Broodwar->drawLine(CoordinateType::Map,point1.x(),point1.y(),point2.x(),point2.y(),Colors::Green);
		}
	}

	//we will visualize the chokepoints with red lines
	for(std::set<BWTA::Region*>::const_iterator r=BWTA::getRegions().begin();r!=BWTA::getRegions().end();r++)
	{
		for(std::set<BWTA::Chokepoint*>::const_iterator c=(*r)->getChokepoints().begin();c!=(*r)->getChokepoints().end();c++)
		{
			Position point1=(*c)->getSides().first;
			Position point2=(*c)->getSides().second;
			Broodwar->drawLine(CoordinateType::Map,point1.x(),point1.y(),point2.x(),point2.y(),Colors::Red);
		}
	}
}

void BWRepDump::showPlayers()
{
	std::set<Player*> players=Broodwar->getPlayers();
	for(std::set<Player*>::iterator i=players.begin();i!=players.end();i++)
	{
		//Broodwar->printf("Player [%d]: %s is in force: %s",(*i)->getID(),(*i)->getName().c_str(), (*i)->getForce()->getName().c_str());
	}
}

void BWRepDump::showForces()
{
	std::set<Force*> forces=Broodwar->getForces();
	for(std::set<Force*>::iterator i=forces.begin();i!=forces.end();i++)
	{
		std::set<Player*> players=(*i)->getPlayers();
		Broodwar->printf("Force %s has the following players:",(*i)->getName().c_str());
		for(std::set<Player*>::iterator j=players.begin();j!=players.end();j++)
		{
			//Broodwar->printf("  - Player [%d]: %s",(*j)->getID(),(*j)->getName().c_str());
		}
	}
}
