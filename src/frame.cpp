// src/frame.cpp
#include "frame.h"
#include "app.h"
#include <wx/sizer.h>
#include <wx/timer.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <sqlite3.h>

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size)
{
    wxPanel* mainContainer = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    // ────────────────────────────────────────────────
    // Pannello con header filtrabile + listctrl sincronizzati
    // ────────────────────────────────────────────────
    wxPanel* contentPanel = new wxPanel(mainContainer);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);

    // ── Header scrolled (solo orizzontale) ──────────────────────────────
    headerScroll = new wxScrolledWindow(contentPanel, wxID_ANY,
                                        wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxBORDER_SIMPLE);
    headerScroll->SetScrollRate(15, 0);  // solo orizzontale

    wxPanel* headerPanel = new wxPanel(headerScroll);
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);

    // Colonna ID (fissa, non filtrabile)
    wxStaticText* idHeader = new wxStaticText(headerPanel, wxID_ANY, "ID");
    idHeader->SetMinSize(wxSize(60, -1));
    headerSizer->Add(idHeader, 0, wxEXPAND | wxALL, 4);

    // Campi di ricerca
    searchFields.clear();

    std::vector<wxString> colNames = {"Puntata", "Numero", "Venditore", "Tipo", "Informazioni", "Oggetto", 
		"Storia", "Valutazione",  "Offerta", "Esito", "Rilancio", "Esito Rilancio", "Compratore", "Scostamento"
    };

    //std::vector<int> colWidths = {100, 80, 140, 100, 140, 220, 280, 100, 100, 110, 100, 130, 140, 90};
    std::vector<int> colWidths = {80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80, 80};

    for (size_t i = 0; i < colNames.size(); ++i)  {
        wxTextCtrl* txt = new wxTextCtrl(headerPanel, wxID_ANY, "",
                                         wxDefaultPosition, wxSize(colWidths[i], -1), wxTE_PROCESS_ENTER);
        txt->SetHint(colNames[i]);
        searchFields.push_back(txt);

        txt->Bind(wxEVT_TEXT,       &MyFrame::OnFilterText, this);
        txt->Bind(wxEVT_TEXT_ENTER, &MyFrame::OnFilterText, this);

        headerSizer->Add(txt, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    }

    headerPanel->SetSizer(headerSizer);
    wxBoxSizer* headerScrollSizer = new wxBoxSizer(wxVERTICAL);
    headerScrollSizer->Add(headerPanel, 0, wxEXPAND);
    headerScroll->SetSizer(headerScrollSizer);

    // ── ListCtrl ────────────────────────────────────────────────────────
    listCtrl = new wxListCtrl(contentPanel, wxID_ANY,
                              wxDefaultPosition, wxDefaultSize,
                              wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_HRULES | wxLC_VRULES | wxHSCROLL);

    listCtrl->AppendColumn("ID", wxLIST_FORMAT_LEFT, 60);
    for (size_t i = 0; i < colNames.size(); ++i) {
        listCtrl->AppendColumn(colNames[i], wxLIST_FORMAT_LEFT, colWidths[i]);
    }

    listCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &MyFrame::OnItemActivated, this);

    // ── Sincronizzazione scroll orizzontale ─────────────────────────────
	// Sincronizzazione scroll orizzontale header → list
	headerScroll->Bind(wxEVT_SCROLLWIN_THUMBTRACK, &MyFrame::OnHeaderScroll, this);
	headerScroll->Bind(wxEVT_SCROLLWIN_PAGEDOWN,   &MyFrame::OnHeaderScroll, this);
	headerScroll->Bind(wxEVT_SCROLLWIN_PAGEUP,     &MyFrame::OnHeaderScroll, this);
	headerScroll->Bind(wxEVT_SCROLLWIN_LINEDOWN,   &MyFrame::OnHeaderScroll, this);
	headerScroll->Bind(wxEVT_SCROLLWIN_LINEUP,     &MyFrame::OnHeaderScroll, this);

	// Sincronizzazione inversa: list → header
	listCtrl->Bind(wxEVT_SCROLLWIN_THUMBTRACK, &MyFrame::OnListScroll, this);
	listCtrl->Bind(wxEVT_SCROLLWIN_PAGEDOWN,   &MyFrame::OnListScroll, this);
	listCtrl->Bind(wxEVT_SCROLLWIN_PAGEUP,     &MyFrame::OnListScroll, this);
	listCtrl->Bind(wxEVT_SCROLLWIN_LINEDOWN,   &MyFrame::OnListScroll, this);
	listCtrl->Bind(wxEVT_SCROLLWIN_LINEUP,     &MyFrame::OnListScroll, this);

    // Layout content
    contentSizer->Add(headerScroll, 0, wxEXPAND);
    contentSizer->Add(listCtrl, 1, wxEXPAND);
    contentPanel->SetSizer(contentSizer);

    mainSizer->Add(contentPanel, 1, wxEXPAND | wxALL, 6);
    mainContainer->SetSizer(mainSizer);

    // Timer per debounce ricerca
    m_filterTimer = new wxTimer(this);
    Bind(wxEVT_TIMER, &MyFrame::OnFilterTimer, this);

    // Carica tutti i dati inizialmente
    PopulateList("SELECT * FROM stagioni ORDER BY id;");
}

