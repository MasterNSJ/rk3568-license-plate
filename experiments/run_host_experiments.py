"""Compile/run the actual C++ logic. Synthetic sequences test logic, not vision accuracy."""
import argparse, csv, json, platform, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def run(args, **kw):
    return subprocess.run([str(a) for a in args], check=True, text=True,
                          encoding='utf-8', capture_output=True, **kw)

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--output', type=Path, default=ROOT/'experiments/results/host')
    args=parser.parse_args(); out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    suffix='.exe' if platform.system()=='Windows' else ''
    binaries={}
    for name,source in [('unit','tests/quality_consensus_test.cpp'),('replay','experiments/replay_consensus.cpp')]:
        exe=out/(name+suffix)
        run([args.cxx,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',ROOT/source,'-o',exe])
        binaries[name]=exe
    unit=run([binaries['unit']]).stdout
    (out/'unit_tests.txt').write_text(unit,encoding='utf-8')
    # Explicit hand-designed inputs; no detector, OCR model or camera is executed here.
    sequences={
        'stable': ['A']*10,
        'isolated_errors': ['B','A','A','A','A','B','A','A','A','A'],
        'alternating': ['A','B']*5,
        'systematic_error': ['B']*10,
        'invalid_ocr': ['-']*10,
        'early_error': ['B','A','A','A','A','A','A','A','A','A'],
    }
    rows=[];details=[]
    for scenario,seq in sequences.items():
        payload=''.join(f'{i*1000} 10 10 100 30 {int(raw!="-")} {raw}\n' for i,raw in enumerate(seq))
        for window in (1,3,5):
            output=run([binaries['replay'],window],input=payload).stdout
            (out/f'{scenario}_w{window}.tsv').write_text(output,encoding='utf-8')
            records=[line.split('\t') for line in output.splitlines()]
            visible=[r[2] for r in records if r[2]!='-']
            confirmed=[int(r[0]) for r in records if r[2]!='-']
            row={'scenario':scenario,'window':window,'observations':len(seq),
                 'confirmed_observations':len(visible),'wrong_confirmations':sum(x!='A' for x in visible),
                 'confirmed_text_changes':sum(a!=b for a,b in zip(visible,visible[1:])),
                 'first_confirmation_ms':confirmed[0] if confirmed else None}
            rows.append(row); details.append({'config':row,'input_kind':'synthetic_ocr_sequence','input':seq})
    with (out/'consensus_metrics.csv').open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    report={'kind':'host_cpp_synthetic_logic_experiment','platform':platform.platform(),
            'unit_tests':json.loads(unit.strip().splitlines()[-1]),'scenarios':len(sequences),
            'configurations':3,'trials':rows,
            'limitations':['No RK3568/NPU inference','No real image accuracy measurements',
                           'Synthetic systematic errors remain wrong after voting',
                           'Confirmation times use synthetic observation timestamps, not measured processing time']}
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    print(json.dumps(report,indent=2,ensure_ascii=False))

if __name__=='__main__':main()
