"""Run in Ubuntu with the board attached; preserve warm process and raw outputs."""
import argparse, json, os, queue, shlex, subprocess, threading, time
from pathlib import Path
from evaluate_images import evaluate

def main():
    p=argparse.ArgumentParser();p.add_argument('manifest',type=Path);p.add_argument('--adb',default='adb')
    p.add_argument('--serial',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--remote-root',default='/userdata/plate_weekly')
    a=p.parse_args();items=json.loads(a.manifest.read_text(encoding='utf-8'))
    a.output.mkdir(parents=True,exist_ok=True)
    adb=[a.adb,'-s',a.serial];remote=a.remote_root.rstrip('/')
    def call(*args):return subprocess.run(adb+list(args),check=True,capture_output=True,text=True,timeout=120)
    for config in [{'name':'baseline','enabled':0,'score':0},*({'name':f'score_{s}','enabled':1,'score':s} for s in (.2,.3,.4,.5))]:
        local=a.output/config['name'];local.mkdir(exist_ok=True)
        session=f"{remote}/{time.time_ns()}"
        call('shell',f'mkdir -p {shlex.quote(session)}')
        for item in items:
            ident=item['id']
            if not ident or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in ident):raise ValueError('Unsafe ID')
            source=(a.manifest.parent/item['image']).resolve()
            if not source.is_file():raise FileNotFoundError(source)
            call('push',str(source),f'{session}/{ident}.png')
        cmd=f"cd /userdata/plate_persistent && PLATE_FILTER_ENABLED={config['enabled']} PLATE_MIN_SCORE={config['score']} sh run_stream.sh"
        start=time.perf_counter();proc=subprocess.Popen(adb+['shell',cmd],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        lines=queue.Queue();raw=[]
        def reader():
            for line in iter(proc.stdout.readline,b''):lines.put(line)
            lines.put(None)
        threading.Thread(target=reader,daemon=True).start()
        def wait(prefix,timeout=60):
            deadline=time.monotonic()+timeout
            while True:
                line=lines.get(timeout=max(.01,deadline-time.monotonic()))
                if line is None:raise RuntimeError('Backend exited')
                raw.append(line.decode('utf-8',errors='replace'))
                if line.startswith(b'@PLATE_ERROR'):raise RuntimeError(raw[-1])
                if line.startswith(prefix):return line
        try:
            wait(b'@PLATE_READY');ready_ms=(time.perf_counter()-start)*1000
            timings=[]
            # Repeat first image after startup to distinguish first and warm requests.
            for index,item in enumerate([items[0]]+items):
                ident=item['id'];dest=f'{session}/results/{ident}'
                wire=f"{index+1}\t{(session+'/'+ident+'.png').encode().hex()}\t{dest.encode().hex()}\n"
                tick=time.perf_counter();proc.stdin.write(wire.encode());proc.stdin.flush();wait(b'@PLATE_DONE')
                timings.append({'id':ident,'request_index':index+1,'host_round_trip_ms':(time.perf_counter()-tick)*1000})
            proc.stdin.write(b'QUIT\n');proc.stdin.flush();proc.wait(timeout=10)
            call('pull',f'{session}/results/.',str(local))
            report=evaluate(items,local);report['config']=config;report['ready_host_ms']=ready_ms;report['request_timings']=timings
            (local/'evaluation.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
        finally:
            if proc.poll() is None:proc.terminate()
            (local/'backend.log').write_text(''.join(raw),encoding='utf-8')
    print('Completed baseline and four score configurations. Review evaluation.json and timing.json.')
if __name__=='__main__':main()
