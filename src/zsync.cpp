#include "zsync.h"
#include "zobject.h"
#include "zmap.h"
#include "zhash.h"
#include <algorithm>
#include <sstream>
#include <tuple>
std::string ZSync_Hash(const std::vector<ZObject*> &objects,ZMap &map){
 std::vector<std::tuple<int,int,int,int,int>> rows;for(auto *o:objects){unsigned char type,id;o->GetObjectID(type,id);if(type==ANIMAL_OBJECT)continue;rows.emplace_back(o->GetRefID(),type,id,o->GetOwner(),o->GetHealth());}std::sort(rows.begin(),rows.end());std::ostringstream text;for(auto &r:rows)text<<std::get<0>(r)<<','<<std::get<1>(r)<<','<<std::get<2>(r)<<','<<std::get<3>(r)<<','<<std::get<4>(r)<<';';text<<'|';for(const auto &zone:map.GetZoneInfoList())text<<zone.owner<<',';auto s=text.str();return ZSHA256(s.data(),s.size());
}
