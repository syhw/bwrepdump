#pragma once
#include <BWAPI.h>

#include <BWTA.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <list>
#include <utility>

#include "boost/archive/binary_oarchive.hpp"
#include "boost/archive/binary_iarchive.hpp"
#include "boost/serialization/map.hpp"
#include "boost/serialization/utility.hpp"

#define __DEBUG_OUTPUT__
#define __DEBUG_CDR__

extern bool analyzed;
extern bool analysis_just_finished;
extern BWTA::Region* home;
extern BWTA::Region* enemy_base;
DWORD WINAPI AnalyzeThread();

/*struct PathAwareMaps
{
    friend class boost::serialization::access;
	template <class archive>
    void serialize(archive & ar, const unsigned int version)
    {
        ar & regionsPFCenters;
		ar & distRegions;
        ar & distBaseToBase;
    }
	std::map<int, std::map<int, double> > distRegions; // distRegions[R1][R2] w.r.t regionsPFCenters
	std::map<int, std::map<int, double> > distBaseToBase;
};

BOOST_CLASS_TRACKING(PathAwareMaps, boost::serialization::track_never);
BOOST_CLASS_VERSION(PathAwareMaps, 1);*/

typedef int ChokeDepReg;
enum AttackType {
	DROP,
	GROUND,
	AIR,
	INVIS
};

struct heuristics_analyser;

struct regions_data
{
    friend class boost::serialization::access;
	template <class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & chokeDependantRegion;
    }
	// 0 -> unwalkable regions
	std::map<int, std::map<int, ChokeDepReg> > chokeDependantRegion;
	regions_data() 
	{}
	regions_data(const std::map<int, std::map<int, ChokeDepReg> >& cdr)
		: chokeDependantRegion(cdr)
	{}
};

BOOST_CLASS_TRACKING(regions_data, boost::serialization::track_never);
BOOST_CLASS_VERSION(regions_data, 1);

struct attack
{
	std::set<AttackType> types;
	int frame;
	int firstFrame;
	BWAPI::Position position;
	BWAPI::Position initPosition;
	double radius;
	std::map<BWAPI::Player*, std::map<BWAPI::UnitType, int> > unitTypes; // countain the maximum number of units of each type which "engaged" in the attack
	std::map<BWAPI::Player*, std::set<BWAPI::Unit*> > battleUnits;
	BWAPI::Player* defender;
	void addUnit(BWAPI::Unit* u)
	{
		if (!battleUnits[u->getPlayer()].count(u))
		{
			if (unitTypes[u->getPlayer()].count(u->getType()))
				unitTypes[u->getPlayer()][u->getType()] += 1;
			else
				unitTypes[u->getPlayer()].insert(std::make_pair(u->getType(), 1));
			battleUnits[u->getPlayer()].insert(u);
		}
	}
	attack(const std::set<AttackType>& at, 
		int f, BWAPI::Position p, double r, BWAPI::Player* d,
		std::map<BWAPI::Player*, std::list<BWAPI::Unit*> > units)
		: types(at), frame(f), firstFrame(BWAPI::Broodwar->getFrameCount()), position(p), initPosition(p), radius(r), defender(d)
	{
		for each (std::pair<BWAPI::Player*, std::list<BWAPI::Unit*> > pu in units)
		{
			unitTypes.insert(std::make_pair(pu.first, std::map<BWAPI::UnitType, int>()));
			battleUnits.insert(std::make_pair(pu.first, std::set<BWAPI::Unit*>()));
			for each (BWAPI::Unit* u in pu.second)
				addUnit(u);
		}
	}
};

class BWRepDump : public BWAPI::AIModule
{
	friend heuristics_analyser;
protected:
	// attackByPlayer[p] = (frame, Position)
	std::list<attack> attacks;
	std::map<BWAPI::Player*, int> lastDropOrderByPlayer;

