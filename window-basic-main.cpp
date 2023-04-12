#include <ctime>
#include <QGuiApplication>
#include <QDesktopWidget>
#include <QSizePolicy>
#include <QListWidgetItem>
#include <QScreen>
#include <QShowEvent>
#include <QMessageBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QScrollBar>
#include <QLineEdit>

#include <util/dstr.h>
#include <util/util.hpp>
#include <util/platform.h>
#include <util/profiler.hpp>

#include "window-basic-main.hpp"
#include "window-dock.hpp"
#include "main.h"
#include "window-basic-preview.hpp"
#include "window-basic-settings.hpp"
#include "window-basic-status-bar.hpp"
#include "window-basic-stats.hpp"
#include "obs.hpp"
#include "display-helpers.hpp"
#include "qt-wrappers.hpp"
#include <fstream>
#include <sstream>
#include "platform.hpp"

#include "ui_OBSBasic.h"
using namespace std;

#include <QWindow>
extern obs_frontend_callbacks *InitializeAPIInterface(OBSBasic *main);

namespace{
template <typename OBSRef> struct SignalContainer{
    OBSRef ref;
    vector<shared_ptr<OBSSignal>> handlers;

};
}

Q_DECLARE_METATYPE(OBSScene);
Q_DECLARE_METATYPE(SignalContainer<OBSScene>);

QDataStream &operator<<(QDataStream &out, const SignalContainer<OBSScene> &v)
{
    out << v.ref;
    return out;
}
QDataStream &operator>>(QDataStream &in, SignalContainer <OBSScene> &v)
{
   in >> v.ref;
   return in;

}
static inline enum video_format GetVideoFormatFromName(const char *name)
 {
     if (astrcmpi(name, "I420") == 0)
         return VIDEO_FORMAT_I420;
     else if (astrcmpi(name, "NV12") == 0)
         return VIDEO_FORMAT_NV12;
     else if (astrcmpi(name, "I444") == 0)
         return VIDEO_FORMAT_I444;
 #if 0 //currently unsupported
     else if (astrcmpi(name, "YVYU") == 0)
         return VIDEO_FORMAT_YVYU;
     else if (astrcmpi(name, "YUY2") == 0)
         return VIDEO_FORMAT_YUY2;
     else if (astrcmpi(name, "UYVY") == 0)
         return VIDEO_FORMAT_UYVY;
 #endif
     else
         return VIDEO_FORMAT_RGBA;
 }
static inline enum obs_scale_type GetScaleType(ConfigFile &basicConfig)
 {
     const char *scaleTypeStr =
         config_get_string(basicConfig, "Video", "ScaleType");

     if (astrcmpi(scaleTypeStr, "bilinear") == 0)
         return OBS_SCALE_BILINEAR;
     else if (astrcmpi(scaleTypeStr, "lanczos") == 0)
         return OBS_SCALE_LANCZOS;
     else if (astrcmpi(scaleTypeStr, "area") == 0)
         return OBS_SCALE_AREA;
     else
         return OBS_SCALE_BICUBIC;
 }
#ifdef _WIN32
 #define IS_WIN32 1
 #else
 #define IS_WIN32 0
 #endif

 static inline int AttemptToResetVideo(struct obs_video_info *ovi)
 {
     return obs_reset_video(ovi);
 }


