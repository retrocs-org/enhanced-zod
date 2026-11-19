#include "zbuilding.h"
#include "zbuildlist.h"

string ZBuilding::ProductionStatus()
{
    if(IsDestroyed()) return "Destroyed - repair this factory";
    if(owner==NULL_TEAM) return "Capture this zone to produce units";
    if(unit_limit_reached && unit_limit_reached[owner]) return "Unit limit reached - free a unit slot";
    if(queue_list.size()>=MAX_QUEUE_ITEMS) return "Queue full - remove a queued unit to add another";
    if(GetHealth()<GetMaxHealth()) return "Damaged - repair to speed production";
    if(GetBuildState()==BUILDING_PAUSED) return "Production paused";
    if(GetBuildState()==BUILDING_PLACE) return "Cannon ready - choose Place";
    string status=GetBuildState()==BUILDING_SELECT ? "Choose a unit, then OK" : "Production active";
    // Only suggest a level when the factory's build table gains a new unit.
    if(buildlist) for(int next=level+1;next<MAX_BUILDING_LEVELS;++next)
        for(const auto &unit:buildlist->GetBuildList(object_id,next))
            if(!buildlist->UnitInBuildList(object_id,level,unit.ot,unit.oid))
                return status+" | More units at tech "+to_string(next);
    return status+" | Tech "+to_string(level);
}
