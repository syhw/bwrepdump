#include "BWRepDump.h"
#include <float.h>

#define MAX_CDREGION_RADIUS 9

using namespace BWAPI;

bool analyzed;
bool analysis_just_finished;
BWTA::Region* home;
BWTA::Region* enemy_base;

void BWRepDump::createChokeDependantRegions()
{
	// TODO
	// if (serialized)
	//     chokeDependantRegion << file;
	//     return;

	std::vector<int> region(Broodwar->mapWidth() * Broodwar->mapHeight(), -1); // tmp on build tiles coordinates
	std::vector<int> maxTiles; // max tiles for each CDRegion
	int k = 0;
	for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
	{
		/// 1. Init 1 region / choke
		BWAPI::TilePosition tp(c->getCenter());
		region[tp.x() + Broodwar->mapWidth() * tp.y()] = k++;
		/// 2. for each region, max radius = max(MAX_CDREGION_RADIUS, choke size)
		maxTiles.push_back(max(MAX_CDREGION_RADIUS, (int)(c->getWidth())/TILE_SIZE));
	}
	/// 3. Voronoi on both choke's regions
	for (int x = 0; x < Broodwar->mapWidth(); ++x)
		for (int y = 0; y < Broodwar->mapHeight(); ++y)
		{
			TilePosition tmp(x, y);
			double minDist = DBL_MAX;
			int k = 0;
			for each (BWTA::Chokepoint* c in BWTA::getChokepoints())
			{
				double tmpDist = tmp.getDistance(TilePosition(c->getCenter()));
				if (tmpDist < minDist && (int)tmpDist/TILE_SIZE <= maxTiles[k])
				{
					minDist = tmpDist;
					region[x + y * Broodwar->mapWidth()] = k;
				}
				++k;
			}
		}
	/// 4. Complete with (amputated) BWTA regions
	std::map<BWTA::Region*, int> bwtaToCDRegion;
	//k = BWTA::getChokepoints().size();
	for each (BWTA::Region* r in BWTA::getRegions())
		bwtaToCDRegion.insert(std::make_pair(r, k++));
	for (int x = 0; x < Broodwar->mapWidth(); ++x)
		for (int y = 0; y < Broodwar->mapHeight(); ++y)
		{
			TilePosition tmp(x, y);
			if (region[x + y * Broodwar->mapWidth()] == -1)
				this->chokeDependantRegion[tmp] = bwtaToCDRegion[BWTA::getRegion(tmp)];
			else
				this->chokeDependantRegion[tmp] = region[x + y * Broodwar->mapWidth()];
		}
}

void BWRepDump::displayChokeDependantRegions()
{
	for (int x = 0; x < Broodwar->mapWidth(); ++x)
		for (int y = 0; y < Broodwar->mapHeight(); ++y)
		{
			Broodwar->drawBoxMap(x*TILE_SIZE+2, y*TILE_SIZE+2, x*TILE_SIZE+30, y*TILE_SIZE+30, Colors::Cyan);
			Broodwar->drawTextMap(x*TILE_SIZE+6, y*TILE_SIZE+6, "%d", this->chokeDependantRegion[TilePosition(x, y)]);
		}
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
	this->createChokeDependantRegions();

	show_bullets=false;
	show_visibility_data=false;
	unitDestroyedThisTurn=false;

	if (Broodwar->isReplay())
	{
		// Broodwar->setLocalSpeed(0);
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

void BWRepDump::onFrame()
{
	//  if (show_visibility_data)
	//    drawVisibilityData();

	//  if (show_bullets)
	//    drawBullets();

	if (Broodwar->isReplay())
	{
		this->displayChokeDependantRegions();

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
			UnitType u_workerType;
			if(u->getPlayer()->getRace() == BWAPI::Races::Zerg)
			{
				u_workerType = workerTypes[0];
			}
			else if (u->getPlayer()->getRace() == BWAPI::Races::Protoss)
			{
				u_workerType = workerTypes[1];
			}
			else
			{
				u_workerType = workerTypes[2];
			}
			bool mining = false;
			if(u->getType() == u_workerType && (u->isGatheringMinerals() || u->isGatheringGas()))
			{
				mining = true;
			}

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

void BWRepDump::onUnitDestroy(BWAPI::Unit* unit)
{
	if (!Broodwar->isReplay() && Broodwar->getFrameCount()>1)
	{
	}
	//Broodwar->sendText("A %s [%x] has been destroyed at (%d,%d)",unit->getType().getName().c_str(),unit,unit->getPosition().x(),unit->getPosition().y());
	else
	{
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