OBSBasic::OBSBasic(QWidget *parent ):MainWindow(parent),ui(new Ui::OBSBasic)
{
    qRegisterMetaTypeStreamOperators<SignalContainer<OBSScene>>("SignalContainer<OBSScene>");
    setAttribute(Qt::WA_NativeWindow);

    blog(LOG_INFO,"OBSBasic::OBSBasic()------------------Start");
    setAcceptDrops(true);
    //api= InitializeAPIInterface(this);

    ui->setupUi(this);
    ui->previewDisabledWidget->setVisible(false);

    startingDockLayout = saveState();

    statsDock = new OBSDock();
    statsDock->setObjectName(QStringLiteral("statsDock"));
    statsDock->setFeatures(QDockWidget::AllDockWidgetFeatures);
    statsDock->setWindowTitle(QTStr("Basic.Stats"));
    addDockWidget(Qt::BottomDockWidgetArea, statsDock);
    statsDock->setVisible(false);
    statsDock->setFloating(true);
    statsDock->resize(700, 200);

    ui->scenes->setAttribute(Qt::WA_MacShowFocusRect,false);

    copyActionDynamicProperties();
    char styleSheetPath[512];
    int ret = GetProfilePath(styleSheetPath, sizeof(styleSheetPath), "stylesheet.qss");
    if (ret > 0) {
        if (QFile::exists(styleSheetPath)) {
            QString path =
                            QString("file:///") + QT_UTF8(styleSheetPath);
            App()->setStyleSheet(path);
        }
    }

    bool sceneGrid = config_get_bool(App()->GlobalConfig(), "BasicWindow", "gridMode");

    ui->scenes->SetGridMode(sceneGrid);

    ui->scenes->setItemDelegate(new SceneRenameDelegate(ui->scenes));

    qRegisterMetaType<OBSScene>("OBSScene");
    qRegisterMetaType<OBSSceneItem>("OBSSceneItem");
    qRegisterMetaType<OBSSource>("OBSSource");
    qRegisterMetaType<obs_hotkey_id>("obs_hotkey_id");
    qRegisterMetaType<SavedProjectorInfo *>("SaveProjectorInfo");

    qRegisterMetaTypeStreamOperators<std::vector<std::shared_ptr<OBSSignal>>>("std::vector<std::shared_ptr<OBSSignal>>");
    qRegisterMetaTypeStreamOperators<OBSScene>("OBSScene");
    qRegisterMetaTypeStreamOperators<OBSSceneItem>("OBSSceneItem");

    ui->scenes->setAttribute(Qt::WA_MacShowFocusRect, false);
    //ui->sources->

    auto displayResize = [this]() {
       struct obs_video_info ovi;

       if(obs_get_video_info(&ovi))
           ResizePreview(ovi.base_width, ovi.base_height);
    };
    connect(windowHandle(), &QWindow::screenChanged, displayResize);
    connect(ui->preview, &OBSQTDisplay::DisplayResized, displayResize);

    delete shortcutFilter;
    shortcutFilter = CreateShortcutFilter();

    stringstream name;
    name << "OBS" << App()->GetVersionString();
    blog(LOG_INFO, "%s", name.str().c_str());
    blog(LOG_INFO,"_____________________________");

    UpdateTitleBar();

    blog(LOG_INFO,"OBSBasic::OBSBasic()------------------End");

    ui->previewLabel->setProperty("themeId","previewProgramLabels");

    bool labels = config_get_bool(GetGlobalConfig(), "BasicWindow","StdioModeLabels");

    if (!PreviewProgramMode)
        ui->previewLabel->setHidden(true);
    else
        ui->previewLabel->setHidden(!labels);


}
OBSBasic::~OBSBasic()
{
   outputHandler.reset();

}
void OBSBasic::OBSInit()
{
    ProfileScope("OBSBasic::OBSInit");

    auto addDisplay = [this](OBSQTDisplay *window) {
        obs_display_add_draw_callback(window->GetDisplay(),
                                      OBSBasic::RenderMain, this);
        struct obs_video_info ovi;
          if (obs_get_video_info(&ovi))
          // ResizePreview(ovi.base_width, ovi.base_height);
           ResizePreview(720, 480);
    };
    connect(ui->preview, &OBSQTDisplay::DisplayCreated, addDisplay);
    /*--------*/


    //SetAlwaysOnTop(this,true);
    //ui->actionAlwaysOnTop->setChecked(true);

    show();
    blog(LOG_INFO,"OBSBasic::OBSInit()------------------End");

    ui->viewMenu->addSeparator();

    multiviewProjectorMenu = new QMenu(QTStr("MultiviewProjector")) ;
    ui->viewMenu->addMenu(multiviewProjectorMenu);
    AddProjectorMenuMonitors(multiviewProjectorMenu, this, SLOT(OpenMultiviewProject()));

    connect(ui->viewMenu->menuAction(), &QAction::hovered,this,&OBSBasic::UpdateMultiviewProjectorMenu);
    ui->viewMenu->addAction(QTStr("MultiviewWindowed"), this, SLOT(OpenMultiviewProject()));



}
config_t *OBSBasic::Config() const
{
    return basicConfig;
}

void OBSBasic::UpdateMultiviewProjectorMenu()
{
    multiviewProjectorMenu->clear();
    AddProjectorMenuMonitors(multiviewProjectorMenu,this,SLOT(OpenMultivewProjector()));
}


int OBSBasic::GetProfilePath(char *path, size_t size, const char *file) const
{
    char profiles_path[512];
    const char *profile = config_get_string(App()->GlobalConfig(),"Basic","ProfileDir");
    int ret;
    if (!profile)
        return -1;
    if (!path)
        return -1;
    if (!file)
        file = "";
    ret = GetConfigPath(profiles_path, 512, "obs-studio/basic/profiles");
    if (ret <= 0)
        return ret;

    if (!*file)
        return snprintf(path, size, "%s/%s", profiles_path, profile);
    return snprintf(path, size, "%s/%s/%s", profiles_path, profile, file);
}

template<typename T> static T GetOBSRef(QListWidgetItem *item)
{
   return item->data(static_cast<int>(QtDataRole::OBSRef)).value<T>();
}

OBSScene OBSBasic::GetCurrentScene()
{
   QListWidgetItem *item = ui->scenes->currentItem();
   return item ? GetOBSRef<OBSScene>(item) : nullptr;

}
OBSBasic *OBSBasic::Get()
{
    return reinterpret_cast<OBSBasic *>(App()->GetMainWindow());
}

obs_service_t *OBSBasic::GetService()
{
    if (!service){
        service = obs_service_create("rtmp", NULL, NULL, nullptr);
        obs_service_release(service);
    }
    return service;
}

void OBSBasic::SysTrayNotify(const QString &text, QSystemTrayIcon::MessageIcon n)
{
   if (trayIcon && QSystemTrayIcon::supportsMessages())
   {
       QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::MessageIcon(n);
       trayIcon->showMessage("OBS Studio", text, icon, 10000);
   }
}

