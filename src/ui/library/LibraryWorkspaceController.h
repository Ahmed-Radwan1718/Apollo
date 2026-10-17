#pragma once

#include <QObject>

class BasicPanel;
class FilmstripWidget;
class HistoryPanel;
class LibraryGridWidget;
class LibrarySidebar;
class MainWindow;
class NavigatorPanel;
class PhotoCanvas;
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
    BasicPanel *m_basicPanel = nullptr;
    LibrarySidebar *m_sidebar = nullptr;
    LibraryGridWidget *m_grid = nullptr;
    bool m_libraryMode = false;
};
