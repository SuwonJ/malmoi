"""ONNX retrieval encoders with model-specific, documented pooling."""
from pathlib import Path
import numpy as np

INSTRUCTION='Given a description, retrieve Korean dictionary definitions that express the same meaning.'


class CandidateEncoder:
    def __init__(self, directory: Path, kind: str, threads=4, max_length=128):
        import onnxruntime as ort
        from tokenizers import Tokenizer

        self.kind=kind
        self.tokenizer=Tokenizer.from_file(str(directory/'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=max_length)
        pad_id=1 if kind=='kure' else 151643
        pad_token='<pad>' if kind=='kure' else '<|endoftext|>'
        self.tokenizer.enable_padding(direction='right',pad_id=pad_id,pad_token=pad_token)
        opts=ort.SessionOptions(); opts.intra_op_num_threads=threads; opts.inter_op_num_threads=1
        path=directory/('model.onnx' if kind=='kure' else 'onnx/model_int8.onnx')
        self.session=ort.InferenceSession(str(path),sess_options=opts,providers=['CPUExecutionProvider'])
        self.inputs=self.session.get_inputs()
        self.outputs=self.session.get_outputs()
        self.config=__import__('json').loads((directory/'config.json').read_text())

    def feed(self,tokens):
        ids=np.asarray([x.ids for x in tokens],dtype=np.int64)
        mask=np.asarray([x.attention_mask for x in tokens],dtype=np.int64)
        batch,length=ids.shape
        values={'input_ids':ids,'attention_mask':mask,
                'token_type_ids':np.asarray([x.type_ids for x in tokens],dtype=np.int64),
                'position_ids':np.maximum(np.cumsum(mask,axis=1)-1,0).astype(np.int64)}
        for spec in self.inputs:
            if spec.name.startswith('past_key_values'):
                heads=self.config['num_key_value_heads']
                dim=self.config.get('head_dim',self.config['hidden_size']//self.config['num_attention_heads'])
                values[spec.name]=np.empty((batch,heads,0,dim),dtype=np.float32)
            elif spec.name=='use_cache_branch':
                values[spec.name]=np.array([False],dtype=bool)
        unknown={x.name for x in self.inputs}-values.keys()
        if unknown:
            raise ValueError(f'Unsupported ONNX inputs: {unknown}')
        return {x.name:values[x.name] for x in self.inputs},mask

    def encode(self,texts,query=False,batch_size=8):
        result=[]
        for start in range(0,len(texts),batch_size):
            batch=texts[start:start+batch_size]
            if self.kind=='qwen' and query:
                batch=[f'Instruct: {INSTRUCTION}\nQuery:{t}' for t in batch]
            tokens=self.tokenizer.encode_batch(batch)
            feed,mask=self.feed(tokens)
            hidden=self.session.run([self.outputs[0].name],feed)[0]
            if hidden.ndim==2:
                pooled=hidden
            elif self.kind=='kure':
                pooled=hidden[:,0,:]
            else:
                pooled=hidden[np.arange(len(tokens)),mask.sum(axis=1)-1,:]
            pooled=pooled.astype(np.float32)
            pooled/=np.maximum(np.linalg.norm(pooled,axis=1,keepdims=True),1e-12)
            result.append(pooled)
        return np.concatenate(result)
