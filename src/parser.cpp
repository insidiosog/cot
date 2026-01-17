#include "parser.h"
#include <wx/wx.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/fs_zip.h>
#include <wx/xml/xml.h>
#include <map>
#include <vector>
#include <iostream>

wxXmlNode* FindChildByName(wxXmlNode* parent, const wxString& name) {
    if (!parent) return nullptr;
    wxXmlNode* child = parent->GetChildren();
    while (child) {
        if (child->GetName() == name) {
            return child;
        }
        child = child->GetNext();
    }
    return nullptr;
}

bool ImportFromExcel(sqlite3* db, const std::string& filename, const std::vector<std::string>& sheetNames) {
    wxFileSystem::AddHandler(new wxZipFSHandler);

    wxFileSystem fs;
	wxFileName fn(filename);  // se filename è std::string, wxFileName lo accetta direttamente
	fn.MakeAbsolute();
	wxString absFilename = fn.GetFullPath();
	std::cout << "Path assoluto usato: " << absFilename.mb_str() << "\n";

    if (!fs.OpenFile(wxString(filename))) {
        std::cout << "ERRORE: Impossibile aprire il file zip: " << filename << "\n";
        return false;
    }
    std::cout << "File .xlsx aperto con successo: " << filename << "\n";

    // ────────────────────────────────────────────────
    // sharedStrings
    std::vector<wxString> sharedStrings;
    {
        wxString url = wxString::Format("file:%s#zip:xl/sharedStrings.xml", absFilename);
        std::cout << "Tentando di aprire: " << url << "\n";
		wxFSFile* file = fs.OpenFile(url);
        if (file) {
            std::cout << "Trovato sharedStrings.xml → caricamento...\n";
            wxInputStream* stream = file->GetStream();
            wxXmlDocument doc(*stream);
            
			if (doc.IsOk()) {
				wxXmlNode* root = doc.GetRoot();
				if (root) {
					wxString rootName = root->GetName();

					// Stampa per debug (rimuovi dopo se vuoi)
					std::cout << "Nome effettivo del root node: '" << rootName.mb_str() << "'\n";

					// Confronta ignorando namespace: prendi solo la parte dopo ':' o l'intero se non c'è ':'
					wxString localName = rootName;
					size_t colonPos = rootName.find(':');
					if (colonPos != wxString::npos) {
						localName = rootName.substr(colonPos + 1);
					}

					if (localName == "sst") {
						std::cout << "Root riconosciuto come 'sst' (locale)\n";
						size_t count = 0;
						wxXmlNode* si = root->GetChildren();
						while (si) {
							wxString siName = si->GetName();
							// Stesso trucco per si
							size_t siColon = siName.find(':');
							if (siColon != wxString::npos) {
								siName = siName.substr(siColon + 1);
							}

							if (siName == "si") {
								wxXmlNode* t = FindChildByName(si, "t");
								if (t) {
									wxString text = t->GetNodeContent();
									// Rimuovi eventuali spazi extra se necessario
									text.Trim(true).Trim(false);
									sharedStrings.push_back(text);
									if (count % 10 == 0) std::cout << "  ... caricata stringa #" << count << ": '" << text.mb_str() << "'\n";
									count++;
								}
							}
							si = si->GetNext();
						}
						std::cout << "Caricate " << count << " stringhe condivise (dovrebbero essere 54)\n";
					} else {
						std::cout << "WARNING: root locale non è 'sst', trovato: '" << localName.mb_str() << "'\n";
					}
				} else {
					std::cout << "sharedStrings.xml vuoto o senza root\n";
				}
			} else {
				std::cout << "ERRORE: sharedStrings.xml non parsabile da wxXmlDocument\n";
			}
            
            delete file;
        } else {
            std::cout << "sharedStrings.xml NON trovato (possibile file inline o assente)\n";
        }
    }

    // ────────────────────────────────────────────────
    // workbook → nomi fogli → sheetId
    std::map<std::string, std::string> sheetNameToId;
    {
        wxString url = wxString::Format("file:%s#zip:xl/workbook.xml", absFilename);
        std::cout << "Tentando di aprire: " << url << "\n";
		wxFSFile* file = fs.OpenFile(url);
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
                            std::cout << "  Foglio trovato: '" << name.mb_str() << "' → sheetId=" << sid.mb_str() << "\n";
                            sheetCount++;
                        }
                        sheet = sheet->GetNext();
                    }
                    std::cout << "Trovati " << sheetCount << " fogli nel workbook\n";
                } else {
                    std::cout << "ERRORE: nodo 'sheets' non trovato in workbook.xml\n";
                }
            } else {
                std::cout << "ERRORE: workbook.xml non valido\n";
            }
            delete file;
        } else {
            std::cout << "ERRORE: workbook.xml NON trovato!\n";
            return false;
        }
    }

    // Prepara statement INSERT
    const char* insertSQL = 
        "INSERT INTO stagioni (puntata, numero, venditore, tipo, informazioni, oggetto, storia, "
        "valutazione, offerta, esito, rilancio, esito_rilancio, compratore, scostamento) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cout << "ERRORE prepare INSERT: " << sqlite3_errmsg(db) << "\n";
        return false;
    }

    int totalInserted = 0;

    for (const auto& sh : sheetNames) {
        auto it = sheetNameToId.find(sh);
        if (it == sheetNameToId.end()) {
            std::cout << "Foglio richiesto NON trovato nel workbook: '" << sh << "'\n";
            continue;
        }

        std::string sheetPath = "xl/worksheets/sheet" + it->second + ".xml";
        std::cout << "Tentativo apertura foglio: '" << sh << "' (" << sheetPath << ")\n";

        wxString url = wxString::Format("file:%s#zip:%s", absFilename, wxString(sheetPath));
        std::cout << "Tentando di aprire: " << url << "\n";
		wxFSFile* file = fs.OpenFile(url);
		
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
                                        val = sharedStrings[static_cast<size_t>(idx)];
                                    } else {
                                        std::cout << "  Attenzione: indice shared string fuori range: " << idx << "\n";
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
                    // Bind
                    for (int i = 0; i < 14; ++i) {
                        const wxString& v = cells[i];
                        if (i == 7 || i == 8 || i == 10 || i == 13) { // valutazione, offerta, rilancio, scostamento
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
                }
            }
            row = row->GetNext();
        }

        std::cout << "Foglio '" << sh << "': elaborate " << rowCount << " righe totali (escluse header), inserite " << insertedThisSheet << " righe con dati\n";
        delete file;
    }

    sqlite3_finalize(stmt);

    std::cout << "\n=======================================\n";
    std::cout << "TOTALE record inseriti nel database: " << totalInserted << "\n";
    std::cout << "=======================================\n\n";

    return totalInserted > 0;
}
