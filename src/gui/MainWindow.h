#pragma once
#include <QMainWindow>
#include <cstddef>
#include <string>
namespace trunkmonkey {class Logger;class MultiCallManager;class SipEngine;}
class QLabel;class QLineEdit;class QSpinBox;class QTableWidget;class QTimer;class QComboBox;class QPlainTextEdit;class QPushButton;class QTabWidget;
class MainWindow final:public QMainWindow {
    Q_OBJECT
public:
    MainWindow(trunkmonkey::SipEngine& engine,trunkmonkey::MultiCallManager& multi,trunkmonkey::Logger& logger,std::string profilePath,QWidget* parent=nullptr);
private slots:
    void refresh();void refreshDiagnostics();void showRawSip();
    void dial();void answerSelected();void hangupSelected();void foregroundSelected();void holdSelected();void resumeSelected();void toggleMuteSelected();void showDtmfPad();
    void startSipTrace();void stopSipTrace();void startSipPcap();void startRtpPcap();void startCallPcap();void stopPcaps();void openLastPcap();
    void launchBatch();void loadDestinations();void loadCallerIds();void loadQueueAudio();void hangupAll();void editProfile();void showAudioDevices();void showRegistrationHistory();void applyTheme(const QString& theme);
    void showSipLadder();void exportCallReport();
    void runAuditFingerprint();void runAuditVulns();void runAuditProbe();void runAuditDiscover();void runAuditMethods();void runAuditAuth();void runAuditExtensions();void runAuditCompliance();void runAuditParser();void runAuditResilience();void runAuditScenario();void runAuditTls();void runAuditFull();void saveAuditReport();
private:
    int selectedCallId()const;void buildUi();void setDiagnosticsEnabled(bool enabled);void selectCallId(int id);
    trunkmonkey::SipEngine& engine_;trunkmonkey::MultiCallManager& multi_;trunkmonkey::Logger& logger_;
    QLabel* registration_{nullptr};QLineEdit* dialEdit_{nullptr};QLineEdit* callerIdEdit_{nullptr};QTableWidget* calls_{nullptr};
    QLabel* mediaTarget_{nullptr};QLabel* mediaSource_{nullptr};QLabel* mediaLocal_{nullptr};QLabel* mediaCodec_{nullptr};QLabel* mediaQuality_{nullptr};QLabel* callIdLabel_{nullptr};
    QLabel* diagnosticNote_{nullptr};QLabel* captureStatus_{nullptr};QComboBox* captureInterface_{nullptr};QTableWidget* sipLog_{nullptr};QPlainTextEdit* rawSip_{nullptr};
    QPushButton* sipTraceStart_{nullptr};QPushButton* sipTraceStop_{nullptr};QPushButton* sipPcapStart_{nullptr};QPushButton* rtpPcapStart_{nullptr};QPushButton* callPcapStart_{nullptr};QPushButton* pcapStop_{nullptr};QPushButton* muteButton_{nullptr};
    QSpinBox* batchCount_{nullptr};QSpinBox* launchInterval_{nullptr};QLineEdit* batchDestination_{nullptr};QLineEdit* fixedCallerId_{nullptr};
    QLabel* destinationFileLabel_{nullptr};QLabel* callerIdFileLabel_{nullptr};QLabel* queueAudioFileLabel_{nullptr};QComboBox* theme_{nullptr};QTimer* refreshTimer_{nullptr};QTabWidget* tabs_{nullptr};QLabel* profileSummary_{nullptr};QPlainTextEdit* activityLog_{nullptr};
    QLineEdit* auditHost_{nullptr};QLineEdit* auditUser_{nullptr};QSpinBox* auditPort_{nullptr};QSpinBox* auditExtFirst_{nullptr};QSpinBox* auditExtLast_{nullptr};QComboBox* auditTransport_{nullptr};QPlainTextEdit* auditOutput_{nullptr};
    QString destinationFile_,callerIdFile_,queueAudioFile_,lastPcapPath_;std::string lastAuditReport_;std::string profilePath_;int pendingSelectId_{-1};int displayedTraceCallId_{-1};int lastPcapCallId_{-1};std::size_t displayedTraceCount_{0};
};
