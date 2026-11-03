"""Native public API integration tests; no installed Python packages or child tools."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.prepare import read_db,write_db
CLI=ROOT/'build/core/malmoi_cli'

class PublicApiTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)/'사전 데이터 😀';self.folder.mkdir()
        self.database=self.folder/'dictionary.mlm'
        self.database.write_bytes((ROOT/'data/dictionary.mlm').read_bytes())
        self.environment={**os.environ,'MALMOI_DATA':str(self.database),'PATH':''}
    def invoke(self,*args,env=None,cwd=None):
        result=subprocess.run([str(CLI),*args],cwd=cwd or self.folder,capture_output=True,
                              encoding='utf-8',env=env or self.environment,timeout=15)
        self.assertNotIn('Sanitizer',result.stderr)
        return result
    def test_native_description_search_without_any_child_executable(self):
        result=self.invoke('query','작은 물방울로 조용히 내리는 비.')
        self.assertEqual(result.returncode,0,result.stderr)
        rows=[line.split('\t') for line in result.stdout.splitlines()]
        self.assertEqual(rows[0][1],'이슬비')
        self.assertAlmostEqual(float(rows[0][2]),1,places=6)
        self.assertEqual(len(rows),len({row[1] for row in rows}))
        self.assertTrue(all(0<float(row[2])<=1 for row in rows))
        self.assertEqual([float(row[2]) for row in rows],sorted((float(row[2]) for row in rows),reverse=True))
    def test_word_search_and_pair_without_python_or_models(self):
        result=self.invoke('pair','먼지잼','이슬비')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertAlmostEqual(float(result.stdout),.8,places=6)
        result=self.invoke('word','먼지잼')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines()[0].split('\t')[1],'이슬비')
        self.assertNotIn('\t먼지잼\t',result.stdout)
    def test_owner_replaces_mlm_without_rebuilding_caller(self):
        before=hashlib.sha256(CLI.read_bytes()).hexdigest()
        a=self.invoke('pair','먼지잼','이슬비')
        db=read_db(self.database);db['words'][0]['vector']=[0,1,0,0,0,0,0,0]
        write_db(self.database,db)
        b=self.invoke('pair','먼지잼','이슬비')
        self.assertEqual(b.returncode,0,b.stderr)
        self.assertNotEqual(a.stdout,b.stdout)
        self.assertAlmostEqual(float(b.stdout),.6,places=6)
        self.assertEqual(before,hashlib.sha256(CLI.read_bytes()).hexdigest())
    def test_invalid_missing_and_zero_inputs(self):
        for args,status in [(('query',''),1),(('query','!!!'),1),(('query','x'*8193),10),
                            (('pair','missing','눈'),6),(('pair','영벡터','눈'),9)]:
            with self.subTest(args=args[0],status=status):
                self.assertEqual(self.invoke(*args).returncode,status)
        self.database.unlink()
        self.assertEqual(self.invoke('pair','눈','눈').returncode,2)
        self.database.write_bytes(b'invalid')
        self.assertEqual(self.invoke('query','rain').returncode,3)
    def test_default_database_can_be_relocated_with_executable(self):
        data=self.folder/'data';data.mkdir()
        (data/'dictionary.mlm').write_bytes(self.database.read_bytes())
        env=dict(self.environment);env.pop('MALMOI_DATA')
        result=self.invoke('pair','먼지잼','이슬비',env=env)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_ascii_case_and_punctuation_are_normalized(self):
        a=self.invoke('query','작은 물방울로 조용히 내리는 비.')
        b=self.invoke('query','작은물방울로조용히내리는비!!!')
        self.assertEqual(a.stdout,b.stdout)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--cli',type=Path,default=CLI)
    args,rest=parser.parse_known_args();CLI=args.cli.resolve()
    unittest.main(argv=[sys.argv[0],*rest],verbosity=2)
