#include "zplayer.h"
#include "gmm_player_list.h"
#include "zprefs.h"
#include <algorithm>
#include <sstream>
#include "zmenu_theme.h"
#include "zasset_import.h"
#include <ctime>

int ZPlayer::MatchActionAt(int px,int py) const
{
    const int count=CheckpointAvailable()?3:2;
    const int width=std::min(180,(init_w-32)/count);
    for(int slot=0;slot<count;++slot) {
        SDL_Rect r{(init_w-count*(width+8)+8)/2+slot*(width+8),init_h-52,width,30};
        if(px>=r.x && px<r.x+r.w && py>=r.y && py<r.y+r.h)
            return slot==2?3:hosted_setup_available ? slot+1 : slot;
    }
    return -1;
}

void ZPlayer::ChooseMatchAction(int action)
{
    if(!show_match_summary || summary_continue_sent) return;
    if(action==3){RetryCheckpoint();return;}
    if(action==0) { SendContinueAfterEnd(); return; }
    if(action==1 && !hosted_setup_available) {
        // Use the existing server-authorized reset/vote path for campaign,
        // legacy generated games, and matches hosted by somebody else.
        if(client_socket.SendMessage(RESET_MAP,nullptr,0))
            AddNewsEntry("Rematch requested. Server approval or a vote may be required (F1 yes / F2 no).");
        else AddNewsEntry("Could not request rematch: server disconnected. Continue or reconnect.");
        return;
    }
    if(!hosted_setup_available || (action!=1 && action!=2)) return;
    // Starting a fresh hosted match uses a snapshot of the setup that actually
    // created this session, rather than preferences edited since it started.
    const bool setup=hosted_setup_available;
    const auto recipe=hosted_setup;
    ReturnToMenu();
    if(setup) {
        client_socket.ClearConnection();
        CloseAllMenus();
        auto *menu=new GMMMultiplayerCreate(&recipe,action==1);
        AttachMainMenu(menu);
    }
}

bool ZPlayer::IsIdleUnit(ZObject *object) const
{
    if(!object || our_team<=NULL_TEAM || object->GetOwner()!=our_team
       || object->IsDestroyed() || object->IsMinion() || !object->CanMove()
       || !object->GetWayPointList().empty() || !object->GetWayPointDevList().empty()
       || object->GetAttackObject() || object->IsMoving()) return false;
    unsigned char type,id; object->GetObjectID(type,id);
    return type==ROBOT_OBJECT || type==VEHICLE_OBJECT;
}

void ZPlayer::SelectNextIdleUnit()
{
    ZObject *first=nullptr, *next=nullptr;
    for(auto *object:object_list) if(IsIdleUnit(object)) {
        int ref=object->GetRefID();
        if(!first || ref<first->GetRefID()) first=object;
        if(ref>last_idle_ref && (!next || ref<next->GetRefID())) next=object;
    }
    if(!next) next=first;
    if(!next) { AddNewsEntry("No idle mobile units"); return; }
    last_idle_ref=next->GetRefID();
    select_info.Clear(); select_info.selected_list.push_back(next);
    select_info.SetupGroupDetails(); DetermineCursor(); GiveHudSelected();
    int x,y; next->GetCenterCords(x,y); FocusCameraTo(x,y);
}

void ZPlayer::RenderSelectionReadout()
{
    if(replay_active||show_match_summary || summary_pending || our_team==NULL_TEAM) return;
    int robots=0,vehicles=0,cannons=0,idle=0,factories=0;
    for(auto *object:object_list)if(object->GetOwner()==our_team&&object->ProducesUnits()&&!object->IsDestroyed()&&(object->GetBuildState()==BUILDING_SELECT||object->GetBuildState()==BUILDING_PAUSED))++factories;
    for(auto *object:object_list) if(IsIdleUnit(object)) ++idle;
    for(auto *object:select_info.selected_list) {
        if(!object || object->IsDestroyed()) continue;
        unsigned char type,id; object->GetObjectID(type,id);
        robots+=type==ROBOT_OBJECT; vehicles+=type==VEHICLE_OBJECT; cannons+=type==CANNON_OBJECT;
    }
    int total=robots+vehicles+cannons;
    string text=to_string(total)+" selected";
    if(total) text+=": "+to_string(robots)+" infantry, "+to_string(vehicles)+" vehicles, "+to_string(cannons)+" guns";
    text+=" | Idle: "+to_string(idle)+" ["+ZPrefs_KeyLabel(zod_keys[ZKEY_IDLE_UNIT])+"]";
    text+=" / Factories idle: "+to_string(factories)+" ["+ZPrefs_KeyLabel(zod_keys[ZKEY_IDLE_FACTORY])+"]";if(unit_limit_reached[our_team])text+=" / UNIT LIMIT";
    if(text!=selection_readout_text) {
        selection_readout_text=text;
        selection_readout_img=ZFontEngine::GetFont(SMALL_WHITE_FONT).Render(text.c_str());
    }
    if(auto *s=selection_readout_img.GetBaseSurface()) {
        SDL_Rect background{6,6,s->w+12,s->h+8};
        ZSDL_FillSurfaceRect(&background,18,24,30);
        selection_readout_img.BlitSurface(NULL,12,10);
    }
}

