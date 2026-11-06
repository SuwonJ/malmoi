#include "../src/internal/native_hybrid.h"
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  double a = malmoi_hybrid_score(.6, 1, 6, 2, 4, 8);
  double b = malmoi_hybrid_score(.6, 1, 2, 6, 8, 4);
  CHECK(fabs(a - .6125) < 1e-12 && a == b);
  CHECK(malmoi_hybrid_score(.6, 1, 0, 0, 0, 8) == .6);
  CHECK(fabs(malmoi_hybrid_score(0, 0, 6, 2, 4, 8) - .625) < 1e-12);
  CHECK(malmoi_hybrid_score(0, 0, 0, 0, 0, 0) == 0);
  CHECK(malmoi_hybrid_score(1, 1, 4, 4, 4, 4) == 1);
  CHECK(malmoi_hybrid_score(0, 1, 0, 0, 4, 8) == 0);
  puts("PASS symmetric BM25 normalization, clipping and fallback");
  return 0;
}
