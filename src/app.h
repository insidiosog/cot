#ifndef APP_H
#define APP_H

#include <sqlite3.h>
#include <wx/wx.h>

class MyFrame;

class MyApp : public wxApp {
public:
    virtual bool OnInit() override;
    sqlite3* GetDatabase() { return db; }

private:
    sqlite3* db = nullptr;
};

wxDECLARE_APP(MyApp);

#endif // APP_H