void ZPlayer::lobby_countdown_event(ZPlayer *p,char *data,int size,int)
{
    if(size!=sizeof(int) || !data) return;
    int seconds; memcpy(&seconds,data,sizeof(seconds));
    if(seconds< -1 || seconds>3600) return;
    p->lobby_countdown=seconds;
}

void ZPlayer::cloud_player_role_event(ZPlayer *p,char *data,int size,int){int packet[2];if(!data||size!=sizeof(packet))return;memcpy(packet,data,size);if(packet[0]<0||packet[1]<0||packet[1]>3)return;if(p->cloud_player_roles.size()<1024||p->cloud_player_roles.count(packet[0]))p->cloud_player_roles[packet[0]]=packet[1];}
void ZPlayer::cloud_server_role_event(ZPlayer *p,char *data,int size,int){
 int role;if(!data||size!=sizeof(role))return;memcpy(&role,data,sizeof(role));if(role<0||role>3)return;p->cloud_server_role=role;for(auto *menu:p->gui_menu_list)if(auto *list=dynamic_cast<GMMPlayerList*>(menu))list->SetRole(role);
 p->AddNewsEntry(role==2?"You own this public server.":role==1?"You are a server administrator.":role==3?"You are a server moderator. Open Player List to kick or temporarily ban players.":"Server administration is limited to the owner and delegated administrators.");
}
void ZPlayer::cloud_required_event(ZPlayer *p,char *data,int size,int)
{
    int required=0;if(size!=sizeof(required)||!data)return;memcpy(&required,data,size);if(required!=0 && required!=1)return;
    p->managed_dedicated=true;p->public_remote_session=required==1;if(required==0){p->cloud_join_ticket.clear();return;}
    std::string approved=std::move(p->cloud_join_ticket);p->cloud_join_ticket.clear();
    const char *ticket=approved.empty()?getenv("ZOD_JOIN_TICKET"):approved.c_str();
    if(ticket && strlen(ticket)==64) p->client_socket.SendMessage(CLOUD_TICKET,ticket,65);
    else {
        p->cloud_disconnect_reason="Cloud account required. Join this server through Online games to approve it in your browser.";
        p->AddNewsEntry(p->cloud_disconnect_reason);
        for(auto *menu:p->gui_menu_list) if(auto *lobby=dynamic_cast<GMMLobby*>(menu)) lobby->SetError("Cloud login required. Join this server through Online games to approve it in your browser.");
    }
}
void ZPlayer::cloud_result_event(ZPlayer *p,char *data,int size,int)
{
    int success=0;if(size!=sizeof(success)||!data)return;memcpy(&success,data,size);
    if(success==1) p->ProcessConnect();
    else {
        if(p->cloud_disconnect_reason.empty())p->cloud_disconnect_reason="Cloud login failed. Select the server in Online games for a fresh browser approval.";
        p->AddNewsEntry(p->cloud_disconnect_reason);
        for(auto *menu:p->gui_menu_list) if(auto *lobby=dynamic_cast<GMMLobby*>(menu)) lobby->SetError(p->cloud_disconnect_reason);
    }
}

