// numrec-eval -- how well the fast number recognizer (numrec.h) does on the
// recordings numtrain.h made:
//
//   ./numrec-eval [--by-session] [--sweep] [--verbose] [--unusual]
//                 [name=value ...] [dir]
//
// Every session in `dir` (by default where numtrain.h puts them) is learned,
// then each is played back through the recognizer exactly as the microphone
// would feed it, with each utterance compared against every recording except
// itself -- or with --by-session, except any from its own session, which is
// the harder and more honest test once there's more than one.
//
// What it says for each utterance, against the prompt that was up:
//   right   a number, heard as that number
//   wrong   a number, heard as a different one
//   false   not a number, heard as one
//   missed  a number, heard as nothing
//
// name=value sets a parameter of NrParams, e.g. hangover=6 accept=1.4.
// --sweep tries a range of hangovers instead.  --unusual lists the
// recordings the jammer's review would ask about (nr_find_unusual), worst
// first, instead of testing.

#include <dirent.h>
#include <time.h>

#include "numrec.h"

typedef struct {
  char base[1024];
  float* x;
  long long n;
  double rate;
  NrPrompt* prompts;
  int n_prompts;
  NrReview* reviews;  // what a person said some of its words were
  int n_reviews;
  float gate_db;
  float room_db;
} Session;

typedef struct {
  const NrModel* model;
  const Session* session;
  int index;
  bool by_session;
  bool verbose;
  int hop;
  int right, wrong, false_fire, missed, rejected, unclear;
  int heard_prompt[4096];  // how many utterances each prompt had
  double worst_ms, total_ms;
  int classified;
  int waited[7], n_waited[7];  // quiet before each number was taken, frames
} Test;

static bool test_fn(void* ctx, NrStream* s, long long onset, long long end,
                    bool clear, int quiet, bool final) {
  Test* t = ctx;
  const Session* se = t->session;
  long long sample = onset * t->hop;
  int k = nr_prompt_at(se->prompts, se->n_prompts, sample);
  int truth = k < 0 ? -1 : nr_prompt_label(se->prompts[k].text);
  if (truth < 0) return true;  // "get ready"

  float feat[NR_MAX_FRAMES + 2 * NR_PAD][NR_DIM];
  int n = nr_utterance(s, onset, end, feat);
  struct timespec a, b;
  clock_gettime(CLOCK_MONOTONIC, &a);
  NrResult r = nr_classify(t->model, feat, n, &s->p, t->index, sample,
                           t->by_session ? (1LL << 62)
                                         : (long long)(0.2 * se->rate));
  clock_gettime(CLOCK_MONOTONIC, &b);
  double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
  t->total_ms += ms;
  t->classified++;
  if (ms > t->worst_ms) t->worst_ms = ms;

  int heard = clear ? r.label : -1;
  // Not yet: its word's wait isn't up, or it's nothing so far but the rest
  // may still come.
  if (heard >= 0 && quiet < t->model->wait[heard]) return false;
  if (heard < 0 && !final) return false;
  if (k < 4096) t->heard_prompt[k]++;
  if (heard >= 0 && heard == truth) {
    t->waited[heard] += quiet;
    t->n_waited[heard]++;
  }
  const char* verdict;
  if (truth != NR_OTHER) {
    if (heard == truth) { t->right++; verdict = "right"; }
    else if (heard >= 0) { t->wrong++; verdict = "WRONG"; }
    else { t->missed++; verdict = "MISSED"; }
  } else {
    if (heard >= 0) { t->false_fire++; verdict = "FALSE"; }
    else { t->rejected++; verdict = "ok"; }
  }
  if (!clear) t->unclear++;
  bool bad = verdict[0] >= 'A' && verdict[0] <= 'Z';
  if (bad || t->verbose) {
    printf("  %-6s %7.2fs %-32s %4lldms +%3dms  nearest %-5s %.2f, "
           "runner-up %.2f%s\n", verdict, (double)sample / se->rate,
           se->prompts[k].text, (end - onset + 1) * 10, quiet * 10,
           r.nearest >= 0 ? NR_WORDS[r.nearest] : "-", r.dist, r.runner_up,
           clear ? "" : "  (no quiet before)");
    if (heard < 0 && clear && truth != NR_OTHER && r.why) {
      printf("         (%s)\n", r.why);
    }
  }
  return true;
}

