#ifndef OBSBASIC_H
#define OBSBASIC_H
#include "mainwindow.h"
#include <QPointer>
#include <QStyledItemDelegate>
#include <memory>
#include <qsystemtrayicon.h>
#include <obs.hpp>
#include <obs-frontend-internal.hpp>
#include "auth-base.hpp"

#include <util/platform.h>
#include <util/util.hpp>
#include <util/threading.h>

#include "window-basic-preview.hpp"
#include "window-projector.hpp"
#include "window-basic-properties.hpp"
#include "window-basic-main-outputs.hpp"
class QListWidgetItem;
class VolControl;



//QT_BEGIN_NAMESPACE
namespace Ui { class OBSBasic; }
//QT_END_NAMESPACE
//#include "ui_OBSBasic.h"

#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_X264 "X264"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_AMD "amd"

#define PREVIEW_EDGE_SIZE 10

struct BasicOutputHandler;

enum class QtDataRole{
    OBSRef = Qt::UserRole,
    OBSSignals,
};

struct SavedProjectorInfo{
    ProjectorType type;
    int monitor;
    std::string geometry;
    std::string name;
};

struct QuickTransition {
      QPushButton *button = nullptr;
      OBSSource source;
      obs_hotkey_id hotkey = OBS_INVALID_HOTKEY_ID;
      int duration = 0;
      int id = 0;
      bool fadeToBlack = false;

      inline QuickTransition() {}
      inline QuickTransition(OBSSource source_, int duration_, int id_,
                     bool fadeToBlack_ = false)
          : source(source_),
            duration(duration_),
            id(id_),
            fadeToBlack(fadeToBlack_),
            renamedSignal(std::make_shared<OBSSignal>(
                obs_source_get_signal_handler(source), "rename",
               SourceRenamed, this))
     {
     }

 private:
     static void SourceRenamed(void *param, calldata_t *data);
     std::shared_ptr<OBSSignal> renamedSignal;
 };




























class OBSBasic : public MainWindow
{
    Q_OBJECT
    friend class OBSBasicPreview;
    friend class Auth;
    friend class OBSBasicStatusBar;
    friend class OBSStudioAPI;
   // friend class OBSBasicPreview;
    friend class OBSBasicProperties;
    friend class OBSBasicSettings;
    friend class SourceTree;
public:
    explicit OBSBasic(QWidget *parent = 0);
    virtual ~OBSBasic();
    virtual void OBSInit() override;
    virtual config_t *Config() const override;
    virtual int GetProfilePath(char *path, size_t size, const char *file) const override;
    OBSScene GetCurrentScene();
    static OBSBasic *Get();
    void SysTrayNotify(const QString &text, QSystemTrayIcon::MessageIcon n);
    obs_service *GetService();
    void SetService(obs_service_t *service);
    void SaveService();
   int GetTransitionDuration();

    inline Auth *GetAuth() {return auth.get(); }
    inline void EnableOutputs(bool enalbe)
    {
        if (enalbe)
        {
            if(--disableOutputsRef < 0)
                disableOutputsRef = 0;
        }else{
            disableOutputsRef++;
        }
    }
    inline double GetCPUUsage() const
    {
       return os_cpu_usage_info_query(cpuUsageInfo);

    }
    void AddProjectorMenuMonitors(QMenu *parent, QObject *target, const char *slot);

    inline bool IsPreviewProgramMode() const
    {
      return os_atomic_load_bool(&PreviewProgramMode);
    }
    void UpdateTitleBar();

    void ResetUI();
    void ResetOutputs();

    void ResetAudioDevice(const char *sourceId, const char *deviceId,
                       const char *deviceDesc, int channel);
    int ResetVideo();
    const char *GetCurrentOutputPath();

    QIcon GetSourceIcon(const char *id) const;
    QIcon GetGroupIcon() const;
    QIcon GetSceneIcon() const;

    inline bool SavingDisabled() const { return disableSaving; }
    void CreatePropertiesWindow(obs_source_t *source);

    static void InitBrowserPanelSafeBlock();



private:
    int previewX = 0;
    int previewY = 0;
    int previewCX = 0;
    int previewCY = 0;
    float previewScale = 0.0f;
    obs_frontend_callbacks *api = nullptr;

    long disableSaving = 1;
    bool projectChanged = false;
    bool previewEnabled = true;

    int disableOutputsRef = 0;

    std::vector<VolControl*> volumes;



    std::vector<OBSProjector *> projectors;

    bool InitBasicConfig();
    bool InitBasicConfigDefaults();



    void ResizePreview(uint32_t cx, uint32_t cy);
    std::vector<OBSSignal> signalHandlers;
    //QScopedPointer<QPushButton> pause;

    QPointer<QDockWidget> statsDock;
    gs_vertbuffer_t *box = nullptr;

    QScopedPointer<QSystemTrayIcon> trayIcon;

    //std::unique_ptr<BasicOutputHandler> outputHandler;
    std::shared_ptr<BasicOutputHandler> outputHandler;
    OBSService service;
    std::shared_ptr<Auth> auth;

    os_cpu_usage_info_t *cpuUsageInfo = nullptr;
    void UpdatePause(bool active = true);

    QPointer<QMenu> multiviewProjectorMenu;
    QPointer<QObject> shortcutFilter;
    QPointer<QPushButton> replayBufferButton;

