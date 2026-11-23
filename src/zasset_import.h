#ifndef ZASSET_IMPORT_H
#define ZASSET_IMPORT_H
#include <SDL3/SDL.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The profile is shared by source builds and installed applications.
std::string ZAsset_Profile();
enum class ZDiagnosticEvent { ImportFailed, DeploymentFailed, VideoFailed };
void ZAsset_RecordDiagnostic(ZDiagnosticEvent event);
struct ZAssetCheck { std::string category; int present=0,total=0; };
std::vector<ZAssetCheck> ZAsset_CheckContent();
bool ZAsset_ExportDiagnostics(std::string &path,const std::string &phase);
bool ZAsset_RuntimeReady();
std::string ZAsset_TerrainDirectory(int terrain);
bool ZAsset_ImportLevels(const std::string &folder,std::string &message);
int ZAsset_RunSetup();
std::string ZAsset_FindHelper(const std::string &base);
std::string ZAsset_OverlayPath(const std::string &relative);
class ZAssetImport {
public:
    ZAssetImport();
    ~ZAssetImport();
    void Choose(SDL_Window *window);
    void ChooseLevels(SDL_Window *window);
    void Poll();
    void Start();
    void Cancel();
    bool Busy() const { return process || dialog_pending; }
    bool Ready() const { return !Busy() && phase == "ready"; }
    std::string phase, message, source;
    int percent = 0;
private:
    struct Selection { std::mutex mutex; bool returned=false; std::string path,error; };
    std::shared_ptr<Selection> selection;
    SDL_Process *process = nullptr;
    bool choosing_levels=false;
    bool inspecting=false, dialog_pending=false, cancel_requested=false;
    std::string job, id, state_file, cancel_file;
    void Launch(bool inspect);
};
#endif