static int load_sessions(const char* dir, Session** out) {
  DIR* d = opendir(dir);
  if (!d) return 0;
  Session* s = NULL;
  int n = 0;
  struct dirent* e;
  while ((e = readdir(d))) {
    size_t len = strlen(e->d_name);
    if (len < 5 || strcmp(e->d_name + len - 4, ".wav") != 0) continue;
    s = realloc(s, sizeof(Session) * (size_t)(n + 1));
    Session* se = &s[n];
    memset(se, 0, sizeof(*se));
    snprintf(se->base, sizeof(se->base), "%s/%.*s", dir, (int)len - 4,
             e->d_name);
    char path[1100];
    snprintf(path, sizeof(path), "%s.wav", se->base);
    se->x = nr_read_wav(path, &se->n, &se->rate);
    snprintf(path, sizeof(path), "%s.tsv", se->base);
    se->gate_db = -20;
    se->room_db = -70;
    se->n_prompts = nr_read_prompts(path, &se->prompts, &se->gate_db,
                                    &se->room_db);
    se->n_reviews = nr_read_reviews(path, &se->reviews);
    if (!se->x || se->n_prompts <= 0) {
      fprintf(stderr, "skipping %s: can't read it\n", se->base);
      continue;
    }
    n++;
  }
  closedir(d);
  *out = s;
  return n;
}

static bool set_param(NrParams* p, const char* arg) {
  const char* eq = strchr(arg, '=');
  if (!eq) return false;
  double v = atof(eq + 1);
  size_t len = (size_t)(eq - arg);
#define P(name, field) \
  if (len == strlen(name) && strncmp(arg, name, len) == 0) { \
    p->field = (__typeof__(p->field))v; return true; }
  P("trigger_db", trigger_db) P("floor_db", floor_db) P("noise_db", noise_db)
  P("rel_db", rel_db) P("hangover", hangover) P("pre_quiet", pre_quiet)
  P("end_hangover", end_hangover)
  P("max_back", max_back) P("accept", accept) P("margin", margin)
#undef P
  return false;
}

// Learn everything with these parameters, then test every session.
static void run(Session* s, int n, NrParams p, bool trigger_set,
                bool by_session, bool verbose, bool quiet) {
  NrModel model = {0};
  int learned[NR_N_LABELS] = {0};
  for (int i = 0; i < n; i++) {
    NrParams sp = p;
    if (!trigger_set) sp.trigger_db = s[i].gate_db;
    sp.room_db = s[i].room_db;
    nr_learn_session(&model, s[i].x, s[i].n, s[i].rate, s[i].prompts,
                     s[i].n_prompts, s[i].reviews, s[i].n_reviews, false,
                     &sp, i);
  }
  for (int i = 0; i < model.n; i++) learned[model.t[i].label]++;
  nr_model_finish(&model, &p);
  if (!quiet) {
    printf("learned:");
    for (int l = 0; l < NR_N_LABELS; l++) {
      printf(" %s %d", NR_WORDS[l], learned[l]);
    }
    printf("\nwaits:");
    for (int l = 0; l < 7; l++) {
      printf(" %s %dms", NR_WORDS[l], model.wait[l] * 10);
    }
    printf("\n");
  }

  Test total = {0};
  int prompts_no_utterance = 0;
  for (int i = 0; i < n; i++) {
    if (!quiet) printf("%s\n", s[i].base);
    Test t = {.model = &model, .session = &s[i], .index = i,
              .by_session = by_session, .verbose = verbose && !quiet};
    NrParams sp = p;
    if (!trigger_set) sp.trigger_db = s[i].gate_db;
    sp.room_db = s[i].room_db;
    NrStream* st = malloc(sizeof(NrStream));
    nr_stream_init(st, s[i].rate, sp, test_fn, &t);
    t.hop = st->f.hop;
    if (quiet) t.verbose = false;
    for (long long j = 0; j < s[i].n; j += 480) {
      nr_stream_push(st, s[i].x + j,
                     (int)(s[i].n - j < 480 ? s[i].n - j : 480));
    }
    for (int k = 0; k < s[i].n_prompts && k < 4096; k++) {
      int label = nr_prompt_label(s[i].prompts[k].text);
      if (label >= 0 && label != NR_OTHER && t.heard_prompt[k] == 0) {
        prompts_no_utterance++;
        if (!quiet) {
          printf("  NOTHING %6.2fs %s: no utterance found\n",
                 (double)s[i].prompts[k].sample / s[i].rate,
                 s[i].prompts[k].text);
        }
      }
    }
    total.right += t.right; total.wrong += t.wrong;
    total.false_fire += t.false_fire; total.missed += t.missed;
    total.rejected += t.rejected; total.unclear += t.unclear;
    total.total_ms += t.total_ms; total.classified += t.classified;
    if (t.worst_ms > total.worst_ms) total.worst_ms = t.worst_ms;
    for (int l = 0; l < 7; l++) {
      total.waited[l] += t.waited[l];
      total.n_waited[l] += t.n_waited[l];
    }
    nr_stream_free(st);
    free(st);
  }
  int numbers = total.right + total.wrong + total.missed;
  int all_waited = 0, all_n = 0;
  for (int l = 0; l < 7; l++) {
    all_waited += total.waited[l];
    all_n += total.n_waited[l];
  }
  printf("hangover %3dms-%3dms margin %.2f accept %.2f: right %d/%d "
         "(after %.0fms on average), wrong %d, missed %d (+%d never found), "
         "false %d of %d others", p.hangover * 10, p.end_hangover * 10,
         p.margin, p.accept, total.right, numbers,
         all_n ? 10.0 * all_waited / all_n : 0, total.wrong, total.missed, prompts_no_utterance,
         total.false_fire, total.false_fire + total.rejected);
  if (!quiet) {
    printf("\n  quiet waited for, by word:");
    for (int l = 0; l < 7; l++) {
      printf(" %s %.0fms", NR_WORDS[l],
             total.n_waited[l] ? 10.0 * total.waited[l] / total.n_waited[l]
                               : 0);
    }
    printf("\n  matching took %.2fms on average, %.2fms at worst",
           total.classified ? total.total_ms / total.classified : 0,
           total.worst_ms);
  }
  printf("\n");
  nr_model_free(&model);
}