    QPointer<OBSBasicProperties> properties;


    void ReplayBufferClicked();

    static void RenderMain(void *data, uint32_t calldata_t, uint32_t cy);
    void DrawBackdrop(float cx, float cy);

    volatile bool PreviewProgramMode = true;
    QByteArray startingDockLayout;

    void copyActionDynamicProperties();
    void UpdateMultiviewProjectorMenu();
    OBSProjector *OpenProjector(obs_source_t *source,int monitor, ProjectorType type);

    void UpdateVolumeControlsDecayRate();
    void UpdateVolumeControlsPeakMeterType();

    void GetFPSCommon(uint32_t &num, uint32_t &den) const;
    void GetFPSInteger(uint32_t &num, uint32_t &den) const;
    void GetFPSFraction(uint32_t &num, uint32_t &den) const;
    void GetFPSNanoseconds(uint32_t &num, uint32_t &den) const;
    void GetConfigFPS(uint32_t &num, uint32_t &den) const;

    void ResizeProgram(uint32_t cx, uint32_t cy);

     QPointer<OBSQTDisplay> program;

     void InitDefaultTransitions();
     void InitTransition(obs_source_t *transition);
     obs_source_t *FindTransition(const char *name);
     OBSSource GetCurrentTransition();
     obs_data_array_t *SaveTransitions();
     void LoadTransitions(obs_data_array_t *transitions);

     obs_source_t *fadeTransition;
/*
     void CreateProgramDisplay();
     void CreateProgramOptions();
     void AddQuickTransitionId(int id);
     void AddQuickTransition();
     void AddQuickTransitionHotkey(QuickTransition *qt);
     void RemoveQuickTransitionHotkey(QuickTransition *qt);
     void LoadQuickTransitions(obs_data_array_t *array);
     obs_data_array_t *SaveQuickTransitions();
     void ClearQuickTransitionWidgets();
     void RefreshQuickTransitions();
     void DisableQuickTransitionWidgets();
     void EnableQuickTransitionWidgets();
     void CreateDefaultQuickTransitions();

     QMenu *CreatePerSceneTransitionMenu();

     QuickTransition *GetQuickTransition(int id);
     int GetQuickTransitionIdx(int id);
     QMenu *CreateTransitionMenu(QWidget *parent, QuickTransition *qt);
     void ClearQuickTransitions();
     void QuickTransitionClicked();
     void QuickTransitionChange();
     void QuickTransitionChangeDuration(int value);
     void QuickTransitionRemoveClicked();
*/
     int programX = 0, programY = 0;
     int programCX = 0, programCY = 0;
     float programScale = 0.0f;

     QIcon imageIcon;
     QIcon colorIcon;
     QIcon slideshowIcon;
     QIcon audioInputIcon;
     QIcon audioOutputIcon;
     QIcon desktopCapIcon;
     QIcon windowCapIcon;
     QIcon gameCapIcon;
     QIcon cameraIcon;
     QIcon textIcon;
     QIcon mediaIcon;
     QIcon browserIcon;
     QIcon groupIcon;
     QIcon sceneIcon;
     QIcon defaultIcon;





     QIcon GetImageIcon() const;
     QIcon GetColorIcon() const;
     QIcon GetSlideshowIcon() const;
     QIcon GetAudioInputIcon() const;
     QIcon GetAudioOutputIcon() const;
     QIcon GetDesktopCapIcon() const;
     QIcon GetWindowCapIcon() const;
     QIcon GetGameCapIcon() const;
     QIcon GetCameraIcon() const;
     QIcon GetTextIcon() const;
     QIcon GetMediaIcon() const;
     QIcon GetBrowserIcon() const;
     QIcon GetDefaultIcon() const;










private:
    std::unique_ptr<Ui::OBSBasic> ui;
    ConfigFile basicConfig;
private slots:
    void OpenMultiviewProject();
    void SaveProject();
/*
    void AddTransition();
    void RenameTransition();
    void TransitionClicked();
    void TransitionStopped();
    void TransitionFullyStopped();
    void TriggerQuickTransition(int id);
*/
    void SetImageIcon(const QIcon &icon);
    void SetColorIcon(const QIcon &icon);
    void SetSlideshowIcon(const QIcon &icon);
    void SetAudioInputIcon(const QIcon &icon);
    void SetAudioOutputIcon(const QIcon &icon);
    void SetDesktopCapIcon(const QIcon &icon);
    void SetWindowCapIcon(const QIcon &icon);
    void SetGameCapIcon(const QIcon &icon);
    void SetCameraIcon(const QIcon &icon);
    void SetTextIcon(const QIcon &icon);
    void SetMediaIcon(const QIcon &icon);
    void SetBrowserIcon(const QIcon &icon);
    void SetGroupIcon(const QIcon &icon);
    void SetSceneIcon(const QIcon &icon);
    void SetDefaultIcon(const QIcon &icon);





};


class SceneRenameDelegate: public QStyledItemDelegate{
    Q_OBJECT

public:
    SceneRenameDelegate(QObject *parent);
    virtual void setEditorData(QWidget *editor, const QModelIndex &index) const override;
protected:
    virtual bool eventFilter(QObject *object, QEvent *event) override;

};


#endif // OBSBASIC_H