void ZPlayer::ReceiveObjective(const string &packet){
 std::istringstream input(packet.substr(11));string value;vector<string> fields;for(int i=0;i<5&&std::getline(input,value,'|');++i)fields.push_back(value);if(fields.size()!=5)return;
 auto number=[](const string&s,int &n){if(s.empty()||s.size()>10)return false;char *end=nullptr;long value=strtol(s.c_str(),&end,10);if(!end||*end||value< -1||value>1000000)return false;n=value;return true;};int step,total,time,x,y;
 if(!number(fields[0],step)||!number(fields[1],total)||!number(fields[2],time)||!number(fields[3],x)||!number(fields[4],y)||total>64||step<0||step>total)return;
 string text;std::getline(input,text);if(text.size()>512)return;if(objective_total&&step>objective_step){for(int completed=std::max(1,objective_step);completed<step;++completed)frontend.RecordEditorObjective(completed);AddNewsEntry("Objective complete: "+objective_text);BattleFeedback("Objective complete: "+objective_text);}if(step!=objective_step)BattleEventLog("objective",text,step);objective_step=step;objective_total=total;objective_remaining=time;objective_x=x;objective_y=y;objective_text=text;objective_received=ztime.ztime;
}
void ZPlayer::RenderObjectiveHUD(){
 if(!objective_total||objective_text.empty())return;auto *surface=ZSDL_Surface::GetMainSoftwareSurface();if(!surface)return;
 int remaining=objective_remaining<0?-1:std::max(0,objective_remaining-(int)std::max(0.0,ztime.ztime-objective_received));string text="Objective "+to_string(objective_step)+"/"+to_string(objective_total);if(remaining>=0)text+=" / "+to_string(remaining)+"s";text+=": "+objective_text;
 ZMenuTheme::panel(surface,8,46,std::min(560,init_w-120),34);ZMenuTheme::queueText(text.c_str(),18,56,12,{244,184,85,255},std::min(536,init_w-144));
 if(objective_x>=0&&objective_y>=0){int sx,sy;zmap.GetViewShift(sx,sy);int x=objective_x-sx,y=objective_y-sy;if(x>=0&&y>=0&&x<init_w-100&&y<init_h-36){ZMenuTheme::rect(surface,x-6,y-6,14,2,255,195,60);ZMenuTheme::rect(surface,x-6,y+6,14,2,255,195,60);ZMenuTheme::rect(surface,x-6,y-6,2,14,255,195,60);ZMenuTheme::rect(surface,x+6,y-6,2,14,255,195,60);}}
}

void ZPlayer::BattleFeedback(const string &text){if(text==battle_notice&&SDL_GetTicks()<battle_notice_until)return;battle_notice=text.substr(0,80);battle_notice_until=SDL_GetTicks()+6000;}
void ZPlayer::RenderBattleFeedback(){
    auto *surface=ZSDL_Surface::GetMainSoftwareSurface();if(!surface)return;
    if(!battle_notice.empty()&&SDL_GetTicks()<battle_notice_until){int width=std::min(560,init_w-120);ZMenuTheme::panel(surface,8,86,width,34);ZMenuTheme::queueText(battle_notice.c_str(),18,96,12,{255,215,130,255},width-20);}
    if(notification_history){int count=0;for(auto it=battle_notifications.rbegin();it!=battle_notifications.rend()&&count<notification_history_page*6+6;++it,++count){if(count<notification_history_page*6)continue;int row=count-notification_history_page*6;ZMenuTheme::panel(surface,8,132+row*28,std::min(500,init_w-120),24);string label=(it->damage?"Damage / ":"Production / ")+it->text;ZMenuTheme::queueText(label.c_str(),16,136+row*28,11,{230,235,240,255},std::min(480,init_w-140));}const char *labels[]={"Newer","Older","Close history"};for(int i=0;i<3;++i){ZMenuTheme::rect(surface,8+i*140,310,134,24,40,55,65);ZMenuTheme::queueText(labels[i],16+i*140,314,11,{240,240,240,255},124);}return;}
    ZMenuTheme::rect(surface,8,224,240,24,40,55,65);string history="Alerts history ("+to_string(battle_notifications.size())+")";ZMenuTheme::queueText(history.c_str(),16,228,11,{240,240,240,255},230);
    int row=0;for(auto it=battle_notifications.rbegin();it!=battle_notifications.rend()&&row<3;++it){if(SDL_GetTicks()>=it->until)continue;int width=std::min(420,init_w-120);ZMenuTheme::panel(surface,8,132+row*28,width,24);ZMenuTheme::queueText(it->text.c_str(),16,136+row*28,11,it->damage?SDL_Color{255,140,100,255}:SDL_Color{180,235,180,255},width-16);if(it->damage){int sx,sy;zmap.GetViewShift(sx,sy);int px=std::max(12,std::min(init_w-116,it->x-sx)),py=std::max(16,std::min(init_h-52,it->y-sy));ZMenuTheme::rect(surface,px-6,py-6,12,12,230,55,35);ZMenuTheme::queueText("!",px-2,py-6,10,{255,255,255,255},10);}++row;}
    if(zod_team_markers){int sx,sy;zmap.GetViewShift(sx,sy);int count=0;for(auto *object:object_list){if(object->IsDestroyed()||object->IsMinion()||object->GetOwner()==NULL_TEAM)continue;int x,y;object->GetCenterCords(x,y);x-=sx;y-=sy;if(x<0||y<0||x>=init_w-100||y>=init_h-36)continue;string marker="T"+to_string(object->GetOwner());ZMenuTheme::rect(surface,x-8,y-24,22,12,10,17,23);ZMenuTheme::queueText(marker.c_str(),x-6,y-24,10,{255,255,255,255},20);if(++count>=100)break;}}
}
void ZPlayer::SelectNextIdleFactory(){ZObject *first=nullptr,*next=nullptr;for(auto *object:object_list){if(object->GetOwner()!=our_team||!object->ProducesUnits()||object->IsDestroyed()||(object->GetBuildState()!=BUILDING_SELECT&&object->GetBuildState()!=BUILDING_PAUSED))continue;if(!first||object->GetRefID()<first->GetRefID())first=object;if(object->GetRefID()>last_factory_ref&&(!next||object->GetRefID()<next->GetRefID()))next=object;}if(!next)next=first;if(!next){BattleFeedback("All factories are producing or ready to deploy.");return;}last_factory_ref=next->GetRefID();int x,y;next->GetCenterCords(x,y);FocusCameraTo(x,y);BattleFeedback(unit_limit_reached[our_team]?"Production blocked: unit limit reached. Free a unit slot.":"Idle factory selected: choose production or resume its queue.");select_info.Clear();select_info.selected_list.push_back(next);select_info.SetupGroupDetails();GiveHudSelected();}

