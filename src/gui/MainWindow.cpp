#include "MainWindow.h"
#include "ProfileDialog.h"
#include "trunkmonkey/CallSnapshot.h"
#include "trunkmonkey/CaptureManager.h"
#include "trunkmonkey/Logger.h"
#include "trunkmonkey/MultiCallManager.h"
#include "trunkmonkey/PbxAudit.h"
#include "trunkmonkey/RuntimePaths.h"
#include "trunkmonkey/SipEngine.h"
#include "trunkmonkey/SipTrace.h"
#include "trunkmonkey/TextPool.h"
#include <QAbstractItemView>
#include <algorithm>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QKeySequence>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <sstream>
#include <utility>
using namespace trunkmonkey;
static std::vector<std::string> loadList(const QString&p){if(p.isEmpty())return{};TextPool t;t.load(p.toStdString());return t.values();}
static QString showAddr(const std::string&s){return s.empty()?QStringLiteral("--"):QString::fromStdString(s);}
static const char* kSipherBlockLogo=u8R"SIPHER(  ██████  ██▓ ██▓███   ██░ ██ ▓█████  ██▀███
▒██    ▒ ▓██▒▓██░  ██▒▓██░ ██▒▓█   ▀ ▓██ ▒ ██▒
░ ▓██▄   ▒██▒▓██░ ██▓▒▒██▀▀██░▒███   ▓██ ░▄█ ▒
  ▒   ██▒░██░▒██▄█▓▒ ▒░▓█ ░██ ▒▓█  ▄ ▒██▀▀█▄
▒██████▒▒░██░▒██▒ ░  ░░▓█▒░██▓░▒████▒░██▓ ▒██▒
▒ ▒▓▒ ▒ ░░▓  ▒▓▒░ ░  ░ ▒ ░░▒░▒░░ ▒░ ░░ ▒▓ ░▒▓░
░ ░▒  ░ ░ ▒ ░░▒ ░      ▒ ░▒░ ░ ░ ░  ░  ░▒ ░ ▒░
░  ░  ░   ▒ ░░░        ░  ░░ ░   ░     ░░   ░
      ░   ░            ░  ░  ░   ░  ░   ░)SIPHER";

