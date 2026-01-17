#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <vector>
#include <sqlite3.h>

bool ImportFromExcel(sqlite3* db, const std::string& filename, const std::vector<std::string>& sheetNames);

#endif // PARSER_H