void ZPlayer::NotifyBattle(const string &text,int x,int y,bool damage){if(replay_seeking||(damage?!zod_damage_alerts:!zod_production_alerts))return;Uint64 now=SDL_GetTicks();for(auto &notice:battle_notifications)if(notice.damage==damage&&notice.text==text&&std::abs(notice.x-x)<128&&std::abs(notice.y-y)<128&&now-notice.born<(Uint64)zod_alert_cooldown_ms){auto merged=notice;merged.until=now+8000;merged.born=now;merged.x=x;merged.y=y;auto index=&notice-battle_notifications.data();battle_notifications.erase(battle_notifications.begin()+index);battle_notifications.push_back(merged);return;}if(battle_notifications.size()>=32)battle_notifications.erase(battle_notifications.begin());battle_notifications.push_back({text.substr(0,64),x,y,damage,now,now+8000});int channel=damage?1:0;if(zod_alert_sounds&&(!notification_sound_at[channel]||now-notification_sound_at[channel]>=(Uint64)zod_alert_cooldown_ms)){notification_sound_at[channel]=now?now:1;ZSoundEngine::PlayWav(RADAR_SND);}}
bool ZPlayer::ClickNotification(int x,int y){if(notification_history){if(y>=310&&y<334&&x>=8&&x<428){int button=(x-8)/140;if(button==0)notification_history_page=max(0,notification_history_page-1);if(button==1)notification_history_page=min(max(0,((int)battle_notifications.size()-1)/6),notification_history_page+1);if(button==2)notification_history=false;notification_click=true;return true;}if(x>=8&&x<std::min(508,init_w-112)&&y>=132&&y<300){int index=(int)battle_notifications.size()-1-notification_history_page*6-(y-132)/28;if(index>=0){FocusCameraTo(battle_notifications[index].x,battle_notifications[index].y);notification_click=true;return true;}}return false;}if(x>=8&&x<248&&y>=224&&y<248){notification_history=true;notification_history_page=0;notification_click=true;return true;}int row=0;for(auto it=battle_notifications.rbegin();it!=battle_notifications.rend()&&row<3;++it){if(SDL_GetTicks()>=it->until)continue;int width=std::min(420,init_w-120);if(x>=8&&x<8+width&&y>=132+row*28&&y<158+row*28){FocusCameraTo(it->x,it->y);notification_click=true;return true;}++row;}return false;}