MainWindow::MainWindow(SipEngine&e,MultiCallManager&m,Logger&l,std::string profilePath,QWidget*p):QMainWindow(p),engine_(e),multi_(m),logger_(l),profilePath_(std::move(profilePath)){
    buildUi();setWindowTitle("S.I.P.H.E.R. By GITSC 1.0.0 — SIP Inspection, Protocol Handling, Enumeration & Recon");setMinimumSize(680,440);resize(860,600);refreshTimer_=new QTimer(this);connect(refreshTimer_,&QTimer::timeout,this,&MainWindow::refresh);refreshTimer_->start(250);refresh();
}
void MainWindow::buildUi(){
    auto* fileMenu=menuBar()->addMenu(QStringLiteral("&File"));
    auto* exitAction=fileMenu->addAction(QStringLiteral("E&xit"));
    exitAction->setShortcut(QKeySequence(QKeySequence::Quit));
    connect(exitAction,&QAction::triggered,qApp,&QApplication::quit);

    auto* settingsMenu=menuBar()->addMenu(QStringLiteral("&Settings"));
    auto* profileAction=settingsMenu->addAction(QStringLiteral("SIP &Profile..."));
    connect(profileAction,&QAction::triggered,this,&MainWindow::editProfile);
    auto* audioAction=settingsMenu->addAction(QStringLiteral("&Audio Devices..."));
    connect(audioAction,&QAction::triggered,this,&MainWindow::showAudioDevices);
    auto* regHistoryAction=settingsMenu->addAction(QStringLiteral("&Registration History..."));
    connect(regHistoryAction,&QAction::triggered,this,&MainWindow::showRegistrationHistory);

    auto*c=new QWidget;auto*outer=new QVBoxLayout(c);outer->setContentsMargins(7,7,7,7);outer->setSpacing(5);
    auto*logoBanner=new QLabel(QString::fromUtf8(kSipherBlockLogo));
    logoBanner->setTextFormat(Qt::PlainText);
    logoBanner->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont logoFont=QFontDatabase::systemFont(QFontDatabase::FixedFont);
    logoFont.setPixelSize(8);
    logoFont.setBold(true);
    logoBanner->setFont(logoFont);
    logoBanner->setAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    logoBanner->setStyleSheet(QStringLiteral("padding: 2px 5px; border: 1px solid palette(mid); border-radius: 3px;"));
    logoBanner->setToolTip(QStringLiteral("S.I.P.H.E.R. — SIP Inspection, Protocol Handling, Enumeration & Recon — By GITSC"));
    outer->addWidget(logoBanner);
    auto*top=new QGridLayout;
    auto*brand=new QLabel(QStringLiteral("S.I.P.H.E.R. By GITSC"));brand->setStyleSheet(QStringLiteral("font-weight: 800; font-size: 15px; letter-spacing: 1px;"));
    registration_=new QLabel("SIP: starting...");registration_->setStyleSheet("font-weight: 600;");
    theme_=new QComboBox;
    theme_->addItem("System","system");theme_->addItem("Hacker","hacker");theme_->addItem("Matrix","matrix");theme_->addItem("Phosphor","phosphor");theme_->addItem("Midnight","midnight");theme_->addItem("Amber","amber");theme_->addItem("Ice","ice");theme_->addItem("Classic Light","classic-light");theme_->addItem("Solarized Dark","solarized");theme_->addItem("Dracula","dracula");theme_->addItem("Nord","nord");theme_->addItem("Cyberpunk","cyberpunk");theme_->addItem("Blood Moon","blood-moon");theme_->addItem("Ocean","ocean");theme_->addItem("Retro Blue","retro-blue");theme_->addItem("Monochrome","monochrome");theme_->addItem("Blue Box","blue-box");theme_->addItem("Red Box","red-box");theme_->addItem("Beige Box","beige-box");theme_->addItem("2600","2600");theme_->addItem("WarGames","wargames");theme_->addItem("CRT Green","crt-green");theme_->addItem("VT220","vt220");theme_->addItem("Cobalt","cobalt");theme_->addItem("Vaporwave","vaporwave");theme_->addItem("Stealth","stealth");
    const QString settingsPath=QString::fromStdString(trunkmonkey::runtime::settingsPath().string());QSettings themeSettings(settingsPath,QSettings::IniFormat);const QString savedTheme=themeSettings.value(QStringLiteral("ui/theme"),QStringLiteral("system")).toString().toCaseFolded();int themeIndex=theme_->findData(savedTheme);if(themeIndex<0)themeIndex=0;theme_->setCurrentIndex(themeIndex);
    connect(theme_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int){applyTheme(theme_->currentData().toString());});
    top->addWidget(brand,0,0);top->addWidget(registration_,0,1);top->setColumnStretch(2,1);top->addWidget(new QLabel("Theme:"),0,3);top->addWidget(theme_,0,4);outer->addLayout(top);

    tabs_=new QTabWidget;tabs_->setDocumentMode(true);

    // MAIN: phone controls, selected media and packet capture in one compact workspace.
    auto*mainPage=new QWidget;auto*ml=new QVBoxLayout(mainPage);ml->setContentsMargins(7,7,7,7);ml->setSpacing(6);
    auto*f=new QFormLayout;dialEdit_=new QLineEdit;dialEdit_->setPlaceholderText("3305551212 or sip:user@example.net");callerIdEdit_=new QLineEdit;callerIdEdit_->setPlaceholderText("Optional caller identity");f->addRow("Destination",dialEdit_);f->addRow("Caller ID",callerIdEdit_);ml->addLayout(f);
    auto*r=new QGridLayout;auto*b=new QPushButton("CALL");connect(b,&QPushButton::clicked,this,&MainWindow::dial);r->addWidget(b,0,0);b=new QPushButton("ANSWER");connect(b,&QPushButton::clicked,this,&MainWindow::answerSelected);r->addWidget(b,0,1);b=new QPushButton("HANGUP");connect(b,&QPushButton::clicked,this,&MainWindow::hangupSelected);r->addWidget(b,0,2);b=new QPushButton("DTMF PAD...");connect(b,&QPushButton::clicked,this,&MainWindow::showDtmfPad);r->addWidget(b,0,3);muteButton_=new QPushButton("MUTE MIC");connect(muteButton_,&QPushButton::clicked,this,&MainWindow::toggleMuteSelected);r->addWidget(muteButton_,0,4);b=new QPushButton("HANGUP ALL");connect(b,&QPushButton::clicked,this,&MainWindow::hangupAll);r->addWidget(b,0,5);b=new QPushButton("EXIT");connect(b,&QPushButton::clicked,qApp,&QApplication::quit);r->addWidget(b,0,6);ml->addLayout(r);

    auto*mediaBox=new QGroupBox("Selected Call Media");auto*media=new QGridLayout(mediaBox);callIdLabel_=new QLabel("--");mediaTarget_=new QLabel("--");mediaSource_=new QLabel("--");mediaLocal_=new QLabel("--");mediaCodec_=new QLabel("--");mediaQuality_=new QLabel("--");mediaQuality_->setTextInteractionFlags(Qt::TextSelectableByMouse);media->addWidget(new QLabel("SIP Call-ID:"),0,0);media->addWidget(callIdLabel_,0,1,1,3);media->addWidget(new QLabel("RTP target:"),1,0);media->addWidget(mediaTarget_,1,1);media->addWidget(new QLabel("RTP source:"),1,2);media->addWidget(mediaSource_,1,3);media->addWidget(new QLabel("Local RTP:"),2,0);media->addWidget(mediaLocal_,2,1);media->addWidget(new QLabel("Codec:"),2,2);media->addWidget(mediaCodec_,2,3);media->addWidget(new QLabel("Quality:"),3,0);media->addWidget(mediaQuality_,3,1,1,3);media->setColumnStretch(1,1);media->setColumnStretch(3,1);ml->addWidget(mediaBox);auto*diagButtons=new QGridLayout;b=new QPushButton("SIP LADDER...");connect(b,&QPushButton::clicked,this,&MainWindow::showSipLadder);diagButtons->addWidget(b,0,0);b=new QPushButton("EXPORT CALL REPORT...");connect(b,&QPushButton::clicked,this,&MainWindow::exportCallReport);diagButtons->addWidget(b,0,1);ml->addLayout(diagButtons);

    auto*capBox=new QGroupBox("Packet Capture");auto*cap=new QGridLayout(capBox);captureInterface_=new QComboBox;captureInterface_->setEditable(true);for(const auto& iface:CaptureManager::availableInterfaces())captureInterface_->addItem(QString::fromStdString(iface));if(captureInterface_->count()==0)captureInterface_->addItem("any");int anyIndex=captureInterface_->findText("any");if(anyIndex>=0)captureInterface_->setCurrentIndex(anyIndex);captureInterface_->setToolTip(QString::fromStdString(CaptureManager::permissionHint()));cap->addWidget(new QLabel("Interface:"),0,0);cap->addWidget(captureInterface_,0,1);
    sipPcapStart_=new QPushButton("START SIP PCAP...");connect(sipPcapStart_,&QPushButton::clicked,this,&MainWindow::startSipPcap);cap->addWidget(sipPcapStart_,0,2);rtpPcapStart_=new QPushButton("START RTP PCAP...");connect(rtpPcapStart_,&QPushButton::clicked,this,&MainWindow::startRtpPcap);cap->addWidget(rtpPcapStart_,0,3);callPcapStart_=new QPushButton("START CALL PCAP...");connect(callPcapStart_,&QPushButton::clicked,this,&MainWindow::startCallPcap);cap->addWidget(callPcapStart_,0,4);pcapStop_=new QPushButton("STOP PCAPS");connect(pcapStop_,&QPushButton::clicked,this,&MainWindow::stopPcaps);cap->addWidget(pcapStop_,0,5);captureStatus_=new QLabel("SIP PCAP: stopped | RTP PCAP: stopped | CALL PCAP: stopped");captureStatus_->setWordWrap(true);cap->addWidget(captureStatus_,1,0,1,4);b=new QPushButton("OPEN LAST PCAP (AUTO RTP)");b->setToolTip("Launch Wireshark with this call's negotiated RTP/RTCP ports pre-decoded; no manual Decode As step.");connect(b,&QPushButton::clicked,this,&MainWindow::openLastPcap);cap->addWidget(b,1,4,1,2);auto*perm=new QLabel(QString::fromStdString(CaptureManager::permissionHint()));perm->setWordWrap(true);perm->setStyleSheet(QStringLiteral("font-size: 10px;"));cap->addWidget(perm,2,0,1,6);ml->addWidget(capBox);
    auto*mainNote=new QLabel("Select a normal Phone call on Active Calls for media/capture controls. RTP-only PCAPs may require Wireshark's rtp_udp heuristic to populate RTP Streams.");mainNote->setWordWrap(true);ml->addWidget(mainNote);ml->addStretch();tabs_->addTab(mainPage,"Main");

    // ACTIVE CALLS
    auto*active=new QWidget;auto*al=new QVBoxLayout(active);calls_=new QTableWidget(0,12);calls_->setHorizontalHeaderLabels({"ID","Mode","Dir","FG","State","SIP","Remote","Caller ID","RTP Target","RTP Source","Codec","Reason"});calls_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);calls_->horizontalHeader()->setStretchLastSection(true);calls_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);calls_->setColumnWidth(0,48);calls_->setColumnWidth(1,72);calls_->setColumnWidth(2,48);calls_->setColumnWidth(3,38);calls_->setColumnWidth(4,110);calls_->setColumnWidth(5,58);calls_->setColumnWidth(6,190);calls_->setColumnWidth(7,120);calls_->setSelectionBehavior(QAbstractItemView::SelectRows);calls_->setSelectionMode(QAbstractItemView::SingleSelection);connect(calls_,&QTableWidget::itemSelectionChanged,this,&MainWindow::refreshDiagnostics);al->addWidget(calls_,1);
    r=new QGridLayout;b=new QPushButton("FOREGROUND");connect(b,&QPushButton::clicked,this,&MainWindow::foregroundSelected);r->addWidget(b,0,0);b=new QPushButton("HOLD");connect(b,&QPushButton::clicked,this,&MainWindow::holdSelected);r->addWidget(b,0,1);b=new QPushButton("RESUME");connect(b,&QPushButton::clicked,this,&MainWindow::resumeSelected);r->addWidget(b,0,2);b=new QPushButton("MUTE / UNMUTE MIC");connect(b,&QPushButton::clicked,this,&MainWindow::toggleMuteSelected);r->addWidget(b,0,3);b=new QPushButton("DTMF PAD...");connect(b,&QPushButton::clicked,this,&MainWindow::showDtmfPad);r->addWidget(b,0,4);al->addLayout(r);tabs_->addTab(active,"Active Calls");

    // SIP LOG
    auto*sipPage=new QWidget;auto*sl=new QVBoxLayout(sipPage);diagnosticNote_=new QLabel("Select a normal Phone call on Active Calls to inspect its SIP dialog.");diagnosticNote_->setWordWrap(true);sl->addWidget(diagnosticNote_);
    auto*sipButtons=new QGridLayout;sipTraceStart_=new QPushButton("START RAW SIP TRACE...");connect(sipTraceStart_,&QPushButton::clicked,this,&MainWindow::startSipTrace);sipButtons->addWidget(sipTraceStart_,0,0);sipTraceStop_=new QPushButton("STOP RAW SIP TRACE");connect(sipTraceStop_,&QPushButton::clicked,this,&MainWindow::stopSipTrace);sipButtons->addWidget(sipTraceStop_,0,1);sl->addLayout(sipButtons);
    sipLog_=new QTableWidget(0,6);sipLog_->setHorizontalHeaderLabels({"Time","Dir","Signal","CSeq","Code","Reason"});sipLog_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);sipLog_->horizontalHeader()->setStretchLastSection(true);sipLog_->setSelectionBehavior(QAbstractItemView::SelectRows);sipLog_->setSelectionMode(QAbstractItemView::SingleSelection);connect(sipLog_,&QTableWidget::itemSelectionChanged,this,&MainWindow::showRawSip);sl->addWidget(sipLog_,2);rawSip_=new QPlainTextEdit;rawSip_->setReadOnly(true);rawSip_->setPlaceholderText("Select a SIP transaction above to inspect the full message.");rawSip_->setMaximumBlockCount(10000);sl->addWidget(rawSip_,2);tabs_->addTab(sipPage,"SIP Log");

    // QUEUE TEST
    auto*q=new QWidget;auto*ql=new QVBoxLayout(q);f=new QFormLayout;batchCount_=new QSpinBox;batchCount_->setRange(1,50);batchCount_->setValue(5);launchInterval_=new QSpinBox;launchInterval_->setRange(50,60000);launchInterval_->setValue(250);launchInterval_->setSuffix(" ms");batchDestination_=new QLineEdit;batchDestination_->setPlaceholderText("Single queue/DID target");fixedCallerId_=new QLineEdit;fixedCallerId_->setPlaceholderText("Fixed CID (ignored when list is loaded)");f->addRow("Calls",batchCount_);f->addRow("Launch interval",launchInterval_);f->addRow("Single destination",batchDestination_);f->addRow("Fixed caller ID",fixedCallerId_);ql->addLayout(f);destinationFileLabel_=new QLabel("No destination list loaded");callerIdFileLabel_=new QLabel("No caller-ID list loaded");queueAudioFileLabel_=new QLabel("Live/no injected audio");r=new QGridLayout;b=new QPushButton("LOAD DESTINATIONS.TXT");connect(b,&QPushButton::clicked,this,&MainWindow::loadDestinations);r->addWidget(b,0,0);r->addWidget(destinationFileLabel_,0,1);b=new QPushButton("LOAD CALLERIDS.TXT");connect(b,&QPushButton::clicked,this,&MainWindow::loadCallerIds);r->addWidget(b,1,0);r->addWidget(callerIdFileLabel_,1,1);b=new QPushButton("LOAD AUDIO FILE...");connect(b,&QPushButton::clicked,this,&MainWindow::loadQueueAudio);r->addWidget(b,2,0);r->addWidget(queueAudioFileLabel_,2,1);ql->addLayout(r);auto*note=new QLabel("Each launched call is an independent SIP dialog and RTP session. Optional WAV/MP3 audio is normalized by ffmpeg and injected into every queue-test call. Batch calls are not conferenced and are not automatically routed to the local headset.");note->setWordWrap(true);ql->addWidget(note);b=new QPushButton("START QUEUE TEST");connect(b,&QPushButton::clicked,this,&MainWindow::launchBatch);ql->addWidget(b);ql->addStretch();tabs_->addTab(q,"Queue Test");

    // PBX AUDIT — active, bounded probes for systems the operator is authorized to test.
    auto*auditPage=new QWidget;auto*aul=new QVBoxLayout(auditPage);auto*warning=new QLabel(QString::fromUtf8(PbxAudit::warningText()));warning->setWordWrap(true);warning->setStyleSheet(QStringLiteral("font-weight:700; color:#ff5a5a;"));aul->addWidget(warning);
    auto*auf=new QFormLayout;auditHost_=new QLineEdit;auditHost_->setPlaceholderText("PBX/SBC hostname, IP, or /27-/32 CIDR for discovery");auditUser_=new QLineEdit;auditUser_->setPlaceholderText("Known test extension/account for auth policy");auditPort_=new QSpinBox;auditPort_->setRange(1,65535);auditPort_->setValue(5060);auditTransport_=new QComboBox;auditTransport_->addItem("UDP","udp");auditTransport_->addItem("TCP","tcp");auditExtFirst_=new QSpinBox;auditExtFirst_->setRange(1,999999);auditExtFirst_->setValue(100);auditExtLast_=new QSpinBox;auditExtLast_->setRange(1,999999);auditExtLast_->setValue(120);auf->addRow("Target",auditHost_);auf->addRow("SIP port",auditPort_);auf->addRow("Transport",auditTransport_);auf->addRow("Test user",auditUser_);auf->addRow("Extension range start",auditExtFirst_);auf->addRow("Extension range end",auditExtLast_);aul->addLayout(auf);
    auto*aub=new QGridLayout;b=new QPushButton("PBX FINGERPRINT");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditFingerprint);aub->addWidget(b,0,0);b=new QPushButton("CVE / EXPLOIT-DB LOOKUP");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditVulns);aub->addWidget(b,0,1);
    b=new QPushButton("SERVICE PROBE");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditProbe);aub->addWidget(b,1,0);b=new QPushButton("DISCOVER CIDR");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditDiscover);aub->addWidget(b,1,1);b=new QPushButton("METHOD POLICY");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditMethods);aub->addWidget(b,1,2);b=new QPushButton("AUTH POLICY");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditAuth);aub->addWidget(b,2,0);b=new QPushButton("EXTENSION AUDIT");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditExtensions);aub->addWidget(b,2,1);b=new QPushButton("COMPLIANCE");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditCompliance);aub->addWidget(b,2,2);b=new QPushButton("PARSER ABUSE");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditParser);aub->addWidget(b,3,0);b=new QPushButton("RATE RESILIENCE");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditResilience);aub->addWidget(b,3,1);b=new QPushButton("ATTACK SCENARIO");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditScenario);aub->addWidget(b,3,2);b=new QPushButton("TLS 5061");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditTls);aub->addWidget(b,4,0);b=new QPushButton("FULL ENGINEERING AUDIT");connect(b,&QPushButton::clicked,this,&MainWindow::runAuditFull);aub->addWidget(b,4,1);b=new QPushButton("SAVE REPORT...");connect(b,&QPushButton::clicked,this,&MainWindow::saveAuditReport);aub->addWidget(b,4,2);aul->addLayout(aub);auditOutput_=new QPlainTextEdit;auditOutput_->setReadOnly(true);auditOutput_->setPlaceholderText("PBX audit results appear here. Attack-scenario tests are realistic but rate-capped and intentionally exclude password cracking, destructive crash payloads, and takeover automation.");aul->addWidget(auditOutput_,1);tabs_->addTab(auditPage,"PBX Audit");

    // PROFILE / CONFIG
    auto*profilePage=new QWidget;auto*pfl=new QVBoxLayout(profilePage);profileSummary_=new QLabel;profileSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);profileSummary_->setAlignment(Qt::AlignTop|Qt::AlignLeft);profileSummary_->setWordWrap(true);pfl->addWidget(profileSummary_);b=new QPushButton("EDIT SIP PROFILE...");connect(b,&QPushButton::clicked,this,&MainWindow::editProfile);pfl->addWidget(b);pfl->addStretch();tabs_->addTab(profilePage,"Profile");

    // ACTIVITY
    auto*activityPage=new QWidget;auto*actl=new QVBoxLayout(activityPage);activityLog_=new QPlainTextEdit;activityLog_->setReadOnly(true);activityLog_->setMaximumBlockCount(2000);actl->addWidget(activityLog_);tabs_->addTab(activityPage,"Activity");

    outer->addWidget(tabs_,1);setCentralWidget(c);applyTheme(theme_->currentData().toString());statusBar()->showMessage("S.I.P.H.E.R. By GITSC 1.0.0 — SIP Inspection, Protocol Handling, Enumeration & Recon");setDiagnosticsEnabled(false);
}

