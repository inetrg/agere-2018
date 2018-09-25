#!/usr/bin/python

mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.2.2-py2.7.egg'
#mininet_path = '/usr/local/lib/python2.7/dist-packages/mininet-2.3.0d4-py2.7.egg'

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

    def build(self):
        "Build two nodes with a direct link"

        # Add two hosts.
        lhs = self.addHost('h1', cpu=.5)
        rhs = self.addHost('h2', cpu=.5)

        # Add direct link.
        self.addLink(lhs, rhs)

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
    " Change min_rto for a route on Linux, might require the 'ms' ...
    command = 'ip route change 10.0.0.0/8 dev {}-eth0 proto kernel scope link src {} rto_min {}'.format(host.name, host.IP(), timeout)
    print('{} -> "{}"'.format(host.name, command))
    host.cmdPrint(command)

def main():
    parser = argparse.ArgumentParser(description='CAF newbs on Mininet.')
    parser.add_argument('-l', '--loss',    help='', type=int, default=0)
    parser.add_argument('-d', '--delay',   help='', type=int, default=0)
    parser.add_argument('-r', '--runs',    help='', type=int, default=1)
    parser.add_argument('-T', '--threads', help='', type=int, default=1)
    parser.add_argument('-R', '--rto',     help='', type=int, default=40)
    parser.add_argument('-o', '--ordered', help='', action='store_true')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-t', '--tcp',  help='', action='store_true')
    group.add_argument('-u', '--udp',  help='', action='store_true')
    group.add_argument('-q', '--quic', help='', action='store_true')
    args = vars(parser.parse_args())
    run = 0
    max_runs = args['runs']
    while run < max_runs:
        is_tcp = args['tcp']
        loss = args['loss']
        delay = args['delay']
        min_rto = args['rto']
        proto = ''
        if args['tcp']:
            proto = 'tcp'
        elif args['udp']:
            if args['ordered']:
                proto = 'udp-ordered'
            else:
                proto = 'udp'
        elif args['quic']:
            proto = 'quic'
        print(">> Run {} with {}% loss and {}ms delay".format(run, loss, delay))
        net = Mininet(topo = TwoHostsTopology(), link=TCLink, host=CPULimitedHost)
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

        servererr = open('./pingpong/{}-server-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        serverout = open('./pingpong/{}-server-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')
        clienterr = open('./pingpong/{}-client-{}-{}-{}.err'.format(proto, loss, delay, run), 'w')
        clientout = open('./pingpong/{}-client-{}-{}-{}.out'.format(proto, loss, delay, run), 'w')

        caf_opts = '--scheduler.max-threads={}'.format(args['threads'])
        prog = ''
        if args['tcp']:
            prog = 'pingpong_tcp'
	elif args['udp']:
            prog = 'pingpong_udp'
            if args['ordered']:
                caf_opts = '{} --ordered'.format(caf_opts)
        elif args['quic']:
            prog = 'pingpong_quic'

        print("Starting server")
        servercommand = '../build/bin/{} -s {} '.format(prog, caf_opts)
        print('> {}'.format(servercommand))
        # h1.cmdPrint(servercommand)
        # p1 = int(h1.cmd('echo $!'))
        p1 = h1.popen(servercommand, shell=True, universal_newlines=True, stdout=serverout, stderr=servererr)

        # Give everything a bit of time to start.
        sleep(1)

        print("Starting client")
        clientcommand = '../build/bin/{} -m 2000 --host=\\"{}\\" {}'.format(prog, h1.IP(), caf_opts)
        print('> {}'.format(clientcommand))
        # h2.cmdPrint(clientcommand)
        # p2 = int(h2.cmd('echo $!'))
        p2 = h2.popen(clientcommand, shell=True, universal_newlines=True, stdout=clientout, stderr=clienterr)

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

