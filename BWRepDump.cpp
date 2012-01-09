#include "BWRepDump.h"
#include <float.h>

#define MAX_CDREGION_RADIUS 12
#define SECONDS_SINCE_LAST_ATTACK 20
#define DISTANCE_TO_OTHER_ATTACK 14*TILE_SIZE // in pixels

using namespace BWAPI;

bool analyzed;
bool analysis_just_finished;
BWTA::Region* home;
BWTA::Region* enemy_base;

/* Return TRUE if file 'fileName' exists */
bool fileExists(const char *fileName)
{
    DWORD       fileAttr;
    fileAttr = GetFileAttributesA(fileName);
    if (0xFFFFFFFF == fileAttr)
        return false;
    return true;
}

int hashRegionCenter(BWTA::Region* r)
{
	/// Max size for a map is 512x512 build tiles => 512*32 = 16384 = 2^14 pixels
	/// Unwalkable regions will map to 0
	Position p(r->getPolygon().getCenter());
	int tmp = p.x() + 1; // + 1 to give room for choke dependant regions (after shifting)
	tmp = (tmp << 16) | p.y();
	return tmp;
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
	sprintf_s(buf, "bwapi-data/AI/terrain/%s.cdreg", BWAPI::Broodwar->mapHash().c_str());
	if (fileExists(buf))
	{	
		// fill our own regions data (rd) with the archived file
		std::ifstream ifs(buf, std::ios::binary);
		boost::archive::binary_iarchive ia(ifs);
		ia >> rd;

		// initialize BWTARegion indices
		for each (BWTA::Region* r in BWTA::getRegions())
			BWTARegion.insert(std::make_pair(r, hashRegionCenter(r)));
	}
	else
	{
		std::vector<int> region(Broodwar->mapWidth() * Broodwar->mapHeight(), -1); // tmp on build tiles coordinates
		std::vector<int> maxTiles; // max tiles for each CDRegion
		int k = 1; // 0 is reserved for unwalkable regions
		/// 1. for each region, max radius = max(MAX_CDREGION_RADIUS, choke size)
		for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
		{
			maxTiles.push_back(max(MAX_CDREGION_RADIUS, static_cast<int>(c->getWidth())/TILE_SIZE));
		}
		/// 2. Voronoi on both choke's regions
		for (int x = 0; x < Broodwar->mapWidth(); ++x)
			for (int y = 0; y < Broodwar->mapHeight(); ++y)
			{
				TilePosition tmp(x, y);
				BWTA::Region* r = BWTA::getRegion(tmp);
				double minDist = DBL_MAX;
				int cpn = 1;
				for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
				{
					// TODO could use ground distance??
					// something like double tmpDist = BWTA::getGroundDistance(tmp, TilePosition(c->getCenter()));
					Position chokeCenter(c->getCenter());
					double tmpDist = tmp.getDistance(TilePosition(chokeCenter));
					if (tmpDist < minDist && static_cast<int>(tmpDist) <= maxTiles[cpn-1]
					    && (c->getRegions().first == r || c->getRegions().second == r)
					)
					{
						minDist = tmpDist;
						region[x + y * Broodwar->mapWidth()] = ((chokeCenter.x()+1) << 16) | chokeCenter.y();
					}
					++cpn;
				}
			}
		/// 3. Complete with (amputated) BWTA regions
		// initialize BWTARegion indices
		for each (BWTA::Region* r in BWTA::getRegions())
		{
			/// Max size for a map is 512x512 build tiles => 512*32 = 16384 = 2^14 pixels
			Position p(r->getPolygon().getCenter());
			int tmp = p.x() + 1; // + 1 to give room for choke dependant regions (after shifting)
			tmp = (tmp << 16) | p.y();
			BWTARegion.insert(std::make_pair(r, tmp));
		}
		for (int x = 0; x < Broodwar->mapWidth(); ++x)
			for (int y = 0; y < Broodwar->mapHeight(); ++y)
			{
				TilePosition tmp(x, y);
				if (region[x + y * Broodwar->mapWidth()] == -1)
					this->rd.chokeDependantRegion[x][y] = BWTARegion[BWTA::getRegion(tmp)];
				else
					this->rd.chokeDependantRegion[x][y] = region[x + y * Broodwar->mapWidth()];
			}
		std::ofstream ofs(buf, std::ios::binary);
		{
			boost::archive::binary_oarchive oa(ofs);
			oa << rd;
		}
	}
}

