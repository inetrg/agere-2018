#!/usr/bin/python

mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.2.2-py2.7.egg'

import time
import sys
import argparse

sys.path.insert(0, mininet_path)

from mininet.topo import Topo
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.cli import CLI
from mininet.node import CPULimitedHost
from mininet.util import pmonitor
from time import time

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
    print('{} -> "{}"'.format(host.name, command))
    host.cmd(command)

def set_interface_packet_lost(host, action, interface, packet_loss): #action: add/change/delete
    # command = 'tc qdisc {} dev {} parent 1:1 netem loss {}%'.format(action, interface, packet_loss)
    command = 'tc qdisc {} dev {} root netem loss {}%'.format(action, interface, packet_loss)
    print('{} -> "{}"').format(host.name, command)
    host.cmd(command)

def set_loss(host, packet_loss):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if packet_loss > 0 else 'del'
    set_interface_packet_lost(host, op, iface, packet_loss)

def set_delay(host, delay):
    iface = '{}-eth0'.format(host.name)
    op = 'add' if delay > 0 else 'del'
    set_interface_delay(host, op, iface, delay)

def get_info(host):
    iface = '{}-eth0'.format(host.name)
    command = 'tc qdisc show  dev {}'.format(iface)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def set_min_rto(host, timeout):
    command = '10.0.0.0/8 dev {}-eth0 proto kernel scope link src {} rto_min {}ms'.format(host.name, host.IP(), timeout)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def main():
    parser = argparse.ArgumentParser(description='CAF newbs on Mininet.')
    parser.add_argument('-l', '--loss',    help='', type=int, default=0)
    parser.add_argument('-d', '--delay',   help='', type=int, default=0)
    parser.add_argument('-r', '--runs',    help='', type=int, default=1)
    parser.add_argument('-T', '--threads', help='', type=int, default=1)
    parser.add_argument('-o', '--min-rto', help='', type=int, default=40)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-t', '--tcp', help='', action='store_true')
    group.add_argument('-u', '--udp', help='', action='store_true')
    args = vars(parser.parse_args())
    run = 0
    max_runs = args['runs']
    while run < max_runs:
        is_tcp = args['tcp']
        loss = args['loss']
        delay = args['delay']
        min_rto = args['min-rto']
        proto = 'udp'
        if is_tcp:
            proto = 'tcp'
        print(">> Run {} with {}% loss and {}ms delay".format(run, loss, delay))
        net = Mininet(topo = TwoHostsTopology(ls=loss), link=TCLink, host=CPULimitedHost)
        net.start()

        print("Configuring hosts ...")
        # host 1
        h1 = net.get('h1')
        set_delay(h1, delay)
        set_loss(h1, loss)
        if is_tcp:
            set_min_rto(h1, min_rto)
        # host 2
        h2 = net.get('h2')
        set_delay(h2, delay)
        set_loss(h2, loss)
        if is_tcp:
            set_min_rto(h2, min_rto)

        files = {}
        # servererr = './server-{}-{}-{}.err'.format(loss, delay, run)
        # serverout = './server-{}-{}-{}.out'.format(loss, delay, run)
        # clienterr = './client-{}-{}-{}.err'.format(loss, delay, run)
        # clientout = './client-{}-{}-{}.out'.format(loss, delay, run)
        servererr = open('./{}-server-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        serverout = open('./{}-server-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')
        clienterr = open('./{}-client-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        clientout = open('./{}-client-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')

        caf_opts = '--scheduler.max-threads={}'.format(args['threads'])
        prog = 'pingpong'
        if args['tcp']:
            prog = 'pingpong_tcp'

        print("Starting server")
        servercommand = '../build/bin/{} -s --host=\\"{}\\" {} '.format(prog, h1.IP(), caf_opts)
        print('> {}'.format(servercommand))
        # h1.cmdPrint(servercommand)
        # p1 = int(h1.cmd('echo $!'))
        p1 = h1.popen(servercommand, shell=True, universal_newlines=True, stdout=serverout, stderr=servererr)

        print("Starting client")
        clientcommand = '../build/bin/{} -m 2000 --host=\\"{}\\" {}'.format(prog, h1.IP(), caf_opts)
        print('> {}'.format(clientcommand))
        # h2.cmdPrint(clientcommand)
        # p2 = int(h2.cmd('echo $!'))
        p2 = h2.popen(clientcommand, shell=True, universal_newlines=True, stdout=clientout, stderr=clienterr)
        # end_time = time() + 150
        # for line in h2.monitor():
            # print '%s: %s'.format(h2.name, line)
            # if time() >= end_time:
                # for p in processes.values():
                    # p.send_signal(SIGINT)

        # h2.cmd('wait', p2)
        # h1.cmd('kill', p1)
        # CLI(net)
        p2.wait()
        p1.kill()
        servererr.close()
        serverout.close()
        clienterr.close()
        clientout.close()
        h1.cmd('kill %{}'.format(prog))
        h1.cmd('killall {}'.format(prog))
        h2.cmd('kill %{}'.format(prog))
        h2.cmd('killall {}'.format(prog))
        # sleep(1)
        net.stop()
        # sleep(1)
        run += 1

if __name__ == '__main__':
    main()

