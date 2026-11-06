#include "game.h"
#include "malmoi.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  Malmoi *m = NULL;
  MalmoiSense sense;
  MalmoiResult rain[32], song[32], repeat[32];
  size_t count, nr, ns, i, n;
  float a, b;
  GameState game = {0}, song_game = {0}, unchanged;
  clock_t t;
  int saw_rain = 0, saw_song = 0;
  CHECK(malmoi_open(&m) == 0);
  CHECK(malmoi_sense_count(m, "이슬비", &count) == 0 && count == 2);
  for (i = 0; i < count; i++) {
    CHECK(malmoi_sense_at(m, "이슬비", i, &sense) == 0);
    CHECK(sense.word_id < malmoi_word_count(m));
    if (sense.sense_id == 250524) {
      saw_rain = 1;
      CHECK(strstr(sense.definition, "아주 가늘게 내리는 비"));
    }
    if (sense.sense_id == 1626980) {
      saw_song = 1;
      CHECK(strstr(sense.definition, "대중가요"));
    }
  }
  CHECK(saw_rain && saw_song);
  CHECK(malmoi_sense_at(m, "이슬비", count, &sense) == MALMOI_RANGE);
  CHECK(malmoi_similarity_sense(m, "이슬비", 1626983, "사랑의 그림자", &a) ==
        MALMOI_NOT_FOUND);
  CHECK(malmoi_similarity_sense(m, "이슬비", -1, "비", &a) == MALMOI_NOT_FOUND);
  CHECK(malmoi_similar_sense(m, "이슬비", 250524, rain, 32, &nr) == 0 &&
        nr == 32);
  CHECK(malmoi_similar_sense(m, "이슬비", 1626980, song, 32, &ns) == 0 &&
        ns == 32);
  for (i = 0; i < nr; i++) {
    CHECK(malmoi_similarity_sense(m, "이슬비", 250524, rain[i].key, &a) == 0);
    CHECK(fabs(a - rain[i].score) < 1e-6);
    CHECK(isfinite(a) && a >= 0 && a <= 1);
    CHECK(strcmp(rain[i].key, "사랑의 그림자"));
    printf("rain_hint %zu %s %.12f\n", i + 1, rain[i].key, (double)a);
  }
  for (i = 0; i < ns; i++) {
    CHECK(malmoi_similarity_sense(m, "이슬비", 1626980, song[i].key, &a) == 0);
    CHECK(fabs(a - song[i].score) < 1e-6);
  }
  CHECK(malmoi_similar_sense(m, "이슬비", 250524, repeat, 32, &n) == 0 &&
        n == nr);
  for (i = 0; i < n; i++)
    CHECK(repeat[i].word_id == rain[i].word_id &&
          repeat[i].score == rain[i].score);
  {
    const char *words[] = {"그대 모습", "세월은 흘러", "사랑의 그림자"};
    const double expected_rain[] = {.016431836401197854, .01568051115557547,
                                    .013481345730};
    const double expected_song[] = {.7305224301208575, .71066312760631, 1};
    for (i = 0; i < 3; i++) {
      CHECK(malmoi_similarity_sense(m, "이슬비", 250524, words[i], &a) == 0);
      CHECK(malmoi_similarity_sense(m, "이슬비", 1626980, words[i], &b) == 0);
      CHECK(fabs(a - expected_rain[i]) < 1e-7);
      CHECK(fabs(b - expected_song[i]) < 1e-7);
      printf("pair %s rain=%.12f song=%.12f\n", words[i], (double)a, (double)b);
    }
  }
  CHECK(game_start_sense(&game, m, "이슬비", 250524) == 1);
  CHECK(game.answer_has_sense && game.answer_sense_id == 250524);
  for (i = 0; i < (size_t)game.cardCount; i++) {
    CHECK(malmoi_similarity_sense(m, game.answer, game.answer_sense_id,
                                  game.cards[i].word, &a) == 0);
    CHECK(fabs(a - game.cards[i].similarity) < 1e-6);
  }
  unchanged = game;
  CHECK(game_start_sense(&game, m, "이슬비", 1626983) == 0);
  CHECK(!memcmp(&game, &unchanged, sizeof(game)));
  {
    GameResult r = submit_guess(&game, "사랑의 그림자");
    CHECK(r.status == GUESS_WRONG && r.engineStatus == 0 &&
          fabs(r.similarity - .013481345730) < 1e-7);
    CHECK(submit_guess(&game, "사랑의 그림자").status == GUESS_DUPLICATE);
  }
  CHECK(game_start_sense(&song_game, m, "이슬비", 1626980) == 1);
  {
    GameResult r = submit_guess(&song_game, "사랑의 그림자");
    CHECK(r.status == GUESS_WRONG && r.similarity == 1);
  }
  CHECK(submit_guess(&game, "이슬비").status == GUESS_CORRECT);
  CHECK(game_start(&game, m, "이슬비") == 1 && game.answer_has_sense &&
        game.answer_sense_id == 250524);
  {
    const char *variants[] = {"이슬비 ",
                              " 이슬비",
                              "\t이슬비\t",
                              "　이슬비　",
                              "\xc2\xa0"
                              "이슬비"
                              "\xc2\xa0",
                              "\r\n이슬비\v"};
    for (i = 0; i < sizeof(variants) / sizeof(*variants); i++) {
      CHECK(game_start_sense(&game, m, "이슬비", 250524) == 1);
      CHECK(submit_guess(&game, variants[i]).status == GUESS_CORRECT);
      CHECK(game.won && game.attempts == 5);
    }
  }
  CHECK(game_start_sense(&game, m, "이슬비", 250524) == 1);
  {
    const char *invalid[] = {
        "", " ", "\t\n", "　", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
    for (i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
      GameResult r = submit_guess(&game, invalid[i]);
      CHECK(r.status == GUESS_INVALID && r.engineStatus == 0);
      CHECK(game.attempts == 5 && game.usedCount == 0);
    }
  }
  {
    char long_guess[128];
    memset(long_guess, 'A', sizeof(long_guess) - 1);
    long_guess[sizeof(long_guess) - 1] = 0;
    CHECK(submit_guess(&game, long_guess).status == GUESS_INVALID);
    CHECK(game.attempts == 5);
  }
  {
    GameResult r = submit_guess(&game, "  사랑의 그림자 \t");
    CHECK(r.status == GUESS_WRONG && r.engineStatus == 0);
    CHECK(fabs(r.similarity - .013481345730) < 1e-7);
    CHECK(!strcmp(game.usedWords[0], "사랑의 그림자"));
    CHECK(game.attempts == 4);
    CHECK(submit_guess(&game, "사랑의 그림자").status == GUESS_DUPLICATE);
    CHECK(submit_guess(&game, "\t사랑의 그림자 ").status == GUESS_DUPLICATE);
    CHECK(game.attempts == 4);
  }
  {
    GameResult r = submit_guess(&game, "__no_such_word__");
    CHECK(r.status == GUESS_UNKNOWN && r.engineStatus == 0);
    CHECK(game.attempts == 4);
  }
  CHECK(submit_guess(&game, "이슬비 ").status == GUESS_CORRECT);
  t = clock();
  for (i = 0; i < 1000; i++)
    CHECK(malmoi_similarity_sense(m, "이슬비", 250524, "사랑의 그림자", &a) ==
          0);
  printf("sense_pair_cached_us=%.6f\n",
         (double)(clock() - t) / CLOCKS_PER_SEC * 1000);
  malmoi_close(m);
  puts("PASS selected-sense hints, guesses and cache isolation");
  return 0;
}