void BWRepDump::displayChokeDependantRegions()
{
	for (int x = 0; x < Broodwar->mapWidth(); x += 4)
		for (int y = 0; y < Broodwar->mapHeight(); y += 2)
		{
			//Broodwar->drawBoxMap(x*TILE_SIZE+2, y*TILE_SIZE+2, x*TILE_SIZE+30, y*TILE_SIZE+30, Colors::Cyan);
			Broodwar->drawTextMap(x*TILE_SIZE+6, y*TILE_SIZE+8, "%d", rd.chokeDependantRegion[x][y]);
			Broodwar->drawTextMap(x*TILE_SIZE+6, y*TILE_SIZE+16, "%d", this->BWTARegion[BWTA::getRegion(TilePosition(x, y))]);
		}
}

/*std::set<Unit*> getUnitsRegionPlayer(BWTA::Region* r, BWAPI::Player* p)
{
	std::set<Unit*> tmp;
	for each (Unit* u in p->getUnits())
	{
		TilePosition tp(u->getTilePosition());
		if (BWTA::getRegion(tp) == r)
			tmp.insert(u);
	}
	return tmp;
}

std::set<Unit*> BWRepDump::getUnitsCDRegionPlayer(ChokeDepReg cdr, BWAPI::Player* p)
{
	std::set<Unit*> tmp;
	for each (Unit* u in p->getUnits())
	{
		TilePosition tp(u->getTilePosition());
		if (rd.chokeDependantRegion[tp.x()][tp.y()] == cdr)
			tmp.insert(u);
	}
	return tmp;
}*/

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
			|| tmp->getType() == UnitTypes::Protoss_Interceptor
			|| tmp->getType() == UnitTypes::Protoss_Scarab
			|| tmp->getType() == UnitTypes::Terran_Nuclear_Missile)
			continue; // we take only military/aggressive units
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
		if (u->getType().isResourceDepot())
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
	std::map<BWTA::Region*, std::set<Unit*> > unitsByRegion;
	std::map<ChokeDepReg, std::set<Unit*> > unitsByCDR;
	std::map<BWTA::Region*, double> ecoRegion;
	std::map<ChokeDepReg, double> ecoCDR;
	std::map<BWTA::Region*, double> tacRegion;
	std::map<ChokeDepReg, double> tacCDR;
	std::set<ChokeDepReg> cdrSet;
	std::set<Unit*> emptyUnitsSet;

	heuristics_analyser(Player* p, BWRepDump* bwrepdump)
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

	const std::set<Unit*>& getUnitsCDRegionPlayer(ChokeDepReg cdr, Player* p)
	{
		if (unitsByCDR.count(cdr))
			return unitsByCDR[cdr];
		return emptyUnitsSet;
	}

	const std::set<Unit*>& getUnitsRegionPlayer(BWTA::Region* r, Player* p)
	{
		if (unitsByRegion.count(r))
			return unitsByRegion[r];
		return emptyUnitsSet;
	}

	// ground forces
	double scoreGround(ChokeDepReg cdr, Player* defender)
	{
		return scoreUnitsGround(getUnitsCDRegionPlayer(cdr, defender));
	}
	double scoreGround(BWTA::Region* r, Player* defender)
	{
		return scoreUnitsGround(getUnitsRegionPlayer(r, defender));
	}

	// air forces
	double scoreAir(ChokeDepReg cdr, Player* defender)
	{
		return scoreUnitsAir(getUnitsCDRegionPlayer(cdr, defender));
	}
	double scoreAir(BWTA::Region* r, Player* defender)
	{
		return scoreUnitsAir(getUnitsRegionPlayer(r, defender));
	}

	// detection
	double scoreInvis(ChokeDepReg cdr, Player* defender)
	{
		return (1.0 / (1.0 + countDetectorUnits(getUnitsCDRegionPlayer(cdr, defender))));
	}
	double scoreInvis(BWTA::Region* r, Player* defender)
	{
		return (1.0 / (1.0 + countDetectorUnits(getUnitsRegionPlayer(r, defender))));
	}

	// economy
	double economicImportance(BWTA::Region* r, BWAPI::Player* p)
	{
		if (ecoRegion.empty())
		{
			double s = 0.0;
			for each (BWTA::Region* rr in BWTA::getRegions())
			{
				int c = countWorkingPeons(getUnitsRegionPlayer(rr, p));
				ecoRegion.insert(std::make_pair(rr, c));
				s += c;
			}
			for each (BWTA::Region* rr in BWTA::getRegions())
				ecoRegion[rr] = ecoRegion[rr] / s;
		}
		return ecoRegion[r];
	}
	double economicImportance(ChokeDepReg cdr, BWAPI::Player* p)
	{
		if (ecoCDR.empty())
		{
			double s = 0.0;
			for each (ChokeDepReg cdrr in cdrSet)
			{
				int c = countWorkingPeons(getUnitsCDRegionPlayer(cdrr, p));
				ecoCDR.insert(std::make_pair(cdrr, c));
				s += c;
			}
			for each (ChokeDepReg cdrr in cdrSet)
				ecoCDR[cdrr] = ecoCDR[cdrr] / s;
		}
		return ecoCDR[cdr];
	}

	// tactic (w.r.t. ground only)
	double tacticalImportance(BWTA::Region* r, BWAPI::Player* p)
	{
		if (tacRegion.empty())
		{
			std::set<Unit*> ths = getTownhalls(p->getUnits());
			for each (BWTA::Region* rr in BWTA::getRegions())
			{
				tacRegion.insert(std::make_pair(rr, 0.0));
				for each (Unit* th in ths)
				{
					BWTA::Region* thr = BWTA::getRegion(th->getTilePosition());
					if (thr->getReachableRegions().count(rr))
						tacRegion[rr] += BWTA::getGroundDistance(
						TilePosition(rr->getCenter()), TilePosition(thr->getCenter()));
					else
						tacRegion[rr] += Broodwar->mapHeight() * Broodwar->mapWidth();
				}
			}
		}
		return tacRegion[r];
	}
};

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
		//Broodwar->setLocalSpeed(0);
		Broodwar->setLatCom(false);
		//Broodwar->setFrameSkip(0);
		std::ofstream myfile;
		std::string filepath = Broodwar->mapPathName() + ".rgd";
		std::string locationfilepath = Broodwar->mapPathName() + ".rld";
		std::string ordersfilepath = Broodwar->mapPathName() + ".rod";
		replayDat.open(filepath.c_str());
		replayLocationDat.open(locationfilepath.c_str());
		replayOrdersDat.open(ordersfilepath.c_str());
		replayDat << "[Replay Start]\n";
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
	this->replayDat << "[EndGame]\n";
	this->replayDat.close();
	this->replayLocationDat.close();
	this->replayOrdersDat.close();
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
				techListPtr = new std::list<TechType>();
				this->listCurrentlyResearching[p] = (*techListPtr);
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
				upgradeListPtr = new std::list<UpgradeType>();
				this->listCurrentlyUpgrading[p] = (*upgradeListPtr);
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
	for (std::list<attack>::iterator it = attacks.begin();
		it != attacks.end(); )
	{
#ifdef __DEBUG_OUTPUT__
		Broodwar->drawCircleMap(it->position.x(), it->position.y(), static_cast<int>(it->radius), Colors::Green);
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
				it->addUnit(uu);
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
			if (u->isAttacking() || u->isUnderAttack())
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
			if (it->radius < TILE_SIZE*5.0)
				it->radius = TILE_SIZE*5.0;
			it->frame = Broodwar->getFrameCount();
			++it;
		}
		else if (Broodwar->getFrameCount() - it->frame >= Broodwar->getFPS()*SECONDS_SINCE_LAST_ATTACK)
		{			
			// Attack is finished, who won the battle ?
			if (scoreUnits(playerUnits[it->defender]) * 3 < scoreUnits(playerUnits[offender]))
			{
				winner = offender; 
				loser = it->defender;
			}
			else
			{
				loser = offender; 
				winner = it->defender;
			}
			if (loser != NULL && winner != NULL)
			{
#ifdef __DEBUG_OUTPUT__
				Broodwar->printf("Player %s (race %s) won the battle against player %s (race %s) at Position (%d,%d)",
					winner->getName().c_str(), winner->getRace().c_str(), 
					loser->getName().c_str(), loser->getRace().c_str(),
					it->position.x(), it->position.y());
#endif
			}
			// if the currently examined attack is too old and too far,
			// remove it (no longer a real attack)
			attacks.erase(it++);
		}
		else 
			++it;
	}
}

