#include <stdio.h>
#include <string>
#include <sstream>
#include <wchar.h>
#include <util/platform.h>
#include <util/profiler.hpp>
#include <util/dstr.h>
#include <util/lexer.h>

#include <QGuiApplication>
#include <QMouseEvent>
#include <signal.h>
#include <pthread.h>
#include <obs.hpp>

#include "qt-wrappers.hpp"
#include "mainwindow.h"
#include "window-basic-main.hpp"
#include "main.h"
#include "platform.hpp"
#include <fstream>
#include "window-projector.hpp"
#include "window-basic-interaction.hpp"

#include <iostream>
#include "ui-config.h.in"

//#include <windows.h>


using namespace std;

static log_handler_t def_log_handler;
static string currentLogFile;

bool portable_mode = false;
string opt_starting_collection;
string opt_starting_profile;
string opt_starting_scene;

QObject *CreateShortcutFilter()
{
    return new OBSEventFilter([](QObject *obj, QEvent *event) {
        auto mouse_event = [](QMouseEvent &event) {
            if (!App()->HotkeysEnabledInFocus())
                return true;

            obs_key_combination_t hotkey = {0, OBS_KEY_NONE};
            bool pressed = event.type() == QEvent::MouseButtonPress;

            switch (event.button()) {
            case Qt::NoButton:
            case Qt::LeftButton:
            case Qt::RightButton:
            case Qt::AllButtons:
            case Qt::MouseButtonMask:
                return false;

            case Qt::MidButton:
                hotkey.key = OBS_KEY_MOUSE3;
                break;

#define MAP_BUTTON(i, j)                       \
            case Qt::ExtraButton##i:               \
    hotkey.key = OBS_KEY_MOUSE##j; \
    break;
                MAP_BUTTON(1, 4);
                MAP_BUTTON(2, 5);
                MAP_BUTTON(3, 6);
                MAP_BUTTON(4, 7);
                MAP_BUTTON(5, 8);
                MAP_BUTTON(6, 9);
                MAP_BUTTON(7, 10);
                MAP_BUTTON(8, 11);
                MAP_BUTTON(9, 12);
                MAP_BUTTON(10, 13);
                MAP_BUTTON(11, 14);
                MAP_BUTTON(12, 15);
                MAP_BUTTON(13, 16);
                MAP_BUTTON(14, 17);
                MAP_BUTTON(15, 18);
                MAP_BUTTON(16, 19);
                MAP_BUTTON(17, 20);
                MAP_BUTTON(18, 21);
                MAP_BUTTON(19, 22);
                MAP_BUTTON(20, 23);
                MAP_BUTTON(21, 24);
                MAP_BUTTON(22, 25);
                MAP_BUTTON(23, 26);
                MAP_BUTTON(24, 27);
#undef MAP_BUTTON
            }

            hotkey.modifiers = TranslateQtKeyboardEventModifiers(
                                    event.modifiers());

            obs_hotkey_inject_event(hotkey, pressed);
            return true;
        };

        auto key_event = [&](QKeyEvent *event) {
            if (!App()->HotkeysEnabledInFocus())
                return true;

            QDialog *dialog = qobject_cast<QDialog *>(obj);

            obs_key_combination_t hotkey = {0, OBS_KEY_NONE};
            bool pressed = event->type() == QEvent::KeyPress;

            switch (event->key()) {
            case Qt::Key_Shift:
            case Qt::Key_Control:
            case Qt::Key_Alt:
            case Qt::Key_Meta:
                break;

#ifdef __APPLE__
            case Qt::Key_CapsLock:
                // kVK_CapsLock == 57
                hotkey.key = obs_key_from_virtual_key(57);
                pressed = true;
                break;
#endif

            case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Return:
                if (dialog && pressed)
                    return false;
                /* Falls through. */
            default:
                hotkey.key = obs_key_from_virtual_key(
                                        event->nativeVirtualKey());
            }

            hotkey.modifiers = TranslateQtKeyboardEventModifiers(
                                    event->modifiers());

            obs_hotkey_inject_event(hotkey, pressed);
            return true;
        };

        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
            return mouse_event(*static_cast<QMouseEvent *>(event));

            /*case QEvent::MouseButtonDblClick:
                  case QEvent::Wheel:*/
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            return key_event(static_cast<QKeyEvent *>(event));

        default:
            return false;
        }
    });
}


bool remuxAfterRecord = false;
string remuxFilename;

