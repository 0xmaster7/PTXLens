#!/usr/bin/env python3
import argparse, datetime, hashlib, json, pathlib, platform, subprocess, sys, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tests'))
from test import validate, HEADER

def output(*args):return subprocess.check_output(args,text=True,cwd=ROOT).strip()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('binary');parser.add_argument('--smoke',action='store_true');a=parser.parse_args();binary=str(pathlib.Path(a.binary).resolve())
    correctness=validate(binary,a.smoke);timing=[]
    paths=sorted((ROOT/'fixtures').glob('*.ptx'))
    with tempfile.TemporaryDirectory() as temp:
        for name,repetitions in [('synthetic_small',20 if a.smoke else 100),('synthetic_large',100 if a.smoke else 5000)]:
            path=pathlib.Path(temp)/(name+'.ptx')
            path.write_text(HEADER+'.visible .entry batch(){ .reg .b32 %r<2>;\n'+('add.u32 %r0, %r0, 1; xor.b32 %r1, %r0, 7;\n'*repetitions)+'ret; }\n');paths.append(path)
        for path in paths:
            row=json.loads(output(binary,'benchmark',str(path)));row['fixture']=path.name;row['synthetic_throughput_only']=path.name.startswith('synthetic_');row['sha256']=hashlib.sha256(path.read_bytes()).hexdigest();timing.append(row)
        if a.smoke:
            assert len(timing)==10 and all(r['instructions']>0 and r['latency_us_median']>0 for r in timing)
            print('benchmark smoke passed');return
    data={'source_commit':output('git','rev-parse','HEAD'),'date_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'os':platform.platform(),
          'cpu':next(l.split(':',1)[1].strip() for l in pathlib.Path('/proc/cpuinfo').read_text().splitlines() if l.startswith('model name')),
          'compiler':output('g++','--version').splitlines()[0],'build':'CMake Release; GCC; -O3 -DNDEBUG',
          'method':'In-memory source -> tokenizer -> parser -> static analysis; one warmup, 15 steady_clock samples of 10 analyses; microseconds per analysis. Excludes disk reads and JSON serialization. Synthetic small=100 and large=5000 two-instruction repetitions plus ret.',
          'correctness':correctness,'timing_files_analyzed':len(timing),'timing_instructions_total':sum(r['instructions'] for r in timing),'timings':timing,
          'classification_denominator':'103 manually labeled instructions in committed fixtures; synthetic throughput inputs are excluded from classification agreement.',
          'limitations':['No CUDA compiler, ptxas, NVIDIA GPU, or GPU execution was available.','Fixtures follow official PTX ISA syntax; no claim of assembler validation, register allocation, occupancy, divergence, spilling, or GPU performance.','The intentionally unknown opcode fixture tests retention, not ISA validity.','Agreement is only against the committed hand-annotated corpus, not the complete PTX ISA.','Source commit identifies measured code; results are committed afterward.']}
    (ROOT/'benchmarks/results.json').write_text(json.dumps(data,indent=2)+'\n')
    c=correctness;lines=['# Measured PTXLens results','',f"Source commit: `{data['source_commit']}`; date: {data['date_utc']}",f"Environment: {data['os']}; {data['cpu']}; {data['compiler']}",'',data['method'],'',f"Annotated corpus: {c['ptx_files_analyzed']} files, {c['kernels_analyzed']} kernels, {c['total_instructions']} instructions; {c['correctly_classified_instructions']}/{c['independently_labeled_instructions']} classification agreement ({c['classification_agreement_pct']:.1f}%). {c['supported_categories']} categories; {c['recognized_opcode_families']} recognized opcode families. Passed {c['parser_cases_passed']} parser/analysis cases and rejected {c['malformed_inputs_rejected']} malformed inputs. Failures: {c['failures']}.",f"Timing corpus: {len(timing)} files and {data['timing_instructions_total']} instructions including two synthetic throughput workloads. Synthetic instructions are excluded from the classification denominator.",'','| Fixture | Instructions | Median µs | Min–max µs | Instructions/s |','|---|---:|---:|---:|---:|']
    for r in timing:lines.append(f"| {r['fixture']} | {r['instructions']} | {r['latency_us_median']:.2f} | {r['latency_us_min']:.2f}–{r['latency_us_max']:.2f} | {r['instructions_per_second']:.0f} |")
    lines+=['','Reproduce with `python3 benchmarks/benchmark.py build/ptxlens`. Raw samples and SHA-256 fixture digests are in results.json.','','## Limitations','']+['- '+s for s in data['limitations']]
    (ROOT/'benchmarks/results.md').write_text('\n'.join(lines)+'\n');print(json.dumps({k:v for k,v in c.items() if k!='files'}))
if __name__=='__main__':main()
