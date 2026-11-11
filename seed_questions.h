#ifndef SEED_QUESTIONS_H
#define SEED_QUESTIONS_H

#include <sqlite3.h>

/* Populate a new, empty question bank. Existing questions are left untouched. */
int seed_questions(sqlite3 *db);

#endif
