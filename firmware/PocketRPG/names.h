// Hero names: 2 or 3 random syllables, with a filter for English swear words and slurs.
#pragma once

void rollName(char* out, int size, const char* avoid);   // avoid = the name on screen now (not repeated)
bool nameBlocked(const char* name);