static auto ProfilerNameStoreRelease = [](profiler_name_store_t *store){
    profiler_name_store_free(store);
};
using profilerNameStore = std::unique_ptr<profiler_name_store_t, decltype(ProfilerNameStoreRelease)>;
profilerNameStore CreateNameStore()
{
    return  profilerNameStore{profiler_name_store_create(),ProfilerNameStoreRelease};
}

inline void OBSApp::ResetHotKeyState(bool inFocus)
{
   obs_hotkey_enable_background_press(
               (inFocus && enableHotkeysInFocus)||
               (!inFocus && enableHotkeysOutOfFocus));
}
 static const char *run_program_init = "run_program_init";
#define BASE_PATH "../.."
#define CONFIG_PATH BASE_PATH "/config"

#ifndef OBS_UNIX_STRUCTURE
#define OBS_UNIX_STRUCTURE 0
#endif

static bool StartupOBS(const char *locale, profiler_name_store_t *store)
{
    char path[512];
    if (GetConfigPath(path, sizeof(path),"obs-studio/plugin_config") <= 0)
        return false;
    return obs_startup(locale,path, store);
}

int GetConfigPath(char *path, size_t size, const char *name)
{
    if (!OBS_UNIX_STRUCTURE && portable_mode){
       if (name && *name) {
           return snprintf(path, size, CONFIG_PATH "/%S",(wchar_t *)name);
       }else{
           return snprintf(path, size,CONFIG_PATH);
       }
    } else {
        return os_get_config_path(path, size, name);
    }

}

vector <pair<string,string>> GetLocaleNames()
{
   string path;
   if (!GetDataFilePath("locale.ini", path))
       throw "Could not find locale.ini path";
   ConfigFile ini;
   if (ini.Open(path.c_str(),CONFIG_OPEN_EXISTING) != 0)
       throw "Could not open local.ini";
   size_t sections = config_num_sections(ini);

   vector<pair<string,string>> names;
   names.reserve(sections);
   for (size_t i =0; i<sections; i++)
   {
       const char *tag = config_get_section(ini,i);
       const char *name = config_get_string(ini,tag,"Name");
       names.emplace_back(tag, name);
   }

   return names;
}


char *GetConfigPathPtr(const char *name)
{
    if (!OBS_UNIX_STRUCTURE && portable_mode)
    {
        char path[512];
        if(snprintf(path,sizeof(path), CONFIG_PATH"/%s", name) > 0)
        {
            return bstrdup(path);
        }else {
            return NULL;
        }
   }else{
            return os_get_config_path_ptr(name);
   }


}

static bool do_mkdir(const char *path)
{
    if (os_mkdirs(path) == MKDIR_ERROR) {
        OBSErrorBox(NULL, "Failed to create directory %s", path);
        return false;
    }

    return true;
}

