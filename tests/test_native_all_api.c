#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "malmoi.h"
#include "malmoi_bridge.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);              \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static double now(void) { return (double)clock() / CLOCKS_PER_SEC; }
int main(void) {
  Malmoi *m = NULL;
  MalmoiWord w, prev = {0};
  MalmoiResult hits[32];
  GameState game = {0};
  size_t n, i, total;
  float ab, ba, self;
  double t = now(), first;
  int rc;
  CHECK(malmoi_open(&m) == MALMOI_OK);
  printf("open_cpu_s=%.6f\n", now() - t);
  CHECK(!strcmp(malmoi_search_mode(m), "native-index"));
  total = malmoi_word_count(m);
  CHECK(total > 204632);
  for (i = 0; i < total; i++) {
    CHECK(malmoi_word_at(m, i, &w) == 0);
    CHECK(w.id == i && w.word && w.key);
    if (i)
      CHECK(strcmp(prev.key, w.key) < 0);
    prev = w;
  }
  printf("unique_words=%zu\n", total);
  CHECK(malmoi_word_at(m, total, &w) == MALMOI_RANGE);
  CHECK(malmoi_search(m, "우울한 기분", hits, 20, &n) == 0 && n == 20);
  for (i = 0; i < n; i++) {
    CHECK(malmoi_word_at(m, hits[i].word_id, &w) == 0);
    CHECK(!strcmp(w.word, hits[i].word) && !strcmp(w.key, hits[i].key));
  }
  /* Independent float64 SciPy reference:
   * tests/fixtures/native_hybrid_similarity.json.
   */
  {
    const char *a[] = {"감정", "내로남불", "울기", "올톡볼톡하다", "서성이다"};
    const char *b[] = {"가망", "내로남불식", "우울", "올톡볼톡", "서성거리다"};
    const double expected[] = {1.0, 0.6935827073704304, 0.18826474818695327,
                               0.8612266025156878, 0.9369187507534757};
    for (i = 0; i < 5; i++) {
      CHECK(malmoi_similarity(m, a[i], b[i], &ab) == 0);
      CHECK(fabs(ab - expected[i]) < 1e-6);
      CHECK(malmoi_similarity(m, b[i], a[i], &ba) == 0);
      CHECK(ab == ba);
    }
  }
  CHECK(malmoi_similarity(m, "내로남불", "울기", &ab) == 0);
  CHECK(malmoi_similarity(m, "울기", "내로남불", &ba) == 0);
  CHECK(ab == ba && isfinite(ab) && ab >= 0 && ab <= 1);
  CHECK(malmoi_similarity(m, "내로-남불", "내로남불", &self) == 0 &&
        fabs(self - 1) < 1e-6);
  CHECK(malmoi_similarity(m, "__no_such_word__", "울기", &self) ==
        MALMOI_NOT_FOUND);
  CHECK(malmoi_similar(m, "내로남불", NULL, 0, &n) == 0 && n == 0);
  t = now();
  CHECK(malmoi_similar(m, "내로남불", hits, 32, &n) == 0 && n == 32);
  first = now() - t;
  printf("similar_first_cpu_s=%.6f\n", first);
  for (i = 0; i < n; i++) {
    CHECK(malmoi_word_at(m, hits[i].word_id, &w) == 0);
    CHECK(!strcmp(w.key, hits[i].key) && strcmp(w.key, "내로남불"));
    CHECK(isfinite(hits[i].score) && hits[i].score >= 0 && hits[i].score <= 1);
    CHECK(malmoi_similarity(m, "내로남불", hits[i].key, &ab) == 0);
    CHECK(fabs(ab - hits[i].score) < 1e-6);
    printf("similar %zu %s %.9g\n", i + 1, hits[i].key, (double)hits[i].score);
  }
  /* Multiple source senses: the returned score must come from the same
   * sense pair as scalar similarity, not maxima from different pairs. */
  t = now();
  CHECK(malmoi_similar(m, "감정", hits, 10, &n) == 0 && n == 10);
  printf("polysemous_similar_cpu_s=%.6f\n", now() - t);
  for (i = 0; i < n; i++) {
    CHECK(malmoi_similarity(m, "감정", hits[i].key, &ab) == 0);
    CHECK(malmoi_similarity(m, hits[i].key, "감정", &ba) == 0);
    CHECK(ab == ba && fabs(ab - hits[i].score) < 1e-6);
  }
  t = now();
  CHECK(malmoi_similar(m, "내로남불", hits, 32, &n) == 0);
  printf("similar_repeat_cpu_s=%.6f\n", now() - t);
  CHECK(game_start(&game, m, "내로남불") == 1);
  t = now();
  {
    GameResult r = submit_guess(&game, "울기");
    CHECK(r.status == GUESS_WRONG && r.engineStatus == 0);
    CHECK(r.similarity >= 0 && r.similarity <= 1);
  }
  printf("game_guess_cpu_s=%.6f\n", now() - t);
  CHECK(submit_guess(&game, "울기").status == GUESS_DUPLICATE);
  t = now();
  for (i = 0; i < 1000; i++) {
    rc = malmoi_similarity(m, "내로남불", "울기", &ab);
    CHECK(rc == 0);
  }
  printf("pair_cached_average_cpu_s=%.9f\n", (now() - t) / 1000);
  CHECK(submit_guess(&game, "내로남불").status == GUESS_CORRECT);
  {
    MalmoiBridgeOptions options;
    MalmoiStep path[3];
    size_t steps = 0;
    malmoi_bridge_default_options(&options);
    options.neighbors = 5;
    options.max_steps = 3;
    options.max_visits = 3;
    t = now();
    CHECK(malmoi_bridge(m, "내로남불", "내로남불식", &options, path, 3,
                        &steps) == 0);
    CHECK(steps == 2);
    printf("bridge_cpu_s=%.6f\n", now() - t);
  }
  malmoi_close(m);
  puts("PASS all native APIs and game without MLM");
  return 0;
}
