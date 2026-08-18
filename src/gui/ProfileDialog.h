#pragma once
#include "trunkmonkey/Profile.h"
#include <QString>
class QWidget;

bool editSipProfileDialog(QWidget* parent, trunkmonkey::SipProfile& profile,
                          const QString& profilePath, bool firstRun=false);
