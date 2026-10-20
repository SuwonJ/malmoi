import unittest
from types import SimpleNamespace
import numpy as np
from src.semantic_search import MorphIndex, content_text, rrf


class SemanticTests(unittest.TestCase):
    def test_keep_negation_and_content(self):
        tokens = [SimpleNamespace(form=f,tag=t) for f,t in [('보','VV'),('이지','EC'),('않','VX'),('희미','XR'),('을','JKO')]]
        self.assertEqual(content_text(tokens),'보 않 희미')

    def test_morph_bm25_matches_hand_calculation(self):
        # unigrams and bigrams: document lengths 3 and 1, df(고양이)=2.
        m=MorphIndex(['고양이 자다','고양이'],'bm25')
        idf=np.log1p(.5/2.5)
        expected=np.array([idf*2.2/(1+1.2*(.25+.75*3/2)),
                           idf*2.2/(1+1.2*(.25+.75*1/2))])
        np.testing.assert_allclose(m.score_tokens('고양이'),expected,rtol=1e-6)

    def test_rrf_rank_scale_invariant(self):
        a=np.array([3.,1.,0.]); b=np.array([0.,4.,2.])
        np.testing.assert_allclose(rrf([a,b]),rrf([a*100,b*.001]))
        expected=np.array([1/61,1/62+1/61,1/62])
        np.testing.assert_allclose(rrf([a,b]),expected,rtol=1e-6)


if __name__=='__main__':
    unittest.main()
