import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from export_e5 import export, fingerprint
from prepare import read_db

class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.model=self.root/'model';(self.model/'onnx').mkdir(parents=True)
        (self.model/'onnx/model_quantized.onnx').write_bytes(b'fixture-model')
        (self.model/'tokenizer.json').write_text('{}')
        provenance={'repo':'Xenova/multilingual-e5-small','revision':'fixture'}
        (self.model/'download.json').write_text(json.dumps(provenance))
        self.dictionary=self.root/'dictionary.jsonl'
        rows=[dict(id=i,word=w,definition=d,word_type='고유어') for i,w,d in [(1,'테스트가','뜻 하나'),(2,'테스트나','뜻 둘'),(3,'테스트다','뜻 하나')]]
        self.dictionary.write_text(''.join(json.dumps(x,ensure_ascii=False)+'\n' for x in rows),encoding='utf-8')
        self.cache=self.root/'cache.npy';np.save(self.cache,np.array([[.6,.8],[0.,1.]],dtype=np.float32))
        state={'data_sha256':fingerprint(self.dictionary),'model_sha256':fingerprint(self.model/'onnx/model_quantized.onnx'),'completed':2,'total':2}
        self.cache.with_suffix('.json').write_text(json.dumps(state))
        self.manifest=self.root/'manifest.json'
        self.manifest.write_text(json.dumps(dict(state,status='completed',embedding_sha256=fingerprint(self.cache),tokenizer_sha256=fingerprint(self.model/'tokenizer.json'),model_download=provenance,senses=3)))
        self.output=self.root/'out.mlm'
    def perform(self,limit=3,words=()):
        with contextlib.redirect_stdout(io.StringIO()):
            return export(self.dictionary,self.cache,self.manifest,self.model,self.output,limit,words)
    def test_real_cache_mapping_and_duplicate_definitions(self):
        manifest=self.perform();db=read_db(self.output)
        self.assertFalse(manifest['synthetic'])
        self.assertEqual(manifest['word_count'],3)
        self.assertEqual(db['words'][0]['vector'],db['words'][2]['vector'])
        self.assertEqual(db['word_dimension'],2)
        self.assertEqual(db['sense_dimension'],0)
    def test_required_word_selection(self):
        self.perform(1,['테스트다'])
        self.assertEqual(read_db(self.output)['words'][0]['key'],'테스트다')
    def test_modified_cache_and_model_are_rejected(self):
        for path in [self.cache,self.model/'onnx/model_quantized.onnx',self.model/'tokenizer.json',self.dictionary]:
            with self.subTest(path=path):
                original=path.read_bytes();path.write_bytes(original+b'x')
                try:
                    with self.assertRaises(ValueError):self.perform()
                finally:path.write_bytes(original)
    def test_incomplete_cache_is_rejected(self):
        p=self.cache.with_suffix('.json');state=json.loads(p.read_text());state['completed']=1;p.write_text(json.dumps(state))
        with self.assertRaises(ValueError):self.perform()

if __name__=='__main__':unittest.main(verbosity=2)