	// Neither Region* (of course) nor the ordering in the Regions set is
	// deterministic, so we have a map which maps Region* to a unique int
	// which is region's center (0)<x Position + 1><y Position>
	// on                               16 bits      16 bits
	std::map<BWTA::Region*, int> BWTARegion;
	regions_data rd;
	void createChokeDependantRegions();
	void displayChokeDependantRegions();
	std::set<BWAPI::Unit*> getUnitsCDRegionPlayer(int cdr, BWAPI::Player* p);
	std::map<BWAPI::Player*, std::list<BWAPI::Unit*> > getPlayerMilitaryUnits(const std::set<BWAPI::Unit*>& unitsAround);
	double scoreGround(ChokeDepReg cdr, BWAPI::Player* defender);
	double scoreAir(ChokeDepReg cdr, BWAPI::Player* defender);
	double scoreInvis(ChokeDepReg cdr, BWAPI::Player* defender);
public:
	virtual void onStart();
	virtual void onEnd(bool isWinner);
	virtual void onFrame();
	virtual void onSendText(std::string text);
	virtual void onReceiveText(BWAPI::Player* player, std::string text);
	virtual void onPlayerLeft(BWAPI::Player* player);
	virtual void onNukeDetect(BWAPI::Position target);
	virtual void onUnitDiscover(BWAPI::Unit* unit);
	virtual void onUnitEvade(BWAPI::Unit* unit);
	virtual void onUnitShow(BWAPI::Unit* unit);
	virtual void onUnitHide(BWAPI::Unit* unit);
	virtual void onUnitCreate(BWAPI::Unit* unit);
	virtual void onUnitDestroy(BWAPI::Unit* unit);
	virtual void onUnitMorph(BWAPI::Unit* unit);
	virtual void onUnitRenegade(BWAPI::Unit* unit);
	virtual void onSaveGame(std::string gameName);
	void updateAggroPlayers(BWAPI::Unit* u);
	void updateAttacks();
	void drawStats(); //not part of BWAPI::AIModule
	void drawBullets();
	void drawVisibilityData();
	void drawTerrainData();
	void showPlayers();
	void showForces();
	bool show_bullets;
	bool show_visibility_data;

	//Replay analysis encoding data
	void handleTechEvents();
	void handleVisionEvents();
	void checkVision(BWAPI::Unit*);

	std::ofstream replayDat;
	std::ofstream replayLocationDat;
	std::ofstream replayOrdersDat;
	std::map<BWAPI::Unit*, BWAPI::Position> unitPositionMap;
	std::map<BWAPI::Unit*, ChokeDepReg> unitCDR;
	std::map<BWAPI::Unit*, BWTA::Region*> unitRegion;

	std::map<BWAPI::Unit*, BWAPI::Order> unitOrders;
	std::map<BWAPI::Unit*, BWAPI::Unit*> unitOrdersTargets;
	std::map<BWAPI::Unit*, BWAPI::Position> unitOrdersTargetPositions;
	std::map<BWAPI::Unit*, int> minerResourceGroup;

	//std::map<BWAPI::Unit*, BWAPI::UpgradeType> lastUpgrading;
	//std::map<BWAPI::Unit*, BWAPI::TechType> lastResearching;

	std::map<BWAPI::Player*, std::list<BWAPI::TechType> > listCurrentlyResearching;
	std::map<BWAPI::Player*, std::list<BWAPI::TechType> > listResearched;

	std::map<BWAPI::Player*, std::list<BWAPI::UpgradeType> > listCurrentlyUpgrading;
	std::map<BWAPI::Player*, std::list<std::pair<BWAPI::UpgradeType, int> > > listUpgraded;

	std::map<BWAPI::Player*, std::set<std::pair<BWAPI::Unit*, BWAPI::UnitType> > > unseenUnits;
	//std::map<BWAPI::Player*, std::set<BWAPI::Unit*> > seenUnits;
	std::map<BWAPI::Player*, std::set<BWAPI::Unit*> > seenThisTurn;

	std::set<BWAPI::Player*> activePlayers;

	//std::map<BWAPI::Unit*, int> lastOrderFrame;
	bool unitDestroyedThisTurn;
};