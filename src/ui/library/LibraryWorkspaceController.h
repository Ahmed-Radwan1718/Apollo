#pragma once

#include "imaging/PhotoMetadata.h"

#include <QHash>
#include <QObject>

class BasicPanel;
class FilmstripWidget;
class HistogramWidget;
class HistoryPanel;
class LibraryGridWidget;
class LibraryImportService;
class LibrarySidebar;
class MainWindow;
class NavigatorPanel;
class PhotoCanvas;
class PhotoMetadataWidget;
class WorkspaceModeBar;

class LibraryWorkspaceController final : public QObject
{
public:
    LibraryWorkspaceController(MainWindow *window,
                               WorkspaceModeBar *modeBar,
                               QObject *parent = nullptr);

private:
    void setLibraryMode(bool libraryMode);

    MainWindow *m_window = nullptr;
    WorkspaceModeBar *m_modeBar = nullptr;
    NavigatorPanel *m_navigator = nullptr;
    HistoryPanel *m_history = nullptr;
    PhotoCanvas *m_canvas = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    HistogramWidget *m_histogram = nullptr;
    PhotoMetadataWidget *m_photoMetadata = nullptr;
    BasicPanel *m_basicPanel = nullptr;
    LibrarySidebar *m_sidebar = nullptr;
    LibraryGridWidget *m_grid = nullptr;
    LibraryImportService *m_importService = nullptr;
    QHash<QString, imaging::PhotoMetadata> m_libraryMetadata;
    bool m_libraryMode = false;
};
