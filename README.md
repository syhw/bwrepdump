Requirements
------------
[BWAPI](http://code.google.com/p/bwapi/)   
MS Visual C++ 2008 (optional, [a DLL is here](https://github.com/SnippyHolloW/bwrepdump/blob/master/Release/BWRepDump.dll?raw=true))   
StarCraft: Broodwar  

Pipeline used
-------------
First, get some replays (I did get mine from ICCUP, TeamLiquid and GosuGamers),
 with [scripts](https://github.com/SnippyHolloW/Broodwar_replays_scrappers).  
I use ChaosLauncher to inject BWAPI in StarCraft: Broodwar, in Release mode
(Debug will not be able to deal with un-analysed/un-serialized 
[BWTA](http://code.google.com/p/bwta/) maps), with:

        auto_menu = SINGLE_PLAYER
        auto_restart = ON
        map = maps\replays\some_folder\*.rep
        mapiteration = SEQUENCE

Regions
-------
### Serialization
To serialize, we [hash](https://github.com/SnippyHolloW/bwrepdump/blob/master/BWRepDump.cpp#L40-43) BWTA's regions and ChokeDepReg regions on their TilePosition center.

### ChokeDepReg: Choke dependant regions
ChokeDepReg are regions created from the center of chokes to MAX(MIN\_CDR\_RADIUS(currently 9), CHOKE\_WIDTH) build tiles (TilePositions) away, in a Voronoi tiling fashion. Once that is done, ChokeDepRegs are completed with BWTA::Regions minus existing ChokeDepRegs.
  
Result for one replay
---------------------
Data is partly redundant, in a way that eases analysis.
### RGD file
        [Replay Start]
        RepPath: $(PATH/TO/REP)
        MapName: $MAPNAME
        NumStartPositions: $N
        The following players are in this replay:
        <list of 
        $PLAYER_ID, $PLAYER_NAME, $START_LOC
        separated by newlines>
        Begin replay data:
        <list of
        $FRAME_NUMBER,$PLAYER_ID,$ACTION,[$ACTION_DEP_ARGS]
        separated by newlines>
        [EndGame]
        
Actions can be:
- *Created*,$UNIT\_ID,$UNIT\_TYPE,($POS\_X,$POS\_Y),$CDR\_HASH,$REGION\_HASH  
- *Destroyed*,$UNIT\_ID,$UNIT\_TYPE,($POS\_X,$POS\_Y)  
- *Discovered*,$UNIT\_ID,$UNIT\_TYPE  
- *R*,$MINERALS,$GAS,$GATHERED\_MINERALS,$GATHERED\_GAS,  
$SUPPLY\_USED,$SUPPLY\_TOTAL  
- *ChangedOwnership*,$UNIT\_ID  
- *Morph*,$UNIT\_ID,$UNIT\_TYPE,($POS\_X,$POS\_Y)  
- $FIRST\_FRAME,$DEFENDER\_ID,*IsAttacked*,($ATTACK\_TYPES),
($INIT\_POSITION.X,$INIT\_POSITION.Y),  
$INIT\_CDR, $INIT\_REGION,
{$PLAYER\_ID:{$TYPE:$MAX\_NUMBER\_INVOLVED}},  
($SCORE\_GROUND\_CDR,$SCORE\_GROUND\_REGION,$SCORE\_AIR\_CDR,  
$SCORE\_AIR\_REGION,$SCORE\_DETECT\_CDR,$SCORE\_DETECT\_REGION,  
$ECO\_IMPORTANCE\_CDR,$ECO\_IMPORTANCE\_REGION,  
$TACT\_IMPORTANCE\_CDR,$TACT\_IMPORTANCE\_REGION),  
{$PLAYER\_ID:{$TYPE:$NUMBER\_AT\_END}},($LAST\_POSITION.X,$LAST\_POSITION.Y),  
{$PLAYER\_ID:$NB\_WORKERS\_DEAD},$LAST\_FRAME,$WINNER\_ID(OPTIONAL)  

$ATTACK\_TYPES are in {DropAttack, GroundAttack, AirAttack, InvisAttack, UnknownAttackError}.  

[$TACT\_IMPORTANCE](https://github.com/SnippyHolloW/bwrepdump/blob/master/BWRepDump.cpp#L700) and [$ECO\_IMPORTANCE](https://github.com/SnippyHolloW/bwrepdump/blob/master/BWRepDump.cpp#L666) are from in-game heuristics.  

### ROD file
        <list of
        $FRAME,$PLAYER_ID,$ORDER,TargetOrPosition,$POS_X,$POS_Y
        separated by newlines>
with TargetOrPositions being *T* if the order in on a unit, *P* if it's a map position.  

### RLD file
        Regions,$REGIONS_IDS_COMMA_SEPARATED
        $REGION_ID, $DIST, $DIST, ...
        $REGION_ID, $DIST, $DIST, ...
        .
        .
        .
        ChokeDepReg,$REGIONS_IDS_COMMA_SEPARATED
        $REGION_ID, $DIST, $DIST, ...
        $REGION_ID, $DIST, $DIST, ...
        .
        .
        .
        [Replay Start]
        <list of
        $FRAME,$UNIT_ID,$POS_X,$POS_Y
        $FRAME,$UNIT_ID,Reg,$REGION_ID
        $FRAME,$UNIT_ID,CDR,$CDR_ID
        separated by newlines>

With new lines uniquely when the unit moved (of Position and/or Region and/or ChokeDepReg) in the last refresh rate frames (100 atm).

Tuning
------
[You can tune these defines.](https://github.com/SnippyHolloW/bwrepdump/blob/master/BWRepDump.cpp#L7-14)


Final words
-----------
This work is an extension of [bwrepanalysis](http://code.google.com/p/bwrepanalysis/)

