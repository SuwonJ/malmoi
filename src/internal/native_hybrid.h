#ifndef MALMOI_NATIVE_HYBRID_H
#define MALMOI_NATIVE_HYBRID_H
/* BM25(A->B) is normalized against BM25(A->A), clipped per direction.
 * Symmetric mean and equal TF-IDF/BM25 mix are exploratory baselines. */
static double malmoi_hybrid_score(double cosine, int has_char,
                                  double bm_forward, double bm_reverse,
                                  double bm_self_a, double bm_self_b) {
    int has_bm = bm_self_a > 0 && bm_self_b > 0;
    double bm = 0;
    if (has_bm) {
        double x = bm_forward / bm_self_a, y = bm_reverse / bm_self_b;
        if (x > 1) x = 1;
        if (y > 1) y = 1;
        bm = (x + y) * 0.5;
    }
    if (has_char && has_bm) return (cosine + bm) * 0.5;
    return has_char ? cosine : has_bm ? bm : 0;
}
#endif
