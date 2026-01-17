#ifndef FRAME_H
#define FRAME_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <vector>

class MyFrame : public wxFrame {
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);

private:
    wxListCtrl* listCtrl;
    std::vector<wxTextCtrl*> searchFields;
    wxScrolledWindow* headerScroll;
    wxTimer* m_filterTimer;

    void OnFilterText(wxCommandEvent& event);
    void OnFilterTimer(wxTimerEvent& event);
    void PopulateList(const std::string& query);
    void OnItemActivated(wxListEvent& event);
    void OnHeaderScroll(wxScrollWinEvent& event);
    void OnListScroll(wxScrollWinEvent& event);
};

#endif // FRAME_H
