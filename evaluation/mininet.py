#!/usr/bin/python

mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.2.2-py2.7.egg'

import time
import sys

sys.path.insert(0, mininet_path)

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.node import CPULimitedHost
from mininet.util import pmonitor

from time import sleep

class TwoHostsTopology(Topo):
    "peer to peer topology"

    def build(self, ls=0, dy='10ms'):
        "Build two nodes with a direct link"
        #print('loss = %d, delay = %s' % (ls, dy))

        # Add two hosts.
        lhs = self.addHost('h1', cpu=.5)
        rhs = self.addHost('h2', cpu=.5)

        # Add direct link.
        self.addLink(lhs, rhs)
        #if ls != 0:
        #    linkopts = dict(bw=100, delay=dy, loss=ls) #, use_htb=True)
        #    self.addLink(lhs, rhs, **linkopts)
        #else:
        #    linkopts = dict(bw=100, delay=dy) #, use_htb=True)
        #    self.addLink(lhs, rhs, **linkopts)

topos = { 'p2p': ( lambda: TwoHostsTopology() ) }

def set_interface_delay(host, action, interface, delay): #action: add/change/delete
    # command = 'tc qdisc {} dev {} root handle 1: netem delay {}ms'.format(action, interface, delay)
    command = ''
    if (delay > 0):
        variance = delay / 10
        distribution = 'normal'
        command = 'tc qdisc {} dev {} root netem delay {}ms {}ms distribution {}'.format(action, interface, delay, variance, distribution)
    else:
        command = 'tc qdisc {} dev {} root netem delay {}ms'.format(action, interface, delay)
    print('running "{}"'.format(command))
    host.cmd(command)

def set_interface_packet_lost(host, action, interface, packet_loss): #action: add/change/delete
    # command = 'tc qdisc {} dev {} parent 1:1 netem loss {}%'.format(action, interface, packet_loss)
    command = 'tc qdisc {} dev {} root netem loss {}%'.format(action, interface, packet_loss)
    print('running: "{}"').format(command)
    host.cmd(command)

def set_loss(host, packet_loss):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if packet_loss > 0 else 'del'
    print('setting loss on "{}" to "{}%" using "{}"'.format(iface, packet_loss, op))
    set_interface_packet_lost(host, op, iface, packet_loss)

def set_delay(host, delay):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if delay > 0 else 'del'
    print('setting delay on "{}" to "{}ms" using "{}"'.format(iface, delay, op))
    set_interface_delay(host, op, iface, delay)

def get_info(host):
    iface = '{}-eth0'.format(host.name)
    command = 'tc qdisc show  dev {}'.format(iface)
    print('running: "{}"'.format(command))
    host.cmdPrint(command)

def set_min_rto(host, timeout):
    command = '10.0.0.0/8 dev {}-eth0 proto kernel scope link src {} rto_min {}ms'.format(host.name, host.IP(), timeout)
    print('running: "{}"'.format(command))
    host.cmdPrint(command)

def main():
    delay = 0
    for loss in range(0, 11):
        for run in range(10):
            print(">> Loss = {}%, run {}".format(loss, run))
            net = Mininet(topo = TwoHostsTopology(ls=loss), link=TCLink, host=CPULimitedHost)
            net.start()

            print("configuring hosts")
            h1 = net.get('h1')
            set_delay(h1, delay)
            set_loss(h1, loss)
            set_min_rto(h1, 40)
            # get_info(h1) # does not print anything ...
            h2 = net.get('h2')
            set_delay(h2, delay)
            set_loss(h2, loss)
            set_min_rto(h2, 40)
            # get_info(h2)

            files = {}
            file_path = '/home/localadmin/logs/reliable-udp' # was /tmp
            servererr = open('{}/ppserver-{}-{}.err'.format(file_path, loss, run), 'w')
            serverout = open('{}/ppserver-{}-{}.out'.format(file_path, loss, run), 'w')
            clienterr = open('{}/ppclient-{}-{}.err'.format(file_path, loss, run), 'w')
            clientout = open('{}/ppclient-{}-{}.out'.format(file_path, loss, run), 'w')

            caf_opts = '--scheduler.max-threads=1'

            print("starting server")
            servercommand = '../build/bin/pingpong_tcp -s --host=\\"{}\\" {}'.format(h1.IP(), caf_opts)
            print(servercommand)
            p1 = h1.popen(servercommand, shell=True, universal_newlines=True, stdout=serverout, stderr=servererr) # not sure if this shouldn't be h1.cmd

            print("starting client")
            clientcommand = '../build/bin/pingpong_tcp -m 2000 --host=\\"{}\\" {}'.format(h1.IP(), caf_opts)
            print(clientcommand)
            p2 = h2.popen(clientcommand, shell=True, universal_newlines=True, stdout=clientout, stderr=clienterr) # not sure if this shouldn't be h1.cmd

            # CLI(net)
            p2.wait()
            p1.kill()
            servererr.close()
            serverout.close()
            clienterr.close()
            clientout.close()
            h1.cmd('kill %pingpong')
            h1.cmd('killall pingpong')
            h2.cmd('kill %pingpong')
            h2.cmd('killall pingpong')
            sleep(1)
            net.stop()
            sleep(1)

if __name__ == '__main__':
    main()

