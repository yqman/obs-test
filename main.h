#ifndef MAIN_H
#define MAIN_H
#include <QApplication>
#include <QTranslator>
#include <util/profiler.h>
#include <util/util.hpp>
#include <util/platform.h>
#include <util/lexer.h>
#include "mainwindow.h"
#include <string>

std::string GenerateSpecifiedFilename(const char *extension, bool noSpace, const char *format);
struct BaseLexer {
    lexer lex;

public:
    inline BaseLexer() { lexer_init(&lex); }
    inline ~BaseLexer() { lexer_free(&lex); }
    operator lexer *() { return &lex; }
};

QObject *CreateShortcutFilter();

typedef std::function<void()> VoidFunc;

class OBSApp : public QApplication{
    Q_OBJECT
private:
    std::string locale;
    std::string theme;
    ConfigFile globalConfig;
    TextLookup textLookup;
    profiler_name_store_t *profilerNameStore = nullptr;
    QPointer<MainWindow> mainWindow;
    os_inhibit_t *sleepInhibitor = nullptr;

    bool enableHotkeysInFocus = true;
    bool enableHotkeysOutOfFocus = true;

    inline void ResetHotKeyState(bool infocus);

    bool InitGlobalConfig();
    bool InitGlobalConfigDefaults();

    bool UpdatePre22MultiviewLayout(const char *layout);
    inline void ResetHotkeyState(bool inFocus);
public:
    OBSApp(int &argc, char **argv, profiler_name_store_t *store);
    ~OBSApp();

    void AppInit();
    bool OBSInit();
    inline QMainWindow *GetMainWindow() const {return mainWindow.data();}
    inline config_t *GlobalConfig() const {return globalConfig;}

    inline bool HotkeysEnabledInFocus() const
    {
        return enableHotkeysInFocus;
    }
    void DisableHotkeys();
    void UpdateHotkeyFocusSetting(bool reset = true);

    inline const char *GetLocale() const { return locale.c_str(); }
    inline const char *GetTheme() const { return theme.c_str(); }
    bool SetTheme(std::string name, std::string path = "");

    const char *InputAudioSource() const;
    const char *OutputAudioSource() const;

    const char *GetRenderModule() const;




    inline const char *GetString(const char *lookupVal) const
    {
      return textLookup.GetString(lookupVal);
    }
    profiler_name_store_t *GetProfilerNameStore() const
    {
       return profilerNameStore;
    }
    std::string GetVersionString() const;
signals:
    void StyleChanged();

};

int GetConfigPath(char *path, size_t size, const char *name);

inline OBSApp *App()
{
    return static_cast<OBSApp *>(qApp);

}
inline const char *str(const char *lookup)
{
    return App()->GetString(lookup);
}

std::vector<std::pair<std::string, std::string>> GetLocaleNames();

inline config_t *GetGlobalConfig()
{
  return App()->GlobalConfig();

}

inline const char *Str(const char *lookup)
{
   return App()->GetString(lookup);

}
#define QTStr(lookupVal) QString::fromUtf8(str(lookupVal))

static inline int GetProfilePath(char *path, size_t size, const char *file)
{
   MainWindow *window = reinterpret_cast<MainWindow *> (App()->GetMainWindow());
   return window->GetProfilePath(path, size, file);

}




#endif // MAIN_H