void BWRepDump::onFrame()
{
	//  if (show_visibility_data)
	//    drawVisibilityData();

	//  if (show_bullets)
	//    drawBullets();

	if (Broodwar->isReplay())
	{
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
				this->replayDat << Broodwar->getFrameCount() << "," << p->getID() << ",R," << p->minerals() << "," << p->gas() << "," << p->gatheredMinerals() << "," << p->gatheredGas() << "\n";
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

				if(u->exists() && !(u->getPlayer()->getID() == -1) && (!mining && !newOrders) && u->getType() != BWAPI::UnitTypes::Zerg_Larva && unitPositionMap[u] != u->getPosition())
				{
					Position p = u->getPosition();
					this->unitPositionMap[u] = p;
					this->replayLocationDat << Broodwar->getFrameCount() << "," << u->getID() << "," << p.x() << "," << p.y() << "\n";
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
	this->unitPositionMap[unit] = unit->getPosition();
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
		this->replayDat << Broodwar->getFrameCount() << "," << unit->getPlayer()->getID()  << ",Created," << unit->getID() << "," << unit->getType().getName() << ",(" << unit->getPosition().x() << "," << unit->getPosition().y() <<")\n";
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

	/// Initialization
	std::map<Player*, std::list<Unit*> > playerUnits = getPlayerMilitaryUnits(
		Broodwar->getUnitsInRadius(u->getPosition(), DISTANCE_TO_OTHER_ATTACK));

	/// Check if it is part of an existing attack (a continuation)
	for (std::list<attack>::iterator it = attacks.begin();
		it != attacks.end(); ++it)
	{
		if (u->getPosition().getDistance(it->position) < DISTANCE_TO_OTHER_ATTACK + it->radius)
			return;
	}
	
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
	double radius = DISTANCE_TO_OTHER_ATTACK;
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
		if (radius < TILE_SIZE*5.0)
			radius = TILE_SIZE*5.0;
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
						&& (Broodwar->getFrameCount() - lastDropOrderByPlayer[p]) < Broodwar->getFPS()*SECONDS_SINCE_LAST_ATTACK)
						currentAttackType.insert(DROP);
					else if (ut.canAttack()
						|| ut == UnitTypes::Terran_Science_Vessel
						|| ut == UnitTypes::Zerg_Queen)
						currentAttackType.insert(AIR);
					// not DROP nor AIR for observers / overlords
				}
				else // not a flyer
				{
					if (tmp->isCloaked() || tmp->isBurrowed())
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
	
	// and record it
	for each (AttackType at in currentAttackType)
	{
#ifdef __DEBUG_OUTPUT__
		Broodwar->printf("Player %s is attacked at Position (%d,%d) type %d, %s",
			defender->getName().c_str(), attackPos.x(), attackPos.y(), at, attackTypeToStr(at).c_str());
#endif
		this->replayDat << Broodwar->getFrameCount() << "," << attackTypeToStr(at).c_str() << "," << defender << "," << ",(" << attackPos.x() << "," << attackPos.y() <<")\n";
	}
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