void OBSBasic::AddProjectorMenuMonitors(QMenu *parent, QObject *target, const char *slot)
{
   QAction *action;
   QList<QScreen *> screens = QGuiApplication::screens();
   for (int i=0; i<screens.size(); i++)
   {
       QRect screenGeometry = screens[i]->geometry();
       QString str = QString("%1 %2:%3x%4 @ %5,%6")
               .arg( "Display", QString::number(i + 1),
                                                        QString::number(screenGeometry.width()),
                                                        QString::number(screenGeometry.height()),
                                                        QString::number(screenGeometry.x()),
                                                        QString::number(screenGeometry.y()));
   action = parent->addAction(str,target, slot);
   action->setProperty("monitor", i);
   }
}

void OBSBasic::OpenMultiviewProject()
{
    blog(LOG_INFO,"OBSBasic::OpenMultiviewProject-------------Start");
    int monitor = sender()->property("monitor").toInt();
      OpenProjector(nullptr, monitor, ProjectorType::Multiview);
}

void OBSBasic::RenderMain(void *data, uint32_t cx, uint32_t cy)
{
    blog(LOG_INFO,"OBSBasic::RenderMain()-------------Start");
    GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "RenderMain");

    OBSBasic *window = static_cast<OBSBasic *>(data);
    obs_video_info ovi;

    obs_get_video_info(&ovi);

    window->previewCX = int(window->previewScale * float(ovi.base_width));
    window->previewCY = int(window->previewScale * float(ovi.base_height));

    gs_viewport_push();
    gs_projection_push();

    obs_display_t *display = window->ui->preview->GetDisplay();
    uint32_t width, height;
    obs_display_size(display, &width, &height);
    float right = float(width) - window->previewX;
    float bottom = float(height) - window->previewY;

    gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f,
             100.0f);

    window->ui->preview->DrawOverflow();
    blog(LOG_INFO,"OBSBasic::RenderMain()---------------End");

    /* --------------------------------------- */

    gs_ortho(0.0f, float(ovi.base_width), 0.0f, float(ovi.base_height),
             -100.0f, 100.0f);
    gs_set_viewport(window->previewX, window->previewY, window->previewCX,
                    window->previewCY);

    if (window->IsPreviewProgramMode()) {
        window->DrawBackdrop(float(ovi.base_width),
                             float(ovi.base_height));

        OBSScene scene = window->GetCurrentScene();
        obs_source_t *source = obs_scene_get_source(scene);
        if (source)
            obs_source_video_render(source);
    } else {
        obs_render_main_texture_src_color_only();
    }
    gs_load_vertexbuffer(nullptr);

    /* --------------------------------------- */

    gs_ortho(-window->previewX, right, -window->previewY, bottom, -100.0f,
             100.0f);
    gs_reset_viewport();

    window->ui->preview->DrawSceneEditing();

    /* --------------------------------------- */

    gs_projection_pop();
    gs_viewport_pop();

    GS_DEBUG_MARKER_END();

    UNUSED_PARAMETER(cx);
    UNUSED_PARAMETER(cy);


}

void OBSBasic::DrawBackdrop(float cx, float cy)
{
    if (!box)
        return;

    blog(LOG_INFO,"OBSBasic::DrawBackdrop-------------Start");

    GS_DEBUG_MARKER_BEGIN(GS_DEBUG_COLOR_DEFAULT, "DrawBackdrop");

    gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
    gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
    gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

    vec4 colorVal;
    vec4_set(&colorVal, 0.0f, 0.0f, 0.0f, 1.0f);
    gs_effect_set_vec4(color, &colorVal);

    gs_technique_begin(tech);
    gs_technique_begin_pass(tech, 0);
    gs_matrix_push();
    gs_matrix_identity();
    gs_matrix_scale3f(float(cx), float(cy), 1.0f);

    gs_load_vertexbuffer(box);
    gs_draw(GS_TRISTRIP, 0, 0);

    gs_matrix_pop();
    gs_technique_end_pass(tech);
    gs_technique_end(tech);

    gs_load_vertexbuffer(nullptr);

    GS_DEBUG_MARKER_END();
}

void OBSBasic::ResizePreview(uint32_t cx, uint32_t cy)
{

    QSize targetSize;
    bool isFixedScaling;
    obs_video_info ovi;

    /* resize preview panel to fix to the top section of the window */
    targetSize = GetPixelSize(ui->preview);

    isFixedScaling = ui->preview->IsFixedScaling();
    obs_get_video_info(&ovi);

    if (isFixedScaling) {
        previewScale = ui->preview->GetScalingAmount();
        GetCenterPosFromFixedScale(
                                int(cx), int(cy),
                                targetSize.width() - PREVIEW_EDGE_SIZE * 2,
                                targetSize.height() - PREVIEW_EDGE_SIZE * 2, previewX,
                                previewY, previewScale);
        previewX += ui->preview->GetScrollX();
        previewY += ui->preview->GetScrollY();

    } else {
        GetScaleAndCenterPos(int(cx), int(cy),
                             targetSize.width() - PREVIEW_EDGE_SIZE * 2,
                             targetSize.height() -
                             PREVIEW_EDGE_SIZE * 2,
                             previewX, previewY, previewScale);
    }

    previewX += float(PREVIEW_EDGE_SIZE);
    previewY += float(PREVIEW_EDGE_SIZE);
    blog(LOG_INFO,"OBSBasic::ResizePreview-------------End");
}

SceneRenameDelegate::SceneRenameDelegate(QObject *parent): QStyledItemDelegate(parent)
{

}

void SceneRenameDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
   QStyledItemDelegate::setEditorData(editor,index);
   QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
   if (lineEdit)
      lineEdit->selectAll();

}

bool SceneRenameDelegate::eventFilter(QObject *editor, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
            if (lineEdit)
                lineEdit->undo();
        }

    }
    return QStyledItemDelegate::eventFilter(editor,event);
}

void OBSBasic::UpdateTitleBar()
{
    stringstream name;

    const char *profile =
                    config_get_string(App()->GlobalConfig(), "Basic", "Profile");
    const char *sceneCollection = config_get_string(
                            App()->GlobalConfig(), "Basic", "SceneCollection");

    name << "OBS ";
    // if (previewProgramMode)
    //    name << "Studio ";

    // name << App()->GetVersionString();
    //if (App()->IsPortableMode())
    name << " - Portable Mode";

    name << " - " << Str("TitleBar.Profile") << ": " << profile;
    name << " - " << Str("TitleBar.Scenes") << ": " << sceneCollection;

    setWindowTitle(QT_UTF8(name.str().c_str()));
}

void OBSBasic::copyActionDynamicProperties()
{
    for(QAction *x: ui->scenesToolbar->actions())
    {
        QWidget *temp = ui->scenesToolbar->widgetForAction(x);
        for (QByteArray &y : x->dynamicPropertyNames())
        {
            temp->setProperty(y, x->property(y));
        }
    }
    for(QAction *x : ui->sourcesToolbar->actions())
    {
        QWidget *temp = ui->sourcesToolbar->widgetForAction(x);

        for(QByteArray &y : x->dynamicPropertyNames())
        {
            temp->setProperty(y,x->property(y));
        }
    }

}
void OBSBasic::SaveProject()
{
    if (disableSaving)
        return;
    projectChanged = true;
    QMetaObject::invokeMethod(this,"SaveProjectDeferred",Qt::QueuedConnection);

}
int OBSBasic::GetTransitionDuration()
{
  //  return ui->trasitionDuration->value();
   return 0;
}
OBSProjector *OBSBasic::OpenProjector(obs_source_t *source, int monitor,
                       ProjectorType type)
 {
     /* seriously?  10 monitors? */
     if (monitor > 9 || monitor > QGuiApplication::screens().size() - 1)
         return nullptr;

     OBSProjector *projector =
         new OBSProjector(nullptr, source, monitor, type);

     if (projector)
         projectors.emplace_back(projector);

     return projector;
 }
bool OBSBasic::InitBasicConfig()
{
    ProfileScope("OBSBasic::InitBasicConfig");

    char configPath[512];

    int ret = GetProfilePath(configPath, sizeof(configPath), "");
    if (ret <= 0) {
        OBSErrorBox(nullptr, "Failed to get profile path");
        return false;
    }

    if (os_mkdir(configPath) == MKDIR_ERROR) {
        OBSErrorBox(nullptr, "Failed to create profile path");
        return false;
    }

    ret = GetProfilePath(configPath, sizeof(configPath), "basic.ini");
    if (ret <= 0) {
        OBSErrorBox(nullptr, "Failed to get base.ini path");
        return false;
    }

    int code = basicConfig.Open(configPath, CONFIG_OPEN_ALWAYS);
    if (code != CONFIG_SUCCESS) {
        OBSErrorBox(NULL, "Failed to open basic.ini: %d", code);
        return false;
    }

    if (config_get_string(basicConfig, "General", "Name") == nullptr) {
        const char *curName = config_get_string(App()->GlobalConfig(),
                                                "Basic", "Profile");

        config_set_string(basicConfig, "General", "Name", curName);
        basicConfig.SaveSafe("tmp");
    }

    return InitBasicConfigDefaults();
}

static const double scaled_vals[] = {1.0,         1.25, (1.0 / 0.75), 1.5,
                      (1.0 / 0.6), 1.75, 2.0,          2.25,
                      2.5,         2.75, 3.0,          0.0};

extern void CheckExistingCookieId();


