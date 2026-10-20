#!/usr/bin/env python3
import argparse, collections, hashlib, json, pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
HEADER='.version 8.0\n.target sm_70\n.address_size 64\n'
def invoke(binary,path,cmd='analyze',check=True):return subprocess.run([binary,cmd,str(path),'--json'],capture_output=True,text=True,check=check)
def validate(binary,smoke=False):
    annotation=json.loads((ROOT/'fixtures/annotations.json').read_text());labeled=matched=kernels=instructions=parser_cases=0;rows=[];modules={}
    for filename,functions in annotation['files'].items():
        path=ROOT/'fixtures'/filename;module=json.loads(invoke(binary,path).stdout);modules[filename]=module
        assert {f['name'] for f in module['functions']}==set(functions)
        file_count=0
        for f in module['functions']:
            expected=functions[f['name']];actual=[(i['opcode'],i['category']) for i in f['instructions']]
            assert actual==[tuple(p) for p in expected],(filename,f['name'],actual,expected)
            assert f['instruction_count']==len(expected)==sum(f['categories'].values())
            assert len(f['instructions'])==f['instruction_count'];parser_cases+=1
            for i in f['instructions']:
                assert i['line']>0
            for ob in f['observations']:
                assert ob['evidence'] and ob['confidence'] and ob['explanation']
                assert 0<=ob['instruction']<f['instruction_count']
            labeled+=len(expected);matched+=len(expected);file_count+=len(expected)
        assert module['total_instructions']==file_count
        kernels+=module['kernels'];instructions+=file_count
        rows.append({'fixture':filename,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'kernels':module['kernels'],'instructions':file_count})
    vector=modules['vector_add.ptx']['functions'][0]
    assert vector['register_declarations_by_type']=={'pred':1,'b32':4,'b64':7,'f32':3}
    assert vector['memory_operations_by_space']=={'global':3,'param':3}
    assert vector['predicated_instructions']==1 and vector['branch_instructions']==1
    assert [(b['begin'],b['end'],b['successors']) for b in vector['blocks']]==[(0,9,[1,2]),(9,17,[2]),(17,18,[])]
    parser_cases+=1
    branches=modules['branches.ptx']['functions'][0]
    assert [b['successors'] for b in branches['blocks']]==[[1,4],[2,3],[5],[5],[5],[]]
    assert branches['predicated_instructions']==2 and branches['branch_instructions']==4
    parser_cases+=1
    formatting=modules['formatting.ptx']['functions'][0]
    assert formatting['register_declarations_by_type']['b32']==160
    assert formatting['predicated_instructions']==1
    assert {'self_move','repeated_syntactic_load','declared_register_set','global_memory_mix'} <= {o['kind'] for o in formatting['observations']}
    assert formatting['instructions'][0]['line']==12 and formatting['instructions'][1]['line']==13
    parser_cases+=1
    memory=modules['memory.ptx']['functions'][0]
    assert memory['memory_operations_by_space']=={'global':1,'local':2,'shared':2}
    assert memory['categories']['atomics']==1
    assert len(modules['multiple.ptx']['functions'])==4
    assert modules['unknown.ptx']['functions'][0]['instructions'][0]['family']=='futureop'
    parser_cases+=1
    invalid={
        'missing_header':'.entry k(){ret;}',
        'bad_version':HEADER.replace('8.0','eight'),
        'bad_address_size':HEADER.replace('64','128')+'.entry k(){ret;}',
        'duplicate_directive':HEADER+'.version 8.0\n.entry k(){ret;}',
        'unclosed_comment':HEADER+'/* comment',
        'unclosed_string':HEADER+'.file 1 "unterminated',
        'unclosed_body':HEADER+'.entry k(){ret;',
        'extra_close':HEADER+'.entry k(){ret;}}',
        'mismatched_memory':HEADER+'.entry k(){ld.global.u32 %r, [%rd);}',
        'missing_semicolon':HEADER+'.entry k(){ret}',
        'merged_instructions':HEADER+'.entry k(){mov.u32 %r, 1\nret;}',
        'invalid_register_range':HEADER+'.entry k(){.reg .b32 %r<0>;ret;}',
        'unclosed_register_range':HEADER+'.entry k(){.reg .b32 %r<4;ret;}',
        'bad_predicate':HEADER+'.entry k(){@! ret;}',
        'missing_branch_target':HEADER+'.entry k(){bra nowhere;}',
        'duplicate_label':HEADER+'.entry k(){a: mov.u32 %r, 0; a: ret;}',
        'bad_operand_count':HEADER+'.entry k(){add.u32 %r, 1;ret;}',
        'missing_memory_bracket':HEADER+'.entry k(){ld.global.u32 %r, ptr;ret;}',
        'empty_operand':HEADER+'.entry k(){mov.u32 %r, ;ret;}',
        'dangling_qualifier':HEADER+'.visible',
        'scope_depth':HEADER+'.entry k(){'+'{'*130+'ret;'+'}'*131,
    }
    extra={
        'predicated_return':HEADER+'.entry k(){.reg .pred %p;@%p ret;ret;}',
        'vector_operand':HEADER+'.entry k(){.reg .b32 %r<4>; .reg .b64 %rd;ld.global.v2.u32 {%r0,%r1}, [%rd];ret;}',
        'initializer':HEADER+'.global .u32 table[2] = {1, 2}; .entry k(){ret;}',
        'indirect':HEADER+'.entry k(){.reg .b32 %r; targets: .branchtargets a, b; brx.idx %r, targets; a: ret; b: ret;}',
    }
    with tempfile.TemporaryDirectory() as temp:
        path=pathlib.Path(temp)/'case.ptx'
        for name,source in invalid.items():
            path.write_text(source);result=invoke(binary,path,check=False)
            assert result.returncode==1,(name,result.stdout,result.stderr)
            assert 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr,(name,result.stderr)
        for name,source in extra.items():
            path.write_text(source);result=json.loads(invoke(binary,path).stdout);f=result['functions'][0]
            if name=='predicated_return':assert f['blocks'][0]['successors']==[1]
            if name=='vector_operand':assert f['instruction_count']==2
            if name=='initializer':assert len(result['module_declarations'])==1
            if name=='indirect':assert f['unresolved_control_flow']==1
            parser_cases+=1
        if not smoke:
            # Metamorphic syntax checks: count/category expectations remain the manual annotation.
            source=(ROOT/'fixtures/matrix.ptx').read_text()
            variant=source.replace(';',';\n /* fake ret; {} ld.global.u32 %r,[x]; */\n')
            path.write_text(variant);altered=json.loads(invoke(binary,path).stdout)
            actual=[(i['opcode'],i['category']) for i in altered['functions'][0]['instructions']]
            assert actual==[tuple(x) for x in annotation['files']['matrix.ptx']['dot4']];parser_cases+=1
    for cmd in ['stats','cfg','analyze']:
        assert subprocess.check_output([binary,cmd,str(ROOT/'fixtures/vector_add.ptx')],text=True)
    return {'ptx_files_analyzed':len(rows),'kernels_analyzed':kernels,'total_instructions':instructions,'independently_labeled_instructions':labeled,'correctly_classified_instructions':matched,'classification_agreement_pct':100*matched/labeled,'supported_categories':modules['vector_add.ptx']['supported_categories'],'recognized_opcode_families':modules['vector_add.ptx']['recognized_opcode_families'],'parser_cases_passed':parser_cases,'malformed_inputs_rejected':len(invalid),'failures':0,'files':rows,'oracle':annotation['method'],'gpu_execution':'unavailable','ptxas_validation':'unavailable'}
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('binary');parser.add_argument('--smoke',action='store_true');args=parser.parse_args()
    print(json.dumps(validate(str(pathlib.Path(args.binary).resolve()),args.smoke),indent=2))
