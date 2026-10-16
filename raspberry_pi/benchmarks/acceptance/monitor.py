"""Observe the monitored process only; plain-latency and ORT-profile runs use no observer."""
import argparse
import csv
import datetime
import json
import os
import pathlib
import subprocess
import time

def values(path):
    out={}
    for line in pathlib.Path(path).read_text().splitlines():
        if ':' not in line: continue
        key,rest=line.split(':',1); parts=rest.split()
        if parts and parts[0].isdigit(): out[key]=int(parts[0])
    return out

def stamp(): return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='microseconds')
def cpu_ticks():
    out={}
    for line in pathlib.Path('/proc/stat').read_text().splitlines():
        parts=line.split()
        if parts and (parts[0]=='cpu' or parts[0][3:].isdigit() and parts[0].startswith('cpu')):
            v=list(map(int,parts[1:9])); out[parts[0]]=(sum(v),v[3]+v[4])
    return out

def process_ticks(pid):
    parts=pathlib.Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()
    return int(parts[11])+int(parts[12])

def read_number(path,scale=1):
    try: return float(pathlib.Path(path).read_text())/scale
    except (OSError,ValueError): return ''

def main():
    p=argparse.ArgumentParser();p.add_argument('output',type=pathlib.Path);p.add_argument('command',nargs=argparse.REMAINDER)
    a=p.parse_args(); command=a.command[1:] if a.command[:1]==['--'] else a.command
    if not command: p.error('Missing target executable')
    a.output.mkdir(parents=True,exist_ok=True)
    mem_path=a.output/'memory_samples.csv'; cpu_path=a.output/'system_samples.csv'
    if mem_path.exists() or cpu_path.exists(): p.error('Existing samples; use new directory')
    before=values('/proc/meminfo'); cores=os.cpu_count(); hz=os.sysconf('SC_CLK_TCK')
    (a.output/'system_before_process_start.json').write_text(json.dumps({'timestamp_utc':stamp(),'meminfo_kib':before,'cpu_count':cores,'clock_ticks_per_second':hz},indent=2))
    mem_fields=['timestamp_utc','monotonic_ns','elapsed_ms','sample_index','pid','phase','VmRSS_kib','VmHWM_kib','VmSize_kib','VmPeak_kib','Pss_kib','RssAnon_kib','RssFile_kib','RssShmem_kib','MemTotal_kib','MemAvailable_kib','SwapUsed_kib']
    cpu_fields=['timestamp_utc','monotonic_ns','elapsed_ms','sample_index','pid','system_cpu_percent']+[f'cpu{i}_percent' for i in range(cores)]+['process_cpu_percent','process_cpu_machine_percent','threads','temperature_c']+[f'cpu{i}_frequency_khz' for i in range(cores)]+['throttled_hex','throttled_observation_timestamp_utc','MemAvailable_kib']
    counts=0; hwm=0; psmax=None; ps_available=False; errors=[]
    with mem_path.open('w',newline='') as mf,cpu_path.open('w',newline='') as cf,(a.output/'stdout.log').open('wb') as stdout:
        mw=csv.DictWriter(mf,mem_fields);cw=csv.DictWriter(cf,cpu_fields);mw.writeheader();cw.writeheader()
        initial={'timestamp_utc':stamp(),'monotonic_ns':time.monotonic_ns(),'sample_index':-1,'phase':'system_before_process_start','MemTotal_kib':before['MemTotal'],'MemAvailable_kib':before['MemAvailable'],'SwapUsed_kib':before['SwapTotal']-before['SwapFree']}
        mw.writerow(initial);mf.flush()
        started=time.monotonic_ns(); process=subprocess.Popen(command,stdout=stdout,stderr=subprocess.STDOUT)
        previous_cpu=cpu_ticks();previous_time=None;previous_proc=None;last_throttle=-10
        while True:
            pid,status,usage=os.wait4(process.pid,os.WNOHANG)
            if pid:
                process.returncode=os.waitstatus_to_exitcode(status);break
            now=time.monotonic_ns();timestamp=stamp()
            try:
                proc=values(f'/proc/{process.pid}/status');system=values('/proc/meminfo')
                if 'VmRSS' not in proc: time.sleep(.01);continue
                try: pss=values(f'/proc/{process.pid}/smaps_rollup').get('Pss','')
                except OSError: pss=''
                base=dict(timestamp_utc=timestamp,monotonic_ns=now,elapsed_ms=(now-started)/1e6,sample_index=counts,pid=process.pid)
                row={**base,'phase':'periodic',**{k+'_kib':proc.get(k,'') for k in ['VmRSS','VmHWM','VmSize','VmPeak','RssAnon','RssFile','RssShmem']},'Pss_kib':pss,
                     'MemTotal_kib':system['MemTotal'],'MemAvailable_kib':system['MemAvailable'],'SwapUsed_kib':system['SwapTotal']-system['SwapFree']}
                mw.writerow(row);mf.flush();hwm=max(hwm,proc.get('VmHWM',0))
                if pss!='': ps_available=True;psmax=max(psmax or 0,pss)
                current=cpu_ticks(); ticks=process_ticks(process.pid)
                cpu={**base,'threads':proc.get('Threads',''),'temperature_c':read_number('/sys/class/thermal/thermal_zone0/temp',1000),'MemAvailable_kib':system['MemAvailable']}
                for name,(total,idle) in current.items():
                    prev=previous_cpu.get(name);delta=total-prev[0] if prev else 0
                    cpu['system_cpu_percent' if name=='cpu' else name+'_percent']=100*(delta-(idle-prev[1]))/delta if delta>0 else ''
                if previous_time is not None:
                    cpu['process_cpu_percent']=100*(ticks-previous_proc)/hz/((now-previous_time)/1e9)
                    cpu['process_cpu_machine_percent']=cpu['process_cpu_percent']/cores
                for i in range(cores):cpu[f'cpu{i}_frequency_khz']=read_number(f'/sys/devices/system/cpu/cpu{i}/cpufreq/scaling_cur_freq')
                if (now-started)/1e9-last_throttle>=1:
                    try:
                        throttle=subprocess.run(['vcgencmd','get_throttled'],capture_output=True,text=True,timeout=2)
                        if throttle.returncode==0:
                            cpu['throttled_hex']=throttle.stdout.strip().split('=',1)[1];cpu['throttled_observation_timestamp_utc']=stamp()
                    except (OSError,subprocess.TimeoutExpired) as e:errors.append(type(e).__name__)
                    last_throttle=(now-started)/1e9
                cw.writerow(cpu);cf.flush();previous_cpu=current;previous_time=now;previous_proc=ticks;counts+=1
            except (FileNotFoundError,ProcessLookupError):pass
            time.sleep(.1)
    summary={'exit_code':process.returncode,'target_pid':process.pid,'interval_requested_ms':100,'sample_count':counts,
             'process_wall_ms':(time.monotonic_ns()-started)/1e6,'kernel_lifetime_peak_rss_kib':usage.ru_maxrss,
             'sampled_max_VmHWM_kib':hwm,'sampled_max_pss_kib':psmax,'pss_available':ps_available,
             'peak_method':'Linux wait4 rusage.ru_maxrss; KiB','scope':'Whole monitored process; init, preflight, warmup, 100 measured calls, output serialization and cleanup',
             'cpu_method':'/proc/stat deltas; process /proc/PID/stat utime+stime / elapsed / CLK_TCK; process percent may exceed 100; machine-normalized value also provided',
             'sampling_method':'/proc/PID/status and smaps_rollup; /proc/meminfo; sysfs temperature/frequency; vcgencmd get_throttled about once per second',
             'sampling_overhead':'Observer and target checkpoints run only in monitor mode; not equivalent to plain latency',
             'errors':errors}
    (a.output/'monitor_metadata.json').write_text(json.dumps(summary,indent=2))
    print(json.dumps(summary,indent=2))
    if process.returncode:raise SystemExit(process.returncode if process.returncode>0 else 1)

if __name__=='__main__':main()