void MainWindow::refresh(){
    registration_->setText(QString::fromStdString("SIP: "+engine_.registrationText()));
    if(profileSummary_){const auto&p=engine_.profile();profileSummary_->setText(QString("<b>%1</b><br>File: %2<br>SIP URI: sip:%3@%4<br>Registrar: %5<br>Transport: %6<br>ICE: %7 &nbsp; SRTP: %8")
        .arg(QString::fromStdString(p.name)).arg(QString::fromStdString(profilePath_)).arg(QString::fromStdString(p.username)).arg(QString::fromStdString(p.sipDomain)).arg(QString::fromStdString(p.registrar)).arg(QString::fromStdString(toString(p.transport)).toUpper()).arg(p.useIce?"enabled":"disabled").arg(p.enableSrtp?"enabled":"disabled"));}
    if(activityLog_){QString summary=QString("Registration: %1\nCalls known: %2\n%3\nLog file: %4")
        .arg(QString::fromStdString(engine_.registrationText())).arg((int)engine_.calls().size()).arg(QString::fromStdString(engine_.captureStatus())).arg(QString::fromStdString(trunkmonkey::runtime::logPath().string()));if(activityLog_->toPlainText()!=summary)activityLog_->setPlainText(summary);}
    int keep=pendingSelectId_>=0?pendingSelectId_:selectedCallId();pendingSelectId_=-1;
    auto v=engine_.calls();calls_->blockSignals(true);calls_->setRowCount((int)v.size());int row=0,selectRow=-1;for(auto&x:v){
        QString codec=x.codecName.empty()?"--":QString::fromStdString(x.codecName)+(x.codecClockRate?QString("/%1").arg(x.codecClockRate):QString{});
        QStringList s={QString::number(x.id),x.purpose==CallPurpose::Phone?"PHONE":"QUEUE",x.direction==CallDirection::Incoming?"IN":"OUT",x.foreground?"*":"",QString::fromStdString(x.state),QString::number(x.lastStatusCode),QString::fromStdString(x.remoteUri),QString::fromStdString(x.callerId),showAddr(x.remoteRtpAddress),showAddr(x.sourceRtpAddress),codec,QString::fromStdString(x.lastReason)};
        for(int col=0;col<s.size();++col)calls_->setItem(row,col,new QTableWidgetItem(s[col]));if(x.id==keep)selectRow=row;++row;
    }
    calls_->blockSignals(false);if(selectRow>=0)calls_->selectRow(selectRow);else if(v.size()==1&&v.front().purpose==CallPurpose::Phone)calls_->selectRow(0);refreshDiagnostics();
}
int MainWindow::selectedCallId()const{auto rows=calls_->selectionModel()->selectedRows();if(rows.isEmpty())return-1;auto*item=calls_->item(rows.first().row(),0);return item?item->text().toInt():-1;}
void MainWindow::selectCallId(int id){for(int r=0;r<calls_->rowCount();++r){auto*i=calls_->item(r,0);if(i&&i->text().toInt()==id){calls_->selectRow(r);return;}}pendingSelectId_=id;}
void MainWindow::setDiagnosticsEnabled(bool e){for(auto*w:{sipTraceStart_,sipTraceStop_,sipPcapStart_,rtpPcapStart_,callPcapStart_})if(w)w->setEnabled(e);if(captureInterface_)captureInterface_->setEnabled(e);}
void MainWindow::refreshDiagnostics(){
    int id=selectedCallId();captureStatus_->setText(QString::fromStdString(engine_.captureStatus()));if(id<0){setDiagnosticsEnabled(false);if(muteButton_){muteButton_->setEnabled(false);muteButton_->setText("MUTE MIC");}callIdLabel_->setText("--");mediaTarget_->setText("--");mediaSource_->setText("--");mediaLocal_->setText("--");mediaCodec_->setText("--");if(mediaQuality_)mediaQuality_->setText("--");diagnosticNote_->setText("Select a normal Phone call to view its SIP dialog and media endpoints.");sipLog_->setRowCount(0);rawSip_->clear();displayedTraceCallId_=-1;displayedTraceCount_=0;return;}
    try{
        auto c=engine_.callSnapshot(id);callIdLabel_->setText(showAddr(c.callIdString));mediaTarget_->setText(showAddr(c.remoteRtpAddress));mediaSource_->setText(showAddr(c.sourceRtpAddress));mediaLocal_->setText(showAddr(c.localRtpAddress));
        mediaCodec_->setText(c.codecName.empty()?"--":QString::fromStdString(c.codecName)+(c.codecClockRate?QString(" / %1 Hz").arg(c.codecClockRate):QString{}));
        if(mediaQuality_){const double den=(double)(c.rtpRxPackets+c.rtpRxLoss);const double loss=den>0.0?100.0*c.rtpRxLoss/den:0.0;mediaQuality_->setText(QString("RX %1 pkts | loss %2% | jitter %3 ms | RTT %4 ms | R %5 | MOS %6").arg((qulonglong)c.rtpRxPackets).arg(loss,0,'f',2).arg(c.rxJitterMs,0,'f',1).arg(c.rttMs,0,'f',1).arg(c.estimatedRFactor,0,'f',1).arg(c.estimatedMos,0,'f',2));}
        if(muteButton_){muteButton_->setEnabled(c.purpose==CallPurpose::Phone&&!c.disconnected);muteButton_->setText(c.microphoneMuted?"UNMUTE MIC":"MUTE MIC");}
        if(c.purpose!=CallPurpose::Phone){setDiagnosticsEnabled(false);diagnosticNote_->setText("Queue-test call selected. 2.0 keeps detailed SIP/RTP trace controls on normal single Phone calls only.");sipLog_->setRowCount(0);rawSip_->clear();displayedTraceCallId_=-1;displayedTraceCount_=0;return;}
        setDiagnosticsEnabled(true);auto rec=engine_.sipTraceRecording(id);sipTraceStart_->setEnabled(!rec);sipTraceStop_->setEnabled(rec);diagnosticNote_->setText(rec?QString("Raw SIP trace recording: %1").arg(QString::fromStdString(engine_.sipTracePath(id))):"Live SIP dialog logging active. Later INVITE transactions are labeled RE-INVITE.");
        auto t=engine_.sipTrace(id);if(displayedTraceCallId_!=id||displayedTraceCount_!=t.size()){
            sipLog_->blockSignals(true);sipLog_->setRowCount((int)t.size());for(int r=0;r<(int)t.size();++r){auto&e=t[(std::size_t)r];QStringList s={QDateTime::fromMSecsSinceEpoch((qint64)e.timestampMs).toString("HH:mm:ss.zzz"),e.direction==SipDirection::Sent?"TX":"RX",QString::fromStdString(e.label),QString::number(e.cseq),e.statusCode?QString::number(e.statusCode):QString{},QString::fromStdString(e.reason)};for(int col=0;col<s.size();++col){auto*i=new QTableWidgetItem(s[col]);if(col==0)i->setData(Qt::UserRole,QString::fromStdString(e.rawMessage));sipLog_->setItem(r,col,i);}}
            sipLog_->blockSignals(false);displayedTraceCallId_=id;displayedTraceCount_=t.size();if(!t.empty())sipLog_->scrollToBottom();
        }
    }catch(const std::exception&e){diagnosticNote_->setText(QString("Diagnostics unavailable: %1").arg(e.what()));setDiagnosticsEnabled(false);}
}
void MainWindow::showRawSip(){auto rows=sipLog_->selectionModel()->selectedRows();if(rows.isEmpty())return;auto*i=sipLog_->item(rows.first().row(),0);if(i)rawSip_->setPlainText(i->data(Qt::UserRole).toString());}
void MainWindow::dial(){try{auto d=dialEdit_->text().trimmed();if(d.isEmpty())return;auto id=engine_.makeCall(d.toStdString(),callerIdEdit_->text().trimmed().toStdString(),true,CallPurpose::Phone);pendingSelectId_=id;}catch(const pj::Error&e){QMessageBox::critical(this,"Dial failed",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::critical(this,"Dial failed",e.what());}}
void MainWindow::answerSelected(){try{int id=selectedCallId();if(id>=0)engine_.answer(id);}catch(const pj::Error&e){QMessageBox::warning(this,"Answer",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Answer",e.what());}}
void MainWindow::hangupSelected(){try{int id=selectedCallId();if(id>=0)engine_.hangup(id);}catch(const pj::Error&e){QMessageBox::warning(this,"Hangup",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Hangup",e.what());}}
void MainWindow::foregroundSelected(){try{int id=selectedCallId();if(id>=0)engine_.setForeground(id);}catch(const pj::Error&e){QMessageBox::warning(this,"Foreground",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Foreground",e.what());}}
void MainWindow::holdSelected(){try{int id=selectedCallId();if(id>=0)engine_.hold(id);}catch(const pj::Error&e){QMessageBox::warning(this,"Hold",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Hold",e.what());}}
void MainWindow::resumeSelected(){try{int id=selectedCallId();if(id>=0)engine_.resume(id);}catch(const pj::Error&e){QMessageBox::warning(this,"Resume",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Resume",e.what());}}
void MainWindow::toggleMuteSelected(){try{int id=selectedCallId();if(id<0)return;auto c=engine_.callSnapshot(id);if(c.purpose!=CallPurpose::Phone)return;engine_.setMicrophoneMuted(id,!c.microphoneMuted);refreshDiagnostics();}catch(const pj::Error&e){QMessageBox::warning(this,"Microphone",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Microphone",e.what());}}
void MainWindow::showDtmfPad(){
    const int id=selectedCallId();
    if(id<0){QMessageBox::information(this,"DTMF","Select an active Phone call first.");return;}
    try{if(engine_.callSnapshot(id).purpose!=CallPurpose::Phone){QMessageBox::information(this,"DTMF","DTMF pad is available for normal Phone calls.");return;}}catch(const std::exception&e){QMessageBox::warning(this,"DTMF",e.what());return;}

    QDialog pad(this);pad.setWindowTitle(QString("DTMF Pad — Call %1").arg(id));pad.setModal(false);pad.setMinimumWidth(280);
    auto*layout=new QVBoxLayout(&pad);auto*hint=new QLabel("Press and hold a key. The RFC4733 event duration follows how long you hold the mouse button; the event is sent on release.");hint->setWordWrap(true);layout->addWidget(hint);
    auto*grid=new QGridLayout;layout->addLayout(grid);auto*status=new QLabel("Ready");status->setAlignment(Qt::AlignCenter);layout->addWidget(status);
    QElapsedTimer held;QString activeDigit;
    const QStringList digits={"1","2","3","4","5","6","7","8","9","*","0","#"};
    for(int i=0;i<digits.size();++i){
        auto*key=new QPushButton(digits[i]);key->setMinimumSize(70,48);key->setAutoRepeat(false);grid->addWidget(key,i/3,i%3);
        connect(key,&QPushButton::pressed,&pad,[&,key](){activeDigit=key->text();held.restart();status->setText(QString("Holding %1...").arg(activeDigit));});
        connect(key,&QPushButton::released,&pad,[&,key](){
            if(activeDigit!=key->text() || !held.isValid())return;
            const auto elapsed=held.elapsed();
            const unsigned duration=static_cast<unsigned>(std::clamp<qint64>(elapsed,80,5000));
            try{engine_.sendDtmf(id,key->text().toStdString(),duration);status->setText(QString("Sent %1 — %2 ms").arg(key->text()).arg(duration));}
            catch(const pj::Error&e){QMessageBox::warning(&pad,"DTMF",QString::fromStdString(e.info()));}
            catch(const std::exception&e){QMessageBox::warning(&pad,"DTMF",e.what());}
            activeDigit.clear();held.invalidate();
        });
    }
    auto*close=new QPushButton("CLOSE");connect(close,&QPushButton::clicked,&pad,&QDialog::accept);layout->addWidget(close);
    pad.exec();
}
void MainWindow::startSipTrace(){int id=selectedCallId();if(id<0)return;auto path=QFileDialog::getSaveFileName(this,"Save raw SIP trace",QString("sipher-call-%1-sip.log").arg(id),"Log files (*.log *.txt);;All files (*)");if(path.isEmpty())return;try{engine_.startSipTraceFile(id,path.toStdString());refreshDiagnostics();}catch(const std::exception&e){QMessageBox::warning(this,"SIP trace",e.what());}}
void MainWindow::stopSipTrace(){int id=selectedCallId();if(id<0)return;try{engine_.stopSipTraceFile(id);refreshDiagnostics();}catch(const std::exception&e){QMessageBox::warning(this,"SIP trace",e.what());}}
void MainWindow::startSipPcap(){int id=selectedCallId();if(id<0)return;auto path=QFileDialog::getSaveFileName(this,"Save SIP packet capture",QString("sipher-call-%1-sip.pcapng").arg(id),"PCAP files (*.pcap *.pcapng);;All files (*)");if(path.isEmpty())return;try{engine_.startSipPcap(id,path.toStdString(),captureInterface_->currentText().trimmed().toStdString());refreshDiagnostics();}catch(const std::exception&e){QMessageBox::warning(this,"SIP PCAP",e.what());}}
void MainWindow::startRtpPcap(){int id=selectedCallId();if(id<0)return;auto path=QFileDialog::getSaveFileName(this,"Save RTP packet capture",QString("sipher-call-%1-rtp.pcapng").arg(id),"PCAP files (*.pcap *.pcapng);;All files (*)");if(path.isEmpty())return;try{engine_.startRtpPcap(id,path.toStdString(),captureInterface_->currentText().trimmed().toStdString());lastPcapPath_=path;lastPcapCallId_=id;refreshDiagnostics();}catch(const std::exception&e){QMessageBox::warning(this,"RTP PCAP",e.what());}}
void MainWindow::startCallPcap(){int id=selectedCallId();if(id<0)return;auto path=QFileDialog::getSaveFileName(this,"Save combined call packet capture",QString("sipher-call-%1-combined.pcapng").arg(id),"PCAP files (*.pcap *.pcapng);;All files (*)");if(path.isEmpty())return;try{engine_.startCallPcap(id,path.toStdString(),captureInterface_->currentText().trimmed().toStdString());lastPcapPath_=path;lastPcapCallId_=id;refreshDiagnostics();}catch(const std::exception&e){QMessageBox::warning(this,"Call PCAP",e.what());}}
void MainWindow::stopPcaps(){engine_.stopCaptures();refreshDiagnostics();}
void MainWindow::openLastPcap(){if(lastPcapPath_.isEmpty()||lastPcapCallId_<0){QMessageBox::information(this,"Wireshark Auto RTP","Start an RTP or combined call PCAP first.");return;}try{engine_.openPcapInWireshark(lastPcapCallId_,lastPcapPath_.toStdString());statusBar()->showMessage("Opened in Wireshark with automatic RTP/RTCP Decode As",5000);}catch(const std::exception&e){QMessageBox::warning(this,"Wireshark Auto RTP",e.what());}}
void MainWindow::loadDestinations(){destinationFile_=QFileDialog::getOpenFileName(this,"Destination list",{},"Text files (*.txt);;All files (*)");destinationFileLabel_->setText(destinationFile_.isEmpty()?"No destination list loaded":destinationFile_);}
void MainWindow::loadCallerIds(){callerIdFile_=QFileDialog::getOpenFileName(this,"Caller-ID list",{},"Text files (*.txt);;All files (*)");callerIdFileLabel_->setText(callerIdFile_.isEmpty()?"No caller-ID list loaded":callerIdFile_);}
void MainWindow::loadQueueAudio(){queueAudioFile_=QFileDialog::getOpenFileName(this,"Queue-test audio",{},"Audio files (*.wav *.mp3 *.flac *.ogg *.m4a);;All files (*)");if(queueAudioFileLabel_)queueAudioFileLabel_->setText(queueAudioFile_.isEmpty()?"Live/no injected audio":queueAudioFile_);}
void MainWindow::launchBatch(){try{MultiCallPlan p;p.callCount=(std::size_t)batchCount_->value();p.launchIntervalMs=(unsigned)launchInterval_->value();p.singleDestination=batchDestination_->text().trimmed().toStdString();p.fixedCallerId=fixedCallerId_->text().trimmed().toStdString();p.audioFile=queueAudioFile_.toStdString();if(!destinationFile_.isEmpty())p.destinations=loadList(destinationFile_);if(!callerIdFile_.isEmpty())p.callerIds=loadList(callerIdFile_);multi_.start(p);}catch(const std::exception&e){QMessageBox::critical(this,"Queue test",e.what());}}
void MainWindow::hangupAll(){engine_.hangupAll();}
void MainWindow::showSipLadder(){int id=selectedCallId();if(id<0)return;try{QMessageBox box(this);box.setWindowTitle(QString("SIP Ladder — Call %1").arg(id));box.setTextFormat(Qt::PlainText);box.setText(QString::fromStdString(engine_.sipLadder(id)));box.setStandardButtons(QMessageBox::Ok);box.exec();}catch(const std::exception&e){QMessageBox::warning(this,"SIP ladder",e.what());}}
void MainWindow::exportCallReport(){int id=selectedCallId();if(id<0)return;auto path=QFileDialog::getSaveFileName(this,"Export call diagnostic report",QString("sipher-call-%1-report.txt").arg(id),"Text reports (*.txt);;All files (*)");if(path.isEmpty())return;try{engine_.exportCallReport(id,path.toStdString());statusBar()->showMessage("Call report exported",5000);}catch(const std::exception&e){QMessageBox::warning(this,"Call report",e.what());}}

void MainWindow::showAudioDevices(){try{const auto devices=engine_.audioDevices();if(devices.empty()){QMessageBox::information(this,"Audio Devices","No PJSIP audio devices are available.");return;}QDialog d(this);d.setWindowTitle("Audio Devices");auto*layout=new QVBoxLayout(&d);auto*form=new QFormLayout;auto*capture=new QComboBox;auto*playback=new QComboBox;for(const auto&dev:devices){const auto label=QString("[%1] %2 / %3").arg(dev.id).arg(QString::fromStdString(dev.driver)).arg(QString::fromStdString(dev.name));if(dev.inputCount>0){capture->addItem(label,dev.id);if(dev.id==engine_.activeCaptureDevice())capture->setCurrentIndex(capture->count()-1);}if(dev.outputCount>0){playback->addItem(label,dev.id);if(dev.id==engine_.activePlaybackDevice())playback->setCurrentIndex(playback->count()-1);}}form->addRow("Microphone",capture);form->addRow("Playback",playback);layout->addLayout(form);auto*hint=new QLabel("Capture and playback are independent. S.I.P.H.E.R. keeps the verified FreeBSD headset-mic auto-routing, but these controls let you override it when using USB/HDMI/other devices.");hint->setWordWrap(true);layout->addWidget(hint);auto*buttons=new QGridLayout;auto*apply=new QPushButton("APPLY");auto*cancel=new QPushButton("CANCEL");buttons->addWidget(apply,0,0);buttons->addWidget(cancel,0,1);layout->addLayout(buttons);connect(cancel,&QPushButton::clicked,&d,&QDialog::reject);connect(apply,&QPushButton::clicked,&d,[&](){engine_.selectAudioDevices(capture->currentData().toInt(),playback->currentData().toInt());d.accept();});d.exec();}catch(const pj::Error&e){QMessageBox::warning(this,"Audio Devices",QString::fromStdString(e.info()));}catch(const std::exception&e){QMessageBox::warning(this,"Audio Devices",e.what());}}
void MainWindow::showRegistrationHistory(){std::ostringstream out;for(const auto&line:engine_.registrationHistory())out<<line<<"\n";QMessageBox box(this);box.setWindowTitle("Registration History");box.setTextFormat(Qt::PlainText);box.setText(QString::fromStdString(out.str().empty()?std::string("No registration state changes recorded yet."):out.str()));box.exec();}

static AuditTransport guiAuditTransport(QComboBox* box){return PbxAudit::transportFromString(box?box->currentData().toString().toStdString():"udp");}
void MainWindow::runAuditFingerprint(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty()){QMessageBox::information(this,"PBX fingerprint","Enter a target first.");return;}auto fp=PbxAudit::fingerprint(host,(std::uint16_t)auditPort_->value(),PbxAudit::transportFromString(auditTransport_->currentData().toString().toStdString()));lastAuditReport_=fp.toText();auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX fingerprint",e.what());}}
void MainWindow::runAuditVulns(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty()){QMessageBox::information(this,"Vulnerability lookup","Enter a target first.");return;}auto fp=PbxAudit::fingerprint(host,(std::uint16_t)auditPort_->value(),PbxAudit::transportFromString(auditTransport_->currentData().toString().toStdString()));lastAuditReport_=fp.toText()+"\n"+PbxAudit::vulnerabilityLookupReport(fp);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"Vulnerability lookup",e.what());}}
void MainWindow::runAuditProbe(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto r=PbxAudit::serviceProbe(host,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX SERVICE PROBE",{r});auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}
void MainWindow::runAuditDiscover(){try{const auto cidr=auditHost_->text().trimmed().toStdString();if(cidr.empty())throw std::runtime_error("IPv4 CIDR is required");auto entries=PbxAudit::discoverIpv4Cidr(cidr,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("BOUNDED SIP DISCOVERY",{}, {}, {},entries);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX discovery",e.what());}}
void MainWindow::runAuditMethods(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto rs=PbxAudit::methodAudit(host,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX METHOD POLICY AUDIT",rs);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX methods",e.what());}}
void MainWindow::runAuditAuth(){try{const auto host=auditHost_->text().trimmed().toStdString();const auto user=auditUser_->text().trimmed().toStdString();if(host.empty()||user.empty())throw std::runtime_error("Target and a known test user are required");auto r=PbxAudit::authenticationAudit(host,user,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX AUTHENTICATION AUDIT",{r});auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}
void MainWindow::runAuditExtensions(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto entries=PbxAudit::extensionAudit(host,(unsigned)auditExtFirst_->value(),(unsigned)auditExtLast_->value(),(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX EXTENSION DIFFERENTIAL AUDIT",{},entries);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}
void MainWindow::runAuditCompliance(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto rs=PbxAudit::complianceAudit(host,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX BOUNDED COMPLIANCE AUDIT",rs);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}
void MainWindow::runAuditParser(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto rs=PbxAudit::parserAbuseAudit(host,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX PARSER-ABUSE SIMULATION",rs);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX parser audit",e.what());}}
void MainWindow::runAuditResilience(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto rs=PbxAudit::resilienceAudit(host,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));lastAuditReport_=PbxAudit::report("PBX BOUNDED RATE-RESILIENCE SIMULATION",rs);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX resilience audit",e.what());}}
void MainWindow::runAuditScenario(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");auto user=auditUser_->text().trimmed().toStdString();if(user.empty())user=engine_.profile().username;auto rs=PbxAudit::attackScenarioAudit(host,user,(std::uint16_t)auditPort_->value(),guiAuditTransport(auditTransport_));std::string tls;try{tls=PbxAudit::tlsAudit(host,5061,3500);}catch(const std::exception&e){tls=std::string("TLS probe unavailable: ")+e.what();}lastAuditReport_=PbxAudit::report("REAL-WORLD PBX ATTACK-SCENARIO SIMULATION",rs,{},tls);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX attack scenario",e.what());}}
void MainWindow::runAuditTls(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");const auto tls=PbxAudit::tlsAudit(host,5061);lastAuditReport_=PbxAudit::report("PBX TLS AUDIT",{}, {},tls);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX TLS audit",e.what());}}
void MainWindow::runAuditFull(){try{const auto host=auditHost_->text().trimmed().toStdString();if(host.empty())throw std::runtime_error("Target is required");const auto t=guiAuditTransport(auditTransport_);auto user=auditUser_->text().trimmed().toStdString();if(user.empty())user=engine_.profile().username;auto rs=PbxAudit::attackScenarioAudit(host,user,(std::uint16_t)auditPort_->value(),t);const auto alt=t==AuditTransport::Udp?AuditTransport::Tcp:AuditTransport::Udp;rs.push_back(PbxAudit::serviceProbe(host,(std::uint16_t)auditPort_->value(),alt));auto more=PbxAudit::complianceAudit(host,(std::uint16_t)auditPort_->value(),t);rs.insert(rs.end(),more.begin(),more.end());std::string tls;try{tls=PbxAudit::tlsAudit(host,5061,3500);}catch(const std::exception&e){tls=std::string("TLS probe unavailable: ")+e.what();}lastAuditReport_=PbxAudit::report("FULL PBX ENGINEERING AUDIT",rs,{},tls);auditOutput_->setPlainText(QString::fromStdString(lastAuditReport_));}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}
void MainWindow::saveAuditReport(){if(lastAuditReport_.empty()){QMessageBox::information(this,"PBX audit","Run an audit first.");return;}auto path=QFileDialog::getSaveFileName(this,"Save PBX audit report","sipher-pbx-audit.txt","Text reports (*.txt);;All files (*)");if(path.isEmpty())return;try{PbxAudit::saveReport(path.toStdString(),lastAuditReport_);statusBar()->showMessage("PBX audit report saved",5000);}catch(const std::exception&e){QMessageBox::warning(this,"PBX audit",e.what());}}

void MainWindow::editProfile(){
    try{
        for(const auto& c:engine_.calls()){
            if(!c.disconnected){QMessageBox::warning(this,"SIP Profile","Hang up active calls before changing the SIP profile.");return;}
        }
        multi_.cancelLaunching();
        SipProfile oldProfile=engine_.profile();
        SipProfile edited=ProfileStore::loadDraft(profilePath_);
        if(!editSipProfileDialog(this,edited,QString::fromStdString(profilePath_),false))return;
        ProfileStore::save(edited,profilePath_);
        try{
            engine_.stop();
            engine_.start(ProfileStore::load(profilePath_),50);
            statusBar()->showMessage("SIP profile saved and reloaded",5000);
        }catch(...){
            ProfileStore::save(oldProfile,profilePath_);
            try{engine_.stop();engine_.start(oldProfile,50);}catch(...){}
            throw;
        }
    }catch(const pj::Error&e){QMessageBox::critical(this,"SIP profile reload failed",QString::fromStdString(e.info()));}
     catch(const std::exception&e){QMessageBox::critical(this,"SIP profile reload failed",e.what());}
}
void MainWindow::applyTheme(const QString&t){
    const QString themeId=t.toCaseFolded();
    const QString settingsPath=QString::fromStdString(trunkmonkey::runtime::settingsPath().string());
    QSettings settings(settingsPath,QSettings::IniFormat);
    settings.setValue(QStringLiteral("ui/theme"),themeId);

    QString sheet;
    if(themeId==QStringLiteral("hacker")){
        sheet=QStringLiteral(
            "QWidget { background-color: #020402; color: #39ff14; font-family: 'Monospace'; }"
            "QMainWindow,QDialog { background-color: #020402; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background-color: #000000; color: #55ff33; border: 1px solid #168a0f; "
            " selection-background-color: #124d0d; selection-color: #b7ff9f; }"
            "QLineEdit:focus,QPlainTextEdit:focus,QTextEdit:focus,QListWidget:focus,QTreeWidget:focus,QTableWidget:focus,QComboBox:focus { "
            " border: 1px solid #39ff14; }"
            "QPushButton { background-color: #071007; color: #39ff14; border: 1px solid #1dbb13; "
            " padding: 5px 10px; min-height: 18px; }"
            "QPushButton:hover { background-color: #0d250b; border-color: #55ff33; }"
            "QPushButton:pressed { background-color: #12360e; }"
            "QPushButton:disabled { color: #286326; border-color: #173c17; background-color: #050905; }"
            "QMenuBar,QMenu,QStatusBar,QTabBar::tab { background-color: #040904; color: #39ff14; }"
            "QMenu::item:selected,QMenuBar::item:selected,QTabBar::tab:selected { background-color: #12360e; }"
            "QHeaderView::section { background-color: #071207; color: #55ff33; border: 1px solid #168a0f; padding: 4px; }"
            "QGroupBox { border: 1px solid #168a0f; margin-top: 8px; padding-top: 6px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #55ff33; }"
            "QToolTip { background-color: #000000; color: #55ff33; border: 1px solid #39ff14; }"
            "QScrollBar:vertical { background: #020602; width: 12px; margin: 0; }"
            "QScrollBar::handle:vertical { background: #176510; min-height: 24px; border: 1px solid #39ff14; }"
            "QScrollBar::handle:vertical:hover { background: #209116; }"
            "QScrollBar:add-line:vertical,QScrollBar:sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal { background: #020602; height: 12px; margin: 0; }"
            "QScrollBar::handle:horizontal { background: #176510; min-width: 24px; border: 1px solid #39ff14; }"
            "QScrollBar:add-line:horizontal,QScrollBar:sub-line:horizontal { width: 0; }"
            "QSlider::groove:horizontal { height: 6px; background: #0b2609; border: 1px solid #176510; }"
            "QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: #39ff14; border: 1px solid #88ff70; }"
            "QSplitter::handle { background: #168a0f; }"
            "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #39ff14; background: #000000; }"
            "QCheckBox::indicator:checked { background: #39ff14; }");
    }else if(themeId==QStringLiteral("matrix")){
        sheet=QStringLiteral(
            "QWidget { background-color: #000000; color: #00ff41; font-family: 'Monospace'; }"
            "QMainWindow,QDialog { background-color: #000000; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background-color: #000000; color: #00ff41; border: 1px solid #008f11; "
            " selection-background-color: #003b0a; selection-color: #b6ffbf; }"
            "QLineEdit:focus,QPlainTextEdit:focus,QTextEdit:focus,QListWidget:focus,QTreeWidget:focus,QTableWidget:focus,QComboBox:focus { border: 1px solid #00ff41; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background-color: #001600; color: #00ff41; }"
            "QPushButton { border: 1px solid #008f11; padding: 5px 9px; }"
            "QPushButton:hover { background-color: #003b0a; border-color: #00ff41; }"
            "QPushButton:pressed { background-color: #005b12; }"
            "QTabBar::tab:selected { background-color: #003b0a; }"
            "QHeaderView::section { background-color: #001d05; color: #00ff41; border: 1px solid #008f11; padding: 4px; }"
            "QGroupBox { border: 1px solid #008f11; margin-top: 8px; padding-top: 6px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #00ff41; }"
            "QScrollBar:vertical { background: #000000; width: 12px; }"
            "QScrollBar::handle:vertical { background: #006b10; min-height: 24px; border: 1px solid #00ff41; }"
            "QScrollBar:horizontal { background: #000000; height: 12px; }"
            "QScrollBar::handle:horizontal { background: #006b10; min-width: 24px; border: 1px solid #00ff41; }"
            "QScrollBar:add-line,QScrollBar:sub-line { width: 0; height: 0; }"
            "QSlider::groove:horizontal { height: 6px; background: #003b0a; border: 1px solid #008f11; }"
            "QSlider::handle:horizontal { width: 14px; margin: -5px 0; background: #00ff41; border: 1px solid #b6ffbf; }"
            "QSplitter::handle { background: #008f11; }");
    }else if(themeId==QStringLiteral("phosphor")){
        sheet=QStringLiteral(
            "QWidget { background: #071007; color: #75ff83; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background: #020702; color: #89ff95; border: 1px solid #2b7a34; selection-background-color: #215f29; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background: #0b180c; color: #89ff95; }"
            "QPushButton { border: 1px solid #2b7a34; padding: 4px 8px; }"
            "QPushButton:hover,QTabBar::tab:selected { background: #17391b; }"
            "QHeaderView::section { background: #102512; color: #89ff95; border: 1px solid #2b7a34; }"
            "QGroupBox { border: 1px solid #2b7a34; margin-top: 8px; }"
            "QScrollBar::handle { background: #2b7a34; }"
            "QSplitter::handle { background: #2b7a34; }");
    }else if(themeId==QStringLiteral("midnight")){
        sheet=QStringLiteral(
            "QWidget { background-color: #11141b; color: #e1e7f0; }"
            "QMainWindow,QDialog { background-color: #11141b; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background-color: #0a0d12; color: #e7edf7; border: 1px solid #394150; "
            " selection-background-color: #31476d; selection-color: #ffffff; }"
            "QPushButton { background-color: #202631; color: #eef3fb; border: 1px solid #4b5668; padding: 5px 9px; }"
            "QPushButton:hover { background-color: #2a3342; border-color: #72809a; }"
            "QMenuBar,QMenu,QStatusBar,QTabBar::tab { background-color: #171c25; color: #e7edf7; }"
            "QMenu::item:selected,QMenuBar::item:selected,QTabBar::tab:selected { background-color: #2a3b5c; }"
            "QHeaderView::section { background-color: #202631; color: #eef3fb; border: 1px solid #3f4958; padding: 4px; }"
            "QGroupBox { border: 1px solid #3f4958; margin-top: 8px; }"
            "QScrollBar::handle { background: #4b5668; }"
            "QSplitter::handle { background: #3f4958; }");
    }else if(themeId==QStringLiteral("amber")){
        sheet=QStringLiteral(
            "QWidget { background: #130d03; color: #ffbf47; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background: #070401; color: #ffc85c; border: 1px solid #8f5e15; selection-background-color: #6a4510; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background: #211604; color: #ffc85c; }"
            "QPushButton { border: 1px solid #8f5e15; padding: 4px 8px; }"
            "QPushButton:hover,QTabBar::tab:selected { background: #3a2709; }"
            "QHeaderView::section { background: #2a1c06; color: #ffc85c; border: 1px solid #8f5e15; }"
            "QGroupBox { border: 1px solid #8f5e15; margin-top: 8px; }"
            "QScrollBar::handle { background: #8f5e15; }"
            "QSplitter::handle { background: #8f5e15; }");
    }else if(themeId==QStringLiteral("ice")){
        sheet=QStringLiteral(
            "QWidget { background: #071018; color: #bde8ff; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background: #02070b; color: #c9edff; border: 1px solid #2e6d91; selection-background-color: #1f526f; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background: #0c1b25; color: #c9edff; }"
            "QPushButton { border: 1px solid #2e6d91; padding: 4px 8px; }"
            "QPushButton:hover,QTabBar::tab:selected { background: #153447; }"
            "QHeaderView::section { background: #102b3a; color: #c9edff; border: 1px solid #2e6d91; }"
            "QGroupBox { border: 1px solid #2e6d91; margin-top: 8px; }"
            "QScrollBar::handle { background: #2e6d91; }"
            "QSplitter::handle { background: #2e6d91; }");
    }else if(themeId==QStringLiteral("solarized")){
        sheet=QStringLiteral(
            "QWidget { background:#002b36; color:#839496; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#073642; color:#eee8d5; border:1px solid #586e75; selection-background-color:#0b4f5c; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#073642; color:#93a1a1; }"
            "QPushButton { border:1px solid #657b83; padding:4px 8px; }"
            "QPushButton:hover,QTabBar::tab:selected { background:#0b4f5c; color:#fdf6e3; }"
            "QHeaderView::section { background:#073642; color:#b58900; border:1px solid #586e75; }"
            "QGroupBox { border:1px solid #586e75; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#586e75; }");
    }else if(themeId==QStringLiteral("dracula")){
        sheet=QStringLiteral(
            "QWidget { background:#282a36; color:#f8f8f2; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#1e1f29; color:#f8f8f2; border:1px solid #6272a4; selection-background-color:#44475a; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#343746; color:#f8f8f2; }"
            "QPushButton { border:1px solid #bd93f9; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#44475a; color:#50fa7b; }"
            "QHeaderView::section { background:#343746; color:#ff79c6; border:1px solid #6272a4; }"
            "QGroupBox { border:1px solid #6272a4; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#6272a4; }");
    }else if(themeId==QStringLiteral("nord")){
        sheet=QStringLiteral(
            "QWidget { background:#2e3440; color:#eceff4; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#3b4252; color:#eceff4; border:1px solid #4c566a; selection-background-color:#5e81ac; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#3b4252; color:#d8dee9; }"
            "QPushButton { border:1px solid #81a1c1; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#434c5e; color:#88c0d0; }"
            "QHeaderView::section { background:#434c5e; color:#8fbcbb; border:1px solid #4c566a; }"
            "QGroupBox { border:1px solid #4c566a; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#5e81ac; }");
    }else if(themeId==QStringLiteral("cyberpunk")){
        sheet=QStringLiteral(
            "QWidget { background:#090014; color:#f8f8ff; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#120022; color:#00f5ff; border:1px solid #ff00cc; selection-background-color:#4b006e; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#1b0033; color:#00f5ff; }"
            "QPushButton { border:1px solid #ff00cc; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#39005e; color:#f7ff00; }"
            "QHeaderView::section { background:#21003d; color:#ff00cc; border:1px solid #00f5ff; }"
            "QGroupBox { border:1px solid #ff00cc; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#00a8b5; }");
    }else if(themeId==QStringLiteral("blood-moon")){
        sheet=QStringLiteral(
            "QWidget { background:#160708; color:#f3dddd; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#090303; color:#ffd7d7; border:1px solid #7d1d25; selection-background-color:#5a1018; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#260b0e; color:#ffd7d7; }"
            "QPushButton { border:1px solid #a62a35; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#481117; color:#ff9a78; }"
            "QHeaderView::section { background:#351014; color:#ff6b63; border:1px solid #7d1d25; }"
            "QGroupBox { border:1px solid #7d1d25; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#7d1d25; }");
    }else if(themeId==QStringLiteral("ocean")){
        sheet=QStringLiteral(
            "QWidget { background:#061923; color:#d7f3ff; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#031018; color:#d7f3ff; border:1px solid #19799b; selection-background-color:#11516b; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#0a2633; color:#c8f3ff; }"
            "QPushButton { border:1px solid #2596be; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#10465d; color:#80f0ff; }"
            "QHeaderView::section { background:#0c3242; color:#62d9ff; border:1px solid #19799b; }"
            "QGroupBox { border:1px solid #19799b; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#19799b; }");
    }else if(themeId==QStringLiteral("retro-blue")){
        sheet=QStringLiteral(
            "QWidget { background:#07112a; color:#b8d5ff; font-family:'Monospace'; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#020817; color:#c8e0ff; border:1px solid #365f9c; selection-background-color:#173c73; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#0d1d42; color:#c8e0ff; }"
            "QPushButton { border:1px solid #4d7fc4; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#193a72; color:#ffffff; }"
            "QHeaderView::section { background:#102858; color:#8fc5ff; border:1px solid #365f9c; }"
            "QGroupBox { border:1px solid #365f9c; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#365f9c; }");
    }else if(themeId==QStringLiteral("blue-box")){
        sheet=QStringLiteral("QWidget{background:#031325;color:#bde7ff;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#010812;color:#d8f2ff;border:1px solid #1b77b7;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#08213a;color:#c7ecff;} QPushButton{border:1px solid #249ee8;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#0d3f67;} QHeaderView::section{background:#0a2c4d;color:#77d5ff;border:1px solid #1b77b7;} QGroupBox{border:1px solid #1b77b7;margin-top:8px;}");
    }else if(themeId==QStringLiteral("red-box")){
        sheet=QStringLiteral("QWidget{background:#180404;color:#ffd6d6;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#080101;color:#ffe9e9;border:1px solid #aa2525;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#290808;color:#ffdede;} QPushButton{border:1px solid #dc3f3f;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#541010;} QHeaderView::section{background:#3a0b0b;color:#ff7e7e;border:1px solid #aa2525;} QGroupBox{border:1px solid #aa2525;margin-top:8px;}");
    }else if(themeId==QStringLiteral("beige-box")){
        sheet=QStringLiteral("QWidget{background:#2a2419;color:#f1dfbd;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#15110c;color:#f7e9cc;border:1px solid #8d7651;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#3a3021;color:#f1dfbd;} QPushButton{border:1px solid #b59a68;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#58492f;} QHeaderView::section{background:#463a27;color:#f0cf8f;border:1px solid #8d7651;} QGroupBox{border:1px solid #8d7651;margin-top:8px;}");
    }else if(themeId==QStringLiteral("2600")){
        sheet=QStringLiteral("QWidget{background:#050505;color:#d7ffd7;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#000;color:#8cff8c;border:1px solid #19c719;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#071707;color:#76ff76;} QPushButton{border:1px solid #21ed21;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#0d350d;color:#fff000;} QHeaderView::section{background:#0a280a;color:#32ff32;border:1px solid #19c719;} QGroupBox{border:1px solid #19c719;margin-top:8px;}");
    }else if(themeId==QStringLiteral("wargames")){
        sheet=QStringLiteral("QWidget{background:#020b02;color:#55ff55;font-family:'Monospace';} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#000400;color:#55ff55;border:1px solid #138f13;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#061306;color:#55ff55;} QPushButton{border:1px solid #1dc51d;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#0a2a0a;} QHeaderView::section{background:#071d07;color:#8cff8c;border:1px solid #138f13;} QGroupBox{border:1px solid #138f13;margin-top:8px;}");
    }else if(themeId==QStringLiteral("crt-green")){
        sheet=QStringLiteral("QWidget{background:#071008;color:#b8ffb8;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#010501;color:#cbffcb;border:1px solid #3b8f45;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#0c1c0e;color:#c7ffc7;} QPushButton{border:1px solid #55b85f;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#16361a;} QHeaderView::section{background:#102913;color:#8eff98;border:1px solid #3b8f45;} QGroupBox{border:1px solid #3b8f45;margin-top:8px;}");
    }else if(themeId==QStringLiteral("vt220")){
        sheet=QStringLiteral("QWidget{background:#161616;color:#e8e8e8;font-family:'Monospace';} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#050505;color:#f2f2f2;border:1px solid #767676;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#242424;color:#eee;} QPushButton{border:1px solid #919191;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#393939;} QHeaderView::section{background:#2d2d2d;color:#fff;border:1px solid #767676;} QGroupBox{border:1px solid #767676;margin-top:8px;}");
    }else if(themeId==QStringLiteral("cobalt")){
        sheet=QStringLiteral("QWidget{background:#07152b;color:#e5efff;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#020916;color:#e5efff;border:1px solid #456fb5;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#0d2446;color:#dce9ff;} QPushButton{border:1px solid #668fd0;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#173d75;color:#9ee7ff;} QHeaderView::section{background:#102f5c;color:#8cc7ff;border:1px solid #456fb5;} QGroupBox{border:1px solid #456fb5;margin-top:8px;}");
    }else if(themeId==QStringLiteral("vaporwave")){
        sheet=QStringLiteral("QWidget{background:#14051f;color:#ffe9ff;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#07020c;color:#7ff6ff;border:1px solid #d554ff;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#241035;color:#8ff7ff;} QPushButton{border:1px solid #ff69da;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#47205d;color:#fff28a;} QHeaderView::section{background:#32164a;color:#ff83dc;border:1px solid #7cefff;} QGroupBox{border:1px solid #d554ff;margin-top:8px;}");
    }else if(themeId==QStringLiteral("stealth")){
        sheet=QStringLiteral("QWidget{background:#101214;color:#c7cbd0;} QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QSpinBox{background:#070809;color:#d5d9de;border:1px solid #4c535a;} QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab{background:#1a1d20;color:#cbd0d5;} QPushButton{border:1px solid #5b636b;padding:4px 8px;} QPushButton:hover,QTabBar::tab:selected{background:#292e33;color:#e8ecef;} QHeaderView::section{background:#22272b;color:#d5d9de;border:1px solid #4c535a;} QGroupBox{border:1px solid #4c535a;margin-top:8px;}");
    }else if(themeId==QStringLiteral("monochrome")){
        sheet=QStringLiteral(
            "QWidget { background:#111111; color:#eeeeee; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { background:#050505; color:#f5f5f5; border:1px solid #777777; selection-background-color:#444444; }"
            "QPushButton,QMenuBar,QMenu,QStatusBar,QTabBar::tab { background:#222222; color:#eeeeee; }"
            "QPushButton { border:1px solid #888888; padding:4px 8px; } QTabBar::tab:selected,QPushButton:hover { background:#3a3a3a; color:#ffffff; }"
            "QHeaderView::section { background:#292929; color:#ffffff; border:1px solid #777777; }"
            "QGroupBox { border:1px solid #777777; margin-top:8px; } QScrollBar::handle,QSplitter::handle { background:#777777; }");
    }else if(themeId==QStringLiteral("classic-light")){
        sheet=QStringLiteral(
            "QWidget { background-color: #f2f2f2; color: #202020; }"
            "QMainWindow,QDialog { background-color: #f2f2f2; }"
            "QLineEdit,QPlainTextEdit,QTextEdit,QListWidget,QTreeWidget,QTableWidget,QComboBox,QSpinBox { "
            " background-color: #ffffff; color: #202020; border: 1px solid #9a9a9a; "
            " selection-background-color: #2f6fa7; selection-color: #ffffff; }"
            "QPushButton { background-color: #e5e5e5; color: #202020; border: 1px solid #8b8b8b; padding: 5px 9px; }"
            "QPushButton:hover { background-color: #d8e6f3; }"
            "QMenuBar,QMenu,QStatusBar,QTabBar::tab { background-color: #e8e8e8; color: #202020; }"
            "QMenu::item:selected,QMenuBar::item:selected,QTabBar::tab:selected { background-color: #d2e5f5; color: #101010; }"
            "QHeaderView::section { background-color: #dddddd; color: #202020; border: 1px solid #a0a0a0; padding: 4px; }"
            "QGroupBox { border: 1px solid #a0a0a0; margin-top: 8px; }"
            "QScrollBar::handle { background: #b1b1b1; }"
            "QSplitter::handle { background: #a0a0a0; }");
    }
    qApp->setStyleSheet(sheet);
}