static bool MakeUserDirs()
  {
     blog(LOG_INFO,"MakeUserDirs");
      char path[512];

      if (GetConfigPath(path, sizeof(path), "obs-studio/basic") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;

      if (GetConfigPath(path, sizeof(path), "obs-studio/logs") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;

      if (GetConfigPath(path, sizeof(path), "obs-studio/profiler_data") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;

  #ifdef _WIN32
      if (GetConfigPath(path, sizeof(path), "obs-studio/crashes") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;

      if (GetConfigPath(path, sizeof(path), "obs-studio/updates") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;
  #endif

      if (GetConfigPath(path, sizeof(path), "obs-studio/plugin_config") <= 0)
          return false;
      if (!do_mkdir(path))
          return false;
      blog(LOG_INFO,"MakeUserDirs-----End");
      return true;
  }

bool OBSApp::OBSInit()
{
    ProfileScope("OBSApp::OBSInit");
    setAttribute(Qt::AA_UseHighDpiPixmaps);
    //qRegisterMetaType<Voidfunc>();

    if (!StartupOBS(locale.c_str(),GetProfilerNameStore()))
        return false;
   setQuitOnLastWindowClosed(false);

    mainWindow = new OBSBasic();
    mainWindow->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(mainWindow, SIGNAL(destroyed()), this, SLOT(quit()));

    mainWindow->OBSInit();

    connect(this, &QGuiApplication::applicationStateChanged,
            [this](Qt::ApplicationState state){
        ResetHotKeyState(state == Qt::ApplicationActive);
    });
    ResetHotKeyState(applicationState() == Qt::ApplicationActive);

    return true;
}

OBSApp::OBSApp(int &argc, char **argv, profiler_name_store_t *store)
    : QApplication(argc,argv), profilerNameStore(store)
{
    sleepInhibitor = os_inhibit_sleep_create("OBS Video/audio");

}
OBSApp::~OBSApp()
{
    os_inhibit_sleep_set_active(sleepInhibitor, false);
    os_inhibit_sleep_destroy(sleepInhibitor);

}

void OBSApp::AppInit()
{
   // ProfileScope("OBSApp::AppInit");
    blog(LOG_INFO,"OBSApp::AppInit()--------------Start");
    if (!InitApplicationBundle())
        throw "Faliled to initialize application bundle";
    if (!MakeUserDirs())
        throw "Failed to create required user directories";
    if(!InitGlobalConfig())
    {
        blog(LOG_ERROR,"OBSApp::AppInit---------------error!");
        throw "Failed to initialize global config";

    }
    blog(LOG_INFO,"OBSApp::AppInit()--------------End");

}

string OBSApp::GetVersionString() const
{
    stringstream ver;
    ver << LIBOBS_API_MAJOR_VER;
    ver << "(";
    return ver.str();

}
void OBSApp::DisableHotkeys()
{
  enableHotkeysInFocus = false;
  enableHotkeysOutOfFocus = false;
  ResetHotKeyState(applicationState() == Qt::ApplicationActive);
}
void OBSApp::UpdateHotkeyFocusSetting(bool resetState)
{
    enableHotkeysInFocus = true;
    enableHotkeysOutOfFocus = true;

    const char *hotkeyFocusType =
                    config_get_string(globalConfig, "General", "HotkeyFocusType");

    if (astrcmpi(hotkeyFocusType, "DisableHotkeysInFocus") == 0) {
        enableHotkeysInFocus = false;
    } else if (astrcmpi(hotkeyFocusType, "DisableHotkeysOutOfFocus") == 0) {
        enableHotkeysOutOfFocus = false;
    }

    if (resetState)
        ResetHotkeyState(applicationState() == Qt::ApplicationActive);
}
inline void OBSApp::ResetHotkeyState(bool inFocus)
{
    obs_hotkey_enable_background_press(
                            (inFocus && enableHotkeysInFocus) ||
                            (!inFocus && enableHotkeysOutOfFocus));
}

#define INPUT_AUDIO_SOURCE "pulse_input_capture"
#define OUTPUT_AUDIO_SOURCE "pulse_output_capture"
const char *OBSApp::InputAudioSource() const
{
    return INPUT_AUDIO_SOURCE;

}
const char *OBSApp::OutputAudioSource() const
{
   return OUTPUT_AUDIO_SOURCE;
}

static string GetProfileDirFromName(const char *name)
{
     string outputPath;
     os_glob_t *glob;
     char path[512];

     if (GetConfigPath(path, sizeof(path), "obs-studio/basic/profiles") <= 0)
         return outputPath;

     strcat(path, "/*");

     if (os_glob(path, 0, &glob) != 0)
         return outputPath;

     for (size_t i = 0; i < glob->gl_pathc; i++) {
         struct os_globent ent = glob->gl_pathv[i];
         if (!ent.directory)
             continue;

         strcpy(path, ent.path);
         strcat(path, "/basic.ini");

         ConfigFile config;
         if (config.Open(path, CONFIG_OPEN_EXISTING) != 0)
             continue;

         const char *curName =
             config_get_string(config, "General", "Name");
         if (astrcmpi(curName, name) == 0) {
             outputPath = ent.path;
             break;
         }
     }

     os_globfree(glob);

     if (!outputPath.empty()) {
         replace(outputPath.begin(), outputPath.end(), '\\', '/');
         const char *start = strrchr(outputPath.c_str(), '/');
         if (start)
             outputPath.erase(0, start - outputPath.c_str() + 1);
     }

     return outputPath;
}

static string GetSceneCollectionFileFromName(const char *name)
{
    string outputPath;
    os_glob_t *glob;
    char path[512];

    if (GetConfigPath(path, sizeof(path), "obs-studio/basic/scenes") <= 0)
        return outputPath;

    strcat(path, "/*.json");

    if (os_glob(path, 0, &glob) != 0)
        return outputPath;

    for (size_t i = 0; i < glob->gl_pathc; i++) {
        struct os_globent ent = glob->gl_pathv[i];
        if (ent.directory)
            continue;

        obs_data_t *data =
                        obs_data_create_from_json_file_safe(ent.path, "bak");
        const char *curName = obs_data_get_string(data, "name");

        if (astrcmpi(name, curName) == 0) {
            outputPath = ent.path;
            obs_data_release(data);
            break;
        }

        obs_data_release(data);
    }

    os_globfree(glob);

    if (!outputPath.empty()) {
        outputPath.resize(outputPath.size() - 5);
        replace(outputPath.begin(), outputPath.end(), '\\', '/');
        const char *start = strrchr(outputPath.c_str(), '/');
        if (start)
            outputPath.erase(0, start - outputPath.c_str() + 1);
    }

    return outputPath;
}



bool OBSApp::InitGlobalConfig()
{
    blog(LOG_INFO,"OBSApp::InitGlobalConfig");
    char path[512];
    bool changed = false;

    int len = GetConfigPath(path, sizeof(path), "obs-studio/global.ini");
    if (len <= 0) {
        return false;
    }

    int errorcode = globalConfig.Open(path, CONFIG_OPEN_ALWAYS);
    if (errorcode != CONFIG_SUCCESS) {
        OBSErrorBox(NULL, "Failed to open global.ini: %d", errorcode);
        return false;
    }

    if (!opt_starting_collection.empty()) {
         blog(LOG_INFO,"OBSApp::InitGlobalConfig..............opt_starting_collection.empty()");
        string path = GetSceneCollectionFileFromName(
                                opt_starting_collection.c_str());
        if (!path.empty()) {
            config_set_string(globalConfig, "Basic",
                              "SceneCollection",
                              opt_starting_collection.c_str());
            config_set_string(globalConfig, "Basic",
                              "SceneCollectionFile", path.c_str());
            changed = true;
        }
    }

    if (!opt_starting_profile.empty()) {
        string path =
                        GetProfileDirFromName(opt_starting_profile.c_str());
        if (!path.empty()) {
            config_set_string(globalConfig, "Basic", "Profile",
                              opt_starting_profile.c_str());
            config_set_string(globalConfig, "Basic", "ProfileDir",
                              path.c_str());
            changed = true;
        }
    }

    uint32_t lastVersion =
                    config_get_int(globalConfig, "General", "LastVersion");

    if (!config_has_user_value(globalConfig, "General", "Pre19Defaults")) {
        bool useOldDefaults = lastVersion &&
                        lastVersion <
                        MAKE_SEMANTIC_VERSION(19, 0, 0);

        config_set_bool(globalConfig, "General", "Pre19Defaults",
                        useOldDefaults);
        changed = true;
    }

    if (!config_has_user_value(globalConfig, "General", "Pre21Defaults")) {
        bool useOldDefaults = lastVersion &&
                        lastVersion <
                        MAKE_SEMANTIC_VERSION(21, 0, 0);

        config_set_bool(globalConfig, "General", "Pre21Defaults",
                        useOldDefaults);
        changed = true;
    }

    if (!config_has_user_value(globalConfig, "General", "Pre23Defaults")) {
        bool useOldDefaults = lastVersion &&
                        lastVersion <
                        MAKE_SEMANTIC_VERSION(23, 0, 0);

        config_set_bool(globalConfig, "General", "Pre23Defaults",
                        useOldDefaults);
        changed = true;
    }

#define PRE_24_1_DEFS "Pre24.1Defaults"
    if (!config_has_user_value(globalConfig, "General", PRE_24_1_DEFS)) {
        bool useOldDefaults = lastVersion &&
                        lastVersion <
                        MAKE_SEMANTIC_VERSION(24, 1, 0);

        config_set_bool(globalConfig, "General", PRE_24_1_DEFS,
                        useOldDefaults);
        changed = true;
    }
#undef PRE_24_1_DEFS

    if (config_has_user_value(globalConfig, "BasicWindow",
                              "MultiviewLayout")) {
        const char *layout = config_get_string(
                                globalConfig, "BasicWindow", "MultiviewLayout");
        changed |= UpdatePre22MultiviewLayout(layout);
    }

    if (lastVersion && lastVersion < MAKE_SEMANTIC_VERSION(24, 0, 0)) {
        bool disableHotkeysInFocus = config_get_bool(
                                globalConfig, "General", "DisableHotkeysInFocus");
        if (disableHotkeysInFocus)
            config_set_string(globalConfig, "General",
                              "HotkeyFocusType",
                              "DisableHotkeysInFocus");
        changed = true;
    }

    if (changed)
        config_save_safe(globalConfig, "tmp", nullptr);

    return InitGlobalConfigDefaults();
}
#define DEFAULT_LANG "en-US"

 bool OBSApp::InitGlobalConfigDefaults()
 {
     blog(LOG_INFO,"OBSApp::InitGlobalConfigDefaults");
     config_set_default_string(globalConfig, "General", "Language",
                               DEFAULT_LANG);
     config_set_default_uint(globalConfig, "General", "MaxLogs", 10);
     config_set_default_int(globalConfig, "General", "InfoIncrement", -1);
     config_set_default_string(globalConfig, "General", "ProcessPriority",
                               "Normal");
     config_set_default_bool(globalConfig, "General", "EnableAutoUpdates",
                             true);

#if _WIN32
          config_set_default_string(globalConfig, "Video", "Renderer",
                                       388                   "Direct3D 11");
#else
     config_set_default_string(globalConfig, "Video", "Renderer", "OpenGL");
#endif

     config_set_default_bool(globalConfig, "BasicWindow", "PreviewEnabled",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "PreviewProgramMode", false);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "SceneDuplicationMode", true);
     config_set_default_bool(globalConfig, "BasicWindow", "SwapScenesMode",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow", "SnappingEnabled",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow", "ScreenSnapping",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow", "SourceSnapping",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow", "CenterSnapping",
                             false);
     config_set_default_double(globalConfig, "BasicWindow", "SnapDistance",
                               10.0);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "RecordWhenStreaming", false);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "KeepRecordingWhenStreamStops", false);
     config_set_default_bool(globalConfig, "BasicWindow", "SysTrayEnabled",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "SysTrayWhenStarted", false);
     config_set_default_bool(globalConfig, "BasicWindow", "SaveProjectors",
                             false);
     config_set_default_bool(globalConfig, "BasicWindow", "ShowTransitions",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "ShowListboxToolbars", true);
     config_set_default_bool(globalConfig, "BasicWindow", "ShowStatusBar",
                             true);
     config_set_default_bool(globalConfig, "BasicWindow", "StudioModeLabels",
                             true);

     if (!config_get_bool(globalConfig, "General", "Pre21Defaults")) {
         config_set_default_string(globalConfig, "General",
                                   "CurrentTheme", DEFAULT_THEME);
     }

     config_set_default_string(globalConfig, "General", "HotkeyFocusType",
                               "NeverDisableHotkeys");
     config_set_default_bool(globalConfig, "BasicWindow",
                             "VerticalVolControl", false);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "MultiviewMouseSwitch", true);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "MultiviewDrawNames", true);
     config_set_default_bool(globalConfig, "BasicWindow",
                             "MultiviewDrawAreas", true);
#ifdef _WIN32
     uint32_t winver = GetWindowsVersion();

     config_set_default_bool(globalConfig, "Audio", "DisableAudioDucking",
                             true);
     config_set_default_bool(globalConfig, "General", "BrowserHWAccel",
                             winver > 0x601);
#endif

#ifdef __APPLE__
     config_set_default_bool(globalConfig, "Video", "DisableOSXVSync", true);
     config_set_default_bool(globalConfig, "Video", "ResetOSXVSyncOnExit",
                             true);
#endif
     blog(LOG_INFO,"OBSApp::InitGlobalConfigDefaults----------------End");
     return true;
}

