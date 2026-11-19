#include "zmovie.h"
#include "zvideo.h"
#include "zmenu_theme.h"
#include "zprefs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <cmath>
using namespace std;
vector<ZSubtitleCue> ZMovie_LoadSubtitles(const string &path){
 vector<ZSubtitleCue> cues;ifstream file(path,ios::binary|ios::ate);if(!file||file.tellg()>1024*1024)return cues;file.seekg(0);string line;
 auto stamp=[](const string&s)->double{int h,m,sec,ms;if(sscanf(s.c_str(),"%d:%d:%d,%d",&h,&m,&sec,&ms)!=4||h<0||h>99||m<0||m>59||sec<0||sec>59||ms<0||ms>999)return -1;return h*3600+m*60+sec+ms/1000.0;};
 while(cues.size()<10000&&getline(file,line)){auto arrow=line.find("-->");if(arrow==string::npos)continue;double begin=stamp(line.substr(0,arrow)),end=stamp(line.substr(arrow+3));string text,part;while(getline(file,part)){if(!part.empty()&&part.back()=='\r')part.pop_back();if(part.empty())break;if(!text.empty())text+=' ';if(text.size()+part.size()<1024)text+=part;}if(begin>=0&&end>begin&&!text.empty())cues.push_back({begin,end,text});}
 sort(cues.begin(),cues.end(),[](const ZSubtitleCue&a,const ZSubtitleCue&b){return a.begin<b.begin;});return cues;
}
namespace {
SDL_Process *decoder(const vector<string>&args){vector<const char*> argv;for(auto&s:args)argv.push_back(s.c_str());argv.push_back(nullptr);auto props=SDL_CreateProperties();SDL_SetPointerProperty(props,SDL_PROP_PROCESS_CREATE_ARGS_POINTER,argv.data());SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDIN_NUMBER,SDL_PROCESS_STDIO_NULL);SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,SDL_PROCESS_STDIO_APP);SDL_SetNumberProperty(props,SDL_PROP_PROCESS_CREATE_STDERR_NUMBER,SDL_PROCESS_STDIO_NULL);auto *p=SDL_CreateProcessWithProperties(props);SDL_DestroyProperties(props);return p;}
bool exact(SDL_IOStream *input,unsigned char *data,size_t size,atomic<bool>&stop){size_t got=0;while(got<size&&!stop){size_t n=SDL_ReadIO(input,data+got,size-got);if(n)got+=n;else if(SDL_GetIOStatus(input)==SDL_IO_STATUS_NOT_READY)SDL_Delay(2);else return false;}return got==size;}
void shutdown(SDL_Process *p){if(p)SDL_KillProcess(p,true);}
}
int ZMovie_PlayPortable(const string &path){
 auto *renderer=ZVideo_GetRenderer();auto *window=ZVideo_GetWindow();ifstream check(path);if(!renderer||!window||!check)return ZMOVIE_MISSING;
 string ffmpeg;const char *base=SDL_GetBasePath();if(base)ffmpeg=string(base)+"ffmpeg";
#ifdef _WIN32
 ffmpeg+=".exe";
#endif
 SDL_PathInfo info;if(ffmpeg.empty()||!SDL_GetPathInfo(ffmpeg.c_str(),&info))ffmpeg="ffmpeg";
 const int width=640,height=360;string filter="scale=640:360:force_original_aspect_ratio=decrease,pad=640:360:(ow-iw)/2:(oh-ih)/2:black";
 SDL_Process *video=decoder({ffmpeg,"-nostdin","-v","error","-protocol_whitelist","file,pipe","-i",path,"-an","-vf",filter,"-r","30","-pix_fmt","rgba","-f","rawvideo","pipe:1"});if(!video)return ZMOVIE_MISSING;
 SDL_Process *audio=decoder({ffmpeg,"-nostdin","-v","error","-protocol_whitelist","file,pipe","-i",path,"-vn","-ac","2","-ar","48000","-f","f32le","pipe:1"});
 bool ownAudio=!SDL_WasInit(SDL_INIT_AUDIO);if(ownAudio)SDL_InitSubSystem(SDL_INIT_AUDIO);SDL_AudioSpec spec{SDL_AUDIO_F32,2,48000};SDL_AudioStream *sound=audio?SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec,nullptr,nullptr):nullptr;if(sound){SDL_SetAudioStreamGain(sound,zod_video_volume/100.0f);}
 atomic<bool> stop{false},finished{false},audioFinished{!audio||!sound};mutex lock;condition_variable changed;deque<vector<unsigned char>> frames;
 thread videoWorker([&](){auto *input=SDL_GetProcessOutput(video);while(!stop){vector<unsigned char> frame(width*height*4);if(!input||!exact(input,frame.data(),frame.size(),stop))break;unique_lock<mutex> held(lock);changed.wait(held,[&](){return stop||frames.size()<4;});if(stop)break;frames.push_back(std::move(frame));changed.notify_all();}finished=true;changed.notify_all();});
 thread audioWorker;if(audio&&sound)audioWorker=thread([&](){auto *input=SDL_GetProcessOutput(audio);vector<unsigned char> chunk(8192);while(!stop){while(!stop&&SDL_GetAudioStreamQueued(sound)>48000*8)SDL_Delay(5);if(stop||!input)break;size_t n=SDL_ReadIO(input,chunk.data(),chunk.size());if(!n){if(SDL_GetIOStatus(input)==SDL_IO_STATUS_NOT_READY){SDL_Delay(2);continue;}break;}SDL_PutAudioStreamData(sound,chunk.data(),(int)n);}audioFinished=true;});else if(audio)shutdown(audio);
 ZVideoWindowScope windowPresentation(renderer);
 SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,width,height);SDL_Texture *subtitle=nullptr,*controls=nullptr;string lastSubtitle,lastControls;size_t dot=path.find_last_of('.');auto cues=ZMovie_LoadSubtitles(path.substr(0,dot)+".srt");
 Uint64 launched=SDL_GetTicks(),started=0,pauseAt=0,pausedMs=0,lastFrame=launched;int rendered=-1,result=texture?ZMOVIE_FINISHED:ZMOVIE_MISSING;bool paused=false,playing=false,volumeDrag=false;
 auto label=[&](SDL_Texture *&tex,string &old,const string &text,int size){string key=to_string(size)+":"+text;if(key==old)return;SDL_DestroyTexture(tex);tex=nullptr;old=key;if(!text.empty()){SDL_Surface *surface=ZMenuTheme::text(text.c_str(),size);if(surface){tex=SDL_CreateTextureFromSurface(renderer,surface);SDL_DestroySurface(surface);}}};
 while(texture&&!stop){
  SDL_Event event;while(SDL_PollEvent(&event)){
   if(event.type==SDL_EVENT_QUIT){result=ZMOVIE_SKIPPED;stop=true;SDL_PushEvent(&event);break;}
   if(event.type==SDL_EVENT_KEY_DOWN){if(event.key.key==SDLK_ESCAPE||event.key.key==SDLK_RETURN){result=ZMOVIE_SKIPPED;stop=true;}if(event.key.key==SDLK_SPACE){paused=!paused;if(paused)pauseAt=SDL_GetTicks();else {pausedMs+=SDL_GetTicks()-pauseAt;lastFrame=SDL_GetTicks();}if(sound){if(paused)SDL_PauseAudioStreamDevice(sound);else SDL_ResumeAudioStreamDevice(sound);}}if(event.key.key==SDLK_S)zod_video_subtitles=!zod_video_subtitles;if(event.key.key==SDLK_UP||event.key.key==SDLK_DOWN)zod_video_volume=max(0,min(100,zod_video_volume+(event.key.key==SDLK_UP?5:-5)));}
   if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN){int ww=1,wh=1,ow=1,oh=1;SDL_GetWindowSize(window,&ww,&wh);SDL_GetRenderOutputSize(renderer,&ow,&oh);float px=event.button.x*ow/max(1,ww),py=event.button.y*oh/max(1,wh);if(py>oh-46&&px>ow-270){volumeDrag=true;zod_video_volume=max(0,min(100,(int)((px-(ow-260))*100/240)));}else {result=ZMOVIE_SKIPPED;stop=true;}}
   if(event.type==SDL_EVENT_MOUSE_BUTTON_UP)volumeDrag=false;
   if(event.type==SDL_EVENT_MOUSE_MOTION&&volumeDrag){int ww=1,wh=1,ow=1,oh=1;SDL_GetWindowSize(window,&ww,&wh);SDL_GetRenderOutputSize(renderer,&ow,&oh);float px=event.motion.x*ow/max(1,ww);zod_video_volume=max(0,min(100,(int)((px-(ow-260))*100/240)));}
   if(sound)SDL_SetAudioStreamGain(sound,zod_video_volume/100.0f);
  }
  Uint64 now=SDL_GetTicks();if((!playing&&now-launched>15000)||(playing&&!paused&&now-lastFrame>10000)){result=ZMOVIE_MISSING;break;}
  double time=playing?((paused?pauseAt:now)-started-pausedMs)/1000.0:0;int desired=(int)(time*30);
  if(!paused&&(!playing||desired>rendered)){unique_lock<mutex> held(lock);while(!frames.empty()&&(!playing||rendered<desired)){auto frame=std::move(frames.front());frames.pop_front();held.unlock();SDL_UpdateTexture(texture,nullptr,frame.data(),width*4);lastFrame=SDL_GetTicks();++rendered;if(!playing){started=SDL_GetTicks();pausedMs=0;playing=true;if(sound)SDL_ResumeAudioStreamDevice(sound);}held.lock();changed.notify_all();}if(finished&&frames.empty()&&(!playing||rendered<desired)){if(!playing)result=ZMOVIE_MISSING;break;}}
  int outw=640,outh=480;SDL_GetRenderOutputSize(renderer,&outw,&outh);outw=max(1,outw);outh=max(80,outh);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);float scale=min(outw/(float)width,(outh-72)/(float)height);SDL_FRect dst{(outw-width*scale)/2,(outh-72-height*scale)/2,width*scale,height*scale};if(playing)SDL_RenderTexture(renderer,texture,nullptr,&dst);
  string text;if(zod_video_subtitles)for(auto &cue:cues){if(cue.begin>time)break;if(time<cue.end)text=cue.text;}
  label(subtitle,lastSubtitle,text,max(18,outh/32));if(subtitle){float w,h;SDL_GetTextureSize(subtitle,&w,&h);float fit=min(1.0f,(outw-60)/w);SDL_FRect box{(outw-w*fit)/2-10,outh-145.0f,w*fit+20,h*fit+16};SDL_SetRenderDrawColor(renderer,0,0,0,230);SDL_RenderFillRect(renderer,&box);SDL_FRect line{box.x+10,box.y+8,w*fit,h*fit};SDL_RenderTexture(renderer,subtitle,nullptr,&line);}
  string help=string(paused?"Paused / ":"")+"Space: pause  Esc: skip  S: subtitles "+(zod_video_subtitles?"on":"off")+"  Volume "+to_string(zod_video_volume)+"% (Up/Down)";label(controls,lastControls,help,max(14,outh/48));if(controls){float w,h;SDL_GetTextureSize(controls,&w,&h);float fit=min(1.0f,max(1.0f,outw-40.0f)/w);SDL_FRect line{20.0f,outh-58.0f,w*fit,h*fit};SDL_RenderTexture(renderer,controls,nullptr,&line);}SDL_FRect track{(float)outw-260,(float)outh-24,240.0f,6};SDL_SetRenderDrawColor(renderer,60,70,80,255);SDL_RenderFillRect(renderer,&track);track.w*=zod_video_volume/100.0f;SDL_SetRenderDrawColor(renderer,244,164,65,255);SDL_RenderFillRect(renderer,&track);SDL_RenderPresent(renderer);SDL_Delay(4);
 }
 stop=true;changed.notify_all();shutdown(video);shutdown(audio);videoWorker.join();if(audioWorker.joinable())audioWorker.join();SDL_DestroyAudioStream(sound);if(ownAudio)SDL_QuitSubSystem(SDL_INIT_AUDIO);if(video){SDL_WaitProcess(video,true,nullptr);SDL_DestroyProcess(video);}if(audio){SDL_WaitProcess(audio,true,nullptr);SDL_DestroyProcess(audio);}SDL_DestroyTexture(texture);SDL_DestroyTexture(subtitle);SDL_DestroyTexture(controls);ZPrefs_Save();SDL_FlushEvents(SDL_EVENT_KEY_DOWN,SDL_EVENT_KEY_UP);SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION,SDL_EVENT_MOUSE_WHEEL);return result;
}
