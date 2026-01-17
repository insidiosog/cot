bool ImportFromExcel(sqlite3* db, const std::string& filename, const std::vector<std::string>& sheetNames) {
    wxFileSystem::AddHandler(new wxZipFSHandler);

    wxFileSystem fs;
    if (!fs.OpenFile(wxString(filename))) {
        std::cout << "ERRORE: Impossibile aprire il file zip: " << filename << "\n";
        return false;
    }
    std::cout << "File .xlsx aperto con successo: " << filename << "\n";

    // ────────────────────────────────────────────────
    // sharedStrings
    std::vector<wxString> sharedStrings;
    {
        wxFSFile* file = fs.OpenFile("zip://" + wxString(filename) + "#xl/sharedStrings.xml");
        if (file) {
            std::cout << "Trovato sharedStrings.xml → caricamento...\n";
            wxInputStream* stream = file->GetStream();
            wxXmlDocument doc(*stream);
            if (doc.IsOk()) {
                wxXmlNode* sst = doc.GetRoot();
                if (sst && sst->GetName() == "sst") {
                    size_t count = 0;
                    wxXmlNode* si = sst->GetChildren();
                    while (si) {
                        if (si->GetName() == "si") {
                            wxXmlNode* t = FindChildByName(si, "t");
                            if (t) {
                                sharedStrings.push_back(t->GetNodeContent());
                                count++;
                            }
                        }
                        si = si->GetNext();
                    }
                    std::cout << "Caricate " << count << " stringhe condivise\n";
                } else {
                    std::cout << "sharedStrings.xml presente ma root non è 'sst'\n";
                }
            } else {
                std::cout << "ERRORE: sharedStrings.xml non valido (wxXmlDocument non ok)\n";
            }
            delete file;
        } else {
            std::cout << "sharedStrings.xml NON trovato (file può essere inline o assente)\n";
        }
    }

    // ────────────────────────────────────────────────
    // workbook → nomi fogli
    std::map<std::string, std::string> sheetNameToId;
    {
        wxFSFile* file = fs.OpenFile("zip://" + wxString(filename) + "#xl/workbook.xml");
        if (file) {
            std::cout << "Trovato workbook.xml → lettura nomi fogli...\n";
            wxInputStream* stream = file->GetStream();
            wxXmlDocument doc(*stream);
            if (doc.IsOk()) {
                wxXmlNode* workbook = doc.GetRoot();
                wxXmlNode* sheets = FindChildByName(workbook, "sheets");
                if (sheets) {
                    size_t sheetCount = 0;
                    wxXmlNode* sheet = sheets->GetChildren();
                    while (sheet) {
                        if (sheet->GetName() == "sheet") {
                            wxString name = sheet->GetAttribute("name");
                            wxString sid  = sheet->GetAttribute("sheetId");
                            sheetNameToId[name.ToStdString()] = sid.ToStdString();
                            std::cout << "  Foglio trovato: '" << name << "' → sheetId=" << sid << "\n";
                            sheetCount++;
                        }
                        sheet = sheet->GetNext();
                    }
                    std::cout << "Trovati " << sheetCount << " fogli nel workbook\n";
                } else {
                    std::cout << "ERRORE: nodo 'sheets' non trovato in workbook.xml\n";
                }
            }
            delete file;
        } else {
            std::cout << "ERRORE: workbook.xml NON trovato!\n";
            return false;
        }
    }

    // Prepara insert
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cout << "ERRORE prepare INSERT: " << sqlite3_errmsg(db) << "\n";
        return false;
    }

    int totalInserted = 0;

    for (const auto& sh : sheetNames) {
        auto it = sheetNameToId.find(sh);
        if (it == sheetNameToId.end()) {
            std::cout << "Foglio richiesto NON trovato nel workbook: " << sh << "\n";
            continue;
        }

        std::string sheetPath = "xl/worksheets/sheet" + it->second + ".xml";
        std::cout << "Tentativo apertura foglio: " << sh << " (" << sheetPath << ")\n";

        wxFSFile* file = fs.OpenFile("zip://" + wxString(filename) + "#" + sheetPath);
        if (!file) {
            std::cout << "  → Foglio XML NON trovato: " << sheetPath << "\n";
            continue;
        }

        std::cout << "  → Foglio aperto con successo\n";

        wxInputStream* stream = file->GetStream();
        wxXmlDocument doc(*stream);
        if (!doc.IsOk()) {
            std::cout << "  → ERRORE: XML del foglio non valido\n";
            delete file;
            continue;
        }

        wxXmlNode* worksheet = doc.GetRoot();
        wxXmlNode* sheetData = FindChildByName(worksheet, "sheetData");
        if (!sheetData) {
            std::cout << "  → ERRORE: nodo <sheetData> non trovato nel foglio\n";
            delete file;
            continue;
        }

        int rowCount = 0;
        int insertedThisSheet = 0;

        wxXmlNode* row = sheetData->GetChildren();
        while (row) {
            if (row->GetName() == "row") {
                rowCount++;
                wxString rAttr = row->GetAttribute("r");
                long rnum = 0;
                rAttr.ToLong(&rnum);

                if (rnum <= 1) {
                    // std::cout << "  Riga " << rnum << " saltata (header)\n";
                    row = row->GetNext();
                    continue;
                }

                std::vector<wxString> cells(14, wxEmptyString);
                bool hasData = false;

                wxXmlNode* c = row->GetChildren();
                while (c) {
                    if (c->GetName() == "c") {
                        wxString ref = c->GetAttribute("r");
                        if (ref.length() >= 2 && wxIsalpha(ref[0])) {
                            int col = ref[0] - 'A';
                            if (col >= 0 && col < 14) {
                                wxString type = c->GetAttribute("t", "n");
                                wxXmlNode* vNode = FindChildByName(c, "v");
                                wxString val = vNode ? vNode->GetNodeContent() : wxString();

                                if (type == "s" && !val.empty()) {
                                    long idx = 0;
                                    if (val.ToLong(&idx) && idx >= 0 && static_cast<size_t>(idx) < sharedStrings.size()) {
                                        val = sharedStrings[idx];
                                    }
                                }

                                if (!val.empty()) hasData = true;
                                cells[col] = val;
                            }
                        }
                    }
                    c = c->GetNext();
                }

                if (hasData) {
                    // ─── Bind ───────────────────────────────────────
                    for (int i = 0; i < 14; ++i) {
                        const wxString& v = cells[i];
                        if (i == 7 || i == 8 || i == 10 || i == 13) { // int
                            int ival = 0;
                            v.ToInt(&ival);
                            sqlite3_bind_int(stmt, i + 1, ival);
                        } else {
                            sqlite3_bind_text(stmt, i + 1, v.utf8_str(), -1, SQLITE_TRANSIENT);
                        }
                    }

                    if (sqlite3_step(stmt) == SQLITE_DONE) {
                        insertedThisSheet++;
                        totalInserted++;
                    } else {
                        std::cout << "  ERRORE insert riga " << rnum << ": " << sqlite3_errmsg(db) << "\n";
                    }
                    sqlite3_reset(stmt);
                } // else riga vuota → skip
            }
            row = row->GetNext();
        }

        std::cout << "Foglio '" << sh << "': elaborate " << rowCount << " righe totali, inserite " << insertedThisSheet << " righe con dati\n";
        delete file;
    }

    sqlite3_finalize(stmt);

    std::cout << "\nTOTALE record inseriti nel DB: " << totalInserted << "\n\n";

    return totalInserted > 0;
}