// The recordings that sound more like another word than their own, as the
// jammer's review would ask about them, but all of them.
static void list_unusual(Session* s, int n, NrParams p, bool trigger_set) {
  NrModel model = {0};
  for (int i = 0; i < n; i++) {
    NrParams sp = p;
    if (!trigger_set) sp.trigger_db = s[i].gate_db;
    sp.room_db = s[i].room_db;
    nr_learn_session(&model, s[i].x, s[i].n, s[i].rate, s[i].prompts,
                     s[i].n_prompts, s[i].reviews, s[i].n_reviews, false,
                     &sp, i);
  }
  nr_model_finish(&model, &p);
  static NrUnusual u[4096];
  int found = nr_find_unusual(&model, &p, u, 4096);
  int reviewed = 0;
  for (int i = 0; i < model.n; i++) reviewed += model.t[i].reviewed;
  printf("%d of %d recordings are unusual (%d reviewed, not counted)\n",
         found, model.n, reviewed);
  for (int i = 0; i < found; i++) {
    const NrTemplate* t = &model.t[u[i].index];
    printf("  %5.2f  %-6s sounds like %-6s  %s at %.2fs\n", u[i].score,
           NR_WORDS[t->label], NR_WORDS[u[i].nearest], s[t->session].base,
           t->sample / s[t->session].rate);
  }
  nr_model_free(&model);
}

int main(int argc, char** argv) {
  char dir[1024];
  snprintf(dir, sizeof(dir),
           "%s/Library/Application Support/com.jefftk.jammer/numbers",
           getenv("HOME") ?: ".");
  NrParams p = nr_default_params(-20);
  bool by_session = false, sweep = false, verbose = false, trigger_set = false;
  bool unusual = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--by-session") == 0) by_session = true;
    else if (strcmp(argv[i], "--sweep") == 0) sweep = true;
    else if (strcmp(argv[i], "--verbose") == 0) verbose = true;
    else if (strcmp(argv[i], "--unusual") == 0) unusual = true;
    else if (strchr(argv[i], '=')) {
      if (!set_param(&p, argv[i])) {
        fprintf(stderr, "unknown parameter: %s\n", argv[i]);
        return 1;
      }
      if (strncmp(argv[i], "trigger_db=", 11) == 0) trigger_set = true;
    } else {
      snprintf(dir, sizeof(dir), "%s", argv[i]);
    }
  }
  Session* s;
  int n = load_sessions(dir, &s);
  if (n == 0) {
    fprintf(stderr, "no sessions in %s\n", dir);
    return 1;
  }
  if (unusual) {
    list_unusual(s, n, p, trigger_set);
    return 0;
  }
  if (!sweep) {
    run(s, n, p, trigger_set, by_session, verbose, false);
    return 0;
  }
  static const int HANGOVERS[] = {2, 3, 4, 6};
  static const int END_HANGOVERS[] = {15, 18, 22};
  for (size_t h = 0; h < sizeof(HANGOVERS) / sizeof(int); h++) {
    for (size_t m = 0; m < sizeof(END_HANGOVERS) / sizeof(int); m++) {
      NrParams q = p;
      q.hangover = HANGOVERS[h];
      q.end_hangover = END_HANGOVERS[m];
      run(s, n, q, trigger_set, by_session, false, true);
    }
  }
  return 0;
}
