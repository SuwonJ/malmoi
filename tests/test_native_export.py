import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
import numpy as np
from scipy.sparse import csr_matrix

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from export_native_index import HEADER, ROW, MAGIC, save_terms, save_postings, save_senses


class ExportTests(unittest.TestCase):
    def test_terms_and_postings_are_little_endian_and_versioned(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); h = '12'*32; vocab = {'나': 1, '가': 0}
            save_terms(root/'terms', vocab, h, 1)
            data = (root/'terms').read_bytes()
            magic, version, section, n, corpus = HEADER.unpack_from(data)
            self.assertEqual((magic, version, section, n, corpus), (MAGIC, 1, 1, 2, bytes.fromhex(h)))
            offsets = struct.unpack_from('<3Q', data, HEADER.size)
            pos = HEADER.size + 24
            self.assertEqual(data[pos+offsets[0]+4:pos+offsets[1]].decode(), '가')
            self.assertEqual(struct.unpack_from('<I', data, pos+offsets[1])[0], 1)
            matrix = csr_matrix(np.array([[0., .5], [.25, 0.]], dtype=np.float32))
            save_postings(root/'post', matrix, np.array([1., 2.], dtype=np.float32), h, 2)
            p = (root/'post').read_bytes()
            self.assertEqual(struct.unpack_from('<QQ', p, HEADER.size), (2, 2))
            self.assertEqual(struct.unpack_from('<3Q', p, HEADER.size+16), (0, 1, 2))
            self.assertEqual(struct.unpack_from('<2I', p, HEADER.size+16+24), (1, 0))

    def test_sense_rows_preserve_id_original_and_metadata(self):
        rows = [{'id': 123, 'word': '내로-남불', 'definition': '뜻 하나', 'word_type': '혼종어'},
                {'id': 123, 'word': '내로-남불', 'definition': '다른 뜻', 'word_type': '혼종어'}]
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)/'senses'; save_senses(p, rows, 'ab'*32)
            b=p.read_bytes(); offsets=struct.unpack_from('<3Q', b, HEADER.size); base=HEADER.size+24
            values=[]
            for i in range(2):
                pos=base+offsets[i]; sid,*lengths=ROW.unpack_from(b,pos);pos+=ROW.size
                fields=[]
                for length in lengths:fields.append(b[pos:pos+length].decode());pos+=length
                values.append((sid,fields));self.assertEqual(pos,base+offsets[i+1])
            self.assertEqual(values[0],(123,['내로남불','내로-남불','뜻 하나','혼종어']))
            self.assertEqual(values[1][1][2],'다른 뜻')

    def test_no_invented_sense_ids(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):
                save_senses(Path(d)/'bad',[{'id':None,'word':'단어','definition':'뜻'}],'ab'*32)

if __name__=='__main__':unittest.main()