void ZPlayer::RecordReplayPacket(int id,const char *data,int size){if(replay_active||!zod_record_replays)return;if(id==RESET_GAME||(!replay_recorder.Active()&&id==STORE_MAP&&size>=4&&data&&data[0]==0&&data[1]==0&&data[2]==0&&data[3]==0)){replay_recorder.Finish();auto folder=ZAsset_Profile()+"replays";SDL_CreateDirectory(folder.c_str());replay_record_start=SDL_GetTicks();replay_recorder.Start(folder+"/battle-"+to_string(std::time(nullptr))+"-"+to_string(SDL_GetTicksNS())+".zreplay",our_team);}if(replay_recorder.Active()&&!replay_recorder.Record(SDL_GetTicks()-replay_record_start,id,data,size)){replay_recorder.Finish();BattleFeedback("Replay recording stopped: recording limit or storage error");}}
bool ZPlayer::PlayReplay(const string &path){ZReplayTape tape;string error;if(!tape.Load(path,error)){frontend.RecordingError(error);return false;}replay_recorder.Finish();ReturnToMenu();client_socket.ClearConnection();replay_tape=std::move(tape);replay_active=true;replay_seeking=false;replay_paused=replay_ended=false;replay_clock=0;replay_speed=1;replay_index=0;replay_tick=SDL_GetTicks();replay_previous_team=our_team;SetPlayerTeam((team_type)replay_tape.team);frontend.Hide();CloseAllMenus();ztime.Resume();ztime.SetGameSpeed(1);return true;}
void ZPlayer::StopReplay(){if(!replay_active)return;replay_active=false;replay_tape={};replay_index=0;ztime.SetGameSpeed(zod_game_speed);ztime.Resume();SetPlayerTeam((team_type)replay_previous_team);ReturnToMenu();frontend.OpenRecordings();}
void ZPlayer::ProcessReplay(){auto now=SDL_GetTicks();if(!replay_paused&&!replay_seeking)replay_clock+=(now-replay_tick)*replay_speed;replay_tick=now;int budget=2000;while(replay_index<replay_tape.packets.size()&&replay_tape.packets[replay_index].ms<=replay_clock&&budget--){auto &packet=replay_tape.packets[replay_index++];ehandler.ProcessEvent(TCP_EVENT,packet.id,packet.data.empty()?nullptr:packet.data.data(),packet.data.size(),0);}if(replay_seeking&&(replay_index==replay_tape.packets.size()||replay_tape.packets[replay_index].ms>replay_clock)){replay_seeking=false;ztime.ztime=replay_clock/1000.0;}if(replay_index==replay_tape.packets.size()&&!replay_ended){replay_ended=true;replay_paused=true;BattleFeedback("Replay finished. Esc returns to recordings.");}if(replay_paused)ztime.Pause();else ztime.Resume();ztime.SetGameSpeed(replay_speed);}

void ZPlayer::SeekReplay(double milliseconds){if(!replay_active)return;double target=std::max(0.0,std::min((double)replay_tape.duration,milliseconds));ProcessResetGame();replay_index=0;replay_clock=target;replay_tick=SDL_GetTicks();replay_ended=false;replay_seeking=true;ztime.ztime=target/1000.0;}
bool ZPlayer::ReplayClick(int x,int y){if(!replay_active||!gui_menu_list.empty())return false;int width=std::min(640,init_w-16),slot=std::max(1,(width-16)/6);if(x<8||x>8+width||y<8||y>82)return false;if(y>=68){SeekReplay((x-16)/(double)std::max(1,width-16)*replay_tape.duration);return true;}if(y>=40&&y<=66){int button=(x-16)/slot;switch(button){case 0:replay_paused=!replay_paused;break;case 1:SeekReplay(replay_clock-15000);break;case 2:SeekReplay(replay_clock+15000);break;case 3:replay_speed=replay_speed>=4?.25:replay_speed*2;break;case 4:SetPlayerTeam((team_type)(our_team%4+1));break;case 5:StopReplay();break;}}return true;}
void ZPlayer::RenderReplay(){if(!replay_active)return;auto *surface=ZSDL_Surface::GetMainSoftwareSurface();int width=std::min(640,init_w-16),slot=std::max(1,(width-16)/6);ZMenuTheme::panel(surface,8,8,width,74);auto stamp=[](double ms){int seconds=ms/1000;return to_string(seconds/60)+":"+(seconds%60<10?"0":"")+to_string(seconds%60);};string label=string(replay_seeking?"SEEKING / ":"REPLAY / ")+stamp(replay_clock)+" / "+stamp(replay_tape.duration)+" / "+to_string(replay_speed).substr(0,4)+"x / "+team_type_string[our_team];ZMenuTheme::queueText(label.c_str(),16,16,13,{255,220,130,255},width-20);const string labels[]={replay_paused?"Play":"Pause","-15 seconds","+15 seconds","Speed","Team view","Exit"};for(int i=0;i<6;++i){ZMenuTheme::rect(surface,16+i*slot,40,slot-6,24,42,55,65);ZMenuTheme::queueText(labels[i].c_str(),22+i*slot,45,11,{255,255,255,255},slot-12);}ZMenuTheme::rect(surface,16,70,width-16,6,60,70,80);ZMenuTheme::rect(surface,16,70,replay_tape.duration?std::min(1.0,replay_clock/replay_tape.duration)*(width-16):0,6,244,164,65);}