bool OBSBasic::InitBasicConfigDefaults()
{
    QList<QScreen *> screens = QGuiApplication::screens();

    if (!screens.size()) {
        OBSErrorBox(NULL, "There appears to be no monitors.  Er, this "
                          "technically shouldn't be possible.");
        return false;
    }

    QScreen *primaryScreen = QGuiApplication::primaryScreen();

    uint32_t cx = primaryScreen->size().width();
    uint32_t cy = primaryScreen->size().height();

    bool oldResolutionDefaults = config_get_bool(
                            App()->GlobalConfig(), "General", "Pre19Defaults");

    /* use 1920x1080 for new default base res if main monitor is above
      * 1920x1080, but don't apply for people from older builds -- only to
      * new users */
    if (!oldResolutionDefaults && (cx * cy) > (1920 * 1080)) {
        cx = 1920;
        cy = 1080;
    }

    bool changed = false;

    /* ----------------------------------------------------- */
    /* move over old FFmpeg track settings                   */
    if (config_has_user_value(basicConfig, "AdvOut", "FFAudioTrack") &&
                    !config_has_user_value(basicConfig, "AdvOut", "Pre22.1Settings")) {

        int track = (int)config_get_int(basicConfig, "AdvOut",
                                        "FFAudioTrack");
        config_set_int(basicConfig, "AdvOut", "FFAudioMixes",
                       1LL << (track - 1));
        config_set_bool(basicConfig, "AdvOut", "Pre22.1Settings", true);
        changed = true;
    }

    /* ----------------------------------------------------- */
    /* move over mixer values in advanced if older config */
    if (config_has_user_value(basicConfig, "AdvOut", "RecTrackIndex") &&
                    !config_has_user_value(basicConfig, "AdvOut", "RecTracks")) {

        uint64_t track =
                        config_get_uint(basicConfig, "AdvOut", "RecTrackIndex");
        track = 1ULL << (track - 1);
        config_set_uint(basicConfig, "AdvOut", "RecTracks", track);
        config_remove_value(basicConfig, "AdvOut", "RecTrackIndex");
        changed = true;
    }

    /* ----------------------------------------------------- */
    /* set twitch chat extensions to "both" if prev version  */
    /* is under 24.1                                         */
    if (config_get_bool(GetGlobalConfig(), "General", "Pre24.1Defaults") &&
                    !config_has_user_value(basicConfig, "Twitch", "AddonChoice")) {
        config_set_int(basicConfig, "Twitch", "AddonChoice", 3);
        changed = true;
    }

    /* ----------------------------------------------------- */

    if (changed)
        config_save_safe(basicConfig, "tmp", nullptr);

    /* ----------------------------------------------------- */

    config_set_default_string(basicConfig, "Output", "Mode", "Simple");

    config_set_default_string(basicConfig, "SimpleOutput", "FilePath",
                              GetDefaultVideoSavePath().c_str());
    config_set_default_string(basicConfig, "SimpleOutput", "RecFormat",
                              "mkv");
    config_set_default_uint(basicConfig, "SimpleOutput", "VBitrate", 2500);
    config_set_default_uint(basicConfig, "SimpleOutput", "ABitrate", 160);
    config_set_default_bool(basicConfig, "SimpleOutput", "UseAdvanced",
                            false);
    config_set_default_bool(basicConfig, "SimpleOutput", "EnforceBitrate",
                            true);
    config_set_default_string(basicConfig, "SimpleOutput", "Preset",
                              "veryfast");
    config_set_default_string(basicConfig, "SimpleOutput", "NVENCPreset",
                              "hq");
    config_set_default_string(basicConfig, "SimpleOutput", "RecQuality",
                              "Stream");
    config_set_default_bool(basicConfig, "SimpleOutput", "RecRB", false);
    config_set_default_int(basicConfig, "SimpleOutput", "RecRBTime", 20);
    config_set_default_int(basicConfig, "SimpleOutput", "RecRBSize", 512);
    config_set_default_string(basicConfig, "SimpleOutput", "RecRBPrefix",
                              "Replay");

    config_set_default_bool(basicConfig, "AdvOut", "ApplyServiceSettings",
                            true);
    config_set_default_bool(basicConfig, "AdvOut", "UseRescale", false);
    config_set_default_uint(basicConfig, "AdvOut", "TrackIndex", 1);
    config_set_default_string(basicConfig, "AdvOut", "Encoder", "obs_x264");

    config_set_default_string(basicConfig, "AdvOut", "RecType", "Standard");

    config_set_default_string(basicConfig, "AdvOut", "RecFilePath",
                              GetDefaultVideoSavePath().c_str());
    config_set_default_string(basicConfig, "AdvOut", "RecFormat", "mkv");
    config_set_default_bool(basicConfig, "AdvOut", "RecUseRescale", false);
    config_set_default_uint(basicConfig, "AdvOut", "RecTracks", (1 << 0));
    config_set_default_string(basicConfig, "AdvOut", "RecEncoder", "none");
    config_set_default_uint(basicConfig, "AdvOut", "FLVTrack", 1);

    config_set_default_bool(basicConfig, "AdvOut", "FFOutputToFile", true);
    config_set_default_string(basicConfig, "AdvOut", "FFFilePath",
                              GetDefaultVideoSavePath().c_str());
    config_set_default_string(basicConfig, "AdvOut", "FFExtension", "mp4");
    config_set_default_uint(basicConfig, "AdvOut", "FFVBitrate", 2500);
    config_set_default_uint(basicConfig, "AdvOut", "FFVGOPSize", 250);
    config_set_default_bool(basicConfig, "AdvOut", "FFUseRescale", false);
    config_set_default_bool(basicConfig, "AdvOut", "FFIgnoreCompat", false);
    config_set_default_uint(basicConfig, "AdvOut", "FFABitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "FFAudioMixes", 1);

    config_set_default_uint(basicConfig, "AdvOut", "Track1Bitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "Track2Bitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "Track3Bitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "Track4Bitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "Track5Bitrate", 160);
    config_set_default_uint(basicConfig, "AdvOut", "Track6Bitrate", 160);

    config_set_default_bool(basicConfig, "AdvOut", "RecRB", false);
    config_set_default_uint(basicConfig, "AdvOut", "RecRBTime", 20);
    config_set_default_int(basicConfig, "AdvOut", "RecRBSize", 512);

    config_set_default_uint(basicConfig, "Video", "BaseCX", cx);
    config_set_default_uint(basicConfig, "Video", "BaseCY", cy);

    /* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
    if (!config_has_user_value(basicConfig, "Video", "BaseCX") ||
                    !config_has_user_value(basicConfig, "Video", "BaseCY")) {
        config_set_uint(basicConfig, "Video", "BaseCX", cx);
        config_set_uint(basicConfig, "Video", "BaseCY", cy);
        config_save_safe(basicConfig, "tmp", nullptr);
    }

    config_set_default_string(basicConfig, "Output", "FilenameFormatting",
                              "%CCYY-%MM-%DD %hh-%mm-%ss");

    config_set_default_bool(basicConfig, "Output", "DelayEnable", false);
    config_set_default_uint(basicConfig, "Output", "DelaySec", 20);
    config_set_default_bool(basicConfig, "Output", "DelayPreserve", true);

    config_set_default_bool(basicConfig, "Output", "Reconnect", true);
    config_set_default_uint(basicConfig, "Output", "RetryDelay", 10);
    config_set_default_uint(basicConfig, "Output", "MaxRetries", 20);

    config_set_default_string(basicConfig, "Output", "BindIP", "default");
    config_set_default_bool(basicConfig, "Output", "NewSocketLoopEnable",
                            false);
    config_set_default_bool(basicConfig, "Output", "LowLatencyEnable",
                            false);

    int i = 0;
    uint32_t scale_cx = cx;
    uint32_t scale_cy = cy;

    /* use a default scaled resolution that has a pixel count no higher
      * than 1280x720 */
    while (((scale_cx * scale_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
        double scale = scaled_vals[i++];
        scale_cx = uint32_t(double(cx) / scale);
        scale_cy = uint32_t(double(cy) / scale);
    }

    config_set_default_uint(basicConfig, "Video", "OutputCX", scale_cx);
    config_set_default_uint(basicConfig, "Video", "OutputCY", scale_cy);

    /* don't allow OutputCX/OutputCY to be susceptible to defaults
      * changing */
    if (!config_has_user_value(basicConfig, "Video", "OutputCX") ||
                    !config_has_user_value(basicConfig, "Video", "OutputCY")) {
        config_set_uint(basicConfig, "Video", "OutputCX", scale_cx);
        config_set_uint(basicConfig, "Video", "OutputCY", scale_cy);
        config_save_safe(basicConfig, "tmp", nullptr);
    }

    config_set_default_uint(basicConfig, "Video", "FPSType", 0);
    config_set_default_string(basicConfig, "Video", "FPSCommon", "30");
    config_set_default_uint(basicConfig, "Video", "FPSInt", 30);
    config_set_default_uint(basicConfig, "Video", "FPSNum", 30);
    config_set_default_uint(basicConfig, "Video", "FPSDen", 1);
    config_set_default_string(basicConfig, "Video", "ScaleType", "bicubic");
    config_set_default_string(basicConfig, "Video", "ColorFormat", "NV12");
    config_set_default_string(basicConfig, "Video", "ColorSpace", "601");
    config_set_default_string(basicConfig, "Video", "ColorRange",
                              "Partial");

    config_set_default_string(basicConfig, "Audio", "MonitoringDeviceId",
                              "default");
    config_set_default_string(
                            basicConfig, "Audio", "MonitoringDeviceName",
                            Str("Basic.Settings.Advanced.Audio.MonitoringDevice"
                                ".Default"));
    config_set_default_uint(basicConfig, "Audio", "SampleRate", 44100);
    config_set_default_string(basicConfig, "Audio", "ChannelSetup",
                              "Stereo");
    config_set_default_double(basicConfig, "Audio", "MeterDecayRate",
                              VOLUME_METER_DECAY_FAST);
    config_set_default_uint(basicConfig, "Audio", "PeakMeterType", 0);

    CheckExistingCookieId();

    return true;
}


void OBSBasic::ResetUI()
{
    bool studioPortraitLayout = config_get_bool(GetGlobalConfig(), "BasicWindow", "StudioPortraitLayout");
    bool labels = config_get_bool(GetGlobalConfig(), "BasicWindow","StudioModeLabels");
    if (studioPortraitLayout)
        ui->previewLayout->setDirection(QBoxLayout::TopToBottom);
    else
        ui->previewLayout->setDirection(QBoxLayout::LeftToRight);

    if(PreviewProgramMode)
        ui->previewLabel->setHidden(!labels);
    //if(programLabel)
    // programLabel->setHidden(!labels);

}

void OBSBasic::ResetOutputs()
 {
     ProfileScope("OBSBasic::ResetOutputs");

     const char *mode = config_get_string(basicConfig, "Output", "Mode");
     bool advOut = astrcmpi(mode, "Advanced") == 0;

     if (!outputHandler || !outputHandler->Active()) {
         outputHandler.reset();
         outputHandler.reset(advOut ? CreateAdvancedOutputHandler(this)
                        : CreateSimpleOutputHandler(this));

         delete replayBufferButton;

         if (outputHandler->replayBuffer) {
             replayBufferButton = new QPushButton(
                 QTStr("Basic.Main.StartReplayBuffer"), this);
             replayBufferButton->setCheckable(true);
             connect(replayBufferButton.data(),
                 &QPushButton::clicked, this,
                 &OBSBasic::ReplayBufferClicked);

             replayBufferButton->setProperty("themeID",
                             "replayBufferButton");
             ui->buttonsVLayout->insertWidget(2, replayBufferButton);
         }

       //  if (sysTrayReplayBuffer)
        //     sysTrayReplayBuffer->setEnabled(
         //        !!outputHandler->replayBuffer);
     } else {
         outputHandler->Update();
     }
 }

void OBSBasic::ReplayBufferClicked()
 {
   //  if (outputHandler->ReplayBufferActive())
      //   StopReplayBuffer();
  //   else
       //  StartReplayBuffer();
 }

void OBSBasic::UpdateVolumeControlsDecayRate()
  {
      double meterDecayRate =
          config_get_double(basicConfig, "Audio", "MeterDecayRate");

      for (size_t i = 0; i < volumes.size(); i++) {
        //  volumes[i]->SetMeterDecayRate(meterDecayRate);
      }
  }

  void OBSBasic::UpdateVolumeControlsPeakMeterType()
  {
      uint32_t peakMeterTypeIdx =
          config_get_uint(basicConfig, "Audio", "PeakMeterType");

      enum obs_peak_meter_type peakMeterType;
      switch (peakMeterTypeIdx) {
      case 0:
          peakMeterType = SAMPLE_PEAK_METER;
          break;
      case 1:
          peakMeterType = TRUE_PEAK_METER;
          break;
      default:
          peakMeterType = SAMPLE_PEAK_METER;
          break;
      }

      for (size_t i = 0; i < volumes.size(); i++) {
        //  volumes[i]->setPeakMeterType(peakMeterType);
      }
  }
  void OBSBasic::ResetAudioDevice(const char *sourceId, const char *deviceId,
                   const char *deviceDesc, int channel)
   {
       bool disable = deviceId && strcmp(deviceId, "disabled") == 0;
       obs_source_t *source;
       obs_data_t *settings;

       source = obs_get_output_source(channel);
       if (source) {
           if (disable) {
               obs_set_output_source(channel, nullptr);
           } else {
               settings = obs_source_get_settings(source);
               const char *oldId =
                   obs_data_get_string(settings, "device_id");
               if (strcmp(oldId, deviceId) != 0) {
                   obs_data_set_string(settings, "device_id",
                               deviceId);
                   obs_source_update(source, settings);
               }
               obs_data_release(settings);
           }

           obs_source_release(source);

       } else if (!disable) {
           settings = obs_data_create();
           obs_data_set_string(settings, "device_id", deviceId);
           source = obs_source_create(sourceId, deviceDesc, settings,
                          nullptr);
           obs_data_release(settings);

           obs_set_output_source(channel, source);
           obs_source_release(source);
       }
  }
  int OBSBasic::ResetVideo()
  {
      if (outputHandler && outputHandler->Active())
          return OBS_VIDEO_CURRENTLY_ACTIVE;

      ProfileScope("OBSBasic::ResetVideo");

      struct obs_video_info ovi;
      int ret;

      GetConfigFPS(ovi.fps_num, ovi.fps_den);

      const char *colorFormat =
                      config_get_string(basicConfig, "Video", "ColorFormat");
      const char *colorSpace =
                      config_get_string(basicConfig, "Video", "ColorSpace");
      const char *colorRange =
                      config_get_string(basicConfig, "Video", "ColorRange");

      ovi.graphics_module = App()->GetRenderModule();
      ovi.base_width =
                      (uint32_t)config_get_uint(basicConfig, "Video", "BaseCX");
      ovi.base_height =
                      (uint32_t)config_get_uint(basicConfig, "Video", "BaseCY");
      ovi.output_width =
                      (uint32_t)config_get_uint(basicConfig, "Video", "OutputCX");
      ovi.output_height =
                      (uint32_t)config_get_uint(basicConfig, "Video", "OutputCY");
      ovi.output_format = GetVideoFormatFromName(colorFormat);
      ovi.colorspace = astrcmpi(colorSpace, "601") == 0 ? VIDEO_CS_601
                                                        : VIDEO_CS_709;
      ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL
                                                    : VIDEO_RANGE_PARTIAL;
      ovi.adapter =
                      config_get_uint(App()->GlobalConfig(), "Video", "AdapterIdx");
      ovi.gpu_conversion = true;
      ovi.scale_type = GetScaleType(basicConfig);

      if (ovi.base_width == 0 || ovi.base_height == 0) {
          ovi.base_width = 1920;
          ovi.base_height = 1080;
          config_set_uint(basicConfig, "Video", "BaseCX", 1920);
          config_set_uint(basicConfig, "Video", "BaseCY", 1080);
      }

      if (ovi.output_width == 0 || ovi.output_height == 0) {
          ovi.output_width = ovi.base_width;
          ovi.output_height = ovi.base_height;
          config_set_uint(basicConfig, "Video", "OutputCX",
                          ovi.base_width);
          config_set_uint(basicConfig, "Video", "OutputCY",
                          ovi.base_height);
      }

      ret = AttemptToResetVideo(&ovi);
      if (IS_WIN32 && ret != OBS_VIDEO_SUCCESS) {
          if (ret == OBS_VIDEO_CURRENTLY_ACTIVE) {
              blog(LOG_WARNING, "Tried to reset when "
                                "already active");
              return ret;
          }

          /* Try OpenGL if DirectX fails on windows */
        //  if (astrcmpi(ovi.graphics_module, DL_OPENGL) != 0) {
         //     blog(LOG_WARNING,
          //         "Failed to initialize obs video (%d) "
           //        "with graphics_module='%s', retrying "
           //        "with graphics_module='%s'",
            //       ret, ovi.graphics_module, DL_OPENGL);
             // ovi.graphics_module = DL_OPENGL;
            //  ret = AttemptToResetVideo(&ovi);
         // }
      } else if (ret == OBS_VIDEO_SUCCESS) {
          ResizePreview(ovi.base_width, ovi.base_height);
          if (program)
              ResizeProgram(ovi.base_width, ovi.base_height);
      }

      if (ret == OBS_VIDEO_SUCCESS) {
          OBSBasicStats::InitializeValues();
          OBSProjector::UpdateMultiviewProjectors();
      }

      return ret;
  }

  void OBSBasic::GetConfigFPS(uint32_t &num, uint32_t &den) const
   {
       uint32_t type = config_get_uint(basicConfig, "Video", "FPSType");

       if (type == 1) //"Integer"
           GetFPSInteger(num, den);
       else if (type == 2) //"Fraction"
           GetFPSFraction(num, den);
       else if (false) //"Nanoseconds", currently not implemented
           GetFPSNanoseconds(num, den);
       else
           GetFPSCommon(num, den);
   }

  void OBSBasic::GetFPSCommon(uint32_t &num, uint32_t &den) const
  {
      const char *val = config_get_string(basicConfig, "Video", "FPSCommon");

      if (strcmp(val, "10") == 0) {
          num = 10;
          den = 1;
      } else if (strcmp(val, "20") == 0) {
          num = 20;
          den = 1;
      } else if (strcmp(val, "24 NTSC") == 0) {
          num = 24000;
          den = 1001;
      } else if (strcmp(val, "25 PAL") == 0) {
          num = 25;
          den = 1;
      } else if (strcmp(val, "29.97") == 0) {
          num = 30000;
          den = 1001;
      } else if (strcmp(val, "48") == 0) {
          num = 48;
          den = 1;
      } else if (strcmp(val, "50 PAL") == 0) {
          num = 50;
          den = 1;
      } else if (strcmp(val, "59.94") == 0) {
          num = 60000;
          den = 1001;
      } else if (strcmp(val, "60") == 0) {
          num = 60;
          den = 1;
      } else {
          num = 30;
          den = 1;
      }
  }
  void OBSBasic::GetFPSInteger(uint32_t &num, uint32_t &den) const
  {
      num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSInt");
      den = 1;
  }

  void OBSBasic::GetFPSFraction(uint32_t &num, uint32_t &den) const
  {
      num = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNum");
      den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSDen");
  }

  void OBSBasic::GetFPSNanoseconds(uint32_t &num, uint32_t &den) const
  {
      num = 1000000000;
      den = (uint32_t)config_get_uint(basicConfig, "Video", "FPSNS");
  }

  const char *OBSBasic::GetCurrentOutputPath()
   {
       const char *path = nullptr;
       const char *mode = config_get_string(Config(), "Output", "Mode");

       if (strcmp(mode, "Advanced") == 0) {
           const char *advanced_mode =
               config_get_string(Config(), "AdvOut", "RecType");

           if (strcmp(advanced_mode, "FFmpeg") == 0) {
               path = config_get_string(Config(), "AdvOut",
                            "FFFilePath");
           } else {
               path = config_get_string(Config(), "AdvOut",
                            "RecFilePath");
           }
       } else {
           path = config_get_string(Config(), "SimpleOutput", "FilePath");
       }

       return path;
   }
  void OBSBasic::CreatePropertiesWindow(obs_source_t *source)
  {
      if (properties)
          properties->close();

      properties = new OBSBasicProperties(this, source);
      properties->Init();
      properties->setAttribute(Qt::WA_DeleteOnClose, true);
  }
  void OBSBasic::ResizeProgram(uint32_t cx, uint32_t cy)
  {
      QSize targetSize;

      /* resize program panel to fix to the top section of the window */
      targetSize = GetPixelSize(program);
      GetScaleAndCenterPos(int(cx), int(cy),
                   targetSize.width() - PREVIEW_EDGE_SIZE * 2,
                   targetSize.height() - PREVIEW_EDGE_SIZE * 2,
                   programX, programY, programScale);

      programX += float(PREVIEW_EDGE_SIZE);
      programY += float(PREVIEW_EDGE_SIZE);
  }
#define SERVICE_PATH "service.json"

  void OBSBasic::SaveService()
  {
      if (!service)
          return;

      char serviceJsonPath[512];
      int ret = GetProfilePath(serviceJsonPath, sizeof(serviceJsonPath),
                               SERVICE_PATH);
      if (ret <= 0)
          return;

      obs_data_t *data = obs_data_create();
      obs_data_t *settings = obs_service_get_settings(service);

      obs_data_set_string(data, "type", obs_service_get_type(service));
      obs_data_set_obj(data, "settings", settings);

      if (!obs_data_save_json_safe(data, serviceJsonPath, "tmp", "bak"))
          blog(LOG_WARNING, "Failed to save service");

      obs_data_release(settings);
      obs_data_release(data);
  }

  void OBSBasic::SetService(obs_service_t *newService)
   {
       if (newService)
           service = newService;
   }































