#include "MainWindow.h"
#include "ProfileDialog.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/MultiCallManager.h"
#include "trunkmonkey/Profile.h"
#include "trunkmonkey/RuntimePaths.h"
#include "trunkmonkey/SipEngine.h"
#include "trunkmonkey/Version.h"
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <filesystem>
using namespace trunkmonkey;
static QIcon makeSipherIcon(){
    QPixmap pix(64,64);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing,false);
    p.fillRect(QRect(2,2,60,60),QColor(10,12,10));
    QPen border(QColor(110,255,150));border.setWidth(2);p.setPen(border);p.drawRect(3,3,58,58);
    QFont f=QFontDatabase::systemFont(QFontDatabase::FixedFont);f.setBold(true);f.setPixelSize(34);p.setFont(f);
    p.setPen(QColor(125,255,160));p.drawText(QRect(4,3,56,42),Qt::AlignCenter,QStringLiteral("S"));
    QFont small=QFontDatabase::systemFont(QFontDatabase::FixedFont);small.setBold(true);small.setPixelSize(9);p.setFont(small);
    p.drawText(QRect(4,42,56,16),Qt::AlignCenter,QStringLiteral("IPHER"));
    p.setPen(QPen(QColor(75,170,95),1));for(int y=8;y<60;y+=6)p.drawLine(7,y,57,y);
    p.end();return QIcon(pix);
}
int main(int argc,char**argv){
    QApplication app(argc,argv);app.setApplicationName("S.I.P.H.E.R. By GITSC");app.setApplicationVersion(TRUNKMONKEY_VERSION);
    app.setWindowIcon(makeSipherIcon());
    try{runtime::ensureUserDirectories();}catch(const std::exception&error){QMessageBox::critical(nullptr,"S.I.P.H.E.R.",QString::fromStdString(error.what()));return 2;}
    const std::filesystem::path executable=argc>0?std::filesystem::path(argv[0]):std::filesystem::path{};
    const std::filesystem::path profilePath=argc>=2?std::filesystem::path(argv[1]):runtime::defaultProfilePath(executable);
    try{
        const bool created=ProfileStore::createDefaultIfMissing(profilePath.string());
        SipProfile draft=ProfileStore::loadDraft(profilePath.string());
        if(created || !ProfileStore::isConfigured(draft)){
            QMessageBox::information(nullptr,"S.I.P.H.E.R. SIP setup",QStringLiteral("S.I.P.H.E.R. created your SIP profile at:\n%1\n\nConfigure it now to start registration.").arg(QString::fromStdString(profilePath.string())));
            if(!editSipProfileDialog(nullptr,draft,QString::fromStdString(profilePath.string()),true))return 0;
            ProfileStore::save(draft,profilePath.string());
        }
    }catch(const std::exception&error){QMessageBox::critical(nullptr,"Profile setup failed",error.what());return 2;}
    Logger log(runtime::logPath().string());log.setConsoleEnabled(false);SipEngine engine(log);MultiCallManager multi(engine,log);
    try{engine.start(ProfileStore::load(profilePath.string()),50);}catch(const pj::Error&error){QMessageBox::critical(nullptr,"SIP startup failed",QString::fromStdString(error.info()));return 1;}catch(const std::exception&error){QMessageBox::critical(nullptr,"Startup failed",error.what());return 1;}
    MainWindow window(engine,multi,log,profilePath.string());window.show();const int rc=app.exec();multi.cancelLaunching();engine.stop();return rc;
}
