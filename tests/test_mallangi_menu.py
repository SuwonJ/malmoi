"""Real menu stdin regression; test the built executable, not a mocked engine."""
import os
from pathlib import Path
import subprocess
import unittest

BINARY = os.environ.get('MALMOI_MENU_BINARY')

@unittest.skipUnless(BINARY, 'Set MALMOI_MENU_BINARY to test real interactive input')
class MenuInputTests(unittest.TestCase):
    def test_boundary_input_via_real_menu(self):
        env = dict(os.environ, MALLANGI_ANSWER='이슬비', MALLANGI_SENSE_ID='250524', MALMOI_DATA='/nonexistent/old.mlm')
        env.pop('MALMOI_MODEL',None);env.pop('MALMOI_SEARCH_BACKEND',None)
        # Multiple games in one actual process: spaces, tabs, whitespace-only,
        # internal spaces in a real word, duplicates, unknowns, and empty exit.
        text = '2\n페이소스\n2\n페이소스\n3\n이슬비 \n3\n 이슬비\n3\n\t이슬비\t\n3\n　이슬비　\n3\n   \n\t\n 사랑의 그림자 \n사랑의 그림자\n__no_such_word__\n이슬비 \n3\n\n0\n'
        result = subprocess.run([str(Path(BINARY).resolve())], input=text, text=True, encoding='utf-8', env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('게임 오류',result.stdout)
        self.assertNotIn('invalid argument',result.stdout+result.stderr)
        # Repeated word searches exercise the real menu and the similarity cache.
        sections=result.stdout.split('단어: ')
        self.assertGreaterEqual(len(sections),3,result.stdout)
        first=sections[1].split('\n\nMalmoi',1)[0]
        second=sections[2].split('\n\nMalmoi',1)[0]
        self.assertEqual(first,second)
        self.assertEqual(len(first.strip().splitlines()),10)
        self.assertEqual(result.stdout.count('정답입니다!'),5,result.stdout)
        self.assertEqual(result.stdout.count('입력이 올바르지 않습니다.'),2,result.stdout)
        self.assertEqual(result.stdout.count('이미 입력한 단어입니다.'),1,result.stdout)
        self.assertEqual(result.stdout.count('아닙니다. 유사도: 0.0135'),1,result.stdout)
        self.assertIn('사전에 없거나 유효한 벡터가 없는 단어',result.stdout)
        chunks=result.stdout.split('[말랑이] 힌트 단어')
        self.assertEqual(len(chunks),7)
        active=chunks[5]
        self.assertEqual(active.count('남은 횟수: 5'),3,active)
        self.assertEqual(active.count('남은 횟수: 4'),3,active)

if __name__=='__main__':unittest.main()
