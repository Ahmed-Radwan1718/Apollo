#pragma once

#include "imaging/PhotoMetadata.h"

#include <QWidget>

class QLabel;

class PhotoMetadataWidget final : public QWidget
{
public:
    explicit PhotoMetadataWidget(QWidget *parent = nullptr);

    void setMetadata(const imaging::PhotoMetadata &metadata);
    void clear();

private:
    QLabel *m_shutterValue = nullptr;
    QLabel *m_apertureValue = nullptr;
    QLabel *m_isoValue = nullptr;
    QLabel *m_focalLengthValue = nullptr;
};
