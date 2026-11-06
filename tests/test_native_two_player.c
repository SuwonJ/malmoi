#include "wordgame.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %s:%d\n", #x, __LINE__);                           \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  Malmoi *m = NULL;
  Game g;
  Record *r = NULL;
  SubmitStatus s;
  clock_t t;
  CHECK(malmoi_open(&m) == 0);
  t = clock();
  CHECK(game_init(&g, m, "내로남불") == 0);
  CHECK(g.rankingCount == RANK_LIMIT);
  printf("two_player_init_cpu_s=%.6f threshold=%.9g\n",
         (double)(clock() - t) / CLOCKS_PER_SEC, (double)g.threshold);
  t = clock();
  s = game_submit(&g, "울기", &r);
  CHECK(s == SUBMIT_PASS || s == SUBMIT_FAIL);
  CHECK(r && isfinite(r->score));
  printf("two_player_submit_cpu_s=%.6f score=%.9g\n",
         (double)(clock() - t) / CLOCKS_PER_SEC, (double)r->score);
  CHECK(game_submit(&g, "울기", &r) == SUBMIT_DUPLICATE);
  game_free(&g);
  malmoi_close(m);
  puts("PASS native two-player");
  return 0;
}