bool OBSApp::SetTheme(std::string name, std::string path)
{






  return false;
}







void ctrlc_handler(int s)
{
    UNUSED_PARAMETER(s)    ;
    OBSBasic *main = reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
    main->close();
}
static auto SnapshotRelease = [](profiler_snapshot_t *snap)
{
    profile_snapshot_free(snap);
};


using ProfilerSnapshot =
std::unique_ptr<profiler_snapshot_t, decltype(SnapshotRelease)>;


ProfilerSnapshot GetSnapshot()
{
    return ProfilerSnapshot{profile_snapshot_create(),SnapshotRelease};
}

static void SaveProfilerData(const ProfilerSnapshot &snap)
{
    if (currentLogFile.empty())
        return;

    auto pos = currentLogFile.rfind('.');
    if (pos == currentLogFile.npos)
        return;

#define LITERAL_SIZE(x) x, (sizeof(x) - 1)
    ostringstream dst;
    dst.write(LITERAL_SIZE("obs-studio/profiler_data/"));
    dst.write(currentLogFile.c_str(), pos);
    dst.write(LITERAL_SIZE(".csv.gz"));
#undef LITERAL_SIZE

    BPtr<char> path = GetConfigPathPtr(dst.str().c_str());
    if (!profiler_snapshot_dump_csv_gz(snap.get(), path))
        blog(LOG_WARNING, "Could not save profiler data to '%s'",
             static_cast<const char *>(path));
}

