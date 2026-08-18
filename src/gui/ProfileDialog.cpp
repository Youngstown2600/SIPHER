#include "ProfileDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <limits>

using namespace trunkmonkey;

bool editSipProfileDialog(QWidget* parent,SipProfile& profile,const QString& profilePath,bool firstRun)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(firstRun?QStringLiteral("S.I.P.H.E.R. First-Run SIP Setup"):QStringLiteral("S.I.P.H.E.R. SIP Profile"));
    dialog.resize(560,540);
    auto* outer=new QVBoxLayout(&dialog);
    auto* note=new QLabel(QStringLiteral("Profile file: %1").arg(profilePath),&dialog);
    note->setWordWrap(true);
    outer->addWidget(note);
    if(firstRun){
        auto* intro=new QLabel(QStringLiteral("Enter the SIP account S.I.P.H.E.R. should register with. You can change these settings later from Settings → SIP Profile."),&dialog);
        intro->setWordWrap(true); outer->addWidget(intro);
    }
    auto* form=new QFormLayout;
    QLineEdit name(QString::fromStdString(profile.name));
    QLineEdit domain(QString::fromStdString(profile.sipDomain));
    QLineEdit registrar(QString::fromStdString(profile.registrar));
    QLineEdit username(QString::fromStdString(profile.username));
    QLineEdit authUsername(QString::fromStdString(profile.authUsername));
    QLineEdit password(QString::fromStdString(profile.password)); password.setEchoMode(QLineEdit::Password);
    QLineEdit displayName(QString::fromStdString(profile.displayName));
    QLineEdit proxy(QString::fromStdString(profile.outboundProxy));
    QLineEdit callerDomain(QString::fromStdString(profile.callerIdDomain));
    QComboBox transport; transport.addItems({"UDP","TCP","TLS"}); transport.setCurrentText(QString::fromStdString(toString(profile.transport)).toUpper());
    QSpinBox port; port.setRange(1,65535); port.setValue(profile.localSipPort);
    QSpinBox expires; expires.setRange(1,std::numeric_limits<int>::max()); expires.setValue(static_cast<int>(std::min(profile.registrationExpires,static_cast<unsigned>(std::numeric_limits<int>::max()))));
    QComboBox identity; identity.addItem("From","from"); identity.addItem("P-Asserted-Identity","pai"); identity.addItem("Remote-Party-ID","rpid"); identity.addItem("From + PAI","from+pai");
    int identityIndex=identity.findData(QString::fromStdString(toString(profile.identityMode))); if(identityIndex>=0) identity.setCurrentIndex(identityIndex);
    QLineEdit stun(QString::fromStdString(profile.stunServer));
    QCheckBox ice; ice.setChecked(profile.useIce);
    QCheckBox srtp; srtp.setChecked(profile.enableSrtp);
    form->addRow("Profile name",&name); form->addRow("SIP domain",&domain); form->addRow("Registrar",&registrar);
    form->addRow("Username",&username); form->addRow("Auth username",&authUsername); form->addRow("Password",&password);
    form->addRow("Display name",&displayName); form->addRow("Outbound proxy",&proxy); form->addRow("Caller-ID domain",&callerDomain);
    form->addRow("Transport",&transport); form->addRow("Local SIP port",&port); form->addRow("Registration expires",&expires);
    form->addRow("Identity mode",&identity); form->addRow("STUN server",&stun); form->addRow("Use ICE",&ice); form->addRow("Enable SRTP",&srtp);
    outer->addLayout(form);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); outer->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        SipProfile candidate=profile;
        candidate.name=name.text().trimmed().toStdString();
        candidate.sipDomain=domain.text().trimmed().toStdString();
        candidate.registrar=registrar.text().trimmed().toStdString();
        candidate.username=username.text().trimmed().toStdString();
        candidate.authUsername=authUsername.text().trimmed().toStdString();
        candidate.password=password.text().toStdString();
        candidate.displayName=displayName.text().trimmed().toStdString();
        candidate.outboundProxy=proxy.text().trimmed().toStdString();
        candidate.callerIdDomain=callerDomain.text().trimmed().toStdString();
        candidate.transport=transportFromString(transport.currentText().toStdString());
        candidate.localSipPort=static_cast<std::uint16_t>(port.value());
        candidate.registrationExpires=static_cast<unsigned>(expires.value());
        candidate.identityMode=identityModeFromString(identity.currentData().toString().toStdString());
        candidate.stunServer=stun.text().trimmed().toStdString();
        candidate.useIce=ice.isChecked(); candidate.enableSrtp=srtp.isChecked();
        try{ ProfileStore::validate(candidate); }
        catch(const std::exception& e){ QMessageBox::warning(&dialog,"Invalid SIP profile",e.what()); return; }
        profile=std::move(candidate); dialog.accept();
    });
    return dialog.exec()==QDialog::Accepted;
}
