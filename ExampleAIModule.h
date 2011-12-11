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

extern bool analyzed;
extern bool analysis_just_finished;
extern BWTA::Region* home;
extern BWTA::Region* enemy_base;
DWORD WINAPI AnalyzeThread();

class ExampleAIModule : public BWAPI::AIModule
{
	//std::vector<bool> aggroPlayers;
	std::map<BWAPI::TilePosition, int> chokeDependantRegion;
	void createChokeDependantRegions();
	void displayChokeDependantRegions();
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

	std::map<BWAPI::Player*, std::list<BWAPI::TechType>> listCurrentlyResearching;
	std::map<BWAPI::Player*, std::list<BWAPI::TechType>> listResearched;

	std::map<BWAPI::Player*, std::list<BWAPI::UpgradeType>> listCurrentlyUpgrading;
	std::map<BWAPI::Player*, std::list<std::pair<BWAPI::UpgradeType, int>>> listUpgraded;

	std::map<BWAPI::Player*, std::set<std::pair<BWAPI::Unit*, BWAPI::UnitType>>> unseenUnits;
	//std::map<BWAPI::Player*, std::set<BWAPI::Unit*>> seenUnits;
	std::map<BWAPI::Player*, std::set<BWAPI::Unit*>> seenThisTurn;


	std::set<BWAPI::Player*> activePlayers;

	//std::map<BWAPI::Unit*, int> lastOrderFrame;
	bool unitDestroyedThisTurn;
};
