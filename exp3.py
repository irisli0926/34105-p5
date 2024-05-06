#!/usr/bin/python3

import sys
import os
import time
import matplotlib.pyplot as plt

# associtivity range
assoc_range = [4]
# block size
bsize_range = [b for b in range(1, 14)]
# capacity range
cap_range = [16]
# number of cores (1, 2, 4)
cores = [1]
# coherence protocol: (none, vi, or msi)
protocol='none'

expname='exp3'
figname='graph3.png'


def get_stats(logfile, key):
    for line in open(logfile):
        if line[2:].startswith(key):
            line = line.split()
            return float(line[1])
    return 0

def run_exp(logfile, core, cap, bsize, assoc):
    trace = 'trace.%dt.long.txt' % core
    cmd="./p5 -t %s -p %s -n %d -cache %d %d %d >> %s" % (
            trace, protocol, core, cap, bsize, assoc, logfile)
    print(cmd)
    os.system(cmd)

def graph():
    timestr = time.strftime("%m.%d-%H_%M_%S")
    folder = "results/"+expname+"/"+timestr+"/"
    os.system("mkdir -p "+folder)

    traffic_wb = {a:[] for a in assoc_range}
    traffic_wt = {a:[] for a in assoc_range}

    for a in assoc_range:
        for b in bsize_range:
            for c in cap_range:
                for d in cores:
                    logfile = folder+"%s-%02d-%02d-%02d-%02d.out" % (
                            protocol, d, c, b, a)
                    run_exp(logfile, d, c, b, a)
                    traffic_wb[a].append(get_stats(logfile, 'B_written_cache_to_bus_wb'))
                    traffic_wt[a].append(get_stats(logfile, 'B_written_cache_to_bus_wt'))

    plots = []
    for a in traffic_wb:
        p,=plt.plot([2**i for i in bsize_range], traffic_wb[a])
        plots.append(p)
    for a in traffic_wt:
        p,=plt.plot([2**i for i in bsize_range], traffic_wt[a])
        plots.append(p)
    plt.legend(plots, ['WB traffic', 'WT traffic'])
    plt.xscale('log', base=2)
    plt.yscale('log', base=2)
    plt.title('Graph #3: Traffic in Bytes vs Block Size')
    plt.xlabel('Block Size (Bytes)')
    plt.ylabel('Traffic (Bytes)')
    plt.savefig(figname)
    plt.show()

graph()