static auto ProfilerFree = [](void *) {

    profiler_stop();

    auto snap = GetSnapshot();

    profiler_print(snap.get());
    profiler_print_time_between_calls(snap.get());

    SaveProfilerData(snap);

    profiler_free();
};

string GenerateSpecifiedFilename(const char *extension, bool noSpace, const char *format)
{
   OBSBasic *main = reinterpret_cast<OBSBasic *> (App()->GetMainWindow());
   bool autoRemux = config_get_bool(main->Config(), "Video", "AutoRemux");
   if ((strcmp(extension, "mp4") == 0) && autoRemux)
       extension = "mkv";
   BPtr<char> filename = os_generate_formatted_filename(extension, !noSpace, format);
   remuxFilename = string(filename);
   remuxAfterRecord = autoRemux;

   return string(filename);
}
bool OBSApp::UpdatePre22MultiviewLayout(const char *layout)
{
    if (!layout)
        return false;

    if (astrcmpi(layout, "horizontaltop") == 0) {
        config_set_int( globalConfig, "BasicWindow", "MultiviewLayout",static_cast<int>(MultiviewLayout::HORIZONTAL_TOP_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "horizontalbottom") == 0) {
        config_set_int( globalConfig, "BasicWindow", "MultiviewLayout",static_cast<int>(MultiviewLayout::HORIZONTAL_BOTTOM_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "verticalleft") == 0) {
        config_set_int(
                                globalConfig, "BasicWindow", "MultiviewLayout",
                                static_cast<int>(
                                        MultiviewLayout::VERTICAL_LEFT_8_SCENES));
        return true;
    }

    if (astrcmpi(layout, "verticalright") == 0) {
        config_set_int(
                                globalConfig, "BasicWindow", "MultiviewLayout",
                                static_cast<int>(
                                        MultiviewLayout::VERTICAL_RIGHT_8_SCENES));
        return true;
    }

    return false;
}
static bool get_token(lexer *lex, string &str, base_token_type type)
{
    base_token token;
    if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
        return false;
    if (token.type != type)
        return false;

    str.assign(token.text.array, token.text.len);
    return true;
}
static bool expect_token(lexer *lex, const char *str, base_token_type type)
 {
     base_token token;
     if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
         return false;
     if (token.type != type)
         return false;

     return strref_cmp(&token.text, str) == 0;
 }

 static uint64_t convert_log_name(bool has_prefix, const char *name)
 {
     BaseLexer lex;
     string year, month, day, hour, minute, second;
     lexer_start(lex, name);

     if (has_prefix) {
         string temp;
         if (!get_token(lex, temp, BASETOKEN_ALPHA))
             return 0;
     }
     if (!get_token(lex, year, BASETOKEN_DIGIT))
         return 0;
     if (!expect_token(lex, "-", BASETOKEN_OTHER))
         return 0;
     if (!get_token(lex, month, BASETOKEN_DIGIT))
         return 0;
     if (!expect_token(lex, "-", BASETOKEN_OTHER))
         return 0;
     if (!get_token(lex, day, BASETOKEN_DIGIT))
         return 0;
     if (!get_token(lex, hour, BASETOKEN_DIGIT))
         return 0;
     if (!expect_token(lex, "-", BASETOKEN_OTHER))
         return 0;
     if (!get_token(lex, minute, BASETOKEN_DIGIT))
         return 0;
     if (!expect_token(lex, "-", BASETOKEN_OTHER))
         return 0;
     if (!get_token(lex, second, BASETOKEN_DIGIT))
         return 0;
     stringstream timestring;
     timestring << year << month << day << hour << minute << second;
     return std::stoull(timestring.str());
 }

static void delete_oldest_file(bool has_prefix, const char *location)
 {
     BPtr<char> logDir(GetConfigPathPtr(location));
     string oldestLog;
     uint64_t oldest_ts = (uint64_t)-1;
     struct os_dirent *entry;

     unsigned int maxLogs = (unsigned int)config_get_uint(
         App()->GlobalConfig(), "General", "MaxLogs");

     os_dir_t *dir = os_opendir(logDir);
     if (dir) {
         unsigned int count = 0;

         while ((entry = os_readdir(dir)) != NULL) {
             if (entry->directory || *entry->d_name == '.')
                 continue;

             uint64_t ts =
                 convert_log_name(has_prefix, entry->d_name);

             if (ts) {
                 if (ts < oldest_ts) {
                     oldestLog = entry->d_name;
                     oldest_ts = ts;
                 }

                 count++;
             }
         }

         os_closedir(dir);

         if (count > maxLogs) {
             stringstream delPath;

             delPath << logDir << "/" << oldestLog;
             os_unlink(delPath.str().c_str());
         }
     }
 }
static bool move_reconnect_settings(ConfigFile &config, const char *sec)
 {
     bool changed = false;

     if (config_has_user_value(config, sec, "Reconnect")) {
         bool reconnect = config_get_bool(config, sec, "Reconnect");
         config_set_bool(config, "Output", "Reconnect", reconnect);
         changed = true;
     }
     if (config_has_user_value(config, sec, "RetryDelay")) {
         int delay = (int)config_get_uint(config, sec, "RetryDelay");
         config_set_uint(config, "Output", "RetryDelay", delay);
         changed = true;
     }
     if (config_has_user_value(config, sec, "MaxRetries")) {
         int retries = (int)config_get_uint(config, sec, "MaxRetries");
         config_set_uint(config, "Output", "MaxRetries", retries);
         changed = true;
     }

     return changed;
 }

static bool update_reconnect(ConfigFile &config)
 {
    blog(LOG_INFO,"update_reconnect----------------End");
     if (!config_has_user_value(config, "Output", "Mode"))
         return false;

     const char *mode = config_get_string(config, "Output", "Mode");
     if (!mode)
         return false;

     const char *section = (strcmp(mode, "Advanced") == 0) ? "AdvOut"
                                   : "SimpleOutput";

     if (move_reconnect_settings(config, section)) {
         config_remove_value(config, "SimpleOutput", "Reconnect");
         config_remove_value(config, "SimpleOutput", "RetryDelay");
         config_remove_value(config, "SimpleOutput", "MaxRetries");
         config_remove_value(config, "AdvOut", "Reconnect");
         config_remove_value(config, "AdvOut", "RetryDelay");
         config_remove_value(config, "AdvOut", "MaxRetries");
         return true;
     }

     return false;
 }

static bool update_ffmpeg_output(ConfigFile &config)
 {
     if (config_has_user_value(config, "AdvOut", "FFOutputToFile"))
         return false;

     const char *url = config_get_string(config, "AdvOut", "FFURL");
     if (!url)
         return false;

     bool isActualURL = strstr(url, "://") != nullptr;
     if (isActualURL)
         return false;

     string urlStr = url;
     string extension;

     for (size_t i = urlStr.length(); i > 0; i--) {
         size_t idx = i - 1;

         if (urlStr[idx] == '.') {
             extension = &urlStr[i];
         }

         if (urlStr[idx] == '\\' || urlStr[idx] == '/') {
             urlStr[idx] = 0;
             break;
         }
     }

     if (urlStr.empty() || extension.empty())
         return false;

     config_remove_value(config, "AdvOut", "FFURL");
     config_set_string(config, "AdvOut", "FFFilePath", urlStr.c_str());
     config_set_string(config, "AdvOut", "FFExtension", extension.c_str());
     config_set_bool(config, "AdvOut", "FFOutputToFile", true);
     return true;
 }

static void convert_x264_settings(obs_data_t *data)
 {
     bool use_bufsize = obs_data_get_bool(data, "use_bufsize");

     if (use_bufsize) {
         int buffer_size = (int)obs_data_get_int(data, "buffer_size");
         if (buffer_size == 0)
             obs_data_set_string(data, "rate_control", "CRF");
     }
 }


static void convert_14_2_encoder_setting(const char *encoder, const char *file)
 {
     obs_data_t *data = obs_data_create_from_json_file_safe(file, "bak");
     obs_data_item_t *cbr_item = obs_data_item_byname(data, "cbr");
     obs_data_item_t *rc_item = obs_data_item_byname(data, "rate_control");
     bool modified = false;
     bool cbr = true;

     if (cbr_item) {
         cbr = obs_data_item_get_bool(cbr_item);
         obs_data_item_unset_user_value(cbr_item);

         obs_data_set_string(data, "rate_control", cbr ? "CBR" : "VBR");

         modified = true;
     }

     if (!rc_item && astrcmpi(encoder, "obs_x264") == 0) {
         if (!cbr_item)
             obs_data_set_string(data, "rate_control", "CBR");
         else if (!cbr)
             convert_x264_settings(data);

         modified = true;
     }

     if (modified)
         obs_data_save_json_safe(data, file, "tmp", "bak");

     obs_data_item_release(&rc_item);
     obs_data_item_release(&cbr_item);
     obs_data_release(data);
 }

static void upgrade_settings(void)
{
    blog(LOG_INFO,"update_settings----------------Start");
    char path[512];
    int pathlen = GetConfigPath(path, 512, "obs-studio/basic/profiles");

    if (pathlen <= 0)
        return;
    blog(LOG_INFO,"update_settings----------------os_file_exists");

    if (!os_file_exists(path))
        return;

    os_dir_t *dir = os_opendir(path);
    if (!dir)
        return;

    struct os_dirent *ent = os_readdir(dir);

    while (ent) {
        if (ent->directory && strcmp(ent->d_name, ".") != 0 &&
                        strcmp(ent->d_name, "..") != 0) {
            strcat(path, "/");
            strcat(path, ent->d_name);
            strcat(path, "/basic.ini");

            ConfigFile config;
            int ret;

            ret = config.Open(path, CONFIG_OPEN_EXISTING);
            if (ret == CONFIG_SUCCESS) {
                if (update_ffmpeg_output(config) ||
                                update_reconnect(config)) {
                    config_save_safe(config, "tmp",
                                     nullptr);
                }
            }

            if (config) {
                const char *sEnc = config_get_string(
                                        config, "AdvOut", "Encoder");
                const char *rEnc = config_get_string(
                                        config, "AdvOut", "RecEncoder");

                /* replace "cbr" option with "rate_control" for
          * each profile's encoder data */
                path[pathlen] = 0;
                strcat(path, "/");
                strcat(path, ent->d_name);
                strcat(path, "/recordEncoder.json");
                convert_14_2_encoder_setting(rEnc, path);

                path[pathlen] = 0;
                strcat(path, "/");
                strcat(path, ent->d_name);
                strcat(path, "/streamEncoder.json");
                convert_14_2_encoder_setting(sEnc, path);
            }

            path[pathlen] = 0;
        }

        ent = os_readdir(dir);
    }
    blog(LOG_INFO,"update_settings----------------End");
    os_closedir(dir);
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE,SIG_IGN);

    struct sigaction sig_handler;

    sig_handler.sa_handler = ctrlc_handler;
    sigemptyset(&sig_handler.sa_mask);
    sig_handler.sa_flags = 0;
    sigaction(SIGINT,&sig_handler, NULL);

    sigset_t sigpipe_mask;
    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask,SIGPIPE);
    sigset_t saved_mask;
    if(pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1)
    {
        perror("pthread_sigmask");
        exit(1);
    }
    base_get_log_handler(&def_log_handler,nullptr);
    obs_set_cmdline_args(argc,argv);

    upgrade_settings();
    //run_program
    auto profilerNameStore = CreateNameStore();

    std::unique_ptr<void,decltype(ProfilerFree)> prof_release(static_cast<void *>(&ProfilerFree), ProfilerFree);

    profiler_start();
    profile_register_root(run_program_init,0);

    ScopeProfiler prof{run_program_init};
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    OBSApp program(argc, argv,profilerNameStore.get());
    try{
       // bool created_log = false;

        program.AppInit();

        delete_oldest_file(false, "obs-studio/profiler_data");

        if (!program.OBSInit())
            return 0;

        prof.Stop();

        return  program.exec();

    } catch (const char *error){
         //blog(LOG_ERROR, "%S", error);
        OBSErrorBox(nullptr, "%s", error);
    }
    blog(LOG_INFO, "Number of memory leaks: %ld", bnum_allocs());

    base_set_log_handler(nullptr, nullptr);
    return 0;

}
const char *OBSApp::GetRenderModule() const
 {
     const char *renderer =
         config_get_string(globalConfig, "Video", "Renderer");

   //yy  return (astrcmpi(renderer, "Direct3D 11") == 0) ? DL_D3D11 : DL_OPENGL;

     return (astrcmpi(renderer, "Direct3D 11") == 0) ? "1" :"0" ;
 }
