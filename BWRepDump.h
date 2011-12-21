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
//#define __DEBUG_CDR__

extern bool analyzed;
extern bool analysis_just_finished;
extern BWTA::Region* home;
extern BWTA::Region* enemy_base;
DWORD WINAPI AnalyzeThread();

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
	int frame;
	BWAPI::Position position;
	std::map<BWAPI::Player*, std::map<BWAPI::UnitType, int> > unitTypes;
	BWAPI::Player* defender;
	attack(int f, BWAPI::Position p, BWAPI::Player* d,
		std::map<BWAPI::Player*, std::set<BWAPI::Unit*> > units)
		: frame(f), position(p), defender(d)
	{
		for each (std::pair<BWAPI::Player*, std::set<BWAPI::Unit*> > pu in units)
		{
			unitTypes.insert(std::make_pair(pu.first, std::map<BWAPI::UnitType, int>()));
			for each (BWAPI::Unit* u in pu.second)
			{
				if (unitTypes[pu.first].count(u->getType()))
					unitTypes[pu.first][u->getType()] += 1;
				else
					unitTypes[pu.first].insert(std::make_pair(u->getType(), 1));
			}
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