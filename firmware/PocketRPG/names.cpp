// Hero names rolled by the dice: 2 or 3 syllables glued together, checked against a list of blocked words.
#include "names.h"
#include "app.h"   // platformRandom
#include <string.h>

// First syllable (starts the name, already capitalized)
static const char* const FIRST[] = {
  "Ka", "Lo", "Mi", "Ar", "El", "Tha", "Bri", "Da", "Fen", "Ga", "Ha", "Is", "Jo", "Kel", "Ly", "Mor", "Na", "Or",
  "Pe", "Quin", "Ra", "Se", "Tor", "Ul", "Va", "Wy", "Xa", "Yo", "Za", "Cor", "Dre", "Fa", "Gwen", "Hal", "I", "Lu",
  "Mae", "Ny", "Os", "Rho", "Sa", "Ti", "Ve", "Bel", "Cy", "Eo", "Fi", "Kai", "Le", "A", "Bran", "Ce", "Del", "E",
  "Gil", "Jun", "Mer", "Ni", "O", "Ren", "Sel", "Ta", "U", "Vi", "Wen", "Ze", "Al", "Ber", "Dar", "Ela", "Ky", "Rhi",
};
// Middle syllable (3-syllable names only)
static const char* const MIDDLE[] = {
  "ra", "li", "no", "ve", "ma", "ri", "the", "la", "de", "si", "ta", "ne", "lo", "va", "mi", "ga", "do", "be", "lu",
  "ro", "le", "na", "ri", "sa", "vi", "ka", "di", "me", "ly", "ze", "an", "el", "or", "in",
};
// Last syllable
static const char* const LAST[] = {
  "n", "ra", "l", "th", "s", "ia", "on", "is", "ar", "en", "wyn", "el", "or", "a", "us", "ie", "ric", "mir", "dor",
  "na", "ka", "lyn", "rin", "vin", "ro", "x", "to", "ras", "dra", "lia", "nor", "ris", "ven", "ya", "lo", "mo", "ren",
  "ith", "ael", "ion", "eth", "iel", "ara", "isa", "ox", "ek", "in", "an", "ul", "ys",
};
#define COUNT(a) (int)(sizeof(a) / sizeof(a[0]))

// Blocked: English swear words, slurs and crude words. A name is rejected if any of these appears anywhere in it
// (letters only, case ignored), so short roots also catch longer forms.
static const char* const BLOCKED[] = {
  "fuck", "fuk", "fck", "shit", "sht", "cunt", "cock", "dick", "dik", "piss", "twat", "slut", "whore", "hore", "fag",
  "nig", "nigg", "negro", "kike", "spic", "chink", "gook", "coon", "retard", "tranny", "dyke", "homo", "ass", "arse",
  "tit", "cum", "jizz", "sex", "porn", "rape", "rapist", "anal", "anus", "penis", "vagina", "vulva", "boob", "puss",
  "bitch", "bastard", "damn", "crap", "wank", "prick", "dildo", "butt", "turd", "poo", "pee", "fart", "bum", "nazi",
  "hitler", "kkk", "isis", "satan", "hell", "god", "jesus", "allah", "kill", "die", "dead", "slave", "pedo", "pimp",
  "hoe", "skank", "thot", "milf", "nude", "naked", "orgy", "semen", "sperm", "lust", "kink", "bong", "weed", "meth",
  "crack", "drug", "gay", "lesbo", "queer", "wop", "jap", "paki", "gyp", "nob", "knob", "minge", "bollock", "bugger",
  "tosser", "spunk", "shag", "slag", "coc", "kok", "kum", "fuc", "sux", "suck", "lick", "dumb", "idiot", "moron",
  "stupid", "ugly", "fat", "loser", "nympho", "horny", "tits", "vag", "clit", "taint", "scrot", "testi", "rectum",
  "pube", "erect", "booty", "dong", "wiener", "weiner", "schlong", "balls", "nut", "smeg", "felch", "rimjob", "jerk",
};

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool nameBlocked(const char* name) {
  char low[32]; int n = 0;
  for (const char* p = name; *p && n < 31; p++) if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) low[n++] = lower(*p);
  low[n] = 0;
  for (int i = 0; i < COUNT(BLOCKED); i++) if (strstr(low, BLOCKED[i])) return true;
  return false;
}

static bool awkward(const char* s) {
  int n = (int)strlen(s);
  for (int i = 2; i < n; i++) if (lower(s[i]) == lower(s[i - 1]) && lower(s[i]) == lower(s[i - 2])) return true;   // "lll"
  int run = 0, vrun = 0;   // more than 3 consonants or 2 vowels in a row is hard to read
  for (int i = 0; i < n; i++) {
    char c = lower(s[i]);
    bool v = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
    run = v ? 0 : run + 1;
    vrun = v ? vrun + 1 : 0;
    if (run > 3 || vrun > 2) return true;
    if (i && v && c != 'e' && c == lower(s[i - 1])) return true;   // "aa", "ii", "oo", "uu"
  }
  return false;
}

void rollName(char* out, int size, const char* avoid) {
  for (int tries = 0; tries < 100; tries++) {
    char s[32];
    strcpy(s, FIRST[platformRandom(COUNT(FIRST))]);
    if (platformRandom(2)) strcat(s, MIDDLE[platformRandom(COUNT(MIDDLE))]);   // half the names get 3 syllables
    strcat(s, LAST[platformRandom(COUNT(LAST))]);
    int n = (int)strlen(s);
    if (n < 3 || n > size - 1 || n > HERO_MAX_NAME) continue;
    if (awkward(s) || nameBlocked(s)) continue;
    if (avoid && !strcmp(s, avoid)) continue;
    strcpy(out, s);
    return;
  }
  strncpy(out, "Hero", size); out[size - 1] = 0;
}
