#include "app/ExportDialog.h"

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export"));
    setModal(true);
    resize(430, 420);
    setMinimumWidth(430);
    setObjectName(QStringLiteral("exportDialog"));

    setStyleSheet(QStringLiteral(
        "QDialog#exportDialog {"
        "  background: #181818;"
        "  color: #d6d6d6;"
        "  font-family: \"Segoe UI Variable\";"
        "  font-size: 12px;"
        "}"));
}

ExportDialog::Settings ExportDialog::settings() const
{
    return {};
}
