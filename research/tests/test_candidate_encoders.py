import unittest
from types import SimpleNamespace
import numpy as np
from src.candidate_encoders import CandidateEncoder, INSTRUCTION


class EncoderPoolingTests(unittest.TestCase):
    def encoder(self,kind):
        model=CandidateEncoder.__new__(CandidateEncoder);model.kind=kind
        self.batches=[]
        def encode_batch(texts):
            self.batches.append(texts)
            return [SimpleNamespace(ids=[1,2,0],attention_mask=[1,1,0],type_ids=[0,0,0]),
                    SimpleNamespace(ids=[1,2,3],attention_mask=[1,1,1],type_ids=[0,0,0])][:len(texts)]
        model.tokenizer=SimpleNamespace(encode_batch=encode_batch)
        model.config={}
        model.inputs=[SimpleNamespace(name='input_ids'),SimpleNamespace(name='attention_mask')]
        model.outputs=[SimpleNamespace(name='last_hidden_state')]
        hidden=np.array([[[3.,4.],[0.,8.],[100.,0.]],[[0.,2.],[2.,0.],[4.,3.]]],dtype=np.float32)
        model.session=SimpleNamespace(run=lambda names,feed:[hidden[:len(feed['input_ids'])]])
        return model

    def test_kure_uses_cls_not_mean(self):
        e=self.encoder('kure')
        np.testing.assert_allclose(e.encode(['가','나']),[[.6,.8],[0.,1.]],rtol=1e-6)

    def test_qwen_uses_last_nonpadding_token(self):
        e=self.encoder('qwen')
        np.testing.assert_allclose(e.encode(['가','나']),[[0.,1.],[.8,.6]],rtol=1e-6)

    def test_instruction_only_for_qwen_queries(self):
        e=self.encoder('qwen'); e.encode(['설명'],query=True)
        self.assertEqual(self.batches[-1],[f'Instruct: {INSTRUCTION}\nQuery:설명'])
        e.encode(['뜻풀이']);self.assertEqual(self.batches[-1],['뜻풀이'])


if __name__=='__main__':unittest.main()
