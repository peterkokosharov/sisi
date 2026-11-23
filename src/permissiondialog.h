// Copyright (c) 2026 Peter Kokosharov <peter.kokosharov@gmail.com>
// LLM: Claude Sonnet 5.5 (claude-sonnet-5-5) - permission dialog
// Licensed under MIT License.
//
// Modal question "may rex run this?": Allow once / Allow always / Deny.
// Deny is the default button; Esc and closing the window also mean Deny.
#ifndef SISI_PERMISSIONDIALOG_H
#define SISI_PERMISSIONDIALOG_H

#include <QDialog>

#include "core.h"

class PermissionDialog : public QDialog {
    Q_OBJECT
public:
    PermissionDialog(const PermissionReq &req, QWidget *parent);

    PermDecision decision() const { return decision_; }
    // Closes the dialog as Deny (rex exited or Stop was pressed).
    void forceDeny();

private:
    PermDecision decision_ = PermDecision::Deny;
};

#endif
