// https://x.com/i/grok?conversation=2012291027764207884

#include "app.h"
#include "frame.h"
#include "parser.h"
#include <wx/filename.h>
#include <sqlite3.h>

wxIMPLEMENT_APP(MyApp);

bool MyApp::OnInit() {
    // Apri o crea il database
    if (sqlite3_open("cot.db", &db) != SQLITE_OK) {
        wxMessageBox("Errore nell'apertura del database", "Errore", wxOK | wxICON_ERROR);
        return false;
    }

    // Crea la tabella se non esiste
    const char* createTableSQL = 
        "CREATE TABLE IF NOT EXISTS stagioni ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "puntata TEXT,"
        "numero TEXT,"
        "venditore TEXT,"
        "tipo TEXT,"
        "informazioni TEXT,"
        "oggetto TEXT,"
        "storia TEXT,"
        "valutazione INTEGER,"
        "offerta INTEGER,"
        "esito TEXT,"
        "rilancio INTEGER,"
        "esito_rilancio TEXT,"
        "compratore TEXT,"
        "scostamento INTEGER"
        ");";
    char* errMsg = nullptr;
    if (sqlite3_exec(db, createTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        wxMessageBox(wxString("Errore nella creazione della tabella: ") + errMsg, "Errore", wxOK | wxICON_ERROR);
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return false;
    }

    // Controlla se la tabella è vuota
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM stagioni;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count == 0) {
                // Importa dai fogli Excel
                if (!ImportFromExcel(db, "cot.xlsx", {"Serie1","Serie6","Serie7"})) {
                    wxMessageBox("Errore nell'importazione dai file Excel", "Errore", wxOK | wxICON_ERROR);
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    return false;
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    MyFrame* frame = new MyFrame("Gestione Stagioni", wxPoint(150, 50), wxSize(1200, 800));
    frame->Show(true);
    return true;
}
