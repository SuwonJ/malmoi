import unittest
import numpy as np
from src.experiment_suite import canonical, ranked_unique, metrics, Lexical


class EvaluationTests(unittest.TestCase):
    def test_dictionary_markers(self):
        self.assertEqual(canonical('먼지-잼'), '먼지잼')
        self.assertEqual(canonical('검토^위원'), '검토위원')

    def test_unique_words_and_positive_scores(self):
        ids = ranked_unique(np.array([.8, .9, .7, 0.]), ['가', '가', '나', '다'])
        self.assertEqual(ids, [1, 2])
        self.assertEqual(ranked_unique(np.zeros(4), ['가'] * 4), [])

    def test_hit_is_not_recall(self):
        m = metrics(['가'], {'가': 3, '나': 3})
        self.assertEqual(m['hit1'], 1.)
        self.assertEqual(m['recall10'], .5)
        self.assertLess(m['ndcg10'], 1.)
        self.assertAlmostEqual(metrics(['가', '나'], {'가': 3, '나': 3})['ndcg10'], 1.)

    def test_postings_equal_row_scan(self):
        texts = ['희미한 빛이 보인다', '희미한 모습이 보인다', '밝은 빛이 보인다']
        for kind in ('char', 'word', 'bm25'):
            model = Lexical(texts, kind, 100)
            q = model.vectorizer.transform(['희미한 빛이'])
            if kind == 'bm25':
                q.data[:] = 1
            np.testing.assert_allclose(model.score('희미한 빛이'), (model.x @ q.T).toarray().ravel())
            np.testing.assert_array_equal(model.score('zzzzzz'), np.zeros(3))


if __name__ == '__main__':
    unittest.main()
