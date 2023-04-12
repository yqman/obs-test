#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>

#include <util/config-file.h>

/*QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE
*/
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
  //  MainWindow(QWidget *parent = nullptr);
    inline MainWindow(QWidget *parent) : QMainWindow(parent){}

   // ~MainWindow();
    virtual config_t *Config() const = 0;
    virtual void OBSInit() = 0;
    virtual int GetProfilePath(char *path, size_t size, const char *file) const = 0;
private:
 //   QScopedPointer<QThread> updateCheckThread;
};
#endif // MAINWINDOW_H
