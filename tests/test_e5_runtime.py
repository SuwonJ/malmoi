"""Opt-in integration test with real local E5 assets; never downloads assets.

python tests/test_e5_runtime.py --cli build/e5/malmoi_cli \
    --database /path/to/e5.mlm --model /path/to/model
Run using the Python environment selected for the E5 CMake build.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.embed_query import QueryEncoder
from tools.prepare import write_db
ARGS=None


class E5RuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if ARGS is None:raise unittest.SkipTest('Explicit local E5 assets required')
        encoder=QueryEncoder(ARGS.database,ARGS.model)
        query=encoder.encode('뜨거운 느낌')
        cls.folder=tempfile.TemporaryDirectory(prefix='malmoi-e5-')
        cls.addClassCleanup(cls.folder.cleanup)
        cls.database=Path(cls.folder.name)/'사전 with spaces.mlm'
        # The lexical answer deliberately conflicts with the vector answer.
        # This verifies that the public API actually dispatches to E5.
        words=[{'id':0,'word':'정답','key':'정답','lexeme_id':'w0','vector':query},
               {'id':1,'word':'오답','key':'오답','lexeme_id':'w1','vector':[-x for x in query]}]
        senses=[{'id':0,'word_id':0,'source_id':'s0','definition':'차갑다','pos':'예제','vector':[]},
                {'id':1,'word_id':1,'source_id':'s1','definition':'뜨거운 느낌','pos':'예제','vector':[]}]
        write_db(cls.database,{'words':words,'senses':senses,
                              'word_space':json.dumps(encoder.space,ensure_ascii=False,sort_keys=True,separators=(',',':')),
                              'sense_space':'','metadata':{'synthetic':True},
                              'word_dimension':len(query),'sense_dimension':0,'flags':1})

    def invoke(self,*args,model=True):
        env={**os.environ,'MALMOI_DATA':str(self.database),
             'MALMOI_ENCODER_SCRIPT':str(ROOT/'tools/embed_query.py')}
        env.pop('MALMOI_MODEL',None)
        if model:env['MALMOI_MODEL']=str(ARGS.model)
        return subprocess.run([str(ARGS.cli),*args],cwd=self.folder.name,env=env,
                              capture_output=True,encoding='utf-8',timeout=60)

    def test_real_encoder_dispatch_and_cosine(self):
        result=self.invoke('query','뜨거운 느낌')
        self.assertEqual(result.returncode,0,result.stderr)
        rows=[line.split('\t') for line in result.stdout.splitlines()]
        self.assertEqual([r[1] for r in rows],['정답','오답'])
        self.assertAlmostEqual(float(rows[0][2]),1,places=5)
        self.assertAlmostEqual(float(rows[1][2]),-1,places=5)

    def test_same_binary_without_model_uses_lexical(self):
        result=self.invoke('query','뜨거운 느낌',model=False)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines()[0].split('\t')[1],'오답')

    def test_word_similarity_unchanged(self):
        result=self.invoke('pair','정답','오답')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertAlmostEqual(float(result.stdout),-1,places=5)

    def test_query_limit_before_encoding(self):
        result=self.invoke('query','a'*8193)
        self.assertEqual(result.returncode,10,result.stderr)

    def test_mismatched_dictionary_rejected(self):
        env={**os.environ,'MALMOI_DATA':str(ROOT/'data/dictionary.mlm'),
             'MALMOI_MODEL':str(ARGS.model),'MALMOI_ENCODER_SCRIPT':str(ROOT/'tools/embed_query.py')}
        result=subprocess.run([str(ARGS.cli),'query','뜨거운 느낌'],env=env,
                              capture_output=True,encoding='utf-8',timeout=60)
        self.assertEqual(result.returncode,12,result.stderr)
        self.assertIn('invalid encoder configuration',result.stderr)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('cli','database','model'):parser.add_argument('--'+name,type=Path,required=True)
    ARGS=parser.parse_args()
    ARGS.cli=ARGS.cli.resolve();ARGS.database=ARGS.database.resolve();ARGS.model=ARGS.model.resolve()
    unittest.main(argv=[sys.argv[0]],verbosity=2)
