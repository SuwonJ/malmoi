import json
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from extract_dictionary import extract
from lexical_search import LexicalSearch, load_records

class ToolTests(unittest.TestCase):
    def test_extract_records_and_keep_existing_output_on_bad_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'raw';source.mkdir();output=root/'dictionary.jsonl'
            row={'target_code':1,'wordinfo':{'word':'이슬비','word_type':'고유어'},'senseinfo':{'definition':'작은 물방울로 내리는 비','pos':'명사'}}
            path=source/'one.json';path.write_text(json.dumps({'channel':{'item':[row,{'target_code':2}]}}),encoding='utf-8')
            self.assertEqual(extract(source,output),1)
            self.assertEqual(load_records(output)[0]['word'],'이슬비')
            before=output.read_bytes();path.write_text('{invalid')
            with self.assertRaises(ValueError):extract(source,output)
            self.assertEqual(output.read_bytes(),before)
    def test_postings_match_direct_cosine_and_group_words(self):
        import numpy as np
        rows=[{'id':1,'word':'먼지-잼','definition':'먼지를 적시는 작은 비'},
              {'id':2,'word':'먼지잼','definition':'먼지를 적시는 작은 비'},
              {'id':3,'word':'이슬비','definition':'조용히 내리는 작은 비'},
              {'id':4,'word':'산들바람','definition':'산에서 부는 부드러운 바람'}]
        search=LexicalSearch(rows)
        for query in ['먼지를 적시는 작은 비','조용히 내리는 비','부드러운 바람']:
            q=search.vectorizer.transform([query]);matrix=search.vectorizer.transform(row['definition'] for row in rows)
            actual=(q@search.index).toarray().ravel()
            expected=(matrix@q.T).toarray().ravel()
            np.testing.assert_allclose(actual,expected,atol=1e-6)
        hits=search.search('먼지를 적시는 작은 비')
        self.assertEqual(hits[0]['key'],'먼지잼')
        self.assertEqual(len(hits),len({h['key'] for h in hits}))
        self.assertAlmostEqual(hits[0]['score'],1,places=5)
        self.assertEqual(search.search('zzzzzz'),[])
        self.assertEqual(search.search('비',0),[])
        with self.assertRaises(ValueError):search.search('')
        with self.assertRaises(ValueError):search.search('비',-1)

if __name__=='__main__':unittest.main(verbosity=2)