void MyFrame::OnFilterText(wxCommandEvent& WXUNUSED(event))
{
    m_filterTimer->Start(400, wxTIMER_ONE_SHOT);
}

void MyFrame::OnFilterTimer(wxTimerEvent& WXUNUSED(event))
{
    std::string query = "SELECT * FROM stagioni WHERE 1=1";
    std::vector<std::string> dbFields = {
        "puntata", "numero", "venditore", "tipo", "informazioni",
        "oggetto", "storia", "valutazione", "offerta", "esito",
        "rilancio", "esito_rilancio", "compratore", "scostamento"
    };

    bool hasFilter = false;
    for (size_t i = 0; i < searchFields.size(); ++i)
    {
        wxString val = searchFields[i]->GetValue().Trim();
        if (!val.empty())
        {
            hasFilter = true;
            query += " AND " + dbFields[i] + " LIKE '%" + val.ToStdString() + "%'";
        }
    }

    if (hasFilter)
        query += " ORDER BY id;";
    else
        query += " ORDER BY id;";

    PopulateList(query);
}

void MyFrame::PopulateList(const std::string& query)
{
    listCtrl->Freeze();
    listCtrl->DeleteAllItems();

    sqlite3* db = wxGetApp().GetDatabase();
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        wxMessageBox(wxString("Errore nella query: ") + sqlite3_errmsg(db), "Errore DB", wxOK | wxICON_ERROR);
        listCtrl->Thaw();
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        long idx = listCtrl->InsertItem(listCtrl->GetItemCount(),
                                        wxString::Format("%d", sqlite3_column_int(stmt, 0)));

        for (int c = 1; c <= 14; ++c)
        {
            const unsigned char* text = sqlite3_column_text(stmt, c);
            wxString cellText = text ? wxString::FromUTF8(reinterpret_cast<const char*>(text)) : wxString();
            listCtrl->SetItem(idx, c, cellText);
        }
    }

    sqlite3_finalize(stmt);
    listCtrl->Thaw();
}
/*
void MyFrame::OnItemActivated(wxListEvent& event)
{
    long item = event.GetIndex();
    if (item < 0) return;

    std::vector<wxString> fieldLabels = {
        "ID", "Puntata", "Numero", "Venditore", "Tipo", "Informazioni",
        "Oggetto", "Storia", "Valutazione", "Offerta", "Esito",
        "Rilancio", "Esito Rilancio", "Compratore", "Scostamento"
    };

    wxDialog dlg(this, wxID_ANY, "Dettagli puntata",
                 wxDefaultPosition, wxSize(720, 640),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxBoxSizer* dlgSizer = new wxBoxSizer(wxVERTICAL);

    wxPanel* contentPanel = new wxPanel(&dlg);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);

    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 10, 10);
    grid->AddGrowableCol(1);
    grid->SetFlexibleDirection(wxBOTH);
    grid->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    for (int col = 0; col < listCtrl->GetColumnCount(); ++col)
    {
        wxString labelStr = fieldLabels[col] + ":";
        wxString value = listCtrl->GetItemText(item, col);
        if (value.IsEmpty()) value = wxT("—");

        // Etichetta a sinistra (grassetto)
        wxStaticText* lbl = new wxStaticText(contentPanel, wxID_ANY, labelStr);
        lbl->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        grid->Add(lbl, 0, wxALIGN_RIGHT | wxALIGN_TOP | wxRIGHT | wxBOTTOM, 8);

        // Valore a destra → wxTextCtrl read-only
        wxTextCtrl* valCtrl = new wxTextCtrl(contentPanel, wxID_ANY, value,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH | wxTE_WORDWRAP);
        valCtrl->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        valCtrl->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        valCtrl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

if (col == 6 || col == 4) {  // Storia e Informazioni
    valCtrl->SetMinSize(wxSize(-1, 80));
}
if (col >= 7 && col <= 13) {  // Valutazione → Scostamento
    valCtrl->SetBackgroundColour(wxColour(240, 245, 255));  // azzurro chiaro
}
valCtrl->Bind(wxEVT_SET_FOCUS, [](wxFocusEvent& e) {
    wxTextCtrl* ctrl = wxDynamicCast(e.GetEventObject(), wxTextCtrl);
    if (ctrl) ctrl->SelectAll();
    e.Skip();
});
        grid->Add(valCtrl, 1, wxEXPAND | wxALIGN_TOP | wxLEFT | wxBOTTOM, 8);
    }

    contentSizer->Add(grid, 1, wxEXPAND | wxALL, 15);

    // Pulsante Chiudi
    wxButton* btnClose = new wxButton(contentPanel, wxID_CANCEL, "Chiudi");
    contentSizer->Add(btnClose, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 20);

    contentPanel->SetSizer(contentSizer);
    dlgSizer->Add(contentPanel, 1, wxEXPAND);
    dlg.SetSizer(dlgSizer);
    //dlg.Fit();
    dlg.CentreOnParent();

    dlg.ShowModal();
}*/
void MyFrame::OnItemActivated(wxListEvent& event)
{
    long item = event.GetIndex();
    if (item < 0) return;

    std::vector<wxString> fieldLabels = {
        "ID", "Puntata", "Numero", "Venditore", "Tipo", "Informazioni",
        "Oggetto", "Storia", "Valutazione", "Offerta", "Esito",
        "Rilancio", "Esito Rilancio", "Compratore", "Scostamento"
    };

    wxDialog dlg(this, wxID_ANY, "Dettagli puntata",
                 wxDefaultPosition, wxSize(750, 800),
                 wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxBoxSizer* dlgSizer = new wxBoxSizer(wxVERTICAL);

    wxPanel* contentPanel = new wxPanel(&dlg);
    wxBoxSizer* contentSizer = new wxBoxSizer(wxVERTICAL);

    // Ridotto spacing verticale tra righe: da 10,10 → 4,2
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 3, 1);
    grid->AddGrowableCol(1);
    grid->SetFlexibleDirection(wxBOTH);
    grid->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    for (int col = 0; col < listCtrl->GetColumnCount(); ++col)
    {
        wxString labelStr = fieldLabels[col] + ":";
        wxString value = listCtrl->GetItemText(item, col);
        if (value.IsEmpty()) value = wxT("—");

        // Etichetta
        wxStaticText* lbl = new wxStaticText(contentPanel, wxID_ANY, labelStr);
        lbl->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
        grid->Add(lbl, 0, wxALIGN_RIGHT | wxALIGN_TOP | wxRIGHT, 4);   // ridotto right da 12 a 8

        // Campo testo read-only
        wxTextCtrl* valCtrl;
        if(col==5 || col==6 || col==7) {
        valCtrl = new wxTextCtrl(contentPanel, wxID_ANY, value,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH | wxTE_WORDWRAP);
        } else {
			 valCtrl = new wxTextCtrl(contentPanel, wxID_ANY, value,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxTE_READONLY | wxTE_RICH);
		}
        valCtrl->SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        valCtrl->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        valCtrl->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));

        // Ridotto padding intorno al controllo
        grid->Add(valCtrl, 1, wxEXPAND | wxALIGN_TOP | wxLEFT | wxBOTTOM, 4);  // da 8 a 4
    }

    // Ridotto padding generale intorno alla griglia
    contentSizer->Add(grid, 1, wxEXPAND | wxALL, 5);   // da 15 a 10

    wxButton* btnClose = new wxButton(contentPanel, wxID_CANCEL, "Chiudi");
    contentSizer->Add(btnClose, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 6);  // da 20 a 12

    contentPanel->SetSizer(contentSizer);
    dlgSizer->Add(contentPanel, 1, wxEXPAND);
    dlg.SetSizer(dlgSizer);
    dlg.CentreOnParent();

    dlg.ShowModal();
}


void MyFrame::OnHeaderScroll(wxScrollWinEvent& event)
{
    int pos = event.GetPosition();
    listCtrl->ScrollList(pos - listCtrl->GetScrollPos(wxHORIZONTAL), 0);
    event.Skip();
}

void MyFrame::OnListScroll(wxScrollWinEvent& event)
{
    int pos = event.GetPosition();
    headerScroll->Scroll(pos, 0);
    event.Skip();
}
