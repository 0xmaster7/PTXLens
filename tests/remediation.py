#!/usr/bin/env python3
"""P-1 regressions. Expected identities/counts are literal source-level labels."""
import json,pathlib,subprocess,sys,tempfile
HEADER='.version 8.0\n.target sm_70\n.address_size 64\n'

def validate(binary):
    cases={
        'scalar_prefix':('.global .u32 r0; .entry k(){.reg .u64 r;mov.u64 r,r0;ret;}',1,['r']),
        'reverse_prefix':('.global .u32 r; .entry k(){.reg .u32 r<1>;.reg .u64 addr;mov.u64 addr,r;ret;}',1,['addr']),
        'scalar_digits':('.global .u32 r10; .entry k(){.reg .u64 r1;mov.u64 r1,r10;mov.u64 r1,r1;ret;}',3,['r1']),
        'range_boundaries':('.global .u32 r3,r00; .entry k(){.reg .u32 r<3>;.reg .u64 addr;mov.u32 r0,r2;mov.u64 addr,r3;mov.u64 addr,r00;ret;}',4,['addr','r0','r2']),
        'overlapping_prefixes':('.global .u32 r12; .entry k(){.reg .u32 r<2>,r1<2>;mov.u32 r0,r10;mov.u32 r11,r1;ret;}',4,['r0','r1','r10','r11']),
        'percent_range':('.entry k(){.reg .u32 %r<2>;mov.u32 %r0,%r1;mov.u32 %r2,%r00;ret;}',2,['%r0','%r1']),
        'percent_special':('.entry k(){.reg .u32 %r;mov.u32 %r,%tid.x;ret;}',2,['%r','%tid.x']),
        'lexical_percent':('.entry k(){mov.u32 %unlisted,%laneid;ret;}',2,['%laneid','%unlisted']),
        'all_storage':('.global .u32 g; .entry k(.param .u64 arg){.shared .u32 s;.local .u32 l;.reg .u64 a;ld.param.u64 a,[arg];mov.u64 a,g;mov.u64 a,s;mov.u64 a,l;ret;}',4,['a']),
        'single_range_element':('.entry k(){.reg .u32 r<1>;mov.u32 r0,r0;ret;}',2,['r0']),
        'percent_global':('.entry k(){.reg .u64 %r;mov.u64 %r,%g;ret;} .global .u32 %g;',1,['%r']),
        'predicate_source_dest':('.global .u32 p0; .entry k(){.reg .pred p;.reg .u64 r;@!p mov.u64 r,p0;ret;}',2,['p','r']),
    }
    with tempfile.TemporaryDirectory() as temp:
        path=pathlib.Path(temp)/'case.ptx'
        for name,(body,count,unique) in cases.items():
            path.write_text(HEADER+body)
            result=subprocess.run([binary,'analyze',str(path),'--json'],text=True,capture_output=True,timeout=20)
            assert result.returncode==0 and 'AddressSanitizer' not in result.stderr and 'runtime error:' not in result.stderr,(name,result.stderr)
            data=json.loads(result.stdout);f=data['functions'][0]
            assert f['register_reference_occurrences']==count,(name,f['register_reference_occurrences'],count)
            assert f['unique_register_references']==unique,(name,f['unique_register_references'],unique)
            if name=='reverse_prefix':assert f['declarations'][0]['kind']=='register_range'
            if name=='scalar_prefix':assert f['declarations'][0]['kind']=='scalar'
    return {'register_identity_cases':len(cases),'failures':0}

if __name__=='__main__':print(json.dumps(validate(str(pathlib.Path(sys.argv[1]).resolve())),indent=2))
